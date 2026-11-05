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
#include <QSpacerItem>
#include <QSpinBox>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct AdjustHSLProcessUiElements
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
    QSpinBox* hueValue = nullptr;
    QSlider* hueSlider = nullptr;
    QSpinBox* lightnessValue = nullptr;
    QSlider* lightnessSlider = nullptr;
    QSpinBox* saturationValue = nullptr;
    QSlider* saturationSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline AdjustHSLProcessUiElements buildAdjustHSLProcessUi(QWidget* parent)
{
    AdjustHSLProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("AdjustHSLProcess"));
    parent->resize(687, 527);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(QCoreApplication::translate("AdjustHSLProcess", "Adjust HSL", nullptr));

    const auto& t = an::ui::tokens();

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    const auto split = rt_preview_ui::buildMainProcessSplitter(ui.horizontalLayoutMain, parent);
    auto* controlsLayout = split.controlsLayout;
    auto* controlsPanel = split.controlsPanel;
    ui.previewLayout = split.previewLayout;

    auto* chartLayout = new QHBoxLayout();
    chartLayout->setObjectName(QStringLiteral("horizontalLayout"));

    ui.chartScrollArea = new QScrollArea(controlsPanel);
    ui.chartScrollArea->setObjectName(QStringLiteral("chartScrollArea"));
    ui.chartScrollArea->setMinimumSize(QSize(380, 220));
    ui.chartScrollArea->setWidgetResizable(true);

    auto* scrollAreaWidgetContents = new QWidget();
    scrollAreaWidgetContents->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
    scrollAreaWidgetContents->resize(378, 218);
    ui.chartScrollArea->setWidget(scrollAreaWidgetContents);
    chartLayout->addWidget(ui.chartScrollArea);

    controlsLayout->addLayout(chartLayout);

    auto* histogramSettingsLayout = new QHBoxLayout();
    histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout_2"));
    const auto toolbar = rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                              {.translationContext = "AdjustHSLProcess"});
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

    auto* hueLayout = new QHBoxLayout();
    hueLayout->setObjectName(QStringLiteral("horizontalLayout_2"));

    auto* hueLabel = new QLabel(QStringLiteral("Hue"), controlsPanel);
    hueLabel->setObjectName(QStringLiteral("label_2"));
    hueLayout->addWidget(hueLabel);

    ui.hueValue = new QSpinBox(controlsPanel);
    ui.hueValue->setObjectName(QStringLiteral("hueValue"));
    ui.hueValue->setMinimum(-180);
    ui.hueValue->setMaximum(180);
    ui.hueValue->setMinimumWidth(80);
    hueLayout->addWidget(ui.hueValue);

    ui.hueSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.hueSlider->setObjectName(QStringLiteral("hueSlider"));
    ui.hueSlider->setMinimum(-180);
    ui.hueSlider->setMaximum(180);
    ui.hueSlider->setMinimumWidth(150);
    hueLayout->addWidget(ui.hueSlider);

    controlsLayout->addLayout(hueLayout);

    auto* lightnessLayout = new QHBoxLayout();
    lightnessLayout->setObjectName(QStringLiteral("horizontalLayout_5"));

    auto* lightnessLabel = new QLabel(QStringLiteral("Lightness"), controlsPanel);
    lightnessLabel->setObjectName(QStringLiteral("label_5"));
    lightnessLayout->addWidget(lightnessLabel);

    ui.lightnessValue = new QSpinBox(controlsPanel);
    ui.lightnessValue->setObjectName(QStringLiteral("lightnessValue"));
    ui.lightnessValue->setMinimum(-100);
    ui.lightnessValue->setMaximum(100);
    ui.lightnessValue->setMinimumWidth(80);
    lightnessLayout->addWidget(ui.lightnessValue);

    ui.lightnessSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.lightnessSlider->setObjectName(QStringLiteral("lightnessSlider"));
    ui.lightnessSlider->setMinimum(-100);
    ui.lightnessSlider->setMaximum(100);
    ui.lightnessSlider->setMinimumWidth(150);
    lightnessLayout->addWidget(ui.lightnessSlider);

    controlsLayout->addLayout(lightnessLayout);

    auto* saturationLayout = new QHBoxLayout();
    saturationLayout->setObjectName(QStringLiteral("horizontalLayout_6"));

    auto* saturationLabel = new QLabel(QStringLiteral("Saturation"), controlsPanel);
    saturationLabel->setObjectName(QStringLiteral("label_6"));
    saturationLayout->addWidget(saturationLabel);

    ui.saturationValue = new QSpinBox(controlsPanel);
    ui.saturationValue->setObjectName(QStringLiteral("saturationValue"));
    ui.saturationValue->setMinimum(-100);
    ui.saturationValue->setMaximum(100);
    ui.saturationValue->setMinimumWidth(80);
    saturationLayout->addWidget(ui.saturationValue);

    ui.saturationSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.saturationSlider->setObjectName(QStringLiteral("saturationSlider"));
    ui.saturationSlider->setMinimum(-100);
    ui.saturationSlider->setMaximum(100);
    ui.saturationSlider->setMinimumWidth(150);
    saturationLayout->addWidget(ui.saturationSlider);

    controlsLayout->addLayout(saturationLayout);
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

    ui.hueValue->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Rotate hue around the color wheel in degrees.", nullptr));
    ui.hueSlider->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Rotate hue around the color wheel in degrees.", nullptr));
    ui.lightnessValue->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Adjust perceived lightness in HSL space.", nullptr));
    ui.lightnessSlider->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Adjust perceived lightness in HSL space.", nullptr));
    ui.saturationValue->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Adjust color saturation in HSL space.", nullptr));
    ui.saturationSlider->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Adjust color saturation in HSL space.", nullptr));
    ui.targetImageSelector->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Choose the open image to process.", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("AdjustHSLProcess", "Enable or disable live preview updates.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("AdjustHSLProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(
            QCoreApplication::translate("AdjustHSLProcess", "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
