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

} // namespace an::ui
