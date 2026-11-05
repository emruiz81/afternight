// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSlider>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct BandingReductionProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    QDoubleSpinBox* amountValue = nullptr;
    QSlider* amountSlider = nullptr;
    QCheckBox* protectFromHighlights = nullptr;
    QDoubleSpinBox* sigmaValue = nullptr;
    QSlider* sigmaSlider = nullptr;
    QCheckBox* verticalBanding = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline BandingReductionProcessUiElements buildBandingReductionProcessUi(QWidget* parent)
{
    BandingReductionProcessUiElements ui{};

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("BandingReductionProcess"));
    parent->resize(590, 275);
    parent->setWindowTitle(QCoreApplication::translate("BandingReductionProcess", "Banding Reduction", nullptr));
    const auto& t = an::ui::tokens();

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* controlsLayout = new QVBoxLayout();
    controlsLayout->setObjectName(QStringLiteral("verticalLayout"));
    controlsLayout->setSizeConstraint(QLayout::SetFixedSize);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(t.spacing.md);

    // Unified grid: col 0 = labels, col 1 = spinboxes, col 2 = sliders/comboboxes (stretch)
    auto* grid = new QGridLayout();
    grid->setObjectName(QStringLiteral("gridLayout"));
    grid->setHorizontalSpacing(t.spacing.md);
    grid->setVerticalSpacing(t.spacing.sm);
    grid->setColumnStretch(2, 1);

    // Row 0: Amount
    auto* amountLabel = new QLabel(QCoreApplication::translate("BandingReductionProcess", "Amount", nullptr), parent);
    amountLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(amountLabel, 0, 0);

    ui.amountValue = new QDoubleSpinBox(parent);
    ui.amountValue->setObjectName(QStringLiteral("amountValue"));
    ui.amountValue->setMaximum(4.0);
    ui.amountValue->setSingleStep(0.05);
    ui.amountValue->setValue(1.0);
    ui.amountValue->setMinimumWidth(80);
    grid->addWidget(ui.amountValue, 0, 1);

    ui.amountSlider = new QSlider(Qt::Horizontal, parent);
    ui.amountSlider->setObjectName(QStringLiteral("amountSlider"));
    ui.amountSlider->setMinimumWidth(150);
    ui.amountSlider->setMaximum(4000);
    ui.amountSlider->setValue(1000);
    grid->addWidget(ui.amountSlider, 0, 2);

    // Row 1: Protect From Highlights (spans all columns)
    ui.protectFromHighlights = new QCheckBox(
        QCoreApplication::translate("BandingReductionProcess", "Protect From Highlights", nullptr), parent);
    ui.protectFromHighlights->setObjectName(QStringLiteral("protectFromHighlights"));
    grid->addWidget(ui.protectFromHighlights, 1, 0, 1, 3);

    // Row 2: Sigma
    auto* sigmaLabel = new QLabel(QCoreApplication::translate("BandingReductionProcess", "Sigma", nullptr), parent);
    sigmaLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(sigmaLabel, 2, 0);

    ui.sigmaValue = new QDoubleSpinBox(parent);
    ui.sigmaValue->setObjectName(QStringLiteral("sigmaValue"));
    ui.sigmaValue->setMaximum(5.0);
    ui.sigmaValue->setSingleStep(0.05);
    ui.sigmaValue->setValue(1.0);
    ui.sigmaValue->setMinimumWidth(80);
    grid->addWidget(ui.sigmaValue, 2, 1);

    ui.sigmaSlider = new QSlider(Qt::Horizontal, parent);
    ui.sigmaSlider->setObjectName(QStringLiteral("sigmaSlider"));
    ui.sigmaSlider->setMinimumWidth(150);
    ui.sigmaSlider->setMaximum(5000);
    ui.sigmaSlider->setValue(1000);
    grid->addWidget(ui.sigmaSlider, 2, 2);

    // Row 3: Vertical Banding (spans all columns)
    ui.verticalBanding =
        new QCheckBox(QCoreApplication::translate("BandingReductionProcess", "Vertical Banding", nullptr), parent);
    ui.verticalBanding->setObjectName(QStringLiteral("verticalBanding"));
    grid->addWidget(ui.verticalBanding, 3, 0, 1, 3);

    // Row 4: Target Image
    auto* targetLabel =
        new QLabel(QCoreApplication::translate("BandingReductionProcess", "Target Image", nullptr), parent);
    targetLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(targetLabel, 4, 0);

    ui.targetImageSelector = new QComboBox(parent);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    grid->addWidget(ui.targetImageSelector, 4, 1, 1, 2);

    // Row 5: Channel
    auto* channelLabel = new QLabel(QCoreApplication::translate("BandingReductionProcess", "Channel", nullptr), parent);
    channelLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(channelLabel, 5, 0);

    ui.channelSelector = new QComboBox(parent);
    ui.channelSelector->setObjectName(QStringLiteral("channelSelector"));
    ui.channelSelector->setMinimumWidth(150);
    ui.channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    grid->addWidget(ui.channelSelector, 5, 1, 1, 2);

    controlsLayout->addLayout(grid);
    controlsLayout->addStretch();

    // Preview
    auto* previewLayout = new QHBoxLayout();
    previewLayout->setSpacing(t.spacing.md);

    ui.preview = new QCheckBox(QCoreApplication::translate("BandingReductionProcess", "Preview", nullptr), parent);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setChecked(true);
    previewLayout->addWidget(ui.preview);
    previewLayout->addStretch();

    controlsLayout->addLayout(previewLayout);

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    buttonLayout->setSpacing(t.spacing.md);

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    buttonLayout->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonLayout);

    ui.horizontalLayoutMain->addLayout(controlsLayout);

    // Preview Host
    ui.previewLayout = new QVBoxLayout();
    ui.previewLayout->setObjectName(QStringLiteral("verticalLayout_2"));
    auto* previewHost = new QWidget(parent);
    previewHost->setObjectName(QStringLiteral("widget"));
    ui.previewLayout->addWidget(previewHost);

    ui.horizontalLayoutMain->addLayout(ui.previewLayout);
    ui.horizontalLayoutMain->setStretch(0, 0);
    ui.horizontalLayoutMain->setStretch(1, 1);

    // Tooltips
    ui.amountValue->setToolTip(
        QCoreApplication::translate("BandingReductionProcess", "Strength of the banding reduction effect.", nullptr));
    ui.amountSlider->setToolTip(
        QCoreApplication::translate("BandingReductionProcess", "Strength of the banding reduction effect.", nullptr));
    ui.protectFromHighlights->setToolTip(QCoreApplication::translate(
        "BandingReductionProcess", "Prevent the filter from affecting bright areas.", nullptr));
    ui.sigmaValue->setToolTip(QCoreApplication::translate("BandingReductionProcess",
                                                          "Standard deviation for the banding detection.", nullptr));
    ui.sigmaSlider->setToolTip(QCoreApplication::translate("BandingReductionProcess",
                                                           "Standard deviation for the banding detection.", nullptr));
    ui.verticalBanding->setToolTip(QCoreApplication::translate(
        "BandingReductionProcess", "Check this if the banding is vertical instead of horizontal.", nullptr));

    ui.targetImageSelector->setToolTip(
        QCoreApplication::translate("BandingReductionProcess", "Select the target image to process.", nullptr));
    ui.channelSelector->setToolTip(
        QCoreApplication::translate("BandingReductionProcess", "Select the specific channel to correct.", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("BandingReductionProcess", "Enable or disable the live preview.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("BandingReductionProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("BandingReductionProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
