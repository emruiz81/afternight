// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
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

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct GammaCorrectionProcessUiElements
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
    QDoubleSpinBox* gammaValue = nullptr;
    QSlider* gammaSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline GammaCorrectionProcessUiElements buildGammaCorrectionProcessUi(QWidget* parent)
{
    GammaCorrectionProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("GammaCorrectionProcess"));
    parent->resize(864, 551);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(QCoreApplication::translate("GammaCorrectionProcess", "Gamma Correction", nullptr));

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
    chartLayout->setObjectName(QStringLiteral("horizontalLayout_2"));

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
                                                              {.translationContext = "GammaCorrectionProcess"});
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

    auto* gammaLayout = new QHBoxLayout();
    gammaLayout->setObjectName(QStringLiteral("horizontalLayout"));

    auto* gammaLabel = new QLabel(QStringLiteral("Gamma"), controlsPanel);
    gammaLabel->setObjectName(QStringLiteral("label"));
    gammaLayout->addWidget(gammaLabel);

    ui.gammaValue = new QDoubleSpinBox(controlsPanel);
    ui.gammaValue->setObjectName(QStringLiteral("gammaValue"));
    ui.gammaValue->setMinimum(0.01);
    ui.gammaValue->setMaximum(9.9);
    ui.gammaValue->setSingleStep(0.03);
    ui.gammaValue->setValue(1.0);
    ui.gammaValue->setMinimumWidth(80);
    gammaLayout->addWidget(ui.gammaValue);

    ui.gammaSlider = new QSlider(Qt::Horizontal, controlsPanel);
    ui.gammaSlider->setObjectName(QStringLiteral("gammaSlider"));
    ui.gammaSlider->setMinimumSize(QSize(120, 0));
    ui.gammaSlider->setMinimum(0);
    ui.gammaSlider->setMaximum(1000);
    ui.gammaSlider->setValue(500);
    ui.gammaSlider->setMinimumWidth(150);
    gammaLayout->addWidget(ui.gammaSlider);

    controlsLayout->addLayout(gammaLayout);
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

    ui.gammaValue->setToolTip(QCoreApplication::translate(
        "GammaCorrectionProcess",
        "Gamma exponent used to brighten or darken midtones without clipping shadows/highlights.", nullptr));
    ui.gammaSlider->setToolTip(QCoreApplication::translate(
        "GammaCorrectionProcess",
        "Gamma exponent used to brighten or darken midtones without clipping shadows/highlights.", nullptr));
    ui.targetImageSelector->setToolTip(QCoreApplication::translate(
        "GammaCorrectionProcess", "Choose the open image to apply gamma correction to.", nullptr));
    ui.channelSelector->setToolTip(QCoreApplication::translate(
        "GammaCorrectionProcess", "Select the channel to process, or RGB to process all channels together.", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("GammaCorrectionProcess", "Enable or disable live preview updates.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("GammaCorrectionProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("GammaCorrectionProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
