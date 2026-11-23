// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Where a window that stays a window opens: within the area its screen leaves it - the screen less its taskbar - with
/// its frame, so that no part of it, its title bar least of all, opens off the screen.

#include "AfterNightControlsExport.h"

#include <QMargins>
#include <QRect>
#include <QSize>

class QWidget;

namespace an::ui
{

/// The geometry a window takes so that the whole of it, its frame included, lies within `area`: its size bounded to
/// what the area leaves it once the frame is taken out, never under `least`, and moved in where it stands out - the
/// least it can, so a window that fits is left where it is. An area that cannot hold the window at its least size
/// bounds nothing, as a restored size is not bounded then either: the geometry is handed back as it is.
[[nodiscard]] AFTERNIGHTCONTROLS_API QRect geometryWithinArea(const QRect& geometry, const QMargins& frame,
                                                              const QSize& least, const QRect& area);

/// Holds a top-level window within the available area of the screen it is on, as `geometryWithinArea` says, its frame
/// as the window system draws it. Called as the window is first shown.
AFTERNIGHTCONTROLS_API void holdWithinAvailableArea(QWidget& window);

/// The name of the scroll area the body of a window scrolls in, where it has one (`scrollWindowBody`).
inline constexpr const char* kWindowBodyName = "anWindowBody";

/// The height the scrolling body of a window needs to be shown whole, at the width it has with no bar beside it;
/// nothing for a window with no such body. It changes when what the body holds changes - not when the window is
/// made lower and the body scrolls - so a window that follows its content asks again only when this does.
[[nodiscard]] AFTERNIGHTCONTROLS_API int windowBodyHeightNeeded(QWidget& window);

/// Gives a window whose body scrolls the height that shows the body whole, no more and no less, and holds it within
/// the available area of its screen, where the body scrolls if it is higher than the area leaves it: what a window
/// that is as high as what it holds does when what it holds changes - a section opened, a message shown. A window
/// with no such body is only held.
AFTERNIGHTCONTROLS_API void fitHeightToBodyWithinAvailableArea(QWidget& window);

/// Has a window held within the available area of its screen as it is first shown as a window of its own, and only
/// then: sized or moved since, it is the person's. Called as the window is made, by a window that stays a window - a
/// tool window of a view, a dialog - so that none opens larger than the screen leaves it or with its title bar off it.
/// A widget that is first shown inside another is left alone.
///
/// The window is sized for the look first. A size stated for one of its parts, or for itself, that is smaller than
/// the part needs in the look's font and density is dropped - a field 60 pixels wide, a row 28 high, a least size the
/// heading no longer fits in - so the part takes what it needs; and a window
/// whose body scrolls (`kWindowBodyName`) is given the height that shows the body whole, which the hold then bounds
/// to the screen.
AFTERNIGHTCONTROLS_API void holdWithinAvailableAreaWhenFirstShown(QWidget& window);

} // namespace an::ui
