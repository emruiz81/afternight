// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QSlider>
#include <QSpacerItem>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct NoiseGeneratorProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QSplitter* mainSplitter = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    QGridLayout* gridLayout = nullptr; // unified grid: col0=labels, col1=spinboxes, col2=sliders
    QLabel* label_7 = nullptr;
    QComboBox* noiseTypeSelector = nullptr;
    QLabel* label = nullptr;
    QDoubleSpinBox* meanValue = nullptr;
    QSlider* meanSlider = nullptr;
    QLabel* label_2 = nullptr;
    QDoubleSpinBox* varValue = nullptr;
    QSlider* varSlider = nullptr;
    QLabel* label_5 = nullptr;
    QDoubleSpinBox* amountValue = nullptr;
    QSlider* amountSlider = nullptr;
    QLabel* label_6 = nullptr;
    QDoubleSpinBox* saltVsPepperValue = nullptr;
    QSlider* saltVsPepperSlider = nullptr;
    QLabel* label_3 = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QLabel* label_4 = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    void retranslateUi(QWidget* NoiseGeneratorProcess)
    {
        NoiseGeneratorProcess->setWindowTitle(
            QCoreApplication::translate("NoiseGeneratorProcess", "Noise Generator", nullptr));
        label_7->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Noise Type", nullptr));
        label->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Mean", nullptr));
        label_2->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Var", nullptr));
        label_5->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Amount", nullptr));
        label_6->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Salt vs Pepper", nullptr));
        label_3->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Target Image", nullptr));
        label_4->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("NoiseGeneratorProcess", "Preview", nullptr));

        // Tooltips
        noiseTypeSelector->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Select the type of noise to generate.", nullptr));
        meanValue->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Mean value of the Gaussian noise.", nullptr));
        meanSlider->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Mean value of the Gaussian noise.", nullptr));
        varValue->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Variance of the Gaussian noise.", nullptr));
        varSlider->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Variance of the Gaussian noise.", nullptr));
        amountValue->setToolTip(QCoreApplication::translate("NoiseGeneratorProcess",
                                                            "Amount of noise to add (for Salt & Pepper).", nullptr));
        amountSlider->setToolTip(QCoreApplication::translate("NoiseGeneratorProcess",
                                                             "Amount of noise to add (for Salt & Pepper).", nullptr));
        saltVsPepperValue->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Ratio of Salt vs Pepper noise.", nullptr));
        saltVsPepperSlider->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Ratio of Salt vs Pepper noise.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Select the target image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "NoiseGeneratorProcess", "Select the specific channel to apply noise to.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("NoiseGeneratorProcess", "Enable or disable the live preview.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("NoiseGeneratorProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("NoiseGeneratorProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    }
};

inline NoiseGeneratorProcessUiElements buildNoiseGeneratorProcessUi(QWidget* parent)
{
    NoiseGeneratorProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setObjectName("NoiseGeneratorProcess");
    parent->resize(800, 500);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(QCoreApplication::translate("NoiseGeneratorProcess", "Noise Generator", nullptr));
    const auto& t = an::ui::tokens();

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(0);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(0, 0, 0, 0);

    const auto split = rt_preview_ui::buildMainProcessSplitter(ui.horizontalLayoutMain, parent, QMargins(16, 8, 16, 8));

    ui.mainSplitter = split.mainSplitter;
    ui.previewLayout = split.previewLayout;
    auto* controlsPanel = split.controlsPanel;
    auto* controlsLayout = split.controlsLayout;

    // --- Unified grid: col 0 = labels, col 1 = spinboxes, col 2 = sliders (stretch) ---
    ui.gridLayout = new QGridLayout();
    ui.gridLayout->setObjectName("gridLayout");
    ui.gridLayout->setHorizontalSpacing(t.spacing.md);
    ui.gridLayout->setVerticalSpacing(t.spacing.sm);
    ui.gridLayout->setColumnStretch(2, 1); // sliders expand

    // Row 0: Noise Type
    ui.label_7 = new QLabel(controlsPanel);
    ui.label_7->setObjectName("label_7");
    ui.label_7->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.gridLayout->addWidget(ui.label_7, 0, 0);

    ui.noiseTypeSelector = new QComboBox(controlsPanel);
    ui.noiseTypeSelector->setObjectName("noiseTypeSelector");
    ui.gridLayout->addWidget(ui.noiseTypeSelector, 0, 1, 1, 2); // spans spinbox + slider columns

    // Row 1: Mean
    ui.label = new QLabel(controlsPanel);
    ui.label->setObjectName("label");
    ui.label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.gridLayout->addWidget(ui.label, 1, 0);

    ui.meanValue = new QDoubleSpinBox(controlsPanel);
    ui.meanValue->setObjectName("meanValue");
    ui.meanValue->setDecimals(4);
    ui.meanValue->setMinimum(0.0);
    ui.meanValue->setMaximum(0.5);
    ui.meanValue->setSingleStep(0.005);
    ui.meanValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    ui.meanValue->setValue(0.0);
    ui.meanValue->setMinimumWidth(80);
    ui.gridLayout->addWidget(ui.meanValue, 1, 1);

    ui.meanSlider = new QSlider(controlsPanel);
    ui.meanSlider->setObjectName("meanSlider");
    ui.meanSlider->setMinimum(0);
    ui.meanSlider->setMaximum(500);
    ui.meanSlider->setValue(0);
    ui.meanSlider->setOrientation(Qt::Horizontal);
    ui.meanSlider->setMinimumWidth(150);
    ui.gridLayout->addWidget(ui.meanSlider, 1, 2);

    // Row 2: Var
    ui.label_2 = new QLabel(controlsPanel);
    ui.label_2->setObjectName("label_2");
    ui.label_2->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.gridLayout->addWidget(ui.label_2, 2, 0);

    ui.varValue = new QDoubleSpinBox(controlsPanel);
    ui.varValue->setObjectName("varValue");
    ui.varValue->setDecimals(4);
    ui.varValue->setMinimum(0.0);
    ui.varValue->setMaximum(0.5);
    ui.varValue->setSingleStep(0.001);
    ui.varValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    ui.varValue->setValue(0.002);
    ui.varValue->setMinimumWidth(80);
    ui.gridLayout->addWidget(ui.varValue, 2, 1);

    ui.varSlider = new QSlider(controlsPanel);
    ui.varSlider->setObjectName("varSlider");
    ui.varSlider->setMinimum(0);
    ui.varSlider->setMaximum(500);
    ui.varSlider->setValue(1);
    ui.varSlider->setOrientation(Qt::Horizontal);
    ui.varSlider->setMinimumWidth(150);
    ui.gridLayout->addWidget(ui.varSlider, 2, 2);

    // Row 3: Amount
    ui.label_5 = new QLabel(controlsPanel);
    ui.label_5->setObjectName("label_5");
    ui.label_5->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.gridLayout->addWidget(ui.label_5, 3, 0);

    ui.amountValue = new QDoubleSpinBox(controlsPanel);
    ui.amountValue->setObjectName("amountValue");
    ui.amountValue->setEnabled(false);
    ui.amountValue->setDecimals(4);
    ui.amountValue->setMinimum(0.0);
    ui.amountValue->setMaximum(0.5);
    ui.amountValue->setSingleStep(0.001);
    ui.amountValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    ui.amountValue->setValue(0.02);
    ui.amountValue->setMinimumWidth(80);
    ui.gridLayout->addWidget(ui.amountValue, 3, 1);

    ui.amountSlider = new QSlider(controlsPanel);
    ui.amountSlider->setObjectName("amountSlider");
    ui.amountSlider->setEnabled(false);
    ui.amountSlider->setMinimum(0);
    ui.amountSlider->setMaximum(500);
    ui.amountSlider->setValue(10);
    ui.amountSlider->setOrientation(Qt::Horizontal);
    ui.amountSlider->setMinimumWidth(150);
    ui.gridLayout->addWidget(ui.amountSlider, 3, 2);

    // Row 4: Salt vs Pepper
    ui.label_6 = new QLabel(controlsPanel);
    ui.label_6->setObjectName("label_6");
    ui.label_6->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.gridLayout->addWidget(ui.label_6, 4, 0);

    ui.saltVsPepperValue = new QDoubleSpinBox(controlsPanel);
    ui.saltVsPepperValue->setObjectName("saltVsPepperValue");
    ui.saltVsPepperValue->setEnabled(false);
    ui.saltVsPepperValue->setMinimum(0.0);
    ui.saltVsPepperValue->setMaximum(1.0);
    ui.saltVsPepperValue->setSingleStep(0.01);
    ui.saltVsPepperValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    ui.saltVsPepperValue->setValue(0.5);
    ui.saltVsPepperValue->setMinimumWidth(80);
    ui.gridLayout->addWidget(ui.saltVsPepperValue, 4, 1);

    ui.saltVsPepperSlider = new QSlider(controlsPanel);
    ui.saltVsPepperSlider->setObjectName("saltVsPepperSlider");
    ui.saltVsPepperSlider->setEnabled(false);
    ui.saltVsPepperSlider->setMinimum(0);
    ui.saltVsPepperSlider->setMaximum(1000);
    ui.saltVsPepperSlider->setValue(500);
    ui.saltVsPepperSlider->setOrientation(Qt::Horizontal);
    ui.saltVsPepperSlider->setMinimumWidth(150);
    ui.gridLayout->addWidget(ui.saltVsPepperSlider, 4, 2);

    // Row 5: Target Image
    ui.label_3 = new QLabel(controlsPanel);
    ui.label_3->setObjectName("label_3");
    ui.label_3->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.gridLayout->addWidget(ui.label_3, 5, 0);

    ui.targetImageSelector = new QComboBox(controlsPanel);
    ui.targetImageSelector->setObjectName("targetImageSelector");
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    ui.gridLayout->addWidget(ui.targetImageSelector, 5, 1, 1, 2); // spans spinbox + slider columns

    // Row 6: Channel
    ui.label_4 = new QLabel(controlsPanel);
    ui.label_4->setObjectName("label_4");
    ui.label_4->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.gridLayout->addWidget(ui.label_4, 6, 0);

    ui.channelSelector = new QComboBox(controlsPanel);
    ui.channelSelector->setObjectName("channelSelector");
    ui.channelSelector->setMinimumWidth(150);
    ui.channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    ui.gridLayout->addWidget(ui.channelSelector, 6, 1, 1, 2); // spans spinbox + slider columns

    controlsLayout->addLayout(ui.gridLayout);

    controlsLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding));

    // Preview checkbox row
    auto* previewRow = new QHBoxLayout();
    previewRow->setObjectName(QStringLiteral("horizontalLayout_4"));
    previewRow->setSpacing(t.spacing.md);

    ui.preview = new QCheckBox(controlsPanel);
    ui.preview->setObjectName("preview");
    ui.preview->setChecked(true);
    previewRow->addWidget(ui.preview);
    previewRow->addItem(new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum));

    controlsLayout->addLayout(previewRow);

    // Button box row
    auto* buttonRow = new QHBoxLayout();
    buttonRow->setObjectName(QStringLiteral("horizontalLayout_3"));
    buttonRow->setSpacing(t.spacing.md);

    ui.buttonBox = new QDialogButtonBox(controlsPanel);
    ui.buttonBox->setObjectName("buttonBox");
    ui.buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset);
    buttonRow->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonRow);

    ui.retranslateUi(parent);

    return ui;
}

} // namespace an::ui::processes
