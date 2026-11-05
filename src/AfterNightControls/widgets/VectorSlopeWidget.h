// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QCborMap>
#include <QByteArray>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <vector>

class QMouseEvent;
class QPainter;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API VectorSlopeWidget : public QWidget
{
    W_OBJECT(VectorSlopeWidget)

public:
    struct VectorDefinition
    {
        QString key;
        QString hue_param;
        QString saturation_param;
        QByteArray property_name;
        double base_hue_degrees = 0.0;
        double saturation_scale = 100.0;
        QColor color;
    };

    explicit VectorSlopeWidget(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

    void setGraphHeight(int height);
    void setVectorDefinitions(std::vector<VectorDefinition> definitions);
    [[nodiscard]] const std::vector<VectorDefinition>& vectorDefinitions() const noexcept;

    void setVectorsFromParams(const QCborMap& params);
    [[nodiscard]] double hueShiftDegrees(const QString& key) const;
    [[nodiscard]] double saturation(const QString& key) const;

    void setLayerColorPickerEnabled(bool enabled);
    [[nodiscard]] bool isLayerColorPickerEnabled() const noexcept;
    void setLayerColor(double hue_degrees, double saturation);
    [[nodiscard]] double layerHueDegrees() const noexcept;
    [[nodiscard]] double layerSaturation() const noexcept;

    [[nodiscard]] static std::vector<VectorDefinition> defaultHueWheelVectors();

signals:
    void layerColorChanged(double hue_degrees, double saturation) W_SIGNAL(layerColorChanged, hue_degrees, saturation);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct VectorValue
    {
        VectorDefinition definition;
        double hue_shift_degrees = 0.0;
        double saturation = 0.0;
    };

    [[nodiscard]] static double numericParam(const QCborMap& params, const QString& key, double fallback);
    [[nodiscard]] bool pickLayerColorAt(const QPointF& point, bool emit_signal);
    void paintLayerColorPicker(QPainter& painter, const QRectF& bounds);
    void paintVectorSlope(QPainter& painter, const QRectF& bounds);
    void publishVectorProperties();

    int graph_height_ = 220;
    std::vector<VectorDefinition> definitions_;
    std::vector<VectorValue> vectors_;
    bool layer_color_picker_enabled_ = false;
    double layer_hue_degrees_ = 0.0;
    double layer_saturation_ = 0.5;
};

} // namespace an::ui
