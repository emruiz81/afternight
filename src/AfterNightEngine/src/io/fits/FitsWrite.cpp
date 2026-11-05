// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - encoding a FITS image into a pending path.
//
// Every option, card and sample value is checked before the file is created. Rows go to CFITSIO
// directly when they are aligned and in native byte order, and through one row of engine-budgeted
// workspace otherwise. The card set is the output intent's: the acquisition cards, then the source's
// own cards when the intent preserves them, then the intent's cards, each replacing an earlier card
// of the same keyword. The structural cards and the ones this writer states itself are taken from
// neither, and an identity card only from the intent. A small uncompressed file is built in memory
// and written in one transfer when the engine context's budget allows, so the disk sees one
// completed write rather than CFITSIO's delete, create and seek cycle.

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
/// Bounded optional staging for a small uncompressed container. CFITSIO still writes every
/// header and sample; the disk sees one completed write instead of a delete/create/seek cycle.
/// A tight memory budget simply retains the ordinary disk-backed implementation.
class SmallFitsOutput
{
public:
    SmallFitsOutput() = default;
    SmallFitsOutput(const SmallFitsOutput&) = delete;
    SmallFitsOutput& operator=(const SmallFitsOutput&) = delete;
    ~SmallFitsOutput()
    {
        if (data != nullptr)
            m_resource->deallocate(data, m_capacity, 64u);
    }
    void prepare(const EngineContext& engine, std::uint64_t payload, std::span<const CodecCard> cards)
    {
        constexpr std::uint64_t kMaxPayload = 4u * 1024u * 1024u;
        constexpr std::uint64_t kHeaderCapacity = 65536u;
        if (payload > kMaxPayload)
            return;
        std::uint64_t headerBound = 8192u;
        for (const auto& card : cards)
        {
            // Allow escaping, continuation markers and header padding in addition to the
            // structural cards and the encoder's reserved headroom.
            headerBound +=
                160u + 160u * ((card.keyword().size() + 2u * card.value().size() + card.comment().size() + 59u) / 60u);
            if (headerBound > kHeaderCapacity)
                return;
        }
        m_resource = engine.memoryResource();
        if (m_resource == nullptr)
            return;
        m_capacity = ((payload + kHeaderCapacity + 2879u) / 2880u) * 2880u;
        if (!fitsMemoryBudget(engine.limits(), m_resource->counters().liveBytes, m_capacity))
            return;
        auto allocated = m_resource->allocate(m_capacity, 64u);
        if (!allocated.hasValue())
            return;
        data = *allocated;
        size = static_cast<std::size_t>(m_capacity);
        std::memset(data, 0, size);
    }
    void* data = nullptr;
    std::size_t size = 0u;

private:
    MemoryResource* m_resource = nullptr;
    std::uint64_t m_capacity = 0u;
};

bool managedKeyword(std::string_view key)
{
    return isStructuralFitsKeyword(key) || key == "ROWORDER" || key == "BAYERPAT" || key == "XBAYROFF" ||
           key == "YBAYROFF" || key == "SOFTWARE" || key == "PROGRAM";
}

bool ascii(std::string_view value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32u && c <= 126u; });
}

Status writeCard(fitsfile* file, const CodecCard& card, std::string_view path)
{
    int status = 0;
    const auto& key = card.keyword();
    const auto& value = card.value();
    if (key == "HISTORY" || key == "COMMENT")
    {
        const auto& text = value.empty() ? card.comment() : value;
        if (key == "HISTORY")
            fits_write_history(file, text.c_str(), &status);
        else
            fits_write_comment(file, text.c_str(), &status);
    }
    else if (key == "CONTINUE")
    {
        const auto raw = "CONTINUE  " + value + (card.comment().empty() ? "" : " / " + card.comment());
        fits_write_record(file, raw.c_str(), &status);
    }
    else
    {
        const bool quoted = value.size() >= 2u && value.front() == '\'' && value.back() == '\'';
        const bool numeric = !quoted && value.find('/') == std::string::npos && parseNumericCard(value).hasValue();
        const auto prefix = key.size() <= 8u ? key + std::string(8u - key.size(), ' ') : "HIERARCH " + key + " ";
        if (value == "T" || value == "F" || numeric || value.empty() ||
            (quoted && prefix.size() + 2u + value.size() <= 80u))
        {
            // Keeping the lexical value preserves quoted numeric strings and exact integer cards.
            const auto raw = prefix + "= " + value + (card.comment().empty() ? "" : " / " + card.comment());
            fits_update_card(file, key.c_str(), raw.c_str(), &status);
        }
        else
        {
            std::string logical = value;
            if (quoted)
            {
                logical.clear();
                for (std::size_t i = 1u; i + 1u < value.size(); ++i)
                {
                    logical += value[i];
                    if (value[i] == '\'' && i + 2u < value.size() && value[i + 1u] == '\'')
                        ++i;
                }
            }
            fits_update_key_longstr(file, key.c_str(), logical.c_str(), card.comment().c_str(), &status);
        }
    }
    return status == 0 ? Status{} : fitsFailure(status, "write FITS card", path, true);
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
        // FITS keyword lookup is case insensitive. Apply policy to the same spelling CFITSIO
        // will use, so a lowercase structural keyword cannot bypass the reserved-card filter.
        for (auto& c : key)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        if (managedKeyword(key) || (inherited && isArtifactIdentityKeyword(key)))
            return {};
        auto normalized = CodecCard::create(CodecNamespace::Fits, key, card.value(), card.comment());
        if (!normalized.hasValue())
            return normalized.status();
        if (key != "HISTORY" && key != "COMMENT" && key != "CONTINUE")
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
        if ((scalar || card.keyword() == "CONTINUE") && prefixBytes + card.value().size() > 80u)
            return Status::failure(ErrorCode::LimitExceeded);
    }
    return cards;
}
} // namespace

Result<CodecEncodeReport> writeFits(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata)
{
    const auto descriptor = *frame.descriptor();
    const auto layout = *frame.layout();
    const auto format = descriptor.sampleFormat();
    const int imageType = fitsImageType(format), datatype = fitsDatatype(format);
    const auto compression = request.intent != nullptr ? request.intent->compression() : OutputCompression::None;
    int compressionType = 0;
    switch (compression)
    {
    case OutputCompression::None:
        break;
    case OutputCompression::Rice:
        compressionType = RICE_1;
        break;
    case OutputCompression::Gzip:
        compressionType = GZIP_1;
        break;
    case OutputCompression::Plio:
        compressionType = PLIO_1;
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
    if (imageType == 0 || layout.samplesPerPixel() != 1u || request.pendingPath.size() + 2u >= FLEN_FILENAME ||
        statesPayloadForm(request.intent) ||
        (request.intent != nullptr && (request.intent->scaling() == OutputScaling::NormalizeFullRange ||
                                       request.intent->byteOrder() == ByteOrder::Little)) ||
        (compression != OutputCompression::None && format.sizeBytes == 8u &&
         format.sampleClass != RawSampleClass::FloatingPoint) ||
        ((compression == OutputCompression::Rice || compression == OutputCompression::Plio) &&
         format.sampleClass == RawSampleClass::FloatingPoint) ||
        (compression == OutputCompression::Plio && datatype == TUINT) ||
        (descriptor.cfa().isMosaiced() && cfaPatternBlockSize(descriptor.cfa().pattern()) != 2u))
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Fits,
                            "encode FITS options", request.pendingPath);
    auto cards = outputCards(request, metadata);
    if (!cards.hasValue())
        return cards.status();
    double scale = 1.0, zero = 0.0;
    const bool explicitScaling = request.intent != nullptr && request.intent->scaling() == OutputScaling::Explicit;
    const bool rawScaling =
        request.intent == nullptr && datatype == TLONGLONG && metadata.codec().sourceEncoding().sampleFormat == format;
    if (explicitScaling)
    {
        scale = *request.intent->scale();
        zero = *request.intent->zeroOffset();
        if (datatype == TSBYTE)
            zero -= 128.0 * scale;
        if (datatype == TUSHORT)
            zero += 32768.0 * scale;
        if (datatype == TUINT)
            zero += 2147483648.0 * scale;
    }
    else if (rawScaling)
    {
        scale = metadata.codec().sourceEncoding().scale.value_or(1.0);
        zero = metadata.codec().sourceEncoding().zeroOffset.value_or(0.0);
    }
    if (!std::isfinite(scale) || scale == 0.0 || !std::isfinite(zero))
        return Status::failure(ErrorCode::OutOfRange);
    const auto& extent = descriptor.extent();
    const auto rowBytes = static_cast<std::uint64_t>(extent.width()) * format.sizeBytes;
    const bool swapped = format.sizeBytes > 1u && descriptor.byteOrder() != ByteOrder::Native &&
                         ((descriptor.byteOrder() == ByteOrder::Big) != (std::endian::native == std::endian::big));
    const bool misaligned = reinterpret_cast<std::uintptr_t>(frame.bytes().data()) % format.sizeBytes != 0u ||
                            layout.rowStrideBytes() % format.sizeBytes != 0u ||
                            layout.planeStrideBytes() % format.sizeBytes != 0u;
    std::optional<SampleBuffer> scratch;
    if (swapped || misaligned)
    {
        // The frame was described under whichever context made it, and a caller may hand it to this
        // codec directly from a narrower one; a row this context cannot describe is refused here.
        const auto scratchExtent = ImageExtent::create(extent.width(), 1u, 1u, engine.limits());
        if (!scratchExtent)
            return Status::failure(ErrorCode::LimitExceeded);
        const auto scratchLayout = ImageLayout::create({*scratchExtent, format.sizeBytes, 1u, 1u, 1u}, engine.limits());
        if (!scratchLayout)
            return Status::failure(ErrorCode::LimitExceeded);
        auto allocated = SampleBuffer::createOwned(engine, *scratchLayout, StorageInitialization::Uninitialized);
        if (!allocated.hasValue())
            return allocated.status();
        scratch = std::move(*allocated);
    }
    // PLIO is a positive 24-bit mask format. Refuse outside its domain before opening any file.
    if (compression == OutputCompression::Plio)
        for (std::uint32_t plane = 0u; plane < extent.channelCount(); ++plane)
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
    fitsfile* rawFile = nullptr;
    int status = 0;
    SmallFitsOutput buffered;
    if (compression == OutputCompression::None)
        buffered.prepare(engine, rowBytes * extent.height() * extent.channelCount(), *cards);
    // Disk-file creation deliberately disables CFITSIO's extended filename parser, including its
    // overwrite prefix. Release only an empty transaction reservation before creating the literal
    // pending name. Never remove an existing payload or a directory handed to the encoder.
    const std::filesystem::path pending(
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
        if (buffered.data == nullptr && (!std::filesystem::remove(pending, pathError) || pathError))
            return Status::failure(ErrorCode::ExternalFailure);
    }
    if (pathError)
        return Status::failure(ErrorCode::ExternalFailure);
    if (buffered.data != nullptr)
        fits_create_memfile(&rawFile, &buffered.data, &buffered.size, 0u, nullptr, &status);
    else
        fits_create_diskfile(&rawFile, fitsPath(request.pendingPath).c_str(), &status);
    FitsHandle file(rawFile);
    if (status != 0)
        return fitsFailure(status, "create FITS pending output", request.pendingPath, true);
    if (compression != OutputCompression::None)
    {
        fits_set_compression_type(file.get(), compressionType, &status);
        if (format.sampleClass == RawSampleClass::FloatingPoint)
            fits_set_quantize_level(file.get(), 0.0f, &status);
        long tiles[3] = {static_cast<long>(extent.width()), 1, 1};
        fits_set_tile_dim(file.get(), 3, tiles, &status);
    }
    LONGLONG axes[3] = {extent.width(), extent.height(), extent.channelCount()};
    fits_create_imgll(file.get(), imageType, extent.channelCount() == 1u ? 2 : 3, axes, &status);
    if (status != 0)
        return fitsFailure(status, "create FITS image header", request.pendingPath, true);
    for (const auto& card : *cards)
        if (const auto written = writeCard(file.get(), card, request.pendingPath); written.failed())
            return written;
    fits_update_key_str(file.get(), "SOFTWARE", kEngineVersionString, nullptr, &status);
    fits_update_key_str(file.get(), "PROGRAM", "AfterNight", nullptr, &status);
    fits_update_key_str(file.get(), "ROWORDER",
                        descriptor.coordinateOrigin() == CoordinateOrigin::BottomLeft ? "BOTTOM-UP" : "TOP-DOWN",
                        nullptr, &status);
    if (descriptor.cfa().isMosaiced())
    {
        std::string pattern;
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t x = 0u; x < 2u; ++x)
            {
                const auto color = descriptor.cfa().colorAt(x, y);
                pattern += color == CfaColor::Red ? 'R' : color == CfaColor::Blue ? 'B' : 'G';
            }
        fits_update_key_str(file.get(), "BAYERPAT", pattern.c_str(), nullptr, &status);
    }
    if (status != 0)
        return fitsFailure(status, "write FITS header", request.pendingPath, true);
    int headerCards = 0;
    fits_get_hdrspace(file.get(), &headerCards, nullptr, &status);
    if (status != 0)
        return fitsFailure(status, "check FITS header size", request.pendingPath, true);
    if (headerCards > static_cast<int>(kMaxCodecCards) - 2)
        return Status::failure(ErrorCode::LimitExceeded);
    // Reserve the scaling cards after CFITSIO has expanded long strings, HISTORY and COMMENT into
    // their actual records, and before any payload exists. Appending BSCALE/BZERO after the pixels
    // without this space makes CFITSIO relocate the complete data unit when the header block is full.
    fits_set_hdrsize(file.get(), explicitScaling || rawScaling ? 2 : 0, &status);
    if (status != 0)
        return fitsFailure(status, "reserve FITS scaling cards", request.pendingPath, true);
    if (compression == OutputCompression::Plio)
    {
        // CFITSIO 4.6.4 budgets four bytes per PLIO pixel, omitting the seven-short
        // header and the worst-case three shorts per pixel. Finish the header scan
        // before correcting this private handle's tile bound; no file bytes change.
        const auto plioBytes = 14u + 6ull * extent.width();
        if (plioBytes > static_cast<std::uint64_t>((std::numeric_limits<int>::max)()))
            return Status::failure(ErrorCode::LimitExceeded);
        fits_set_hdustruc(file.get(), &status);
        if (status != 0)
            return fitsFailure(status, "prepare FITS PLIO buffer", request.pendingPath, true);
        file->Fptr->maxelem = (std::max)(file->Fptr->maxelem, static_cast<long>(plioBytes));
    }
    for (std::uint32_t plane = 0u; plane < extent.channelCount(); ++plane)
    {
        std::uint32_t y = 0u;
        while (y < extent.height())
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            auto* data = const_cast<std::byte*>(frame.bytes().data() + plane * layout.planeStrideBytes() +
                                                y * layout.rowStrideBytes());
            const auto rows =
                !scratch && layout.rowStrideBytes() == rowBytes
                    ? (std::min)(extent.height() - y, (std::max)(1u, 4194304u / format.sizeBytes / extent.width()))
                    : 1u;
            if (scratch)
            {
                auto target = *scratch->mutableBytes();
                std::memcpy(target.data(), data, rowBytes);
                if (swapped)
                    // In 64 bits: a row may pass four gibibytes under raised limits, and a wrapped
                    // offset would reverse a range whose end precedes its start.
                    for (std::size_t x = 0u; x < extent.width(); ++x)
                        std::reverse(target.begin() + x * format.sizeBytes,
                                     target.begin() + (x + 1u) * format.sizeBytes);
                data = target.data();
            }
            const auto first = static_cast<LONGLONG>(plane) * extent.width() * extent.height() +
                               static_cast<LONGLONG>(y) * extent.width() + 1;
            fits_write_img(file.get(), datatype, first, static_cast<LONGLONG>(extent.width()) * rows, data, &status);
            if (status != 0)
                return fitsFailure(status, "write FITS pixels", request.pendingPath, true);
            y += rows;
            execution.reportProgress(static_cast<std::uint64_t>(plane) * extent.height() + y,
                                     static_cast<std::uint64_t>(extent.channelCount()) * extent.height(),
                                     "encode FITS");
        }
    }
    if (explicitScaling || rawScaling)
    {
        // CFITSIO's generic TDOUBLE keyword writer uses too few digits to round-trip a
        // binary64 scale or the unsigned-64 offset. State sufficient precision explicitly.
        fits_update_key_dbl(file.get(), "BSCALE", scale, 17, nullptr, &status);
        fits_update_key_dbl(file.get(), "BZERO", zero, 17, nullptr, &status);
    }
    if (status != 0)
        return fitsFailure(status, "write FITS scaling", request.pendingPath, true);
    // An ordinary image's complete padded extent is already known to CFITSIO. The transaction
    // independently verifies that reported extent on disk after close. Compressed heap size is
    // finalized by close, so that case still obtains its byte count from the completed file.
    LONGLONG serializedBytes = 0;
    if (compression == OutputCompression::None)
        fits_get_hduaddrll(file.get(), nullptr, nullptr, &serializedBytes, &status);
    if (status != 0)
        return fitsFailure(status, "measure FITS output", request.pendingPath, true);
    fits_close_file(file.release(), &status);
    if (status != 0)
        return fitsFailure(status, "close FITS output", request.pendingPath, true);
    if (buffered.data != nullptr)
    {
        if (serializedBytes <= 0 || static_cast<std::uint64_t>(serializedBytes) > buffered.size)
            return Status::failure(ErrorCode::Internal);
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        // A pending reservation was proved above to be an empty regular file, so it is written in place:
        // truncating it again costs a measurable filesystem update for every frame of a sequence.
        std::fstream output(pending, std::ios::binary | std::ios::in | std::ios::out);
        if (!output.is_open())
            output.open(pending, std::ios::binary | std::ios::out | std::ios::trunc);
        output.write(static_cast<const char*>(buffered.data), static_cast<std::streamsize>(serializedBytes));
        output.close();
        if (!output)
            return codecFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, CodecFormat::Fits,
                                "write FITS pending output", request.pendingPath);
    }
    if (compression == OutputCompression::None && serializedBytes > 0)
        return CodecEncodeReport{static_cast<std::uint64_t>(serializedBytes)};
    std::error_code error;
    const auto size = std::filesystem::file_size(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(request.pendingPath.data()),
                                                 request.pendingPath.size())),
        error);
    if (error)
        return Status::failure(ErrorCode::ExternalFailure);
    return CodecEncodeReport{size};
}
} // namespace an
