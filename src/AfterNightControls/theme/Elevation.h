// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How a raised part stands off what is under it under the Observatory profile: a menu, a tooltip, a popover, the
/// palette, the transient message. A raised part draws a 1 px line of the `edge` colour under its bottom edge and a
/// shadow of the `shadowOverlay` colour 10 px down, whose edge fades over 28 px. Nothing behind it is blurred and no
/// gradient is drawn: the shadow is an image, rendered once per corner radius, device pixel ratio and colour and drawn
/// as a nine-patch around the part, so a part of any size costs nine image draws.

#include "AfterNightControlsExport.h"

#include <QImage>
#include <QMargins>

#include <cstddef>
#include <cstdint>

class QPainter;
class QRectF;

namespace an::ui
{

/// How far below a raised part its shadow lies, in logical pixels.
inline constexpr int kElevationShadowOffset = 10;

/// How wide the shadow's edge is, in logical pixels: it fades from the shadow's colour to nothing over this width,
/// centred on the outline of the part moved down by the offset.
inline constexpr int kElevationShadowSpread = 28;

/// The room a raised part that is a window of its own - a menu, a tooltip - keeps around its surface, from a
/// translucent window background, for its shadow to be drawn in: half the spread at the sides, half the spread less the
/// offset above, half the spread and the offset below.
[[nodiscard]] AFTERNIGHTCONTROLS_API QMargins elevationWindowMargins();

/// Paints the shadow and the edge line of a raised part whose surface is `surface`, with corners of `radius`, in the
/// colours of the tokens in force. Called before the surface is filled: the surface is drawn over them.
AFTERNIGHTCONTROLS_API void paintElevation(QPainter& painter, const QRectF& surface, int radius);

/// The shadow's nine-patch for a corner radius at a device pixel ratio, in the shadow colour in force: a rounded square
/// of the radius faded over the spread, `2 * (radius + spread) + 1` logical pixels across, its middle row and column
/// the full colour and stretched, its corners `radius + spread` wide. Rendered at its first ask and kept; GUI thread
/// only.
[[nodiscard]] AFTERNIGHTCONTROLS_API QImage elevationShadow(int radius, qreal devicePixelRatio);

/// Lets every kept nine-patch go. The proxy style calls it when the theme changes: the next ask renders the shadow in
/// the new colour.
AFTERNIGHTCONTROLS_API void dropElevationImages();

namespace testing
{

/// How many nine-patches are kept now. For the suites.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t keptElevationImages();

/// How many nine-patches have been rendered since the process started. For the suites, which prove an image is
/// rendered once and kept.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t elevationRenders();

} // namespace testing

} // namespace an::ui
