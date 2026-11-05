// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSpacerItem>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

struct LRGBCombinationProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
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

    QComboBox* luminanceImageSelector = nullptr;
    QComboBox* rgbImageSelector = nullptr;

    QDoubleSpinBox* lightnessWeightValue = nullptr;
    QSlider* lightnessWeightSlider = nullptr;
    QDoubleSpinBox* redWeightValue = nullptr;
    QSlider* redWeightSlider = nullptr;
    QDoubleSpinBox* greenWeightValue = nullptr;
    QSlider* greenWeightSlider = nullptr;
    QDoubleSpinBox* blueWeightValue = nullptr;
    QSlider* blueWeightSlider = nullptr;

    QDoubleSpinBox* lightnessMTFValue = nullptr;
    QSlider* lightnessMTFSlider = nullptr;
    QDoubleSpinBox* saturationMTFValue = nullptr;
    QSlider* saturationMTFSlider = nullptr;

    QDialogButtonBox* buttonBox = nullptr;
};

inline LRGBCombinationProcessUiElements buildLRGBCombinationProcessUi(QWidget* parent)
{
    LRGBCombinationProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("LRGBCombinationProcess"));
    parent->resize(1000, 819);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(QStringLiteral("LRGB Combination"));

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    const auto split = rt_preview_ui::buildMainProcessSplitter(
        ui.horizontalLayoutMain, parent, QMargins(t.spacing.lg, t.spacing.sm, t.spacing.lg, t.spacing.sm));
    auto* controlsLayout = split.controlsLayout;
    auto* controlsPanel = split.controlsPanel;
    ui.previewLayout = split.previewLayout;

    auto* chartLayout = new QHBoxLayout();
    chartLayout->setObjectName(QStringLiteral("horizontalLayout_5"));

    ui.chartScrollArea = new QScrollArea(controlsPanel);
    ui.chartScrollArea->setObjectName(QStringLiteral("chartScrollArea"));
    ui.chartScrollArea->setMinimumSize(QSize(380, 220));
    ui.chartScrollArea->setWidgetResizable(true);

    auto* scrollAreaWidgetContents = new QWidget();
    scrollAreaWidgetContents->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
    scrollAreaWidgetContents->setMinimumSize(QSize(378, 218));
    ui.chartScrollArea->setWidget(scrollAreaWidgetContents);

    chartLayout->addWidget(ui.chartScrollArea);
    controlsLayout->addLayout(chartLayout);

    auto* histogramSettingsLayout = new QHBoxLayout();
    histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout_2"));
    const auto toolbar = rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                              {.translationContext = "LRGBCombinationProcess"});
    ui.zoomIn = toolbar.zoomIn;
    ui.zoomOut = toolbar.zoomOut;
    ui.zoomReset = toolbar.zoomReset;
    ui.gridButton = toolbar.gridButton;
    ui.showClippingInfo = toolbar.showClippingInfo;
    ui.localChannelNormalization = toolbar.localChannelNormalization;
    ui.showHistogramAreas = toolbar.showHistogramAreas;
    ui.showHistogram = toolbar.showHistogram;
    ui.plotResolution = toolbar.plotResolution;
    controlsLayout->addLayout(histogramSettingsLayout);

    auto* selectorsLayout = new QFormLayout();
    selectorsLayout->setObjectName(QStringLiteral("formLayout"));

    auto* luminanceLabel = new QLabel(QStringLiteral("Luminance"), parent);
    luminanceLabel->setObjectName(QStringLiteral("label_3"));
    selectorsLayout->setWidget(0, QFormLayout::LabelRole, luminanceLabel);

    ui.luminanceImageSelector = new QComboBox(parent);
    ui.luminanceImageSelector->setObjectName(QStringLiteral("luminanceImageSelector"));
    ui.luminanceImageSelector->setMinimumWidth(150);
    ui.luminanceImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorsLayout->setWidget(0, QFormLayout::FieldRole, ui.luminanceImageSelector);

    auto* rgbLabel = new QLabel(QStringLiteral("RGB"), parent);
    rgbLabel->setObjectName(QStringLiteral("label_4"));
    selectorsLayout->setWidget(1, QFormLayout::LabelRole, rgbLabel);

    ui.rgbImageSelector = new QComboBox(parent);
    ui.rgbImageSelector->setObjectName(QStringLiteral("rgbImageSelector"));
    ui.rgbImageSelector->setMinimumWidth(150);
    ui.rgbImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorsLayout->setWidget(1, QFormLayout::FieldRole, ui.rgbImageSelector);

    controlsLayout->addLayout(selectorsLayout);

    auto* channelWeightsGroupBox = new QGroupBox(QStringLiteral("Channel Weights"), parent);
    channelWeightsGroupBox->setObjectName(QStringLiteral("channelWeightsGroupBox"));
    auto* channelWeightsLayout = new QVBoxLayout(channelWeightsGroupBox);
    channelWeightsLayout->setObjectName(QStringLiteral("verticalLayout_3"));

    auto* channelWeightsGrid = new QGridLayout();
    channelWeightsGrid->setObjectName(QStringLiteral("gridLayout"));

    auto* lightnessLabel = new QLabel(QStringLiteral("Lightness"), channelWeightsGroupBox);
    lightnessLabel->setObjectName(QStringLiteral("label"));
    channelWeightsGrid->addWidget(lightnessLabel, 0, 0, 1, 1);

    ui.lightnessWeightValue = new QDoubleSpinBox(channelWeightsGroupBox);
    ui.lightnessWeightValue->setObjectName(QStringLiteral("lightnessWeightValue"));
    ui.lightnessWeightValue->setMaximum(1.0);
    ui.lightnessWeightValue->setSingleStep(0.05);
    ui.lightnessWeightValue->setValue(1.0);
    ui.lightnessWeightValue->setMinimumWidth(80);
    channelWeightsGrid->addWidget(ui.lightnessWeightValue, 0, 2, 1, 1);

    ui.lightnessWeightSlider = new QSlider(Qt::Horizontal, channelWeightsGroupBox);
    ui.lightnessWeightSlider->setObjectName(QStringLiteral("lightnessWeightSlider"));
    ui.lightnessWeightSlider->setMaximum(1000);
    ui.lightnessWeightSlider->setValue(1000);
    ui.lightnessWeightSlider->setTracking(false);
    ui.lightnessWeightSlider->setMinimumWidth(150);
    channelWeightsGrid->addWidget(ui.lightnessWeightSlider, 0, 1, 1, 1);

    auto* redLabel = new QLabel(QStringLiteral("Red"), channelWeightsGroupBox);
    redLabel->setObjectName(QStringLiteral("label_2"));
    channelWeightsGrid->addWidget(redLabel, 1, 0, 1, 1);

    ui.redWeightSlider = new QSlider(Qt::Horizontal, channelWeightsGroupBox);
    ui.redWeightSlider->setObjectName(QStringLiteral("redWeightSlider"));
    ui.redWeightSlider->setMaximum(1000);
    ui.redWeightSlider->setValue(1000);
    ui.redWeightSlider->setTracking(false);
    channelWeightsGrid->addWidget(ui.redWeightSlider, 1, 1, 1, 1);

    ui.redWeightValue = new QDoubleSpinBox(channelWeightsGroupBox);
    ui.redWeightValue->setObjectName(QStringLiteral("redWeightValue"));
    ui.redWeightValue->setMaximum(1.0);
    ui.redWeightValue->setSingleStep(0.05);
    ui.redWeightValue->setValue(1.0);
    ui.redWeightValue->setMinimumWidth(80);
    channelWeightsGrid->addWidget(ui.redWeightValue, 1, 2, 1, 1);

    auto* greenLabel = new QLabel(QStringLiteral("Green"), channelWeightsGroupBox);
    greenLabel->setObjectName(QStringLiteral("label_5"));
    channelWeightsGrid->addWidget(greenLabel, 2, 0, 1, 1);

    ui.greenWeightSlider = new QSlider(Qt::Horizontal, channelWeightsGroupBox);
    ui.greenWeightSlider->setObjectName(QStringLiteral("greenWeightSlider"));
    ui.greenWeightSlider->setMaximum(1000);
    ui.greenWeightSlider->setValue(1000);
    ui.greenWeightSlider->setTracking(false);
    channelWeightsGrid->addWidget(ui.greenWeightSlider, 2, 1, 1, 1);

    ui.greenWeightValue = new QDoubleSpinBox(channelWeightsGroupBox);
    ui.greenWeightValue->setObjectName(QStringLiteral("greenWeightValue"));
    ui.greenWeightValue->setMaximum(1.0);
    ui.greenWeightValue->setSingleStep(0.05);
    ui.greenWeightValue->setValue(1.0);
    ui.greenWeightValue->setMinimumWidth(80);
    channelWeightsGrid->addWidget(ui.greenWeightValue, 2, 2, 1, 1);

    auto* blueLabel = new QLabel(QStringLiteral("Blue"), channelWeightsGroupBox);
    blueLabel->setObjectName(QStringLiteral("label_6"));
    channelWeightsGrid->addWidget(blueLabel, 3, 0, 1, 1);

    ui.blueWeightSlider = new QSlider(Qt::Horizontal, channelWeightsGroupBox);
    ui.blueWeightSlider->setObjectName(QStringLiteral("blueWeightSlider"));
    ui.blueWeightSlider->setMaximum(1000);
    ui.blueWeightSlider->setValue(1000);
    ui.blueWeightSlider->setTracking(false);
    channelWeightsGrid->addWidget(ui.blueWeightSlider, 3, 1, 1, 1);

    ui.blueWeightValue = new QDoubleSpinBox(channelWeightsGroupBox);
    ui.blueWeightValue->setObjectName(QStringLiteral("blueWeightValue"));
    ui.blueWeightValue->setMaximum(1.0);
    ui.blueWeightValue->setSingleStep(0.05);
    ui.blueWeightValue->setValue(1.0);
    ui.blueWeightValue->setMinimumWidth(80);
    channelWeightsGrid->addWidget(ui.blueWeightValue, 3, 2, 1, 1);

    channelWeightsLayout->addLayout(channelWeightsGrid);
    controlsLayout->addWidget(channelWeightsGroupBox);

    auto* transferFunctionsGroupBox = new QGroupBox(QStringLiteral("Transfer Functions"), parent);
    transferFunctionsGroupBox->setObjectName(QStringLiteral("verticalGroupBox"));
    auto* transferFunctionsLayout = new QVBoxLayout(transferFunctionsGroupBox);
    transferFunctionsLayout->setObjectName(QStringLiteral("verticalLayout_4"));

    auto* transferFunctionsGrid = new QGridLayout();
    transferFunctionsGrid->setObjectName(QStringLiteral("gridLayout_2"));

    auto* lightnessMTFLabel = new QLabel(QStringLiteral("Lightness"), transferFunctionsGroupBox);
    lightnessMTFLabel->setObjectName(QStringLiteral("label_7"));
    transferFunctionsGrid->addWidget(lightnessMTFLabel, 0, 0, 1, 1);

    ui.lightnessMTFSlider = new QSlider(Qt::Horizontal, transferFunctionsGroupBox);
    ui.lightnessMTFSlider->setObjectName(QStringLiteral("lightnessMTFSlider"));
    ui.lightnessMTFSlider->setMaximum(1000);
    ui.lightnessMTFSlider->setValue(500);
    ui.lightnessMTFSlider->setTracking(false);
    transferFunctionsGrid->addWidget(ui.lightnessMTFSlider, 0, 1, 1, 1);

    ui.lightnessMTFValue = new QDoubleSpinBox(transferFunctionsGroupBox);
    ui.lightnessMTFValue->setObjectName(QStringLiteral("lightnessMTFValue"));
    ui.lightnessMTFValue->setMaximum(1.0);
    ui.lightnessMTFValue->setSingleStep(0.05);
    ui.lightnessMTFValue->setValue(0.5);
    ui.lightnessMTFValue->setMinimumWidth(80);
    transferFunctionsGrid->addWidget(ui.lightnessMTFValue, 0, 2, 1, 1);

    auto* saturationLabel = new QLabel(QStringLiteral("Saturation"), transferFunctionsGroupBox);
    saturationLabel->setObjectName(QStringLiteral("label_10"));
    transferFunctionsGrid->addWidget(saturationLabel, 1, 0, 1, 1);

    ui.saturationMTFSlider = new QSlider(Qt::Horizontal, transferFunctionsGroupBox);
    ui.saturationMTFSlider->setObjectName(QStringLiteral("saturationMTFSlider"));
    ui.saturationMTFSlider->setMaximum(1000);
    ui.saturationMTFSlider->setValue(500);
    ui.saturationMTFSlider->setTracking(false);
    transferFunctionsGrid->addWidget(ui.saturationMTFSlider, 1, 1, 1, 1);

    ui.saturationMTFValue = new QDoubleSpinBox(transferFunctionsGroupBox);
    ui.saturationMTFValue->setObjectName(QStringLiteral("saturationMTFValue"));
    ui.saturationMTFValue->setMaximum(1.0);
    ui.saturationMTFValue->setSingleStep(0.05);
    ui.saturationMTFValue->setValue(0.5);
    ui.saturationMTFValue->setMinimumWidth(80);
    transferFunctionsGrid->addWidget(ui.saturationMTFValue, 1, 2, 1, 1);

    transferFunctionsLayout->addLayout(transferFunctionsGrid);
    controlsLayout->addWidget(transferFunctionsGroupBox);

    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setObjectName(QStringLiteral("horizontalLayout_3"));
    buttonLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

    ui.buttonBox = new QDialogButtonBox(parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    ui.buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset);
    buttonLayout->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonLayout);

    ui.luminanceImageSelector->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Select the luminance image to combine with the RGB target.", nullptr));
    ui.rgbImageSelector->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Select the RGB target image to receive luminance detail.", nullptr));
    ui.lightnessWeightValue->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Blend strength of the luminance contribution.", nullptr));
    ui.lightnessWeightSlider->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Blend strength of the luminance contribution.", nullptr));
    ui.redWeightValue->setToolTip(
        QCoreApplication::translate("LRGBCombinationProcess", "Weight multiplier for the red channel.", nullptr));
    ui.redWeightSlider->setToolTip(
        QCoreApplication::translate("LRGBCombinationProcess", "Weight multiplier for the red channel.", nullptr));
    ui.greenWeightValue->setToolTip(
        QCoreApplication::translate("LRGBCombinationProcess", "Weight multiplier for the green channel.", nullptr));
    ui.greenWeightSlider->setToolTip(
        QCoreApplication::translate("LRGBCombinationProcess", "Weight multiplier for the green channel.", nullptr));
    ui.blueWeightValue->setToolTip(
        QCoreApplication::translate("LRGBCombinationProcess", "Weight multiplier for the blue channel.", nullptr));
    ui.blueWeightSlider->setToolTip(
        QCoreApplication::translate("LRGBCombinationProcess", "Weight multiplier for the blue channel.", nullptr));
    ui.lightnessMTFValue->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Midtones transfer value for luminance shaping.", nullptr));
    ui.lightnessMTFSlider->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Midtones transfer value for luminance shaping.", nullptr));
    ui.saturationMTFValue->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Midtones transfer value for saturation shaping.", nullptr));
    ui.saturationMTFSlider->setToolTip(QCoreApplication::translate(
        "LRGBCombinationProcess", "Midtones transfer value for saturation shaping.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("LRGBCombinationProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("LRGBCombinationProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
