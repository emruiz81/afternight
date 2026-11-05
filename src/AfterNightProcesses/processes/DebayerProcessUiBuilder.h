// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class DebayerProcessUiElements
{
public:
    QDialogButtonBox* buttonBox;
    QComboBox* demosaicingMethod;
    QLabel* label_3;
    QComboBox* outputMode;
    QLabel* label;
    QLabel* label_2;
    QComboBox* bayerPattern;
    QComboBox* targetImageSelector;
    QLabel* label_7;

    void setupUi(QWidget* DebayerProcess)
    {
        if (DebayerProcess->objectName().isEmpty())
            DebayerProcess->setObjectName("DebayerProcess");
        DebayerProcess->resize(400, 200);
        DebayerProcess->setMinimumSize(QSize(400, 200));

        const auto& t = an::ui::tokens();

        auto* rootLayout = new QVBoxLayout(DebayerProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        auto* formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        formLayout->setHorizontalSpacing(t.spacing.md);
        formLayout->setVerticalSpacing(t.spacing.sm);

        label = new QLabel(DebayerProcess);
        label->setObjectName("label");
        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        bayerPattern = new QComboBox(DebayerProcess);
        bayerPattern->setObjectName("bayerPattern");
        bayerPattern->setMinimumWidth(150);
        bayerPattern->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        formLayout->setWidget(0, QFormLayout::FieldRole, bayerPattern);

        label_2 = new QLabel(DebayerProcess);
        label_2->setObjectName("label_2");
        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        demosaicingMethod = new QComboBox(DebayerProcess);
        demosaicingMethod->setObjectName("demosaicingMethod");
        demosaicingMethod->setMinimumWidth(150);
        demosaicingMethod->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        formLayout->setWidget(1, QFormLayout::FieldRole, demosaicingMethod);

        label_3 = new QLabel(DebayerProcess);
        label_3->setObjectName("label_3");
        formLayout->setWidget(2, QFormLayout::LabelRole, label_3);

        outputMode = new QComboBox(DebayerProcess);
        outputMode->setObjectName("outputMode");
        outputMode->setMinimumWidth(150);
        outputMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        formLayout->setWidget(2, QFormLayout::FieldRole, outputMode);

        label_7 = new QLabel(DebayerProcess);
        label_7->setObjectName("label_7");
        formLayout->setWidget(3, QFormLayout::LabelRole, label_7);

        targetImageSelector = new QComboBox(DebayerProcess);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        formLayout->setWidget(3, QFormLayout::FieldRole, targetImageSelector);

        rootLayout->addLayout(formLayout);
        rootLayout->addItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

        buttonBox = new QDialogButtonBox(DebayerProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(DebayerProcess);

        retranslateUi(DebayerProcess);

        DebayerProcess->adjustSize();
    } // setupUi

    void retranslateUi(QWidget* DebayerProcess)
    {
        DebayerProcess->setWindowTitle(QCoreApplication::translate("DebayerProcess", "Debayer", nullptr));
        label_3->setText(QCoreApplication::translate("DebayerProcess", "Output mode", nullptr));
        label->setText(QCoreApplication::translate("DebayerProcess", "Bayer/mosaic pattern", nullptr));
        label_2->setText(QCoreApplication::translate("DebayerProcess", "Demosaicing method", nullptr));
        label_7->setText(QCoreApplication::translate("DebayerProcess", "Target Image ", nullptr));

        bayerPattern->setToolTip(QCoreApplication::translate(
            "DebayerProcess",
            "From metadata demosaics under the filter array the image states, and refuses an image that states none.",
            nullptr));
        demosaicingMethod->setToolTip(QCoreApplication::translate(
            "DebayerProcess", "Choose the demosaicing algorithm used to reconstruct RGB data.", nullptr));
        outputMode->setToolTip(QCoreApplication::translate(
            "DebayerProcess", "Select whether to generate RGB output, separated channels, or both.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("DebayerProcess", "Choose the open image or sequence to process.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("DebayerProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Ok))
            btn->setToolTip(
                QCoreApplication::translate("DebayerProcess", "Run debayer with the current settings.", nullptr));
    } // retranslateUi
};

inline DebayerProcessUiElements buildDebayerProcessUi(QWidget* parent)
{
    DebayerProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
