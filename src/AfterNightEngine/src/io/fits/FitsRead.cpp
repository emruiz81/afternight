// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - decoding a FITS image's pixels, whole or by region.
//
// The frame is allocated once and every strip is read straight into it: a full-width region in
// strips of up to 4 MiB, any other region a row at a time, one plane after another. A BITPIX=64
// image is read as raw 64-bit codes into the same memory, and each code is checked against the
// policy before it is converted to Float64 and scaled in place, so no raw code is ever scaled first.
// The strips of a large uncompressed payload are independent, so an operation granted more than one
// worker reads them on its workers, each worker through a handle of its own: a handle opened for
// synchronous reads serves one request at a time, whatever offsets they name.

#include "io/fits/FitsFile.h"
#include "io/fits/FitsPayloadFile.h"

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

namespace an
{
namespace
{
void readCompressedPixels(fitsfile* file, int datatype, LONGLONG* first, LONGLONG count, void* destination,
                          int* anyNull, int* status)
{
    // CFITSIO dereferences nulval as the requested C type even when substitution is disabled.
    const auto read = [&](auto nullValue) {
        fits_read_pixll(file, datatype, first, count, &nullValue, destination, anyNull, status);
    };
    switch (datatype)
    {
    case TBYTE:
        read(static_cast<unsigned char>(0));
        break;
    case TSBYTE:
        read(static_cast<signed char>(0));
        break;
    case TUSHORT:
        read(static_cast<unsigned short>(0));
        break;
    case TSHORT:
        read(static_cast<short>(0));
        break;
    case TUINT:
        read(0u);
        break;
    case TINT:
        read(0);
        break;
    case TULONGLONG:
        read(static_cast<ULONGLONG>(0));
        break;
    case TLONGLONG:
        read(static_cast<LONGLONG>(0));
        break;
    case TFLOAT:
        read(0.0f);
        break;
    case TDOUBLE:
        read(0.0);
        break;
    default:
        *status = BAD_DATATYPE;
        break;
    }
}
} // namespace

Result<CodecDecodeResult> readFits(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, const CodecRegion* region,
                                   FitsInteger64Policy integer64)
{
    auto file = openFits(request.path);
    if (!file.hasValue())
        return file.status();
    auto headerRequest = request;
    headerRequest.validatePayload = false;
    auto header = readFitsHeader(file->get(), engine, execution, headerRequest, integer64, false);
    if (!header.hasValue())
        return header.status();
    return readPreparedFits(engine, execution, request, region, integer64, file->get(), std::move(*header));
}

namespace
{

/// The fewest payload bytes a read shares between workers: below it one worker reads the payload sooner than several
/// open their handles.
constexpr std::uint64_t kSharedPayloadBytes = std::uint64_t{32u} << 20u;

/// The fewest workers an operation is granted before its read shares them. A read granted fewer is one of several
/// operations running side by side - a stage's reader beside its computing operation - and the memory bandwidth more
/// readers would take is what the operation beside it is waiting on.
constexpr std::uint32_t kSharedPayloadWorkers = 8u;

/// One whole-row strip of a payload: its plane, its first row inside the region and how many rows it holds.
struct PayloadStrip
{
    std::uint32_t plane = 0u; ///< The plane.
    std::uint32_t y = 0u; ///< The strip's first row, counted from the region's top.
    std::uint32_t rows = 0u; ///< How many rows the strip holds.
};

/// Reads every whole-row strip of an uncompressed payload into place and converts it, on the operation's workers.
///
/// Each worker reads through a handle of its own, opened when it takes its first strip. Progress counts rows in the
/// order strips finish, under one lock, so it never moves back; a failure is the first in strip order.
///
/// @return success, the operation's stop, or the first strip's failure.
[[nodiscard]] Status readPayloadStripsShared(const ExecutionContext& execution, const CodecReadRequest& request,
                                             const CodecRegion& area, const ImageExtent& sourceExtent,
                                             const FitsPayloadTransform& transform, std::uint64_t dataStart,
                                             std::uint32_t stripRows, std::byte* samples, const ImageLayout& layout)
{
    std::vector<PayloadStrip> strips;
    for (std::uint32_t plane = 0u; plane < sourceExtent.channelCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < area.height; y += stripRows)
        {
            strips.push_back(PayloadStrip{plane, y, (std::min)(area.height - y, stripRows)});
        }
    }
    const std::uint64_t totalRows = static_cast<std::uint64_t>(area.height) * sourceExtent.channelCount();
    std::mutex progressMutex;
    std::uint64_t completed = 0u;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(stripRows) * area.width * transform.sampleBytes,
                        .operationsPerItem = static_cast<std::uint64_t>(stripRows) * area.width};
    std::vector<FitsPayloadFile> handles(plannedWorkerCount(execution, strips.size(), cost));
    return parallelForWorkers(execution, strips.size(), cost, [&](WorkerTask task) -> Status {
        FitsPayloadFile& handle = handles[task.workerSlot];
        if (!handle.isOpen())
        {
            if (const Status opened = handle.open(request.path); opened.failed())
                return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                    "decode FITS pixels", request.path, opened);
        }
        for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const PayloadStrip& strip = strips[static_cast<std::size_t>(index)];
            const std::uint64_t firstSample =
                (static_cast<std::uint64_t>(strip.plane) * sourceExtent.height() + area.y + strip.y) *
                sourceExtent.width();
            const std::uint64_t count = static_cast<std::uint64_t>(area.width) * strip.rows;
            std::byte* const destination =
                samples + strip.plane * layout.planeStrideBytes() + strip.y * layout.rowStrideBytes();
            const std::span<std::byte> bytes(destination, static_cast<std::size_t>(count) * transform.sampleBytes);
            if (const auto read = handle.readAt(bytes, dataStart + firstSample * transform.sampleBytes); read.failed())
                return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                    "decode FITS pixels", request.path, read);
            convertFitsPayload(destination, count, transform);
            const std::lock_guard lock(progressMutex);
            completed += strip.rows;
            execution.reportProgress(completed, totalRows, "decode FITS");
        }
        return Status::success();
    });
}

} // namespace

Status readFitsPixels(const ExecutionContext& execution, const CodecReadRequest& request, const CodecRegion& area,
                      FitsInteger64Policy integer64, fitsfile* file, const FitsHeader& header, std::byte* samples,
                      const ImageLayout& layout, FitsPayloadFile& payloadFile)
{
    const ImageExtent& sourceExtent = header.info.descriptor.extent();
    const std::uint32_t sampleBytes = header.info.descriptor.sampleFormat().sizeBytes;
    int status = 0;
    const bool integerImage = header.bitpix == LONGLONG_IMG;
    // Whole rows of an image the library would only copy or flip are read from the payload itself,
    // one strip per read, and converted in place.
    const bool wholeRows = area.x == 0u && area.width == sourceExtent.width();
    const std::optional<FitsPayloadTransform> payload = wholeRows ? fitsPayloadTransform(header) : std::nullopt;
    std::uint64_t dataStart = 0u;
    if (payload.has_value())
    {
        LONGLONG headerStart = 0;
        LONGLONG dataOffset = 0;
        LONGLONG dataEnd = 0;
        fits_get_hduaddrll(file, &headerStart, &dataOffset, &dataEnd, &status);
        if (status != 0)
            return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                "decode FITS pixels", request.path,
                                fitsFailure(status, "locate FITS pixels", request.path));
        dataStart = static_cast<std::uint64_t>(dataOffset);
        const std::uint64_t payloadBytes =
            static_cast<std::uint64_t>(area.width) * area.height * sourceExtent.channelCount() * sampleBytes;
        if (execution.admittedConcurrency() >= kSharedPayloadWorkers && payloadBytes >= kSharedPayloadBytes)
        {
            return readPayloadStripsShared(execution, request, area, sourceExtent, *payload, dataStart,
                                           (std::max)(1u, 4194304u / sampleBytes / area.width), samples, layout);
        }
        if (!payloadFile.isOpen())
        {
            if (const auto opened = payloadFile.open(request.path); opened.failed())
                return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                    "decode FITS pixels", request.path, opened);
        }
    }
    if (integerImage)
        fits_set_bscale(file, 1.0, 0.0, &status);
    // CFITSIO's tile decoder evaluates its integer null sentinel even when null checking is off.
    // Initialize the otherwise absent sentinel in this private handle; no header card or validity
    // claim is added, and a zero null argument below explicitly disables tile substitution.
    if (header.compressed && file->Fptr->cn_zblank == 0)
    {
        file->Fptr->zblank = 0;
        file->Fptr->cn_zblank = -1;
    }
    // Read strips directly into the destination. The 64-bit path uses that same memory for raw codes
    // and converts in place only after checking the integer, before any scaling.
    const auto totalRows = static_cast<std::uint64_t>(area.height) * sourceExtent.channelCount();
    std::uint64_t completed = 0u;
    for (std::uint32_t plane = 0u; plane < sourceExtent.channelCount(); ++plane)
    {
        std::uint32_t y = 0u;
        while (y < area.height)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto rows =
                wholeRows ? (std::min)(area.height - y, (std::max)(1u, 4194304u / sampleBytes / area.width)) : 1u;
            std::array<LONGLONG, 9> first{};
            first.fill(1);
            first[0] = static_cast<LONGLONG>(area.x) + 1;
            first[1] = static_cast<LONGLONG>(area.y) + y + 1;
            first[2] = static_cast<LONGLONG>(plane) + 1;
            auto* destination = samples + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes();
            const auto count = static_cast<LONGLONG>(area.width) * rows;
            int anyNull = 0;
            const auto datatype = integerImage ? TLONGLONG : header.datatype;
            if (payload.has_value())
            {
                const std::uint64_t firstSample =
                    (static_cast<std::uint64_t>(plane) * sourceExtent.height() + area.y + y) * sourceExtent.width();
                const std::span<std::byte> strip(destination, static_cast<std::size_t>(count) * payload->sampleBytes);
                if (const auto read = payloadFile.readAt(strip, dataStart + firstSample * payload->sampleBytes);
                    read.failed())
                    return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                        "decode FITS pixels", request.path, read);
                convertFitsPayload(destination, static_cast<std::uint64_t>(count), *payload);
            }
            else if (header.compressed)
                readCompressedPixels(file, datatype, first.data(), count, destination, &anyNull, &status);
            else
                fits_read_pixll(file, datatype, first.data(), count, nullptr, destination, &anyNull, &status);
            if (status != 0)
                return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                    "decode FITS pixels", request.path,
                                    fitsFailure(status, "read FITS pixels", request.path));
            if (integerImage && integer64 != FitsInteger64Policy::PreserveRaw)
            {
                for (LONGLONG i = 0; i < count; ++i)
                {
                    std::int64_t raw = 0;
                    std::memcpy(&raw, destination + i * 8, 8u);
                    constexpr std::int64_t kExact = std::int64_t{1} << 53;
                    if (integer64 == FitsInteger64Policy::CheckedFloat64 && (raw < -kExact || raw > kExact))
                        return Status::failure(ErrorCode::OutOfRange,
                                               {.message = "FITS integer exceeds Float64's guaranteed exact range; "
                                                           "preserve raw codes or acknowledge lossy decoding",
                                                .operation = "decode FITS integer",
                                                .domain = ErrorDomain::Io,
                                                .context = {StatusContextField::ofNumber(ContextKey::Requested, raw)}});
                    const double value = static_cast<double>(raw) * header.scale + header.zero;
                    std::memcpy(destination + i * 8, &value, 8u);
                }
            }
            y += rows;
            completed += rows;
            execution.reportProgress(completed, totalRows, "decode FITS");
        }
    }
    return Status::success();
}

Result<CodecDecodeResult> readPreparedFits(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion* region,
                                           FitsInteger64Policy integer64, fitsfile* file, FitsHeader header)
{
    auto description = header.info.descriptor.toRequest();
    const auto sourceExtent = description.extent;
    const CodecRegion area =
        region != nullptr ? *region : CodecRegion{0u, 0u, sourceExtent.width(), sourceExtent.height()};
    if (area.width == 0u || area.height == 0u || area.width > sourceExtent.width() ||
        area.height > sourceExtent.height() || area.x > sourceExtent.width() - area.width ||
        area.y > sourceExtent.height() - area.height)
        return Status::failure(ErrorCode::OutOfRange);
    description.extent = *ImageExtent::create(area.width, area.height, sourceExtent.channelCount(), engine.limits());
    if (description.cfa.isMosaiced())
    {
        const auto block = cfaPatternBlockSize(description.cfa.pattern());
        description.cfa =
            *CfaDescriptor::create(description.cfa.pattern(), (description.cfa.originX() + area.x % block) % block,
                                   (description.cfa.originY() + area.y % block) % block);
    }
    const auto descriptor = *RawFrameDescriptor::create(description);
    const auto layout = *ImageLayout::create(
        {description.extent, description.sampleFormat.sizeBytes, 1u, description.extent.channelCount(), 1u},
        engine.limits());
    auto buffer = SampleBuffer::createOwned(engine, layout, StorageInitialization::Uninitialized);
    if (!buffer.hasValue())
        return buffer.status();
    auto output = RawFrame::create(descriptor, layout, std::move(*buffer));
    if (!output.hasValue())
        return output.status();
    FitsPayloadFile payloadFile;
    if (const auto read = readFitsPixels(execution, request, area, integer64, file, header,
                                         output->mutableBytes()->data(), layout, payloadFile);
        read.failed())
        return read;
    const bool integerImage = header.bitpix == LONGLONG_IMG;
    if (region != nullptr && request.readMetadata)
    {
        auto metadata =
            fitsMetadata(header.info.metadata.codec(), sourceExtent, request.path, integer64, integerImage, region);
        if (!metadata.hasValue())
            return metadata.status();
        header.info.metadata = std::move(*metadata);
    }
    // An acknowledged lossy read must carry its decision even when ordinary metadata was omitted.
    if (!request.readMetadata && integerImage && integer64 == FitsInteger64Policy::AllowLossyFloat64)
    {
        auto metadata = fitsMetadata(header.info.metadata.codec(), sourceExtent, request.path, integer64, true);
        if (!metadata.hasValue())
            return metadata.status();
        header.info.metadata = std::move(*metadata);
    }
    return CodecDecodeResult{std::move(*output), std::move(header.info.metadata)};
}
} // namespace an
