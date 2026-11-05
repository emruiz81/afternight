// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
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

struct ColorSaturationProcessUiElements
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
    QComboBox* hueSelector = nullptr;
    QDoubleSpinBox* amountValue = nullptr;
    QSlider* amountSlider = nullptr;
    QDoubleSpinBox* bgFactorValue = nullptr;
    QSlider* bgFactorSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline ColorSaturationProcessUiElements buildColorSaturationProcessUi(QWidget* parent)
{
    ColorSaturationProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("ColorSaturationProcess"));
    parent->resize(791, 611);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(QStringLiteral("Color Saturation"));

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
    scrollAreaWidgetContents->resize(378, 218);
    ui.chartScrollArea->setWidget(scrollAreaWidgetContents);
    chartLayout->addWidget(ui.chartScrollArea);

    controlsLayout->addLayout(chartLayout);

    auto* histogramSettingsLayout = new QHBoxLayout();
    histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout_2"));
    const auto toolbar = rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                              {.translationContext = "ColorSaturationProcess"});
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
    hueLayout->setObjectName(QStringLiteral("horizontalLayout"));

    auto* hueLabel = new QLabel(QStringLiteral("Hue"), controlsPanel);
    hueLabel->setObjectName(QStringLiteral("label"));
    hueLayout->addWidget(hueLabel);

    ui.hueSelector = new QComboBox(controlsPanel);
    ui.hueSelector->setObjectName(QStringLiteral("hueSelector"));
    ui.hueSelector->setMinimumWidth(150);
    ui.hueSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    hueLayout->addWidget(ui.hueSelector);
    hueLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    controlsLayout->addLayout(hueLayout);

    auto* adjustmentLayout = new QGridLayout();
    adjustmentLayout->setObjectName(QStringLiteral("gridLayout"));

    auto* amountLabel = new QLabel(QStringLiteral("Amount"), controlsPanel);
    amountLabel->setObjectName(QStringLiteral("label_2"));
    adjustmentLayout->addWidget(amountLabel, 0, 0);

    ui.amountValue = new QDoubleSpinBox(controlsPanel);
    ui.amountValue->setObjectName(QStringLiteral("amountValue"));
    ui.amountValue->setMinimum(-1.0);
    ui.amountValue->setMaximum(1.0);
    ui.amountValue->setSingleStep(0.01);
    ui.amountValue->setMinimumWidth(80);
    adjustmentLayout->addWidget(ui.amountValue, 0, 1);

    ui.amountSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.amountSlider->setObjectName(QStringLiteral("amountSlider"));
    ui.amountSlider->setMinimum(-1000);
    ui.amountSlider->setMaximum(1000);
    ui.amountSlider->setMinimumWidth(150);
    adjustmentLayout->addWidget(ui.amountSlider, 0, 2);

    auto* bgFactorLabel = new QLabel(QStringLiteral("Background factor"), controlsPanel);
    bgFactorLabel->setObjectName(QStringLiteral("label_5"));
    adjustmentLayout->addWidget(bgFactorLabel, 1, 0);

    ui.bgFactorValue = new QDoubleSpinBox(controlsPanel);
    ui.bgFactorValue->setObjectName(QStringLiteral("bgFactorValue"));
    ui.bgFactorValue->setMaximum(2.0);
    ui.bgFactorValue->setSingleStep(0.01);
    ui.bgFactorValue->setValue(1.0);
    ui.bgFactorValue->setMinimumWidth(80);
    adjustmentLayout->addWidget(ui.bgFactorValue, 1, 1);

    ui.bgFactorSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.bgFactorSlider->setObjectName(QStringLiteral("bgFactorSlider"));
    ui.bgFactorSlider->setMaximum(2000);
    ui.bgFactorSlider->setValue(1000);
    ui.bgFactorSlider->setMinimumWidth(150);
    adjustmentLayout->addWidget(ui.bgFactorSlider, 1, 2);

    controlsLayout->addLayout(adjustmentLayout);
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

    ui.hueSelector->setToolTip(QCoreApplication::translate(
        "ColorSaturationProcess", "Select the hue interval to adjust, or Global to affect all hues.", nullptr));
    ui.amountValue->setToolTip(QCoreApplication::translate(
        "ColorSaturationProcess", "Saturation adjustment strength for the selected hue range.", nullptr));
    ui.amountSlider->setToolTip(QCoreApplication::translate(
        "ColorSaturationProcess", "Saturation adjustment strength for the selected hue range.", nullptr));
    ui.bgFactorValue->setToolTip(QCoreApplication::translate(
        "ColorSaturationProcess",
        "Background protection factor. Higher values preserve low-saturation background areas.", nullptr));
    ui.bgFactorSlider->setToolTip(QCoreApplication::translate(
        "ColorSaturationProcess",
        "Background protection factor. Higher values preserve low-saturation background areas.", nullptr));
    ui.targetImageSelector->setToolTip(
        QCoreApplication::translate("ColorSaturationProcess", "Choose the open image to process.", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("ColorSaturationProcess", "Enable or disable live preview updates.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("ColorSaturationProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("ColorSaturationProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
