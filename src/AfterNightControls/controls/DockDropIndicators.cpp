// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/DockDropIndicators.h"

#include "theme/ObservatoryDock.h"

#include <QGuiApplication>
#include <QPainter>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/qtcommon/View.h>

#include <array>

namespace
{

using KDDockWidgets::DropLocation;

constexpr std::array<DropLocation, 9> kLocations = {
    KDDockWidgets::DropLocation_Center,       KDDockWidgets::DropLocation_Left,
    KDDockWidgets::DropLocation_Top,          KDDockWidgets::DropLocation_Right,
    KDDockWidgets::DropLocation_Bottom,       KDDockWidgets::DropLocation_OutterLeft,
    KDDockWidgets::DropLocation_OutterTop,    KDDockWidgets::DropLocation_OutterRight,
    KDDockWidgets::DropLocation_OutterBottom,
};

/// On Wayland a window cannot be placed: the indicators are a child of the dock area there, as the library's are.
bool indicatorsInsideTheArea()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

QWidget* parentFor(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators)
{
    return indicatorsInsideTheArea() ? KDDockWidgets::QtCommon::View_qt::asQWidget(indicators->view()) : nullptr;
}

Qt::WindowFlags flagsFor()
{
    return indicatorsInsideTheArea() ? Qt::Widget : (Qt::Tool | Qt::BypassWindowManagerHint | Qt::FramelessWindowHint);
}

an::ui::ObservatoryDropSide sideOf(const DropLocation location)
{
    switch (location)
    {
    case KDDockWidgets::DropLocation_Left:
    case KDDockWidgets::DropLocation_OutterLeft:
        return an::ui::ObservatoryDropSide::Left;
    case KDDockWidgets::DropLocation_Right:
    case KDDockWidgets::DropLocation_OutterRight:
        return an::ui::ObservatoryDropSide::Right;
    case KDDockWidgets::DropLocation_Top:
    case KDDockWidgets::DropLocation_OutterTop:
        return an::ui::ObservatoryDropSide::Top;
    case KDDockWidgets::DropLocation_Bottom:
    case KDDockWidgets::DropLocation_OutterBottom:
        return an::ui::ObservatoryDropSide::Bottom;
    default:
        return an::ui::ObservatoryDropSide::Centre;
    }
}

} // namespace

// ---------------- AN_DropIndicators ----------------

AN_DropIndicators::AN_DropIndicators(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators)
    : QWidget(parentFor(indicators), flagsFor()), m_indicators(indicators)
{
    if (KDDockWidgets::Config::self().flags() & KDDockWidgets::Config::Flag_KeepAboveIfNotUtilityWindow)
    {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
    }
    setAttribute(Qt::WA_TranslucentBackground);
}

QRect AN_DropIndicators::targetRect(const DropLocation location) const
{
    if (m_indicators == nullptr || !m_indicators->dropIndicatorVisible(location))
    {
        return {};
    }
    const int side = an::ui::observatoryDropTargetSide();
    const int pitch = an::ui::observatoryDropTargetPitch();
    const int margin = an::ui::observatoryDropTargetMargin();
    const QRect area = rect();

    switch (location)
    {
    case KDDockWidgets::DropLocation_OutterLeft:
        return {area.left() + margin, area.top() + (area.height() - side) / 2, side, side};
    case KDDockWidgets::DropLocation_OutterRight:
        return {area.left() + area.width() - margin - side, area.top() + (area.height() - side) / 2, side, side};
    case KDDockWidgets::DropLocation_OutterTop:
        return {area.left() + (area.width() - side) / 2, area.top() + margin, side, side};
    case KDDockWidgets::DropLocation_OutterBottom:
        return {area.left() + (area.width() - side) / 2, area.top() + area.height() - margin - side, side, side};
    default:
        break;
    }

    // The cross stands over the group under the cursor.
    if (m_indicators->hoveredGroup() == nullptr)
    {
        return {};
    }
    const QRect centre(m_indicators->hoveredGroupRect().center() - QPoint(side / 2, side / 2), QSize(side, side));
    switch (location)
    {
    case KDDockWidgets::DropLocation_Center:
        return centre;
    case KDDockWidgets::DropLocation_Left:
        return centre.translated(-pitch, 0);
    case KDDockWidgets::DropLocation_Right:
        return centre.translated(pitch, 0);
    case KDDockWidgets::DropLocation_Top:
        return centre.translated(0, -pitch);
    case KDDockWidgets::DropLocation_Bottom:
        return centre.translated(0, pitch);
    default:
        return {};
    }
}

DropLocation AN_DropIndicators::hover(QPoint globalPos)
{
    const QPoint local = mapFromGlobal(globalPos);
    DropLocation found = KDDockWidgets::DropLocation_None;
    for (const DropLocation location : kLocations)
    {
        if (targetRect(location).contains(local))
        {
            found = location;
            break;
        }
    }
    if (found != m_hovered)
    {
        m_hovered = found;
        update();
    }
    return found;
}

QPoint AN_DropIndicators::posForIndicator(DropLocation location) const
{
    return mapToGlobal(targetRect(location).center());
}

void AN_DropIndicators::updatePositions()
{
    update();
}

void AN_DropIndicators::raise()
{
    QWidget::raise();
}

void AN_DropIndicators::setVisible(bool visible)
{
    if (!visible)
    {
        m_hovered = KDDockWidgets::DropLocation_None;
    }
    QWidget::setVisible(visible);
}

void AN_DropIndicators::resize(QSize size)
{
    QWidget::resize(size);
}

void AN_DropIndicators::setGeometry(QRect geometry)
{
    QWidget::setGeometry(geometry);
}

bool AN_DropIndicators::isWindow() const
{
    return QWidget::isWindow();
}

void AN_DropIndicators::setObjectName(const QString& name)
{
    QWidget::setObjectName(name);
}

void AN_DropIndicators::updateIndicatorVisibility()
{
    update();
}

void AN_DropIndicators::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    const DropLocation current =
        m_indicators != nullptr ? m_indicators->currentDropLocation() : KDDockWidgets::DropLocation_None;
    for (const DropLocation location : kLocations)
    {
        if (const QRect target = targetRect(location); !target.isEmpty())
        {
            an::ui::paintObservatoryDropTarget(painter, target, sideOf(location), location == current);
        }
    }
}

// ---------------- AN_DropBand ----------------

AN_DropBand::AN_DropBand(QWidget* parent) : KDDockWidgets::QtWidgets::RubberBand(parent)
{
    if (parent == nullptr)
    {
        setAttribute(Qt::WA_TranslucentBackground);
    }
}

void AN_DropBand::paintEvent(QPaintEvent* event)
{
    if (an::ui::observatoryDockDecorations())
    {
        QPainter painter(this);
        an::ui::paintObservatoryDropBand(painter, rect());
        return;
    }
    KDDockWidgets::QtWidgets::RubberBand::paintEvent(event);
}
