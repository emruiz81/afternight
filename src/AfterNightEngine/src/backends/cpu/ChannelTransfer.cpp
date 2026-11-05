// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - extracting one channel of an image, and composing an image from channels.
//
// Two operations that address a channel rather than a picture, beside the whole-picture transfers in
// `ImageTransfer.cpp` for the same reason those are here: reaching a channel needs the operation's
// threads, its cancellation and the table it dispatches to.
//
// The unit of work is one row of the *picture* rather than one row of a plane. A channel move reads
// or writes one row of every plane it touches, so the row is the item both sides share; splitting by
// plane row instead would give two threads pieces of the same destination row in the composing
// direction.
//
// Two kernels do all of it and which one runs is a question about shape, not about samples. A
// channel that is a strided run inside a wider row is a strided copy; a destination whose channels
// are adjacent and few enough for one vector store is a channel join, which reads every source once
// and writes the destination once instead of once per channel. The whole-pixel path exists only for
// the join, and it is a performance property: both paths write the same bytes, which is what the
// case comparing them asserts.
//
// Neither operation counts anything, for the reason the whole-picture copy does not: nothing here
// reads a sample's meaning, so there is nothing to meet and refuse.

#include <AfterNightEngine/Image/ChannelTransfer.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "backends/cpu/CpuKernelTable.h"
#include "backends/cpu/ImageWalk.h"
#include "kernels/reference/ConversionKernels.h"
#include "kernels/reference/LayoutKernels.h"

#include <array>
#include <cstdint>

namespace an
{
namespace
{

using image_walk::badArgument;
using image_walk::describesSamples;
using image_walk::selectTable;

/// The failure two geometries that cannot be the same picture produce.
///
/// @param operation the engine operation to name.
/// @param message the sentence explaining what disagreed.
/// @param requested what the caller supplied.
/// @param available what the operation needed.
/// @return `ErrorCode::IncompatibleArguments`, carrying both counts.
[[nodiscard]] Status mismatchedShape(const char* operation, const char* message, std::uint64_t requested,
                                     std::uint64_t available)
{
    return Status::failure(ErrorCode::IncompatibleArguments,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                    StatusContextField::ofCount(ContextKey::Available, available)}});
}

/// The failure a source and a destination that share memory produce.
///
/// @param operation the engine operation to name.
/// @param parameter which argument was the overlapping one.
/// @return `ErrorCode::IncompatibleArguments`, naming the argument.
[[nodiscard]] Status overlappingBuffers(const char* operation, const char* parameter)
{
    return Status::failure(
        ErrorCode::IncompatibleArguments,
        StatusDetail{.message = "the destination shares memory with a source, so what a sample ends up as would "
                                "depend on the order the rows happened to be scheduled in",
                     .operation = operation,
                     .domain = ErrorDomain::Image,
                     .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether a layout describes a single-channel picture of a stated size.
///
/// @param layout the geometry.
/// @param width how many pixels wide the picture must be.
/// @param height how many rows it must have.
/// @return true when the extent is exactly that picture with one channel.
[[nodiscard]] bool isOneChannelPictureOf(const ImageLayout& layout, std::uint32_t width, std::uint32_t height) noexcept
{
    return layout.extent().channelCount() == 1u && layout.extent().width() == width &&
           layout.extent().height() == height;
}

/// Where one channel's first sample of one row sits, in bytes from the buffer's first.
///
/// @param layout the geometry the channel belongs to.
/// @param channelIndex which channel, below the layout's channel count.
/// @param y the row.
/// @return the byte offset.
[[nodiscard]] std::uint64_t channelRowByteOffset(const ImageLayout& layout, std::uint32_t channelIndex,
                                                 std::uint64_t y) noexcept
{
    const std::uint32_t plane = channelIndex / layout.samplesPerPixel();
    const std::uint32_t within = channelIndex % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
           static_cast<std::uint64_t>(within) * static_cast<std::uint64_t>(layout.elementSizeBytes());
}

} // namespace

Status extractImageChannel(const ExecutionContext& execution, SampleType sampleType, const void* source,
                           const ImageLayout& sourceLayout, std::uint32_t channelIndex, void* destination,
                           const ImageLayout& destinationLayout)
{
    constexpr const char* kOperation = "an::extractImageChannel";

    const CpuKernelTable* table = nullptr;
    const Status ready = selectTable(execution, kOperation, table);
    if (ready.failed())
    {
        return ready;
    }
    if (source == nullptr || destination == nullptr)
    {
        return badArgument(kOperation, source == nullptr ? "source" : "destination");
    }
    if (!describesSamples(sampleType, sourceLayout))
    {
        return badArgument(kOperation, "sourceLayout");
    }
    if (!describesSamples(sampleType, destinationLayout))
    {
        return badArgument(kOperation, "destinationLayout");
    }
    if (channelIndex >= sourceLayout.extent().channelCount())
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{
                .message = "the source has no such channel",
                .operation = kOperation,
                .domain = ErrorDomain::Image,
                .context = {StatusContextField::ofText(ContextKey::Parameter, "channelIndex"),
                            StatusContextField::ofCount(ContextKey::Requested, channelIndex),
                            StatusContextField::ofCount(ContextKey::Available, sourceLayout.extent().channelCount())}});
    }
    if (!isOneChannelPictureOf(destinationLayout, sourceLayout.extent().width(), sourceLayout.extent().height()))
    {
        return mismatchedShape(kOperation,
                               "an extraction produces one channel of the picture it was given, so the destination "
                               "must be a one-channel image of the source's width and height",
                               destinationLayout.extent().sampleCount(),
                               static_cast<std::uint64_t>(sourceLayout.extent().width()) *
                                   static_cast<std::uint64_t>(sourceLayout.extent().height()));
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source), sourceLayout.totalBytes(),
                       static_cast<std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return overlappingBuffers(kOperation, "destination");
    }

    const StrideCopyRunFn kernel = table->strideCopyRun(sampleType);
    if (kernel == nullptr)
    {
        return badArgument(kOperation, "sampleType");
    }

    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint64_t elementSize = static_cast<std::uint64_t>(sourceLayout.elementSizeBytes());
    const std::uint64_t sourceStride = static_cast<std::uint64_t>(sourceLayout.samplesPerPixel());
    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);

    // One sample read and one written per pixel, whatever the source arrangement. The bytes a
    // strided read actually pulls from memory are wider than that on an interleaved source, but the
    // grain policy is about how much work a task is worth and not about what a cache line costs.
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * elementSize * 2u,
                        .operationsPerItem = width};

    return parallelFor(
        execution, static_cast<std::uint64_t>(sourceLayout.extent().height()), cost, [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const std::uint8_t* const in = sourceBytes + channelRowByteOffset(sourceLayout, channelIndex, y);
                std::uint8_t* const out = destinationBytes + y * destinationLayout.rowStrideBytes();
                kernel(in, sourceStride, out, 1u, width);
            }
            return Status::success();
        });
}

Status composeImageChannels(const ExecutionContext& execution, SampleType sampleType,
                            std::span<const ChannelSource> sources, void* destination,
                            const ImageLayout& destinationLayout)
{
    constexpr const char* kOperation = "an::composeImageChannels";

    const CpuKernelTable* table = nullptr;
    const Status ready = selectTable(execution, kOperation, table);
    if (ready.failed())
    {
        return ready;
    }
    if (destination == nullptr)
    {
        return badArgument(kOperation, "destination");
    }
    if (!describesSamples(sampleType, destinationLayout))
    {
        return badArgument(kOperation, "destinationLayout");
    }

    const std::uint32_t channelCount = destinationLayout.extent().channelCount();
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    if (static_cast<std::uint64_t>(sources.size()) != static_cast<std::uint64_t>(channelCount))
    {
        return mismatchedShape(kOperation,
                               "a composition fills every channel of its destination and invents none, so there "
                               "must be exactly one source per destination channel",
                               static_cast<std::uint64_t>(sources.size()), static_cast<std::uint64_t>(channelCount));
    }

    for (const ChannelSource& source : sources)
    {
        if (source.samples == nullptr || source.layout == nullptr)
        {
            return badArgument(kOperation, "sources");
        }
        if (!describesSamples(sampleType, *source.layout))
        {
            return badArgument(kOperation, "sources");
        }
        if (!isOneChannelPictureOf(*source.layout, width, height))
        {
            return mismatchedShape(kOperation,
                                   "every source of a composition supplies one channel of the destination, so each "
                                   "must be a one-channel image of the destination's width and height",
                                   source.layout->extent().sampleCount(),
                                   static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height));
        }
        if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(),
                           static_cast<std::uint8_t*>(destination), destinationLayout.totalBytes()))
        {
            return overlappingBuffers(kOperation, "sources");
        }
    }

    const std::uint64_t elementSize = static_cast<std::uint64_t>(destinationLayout.elementSizeBytes());
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);

    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channelCount * elementSize * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channelCount};

    // A destination whose pixels hold every channel in one plane is what a vector instruction set
    // has a single store for. Recognising it here rather than inside the kernel is what makes the
    // difference visible: the general path writes the destination once per channel, and this writes
    // it once.
    const bool wholePixel = channelCount >= 2u && channelCount <= kMaxInterleavedChannels &&
                            destinationLayout.samplesPerPixel() == channelCount && destinationLayout.planeCount() == 1u;
    if (wholePixel)
    {
        const InterleaveRunFn join = table->interleaveRun(sampleType);
        if (join == nullptr)
        {
            return badArgument(kOperation, "sampleType");
        }
        return parallelFor(execution, static_cast<std::uint64_t>(height), cost, [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                std::array<const void*, kMaxInterleavedChannels> rows{};
                for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
                {
                    const ChannelSource& source = sources[channel];
                    rows[channel] =
                        static_cast<const std::uint8_t*>(source.samples) + y * source.layout->rowStrideBytes();
                }
                join(rows.data(), destinationBytes + y * destinationLayout.rowStrideBytes(), channelCount, width);
            }
            return Status::success();
        });
    }

    const StrideCopyRunFn kernel = table->strideCopyRun(sampleType);
    if (kernel == nullptr)
    {
        return badArgument(kOperation, "sampleType");
    }

    const std::uint64_t destinationStride = static_cast<std::uint64_t>(destinationLayout.samplesPerPixel());
    return parallelFor(execution, static_cast<std::uint64_t>(height), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
            {
                const ChannelSource& source = sources[channel];
                const std::uint8_t* const in =
                    static_cast<const std::uint8_t*>(source.samples) + y * source.layout->rowStrideBytes();
                std::uint8_t* const out = destinationBytes + channelRowByteOffset(destinationLayout, channel, y);
                kernel(in, 1u, out, destinationStride, width);
            }
        }
        return Status::success();
    });
}

} // namespace an
