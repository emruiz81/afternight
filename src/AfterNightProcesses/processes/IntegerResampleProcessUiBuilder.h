// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

#include <limits>

namespace an::ui::processes
{

class IntegerResampleProcessUiElements
{
public:
    QGroupBox* groupBox_2;
    QLabel* label_5;
    QSpinBox* resampleFactor;
    QLabel* label_6;
    QComboBox* downsampleMode;
    QRadioButton* downSampleRadioButton;
    QRadioButton* UpsampleRadioButton;

    QGroupBox* groupBox;
    QLabel* label_3;
    QLabel* label_4;
    QLabel* label;
    QSpinBox* originalWidth;
    QSpinBox* targetWidth;
    QLabel* label_2;
    QSpinBox* originalHeight;
    QSpinBox* targetHeight;

    QLabel* currentSizeLbl;
    QLineEdit* currentSize;
    QLabel* newSizeLbl;
    QLineEdit* newSize;

    QLabel* label_7;
    QComboBox* targetImageSelector;

    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* IntegerResampleProcess)
    {
        if (IntegerResampleProcess->objectName().isEmpty())
            IntegerResampleProcess->setObjectName("IntegerResampleProcess");

        const auto& t = an::ui::tokens();

        auto* rootLayout = new QVBoxLayout(IntegerResampleProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        // Resample Group
        groupBox_2 = new QGroupBox(IntegerResampleProcess);
        groupBox_2->setObjectName("groupBox_2");
        auto* resampleLayout = new QVBoxLayout(groupBox_2);
        resampleLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        resampleLayout->setSpacing(t.spacing.md);

        auto* factorLayout = new QHBoxLayout();
        factorLayout->setSpacing(t.spacing.md);
        label_5 = new QLabel(groupBox_2);
        label_5->setObjectName("label_5");
        factorLayout->addWidget(label_5);

        resampleFactor = new QSpinBox(groupBox_2);
        resampleFactor->setObjectName("resampleFactor");
        resampleFactor->setMinimum(2);
        resampleFactor->setMaximum(100);
        resampleFactor->setMinimumWidth(80);
        factorLayout->addWidget(resampleFactor, 1);

        label_6 = new QLabel(groupBox_2);
        label_6->setObjectName("label_6");
        factorLayout->addWidget(label_6);

        downsampleMode = new QComboBox(groupBox_2);
        downsampleMode->setObjectName("downsampleMode");
        downsampleMode->setMinimumWidth(150);
        factorLayout->addWidget(downsampleMode, 2);

        resampleLayout->addLayout(factorLayout);

        downSampleRadioButton = new QRadioButton(groupBox_2);
        downSampleRadioButton->setObjectName("downSampleRadioButton");
        downSampleRadioButton->setChecked(true);
        resampleLayout->addWidget(downSampleRadioButton);

        resampleLayout->addSpacing(t.spacing.xs);

        UpsampleRadioButton = new QRadioButton(groupBox_2);
        UpsampleRadioButton->setObjectName("UpsampleRadioButton");
        resampleLayout->addWidget(UpsampleRadioButton);

        rootLayout->addWidget(groupBox_2);

        // New Size Group
        groupBox = new QGroupBox(IntegerResampleProcess);
        groupBox->setObjectName("groupBox");
        auto* sizeLayout = new QGridLayout(groupBox);
        sizeLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        sizeLayout->setHorizontalSpacing(t.spacing.md);
        sizeLayout->setVerticalSpacing(t.spacing.sm);

        label_3 = new QLabel(groupBox);
        label_3->setObjectName("label_3");
        label_3->setAlignment(Qt::AlignCenter);
        sizeLayout->addWidget(label_3, 0, 1);

        label_4 = new QLabel(groupBox);
        label_4->setObjectName("label_4");
        label_4->setAlignment(Qt::AlignCenter);
        sizeLayout->addWidget(label_4, 0, 2);

        label = new QLabel(groupBox);
        label->setObjectName("label");
        sizeLayout->addWidget(label, 1, 0);

        originalWidth = new QSpinBox(groupBox);
        originalWidth->setObjectName("originalWidth");
        originalWidth->setReadOnly(true);
        originalWidth->setMinimum(1);
        originalWidth->setMaximum(std::numeric_limits<int>::max());
        originalWidth->setMinimumWidth(80);
        sizeLayout->addWidget(originalWidth, 1, 1);

        targetWidth = new QSpinBox(groupBox);
        targetWidth->setObjectName("targetWidth");
        targetWidth->setReadOnly(true);
        targetWidth->setMinimum(1);
        targetWidth->setMaximum(std::numeric_limits<int>::max());
        targetWidth->setMinimumWidth(80);
        sizeLayout->addWidget(targetWidth, 1, 2);

        label_2 = new QLabel(groupBox);
        label_2->setObjectName("label_2");
        sizeLayout->addWidget(label_2, 2, 0);

        originalHeight = new QSpinBox(groupBox);
        originalHeight->setObjectName("originalHeight");
        originalHeight->setReadOnly(true);
        originalHeight->setMinimum(1);
        originalHeight->setMaximum(std::numeric_limits<int>::max());
        originalHeight->setMinimumWidth(80);
        sizeLayout->addWidget(originalHeight, 2, 1);

        targetHeight = new QSpinBox(groupBox);
        targetHeight->setObjectName("targetHeight");
        targetHeight->setReadOnly(true);
        targetHeight->setMinimum(1);
        targetHeight->setMaximum(std::numeric_limits<int>::max());
        targetHeight->setMinimumWidth(80);
        sizeLayout->addWidget(targetHeight, 2, 2);

        auto* memSizeLayout = new QHBoxLayout();
        memSizeLayout->setContentsMargins(0, t.spacing.sm, 0, 0);
        memSizeLayout->setSpacing(t.spacing.md);
        currentSizeLbl = new QLabel(groupBox);
        currentSizeLbl->setObjectName("currentSizeLbl");
        memSizeLayout->addWidget(currentSizeLbl);

        currentSize = new QLineEdit(groupBox);
        currentSize->setObjectName("currentSize");
        currentSize->setReadOnly(true);
        currentSize->setMinimumWidth(100);
        memSizeLayout->addWidget(currentSize);

        memSizeLayout->addStretch();

        newSizeLbl = new QLabel(groupBox);
        newSizeLbl->setObjectName("newSizeLbl");
        memSizeLayout->addWidget(newSizeLbl);

        newSize = new QLineEdit(groupBox);
        newSize->setObjectName("newSize");
        newSize->setReadOnly(true);
        newSize->setMinimumWidth(100);
        memSizeLayout->addWidget(newSize);

        sizeLayout->addLayout(memSizeLayout, 3, 0, 1, 3);

        rootLayout->addWidget(groupBox);

        // Target Image
        auto* targetLayout = new QHBoxLayout();
        targetLayout->setSpacing(t.spacing.md);
        label_7 = new QLabel(IntegerResampleProcess);
        label_7->setObjectName("label_7");
        targetLayout->addWidget(label_7);

        targetImageSelector = new QComboBox(IntegerResampleProcess);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetLayout->addWidget(targetImageSelector, 1);

        rootLayout->addLayout(targetLayout);

        // Button Box
        buttonBox = new QDialogButtonBox(IntegerResampleProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(IntegerResampleProcess);

        QMetaObject::connectSlotsByName(IntegerResampleProcess);
    } // setupUi

    void retranslateUi(QWidget* IntegerResampleProcess)
    {
        IntegerResampleProcess->setWindowTitle(
            QCoreApplication::translate("IntegerResampleProcess", "Integer Resample", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("IntegerResampleProcess", "Resample", nullptr));
        downSampleRadioButton->setText(QCoreApplication::translate("IntegerResampleProcess", "Downsample", nullptr));
        UpsampleRadioButton->setText(QCoreApplication::translate("IntegerResampleProcess", "Upsample", nullptr));
        label_5->setText(QCoreApplication::translate("IntegerResampleProcess", "Resample Factor", nullptr));
        label_6->setText(QCoreApplication::translate("IntegerResampleProcess", "Downsample Mode", nullptr));
        groupBox->setTitle(QCoreApplication::translate("IntegerResampleProcess", "New Size", nullptr));
        label_3->setText(QCoreApplication::translate("IntegerResampleProcess", "Original Px", nullptr));
        label_2->setText(QCoreApplication::translate("IntegerResampleProcess", "Height", nullptr));
        label->setText(QCoreApplication::translate("IntegerResampleProcess", "Width", nullptr));
        label_4->setText(QCoreApplication::translate("IntegerResampleProcess", "Target Px", nullptr));
        currentSizeLbl->setText(QCoreApplication::translate("IntegerResampleProcess", "Current Size:", nullptr));
        newSizeLbl->setText(QCoreApplication::translate("IntegerResampleProcess", "New Size:", nullptr));
        label_7->setText(QCoreApplication::translate("IntegerResampleProcess", "Target Image", nullptr));

        // Tooltips — Resample group
        resampleFactor->setToolTip(
            QCoreApplication::translate("IntegerResampleProcess",
                                        "Integer multiplier for resampling. In downsample mode the image "
                                        "dimensions are divided by this factor; in upsample mode they are "
                                        "multiplied by it. Only whole-number factors are allowed.",
                                        nullptr));
        downsampleMode->setToolTip(QCoreApplication::translate(
            "IntegerResampleProcess",
            "<p style='margin:0;'>Pixel combination strategy used when downsampling.</p>"
            "<p style='margin:4px 0 0 0;'>"
            "\u2022 Average \u2014 averages the pixel block (smooth result)<br>"
            "\u2022 Median \u2014 takes the median value (reduces outlier influence)<br>"
            "\u2022 Max \u2014 keeps the brightest pixel in each block<br>"
            "\u2022 Min \u2014 keeps the faintest pixel in each block</p>"
            "<p style='margin:4px 0 0 0;'>Only applies when \"Downsample\" is selected.</p>",
            nullptr));
        downSampleRadioButton->setToolTip(
            QCoreApplication::translate("IntegerResampleProcess",
                                        "Reduce the image dimensions by the resample factor. "
                                        "Useful for creating smaller preview images or reducing noise by binning.",
                                        nullptr));
        UpsampleRadioButton->setToolTip(
            QCoreApplication::translate("IntegerResampleProcess",
                                        "Enlarge the image dimensions by the resample factor. "
                                        "Each pixel is replicated into an N\u00d7N block (no interpolation).",
                                        nullptr));

        // Tooltips — New Size group
        originalWidth->setToolTip(QCoreApplication::translate(
            "IntegerResampleProcess", "Current width of the source image in pixels (read-only).", nullptr));
        originalHeight->setToolTip(QCoreApplication::translate(
            "IntegerResampleProcess", "Current height of the source image in pixels (read-only).", nullptr));
        targetWidth->setToolTip(QCoreApplication::translate(
            "IntegerResampleProcess", "Resulting width after resampling (read-only, computed from factor).", nullptr));
        targetHeight->setToolTip(QCoreApplication::translate(
            "IntegerResampleProcess", "Resulting height after resampling (read-only, computed from factor).", nullptr));
        currentSize->setToolTip(QCoreApplication::translate(
            "IntegerResampleProcess", "Estimated memory footprint of the current image before resampling.", nullptr));
        newSize->setToolTip(QCoreApplication::translate(
            "IntegerResampleProcess", "Estimated memory footprint of the image after resampling.", nullptr));

        // Tooltips — Target image
        targetImageSelector->setToolTip(QCoreApplication::translate("IntegerResampleProcess",
                                                                    "Choose the open image to resample. "
                                                                    "Hover over each entry to see image details.",
                                                                    nullptr));

        // Tooltips — Dialog buttons
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("IntegerResampleProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("IntegerResampleProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline IntegerResampleProcessUiElements buildIntegerResampleProcessUi(QWidget* parent)
{
    IntegerResampleProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(400, 390));
    parent->setMaximumSize(QSize(400, 390));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
