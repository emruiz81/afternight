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
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class BinarizeProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QLabel* rgbkLabel;
    QDoubleSpinBox* rgbkValue;
    QSlider* rgbkSlider;
    QHBoxLayout* horizontalLayout_2;
    QLabel* label_2;
    QDoubleSpinBox* greenValue;
    QSlider* greenSlider;
    QHBoxLayout* horizontalLayout_5;
    QLabel* label_4;
    QDoubleSpinBox* blueValue;
    QSlider* blueSlider;
    QHBoxLayout* horizontalLayout_6;
    QLabel* label;
    QComboBox* selectionMode;
    QSpacerItem* horizontalSpacer_2;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QLabel* label_3;
    QComboBox* targetImageSelector;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* BinarizeProcess)
    {
        if (BinarizeProcess->objectName().isEmpty())
            BinarizeProcess->setObjectName("BinarizeProcess");
        const auto& t = an::ui::tokens();
        BinarizeProcess->resize(898, 547);
        BinarizeProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(BinarizeProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(BinarizeProcess);
        horizontalLayoutWidget_4->setObjectName("horizontalLayoutWidget_4");
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SizeConstraint::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->addWidget(horizontalLayoutWidget_4);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SizeConstraint::SetFixedSize);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        rgbkLabel = new QLabel(horizontalLayoutWidget_4);
        rgbkLabel->setObjectName("rgbkLabel");
        rgbkLabel->setMinimumSize(QSize(50, 0));

        horizontalLayout->addWidget(rgbkLabel);

        rgbkValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        rgbkValue->setObjectName("rgbkValue");
        rgbkValue->setMinimumWidth(95);
        rgbkValue->setDecimals(6);
        rgbkValue->setMaximum(1.000000000000000);
        rgbkValue->setSingleStep(0.050000000000000);
        rgbkValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        rgbkValue->setValue(0.500000000000000);

        horizontalLayout->addWidget(rgbkValue);

        rgbkSlider = new QSlider(horizontalLayoutWidget_4);
        rgbkSlider->setObjectName("rgbkSlider");
        rgbkSlider->setMinimumSize(QSize(200, 0));
        rgbkSlider->setMaximum(1000);
        rgbkSlider->setValue(500);
        rgbkSlider->setOrientation(Qt::Orientation::Horizontal);

        horizontalLayout->addWidget(rgbkSlider);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");
        label_2->setMinimumSize(QSize(50, 0));

        horizontalLayout_2->addWidget(label_2);

        greenValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        greenValue->setObjectName("greenValue");
        greenValue->setEnabled(false);
        greenValue->setMinimumWidth(95);
        greenValue->setDecimals(6);
        greenValue->setMaximum(1.000000000000000);
        greenValue->setSingleStep(0.050000000000000);
        greenValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        greenValue->setValue(0.500000000000000);

        horizontalLayout_2->addWidget(greenValue);

        greenSlider = new QSlider(horizontalLayoutWidget_4);
        greenSlider->setObjectName("greenSlider");
        greenSlider->setEnabled(false);
        greenSlider->setMaximum(1000);
        greenSlider->setValue(500);
        greenSlider->setOrientation(Qt::Orientation::Horizontal);

        horizontalLayout_2->addWidget(greenSlider);

        verticalLayout->addLayout(horizontalLayout_2);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalLayout_5->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label_4 = new QLabel(horizontalLayoutWidget_4);
        label_4->setObjectName("label_4");
        label_4->setMinimumSize(QSize(50, 0));

        horizontalLayout_5->addWidget(label_4);

        blueValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        blueValue->setObjectName("blueValue");
        blueValue->setEnabled(false);
        blueValue->setMinimumWidth(95);
        blueValue->setDecimals(6);
        blueValue->setMaximum(1.000000000000000);
        blueValue->setSingleStep(0.050000000000000);
        blueValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        blueValue->setValue(0.500000000000000);

        horizontalLayout_5->addWidget(blueValue);

        blueSlider = new QSlider(horizontalLayoutWidget_4);
        blueSlider->setObjectName("blueSlider");
        blueSlider->setEnabled(false);
        blueSlider->setMaximum(1000);
        blueSlider->setValue(500);
        blueSlider->setOrientation(Qt::Orientation::Horizontal);

        horizontalLayout_5->addWidget(blueSlider);

        verticalLayout->addLayout(horizontalLayout_5);

        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        horizontalLayout_6->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");
        label->setMinimumSize(QSize(50, 0));

        horizontalLayout_6->addWidget(label);

        selectionMode = new QComboBox(horizontalLayoutWidget_4);
        selectionMode->addItem(QString());
        selectionMode->addItem(QString());
        selectionMode->setObjectName("selectionMode");
        selectionMode->setMinimumWidth(150);
        selectionMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout_6->addWidget(selectionMode);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_2);

        horizontalLayout_6->setStretch(1, 1);

        verticalLayout->addLayout(horizontalLayout_6);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        label_3 = new QLabel(horizontalLayoutWidget_4);
        label_3->setObjectName("label_3");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label_3);

        targetImageSelector = new QComboBox(horizontalLayoutWidget_4);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, targetImageSelector);

        verticalLayout->addLayout(formLayout);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        preview = new QCheckBox(horizontalLayoutWidget_4);
        preview->setObjectName("preview");
        preview->setChecked(true);

        horizontalLayout_4->addWidget(preview);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer);

        verticalLayout->addLayout(horizontalLayout_4);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        horizontalLayout_3->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        buttonBox = new QDialogButtonBox(horizontalLayoutWidget_4);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Apply |
                                      QDialogButtonBox::StandardButton::Close |
                                      QDialogButtonBox::StandardButton::Reset);

        horizontalLayout_3->addWidget(buttonBox);

        verticalLayout->addLayout(horizontalLayout_3);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, horizontalLayoutWidget_4);
        split.controlsLayout->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");

        verticalLayout_2->addWidget(widget);

        split.previewLayout->addLayout(verticalLayout_2);

        retranslateUi(BinarizeProcess);

        QMetaObject::connectSlotsByName(BinarizeProcess);
    } // setupUi

    void retranslateUi(QWidget* BinarizeProcess)
    {
        BinarizeProcess->setWindowTitle(QCoreApplication::translate("BinarizeProcess", "Binarize", nullptr));
        rgbkLabel->setText(QCoreApplication::translate("BinarizeProcess", "RGB/K", nullptr));
        label_2->setText(QCoreApplication::translate("BinarizeProcess", "Green", nullptr));
        label_4->setText(QCoreApplication::translate("BinarizeProcess", "Blue", nullptr));
        label->setText(QCoreApplication::translate("BinarizeProcess", "Selection", nullptr));
        selectionMode->setItemText(0, QCoreApplication::translate("BinarizeProcess", "Joint RGB/K channels", nullptr));
        selectionMode->setItemText(
            1, QCoreApplication::translate("BinarizeProcess", "Individual RGB/K channels", nullptr));

        label_3->setText(QCoreApplication::translate("BinarizeProcess", "Target Image ", nullptr));
        preview->setText(QCoreApplication::translate("BinarizeProcess", "Preview", nullptr));

        rgbkValue->setToolTip(
            QCoreApplication::translate("BinarizeProcess", "Set the threshold for the RGB/K channel.", nullptr));
        greenValue->setToolTip(QCoreApplication::translate(
            "BinarizeProcess", "Set the threshold for the green channel when individual mode is enabled.", nullptr));
        blueValue->setToolTip(QCoreApplication::translate(
            "BinarizeProcess", "Set the threshold for the blue channel when individual mode is enabled.", nullptr));
        selectionMode->setToolTip(QCoreApplication::translate(
            "BinarizeProcess", "Select whether thresholds are applied jointly or per channel.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("BinarizeProcess", "Choose the open image or sequence to process.", nullptr));
        preview->setToolTip(QCoreApplication::translate("BinarizeProcess",
                                                        "Enable real-time preview while editing parameters.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Apply))
            btn->setToolTip(QCoreApplication::translate("BinarizeProcess",
                                                        "Apply binarization with the current settings.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("BinarizeProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("BinarizeProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline BinarizeProcessUiElements buildBinarizeProcessUi(QWidget* parent)
{
    BinarizeProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
