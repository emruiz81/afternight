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
#include <QtWidgets/QLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class SCNRProcessUiElements
{
public:
    QCheckBox* preserveLightness;
    QComboBox* protectionType;
    QSlider* amountSlider;
    QDialogButtonBox* buttonBox;
    QDoubleSpinBox* amountValue;
    QLabel* label;
    QLabel* label_2;
    QLabel* label_7;
    QComboBox* targetImageSelector;

    void setupUi(QWidget* SCNRProcess)
    {
        const auto& t = an::ui::tokens();

        if (SCNRProcess->objectName().isEmpty())
            SCNRProcess->setObjectName("SCNRProcess");
        auto* rootLayout = new QVBoxLayout(SCNRProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        auto* formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        formLayout->setHorizontalSpacing(t.spacing.md);
        formLayout->setVerticalSpacing(t.spacing.sm);

        label = new QLabel(SCNRProcess);
        label->setObjectName("label");
        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        protectionType = new QComboBox(SCNRProcess);
        protectionType->setObjectName("protectionType");
        protectionType->setMinimumWidth(150);
        protectionType->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        formLayout->setWidget(0, QFormLayout::FieldRole, protectionType);

        label_2 = new QLabel(SCNRProcess);
        label_2->setObjectName("label_2");
        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        auto* amountLayout = new QHBoxLayout();
        amountLayout->setSpacing(t.spacing.sm);

        amountValue = new QDoubleSpinBox(SCNRProcess);
        amountValue->setObjectName("amountValue");
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.050000000000000);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);
        amountLayout->addWidget(amountValue);

        amountSlider = new QSlider(SCNRProcess);
        amountSlider->setObjectName("amountSlider");
        // The window binds the slider to the amount spin box through QSliderDoubleSpinBox, which spans the spin box's
        // range in a thousand steps: the full amount of 1 is the slider's end.
        amountSlider->setRange(0, 1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Horizontal);
        amountSlider->setMinimumWidth(160);
        amountLayout->addWidget(amountSlider);

        formLayout->setLayout(1, QFormLayout::FieldRole, amountLayout);

        label_7 = new QLabel(SCNRProcess);
        label_7->setObjectName("label_7");
        formLayout->setWidget(2, QFormLayout::LabelRole, label_7);

        targetImageSelector = new QComboBox(SCNRProcess);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        formLayout->setWidget(2, QFormLayout::FieldRole, targetImageSelector);

        rootLayout->addLayout(formLayout);

        preserveLightness = new QCheckBox(SCNRProcess);
        preserveLightness->setObjectName("preserveLightness");
        preserveLightness->setChecked(true);
        rootLayout->addWidget(preserveLightness);

        rootLayout->addItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

        buttonBox = new QDialogButtonBox(SCNRProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(SCNRProcess);

        retranslateUi(SCNRProcess);

        QMetaObject::connectSlotsByName(SCNRProcess);
    } // setupUi

    void retranslateUi(QWidget* SCNRProcess)
    {
        SCNRProcess->setWindowTitle(QCoreApplication::translate("SCNRProcess", "SCNR", nullptr));
        preserveLightness->setText(QCoreApplication::translate("SCNRProcess", "Preserve Lightness", nullptr));
        label->setText(QCoreApplication::translate("SCNRProcess", "Protection Type", nullptr));
        label_2->setText(QCoreApplication::translate("SCNRProcess", "Amount", nullptr));
        label_7->setText(QCoreApplication::translate("SCNRProcess", "Target Image ", nullptr));

        protectionType->setToolTip(QCoreApplication::translate(
            "SCNRProcess",
            "Select the SCNR protection model used to suppress the selected cast while preserving image structure.",
            nullptr));
        amountValue->setToolTip(QCoreApplication::translate(
            "SCNRProcess", "Strength of the SCNR correction. Higher values remove more of the selected cast.",
            nullptr));
        amountSlider->setToolTip(QCoreApplication::translate(
            "SCNRProcess", "Strength of the SCNR correction. Higher values remove more of the selected cast.",
            nullptr));
        preserveLightness->setToolTip(QCoreApplication::translate(
            "SCNRProcess", "Keep luminance as stable as possible while applying chromatic cast reduction.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("SCNRProcess", "Choose the open image to process.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(
                QCoreApplication::translate("SCNRProcess", "Close this dialog without applying any changes.", nullptr));
    } // retranslateUi
};

inline SCNRProcessUiElements buildSCNRProcessUi(QWidget* parent)
{
    SCNRProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(360, 190));
    parent->setMaximumSize(QSize(360, 190));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
