// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class ResizeProcessUiElements
{
public:
    QCheckBox* keepAspectRatio;
    QLabel* label_2;
    QDoubleSpinBox* targetWidthPercent;
    QSpinBox* originalHeight;
    QLabel* label_6;
    QSpinBox* targetWidth;
    QSpinBox* originalWidth;
    QLineEdit* currentSize;
    QLabel* currentSizeLbl;
    QLabel* label_3;
    QLabel* label_4;
    QComboBox* interpolation;
    QDoubleSpinBox* targetHeightPercent;
    QSpinBox* targetHeight;
    QLabel* label_5;
    QDialogButtonBox* buttonBox;
    QLabel* label;
    QLabel* newSizeLbl;
    QLineEdit* newSize;
    QLabel* label_7;
    QComboBox* targetImageSelector;

    void setupUi(QWidget* ResizeProcess)
    {
        if (ResizeProcess->objectName().isEmpty())
            ResizeProcess->setObjectName("ResizeProcess");

        const auto& t = an::ui::tokens();

        auto* rootLayout = new QVBoxLayout(ResizeProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        auto* gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        gridLayout->setHorizontalSpacing(t.spacing.md);
        gridLayout->setVerticalSpacing(t.spacing.sm);

        // Row 0: Headers
        label_3 = new QLabel(ResizeProcess);
        label_3->setObjectName("label_3");
        label_3->setAlignment(Qt::AlignCenter);
        gridLayout->addWidget(label_3, 0, 1);

        label_4 = new QLabel(ResizeProcess);
        label_4->setObjectName("label_4");
        label_4->setAlignment(Qt::AlignCenter);
        gridLayout->addWidget(label_4, 0, 2);

        label_5 = new QLabel(ResizeProcess);
        label_5->setObjectName("label_5");
        label_5->setAlignment(Qt::AlignCenter);
        gridLayout->addWidget(label_5, 0, 3);

        // Row 1: Width
        label = new QLabel(ResizeProcess);
        label->setObjectName("label");
        gridLayout->addWidget(label, 1, 0);

        originalWidth = new QSpinBox(ResizeProcess);
        originalWidth->setObjectName("originalWidth");
        originalWidth->setReadOnly(true);
        originalWidth->setMinimum(1);
        originalWidth->setMaximum(100000);
        originalWidth->setMinimumWidth(80);
        gridLayout->addWidget(originalWidth, 1, 1);

        targetWidth = new QSpinBox(ResizeProcess);
        targetWidth->setObjectName("targetWidth");
        targetWidth->setMinimum(1);
        targetWidth->setMaximum(100000);
        targetWidth->setMinimumWidth(80);
        gridLayout->addWidget(targetWidth, 1, 2);

        targetWidthPercent = new QDoubleSpinBox(ResizeProcess);
        targetWidthPercent->setObjectName("targetWidthPercent");
        targetWidthPercent->setMinimum(1.0);
        targetWidthPercent->setMaximum(1000.0);
        targetWidthPercent->setValue(100.0);
        targetWidthPercent->setMinimumWidth(80);
        gridLayout->addWidget(targetWidthPercent, 1, 3);

        // Row 2: Height
        label_2 = new QLabel(ResizeProcess);
        label_2->setObjectName("label_2");
        gridLayout->addWidget(label_2, 2, 0);

        originalHeight = new QSpinBox(ResizeProcess);
        originalHeight->setObjectName("originalHeight");
        originalHeight->setReadOnly(true);
        originalHeight->setMinimum(1);
        originalHeight->setMaximum(100000);
        originalHeight->setMinimumWidth(80);
        gridLayout->addWidget(originalHeight, 2, 1);

        targetHeight = new QSpinBox(ResizeProcess);
        targetHeight->setObjectName("targetHeight");
        targetHeight->setMinimum(1);
        targetHeight->setMaximum(100000);
        targetHeight->setMinimumWidth(80);
        gridLayout->addWidget(targetHeight, 2, 2);

        targetHeightPercent = new QDoubleSpinBox(ResizeProcess);
        targetHeightPercent->setObjectName("targetHeightPercent");
        targetHeightPercent->setMinimum(1.0);
        targetHeightPercent->setMaximum(1000.0);
        targetHeightPercent->setValue(100.0);
        targetHeightPercent->setMinimumWidth(80);
        gridLayout->addWidget(targetHeightPercent, 2, 3);

        rootLayout->addLayout(gridLayout);

        // Keep aspect ratio
        keepAspectRatio = new QCheckBox(ResizeProcess);
        keepAspectRatio->setObjectName("keepAspectRatio");
        keepAspectRatio->setChecked(true);
        rootLayout->addWidget(keepAspectRatio);

        // Interpolation
        auto* interpLayout = new QHBoxLayout();
        interpLayout->setObjectName("interpLayout");
        interpLayout->setSpacing(t.spacing.sm);

        label_6 = new QLabel(ResizeProcess);
        label_6->setObjectName("label_6");
        interpLayout->addWidget(label_6);

        interpolation = new QComboBox(ResizeProcess);
        interpolation->setObjectName("interpolation");
        interpolation->setMinimumWidth(150);
        interpLayout->addWidget(interpolation);
        interpLayout->addStretch(1);

        rootLayout->addLayout(interpLayout);

        // Sizes
        auto* sizesLayout = new QHBoxLayout();
        sizesLayout->setObjectName("sizesLayout");
        sizesLayout->setSpacing(t.spacing.sm);

        currentSizeLbl = new QLabel(ResizeProcess);
        currentSizeLbl->setObjectName("currentSizeLbl");
        sizesLayout->addWidget(currentSizeLbl);

        currentSize = new QLineEdit(ResizeProcess);
        currentSize->setObjectName("currentSize");
        currentSize->setReadOnly(true);
        currentSize->setMinimumWidth(100);
        sizesLayout->addWidget(currentSize);

        sizesLayout->addStretch(1);

        newSizeLbl = new QLabel(ResizeProcess);
        newSizeLbl->setObjectName("newSizeLbl");
        sizesLayout->addWidget(newSizeLbl);

        newSize = new QLineEdit(ResizeProcess);
        newSize->setObjectName("newSize");
        newSize->setReadOnly(true);
        newSize->setMinimumWidth(100);
        sizesLayout->addWidget(newSize);

        rootLayout->addLayout(sizesLayout);

        // Target Image
        auto* targetLayout = new QHBoxLayout();
        targetLayout->setObjectName("targetLayout");
        targetLayout->setSpacing(t.spacing.sm);

        label_7 = new QLabel(ResizeProcess);
        label_7->setObjectName("label_7");
        targetLayout->addWidget(label_7);

        targetImageSelector = new QComboBox(ResizeProcess);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetLayout->addWidget(targetImageSelector);
        targetLayout->setStretch(1, 1);

        rootLayout->addLayout(targetLayout);

        // Button Box
        buttonBox = new QDialogButtonBox(ResizeProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(ResizeProcess);

        QMetaObject::connectSlotsByName(ResizeProcess);
    } // setupUi

    void retranslateUi(QWidget* ResizeProcess)
    {
        ResizeProcess->setWindowTitle(QCoreApplication::translate("ResizeProcess", "Resize", nullptr));
        keepAspectRatio->setText(QCoreApplication::translate("ResizeProcess", "Keep aspect ratio", nullptr));
        label_2->setText(QCoreApplication::translate("ResizeProcess", "Height", nullptr));
        label_6->setText(QCoreApplication::translate("ResizeProcess", "Interpolation", nullptr));
        currentSizeLbl->setText(QCoreApplication::translate("ResizeProcess", "Current Size:", nullptr));
        label_3->setText(QCoreApplication::translate("ResizeProcess", "Original Px", nullptr));
        label_4->setText(QCoreApplication::translate("ResizeProcess", "Target Px", nullptr));
        label_5->setText(QCoreApplication::translate("ResizeProcess", "% Adjust", nullptr));
        label->setText(QCoreApplication::translate("ResizeProcess", "Width", nullptr));
        newSizeLbl->setText(QCoreApplication::translate("ResizeProcess", "New Size:", nullptr));
        label_7->setText(QCoreApplication::translate("ResizeProcess", "Target Image ", nullptr));

        // Tooltips
        originalWidth->setToolTip(QCoreApplication::translate(
            "ResizeProcess", "Current width of the source image in pixels (read-only).", nullptr));
        originalHeight->setToolTip(QCoreApplication::translate(
            "ResizeProcess", "Current height of the source image in pixels (read-only).", nullptr));
        targetWidth->setToolTip(
            QCoreApplication::translate("ResizeProcess",
                                        "Desired width of the output image in pixels. "
                                        "When 'Keep aspect ratio' is enabled, changing this also updates the height.",
                                        nullptr));
        targetHeight->setToolTip(
            QCoreApplication::translate("ResizeProcess",
                                        "Desired height of the output image in pixels. "
                                        "When 'Keep aspect ratio' is enabled, changing this also updates the width.",
                                        nullptr));
        targetWidthPercent->setToolTip(
            QCoreApplication::translate("ResizeProcess",
                                        "Scale factor for the width expressed as a percentage of the original. "
                                        "100% keeps the original width; values below 100% shrink, above 100% enlarge.",
                                        nullptr));
        targetHeightPercent->setToolTip(
            QCoreApplication::translate("ResizeProcess",
                                        "Scale factor for the height expressed as a percentage of the original. "
                                        "100% keeps the original height; values below 100% shrink, above 100% enlarge.",
                                        nullptr));
        keepAspectRatio->setToolTip(
            QCoreApplication::translate("ResizeProcess",
                                        "When enabled, width and height are linked so the image "
                                        "retains its original proportions.",
                                        nullptr));
        interpolation->setToolTip(QCoreApplication::translate(
            "ResizeProcess",
            "<p style='margin:0;'>Resampling algorithm used to compute pixel values at the new resolution.</p>"
            "<p style='margin:4px 0 0 0;'>"
            "\u2022 Nearest Neighbor \u2014 fastest, may look blocky<br>"
            "\u2022 Bilinear \u2014 smooth, good general-purpose choice<br>"
            "\u2022 Bicubic \u2014 sharper edges than bilinear, slightly slower<br>"
            "\u2022 Pixel Area Relation \u2014 best for downscaling, averages source pixels<br>"
            "\u2022 Lanczos4 \u2014 highest quality, best for final output</p>",
            nullptr));
        currentSize->setToolTip(QCoreApplication::translate(
            "ResizeProcess", "Estimated memory footprint of the current image before resizing.", nullptr));
        newSize->setToolTip(QCoreApplication::translate(
            "ResizeProcess", "Estimated memory footprint of the image after resizing.", nullptr));
        targetImageSelector->setToolTip(QCoreApplication::translate("ResizeProcess",
                                                                    "Choose the open image to apply the resize to. "
                                                                    "Hover over each entry to see image details.",
                                                                    nullptr));

        // Tooltips — Dialog buttons
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("ResizeProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(
                QCoreApplication::translate("ResizeProcess", "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline ResizeProcessUiElements buildResizeProcessUi(QWidget* parent)
{
    ResizeProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(400, 310));
    parent->setMaximumSize(QSize(400, 310));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
