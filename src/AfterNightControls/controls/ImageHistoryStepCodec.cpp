// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The step codecs: the engine's image snapshot behind the codec interface, and a step as an XISF image that carries
/// its own record.

#include "stdafx.hpp"
#include "ImageHistoryStepCodec.h"

#include "ImageHistoryBackupQueue.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ImageSnapshot.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QUuid>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace an::ui::history
{

namespace
{

[[nodiscard]] an::Status refusal(const an::ErrorCode code, std::string message, const char* operation)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message), .operation = operation});
}

/// The codec of the engine's image snapshot.
class SnapshotStepCodec final : public StepCodec
{
public:
    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "snapshot";
    }

    [[nodiscard]] std::string_view fileExtension() const noexcept override
    {
        return ".ansnap";
    }

    [[nodiscard]] an::Status write(const an::consumer::EngineSession& session,
                                   const an::consumer::AssetReadLease& image, const std::string& path,
                                   const an::ArtifactIdentity& identity, std::stop_token stop) const override
    {
        return writeHistorySnapshot(session, image, path, identity, std::move(stop));
    }

    [[nodiscard]] an::Result<an::consumer::ImageAsset> read(const an::consumer::EngineSession& session,
                                                            const std::string& path,
                                                            const an::ArtifactIdentity& expected,
                                                            std::stop_token stop) const override
    {
        return readHistorySnapshot(session, path, expected, std::move(stop));
    }
};

// ---- A step as an XISF image.

/// The XISF properties a step's record and its mask travel in.
constexpr std::string_view kRecordProperty = "AfterNight:HistoryStep:Record";
constexpr std::string_view kValidityProperty = "AfterNight:HistoryStep:Validity";

/// The comment an engine card carries when it continues the value of the card before it.
constexpr std::string_view kContinuedCardComment = "continued";

/// The bytes a mask is written with before its rows: its planes, its width and its height, little-endian.
constexpr std::size_t kValidityHeaderBytes = 12u;

/// The largest snapshot a record holds: a snapshot's header, the most metadata one holds, and the samples and the
/// mask of the one pixel it stands for.
constexpr std::uint64_t kMaxRecordSnapshotBytes = an::kMaxImageSnapshotMetadataBytes + 4096u;

[[nodiscard]] std::filesystem::path pathOf(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/// Removes a file when it goes, whether or not the file was made.
class ScopedFile
{
public:
    explicit ScopedFile(std::string path) : m_Path(std::move(path)) {}

    ~ScopedFile()
    {
        std::error_code error;
        std::filesystem::remove(pathOf(m_Path), error);
    }

    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    [[nodiscard]] const std::string& path() const
    {
        return m_Path;
    }

private:
    std::string m_Path;
};

/// A path beside a step's file that no other write or read of the step uses.
[[nodiscard]] std::string recordPathBeside(const std::string& stepPath)
{
    return stepPath + "." + QUuid::createUuid().toString(QUuid::Id128).toStdString() + ".record";
}

/// Bytes as the text a card carries: compressed, then base 64.
[[nodiscard]] QByteArray textOf(const QByteArray& bytes, const int compressionLevel)
{
    return qCompress(bytes, compressionLevel).toBase64();
}

/// The bytes a card's text carries, or nothing for text that is not what `textOf` writes or that states more than
/// `mostBytes`.
[[nodiscard]] std::optional<QByteArray> bytesOf(const QByteArray& text, const std::uint64_t mostBytes)
{
    const QByteArray::FromBase64Result decoded =
        QByteArray::fromBase64Encoding(text, QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || decoded.decoded.size() < 4)
    {
        return std::nullopt;
    }
    // The compressed form opens with the length it expands to, which is checked before anything is expanded.
    const auto* const head = reinterpret_cast<const unsigned char*>(decoded.decoded.constData());
    const std::uint64_t stated = (std::uint64_t{head[0]} << 24u) | (std::uint64_t{head[1]} << 16u) |
                                 (std::uint64_t{head[2]} << 8u) | std::uint64_t{head[3]};
    if (stated == 0u || stated > mostBytes)
    {
        return std::nullopt;
    }
    QByteArray bytes = qUncompress(decoded.decoded);
    if (static_cast<std::uint64_t>(bytes.size()) != stated)
    {
        return std::nullopt;
    }
    return bytes;
}

/// Appends a field's text as one String property of the image, continued across as many cards as it takes.
[[nodiscard]] an::Status appendTextCards(std::vector<an::CodecCard>& cards, const std::string_view property,
                                         const QByteArray& text)
{
    const std::size_t total = static_cast<std::size_t>(text.size());
    for (std::size_t offset = 0u; offset < total; offset += an::kMaxCodecCardTextBytes)
    {
        an::Result<an::CodecCard> card = an::CodecCard::create(
            an::CodecNamespace::Xisf, std::string(property),
            std::string(text.constData() + offset, (std::min)(an::kMaxCodecCardTextBytes, total - offset)),
            offset == 0u ? std::string("String") : std::string(kContinuedCardComment));
        if (!card.hasValue())
        {
            return card.status();
        }
        cards.push_back(std::move(*card));
    }
    return an::Status::success();
}

/// The text of a property as the cards of a file hold it, its continuations joined; empty when the file states none.
[[nodiscard]] QByteArray textOfCards(const an::CodecMetadata& codec, const std::string_view property)
{
    QByteArray text;
    for (const an::CodecCard& card : codec.cards())
    {
        if (card.origin() == an::CodecNamespace::Xisf && card.keyword() == property)
        {
            text.append(card.value().data(), static_cast<qsizetype>(card.value().size()));
        }
    }
    return text;
}

/// The frame the XISF encoder is handed for a frame: the same bytes, described without what XISF cannot spell - a
/// signed integer, rows counted from the bottom. The step's record states the description whole, so the image's own
/// description is for another reader.
struct EncodedFrame
{
    /// The frame re-described, when the format cannot spell the one given.
    std::optional<an::RawFrame> redescribed;

    /// The representation the encoder's intent states.
    an::SampleType sampleType = an::SampleType::Float32;
};

[[nodiscard]] an::Result<EncodedFrame> encodedFrameOf(const an::RawFrame& frame)
{
    const std::optional<an::RawFrameDescriptor> descriptor = frame.descriptor();
    const std::optional<an::ImageLayout> layout = frame.layout();
    const std::optional<an::SampleType> type = descriptor.has_value() ? descriptor->residentSampleType() : std::nullopt;
    if (!descriptor.has_value() || !layout.has_value() || !type.has_value())
    {
        return refusal(an::ErrorCode::Unsupported, "the step's samples are not of a resident type", "StepCodec::write");
    }
    EncodedFrame encoded;
    encoded.sampleType = *type;
    an::RawFrameDescriptorRequest request = descriptor->toRequest();
    bool changed = false;
    if (request.sampleFormat.sampleClass == an::RawSampleClass::SignedInteger)
    {
        request.sampleFormat.sampleClass = an::RawSampleClass::UnsignedInteger;
        encoded.sampleType = *type == an::SampleType::Int8    ? an::SampleType::UInt8
                             : *type == an::SampleType::Int16 ? an::SampleType::UInt16
                                                              : an::SampleType::UInt32;
        changed = true;
    }
    if (request.coordinateOrigin != an::CoordinateOrigin::TopLeft)
    {
        request.coordinateOrigin = an::CoordinateOrigin::TopLeft;
        changed = true;
    }
    if (!changed)
    {
        return encoded;
    }
    const std::optional<an::RawFrameDescriptor> described = an::RawFrameDescriptor::create(request);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::Internal, "the step's samples could not be described for the format",
                       "StepCodec::write");
    }
    an::Result<an::SampleBuffer> bytes = an::SampleBuffer::borrowConst(frame.bytes());
    if (!bytes.hasValue())
    {
        return bytes.status();
    }
    an::Result<an::RawFrame> redescribed = an::RawFrame::create(*described, *layout, std::move(*bytes));
    if (!redescribed.hasValue())
    {
        return redescribed.status();
    }
    encoded.redescribed.emplace(std::move(*redescribed));
    return encoded;
}

/// The step's record as text: the engine's snapshot of the metadata and the identity over a frame of one pixel that
/// carries the image's description.
///
/// The snapshot is the one encoding of an image's metadata the engine reads back whole, version and checksums
/// included, so the record is that and not a second encoding kept in step with it by hand. It is written beside the
/// step and removed before this returns.
[[nodiscard]] an::Result<QByteArray> recordTextOf(const an::consumer::OperationScope& scope,
                                                  const an::RawFrameDescriptor& descriptor,
                                                  const an::ImageMetadata& metadata, const std::string& stepPath,
                                                  const an::ArtifactIdentity& identity)
{
    const std::uint32_t channels = descriptor.extent().channelCount();
    const std::optional<an::ImageExtent> pixel = an::ImageExtent::create(1u, 1u, channels, scope.engine().limits());
    an::RawFrameDescriptorRequest request = descriptor.toRequest();
    if (!pixel.has_value())
    {
        return refusal(an::ErrorCode::LimitExceeded, "the step's channels are beyond the engine's limits",
                       "StepCodec::write");
    }
    request.extent = *pixel;
    const std::optional<an::RawFrameDescriptor> standIn = an::RawFrameDescriptor::create(request);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *pixel,
                                                       .elementSizeBytes = descriptor.sampleFormat().sizeBytes,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = channels,
                                                       .rowAlignmentBytes = 1u},
                                scope.engine().limits());
    if (!standIn.has_value() || !layout.has_value())
    {
        return refusal(an::ErrorCode::Internal, "the step's description could not be recorded", "StepCodec::write");
    }
    // Eight-byte storage, so a sample of any representation is read from an address it may be read from.
    const std::vector<std::uint64_t> zeros((layout->totalBytes() + 7u) / 8u, 0u);
    an::Result<an::SampleBuffer> bytes = an::SampleBuffer::borrowConst(
        std::as_bytes(std::span<const std::uint64_t>(zeros)).first(static_cast<std::size_t>(layout->totalBytes())));
    if (!bytes.hasValue())
    {
        return bytes.status();
    }
    const an::Result<an::RawFrame> frame = an::RawFrame::create(*standIn, *layout, std::move(*bytes));
    if (!frame.hasValue())
    {
        return frame.status();
    }
    // A snapshot whose metadata names a mask must hold one; the stand-in's says nothing of the image's.
    std::optional<an::ValidityMask> mask;
    if (metadata.semantics().validity().rule() == an::ValidityRule::ExplicitMask)
    {
        const std::optional<an::ImageExtent> shared = an::ImageExtent::create(1u, 1u, 1u, scope.engine().limits());
        an::Result<an::ValidityMask> made = an::ValidityMask::createAllValid(scope.engine(), *shared);
        if (!made.hasValue())
        {
            return made.status();
        }
        mask.emplace(std::move(*made));
    }
    const ScopedFile record(recordPathBeside(stepPath));
    const an::Result<an::ImageSnapshotWriteReport> written = an::writeImageSnapshot(
        scope.engine(), scope.execution(),
        an::ImageSnapshotWriteRequest{
            .destinationPath = record.path(), .identity = &identity, .durability = an::OutputDurability::CloseOnly},
        *frame, metadata, mask.has_value() ? &*mask : nullptr);
    if (!written.hasValue())
    {
        return written.status();
    }
    QFile file(QString::fromUtf8(record.path().data(), static_cast<qsizetype>(record.path().size())));
    if (!file.open(QIODevice::ReadOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure, "the step's record could not be read back", "StepCodec::write");
    }
    const QByteArray snapshot = file.readAll();
    file.close();
    if (static_cast<std::uint64_t>(snapshot.size()) != written->bytesWritten)
    {
        return refusal(an::ErrorCode::ExternalFailure, "the step's record was read back short", "StepCodec::write");
    }
    return textOf(snapshot, 6);
}

/// A mask as text: its planes, width and height, then its rows without their padding.
[[nodiscard]] QByteArray validityTextOf(const an::ValidityMask& mask)
{
    const std::optional<an::ValidityMaskView<const std::uint8_t>> view = mask.view();
    if (!view.has_value())
    {
        return {};
    }
    const std::uint64_t rowBytes = view->minimumRowBytes();
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(kValidityHeaderBytes + rowBytes * view->height() * view->planeCount()));
    for (const std::uint32_t word : {view->planeCount(), view->width(), view->height()})
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes.append(static_cast<char>((word >> shift) & 0xffu));
        }
    }
    for (std::uint32_t plane = 0u; plane < view->planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            const std::uint8_t* const row =
                view->data() + plane * view->planeStrideBytes() + y * view->rowStrideBytes();
            bytes.append(reinterpret_cast<const char*>(row), static_cast<qsizetype>(rowBytes));
        }
    }
    // The fastest level: a mask is long runs of ones where an image is covered, and those cost nothing at any level.
    return textOf(bytes, 1);
}

/// The mask a file's text states, for an image of the extent given.
[[nodiscard]] an::Result<an::ValidityMask> validityOf(const an::EngineContext& engine, const QByteArray& text,
                                                      const an::ImageExtent& image)
{
    const std::uint64_t rowBytes = an::packedMaskRowBytes(image.width());
    const std::uint64_t mostBytes =
        kValidityHeaderBytes + rowBytes * image.height() * static_cast<std::uint64_t>(image.channelCount());
    const std::optional<QByteArray> bytes = bytesOf(text, mostBytes);
    const auto damaged = [] {
        return refusal(an::ErrorCode::InvalidData, "the step's validity mask is damaged", "StepCodec::read");
    };
    if (!bytes.has_value() || static_cast<std::size_t>(bytes->size()) < kValidityHeaderBytes)
    {
        return damaged();
    }
    const auto* const data = reinterpret_cast<const std::uint8_t*>(bytes->constData());
    const auto word = [data](const std::size_t index) {
        const std::uint8_t* const at = data + 4u * index;
        return std::uint32_t{at[0]} | (std::uint32_t{at[1]} << 8u) | (std::uint32_t{at[2]} << 16u) |
               (std::uint32_t{at[3]} << 24u);
    };
    const std::uint32_t planes = word(0u);
    if ((planes != 1u && planes != image.channelCount()) || word(1u) != image.width() || word(2u) != image.height() ||
        static_cast<std::uint64_t>(bytes->size()) != kValidityHeaderBytes + rowBytes * image.height() * planes)
    {
        return damaged();
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(image.width(), image.height(), planes, engine.limits());
    if (!extent.has_value())
    {
        return damaged();
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, *extent);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> view = mask->view();
    if (!view.has_value())
    {
        return damaged();
    }
    const std::uint8_t padding = static_cast<std::uint8_t>(~an::packedMaskLastByteMask(image.width()));
    const std::uint8_t* source = data + kValidityHeaderBytes;
    for (std::uint32_t plane = 0u; plane < planes; ++plane)
    {
        for (std::uint32_t y = 0u; y < image.height(); ++y, source += rowBytes)
        {
            // A bit past the row's width belongs to no sample, and a mask whose pad bits are set is not one.
            if ((source[rowBytes - 1u] & padding) != 0u)
            {
                return damaged();
            }
            std::memcpy(view->data() + plane * view->planeStrideBytes() + y * view->rowStrideBytes(), source,
                        static_cast<std::size_t>(rowBytes));
        }
    }
    return mask;
}

/// Whether text is what a FITS record holds: printable ASCII.
[[nodiscard]] bool printableAscii(const std::string_view text) noexcept
{
    return std::all_of(text.begin(), text.end(), [](const unsigned char c) { return c >= 32u && c <= 126u; });
}

/// The longest text a FITS card is written with: what a read of the file takes back as one card, with room to spare.
constexpr std::size_t kPlainFitsTextBytes = 4000u;

/// Whether a FITS card is one an XISF unit states as a FITS keyword and reads back: a keyword of at most eight
/// capitals, digits, hyphens and underscores, text in printable ASCII no longer than a read takes, and a number no
/// longer than one record.
[[nodiscard]] bool plainFitsCard(const an::CodecCard& card)
{
    const std::string& keyword = card.keyword();
    const bool named = !keyword.empty() && keyword.size() <= 8u && keyword != "CONTINUE" &&
                       std::all_of(keyword.begin(), keyword.end(), [](const char c) {
                           return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
                       });
    if (!named || !printableAscii(card.value()) || !printableAscii(card.comment()) ||
        card.value().size() > kPlainFitsTextBytes || card.comment().size() > kPlainFitsTextBytes)
    {
        return false;
    }
    const std::string& value = card.value();
    const bool number =
        !value.starts_with('\'') && value.find('/') == std::string::npos && an::parseNumericCard(value).hasValue();
    return !number || value.size() <= 70u;
}

/// Whether an XISF card is one an XISF unit states as a property: a name of words of letters, digits and underscores
/// joined by colons, a value in UTF-8 without a null character, and a comment without a control character.
[[nodiscard]] bool plainXisfCard(const an::CodecCard& card)
{
    bool wordStart = true;
    for (const char c : card.keyword())
    {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (c == ':')
        {
            if (wordStart)
            {
                return false;
            }
            wordStart = true;
            continue;
        }
        if (!letter && !(digit && !wordStart))
        {
            return false;
        }
        wordStart = false;
    }
    const std::string& value = card.value();
    const bool spelled = QByteArrayView(value.data(), static_cast<qsizetype>(value.size())).isValidUtf8() &&
                         value.find('\0') == std::string::npos;
    const bool commented = std::none_of(card.comment().begin(), card.comment().end(),
                                        [](const unsigned char c) { return c < 0x20u || c == 0x7fu; });
    return !card.keyword().empty() && !wordStart && spelled && commented;
}

/// Whether a card is one a step's record travels in: such a card of an image that was itself read from a step's
/// file is that step's, and written again it would be read as part of this one.
[[nodiscard]] bool recordCard(const an::CodecCard& card)
{
    return card.origin() == an::CodecNamespace::Xisf &&
           (card.keyword() == kRecordProperty || card.keyword() == kValidityProperty);
}

/// The metadata the encoder is handed beside the record, for the header another reader of the file sees: the image's
/// own, less what an XISF unit does not carry. The step's record holds the metadata whole, so what is left out here
/// is left out of nothing a read of the step gives back.
///
/// The encoder refuses a whole image for one card it cannot spell - a keyword longer than FITS allows, a name in
/// another alphabet - so such cards and such names are left out one by one, and the rest of the header stays.
[[nodiscard]] an::Result<an::ImageMetadata> headerMetadataOf(const an::ImageMetadata& metadata)
{
    std::vector<an::CodecCard> kept;
    for (const an::CodecCard& card : metadata.codec().cards())
    {
        if (recordCard(card))
        {
            continue;
        }
        const bool carried = card.origin() == an::CodecNamespace::Fits
                                 ? plainFitsCard(card)
                                 : (card.origin() == an::CodecNamespace::Xisf && plainXisfCard(card));
        if (carried)
        {
            kept.push_back(card);
        }
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(metadata.codec().sourceEncoding(), std::move(kept));
    if (!codec.hasValue())
    {
        return codec.status();
    }
    an::AcquisitionMetadataRequest acquisition = metadata.acquisition().toRequest();
    for (std::string* const name : {&acquisition.instrumentName, &acquisition.telescopeName, &acquisition.observerName,
                                    &acquisition.objectName, &acquisition.filterName})
    {
        if (!printableAscii(*name))
        {
            name->clear();
        }
    }
    an::Result<an::AcquisitionMetadata> stated = an::AcquisitionMetadata::create(acquisition);
    if (!stated.hasValue())
    {
        return stated.status();
    }
    return an::ImageMetadata::create(std::move(*stated), metadata.semantics(), metadata.astrometry(),
                                     metadata.provenance(), std::move(*codec), std::nullopt);
}

/// Whether a refused write may have been refused for the image's own cards: a card the format cannot spell, or more
/// cards than one image holds.
[[nodiscard]] bool refusedForItsCards(const an::Status& status)
{
    return status.code() == an::ErrorCode::InvalidArgument || status.code() == an::ErrorCode::LimitExceeded;
}

/// A step as an XISF image that carries its record.
class XisfStepCodec final : public StepCodec
{
public:
    explicit XisfStepCodec(const an::OutputCompression compression) : m_Compression(compression) {}

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "xisf";
    }

    [[nodiscard]] std::string_view fileExtension() const noexcept override
    {
        return ".xisf";
    }

    [[nodiscard]] an::Status write(const an::consumer::EngineSession& session,
                                   const an::consumer::AssetReadLease& image, const std::string& path,
                                   const an::ArtifactIdentity& identity, std::stop_token stop) const override
    {
        if (!image.valid())
        {
            return refusal(an::ErrorCode::NotInitialized, "there is no image to keep", "StepCodec::write");
        }
        // A step is written with one thread, as a snapshot is: the edit it keeps has the session's others.
        an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
            .purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 1u, .stopToken = std::move(stop)});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        return image.visit([&](const auto& typed) -> an::Status {
            const an::Result<an::RawFrame> frame = an::borrowImageFrame(typed);
            if (!frame.hasValue())
            {
                return frame.status();
            }
            return writeStep(*scope, *frame, typed.metadata(), image.validity(), path, identity);
        });
    }

    [[nodiscard]] an::Result<an::consumer::ImageAsset> read(const an::consumer::EngineSession& session,
                                                            const std::string& path,
                                                            const an::ArtifactIdentity& expected,
                                                            std::stop_token stop) const override
    {
        an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
            .purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 1u, .stopToken = std::move(stop)});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::CodecDecodeResult> decoded = an::decodeImage(
            scope->engine(), scope->execution(), an::CodecReadRequest{.path = path, .format = an::CodecFormat::Xisf});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const auto notAStep = [](const char* why) {
            return refusal(an::ErrorCode::InvalidData, why, "StepCodec::read");
        };
        const QByteArray recordText = textOfCards(decoded->metadata.codec(), kRecordProperty);
        const std::optional<QByteArray> snapshot =
            recordText.isEmpty() ? std::nullopt : bytesOf(recordText, kMaxRecordSnapshotBytes);
        if (!snapshot.has_value())
        {
            return notAStep("the file at the step's path carries no step record");
        }
        // The engine reads a snapshot from a file, so the record goes to one beside the step for as long as that
        // takes.
        std::optional<an::ImageSnapshotContents> record;
        {
            const ScopedFile file(recordPathBeside(path));
            QFile stream(QString::fromUtf8(file.path().data(), static_cast<qsizetype>(file.path().size())));
            if (!stream.open(QIODevice::WriteOnly) || stream.write(*snapshot) != snapshot->size())
            {
                return refusal(an::ErrorCode::ExternalFailure, "the step's record could not be staged",
                               "StepCodec::read");
            }
            stream.close();
            an::Result<an::ImageSnapshotContents> contents =
                an::readImageSnapshot(scope->engine(), scope->execution(), file.path());
            if (!contents.hasValue())
            {
                return contents.status().code() == an::ErrorCode::Cancelled ? contents.status()
                                                                            : notAStep("the step's record is damaged");
            }
            record.emplace(std::move(*contents));
        }
        // Only the step this history wrote at this path is its step: anything else there is another file.
        if (!(record->identity == expected))
        {
            return notAStep("the file at the step's path is not the step the history wrote");
        }
        const std::optional<an::RawFrameDescriptor> stated = record->frame.descriptor();
        const std::optional<an::RawFrameDescriptor> stored = decoded->frame.descriptor();
        const std::optional<an::SampleType> sampleType =
            stated.has_value() ? stated->residentSampleType() : std::nullopt;
        if (!sampleType.has_value() || !stored.has_value() ||
            stored->sampleFormat().sizeBytes != stated->sampleFormat().sizeBytes ||
            (stored->sampleFormat().sampleClass == an::RawSampleClass::FloatingPoint) !=
                (stated->sampleFormat().sampleClass == an::RawSampleClass::FloatingPoint) ||
            stored->extent().channelCount() != stated->extent().channelCount())
        {
            return notAStep("the step's samples are not the ones its record describes");
        }
        // The description is the record's, over the extent the samples have.
        an::RawFrameDescriptorRequest request = stated->toRequest();
        request.extent = stored->extent();
        const std::optional<an::RawFrameDescriptor> described = an::RawFrameDescriptor::create(request);
        const std::optional<an::ImageDescriptor> descriptor =
            described.has_value() ? described->toImageDescriptor() : std::nullopt;
        if (!descriptor.has_value())
        {
            return notAStep("the step's record describes no image");
        }
        std::optional<an::ValidityMask> validity;
        const QByteArray validityText = textOfCards(decoded->metadata.codec(), kValidityProperty);
        if (!validityText.isEmpty())
        {
            an::Result<an::ValidityMask> mask = validityOf(scope->engine(), validityText, stored->extent());
            if (!mask.hasValue())
            {
                return mask.status();
            }
            validity.emplace(std::move(*mask));
        }
        an::Result<an::AdoptedFrame> adopted = decoded->frame.releaseAdoptable();
        scope->end();
        if (!adopted.hasValue())
        {
            return adopted.status();
        }
        an::Result<an::consumer::AnyImage> image =
            refusal(an::ErrorCode::Internal, "the step's samples were not adopted", "StepCodec::read");
        (void)an::dispatchSampleType(*sampleType, [&]<typename SampleT>() {
            an::Result<an::ImageStorage<SampleT>> storage =
                an::ImageStorage<SampleT>::fromBytes(std::move(adopted->storage));
            if (!storage.hasValue())
            {
                image = storage.status();
                return;
            }
            an::Result<an::Image<SampleT>> restored =
                an::Image<SampleT>::fromStorage(std::move(*storage), *descriptor, std::move(record->metadata));
            image = restored.hasValue()
                        ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage{std::move(*restored)})
                        : an::Result<an::consumer::AnyImage>(restored.status());
        });
        if (!image.hasValue())
        {
            return image.status();
        }
        return an::consumer::ImageAsset::adopt(session, std::move(*image), std::move(validity));
    }

private:
    [[nodiscard]] an::Status writeStep(const an::consumer::OperationScope& scope, const an::RawFrame& frame,
                                       const an::ImageMetadata& metadata, const an::ValidityMask* validity,
                                       const std::string& path, const an::ArtifactIdentity& identity) const
    {
        const an::Result<EncodedFrame> encoded = encodedFrameOf(frame);
        if (!encoded.hasValue())
        {
            return encoded.status();
        }
        const an::Result<QByteArray> record = recordTextOf(scope, *frame.descriptor(), metadata, path, identity);
        if (!record.hasValue())
        {
            return record.status();
        }
        const QByteArray validityText = validity != nullptr ? validityTextOf(*validity) : QByteArray();
        if (validity != nullptr && validityText.isEmpty())
        {
            return refusal(an::ErrorCode::NotInitialized, "the step's validity mask holds nothing", "StepCodec::write");
        }
        if (static_cast<std::size_t>(record->size()) + static_cast<std::size_t>(validityText.size()) >
            kMaxXisfStepRecordTextBytes)
        {
            return refusal(an::ErrorCode::LimitExceeded,
                           fmt::format("the step's record, {} bytes of text, is more than the {} an XISF image's "
                                       "cards carry",
                                       record->size() + validityText.size(), kMaxXisfStepRecordTextBytes),
                           "StepCodec::write");
        }
        std::vector<an::CodecCard> cards;
        if (an::Status added = appendTextCards(cards, kRecordProperty, *record); added.failed())
        {
            return added;
        }
        if (an::Status added = appendTextCards(cards, kValidityProperty, validityText); added.failed())
        {
            return added;
        }
        // First with the image's own cards, so another reader finds the header a person looks for. An image may
        // still hold a card the format refuses, or more cards than leave room for the record: the step is then
        // written with the record and the identity alone, which is all a read of it needs.
        const an::Result<an::ImageMetadata> header = headerMetadataOf(metadata);
        an::Status written = header.hasValue()
                                 ? writeImage(scope, encoded->redescribed.has_value() ? *encoded->redescribed : frame,
                                              encoded->sampleType, *header, true, cards, path, identity)
                                 : header.status();
        if (written.failed() && refusedForItsCards(written))
        {
            written = writeImage(scope, encoded->redescribed.has_value() ? *encoded->redescribed : frame,
                                 encoded->sampleType, an::ImageMetadata{}, false, cards, path, identity);
        }
        return written;
    }

    [[nodiscard]] an::Status writeImage(const an::consumer::OperationScope& scope, const an::RawFrame& frame,
                                        const an::SampleType sampleType, const an::ImageMetadata& metadata,
                                        const bool preserveSourceCards, const std::vector<an::CodecCard>& cards,
                                        const std::string& path, const an::ArtifactIdentity& identity) const
    {
        const an::Result<an::OutputIntent> intent =
            an::OutputIntent::create(an::OutputIntentRequest{.sampleType = sampleType,
                                                             .compression = m_Compression,
                                                             .preserveSourceCards = preserveSourceCards,
                                                             .cards = cards});
        if (!intent.hasValue())
        {
            return intent.status();
        }
        // A step lives as long as its history, which a crash of the machine ends, so it is not flushed to stable
        // storage; it is still published whole or not at all.
        an::OutputRequest request;
        request.destinationPath = path;
        request.format = an::CodecFormat::Xisf;
        request.intent = &*intent;
        request.identity = &identity;
        request.durability = an::OutputDurability::CloseOnly;
        request.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
        // Without the declaration and the comment a header opens with, a virus scanner does not take the step for
        // markup and read every sample of it for tags.
        request.headerProlog = an::OutputHeaderProlog::Blank;
        const an::Result<an::OutputReport> report =
            an::writeImage(scope.engine(), scope.execution(), request, frame, metadata);
        return report.hasValue() ? an::Status::success() : report.status();
    }

    const an::OutputCompression m_Compression;
};

} // namespace

std::string_view stepCompressionName(const StepCompression compression) noexcept
{
    switch (compression)
    {
    case StepCompression::None:
        return "none";
    case StepCompression::Lz4:
        return "lz4";
    case StepCompression::Zstd:
        return "zstd";
    }
    return "none";
}

StepCodecPtr snapshotStepCodec()
{
    return std::make_shared<const SnapshotStepCodec>();
}

StepCodecPtr xisfStepCodec(const StepCompression compression)
{
    const an::OutputCompression samples = compression == StepCompression::Lz4    ? an::OutputCompression::Lz4
                                          : compression == StepCompression::Zstd ? an::OutputCompression::Zstd
                                                                                 : an::OutputCompression::None;
    return std::make_shared<const XisfStepCodec>(samples);
}

} // namespace an::ui::history
