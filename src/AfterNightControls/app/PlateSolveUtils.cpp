// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/PlateSolveUtils.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/IO/FitsHeaderCards.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui
{

PlateSolveRunGuard::PlateSolveRunGuard(const std::chrono::milliseconds timeout)
    : m_deadline(std::chrono::steady_clock::now() + timeout)
{
}

bool PlateSolveRunGuard::shouldStop(const bool externally_cancelled) const noexcept
{
    if (externally_cancelled)
    {
        return true;
    }
    if (std::chrono::steady_clock::now() < m_deadline)
    {
        return false;
    }

    m_timedOut.store(true, std::memory_order_relaxed);
    return true;
}

bool PlateSolveRunGuard::timedOut() const noexcept
{
    return m_timedOut.load(std::memory_order_relaxed) || shouldStop(false);
}

std::chrono::steady_clock::time_point PlateSolveRunGuard::deadline() const noexcept
{
    return m_deadline;
}

QString createUniquePlateSolveInputPath(const QString& work_directory_root)
{
    const QString root = work_directory_root.trimmed().isEmpty() ? QDir::tempPath() : work_directory_root;
    const QString staging_directory = QDir(root).filePath(QStringLiteral(".afternight/plate_solve/temp"));
    if (!QDir().mkpath(staging_directory))
    {
        return {};
    }

    constexpr int kMaxPathAttempts = 8;
    for (int attempt = 0; attempt < kMaxPathAttempts; ++attempt)
    {
        const QString unique_name =
            QStringLiteral("solve_input_%1.fits").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString path = QDir(staging_directory).filePath(unique_name);
        if (!QFileInfo::exists(path))
        {
            return path;
        }
    }

    return {};
}

std::vector<int> internalGaiaDetectionChannels(const int channel_count)
{
    if (channel_count <= 0)
    {
        return {};
    }

    std::vector<int> channels;
    channels.reserve(static_cast<size_t>(channel_count));
    for (int channel = 0; channel < channel_count; ++channel)
    {
        channels.push_back(channel);
    }
    return channels;
}

an::Result<an::OutputIntent> plateSolveInputIntent()
{
    return an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::Float32});
}

namespace
{

/// A failure the plate solve reports in its own words.
[[nodiscard]] an::Status refusal(const an::ErrorCode code, const std::string& message)
{
    return an::Status::failure(code, an::StatusDetail{.message = message});
}

/// Whether the cards name the celestial reference frame.
[[nodiscard]] bool statesReferenceFrame(const std::vector<an::CodecCard>& cards)
{
    for (const an::CodecCard& card : cards)
    {
        if (card.origin() == an::CodecNamespace::Fits && (card.keyword() == "RADESYS" || card.keyword() == "RADECSYS"))
        {
            return true;
        }
    }
    return false;
}

/// A header card in the FITS vocabulary.
[[nodiscard]] an::Result<an::CodecCard> fitsCard(std::string keyword, std::string value, std::string comment)
{
    return an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::move(comment));
}

/// A FITS string value: quoted, with an embedded quote doubled.
[[nodiscard]] std::string fitsString(const QString& text)
{
    std::string quoted = "'";
    for (const char character : text.trimmed().toStdString())
    {
        quoted += character;
        if (character == '\'')
        {
            quoted += '\'';
        }
    }
    quoted += '\'';
    return quoted;
}

/// A number as a FITS value, to six decimals.
[[nodiscard]] std::string fitsNumber(const double value)
{
    return QString::number(value, 'f', 6).toStdString();
}

/// The same cards without any of the given keywords.
[[nodiscard]] an::Result<an::CodecMetadata> withoutKeywords(const an::CodecMetadata& codec,
                                                            const std::vector<std::string_view>& keywords)
{
    std::vector<an::CodecCard> kept;
    kept.reserve(codec.cards().size());
    for (const an::CodecCard& card : codec.cards())
    {
        const bool removed =
            card.origin() == an::CodecNamespace::Fits &&
            std::find(keywords.begin(), keywords.end(), std::string_view(card.keyword())) != keywords.end();
        if (!removed)
        {
            kept.push_back(card);
        }
    }
    return an::CodecMetadata::create(codec.sourceEncoding(), std::move(kept));
}

/// The metadata with new cards, a new summary and one more lineage entry; everything else as it was.
[[nodiscard]] an::Result<an::ImageMetadata> rebuildMetadata(const an::ImageMetadata& metadata, an::CodecMetadata codec,
                                                            an::AstrometricSolution astrometry, std::string operation,
                                                            std::string detail)
{
    const an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(std::move(operation), std::move(detail));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    an::Result<an::Provenance> provenance = metadata.provenance().appended(*entry);
    if (!provenance.hasValue())
    {
        return provenance.status();
    }
    return an::ImageMetadata::create(metadata.acquisition(), metadata.semantics(), std::move(astrometry),
                                     std::move(*provenance), std::move(codec), metadata.outputIntent());
}

} // namespace

an::Result<std::vector<an::CodecCard>> solvedHeaderCards(const std::string_view header)
{
    an::Result<std::vector<an::CodecCard>> cards = an::parseFitsHeaderCards(header);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    if (!statesReferenceFrame(*cards))
    {
        an::Result<an::CodecCard> frame = fitsCard("RADESYS", "'ICRS'", "stated by the plate solve");
        if (!frame.hasValue())
        {
            return frame.status();
        }
        cards->push_back(std::move(*frame));
    }
    return cards;
}

an::Result<an::WcsSolution> readSolvedWcsHeader(const std::string_view header, const std::uint32_t width,
                                                const std::uint32_t height)
{
    const an::Result<std::vector<an::CodecCard>> cards = solvedHeaderCards(header);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    return an::parseWcsCards(*cards, width, height);
}

an::Result<an::WcsSolution> readSolvedWcsHeaderFile(const QString& path, const std::uint32_t width,
                                                    const std::uint32_t height)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       QStringLiteral("the solved header '%1' cannot be read").arg(path).toStdString());
    }
    const QByteArray bytes = file.read(static_cast<qint64>(an::kDefaultFitsHeaderReadBytes));
    return readSolvedWcsHeader(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), width,
                               height);
}

an::Result<an::ImageMetadata> metadataWithPlateSolution(const an::ImageMetadata& metadata,
                                                        const an::WcsSolution& solution,
                                                        const PlateSolutionFacts& facts)
{
    std::vector<std::string_view> replaced{kPlateSolveSolverKeyword};
    std::vector<an::CodecCard> written;
    const auto add = [&written, &replaced](std::string_view keyword, std::string value) -> an::Status {
        an::Result<an::CodecCard> card = fitsCard(std::string(keyword), std::move(value), std::string());
        if (!card.hasValue())
        {
            return card.status();
        }
        replaced.push_back(keyword);
        written.push_back(std::move(*card));
        return an::Status::success();
    };
    if (facts.focalLengthMillimetres.has_value() && *facts.focalLengthMillimetres > 0.0)
    {
        if (an::Status added = add("FOCALLEN", fitsNumber(*facts.focalLengthMillimetres)); added.failed())
        {
            return added;
        }
    }
    if (facts.pixelSizeMicrometres.has_value() && *facts.pixelSizeMicrometres > 0.0)
    {
        for (const std::string_view keyword : {std::string_view("XPIXSZ"), std::string_view("YPIXSZ")})
        {
            if (an::Status added = add(keyword, fitsNumber(*facts.pixelSizeMicrometres)); added.failed())
            {
                return added;
            }
        }
    }
    if (!facts.solver.trimmed().isEmpty())
    {
        if (an::Status added = add(kPlateSolveSolverKeyword, fitsString(facts.solver)); added.failed())
        {
            return added;
        }
    }

    an::Result<an::CodecMetadata> withSolution = an::withWcsCards(metadata.codec(), solution);
    if (!withSolution.hasValue())
    {
        return withSolution.status();
    }
    an::Result<an::CodecMetadata> codec = withoutKeywords(*withSolution, replaced);
    for (const an::CodecCard& card : written)
    {
        if (!codec.hasValue())
        {
            break;
        }
        codec = codec->appended(card);
    }
    if (!codec.hasValue())
    {
        return codec.status();
    }
    an::Result<an::AstrometricSolution> summary = solution.toAstrometricSolution();
    if (!summary.hasValue())
    {
        return summary.status();
    }
    const std::string solver = facts.solver.trimmed().toStdString();
    return rebuildMetadata(metadata, std::move(*codec), std::move(*summary), "plateSolve",
                           solver.empty() ? std::string("solution written") : "solved by " + solver);
}

an::Result<an::ImageMetadata> metadataWithoutPlateSolution(const an::ImageMetadata& metadata)
{
    an::Result<an::CodecMetadata> withoutSolution = an::withoutWcsCards(metadata.codec());
    if (!withoutSolution.hasValue())
    {
        return withoutSolution.status();
    }
    an::Result<an::CodecMetadata> codec = withoutKeywords(*withoutSolution, {kPlateSolveSolverKeyword});
    if (!codec.hasValue())
    {
        return codec.status();
    }
    return rebuildMetadata(metadata, std::move(*codec), an::AstrometricSolution{}, "clearAstrometry",
                           "the world coordinates and the solver withdrawn");
}

an::Result<an::consumer::ImageAsset> copyImageWithMetadata(const an::consumer::EngineSession& session,
                                                           const an::consumer::AssetWriteLease& image,
                                                           an::ImageMetadata metadata, std::stop_token stop)
{
    // The admission observes the stop: a stopped edit is refused before anything is copied.
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    std::optional<an::ValidityMask> mask;
    if (const an::ValidityMask* validity = image.validity(); validity != nullptr)
    {
        an::Result<an::ValidityMask> copied = validity->clone();
        if (!copied.hasValue())
        {
            return copied.status();
        }
        mask = std::move(*copied);
    }
    an::Result<an::consumer::AnyImage> copy = std::visit(
        [&scope, &metadata](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using ImageT = std::decay_t<decltype(typed)>;
            const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
            const auto source = typed.view();
            if (!descriptor.has_value() || !source.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            an::Result<ImageT> created =
                ImageT::create(scope->engine(), *descriptor, an::storageArrangementOf(source->layout()),
                               an::StorageInitialization::Uninitialized, std::move(metadata));
            if (!created.hasValue())
            {
                return created.status();
            }
            {
                auto access = created->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the copy holds nothing");
                }
                if (an::Status copied = an::copyImage(scope->execution(), *source, access->view()); copied.failed())
                {
                    return copied;
                }
            }
            return an::consumer::AnyImage(std::move(*created));
        },
        image.image());
    if (!copy.hasValue())
    {
        return copy.status();
    }
    scope->end();
    return an::consumer::ImageAsset::adopt(session, std::move(*copy), std::move(mask));
}

} // namespace an::ui
