// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "theme/AnDesignTokens.h"
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QSizePolicy>
#include <QSlider>
#include <QSpacerItem>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

struct MedianFilterProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    QComboBox* kernelSize = nullptr;
    QSpinBox* iterationsValue = nullptr;
    QSlider* iterationsSlider = nullptr;
    QDoubleSpinBox* modulationValue = nullptr;
    QSlider* modulationSlider = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline MedianFilterProcessUiElements buildMedianFilterProcessUi(QWidget* parent)
{
    MedianFilterProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    const auto& t = an::ui::tokens();

    parent->setObjectName(QStringLiteral("MedianFilterProcess"));
    parent->resize(543, 273);
    parent->setWindowTitle(QCoreApplication::translate("MedianFilterProcess", "Median Filter", nullptr));

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

    auto* kernelLayout = new QHBoxLayout();
    kernelLayout->setObjectName(QStringLiteral("horizontalLayout"));
    kernelLayout->setSpacing(t.spacing.md);

    auto* kernelLabel = new QLabel(QCoreApplication::translate("MedianFilterProcess", "Kernel Size", nullptr), parent);
    kernelLabel->setObjectName(QStringLiteral("label"));
    kernelLayout->addWidget(kernelLabel);

    ui.kernelSize = new QComboBox(parent);
    ui.kernelSize->setObjectName(QStringLiteral("kernelSize"));
    ui.kernelSize->setMinimumWidth(80);
    kernelLayout->addWidget(ui.kernelSize);
    kernelLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    controlsLayout->addLayout(kernelLayout);

    auto* gridLayout = new QGridLayout();
    gridLayout->setObjectName(QStringLiteral("gridLayout"));
    gridLayout->setVerticalSpacing(t.spacing.sm);
    gridLayout->setHorizontalSpacing(t.spacing.md);

    auto* iterationsLabel =
        new QLabel(QCoreApplication::translate("MedianFilterProcess", "Iterations", nullptr), parent);
    iterationsLabel->setObjectName(QStringLiteral("iterationsLabel"));
    gridLayout->addWidget(iterationsLabel, 0, 0);

    ui.iterationsValue = new QSpinBox(parent);
    ui.iterationsValue->setObjectName(QStringLiteral("iterationsValue"));
    ui.iterationsValue->setMinimum(1);
    ui.iterationsValue->setMaximum(10);
    ui.iterationsValue->setMinimumWidth(80);
    gridLayout->addWidget(ui.iterationsValue, 0, 1);

    ui.iterationsSlider = new QSlider(Qt::Horizontal, parent);
    ui.iterationsSlider->setObjectName(QStringLiteral("iterationsSlider"));
    ui.iterationsSlider->setMinimumSize(QSize(150, 0));
    ui.iterationsSlider->setMinimum(1);
    ui.iterationsSlider->setMaximum(10);
    gridLayout->addWidget(ui.iterationsSlider, 0, 2);

    auto* modulationLabel =
        new QLabel(QCoreApplication::translate("MedianFilterProcess", "Modulation", nullptr), parent);
    modulationLabel->setObjectName(QStringLiteral("modulationLabel"));
    gridLayout->addWidget(modulationLabel, 1, 0);

    ui.modulationValue = new QDoubleSpinBox(parent);
    ui.modulationValue->setObjectName(QStringLiteral("modulationValue"));
    ui.modulationValue->setMaximum(1.0);
    ui.modulationValue->setSingleStep(0.05);
    ui.modulationValue->setValue(1.0);
    ui.modulationValue->setMinimumWidth(80);
    gridLayout->addWidget(ui.modulationValue, 1, 1);

    ui.modulationSlider = new QSlider(Qt::Horizontal, parent);
    ui.modulationSlider->setObjectName(QStringLiteral("modulationSlider"));
    ui.modulationSlider->setMinimumSize(QSize(150, 0));
    ui.modulationSlider->setMaximum(100);
    ui.modulationSlider->setValue(100);
    gridLayout->addWidget(ui.modulationSlider, 1, 2);

    controlsLayout->addLayout(gridLayout);
    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("formLayout"));
    selectorLayout->setVerticalSpacing(t.spacing.sm);
    selectorLayout->setHorizontalSpacing(t.spacing.md);

    auto* targetLabel = new QLabel(QCoreApplication::translate("MedianFilterProcess", "Target Image", nullptr), parent);
    targetLabel->setObjectName(QStringLiteral("targetLabel"));
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(parent);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);

    auto* channelLabel = new QLabel(QCoreApplication::translate("MedianFilterProcess", "Channel", nullptr), parent);
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

    ui.preview = new QCheckBox(QCoreApplication::translate("MedianFilterProcess", "Preview", nullptr), parent);
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
    ui.kernelSize->setToolTip(
        QCoreApplication::translate("MedianFilterProcess", "Size of the median filter kernel.", nullptr));
    ui.iterationsValue->setToolTip(
        QCoreApplication::translate("MedianFilterProcess", "Number of times to apply the filter.", nullptr));
    ui.iterationsSlider->setToolTip(
        QCoreApplication::translate("MedianFilterProcess", "Number of times to apply the filter.", nullptr));
    ui.modulationValue->setToolTip(
        QCoreApplication::translate("MedianFilterProcess", "Strength of the filter effect (0.0 to 1.0).", nullptr));
    ui.modulationSlider->setToolTip(
        QCoreApplication::translate("MedianFilterProcess", "Strength of the filter effect (0.0 to 1.0).", nullptr));
    ui.targetImageSelector->setToolTip(
        QCoreApplication::translate("MedianFilterProcess", "Select the target image to process.", nullptr));
    ui.channelSelector->setToolTip(QCoreApplication::translate(
        "MedianFilterProcess", "Select the specific channel to filter (or All Channels).", nullptr));
    ui.preview->setToolTip(
        QCoreApplication::translate("MedianFilterProcess", "Enable or disable the live preview.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("MedianFilterProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("MedianFilterProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
