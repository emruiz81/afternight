// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The NB Color Mapper dialog as its builder makes it, and its layer colour picker: the picker is offered only when
/// asked for, holds a hue and saturation in range, and paints its wheel in the hue each direction stands for.

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTableWidget>
#include <QWidget>

#include "processes/NBColorMapperProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

QString headerText(const QTableWidget* table, const int column)
{
    const auto* item = table ? table->horizontalHeaderItem(column) : nullptr;
    return item ? item->text() : QString();
}

bool isDescendantOf(QWidget* child, QWidget* ancestor)
{
    for (QWidget* widget = child; widget; widget = widget->parentWidget())
    {
        if (widget == ancestor)
        {
            return true;
        }
    }
    return false;
}

void expectHasToolTip(const QWidget* widget)
{
    ASSERT_NE(widget, nullptr);
    EXPECT_FALSE(widget->toolTip().isEmpty()) << widget->objectName().toStdString();
}

QPoint layerPickerPointForHue(const an::ui::VectorSlopeWidget& widget, const double hueDegrees)
{
    const auto& t = an::ui::tokens();
    const QRectF bounds = widget.rect().adjusted(0, 0, -1, -1);
    const double pad = static_cast<double>(t.spacing.sm);
    const QRectF inner = bounds.adjusted(pad, pad, -pad, -pad);
    const double side = (std::max)(1.0, (std::min)(inner.height(), inner.width() * 0.48));
    const QPointF center(inner.left() + side * 0.5, inner.top() + (inner.height() - side) * 0.5 + side * 0.5);
    const double sampleRadius = side * 0.32;
    const double radians = hueDegrees * kDegreesToRadians;
    return QPoint(static_cast<int>(std::lround(center.x() + sampleRadius * std::sin(radians))),
                  static_cast<int>(std::lround(center.y() - sampleRadius * std::cos(radians))));
}

QColor renderedLayerPickerWheelColorAtHue(an::ui::VectorSlopeWidget& widget, const double hueDegrees)
{
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image.pixelColor(layerPickerPointForHue(widget, hueDegrees));
}

} // namespace

// The dialog opens with its layer table, layer editor, background and output controls at their defaults, no button to
// update a layer by hand, and a tooltip on every control.
TEST(NBColorMapperProgrammaticUiTest, ConstructorDefaultsMatchNativeProcessContract)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    const auto ui = an::ui::processes::buildNBColorMapperProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.controlsScrollArea, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.histogramControlsGroup, nullptr);
    ASSERT_NE(ui.layerSettingsGroup, nullptr);
    ASSERT_NE(ui.colorControlsGroup, nullptr);
    ASSERT_NE(ui.modificationControlsGroup, nullptr);
    ASSERT_NE(ui.backgroundNeutralizationGroup, nullptr);
    ASSERT_NE(ui.outputOptionsGroup, nullptr);
    ASSERT_NE(ui.layersTable, nullptr);
    ASSERT_NE(ui.layerImageSelector, nullptr);
    ASSERT_NE(ui.layerColorPicker, nullptr);
    ASSERT_NE(ui.modificationMode, nullptr);
    ASSERT_NE(ui.backgroundEnabled, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.findChild<QComboBox*>(QStringLiteral("targetImageSelector")), nullptr);
    EXPECT_EQ(host.findChild<QComboBox*>(QStringLiteral("colorizeMethod")), nullptr);
    EXPECT_EQ(host.findChild<QPushButton*>(QStringLiteral("updateLayerButton")), nullptr);
    EXPECT_EQ(host.objectName(), QStringLiteral("NBColorMapperProcess"));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("NB Color Mapper"));
    EXPECT_EQ(host.minimumSize(), QSize(320, 220));
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.controlsScrollArea->objectName(), QStringLiteral("controlsScrollArea"));
    EXPECT_TRUE(ui.controlsScrollArea->widgetResizable());
    EXPECT_EQ(ui.controlsScrollArea->frameShape(), QFrame::NoFrame);
    EXPECT_EQ(ui.controlsScrollArea->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    ASSERT_NE(ui.controlsScrollArea->widget(), nullptr);
    EXPECT_TRUE(isDescendantOf(ui.chartScrollArea, ui.controlsScrollArea->widget()));
    EXPECT_FALSE(isDescendantOf(ui.buttonBox, ui.controlsScrollArea->widget()));
    EXPECT_TRUE(isDescendantOf(ui.histogramControlsGroup, ui.controlsScrollArea->widget()));
    EXPECT_TRUE(isDescendantOf(ui.layerSettingsGroup, ui.controlsScrollArea->widget()));
    EXPECT_TRUE(isDescendantOf(ui.colorControlsGroup, ui.controlsScrollArea->widget()));
    EXPECT_TRUE(isDescendantOf(ui.modificationControlsGroup, ui.controlsScrollArea->widget()));
    EXPECT_TRUE(isDescendantOf(ui.backgroundNeutralizationGroup, ui.controlsScrollArea->widget()));
    EXPECT_TRUE(isDescendantOf(ui.outputOptionsGroup, ui.controlsScrollArea->widget()));

    EXPECT_EQ(ui.histogramControlsGroup->title(), QStringLiteral("Histogram Controls"));
    EXPECT_EQ(ui.layerSettingsGroup->title(), QStringLiteral("Layer Settings"));
    EXPECT_EQ(ui.colorControlsGroup->title(), QStringLiteral("Color Controls"));
    EXPECT_EQ(ui.modificationControlsGroup->title(), QStringLiteral("Modification Controls"));
    EXPECT_EQ(ui.backgroundNeutralizationGroup->title(), QStringLiteral("Background Neutralization"));
    EXPECT_EQ(ui.outputOptionsGroup->title(), QStringLiteral("Output Options"));

    auto* splitter = host.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    ASSERT_NE(splitter, nullptr);
    host.show();
    QApplication::processEvents();

    const QList<int> splitterSizes = splitter->sizes();
    ASSERT_GE(splitterSizes.size(), 2);
    EXPECT_GE(splitterSizes[0], an::ui::processes::kNBColorMapperInitialControlsPaneWidth);

    EXPECT_EQ(ui.layersTable->objectName(), QStringLiteral("layersTable"));
    EXPECT_EQ(ui.layersTable->columnCount(), 5);
    EXPECT_EQ(headerText(ui.layersTable, 0), QStringLiteral("Include"));
    EXPECT_EQ(headerText(ui.layersTable, 1), QStringLiteral("Layer"));
    EXPECT_EQ(headerText(ui.layersTable, 2), QStringLiteral("Image"));
    EXPECT_EQ(headerText(ui.layersTable, 3), QStringLiteral("Color"));
    EXPECT_EQ(headerText(ui.layersTable, 4), QStringLiteral("Modifier"));
    EXPECT_GE(ui.layersTable->minimumHeight(),
              an::ui::processes::minimumNBColorMapperLayerTableHeight(
                  ui.layersTable, an::ui::processes::kNBColorMapperMinimumVisibleLayerRows));

    EXPECT_TRUE(ui.layerColorPicker->isLayerColorPickerEnabled());
    EXPECT_DOUBLE_EQ(ui.layerColorPicker->layerHueDegrees(), 0.0);
    EXPECT_DOUBLE_EQ(ui.layerColorPicker->layerSaturation(), 0.5);
    ui.layerColorPicker->setLayerColor(420.0, 1.5);
    EXPECT_DOUBLE_EQ(ui.layerColorPicker->layerHueDegrees(), 60.0);
    EXPECT_DOUBLE_EQ(ui.layerColorPicker->layerSaturation(), 1.0);

    EXPECT_FALSE(ui.backgroundEnabled->isChecked());
    EXPECT_FALSE(ui.backgroundRescaleHigh->isChecked());
    EXPECT_NEAR(ui.backgroundLowerSigma->value(), -2.8, 1.0e-9);
    EXPECT_NEAR(ui.backgroundUpperSigma->value(), 2.0, 1.0e-9);
    EXPECT_TRUE(ui.applyPreviewStf->isChecked());
    EXPECT_NEAR(ui.stfTargetBackground->value(), 0.25, 1.0e-9);
    EXPECT_FALSE(ui.returnColorizedLayers->isChecked());
    EXPECT_FALSE(ui.applyStfToCombinedOutput->isChecked());

    expectHasToolTip(ui.chartScrollArea);
    expectHasToolTip(ui.layersTable);
    expectHasToolTip(ui.addLayerButton);
    expectHasToolTip(ui.removeLayerButton);
    expectHasToolTip(ui.layerImageSelector);
    expectHasToolTip(ui.layerInclude);
    expectHasToolTip(ui.layerColorPicker);
    expectHasToolTip(ui.layerHue);
    expectHasToolTip(ui.layerSaturation);
    expectHasToolTip(ui.layerMidtones);
    expectHasToolTip(ui.layerBlackpoint);
    expectHasToolTip(ui.modificationMode);
    expectHasToolTip(ui.modificationLayerSelector);
    expectHasToolTip(ui.modificationFactor);
    expectHasToolTip(ui.modificationInvert);
    expectHasToolTip(ui.modificationMedianOffset);
    expectHasToolTip(ui.backgroundEnabled);
    expectHasToolTip(ui.backgroundRescaleHigh);
    expectHasToolTip(ui.backgroundLowerSigma);
    expectHasToolTip(ui.backgroundUpperSigma);
    expectHasToolTip(ui.applyPreviewStf);
    expectHasToolTip(ui.stfTargetBackground);
    expectHasToolTip(ui.returnColorizedLayers);
    expectHasToolTip(ui.applyStfToCombinedOutput);

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
    expectHasToolTip(ui.buttonBox->button(QDialogButtonBox::Apply));
    expectHasToolTip(ui.buttonBox->button(QDialogButtonBox::Close));
    expectHasToolTip(ui.buttonBox->button(QDialogButtonBox::Reset));
}

// The graph picks a layer's colour only once asked to - until then it lets every press through - and holds a hue turned
// into its circle and a saturation held at zero or above.
TEST(NBColorMapperProgrammaticUiTest, VectorSlopeLayerPickerModeIsOptIn)
{
    an::ui::VectorSlopeWidget graph;

    EXPECT_FALSE(graph.isLayerColorPickerEnabled());
    EXPECT_TRUE(graph.testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_FALSE(graph.vectorDefinitions().empty());

    graph.setLayerColorPickerEnabled(true);
    EXPECT_TRUE(graph.isLayerColorPickerEnabled());
    EXPECT_FALSE(graph.testAttribute(Qt::WA_TransparentForMouseEvents));

    graph.setLayerColor(-30.0, -0.25);
    EXPECT_DOUBLE_EQ(graph.layerHueDegrees(), 330.0);
    EXPECT_DOUBLE_EQ(graph.layerSaturation(), 0.0);
}

// The wheel is painted green at a hue of 120 degrees and blue at 240, in the direction each hue is picked at.
TEST(NBColorMapperProgrammaticUiTest, LayerPickerWheelPaintMatchesSelectedHueDirection)
{
    an::ui::VectorSlopeWidget graph;
    graph.setLayerColorPickerEnabled(true);
    graph.setLayerColor(0.0, 1.0);
    graph.resize(350, 160);
    graph.show();
    QApplication::processEvents();

    const QColor greenArea = renderedLayerPickerWheelColorAtHue(graph, 120.0);
    EXPECT_GT(greenArea.green(), greenArea.red());
    EXPECT_GT(greenArea.green(), greenArea.blue());

    const QColor blueArea = renderedLayerPickerWheelColorAtHue(graph, 240.0);
    EXPECT_GT(blueArea.blue(), blueArea.red());
    EXPECT_GT(blueArea.blue(), blueArea.green());
}
