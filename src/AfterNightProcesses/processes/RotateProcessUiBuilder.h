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
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class RotateProcessUiElements
{
public:
    QGroupBox* groupBoxRot;
    QLabel* label;
    QDoubleSpinBox* rotationAngle;
    QCheckBox* resizeImage;
    QSlider* rotationAngleSlider;
    QLabel* label_6;
    QComboBox* interpolation;

    QGroupBox* groupBox;
    QLabel* label_3;
    QSpinBox* targetPosX;
    QLabel* label_5;
    QSpinBox* targetWidth;
    QLabel* label_4;
    QSpinBox* targetPosY;
    QLabel* label_2;
    QSpinBox* targetHeight;

    QLabel* currentSizeLbl;
    QLineEdit* currentSize;
    QLabel* newSizeLbl;
    QLineEdit* newSize;

    QLabel* label_7;
    QComboBox* targetImageSelector;

    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* RotateProcess)
    {
        if (RotateProcess->objectName().isEmpty())
            RotateProcess->setObjectName("RotateProcess");

        const auto& t = an::ui::tokens();

        auto* rootLayout = new QVBoxLayout(RotateProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        // Rotation Group
        groupBoxRot = new QGroupBox(RotateProcess);
        groupBoxRot->setObjectName("groupBoxRot");
        auto* rotLayout = new QVBoxLayout(groupBoxRot);
        rotLayout->setSpacing(t.spacing.md);

        auto* rotTopLayout = new QHBoxLayout();
        label = new QLabel(groupBoxRot);
        label->setObjectName("label");
        rotTopLayout->addWidget(label);

        rotationAngle = new QDoubleSpinBox(groupBoxRot);
        rotationAngle->setObjectName("rotationAngle");
        rotationAngle->setMinimum(-180.0);
        rotationAngle->setMaximum(180.0);
        rotationAngle->setValue(0.0);
        rotTopLayout->addWidget(rotationAngle);

        rotTopLayout->addStretch();

        resizeImage = new QCheckBox(groupBoxRot);
        resizeImage->setObjectName("resizeImage");
        rotTopLayout->addWidget(resizeImage);

        rotLayout->addLayout(rotTopLayout);

        rotationAngleSlider = new QSlider(groupBoxRot);
        rotationAngleSlider->setObjectName("rotationAngleSlider");
        rotationAngleSlider->setMinimum(-1800);
        rotationAngleSlider->setMaximum(1800);
        rotationAngleSlider->setOrientation(Qt::Horizontal);
        rotLayout->addWidget(rotationAngleSlider);

        auto* interpLayout = new QHBoxLayout();
        label_6 = new QLabel(groupBoxRot);
        label_6->setObjectName("label_6");
        interpLayout->addWidget(label_6);

        interpolation = new QComboBox(groupBoxRot);
        interpolation->setObjectName("interpolation");
        interpLayout->addWidget(interpolation);
        interpLayout->addStretch();

        rotLayout->addLayout(interpLayout);
        rootLayout->addWidget(groupBoxRot);

        // Crop Group — checkable with indicator to the right of the title.
        groupBox = new QGroupBox(RotateProcess);
        groupBox->setObjectName("groupBox");
        groupBox->setCheckable(true);
        groupBox->setChecked(false);
        auto* cropLayout = new QGridLayout(groupBox);
        cropLayout->setSpacing(t.spacing.md);

        label_3 = new QLabel(groupBox);
        label_3->setObjectName("label_3");
        cropLayout->addWidget(label_3, 0, 0);

        targetPosX = new QSpinBox(groupBox);
        targetPosX->setObjectName("targetPosX");
        targetPosX->setMaximum(100000);
        cropLayout->addWidget(targetPosX, 0, 1);

        label_5 = new QLabel(groupBox);
        label_5->setObjectName("label_5");
        cropLayout->addWidget(label_5, 0, 2);

        targetWidth = new QSpinBox(groupBox);
        targetWidth->setObjectName("targetWidth");
        targetWidth->setMaximum(100000);
        cropLayout->addWidget(targetWidth, 0, 3);

        label_4 = new QLabel(groupBox);
        label_4->setObjectName("label_4");
        cropLayout->addWidget(label_4, 1, 0);

        targetPosY = new QSpinBox(groupBox);
        targetPosY->setObjectName("targetPosY");
        targetPosY->setMaximum(100000);
        cropLayout->addWidget(targetPosY, 1, 1);

        label_2 = new QLabel(groupBox);
        label_2->setObjectName("label_2");
        cropLayout->addWidget(label_2, 1, 2);

        targetHeight = new QSpinBox(groupBox);
        targetHeight->setObjectName("targetHeight");
        targetHeight->setMaximum(100000);
        cropLayout->addWidget(targetHeight, 1, 3);

        rootLayout->addWidget(groupBox);

        // Sizes
        auto* sizeLayout = new QHBoxLayout();
        sizeLayout->setSpacing(t.spacing.md);

        currentSizeLbl = new QLabel(RotateProcess);
        currentSizeLbl->setObjectName("currentSizeLbl");
        sizeLayout->addWidget(currentSizeLbl);

        currentSize = new QLineEdit(RotateProcess);
        currentSize->setObjectName("currentSize");
        currentSize->setReadOnly(true);
        sizeLayout->addWidget(currentSize);

        sizeLayout->addStretch();

        newSizeLbl = new QLabel(RotateProcess);
        newSizeLbl->setObjectName("newSizeLbl");
        sizeLayout->addWidget(newSizeLbl);

        newSize = new QLineEdit(RotateProcess);
        newSize->setObjectName("newSize");
        newSize->setReadOnly(true);
        sizeLayout->addWidget(newSize);

        rootLayout->addLayout(sizeLayout);

        // Target Image
        auto* targetLayout = new QHBoxLayout();
        targetLayout->setSpacing(t.spacing.md);

        label_7 = new QLabel(RotateProcess);
        label_7->setObjectName("label_7");
        targetLayout->addWidget(label_7);

        targetImageSelector = new QComboBox(RotateProcess);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetLayout->addWidget(targetImageSelector, 1);

        rootLayout->addLayout(targetLayout);

        // Button Box
        buttonBox = new QDialogButtonBox(RotateProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(RotateProcess);

        QMetaObject::connectSlotsByName(RotateProcess);
    } // setupUi

    void retranslateUi(QWidget* RotateProcess)
    {
        RotateProcess->setWindowTitle(QCoreApplication::translate("RotateProcess", "Rotate", nullptr));
        newSizeLbl->setText(QCoreApplication::translate("RotateProcess", "New Size:", nullptr));
        currentSizeLbl->setText(QCoreApplication::translate("RotateProcess", "Current Size:", nullptr));
        groupBox->setTitle(QCoreApplication::translate("RotateProcess", "Crop", nullptr));
        label_4->setText(QCoreApplication::translate("RotateProcess", "Pos Y", nullptr));
        label_3->setText(QCoreApplication::translate("RotateProcess", "Pos X", nullptr));
        label_2->setText(QCoreApplication::translate("RotateProcess", "Height", nullptr));
        label_5->setText(QCoreApplication::translate("RotateProcess", "Width", nullptr));
        groupBoxRot->setTitle(QCoreApplication::translate("RotateProcess", "Rotation", nullptr));
        label_6->setText(QCoreApplication::translate("RotateProcess", "Interpolation", nullptr));
        label->setText(QCoreApplication::translate("RotateProcess", "Rotation Angle:", nullptr));
        resizeImage->setText(QCoreApplication::translate("RotateProcess", "Resize image", nullptr));
        label_7->setText(QCoreApplication::translate("RotateProcess", "Target Image", nullptr));

        // Tooltips — Rotation group
        rotationAngle->setToolTip(QCoreApplication::translate("RotateProcess",
                                                              "Rotation angle in degrees (-180° to +180°). "
                                                              "Positive values rotate the image clockwise.",
                                                              nullptr));
        rotationAngleSlider->setToolTip(QCoreApplication::translate("RotateProcess",
                                                                    "Drag to adjust the rotation angle interactively. "
                                                                    "The slider covers the range in coarse steps; "
                                                                    "type an exact angle in the box.",
                                                                    nullptr));
        resizeImage->setToolTip(
            QCoreApplication::translate("RotateProcess",
                                        "When checked, the output canvas is enlarged to fit the entire "
                                        "rotated image without clipping any corners.",
                                        nullptr));
        interpolation->setToolTip(QCoreApplication::translate(
            "RotateProcess",
            "<p style='margin:0;'>Resampling algorithm used to compute pixel values after rotation.</p>"
            "<p style='margin:4px 0 0 0;'>"
            "\u2022 Nearest Neighbor \u2014 fastest, may look blocky<br>"
            "\u2022 Bilinear \u2014 smooth, good general-purpose choice<br>"
            "\u2022 Bicubic \u2014 sharper than bilinear, slightly slower<br>"
            "\u2022 Lanczos4 \u2014 highest quality, best for final output</p>",
            nullptr));

        // Tooltips — Crop group
        groupBox->setToolTip(QCoreApplication::translate("RotateProcess",
                                                         "Enable to crop the rotated image to a specific region. "
                                                         "Useful for removing empty borders after rotation.",
                                                         nullptr));
        targetPosX->setToolTip(QCoreApplication::translate(
            "RotateProcess", "Horizontal offset (in pixels) of the top-left corner of the crop region.", nullptr));
        targetPosY->setToolTip(QCoreApplication::translate(
            "RotateProcess", "Vertical offset (in pixels) of the top-left corner of the crop region.", nullptr));
        targetWidth->setToolTip(
            QCoreApplication::translate("RotateProcess", "Width (in pixels) of the crop region.", nullptr));
        targetHeight->setToolTip(
            QCoreApplication::translate("RotateProcess", "Height (in pixels) of the crop region.", nullptr));

        // Tooltips — Sizes
        currentSize->setToolTip(QCoreApplication::translate(
            "RotateProcess", "Estimated memory footprint of the current image before rotation.", nullptr));
        newSize->setToolTip(QCoreApplication::translate(
            "RotateProcess", "Estimated memory footprint of the image after rotation (and optional crop).", nullptr));

        // Tooltips — Target image
        targetImageSelector->setToolTip(QCoreApplication::translate("RotateProcess",
                                                                    "Choose the open image to apply the rotation to. "
                                                                    "Hover over each entry to see image details.",
                                                                    nullptr));

        // Tooltips — Dialog buttons
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("RotateProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(
                QCoreApplication::translate("RotateProcess", "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline RotateProcessUiElements buildRotateProcessUi(QWidget* parent)
{
    RotateProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(382, 360));
    parent->setMaximumSize(QSize(382, 360));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
