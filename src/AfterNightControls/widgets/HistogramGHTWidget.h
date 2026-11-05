// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include <QtWidgets/QScrollArea>

#include "HistogramWidget.h"

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>

#include <cstdint>

class HistogramGHTWidgetListener
{
public:
    virtual void onProtectShadowsChanged(double shadows) = 0;
    virtual void onSymmetryPointChanged(double midtones) = 0;
    virtual void onProtectHightlightsChanged(double highlights) = 0;

    virtual void onSettingsUpdated() = 0;

    virtual void onSPPickerUpdated(double value) = 0;

    // The stretch the transfer curve draws: the function and every parameter it reads.
    virtual an::GeneralizedStretchParameters stretchParameters() = 0;

    // The channel whose screen-transfer triplet the curve draws; a channel above two draws the third's.
    virtual std::uint32_t transferCurveChannel() const
    {
        return 0u;
    }
};

class AFTERNIGHTCONTROLS_API HistogramGHTWidget : public HistogramWidget
{
public:
    HistogramGHTWidget(QScrollArea* parent);

    void setTransferCurveEnabled(bool enable);
    bool getTransferCurveEnabled() const;

    void setRangeSelectorEnabled(bool enabled);
    bool getRangeSelectorEnabled() const;

    void setValues(double shadows, double sp, double highlights);
    void setProtectShadowsValue(double value);
    void setSymmetryPointValue(double value);
    void setProtectHighlightsValue(double value);

    void setSPPickerEnabled(bool enable);
    bool getSPPickerEnabled() const;

    void setListener(HistogramGHTWidgetListener* listener);

    HistogramGHTWidgetListener* getListener() const;

    ~HistogramGHTWidget();

private:
    enum class pointer_drag_status : char
    {
        no_drag = 0,
        shadows_drag,
        symmetry_point_drag,
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

    void drawSPPickerSelection(QPainter& painter);

    pointer_drag_status getPointerDragStatus(const QPoint& pos);

    int getPlotHeight() override;

    bool m_TransferCurveEnabled = true;

    bool m_RangeSelectorEnabled = false;
    QRect m_RangeSelectorArea;

    double m_ProtectShadowsValue = 0.0;
    double m_SymmetryPointValue = 0.0;
    double m_ProtectHighlightsValue = 1.0;

    pointer_drag_status m_CurrentPointerDragStatus = pointer_drag_status::no_drag;
    QPoint m_LastMouseDragLocation = QPoint(0, 0);
    bool m_MouseMiddleButtonPressed = false;

    bool m_SPPickerEnabled = false;
    QPoint m_SPPickerSelection = QPoint(0, 0);

    HistogramGHTWidgetListener* m_Listener = nullptr;
};
