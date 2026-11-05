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

class StatisticalStretchUiElements
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
    QComboBox* plotResolution = nullptr;
    QDoubleSpinBox* targetMedianValue = nullptr;
    QSlider* targetMedianSlider = nullptr;
    QDoubleSpinBox* blackPointSigmaValue = nullptr;
    QSlider* blackPointSigmaSlider = nullptr;
    QCheckBox* linkedStretchCheck = nullptr;
    QCheckBox* normalizeImageCheck = nullptr;
    QCheckBox* noBlackClipCheck = nullptr;
    QCheckBox* applyCurvesCheck = nullptr;
    QDoubleSpinBox* curvesBoostValue = nullptr;
    QSlider* curvesBoostSlider = nullptr;
    QLabel* clipStatsLabel = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;

    void setupUi(QWidget* statisticalStretch)
    {
        if (!statisticalStretch)
        {
            return;
        }

        statisticalStretch->setObjectName(QStringLiteral("StatisticalStretch"));
        statisticalStretch->resize(760, 584);
        statisticalStretch->setMinimumSize(QSize(300, 200));
        statisticalStretch->setWindowTitle(
            QCoreApplication::translate("StatisticalStretch", "Statistical Stretch", nullptr));
        const auto& t = an::ui::tokens();

        horizontalLayoutMain = new QHBoxLayout(statisticalStretch);
        horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, statisticalStretch);
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
        const auto toolbar = rt_preview_ui::buildHistogramToolbar(
            controlsPanel, histogramSettingsLayout,
            {.translationContext = "StatisticalStretch", .includeShowHistogram = false});
        zoomIn = toolbar.zoomIn;
        zoomOut = toolbar.zoomOut;
        zoomReset = toolbar.zoomReset;
        gridButton = toolbar.gridButton;
        showClippingInfo = toolbar.showClippingInfo;
        localChannelNormalization = toolbar.localChannelNormalization;
        showHistogramAreas = toolbar.showHistogramAreas;
        plotResolution = toolbar.plotResolution;

        controlsLayout->addLayout(histogramSettingsLayout);

        auto* targetMedianLayout = new QHBoxLayout();
        auto* targetMedianLabel =
            new QLabel(QCoreApplication::translate("StatisticalStretch", "Target Median", nullptr), controlsPanel);
        targetMedianLayout->addWidget(targetMedianLabel);

        targetMedianValue = new QDoubleSpinBox(controlsPanel);
        targetMedianValue->setObjectName(QStringLiteral("targetMedianValue"));
        targetMedianValue->setMinimum(0.01);
        targetMedianValue->setMaximum(0.99);
        targetMedianValue->setDecimals(3);
        targetMedianValue->setSingleStep(0.01);
        targetMedianValue->setValue(0.25);
        targetMedianValue->setMinimumWidth(80);
        targetMedianLayout->addWidget(targetMedianValue);

        targetMedianSlider = new QSlider(Qt::Horizontal, controlsPanel);
        targetMedianSlider->setObjectName(QStringLiteral("targetMedianSlider"));
        targetMedianSlider->setMinimum(0);
        targetMedianSlider->setMaximum(1000);
        targetMedianSlider->setValue(242);
        targetMedianSlider->setMinimumWidth(150);
        targetMedianLayout->addWidget(targetMedianSlider);
        controlsLayout->addLayout(targetMedianLayout);

        linkedStretchCheck =
            new QCheckBox(QCoreApplication::translate("StatisticalStretch", "Linked Stretch", nullptr), controlsPanel);
        linkedStretchCheck->setObjectName(QStringLiteral("linkedStretchCheck"));
        linkedStretchCheck->setChecked(false);
        controlsLayout->addWidget(linkedStretchCheck);

        auto* blackPointSigmaLayout = new QHBoxLayout();
        auto* blackPointSigmaLabel =
            new QLabel(QCoreApplication::translate("StatisticalStretch", "Black Point Sigma", nullptr), controlsPanel);
        blackPointSigmaLayout->addWidget(blackPointSigmaLabel);

        blackPointSigmaValue = new QDoubleSpinBox(controlsPanel);
        blackPointSigmaValue->setObjectName(QStringLiteral("blackPointSigmaValue"));
        blackPointSigmaValue->setMinimum(0.5);
        blackPointSigmaValue->setMaximum(10.0);
        blackPointSigmaValue->setDecimals(2);
        blackPointSigmaValue->setSingleStep(0.1);
        blackPointSigmaValue->setValue(5.0);
        blackPointSigmaValue->setMinimumWidth(80);
        blackPointSigmaLayout->addWidget(blackPointSigmaValue);

        blackPointSigmaSlider = new QSlider(Qt::Horizontal, controlsPanel);
        blackPointSigmaSlider->setObjectName(QStringLiteral("blackPointSigmaSlider"));
        blackPointSigmaSlider->setMinimum(0);
        blackPointSigmaSlider->setMaximum(1000);
        blackPointSigmaSlider->setValue(474);
        blackPointSigmaSlider->setMinimumWidth(150);
        blackPointSigmaLayout->addWidget(blackPointSigmaSlider);
        controlsLayout->addLayout(blackPointSigmaLayout);

        noBlackClipCheck = new QCheckBox(
            QCoreApplication::translate("StatisticalStretch", "No Black Clipping", nullptr), controlsPanel);
        noBlackClipCheck->setObjectName(QStringLiteral("noBlackClipCheck"));
        controlsLayout->addWidget(noBlackClipCheck);

        normalizeImageCheck =
            new QCheckBox(QCoreApplication::translate("StatisticalStretch", "Normalize Image", nullptr), controlsPanel);
        normalizeImageCheck->setObjectName(QStringLiteral("normalizeImageCheck"));
        controlsLayout->addWidget(normalizeImageCheck);

        applyCurvesCheck = new QCheckBox(
            QCoreApplication::translate("StatisticalStretch", "Apply Curves Adjustment", nullptr), controlsPanel);
        applyCurvesCheck->setObjectName(QStringLiteral("applyCurvesCheck"));
        controlsLayout->addWidget(applyCurvesCheck);

        auto* curvesBoostLayout = new QHBoxLayout();
        auto* curvesBoostLabel =
            new QLabel(QCoreApplication::translate("StatisticalStretch", "Curves Boost", nullptr), controlsPanel);
        curvesBoostLayout->addWidget(curvesBoostLabel);

        curvesBoostValue = new QDoubleSpinBox(controlsPanel);
        curvesBoostValue->setObjectName(QStringLiteral("curvesBoostValue"));
        curvesBoostValue->setMaximum(1.0);
        curvesBoostValue->setDecimals(2);
        curvesBoostValue->setSingleStep(0.05);
        curvesBoostValue->setValue(0.2);
        curvesBoostValue->setMinimumWidth(80);
        curvesBoostLayout->addWidget(curvesBoostValue);

        curvesBoostSlider = new QSlider(Qt::Horizontal, controlsPanel);
        curvesBoostSlider->setObjectName(QStringLiteral("curvesBoostSlider"));
        curvesBoostSlider->setMinimum(0);
        curvesBoostSlider->setMaximum(1000);
        curvesBoostSlider->setValue(200);
        curvesBoostSlider->setMinimumWidth(150);
        curvesBoostLayout->addWidget(curvesBoostSlider);
        controlsLayout->addLayout(curvesBoostLayout);

        clipStatsLabel = new QLabel(controlsPanel);
        clipStatsLabel->setObjectName(QStringLiteral("clipStatsLabel"));
        clipStatsLabel->setWordWrap(true);
        clipStatsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        clipStatsLabel->setMinimumHeight(38);
        controlsLayout->addWidget(clipStatsLabel);

        controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

        auto* selectorLayout = new QFormLayout();
        selectorLayout->setVerticalSpacing(t.spacing.sm);
        selectorLayout->setHorizontalSpacing(t.spacing.md);
        auto* targetLabel =
            new QLabel(QCoreApplication::translate("StatisticalStretch", "Target Image", nullptr), controlsPanel);
        selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

        targetImageSelector = new QComboBox(controlsPanel);
        targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        selectorLayout->setWidget(0, QFormLayout::FieldRole, targetImageSelector);
        controlsLayout->addLayout(selectorLayout);

        auto* previewLayout = new QHBoxLayout();
        preview = new QCheckBox(QCoreApplication::translate("StatisticalStretch", "Preview", nullptr), controlsPanel);
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

        targetMedianValue->setToolTip(
            QCoreApplication::translate("StatisticalStretch", "Target median for the stretched image.", nullptr));
        targetMedianSlider->setToolTip(
            QCoreApplication::translate("StatisticalStretch", "Target median for the stretched image.", nullptr));
        blackPointSigmaValue->setToolTip(QCoreApplication::translate(
            "StatisticalStretch",
            "Controls the black point threshold in robust noise-sigma units. Lower values clip more shadows; higher "
            "values preserve more faint background.",
            nullptr));
        blackPointSigmaSlider->setToolTip(blackPointSigmaValue->toolTip());
        noBlackClipCheck->setToolTip(QCoreApplication::translate(
            "StatisticalStretch", "Use the image minimum as the black point to preserve faint background.", nullptr));
        curvesBoostValue->setToolTip(QCoreApplication::translate(
            "StatisticalStretch", "Additional curves boost applied after statistical stretch.", nullptr));
        curvesBoostSlider->setToolTip(QCoreApplication::translate(
            "StatisticalStretch", "Additional curves boost applied after statistical stretch.", nullptr));
        linkedStretchCheck->setToolTip(
            QCoreApplication::translate("StatisticalStretch", "Apply linked stretch to color channels.", nullptr));
        normalizeImageCheck->setToolTip(
            QCoreApplication::translate("StatisticalStretch", "Normalize image intensity after stretch.", nullptr));
        applyCurvesCheck->setToolTip(QCoreApplication::translate(
            "StatisticalStretch", "Enable an additional curves adjustment stage.", nullptr));
        clipStatsLabel->setToolTip(QCoreApplication::translate(
            "StatisticalStretch", "Black point threshold and source pixels affected by the current clipping settings.",
            nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("StatisticalStretch", "Choose the open image to process.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("StatisticalStretch", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("StatisticalStretch",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("StatisticalStretch",
                                                        "Reset all parameters to their default values.", nullptr));
    }
};

inline StatisticalStretchUiElements buildStatisticalStretchUi(QWidget* parent)
{
    StatisticalStretchUiElements ui{};
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
