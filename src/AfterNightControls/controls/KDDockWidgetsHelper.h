// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "theme/ObservatoryDock.h"

#include <kddockwidgets/qtwidgets/views/TabBar.h>
#include <kddockwidgets/qtwidgets/views/TitleBar.h>
#include <kddockwidgets/qtwidgets/views/Group.h>
#include <kddockwidgets/qtwidgets/views/FloatingWindow.h>
#include <kddockwidgets/qtwidgets/views/Separator.h>
#include <kddockwidgets/qtwidgets/ViewFactory.h>
#include <kddockwidgets/qtwidgets/View.h>
#include <kddockwidgets/qtwidgets/Stack.h>

#include <kddockwidgets/core/TitleBar.h>
#include <kddockwidgets/core/TabBar.h>
#include <kddockwidgets/core/Stack.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/FloatingWindow.h>

#include <verdigris/wobjectdefs.h>

#include <QIcon>
#include <QMap>
#include <QAbstractButton>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>

#include <array>
#include <functional>

class QEvent;
class QMouseEvent;
class QPaintEvent;

namespace KDDockWidgets::QtWidgets
{
class DockWidget;
class MainWindow;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui
{

/// What a tab shows after its view's name: an icon in a small tonal square, with its own tooltip, that runs `activated`
/// when it is clicked; `locked` disables the tab's close button and, while the tab is current, the strip's buttons.
struct TabAdornment
{
    QString iconKey; ///< The icon, by the name the application's icon provider knows it by.
    QString toolTip;
    bool locked = false;
    std::function<void()> activated;
};

/// Sets the adornment of a dock widget's tab, replacing the one it had.
AFTERNIGHTCONTROLS_API void setTabAdornment(KDDockWidgets::QtWidgets::DockWidget* dockWidget, TabAdornment adornment);

/// Takes a dock widget's adornment away; nothing when it has none.
AFTERNIGHTCONTROLS_API void clearTabAdornment(KDDockWidgets::QtWidgets::DockWidget* dockWidget);

/// The adornment of a dock widget's tab, or null.
[[nodiscard]] AFTERNIGHTCONTROLS_API const TabAdornment*
tabAdornment(const KDDockWidgets::QtWidgets::DockWidget* dockWidget);

/// Gives the dock library the decorations' separator: groups lie two pixels apart. The library takes it only while no
/// dock widget or main window exists, so the application gives it once the profile is set and before the shell makes a
/// window.
AFTERNIGHTCONTROLS_API void applyDockDecorationsProfile();

/// Marks a dock area locked, or free again. The decorations refuse a double click that would float or dock a view in a
/// locked one, as they do everywhere while the whole layout is locked (the dock library's drop indicators inhibited).
AFTERNIGHTCONTROLS_API void setDockAreaLocked(KDDockWidgets::QtWidgets::MainWindow& area, bool locked);

/// Whether the arrangement is not the person's to change where `widget` is: the whole layout is locked, or the dock
/// area `widget` is in - its first parent that is one, short of a window of its own - is marked locked. A floating
/// window belongs to no dock area, though the library parents it to one.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool layoutChangesLockedAt(const QWidget& widget);

} // namespace an::ui

/// The dock library's flags the decorations are drawn for: the tabs always shown, reordered, closed and switched from
/// their strip, the title bar hidden while the tabs show and focusable, and a floating window under the platform's own
/// title bar. The shell core's `DockHooks` sets them for the process before the first main window is made, with
/// `kDraggedDockWindowOpacity`, `kDockWidgetMinimumSide` and `AN_ViewFactory`; the docking suite sets the same.
inline constexpr std::array<KDDockWidgets::Config::Flag, 8> kDockLibraryFlags = {
    KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible,
    KDDockWidgets::Config::Flag_AlwaysShowTabs,
    KDDockWidgets::Config::Flag_AllowReorderTabs,
    KDDockWidgets::Config::Flag_TabsHaveCloseButton,
    KDDockWidgets::Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden,
    KDDockWidgets::Config::Flag_AllowSwitchingTabsViaMenu,
    KDDockWidgets::Config::Flag_NativeTitleBar,
    KDDockWidgets::Config::Flag_TitleBarIsFocusable,
};

/// The opacity of a window while it is dragged.
inline constexpr double kDraggedDockWindowOpacity = 0.7;

/// The smallest side a dock widget is given, in pixels.
inline constexpr int kDockWidgetMinimumSide = 100;

/// Visual-Studio-style flat button for KDDW tab bar corners and title bars.
/// Draws close (×) or float (⧉) glyphs programmatically — no icons, no QToolButton chrome.
/// Hover/press backgrounds follow the design-token system.
class AN_DockButton : public QAbstractButton
{
public:
    explicit AN_DockButton(KDDockWidgets::TitleBarButtonType type, QWidget* parent = nullptr);

    /// The button that lists a strip's tabs.
    [[nodiscard]] static AN_DockButton* makeTabList(QWidget* parent);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    KDDockWidgets::TitleBarButtonType m_type;
    bool m_hovered = false;
    bool m_tabList = false;
};

/// A group's tab strip. The dock style draws it (`theme/KddwProxyStyle.h`): its tabs are
/// sized from the tokens, a floating image window's strip is the smaller one, a name too long is elided in the middle
/// and never cut under twelve characters, the whole name is the tooltip, and a tab's adornment is drawn and clicked.
class AFTERNIGHTCONTROLS_API AN_TabBar : public KDDockWidgets::QtWidgets::TabBar,
                                         public an::ui::ObservatoryDockTabSource
{
    W_OBJECT(AN_TabBar) // Replaces Q_OBJECT

public:
    explicit AN_TabBar(KDDockWidgets::Core::TabBar* controller, KDDockWidgets::Core::View* parent);
    void applyThemeColors();

    // Force all tabs to render as "unselected" (visual only)
    void setForceUnselected(bool enabled);
    bool isForceUnselected() const;

    /// What the tab at an index shows: its name without the mark of unsaved changes, that mark, its icon, its
    /// adornment.
    [[nodiscard]] an::ui::ObservatoryDockTab observatoryDockTab(int index) const override;

    /// Where a tab's adornment lies; empty when it has none.
    [[nodiscard]] QRect adornmentRect(int index) const;

    /// The tooltip at a place: an adornment's own, or the whole name of the tab there and the image's summary after it;
    /// nothing past the tabs.
    [[nodiscard]] QString toolTipAt(QPoint position) const;

    /// Lays the tabs out again: a tab's parts, the density or the strip's height changed.
    void refreshTabs();

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    /// Whether the strip stands in a floating window: its height is then the smaller strip's.
    [[nodiscard]] bool inFloatingWindow() const;

    /// The tab whose adornment was pressed, until the release.
    int m_pressedAdornment = -1;
};

class AFTERNIGHTCONTROLS_API AN_TitleBar : public KDDockWidgets::QtWidgets::TitleBar
{
    W_OBJECT(AN_TitleBar) // Replaces Q_OBJECT

public:
    explicit AN_TitleBar(KDDockWidgets::Core::TitleBar* controller, KDDockWidgets::Core::View* parent = nullptr);

    QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
};

class AFTERNIGHTCONTROLS_API AN_Stack : public KDDockWidgets::QtWidgets::Stack
{
    W_OBJECT(AN_Stack) // Replaces Q_OBJECT

public:
    AN_Stack(KDDockWidgets::Core::Stack* controller, KDDockWidgets::Core::View* parent = nullptr);
    void styleCornerButtons();
    void applyThemeColors();

    /// Disables a locked tab's close button and, while the current tab is locked, the strip's buttons; enables what no
    /// lock holds.
    void applyTabLocks();

protected:
    void init() override;
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    /// The strip's buttons: their size and place for the strip's height, their tooltips, the list of the tabs shown
    /// with more than one.
    void applyObservatoryStrip();
    void showTabList();

    AN_DockButton* m_tabList = nullptr;
};

/// Token-aware group frame — replaces hardcoded border colors in KDDockWidgets::QtWidgets::Group.
class AFTERNIGHTCONTROLS_API AN_Group : public KDDockWidgets::QtWidgets::Group
{
public:
    explicit AN_Group(KDDockWidgets::Core::Group* controller, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

/// Token-aware floating window — replaces hardcoded border colors in KDDockWidgets::QtWidgets::FloatingWindow.
class AFTERNIGHTCONTROLS_API AN_FloatingWindow : public KDDockWidgets::QtWidgets::FloatingWindow
{
public:
    explicit AN_FloatingWindow(KDDockWidgets::Core::FloatingWindow* controller, QMainWindow* parent = nullptr,
                               Qt::WindowFlags windowFlags = {});

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
};

/// The line between two groups: the dock area's ground, the accent's line while the pointer is over it.
class AFTERNIGHTCONTROLS_API AN_Separator : public KDDockWidgets::QtWidgets::Separator
{
public:
    explicit AN_Separator(KDDockWidgets::Core::Separator* controller, KDDockWidgets::Core::View* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool m_hovered = false;
};

class AFTERNIGHTCONTROLS_API AN_ViewFactory : public KDDockWidgets::QtWidgets::ViewFactory
{
public:
    using KDDockWidgets::QtWidgets::ViewFactory::ViewFactory;

    KDDockWidgets::Core::View* createTabBar(KDDockWidgets::Core::TabBar* controller,
                                            KDDockWidgets::Core::View* parent) const override;
    KDDockWidgets::Core::View* createTitleBar(KDDockWidgets::Core::TitleBar* controller,
                                              KDDockWidgets::Core::View* parent) const override;
    KDDockWidgets::Core::View* createStack(KDDockWidgets::Core::Stack* controller,
                                           KDDockWidgets::Core::View* parent) const override;
    KDDockWidgets::Core::View* createGroup(KDDockWidgets::Core::Group* controller,
                                           KDDockWidgets::Core::View* parent) const override;
    KDDockWidgets::Core::View* createFloatingWindow(KDDockWidgets::Core::FloatingWindow* controller,
                                                    KDDockWidgets::Core::MainWindow* parent = nullptr,
                                                    Qt::WindowFlags windowFlags = {}) const override;
    KDDockWidgets::Core::View* createSeparator(KDDockWidgets::Core::Separator* controller,
                                               KDDockWidgets::Core::View* parent = nullptr) const override;

    /// The product's indicator window and band (`controls/DockDropIndicators.h`) in place of the library's.
    KDDockWidgets::Core::ClassicIndicatorWindowViewInterface*
    createClassicIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators,
                                 KDDockWidgets::Core::View* parent) const override;
    KDDockWidgets::Core::View* createRubberBand(KDDockWidgets::Core::View* parent) const override;

    /// Return token-tinted SVG icons instead of KDDW's bundled PNGs.
    QIcon iconForButtonType(KDDockWidgets::TitleBarButtonType type, qreal dpr) const override;

    /// Return VS-style flat buttons instead of KDDW's default QToolButtons.
    QAbstractButton* createTitleBarButton(QWidget* parent, KDDockWidgets::TitleBarButtonType type) const override;

    /// Invalidate the icon cache (call on theme change so icons are regenerated).
    void clearIconCache();

private:
    mutable QMap<std::pair<KDDockWidgets::TitleBarButtonType, qreal>, QIcon> m_anCachedIcons;
};
