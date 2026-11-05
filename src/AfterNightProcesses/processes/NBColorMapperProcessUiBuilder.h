// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"
#include "widgets/VectorSlopeWidget.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace an::ui::processes
{

inline constexpr int kNBColorMapperInitialControlsPaneWidth = 520;
inline constexpr int kNBColorMapperMinimumVisibleLayerRows = 3;

struct NBColorMapperProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QScrollArea* controlsScrollArea = nullptr;
    QVBoxLayout* previewLayout = nullptr;

    QScrollArea* chartScrollArea = nullptr;
    QToolButton* zoomIn = nullptr;
    QToolButton* zoomOut = nullptr;
    QToolButton* zoomReset = nullptr;
    QToolButton* gridButton = nullptr;
    QToolButton* showClippingInfo = nullptr;
    QToolButton* localChannelNormalization = nullptr;
    QToolButton* showHistogramAreas = nullptr;
    QToolButton* showHistogram = nullptr;
    QComboBox* plotResolution = nullptr;

    QGroupBox* histogramControlsGroup = nullptr;
    QGroupBox* layerSettingsGroup = nullptr;
    QGroupBox* colorControlsGroup = nullptr;
    QGroupBox* modificationControlsGroup = nullptr;
    QGroupBox* backgroundNeutralizationGroup = nullptr;
    QGroupBox* outputOptionsGroup = nullptr;

    QTableWidget* layersTable = nullptr;
    QPushButton* addLayerButton = nullptr;
    QPushButton* removeLayerButton = nullptr;

    QComboBox* layerImageSelector = nullptr;
    QCheckBox* layerInclude = nullptr;
    an::ui::VectorSlopeWidget* layerColorPicker = nullptr;
    QDoubleSpinBox* layerHue = nullptr;
    QDoubleSpinBox* layerSaturation = nullptr;
    QDoubleSpinBox* layerMidtones = nullptr;
    QDoubleSpinBox* layerBlackpoint = nullptr;
    QComboBox* modificationMode = nullptr;
    QComboBox* modificationLayerSelector = nullptr;
    QCheckBox* modificationInvert = nullptr;
    QCheckBox* modificationMedianOffset = nullptr;
    QDoubleSpinBox* modificationFactor = nullptr;

    QCheckBox* backgroundEnabled = nullptr;
    QCheckBox* backgroundRescaleHigh = nullptr;
    QDoubleSpinBox* backgroundLowerSigma = nullptr;
    QDoubleSpinBox* backgroundUpperSigma = nullptr;
    QCheckBox* applyPreviewStf = nullptr;
    QDoubleSpinBox* stfTargetBackground = nullptr;
    QCheckBox* returnColorizedLayers = nullptr;
    QCheckBox* applyStfToCombinedOutput = nullptr;

    QDialogButtonBox* buttonBox = nullptr;
};

inline QDoubleSpinBox* makeNBColorMapperDoubleSpinBox(QWidget* parent, const QString& objectName, const double minimum,
                                                      const double maximum, const double step, const double value,
                                                      const int decimals = 2)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setValue(value);
    spin->setMinimumWidth(88);
    return spin;
}

inline int minimumNBColorMapperLayerTableHeight(const QTableWidget* table, const int visibleRows)
{
    if (!table)
    {
        return 0;
    }

    const int rows = (std::max)(0, visibleRows);
    const int rowHeight = table->verticalHeader() ? table->verticalHeader()->defaultSectionSize() : 0;
    const int headerHeight = table->horizontalHeader() ? table->horizontalHeader()->sizeHint().height() : 0;
    const int scrollBarAllowance =
        table->style() ? table->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, table) : 0;
    return headerHeight + rows * rowHeight + 2 * table->frameWidth() + scrollBarAllowance;
}

inline void applyNBColorMapperToolTips(const NBColorMapperProcessUiElements& ui)
{
    if (ui.chartScrollArea)
    {
        ui.chartScrollArea->setToolTip(QStringLiteral(
            "Histogram preview for the combined layer output. Use the toolbar below it to zoom, reset, show clipping, "
            "toggle the grid, and choose the histogram resolution."));
    }
    if (ui.layersTable)
    {
        ui.layersTable->setToolTip(QStringLiteral(
            "Layers that will be blended into the result. Select a row to edit it below; changes are saved to the "
            "selected row and reflected in the real-time preview immediately."));
    }
    if (ui.addLayerButton)
    {
        ui.addLayerButton->setToolTip(QStringLiteral(
            "Select Add before choosing another image. The new row copies the current Layer Settings and becomes the "
            "selected row for the next source."));
    }
    if (ui.removeLayerButton)
    {
        ui.removeLayerButton->setToolTip(QStringLiteral(
            "Remove the selected layer from the blend. Other layers that used it as a modifier are reset to no "
            "modifier."));
    }
    if (ui.layerImageSelector)
    {
        ui.layerImageSelector->setToolTip(QStringLiteral(
            "Source image for the selected layer. Mono images can be colorized; RGB images pass through as RGB layers. "
            "Only compatible still images are listed."));
    }
    if (ui.layerInclude)
    {
        ui.layerInclude->setToolTip(QStringLiteral(
            "Include this layer in the final screen blend. Turn it off to keep the layer available for editing or as a "
            "reference without contributing to the output."));
    }
    if (ui.layerColorPicker)
    {
        ui.layerColorPicker->setToolTip(QStringLiteral(
            "Pick the hue and saturation for the selected mono layer. Drag in the wheel to choose hue; drag in the "
            "vertical strip to choose saturation. RGB layers ignore this color."));
    }
    if (ui.layerHue)
    {
        ui.layerHue->setToolTip(QStringLiteral(
            "Color angle for mono layers, in degrees. 0 is red, 120 is green, and 240 is blue. Mono layers are "
            "colorized with the script's HSV behavior; RGB layers ignore this value."));
    }
    if (ui.layerSaturation)
    {
        ui.layerSaturation->setToolTip(QStringLiteral(
            "Color strength for mono layers. 0 keeps the layer neutral; 1 applies full color. RGB layers ignore this "
            "value."));
    }
    if (ui.layerMidtones)
    {
        ui.layerMidtones->setToolTip(QStringLiteral(
            "Per-layer midtones stretch before colorizing and blending. 0.5 is neutral; smaller values brighten faint "
            "signal, while larger values hold it back."));
    }
    if (ui.layerBlackpoint)
    {
        ui.layerBlackpoint->setToolTip(QStringLiteral(
            "Per-layer black point before colorizing and blending. Increase it to suppress background and very faint "
            "signal in this layer."));
    }
    if (ui.modificationMode)
    {
        ui.modificationMode->setToolTip(QStringLiteral(
            "Use another layer as a modifier for the selected mono layer. Add adds modifier signal, Multiply masks by "
            "it, and Boost strengthens this layer where the modifier is bright."));
    }
    if (ui.modificationLayerSelector)
    {
        ui.modificationLayerSelector->setToolTip(QStringLiteral(
            "Layer used as the modifier. A layer cannot modify itself. Modifier layers are used as grayscale masks for "
            "this calculation."));
    }
    if (ui.modificationFactor)
    {
        ui.modificationFactor->setToolTip(QStringLiteral(
            "Strength of the selected modifier. 1 uses the modifier as-is; higher values make the modifier effect "
            "stronger."));
    }
    if (ui.modificationInvert)
    {
        ui.modificationInvert->setToolTip(QStringLiteral(
            "Invert the modifier before applying it, so dark areas of the modifier become strong and bright areas "
            "become weak."));
    }
    if (ui.modificationMedianOffset)
    {
        ui.modificationMedianOffset->setToolTip(QStringLiteral(
            "Subtract the modifier median before applying it. This keeps mostly above-background modifier signal and "
            "reduces broad background influence."));
    }
    if (ui.backgroundEnabled)
    {
        ui.backgroundEnabled->setToolTip(QStringLiteral(
            "Neutralize RGB background after the layers are blended by subtracting a sampled background level from "
            "each color channel."));
    }
    if (ui.backgroundRescaleHigh)
    {
        ui.backgroundRescaleHigh->setToolTip(QStringLiteral(
            "After background neutralization, rescale each channel using its corrected high value. This can recover "
            "headroom but may change channel balance more aggressively."));
    }
    if (ui.backgroundLowerSigma)
    {
        ui.backgroundLowerSigma->setToolTip(QStringLiteral(
            "Lower sigma limit for choosing background samples. More negative values accept darker samples; tighter "
            "values reject more low outliers."));
    }
    if (ui.backgroundUpperSigma)
    {
        ui.backgroundUpperSigma->setToolTip(QStringLiteral(
            "Upper sigma limit for choosing background samples. Lower values reject more bright structure such as "
            "stars or nebulosity."));
    }
    if (ui.applyPreviewStf)
    {
        ui.applyPreviewStf->setToolTip(QStringLiteral(
            "Apply a screen-transfer style stretch to the real-time preview only. This helps inspect linear data "
            "without forcing the final output to be stretched."));
    }
    if (ui.stfTargetBackground)
    {
        ui.stfTargetBackground->setToolTip(QStringLiteral(
            "Target background level for the preview STF and optional final STF. Lower values make the preview darker; "
            "higher values make it brighter."));
    }
    if (ui.returnColorizedLayers)
    {
        ui.returnColorizedLayers->setToolTip(QStringLiteral(
            "When applying the process, also create separate image windows for mono layers after colorization. Preview "
            "updates never create these extra images."));
    }
    if (ui.applyStfToCombinedOutput)
    {
        ui.applyStfToCombinedOutput->setToolTip(QStringLiteral(
            "Bake the STF stretch into the final combined image. Leave this off if you want a more linear result for "
            "additional processing."));
    }
    if (ui.buttonBox)
    {
        if (auto* applyButton = ui.buttonBox->button(QDialogButtonBox::Apply))
        {
            applyButton->setToolTip(QStringLiteral(
                "Create the combined NB Color Mapper image from the current layer list and output settings."));
        }
        if (auto* resetButton = ui.buttonBox->button(QDialogButtonBox::Reset))
        {
            resetButton->setToolTip(QStringLiteral("Restore default NB Color Mapper settings."));
        }
        if (auto* closeButton = ui.buttonBox->button(QDialogButtonBox::Close))
        {
            closeButton->setToolTip(QStringLiteral("Close the NB Color Mapper process window."));
        }
    }
}

inline NBColorMapperProcessUiElements buildNBColorMapperProcessUi(QWidget* parent)
{
    NBColorMapperProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("NBColorMapperProcess"));
    parent->resize(1100, 860);
    parent->setMinimumSize(QSize(320, 220));
    parent->setWindowTitle(QStringLiteral("NB Color Mapper"));

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    const auto split = rt_preview_ui::buildMainProcessSplitter(
        ui.horizontalLayoutMain, parent, QMargins(t.spacing.lg, t.spacing.sm, t.spacing.lg, t.spacing.sm));
    auto* outerControlsLayout = split.controlsLayout;
    auto* controlsPanel = split.controlsPanel;
    ui.previewLayout = split.previewLayout;

    ui.controlsScrollArea = new QScrollArea(controlsPanel);
    ui.controlsScrollArea->setObjectName(QStringLiteral("controlsScrollArea"));
    ui.controlsScrollArea->setWidgetResizable(true);
    ui.controlsScrollArea->setFrameShape(QFrame::NoFrame);
    ui.controlsScrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* controlsScrollContents = new QWidget();
    controlsScrollContents->setObjectName(QStringLiteral("controlsScrollContents"));
    auto* controlsLayout = new QVBoxLayout(controlsScrollContents);
    controlsLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    controlsLayout->setSpacing(t.spacing.sm);
    ui.controlsScrollArea->setWidget(controlsScrollContents);
    outerControlsLayout->addWidget(ui.controlsScrollArea);

    ui.histogramControlsGroup = new QGroupBox(QStringLiteral("Histogram Controls"), controlsScrollContents);
    ui.histogramControlsGroup->setObjectName(QStringLiteral("histogramControlsGroup"));
    auto* histogramControlsLayout = new QVBoxLayout(ui.histogramControlsGroup);
    histogramControlsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    histogramControlsLayout->setSpacing(t.spacing.sm);

    ui.chartScrollArea = new QScrollArea(ui.histogramControlsGroup);
    ui.chartScrollArea->setObjectName(QStringLiteral("chartScrollArea"));
    ui.chartScrollArea->setMinimumSize(QSize(380, 220));
    ui.chartScrollArea->setWidgetResizable(true);
    auto* chartContents = new QWidget();
    chartContents->setObjectName(QStringLiteral("chartScrollAreaContents"));
    ui.chartScrollArea->setWidget(chartContents);
    histogramControlsLayout->addWidget(ui.chartScrollArea);

    auto* histogramSettingsLayout = new QHBoxLayout();
    histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout"));
    const auto toolbar = rt_preview_ui::buildHistogramToolbar(ui.histogramControlsGroup, histogramSettingsLayout,
                                                              {.translationContext = "NBColorMapperProcess"});
    ui.zoomIn = toolbar.zoomIn;
    ui.zoomOut = toolbar.zoomOut;
    ui.zoomReset = toolbar.zoomReset;
    ui.gridButton = toolbar.gridButton;
    ui.showClippingInfo = toolbar.showClippingInfo;
    ui.localChannelNormalization = toolbar.localChannelNormalization;
    ui.showHistogramAreas = toolbar.showHistogramAreas;
    ui.showHistogram = toolbar.showHistogram;
    ui.plotResolution = toolbar.plotResolution;
    histogramControlsLayout->addLayout(histogramSettingsLayout);

    auto* histogramParamsLayout = new QFormLayout();
    histogramParamsLayout->setObjectName(QStringLiteral("histogramParamsLayout"));
    histogramParamsLayout->setContentsMargins(0, 0, 0, 0);
    histogramParamsLayout->setSpacing(t.spacing.sm);
    ui.layerMidtones = makeNBColorMapperDoubleSpinBox(ui.histogramControlsGroup, QStringLiteral("layerMidtones"), 0.001,
                                                      0.999, 0.01, 0.5, 3);
    histogramParamsLayout->addRow(QStringLiteral("Midtones"), ui.layerMidtones);
    ui.layerBlackpoint = makeNBColorMapperDoubleSpinBox(ui.histogramControlsGroup, QStringLiteral("layerBlackpoint"),
                                                        0.0, 1.0, 0.001, 0.0, 3);
    histogramParamsLayout->addRow(QStringLiteral("Blackpoint"), ui.layerBlackpoint);
    histogramControlsLayout->addLayout(histogramParamsLayout);
    controlsLayout->addWidget(ui.histogramControlsGroup);

    auto* layersGroup = new QGroupBox(QStringLiteral("Layers"), controlsScrollContents);
    layersGroup->setObjectName(QStringLiteral("layersGroup"));
    auto* layersLayout = new QVBoxLayout(layersGroup);
    layersLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    layersLayout->setSpacing(t.spacing.sm);

    ui.layersTable = new QTableWidget(layersGroup);
    ui.layersTable->setObjectName(QStringLiteral("layersTable"));
    ui.layersTable->setColumnCount(5);
    ui.layersTable->setHorizontalHeaderLabels({QStringLiteral("Include"), QStringLiteral("Layer"),
                                               QStringLiteral("Image"), QStringLiteral("Color"),
                                               QStringLiteral("Modifier")});
    ui.layersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.layersTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.layersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui.layersTable->verticalHeader()->setVisible(false);
    ui.layersTable->horizontalHeader()->setStretchLastSection(true);
    ui.layersTable->setMinimumHeight(
        minimumNBColorMapperLayerTableHeight(ui.layersTable, kNBColorMapperMinimumVisibleLayerRows));
    layersLayout->addWidget(ui.layersTable);

    auto* layerButtons = new QHBoxLayout();
    layerButtons->setObjectName(QStringLiteral("layerButtons"));
    layerButtons->setSpacing(t.spacing.sm);
    ui.addLayerButton = new QPushButton(QStringLiteral("Add"), layersGroup);
    ui.addLayerButton->setObjectName(QStringLiteral("addLayerButton"));
    ui.removeLayerButton = new QPushButton(QStringLiteral("Remove"), layersGroup);
    ui.removeLayerButton->setObjectName(QStringLiteral("removeLayerButton"));
    layerButtons->addWidget(ui.addLayerButton);
    layerButtons->addWidget(ui.removeLayerButton);
    layerButtons->addItem(new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
    layersLayout->addLayout(layerButtons);
    controlsLayout->addWidget(layersGroup);

    ui.layerSettingsGroup = new QGroupBox(QStringLiteral("Layer Settings"), controlsScrollContents);
    ui.layerSettingsGroup->setObjectName(QStringLiteral("layerSettingsGroup"));
    auto* layerEditorLayout = new QFormLayout(ui.layerSettingsGroup);
    layerEditorLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    layerEditorLayout->setSpacing(t.spacing.sm);

    ui.layerImageSelector = new QComboBox(ui.layerSettingsGroup);
    ui.layerImageSelector->setObjectName(QStringLiteral("layerImageSelector"));
    ui.layerImageSelector->setMinimumWidth(150);
    ui.layerImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layerEditorLayout->addRow(QStringLiteral("Image"), ui.layerImageSelector);

    ui.layerInclude = new QCheckBox(QStringLiteral("Include layer"), ui.layerSettingsGroup);
    ui.layerInclude->setObjectName(QStringLiteral("layerInclude"));
    ui.layerInclude->setChecked(true);
    layerEditorLayout->addRow(ui.layerInclude);
    controlsLayout->addWidget(ui.layerSettingsGroup);

    ui.colorControlsGroup = new QGroupBox(QStringLiteral("Color Controls"), controlsScrollContents);
    ui.colorControlsGroup->setObjectName(QStringLiteral("colorControlsGroup"));
    auto* colorControlsLayout = new QVBoxLayout(ui.colorControlsGroup);
    colorControlsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    colorControlsLayout->setSpacing(t.spacing.sm);

    ui.layerColorPicker = new an::ui::VectorSlopeWidget(ui.colorControlsGroup);
    ui.layerColorPicker->setObjectName(QStringLiteral("layerColorPicker"));
    ui.layerColorPicker->setGraphHeight(160);
    ui.layerColorPicker->setLayerColorPickerEnabled(true);
    ui.layerColorPicker->setLayerColor(0.0, 0.5);
    colorControlsLayout->addWidget(ui.layerColorPicker);

    auto* colorNumericLayout = new QFormLayout();
    colorNumericLayout->setObjectName(QStringLiteral("colorNumericLayout"));
    colorNumericLayout->setContentsMargins(0, 0, 0, 0);
    colorNumericLayout->setSpacing(t.spacing.sm);
    ui.layerHue =
        makeNBColorMapperDoubleSpinBox(ui.colorControlsGroup, QStringLiteral("layerHue"), 0.0, 359.99, 1.0, 0.0, 3);
    colorNumericLayout->addRow(QStringLiteral("Hue"), ui.layerHue);
    ui.layerSaturation = makeNBColorMapperDoubleSpinBox(ui.colorControlsGroup, QStringLiteral("layerSaturation"), 0.0,
                                                        1.0, 0.01, 0.5, 3);
    colorNumericLayout->addRow(QStringLiteral("Saturation"), ui.layerSaturation);
    colorControlsLayout->addLayout(colorNumericLayout);
    controlsLayout->addWidget(ui.colorControlsGroup);

    ui.modificationControlsGroup = new QGroupBox(QStringLiteral("Modification Controls"), controlsScrollContents);
    ui.modificationControlsGroup->setObjectName(QStringLiteral("modificationControlsGroup"));
    auto* modificationControlsLayout = new QFormLayout(ui.modificationControlsGroup);
    modificationControlsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    modificationControlsLayout->setSpacing(t.spacing.sm);

    ui.modificationMode = new QComboBox(ui.modificationControlsGroup);
    ui.modificationMode->setObjectName(QStringLiteral("modificationMode"));
    ui.modificationMode->addItems(
        {QStringLiteral("None"), QStringLiteral("Add"), QStringLiteral("Multiply"), QStringLiteral("Boost")});
    modificationControlsLayout->addRow(QStringLiteral("Mode"), ui.modificationMode);

    ui.modificationLayerSelector = new QComboBox(ui.modificationControlsGroup);
    ui.modificationLayerSelector->setObjectName(QStringLiteral("modificationLayerSelector"));
    modificationControlsLayout->addRow(QStringLiteral("With layer"), ui.modificationLayerSelector);

    ui.modificationFactor = makeNBColorMapperDoubleSpinBox(ui.modificationControlsGroup,
                                                           QStringLiteral("modificationFactor"), 0.0, 10.0, 0.05, 1.0);
    modificationControlsLayout->addRow(QStringLiteral("Factor"), ui.modificationFactor);

    ui.modificationInvert = new QCheckBox(QStringLiteral("Invert modifier"), ui.modificationControlsGroup);
    ui.modificationInvert->setObjectName(QStringLiteral("modificationInvert"));
    modificationControlsLayout->addRow(ui.modificationInvert);

    ui.modificationMedianOffset = new QCheckBox(QStringLiteral("Median offset"), ui.modificationControlsGroup);
    ui.modificationMedianOffset->setObjectName(QStringLiteral("modificationMedianOffset"));
    modificationControlsLayout->addRow(ui.modificationMedianOffset);
    controlsLayout->addWidget(ui.modificationControlsGroup);

    ui.backgroundNeutralizationGroup =
        new QGroupBox(QStringLiteral("Background Neutralization"), controlsScrollContents);
    ui.backgroundNeutralizationGroup->setObjectName(QStringLiteral("backgroundNeutralizationGroup"));
    auto* backgroundLayout = new QGridLayout(ui.backgroundNeutralizationGroup);
    backgroundLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    backgroundLayout->setSpacing(t.spacing.sm);

    ui.backgroundEnabled = new QCheckBox(QStringLiteral("Enable"), ui.backgroundNeutralizationGroup);
    ui.backgroundEnabled->setObjectName(QStringLiteral("backgroundEnabled"));
    backgroundLayout->addWidget(ui.backgroundEnabled, 0, 0, 1, 2);

    ui.backgroundRescaleHigh = new QCheckBox(QStringLiteral("Rescale high"), ui.backgroundNeutralizationGroup);
    ui.backgroundRescaleHigh->setObjectName(QStringLiteral("backgroundRescaleHigh"));
    backgroundLayout->addWidget(ui.backgroundRescaleHigh, 0, 2, 1, 1);

    ui.backgroundLowerSigma = makeNBColorMapperDoubleSpinBox(
        ui.backgroundNeutralizationGroup, QStringLiteral("backgroundLowerSigma"), -10.0, 10.0, 0.1, -2.8, 2);
    backgroundLayout->addWidget(new QLabel(QStringLiteral("Lower sigma"), ui.backgroundNeutralizationGroup), 1, 0, 1,
                                1);
    backgroundLayout->addWidget(ui.backgroundLowerSigma, 1, 1, 1, 1);

    ui.backgroundUpperSigma = makeNBColorMapperDoubleSpinBox(
        ui.backgroundNeutralizationGroup, QStringLiteral("backgroundUpperSigma"), -10.0, 10.0, 0.1, 2.0, 2);
    backgroundLayout->addWidget(new QLabel(QStringLiteral("Upper sigma"), ui.backgroundNeutralizationGroup), 1, 2, 1,
                                1);
    backgroundLayout->addWidget(ui.backgroundUpperSigma, 1, 3, 1, 1);
    controlsLayout->addWidget(ui.backgroundNeutralizationGroup);

    ui.outputOptionsGroup = new QGroupBox(QStringLiteral("Output Options"), controlsScrollContents);
    ui.outputOptionsGroup->setObjectName(QStringLiteral("outputOptionsGroup"));
    auto* outputLayout = new QGridLayout(ui.outputOptionsGroup);
    outputLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    outputLayout->setSpacing(t.spacing.sm);

    ui.applyPreviewStf = new QCheckBox(QStringLiteral("Preview STF"), ui.outputOptionsGroup);
    ui.applyPreviewStf->setObjectName(QStringLiteral("applyPreviewStf"));
    ui.applyPreviewStf->setChecked(true);
    outputLayout->addWidget(ui.applyPreviewStf, 0, 0, 1, 2);

    ui.stfTargetBackground = makeNBColorMapperDoubleSpinBox(
        ui.outputOptionsGroup, QStringLiteral("stfTargetBackground"), 0.01, 0.99, 0.01, 0.25, 3);
    outputLayout->addWidget(new QLabel(QStringLiteral("STF target"), ui.outputOptionsGroup), 0, 2, 1, 1);
    outputLayout->addWidget(ui.stfTargetBackground, 0, 3, 1, 1);

    ui.returnColorizedLayers = new QCheckBox(QStringLiteral("Create colorized layer images"), ui.outputOptionsGroup);
    ui.returnColorizedLayers->setObjectName(QStringLiteral("returnColorizedLayers"));
    outputLayout->addWidget(ui.returnColorizedLayers, 1, 0, 1, 2);

    ui.applyStfToCombinedOutput = new QCheckBox(QStringLiteral("Apply STF to combined output"), ui.outputOptionsGroup);
    ui.applyStfToCombinedOutput->setObjectName(QStringLiteral("applyStfToCombinedOutput"));
    outputLayout->addWidget(ui.applyStfToCombinedOutput, 1, 2, 1, 2);
    controlsLayout->addWidget(ui.outputOptionsGroup);

    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    ui.buttonBox = new QDialogButtonBox(controlsPanel);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    ui.buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset);
    outerControlsLayout->addWidget(ui.buttonBox);

    const int controlsWidth = (std::max)(kNBColorMapperInitialControlsPaneWidth, controlsPanel->sizeHint().width());
    const int previewWidth = (std::max)(rt_preview_ui::kMinPreviewPaneWidth, parent->width() - controlsWidth);
    split.mainSplitter->setSizes({controlsWidth, previewWidth});

    applyNBColorMapperToolTips(ui);

    return ui;
}

} // namespace an::ui::processes
