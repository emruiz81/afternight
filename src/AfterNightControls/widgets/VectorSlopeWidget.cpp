// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "VectorSlopeWidget.h"

#include "theme/AnDesignTokens.h"

#include <QBrush>
#include <QCborValue>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QSizePolicy>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>

namespace an::ui
{

W_OBJECT_IMPL(an::ui::VectorSlopeWidget)

namespace
{
constexpr double kFullCircleDegrees = 360.0;
constexpr double kHalfCircleDegrees = 180.0;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

struct LayerPickerGeometry
{
    QRectF wheel_rect;
    QPointF center;
    double radius = 1.0;
    QRectF saturation_rect;
    QRectF swatch_rect;
};

QColor withAlpha(QColor color, const int alpha)
{
    color.setAlpha(alpha);
    return color;
}

QByteArray propertyKey(const QByteArray& property_name, const QByteArray& suffix)
{
    return QByteArrayLiteral("anVectorSlope") + property_name + suffix;
}

[[nodiscard]] double normalizedHueDegrees(double hue_degrees)
{
    hue_degrees = std::fmod(hue_degrees, kFullCircleDegrees);
    if (hue_degrees < 0.0)
    {
        hue_degrees += kFullCircleDegrees;
    }
    return hue_degrees;
}

[[nodiscard]] QColor hsvColor(const double hue_degrees, const double saturation, const double value = 1.0)
{
    return QColor::fromHsvF(normalizedHueDegrees(hue_degrees) / kFullCircleDegrees, std::clamp(saturation, 0.0, 1.0),
                            std::clamp(value, 0.0, 1.0));
}

[[nodiscard]] LayerPickerGeometry layerPickerGeometry(const QRectF& bounds, const an::ui::DesignTokens& design)
{
    const double pad = static_cast<double>(design.spacing.sm);
    const QRectF inner = bounds.adjusted(pad, pad, -pad, -pad);
    const double side = (std::max)(1.0, (std::min)(inner.height(), inner.width() * 0.48));
    const QRectF wheel_rect(inner.left(), inner.top() + (inner.height() - side) * 0.5, side, side);
    const double saturation_width = (std::max)(24.0, (std::min)(56.0, inner.width() * 0.16));
    const QRectF saturation_rect(wheel_rect.right() + pad, wheel_rect.top(), saturation_width, side);
    const double swatch_side = (std::max)(32.0, (std::min)(side * 0.35, inner.right() - saturation_rect.right() - pad));
    const QRectF swatch_rect(saturation_rect.right() + pad, wheel_rect.top(), swatch_side, swatch_side);
    return {wheel_rect, wheel_rect.center(), side * 0.5, saturation_rect, swatch_rect};
}

} // namespace

VectorSlopeWidget::VectorSlopeWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("anNativeParamVectorSlope"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setGraphHeight(220);
    setVectorDefinitions(defaultHueWheelVectors());
}

QSize VectorSlopeWidget::sizeHint() const
{
    return QSize(350, graph_height_);
}

void VectorSlopeWidget::setGraphHeight(const int height)
{
    graph_height_ = (std::max)(160, height);
    setMinimumSize(QSize(260, graph_height_));
    setMaximumHeight(graph_height_);
    updateGeometry();
}

void VectorSlopeWidget::setVectorDefinitions(std::vector<VectorDefinition> definitions)
{
    if (definitions.empty())
    {
        definitions = defaultHueWheelVectors();
    }

    vectors_.clear();
    definitions_.clear();
    definitions_.reserve(definitions.size());
    vectors_.reserve(definitions.size());
    for (auto& definition : definitions)
    {
        if (definition.property_name.isEmpty())
        {
            definition.property_name = definition.key.toLatin1();
        }
        if (std::abs(definition.saturation_scale) <= 1.0e-12)
        {
            definition.saturation_scale = 1.0;
        }
        definitions_.push_back(definition);
        vectors_.push_back({definition, 0.0, 0.0});
    }

    publishVectorProperties();
    update();
}

void VectorSlopeWidget::setLayerColorPickerEnabled(const bool enabled)
{
    layer_color_picker_enabled_ = enabled;
    setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

bool VectorSlopeWidget::isLayerColorPickerEnabled() const noexcept
{
    return layer_color_picker_enabled_;
}

void VectorSlopeWidget::setLayerColor(const double hue_degrees, const double saturation)
{
    layer_hue_degrees_ = normalizedHueDegrees(hue_degrees);
    layer_saturation_ = std::clamp(saturation, 0.0, 1.0);
    update();
}

double VectorSlopeWidget::layerHueDegrees() const noexcept
{
    return layer_hue_degrees_;
}

double VectorSlopeWidget::layerSaturation() const noexcept
{
    return layer_saturation_;
}

const std::vector<VectorSlopeWidget::VectorDefinition>& VectorSlopeWidget::vectorDefinitions() const noexcept
{
    return definitions_;
}

void VectorSlopeWidget::setVectorsFromParams(const QCborMap& params)
{
    for (auto& vector : vectors_)
    {
        vector.hue_shift_degrees = numericParam(params, vector.definition.hue_param, 0.0);
        vector.saturation = std::clamp(numericParam(params, vector.definition.saturation_param, 0.0) /
                                           vector.definition.saturation_scale,
                                       -1.0, 1.0);
    }
    publishVectorProperties();
    update();
}

double VectorSlopeWidget::hueShiftDegrees(const QString& key) const
{
    for (const auto& vector : vectors_)
    {
        if (vector.definition.key == key)
        {
            return vector.hue_shift_degrees;
        }
    }
    return 0.0;
}

double VectorSlopeWidget::saturation(const QString& key) const
{
    for (const auto& vector : vectors_)
    {
        if (vector.definition.key == key)
        {
            return vector.saturation;
        }
    }
    return 0.0;
}

std::vector<VectorSlopeWidget::VectorDefinition> VectorSlopeWidget::defaultHueWheelVectors()
{
    return {
        {QStringLiteral("R"), QStringLiteral("red_hue"), QStringLiteral("red_saturation"), QByteArrayLiteral("Red"),
         0.0, 100.0, hsvColor(0.0, 0.69)},
        {QStringLiteral("Y"), QStringLiteral("yellow_hue"), QStringLiteral("yellow_saturation"),
         QByteArrayLiteral("Yellow"), 60.0, 100.0, hsvColor(60.0, 1.0)},
        {QStringLiteral("G"), QStringLiteral("green_hue"), QStringLiteral("green_saturation"),
         QByteArrayLiteral("Green"), 120.0, 100.0, hsvColor(120.0, 0.69)},
        {QStringLiteral("C"), QStringLiteral("cyan_hue"), QStringLiteral("cyan_saturation"), QByteArrayLiteral("Cyan"),
         180.0, 100.0, hsvColor(180.0, 1.0)},
        {QStringLiteral("B"), QStringLiteral("blue_hue"), QStringLiteral("blue_saturation"), QByteArrayLiteral("Blue"),
         240.0, 100.0, hsvColor(240.0, 0.69)},
        {QStringLiteral("M"), QStringLiteral("magenta_hue"), QStringLiteral("magenta_saturation"),
         QByteArrayLiteral("Magenta"), 300.0, 100.0, hsvColor(300.0, 1.0)},
    };
}

void VectorSlopeWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event && layer_color_picker_enabled_ && (event->buttons() & Qt::LeftButton))
    {
        if (pickLayerColorAt(event->position(), true))
        {
            event->accept();
            return;
        }
    }

    QWidget::mouseMoveEvent(event);
}

void VectorSlopeWidget::mousePressEvent(QMouseEvent* event)
{
    if (event && layer_color_picker_enabled_ && event->button() == Qt::LeftButton)
    {
        if (pickLayerColorAt(event->position(), true))
        {
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void VectorSlopeWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect().adjusted(0, 0, -1, -1);
    if (layer_color_picker_enabled_)
    {
        paintLayerColorPicker(painter, bounds);
        return;
    }

    paintVectorSlope(painter, bounds);
}

bool VectorSlopeWidget::pickLayerColorAt(const QPointF& point, const bool emit_signal)
{
    const auto geometry = layerPickerGeometry(rect().adjusted(0, 0, -1, -1), tokens());
    const QPointF delta = point - geometry.center;
    const double distance = std::hypot(delta.x(), delta.y());

    bool changed = false;
    if (distance <= geometry.radius)
    {
        const double hue = std::atan2(delta.x(), -delta.y()) * kHalfCircleDegrees / 3.14159265358979323846;
        layer_hue_degrees_ = normalizedHueDegrees(hue);
        changed = true;
    }
    else if (geometry.saturation_rect.contains(point))
    {
        layer_saturation_ =
            std::clamp((point.y() - geometry.saturation_rect.top()) / geometry.saturation_rect.height(), 0.0, 1.0);
        changed = true;
    }

    if (!changed)
    {
        return false;
    }

    update();
    if (emit_signal)
    {
        emit layerColorChanged(layer_hue_degrees_, layer_saturation_);
    }
    return true;
}

void VectorSlopeWidget::paintLayerColorPicker(QPainter& painter, const QRectF& bounds)
{
    const auto& design = tokens();
    painter.setBrush(withAlpha(design.colors.bgInput, 220));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bounds, design.radius.md, design.radius.md);

    const auto geometry = layerPickerGeometry(bounds, design);
    QConicalGradient hue_gradient(geometry.center, 90.0);
    for (int step = 0; step <= 12; ++step)
    {
        const double hue = -static_cast<double>(step) * 30.0;
        hue_gradient.setColorAt(static_cast<double>(step) / 12.0, hsvColor(hue, 1.0));
    }
    painter.setBrush(QBrush(hue_gradient));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(geometry.center, geometry.radius, geometry.radius);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(withAlpha(design.colors.textSecondary, 130), 1.0));
    painter.drawEllipse(geometry.center, geometry.radius, geometry.radius);

    const double radians = layer_hue_degrees_ * kDegreesToRadians;
    const QPointF hue_point(geometry.center.x() + geometry.radius * std::sin(radians),
                            geometry.center.y() - geometry.radius * std::cos(radians));
    painter.setPen(QPen(design.colors.bgPrimary, 2.0));
    painter.drawLine(geometry.center, hue_point);
    painter.setPen(QPen(design.colors.textPrimary, 1.0));
    painter.drawEllipse(hue_point, 4.0, 4.0);

    QLinearGradient saturation_gradient(geometry.saturation_rect.topLeft(), geometry.saturation_rect.bottomLeft());
    saturation_gradient.setColorAt(0.0, hsvColor(layer_hue_degrees_, 0.0));
    saturation_gradient.setColorAt(1.0, hsvColor(layer_hue_degrees_, 1.0));
    painter.setBrush(QBrush(saturation_gradient));
    painter.setPen(QPen(withAlpha(design.colors.textSecondary, 130), 1.0));
    painter.drawRoundedRect(geometry.saturation_rect, design.radius.sm, design.radius.sm);

    const double saturation_y = geometry.saturation_rect.top() + layer_saturation_ * geometry.saturation_rect.height();
    painter.setPen(QPen(design.colors.bgPrimary, 2.0));
    painter.drawLine(QPointF(geometry.saturation_rect.left(), saturation_y),
                     QPointF(geometry.saturation_rect.right(), saturation_y));
    painter.setPen(QPen(design.colors.textPrimary, 1.0));
    painter.drawLine(QPointF(geometry.saturation_rect.left(), saturation_y + 2.0),
                     QPointF(geometry.saturation_rect.right(), saturation_y + 2.0));

    painter.setBrush(hsvColor(layer_hue_degrees_, layer_saturation_));
    painter.setPen(QPen(withAlpha(design.colors.textSecondary, 150), 1.0));
    painter.drawRoundedRect(geometry.swatch_rect, design.radius.sm, design.radius.sm);
}

void VectorSlopeWidget::paintVectorSlope(QPainter& painter, const QRectF& bounds)
{
    const auto& design = tokens();
    painter.setBrush(withAlpha(design.colors.bgInput, 220));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bounds, design.radius.md, design.radius.md);

    const double side = (std::min)(bounds.width(), bounds.height());
    const QPointF center(bounds.center());
    const double radius = (std::max)(1.0, side / 2.0 - static_cast<double>(design.spacing.lg));

    QConicalGradient gradient(center, 0.0);
    constexpr int kWheelAlpha = 100;
    for (int step = 0; step <= 6; ++step)
    {
        const double hue = static_cast<double>(step) * 60.0;
        gradient.setColorAt(static_cast<double>(step) / 6.0, withAlpha(hsvColor(hue, 1.0), kWheelAlpha));
    }

    painter.setBrush(QBrush(gradient));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(center, radius, radius);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(withAlpha(design.colors.textSecondary, 100), 1.0));
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(withAlpha(design.colors.textTertiary, 55), 1.0));
    painter.drawEllipse(center, radius * 0.6, radius * 0.6);
    painter.setPen(QPen(withAlpha(design.colors.borderSubtle, 120), 1.0));
    painter.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));

    for (const auto& vector : vectors_)
    {
        const double final_degrees = vector.definition.base_hue_degrees + vector.hue_shift_degrees;
        const double radius_factor = 0.6 + vector.saturation * 0.4;
        const double final_radius = radius * radius_factor;
        const double radians = final_degrees * kDegreesToRadians;
        const QPointF point(center.x() + final_radius * std::cos(radians),
                            center.y() - final_radius * std::sin(radians));

        painter.setPen(QPen(withAlpha(design.colors.textSecondary, 150), 1.0, Qt::DotLine));
        painter.drawLine(center, point);
        painter.setPen(QPen(design.colors.bgPrimary, 1.0));
        painter.setBrush(vector.definition.color);
        painter.drawEllipse(point, 4.0, 4.0);
    }
}

double VectorSlopeWidget::numericParam(const QCborMap& params, const QString& key, const double fallback)
{
    const QCborValue value = params.value(key);
    if (value.isDouble() || value.isInteger())
    {
        return value.toDouble(fallback);
    }
    if (value.isString())
    {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if (ok)
        {
            return parsed;
        }
    }
    return fallback;
}

void VectorSlopeWidget::publishVectorProperties()
{
    for (const auto& vector : vectors_)
    {
        const QByteArray hue_property = propertyKey(vector.definition.property_name, QByteArrayLiteral("HueShift"));
        const QByteArray saturation_property =
            propertyKey(vector.definition.property_name, QByteArrayLiteral("Saturation"));
        setProperty(hue_property.constData(), vector.hue_shift_degrees);
        setProperty(saturation_property.constData(), vector.saturation);
    }
}

} // namespace an::ui
