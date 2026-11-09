// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - decoding a FITS image's pixels, whole or by region.
//
// The frame is allocated once and every strip is read straight into it: a strip is a box of the region's rows, at
// most 4 MiB of decoded samples, read by OpenFITS into the frame at the frame's own row stride, one plane after
// another. The library reads the stored values of an uncompressed payload into the place they land and converts them
// there, and decodes the tiles a strip of a compressed image meets. Values are converted as the codec has always
// decoded them: the canonical unsigned and signed offsets exactly, any other scaling once into Float64, and no stored
// value treated as undefined - a BLANK value scales as any other, and a NaN of floating-point data stays one. A
// BITPIX=64 image is read as raw 64-bit codes into the same memory, and each code is checked against the policy
// before it is converted to Float64 and scaled in place, so no raw code is ever scaled first. The strips of a large
// uncompressed payload are independent, so an operation granted more than one worker reads them on its workers: the
// library reads one file from any number of threads at once.

#include "io/fits/FitsFile.h"
#include "io/fits/FitsMetadata.h"

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <openfits/reader.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace an
{
Result<CodecDecodeResult> readFits(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, const CodecRegion* region,
                                   FitsInteger64Policy integer64)
{
    auto file = openFits(request.path, engine.limits());
    if (!file.hasValue())
        return file.status();
    auto headerRequest = request;
    headerRequest.validatePayload = false;
    auto header = readFitsHeader(*file->reader, engine, execution, headerRequest, integer64, false);
    if (!header.hasValue())
        return header.status();
    return readPreparedFits(engine, execution, request, region, integer64, *file->reader, std::move(*header));
}

namespace
{

/// The most decoded bytes one strip holds.
constexpr std::uint64_t kStripBytes = std::uint64_t{4u} << 20u;

/// The fewest payload bytes a read shares between workers: below it one worker reads the payload sooner than several
/// share it.
constexpr std::uint64_t kSharedPayloadBytes = std::uint64_t{32u} << 20u;

/// The fewest workers an operation is granted before its read shares them. A read granted fewer is one of several
/// operations running side by side - a stage's reader beside its computing operation - and the memory bandwidth more
/// readers would take is what the operation beside it is waiting on.
constexpr std::uint32_t kSharedPayloadWorkers = 8u;

/// The largest code a BITPIX=64 image may hold under `FitsInteger64Policy::CheckedFloat64`: Float64 holds every
/// integer up to it exactly.
constexpr std::int64_t kExactInteger64 = std::int64_t{1} << 53;

/// One strip of a region: its plane, its first row inside the region and how many rows it holds.
struct Strip
{
    std::uint32_t plane = 0u; ///< The plane.
    std::uint32_t y = 0u; ///< The strip's first row, counted from the region's top.
    std::uint32_t rows = 0u; ///< How many rows the strip holds.
};

/// What every strip of one region read shares.
struct RegionRead
{
    const openfits::reader& file; ///< The open file.
    const FitsHeader& header; ///< The selected image.
    const CodecReadRequest& request; ///< The source selection, for failures.
    const CodecRegion& area; ///< The region.
    FitsInteger64Policy integer64; ///< How 64-bit codes are admitted.
    openfits::numeric_type type; ///< What the library reads each sample as.
    openfits::scaling_policy scaling; ///< Whether the library applies the file's scaling.
    std::uint32_t valueBytes; ///< The bytes of one read sample.
    std::byte* samples; ///< Where the region's first sample goes.
    const ImageLayout& layout; ///< The region's layout in that memory.
};

/// The rows one strip of the region holds: as many as 4 MiB of its samples, and at least one.
std::uint32_t stripRows(const CodecRegion& area, std::uint32_t valueBytes) noexcept
{
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(area.width) * valueBytes;
    return static_cast<std::uint32_t>(
        (std::clamp)(kStripBytes / rowBytes, std::uint64_t{1u}, std::uint64_t{area.height}));
}

/// The library's region of one strip, with an entry for each of the image's axes.
openfits::region stripRegion(const RegionRead& read, const Strip& strip)
{
    openfits::region box;
    box.start.assign(read.header.axes, 0u);
    box.size.assign(read.header.axes, 1u);
    box.start[0] = read.area.x;
    box.size[0] = read.area.width;
    if (read.header.axes >= 2u)
    {
        box.start[1] = static_cast<std::uint64_t>(read.area.y) + strip.y;
        box.size[1] = strip.rows;
    }
    if (read.header.axes >= 3u)
        box.start[2] = strip.plane;
    return box;
}

/// Converts 64-bit codes in place into Float64, refusing a code beyond Float64's exact range under the checked policy
/// before any code is scaled.
Status convertInteger64(const RegionRead& read, std::byte* row, std::uint64_t count)
{
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        std::int64_t raw = 0;
        std::memcpy(&raw, row + index * 8u, 8u);
        if (read.integer64 == FitsInteger64Policy::CheckedFloat64 && (raw < -kExactInteger64 || raw > kExactInteger64))
            return Status::failure(ErrorCode::OutOfRange,
                                   {.message = "FITS integer exceeds Float64's guaranteed exact range; "
                                               "preserve raw codes or acknowledge lossy decoding",
                                    .operation = "decode FITS integer",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofNumber(ContextKey::Requested, raw)}});
        const double value = static_cast<double>(raw) * read.header.scale + read.header.zero;
        std::memcpy(row + index * 8u, &value, 8u);
    }
    return Status::success();
}

/// Reads one strip into its place in the region's memory, and finishes its 64-bit codes.
///
/// The library takes memory aligned for the samples it writes, at a row stride of whole samples; a destination that is
/// not so is read a row at a time through `bounce`, one row of aligned memory the caller owns.
Status readStrip(const RegionRead& read, const Strip& strip, std::span<std::byte> bounce)
{
    std::byte* const first =
        read.samples + strip.plane * read.layout.planeStrideBytes() + strip.y * read.layout.rowStrideBytes();
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(read.area.width) * read.valueBytes;
    const bool direct = reinterpret_cast<std::uintptr_t>(first) % read.valueBytes == 0u &&
                        read.layout.rowStrideBytes() % read.valueBytes == 0u;
    Status outcome = Status::success();
    const Status thrown = callAcrossBoundary(kFitsBoundary, [&] {
        try
        {
            openfits::pixel_read_options options;
            options.scaling = read.scaling;
            // No stored value is undefined, as the codec has always read them: BLANK converts as any other value,
            // and a NaN of floating-point data stays one.
            options.blank = openfits::blank_policy::ignore;
            // A decode reads what the file holds; a checksum is verified by a probe that asks for it.
            options.verify_checksum = false;
            if (direct)
            {
                options.region = stripRegion(read, strip);
                options.pitch = read.layout.rowStrideBytes() / read.valueBytes;
                const std::uint64_t span = (strip.rows - 1u) * read.layout.rowStrideBytes() + rowBytes;
                (void)read.file.read_pixels(read.header.unit, read.type,
                                            std::span<std::byte>(first, static_cast<std::size_t>(span)), options);
                return;
            }
            for (std::uint32_t row = 0u; row < strip.rows; ++row)
            {
                options.region = stripRegion(read, Strip{strip.plane, strip.y + row, 1u});
                const auto aligned = bounce.first(static_cast<std::size_t>(rowBytes));
                (void)read.file.read_pixels(read.header.unit, read.type, aligned, options);
                std::memcpy(first + row * read.layout.rowStrideBytes(), aligned.data(), aligned.size());
            }
        }
        catch (const openfits::error& error)
        {
            outcome = fitsLibraryFailure(error, FitsStage::Read, "decode FITS pixels", read.request.path);
        }
    });
    if (thrown.failed())
        return codecFailure(thrown.code(), CodecFailure::PayloadUnreadable, CodecFormat::Fits, "decode FITS pixels",
                            read.request.path, thrown);
    if (outcome.failed())
        return outcome;
    if (read.header.bitpix == 64 && read.integer64 != FitsInteger64Policy::PreserveRaw)
        for (std::uint32_t row = 0u; row < strip.rows; ++row)
            if (const Status converted =
                    convertInteger64(read, first + row * read.layout.rowStrideBytes(), read.area.width);
                converted.failed())
                return converted;
    return Status::success();
}

/// Reads every strip of the region on the operation's workers, into memory the library writes into directly.
///
/// Progress counts rows in the order strips finish, under one lock, so it never moves back; a failure is the first in
/// strip order.
///
/// @return success, the operation's stop, or the first strip's failure.
[[nodiscard]] Status readStripsShared(const ExecutionContext& execution, const RegionRead& read,
                                      const std::vector<Strip>& strips, std::uint32_t rowsPerStrip)
{
    const std::uint64_t totalRows =
        static_cast<std::uint64_t>(read.area.height) * read.header.info.descriptor.extent().channelCount();
    std::mutex progressMutex;
    std::uint64_t completed = 0u;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(rowsPerStrip) * read.area.width * read.valueBytes,
                        .operationsPerItem = static_cast<std::uint64_t>(rowsPerStrip) * read.area.width};
    return parallelForWorkers(execution, strips.size(), cost, [&](WorkerTask task) -> Status {
        for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const Strip& strip = strips[static_cast<std::size_t>(index)];
            if (const Status stripRead = readStrip(read, strip, {}); stripRead.failed())
                return stripRead;
            const std::lock_guard lock(progressMutex);
            completed += strip.rows;
            execution.reportProgress(completed, totalRows, "decode FITS");
        }
        return Status::success();
    });
}

} // namespace

Status readFitsPixels(const EngineContext& engine, const ExecutionContext& execution, const CodecReadRequest& request,
                      const CodecRegion& area, FitsInteger64Policy integer64, const openfits::reader& file,
                      const FitsHeader& header, std::byte* samples, const ImageLayout& layout)
{
    const ImageExtent& sourceExtent = header.info.descriptor.extent();
    const RawSampleFormat format = header.info.descriptor.sampleFormat();
    const bool integerImage = header.bitpix == 64;
    // A 64-bit image is read as its raw codes and finished here; every other one as its decoded representation, with
    // the file's scaling applied wherever it is not the identity of the representation's own stored type.
    const auto type = integerImage ? std::optional(openfits::numeric_type::int64) : fitsNumericType(format);
    if (!type.has_value())
        return Status::failure(ErrorCode::Internal);
    const bool identity = header.scale == 1.0 && header.zero == 0.0;
    const RegionRead read{.file = file,
                          .header = header,
                          .request = request,
                          .area = area,
                          .integer64 = integer64,
                          .type = *type,
                          .scaling = integerImage || identity ? openfits::scaling_policy::raw
                                                              : openfits::scaling_policy::apply,
                          .valueBytes = static_cast<std::uint32_t>(openfits::size_of(*type)),
                          .samples = samples,
                          .layout = layout};
    const std::uint32_t rowsPerStrip = stripRows(area, read.valueBytes);
    std::vector<Strip> strips;
    for (std::uint32_t plane = 0u; plane < sourceExtent.channelCount(); ++plane)
        for (std::uint32_t y = 0u; y < area.height; y += rowsPerStrip)
            strips.push_back(Strip{plane, y, (std::min)(area.height - y, rowsPerStrip)});

    // A destination the library cannot write into - not aligned for the samples, or a row stride of no whole number
    // of them - is read a row at a time through one row of memory this context owns.
    SampleBuffer bounce;
    if (reinterpret_cast<std::uintptr_t>(samples) % read.valueBytes != 0u ||
        layout.rowStrideBytes() % read.valueBytes != 0u || layout.planeStrideBytes() % read.valueBytes != 0u)
    {
        const auto rowExtent = ImageExtent::create(area.width, 1u, 1u, engine.limits());
        const auto rowLayout =
            rowExtent ? ImageLayout::create({*rowExtent, read.valueBytes, 1u, 1u, 1u}, engine.limits()) : std::nullopt;
        if (!rowLayout)
            return Status::failure(ErrorCode::LimitExceeded);
        auto allocated = SampleBuffer::createOwned(engine, *rowLayout, StorageInitialization::Uninitialized);
        if (!allocated.hasValue())
            return allocated.status();
        bounce = std::move(*allocated);
    }
    const std::span<std::byte> bounceBytes = bounce.valid() ? *bounce.mutableBytes() : std::span<std::byte>{};

    const std::uint64_t payloadBytes =
        static_cast<std::uint64_t>(area.width) * area.height * sourceExtent.channelCount() * read.valueBytes;
    if (!header.compressed && !bounce.valid() && execution.admittedConcurrency() >= kSharedPayloadWorkers &&
        payloadBytes >= kSharedPayloadBytes)
        return readStripsShared(execution, read, strips, rowsPerStrip);

    const auto totalRows = static_cast<std::uint64_t>(area.height) * sourceExtent.channelCount();
    std::uint64_t completed = 0u;
    for (const Strip& strip : strips)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (const Status stripRead = readStrip(read, strip, bounceBytes); stripRead.failed())
            return stripRead;
        completed += strip.rows;
        execution.reportProgress(completed, totalRows, "decode FITS");
    }
    return Status::success();
}

Result<CodecDecodeResult> readPreparedFits(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion* region,
                                           FitsInteger64Policy integer64, const openfits::reader& file,
                                           FitsHeader header)
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
    if (const auto read = readFitsPixels(engine, execution, request, area, integer64, file, header,
                                         output->mutableBytes()->data(), layout);
        read.failed())
        return read;
    const bool integerImage = header.bitpix == 64;
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
