// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "theme/AnDesignTokens.h"
#include <QPushButton>
#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace an::ui::processes
{

struct BilateralFilterProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    QDoubleSpinBox* diameterValue = nullptr;
    QSlider* diameterSlider = nullptr;
    QDoubleSpinBox* sigmaSpaceValue = nullptr;
    QSlider* sigmaSpaceSlider = nullptr;
    QDoubleSpinBox* sigmaColorValue = nullptr;
    QSlider* sigmaColorSlider = nullptr;
    QCheckBox* linkSigmaControls = nullptr;
    QDoubleSpinBox* amountValue = nullptr;
    QSlider* amountSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline BilateralFilterProcessUiElements buildBilateralFilterProcessUi(QWidget* parent)
{
    BilateralFilterProcessUiElements ui{};

    if (!parent)
    {
        return ui;
    }

    const auto& t = an::ui::tokens();

    parent->setObjectName(QStringLiteral("BilateralFilterProcess"));
    parent->resize(775, 400);
    parent->setWindowTitle(QCoreApplication::translate("BilateralFilterProcess", "Bilateral Filter", nullptr));

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

    // Parameters Grid
    auto* gridLayout = new QGridLayout();
    gridLayout->setVerticalSpacing(t.spacing.sm);
    gridLayout->setHorizontalSpacing(t.spacing.md);

    // Diameter
    auto* diameterLabel =
        new QLabel(QCoreApplication::translate("BilateralFilterProcess", "Diameter", nullptr), parent);
    gridLayout->addWidget(diameterLabel, 0, 0);

    ui.diameterValue = new QDoubleSpinBox(parent);
    ui.diameterValue->setObjectName(QStringLiteral("diameterValue"));
    ui.diameterValue->setMaximum(20.0);
    ui.diameterValue->setSingleStep(0.5);
    ui.diameterValue->setValue(5.0);
    ui.diameterValue->setMinimumWidth(80);
    gridLayout->addWidget(ui.diameterValue, 0, 1);

    ui.diameterSlider = new QSlider(Qt::Horizontal, parent);
    ui.diameterSlider->setObjectName(QStringLiteral("diameterSlider"));
    ui.diameterSlider->setMaximum(20000);
    ui.diameterSlider->setValue(5000);
    ui.diameterSlider->setMinimumWidth(150);
    gridLayout->addWidget(ui.diameterSlider, 0, 2);

    // Sigma Space
    auto* sigmaSpaceLabel =
        new QLabel(QCoreApplication::translate("BilateralFilterProcess", "Sigma Space", nullptr), parent);
    gridLayout->addWidget(sigmaSpaceLabel, 1, 0);

    ui.sigmaSpaceValue = new QDoubleSpinBox(parent);
    ui.sigmaSpaceValue->setObjectName(QStringLiteral("sigmaSpaceValue"));
    ui.sigmaSpaceValue->setMaximum(200.0);
    ui.sigmaSpaceValue->setSingleStep(0.5);
    ui.sigmaSpaceValue->setValue(30.0);
    ui.sigmaSpaceValue->setMinimumWidth(80);
    gridLayout->addWidget(ui.sigmaSpaceValue, 1, 1);

    ui.sigmaSpaceSlider = new QSlider(Qt::Horizontal, parent);
    ui.sigmaSpaceSlider->setObjectName(QStringLiteral("sigmaSpaceSlider"));
    ui.sigmaSpaceSlider->setMaximum(20000);
    ui.sigmaSpaceSlider->setValue(3000);
    ui.sigmaSpaceSlider->setMinimumWidth(150);
    gridLayout->addWidget(ui.sigmaSpaceSlider, 1, 2);

    // Sigma Color
    auto* sigmaColorLabel =
        new QLabel(QCoreApplication::translate("BilateralFilterProcess", "Sigma Color", nullptr), parent);
    gridLayout->addWidget(sigmaColorLabel, 2, 0);

    ui.sigmaColorValue = new QDoubleSpinBox(parent);
    ui.sigmaColorValue->setObjectName(QStringLiteral("sigmaColorValue"));
    ui.sigmaColorValue->setMaximum(200.0);
    ui.sigmaColorValue->setSingleStep(0.5);
    ui.sigmaColorValue->setValue(30.0);
    ui.sigmaColorValue->setMinimumWidth(80);
    gridLayout->addWidget(ui.sigmaColorValue, 2, 1);

    ui.sigmaColorSlider = new QSlider(Qt::Horizontal, parent);
    ui.sigmaColorSlider->setObjectName(QStringLiteral("sigmaColorSlider"));
    ui.sigmaColorSlider->setMaximum(20000);
    ui.sigmaColorSlider->setValue(3000);
    ui.sigmaColorSlider->setMinimumWidth(150);
    gridLayout->addWidget(ui.sigmaColorSlider, 2, 2);

    controlsLayout->addLayout(gridLayout);

    // Link Sigma Checkbox
    ui.linkSigmaControls =
        new QCheckBox(QCoreApplication::translate("BilateralFilterProcess", "Link Sigma controls", nullptr), parent);
    ui.linkSigmaControls->setObjectName(QStringLiteral("linkSigmaControls"));
    controlsLayout->addWidget(ui.linkSigmaControls);

    // Amount Row
    auto* amountLayout = new QHBoxLayout();
    amountLayout->setSpacing(t.spacing.md);

    auto* amountLabel = new QLabel(QCoreApplication::translate("BilateralFilterProcess", "Amount", nullptr), parent);
    amountLayout->addWidget(amountLabel);

    ui.amountValue = new QDoubleSpinBox(parent);
    ui.amountValue->setObjectName(QStringLiteral("amountValue"));
    ui.amountValue->setMaximum(1.0);
    ui.amountValue->setSingleStep(0.05);
    ui.amountValue->setValue(1.0);
    ui.amountValue->setMinimumWidth(80);
    amountLayout->addWidget(ui.amountValue);

    ui.amountSlider = new QSlider(Qt::Horizontal, parent);
    ui.amountSlider->setObjectName(QStringLiteral("amountSlider"));
    ui.amountSlider->setMaximum(1000);
    ui.amountSlider->setPageStep(10);
    ui.amountSlider->setValue(1000);
    ui.amountSlider->setMinimumWidth(150);
    amountLayout->addWidget(ui.amountSlider);

    controlsLayout->addLayout(amountLayout);
    controlsLayout->addStretch();

    // Selectors
    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("formLayout"));
    selectorLayout->setVerticalSpacing(t.spacing.sm);
    selectorLayout->setHorizontalSpacing(t.spacing.md);

    auto* targetLabel =
        new QLabel(QCoreApplication::translate("BilateralFilterProcess", "Target Image", nullptr), parent);
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(parent);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);

    auto* channelLabel = new QLabel(QCoreApplication::translate("BilateralFilterProcess", "Channel", nullptr), parent);
    selectorLayout->setWidget(1, QFormLayout::LabelRole, channelLabel);

    ui.channelSelector = new QComboBox(parent);
    ui.channelSelector->setObjectName(QStringLiteral("channelSelector"));
    ui.channelSelector->setMinimumWidth(150);
    ui.channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(1, QFormLayout::FieldRole, ui.channelSelector);

    controlsLayout->addLayout(selectorLayout);

    // Preview
    auto* previewLayout = new QHBoxLayout();
    previewLayout->setSpacing(t.spacing.md);

    ui.preview = new QCheckBox(QCoreApplication::translate("BilateralFilterProcess", "Preview", nullptr), parent);
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
    ui.diameterValue->setToolTip(QCoreApplication::translate(
        "BilateralFilterProcess", "Diameter of each pixel neighborhood used during filtering.", nullptr));
    ui.diameterSlider->setToolTip(QCoreApplication::translate(
        "BilateralFilterProcess", "Diameter of each pixel neighborhood used during filtering.", nullptr));
    ui.sigmaSpaceValue->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Filter sigma in the coordinate space.", nullptr));
    ui.sigmaSpaceSlider->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Filter sigma in the coordinate space.", nullptr));
    ui.sigmaColorValue->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Filter sigma in the color space.", nullptr));
    ui.sigmaColorSlider->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Filter sigma in the color space.", nullptr));
    ui.linkSigmaControls->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Link Sigma Space and Sigma Color controls.", nullptr));
    ui.amountValue->setToolTip(QCoreApplication::translate(
        "BilateralFilterProcess", "Strength of the filter effect (blending with original).", nullptr));
    ui.amountSlider->setToolTip(QCoreApplication::translate(
        "BilateralFilterProcess", "Strength of the filter effect (blending with original).", nullptr));

    ui.targetImageSelector->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Select the target image to process.", nullptr));
    ui.channelSelector->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Select the specific channel to filter.", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("BilateralFilterProcess", "Enable or disable the live preview.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("BilateralFilterProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("BilateralFilterProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
