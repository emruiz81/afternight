// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the astrometry suite's cases share: header cards and the metadata that carries them, a tangent-plane solution's
/// cards, images that carry metadata as assets and documents of a session, a solver runner whose answers a case
/// queues, a solver's header-only answer, the bundled catalogs read from the source tree, and the files a case writes
/// as a solver, a catalog or an image.
#pragma once

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/BundledCatalogs.h"
#include "app/CancellationToken.h"
#include "app/SolverProcessRunner.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::tests::astrometry
{

using an::tests::viewer::channelCountOf;
using an::tests::viewer::Channels;
using an::tests::viewer::describeImage;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeView;
using an::tests::viewer::openViewerContext;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;
using an::tests::viewer::settle;
using an::tests::viewer::ViewerTestContext;
using an::tests::viewer::ViewHandle;

/// Adds a FITS card to a list; fails the case when the card cannot be made.
inline void addCard(std::vector<an::CodecCard>& cards, std::string keyword, std::string value, std::string comment = {})
{
    an::Result<an::CodecCard> card =
        an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::move(comment));
    ASSERT_TRUE(card.hasValue()) << card.status().message();
    cards.push_back(std::move(*card));
}

/// The value a list of cards states for a keyword, or nothing.
[[nodiscard]] inline std::optional<std::string> cardValue(std::span<const an::CodecCard> cards,
                                                          const std::string_view keyword)
{
    for (const an::CodecCard& card : cards)
    {
        if (card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword)
        {
            return card.value();
        }
    }
    return std::nullopt;
}

/// How many cards of a list the card bridge owns.
[[nodiscard]] inline std::size_t wcsCardCount(std::span<const an::CodecCard> cards)
{
    std::size_t count = 0u;
    for (const an::CodecCard& card : cards)
    {
        if (card.origin() == an::CodecNamespace::Fits && an::isWcsKeyword(card.keyword()))
        {
            ++count;
        }
    }
    return count;
}

/// A number as a header card writes it: twelve decimals, which read back as the same double for every value a case
/// states.
[[nodiscard]] inline std::string cardNumber(const double value)
{
    return QString::number(value, 'f', 12).toStdString();
}

/// A tangent-plane solution as a header states it: the reference pixel in the convention's one-based centres, where it
/// points, and the four matrix entries in degrees per pixel.
struct TangentPlane
{
    double crpix1 = 0.0;
    double crpix2 = 0.0;
    double rightAscension = 0.0;
    double declination = 0.0;
    double cd11 = 0.0;
    double cd12 = 0.0;
    double cd21 = 0.0;
    double cd22 = 0.0;
};

/// A plane of square pixels with north up and east left, about a reference pixel.
[[nodiscard]] inline TangentPlane northUpPlane(const double crpix1, const double crpix2, const double rightAscension,
                                               const double declination, const double degreesPerPixel)
{
    return TangentPlane{.crpix1 = crpix1,
                        .crpix2 = crpix2,
                        .rightAscension = rightAscension,
                        .declination = declination,
                        .cd11 = -degreesPerPixel,
                        .cd12 = 0.0,
                        .cd21 = 0.0,
                        .cd22 = degreesPerPixel};
}

/// A plane's cards, with the distortion suffix and a second-order forward polynomial when asked for.
[[nodiscard]] inline std::vector<an::CodecCard> tangentPlaneCards(const TangentPlane& plane, const bool sip = false)
{
    std::vector<an::CodecCard> cards;
    addCard(cards, "CTYPE1", sip ? "'RA---TAN-SIP'" : "'RA---TAN'");
    addCard(cards, "CTYPE2", sip ? "'DEC--TAN-SIP'" : "'DEC--TAN'");
    addCard(cards, "CUNIT1", "'deg'");
    addCard(cards, "CUNIT2", "'deg'");
    addCard(cards, "CRPIX1", cardNumber(plane.crpix1));
    addCard(cards, "CRPIX2", cardNumber(plane.crpix2));
    addCard(cards, "CRVAL1", cardNumber(plane.rightAscension));
    addCard(cards, "CRVAL2", cardNumber(plane.declination));
    addCard(cards, "CD1_1", cardNumber(plane.cd11));
    addCard(cards, "CD1_2", cardNumber(plane.cd12));
    addCard(cards, "CD2_1", cardNumber(plane.cd21));
    addCard(cards, "CD2_2", cardNumber(plane.cd22));
    if (sip)
    {
        addCard(cards, "A_ORDER", "2");
        addCard(cards, "B_ORDER", "2");
        addCard(cards, "A_2_0", "0.0001");
        addCard(cards, "B_0_2", "-0.0002");
    }
    return cards;
}

/// Cards appended to others.
[[nodiscard]] inline std::vector<an::CodecCard> joined(std::vector<an::CodecCard> first,
                                                       const std::vector<an::CodecCard>& second)
{
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

/// Metadata carrying cards and an acquisition record, and stating nothing else.
[[nodiscard]] inline an::ImageMetadata metadataWithCards(std::vector<an::CodecCard> cards,
                                                         an::AcquisitionMetadata acquisition = {})
{
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    if (!codec.hasValue())
    {
        return an::ImageMetadata{};
    }
    return an::ImageMetadata::create(std::move(acquisition), an::ImageSemantics{}, an::AstrometricSolution{},
                                     an::Provenance{}, std::move(*codec), std::nullopt);
}

/// An acquisition record stating when the exposure began, in nanoseconds since the Unix epoch.
[[nodiscard]] inline an::AcquisitionMetadata acquisitionStartingAt(const std::int64_t nanosecondsSinceEpoch)
{
    an::Result<an::AcquisitionMetadata> acquisition = an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{
        .exposureStart = an::UtcInstant{.nanosecondsSinceEpoch = nanosecondsSinceEpoch}});
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    return acquisition.hasValue() ? std::move(*acquisition) : an::AcquisitionMetadata{};
}

/// The solution cards state for a frame of a size; fails the case when the bridge refuses them.
[[nodiscard]] inline std::optional<an::WcsSolution> solutionOf(std::span<const an::CodecCard> cards,
                                                               const std::uint32_t width, const std::uint32_t height)
{
    an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, width, height);
    EXPECT_TRUE(solution.hasValue()) << solution.status().message();
    if (!solution.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*solution);
}

/// A new asset of a session of the representation `SampleT`, planar, each sample `value(x, y, channel)`, carrying
/// metadata; fails the case when it cannot be built.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset>
makeAssetWithMetadata(const an::consumer::EngineSession& session, const std::uint32_t width, const std::uint32_t height,
                      const Channels channels,
                      const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
                      an::ImageMetadata metadata)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        describeImage(hold->engine(), an::kSampleTypeOf<SampleT>, width, height, channels);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, std::move(metadata));
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        EXPECT_TRUE(access.has_value());
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t channel = 0u; channel < channelCountOf(channels); ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto row = access->view().row(channel, y);
                EXPECT_TRUE(row.has_value());
                if (!row.has_value())
                {
                    return std::nullopt;
                }
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (*row)[x] = value(x, y, channel);
                }
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// A grey Float32 document of a session carrying cards, its samples a gentle ramp; fails the case when it cannot be
/// built.
[[nodiscard]] inline std::optional<an::ui::ImageDocument>
makeCardDocument(const an::consumer::EngineSession& session, const std::uint32_t width, const std::uint32_t height,
                 std::vector<an::CodecCard> cards, an::AcquisitionMetadata acquisition = {})
{
    std::optional<an::consumer::ImageAsset> asset = makeAssetWithMetadata<float>(
        session, width, height, Channels::Grey,
        [width](const std::uint32_t x, const std::uint32_t y, std::uint32_t) {
            return 0.1f + 0.4f * static_cast<float>((x + y) % width) / static_cast<float>(width);
        },
        metadataWithCards(std::move(cards), std::move(acquisition)));
    if (!asset.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset));
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// The cards a view's document holds now, read under a lease that never waits; empty when the view holds nothing.
[[nodiscard]] inline std::vector<an::CodecCard> documentCards(const ImageViewControl& view)
{
    std::vector<an::CodecCard> cards;
    if (!view.hasImage())
    {
        return cards;
    }
    an::Result<an::consumer::AssetReadLease> lease = view.document().tryRead();
    if (!lease.hasValue())
    {
        return cards;
    }
    lease->visit([&cards](const auto& typed) {
        const auto stated = typed.metadata().codec().cards();
        cards.assign(stated.begin(), stated.end());
    });
    return cards;
}

/// A solver's header-only answer as text lines ending in `END`: a tangent plane of square pixels about a reference
/// pixel.
[[nodiscard]] inline QByteArray makeSolvedHeaderBytes(const double crpix1, const double crpix2, const double crval1_deg,
                                                      const double crval2_deg, const double scale_deg_per_px)
{
    QByteArray content;
    content += "CTYPE1  = 'RA---TAN'\n";
    content += "CTYPE2  = 'DEC--TAN'\n";
    content += "CUNIT1  = 'deg'\n";
    content += "CUNIT2  = 'deg'\n";
    content += QByteArray("CRPIX1  = ") + QString::number(crpix1, 'f', 6).toUtf8() + '\n';
    content += QByteArray("CRPIX2  = ") + QString::number(crpix2, 'f', 6).toUtf8() + '\n';
    content += QByteArray("CRVAL1  = ") + QString::number(crval1_deg, 'f', 8).toUtf8() + '\n';
    content += QByteArray("CRVAL2  = ") + QString::number(crval2_deg, 'f', 8).toUtf8() + '\n';
    content += QByteArray("CD1_1   = ") + QString::number(-scale_deg_per_px, 'f', 10).toUtf8() + '\n';
    content += "CD1_2   = 0.0\n";
    content += "CD2_1   = 0.0\n";
    content += QByteArray("CD2_2   = ") + QString::number(scale_deg_per_px, 'f', 10).toUtf8() + '\n';
    content += "END\n";
    return content;
}

/// Writes bytes to a file, making its directory; false when it cannot.
[[nodiscard]] inline bool writeFileBytes(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).dir().path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    const qint64 written = file.write(bytes);
    file.close();
    return written == bytes.size();
}

/// A stand-in file in a directory, executable and named as an executable is on this system; empty when it cannot be
/// written.
[[nodiscard]] inline QString writeDummyInputFile(const QTemporaryDir& directory, const QString& filename)
{
#ifdef _WIN32
    const QString effectiveFilename = filename + QStringLiteral(".exe");
#else
    const QString& effectiveFilename = filename;
#endif
    const QString path = QDir(directory.path()).filePath(effectiveFilename);
    if (!writeFileBytes(path, QByteArray("dummy")))
    {
        return QString();
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                    QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                    QFileDevice::ExeOther);
    return path;
}

/// Writes a Gaia catalog's manifest into a directory; false when it cannot.
[[nodiscard]] inline bool writeGaiaCatalogMeta(const QString& directory, const QJsonObject& root)
{
    return writeFileBytes(QDir(directory).filePath(QStringLiteral("catalog_meta.json")),
                          QJsonDocument(root).toJson(QJsonDocument::Compact));
}

/// The argument after a flag, or nothing when the flag is not there or ends the list.
[[nodiscard]] inline QString valueAfterFlag(const QStringList& arguments, const QString& flag)
{
    const qsizetype index = arguments.indexOf(flag);
    if (index < 0 || (index + 1) >= arguments.size())
    {
        return QString();
    }
    return arguments.at(index + 1);
}

/// One run a solver runner was asked for.
struct RunInvocation
{
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    int timeoutSeconds = 0;
};

/// A solver runner that answers each run and each file read as the case queued, and records what it was asked.
class MockSolverProcessRunner final : public an::ui::SolverProcessRunner
{
public:
    std::deque<an::ui::SolverProcessResult> queuedRuns;
    std::deque<std::optional<QByteArray>> queuedReadFiles;
    std::vector<RunInvocation> invocations;
    std::vector<QString> readPaths;
    std::function<void(const an::ui::CancellationToken&)> onRun;

    [[nodiscard]] an::ui::SolverProcessResult run(const QString& executable, const QStringList& arguments,
                                                  const QString& workingDirectory, const int timeoutSeconds,
                                                  const an::ui::CancellationToken& token) override
    {
        invocations.push_back({executable, arguments, workingDirectory, timeoutSeconds});
        if (onRun)
        {
            onRun(token);
        }
        if (queuedRuns.empty())
        {
            return {};
        }
        an::ui::SolverProcessResult result = queuedRuns.front();
        queuedRuns.pop_front();
        return result;
    }

    [[nodiscard]] std::optional<QByteArray> readFile(const QString& path) override
    {
        readPaths.push_back(path);
        if (queuedReadFiles.empty())
        {
            return std::nullopt;
        }
        const std::optional<QByteArray> data = queuedReadFiles.front();
        queuedReadFiles.pop_front();
        return data;
    }
};

/// The repository's file at a path, found by walking up from the working directory; empty when it is not there.
[[nodiscard]] inline std::filesystem::path repositoryFile(const std::filesystem::path& relative)
{
    std::error_code error;
    std::filesystem::path root = std::filesystem::current_path(error);
    while (!error && !root.empty())
    {
        if (std::filesystem::is_regular_file(root / relative))
        {
            return root / relative;
        }
        const std::filesystem::path parent = root.parent_path();
        if (parent == root)
        {
            break;
        }
        root = parent;
    }
    return {};
}

/// A session for a case that calls catalog and astrometry operations directly, and one operation admitted in it. The
/// operation is released before the session.
struct DirectExecution
{
    std::optional<an::tests::controls::TestSessionProvider> provider;
    std::optional<an::consumer::OperationScope> scope;

    [[nodiscard]] const an::ExecutionContext& execution() const
    {
        return scope->execution();
    }
};

/// Opens a session and admits an operation in it; fails the case and answers null when either is refused.
[[nodiscard]] inline std::unique_ptr<DirectExecution> openExecution()
{
    auto direct = std::make_unique<DirectExecution>();
    direct->provider = an::tests::controls::openProvider(1u);
    if (!direct->provider.has_value())
    {
        return nullptr;
    }
    direct->scope = an::tests::viewer::admitted(direct->provider->engineSession());
    if (!direct->scope.has_value())
    {
        return nullptr;
    }
    return direct;
}

/// The entries a region query answers, in its order; fails the case when the query is refused.
[[nodiscard]] inline std::vector<const an::CatalogEntry*>
regionEntries(const an::ExecutionContext& execution, const an::SkyCatalog& catalog, const an::CatalogRegionQuery& query)
{
    std::vector<const an::CatalogEntry*> entries;
    const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(execution, catalog, query);
    EXPECT_TRUE(found.hasValue()) << found.status().message();
    if (!found.hasValue())
    {
        return entries;
    }
    for (const std::uint32_t index : found->entryIndices)
    {
        entries.push_back(catalog.entry(index));
    }
    return entries;
}

/// A cone query: a centre, a radius and, when stated, a magnitude limit.
[[nodiscard]] inline an::CatalogRegionQuery cone(const double rightAscension, const double declination,
                                                 const double radiusDegrees,
                                                 const std::optional<double> magnitudeLimit = std::nullopt,
                                                 const bool admitNamedWithoutMagnitude = false)
{
    an::CatalogRegionQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = rightAscension, .declinationDegrees = declination};
    query.radiusDegrees = radiusDegrees;
    query.magnitudeLimit = magnitudeLimit;
    query.admitNamedEntriesWithoutMagnitude = admitNamedWithoutMagnitude;
    return query;
}

/// The bundled catalogs as the product ships them, read from the source tree rather than the product's resources,
/// which an isolated suite does not compile.
[[nodiscard]] inline an::ui::BundledCatalogSource sourceTreeCatalogs()
{
    return [](const an::ui::BundledCatalog catalog) -> an::Result<QByteArray> {
        const std::filesystem::path relative =
            std::filesystem::path("src") / "AfterNightControls" / "data" /
            (catalog == an::ui::BundledCatalog::DeepSky ? "deep_sky_catalog.csv" : "bright_star_catalog.csv");
        const std::filesystem::path found = repositoryFile(relative);
        QFile file(QString::fromStdU16String(found.u16string()));
        if (found.empty() || !file.open(QIODevice::ReadOnly))
        {
            return an::Status::failure(an::ErrorCode::ExternalFailure, "the source tree's catalog cannot be read");
        }
        return file.readAll();
    };
}

/// One catalog text for either bundled catalog.
[[nodiscard]] inline an::ui::BundledCatalogSource fixedCatalogs(QByteArray deepSky, QByteArray brightStars)
{
    return [deepSky = std::move(deepSky),
            brightStars = std::move(brightStars)](const an::ui::BundledCatalog catalog) -> an::Result<QByteArray> {
        return catalog == an::ui::BundledCatalog::DeepSky ? deepSky : brightStars;
    };
}

} // namespace an::tests::astrometry
