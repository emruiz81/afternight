// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "controls/drawing/DrawingTypes.h"

#include <QWidget>
#include <verdigris/wobjectdefs.h>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QGroupBox;
class QSpinBox;
class QGridLayout;

namespace an::ui::drawing
{

class AFTERNIGHTCONTROLS_API DrawingToolbar : public QWidget
{
    W_OBJECT(DrawingToolbar)

public:
    explicit DrawingToolbar(QWidget* parent = nullptr);
    ~DrawingToolbar() override;

    [[nodiscard]] DrawingTool activeTool() const;
    [[nodiscard]] BrushParams currentBrushParams() const;
    [[nodiscard]] float brushSize() const;
    [[nodiscard]] float brushOpacity() const;
    [[nodiscard]] ShapeObject::Type currentShapeType() const;
    [[nodiscard]] float shapeFeather() const;
    [[nodiscard]] bool isShapeFilled() const;
    [[nodiscard]] MaskPreviewMode currentPreviewMode() const;

    void setActiveTool(DrawingTool tool);
    void toggleTool();
    void stepBrushSize(int delta);
    void setBrushParams(const BrushParams& brush);
    void setBrushSize(float size);
    void setBrushHardness(float hardness);
    void setBrushOpacity(float opacity);
    void setBrushFlow(float flow);
    void setBrushSpacing(float spacing);
    void setBrushStabilization(float stabilization);
    void setTipShape(BrushParams::TipShape tipShape);
    void setBlendMode(BrushParams::BlendMode blendMode);
    void setShapeType(ShapeObject::Type shapeType);
    void setShapeFeather(float feather);
    void setShapeFilled(bool filled);
    void setPreviewMode(MaskPreviewMode previewMode);
    void setUndoEnabled(bool enabled);
    void setRedoEnabled(bool enabled);

    // Signals
    void toolChanged(int tool) W_SIGNAL(toolChanged, tool);
    void brushSizeChanged(double size) W_SIGNAL(brushSizeChanged, size);
    void brushHardnessChanged(double hardness) W_SIGNAL(brushHardnessChanged, hardness);
    void brushOpacityChanged(double opacity) W_SIGNAL(brushOpacityChanged, opacity);
    void brushFlowChanged(double flow) W_SIGNAL(brushFlowChanged, flow);
    void brushSpacingChanged(double spacing) W_SIGNAL(brushSpacingChanged, spacing);
    void tipShapeChanged(int tipShape) W_SIGNAL(tipShapeChanged, tipShape);
    void blendModeChanged(int blendMode) W_SIGNAL(blendModeChanged, blendMode);
    void shapeTypeChanged(int shapeType) W_SIGNAL(shapeTypeChanged, shapeType);
    void shapeFeatherChanged(double feather) W_SIGNAL(shapeFeatherChanged, feather);
    void shapeFilledChanged(bool filled) W_SIGNAL(shapeFilledChanged, filled);
    void stabilizationChanged(double stabilization) W_SIGNAL(stabilizationChanged, stabilization);
    void previewModeChanged(int previewMode) W_SIGNAL(previewModeChanged, previewMode);
    void importRequested() W_SIGNAL(importRequested);
    void exportRequested() W_SIGNAL(exportRequested);
    void undoRequested() W_SIGNAL(undoRequested);
    void redoRequested() W_SIGNAL(redoRequested);
    void clearRequested() W_SIGNAL(clearRequested);

private:
    QButtonGroup* m_toolGroup = nullptr;
    QGroupBox* m_toolGroupBox = nullptr;
    QGroupBox* m_brushGroupBox = nullptr;
    QGroupBox* m_shapeGroupBox = nullptr;
    QGroupBox* m_previewGroupBox = nullptr;
    QGroupBox* m_fileGroupBox = nullptr;
    QPushButton* m_brushButton = nullptr;
    QPushButton* m_eraserButton = nullptr;
    QPushButton* m_shapeDrawButton = nullptr;
    QPushButton* m_shapeSelectButton = nullptr;
    QPushButton* m_floodFillButton = nullptr;
    QPushButton* m_gradientFillButton = nullptr;
    QPushButton* m_importMaskButton = nullptr;
    QPushButton* m_exportMaskButton = nullptr;
    QComboBox* m_tipShapeCombo = nullptr;
    QComboBox* m_blendModeCombo = nullptr;
    QComboBox* m_shapeTypeCombo = nullptr;
    QComboBox* m_maskPreviewModeCombo = nullptr;
    QSpinBox* m_sizeSpin = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QDoubleSpinBox* m_hardnessSpin = nullptr;
    QSlider* m_hardnessSlider = nullptr;
    QDoubleSpinBox* m_opacitySpin = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QDoubleSpinBox* m_flowSpin = nullptr;
    QSlider* m_flowSlider = nullptr;
    QDoubleSpinBox* m_spacingSpin = nullptr;
    QSlider* m_spacingSlider = nullptr;
    QDoubleSpinBox* m_stabilizationSpin = nullptr;
    QDoubleSpinBox* m_shapeFeatherSpin = nullptr;
    QCheckBox* m_shapeFilledCheck = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_redoButton = nullptr;
    QPushButton* m_clearButton = nullptr;

    void updateBlendModeEnabledState();
};

} // namespace an::ui::drawing
