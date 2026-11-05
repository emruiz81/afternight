// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what every whole-image operation checks before it walks one.
//
// Converting, repacking, byte-reversing, copying, filling, clearing and moving a channel are seven
// different operations, and the questions they ask first are the same five: does this operation hold
// anything, which table does it dispatch to, does this layout describe the representation it claims,
// do these two images describe the same picture, and where does one row of one plane start.
// Answering them once here is not only less code: a second copy of "same extent, same arrangement"
// is how two operations end up with two slightly different ideas of what shape agreement means.
//
// Everything here is a check or an offset. Nothing walks a picture, nothing dispatches a kernel, and
// nothing decides what an operation's failure means beyond the two shapes that are the same wherever
// they occur - an argument that does not describe movable samples, and an operation that holds
// nothing.

#pragma once

#include "backends/cpu/CpuKernelTable.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstdint>

/// The checks and row offsets the whole-image walks in `backends/cpu` share.
namespace an::image_walk
{

/// Whether two layouts describe the same picture in the same arrangement.
///
/// Strides are deliberately not compared: an operation between a padded source and a tight
/// destination is exactly what a boundary does, and refusing it would make every such caller
/// allocate a matching intermediate.
///
/// @param left one layout.
/// @param right the other.
/// @return true when the extents, the samples per pixel and the plane counts all agree.
[[nodiscard]] inline bool sameShape(const ImageLayout& left, const ImageLayout& right) noexcept
{
    return left.extent() == right.extent() && left.samplesPerPixel() == right.samplesPerPixel() &&
           left.planeCount() == right.planeCount();
}

/// Whether a layout's element size is the one its stated representation has.
///
/// @param sampleType the representation.
/// @param layout the geometry.
/// @return true when the two agree.
[[nodiscard]] inline bool describesSamples(SampleType sampleType, const ImageLayout& layout) noexcept
{
    return isSampleType(sampleType) && sampleTypeSizeBytes(sampleType) == layout.elementSizeBytes();
}

/// The failure a representation and a geometry that disagree produce.
///
/// @param operation the engine operation to name.
/// @param parameter which argument was wrong, as a call site spells it.
/// @return `ErrorCode::InvalidArgument`, naming the argument.
[[nodiscard]] inline Status badArgument(const char* operation, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = "this argument does not describe samples this engine can move",
                                        .operation = operation,
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether an operation may run at all, and the table it would run on.
///
/// @param execution the operation.
/// @param operation the engine operation to name in a failure.
/// @param table where the selected table is written when this succeeds.
/// @return success, or `ErrorCode::NotInitialized` when the operation holds nothing, or whatever
///         the backend selection reported.
[[nodiscard]] inline Status selectTable(const ExecutionContext& execution, const char* operation,
                                        const CpuKernelTable*& table)
{
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "an operation that holds nothing moves nothing",
                                            .operation = operation,
                                            .domain = ErrorDomain::Image});
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    table = &cpuKernelTableFor(execution);
    return Status::success();
}

/// Where one row of one plane starts, in samples of its own representation.
///
/// @param layout the geometry.
/// @param rowIndex the row, counted across every plane: plane zero's rows first, then plane one's.
/// @return the offset in samples from the buffer's first.
[[nodiscard]] inline std::uint64_t rowSampleOffset(const ImageLayout& layout, std::uint64_t rowIndex) noexcept
{
    const std::uint64_t height = static_cast<std::uint64_t>(layout.extent().height());
    const std::uint64_t plane = rowIndex / height;
    const std::uint64_t y = rowIndex % height;
    const std::uint64_t elementSize = static_cast<std::uint64_t>(layout.elementSizeBytes());
    return (plane * layout.planeStrideBytes() + y * layout.rowStrideBytes()) / elementSize;
}

/// How many samples one row of one plane holds.
///
/// @param layout the geometry.
/// @return the width in pixels times the samples one pixel keeps in that plane.
[[nodiscard]] inline std::uint64_t rowSampleCount(const ImageLayout& layout) noexcept
{
    return static_cast<std::uint64_t>(layout.extent().width()) * static_cast<std::uint64_t>(layout.samplesPerPixel());
}

/// How many rows a whole image holds, counting every plane's.
///
/// @param layout the geometry.
/// @return the height times the plane count.
[[nodiscard]] inline std::uint64_t totalRowCount(const ImageLayout& layout) noexcept
{
    return static_cast<std::uint64_t>(layout.extent().height()) * static_cast<std::uint64_t>(layout.planeCount());
}

} // namespace an::image_walk
