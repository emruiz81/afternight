// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "HistogramWidget.h"
#include "widgets/ToneCurveModel.h"

#include <mutex>

class CurvesWidgetListener
{
public:
    // The curve as it stands after the change, a copy the listener keeps or not.
    virtual void onCurvesUpdated(const an::ui::ToneCurveModel& curve) = 0;
};

// Edits a tone curve over its owner's histogram. The control points are the widget's; the engine evaluates the curve.
class AFTERNIGHTCONTROLS_API CurvesWidget : public HistogramWidget
{
public:
    CurvesWidget(QScrollArea* parent);

    void setGuideLineEnabled(bool enabled);
    bool getGuideLineEnabled() const;

    void setHistogramEnabled(bool enabled);
    bool getHistogramEnabled() const;

    void setCurve(const an::ui::ToneCurveModel& curve);

    // A copy of the curve, which a worker may take while the user keeps editing.
    an::ui::ToneCurveModel getCurve();

    // Whether the curve is the straight line through (0, 0) and (1, 1) alone.
    bool curveIsIdentity();

    void setCurveInterpolation(an::CurveInterpolation interpolation);

    an::CurveInterpolation getCurveInterpolation();

    void setListener(CurvesWidgetListener* listener);

    CurvesWidgetListener* getListener() const;

    [[nodiscard]] QColor getSplineColor() const;

    ~CurvesWidget();

protected:
    void paintEvent(QPaintEvent* evt) override;
    void resizeEvent(QResizeEvent* evt) override;

    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;

    void drawSplines(QPainter& painter);

    int getCurrentNodeSelected(const QPoint& pos, bool testOnly);

    void splineUpdated();

    void update_spline_to_render();

    bool m_GuideLineEnabled = true;
    bool m_HistogramEnabled = true;

    // -1: No node selected, >= 0: node idx n spline array
    int m_CurrentNodeSelected = -1;

    std::recursive_mutex m_mutex;
    an::ui::ToneCurveModel m_curve;

    CurvesWidgetListener* m_Listener = nullptr;

    std::vector<QPoint> m_spline_to_render;
    std::vector<QPoint> m_nodes_to_render;

private:
};
