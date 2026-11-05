// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct CosmeticCorrectionProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    QGroupBox* hotPixelsGroupBox = nullptr;
    QDoubleSpinBox* hotPixelsThresholdValue = nullptr;
    QSlider* hotPixelsThresholdSlider = nullptr;
    QGroupBox* coldPixelsGroupBox = nullptr;
    QDoubleSpinBox* coldPixelsThresholdValue = nullptr;
    QSlider* coldPixelsThresholdSlider = nullptr;
    QGroupBox* groupBox = nullptr; // General Settings
    QDoubleSpinBox* amountValue = nullptr;
    QSlider* amountSlider = nullptr;
    QComboBox* kernelSize = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline CosmeticCorrectionProcessUiElements buildCosmeticCorrectionProcessUi(QWidget* parent)
{
    CosmeticCorrectionProcessUiElements ui{};

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("CosmeticCorrectionProcess"));
    parent->resize(830, 540);
    parent->setWindowTitle(QCoreApplication::translate("CosmeticCorrectionProcess", "Cosmetic Correction", nullptr));
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

    // Hot Pixels Group
    ui.hotPixelsGroupBox =
        new QGroupBox(QCoreApplication::translate("CosmeticCorrectionProcess", "Hot Pixels", nullptr), parent);
    ui.hotPixelsGroupBox->setObjectName(QStringLiteral("hotPixelsGroupBox"));
    ui.hotPixelsGroupBox->setCheckable(true);

    auto* hotPixelsLayout = new QHBoxLayout(ui.hotPixelsGroupBox);
    hotPixelsLayout->setSpacing(t.spacing.md);
    hotPixelsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* hotPixelsLabel = new QLabel(QCoreApplication::translate("CosmeticCorrectionProcess", "Threshold", nullptr),
                                      ui.hotPixelsGroupBox);
    hotPixelsLayout->addWidget(hotPixelsLabel);

    ui.hotPixelsThresholdValue = new QDoubleSpinBox(ui.hotPixelsGroupBox);
    ui.hotPixelsThresholdValue->setObjectName(QStringLiteral("hotPixelsThresholdValue"));
    ui.hotPixelsThresholdValue->setMaximum(1.0);
    ui.hotPixelsThresholdValue->setSingleStep(0.05);
    ui.hotPixelsThresholdValue->setValue(0.5);
    ui.hotPixelsThresholdValue->setMinimumWidth(80);
    hotPixelsLayout->addWidget(ui.hotPixelsThresholdValue);

    ui.hotPixelsThresholdSlider = new QSlider(Qt::Horizontal, ui.hotPixelsGroupBox);
    ui.hotPixelsThresholdSlider->setObjectName(QStringLiteral("hotPixelsThresholdSlider"));
    ui.hotPixelsThresholdSlider->setMaximum(1000);
    ui.hotPixelsThresholdSlider->setValue(500);
    ui.hotPixelsThresholdSlider->setMinimumWidth(150);
    hotPixelsLayout->addWidget(ui.hotPixelsThresholdSlider);

    controlsLayout->addWidget(ui.hotPixelsGroupBox);

    // Cold Pixels Group
    ui.coldPixelsGroupBox =
        new QGroupBox(QCoreApplication::translate("CosmeticCorrectionProcess", "Cold Pixels", nullptr), parent);
    ui.coldPixelsGroupBox->setObjectName(QStringLiteral("coldPixelsGroupBox"));
    ui.coldPixelsGroupBox->setCheckable(true);

    auto* coldPixelsLayout = new QHBoxLayout(ui.coldPixelsGroupBox);
    coldPixelsLayout->setSpacing(t.spacing.md);
    coldPixelsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* coldPixelsLabel = new QLabel(QCoreApplication::translate("CosmeticCorrectionProcess", "Threshold", nullptr),
                                       ui.coldPixelsGroupBox);
    coldPixelsLayout->addWidget(coldPixelsLabel);

    ui.coldPixelsThresholdValue = new QDoubleSpinBox(ui.coldPixelsGroupBox);
    ui.coldPixelsThresholdValue->setObjectName(QStringLiteral("coldPixelsThresholdValue"));
    ui.coldPixelsThresholdValue->setMaximum(1.0);
    ui.coldPixelsThresholdValue->setSingleStep(0.05);
    ui.coldPixelsThresholdValue->setValue(0.5);
    ui.coldPixelsThresholdValue->setMinimumWidth(80);
    coldPixelsLayout->addWidget(ui.coldPixelsThresholdValue);

    ui.coldPixelsThresholdSlider = new QSlider(Qt::Horizontal, ui.coldPixelsGroupBox);
    ui.coldPixelsThresholdSlider->setObjectName(QStringLiteral("coldPixelsThresholdSlider"));
    ui.coldPixelsThresholdSlider->setMaximum(1000);
    ui.coldPixelsThresholdSlider->setValue(500);
    ui.coldPixelsThresholdSlider->setMinimumWidth(150);
    coldPixelsLayout->addWidget(ui.coldPixelsThresholdSlider);

    controlsLayout->addWidget(ui.coldPixelsGroupBox);

    // General Settings Group
    ui.groupBox =
        new QGroupBox(QCoreApplication::translate("CosmeticCorrectionProcess", "General Settings", nullptr), parent);
    ui.groupBox->setObjectName(QStringLiteral("groupBox"));

    auto* generalLayout = new QGridLayout(ui.groupBox);
    generalLayout->setVerticalSpacing(t.spacing.sm);
    generalLayout->setHorizontalSpacing(t.spacing.md);
    generalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* kernelLabel =
        new QLabel(QCoreApplication::translate("CosmeticCorrectionProcess", "Kernel Size", nullptr), ui.groupBox);
    generalLayout->addWidget(kernelLabel, 0, 0);

    ui.kernelSize = new QComboBox(ui.groupBox);
    ui.kernelSize->setObjectName(QStringLiteral("kernelSize"));
    ui.kernelSize->setMinimumWidth(80);
    generalLayout->addWidget(ui.kernelSize, 0, 1, 1, 2);

    auto* amountLabel =
        new QLabel(QCoreApplication::translate("CosmeticCorrectionProcess", "Amount", nullptr), ui.groupBox);
    generalLayout->addWidget(amountLabel, 1, 0);

    ui.amountValue = new QDoubleSpinBox(ui.groupBox);
    ui.amountValue->setObjectName(QStringLiteral("amountValue"));
    ui.amountValue->setMaximum(1.0);
    ui.amountValue->setSingleStep(0.05);
    ui.amountValue->setValue(1.0);
    ui.amountValue->setMinimumWidth(80);
    generalLayout->addWidget(ui.amountValue, 1, 1);

    ui.amountSlider = new QSlider(Qt::Horizontal, ui.groupBox);
    ui.amountSlider->setObjectName(QStringLiteral("amountSlider"));
    ui.amountSlider->setMaximum(1000);
    ui.amountSlider->setValue(1000);
    ui.amountSlider->setMinimumWidth(150);
    generalLayout->addWidget(ui.amountSlider, 1, 2);

    controlsLayout->addWidget(ui.groupBox);
    controlsLayout->addStretch();

    // Selectors
    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("formLayout"));
    selectorLayout->setVerticalSpacing(t.spacing.sm);
    selectorLayout->setHorizontalSpacing(t.spacing.md);

    auto* targetLabel =
        new QLabel(QCoreApplication::translate("CosmeticCorrectionProcess", "Target Image", nullptr), parent);
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(parent);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);

    auto* channelLabel =
        new QLabel(QCoreApplication::translate("CosmeticCorrectionProcess", "Channel", nullptr), parent);
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

    ui.preview = new QCheckBox(QCoreApplication::translate("CosmeticCorrectionProcess", "Preview", nullptr), parent);
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
    ui.hotPixelsGroupBox->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Enable removal of hot pixels (bright defective pixels).", nullptr));
    ui.hotPixelsThresholdValue->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Threshold for hot pixel detection (relative intensity).", nullptr));
    ui.hotPixelsThresholdSlider->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Threshold for hot pixel detection (relative intensity).", nullptr));

    ui.coldPixelsGroupBox->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Enable removal of cold pixels (dark defective pixels).", nullptr));
    ui.coldPixelsThresholdValue->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Threshold for cold pixel detection (relative intensity).", nullptr));
    ui.coldPixelsThresholdSlider->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Threshold for cold pixel detection (relative intensity).", nullptr));

    ui.kernelSize->setToolTip(
        QCoreApplication::translate("CosmeticCorrectionProcess", "Size of the kernel used for correction.", nullptr));
    ui.amountValue->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Strength of the correction (blending with original).", nullptr));
    ui.amountSlider->setToolTip(QCoreApplication::translate(
        "CosmeticCorrectionProcess", "Strength of the correction (blending with original).", nullptr));

    ui.targetImageSelector->setToolTip(
        QCoreApplication::translate("CosmeticCorrectionProcess", "Select the target image to process.", nullptr));
    ui.channelSelector->setToolTip(
        QCoreApplication::translate("CosmeticCorrectionProcess", "Select the specific channel to correct.", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("CosmeticCorrectionProcess", "Enable or disable the live preview.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("CosmeticCorrectionProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("CosmeticCorrectionProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
