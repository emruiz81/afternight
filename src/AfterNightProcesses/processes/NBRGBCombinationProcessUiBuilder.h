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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
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

struct NBRGBCombinationProcessUiElements
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

    QComboBox* rgbImageSelector = nullptr;
    QComboBox* redImageSelector = nullptr;
    QComboBox* greenImageSelector = nullptr;
    QComboBox* blueImageSelector = nullptr;

    QDoubleSpinBox* rgbBandwidth = nullptr;
    QDoubleSpinBox* redBandwidth = nullptr;
    QDoubleSpinBox* greenBandwidth = nullptr;
    QDoubleSpinBox* blueBandwidth = nullptr;
    QDoubleSpinBox* redScale = nullptr;
    QDoubleSpinBox* greenScale = nullptr;
    QDoubleSpinBox* blueScale = nullptr;
    QDoubleSpinBox* rejectLow = nullptr;
    QDoubleSpinBox* rejectHigh = nullptr;

    QCheckBox* nonLinearRescale = nullptr;
    QComboBox* previewDisplayMode = nullptr;

    QDialogButtonBox* buttonBox = nullptr;
};

inline QDoubleSpinBox* makeDoubleSpinBox(QWidget* parent, const QString& objectName, double minimum, double maximum,
                                         double step, double value, int decimals = 2)
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

inline void addNarrowbandRow(QGridLayout* layout, int row, const QString& label, QComboBox*& selector,
                             QDoubleSpinBox*& bandwidth, QDoubleSpinBox*& scale, QWidget* parent, double bandwidthValue)
{
    auto* channelLabel = new QLabel(label, parent);
    channelLabel->setObjectName(label.toLower() + QStringLiteral("Label"));
    layout->addWidget(channelLabel, row, 0, 1, 1);

    selector = new QComboBox(parent);
    selector->setObjectName(label.toLower() + QStringLiteral("ImageSelector"));
    selector->setMinimumWidth(150);
    selector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(selector, row, 1, 1, 1);

    bandwidth =
        makeDoubleSpinBox(parent, label.toLower() + QStringLiteral("Bandwidth"), 0.1, 500.0, 0.5, bandwidthValue);
    layout->addWidget(bandwidth, row, 2, 1, 1);

    scale = makeDoubleSpinBox(parent, label.toLower() + QStringLiteral("Scale"), 0.0, 10.0, 0.05, 1.20);
    layout->addWidget(scale, row, 3, 1, 1);
}

inline NBRGBCombinationProcessUiElements buildNBRGBCombinationProcessUi(QWidget* parent)
{
    NBRGBCombinationProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("NBRGBCombinationProcess"));
    parent->resize(1040, 840);
    parent->setMinimumSize(QSize(320, 220));
    parent->setWindowTitle(QStringLiteral("NBRGB Combination"));

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

    ui.chartScrollArea = new QScrollArea(controlsPanel);
    ui.chartScrollArea->setObjectName(QStringLiteral("chartScrollArea"));
    ui.chartScrollArea->setMinimumSize(QSize(380, 220));
    ui.chartScrollArea->setWidgetResizable(true);
    auto* chartContents = new QWidget();
    chartContents->setObjectName(QStringLiteral("chartScrollAreaContents"));
    ui.chartScrollArea->setWidget(chartContents);
    controlsLayout->addWidget(ui.chartScrollArea);

    auto* histogramSettingsLayout = new QHBoxLayout();
    histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout"));
    const auto toolbar = rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                              {.translationContext = "NBRGBCombinationProcess"});
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

    auto* sourceGroup = new QGroupBox(QStringLiteral("Sources"), parent);
    sourceGroup->setObjectName(QStringLiteral("sourceGroup"));
    auto* sourceLayout = new QFormLayout(sourceGroup);
    sourceLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    sourceLayout->setSpacing(t.spacing.sm);

    ui.rgbImageSelector = new QComboBox(sourceGroup);
    ui.rgbImageSelector->setObjectName(QStringLiteral("rgbImageSelector"));
    ui.rgbImageSelector->setMinimumWidth(150);
    ui.rgbImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    sourceLayout->addRow(QStringLiteral("RGB"), ui.rgbImageSelector);

    ui.rgbBandwidth = makeDoubleSpinBox(sourceGroup, QStringLiteral("rgbBandwidth"), 0.1, 1000.0, 1.0, 100.0);
    sourceLayout->addRow(QStringLiteral("RGB bandwidth"), ui.rgbBandwidth);
    controlsLayout->addWidget(sourceGroup);

    auto* narrowbandGroup = new QGroupBox(QStringLiteral("Narrowband Channels"), parent);
    narrowbandGroup->setObjectName(QStringLiteral("narrowbandGroup"));
    auto* narrowbandLayout = new QGridLayout(narrowbandGroup);
    narrowbandLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    narrowbandLayout->setSpacing(t.spacing.sm);
    narrowbandLayout->addWidget(new QLabel(QStringLiteral("Channel"), narrowbandGroup), 0, 0, 1, 1);
    narrowbandLayout->addWidget(new QLabel(QStringLiteral("Image"), narrowbandGroup), 0, 1, 1, 1);
    narrowbandLayout->addWidget(new QLabel(QStringLiteral("Bandwidth"), narrowbandGroup), 0, 2, 1, 1);
    narrowbandLayout->addWidget(new QLabel(QStringLiteral("Scale"), narrowbandGroup), 0, 3, 1, 1);
    addNarrowbandRow(narrowbandLayout, 1, QStringLiteral("Red"), ui.redImageSelector, ui.redBandwidth, ui.redScale,
                     narrowbandGroup, 7.0);
    addNarrowbandRow(narrowbandLayout, 2, QStringLiteral("Green"), ui.greenImageSelector, ui.greenBandwidth,
                     ui.greenScale, narrowbandGroup, 8.5);
    addNarrowbandRow(narrowbandLayout, 3, QStringLiteral("Blue"), ui.blueImageSelector, ui.blueBandwidth, ui.blueScale,
                     narrowbandGroup, 8.5);
    narrowbandLayout->setColumnStretch(1, 1);
    controlsLayout->addWidget(narrowbandGroup);

    auto* optionsGroup = new QGroupBox(QStringLiteral("Options"), parent);
    optionsGroup->setObjectName(QStringLiteral("optionsGroup"));
    auto* optionsLayout = new QFormLayout(optionsGroup);
    optionsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    optionsLayout->setSpacing(t.spacing.sm);

    ui.rejectLow = makeDoubleSpinBox(optionsGroup, QStringLiteral("rejectLow"), 0.0, 1.0, 0.01, 0.0, 3);
    ui.rejectHigh = makeDoubleSpinBox(optionsGroup, QStringLiteral("rejectHigh"), 0.0, 1.0, 0.01, 0.92, 3);
    optionsLayout->addRow(QStringLiteral("Reject low"), ui.rejectLow);
    optionsLayout->addRow(QStringLiteral("Reject high"), ui.rejectHigh);

    ui.nonLinearRescale = new QCheckBox(QStringLiteral("Non Linear rescale"), optionsGroup);
    ui.nonLinearRescale->setObjectName(QStringLiteral("nonLinearRescale"));
    optionsLayout->addRow(ui.nonLinearRescale);

    ui.previewDisplayMode = new QComboBox(optionsGroup);
    ui.previewDisplayMode->setObjectName(QStringLiteral("previewDisplayMode"));
    ui.previewDisplayMode->addItems({QStringLiteral("NBRGB"), QStringLiteral("RGB"), QStringLiteral("NBr"),
                                     QStringLiteral("NBg"), QStringLiteral("NBb")});
    optionsLayout->addRow(QStringLiteral("Preview"), ui.previewDisplayMode);
    controlsLayout->addWidget(optionsGroup);

    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    ui.buttonBox = new QDialogButtonBox(parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    ui.buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset);
    controlsLayout->addWidget(ui.buttonBox);

    return ui;
}
} // namespace an::ui::processes
