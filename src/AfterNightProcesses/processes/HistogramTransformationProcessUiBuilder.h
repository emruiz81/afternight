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
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

class HistogramTransformationProcessUiElements
{
public:
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QScrollArea* previewChartScrollArea = nullptr;
    QScrollArea* originalChartScrollArea = nullptr;
    QToolButton* zoomIn = nullptr;
    QToolButton* zoomOut = nullptr;
    QToolButton* zoomReset = nullptr;
    QToolButton* gridButton = nullptr;
    QToolButton* transferCurve = nullptr;
    QToolButton* localChannelNormalization = nullptr;
    QToolButton* showHistogramAreas = nullptr;
    QComboBox* plotResolution = nullptr;
    QComboBox* channelSelector2 = nullptr;
    QToolButton* autostretchLinked = nullptr;
    QToolButton* autostretchUnlinked = nullptr;
    QDoubleSpinBox* shadowsValue = nullptr;
    QLineEdit* shadowsInfo = nullptr;
    QDoubleSpinBox* highlightsValue = nullptr;
    QLineEdit* highlightsInfo = nullptr;
    QDoubleSpinBox* midtonesValue = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;

    void setupUi(QWidget* histogramTransformationProcess)
    {
        if (!histogramTransformationProcess)
        {
            return;
        }

        constexpr int kPrimaryLabelWidth = 92;
        constexpr int kSecondaryLabelWidth = 56;
        constexpr int kNumericFieldWidth = 90;
        constexpr int kInfoFieldWidth = 100;

        histogramTransformationProcess->setObjectName(QStringLiteral("HistogramTransformationProcess"));
        histogramTransformationProcess->resize(1206, 832);
        histogramTransformationProcess->setMinimumSize(QSize(460, 760));
        histogramTransformationProcess->setWindowTitle(
            QCoreApplication::translate("HistogramTransformationProcess", "Histogram Transformation", nullptr));
        const auto& t = an::ui::tokens();

        horizontalLayoutMain = new QHBoxLayout(histogramTransformationProcess);
        horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        const auto split =
            rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, histogramTransformationProcess);
        auto* controlsLayout = split.controlsLayout;
        auto* controlsPanel = split.controlsPanel;
        verticalLayout_2 = split.previewLayout;

        auto* previewChartLayout = new QHBoxLayout();
        previewChartScrollArea = new QScrollArea(controlsPanel);
        previewChartScrollArea->setObjectName(QStringLiteral("previewChartScrollArea"));
        previewChartScrollArea->setMinimumSize(QSize(350, 220));
        previewChartScrollArea->setWidgetResizable(true);
        auto* previewChartHost = new QWidget();
        previewChartHost->setObjectName(QStringLiteral("previewChartScrollContents"));
        previewChartScrollArea->setWidget(previewChartHost);
        previewChartLayout->addWidget(previewChartScrollArea);
        controlsLayout->addLayout(previewChartLayout);

        auto* originalChartLayout = new QHBoxLayout();
        originalChartScrollArea = new QScrollArea(controlsPanel);
        originalChartScrollArea->setObjectName(QStringLiteral("originalChartScrollArea"));
        originalChartScrollArea->setMinimumSize(QSize(350, 220));
        originalChartScrollArea->setWidgetResizable(true);
        auto* originalChartHost = new QWidget();
        originalChartHost->setObjectName(QStringLiteral("originalChartScrollContents"));
        originalChartScrollArea->setWidget(originalChartHost);
        originalChartLayout->addWidget(originalChartScrollArea);
        controlsLayout->addLayout(originalChartLayout);

        auto* histogramSettingsLayout = new QHBoxLayout();
        const auto toolbar =
            rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                 {.translationContext = "HistogramTransformationProcess",
                                                  .includeShowClippingInfo = false,
                                                  .includeShowHistogram = false,
                                                  .includeTransferCurve = true,
                                                  .transferCurveChecked = true});
        zoomIn = toolbar.zoomIn;
        zoomOut = toolbar.zoomOut;
        zoomReset = toolbar.zoomReset;
        gridButton = toolbar.gridButton;
        transferCurve = toolbar.transferCurve;
        localChannelNormalization = toolbar.localChannelNormalization;
        showHistogramAreas = toolbar.showHistogramAreas;
        plotResolution = toolbar.plotResolution;
        controlsLayout->addLayout(histogramSettingsLayout);

        auto* channelLayout = new QHBoxLayout();
        channelLayout->setContentsMargins(t.spacing.md, 0, t.spacing.md, 0);
        channelLayout->setSpacing(t.spacing.sm);

        auto* channelLabel = new QLabel(
            QCoreApplication::translate("HistogramTransformationProcess", "Channel", nullptr), controlsPanel);
        channelLabel->setMinimumWidth(kPrimaryLabelWidth);
        channelLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        channelLayout->addWidget(channelLabel);

        channelSelector2 = new QComboBox(controlsPanel);
        channelSelector2->setObjectName(QStringLiteral("channelSelector2"));
        channelSelector2->setMinimumWidth(120);
        channelSelector2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        channelLayout->addWidget(channelSelector2);
        channelLayout->setStretch(1, 1);

        channelLayout->addSpacing(t.spacing.xs);

        autostretchLinked = new QToolButton(controlsPanel);
        autostretchLinked->setObjectName(QStringLiteral("autostretchLinked"));
        rt_preview_ui::initToolbarButton(
            autostretchLinked,
            QCoreApplication::translate("HistogramTransformationProcess", "Auto-stretch linked channels", nullptr),
            QStringLiteral("image_filled"));
        channelLayout->addWidget(autostretchLinked);

        autostretchUnlinked = new QToolButton(controlsPanel);
        autostretchUnlinked->setObjectName(QStringLiteral("autostretchUnlinked"));
        rt_preview_ui::initToolbarButton(autostretchUnlinked,
                                         QCoreApplication::translate("HistogramTransformationProcess",
                                                                     "Auto-stretch channels independently", nullptr),
                                         QStringLiteral("image_filled_color"));
        channelLayout->addWidget(autostretchUnlinked);

        channelLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
        controlsLayout->addLayout(channelLayout);

        auto* shadowsLayout = new QHBoxLayout();
        shadowsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, 0);
        shadowsLayout->setSpacing(t.spacing.sm);
        auto* shadowsLabel = new QLabel(
            QCoreApplication::translate("HistogramTransformationProcess", "Shadows", nullptr), controlsPanel);
        shadowsLabel->setMinimumWidth(kPrimaryLabelWidth);
        shadowsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        shadowsLayout->addWidget(shadowsLabel);

        shadowsValue = new QDoubleSpinBox(controlsPanel);
        shadowsValue->setObjectName(QStringLiteral("shadowsValue"));
        shadowsValue->setDecimals(6);
        shadowsValue->setMaximum(1.0);
        shadowsValue->setSingleStep(0.01);
        shadowsValue->setMinimumWidth(kNumericFieldWidth);
        shadowsLayout->addWidget(shadowsValue);

        auto* shadowsInfoLabel = new QLabel(
            QCoreApplication::translate("HistogramTransformationProcess", "Clipped", nullptr), controlsPanel);
        shadowsInfoLabel->setMinimumWidth(kSecondaryLabelWidth);
        shadowsInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        shadowsLayout->addWidget(shadowsInfoLabel);

        shadowsInfo = new QLineEdit(controlsPanel);
        shadowsInfo->setObjectName(QStringLiteral("shadowsInfo"));
        shadowsInfo->setReadOnly(true);
        shadowsInfo->setAlignment(Qt::AlignCenter);
        shadowsInfo->setMinimumWidth(kInfoFieldWidth);
        shadowsLayout->addWidget(shadowsInfo);
        controlsLayout->addLayout(shadowsLayout);

        auto* highlightsLayout = new QHBoxLayout();
        highlightsLayout->setContentsMargins(t.spacing.md, 0, t.spacing.md, 0);
        highlightsLayout->setSpacing(t.spacing.sm);
        auto* highlightsLabel = new QLabel(
            QCoreApplication::translate("HistogramTransformationProcess", "Highlights", nullptr), controlsPanel);
        highlightsLabel->setMinimumWidth(kPrimaryLabelWidth);
        highlightsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        highlightsLayout->addWidget(highlightsLabel);

        highlightsValue = new QDoubleSpinBox(controlsPanel);
        highlightsValue->setObjectName(QStringLiteral("highlightsValue"));
        highlightsValue->setDecimals(6);
        highlightsValue->setMaximum(1.0);
        highlightsValue->setSingleStep(0.01);
        highlightsValue->setValue(1.0);
        highlightsValue->setMinimumWidth(kNumericFieldWidth);
        highlightsLayout->addWidget(highlightsValue);

        auto* highlightsInfoLabel = new QLabel(
            QCoreApplication::translate("HistogramTransformationProcess", "Clipped", nullptr), controlsPanel);
        highlightsInfoLabel->setMinimumWidth(kSecondaryLabelWidth);
        highlightsInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        highlightsLayout->addWidget(highlightsInfoLabel);

        highlightsInfo = new QLineEdit(controlsPanel);
        highlightsInfo->setObjectName(QStringLiteral("highlightsInfo"));
        highlightsInfo->setReadOnly(true);
        highlightsInfo->setAlignment(Qt::AlignCenter);
        highlightsInfo->setMinimumWidth(kInfoFieldWidth);
        highlightsLayout->addWidget(highlightsInfo);
        controlsLayout->addLayout(highlightsLayout);

        auto* midtonesLayout = new QHBoxLayout();
        midtonesLayout->setContentsMargins(t.spacing.md, 0, t.spacing.md, 0);
        midtonesLayout->setSpacing(t.spacing.sm);
        auto* midtonesLabel = new QLabel(
            QCoreApplication::translate("HistogramTransformationProcess", "Midtones", nullptr), controlsPanel);
        midtonesLabel->setMinimumWidth(kPrimaryLabelWidth);
        midtonesLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        midtonesLayout->addWidget(midtonesLabel);

        midtonesValue = new QDoubleSpinBox(controlsPanel);
        midtonesValue->setObjectName(QStringLiteral("midtonesValue"));
        midtonesValue->setDecimals(6);
        midtonesValue->setMaximum(1.0);
        midtonesValue->setSingleStep(0.01);
        midtonesValue->setValue(0.5);
        midtonesValue->setMinimumWidth(kNumericFieldWidth);
        midtonesLayout->addWidget(midtonesValue);
        midtonesLayout->addSpacing(kSecondaryLabelWidth + kInfoFieldWidth + midtonesLayout->spacing());
        midtonesLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
        controlsLayout->addLayout(midtonesLayout);

        controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

        auto* selectorLayout = new QFormLayout();
        selectorLayout->setVerticalSpacing(t.spacing.sm);
        selectorLayout->setHorizontalSpacing(t.spacing.md);
        auto* targetLabel = new QLabel(
            QCoreApplication::translate("HistogramTransformationProcess", "Target Image", nullptr), controlsPanel);
        selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

        targetImageSelector = new QComboBox(controlsPanel);
        targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        selectorLayout->setWidget(0, QFormLayout::FieldRole, targetImageSelector);
        controlsLayout->addLayout(selectorLayout);

        auto* previewLayout = new QHBoxLayout();
        preview = new QCheckBox(QCoreApplication::translate("HistogramTransformationProcess", "Preview", nullptr),
                                controlsPanel);
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

        shadowsValue->setToolTip(QCoreApplication::translate(
            "HistogramTransformationProcess", "Set the shadows clipping point for the selected channel(s).", nullptr));
        highlightsValue->setToolTip(
            QCoreApplication::translate("HistogramTransformationProcess",
                                        "Set the highlights clipping point for the selected channel(s).", nullptr));
        midtonesValue->setToolTip(QCoreApplication::translate("HistogramTransformationProcess",
                                                              "Adjust the midtones transfer value.", nullptr));
        shadowsInfo->setToolTip(QCoreApplication::translate("HistogramTransformationProcess",
                                                            "Displays clipped shadow statistics.", nullptr));
        highlightsInfo->setToolTip(QCoreApplication::translate("HistogramTransformationProcess",
                                                               "Displays clipped highlight statistics.", nullptr));
        targetImageSelector->setToolTip(QCoreApplication::translate("HistogramTransformationProcess",
                                                                    "Choose the open image to process.", nullptr));
        preview->setToolTip(QCoreApplication::translate("HistogramTransformationProcess",
                                                        "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("HistogramTransformationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("HistogramTransformationProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    }
};

inline HistogramTransformationProcessUiElements buildHistogramTransformationProcessUi(QWidget* parent)
{
    HistogramTransformationProcessUiElements ui{};
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
