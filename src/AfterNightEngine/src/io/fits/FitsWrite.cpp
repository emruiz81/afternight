// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - encoding a FITS image into a pending path.
//
// Every option, card and sample value is checked before the file is created. The image goes to OpenFITS whole: the
// header - the structural cards the library writes from the image's description, the scaling cards among them, then
// the cards this writer hands it - is formatted and written before the first sample, so the payload is written once,
// in order, and never moved. A frame that is contiguous, aligned and in native byte order is lent to the library as
// it is; any other - padded rows or planes, a foreign byte order, memory not aligned for its samples - is copied out
// run by run as the library asks for its samples, into the library's own staging. The card set is the output
// intent's: the acquisition cards, then the source's own cards when the intent preserves them, then the intent's
// cards, each replacing an earlier card of the same keyword. The structural cards and the ones this writer states
// itself are taken from neither, and an identity card only from the intent. A card is written as its value's text
// says: a number as the number it spells, a quoted string as that string, an empty value as an undefined one, and any
// other text as a string of that text. A small uncompressed file is built in context memory and written in one
// transfer when the engine context's budget allows; any other is streamed into the pending file.

#include "io/fits/FitsFile.h"
#include "io/registry/PayloadForm.h"

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <openfits/card.h>
#include <openfits/compression.h>
#include <openfits/header.h>
#include <openfits/image.h>
#include <openfits/io.h>
#include <openfits/stream_io.h>
#include <openfits/writer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace an
{
namespace
{
/// The largest payload a file is staged in context memory for.
constexpr std::uint64_t kStagedPayloadBytes = std::uint64_t{4u} << 20u;

/// The most header and fill a staged file may hold beside its payload.
constexpr std::uint64_t kStagedHeaderBytes = std::uint64_t{64u} << 10u;

/// Bounded optional staging for a small uncompressed file: the library writes every header and sample into it, and
/// the disk sees one completed write. A tight memory budget simply keeps the streamed path.
class StagedFile
{
public:
    StagedFile() = default;
    StagedFile(const StagedFile&) = delete;
    StagedFile& operator=(const StagedFile&) = delete;
    ~StagedFile()
    {
        if (m_data != nullptr)
            m_resource->deallocate(m_data, m_capacity, 64u);
    }

    /// Takes room for a file of `bytes` from the context, when the budget allows it.
    void prepare(const EngineContext& engine, std::uint64_t bytes)
    {
        m_resource = engine.memoryResource();
        if (m_resource == nullptr || bytes == 0u ||
            !fitsMemoryBudget(engine.limits(), m_resource->counters().liveBytes, bytes))
            return;
        auto allocated = m_resource->allocate(bytes, 64u);
        if (!allocated.hasValue())
            return;
        m_data = *allocated;
        m_capacity = bytes;
    }

    /// The room taken, empty when none was.
    [[nodiscard]] std::span<std::byte> bytes() const noexcept
    {
        return {static_cast<std::byte*>(m_data), static_cast<std::size_t>(m_capacity)};
    }

private:
    MemoryResource* m_resource = nullptr;
    void* m_data = nullptr;
    std::uint64_t m_capacity = 0u;
};

/// A sink that writes into memory the caller holds, which the whole file must fit.
class StagedSink final : public openfits::output_sink
{
public:
    explicit StagedSink(std::span<std::byte> memory) noexcept : m_memory(memory) {}

    void write(std::span<const std::byte> data) override
    {
        if (data.size() > m_memory.size() - m_position)
            throw openfits::io_error(openfits::errc::write_failed, "the file is larger than the memory staging it");
        std::memcpy(m_memory.data() + m_position, data.data(), data.size());
        m_position += data.size();
    }

    [[nodiscard]] std::uint64_t position() const override
    {
        return m_position;
    }

    [[nodiscard]] bool can_rewrite() const override
    {
        return true;
    }

    void rewrite(std::uint64_t offset, std::span<const std::byte> data) override
    {
        if (offset > m_position || data.size() > m_position - offset)
            throw openfits::io_error(openfits::errc::write_failed, "a rewrite reaches past what was written");
        std::memcpy(m_memory.data() + offset, data.data(), data.size());
    }

private:
    std::span<std::byte> m_memory;
    std::size_t m_position = 0u;
};

bool managedKeyword(std::string_view key)
{
    return isStructuralFitsKeyword(key) || openfits::is_structural(key) || key == "ROWORDER" || key == "BAYERPAT" ||
           key == "XBAYROFF" || key == "YBAYROFF" || key == "SOFTWARE" || key == "PROGRAM";
}

bool ascii(std::string_view value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32u && c <= 126u; });
}

/// A quoted value's string: its quotes taken off and its doubled quotes undone.
std::string unquoted(std::string_view value)
{
    std::string text;
    for (std::size_t i = 1u; i + 1u < value.size(); ++i)
    {
        text += value[i];
        if (value[i] == '\'' && i + 2u < value.size() && value[i + 1u] == '\'')
            ++i;
    }
    return text;
}

/// A number's text with its exponent letter in upper case, the one form the standard writes.
std::string numberText(std::string_view value)
{
    std::string text(value);
    for (char& c : text)
        if (c == 'e' || c == 'd')
            c = static_cast<char>(c - 'a' + 'A');
    return text;
}

/// Adds one card of the set to the header the library writes.
void addCard(openfits::header& header, const CodecCard& card)
{
    const std::string& key = card.keyword();
    const std::string& value = card.value();
    if (key == "HISTORY" || key == "COMMENT")
    {
        const std::string& text = value.empty() ? card.comment() : value;
        if (key == "HISTORY")
            header.add_history(text);
        else
            header.add_comment(text);
        return;
    }
    if (value.empty())
    {
        header.append(openfits::card(key, openfits::undefined, card.comment()));
        return;
    }
    if (value == "T" || value == "F")
    {
        header.append(openfits::card::with_value_text(key, value, card.comment()));
        return;
    }
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
    {
        // A quoted numeric string stays a string; the library continues a long one over CONTINUE records.
        header.append(openfits::card(key, unquoted(value), card.comment()));
        return;
    }
    if (value.find('/') == std::string::npos && parseNumericCard(value).hasValue())
    {
        // Keeping the lexical value preserves exact integer cards and a real as its writer spelled it.
        openfits::card number = openfits::card::with_value_text(key, numberText(value), card.comment());
        if (number.value().kind() != openfits::value_kind::none)
        {
            header.append(std::move(number));
            return;
        }
    }
    // Any other text - a fraction, a word, a number no keyword value holds - is the string it spells.
    header.append(openfits::card(key, value, card.comment()));
}

Result<std::vector<CodecCard>> outputCards(const CodecEncodeRequest& request, const ImageMetadata& metadata)
{
    std::vector<CodecCard> cards;
    if (request.intent != nullptr)
    {
        auto projected = acquisitionCards({.acquisition = metadata.acquisition().toRequest()}, CodecNamespace::Fits);
        if (!projected.hasValue())
            return projected.status();
        cards = std::move(*projected);
    }
    const auto append = [&](const CodecCard& card, bool inherited) -> Status {
        if (card.origin() != CodecNamespace::Fits)
            return {};
        auto key = card.keyword();
        // FITS keyword lookup is case insensitive. Apply policy to the spelling the library writes, so a lowercase
        // structural keyword cannot bypass the reserved-card filter.
        for (auto& c : key)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        if (managedKeyword(key) || (inherited && isArtifactIdentityKeyword(key)))
            return {};
        auto normalized = CodecCard::create(CodecNamespace::Fits, key, card.value(), card.comment());
        if (!normalized.hasValue())
            return normalized.status();
        if (key != "HISTORY" && key != "COMMENT")
        {
            std::erase_if(cards, [&](const CodecCard& old) { return old.keyword() == key; });
        }
        cards.push_back(std::move(*normalized));
        return {};
    };
    if (request.intent != nullptr && request.intent->preserveSourceCards())
        for (const auto& card : metadata.codec().cards())
            if (const auto status = append(card, true); status.failed())
                return status;
    if (request.intent != nullptr)
        for (const auto& card : request.intent->cards())
            if (const auto status = append(card, false); status.failed())
                return status;
    if (cards.size() > kMaxCodecCards)
        return Status::failure(ErrorCode::LimitExceeded);
    for (const auto& card : cards)
    {
        if (card.keyword().size() > 60u || !ascii(card.keyword()) || !ascii(card.value()) || !ascii(card.comment()))
            return Status::failure(ErrorCode::InvalidArgument);
        const auto prefixBytes = card.keyword().size() <= 8u ? 10u : card.keyword().size() + 12u;
        const bool scalar = card.value().find('/') == std::string::npos && parseNumericCard(card.value()).hasValue() &&
                            !card.value().starts_with('\'');
        if (scalar && prefixBytes + card.value().size() > 80u)
            return Status::failure(ErrorCode::LimitExceeded);
    }
    return cards;
}

/// The BITPIX a sample representation is stored with.
openfits::bitpix storedBitpix(RawSampleFormat format) noexcept
{
    if (format.sampleClass == RawSampleClass::FloatingPoint)
        return format.sizeBytes == 4u ? openfits::bitpix::float32 : openfits::bitpix::float64;
    switch (format.sizeBytes)
    {
    case 1u:
        return openfits::bitpix::uint8;
    case 2u:
        return openfits::bitpix::int16;
    case 4u:
        return openfits::bitpix::int32;
    default:
        return openfits::bitpix::int64;
    }
}

/// The offset the standard stores a representation with, which its stored values are its values less: 32768 for
/// unsigned 16-bit samples, 2^31 and 2^63 for 32 and 64 bits, -128 for signed bytes, and zero for every other.
double storageOffset(RawSampleFormat format) noexcept
{
    if (format.sampleClass == RawSampleClass::UnsignedInteger)
    {
        switch (format.sizeBytes)
        {
        case 2u:
            return 32768.0;
        case 4u:
            return 2147483648.0;
        case 8u:
            return 9223372036854775808.0;
        default:
            return 0.0;
        }
    }
    if (format.sampleClass == RawSampleClass::SignedInteger && format.sizeBytes == 1u)
        return -128.0;
    return 0.0;
}

/// The pending file the transaction reserved, as a path, once it is known to be one this adapter may write.
///
/// Only an empty transaction reservation, or no file at all, may stand at the pending path. An existing payload or a
/// directory handed to the encoder is never written over or removed.
Result<std::filesystem::path> pendingFile(const CodecEncodeRequest& request)
{
    std::filesystem::path pending(
        std::u8string_view(reinterpret_cast<const char8_t*>(request.pendingPath.data()), request.pendingPath.size()));
    std::error_code pathError;
    const auto pendingStatus = std::filesystem::symlink_status(pending, pathError);
    if (pathError && pathError != std::errc::no_such_file_or_directory)
        return Status::failure(ErrorCode::ExternalFailure);
    pathError.clear();
    if (std::filesystem::exists(pendingStatus))
    {
        if (!std::filesystem::is_regular_file(pendingStatus) || std::filesystem::file_size(pending, pathError) != 0u ||
            pathError)
            return Status::failure(ErrorCode::InvalidState);
    }
    return pending;
}

/// Opens the pending file for writing from its start. A reservation was proved empty, so it is written in place:
/// truncating it again costs a measurable filesystem update for every frame of a sequence.
bool openPending(std::fstream& output, const std::filesystem::path& pending)
{
    output.open(pending, std::ios::binary | std::ios::in | std::ios::out);
    if (!output.is_open())
        output.open(pending, std::ios::binary | std::ios::out | std::ios::trunc);
    return output.is_open();
}
} // namespace

Result<CodecEncodeReport> writeFits(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata)
{
    const auto descriptor = *frame.descriptor();
    const auto layout = *frame.layout();
    const auto format = descriptor.sampleFormat();
    const auto type = fitsNumericType(format);
    const auto compression = request.intent != nullptr ? request.intent->compression() : OutputCompression::None;
    std::optional<openfits::compression> compressionType;
    switch (compression)
    {
    case OutputCompression::None:
        break;
    case OutputCompression::Rice:
        compressionType = openfits::compression::rice;
        break;
    case OutputCompression::Gzip:
        compressionType = openfits::compression::gzip_1;
        break;
    case OutputCompression::Plio:
        compressionType = openfits::compression::plio;
        break;
    case OutputCompression::Deflate:
    case OutputCompression::Lzw:
    case OutputCompression::Jpeg:
    case OutputCompression::Lz4:
    case OutputCompression::Lz4Hc:
    case OutputCompression::Zstd:
    default:
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Fits,
                            "encode FITS compression", request.pendingPath);
    }
    if (!type.has_value() || layout.samplesPerPixel() != 1u || statesPayloadForm(request.intent) ||
        (request.intent != nullptr && (request.intent->scaling() == OutputScaling::NormalizeFullRange ||
                                       request.intent->byteOrder() == ByteOrder::Little)) ||
        (compression != OutputCompression::None && format.sizeBytes == 8u &&
         format.sampleClass != RawSampleClass::FloatingPoint) ||
        ((compression == OutputCompression::Rice || compression == OutputCompression::Plio) &&
         format.sampleClass == RawSampleClass::FloatingPoint) ||
        (compression == OutputCompression::Plio && *type == openfits::numeric_type::uint32) ||
        (descriptor.cfa().isMosaiced() && cfaPatternBlockSize(descriptor.cfa().pattern()) != 2u))
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Fits,
                            "encode FITS options", request.pendingPath);
    const auto& extent = descriptor.extent();
    // The frame was described under whichever context made it, and a caller may hand it to this codec directly from
    // a narrower one; a frame this context cannot describe is refused here.
    if (!ImageExtent::create(extent.width(), extent.height(), extent.channelCount(), engine.limits()))
        return Status::failure(ErrorCode::LimitExceeded);
    auto cards = outputCards(request, metadata);
    if (!cards.hasValue())
        return cards.status();
    double scale = 1.0, zero = 0.0;
    const bool explicitScaling = request.intent != nullptr && request.intent->scaling() == OutputScaling::Explicit;
    const bool rawScaling = request.intent == nullptr && *type == openfits::numeric_type::int64 &&
                            metadata.codec().sourceEncoding().sampleFormat == format;
    if (explicitScaling)
    {
        // The intent's scale and offset apply to the frame's own values; the header states them for the stored
        // values, which are those values less the representation's own offset.
        scale = *request.intent->scale();
        zero = *request.intent->zeroOffset() + storageOffset(format) * scale;
    }
    else if (rawScaling)
    {
        scale = metadata.codec().sourceEncoding().scale.value_or(1.0);
        zero = metadata.codec().sourceEncoding().zeroOffset.value_or(0.0);
    }
    if (!std::isfinite(scale) || scale == 0.0 || !std::isfinite(zero))
        return Status::failure(ErrorCode::OutOfRange);
    const auto rowBytes = static_cast<std::uint64_t>(extent.width()) * format.sizeBytes;
    const auto planes = extent.channelCount();
    const bool swapped = format.sizeBytes > 1u && descriptor.byteOrder() != ByteOrder::Native &&
                         ((descriptor.byteOrder() == ByteOrder::Big) != (std::endian::native == std::endian::big));
    // PLIO is a positive 24-bit mask format. Refuse outside its domain before opening any file.
    if (compression == OutputCompression::Plio)
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
            for (std::uint32_t y = 0u; y < extent.height(); ++y)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const auto* row =
                    frame.bytes().data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes();
                for (std::uint32_t x = 0u; x < extent.width(); ++x)
                {
                    std::array<std::byte, 8> raw{};
                    std::memcpy(raw.data(), row + static_cast<std::size_t>(x) * format.sizeBytes, format.sizeBytes);
                    if (swapped)
                        std::reverse(raw.begin(), raw.begin() + format.sizeBytes);
                    std::uint64_t value = 0u;
                    std::memcpy(&value, raw.data(), format.sizeBytes);
                    const auto sign = std::uint64_t{1} << (format.sizeBytes * 8u - 1u);
                    if (value > 16777215u ||
                        (format.sampleClass == RawSampleClass::SignedInteger && (value & sign) != 0u))
                        return Status::failure(ErrorCode::OutOfRange);
                }
            }
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    const auto pending = pendingFile(request);
    if (!pending.hasValue())
        return pending.status();

    // A frame of contiguous rows and planes, aligned for its samples and in native byte order, is lent as it is.
    const std::uint64_t payloadBytes = rowBytes * extent.height() * planes;
    const bool lent = !swapped && reinterpret_cast<std::uintptr_t>(frame.bytes().data()) % format.sizeBytes == 0u &&
                      layout.rowStrideBytes() == rowBytes &&
                      (planes == 1u || layout.planeStrideBytes() == rowBytes * extent.height()) &&
                      frame.bytes().size() >= payloadBytes;
    auto encoded = evaluateAcrossBoundary(kFitsBoundary, [&]() -> Result<std::uint64_t> {
        try
        {
            openfits::image_info image;
            image.geometry.axes = planes == 1u ? std::vector<std::uint64_t>{extent.width(), extent.height()}
                                               : std::vector<std::uint64_t>{extent.width(), extent.height(), planes};
            for (const CodecCard& card : *cards)
                addCard(image.header, card);
            image.header.set("SOFTWARE", kEngineVersionString);
            image.header.set("PROGRAM", "AfterNight");
            image.header.set("ROWORDER",
                             descriptor.coordinateOrigin() == CoordinateOrigin::BottomLeft ? "BOTTOM-UP" : "TOP-DOWN");
            if (descriptor.cfa().isMosaiced())
            {
                std::string pattern;
                for (std::uint32_t y = 0u; y < 2u; ++y)
                    for (std::uint32_t x = 0u; x < 2u; ++x)
                    {
                        const auto color = descriptor.cfa().colorAt(x, y);
                        pattern += color == CfaColor::Red ? 'R' : color == CfaColor::Blue ? 'B' : 'G';
                    }
                image.header.set("BAYERPAT", pattern);
            }
            openfits::image_write_options imageOptions;
            if (explicitScaling || rawScaling)
            {
                // The header states the scaling before any sample is written; the samples are stored as the frame's
                // values less the representation's own offset.
                image.bitpix = storedBitpix(format);
                image.scaling = {.scale = scale, .zero = zero};
                const double offset = storageOffset(format);
                if (offset != 0.0)
                {
                    imageOptions.scaling = openfits::scaling_policy::override;
                    imageOptions.override_scaling = {.scale = 1.0, .zero = offset};
                }
                else
                    imageOptions.scaling = openfits::scaling_policy::raw;
            }
            if (compressionType.has_value())
            {
                // One row a tile, as the files of this writer have always been tiled; a floating-point image is kept
                // as its values, never quantized.
                openfits::compression_options packed;
                packed.type = *compressionType;
                packed.tile.assign(image.geometry.axes.size(), 1u);
                packed.tile[0] = extent.width();
                imageOptions.compression = std::move(packed);
            }
            openfits::writer writer;
            if (lent)
            {
                (void)writer.add_image(std::move(image), *type,
                                       frame.bytes().first(static_cast<std::size_t>(payloadBytes)),
                                       std::move(imageOptions));
            }
            else
            {
                // The library asks for runs of whole rows, or of part of a row when one row is larger than its
                // staging, in the order of the data; each run is copied out of the frame where its rows lie. The
                // function is called by the save below, after this block has ended, so it holds its own copies of
                // what it reads.
                const std::uint64_t width = extent.width();
                const std::uint64_t height = extent.height();
                const std::size_t sampleBytes = format.sizeBytes;
                const std::byte* const source = frame.bytes().data();
                const std::uint64_t rowStride = layout.rowStrideBytes();
                const std::uint64_t planeStride = layout.planeStrideBytes();
                (void)writer.add_image(
                    std::move(image), *type,
                    [width, height, sampleBytes, source, rowStride, planeStride, swapped](std::uint64_t first,
                                                                                          std::span<std::byte> values) {
                        std::uint64_t element = first;
                        std::size_t offset = 0u;
                        while (offset < values.size())
                        {
                            const std::uint64_t plane = element / (width * height);
                            const std::uint64_t y = (element / width) % height;
                            const std::uint64_t x = element % width;
                            const std::uint64_t run = (std::min)(width - x, (values.size() - offset) / sampleBytes);
                            const std::byte* const from =
                                source + plane * planeStride + y * rowStride + x * sampleBytes;
                            std::byte* const into = values.data() + offset;
                            std::memcpy(into, from, static_cast<std::size_t>(run * sampleBytes));
                            if (swapped)
                                // In 64 bits: a run may pass four gibibytes under raised limits, and a wrapped offset
                                // would reverse a range whose end precedes its start.
                                for (std::uint64_t sample = 0u; sample < run; ++sample)
                                    std::reverse(into + sample * sampleBytes, into + (sample + 1u) * sampleBytes);
                            offset += static_cast<std::size_t>(run * sampleBytes);
                            element += run;
                        }
                    },
                    std::move(imageOptions));
            }
            openfits::write_options options;
            // The conventions the cards of a header in use need, each written the way CFITSIO wrote it: a keyword
            // longer than eight characters as HIERARCH, a long string - a reserved keyword's too - continued over
            // CONTINUE records, and a comment cut where its record ends.
            options.hierarch = true;
            options.truncate_comments = true;
            options.continue_reserved_keywords = true;
            options.progress = [&execution](std::uint64_t done, std::uint64_t total) {
                execution.reportProgress(done, total, "encode FITS");
                return !execution.checkCancellation().failed();
            };
            StagedFile staged;
            if (!compressionType.has_value() && payloadBytes <= kStagedPayloadBytes)
            {
                const std::uint64_t size = writer.size(options);
                if (size >= payloadBytes && size - payloadBytes <= kStagedHeaderBytes)
                    staged.prepare(engine, size);
            }
            std::fstream output;
            if (!openPending(output, *pending))
                return fitsFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable,
                                   "create FITS pending output", request.pendingPath,
                                   "the pending output path could not be opened for writing");
            std::uint64_t written = 0u;
            if (!staged.bytes().empty())
            {
                StagedSink sink(staged.bytes());
                writer.save(sink, options);
                written = sink.position();
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                output.write(reinterpret_cast<const char*>(staged.bytes().data()),
                             static_cast<std::streamsize>(written));
            }
            else
            {
                // The engine's transaction owns the pending name and the one rename that publishes it, so the file is
                // written into that name as it stands.
                openfits::ostream_sink sink(output);
                writer.save(sink, options);
                written = sink.position();
            }
            output.close();
            if (output.fail())
                return fitsFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "write FITS output",
                                   request.pendingPath, "the pending FITS payload could not be written in full");
            return written;
        }
        catch (const openfits::cancelled_error&)
        {
            auto stopped = execution.checkCancellation();
            return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
        }
        catch (const openfits::error& error)
        {
            return fitsLibraryFailure(error, FitsStage::Write, "encode FITS image", request.pendingPath);
        }
    });
    if (!encoded.hasValue())
        return codecFailure(encoded.status().code(), CodecFailure::CallbackFailure, CodecFormat::Fits,
                            "encode FITS image", request.pendingPath, encoded.status());
    if (!encoded->hasValue())
        return encoded->status();
    return CodecEncodeReport{**encoded};
}
} // namespace an
