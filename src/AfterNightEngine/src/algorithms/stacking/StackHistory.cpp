// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the failure helpers of the stacking units, and the band and scratch
// checks the sample-history combinations share.
//
// The checks are defined once here rather than repeated in each combination so that a band the
// column kernels accept and a band the rejection estimators accept are the same band.

#include "algorithms/stacking/StackHistory.h"

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>

#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <utility>

namespace an::stacking
{
/// A failure of one stacking operation in the algorithms domain.
///
/// @param code why.
/// @param operation which operation.
/// @param message what a person reads.
/// @param context the typed facts, if any.
/// @return the failure.
[[nodiscard]] Status failure(ErrorCode code, const char* operation, std::string message,
                             std::vector<StatusContextField> context)
{
    return Status::failure(code, StatusDetail{.message = std::move(message),
                                              .operation = operation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = std::move(context)});
}

/// A refusal naming the parameter that does not describe the stack.
///
/// @param code why.
/// @param operation which operation.
/// @param parameter the parameter, spelled as a caller reads it.
/// @param message what is wrong with it.
/// @return the failure, with the parameter as a context field.
[[nodiscard]] Status refuse(ErrorCode code, const char* operation, const char* parameter, std::string message)
{
    return failure(code, operation, std::move(message), {StatusContextField::ofText(ContextKey::Parameter, parameter)});
}

/// Whether a pointer begins on a boundary.
///
/// @param pointer the address.
/// @param alignment the boundary.
/// @return true when the address is a multiple of the alignment.
[[nodiscard]] bool pointerIsAligned(const void* pointer, std::uint64_t alignment) noexcept
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0u;
}

/// Checks every band of a history against the model and against the first band.
///
/// @param model the checked stack.
/// @param frames the bands.
/// @param operation the public caller whose arguments are checked.
/// @return the geometry, or the first refusal.
[[nodiscard]] Result<HistoryGeometry> checkHistory(const StackModel& model, std::span<const StackFrameBand> frames,
                                                   const char* operation)
{
    const StackRequest& request = model.request();
    if (frames.size() != request.input.frameCount)
    {
        return failure(ErrorCode::IncompatibleArguments, operation,
                       "frames does not hold one band per frame of the stack",
                       {StatusContextField::ofText(ContextKey::Parameter, "frames"),
                        StatusContextField::ofCount(ContextKey::Requested, frames.size()),
                        StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
    }
    HistoryGeometry geometry;
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        const StackFrameBand& band = frames[index];
        const Status checked = checkStackFrameBand(model, band);
        if (checked.failed())
        {
            return checked;
        }
        if (band.frameIndex != index)
        {
            return failure(ErrorCode::InvalidArgument, operation,
                           "frames.frameIndex of band " + std::to_string(index) + " is not its position",
                           {StatusContextField::ofText(ContextKey::Parameter, "frames.frameIndex"),
                            StatusContextField::ofCount(ContextKey::Requested, band.frameIndex),
                            StatusContextField::ofCount(ContextKey::Limit, index)});
        }
        const ImageExtent& extent = band.layout->extent();
        if (index == 0u)
        {
            geometry.width = extent.width();
            geometry.rows = extent.height();
            geometry.channels = extent.channelCount();
            geometry.firstOutputRow = band.firstOutputRow;
            geometry.planeElements = request.input.descriptor.extent().pixelCount();
            geometry.targetRows = request.input.descriptor.extent().height();
        }
        else
        {
            if (band.firstOutputRow != geometry.firstOutputRow)
            {
                return refuse(ErrorCode::IncompatibleArguments, operation, "frames.firstOutputRow",
                              "frames.firstOutputRow of band " + std::to_string(index) +
                                  " is not the first band's; every band of a history lands on the same rows");
            }
            if (extent.height() != geometry.rows)
            {
                return refuse(ErrorCode::IncompatibleArguments, operation, "frames.layout",
                              "frames.layout of band " + std::to_string(index) +
                                  " does not hold the first band's rows; every band of a history has one shape");
            }
        }
        geometry.planar = geometry.planar && band.layout->samplesPerPixel() == 1u;
    }
    return geometry;
}

/// Re-addresses a checked history to targets that hold one window of the output's rows.
///
/// @param geometry the checked history.
/// @param window the output rows the targets hold.
/// @param operation the public caller whose arguments are checked.
/// @return the geometry over the window, or the refusal.
[[nodiscard]] Result<HistoryGeometry> windowHistory(const HistoryGeometry& geometry,
                                                    const StackAccumulatorWindow& window, const char* operation)
{
    if (window.rows == 0u || geometry.firstOutputRow < window.firstRow ||
        geometry.firstOutputRow - window.firstRow > window.rows ||
        geometry.rows > window.rows - (geometry.firstOutputRow - window.firstRow))
    {
        return refuse(ErrorCode::OutOfRange, operation, "window",
                      "the bands' rows do not lie inside the rows the targets hold");
    }
    HistoryGeometry windowed = geometry;
    windowed.firstOutputRow = geometry.firstOutputRow - window.firstRow;
    windowed.planeElements = static_cast<std::uint64_t>(window.rows) * geometry.width;
    windowed.targetRows = window.rows;
    return windowed;
}

/// Checks the worker scratch against the execution and the model.
///
/// @param execution the operation.
/// @param model the checked stack.
/// @param workerScratch the slices.
/// @param operation the public caller whose scratch is checked.
/// @return success, or the refusal.
[[nodiscard]] Status checkScratch(const ExecutionContext& execution, const StackModel& model,
                                  std::span<const std::span<std::byte>> workerScratch, const char* operation)
{
    const std::uint32_t workers = execution.admittedConcurrency();
    if (workerScratch.size() < workers)
    {
        return failure(ErrorCode::IncompatibleArguments, operation,
                       "workerScratch holds fewer slices than the execution's admitted workers",
                       {StatusContextField::ofText(ContextKey::Parameter, "workerScratch"),
                        StatusContextField::ofCount(ContextKey::Requested, workerScratch.size()),
                        StatusContextField::ofCount(ContextKey::Limit, workers)});
    }
    const std::uint64_t needed = stackScratchBytesPerWorker(model);
    for (std::uint32_t worker = 0u; worker < workers; ++worker)
    {
        const std::span<std::byte>& slice = workerScratch[worker];
        if (slice.size() < needed)
        {
            return failure(ErrorCode::IncompatibleArguments, operation,
                           "a workerScratch slice is smaller than the per-worker scratch the plan states",
                           {StatusContextField::ofText(ContextKey::Parameter, "workerScratch"),
                            StatusContextField::ofCount(ContextKey::Requested, slice.size()),
                            StatusContextField::ofCount(ContextKey::Limit, needed)});
        }
        if (!pointerIsAligned(slice.data(), kStackScratchAlignmentBytes))
        {
            return refuse(ErrorCode::InvalidArgument, operation, "workerScratch",
                          "a workerScratch slice does not begin on the scratch alignment boundary");
        }
    }
    return Status::success();
}

/// One frame's record, as the column row reads it.
///
/// @param band the checked band.
/// @return the record.
[[nodiscard]] StackColumnFrame frameRecordOf(const StackFrameBand& band) noexcept
{
    const ImageLayout& layout = *band.layout;
    StackColumnFrame record;
    record.samples = static_cast<const std::byte*>(band.samples);
    record.planeStrideBytes = layout.planeStrideBytes();
    record.rowStrideBytes = layout.rowStrideBytes();
    record.samplesPerPixel = layout.samplesPerPixel();
    record.elementBytes = layout.elementSizeBytes();
    const ValidityDescriptor& rule = band.validity.descriptor;
    record.rule = rule.rule();
    record.lowestValid = rule.lowestValid().value_or(0.0);
    record.highestValid = rule.highestValid().value_or(0.0);
    if (band.validity.mask != nullptr)
    {
        const ValidityMaskView<const std::uint8_t>& mask = *band.validity.mask;
        record.maskBits = mask.data();
        record.maskPlaneStrideBytes = mask.planeStrideBytes();
        record.maskRowStrideBytes = mask.rowStrideBytes();
        record.maskPlanes = mask.planeCount();
    }
    return record;
}

} // namespace an::stacking
