// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - decoding an XISF image's pixel data, whole or by region, into memory the engine owns.
//
// The library reads, or decompresses, the samples straight into the destination: the layouts this adapter decodes
// into have no padding and the file's own arrangement, so the samples land where the layout puts them and nothing is
// copied twice. A region is read natively - an uncompressed block run by run, a compressed one subblock by subblock,
// those that hold bytes of the region - and a planar region a channel at a time, each channel's rows being one stretch
// of the block. A large read of whole rows whose block can be read in parts is cut into strips, which the operation's
// workers read through the one reader: it reads by position, from any number of threads at once. What the library
// allocates besides the destination is answered here too, from the same plan the read follows.

#include "io/xisf/XisfFile.h"

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace an
{
namespace
{
/// The fewest bytes of samples a read shares between workers: below it one worker reads them sooner than several are
/// started.
constexpr std::uint64_t kSharedPayloadBytes = std::uint64_t{32u} << 20u;

/// The fewest workers an operation is granted before its read shares them. A read granted fewer is one of several
/// operations running side by side - a stage's reader beside its computing operation - and the memory bandwidth more
/// readers would take is what the operation beside it is waiting on.
constexpr std::uint32_t kSharedPayloadWorkers = 8u;

/// The bytes of samples a strip of an uncompressed block holds, at most.
constexpr std::uint64_t kStripBytes = std::uint64_t{4u} << 20u;

/// How many subblocks' worth of decoded data a strip of a compressed block holds in each stretch of the block it
/// reads, at least. A strip decodes the subblocks that hold its bytes, and the subblock at each end of a stretch is
/// decoded by the strips beside it too, so this keeps the decoding strips repeat to a quarter of the block. A shuffled
/// block holds a strip's samples in one stretch for each byte of a sample.
constexpr std::uint64_t kStripSubblocks = 8u;

/// The staging buffer the library reads the runs of a region through when they lie close together: a few MiB by its
/// documentation (`pixel_read_options::region`), its piece size of 4 MiB as built. A run read alone goes straight into
/// the destination.
constexpr std::uint64_t kStagingBytes = std::uint64_t{4u} << 20u;

/// One strip of whole rows a worker reads: the channel it holds, when the image is planar, its first row counted from
/// the top of the area read, and how many rows it holds.
struct XisfStrip
{
    std::uint32_t channel = 0u; ///< The channel of a planar image; 0, every channel, for an interleaved one.
    std::uint32_t y = 0u; ///< The strip's first row, counted from the area's top.
    std::uint32_t rows = 0u; ///< How many rows the strip holds.
};

/// The two counts added without wrapping: a sum past the largest count is the largest count.
std::uint64_t saturatingSum(std::uint64_t first, std::uint64_t second) noexcept
{
    return first > (std::numeric_limits<std::uint64_t>::max)() - second ? (std::numeric_limits<std::uint64_t>::max)()
                                                                        : first + second;
}

/// The product of two counts, or the largest count when it does not fit.
std::uint64_t saturatingProduct(std::uint64_t first, std::uint64_t second) noexcept
{
    return second != 0u && first > (std::numeric_limits<std::uint64_t>::max)() / second
               ? (std::numeric_limits<std::uint64_t>::max)()
               : first * second;
}

/// Whether the reader holds the block's bytes from the open on: a block written in the header.
bool heldByReader(const openxisf::block_info& block) noexcept
{
    return block.location == openxisf::block_location::inline_data ||
           block.location == openxisf::block_location::embedded;
}

/// Whether a region of the block is read by reading all of it and verifying it first.
bool verifiedWhole(const openxisf::block_info& block, const XisfCodecOptions& options) noexcept
{
    return block.checksum.has_value() && options.verifyChecksums && !heldByReader(block);
}

/// Whether an area is the whole image.
bool coversImage(const CodecRegion& area, const ImageExtent& extent) noexcept
{
    return area.x == 0u && area.y == 0u && area.width == extent.width() && area.height == extent.height();
}

/// Whether the image stores each channel's samples apart, which makes each channel's rows a stretch of their own.
bool planarStorage(const XisfHeader& header) noexcept
{
    return header.storage == openxisf::pixel_storage::planar && header.info.descriptor.extent().channelCount() > 1u;
}

/// The bytes of one row of the destination: one channel's in planar storage, every channel's in normal storage.
std::uint64_t destinationRowBytes(const XisfHeader& header, std::uint32_t width) noexcept
{
    const auto& descriptor = header.info.descriptor;
    const std::uint64_t channels = planarStorage(header) ? 1u : descriptor.extent().channelCount();
    return static_cast<std::uint64_t>(width) * channels * descriptor.sampleFormat().sizeBytes;
}

/// What one strip costs, for the scheduler's grain policy.
TaskCost stripCost(const XisfHeader& header, std::uint32_t stripRows) noexcept
{
    const auto rowBytes = destinationRowBytes(header, header.info.descriptor.extent().width());
    return {.bytesPerItem = rowBytes * stripRows, .operationsPerItem = rowBytes * stripRows};
}

/// The strips an area is read in on the operation's workers, or none when it is read in one call.
///
/// Only an area of whole rows of a block read in parts, of `kSharedPayloadBytes` or more, by an operation granted
/// `kSharedPayloadWorkers` or more, is cut into strips, and only when that makes two strips or more. A strip of an
/// uncompressed block holds `kStripBytes` at most; one of a compressed block holds `kStripSubblocks` subblocks' worth
/// of data in each stretch of the block it reads, at least, and a compressed block whose planes hold fewer is read in
/// one call.
std::vector<XisfStrip> stripsOf(const ExecutionContext& execution, const XisfHeader& header, const CodecRegion& area,
                                const XisfCodecOptions& options)
{
    const auto& extent = header.info.descriptor.extent();
    if (!header.block || !xisfBlockReadInParts(*header.block, options) || area.x != 0u ||
        area.width != extent.width() || execution.admittedConcurrency() < kSharedPayloadWorkers)
        return {};
    const openxisf::block_info& block = *header.block;
    const std::uint64_t rowBytes = destinationRowBytes(header, area.width);
    const std::uint32_t planes = planarStorage(header) ? extent.channelCount() : 1u;
    if (saturatingProduct(rowBytes * planes, area.height) < kSharedPayloadBytes)
        return {};
    std::uint64_t target = kStripBytes;
    if (block.codec)
    {
        // A plane whose stretches hold fewer subblocks than a strip needs is not cut at all: its subblocks are large
        // beside it, and a worker reading part of it would decode and hold the same subblocks another worker does.
        const std::uint64_t stretches = block.byte_shuffle ? header.info.descriptor.sampleFormat().sizeBytes : 1u;
        const std::uint64_t subblocks = saturatingProduct(kStripSubblocks, block.subblock_size);
        if (saturatingProduct(rowBytes, area.height) / stretches < subblocks)
            return {};
        target = (std::max)(target, saturatingProduct(subblocks, stretches));
    }
    const auto stripRows = static_cast<std::uint32_t>(
        (std::min)(static_cast<std::uint64_t>(area.height), (std::max)(std::uint64_t{1u}, target / rowBytes)));
    std::vector<XisfStrip> strips;
    for (std::uint32_t plane = 0u; plane < planes; ++plane)
        for (std::uint32_t y = 0u; y < area.height; y += stripRows)
            strips.push_back({plane, y, (std::min)(area.height - y, stripRows)});
    if (strips.size() < 2u)
        return {};
    return strips;
}

/// What one call of the library reading a region of the block allocates besides the destination and the codec's
/// state: the stored bytes of a block the reader holds or verifies whole, the subblock it decodes at a time and that
/// subblock's stored bytes, and the staging buffer of runs read together, which a region of whole rows read a stretch
/// at a time never needs.
std::uint64_t regionCallBytes(const openxisf::block_info& block, const XisfCodecOptions& options, bool wholeRows,
                              std::uint64_t spanBytes) noexcept
{
    const bool held = heldByReader(block);
    const bool whole = verifiedWhole(block, options);
    std::uint64_t bytes = held || whole ? block.stored_size : 0u;
    if (block.codec)
    {
        bytes = saturatingSum(bytes, block.subblock_size);
        // A subblock's stored bytes count as at most its data's: every writer that divides a block into subblocks
        // stores one that does not shrink as it is.
        if (!held && !whole)
            bytes = saturatingSum(bytes, (std::min)(block.stored_size, block.subblock_size));
        return bytes;
    }
    if (!held && !whole && !wholeRows)
        bytes = saturatingSum(bytes, (std::min)(kStagingBytes, spanBytes));
    return bytes;
}

/// What one call of the library reading the whole block allocates besides the destination and the codec's state.
///
/// An uncompressed block is read straight into the destination and verified there. A compressed one is read whole
/// into a buffer of its stored bytes, verified, and decompressed - straight into the destination unless it is
/// shuffled, when it is decompressed into a buffer of its size and unshuffled from it. A block the reader holds from
/// the open on counts its stored bytes besides.
std::uint64_t wholeCallBytes(const openxisf::block_info& block) noexcept
{
    std::uint64_t bytes = heldByReader(block) ? block.stored_size : 0u;
    if (!block.codec)
        return bytes;
    bytes = saturatingSum(bytes, block.stored_size);
    if (block.byte_shuffle)
        bytes = saturatingSum(bytes, block.uncompressed_size);
    return bytes;
}

/// The bytes a region spans in the block's stretch of one channel, or of every channel in normal storage: from its
/// first sample to its last.
std::uint64_t regionSpanBytes(const XisfHeader& header, const CodecRegion& area) noexcept
{
    const auto& extent = header.info.descriptor.extent();
    const std::uint64_t imageRow = destinationRowBytes(header, extent.width());
    return saturatingSum(saturatingProduct(imageRow, area.height - 1u), destinationRowBytes(header, area.width));
}

/// The options of one library call: the region, its storage kept, the checksum rule, and a progress that checks the
/// operation's cancellation and reports the call's share of `total` rows, from `base` on, when `report` is set.
openxisf::pixel_read_options callOptions(const ExecutionContext& execution, const XisfCodecOptions& options,
                                         std::optional<openxisf::pixel_region> region, bool report, std::uint64_t base,
                                         std::uint64_t rows, std::uint64_t total)
{
    openxisf::pixel_read_options read;
    read.region = std::move(region);
    read.verify_checksum = options.verifyChecksums;
    // Units of work are the library's: the bytes read and, for a compressed block, the bytes decompressed. Whatever
    // total it states, its share of the call's rows is what the operation reports.
    read.progress = [&execution, report, base, rows, total](std::uint64_t done, std::uint64_t work) {
        if (execution.checkCancellation().failed())
            return false;
        if (report && work != 0u)
        {
            const double share = static_cast<double>((std::min)(done, work)) / static_cast<double>(work);
            execution.reportProgress(base + static_cast<std::uint64_t>(share * static_cast<double>(rows)), total,
                                     "decode XISF");
        }
        return true;
    };
    return read;
}

/// The region of an image the library reads for an area: its box, along the image's own axes, and its channels.
openxisf::pixel_region libraryRegion(const XisfHeader& header, const CodecRegion& area, std::uint32_t firstChannel,
                                     std::uint32_t channels)
{
    openxisf::pixel_region region;
    // A one-axis image is a single row, whose region names its one axis alone.
    if (header.info.descriptor.extent().height() == 1u && header.axes == 1u)
    {
        region.origin = {area.x};
        region.size = {area.width};
    }
    else
    {
        region.origin = {area.x, area.y};
        region.size = {area.width, area.height};
    }
    region.first_channel = firstChannel;
    region.channel_count = channels;
    return region;
}

/// Runs one call of the library, translating what it throws: the operation's own stop for a cancelled call.
template <typename Call>
Status libraryCall(const ExecutionContext& execution, const CodecReadRequest& request, Call&& call)
{
    Status refused;
    const auto thrown = callAcrossBoundary(kXisfBoundary, [&] {
        try
        {
            call();
        }
        catch (const openxisf::cancelled_error&)
        {
            // Only the progress above cancels, so the execution says why; a frame is never returned unread.
            auto stopped = execution.checkCancellation();
            refused = stopped.failed() ? std::move(stopped) : Status::failure(ErrorCode::Cancelled);
        }
        catch (const openxisf::error& error)
        {
            refused = xisfLibraryFailure(error, XisfStage::Read, "decode XISF pixels", request.path);
        }
    });
    if (thrown.failed())
        return codecFailure(thrown.code(), CodecFailure::PayloadUnreadable, CodecFormat::Xisf, "decode XISF pixels",
                            request.path, thrown);
    return refused;
}

/// Reads every strip of an area into place on the operation's workers.
///
/// The operation's trace states how many strips the read was cut into, under the counter `xisf`/`strips`. Progress
/// counts rows in the order strips finish, under one lock, so it never moves back; a failure is the first in strip
/// order.
Status readStripsShared(const ExecutionContext& execution, const CodecReadRequest& request, const CodecRegion& area,
                        const XisfHeader& header, const openxisf::reader& reader, const XisfCodecOptions& options,
                        const std::vector<XisfStrip>& strips, std::byte* samples, const ImageLayout& layout)
{
    const bool planar = planarStorage(header);
    const std::uint32_t channels = header.info.descriptor.extent().channelCount();
    const std::uint64_t rowBytes = destinationRowBytes(header, area.width);
    const std::uint64_t totalRows = static_cast<std::uint64_t>(area.height) * (planar ? channels : 1u);
    execution.reportTrace(TraceEventKind::Counter, "xisf", "strips", static_cast<double>(strips.size()), "strips");
    std::mutex progressMutex;
    std::uint64_t completed = 0u;
    return parallelFor(
        execution, strips.size(), stripCost(header, strips.front().rows), [&](TaskRange range) -> Status {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const XisfStrip& strip = strips[static_cast<std::size_t>(index)];
                const CodecRegion rows{0u, area.y + strip.y, area.width, strip.rows};
                std::byte* const destination =
                    samples + strip.channel * layout.planeStrideBytes() + strip.y * layout.rowStrideBytes();
                const std::span<std::byte> bytes(destination, static_cast<std::size_t>(rowBytes * strip.rows));
                const auto read = libraryCall(execution, request, [&] {
                    reader.read_pixels(
                        request.imageIndex, bytes,
                        callOptions(execution, options,
                                    libraryRegion(header, rows, planar ? strip.channel : 0u, planar ? 1u : channels),
                                    false, 0u, 0u, 0u));
                });
                if (read.failed())
                    return read;
                const std::lock_guard lock(progressMutex);
                completed += strip.rows;
                execution.reportProgress(completed, totalRows, "decode XISF");
            }
            return Status::success();
        });
}
} // namespace

bool xisfBlockReadInParts(const openxisf::block_info& block, const XisfCodecOptions& options) noexcept
{
    return !heldByReader(block) && !verifiedWhole(block, options) && (!block.codec || block.subblock_count > 1u);
}

std::uint32_t xisfDecodeGranularityRows(const XisfHeader& header, const XisfCodecOptions& options) noexcept
{
    const auto& descriptor = header.info.descriptor;
    const std::uint32_t height = descriptor.extent().height();
    if (!header.block || height == 0u)
        return 1u;
    const openxisf::block_info& block = *header.block;
    if (verifiedWhole(block, options) || (block.codec && block.subblock_count <= 1u))
        return height;
    if (!block.codec)
        return 1u;
    // A band's bytes of one channel lie in one run of the block, or in one run of each byte's stretch when the bytes
    // are shuffled, where a row is a byte for each of its samples.
    const std::uint64_t rowBytes =
        destinationRowBytes(header, descriptor.extent().width()) /
        (block.byte_shuffle ? std::uint64_t{(std::max)(descriptor.sampleFormat().sizeBytes, 1u)} : 1u);
    if (rowBytes == 0u)
        return 1u;
    const std::uint64_t rows = (block.subblock_size + rowBytes - 1u) / rowBytes;
    return static_cast<std::uint32_t>((std::clamp)(rows, std::uint64_t{1u}, std::uint64_t{height}));
}

Status readXisfPixels(const ExecutionContext& execution, const CodecReadRequest& request, const CodecRegion& area,
                      const XisfHeader& header, const openxisf::reader& reader, const XisfCodecOptions& options,
                      std::byte* samples, const ImageLayout& layout)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    // An image whose data block the library could not use was still described; this is where it is refused, with the
    // block's own reason, before anything is read.
    if (header.blockFailure.failed())
        return header.blockFailure;
    const auto& extent = header.info.descriptor.extent();
    const bool planar = planarStorage(header);
    const std::uint32_t channels = extent.channelCount();
    const std::uint64_t rowBytes = destinationRowBytes(header, area.width);
    const std::uint64_t planes = planar ? channels : 1u;
    // The destination's rows follow each other and its planes too, as every layout this adapter decodes into has
    // them, so each call's samples are exactly the bytes the library is handed.
    if (layout.rowStrideBytes() != rowBytes || (planar && layout.planeStrideBytes() != rowBytes * area.height) ||
        layout.totalBytes() < rowBytes * area.height * planes)
        return xisfFailure(ErrorCode::Internal, CodecFailure::DescriptorMismatch, "decode XISF pixels", request.path,
                           "the destination's rows are not the samples the library reads");
    if (const auto strips = stripsOf(execution, header, area, options); !strips.empty())
        return readStripsShared(execution, request, area, header, reader, options, strips, samples, layout);
    const std::uint64_t totalRows = static_cast<std::uint64_t>(area.height) * planes;
    if (coversImage(area, extent))
        return libraryCall(execution, request, [&] {
            reader.read_pixels(request.imageIndex,
                               std::span<std::byte>(samples, static_cast<std::size_t>(rowBytes * area.height * planes)),
                               callOptions(execution, options, std::nullopt, true, 0u, totalRows, totalRows));
        });
    // A planar region is read a channel at a time: each channel's rows are a stretch of the block of their own, which
    // a region of whole rows reads in one run straight into its plane.
    for (std::uint32_t plane = 0u; plane < planes; ++plane)
    {
        const std::span<std::byte> bytes(samples + plane * layout.planeStrideBytes(),
                                         static_cast<std::size_t>(rowBytes * area.height));
        const auto read = libraryCall(execution, request, [&] {
            reader.read_pixels(request.imageIndex, bytes,
                               callOptions(execution, options,
                                           libraryRegion(header, area, planar ? plane : 0u, planar ? 1u : channels),
                                           true, plane * static_cast<std::uint64_t>(area.height), area.height,
                                           totalRows));
        });
        if (read.failed())
            return read;
    }
    return Status::success();
}

std::uint64_t xisfReadLibraryBytes(const ExecutionContext& execution, const XisfHeader& header, const CodecRegion& area,
                                   const XisfCodecOptions& options)
{
    if (!header.block)
        return 0u;
    const openxisf::block_info& block = *header.block;
    if (const auto strips = stripsOf(execution, header, area, options); !strips.empty())
    {
        const std::uint64_t workers =
            plannedWorkerCount(execution, strips.size(), stripCost(header, strips.front().rows));
        return saturatingProduct((std::max)(std::uint64_t{1u}, workers), regionCallBytes(block, options, true, 0u));
    }
    const auto& extent = header.info.descriptor.extent();
    if (coversImage(area, extent))
        return wholeCallBytes(block);
    return regionCallBytes(block, options, area.x == 0u && area.width == extent.width(), regionSpanBytes(header, area));
}

Result<CodecDecodeResult> readXisf(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, const CodecRegion* region,
                                   const XisfCodecOptions& options)
{
    auto source = openXisf(request.path, engine.limits(), !request.readMetadata);
    if (!source.hasValue())
        return source.status();
    auto headerRequest = request;
    headerRequest.validatePayload = false;
    auto header = readXisfHeader(*source->reader, engine, execution, headerRequest);
    if (!header.hasValue())
        return header.status();
    return readPreparedXisf(engine, execution, request, region, options, *source, std::move(*header));
}

Result<CodecDecodeResult> readPreparedXisf(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion* region,
                                           const XisfCodecOptions& options, XisfHandle& source, XisfHeader header)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (header.blockFailure.failed())
        return header.blockFailure;
    const ImageExtent sourceExtent = header.info.descriptor.extent();
    const CodecRegion area =
        region != nullptr ? *region : CodecRegion{0u, 0u, sourceExtent.width(), sourceExtent.height()};
    if (area.width == 0u || area.height == 0u || area.width > sourceExtent.width() ||
        area.height > sourceExtent.height() || area.x > sourceExtent.width() - area.width ||
        area.y > sourceExtent.height() - area.height)
        return Status::failure(ErrorCode::OutOfRange);
    auto descriptor = header.info.descriptor;
    auto layout = header.info.decodedLayout;
    if (region != nullptr)
    {
        // The region keeps the source's meaning, and a filter array's phase moves by the region's offset.
        auto description = descriptor.toRequest();
        const auto extent = ImageExtent::create(area.width, area.height, sourceExtent.channelCount(), engine.limits());
        if (!extent)
            return Status::failure(ErrorCode::LimitExceeded);
        description.extent = *extent;
        if (description.cfa.isMosaiced())
        {
            const auto block = cfaPatternBlockSize(description.cfa.pattern());
            const auto x = (description.cfa.originX() + area.x % block) % block;
            const auto y = (description.cfa.originY() + area.y % block) % block;
            const auto moved = description.cfa.hasFilterTable()
                                   ? CfaDescriptor::create(description.cfa.filterTable().entries(), x, y)
                                   : CfaDescriptor::create(description.cfa.pattern(), x, y);
            if (!moved)
                return Status::failure(ErrorCode::InvalidData);
            description.cfa = *moved;
        }
        const auto cropped = RawFrameDescriptor::create(description);
        if (!cropped)
            return Status::failure(ErrorCode::InvalidData);
        descriptor = *cropped;
        const bool planar = header.storage == openxisf::pixel_storage::planar;
        const auto channels = extent->channelCount();
        const auto regionLayout = ImageLayout::create({.extent = *extent,
                                                       .elementSizeBytes = descriptor.sampleFormat().sizeBytes,
                                                       .samplesPerPixel = planar ? 1u : channels,
                                                       .planeCount = planar ? channels : 1u,
                                                       .rowAlignmentBytes = 1u},
                                                      engine.limits());
        if (!regionLayout)
            return Status::failure(ErrorCode::LimitExceeded);
        layout = *regionLayout;
    }
    auto buffer = SampleBuffer::createOwned(engine, layout, StorageInitialization::Uninitialized);
    if (!buffer.hasValue())
        return buffer.status();
    auto frame = RawFrame::create(descriptor, layout, std::move(*buffer));
    if (!frame.hasValue())
        return frame.status();
    if (const auto read = readXisfPixels(execution, request, area, header, *source.reader, options,
                                         frame->mutableBytes()->data(), layout);
        read.failed())
        return read;
    if (region != nullptr && request.readMetadata)
    {
        auto cropped = croppedXisfMetadata(header.info.metadata, sourceExtent, area);
        if (!cropped.hasValue())
            return cropped.status();
        header.info.metadata = std::move(*cropped);
    }
    return CodecDecodeResult{std::move(*frame), std::move(header.info.metadata)};
}
} // namespace an
