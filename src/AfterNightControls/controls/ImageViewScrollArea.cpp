// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewScrollArea.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "controls/ImageViewThemeColors.h"
#include "common.h"
#include "theme/AnDesignTokens.h"

ImageViewScrollAreaListener::ImageViewScrollAreaListener(ImageViewScrollArea* parent) : m_ParentArea(parent)
{
    if (m_ParentArea)
    {
        m_ParentArea->subscribeListener(this);
        m_ViewControl = m_ParentArea->getViewControl();
    }
}

void ImageViewScrollAreaListener::onImageViewScrollAreaDestroyed()
{
    m_ParentArea = nullptr;
    m_ViewControl = nullptr;
}

void ImageViewScrollAreaListener::setListenerEnabled(const bool enabled)
{
    m_IsListenerEnabled = enabled;
}

bool ImageViewScrollAreaListener::isListenerEnabled() const
{
    return m_IsListenerEnabled;
}

ImageViewScrollAreaListener::~ImageViewScrollAreaListener()
{
    if (m_ParentArea)
        m_ParentArea->unsubscribeListener(this);
}

ImageViewScrollArea::ImageViewScrollArea(QWidget* parent) : QScrollArea(parent)
{
    applyTokenPalette();
    restoreDefaultCursor();
}

void ImageViewScrollArea::setViewControl(ImageViewControl* control)
{
    m_ViewControl = control;
}

ImageViewControl* ImageViewScrollArea::getViewControl()
{
    return m_ViewControl;
}

void ImageViewScrollArea::restoreDefaultCursor()
{
    auto w = this->widget();
    if (w)
    {
        w->setCursor(createSolidCrossCursor());
    }
}

void ImageViewScrollArea::setCursor(const QCursor& cursor)
{
    auto w = this->widget();
    if (w)
    {
        w->setCursor(cursor);
    }
}

void ImageViewScrollArea::subscribeListener(ImageViewScrollAreaListener* listener)
{
    m_Listeners.insert(listener);
}

void ImageViewScrollArea::unsubscribeListener(ImageViewScrollAreaListener* listener)
{
    m_Listeners.erase(listener);
}

void ImageViewScrollArea::setWidget(QWidget* widget)
{
    QScrollArea::setWidget(widget);
    restoreDefaultCursor();
}

void ImageViewScrollArea::setOverlayWidget(QWidget* widget)
{
    m_OverlayWidget = widget;
    refreshOverlayWidget();
}

void ImageViewScrollArea::refreshOverlayWidget()
{
    applyTokenPalette();

    QWidget* vp = viewport();

    if (m_OverlayWidget)
    {
        // Ensure overlay is a child of the viewport so it renders above the scrolled content
        // and stays visible even when the content fills the entire viewport.

        if (m_OverlayWidget->parentWidget() != vp)
            m_OverlayWidget->setParent(vp);

        // Keep it on top
        m_OverlayWidget->raise();

        if (m_OverlayWidget->isEnabled())
            m_OverlayWidget->show();
        else
            m_OverlayWidget->hide();

        m_OverlayWidget->update();
    }

    if (vp)
        vp->update();

    update();
}

ImageViewScrollArea::~ImageViewScrollArea()
{
    for (auto& listener : m_Listeners)
    {
        listener->onImageViewScrollAreaDestroyed();
    }
}

QColor ImageViewScrollArea::overlayWindowColor() const
{
    return an::ui::imageViewScrollAreaWindowColor(an::ui::isDarkTheme());
}

void ImageViewScrollArea::wheelEvent(QWheelEvent* ev)
{
    bool eventAccepted = false;

    for (auto& listener : m_Listeners)
    {
        if (listener->onWheelEvent(ev))
            eventAccepted = true;
    }

    if (!eventAccepted)
        QScrollArea::wheelEvent(ev);
}

void ImageViewScrollArea::mousePressEvent(QMouseEvent* ev)
{
    bool eventAccepted = false;

    for (auto& listener : m_Listeners)
    {
        if (listener->onMousePressEvent(ev))
            eventAccepted = true;
    }

    if (!eventAccepted)
        QScrollArea::mousePressEvent(ev);
}

void ImageViewScrollArea::mouseDoubleClickEvent(QMouseEvent* ev)
{
    bool eventAccepted = false;

    for (auto& listener : m_Listeners)
    {
        if (listener->onMouseDoubleClickEvent(ev))
            eventAccepted = true;
    }

    if (!eventAccepted)
        QScrollArea::mouseDoubleClickEvent(ev);
}

void ImageViewScrollArea::mouseReleaseEvent(QMouseEvent* ev)
{
    bool eventAccepted = false;

    for (auto& listener : m_Listeners)
    {
        if (listener->onMouseReleaseEvent(ev))
            eventAccepted = true;
    }

    if (!eventAccepted)
        QScrollArea::mouseReleaseEvent(ev);
}

void ImageViewScrollArea::mouseMoveEvent(QMouseEvent* ev)
{
    bool eventAccepted = false;

    for (auto& listener : m_Listeners)
    {
        if (listener->onMouseMoveEvent(ev))
            eventAccepted = true;
    }

    if (!eventAccepted)
        QScrollArea::mouseMoveEvent(ev);
}

void ImageViewScrollArea::tabletEvent(QTabletEvent* ev)
{
    bool eventAccepted = false;

    for (auto& listener : m_Listeners)
    {
        if (listener->onTabletEvent(ev))
            eventAccepted = true;
    }

    if (!eventAccepted)
        QScrollArea::tabletEvent(ev);
}

void ImageViewScrollArea::paintEvent(QPaintEvent* ev)
{
    applyTokenPalette();
    QScrollArea::paintEvent(ev);
    if (m_OverlayWidget)
    {
        // Keep overlay above the content
        m_OverlayWidget->raise();
        if (m_OverlayWidget->isEnabled())
        {
            m_OverlayWidget->show();
        }
        else
        {
            m_OverlayWidget->hide();
        }
    }
}

void ImageViewScrollArea::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    if (m_OverlayWidget)
    {
        // Keep overlay above the content
        m_OverlayWidget->raise();
        if (m_OverlayWidget->isEnabled())
        {
            m_OverlayWidget->show();
        }
        else
        {
            m_OverlayWidget->hide();
        }
    }
}

void ImageViewScrollArea::applyTokenPalette()
{
    const QColor windowColor = overlayWindowColor();

    // This runs from the paint event, so a theme switch applies at the next paint. Each role, colour and fill is set
    // only when it differs: setting a background role re-sends the palette change even when the role is the one the
    // widget has, and a palette change asks for another paint, so a role set on every paint repaints for ever.
    const auto apply = [&windowColor](QWidget* widget) {
        if (widget->backgroundRole() != QPalette::Dark)
        {
            widget->setBackgroundRole(QPalette::Dark);
        }
        QPalette palette = widget->palette();
        bool changed = false;
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        {
            for (const QPalette::ColorRole role : {QPalette::Window, QPalette::Base, QPalette::Dark})
            {
                if (palette.color(group, role) != windowColor)
                {
                    palette.setColor(group, role, windowColor);
                    changed = true;
                }
            }
        }
        if (changed)
        {
            widget->setPalette(palette);
        }
        if (!widget->autoFillBackground())
        {
            widget->setAutoFillBackground(true);
        }
    };

    apply(this);
    if (QWidget* vp = viewport())
    {
        apply(vp);
    }
}
