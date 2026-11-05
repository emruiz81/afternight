// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <kddockwidgets/qtwidgets/views/TabBar.h>
#include <kddockwidgets/qtwidgets/views/TitleBar.h>
#include <kddockwidgets/qtwidgets/views/Group.h>
#include <kddockwidgets/qtwidgets/views/FloatingWindow.h>
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

#include <kddockwidgets/KDDockWidgets.h>

class QEvent;
class QMouseEvent;
class QPaintEvent;

/// Visual-Studio-style flat button for KDDW tab bar corners and title bars.
/// Draws close (×) or float (⧉) glyphs programmatically — no icons, no QToolButton chrome.
/// Hover/press backgrounds follow the design-token system.
class AN_DockButton : public QAbstractButton
{
public:
    explicit AN_DockButton(KDDockWidgets::TitleBarButtonType type, QWidget* parent = nullptr);

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
};

class AN_TabBar : public KDDockWidgets::QtWidgets::TabBar
{
    W_OBJECT(AN_TabBar) // Replaces Q_OBJECT

public:
    explicit AN_TabBar(KDDockWidgets::Core::TabBar* controller, KDDockWidgets::Core::View* parent);
    void applyThemeColors();

    // Force all tabs to render as "unselected" (visual only)
    void setForceUnselected(bool enabled);
    bool isForceUnselected() const;

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
};

class AN_TitleBar : public KDDockWidgets::QtWidgets::TitleBar
{
    W_OBJECT(AN_TitleBar) // Replaces Q_OBJECT

public:
    explicit AN_TitleBar(KDDockWidgets::Core::TitleBar* controller, KDDockWidgets::Core::View* parent = nullptr);

protected:
    bool event(QEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
};

class AN_Stack : public KDDockWidgets::QtWidgets::Stack
{
    W_OBJECT(AN_Stack) // Replaces Q_OBJECT

public:
    AN_Stack(KDDockWidgets::Core::Stack* controller, KDDockWidgets::Core::View* parent = nullptr);
    void styleCornerButtons();
    void applyThemeColors();

protected:
    void init() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
};

/// Token-aware group frame — replaces hardcoded border colors in KDDockWidgets::QtWidgets::Group.
class AN_Group : public KDDockWidgets::QtWidgets::Group
{
public:
    explicit AN_Group(KDDockWidgets::Core::Group* controller, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

/// Token-aware floating window — replaces hardcoded border colors in KDDockWidgets::QtWidgets::FloatingWindow.
class AN_FloatingWindow : public KDDockWidgets::QtWidgets::FloatingWindow
{
public:
    explicit AN_FloatingWindow(KDDockWidgets::Core::FloatingWindow* controller, QMainWindow* parent = nullptr,
                               Qt::WindowFlags windowFlags = {});

protected:
    void paintEvent(QPaintEvent* event) override;
};

class AN_ViewFactory : public KDDockWidgets::QtWidgets::ViewFactory
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

    /// Return token-tinted SVG icons instead of KDDW's bundled PNGs.
    QIcon iconForButtonType(KDDockWidgets::TitleBarButtonType type, qreal dpr) const override;

    /// Return VS-style flat buttons instead of KDDW's default QToolButtons.
    QAbstractButton* createTitleBarButton(QWidget* parent, KDDockWidgets::TitleBarButtonType type) const override;

    /// Invalidate the icon cache (call on theme change so icons are regenerated).
    void clearIconCache();

private:
    mutable QMap<std::pair<KDDockWidgets::TitleBarButtonType, qreal>, QIcon> m_anCachedIcons;
};
