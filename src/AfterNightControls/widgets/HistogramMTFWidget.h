// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include <QtWidgets/QScrollArea>

#include "HistogramWidget.h"

class HistogramMTFWidgetListener
{
public:
    virtual void onShadowsChanged(double shadows) = 0;
    virtual void onMidtonesChanged(double midtones) = 0;
    virtual void onHightlightsChanged(double highlights) = 0;

    virtual void onSettingsUpdated() = 0;
};

class AFTERNIGHTCONTROLS_API HistogramMTFWidget : public HistogramWidget
{
public:
    HistogramMTFWidget(QScrollArea* parent);

    void setTransferCurveEnabled(bool enable);
    bool getTransferCurveEnabled() const;

    void setRangeSelectorEnabled(bool enabled);
    bool getRangeSelectorEnabled() const;

    void setValues(double shadows, double midtones, double highlights);
    void setShadowsValue(double value);
    void setMidtonesValue(double value);
    void setHighlightsValue(double value);

    void setListener(HistogramMTFWidgetListener* listener);

    HistogramMTFWidgetListener* getListener() const;

    ~HistogramMTFWidget();

private:
    enum class pointer_drag_status : char
    {
        no_drag = 0,
        shadows_drag,
        midtones_drag,
        highlights_drag
    };

    void paintEvent(QPaintEvent* evt) override;
    void resizeEvent(QResizeEvent* evt) override;

    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;

    void drawTransferCurve(QPainter& painter);

    void drawRangeSelector(QPainter& painter);
    void drawRangeSelectorPointer(QPainter& painter, QPoint& pos);

    double getRelativeMidtones(double absMidtones);
    double getAbsoluteMidtones();

    pointer_drag_status getPointerDragStatus(const QPoint& pos);

    int getPlotHeight() override;

    bool m_TransferCurveEnabled = true;

    bool m_RangeSelectorEnabled = false;
    QRect m_RangeSelectorArea;

    double m_ShadowsValue = 0.0;
    double m_MidtonesValue = 0.5;
    double m_HighlightsValue = 1.0;

    pointer_drag_status m_CurrentPointerDragStatus = pointer_drag_status::no_drag;
    QPoint m_LastMouseDragLocation = QPoint(0, 0);
    bool m_MouseMiddleButtonPressed = false;

    HistogramMTFWidgetListener* m_Listener = nullptr;
};
