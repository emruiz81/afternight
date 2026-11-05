// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The grid a local normalization of drizzled frames has, on the plane it measures them on.
///
/// A drizzled stack's frames are registered onto a canvas and enlarged by the scale. A local
/// normalization fitted before they are drizzled measures them on the canvas's analysis plane - the
/// same rectangle of the reference plane, as many whole pixels as it holds - so its grid describes
/// that plane rather than the enlarged one. A position `p` of the drizzled canvas, in engine pixel
/// coordinates, lies at `p / scale` on the analysis plane, which `evaluateLocalGrid` reads at the
/// pixel index `p / scale - 0.5`. `Algorithms/Drizzle/DrizzleCanvas.h` plans the canvas; this header
/// states the grid over it, so a grid fitted for one canvas is refused by every shape check against
/// another.

#pragma once

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
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

} // namespace an
