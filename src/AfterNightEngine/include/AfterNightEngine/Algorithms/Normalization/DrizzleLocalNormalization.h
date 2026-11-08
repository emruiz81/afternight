// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The grid a local normalization of drizzled frames has, on the plane it measures them on, and
/// where the drizzled frames lie on that plane.
///
/// A drizzled stack's frames are registered onto a canvas and enlarged by the scale. A local
/// normalization fitted before they are drizzled measures them on the canvas's analysis plane - the
/// same rectangle of the reference plane, as many whole pixels as it holds - so its grid describes
/// that plane rather than the enlarged one. A position `p` of the drizzled canvas, in engine pixel
/// coordinates, lies at `p / scale` on the analysis plane, which `evaluateLocalGrid` reads at the
/// pixel index `p / scale - 0.5`. `Algorithms/Drizzle/DrizzleCanvas.h` plans the canvas; this header
/// states the grid over it, so a grid fitted for one canvas is refused by every shape check against
/// another, and the `StackPlaneMapping` a stack of the drizzled frames reads the grid through.

#pragma once

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>

namespace an
{

/// The output plane of a drizzled set. Defined in `Algorithms/Drizzle/DrizzleCanvas.h`.
///
/// Named here rather than included: a caller asking for the grid over a canvas planned one, and has
/// therefore already included that header.
struct DrizzleCanvas;

/// The grid shape of a local normalization measured on a drizzle canvas's analysis plane.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes.
///
/// @param canvas the canvas, from `planDrizzleCanvas` or `planDrizzleCanvasOnPlane`.
/// @param gridSize how many cells the grid has along each axis, within
///        `[1, kMaxLocalNormalizationGridSize]`.
/// @param channelCount the channels the frames are measured in: the drizzle's output channels,
///        three for a colour filter array source.
/// @return the shape, of the canvas's analysis width and height; or the refusal
///         `checkLocalNormalizationGridShape` gives it - `ErrorCode::OutOfRange` for a grid size
///         outside its range, `ErrorCode::InvalidArgument` for a channel count outside its range or
///         a canvas with no analysis plane.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalNormalizationGridShape>
drizzleLocalNormalizationShape(const DrizzleCanvas& canvas, std::uint32_t gridSize, std::uint32_t channelCount);

/// Where the frames of a stack over a drizzle canvas lie on the canvas's analysis plane.
///
/// `DrizzledStackFrameSource` renders every frame on the whole canvas, `canvas.width` by
/// `canvas.height` - the guard is cropped from the finished stack only - and states the samples no
/// drop reached as quiet NaN under the finite-only rule. A document measured on the analysis plane is
/// applied to those frames through the mapping this returns: the scale, no offset and the
/// finite-only rule. Every canvas `planDrizzleCanvas` or `planDrizzleCanvasOnPlane` plans lies on
/// its analysis plane at the scale it was planned with; a canvas that does not - one planned at
/// another scale, or not planned at all - is refused rather than mapped.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes.
///
/// @param canvas the canvas the frames are drizzled onto.
/// @param scale the enlargement the canvas was planned with, finite and in [1, 4].
/// @return the mapping; `ErrorCode::InvalidArgument` for a scale outside that range, or a canvas
///         with no extent or no analysis plane; `ErrorCode::IncompatibleArguments` when a pixel of
///         the canvas lies wholly past its analysis plane enlarged by the scale.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackPlaneMapping> drizzleLocalNormalizationPlane(const DrizzleCanvas& canvas,
                                                                                            double scale);

} // namespace an
