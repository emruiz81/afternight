// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Where the main window was, kept between runs.
///
/// The window's normal place and size and whether it was maximized are written to the interface settings when it
/// closes. The next run shows it at that place when the person asked for the window to be remembered and the place is
/// on a screen there is now, never smaller than the window's minimum; and maximized when it was, when the place is on
/// no screen - a second screen no longer plugged in - and when nothing was kept.

#include "AfterNightShellCoreExport.h"

#include <QList>
#include <QRect>
#include <QSize>

class QWidget;

namespace an::ui::shell
{

class ApplicationCore;

class AFTERNIGHTSHELLCORE_API WindowGeometryStore
{
public:
    /// How a window is shown.
    struct Placement
    {
        bool maximized = true;
        /// The place and size of a window that is not maximized.
        QRect geometry;
    };

    /// Reads and writes the core's interface settings.
    explicit WindowGeometryStore(ApplicationCore& core);

    /// Keeps the window's normal place and size and whether it is maximized.
    void save(const QWidget& window) const;

    /// How a window is shown over `screens`, never smaller than `minimum`.
    [[nodiscard]] Placement restoredPlacement(const QList<QRect>& screens, const QSize& minimum) const;

    /// Shows the window as `restoredPlacement` says, over `screens` and the window's own minimum size.
    void show(QWidget& window, const QList<QRect>& screens) const;
    /// Shows the window over the screens there are now.
    void show(QWidget& window) const;

    /// The available area of each screen there is now.
    [[nodiscard]] static QList<QRect> availableScreenGeometries();
    /// `size`, grown where it is smaller than `minimum`.
    [[nodiscard]] static QSize clampedToMinimum(const QSize& size, const QSize& minimum);
    /// Whether a place is on one of the screens, by any part of it.
    [[nodiscard]] static bool intersectsAnyScreen(const QRect& place, const QList<QRect>& screens);

private:
    ApplicationCore& m_core;
};

} // namespace an::ui::shell
