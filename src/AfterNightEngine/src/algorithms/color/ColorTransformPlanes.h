// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the colour transform over three source planes that lie at addresses of their own.
//
// `convertImageColor` reads a source through one layout, so its three channels must lie one plane stride apart. The
// channel scope converts a component back from the plane its operation wrote and the two planes a preparation keeps,
// which lie in two allocations; copying the two into one image first would cost a frame two planes' worth of reads and
// writes for nothing. This converts from the three planes where they lie, through the same row and range loops - the
// same expressions over the same samples - so the answer is what `convertImageColor` gives over a planar image that
// holds them.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <array>

/// The colour transform's internal entry points.
namespace an::color_detail
{

/// Runs a plan over a source of three planes, each at its own address and laid out by one plane layout.
///
/// A plane may be the destination's own channel of the same index - planar, at the same row stride - which each pixel
/// reads before it writes; any other overlap with the destination is refused. Production arithmetic only.
///
/// Thread-safe with respect to the planes it only reads. Runs on the threads the operation was admitted, and the
/// answer does not depend on how many.
///
/// @param execution the operation this conversion belongs to.
/// @param plan the validated conversion, of three channels on either side.
/// @param sampleType what the samples are, on both sides: Float32 or Float64.
/// @param planes each source channel's first sample, in the plan's source channel order.
/// @param planeLayout where each plane's samples sit: one channel, of the destination's width and height.
/// @param destination where the converted samples go.
/// @param destinationLayout where they sit.
/// @return success; `ErrorCode::NotInitialized` for an execution that holds nothing; `ErrorCode::InvalidArgument` for
///         a null pointer or a layout that does not describe the stated representation;
///         `ErrorCode::IncompatibleArguments` for another representation, channel count or picture size, or an overlap
///         other than a plane that is the destination's own channel; or the execution's cancellation or deadline.
[[nodiscard]] Status convertImageColorFromPlanes(const ExecutionContext& execution, const ColorConversionPlan& plan,
                                                 SampleType sampleType, const std::array<const void*, 3u>& planes,
                                                 const ImageLayout& planeLayout, void* destination,
                                                 const ImageLayout& destinationLayout);

} // namespace an::color_detail
