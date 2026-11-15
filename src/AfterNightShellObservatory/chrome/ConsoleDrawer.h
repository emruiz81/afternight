// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Session Console as a drawer of the Observatory window: under the centre and the column on every place, resized
/// by its top edge, floated to a window of its own and attached again.

#include "AfterNightShellObservatoryExport.h"

#include <QIcon>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

class LogViewerControl;
class QAction;
class QEvent;
class QLabel;
class QPaintEvent;
class QToolBar;
class QVBoxLayout;

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class DockWidget;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui
{
class AnTag;
} // namespace an::ui

namespace an::ui::observatory
{

/// The drawer of the Session Console. It holds AfterNight 1.1's console (`LogViewerControl`), made when it is first
/// opened and kept while the window lives, so its lines, its scroll position and its filters stay as the person left
/// them. Its header is the console's own toolbar, in its order, with the drawer's parts about it: the title first, a
/// stretch and the count of the session's warnings as a tag before Search, then Float and a button that closes the
/// drawer. It is 220 px high at first, resized by its top edge between 120 px and 60 % of the window. `Float` moves the
/// console into a floating window of the dock library with the affinity `an.console`, which no workspace and no image's
/// window shares and the layout saver skips; `Attach` brings it back. A warning or an error that arrives while the
/// console is closed is counted as not seen until it opens. Colours are read where it paints.
class AFTERNIGHTSHELLOBSERVATORY_API ConsoleDrawer final : public QWidget
{
public:
    /// The affinity of the console's own window.
    static constexpr char kAffinity[] = "an.console";
    /// The property that marks an entry of the header as the drawer's own rather than the console's.
    static constexpr char kOwnPartProperty[] = "an.consoleDrawerPart";

    struct Hooks
    {
        /// Told when the console opens or closes, floats or attaches, and when a height is chosen.
        std::function<void()> changed;
        /// Told when the count of the warnings not seen changes.
        std::function<void(int unseen)> unseenChanged;
        /// The icon of a name, in the theme's colours.
        std::function<QIcon(const QString& name)> icon;
    };

    /// @param window the window the drawer is in, whose height bounds it.
    ConsoleDrawer(QWidget& window, Hooks hooks, QWidget* parent = nullptr);
    ~ConsoleDrawer() override;

    ConsoleDrawer(const ConsoleDrawer&) = delete;
    ConsoleDrawer& operator=(const ConsoleDrawer&) = delete;

    /// Opens the console - in the drawer, or in its window while it floats - or closes it.
    void setOpen(bool open);
    /// Whether the console is on screen, in the drawer or in its window.
    [[nodiscard]] bool isOpen() const noexcept;
    /// Moves the console into its window of its own, or back into the drawer; kept while it is closed.
    void setFloating(bool floating);
    [[nodiscard]] bool isFloating() const noexcept;

    /// The height a person chose, kept within the drawer's limits.
    void setDrawerHeight(int height);
    [[nodiscard]] int drawerHeight() const noexcept;
    [[nodiscard]] int minimumDrawerHeight() const noexcept;
    /// 60 % of the window's height.
    [[nodiscard]] int maximumDrawerHeight() const;
    /// Keeps the drawer within its limits after the window's height changed.
    void windowResized();

    /// The warnings and the errors the session's lines hold.
    [[nodiscard]] int warnings() const noexcept;
    /// The warnings and the errors that arrived while the console was closed.
    [[nodiscard]] int unseenWarnings() const noexcept;

    /// The console, once it has been opened; null before.
    [[nodiscard]] LogViewerControl* console() const noexcept;
    /// The console's toolbar with the drawer's parts; null before the console is made.
    [[nodiscard]] QToolBar* header() const noexcept;
    [[nodiscard]] QAction* floatAction() const noexcept;
    [[nodiscard]] QAction* closeAction() const noexcept;
    [[nodiscard]] an::ui::AnTag* warningTag() const noexcept;
    /// The band along the top edge that resizes the drawer.
    [[nodiscard]] QWidget* grip() const noexcept;
    /// The dock widget of the console's window, once it has floated; null before.
    [[nodiscard]] KDDockWidgets::QtWidgets::DockWidget* floatingDock() const noexcept;

    /// Sets the header's own icons again, in the theme's colours.
    void refreshIcons();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    class Grip;
    friend class Grip;

    void makeConsole();
    void makeDock();
    /// Puts the console where the drawer's state says: in the drawer, in its window, or nowhere on screen.
    void placeConsole();
    /// Shows the drawer at its height, within its limits.
    void applyHeight();
    void resizeWhileDragged(int height);
    void entryAppended(int level);
    void historyCleared();
    void setUnseen(int unseen);
    void updateHeader();
    void tellChanged();

    QWidget& m_window;
    Hooks m_hooks;
    QVBoxLayout* m_layout = nullptr;
    Grip* m_grip = nullptr;
    QPointer<LogViewerControl> m_console;
    KDDockWidgets::QtWidgets::DockWidget* m_dock = nullptr;
    QLabel* m_title = nullptr;
    an::ui::AnTag* m_tag = nullptr;
    QAction* m_tagAction = nullptr;
    QAction* m_float = nullptr;
    QAction* m_close = nullptr;
    bool m_open = false;
    bool m_floating = false;
    /// While the drawer itself opens or closes the console's window, which it is then not told of.
    bool m_placing = false;
    int m_height = 0;
    int m_warnings = 0;
    int m_unseen = 0;
};

} // namespace an::ui::observatory
