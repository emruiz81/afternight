// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "theme/AnDesignTokens.h"
#include <QPushButton>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QSlider>
#include <QSpacerItem>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

struct GaussianBlurProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    QDoubleSpinBox* sigmaValue = nullptr;
    QSlider* sigmaSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline GaussianBlurProcessUiElements buildGaussianBlurProcessUi(QWidget* parent)
{
    GaussianBlurProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    const auto& t = an::ui::tokens();

    parent->setObjectName(QStringLiteral("GaussianBlurProcess"));
    parent->resize(550, 200);
    parent->setWindowTitle(QCoreApplication::translate("GaussianBlurProcess", "Gaussian Blur", nullptr));

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

    auto* sigmaLayout = new QHBoxLayout();
    sigmaLayout->setObjectName(QStringLiteral("horizontalLayout"));
    sigmaLayout->setSpacing(t.spacing.md);

    auto* sigmaLabel = new QLabel(QCoreApplication::translate("GaussianBlurProcess", "Sigma", nullptr), parent);
    sigmaLabel->setObjectName(QStringLiteral("label"));
    sigmaLayout->addWidget(sigmaLabel);

    ui.sigmaValue = new QDoubleSpinBox(parent);
    ui.sigmaValue->setObjectName(QStringLiteral("sigmaValue"));
    ui.sigmaValue->setMinimum(0.0);
    ui.sigmaValue->setMaximum(100.0);
    ui.sigmaValue->setSingleStep(0.5);
    ui.sigmaValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    ui.sigmaValue->setValue(0.0);
    ui.sigmaValue->setMinimumWidth(80);
    sigmaLayout->addWidget(ui.sigmaValue);

    ui.sigmaSlider = new QSlider(Qt::Horizontal, parent);
    ui.sigmaSlider->setObjectName(QStringLiteral("sigmaSlider"));
    ui.sigmaSlider->setMinimum(0);
    ui.sigmaSlider->setMaximum(1000);
    ui.sigmaSlider->setValue(0);
    ui.sigmaSlider->setMinimumWidth(150);
    sigmaLayout->addWidget(ui.sigmaSlider);

    controlsLayout->addLayout(sigmaLayout);
    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("formLayout"));
    selectorLayout->setVerticalSpacing(t.spacing.sm);
    selectorLayout->setHorizontalSpacing(t.spacing.md);

    auto* targetLabel = new QLabel(QCoreApplication::translate("GaussianBlurProcess", "Target Image", nullptr), parent);
    targetLabel->setObjectName(QStringLiteral("targetLabel"));
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(parent);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);

    auto* channelLabel = new QLabel(QCoreApplication::translate("GaussianBlurProcess", "Channel", nullptr), parent);
    channelLabel->setObjectName(QStringLiteral("channelLabel"));
    selectorLayout->setWidget(1, QFormLayout::LabelRole, channelLabel);

    ui.channelSelector = new QComboBox(parent);
    ui.channelSelector->setObjectName(QStringLiteral("channelSelector"));
    ui.channelSelector->setMinimumWidth(150);
    ui.channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(1, QFormLayout::FieldRole, ui.channelSelector);

    controlsLayout->addLayout(selectorLayout);

    auto* previewLayout = new QHBoxLayout();
    previewLayout->setObjectName(QStringLiteral("horizontalLayout_4"));
    previewLayout->setSpacing(t.spacing.md);

    ui.preview = new QCheckBox(QCoreApplication::translate("GaussianBlurProcess", "Preview", nullptr), parent);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setChecked(true);
    previewLayout->addWidget(ui.preview);
    previewLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    controlsLayout->addLayout(previewLayout);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setObjectName(QStringLiteral("horizontalLayout_3"));
    buttonLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    buttonLayout->setSpacing(t.spacing.md);

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    buttonLayout->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonLayout);

    ui.horizontalLayoutMain->addLayout(controlsLayout);

    ui.previewLayout = new QVBoxLayout();
    ui.previewLayout->setObjectName(QStringLiteral("verticalLayout_2"));
    auto* previewHost = new QWidget(parent);
    previewHost->setObjectName(QStringLiteral("widget"));
    ui.previewLayout->addWidget(previewHost);

    ui.horizontalLayoutMain->addLayout(ui.previewLayout);
    ui.horizontalLayoutMain->setStretch(0, 0);
    ui.horizontalLayoutMain->setStretch(1, 1);

    // Tooltips
    ui.sigmaValue->setToolTip(QCoreApplication::translate(
        "GaussianBlurProcess", "Standard deviation of the Gaussian distribution (blur radius).", nullptr));
    ui.sigmaSlider->setToolTip(QCoreApplication::translate(
        "GaussianBlurProcess", "Standard deviation of the Gaussian distribution (blur radius).", nullptr));
    ui.targetImageSelector->setToolTip(
        QCoreApplication::translate("GaussianBlurProcess", "Select the target image to process.", nullptr));
    ui.channelSelector->setToolTip(QCoreApplication::translate(
        "GaussianBlurProcess", "Select the specific channel to blur (or All Channels).", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("GaussianBlurProcess", "Enable or disable the live preview.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("GaussianBlurProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("GaussianBlurProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
