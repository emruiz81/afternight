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

class ArcSinhStretchProcessUiElements
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
    QDoubleSpinBox* stretchFactor = nullptr;
    QSlider* stretchFactorSlider = nullptr;
    QDoubleSpinBox* blackPoint = nullptr;
    QSlider* blackPointSlider = nullptr;
    QCheckBox* humanWeightedLuminance = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;

    void setupUi(QWidget* arcSinhStretchProcess)
    {
        if (!arcSinhStretchProcess)
        {
            return;
        }

        arcSinhStretchProcess->setObjectName(QStringLiteral("ArcSinhStretchProcess"));
        arcSinhStretchProcess->resize(1030, 658);
        arcSinhStretchProcess->setMinimumSize(QSize(300, 200));
        arcSinhStretchProcess->setWindowTitle(
            QCoreApplication::translate("ArcSinhStretchProcess", "ArcSinh Stretch", nullptr));
        const auto& t = an::ui::tokens();

        horizontalLayoutMain = new QHBoxLayout(arcSinhStretchProcess);
        horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, arcSinhStretchProcess);
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
        const auto toolbar = rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                                  {.translationContext = "ArcSinhStretchProcess"});
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

        auto* stretchLayout = new QHBoxLayout();
        auto* stretchLabel =
            new QLabel(QCoreApplication::translate("ArcSinhStretchProcess", "Stretch Factor", nullptr), controlsPanel);
        stretchLayout->addWidget(stretchLabel);

        stretchFactor = new QDoubleSpinBox(controlsPanel);
        stretchFactor->setObjectName(QStringLiteral("stretchFactor"));
        stretchFactor->setMaximum(1000.0);
        stretchFactor->setSingleStep(1.0);
        stretchFactor->setMinimumWidth(80);
        stretchLayout->addWidget(stretchFactor);

        stretchFactorSlider = new QSlider(Qt::Horizontal, controlsPanel);
        stretchFactorSlider->setObjectName(QStringLiteral("stretchFactorSlider"));
        stretchFactorSlider->setMaximum(1000);
        stretchFactorSlider->setMinimumWidth(150);
        stretchLayout->addWidget(stretchFactorSlider);
        controlsLayout->addLayout(stretchLayout);

        auto* blackPointLayout = new QHBoxLayout();
        auto* blackPointLabel =
            new QLabel(QCoreApplication::translate("ArcSinhStretchProcess", "Black Point", nullptr), controlsPanel);
        blackPointLayout->addWidget(blackPointLabel);

        blackPoint = new QDoubleSpinBox(controlsPanel);
        blackPoint->setObjectName(QStringLiteral("blackPoint"));
        blackPoint->setDecimals(5);
        blackPoint->setMaximum(0.2);
        blackPoint->setSingleStep(0.001);
        blackPoint->setMinimumWidth(80);
        blackPointLayout->addWidget(blackPoint);

        blackPointSlider = new QSlider(Qt::Horizontal, controlsPanel);
        blackPointSlider->setObjectName(QStringLiteral("blackPointSlider"));
        blackPointSlider->setMaximum(1000);
        blackPointSlider->setMinimumWidth(150);
        blackPointLayout->addWidget(blackPointSlider);
        controlsLayout->addLayout(blackPointLayout);

        humanWeightedLuminance = new QCheckBox(
            QCoreApplication::translate("ArcSinhStretchProcess", "Human-weighted luminance", nullptr), controlsPanel);
        humanWeightedLuminance->setObjectName(QStringLiteral("humanWeightedLuminance"));
        humanWeightedLuminance->setChecked(true);
        controlsLayout->addWidget(humanWeightedLuminance);

        controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

        auto* selectorLayout = new QFormLayout();
        selectorLayout->setVerticalSpacing(t.spacing.sm);
        selectorLayout->setHorizontalSpacing(t.spacing.md);
        auto* targetLabel =
            new QLabel(QCoreApplication::translate("ArcSinhStretchProcess", "Target Image", nullptr), controlsPanel);
        selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

        targetImageSelector = new QComboBox(controlsPanel);
        targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        selectorLayout->setWidget(0, QFormLayout::FieldRole, targetImageSelector);
        controlsLayout->addLayout(selectorLayout);

        auto* previewLayout = new QHBoxLayout();
        preview =
            new QCheckBox(QCoreApplication::translate("ArcSinhStretchProcess", "Preview", nullptr), controlsPanel);
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

        stretchFactor->setToolTip(QCoreApplication::translate(
            "ArcSinhStretchProcess", "Controls the strength of the ArcSinh stretch curve.", nullptr));
        stretchFactorSlider->setToolTip(QCoreApplication::translate(
            "ArcSinhStretchProcess", "Controls the strength of the ArcSinh stretch curve.", nullptr));
        blackPoint->setToolTip(QCoreApplication::translate(
            "ArcSinhStretchProcess", "Black point clipping threshold applied before stretching.", nullptr));
        blackPointSlider->setToolTip(QCoreApplication::translate(
            "ArcSinhStretchProcess", "Black point clipping threshold applied before stretching.", nullptr));
        humanWeightedLuminance->setToolTip(QCoreApplication::translate(
            "ArcSinhStretchProcess",
            "Use human-vision luminance weighting to preserve color balance during stretching.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("ArcSinhStretchProcess", "Choose the open image to process.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("ArcSinhStretchProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("ArcSinhStretchProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("ArcSinhStretchProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    }
};

inline ArcSinhStretchProcessUiElements buildArcSinhStretchProcessUi(QWidget* parent)
{
    ArcSinhStretchProcessUiElements ui{};
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
