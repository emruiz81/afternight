// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "processes/FixedDialogUiSizing.h"
#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct ChannelCompositionProcessUiElements
{
    static constexpr int kLabelColumnWidth = 130;

    /// The window's row: the controls beside the preview, in the splitter between them.
    QHBoxLayout* horizontalLayoutMain = nullptr;

    /// The controls.
    QVBoxLayout* verticalLayout = nullptr;
    QComboBox* colorSpace = nullptr;
    QLabel* channel0Label = nullptr;
    QLabel* channel1Label = nullptr;
    QLabel* channel2Label = nullptr;
    QComboBox* redChannel = nullptr;
    QComboBox* greenChannel = nullptr;
    QComboBox* blueChannel = nullptr;
    QCheckBox* linearMatchCheckBox = nullptr;

    /// Shows the composed image in the preview's pane, once the first component is chosen.
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    /// The layout the preview view is added to.
    QVBoxLayout* previewLayout = nullptr;
};

inline ChannelCompositionProcessUiElements buildChannelCompositionProcessUi(QWidget* parent)
{
    ChannelCompositionProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("ChannelCompositionProcess"));
    parent->setWindowModality(Qt::NonModal);
    parent->resize(470, 250);
    parent->setMinimumSize(QSize(420, 240));

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    ui.verticalLayout = new QVBoxLayout();
    ui.verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
    ui.verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui.verticalLayout->setSpacing(t.spacing.md);

    auto* colorSpaceLayout = new QHBoxLayout();
    colorSpaceLayout->setObjectName(QStringLiteral("colorSpaceLayout"));
    colorSpaceLayout->setContentsMargins(0, 0, 0, 0);
    colorSpaceLayout->setSpacing(t.spacing.sm);

    auto* colorSpaceLabel = new QLabel(parent);
    colorSpaceLabel->setObjectName(QStringLiteral("label_5"));
    colorSpaceLabel->setText(QStringLiteral("Color Space"));
    colorSpaceLabel->setMinimumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 0));
    colorSpaceLabel->setMaximumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 16777215));
    colorSpaceLayout->addWidget(colorSpaceLabel);

    ui.colorSpace = new QComboBox(parent);
    ui.colorSpace->setObjectName(QStringLiteral("colorSpace"));
    ui.colorSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    colorSpaceLayout->addWidget(ui.colorSpace);
    colorSpaceLayout->setStretch(1, 1);

    ui.verticalLayout->addLayout(colorSpaceLayout);

    auto* channelsGrid = new QGridLayout();
    channelsGrid->setObjectName(QStringLiteral("gridLayout"));
    channelsGrid->setContentsMargins(0, 0, 0, 0);
    channelsGrid->setHorizontalSpacing(t.spacing.sm);
    channelsGrid->setVerticalSpacing(t.spacing.sm);
    channelsGrid->setColumnStretch(1, 1);

    ui.channel0Label = new QLabel(parent);
    ui.channel0Label->setObjectName(QStringLiteral("channel0Label"));
    ui.channel0Label->setText(QStringLiteral("Red"));
    ui.channel0Label->setMinimumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 0));
    ui.channel0Label->setMaximumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 16777215));
    channelsGrid->addWidget(ui.channel0Label, 0, 0, 1, 1);

    ui.redChannel = new QComboBox(parent);
    ui.redChannel->setObjectName(QStringLiteral("redChannel"));
    ui.redChannel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channelsGrid->addWidget(ui.redChannel, 0, 1, 1, 1);

    ui.channel1Label = new QLabel(parent);
    ui.channel1Label->setObjectName(QStringLiteral("channel1Label"));
    ui.channel1Label->setText(QStringLiteral("Green"));
    ui.channel1Label->setMinimumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 0));
    ui.channel1Label->setMaximumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 16777215));
    channelsGrid->addWidget(ui.channel1Label, 1, 0, 1, 1);

    ui.greenChannel = new QComboBox(parent);
    ui.greenChannel->setObjectName(QStringLiteral("greenChannel"));
    ui.greenChannel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channelsGrid->addWidget(ui.greenChannel, 1, 1, 1, 1);

    ui.channel2Label = new QLabel(parent);
    ui.channel2Label->setObjectName(QStringLiteral("channel2Label"));
    ui.channel2Label->setText(QStringLiteral("Blue"));
    ui.channel2Label->setMinimumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 0));
    ui.channel2Label->setMaximumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 16777215));
    channelsGrid->addWidget(ui.channel2Label, 2, 0, 1, 1);

    ui.blueChannel = new QComboBox(parent);
    ui.blueChannel->setObjectName(QStringLiteral("blueChannel"));
    ui.blueChannel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channelsGrid->addWidget(ui.blueChannel, 2, 1, 1, 1);

    ui.verticalLayout->addLayout(channelsGrid);

    auto* linearMatchRow = new QHBoxLayout();
    linearMatchRow->setContentsMargins(0, 0, 0, 0);
    linearMatchRow->setSpacing(t.spacing.sm);

    auto* linearMatchSpacer = new QWidget(parent);
    linearMatchSpacer->setMinimumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 0));
    linearMatchSpacer->setMaximumSize(QSize(ChannelCompositionProcessUiElements::kLabelColumnWidth, 16777215));
    linearMatchRow->addWidget(linearMatchSpacer);

    ui.linearMatchCheckBox = new QCheckBox(QStringLiteral("Linear Match"), parent);
    ui.linearMatchCheckBox->setObjectName(QStringLiteral("linearMatchCheckBox"));
    linearMatchRow->addWidget(ui.linearMatchCheckBox);
    linearMatchRow->addStretch(1);

    ui.verticalLayout->addLayout(linearMatchRow);
    ui.verticalLayout->addStretch(1);

    ui.preview = new QCheckBox(QStringLiteral("Preview"), parent);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setChecked(true);
    ui.verticalLayout->addWidget(ui.preview);

    ui.buttonBox = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Ok, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    ui.verticalLayout->addWidget(ui.buttonBox);

    const auto split = rt_preview_ui::buildMainProcessSplitter(ui.horizontalLayoutMain, parent);
    split.controlsLayout->addLayout(ui.verticalLayout);
    ui.previewLayout = new QVBoxLayout();
    ui.previewLayout->setObjectName(QStringLiteral("previewLayout"));
    auto* previewHolder = new QWidget(split.previewPanel);
    previewHolder->setObjectName(QStringLiteral("widget"));
    ui.previewLayout->addWidget(previewHolder);
    split.previewLayout->addLayout(ui.previewLayout);

    fixed_dialog_ui::applyStandardControlHeights(parent);

    parent->setWindowTitle(QCoreApplication::translate("ChannelCompositionProcess", "Channel Composition", nullptr));

    ui.colorSpace->setToolTip(QCoreApplication::translate(
        "ChannelCompositionProcess", "Select the color space used for channel composition.", nullptr));
    ui.redChannel->setToolTip(QCoreApplication::translate(
        "ChannelCompositionProcess", "Select source image for channel 1 in the selected color space.", nullptr));
    ui.greenChannel->setToolTip(QCoreApplication::translate(
        "ChannelCompositionProcess", "Select source image for channel 2 in the selected color space.", nullptr));
    ui.blueChannel->setToolTip(QCoreApplication::translate(
        "ChannelCompositionProcess", "Select source image for channel 3 in the selected color space.", nullptr));
    ui.linearMatchCheckBox->setToolTip(QCoreApplication::translate(
        "ChannelCompositionProcess", "Apply linear matching between channels before composition.", nullptr));
    ui.preview->setToolTip(QCoreApplication::translate(
        "ChannelCompositionProcess", "Show the composed image in the preview, as Ok will make it.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("ChannelCompositionProcess",
                                                    "Close this dialog without applying any changes.", nullptr));

    return ui;
}

} // namespace an::ui::processes
