// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QScrollArea>
#include <QColor>
#include <set>

class ImageViewControl;
class ImageViewScrollArea;
class ImageViewControlListener;
class QTabletEvent;

class AFTERNIGHTCONTROLS_API ImageViewScrollAreaListener
{
public:
    ImageViewScrollAreaListener(ImageViewScrollArea* parent);

    virtual bool onWheelEvent(QWheelEvent* ev)
    {
        return false;
    }
    virtual bool onMousePressEvent(QMouseEvent* ev)
    {
        return false;
    }
    virtual bool onMouseDoubleClickEvent(QMouseEvent* ev)
    {
        return false;
    }
    virtual bool onMouseReleaseEvent(QMouseEvent* ev)
    {
        return false;
    }
    virtual bool onMouseMoveEvent(QMouseEvent* ev)
    {
        return false;
    }
    virtual bool onTabletEvent(QTabletEvent* ev)
    {
        return false;
    }

    void onImageViewScrollAreaDestroyed();

    virtual void setListenerEnabled(const bool enabled);

    bool isListenerEnabled() const;

    ~ImageViewScrollAreaListener();

protected:
    ImageViewControl* m_ViewControl = nullptr;
    ImageViewScrollArea* m_ParentArea = nullptr;

    bool m_IsListenerEnabled = true;

private:
};

class AFTERNIGHTCONTROLS_API ImageViewScrollArea : public QScrollArea
{
public:
    ImageViewScrollArea(QWidget* parent = nullptr);

    void setViewControl(ImageViewControl* control);
    ImageViewControl* getViewControl();

    void restoreDefaultCursor();
    void setCursor(const QCursor& cursor);

    void subscribeListener(ImageViewScrollAreaListener* listener);
    void unsubscribeListener(ImageViewScrollAreaListener* listener);

    void setWidget(QWidget* widget);
    void setOverlayWidget(QWidget* widget);
    void refreshOverlayWidget();
    [[nodiscard]] QColor overlayWindowColor() const;

    ~ImageViewScrollArea();

private:
    void wheelEvent(QWheelEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void tabletEvent(QTabletEvent* ev) override;
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* event) override;
    void applyTokenPalette();

    ImageViewControl* m_ViewControl = nullptr;
    QPoint m_LastMouseDragLocation = QPoint(0, 0);

    std::set<ImageViewScrollAreaListener*> m_Listeners;

    QWidget* m_OverlayWidget = nullptr;
};
