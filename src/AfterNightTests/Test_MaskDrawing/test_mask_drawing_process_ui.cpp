// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QWidget>

#include "processes/MaskDrawingProcessUiBuilder.h"

namespace
{

TEST(MaskDrawingProcessUiTest, ConstructorDefaultsMatchBrushContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildMaskDrawingProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.drawingToolbar, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Mask Drawing"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_FALSE(ui.preview->isEnabled());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Ok), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Cancel), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);

    const auto brush = ui.drawingToolbar->currentBrushParams();
    EXPECT_NEAR(brush.size, 24.0f, 0.01f);
    EXPECT_NEAR(brush.opacity, 1.0f, 0.01f);
    EXPECT_NEAR(brush.hardness, 1.0f, 0.01f);
    EXPECT_NEAR(brush.flow, 1.0f, 0.01f);
    EXPECT_NEAR(brush.spacing, 0.25f, 0.01f);
    EXPECT_EQ(brush.tipShape, an::ui::drawing::BrushParams::TipShape::Round);
    EXPECT_EQ(brush.blendMode, an::ui::drawing::BrushParams::BlendMode::Normal);
    EXPECT_EQ(ui.drawingToolbar->activeTool(), an::ui::drawing::DrawingTool::Brush);
    EXPECT_EQ(ui.drawingToolbar->currentShapeType(), an::ui::drawing::ShapeObject::Type::Rectangle);
    EXPECT_NEAR(ui.drawingToolbar->shapeFeather(), 0.0f, 0.01f);
    EXPECT_TRUE(ui.drawingToolbar->isShapeFilled());
}

TEST(MaskDrawingProcessUiTest, ControlsExposePreviewModesFileActionsAndAdvancedTools)
{
    QWidget host;
    auto ui = an::ui::processes::buildMaskDrawingProcessUi(&host);

    auto* previewModeCombo = ui.drawingToolbar->findChild<QComboBox*>(QStringLiteral("maskPreviewModeCombo"));
    ASSERT_NE(previewModeCombo, nullptr);
    ASSERT_EQ(previewModeCombo->count(), 6);
    EXPECT_EQ(previewModeCombo->itemText(0), QStringLiteral("Red"));
    EXPECT_EQ(previewModeCombo->itemText(1), QStringLiteral("Green"));
    EXPECT_EQ(previewModeCombo->itemText(2), QStringLiteral("Blue"));
    EXPECT_EQ(previewModeCombo->itemText(3), QStringLiteral("Amber"));
    EXPECT_EQ(previewModeCombo->itemText(4), QStringLiteral("Grayscale"));
    EXPECT_EQ(previewModeCombo->itemText(5), QStringLiteral("Checkerboard"));

    auto* importMaskButton = ui.drawingToolbar->findChild<QPushButton*>(QStringLiteral("importMaskButton"));
    auto* exportMaskButton = ui.drawingToolbar->findChild<QPushButton*>(QStringLiteral("exportMaskButton"));
    auto* floodFillButton = ui.drawingToolbar->findChild<QPushButton*>(QStringLiteral("toolFloodFillButton"));
    auto* gradientFillButton = ui.drawingToolbar->findChild<QPushButton*>(QStringLiteral("toolGradientFillButton"));
    auto* stabilizationSpin = ui.drawingToolbar->findChild<QDoubleSpinBox*>(QStringLiteral("stabilizationSpin"));

    ASSERT_NE(importMaskButton, nullptr);
    ASSERT_NE(exportMaskButton, nullptr);
    ASSERT_NE(floodFillButton, nullptr);
    ASSERT_NE(gradientFillButton, nullptr);
    ASSERT_NE(stabilizationSpin, nullptr);

    EXPECT_EQ(importMaskButton->text(), QStringLiteral("Import Mask..."));
    EXPECT_EQ(exportMaskButton->text(), QStringLiteral("Export Mask..."));
    EXPECT_EQ(floodFillButton->text(), QStringLiteral("Fill"));
    EXPECT_EQ(gradientFillButton->text(), QStringLiteral("Gradient"));
    EXPECT_DOUBLE_EQ(stabilizationSpin->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(stabilizationSpin->value(), 0.0);
}

TEST(MaskDrawingProcessUiTest, ActiveToolButtonStaysCheckedWhenToolChangesProgrammatically)
{
    QWidget host;
    auto ui = an::ui::processes::buildMaskDrawingProcessUi(&host);

    auto* brushButton = ui.drawingToolbar->findChild<QPushButton*>(QStringLiteral("toolBrushButton"));
    auto* editButton = ui.drawingToolbar->findChild<QPushButton*>(QStringLiteral("toolEditButton"));
    auto* shapeTypeCombo = ui.drawingToolbar->findChild<QComboBox*>(QStringLiteral("shapeTypeCombo"));
    ASSERT_NE(brushButton, nullptr);
    ASSERT_NE(editButton, nullptr);
    ASSERT_NE(shapeTypeCombo, nullptr);
    EXPECT_TRUE(brushButton->isChecked());
    EXPECT_FALSE(editButton->isChecked());
    EXPECT_EQ(editButton->text(), QStringLiteral("Edit"));
    EXPECT_FALSE(shapeTypeCombo->isEnabled());

    ui.drawingToolbar->setActiveTool(an::ui::drawing::DrawingTool::ShapeSelect);

    EXPECT_FALSE(brushButton->isChecked());
    EXPECT_TRUE(editButton->isChecked());
    EXPECT_FALSE(shapeTypeCombo->isEnabled());

    ui.drawingToolbar->setActiveTool(an::ui::drawing::DrawingTool::ShapeDraw);

    EXPECT_TRUE(shapeTypeCombo->isEnabled());
}

} // anonymous namespace
