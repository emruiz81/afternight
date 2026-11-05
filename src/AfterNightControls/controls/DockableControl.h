// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QWidget>
#include <kddockwidgets/DockWidget.h>

#include <verdigris/wobjectdefs.h>

class AN_TabBar;
class AN_Stack;
class DockableControl : public QWidget
{
public:
    enum class DockableControlType
    {
        IMAGE_VIEW_CONTROL = 0,
        CONSOLE_LOG
    };

    DockableControl(DockableControlType type, QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

    virtual KDDockWidgets::QtWidgets::DockWidget* getDockWidget() = 0;

    AN_TabBar* getTabBar();
    AN_Stack* getStack();

    [[nodiscard]] DockableControlType getType() const;

    virtual ~DockableControl();

protected:
    void updateTabBarHighlight(bool focused);

    KDDockWidgets::QtWidgets::DockWidget* m_DockWidget = nullptr;

    DockableControlType m_DockableControlType = DockableControlType::IMAGE_VIEW_CONTROL;

private:
};

class CustomDockWidget : public KDDockWidgets::QtWidgets::DockWidget
{
    W_OBJECT(CustomDockWidget) // Replaces Q_OBJECT

public:
    explicit CustomDockWidget(const QString& uniqueName, KDDockWidgets::DockWidgetOptions options = {},
                              KDDockWidgets::LayoutSaverOptions layoutSaverOptions = {},
                              Qt::WindowFlags windowFlags = Qt::Tool);

    void closeRequested() W_SIGNAL(closeRequested); // Replaces the Qt signal macro

protected:
    void closeEvent(QCloseEvent* ev) override;
};
