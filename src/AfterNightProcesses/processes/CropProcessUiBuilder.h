// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class CropProcessUiElements
{
public:
    QPushButton* newSelectionButton;
    QDialogButtonBox* buttonBox;
    QWidget* layoutWidget1;
    QFormLayout* formLayout;
    QLabel* label_3;
    QSpinBox* targetPosX;
    QLabel* label_4;
    QSpinBox* targetPosY;
    QLabel* label;
    QSpinBox* targetWidth;
    QLabel* label_2;
    QSpinBox* targetHeight;
    QWidget* layoutWidget2;
    QFormLayout* formLayout_2;
    QLabel* currentSizeLbl;
    QLineEdit* currentSize;
    QLabel* newSizeLbl;
    QLineEdit* newSize;
    QWidget* layoutWidget;
    QHBoxLayout* horizontalLayout;
    QLabel* label_5;
    QComboBox* targetImageSelector;

    void setupUi(QWidget* CropProcess)
    {
        if (CropProcess->objectName().isEmpty())
            CropProcess->setObjectName("CropProcess");

        const auto& t = an::ui::tokens();

        auto* rootLayout = new QVBoxLayout(CropProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        auto* topLayout = new QHBoxLayout();
        topLayout->setObjectName("topLayout");
        topLayout->setSpacing(t.spacing.md);

        layoutWidget1 = new QWidget(CropProcess);
        layoutWidget1->setObjectName("layoutWidget1");
        formLayout = new QFormLayout(layoutWidget1);
        formLayout->setObjectName("formLayout");
        formLayout->setContentsMargins(0, 0, 0, 0);
        formLayout->setHorizontalSpacing(t.spacing.md);
        formLayout->setVerticalSpacing(t.spacing.sm);
        label_3 = new QLabel(layoutWidget1);
        label_3->setObjectName("label_3");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label_3);

        targetPosX = new QSpinBox(layoutWidget1);
        targetPosX->setObjectName("targetPosX");
        targetPosX->setMaximum(100000);
        targetPosX->setMinimumWidth(80);

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, targetPosX);

        label_4 = new QLabel(layoutWidget1);
        label_4->setObjectName("label_4");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_4);

        targetPosY = new QSpinBox(layoutWidget1);
        targetPosY->setObjectName("targetPosY");
        targetPosY->setMaximum(100000);
        targetPosY->setMinimumWidth(80);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, targetPosY);

        label = new QLabel(layoutWidget1);
        label->setObjectName("label");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, label);

        targetWidth = new QSpinBox(layoutWidget1);
        targetWidth->setObjectName("targetWidth");
        targetWidth->setMaximum(100000);
        targetWidth->setMinimumWidth(80);

        formLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, targetWidth);

        label_2 = new QLabel(layoutWidget1);
        label_2->setObjectName("label_2");

        formLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, label_2);

        targetHeight = new QSpinBox(layoutWidget1);
        targetHeight->setObjectName("targetHeight");
        targetHeight->setMaximum(100000);
        targetHeight->setMinimumWidth(80);

        formLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, targetHeight);
        topLayout->addWidget(layoutWidget1, 0);

        layoutWidget2 = new QWidget(CropProcess);
        layoutWidget2->setObjectName("layoutWidget2");
        auto* rightColumnLayout = new QVBoxLayout(layoutWidget2);
        rightColumnLayout->setObjectName("rightColumnLayout");
        rightColumnLayout->setContentsMargins(0, 0, 0, 0);
        rightColumnLayout->setSpacing(t.spacing.sm);

        formLayout_2 = new QFormLayout();
        formLayout_2->setObjectName("formLayout_2");
        formLayout_2->setContentsMargins(0, 0, 0, 0);
        formLayout_2->setHorizontalSpacing(t.spacing.sm);
        formLayout_2->setVerticalSpacing(t.spacing.sm);
        currentSizeLbl = new QLabel(layoutWidget2);
        currentSizeLbl->setObjectName("currentSizeLbl");

        formLayout_2->setWidget(0, QFormLayout::ItemRole::LabelRole, currentSizeLbl);

        currentSize = new QLineEdit(layoutWidget2);
        currentSize->setObjectName("currentSize");
        currentSize->setReadOnly(true);
        currentSize->setMinimumWidth(100);

        formLayout_2->setWidget(0, QFormLayout::ItemRole::FieldRole, currentSize);

        newSizeLbl = new QLabel(layoutWidget2);
        newSizeLbl->setObjectName("newSizeLbl");

        formLayout_2->setWidget(1, QFormLayout::ItemRole::LabelRole, newSizeLbl);

        newSize = new QLineEdit(layoutWidget2);
        newSize->setObjectName("newSize");
        newSize->setReadOnly(true);
        newSize->setMinimumWidth(100);

        formLayout_2->setWidget(1, QFormLayout::ItemRole::FieldRole, newSize);

        newSelectionButton = new QPushButton(layoutWidget2);
        newSelectionButton->setObjectName("newSelectionButton");
        newSelectionButton->setCheckable(true);
        // give the button more vertical real estate so it stands out in the dialog
        newSelectionButton->setMinimumHeight(32);
        // ensure the size policy allows the height to be respected
        newSelectionButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        formLayout_2->setWidget(2, QFormLayout::ItemRole::FieldRole, newSelectionButton);

        rightColumnLayout->addLayout(formLayout_2);
        rightColumnLayout->addStretch(1);
        topLayout->addWidget(layoutWidget2, 1);

        rootLayout->addLayout(topLayout);

        layoutWidget = new QWidget(CropProcess);
        layoutWidget->setObjectName("layoutWidget");
        horizontalLayout = new QHBoxLayout(layoutWidget);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout->setSpacing(t.spacing.sm);
        label_5 = new QLabel(layoutWidget);
        label_5->setObjectName("label_5");

        horizontalLayout->addWidget(label_5);

        targetImageSelector = new QComboBox(layoutWidget);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout->addWidget(targetImageSelector);

        horizontalLayout->setStretch(1, 1);
        rootLayout->addWidget(layoutWidget);

        buttonBox = new QDialogButtonBox(CropProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(CropProcess);

        QMetaObject::connectSlotsByName(CropProcess);
    } // setupUi

    void retranslateUi(QWidget* CropProcess)
    {
        CropProcess->setWindowTitle(QCoreApplication::translate("CropProcess", "Crop", nullptr));
        newSelectionButton->setText(QCoreApplication::translate("CropProcess", "New Selection", nullptr));
        label_3->setText(QCoreApplication::translate("CropProcess", "Pos X", nullptr));
        label_4->setText(QCoreApplication::translate("CropProcess", "Pos Y", nullptr));
        label->setText(QCoreApplication::translate("CropProcess", "Width", nullptr));
        label_2->setText(QCoreApplication::translate("CropProcess", "Height", nullptr));
        currentSizeLbl->setText(QCoreApplication::translate("CropProcess", "Current Size:", nullptr));
        newSizeLbl->setText(QCoreApplication::translate("CropProcess", "New Size:", nullptr));
        label_5->setText(QCoreApplication::translate("CropProcess", "Target Image ", nullptr));

        // Tooltips
        targetPosX->setToolTip(
            QCoreApplication::translate("CropProcess",
                                        "Horizontal offset (in pixels) of the top-left corner of the crop region, "
                                        "measured from the left edge of the original image.",
                                        nullptr));
        targetPosY->setToolTip(
            QCoreApplication::translate("CropProcess",
                                        "Vertical offset (in pixels) of the top-left corner of the crop region, "
                                        "measured from the top edge of the original image.",
                                        nullptr));
        targetWidth->setToolTip(QCoreApplication::translate("CropProcess",
                                                            "Width (in pixels) of the crop region. "
                                                            "The resulting image will have this width.",
                                                            nullptr));
        targetHeight->setToolTip(QCoreApplication::translate("CropProcess",
                                                             "Height (in pixels) of the crop region. "
                                                             "The resulting image will have this height.",
                                                             nullptr));
        currentSize->setToolTip(QCoreApplication::translate(
            "CropProcess", "Estimated memory footprint of the current image before cropping.", nullptr));
        newSize->setToolTip(QCoreApplication::translate(
            "CropProcess", "Estimated memory footprint of the image after cropping.", nullptr));
        newSelectionButton->setToolTip(
            QCoreApplication::translate("CropProcess",
                                        "Click and drag on the image preview to interactively define "
                                        "a new rectangular crop region.",
                                        nullptr));
        targetImageSelector->setToolTip(QCoreApplication::translate("CropProcess",
                                                                    "Choose the open image to apply the crop to. "
                                                                    "Hover over each entry to see image details.",
                                                                    nullptr));

        // Tooltips — Dialog buttons
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(
                QCoreApplication::translate("CropProcess", "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(
                QCoreApplication::translate("CropProcess", "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline CropProcessUiElements buildCropProcessUi(QWidget* parent)
{
    CropProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(360, 220));
    parent->setMaximumSize(QSize(360, 220));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
