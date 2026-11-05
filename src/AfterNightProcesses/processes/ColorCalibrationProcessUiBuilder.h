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
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class ColorCalibrationProcessUiElements
{
public:
    QLabel* label = nullptr;
    QLabel* label_2 = nullptr;
    QComboBox* whiteReferenceImageSelector = nullptr;
    QLabel* label_3 = nullptr;
    QDoubleSpinBox* whiteLowerLimitValue = nullptr;
    QSlider* whiteLowerLimitSlider = nullptr;
    QLabel* label_5 = nullptr;
    QDoubleSpinBox* whiteUpperLimitValue = nullptr;
    QSlider* whiteUpperLimitSlider = nullptr;
    QGroupBox* whiteRoiGroupBox = nullptr;
    QLabel* label_6 = nullptr;
    QSpinBox* whiteRoiLeftValue = nullptr;
    QLabel* label_7 = nullptr;
    QSpinBox* whiteRoiTopValue = nullptr;
    QPushButton* whiteRoiSelectionButton = nullptr;
    QLabel* label_8 = nullptr;
    QSpinBox* whiteRoiWidthValue = nullptr;
    QLabel* label_9 = nullptr;
    QSpinBox* whiteRoiHeightValue = nullptr;
    QGroupBox* mwbGroupBox = nullptr;
    QLabel* label_4 = nullptr;
    QDoubleSpinBox* mwbRedValue = nullptr;
    QSlider* mwbRedSlider = nullptr;
    QLabel* label_10 = nullptr;
    QDoubleSpinBox* mwbGreenValue = nullptr;
    QSlider* mwbGreenSlider = nullptr;
    QLabel* label_11 = nullptr;
    QDoubleSpinBox* mwbBlueValue = nullptr;
    QSlider* mwbBlueSlider = nullptr;
    QCheckBox* outputWhiteReferenceMask = nullptr;
    QLabel* label_13 = nullptr;
    QLabel* label_12 = nullptr;
    QComboBox* backgroundReferenceImageSelector = nullptr;
    QLabel* label_14 = nullptr;
    QDoubleSpinBox* backgroundLowerLimitValue = nullptr;
    QSlider* backgroundLowerLimitSlider = nullptr;
    QLabel* label_15 = nullptr;
    QDoubleSpinBox* backgroundUpperLimitValue = nullptr;
    QSlider* backgroundUpperLimitSlider = nullptr;
    QGroupBox* backgroundRoiGroupBox = nullptr;
    QLabel* label_16 = nullptr;
    QSpinBox* backgroundRoiLeftValue = nullptr;
    QLabel* label_17 = nullptr;
    QSpinBox* backgroundRoiTopValue = nullptr;
    QPushButton* backgroundRoiSelectionButton = nullptr;
    QLabel* label_18 = nullptr;
    QSpinBox* backgroundRoiWidthValue = nullptr;
    QLabel* label_19 = nullptr;
    QSpinBox* backgroundRoiHeightValue = nullptr;
    QCheckBox* outputBackgroundReferenceMask = nullptr;
    QLabel* label_20 = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    void setupUi(QWidget* parent)
    {
        const auto& t = an::ui::tokens();
        if (parent->objectName().isEmpty())
            parent->setObjectName("ColorCalibrationProcess");
        parent->resize(520, 700);
        parent->setMinimumSize(QSize(500, 500));

        constexpr int kMinContentWidth = 475;
        constexpr int kRoiSpinBoxWidth = 88;

        auto* rootLayout = new QVBoxLayout(parent);
        rootLayout->setObjectName("rootLayout");
        rootLayout->setContentsMargins(t.spacing.lg, t.spacing.md, t.spacing.lg, t.spacing.md);
        rootLayout->setSpacing(t.spacing.sm);
        rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

        // Zero-height spacer enforces a minimum content width that
        // propagates through the shell layout (parent->setMinimumSize
        // is overridden by the SetMinimumSize constraint each pass).
        rootLayout->addItem(new QSpacerItem(kMinContentWidth, 0, QSizePolicy::Minimum, QSizePolicy::Fixed));

        // ── White Reference section header ──
        label = new QLabel(parent);
        label->setObjectName("label");
        label->setFont(t.typography.heading3);
        rootLayout->addWidget(label);

        // Reference image row
        auto* whiteRefRow = new QHBoxLayout();
        whiteRefRow->setContentsMargins(t.spacing.md, 0, 0, 0);
        whiteRefRow->setSpacing(t.spacing.sm);
        label_2 = new QLabel(parent);
        label_2->setObjectName("label_2");
        whiteReferenceImageSelector = new QComboBox(parent);
        whiteReferenceImageSelector->setObjectName("whiteReferenceImageSelector");
        whiteReferenceImageSelector->setMinimumWidth(150);
        whiteReferenceImageSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        whiteRefRow->addWidget(label_2);
        whiteRefRow->addWidget(whiteReferenceImageSelector, 1);
        rootLayout->addLayout(whiteRefRow);

        // Lower Limit row
        auto* whiteLowerRow = new QHBoxLayout();
        whiteLowerRow->setContentsMargins(t.spacing.xl, 0, 0, 0);
        whiteLowerRow->setSpacing(t.spacing.sm);
        label_3 = new QLabel(parent);
        label_3->setObjectName("label_3");
        whiteLowerLimitValue = new QDoubleSpinBox(parent);
        whiteLowerLimitValue->setObjectName("whiteLowerLimitValue");
        whiteLowerLimitValue->setDecimals(6);
        whiteLowerLimitValue->setMaximum(1.0);
        whiteLowerLimitValue->setSingleStep(0.05);
        whiteLowerLimitSlider = new QSlider(Qt::Horizontal, parent);
        whiteLowerLimitSlider->setObjectName("whiteLowerLimitSlider");
        whiteLowerLimitSlider->setMaximum(1000);
        whiteLowerRow->addWidget(label_3);
        whiteLowerRow->addWidget(whiteLowerLimitValue, 1);
        whiteLowerRow->addWidget(whiteLowerLimitSlider, 3);
        rootLayout->addLayout(whiteLowerRow);

        // Upper Limit row
        auto* whiteUpperRow = new QHBoxLayout();
        whiteUpperRow->setContentsMargins(t.spacing.xl, 0, 0, 0);
        whiteUpperRow->setSpacing(t.spacing.sm);
        label_5 = new QLabel(parent);
        label_5->setObjectName("label_5");
        whiteUpperLimitValue = new QDoubleSpinBox(parent);
        whiteUpperLimitValue->setObjectName("whiteUpperLimitValue");
        whiteUpperLimitValue->setDecimals(6);
        whiteUpperLimitValue->setMaximum(1.0);
        whiteUpperLimitValue->setSingleStep(0.05);
        whiteUpperLimitValue->setValue(0.9);
        whiteUpperLimitSlider = new QSlider(Qt::Horizontal, parent);
        whiteUpperLimitSlider->setObjectName("whiteUpperLimitSlider");
        whiteUpperLimitSlider->setMaximum(1000);
        whiteUpperLimitSlider->setValue(900);
        whiteUpperRow->addWidget(label_5);
        whiteUpperRow->addWidget(whiteUpperLimitValue, 1);
        whiteUpperRow->addWidget(whiteUpperLimitSlider, 3);
        rootLayout->addLayout(whiteUpperRow);

        // ── White ROI GroupBox ──
        whiteRoiGroupBox = new QGroupBox(parent);
        whiteRoiGroupBox->setObjectName("whiteRoiGroupBox");
        whiteRoiGroupBox->setCheckable(true);
        whiteRoiGroupBox->setChecked(false);
        auto* whiteRoiLayout = new QGridLayout(whiteRoiGroupBox);
        whiteRoiLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);
        whiteRoiLayout->setHorizontalSpacing(t.spacing.sm);
        whiteRoiLayout->setVerticalSpacing(t.spacing.sm);

        // Row 0: Left [spin]  Top [spin]  [From Selection]
        label_6 = new QLabel(whiteRoiGroupBox);
        label_6->setObjectName("label_6");
        whiteRoiLeftValue = new QSpinBox(whiteRoiGroupBox);
        whiteRoiLeftValue->setObjectName("whiteRoiLeftValue");
        whiteRoiLeftValue->setMinimumWidth(kRoiSpinBoxWidth);
        whiteRoiLeftValue->setMaximumWidth(kRoiSpinBoxWidth);
        label_7 = new QLabel(whiteRoiGroupBox);
        label_7->setObjectName("label_7");
        whiteRoiTopValue = new QSpinBox(whiteRoiGroupBox);
        whiteRoiTopValue->setObjectName("whiteRoiTopValue");
        whiteRoiTopValue->setMinimumWidth(kRoiSpinBoxWidth);
        whiteRoiTopValue->setMaximumWidth(kRoiSpinBoxWidth);
        whiteRoiSelectionButton = new QPushButton(whiteRoiGroupBox);
        whiteRoiSelectionButton->setObjectName("whiteRoiSelectionButton");
        whiteRoiSelectionButton->setCheckable(true);

        whiteRoiLayout->addWidget(label_6, 0, 0);
        whiteRoiLayout->addWidget(whiteRoiLeftValue, 0, 1);
        whiteRoiLayout->addWidget(label_7, 0, 2);
        whiteRoiLayout->addWidget(whiteRoiTopValue, 0, 3);
        whiteRoiLayout->addWidget(whiteRoiSelectionButton, 0, 4);

        // Row 1: Width [spin]  Height [spin]
        label_8 = new QLabel(whiteRoiGroupBox);
        label_8->setObjectName("label_8");
        whiteRoiWidthValue = new QSpinBox(whiteRoiGroupBox);
        whiteRoiWidthValue->setObjectName("whiteRoiWidthValue");
        whiteRoiWidthValue->setMinimumWidth(kRoiSpinBoxWidth);
        whiteRoiWidthValue->setMaximumWidth(kRoiSpinBoxWidth);
        label_9 = new QLabel(whiteRoiGroupBox);
        label_9->setObjectName("label_9");
        whiteRoiHeightValue = new QSpinBox(whiteRoiGroupBox);
        whiteRoiHeightValue->setObjectName("whiteRoiHeightValue");
        whiteRoiHeightValue->setMinimumWidth(kRoiSpinBoxWidth);
        whiteRoiHeightValue->setMaximumWidth(kRoiSpinBoxWidth);

        whiteRoiLayout->addWidget(label_8, 1, 0);
        whiteRoiLayout->addWidget(whiteRoiWidthValue, 1, 1);
        whiteRoiLayout->addWidget(label_9, 1, 2);
        whiteRoiLayout->addWidget(whiteRoiHeightValue, 1, 3);

        whiteRoiLayout->setColumnStretch(1, 1);
        whiteRoiLayout->setColumnStretch(3, 1);
        whiteRoiLayout->setColumnStretch(4, 0);

        rootLayout->addWidget(whiteRoiGroupBox);

        // ── Manual White Balance GroupBox ──
        mwbGroupBox = new QGroupBox(parent);
        mwbGroupBox->setObjectName("mwbGroupBox");
        mwbGroupBox->setCheckable(true);
        mwbGroupBox->setChecked(false);
        auto* mwbLayout = new QGridLayout(mwbGroupBox);
        mwbLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);
        mwbLayout->setHorizontalSpacing(t.spacing.sm);
        mwbLayout->setVerticalSpacing(t.spacing.sm);

        // Red
        label_4 = new QLabel(mwbGroupBox);
        label_4->setObjectName("label_4");
        mwbRedValue = new QDoubleSpinBox(mwbGroupBox);
        mwbRedValue->setObjectName("mwbRedValue");
        mwbRedValue->setDecimals(6);
        // A channel cannot be multiplied by zero; the smallest white value is the dialog's last decimal.
        mwbRedValue->setMinimum(0.000001);
        mwbRedValue->setMaximum(1.0);
        mwbRedValue->setSingleStep(0.05);
        mwbRedValue->setValue(1.0);
        mwbRedSlider = new QSlider(Qt::Horizontal, mwbGroupBox);
        mwbRedSlider->setObjectName("mwbRedSlider");
        mwbRedSlider->setMaximum(1000);
        mwbRedSlider->setValue(1000);
        mwbLayout->addWidget(label_4, 0, 0);
        mwbLayout->addWidget(mwbRedValue, 0, 1);
        mwbLayout->addWidget(mwbRedSlider, 0, 2);

        // Green
        label_10 = new QLabel(mwbGroupBox);
        label_10->setObjectName("label_10");
        mwbGreenValue = new QDoubleSpinBox(mwbGroupBox);
        mwbGreenValue->setObjectName("mwbGreenValue");
        mwbGreenValue->setDecimals(6);
        // A channel cannot be multiplied by zero; the smallest white value is the dialog's last decimal.
        mwbGreenValue->setMinimum(0.000001);
        mwbGreenValue->setMaximum(1.0);
        mwbGreenValue->setSingleStep(0.05);
        mwbGreenValue->setValue(1.0);
        mwbGreenSlider = new QSlider(Qt::Horizontal, mwbGroupBox);
        mwbGreenSlider->setObjectName("mwbGreenSlider");
        mwbGreenSlider->setMaximum(1000);
        mwbGreenSlider->setValue(1000);
        mwbLayout->addWidget(label_10, 1, 0);
        mwbLayout->addWidget(mwbGreenValue, 1, 1);
        mwbLayout->addWidget(mwbGreenSlider, 1, 2);

        // Blue
        label_11 = new QLabel(mwbGroupBox);
        label_11->setObjectName("label_11");
        mwbBlueValue = new QDoubleSpinBox(mwbGroupBox);
        mwbBlueValue->setObjectName("mwbBlueValue");
        mwbBlueValue->setDecimals(6);
        // A channel cannot be multiplied by zero; the smallest white value is the dialog's last decimal.
        mwbBlueValue->setMinimum(0.000001);
        mwbBlueValue->setMaximum(1.0);
        mwbBlueValue->setSingleStep(0.05);
        mwbBlueValue->setValue(1.0);
        mwbBlueSlider = new QSlider(Qt::Horizontal, mwbGroupBox);
        mwbBlueSlider->setObjectName("mwbBlueSlider");
        mwbBlueSlider->setMaximum(1000);
        mwbBlueSlider->setValue(1000);
        mwbLayout->addWidget(label_11, 2, 0);
        mwbLayout->addWidget(mwbBlueValue, 2, 1);
        mwbLayout->addWidget(mwbBlueSlider, 2, 2);

        mwbLayout->setColumnStretch(1, 1);
        mwbLayout->setColumnStretch(2, 3);

        rootLayout->addWidget(mwbGroupBox);

        // Output White Reference Mask
        outputWhiteReferenceMask = new QCheckBox(parent);
        outputWhiteReferenceMask->setObjectName("outputWhiteReferenceMask");
        rootLayout->addWidget(outputWhiteReferenceMask);

        // ── Separator spacing ──
        rootLayout->addSpacing(t.spacing.md);

        // ── Background Reference section header ──
        label_13 = new QLabel(parent);
        label_13->setObjectName("label_13");
        label_13->setFont(t.typography.heading3);
        rootLayout->addWidget(label_13);

        // Background reference image row
        auto* bgRefRow = new QHBoxLayout();
        bgRefRow->setContentsMargins(t.spacing.md, 0, 0, 0);
        bgRefRow->setSpacing(t.spacing.sm);
        label_12 = new QLabel(parent);
        label_12->setObjectName("label_12");
        backgroundReferenceImageSelector = new QComboBox(parent);
        backgroundReferenceImageSelector->setObjectName("backgroundReferenceImageSelector");
        backgroundReferenceImageSelector->setMinimumWidth(150);
        backgroundReferenceImageSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bgRefRow->addWidget(label_12);
        bgRefRow->addWidget(backgroundReferenceImageSelector, 1);
        rootLayout->addLayout(bgRefRow);

        // Background Lower Limit row
        auto* bgLowerRow = new QHBoxLayout();
        bgLowerRow->setContentsMargins(t.spacing.xl, 0, 0, 0);
        bgLowerRow->setSpacing(t.spacing.sm);
        label_14 = new QLabel(parent);
        label_14->setObjectName("label_14");
        backgroundLowerLimitValue = new QDoubleSpinBox(parent);
        backgroundLowerLimitValue->setObjectName("backgroundLowerLimitValue");
        backgroundLowerLimitValue->setDecimals(6);
        backgroundLowerLimitValue->setMaximum(1.0);
        backgroundLowerLimitValue->setSingleStep(0.05);
        backgroundLowerLimitSlider = new QSlider(Qt::Horizontal, parent);
        backgroundLowerLimitSlider->setObjectName("backgroundLowerLimitSlider");
        backgroundLowerLimitSlider->setMaximum(1000);
        bgLowerRow->addWidget(label_14);
        bgLowerRow->addWidget(backgroundLowerLimitValue, 1);
        bgLowerRow->addWidget(backgroundLowerLimitSlider, 3);
        rootLayout->addLayout(bgLowerRow);

        // Background Upper Limit row
        auto* bgUpperRow = new QHBoxLayout();
        bgUpperRow->setContentsMargins(t.spacing.xl, 0, 0, 0);
        bgUpperRow->setSpacing(t.spacing.sm);
        label_15 = new QLabel(parent);
        label_15->setObjectName("label_15");
        backgroundUpperLimitValue = new QDoubleSpinBox(parent);
        backgroundUpperLimitValue->setObjectName("backgroundUpperLimitValue");
        backgroundUpperLimitValue->setDecimals(6);
        backgroundUpperLimitValue->setMaximum(1.0);
        backgroundUpperLimitValue->setSingleStep(0.05);
        backgroundUpperLimitValue->setValue(0.1);
        backgroundUpperLimitSlider = new QSlider(Qt::Horizontal, parent);
        backgroundUpperLimitSlider->setObjectName("backgroundUpperLimitSlider");
        backgroundUpperLimitSlider->setMaximum(1000);
        backgroundUpperLimitSlider->setValue(100);
        bgUpperRow->addWidget(label_15);
        bgUpperRow->addWidget(backgroundUpperLimitValue, 1);
        bgUpperRow->addWidget(backgroundUpperLimitSlider, 3);
        rootLayout->addLayout(bgUpperRow);

        // ── Background ROI GroupBox ──
        backgroundRoiGroupBox = new QGroupBox(parent);
        backgroundRoiGroupBox->setObjectName("backgroundRoiGroupBox");
        backgroundRoiGroupBox->setCheckable(true);
        backgroundRoiGroupBox->setChecked(false);
        auto* bgRoiLayout = new QGridLayout(backgroundRoiGroupBox);
        bgRoiLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);
        bgRoiLayout->setHorizontalSpacing(t.spacing.sm);
        bgRoiLayout->setVerticalSpacing(t.spacing.sm);

        // Row 0: Left [spin]  Top [spin]  [From Selection]
        label_19 = new QLabel(backgroundRoiGroupBox);
        label_19->setObjectName("label_19");
        backgroundRoiLeftValue = new QSpinBox(backgroundRoiGroupBox);
        backgroundRoiLeftValue->setObjectName("backgroundRoiLeftValue");
        backgroundRoiLeftValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiLeftValue->setMaximumWidth(kRoiSpinBoxWidth);
        label_17 = new QLabel(backgroundRoiGroupBox);
        label_17->setObjectName("label_17");
        backgroundRoiTopValue = new QSpinBox(backgroundRoiGroupBox);
        backgroundRoiTopValue->setObjectName("backgroundRoiTopValue");
        backgroundRoiTopValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiTopValue->setMaximumWidth(kRoiSpinBoxWidth);
        backgroundRoiSelectionButton = new QPushButton(backgroundRoiGroupBox);
        backgroundRoiSelectionButton->setObjectName("backgroundRoiSelectionButton");
        backgroundRoiSelectionButton->setCheckable(true);

        bgRoiLayout->addWidget(label_19, 0, 0);
        bgRoiLayout->addWidget(backgroundRoiLeftValue, 0, 1);
        bgRoiLayout->addWidget(label_17, 0, 2);
        bgRoiLayout->addWidget(backgroundRoiTopValue, 0, 3);
        bgRoiLayout->addWidget(backgroundRoiSelectionButton, 0, 4);

        // Row 1: Width [spin]  Height [spin]
        label_18 = new QLabel(backgroundRoiGroupBox);
        label_18->setObjectName("label_18");
        backgroundRoiWidthValue = new QSpinBox(backgroundRoiGroupBox);
        backgroundRoiWidthValue->setObjectName("backgroundRoiWidthValue");
        backgroundRoiWidthValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiWidthValue->setMaximumWidth(kRoiSpinBoxWidth);
        label_16 = new QLabel(backgroundRoiGroupBox);
        label_16->setObjectName("label_16");
        backgroundRoiHeightValue = new QSpinBox(backgroundRoiGroupBox);
        backgroundRoiHeightValue->setObjectName("backgroundRoiHeightValue");
        backgroundRoiHeightValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiHeightValue->setMaximumWidth(kRoiSpinBoxWidth);

        bgRoiLayout->addWidget(label_18, 1, 0);
        bgRoiLayout->addWidget(backgroundRoiWidthValue, 1, 1);
        bgRoiLayout->addWidget(label_16, 1, 2);
        bgRoiLayout->addWidget(backgroundRoiHeightValue, 1, 3);

        bgRoiLayout->setColumnStretch(1, 1);
        bgRoiLayout->setColumnStretch(3, 1);
        bgRoiLayout->setColumnStretch(4, 0);

        rootLayout->addWidget(backgroundRoiGroupBox);

        // Output Background Reference Mask
        outputBackgroundReferenceMask = new QCheckBox(parent);
        outputBackgroundReferenceMask->setObjectName("outputBackgroundReferenceMask");
        rootLayout->addWidget(outputBackgroundReferenceMask);

        // ── Vertical spacer ──
        rootLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

        // ── Target Image row ──
        auto* targetRow = new QHBoxLayout();
        targetRow->setSpacing(t.spacing.sm);
        label_20 = new QLabel(parent);
        label_20->setObjectName("label_20");
        targetImageSelector = new QComboBox(parent);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        targetRow->addWidget(label_20);
        targetRow->addWidget(targetImageSelector, 1);
        rootLayout->addLayout(targetRow);

        // ── Button box ──
        buttonBox = new QDialogButtonBox(parent);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(parent);

        QMetaObject::connectSlotsByName(parent);
    } // setupUi

    void retranslateUi(QWidget* ColorCalibrationProcess)
    {
        ColorCalibrationProcess->setWindowTitle(
            QCoreApplication::translate("ColorCalibrationProcess", "Color Calibration", nullptr));
        label->setText(QCoreApplication::translate("ColorCalibrationProcess", "White Reference", nullptr));
        label_2->setText(QCoreApplication::translate("ColorCalibrationProcess", "Reference image:", nullptr));
        label_3->setText(QCoreApplication::translate("ColorCalibrationProcess", "Lower Limit:", nullptr));
        label_5->setText(QCoreApplication::translate("ColorCalibrationProcess", "Upper Limit:", nullptr));
        whiteRoiGroupBox->setTitle(
            QCoreApplication::translate("ColorCalibrationProcess", "Region of interest", nullptr));
        label_6->setText(QCoreApplication::translate("ColorCalibrationProcess", "Left:", nullptr));
        label_7->setText(QCoreApplication::translate("ColorCalibrationProcess", "Top:", nullptr));
        whiteRoiSelectionButton->setText(
            QCoreApplication::translate("ColorCalibrationProcess", "From Selection", nullptr));
        label_8->setText(QCoreApplication::translate("ColorCalibrationProcess", "Width:", nullptr));
        label_9->setText(QCoreApplication::translate("ColorCalibrationProcess", "Height:", nullptr));
        mwbGroupBox->setTitle(QCoreApplication::translate("ColorCalibrationProcess", "Manual White Balance", nullptr));
        label_4->setText(QCoreApplication::translate("ColorCalibrationProcess", "Red:", nullptr));
        label_10->setText(QCoreApplication::translate("ColorCalibrationProcess", "Green:", nullptr));
        label_11->setText(QCoreApplication::translate("ColorCalibrationProcess", "Blue:", nullptr));
        outputWhiteReferenceMask->setText(
            QCoreApplication::translate("ColorCalibrationProcess", "Output White Reference Mask", nullptr));
        label_13->setText(QCoreApplication::translate("ColorCalibrationProcess", "Background Reference", nullptr));
        label_12->setText(QCoreApplication::translate("ColorCalibrationProcess", "Reference image:", nullptr));
        label_14->setText(QCoreApplication::translate("ColorCalibrationProcess", "Lower Limit:", nullptr));
        label_15->setText(QCoreApplication::translate("ColorCalibrationProcess", "Upper Limit:", nullptr));
        backgroundRoiGroupBox->setTitle(
            QCoreApplication::translate("ColorCalibrationProcess", "Region of interest", nullptr));
        label_19->setText(QCoreApplication::translate("ColorCalibrationProcess", "Left:", nullptr));
        label_17->setText(QCoreApplication::translate("ColorCalibrationProcess", "Top:", nullptr));
        backgroundRoiSelectionButton->setText(
            QCoreApplication::translate("ColorCalibrationProcess", "From Selection", nullptr));
        label_18->setText(QCoreApplication::translate("ColorCalibrationProcess", "Width:", nullptr));
        label_16->setText(QCoreApplication::translate("ColorCalibrationProcess", "Height:", nullptr));
        outputBackgroundReferenceMask->setText(
            QCoreApplication::translate("ColorCalibrationProcess", "Output Background Reference Mask", nullptr));
        label_20->setText(QCoreApplication::translate("ColorCalibrationProcess", "Target Image ", nullptr));

        whiteReferenceImageSelector->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Select the white-reference image used for white balance calibration.",
            nullptr));
        whiteLowerLimitValue->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Lower intensity threshold for white-reference sampling.", nullptr));
        whiteLowerLimitSlider->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Lower intensity threshold for white-reference sampling.", nullptr));
        whiteUpperLimitValue->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Upper intensity threshold for white-reference sampling.", nullptr));
        whiteUpperLimitSlider->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Upper intensity threshold for white-reference sampling.", nullptr));
        backgroundReferenceImageSelector->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Select the background-reference image used for background neutralization.",
            nullptr));
        backgroundLowerLimitValue->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Lower intensity threshold for background-reference sampling.", nullptr));
        backgroundLowerLimitSlider->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Lower intensity threshold for background-reference sampling.", nullptr));
        backgroundUpperLimitValue->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Upper intensity threshold for background-reference sampling.", nullptr));
        backgroundUpperLimitSlider->setToolTip(QCoreApplication::translate(
            "ColorCalibrationProcess", "Upper intensity threshold for background-reference sampling.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("ColorCalibrationProcess", "Choose the open image to process.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("ColorCalibrationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("ColorCalibrationProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline ColorCalibrationProcessUiElements buildColorCalibrationProcessUi(QWidget* parent)
{
    ColorCalibrationProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
