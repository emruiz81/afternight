// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The one owner of the dock library's process-wide state.
///
/// The library keeps its configuration and three hooks for the whole process - a question before a drag starts, one
/// before a drop indicator shows, and a call when a drag ends - each a plain function that captures nothing. No unit
/// but this one sets them: it configures the library once, as the application's core is made and before any shell makes
/// a main window, and it answers the three hooks through one dispatcher that any number of subscribers join and leave.
/// A drag or an indicator is allowed when every subscriber allows it; the end of a drag is told to each.
///
/// A saved layout is the process's too: one blob holds every main window and every floating window, and applying one
/// asks the library's one factory which dock widget a name stands for. So this unit is also where a layout is taken
/// and applied.

#include "AfterNightShellCoreExport.h"

#include <kddockwidgets/KDDockWidgets.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

class QWidget;

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class DockWidget;
class MainWindow;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui::shell
{

/// A drag the library is about to start, or the one that ended.
struct DockDrag
{
    /// The main window the dragged widget is in; null for a floating window, which belongs to none.
    KDDockWidgets::QtWidgets::MainWindow* mainWindow = nullptr;
    /// The dock widget dragged: the one the draggable carries alone, or the current tab of a strip of several.
    KDDockWidgets::QtWidgets::DockWidget* dockWidget = nullptr;
};

/// A drop indicator the library is about to show.
struct DockDrop
{
    KDDockWidgets::DropLocation location = KDDockWidgets::DropLocation_None;
    /// The main window whose dock area would take the drop; null for a floating window's.
    KDDockWidgets::QtWidgets::MainWindow* mainWindow = nullptr;
};

class AFTERNIGHTSHELLCORE_API DockHooks
{
public:
    /// What a subscriber answers; a part left empty allows everything and hears nothing.
    struct Subscriber
    {
        /// Asked at every move while the button is down, so it answers and counts nothing; false refuses the drag.
        std::function<bool(const DockDrag&)> dragAboutToStart;
        /// False keeps the indicator from showing.
        std::function<bool(const DockDrop&)> dropIndicatorAllowed;
        /// Told once a drag the subscribers allowed has ended, with what it named, after the dock widgets are where it
        /// left them. A refused drag is not told.
        std::function<void(const DockDrag&)> dragEnded;
    };

    /// A subscriber's place in the dispatcher: it leaves when the subscription is reset or goes.
    class AFTERNIGHTSHELLCORE_API Subscription
    {
    public:
        Subscription() = default;
        ~Subscription();
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        /// Whether the subscriber is still asked.
        [[nodiscard]] bool active() const noexcept;
        /// The subscriber leaves; nothing when it has.
        void reset() noexcept;

    private:
        friend class DockHooks;
        explicit Subscription(std::uint64_t token) noexcept;

        std::uint64_t m_token = 0;
    };

    DockHooks() = delete;

    /// Configures the library for the decorations and installs the dispatcher as its three hooks: the flags of
    /// `kDockLibraryFlags`, a dragged window at `kDraggedDockWindowOpacity`, no dock widget smaller than
    /// `kDockWidgetMinimumSide` a side, and `AN_ViewFactory`. The library takes its flags and the minimum only before
    /// the first dock widget or main window exists, so the application's core asks it as it is made. Once for the
    /// process: a later call does nothing.
    static void configure();
    /// Whether `configure` has run.
    [[nodiscard]] static bool isConfigured() noexcept;

    /// Joins the dispatcher.
    [[nodiscard]] static Subscription subscribe(Subscriber subscriber);

    /// Whether every subscriber allows the drag: what the library's hook answers.
    [[nodiscard]] static bool allowsDrag(const DockDrag& drag);
    /// Whether every subscriber allows the indicator: what the library's hook answers.
    [[nodiscard]] static bool allowsDrop(const DockDrop& drop);
    /// Tells every subscriber that a drag ended.
    static void tellDragEnded(const DockDrag& drag);

    /// Inhibits the library's drop indicators for the whole process, or lets them show again. The decorations read the
    /// inhibition as the layout being locked: a double click on a tab or a title bar neither floats nor docks a view.
    static void setDropIndicatorsInhibited(bool inhibited);
    [[nodiscard]] static bool dropIndicatorsInhibited();

    /// The main window a widget is in: its first parent that is one, short of a window of its own. A floating window
    /// belongs to none, though the library parents it to a main window.
    [[nodiscard]] static KDDockWidgets::QtWidgets::MainWindow* mainWindowHolding(const QWidget* widget);

    // ---- A saved layout

    /// Which dock widget a name of a saved layout stands for; null for a name that stands for none, whose place the
    /// layout then closes up.
    using DockWidgetResolver = std::function<KDDockWidgets::QtWidgets::DockWidget*(const QString& savedName)>;

    /// The layout of every main window and every floating window as the library writes it: where each dock widget is,
    /// by its name. A dock widget made to be skipped by a saved layout is not in it. Empty when the library refuses to
    /// write one - two dock widgets of one name.
    ///
    /// A dock widget whose guest is gone - a view deleted from under it - is destroyed first, here and before a layout
    /// is applied: the library writes it into a layout as a closed dock widget, and shows the guest of every dock
    /// widget a layout names, which it has none of.
    [[nodiscard]] static QByteArray saveLayout();

    /// The names of the main windows a saved layout holds; empty for bytes that are no layout.
    [[nodiscard]] static QStringList mainWindowsOf(const QByteArray& layout);

    /// Applies a saved layout: each dock widget it names is put where the layout has it, in the main windows as they
    /// are sized now, a floating window where it was on the screen.
    ///
    /// A name the layout holds is the dock widget of that name; when none has it, the one `resolver` answers. A dock
    /// widget the layout names that is on screen is moved, never destroyed: the library closes every dock widget a
    /// layout names before it places it, so one that deletes itself when closed is told not to for the duration. A
    /// dock widget the layout does not know that is docked in one of its main windows floats.
    ///
    /// A main window on screen takes the layout at the size it has. One that is not on screen - a page that is not
    /// shown, or was never shown and has no size of its own yet - is first given the size its layout had when it was
    /// saved and laid out at it: the library fits a layout to the size its main window has at that moment, and one
    /// squeezed into a main window with no size loses where its splitters were. It is given its place when it is
    /// shown, and the layout follows in proportion.
    ///
    /// @param layout the bytes `saveLayout` answered.
    /// @param resolver asked for each name no dock widget has; may be empty.
    /// @return false, with nothing moved, for bytes that are no layout or for a layout that holds a main window the
    ///         process does not have; false too when the library gives up part way.
    [[nodiscard]] static bool restoreLayout(const QByteArray& layout, const DockWidgetResolver& resolver);
};

} // namespace an::ui::shell
