// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class DeepSNRProcessUiElements
{
public:
    QDialogButtonBox* buttonBox;
    QLabel* targetImageLabel;
    QComboBox* targetImageSelector;
    QLabel* stridePresetLabel;
    QComboBox* stridePreset;
    QSpinBox* customStrideValue;
    QCheckBox* preStretchLinearImage;
    QLabel* weightsPathLabel;
    QLineEdit* weightsPath;
    QPushButton* browseWeights;
    QPushButton* configureDeepSNR;
    QHBoxLayout* configureLayout;
    QSpacerItem* configureSpacer;
    QLabel* label;

    void setupUi(QWidget* DeepSNRProcess)
    {
        const auto& t = an::ui::tokens();

        if (DeepSNRProcess->objectName().isEmpty())
            DeepSNRProcess->setObjectName("DeepSNRProcess");
        DeepSNRProcess->resize(520, 360);
        DeepSNRProcess->setMinimumSize(QSize(520, 360));

        auto* rootLayout = new QVBoxLayout(DeepSNRProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        preStretchLinearImage = new QCheckBox(DeepSNRProcess);
        preStretchLinearImage->setObjectName("preStretchLinearImage");
        preStretchLinearImage->setChecked(false);
        rootLayout->addWidget(preStretchLinearImage);

        auto* parameterGrid = new QGridLayout();
        parameterGrid->setObjectName("parameterGrid");
        parameterGrid->setHorizontalSpacing(t.spacing.sm);
        parameterGrid->setVerticalSpacing(t.spacing.sm);
        parameterGrid->setColumnStretch(1, 1);
        parameterGrid->setRowMinimumHeight(0, fixed_dialog_ui::kFieldControlMinHeight);
        parameterGrid->setRowMinimumHeight(1, fixed_dialog_ui::kFieldControlMinHeight);

        stridePresetLabel = new QLabel(DeepSNRProcess);
        stridePresetLabel->setObjectName("stridePresetLabel");
        parameterGrid->addWidget(stridePresetLabel, 0, 0, Qt::AlignVCenter);

        stridePreset = new QComboBox(DeepSNRProcess);
        stridePreset->setObjectName("stridePreset");
        stridePreset->setMinimumWidth(150);
        stridePreset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        parameterGrid->addWidget(stridePreset, 0, 1);

        customStrideValue = new QSpinBox(DeepSNRProcess);
        customStrideValue->setObjectName("customStrideValue");
        customStrideValue->setEnabled(false);
        customStrideValue->setReadOnly(false);
        customStrideValue->setMinimum(2);
        customStrideValue->setMaximum(512);
        customStrideValue->setSingleStep(2);
        customStrideValue->setValue(480);
        customStrideValue->setMinimumWidth(80);
        parameterGrid->addWidget(customStrideValue, 0, 2);

        weightsPathLabel = new QLabel(DeepSNRProcess);
        weightsPathLabel->setObjectName("weightsPathLabel");
        parameterGrid->addWidget(weightsPathLabel, 1, 0, Qt::AlignVCenter);

        weightsPath = new QLineEdit(DeepSNRProcess);
        weightsPath->setObjectName("weightsPath");
        weightsPath->setClearButtonEnabled(true);
        weightsPath->setMinimumWidth(160);
        weightsPath->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        parameterGrid->addWidget(weightsPath, 1, 1);

        browseWeights = new QPushButton(DeepSNRProcess);
        browseWeights->setObjectName("browseWeights");
        browseWeights->setMinimumHeight(32);
        parameterGrid->addWidget(browseWeights, 1, 2);

        rootLayout->addLayout(parameterGrid);

        auto* targetLayout = new QHBoxLayout();
        targetLayout->setSpacing(t.spacing.sm);

        targetImageLabel = new QLabel(DeepSNRProcess);
        targetImageLabel->setObjectName("targetImageLabel");
        targetLayout->addWidget(targetImageLabel);

        targetImageSelector = new QComboBox(DeepSNRProcess);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetLayout->addWidget(targetImageSelector);
        targetLayout->setStretch(1, 1);

        rootLayout->addLayout(targetLayout);

        label = new QLabel(DeepSNRProcess);
        label->setObjectName("label");
        label->setFont(t.typography.heading3);
        label->setAlignment(Qt::AlignLeading | Qt::AlignLeft | Qt::AlignTop);
        label->setWordWrap(true);
        rootLayout->addWidget(label);

        rootLayout->addStretch(1);

        configureDeepSNR = new QPushButton(DeepSNRProcess);
        configureDeepSNR->setObjectName("configureDeepSNR");
        configureDeepSNR->setMinimumHeight(32);
        configureLayout = new QHBoxLayout();
        configureLayout->setObjectName("configureLayout");
        configureLayout->setContentsMargins(0, 0, 0, 0);
        configureLayout->addWidget(configureDeepSNR);
        configureSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        configureLayout->addItem(configureSpacer);
        rootLayout->addLayout(configureLayout);

        buttonBox = new QDialogButtonBox(DeepSNRProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(DeepSNRProcess);

        retranslateUi(DeepSNRProcess);

        QMetaObject::connectSlotsByName(DeepSNRProcess);
    } // setupUi

    void retranslateUi(QWidget* DeepSNRProcess)
    {
        DeepSNRProcess->setWindowTitle(QCoreApplication::translate("DeepSNRProcess", "DeepSNR", nullptr));
        preStretchLinearImage->setText(
            QCoreApplication::translate("DeepSNRProcess", "Pre-Stretch linear image", nullptr));
        stridePresetLabel->setText(QCoreApplication::translate("DeepSNRProcess", "Stride", nullptr));
        weightsPathLabel->setText(QCoreApplication::translate("DeepSNRProcess", "Weights", nullptr));
        weightsPath->setPlaceholderText(
            QCoreApplication::translate("DeepSNRProcess", "Default DeepSNR_weights_v2.onnx", nullptr));
        browseWeights->setText(QCoreApplication::translate("DeepSNRProcess", "Browse", nullptr));
        targetImageLabel->setText(QCoreApplication::translate("DeepSNRProcess", "Target Image ", nullptr));
        configureDeepSNR->setText(QCoreApplication::translate("DeepSNRProcess", "Configure", nullptr));
        label->setText(QCoreApplication::translate(
            "DeepSNRProcess",
            "DeepSNR CLI version 1.2.0 or newer is required. Configure the deepsnr executable before running.",
            nullptr));

        // Tooltips
#if QT_CONFIG(tooltip)
        preStretchLinearImage->setToolTip(QCoreApplication::translate(
            "DeepSNRProcess",
            "Apply an automatic unlinked stretch before denoising a linear image, then reverse the stretch on the "
            "DeepSNR result to preserve the original linear state.",
            nullptr));
        stridePreset->setToolTip(QCoreApplication::translate(
            "DeepSNRProcess",
            "Choose the DeepSNR tile stride. Default 480 matches the CLI default; lower values use smaller "
            "overlapping tiles.",
            nullptr));
        customStrideValue->setToolTip(QCoreApplication::translate(
            "DeepSNRProcess", "Custom even stride value from 2 to 512 pixels for DeepSNR tile processing.", nullptr));
        weightsPath->setToolTip(QCoreApplication::translate(
            "DeepSNRProcess",
            "Optional path to a DeepSNR ONNX model checkpoint. Leave empty to use the model bundled with the CLI.",
            nullptr));
        browseWeights->setToolTip(
            QCoreApplication::translate("DeepSNRProcess", "Select a custom DeepSNR ONNX weights file.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("DeepSNRProcess", "Image to denoise with DeepSNR.", nullptr));
        configureDeepSNR->setToolTip(QCoreApplication::translate(
            "DeepSNRProcess", "Open settings to configure the DeepSNR 1.2.0+ executable path.", nullptr));
        if (auto* okBtn = buttonBox->button(QDialogButtonBox::Ok))
            okBtn->setToolTip(
                QCoreApplication::translate("DeepSNRProcess", "Run DeepSNR on the target image.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("DeepSNRProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline DeepSNRProcessUiElements buildDeepSNRProcessUi(QWidget* parent)
{
    DeepSNRProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
