// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class SplitCFAProcessUiElements
{
public:
    QDialogButtonBox* buttonBox;
    QWidget* layoutWidget1;
    QGridLayout* gridLayout;
    QLabel* label;
    QComboBox* methodSelector;
    QLabel* label_2;
    QComboBox* haOIIIResamplingSelector;
    QWidget* layoutWidget2;
    QHBoxLayout* horizontalLayout;
    QLabel* label_7;
    QComboBox* targetImageSelector;

    void setupUi(QWidget* SplitCFAProcess)
    {
        if (SplitCFAProcess->objectName().isEmpty())
            SplitCFAProcess->setObjectName("SplitCFAProcess");
        SplitCFAProcess->resize(410, 165);

        const auto& t = an::ui::tokens();

        auto* rootLayout = new QVBoxLayout(SplitCFAProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        layoutWidget1 = new QWidget(SplitCFAProcess);
        layoutWidget1->setObjectName("layoutWidget1");
        gridLayout = new QGridLayout(layoutWidget1);
        gridLayout->setObjectName("gridLayout");
        gridLayout->setContentsMargins(0, 0, 0, 0);
        gridLayout->setHorizontalSpacing(t.spacing.md);
        gridLayout->setVerticalSpacing(t.spacing.sm);
        label = new QLabel(layoutWidget1);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        methodSelector = new QComboBox(layoutWidget1);
        methodSelector->setObjectName("methodSelector");
        methodSelector->setMinimumWidth(150);
        methodSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        gridLayout->addWidget(methodSelector, 0, 1, 1, 1);

        label_2 = new QLabel(layoutWidget1);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        haOIIIResamplingSelector = new QComboBox(layoutWidget1);
        haOIIIResamplingSelector->setObjectName("haOIIIResamplingSelector");
        haOIIIResamplingSelector->setMinimumWidth(150);
        haOIIIResamplingSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        gridLayout->addWidget(haOIIIResamplingSelector, 1, 1, 1, 1);

        gridLayout->setColumnStretch(0, 4);
        gridLayout->setColumnStretch(1, 5);
        rootLayout->addWidget(layoutWidget1);

        layoutWidget2 = new QWidget(SplitCFAProcess);
        layoutWidget2->setObjectName("layoutWidget2");
        horizontalLayout = new QHBoxLayout(layoutWidget2);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout->setSpacing(t.spacing.md);
        label_7 = new QLabel(layoutWidget2);
        label_7->setObjectName("label_7");

        horizontalLayout->addWidget(label_7);

        targetImageSelector = new QComboBox(layoutWidget2);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout->addWidget(targetImageSelector);

        horizontalLayout->setStretch(1, 1);
        rootLayout->addWidget(layoutWidget2);

        rootLayout->addItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

        buttonBox = new QDialogButtonBox(SplitCFAProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(SplitCFAProcess);

        retranslateUi(SplitCFAProcess);

        QMetaObject::connectSlotsByName(SplitCFAProcess);

        SplitCFAProcess->adjustSize();
        SplitCFAProcess->setFixedSize(SplitCFAProcess->sizeHint());
    } // setupUi

    void retranslateUi(QWidget* SplitCFAProcess)
    {
        label->setText(QCoreApplication::translate("SplitCFAProcess", "Method", nullptr));
        label_2->setText(QCoreApplication::translate("SplitCFAProcess", "Ha/OIII Resampling", nullptr));
        label_7->setText(QCoreApplication::translate("SplitCFAProcess", "Target Image ", nullptr));

        methodSelector->setToolTip(QCoreApplication::translate(
            "SplitCFAProcess", "Select the extraction mode for CFA channels or derived narrowband channels.", nullptr));
        haOIIIResamplingSelector->setToolTip(QCoreApplication::translate(
            "SplitCFAProcess", "Select optional resampling behavior for Ha/OIII extraction.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("SplitCFAProcess", "Choose the open image or sequence to process.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("SplitCFAProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Ok))
            btn->setToolTip(QCoreApplication::translate("SplitCFAProcess",
                                                        "Run CFA extraction with the current settings.", nullptr));
    } // retranslateUi
};

inline SplitCFAProcessUiElements buildSplitCFAProcessUi(QWidget* parent)
{
    SplitCFAProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
