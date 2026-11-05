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

class AdjustLChProcessUiElements
{
public:
    QHBoxLayout* horizontalLayoutMain = nullptr;
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
    QSpinBox* chromaValue = nullptr;
    QSlider* chromaSlider = nullptr;
    QSpinBox* lightnessValue = nullptr;
    QSlider* lightnessSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;

    void setupUi(QWidget* adjustLChProcess)
    {
        if (!adjustLChProcess)
        {
            return;
        }

        adjustLChProcess->setObjectName(QStringLiteral("AdjustLChProcess"));
        adjustLChProcess->resize(720, 540);
        adjustLChProcess->setMinimumSize(QSize(300, 200));
        adjustLChProcess->setWindowTitle(QCoreApplication::translate("AdjustLChProcess", "Adjust LCh", nullptr));
        const auto& t = an::ui::tokens();

        horizontalLayoutMain = new QHBoxLayout(adjustLChProcess);
        horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, adjustLChProcess);
        auto* controlsLayout = split.controlsLayout;
        auto* controlsPanel = split.controlsPanel;
        verticalLayout_2 = split.previewLayout;

        auto* chartLayout = new QHBoxLayout();
        chartScrollArea = new QScrollArea(controlsPanel);
        chartScrollArea->setObjectName(QStringLiteral("chartScrollArea"));
        chartScrollArea->setMinimumSize(QSize(380, 220));
        chartScrollArea->setWidgetResizable(true);
        auto* chartHost = new QWidget();
        chartHost->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
        chartScrollArea->setWidget(chartHost);
        chartLayout->addWidget(chartScrollArea);
        controlsLayout->addLayout(chartLayout);

        auto* histogramSettingsLayout = new QHBoxLayout();
        histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout"));
        const auto toolbar = rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                                  {.translationContext = "AdjustLChProcess"});
        zoomIn = toolbar.zoomIn;
        zoomOut = toolbar.zoomOut;
        zoomReset = toolbar.zoomReset;
        gridButton = toolbar.gridButton;
        showClippingInfo = toolbar.showClippingInfo;
        localChannelNormalization = toolbar.localChannelNormalization;
        showHistogramAreas = toolbar.showHistogramAreas;
        showHistogram = toolbar.showHistogram;
        plotResolution = toolbar.plotResolution;
        controlsLayout->addLayout(histogramSettingsLayout);

        auto* hueLayout = new QHBoxLayout();
        auto* hueLabel = new QLabel(QCoreApplication::translate("AdjustLChProcess", "Hue", nullptr), controlsPanel);
        hueLabel->setObjectName(QStringLiteral("hueLabel"));
        hueLayout->addWidget(hueLabel);

        hueValue = new QSpinBox(controlsPanel);
        hueValue->setObjectName(QStringLiteral("hueValue"));
        hueValue->setMinimum(-180);
        hueValue->setMaximum(180);
        hueValue->setMinimumWidth(80);
        hueLayout->addWidget(hueValue);

        hueSlider = new QSlider(Qt::Horizontal, controlsPanel);
        hueSlider->setObjectName(QStringLiteral("hueSlider"));
        hueSlider->setMinimum(-180);
        hueSlider->setMaximum(180);
        hueSlider->setMinimumWidth(150);
        hueLayout->addWidget(hueSlider);
        controlsLayout->addLayout(hueLayout);

        auto* chromaLayout = new QHBoxLayout();
        auto* chromaLabel =
            new QLabel(QCoreApplication::translate("AdjustLChProcess", "Chroma", nullptr), controlsPanel);
        chromaLabel->setObjectName(QStringLiteral("chromaLabel"));
        chromaLayout->addWidget(chromaLabel);

        chromaValue = new QSpinBox(controlsPanel);
        chromaValue->setObjectName(QStringLiteral("chromaValue"));
        chromaValue->setMinimum(-100);
        chromaValue->setMaximum(100);
        chromaValue->setMinimumWidth(80);
        chromaLayout->addWidget(chromaValue);

        chromaSlider = new QSlider(Qt::Horizontal, controlsPanel);
        chromaSlider->setObjectName(QStringLiteral("chromaSlider"));
        chromaSlider->setMinimum(-100);
        chromaSlider->setMaximum(100);
        chromaSlider->setMinimumWidth(150);
        chromaLayout->addWidget(chromaSlider);
        controlsLayout->addLayout(chromaLayout);

        auto* lightnessLayout = new QHBoxLayout();
        auto* lightnessLabel =
            new QLabel(QCoreApplication::translate("AdjustLChProcess", "Lightness", nullptr), controlsPanel);
        lightnessLabel->setObjectName(QStringLiteral("lightnessLabel"));
        lightnessLayout->addWidget(lightnessLabel);

        lightnessValue = new QSpinBox(controlsPanel);
        lightnessValue->setObjectName(QStringLiteral("lightnessValue"));
        lightnessValue->setMinimum(-100);
        lightnessValue->setMaximum(100);
        lightnessValue->setMinimumWidth(80);
        lightnessLayout->addWidget(lightnessValue);

        lightnessSlider = new QSlider(Qt::Horizontal, controlsPanel);
        lightnessSlider->setObjectName(QStringLiteral("lightnessSlider"));
        lightnessSlider->setMinimum(-100);
        lightnessSlider->setMaximum(100);
        lightnessSlider->setMinimumWidth(150);
        lightnessLayout->addWidget(lightnessSlider);
        controlsLayout->addLayout(lightnessLayout);

        controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

        auto* selectorLayout = new QFormLayout();
        selectorLayout->setVerticalSpacing(t.spacing.sm);
        selectorLayout->setHorizontalSpacing(t.spacing.md);
        auto* targetLabel =
            new QLabel(QCoreApplication::translate("AdjustLChProcess", "Target Image", nullptr), controlsPanel);
        targetLabel->setObjectName(QStringLiteral("targetLabel"));
        selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

        targetImageSelector = new QComboBox(controlsPanel);
        targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        selectorLayout->setWidget(0, QFormLayout::FieldRole, targetImageSelector);
        controlsLayout->addLayout(selectorLayout);

        auto* previewLayout = new QHBoxLayout();
        preview = new QCheckBox(QCoreApplication::translate("AdjustLChProcess", "Preview", nullptr), controlsPanel);
        preview->setObjectName(QStringLiteral("preview"));
        preview->setChecked(true);
        previewLayout->addWidget(preview);
        previewLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
        controlsLayout->addLayout(previewLayout);

        auto* buttonLayout = new QHBoxLayout();
        buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                         controlsPanel);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        buttonLayout->addWidget(buttonBox);
        controlsLayout->addLayout(buttonLayout);

        hueValue->setToolTip(
            QCoreApplication::translate("AdjustLChProcess", "Adjust hue rotation in CIE LCh space.", nullptr));
        hueSlider->setToolTip(
            QCoreApplication::translate("AdjustLChProcess", "Adjust hue rotation in CIE LCh space.", nullptr));
        chromaValue->setToolTip(QCoreApplication::translate(
            "AdjustLChProcess", "Adjust chroma (color intensity) in CIE LCh space.", nullptr));
        chromaSlider->setToolTip(QCoreApplication::translate(
            "AdjustLChProcess", "Adjust chroma (color intensity) in CIE LCh space.", nullptr));
        lightnessValue->setToolTip(
            QCoreApplication::translate("AdjustLChProcess", "Adjust perceptual lightness in CIE LCh space.", nullptr));
        lightnessSlider->setToolTip(
            QCoreApplication::translate("AdjustLChProcess", "Adjust perceptual lightness in CIE LCh space.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("AdjustLChProcess", "Choose the open image to process.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("AdjustLChProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("AdjustLChProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("AdjustLChProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    }
};

inline AdjustLChProcessUiElements buildAdjustLChProcessUi(QWidget* parent)
{
    AdjustLChProcessUiElements ui{};
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
