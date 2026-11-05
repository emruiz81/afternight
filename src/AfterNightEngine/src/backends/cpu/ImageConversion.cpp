// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - converting, repacking and byte-swapping whole images: one walk, three
// kernels, and the reason this file lives beside the dispatch tables.
//
// The public header is part of the image model, and the image model sits above the runtime; the
// implementation is the other way round, because converting a frame needs the operation's threads,
// its cancellation and the table it dispatches to. That inversion is ordinary - a public header
// names what it needs and includes nothing - and putting the body here rather than in the image
// layer is what lets it reach the compiled kernels at all.
//
// The unit of work of a conversion and a byte swap is a row of one plane - a repack's is a row of
// the picture, because it reads and writes every channel of one - and the loop over them is
// `parallelFor`, so a conversion runs on exactly the threads the operation was admitted and stops
// within one chunk of a cancellation. Rows are the right grain: they are the largest run that is
// contiguous under every layout this engine describes, which is what lets the kernel see a run
// rather than a stride.
//
// The counters are atomics that each range adds to once. Per-sample atomics would cost more than the
// conversion; per-range ones cost one contended add per few thousand samples, and a run that
// converts every sample reports the same number a single-threaded one does, because every count is
// a property of the samples rather than of the schedule.
//
// A rejecting policy is the exception, and deliberately so. The first range to refuse ends the walk,
// so what the counters hold afterwards is what the ranges that had already finished met, and the
// position the failure names is the first refusal *that range* found rather than the first in the
// image. Making either of them schedule-independent would mean converting the whole frame before
// reporting a refusal the caller asked to be stopped by. A caller that needs the first refusal in
// image order asks for one thread.

#include <AfterNightEngine/Image/ImageConversion.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "backends/cpu/CpuKernelTable.h"
#include "backends/cpu/ImageWalk.h"
#include "kernels/reference/ConversionKernels.h"
#include "kernels/reference/LayoutKernels.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace an
{
namespace
{

using image_walk::badArgument;
using image_walk::describesSamples;
using image_walk::rowSampleOffset;
using image_walk::sameShape;
using image_walk::selectTable;

/// The failure a rejecting policy reports, once, from what a kernel said.
///
/// @param outcome where the kernel stopped.
/// @param rowIndex which row it stopped in, counted across every plane.
/// @param rowSamples how many samples a row holds.
/// @param totalSamples how many samples the whole image holds.
/// @return `ErrorCode::InvalidData` for a refused non-finite sample, or `ErrorCode::OutOfRange` for
///         a refused out-of-range one, carrying the sample's position in the image.
[[nodiscard]] Status rejectionFailure(const ConversionRunOutcome& outcome, std::uint64_t rowIndex,
                                      std::uint64_t rowSamples, std::uint64_t totalSamples)
{
    const std::uint64_t position = rowIndex * rowSamples + outcome.rejectedIndex;
    return Status::failure(
        outcome.rejectedForNonFinite ? ErrorCode::InvalidData : ErrorCode::OutOfRange,
        StatusDetail{.message = outcome.rejectedForNonFinite
                                    ? "a source sample is not a finite number and this conversion refuses one"
                                    : "a source sample is outside what the destination holds and this conversion "
                                      "refuses one",
                     .operation = "an::convertImageSamples",
                     .domain = ErrorDomain::Image,
                     .context = {StatusContextField::ofCount(ContextKey::Completed, position),
                                 StatusContextField::ofCount(ContextKey::Total, totalSamples)}});
}

} // namespace

Status convertImageSamples(const ExecutionContext& execution, SampleType sourceType, const void* source,
                           const ImageLayout& sourceLayout, SampleType destinationType, void* destination,
                           const ImageLayout& destinationLayout, const ConversionPolicy& policy,
                           ConversionCounters& counters)
{
    const CpuKernelTable* table = nullptr;
    const Status ready = selectTable(execution, "an::convertImageSamples", table);
    if (ready.failed())
    {
        return ready;
    }
    if (source == nullptr || destination == nullptr)
    {
        return badArgument("an::convertImageSamples", source == nullptr ? "source" : "destination");
    }
    if (!describesSamples(sourceType, sourceLayout))
    {
        return badArgument("an::convertImageSamples", "sourceLayout");
    }
    if (!describesSamples(destinationType, destinationLayout))
    {
        return badArgument("an::convertImageSamples", "destinationLayout");
    }
    const Status decided = checkConversionPolicy(policy, "an::convertImageSamples");
    if (decided.failed())
    {
        return decided;
    }
    if (!sameShape(sourceLayout, destinationLayout))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{
                .message = "a conversion changes what the samples are and not where they sit, so the two "
                           "images must have the same extent and the same arrangement",
                .operation = "an::convertImageSamples",
                .domain = ErrorDomain::Image,
                .context = {
                    StatusContextField::ofCount(ContextKey::Requested, sourceLayout.extent().sampleCount()),
                    StatusContextField::ofCount(ContextKey::Available, destinationLayout.extent().sampleCount())}});
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source), sourceLayout.totalBytes(),
                       static_cast<std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the destination shares memory with the source, so what a sample converts to "
                                    "would depend on the order the rows happened to be scheduled in",
                         .operation = "an::convertImageSamples",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "destination")}});
    }

    const ConvertRunFn kernel = table->convertRun(sourceType, destinationType);
    if (kernel == nullptr)
    {
        return badArgument("an::convertImageSamples", "sourceType");
    }

    const std::uint64_t rowSamples = static_cast<std::uint64_t>(sourceLayout.extent().width()) *
                                     static_cast<std::uint64_t>(sourceLayout.samplesPerPixel());
    const std::uint64_t rowCount = static_cast<std::uint64_t>(sourceLayout.extent().height()) *
                                   static_cast<std::uint64_t>(sourceLayout.planeCount());
    const std::uint64_t totalSamples = sourceLayout.extent().sampleCount();

    std::atomic<std::uint64_t> converted{0u};
    std::atomic<std::uint64_t> nonFinite{0u};
    std::atomic<std::uint64_t> saturated{0u};

    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const std::uint64_t sourceElement = static_cast<std::uint64_t>(sourceLayout.elementSizeBytes());
    const std::uint64_t destinationElement = static_cast<std::uint64_t>(destinationLayout.elementSizeBytes());

    const TaskCost cost{.bytesPerItem = rowSamples * (sourceElement + destinationElement),
                        .operationsPerItem = rowSamples * 8u};

    const Status walked = parallelFor(execution, rowCount, cost, [&](TaskRange range) -> Status {
        ConversionCounters local;
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            const void* const in = sourceBytes + rowSampleOffset(sourceLayout, rowIndex) * sourceElement;
            void* const out = destinationBytes + rowSampleOffset(destinationLayout, rowIndex) * destinationElement;
            const ConversionRunOutcome outcome = kernel(in, out, rowSamples, policy, local);
            if (outcome.rejected)
            {
                converted.fetch_add(local.samplesConverted, std::memory_order_relaxed);
                nonFinite.fetch_add(local.nonFiniteInputs, std::memory_order_relaxed);
                saturated.fetch_add(local.saturatedSamples, std::memory_order_relaxed);
                return rejectionFailure(outcome, rowIndex, rowSamples, totalSamples);
            }
        }
        converted.fetch_add(local.samplesConverted, std::memory_order_relaxed);
        nonFinite.fetch_add(local.nonFiniteInputs, std::memory_order_relaxed);
        saturated.fetch_add(local.saturatedSamples, std::memory_order_relaxed);
        return Status::success();
    });

    counters.samplesConverted += converted.load(std::memory_order_relaxed);
    counters.nonFiniteInputs += nonFinite.load(std::memory_order_relaxed);
    counters.saturatedSamples += saturated.load(std::memory_order_relaxed);
    return walked;
}

Status repackImageSamples(const ExecutionContext& execution, SampleType sampleType, const void* source,
                          const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout)
{
    const CpuKernelTable* table = nullptr;
    const Status ready = selectTable(execution, "an::repackImageSamples", table);
    if (ready.failed())
    {
        return ready;
    }
    if (source == nullptr || destination == nullptr)
    {
        return badArgument("an::repackImageSamples", source == nullptr ? "source" : "destination");
    }
    if (!describesSamples(sampleType, sourceLayout))
    {
        return badArgument("an::repackImageSamples", "sourceLayout");
    }
    if (!describesSamples(sampleType, destinationLayout))
    {
        return badArgument("an::repackImageSamples", "destinationLayout");
    }
    if (!(sourceLayout.extent() == destinationLayout.extent()))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{
                .message = "a repack moves samples and creates none, so the two images must have the same "
                           "extent",
                .operation = "an::repackImageSamples",
                .domain = ErrorDomain::Image,
                .context = {
                    StatusContextField::ofCount(ContextKey::Requested, sourceLayout.extent().sampleCount()),
                    StatusContextField::ofCount(ContextKey::Available, destinationLayout.extent().sampleCount())}});
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source), sourceLayout.totalBytes(),
                       static_cast<std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the destination shares memory with the source, and a repack reads every sample "
                                    "from somewhere it may already have written",
                         .operation = "an::repackImageSamples",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "destination")}});
    }

    const StrideCopyRunFn kernel = table->strideCopyRun(sampleType);
    if (kernel == nullptr)
    {
        return badArgument("an::repackImageSamples", "sampleType");
    }

    const std::uint64_t elementSize = static_cast<std::uint64_t>(sourceLayout.elementSizeBytes());
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t channelCount = sourceLayout.extent().channelCount();
    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);

    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channelCount * elementSize * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channelCount};

    // The whole-pixel repacks are the two this engine actually performs, and they are the two a
    // vector instruction set has a single load and a single store for. Recognising them here rather
    // than inside the kernel is what makes the difference visible: the general path reads the
    // interleaved row once per channel, and these read it once.
    const bool sourceIsInterleaved = sourceLayout.samplesPerPixel() == channelCount && sourceLayout.planeCount() == 1u;
    const bool destinationIsInterleaved =
        destinationLayout.samplesPerPixel() == channelCount && destinationLayout.planeCount() == 1u;
    const bool sourceIsPlanar = sourceLayout.samplesPerPixel() == 1u && sourceLayout.planeCount() == channelCount;
    const bool destinationIsPlanar =
        destinationLayout.samplesPerPixel() == 1u && destinationLayout.planeCount() == channelCount;
    const bool wholePixel = channelCount >= 2u && channelCount <= kMaxInterleavedChannels;

    if (wholePixel && sourceIsInterleaved && destinationIsPlanar)
    {
        const DeinterleaveRunFn split = table->deinterleaveRun(sampleType);
        if (split == nullptr)
        {
            return badArgument("an::repackImageSamples", "sampleType");
        }
        return parallelFor(execution, static_cast<std::uint64_t>(height), cost, [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                std::array<void*, kMaxInterleavedChannels> planes{};
                for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
                {
                    planes[channel] = destinationBytes + channel * destinationLayout.planeStrideBytes() +
                                      y * destinationLayout.rowStrideBytes();
                }
                split(sourceBytes + y * sourceLayout.rowStrideBytes(), planes.data(), channelCount, width);
            }
            return Status::success();
        });
    }

    if (wholePixel && sourceIsPlanar && destinationIsInterleaved)
    {
        const InterleaveRunFn join = table->interleaveRun(sampleType);
        if (join == nullptr)
        {
            return badArgument("an::repackImageSamples", "sampleType");
        }
        return parallelFor(execution, static_cast<std::uint64_t>(height), cost, [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                std::array<const void*, kMaxInterleavedChannels> planes{};
                for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
                {
                    planes[channel] =
                        sourceBytes + channel * sourceLayout.planeStrideBytes() + y * sourceLayout.rowStrideBytes();
                }
                join(planes.data(), destinationBytes + y * destinationLayout.rowStrideBytes(), channelCount, width);
            }
            return Status::success();
        });
    }

    // One item per row of the picture rather than per row of a plane: a repack reads every channel
    // of a row and writes every channel of it, so the row is the unit both sides share.
    return parallelFor(execution, static_cast<std::uint64_t>(height), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
            {
                const std::uint32_t sourcePlane = channel / sourceLayout.samplesPerPixel();
                const std::uint32_t sourceWithin = channel % sourceLayout.samplesPerPixel();
                const std::uint32_t destinationPlane = channel / destinationLayout.samplesPerPixel();
                const std::uint32_t destinationWithin = channel % destinationLayout.samplesPerPixel();

                const std::uint64_t sourceOffset = sourcePlane * sourceLayout.planeStrideBytes() +
                                                   y * sourceLayout.rowStrideBytes() +
                                                   static_cast<std::uint64_t>(sourceWithin) * elementSize;
                const std::uint64_t destinationOffset = destinationPlane * destinationLayout.planeStrideBytes() +
                                                        y * destinationLayout.rowStrideBytes() +
                                                        static_cast<std::uint64_t>(destinationWithin) * elementSize;

                kernel(sourceBytes + sourceOffset, sourceLayout.samplesPerPixel(), destinationBytes + destinationOffset,
                       destinationLayout.samplesPerPixel(), width);
            }
        }
        return Status::success();
    });
}

Status swapImageByteOrder(const ExecutionContext& execution, SampleType sampleType, void* samples,
                          const ImageLayout& layout)
{
    const CpuKernelTable* table = nullptr;
    const Status ready = selectTable(execution, "an::swapImageByteOrder", table);
    if (ready.failed())
    {
        return ready;
    }
    if (samples == nullptr)
    {
        return badArgument("an::swapImageByteOrder", "samples");
    }
    if (!describesSamples(sampleType, layout))
    {
        return badArgument("an::swapImageByteOrder", "layout");
    }
    if (!isByteOrderMeaningful(sampleType))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = std::string("one byte has one order, so there is nothing to swap in a run of ") +
                                    sampleTypeName(sampleType) + " samples",
                         .operation = "an::swapImageByteOrder",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "sampleType")}});
    }

    const ByteSwapRunFn kernel = table->byteSwapRun(sampleType);
    if (kernel == nullptr)
    {
        return badArgument("an::swapImageByteOrder", "sampleType");
    }

    const std::uint64_t rowSamples =
        static_cast<std::uint64_t>(layout.extent().width()) * static_cast<std::uint64_t>(layout.samplesPerPixel());
    const std::uint64_t rowCount =
        static_cast<std::uint64_t>(layout.extent().height()) * static_cast<std::uint64_t>(layout.planeCount());
    const std::uint64_t elementSize = static_cast<std::uint64_t>(layout.elementSizeBytes());
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(samples);

    const TaskCost cost{.bytesPerItem = rowSamples * elementSize * 2u, .operationsPerItem = rowSamples};

    return parallelFor(execution, rowCount, cost, [&](TaskRange range) -> Status {
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            kernel(bytes + rowSampleOffset(layout, rowIndex) * elementSize, rowSamples);
        }
        return Status::success();
    });
}

} // namespace an
