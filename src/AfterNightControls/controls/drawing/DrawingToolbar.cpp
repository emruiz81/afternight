// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/drawing/DrawingToolbar.h"

#include "theme/AnDesignTokens.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::drawing::DrawingToolbar)

namespace
{

constexpr int kPercentScale = 100;

int toPercentSlider(const float value)
{
    return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(kPercentScale)));
}

float fromPercentSlider(const int value)
{
    return std::clamp(static_cast<float>(value) / static_cast<float>(kPercentScale), 0.0f, 1.0f);
}

} // namespace

namespace an::ui::drawing
{

DrawingToolbar::DrawingToolbar(QWidget* parent) : QWidget(parent)
{
    const auto& t = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    mainLayout->setSpacing(t.spacing.md);

    m_toolGroupBox = new QGroupBox(tr("Tool"), this);
    auto* toolLayout = new QGridLayout(m_toolGroupBox);
    toolLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.md);
    toolLayout->setHorizontalSpacing(t.spacing.sm);
    toolLayout->setVerticalSpacing(t.spacing.sm);

    m_brushButton = new QPushButton(tr("Brush"), m_toolGroupBox);
    m_brushButton->setObjectName(QStringLiteral("toolBrushButton"));
    m_brushButton->setCheckable(true);
    m_eraserButton = new QPushButton(tr("Eraser"), m_toolGroupBox);
    m_eraserButton->setObjectName(QStringLiteral("toolEraserButton"));
    m_eraserButton->setCheckable(true);
    m_shapeDrawButton = new QPushButton(tr("Shape"), m_toolGroupBox);
    m_shapeDrawButton->setObjectName(QStringLiteral("toolShapeButton"));
    m_shapeDrawButton->setCheckable(true);
    m_shapeSelectButton = new QPushButton(tr("Edit"), m_toolGroupBox);
    m_shapeSelectButton->setObjectName(QStringLiteral("toolEditButton"));
    m_shapeSelectButton->setCheckable(true);
    m_floodFillButton = new QPushButton(tr("Fill"), m_toolGroupBox);
    m_floodFillButton->setObjectName(QStringLiteral("toolFloodFillButton"));
    m_floodFillButton->setCheckable(true);
    m_gradientFillButton = new QPushButton(tr("Gradient"), m_toolGroupBox);
    m_gradientFillButton->setObjectName(QStringLiteral("toolGradientFillButton"));
    m_gradientFillButton->setCheckable(true);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);
    m_toolGroup->addButton(m_brushButton, static_cast<int>(DrawingTool::Brush));
    m_toolGroup->addButton(m_eraserButton, static_cast<int>(DrawingTool::Eraser));
    m_toolGroup->addButton(m_shapeDrawButton, static_cast<int>(DrawingTool::ShapeDraw));
    m_toolGroup->addButton(m_shapeSelectButton, static_cast<int>(DrawingTool::ShapeSelect));
    m_toolGroup->addButton(m_floodFillButton, static_cast<int>(DrawingTool::FloodFill));
    m_toolGroup->addButton(m_gradientFillButton, static_cast<int>(DrawingTool::GradientFill));
    m_brushButton->setChecked(true);

    toolLayout->addWidget(m_brushButton, 0, 0);
    toolLayout->addWidget(m_eraserButton, 0, 1);
    toolLayout->addWidget(m_shapeDrawButton, 0, 2);
    toolLayout->addWidget(m_shapeSelectButton, 1, 0);
    toolLayout->addWidget(m_floodFillButton, 1, 1);
    toolLayout->addWidget(m_gradientFillButton, 1, 2);
    mainLayout->addWidget(m_toolGroupBox);

    m_brushGroupBox = new QGroupBox(tr("Brush"), this);
    auto* brushLayout = new QFormLayout(m_brushGroupBox);
    brushLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.md);
    brushLayout->setHorizontalSpacing(t.spacing.md);
    brushLayout->setVerticalSpacing(t.spacing.sm);

    m_tipShapeCombo = new QComboBox(m_brushGroupBox);
    m_tipShapeCombo->addItem(tr("Round"), static_cast<int>(BrushParams::TipShape::Round));
    m_tipShapeCombo->addItem(tr("Square"), static_cast<int>(BrushParams::TipShape::Square));
    brushLayout->addRow(tr("Tip Shape"), m_tipShapeCombo);

    auto* sizeRow = new QWidget(m_brushGroupBox);
    auto* sizeRowLayout = new QHBoxLayout(sizeRow);
    sizeRowLayout->setContentsMargins(0, 0, 0, 0);
    sizeRowLayout->setSpacing(t.spacing.sm);
    m_sizeSpin = new QSpinBox(sizeRow);
    m_sizeSpin->setRange(1, kMaxBrushSizePx);
    m_sizeSpin->setValue(24);
    m_sizeSlider = new QSlider(Qt::Horizontal, sizeRow);
    m_sizeSlider->setRange(1, kMaxBrushSizePx);
    m_sizeSlider->setValue(24);
    sizeRowLayout->addWidget(m_sizeSpin);
    sizeRowLayout->addWidget(m_sizeSlider, 1);
    brushLayout->addRow(tr("Size"), sizeRow);

    auto* hardnessRow = new QWidget(m_brushGroupBox);
    auto* hardnessRowLayout = new QHBoxLayout(hardnessRow);
    hardnessRowLayout->setContentsMargins(0, 0, 0, 0);
    hardnessRowLayout->setSpacing(t.spacing.sm);
    m_hardnessSpin = new QDoubleSpinBox(hardnessRow);
    m_hardnessSpin->setDecimals(2);
    m_hardnessSpin->setRange(0.0, 1.0);
    m_hardnessSpin->setSingleStep(0.05);
    m_hardnessSpin->setValue(1.0);
    m_hardnessSlider = new QSlider(Qt::Horizontal, hardnessRow);
    m_hardnessSlider->setRange(0, kPercentScale);
    m_hardnessSlider->setValue(kPercentScale);
    hardnessRowLayout->addWidget(m_hardnessSpin);
    hardnessRowLayout->addWidget(m_hardnessSlider, 1);
    brushLayout->addRow(tr("Hardness"), hardnessRow);

    auto* opacityRow = new QWidget(m_brushGroupBox);
    auto* opacityRowLayout = new QHBoxLayout(opacityRow);
    opacityRowLayout->setContentsMargins(0, 0, 0, 0);
    opacityRowLayout->setSpacing(t.spacing.sm);
    m_opacitySpin = new QDoubleSpinBox(opacityRow);
    m_opacitySpin->setDecimals(2);
    m_opacitySpin->setRange(0.0, 1.0);
    m_opacitySpin->setSingleStep(0.05);
    m_opacitySpin->setValue(1.0);
    m_opacitySlider = new QSlider(Qt::Horizontal, opacityRow);
    m_opacitySlider->setRange(0, kPercentScale);
    m_opacitySlider->setValue(kPercentScale);
    opacityRowLayout->addWidget(m_opacitySpin);
    opacityRowLayout->addWidget(m_opacitySlider, 1);
    brushLayout->addRow(tr("Opacity"), opacityRow);

    auto* flowRow = new QWidget(m_brushGroupBox);
    auto* flowRowLayout = new QHBoxLayout(flowRow);
    flowRowLayout->setContentsMargins(0, 0, 0, 0);
    flowRowLayout->setSpacing(t.spacing.sm);
    m_flowSpin = new QDoubleSpinBox(flowRow);
    m_flowSpin->setDecimals(2);
    m_flowSpin->setRange(0.0, 1.0);
    m_flowSpin->setSingleStep(0.05);
    m_flowSpin->setValue(1.0);
    m_flowSlider = new QSlider(Qt::Horizontal, flowRow);
    m_flowSlider->setRange(0, kPercentScale);
    m_flowSlider->setValue(kPercentScale);
    flowRowLayout->addWidget(m_flowSpin);
    flowRowLayout->addWidget(m_flowSlider, 1);
    brushLayout->addRow(tr("Flow"), flowRow);

    auto* spacingRow = new QWidget(m_brushGroupBox);
    auto* spacingRowLayout = new QHBoxLayout(spacingRow);
    spacingRowLayout->setContentsMargins(0, 0, 0, 0);
    spacingRowLayout->setSpacing(t.spacing.sm);
    m_spacingSpin = new QDoubleSpinBox(spacingRow);
    m_spacingSpin->setDecimals(2);
    m_spacingSpin->setRange(0.01, 1.0);
    m_spacingSpin->setSingleStep(0.01);
    m_spacingSpin->setValue(0.25);
    m_spacingSlider = new QSlider(Qt::Horizontal, spacingRow);
    m_spacingSlider->setRange(1, kPercentScale);
    m_spacingSlider->setValue(25);
    spacingRowLayout->addWidget(m_spacingSpin);
    spacingRowLayout->addWidget(m_spacingSlider, 1);
    brushLayout->addRow(tr("Spacing"), spacingRow);

    m_stabilizationSpin = new QDoubleSpinBox(m_brushGroupBox);
    m_stabilizationSpin->setObjectName(QStringLiteral("stabilizationSpin"));
    m_stabilizationSpin->setDecimals(2);
    m_stabilizationSpin->setRange(0.0, 1.0);
    m_stabilizationSpin->setSingleStep(0.05);
    m_stabilizationSpin->setValue(0.0);
    brushLayout->addRow(tr("Stabilization"), m_stabilizationSpin);

    m_blendModeCombo = new QComboBox(m_brushGroupBox);
    m_blendModeCombo->addItem(tr("Normal"), static_cast<int>(BrushParams::BlendMode::Normal));
    m_blendModeCombo->addItem(tr("Multiply"), static_cast<int>(BrushParams::BlendMode::Multiply));
    m_blendModeCombo->addItem(tr("Screen"), static_cast<int>(BrushParams::BlendMode::Screen));
    m_blendModeCombo->addItem(tr("Overlay"), static_cast<int>(BrushParams::BlendMode::Overlay));
    brushLayout->addRow(tr("Blend Mode"), m_blendModeCombo);

    mainLayout->addWidget(m_brushGroupBox);

    m_shapeGroupBox = new QGroupBox(tr("Shapes"), this);
    auto* shapeLayout = new QFormLayout(m_shapeGroupBox);
    shapeLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.md);
    shapeLayout->setHorizontalSpacing(t.spacing.md);
    shapeLayout->setVerticalSpacing(t.spacing.sm);

    m_shapeTypeCombo = new QComboBox(m_shapeGroupBox);
    m_shapeTypeCombo->setObjectName(QStringLiteral("shapeTypeCombo"));
    m_shapeTypeCombo->addItem(tr("Rectangle"), static_cast<int>(ShapeObject::Type::Rectangle));
    m_shapeTypeCombo->addItem(tr("Ellipse"), static_cast<int>(ShapeObject::Type::Ellipse));
    m_shapeTypeCombo->addItem(tr("Circle"), static_cast<int>(ShapeObject::Type::Circle));
    m_shapeTypeCombo->addItem(tr("Polygon"), static_cast<int>(ShapeObject::Type::Polygon));
    m_shapeTypeCombo->addItem(tr("Line"), static_cast<int>(ShapeObject::Type::Line));
    shapeLayout->addRow(tr("Shape"), m_shapeTypeCombo);

    m_shapeFeatherSpin = new QDoubleSpinBox(m_shapeGroupBox);
    m_shapeFeatherSpin->setDecimals(1);
    m_shapeFeatherSpin->setRange(0.0, 100.0);
    m_shapeFeatherSpin->setSingleStep(0.5);
    m_shapeFeatherSpin->setValue(0.0);
    shapeLayout->addRow(tr("Feather"), m_shapeFeatherSpin);

    m_shapeFilledCheck = new QCheckBox(tr("Filled"), m_shapeGroupBox);
    m_shapeFilledCheck->setChecked(true);
    shapeLayout->addRow(QString(), m_shapeFilledCheck);

    mainLayout->addWidget(m_shapeGroupBox);

    m_previewGroupBox = new QGroupBox(tr("Preview"), this);
    auto* previewLayout = new QFormLayout(m_previewGroupBox);
    previewLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.md);
    previewLayout->setHorizontalSpacing(t.spacing.md);
    previewLayout->setVerticalSpacing(t.spacing.sm);
    m_maskPreviewModeCombo = new QComboBox(m_previewGroupBox);
    m_maskPreviewModeCombo->setObjectName(QStringLiteral("maskPreviewModeCombo"));
    m_maskPreviewModeCombo->addItem(tr("Red"), static_cast<int>(MaskPreviewMode::Red));
    m_maskPreviewModeCombo->addItem(tr("Green"), static_cast<int>(MaskPreviewMode::Green));
    m_maskPreviewModeCombo->addItem(tr("Blue"), static_cast<int>(MaskPreviewMode::Blue));
    m_maskPreviewModeCombo->addItem(tr("Amber"), static_cast<int>(MaskPreviewMode::Amber));
    m_maskPreviewModeCombo->addItem(tr("Grayscale"), static_cast<int>(MaskPreviewMode::Grayscale));
    m_maskPreviewModeCombo->addItem(tr("Checkerboard"), static_cast<int>(MaskPreviewMode::Checkerboard));
    previewLayout->addRow(tr("Mask View"), m_maskPreviewModeCombo);
    mainLayout->addWidget(m_previewGroupBox);

    m_fileGroupBox = new QGroupBox(tr("Mask File"), this);
    auto* fileLayout = new QHBoxLayout(m_fileGroupBox);
    fileLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.md);
    fileLayout->setSpacing(t.spacing.sm);
    m_importMaskButton = new QPushButton(tr("Import Mask..."), m_fileGroupBox);
    m_importMaskButton->setObjectName(QStringLiteral("importMaskButton"));
    m_exportMaskButton = new QPushButton(tr("Export Mask..."), m_fileGroupBox);
    m_exportMaskButton->setObjectName(QStringLiteral("exportMaskButton"));
    fileLayout->addWidget(m_importMaskButton);
    fileLayout->addWidget(m_exportMaskButton);
    mainLayout->addWidget(m_fileGroupBox);

    auto* historyGroupBox = new QGroupBox(tr("History"), this);
    auto* historyLayout = new QHBoxLayout(historyGroupBox);
    historyLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.md);
    historyLayout->setSpacing(t.spacing.sm);
    m_undoButton = new QPushButton(tr("Undo"), historyGroupBox);
    m_redoButton = new QPushButton(tr("Redo"), historyGroupBox);
    m_clearButton = new QPushButton(tr("Clear"), historyGroupBox);
    historyLayout->addWidget(m_undoButton);
    historyLayout->addWidget(m_redoButton);
    historyLayout->addWidget(m_clearButton);
    mainLayout->addWidget(historyGroupBox);

    mainLayout->addStretch();

    connect(m_toolGroup, &QButtonGroup::idClicked, this, [this](const int id) {
        updateBlendModeEnabledState();
        emit toolChanged(id);
    });

    connect(m_tipShapeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { emit tipShapeChanged(m_tipShapeCombo->currentData().toInt()); });

    connect(m_sizeSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](const int value) {
        if (m_sizeSlider->value() != value)
        {
            const QSignalBlocker blocker(m_sizeSlider);
            m_sizeSlider->setValue(value);
        }
        emit brushSizeChanged(static_cast<double>(value));
    });

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](const int value) {
        if (m_sizeSpin->value() != value)
        {
            const QSignalBlocker blocker(m_sizeSpin);
            m_sizeSpin->setValue(value);
        }
        emit brushSizeChanged(static_cast<double>(value));
    });

    connect(m_hardnessSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](const double value) {
        const int sliderValue = toPercentSlider(static_cast<float>(value));
        if (m_hardnessSlider->value() != sliderValue)
        {
            const QSignalBlocker blocker(m_hardnessSlider);
            m_hardnessSlider->setValue(sliderValue);
        }
        emit brushHardnessChanged(value);
    });

    connect(m_hardnessSlider, &QSlider::valueChanged, this, [this](const int value) {
        const double hardness = fromPercentSlider(value);
        if (!qFuzzyCompare(m_hardnessSpin->value(), hardness))
        {
            const QSignalBlocker blocker(m_hardnessSpin);
            m_hardnessSpin->setValue(hardness);
        }
        emit brushHardnessChanged(hardness);
    });

    connect(m_opacitySpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](const double value) {
        const int sliderValue = toPercentSlider(static_cast<float>(value));
        if (m_opacitySlider->value() != sliderValue)
        {
            const QSignalBlocker blocker(m_opacitySlider);
            m_opacitySlider->setValue(sliderValue);
        }
        emit brushOpacityChanged(value);
    });

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](const int value) {
        const double opacity = fromPercentSlider(value);
        if (!qFuzzyCompare(m_opacitySpin->value(), opacity))
        {
            const QSignalBlocker blocker(m_opacitySpin);
            m_opacitySpin->setValue(opacity);
        }
        emit brushOpacityChanged(opacity);
    });

    connect(m_flowSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](const double value) {
        const int sliderValue = toPercentSlider(static_cast<float>(value));
        if (m_flowSlider->value() != sliderValue)
        {
            const QSignalBlocker blocker(m_flowSlider);
            m_flowSlider->setValue(sliderValue);
        }
        emit brushFlowChanged(value);
    });

    connect(m_flowSlider, &QSlider::valueChanged, this, [this](const int value) {
        const double flow = fromPercentSlider(value);
        if (!qFuzzyCompare(m_flowSpin->value(), flow))
        {
            const QSignalBlocker blocker(m_flowSpin);
            m_flowSpin->setValue(flow);
        }
        emit brushFlowChanged(flow);
    });

    connect(m_spacingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](const double value) {
        const int sliderValue = toPercentSlider(static_cast<float>(value));
        if (m_spacingSlider->value() != sliderValue)
        {
            const QSignalBlocker blocker(m_spacingSlider);
            m_spacingSlider->setValue(sliderValue);
        }
        emit brushSpacingChanged(value);
    });

    connect(m_spacingSlider, &QSlider::valueChanged, this, [this](const int value) {
        const double spacing = (std::max)(0.01, static_cast<double>(fromPercentSlider(value)));
        if (!qFuzzyCompare(m_spacingSpin->value(), spacing))
        {
            const QSignalBlocker blocker(m_spacingSpin);
            m_spacingSpin->setValue(spacing);
        }
        emit brushSpacingChanged(spacing);
    });

    connect(m_stabilizationSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](const double value) { emit stabilizationChanged(value); });

    connect(m_blendModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { emit blendModeChanged(m_blendModeCombo->currentData().toInt()); });

    connect(m_shapeTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { emit shapeTypeChanged(m_shapeTypeCombo->currentData().toInt()); });

    connect(m_shapeFeatherSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](const double value) { emit shapeFeatherChanged(value); });

    connect(m_shapeFilledCheck, &QCheckBox::toggled, this,
            [this](const bool checked) { emit shapeFilledChanged(checked); });

    connect(m_maskPreviewModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { emit previewModeChanged(m_maskPreviewModeCombo->currentData().toInt()); });

    connect(m_importMaskButton, &QPushButton::clicked, this, [this]() { emit importRequested(); });
    connect(m_exportMaskButton, &QPushButton::clicked, this, [this]() { emit exportRequested(); });

    connect(m_undoButton, &QPushButton::clicked, this, [this]() { emit undoRequested(); });
    connect(m_redoButton, &QPushButton::clicked, this, [this]() { emit redoRequested(); });
    connect(m_clearButton, &QPushButton::clicked, this, [this]() { emit clearRequested(); });

    setUndoEnabled(false);
    setRedoEnabled(false);
    updateBlendModeEnabledState();
}

DrawingToolbar::~DrawingToolbar() = default;

DrawingTool DrawingToolbar::activeTool() const
{
    return static_cast<DrawingTool>(m_toolGroup ? m_toolGroup->checkedId() : static_cast<int>(DrawingTool::Brush));
}

BrushParams DrawingToolbar::currentBrushParams() const
{
    BrushParams brush;
    brush.tipShape = m_tipShapeCombo ? static_cast<BrushParams::TipShape>(m_tipShapeCombo->currentData().toInt())
                                     : BrushParams::TipShape::Round;
    brush.size = brushSize();
    brush.hardness = m_hardnessSpin ? static_cast<float>(m_hardnessSpin->value()) : 1.0f;
    brush.opacity = brushOpacity();
    brush.flow = m_flowSpin ? static_cast<float>(m_flowSpin->value()) : 1.0f;
    brush.spacing = m_spacingSpin ? static_cast<float>(m_spacingSpin->value()) : 0.25f;
    brush.stabilization = m_stabilizationSpin ? static_cast<float>(m_stabilizationSpin->value()) : 0.0f;
    brush.blendMode = m_blendModeCombo ? static_cast<BrushParams::BlendMode>(m_blendModeCombo->currentData().toInt())
                                       : BrushParams::BlendMode::Normal;
    return brush;
}

float DrawingToolbar::brushSize() const
{
    return m_sizeSpin ? static_cast<float>(m_sizeSpin->value()) : 24.0f;
}

float DrawingToolbar::brushOpacity() const
{
    return m_opacitySpin ? static_cast<float>(m_opacitySpin->value()) : 1.0f;
}

ShapeObject::Type DrawingToolbar::currentShapeType() const
{
    return m_shapeTypeCombo ? static_cast<ShapeObject::Type>(m_shapeTypeCombo->currentData().toInt())
                            : ShapeObject::Type::Rectangle;
}

float DrawingToolbar::shapeFeather() const
{
    return m_shapeFeatherSpin ? static_cast<float>(m_shapeFeatherSpin->value()) : 0.0f;
}

bool DrawingToolbar::isShapeFilled() const
{
    return !m_shapeFilledCheck || m_shapeFilledCheck->isChecked();
}

MaskPreviewMode DrawingToolbar::currentPreviewMode() const
{
    return m_maskPreviewModeCombo ? static_cast<MaskPreviewMode>(m_maskPreviewModeCombo->currentData().toInt())
                                  : MaskPreviewMode::Red;
}

void DrawingToolbar::setActiveTool(const DrawingTool tool)
{
    if (!m_toolGroup)
    {
        return;
    }

    if (auto* button = m_toolGroup->button(static_cast<int>(tool)))
    {
        const QSignalBlocker blocker(m_toolGroup);
        button->setChecked(true);
    }
    updateBlendModeEnabledState();
}

void DrawingToolbar::toggleTool()
{
    const DrawingTool nextTool = (activeTool() == DrawingTool::Brush) ? DrawingTool::Eraser : DrawingTool::Brush;
    if (!m_toolGroup)
    {
        return;
    }

    if (auto* button = m_toolGroup->button(static_cast<int>(nextTool)))
    {
        button->setChecked(true);
        updateBlendModeEnabledState();
        emit toolChanged(static_cast<int>(nextTool));
    }
}

void DrawingToolbar::stepBrushSize(const int delta)
{
    if (!m_sizeSpin)
    {
        return;
    }

    const int nextValue = std::clamp(m_sizeSpin->value() + delta, m_sizeSpin->minimum(), m_sizeSpin->maximum());
    m_sizeSpin->setValue(nextValue);
}

void DrawingToolbar::setBrushParams(const BrushParams& brush)
{
    setTipShape(brush.tipShape);
    setBrushSize(brush.size);
    setBrushHardness(brush.hardness);
    setBrushOpacity(brush.opacity);
    setBrushFlow(brush.flow);
    setBrushSpacing(brush.spacing);
    setBrushStabilization(brush.stabilization);
    setBlendMode(brush.blendMode);
}

void DrawingToolbar::setBrushSize(const float size)
{
    if (!m_sizeSpin || !m_sizeSlider)
    {
        return;
    }

    const int clamped = std::clamp(static_cast<int>(std::lround(size)), m_sizeSpin->minimum(), m_sizeSpin->maximum());
    {
        const QSignalBlocker blocker(m_sizeSpin);
        m_sizeSpin->setValue(clamped);
    }
    {
        const QSignalBlocker blocker(m_sizeSlider);
        m_sizeSlider->setValue(clamped);
    }
}

void DrawingToolbar::setBrushHardness(const float hardness)
{
    if (!m_hardnessSpin || !m_hardnessSlider)
    {
        return;
    }

    const double clamped =
        std::clamp(static_cast<double>(hardness), m_hardnessSpin->minimum(), m_hardnessSpin->maximum());
    {
        const QSignalBlocker blocker(m_hardnessSpin);
        m_hardnessSpin->setValue(clamped);
    }
    {
        const QSignalBlocker blocker(m_hardnessSlider);
        m_hardnessSlider->setValue(toPercentSlider(static_cast<float>(clamped)));
    }
}

void DrawingToolbar::setBrushOpacity(const float opacity)
{
    if (!m_opacitySpin || !m_opacitySlider)
    {
        return;
    }

    const double clamped = std::clamp(static_cast<double>(opacity), m_opacitySpin->minimum(), m_opacitySpin->maximum());
    {
        const QSignalBlocker blocker(m_opacitySpin);
        m_opacitySpin->setValue(clamped);
    }
    {
        const QSignalBlocker blocker(m_opacitySlider);
        m_opacitySlider->setValue(toPercentSlider(static_cast<float>(clamped)));
    }
}

void DrawingToolbar::setBrushFlow(const float flow)
{
    if (!m_flowSpin || !m_flowSlider)
    {
        return;
    }

    const double clamped = std::clamp(static_cast<double>(flow), m_flowSpin->minimum(), m_flowSpin->maximum());
    {
        const QSignalBlocker blocker(m_flowSpin);
        m_flowSpin->setValue(clamped);
    }
    {
        const QSignalBlocker blocker(m_flowSlider);
        m_flowSlider->setValue(toPercentSlider(static_cast<float>(clamped)));
    }
}

void DrawingToolbar::setBrushSpacing(const float spacing)
{
    if (!m_spacingSpin || !m_spacingSlider)
    {
        return;
    }

    const double clamped = std::clamp(static_cast<double>(spacing), m_spacingSpin->minimum(), m_spacingSpin->maximum());
    {
        const QSignalBlocker blocker(m_spacingSpin);
        m_spacingSpin->setValue(clamped);
    }
    {
        const QSignalBlocker blocker(m_spacingSlider);
        m_spacingSlider->setValue(toPercentSlider(static_cast<float>(clamped)));
    }
}

void DrawingToolbar::setBrushStabilization(const float stabilization)
{
    if (!m_stabilizationSpin)
    {
        return;
    }

    const double clamped =
        std::clamp(static_cast<double>(stabilization), m_stabilizationSpin->minimum(), m_stabilizationSpin->maximum());
    const QSignalBlocker blocker(m_stabilizationSpin);
    m_stabilizationSpin->setValue(clamped);
}

void DrawingToolbar::setTipShape(const BrushParams::TipShape tipShape)
{
    if (!m_tipShapeCombo)
    {
        return;
    }

    const int index = m_tipShapeCombo->findData(static_cast<int>(tipShape));
    if (index >= 0)
    {
        const QSignalBlocker blocker(m_tipShapeCombo);
        m_tipShapeCombo->setCurrentIndex(index);
    }
}

void DrawingToolbar::setBlendMode(const BrushParams::BlendMode blendMode)
{
    if (!m_blendModeCombo)
    {
        return;
    }

    const BrushParams::BlendMode effectiveMode =
        (blendMode == BrushParams::BlendMode::Erase) ? BrushParams::BlendMode::Normal : blendMode;
    const int index = m_blendModeCombo->findData(static_cast<int>(effectiveMode));
    if (index >= 0)
    {
        const QSignalBlocker blocker(m_blendModeCombo);
        m_blendModeCombo->setCurrentIndex(index);
    }
}

void DrawingToolbar::setShapeType(const ShapeObject::Type shapeType)
{
    if (!m_shapeTypeCombo)
    {
        return;
    }

    const int index = m_shapeTypeCombo->findData(static_cast<int>(shapeType));
    if (index >= 0)
    {
        const QSignalBlocker blocker(m_shapeTypeCombo);
        m_shapeTypeCombo->setCurrentIndex(index);
    }
}

void DrawingToolbar::setShapeFeather(const float feather)
{
    if (!m_shapeFeatherSpin)
    {
        return;
    }

    const double clamped =
        std::clamp(static_cast<double>(feather), m_shapeFeatherSpin->minimum(), m_shapeFeatherSpin->maximum());
    const QSignalBlocker blocker(m_shapeFeatherSpin);
    m_shapeFeatherSpin->setValue(clamped);
}

void DrawingToolbar::setShapeFilled(const bool filled)
{
    if (!m_shapeFilledCheck)
    {
        return;
    }

    const QSignalBlocker blocker(m_shapeFilledCheck);
    m_shapeFilledCheck->setChecked(filled);
}

void DrawingToolbar::setPreviewMode(const MaskPreviewMode previewMode)
{
    if (!m_maskPreviewModeCombo)
    {
        return;
    }

    const int index = m_maskPreviewModeCombo->findData(static_cast<int>(previewMode));
    if (index >= 0)
    {
        const QSignalBlocker blocker(m_maskPreviewModeCombo);
        m_maskPreviewModeCombo->setCurrentIndex(index);
    }
}

void DrawingToolbar::setUndoEnabled(const bool enabled)
{
    if (m_undoButton)
    {
        m_undoButton->setEnabled(enabled);
    }
}

void DrawingToolbar::setRedoEnabled(const bool enabled)
{
    if (m_redoButton)
    {
        m_redoButton->setEnabled(enabled);
    }
}

void DrawingToolbar::updateBlendModeEnabledState()
{
    const DrawingTool tool = activeTool();
    const bool usesBrushSettings = tool == DrawingTool::Brush || tool == DrawingTool::Eraser ||
                                   tool == DrawingTool::FloodFill || tool == DrawingTool::GradientFill;
    const bool allowsBlendModes =
        tool == DrawingTool::Brush || tool == DrawingTool::FloodFill || tool == DrawingTool::GradientFill;
    if (m_blendModeCombo)
    {
        m_blendModeCombo->setEnabled(allowsBlendModes);
    }
    if (m_brushGroupBox)
    {
        m_brushGroupBox->setEnabled(usesBrushSettings);
    }
    if (m_shapeGroupBox)
    {
        m_shapeGroupBox->setEnabled(tool == DrawingTool::ShapeDraw);
    }
}

} // namespace an::ui::drawing
