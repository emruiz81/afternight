// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The regions of a window that `F6` and `Shift+F6` move the focus through.

#include "AfterNightShellObservatoryExport.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <vector>

class QWidget;

namespace an::ui::observatory
{

/// Moves the keyboard's focus from region to region of a window, in their order and back: the Observatory window's are
/// the rail, the centre and the column (`F6`, `Shift+F6`). A region that is not shown is passed over. A region a widget
/// lies in is the innermost one whose area holds it, so a region may lie inside another - a task page's column inside
/// the centre - and the outer one is the rest of its area.
///
/// The focus is given, in the region moved to: to the control the focus left it from, when it is still there and can
/// take the focus; else to the region's preferred widget - the active image, the canvas - when it has one that can;
/// else to the region's first control in the focus chain; else to the area itself when it takes the focus. It is given
/// as a shortcut gives it (`Qt::ShortcutFocusReason`), so the focus ring shows: it is the keyboard's.
///
/// Made, read and used on the GUI thread.
class AFTERNIGHTSHELLOBSERVATORY_API RegionFocus final
{
public:
    /// A region: its name, the widget that is its area now - null, or hidden, while it is not shown - and the widget
    /// the focus prefers in it, when it has one.
    struct Region
    {
        QString name;
        std::function<QWidget*()> area;
        std::function<QWidget*()> preferred;
    };

    /// @param window the window whose regions they are: the focus is followed while it is in it.
    /// @param regions the regions, in their order.
    RegionFocus(QWidget& window, std::vector<Region> regions);
    ~RegionFocus();

    RegionFocus(const RegionFocus&) = delete;
    RegionFocus& operator=(const RegionFocus&) = delete;
    RegionFocus(RegionFocus&&) = delete;
    RegionFocus& operator=(RegionFocus&&) = delete;

    /// Moves the focus to the next region shown after the one that holds it - the first one when none holds it - and
    /// from the last to the first. False when no region is shown that can take the focus.
    bool next();

    /// Moves the focus to the region shown before the one that holds it - the last one when none holds it - and from
    /// the first to the last.
    bool previous();

    /// The name of the region the focus is in; empty while it is in none.
    [[nodiscard]] QString current() const;

    /// The name of the region a widget lies in; empty for one in none.
    [[nodiscard]] QString regionOf(const QWidget* widget) const;

private:
    [[nodiscard]] int indexOf(const QWidget* widget) const;
    [[nodiscard]] bool isShown(int index) const;
    [[nodiscard]] bool canTakeFocus(const QWidget* widget, int index) const;
    [[nodiscard]] QWidget* target(int index) const;
    bool move(int step);

    QPointer<QWidget> m_window;
    std::vector<Region> m_regions;
    /// The widget the focus was in last, in each region.
    std::vector<QPointer<QWidget>> m_last;
    QMetaObject::Connection m_following;
};

} // namespace an::ui::observatory
