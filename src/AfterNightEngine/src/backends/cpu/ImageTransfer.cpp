// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - copying, filling and clearing whole images on the operation's threads.
//
// Three operations, one walk each, and the reason this file sits beside the dispatch tables rather
// than in the image layer: moving a picture needs the operation's threads, its cancellation and the
// table it dispatches to, none of which the image model knows about.
//
// The unit of work is a row of one plane. That is the largest run that is contiguous under every
// layout this engine describes, which is what lets the kernel see a run rather than a stride, and it
// is also why a fill never touches the padding between two rows: the padding is not part of any row.
// It is also why none of them can reach a storage's retained capacity: every address here is derived
// from the layout it was handed, and a layout describes a picture rather than an allocation.
//
// None of them counts anything. A conversion reports what it met because it can meet a sample it has
// to refuse; these read no sample's meaning at all, so the only thing they can report is whether
// they finished.

#include <AfterNightEngine/Image/ImageTransfer.h>

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
#include "kernels/reference/LayoutKernels.h"

#include <array>
#include <cstdint>

namespace an
{
namespace
{

using image_walk::badArgument;
using image_walk::describesSamples;
using image_walk::rowSampleCount;
using image_walk::rowSampleOffset;
using image_walk::sameShape;
using image_walk::selectTable;
using image_walk::totalRowCount;

/// Writes one sample over every sample a layout describes, on the operation's threads.
///
/// The body a fill and a clear share. They differ in where the sample comes from and in what each
/// has to prove about it before starting, and in nothing after that - so the walk is written once,
/// and a change to how a picture is covered cannot reach one of them and miss the other.
///
/// @param execution the operation, already proved to hold something.
/// @param table the dispatch table the operation selected.
/// @param sampleType the representation, already proved to be this layout's.
/// @param value one sample, in that representation. Read for the duration of the walk.
/// @param destination where the samples start, already proved non-null.
/// @param layout where they sit.
/// @param operation the engine operation to name in a failure.
/// @return success, `ErrorCode::InvalidArgument` when the table has no kernel for this
///         representation, or whatever the parallel walk reported.
[[nodiscard]] Status repeatSampleOverImage(const ExecutionContext& execution, const CpuKernelTable& table,
                                           SampleType sampleType, const void* value, void* destination,
                                           const ImageLayout& layout, const char* operation)
{
    const FillRunFn kernel = table.fillRun(sampleType);
    if (kernel == nullptr)
    {
        return badArgument(operation, "sampleType");
    }

    const std::uint64_t rowSamples = rowSampleCount(layout);
    const std::uint64_t rowCount = totalRowCount(layout);
    const std::uint64_t elementSize = static_cast<std::uint64_t>(layout.elementSizeBytes());
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);

    const TaskCost cost{.bytesPerItem = rowSamples * elementSize, .operationsPerItem = rowSamples};

    return parallelFor(execution, rowCount, cost, [&](TaskRange range) -> Status {
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            kernel(value, destinationBytes + rowSampleOffset(layout, rowIndex) * elementSize, rowSamples);
        }
        return Status::success();
    });
}

} // namespace

Status copyImageSamples(const ExecutionContext& execution, SampleType sampleType, const void* source,
                        const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout)
{
    constexpr const char* kOperation = "an::copyImageSamples";

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
    if (!sameShape(sourceLayout, destinationLayout))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{
                .message = "a copy reproduces a picture and reshapes none, so the two images must have the same "
                           "extent and the same arrangement",
                .operation = kOperation,
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
            StatusDetail{.message = "the destination shares memory with the source, so what a sample ends up as "
                                    "would depend on the order the rows happened to be scheduled in",
                         .operation = kOperation,
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "destination")}});
    }

    const CopyRunFn kernel = table->copyRun(sampleType);
    if (kernel == nullptr)
    {
        return badArgument(kOperation, "sampleType");
    }

    const std::uint64_t rowSamples = rowSampleCount(sourceLayout);
    const std::uint64_t rowCount = totalRowCount(sourceLayout);
    const std::uint64_t sourceElement = static_cast<std::uint64_t>(sourceLayout.elementSizeBytes());
    const std::uint64_t destinationElement = static_cast<std::uint64_t>(destinationLayout.elementSizeBytes());
    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);

    // A copy moves each sample once in and once out and computes nothing, so it is bound by memory
    // rather than by work. The operation count says so, which is what keeps the scheduler from
    // cutting a bandwidth-bound walk into more pieces than the memory system has any use for.
    const TaskCost cost{.bytesPerItem = rowSamples * (sourceElement + destinationElement),
                        .operationsPerItem = rowSamples};

    return parallelFor(execution, rowCount, cost, [&](TaskRange range) -> Status {
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            const void* const in = sourceBytes + rowSampleOffset(sourceLayout, rowIndex) * sourceElement;
            void* const out = destinationBytes + rowSampleOffset(destinationLayout, rowIndex) * destinationElement;
            kernel(in, out, rowSamples);
        }
        return Status::success();
    });
}

Status fillImageSamples(const ExecutionContext& execution, SampleType sampleType, const void* value, void* destination,
                        const ImageLayout& layout)
{
    constexpr const char* kOperation = "an::fillImageSamples";

    const CpuKernelTable* table = nullptr;
    const Status ready = selectTable(execution, kOperation, table);
    if (ready.failed())
    {
        return ready;
    }
    if (value == nullptr || destination == nullptr)
    {
        return badArgument(kOperation, value == nullptr ? "value" : "destination");
    }
    if (!describesSamples(sampleType, layout))
    {
        return badArgument(kOperation, "layout");
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(value),
                       static_cast<std::uint64_t>(sampleTypeSizeBytes(sampleType)),
                       static_cast<std::uint8_t*>(destination), layout.totalBytes()))
    {
        // A value read out of the image being filled would be overwritten partway through, so the
        // second half of the picture would be filled with whatever the first half had already put
        // there. Refused rather than copied to the stack, because a caller doing this has confused
        // two buffers and should hear about it.
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the value to repeat sits inside the image being filled, so it would be "
                                    "overwritten while the fill was still reading it",
                         .operation = kOperation,
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "value")}});
    }

    return repeatSampleOverImage(execution, *table, sampleType, value, destination, layout, kOperation);
}

Status clearImageSamples(const ExecutionContext& execution, SampleType sampleType, void* destination,
                         const ImageLayout& layout)
{
    constexpr const char* kOperation = "an::clearImageSamples";

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
    if (!describesSamples(sampleType, layout))
    {
        return badArgument(kOperation, "layout");
    }

    // The widest sample this engine holds, all-bits-zero, on this thread's stack. It is zero bytes
    // rather than a typed zero because a clear resolves an element width and not a representation,
    // and it is a local because that is what makes the aliasing question a fill has to ask -
    // whether the value sits inside the image - not arise here at all.
    const std::array<std::uint8_t, sizeof(double)> zero{};
    return repeatSampleOverImage(execution, *table, sampleType, zero.data(), destination, layout, kOperation);
}

} // namespace an
