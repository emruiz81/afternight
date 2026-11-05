// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QSpacerItem>
#include <QSpinBox>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct AdjustBrightnessAndContrastProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QSplitter* mainSplitter = nullptr;
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
    QSpinBox* brightnessValue = nullptr;
    QSlider* brightnessSlider = nullptr;
    QSpinBox* contrastValue = nullptr;
    QSlider* contrastSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline AdjustBrightnessAndContrastProcessUiElements buildAdjustBrightnessAndContrastProcessUi(QWidget* parent)
{
    AdjustBrightnessAndContrastProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("AdjustBrightnessAndContrastProcess"));
    parent->resize(791, 611);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(
        QCoreApplication::translate("AdjustBrightnessAndContrastProcess", "Adjust Brightness/Contrast", nullptr));

    const auto& t = an::ui::tokens();

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(0, 0, 0, 0);

    const auto split = rt_preview_ui::buildMainProcessSplitter(
        ui.horizontalLayoutMain, parent, QMargins(t.spacing.lg, t.spacing.sm, t.spacing.lg, t.spacing.sm));

    ui.mainSplitter = split.mainSplitter;
    auto* controlsPanel = split.controlsPanel;
    auto* controlsLayout = split.controlsLayout;
    ui.previewLayout = split.previewLayout;

    auto* chartLayout = new QHBoxLayout();
    chartLayout->setObjectName(QStringLiteral("horizontalLayout_5"));

    ui.chartScrollArea = new QScrollArea(controlsPanel);
    ui.chartScrollArea->setObjectName(QStringLiteral("chartScrollArea"));
    ui.chartScrollArea->setMinimumSize(QSize(260, 220));
    ui.chartScrollArea->setWidgetResizable(true);

    auto* scrollAreaWidgetContents = new QWidget();
    scrollAreaWidgetContents->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
    scrollAreaWidgetContents->setMinimumSize(QSize(378, 218));
    ui.chartScrollArea->setWidget(scrollAreaWidgetContents);

    chartLayout->addWidget(ui.chartScrollArea);
    controlsLayout->addLayout(chartLayout);

    auto* histogramSettingsLayout = new QHBoxLayout();
    histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout_2"));

    const auto toolbar = rt_preview_ui::buildHistogramToolbar(
        controlsPanel, histogramSettingsLayout, {.translationContext = "AdjustBrightnessAndContrastProcess"});

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

    auto* brightnessLayout = new QHBoxLayout();
    brightnessLayout->setObjectName(QStringLiteral("horizontalLayout"));

    auto* brightnessLabel = new QLabel(QStringLiteral("Brightness"), controlsPanel);
    brightnessLabel->setObjectName(QStringLiteral("label"));
    brightnessLayout->addWidget(brightnessLabel);

    ui.brightnessValue = new QSpinBox(controlsPanel);
    ui.brightnessValue->setObjectName(QStringLiteral("brightnessValue"));
    ui.brightnessValue->setMinimum(-150);
    ui.brightnessValue->setMaximum(150);
    ui.brightnessValue->setMinimumWidth(80);
    brightnessLayout->addWidget(ui.brightnessValue);

    ui.brightnessSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.brightnessSlider->setObjectName(QStringLiteral("brightnessSlider"));
    ui.brightnessSlider->setMinimumSize(QSize(120, 0));
    ui.brightnessSlider->setMinimum(-150);
    ui.brightnessSlider->setMaximum(150);
    ui.brightnessSlider->setMinimumWidth(150);
    brightnessLayout->addWidget(ui.brightnessSlider);

    controlsLayout->addLayout(brightnessLayout);

    auto* contrastLayout = new QHBoxLayout();
    contrastLayout->setObjectName(QStringLiteral("horizontalLayout_2"));

    auto* contrastLabel = new QLabel(QStringLiteral("Contrast"), controlsPanel);
    contrastLabel->setObjectName(QStringLiteral("label_2"));
    contrastLayout->addWidget(contrastLabel);

    ui.contrastValue = new QSpinBox(controlsPanel);
    ui.contrastValue->setObjectName(QStringLiteral("contrastValue"));
    ui.contrastValue->setEnabled(true);
    ui.contrastValue->setMinimum(-100);
    ui.contrastValue->setMaximum(100);
    ui.contrastValue->setMinimumWidth(80);
    contrastLayout->addWidget(ui.contrastValue);

    ui.contrastSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.contrastSlider->setObjectName(QStringLiteral("contrastSlider"));
    ui.contrastSlider->setEnabled(true);
    ui.contrastSlider->setMinimumSize(QSize(120, 0));
    ui.contrastSlider->setMinimum(-100);
    ui.contrastSlider->setMaximum(100);
    ui.contrastSlider->setMinimumWidth(150);
    contrastLayout->addWidget(ui.contrastSlider);

    controlsLayout->addLayout(contrastLayout);
    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("formLayout"));

    auto* targetLabel = new QLabel(QStringLiteral("Target Image "), controlsPanel);
    targetLabel->setObjectName(QStringLiteral("label_3"));
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(controlsPanel);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);

    auto* channelLabel = new QLabel(QStringLiteral("Channel"), controlsPanel);
    channelLabel->setObjectName(QStringLiteral("label_4"));
    selectorLayout->setWidget(1, QFormLayout::LabelRole, channelLabel);

    ui.channelSelector = new QComboBox(controlsPanel);
    ui.channelSelector->setObjectName(QStringLiteral("channelSelector"));
    ui.channelSelector->setMinimumWidth(150);
    ui.channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(1, QFormLayout::FieldRole, ui.channelSelector);

    controlsLayout->addLayout(selectorLayout);

    auto* previewLayout = new QHBoxLayout();
    previewLayout->setObjectName(QStringLiteral("horizontalLayout_4"));

    ui.preview = new QCheckBox(QStringLiteral("Preview"), controlsPanel);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setChecked(true);
    previewLayout->addWidget(ui.preview);
    previewLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    controlsLayout->addLayout(previewLayout);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setObjectName(QStringLiteral("horizontalLayout_3"));
    buttonLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

    ui.buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                        controlsPanel);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    buttonLayout->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonLayout);

    ui.brightnessValue->setToolTip(QCoreApplication::translate(
        "AdjustBrightnessAndContrastProcess",
        "Adjust image brightness. Negative values darken, positive values brighten.", nullptr));
    ui.brightnessSlider->setToolTip(QCoreApplication::translate(
        "AdjustBrightnessAndContrastProcess",
        "Adjust image brightness. Negative values darken, positive values brighten.", nullptr));
    ui.contrastValue->setToolTip(QCoreApplication::translate(
        "AdjustBrightnessAndContrastProcess",
        "Adjust tonal contrast. Negative values flatten contrast, positive values increase separation.", nullptr));
    ui.contrastSlider->setToolTip(QCoreApplication::translate(
        "AdjustBrightnessAndContrastProcess",
        "Adjust tonal contrast. Negative values flatten contrast, positive values increase separation.", nullptr));
    ui.targetImageSelector->setToolTip(QCoreApplication::translate("AdjustBrightnessAndContrastProcess",
                                                                   "Choose the open image to process.", nullptr));
    ui.channelSelector->setToolTip(QCoreApplication::translate(
        "AdjustBrightnessAndContrastProcess", "Select the channel to process, or RGB to process all channels together.",
        nullptr));
    ui.preview->setToolTip(QCoreApplication::translate("AdjustBrightnessAndContrastProcess",
                                                       "Enable or disable live preview updates.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("AdjustBrightnessAndContrastProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("AdjustBrightnessAndContrastProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
