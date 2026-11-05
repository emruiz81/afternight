// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
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

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

enum class ImageCompareMode : unsigned char
{
    Toggle = 0,
    HorizontalSplit,
    VerticalSplit
};

struct ImageCompareProcessUiElements
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
    QComboBox* plotResolution = nullptr;

    QToolButton* toggleModeButton = nullptr;
    QToolButton* horizontalSplitButton = nullptr;
    QToolButton* verticalSplitButton = nullptr;
    QButtonGroup* compareModeGroup = nullptr;

    QComboBox* targetImageSelector = nullptr;
    QComboBox* secondImageSelector = nullptr;

    QDialogButtonBox* buttonBox = nullptr;
};

inline ImageCompareProcessUiElements buildImageCompareProcessUi(QWidget* parent)
{
    ImageCompareProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("ImageCompareProcess"));
    parent->resize(1000, 627);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(QStringLiteral("Image Compare"));

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
    const auto toolbar = rt_preview_ui::buildHistogramToolbar(
        controlsPanel, histogramSettingsLayout,
        {.translationContext = "ImageCompareProcess", .includeShowHistogram = false});
    ui.zoomIn = toolbar.zoomIn;
    ui.zoomOut = toolbar.zoomOut;
    ui.zoomReset = toolbar.zoomReset;
    ui.gridButton = toolbar.gridButton;
    ui.showClippingInfo = toolbar.showClippingInfo;
    ui.localChannelNormalization = toolbar.localChannelNormalization;
    ui.showHistogramAreas = toolbar.showHistogramAreas;
    ui.plotResolution = toolbar.plotResolution;

    controlsLayout->addLayout(histogramSettingsLayout);

    // Compare mode selector
    auto* compareModeLayout = new QHBoxLayout();
    compareModeLayout->setObjectName(QStringLiteral("compareModeLayout"));
    compareModeLayout->setContentsMargins(0, 0, 0, 0);

    auto* compareModeLabel = new QLabel(QStringLiteral("Mode"), controlsPanel);
    compareModeLabel->setObjectName(QStringLiteral("compareModeLabel"));
    compareModeLayout->addWidget(compareModeLabel);

    ui.toggleModeButton = new QToolButton(controlsPanel);
    ui.toggleModeButton->setObjectName(QStringLiteral("toggleModeButton"));
    rt_preview_ui::initToolbarButton(
        ui.toggleModeButton,
        QCoreApplication::translate("ImageCompareProcess", "Toggle mode (click to switch between images)"),
        QStringLiteral("flip_camera"), true);
    ui.toggleModeButton->setCheckable(true);
    ui.toggleModeButton->setChecked(true);
    compareModeLayout->addWidget(ui.toggleModeButton);

    ui.horizontalSplitButton = new QToolButton(controlsPanel);
    ui.horizontalSplitButton->setObjectName(QStringLiteral("horizontalSplitButton"));
    rt_preview_ui::initToolbarButton(
        ui.horizontalSplitButton,
        QCoreApplication::translate("ImageCompareProcess", "Horizontal split (side by side with draggable divider)"),
        QStringLiteral("swap_horiz"), true);
    ui.horizontalSplitButton->setCheckable(true);
    compareModeLayout->addWidget(ui.horizontalSplitButton);

    ui.verticalSplitButton = new QToolButton(controlsPanel);
    ui.verticalSplitButton->setObjectName(QStringLiteral("verticalSplitButton"));
    rt_preview_ui::initToolbarButton(
        ui.verticalSplitButton,
        QCoreApplication::translate("ImageCompareProcess", "Vertical split (top/bottom with draggable divider)"),
        QStringLiteral("swap_vert"), true);
    ui.verticalSplitButton->setCheckable(true);
    compareModeLayout->addWidget(ui.verticalSplitButton);

    ui.compareModeGroup = new QButtonGroup(parent);
    ui.compareModeGroup->setExclusive(true);
    ui.compareModeGroup->addButton(ui.toggleModeButton, static_cast<int>(ImageCompareMode::Toggle));
    ui.compareModeGroup->addButton(ui.horizontalSplitButton, static_cast<int>(ImageCompareMode::HorizontalSplit));
    ui.compareModeGroup->addButton(ui.verticalSplitButton, static_cast<int>(ImageCompareMode::VerticalSplit));

    compareModeLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
    controlsLayout->addLayout(compareModeLayout);

    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("formLayout"));

    auto* targetLabel = new QLabel(QStringLiteral("Image 1"), controlsPanel);
    targetLabel->setObjectName(QStringLiteral("label_3"));
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(controlsPanel);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setToolTip(QStringLiteral("Select the first image to compare."));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);

    auto* secondLabel = new QLabel(QStringLiteral("Image 2"), controlsPanel);
    secondLabel->setObjectName(QStringLiteral("label_4"));
    selectorLayout->setWidget(1, QFormLayout::LabelRole, secondLabel);

    ui.secondImageSelector = new QComboBox(controlsPanel);
    ui.secondImageSelector->setObjectName(QStringLiteral("secondImageSelector"));
    ui.secondImageSelector->setToolTip(QStringLiteral("Select the second image to compare."));
    ui.secondImageSelector->setMinimumWidth(150);
    ui.secondImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(1, QFormLayout::FieldRole, ui.secondImageSelector);

    controlsLayout->addLayout(selectorLayout);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setObjectName(QStringLiteral("horizontalLayout_3"));
    buttonLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

    ui.buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, controlsPanel);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QStringLiteral("Close this dialog without applying any changes."));
    buttonLayout->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonLayout);

    return ui;
}

} // namespace an::ui::processes
