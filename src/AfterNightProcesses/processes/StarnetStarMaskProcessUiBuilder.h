// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class StarnetStarMaskProcessUiElements
{
public:
    QDialogButtonBox* buttonBox;
    QGroupBox* groupBox;
    QPushButton* configureStarnet;
    QLabel* stridePresetLabel;
    QComboBox* stridePreset;
    QSpinBox* customStrideValue;
    QCheckBox* highlightProtection;
    QLabel* weightsPathLabel;
    QLineEdit* weightsPath;
    QPushButton* browseWeights;
    QCheckBox* upsamplex2;
    QCheckBox* preStretchLinearImage;
    QLabel* label;
    QGroupBox* groupBox_2;
    QCheckBox* lightness;
    QCheckBox* binaryMask;
    QGridLayout* sliderGrid;
    QLabel* label_3;
    QDoubleSpinBox* lowerLimitValue;
    QSlider* lowerLimitSlider;
    QLabel* label_4;
    QDoubleSpinBox* upperLimitValue;
    QSlider* upperLimitSlider;
    QLabel* label_5;
    QDoubleSpinBox* smoothnessValue;
    QSlider* smoothnessSlider;
    QWidget* layoutWidget;
    QHBoxLayout* horizontalLayout;
    QLabel* label_6;
    QComboBox* targetImageSelector;

    void setupUi(QWidget* StarnetStarMaskProcess)
    {
        const auto& t = an::ui::tokens();

        if (StarnetStarMaskProcess->objectName().isEmpty())
            StarnetStarMaskProcess->setObjectName("StarnetStarMaskProcess");
        StarnetStarMaskProcess->resize(520, 620);
        StarnetStarMaskProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        StarnetStarMaskProcess->setMinimumSize(QSize(520, 620));

        auto* rootLayout = new QVBoxLayout(StarnetStarMaskProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

        buttonBox = new QDialogButtonBox(StarnetStarMaskProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);
        groupBox = new QGroupBox(StarnetStarMaskProcess);
        groupBox->setObjectName("groupBox");
        auto* groupBoxLayout = new QVBoxLayout(groupBox);
        groupBoxLayout->setObjectName("groupBoxLayout");
        groupBoxLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        groupBoxLayout->setSpacing(t.spacing.sm);

        preStretchLinearImage = new QCheckBox(groupBox);
        preStretchLinearImage->setObjectName("preStretchLinearImage");
        preStretchLinearImage->setEnabled(true);
        groupBoxLayout->addWidget(preStretchLinearImage);

        highlightProtection = new QCheckBox(groupBox);
        highlightProtection->setObjectName("highlightProtection");
        highlightProtection->setChecked(true);
        groupBoxLayout->addWidget(highlightProtection);

        auto* parameterGrid = new QGridLayout();
        parameterGrid->setObjectName("parameterGrid");
        parameterGrid->setHorizontalSpacing(t.spacing.sm);
        parameterGrid->setVerticalSpacing(t.spacing.sm);
        parameterGrid->setColumnStretch(1, 1);
        parameterGrid->setRowMinimumHeight(0, fixed_dialog_ui::kFieldControlMinHeight);
        parameterGrid->setRowMinimumHeight(1, fixed_dialog_ui::kFieldControlMinHeight);

        stridePresetLabel = new QLabel(groupBox);
        stridePresetLabel->setObjectName("stridePresetLabel");
        parameterGrid->addWidget(stridePresetLabel, 0, 0, Qt::AlignVCenter);

        stridePreset = new QComboBox(groupBox);
        stridePreset->setObjectName("stridePreset");
        stridePreset->setMinimumWidth(150);
        stridePreset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        parameterGrid->addWidget(stridePreset, 0, 1);

        customStrideValue = new QSpinBox(groupBox);
        customStrideValue->setObjectName("customStrideValue");
        customStrideValue->setEnabled(false);
        customStrideValue->setReadOnly(false);
        customStrideValue->setMinimum(2);
        customStrideValue->setMaximum(512);
        customStrideValue->setSingleStep(2);
        customStrideValue->setValue(256);
        customStrideValue->setMinimumWidth(80);
        parameterGrid->addWidget(customStrideValue, 0, 2);

        weightsPathLabel = new QLabel(groupBox);
        weightsPathLabel->setObjectName("weightsPathLabel");
        parameterGrid->addWidget(weightsPathLabel, 1, 0, Qt::AlignVCenter);

        weightsPath = new QLineEdit(groupBox);
        weightsPath->setObjectName("weightsPath");
        weightsPath->setClearButtonEnabled(true);
        weightsPath->setMinimumWidth(160);
        weightsPath->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        parameterGrid->addWidget(weightsPath, 1, 1);

        browseWeights = new QPushButton(groupBox);
        browseWeights->setObjectName("browseWeights");
        browseWeights->setMinimumHeight(30);
        parameterGrid->addWidget(browseWeights, 1, 2);
        groupBoxLayout->addLayout(parameterGrid);

        upsamplex2 = new QCheckBox(groupBox);
        upsamplex2->setObjectName("upsamplex2");
        upsamplex2->setEnabled(true);
        groupBoxLayout->addWidget(upsamplex2);

        label = new QLabel(groupBox);
        label->setObjectName("label");
        label->setFont(t.typography.heading3);
        label->setAlignment(Qt::AlignLeading | Qt::AlignLeft | Qt::AlignTop);
        label->setWordWrap(true);
        groupBoxLayout->addWidget(label);

        auto* configureLayout = new QHBoxLayout();
        configureLayout->setObjectName("configureLayout");
        configureLayout->setSpacing(t.spacing.sm);
        configureStarnet = new QPushButton(groupBox);
        configureStarnet->setObjectName("configureStarnet");
        configureStarnet->setMinimumHeight(30);
        configureLayout->addWidget(configureStarnet);
        configureLayout->addStretch();
        groupBoxLayout->addLayout(configureLayout);

        groupBox_2 = new QGroupBox(StarnetStarMaskProcess);
        groupBox_2->setObjectName("groupBox_2");

        auto* groupBox2Layout = new QVBoxLayout(groupBox_2);
        groupBox2Layout->setContentsMargins(t.spacing.md, t.spacing.lg, t.spacing.md, t.spacing.md);
        groupBox2Layout->setSpacing(t.spacing.sm);

        lightness = new QCheckBox(groupBox_2);
        lightness->setObjectName("lightness");
        groupBox2Layout->addWidget(lightness);

        binaryMask = new QCheckBox(groupBox_2);
        binaryMask->setObjectName("binaryMask");
        groupBox2Layout->addWidget(binaryMask);

        // Grid layout aligns labels, spinboxes and sliders in shared columns across all three rows
        sliderGrid = new QGridLayout();
        sliderGrid->setHorizontalSpacing(t.spacing.sm);
        sliderGrid->setVerticalSpacing(t.spacing.sm);
        sliderGrid->setColumnStretch(0, 0);
        sliderGrid->setColumnStretch(1, 0);
        sliderGrid->setColumnStretch(2, 1);

        label_3 = new QLabel(groupBox_2);
        label_3->setObjectName("label_3");
        lowerLimitValue = new QDoubleSpinBox(groupBox_2);
        lowerLimitValue->setObjectName("lowerLimitValue");
        lowerLimitValue->setDecimals(6);
        lowerLimitValue->setMaximum(1.000000000000000);
        lowerLimitValue->setSingleStep(0.010000000000000);
        lowerLimitValue->setStepType(QAbstractSpinBox::DefaultStepType);
        lowerLimitValue->setValue(0.100000000000000);
        lowerLimitValue->setMinimumWidth(110);
        lowerLimitSlider = new QSlider(groupBox_2);
        lowerLimitSlider->setObjectName("lowerLimitSlider");
        lowerLimitSlider->setMaximum(1000);
        lowerLimitSlider->setValue(100);
        lowerLimitSlider->setOrientation(Qt::Horizontal);
        sliderGrid->addWidget(label_3, 0, 0, Qt::AlignVCenter);
        sliderGrid->addWidget(lowerLimitValue, 0, 1, Qt::AlignVCenter);
        sliderGrid->addWidget(lowerLimitSlider, 0, 2, Qt::AlignVCenter);

        label_4 = new QLabel(groupBox_2);
        label_4->setObjectName("label_4");
        upperLimitValue = new QDoubleSpinBox(groupBox_2);
        upperLimitValue->setObjectName("upperLimitValue");
        upperLimitValue->setDecimals(6);
        upperLimitValue->setMaximum(1.000000000000000);
        upperLimitValue->setSingleStep(0.050000000000000);
        upperLimitValue->setStepType(QAbstractSpinBox::DefaultStepType);
        upperLimitValue->setValue(1.000000000000000);
        upperLimitValue->setMinimumWidth(110);
        upperLimitSlider = new QSlider(groupBox_2);
        upperLimitSlider->setObjectName("upperLimitSlider");
        upperLimitSlider->setMaximum(1000);
        upperLimitSlider->setValue(1000);
        upperLimitSlider->setOrientation(Qt::Horizontal);
        sliderGrid->addWidget(label_4, 1, 0, Qt::AlignVCenter);
        sliderGrid->addWidget(upperLimitValue, 1, 1, Qt::AlignVCenter);
        sliderGrid->addWidget(upperLimitSlider, 1, 2, Qt::AlignVCenter);

        label_5 = new QLabel(groupBox_2);
        label_5->setObjectName("label_5");
        smoothnessValue = new QDoubleSpinBox(groupBox_2);
        smoothnessValue->setObjectName("smoothnessValue");
        smoothnessValue->setDecimals(2);
        smoothnessValue->setMaximum(100.000000000000000);
        smoothnessValue->setSingleStep(1.000000000000000);
        smoothnessValue->setStepType(QAbstractSpinBox::DefaultStepType);
        smoothnessValue->setMinimumWidth(110);
        smoothnessSlider = new QSlider(groupBox_2);
        smoothnessSlider->setObjectName("smoothnessSlider");
        smoothnessSlider->setMaximum(1000);
        smoothnessSlider->setOrientation(Qt::Horizontal);
        sliderGrid->addWidget(label_5, 2, 0, Qt::AlignVCenter);
        sliderGrid->addWidget(smoothnessValue, 2, 1, Qt::AlignVCenter);
        sliderGrid->addWidget(smoothnessSlider, 2, 2, Qt::AlignVCenter);

        groupBox2Layout->addLayout(sliderGrid);
        layoutWidget = new QWidget(StarnetStarMaskProcess);
        layoutWidget->setObjectName("layoutWidget");
        horizontalLayout = new QHBoxLayout(layoutWidget);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        label_6 = new QLabel(layoutWidget);
        label_6->setObjectName("label_6");

        horizontalLayout->addWidget(label_6);

        targetImageSelector = new QComboBox(layoutWidget);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout->addWidget(targetImageSelector);

        horizontalLayout->setStretch(1, 1);

        rootLayout->addWidget(groupBox_2);
        rootLayout->addWidget(groupBox);
        rootLayout->addWidget(layoutWidget);
        rootLayout->addStretch(1);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(StarnetStarMaskProcess);

        retranslateUi(StarnetStarMaskProcess);

        groupBoxLayout->invalidate();
        groupBox->setMinimumHeight(groupBox->minimumSizeHint().height());
        groupBox->updateGeometry();

        QMetaObject::connectSlotsByName(StarnetStarMaskProcess);
    } // setupUi

    void retranslateUi(QWidget* StarnetStarMaskProcess)
    {
        StarnetStarMaskProcess->setWindowTitle(
            QCoreApplication::translate("StarnetStarMaskProcess", "Starnet Starmask", nullptr));
        groupBox->setTitle(QCoreApplication::translate("StarnetStarMaskProcess", "StarNet2 Settings", nullptr));
        configureStarnet->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Configure", nullptr));
        upsamplex2->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Upsample x2", nullptr));
        preStretchLinearImage->setText(
            QCoreApplication::translate("StarnetStarMaskProcess", "Pre-Stretch linear image", nullptr));
        highlightProtection->setText(
            QCoreApplication::translate("StarnetStarMaskProcess", "Highlight protection", nullptr));
        stridePresetLabel->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Stride", nullptr));
        weightsPathLabel->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Weights", nullptr));
        weightsPath->setPlaceholderText(
            QCoreApplication::translate("StarnetStarMaskProcess", "Default StarNet2_weights.onnx", nullptr));
        browseWeights->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Browse", nullptr));
        label->setText(QCoreApplication::translate(
            "StarnetStarMaskProcess",
            "StarNet2 CLI version 2.5.3 or newer is required. Configure the starnet2 executable before running.",
            nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("StarnetStarMaskProcess", "Mask Settings", nullptr));
        lightness->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Lightness", nullptr));
        binaryMask->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Binary Mask", nullptr));
        label_3->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Lower limit", nullptr));
        label_4->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Upper limit", nullptr));
        label_5->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Smoothness", nullptr));
        label_6->setText(QCoreApplication::translate("StarnetStarMaskProcess", "Target Image", nullptr));

        // Tooltips
#if QT_CONFIG(tooltip)
        upsamplex2->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess",
            "Ask StarNet2 to use its native 2x processing mode for better mask generation on small stars.", nullptr));
        preStretchLinearImage->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess",
            "Apply an automatic stretch before processing linear images. The native StarNet2 mask is generated from "
            "that stretched input.",
            nullptr));
        highlightProtection->setToolTip(
            QCoreApplication::translate("StarnetStarMaskProcess",
                                        "Keep StarNet2's default highlight-protection pass enabled for saturated and "
                                        "near-saturated bright regions.",
                                        nullptr));
        stridePreset->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess",
            "Choose the StarNet2 tile stride. Standard 256 is the default; 384 is useful for wide-field images with "
            "small stars.",
            nullptr));
        customStrideValue->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess", "Custom even stride value from 2 to 512 pixels for StarNet2 tile processing.",
            nullptr));
        weightsPath->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess",
            "Optional path to a StarNet2 ONNX model checkpoint. Leave empty to use the model bundled with the CLI.",
            nullptr));
        browseWeights->setToolTip(QCoreApplication::translate("StarnetStarMaskProcess",
                                                              "Select a custom StarNet2 ONNX weights file.", nullptr));
        lightness->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess",
            "Convert the star mask to grayscale lightness channel for a single-channel mask output.", nullptr));
        binaryMask->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess", "Apply a range selection to produce a binary star mask with hard edges.",
            nullptr));
        lowerLimitValue->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess", "Lower brightness threshold for the binary mask range selection.", nullptr));
        lowerLimitSlider->setToolTip(lowerLimitValue->toolTip());
        upperLimitValue->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess", "Upper brightness threshold for the binary mask range selection.", nullptr));
        upperLimitSlider->setToolTip(upperLimitValue->toolTip());
        smoothnessValue->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess", "Amount of smoothing applied to the binary mask transition edges.", nullptr));
        smoothnessSlider->setToolTip(smoothnessValue->toolTip());
        targetImageSelector->setToolTip(
            QCoreApplication::translate("StarnetStarMaskProcess", "Image to generate the star mask from.", nullptr));
        configureStarnet->setToolTip(QCoreApplication::translate(
            "StarnetStarMaskProcess", "Open settings to configure the StarNet2 2.5.3+ executable path.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("StarnetStarMaskProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline StarnetStarMaskProcessUiElements buildStarnetStarMaskProcessUi(QWidget* parent)
{
    StarnetStarMaskProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
