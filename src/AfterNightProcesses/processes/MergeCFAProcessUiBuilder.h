// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtWidgets/QComboBox>
#include <QCoreApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class MergeCFAProcessUiElements
{
public:
    static constexpr int kLabelColumnWidth = 130;

    QGridLayout* gridLayout = nullptr;
    QLabel* channel0Label = nullptr;
    QComboBox* cfa2Selector = nullptr;
    QLabel* channel1Label = nullptr;
    QLabel* label = nullptr;
    QComboBox* cfa0Selector = nullptr;
    QLabel* channel2Label = nullptr;
    QComboBox* cfa1Selector = nullptr;
    QComboBox* cfa3Selector = nullptr;
    QHBoxLayout* horizontalLayout = nullptr;
    QLabel* label_5 = nullptr;
    QComboBox* patternSelector = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    void setupUi(QWidget* MergeCFAProcess)
    {
        if (MergeCFAProcess->objectName().isEmpty())
            MergeCFAProcess->setObjectName("MergeCFAProcess");
        MergeCFAProcess->setWindowModality(Qt::NonModal);
        MergeCFAProcess->resize(380, 220);
        MergeCFAProcess->setMinimumSize(QSize(380, 220));

        const auto& t = an::ui::tokens();

        auto* rootLayout = new QVBoxLayout(MergeCFAProcess);
        rootLayout->setObjectName("rootLayout");
        rootLayout->setContentsMargins(t.spacing.md, 0, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout->setSpacing(t.spacing.sm);

        label_5 = new QLabel(MergeCFAProcess);
        label_5->setObjectName("label_5");
        label_5->setMinimumSize(QSize(kLabelColumnWidth, 0));
        label_5->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        horizontalLayout->addWidget(label_5);

        patternSelector = new QComboBox(MergeCFAProcess);
        patternSelector->setObjectName("patternSelector");
        patternSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        horizontalLayout->addWidget(patternSelector);
        horizontalLayout->setStretch(1, 1);

        rootLayout->addLayout(horizontalLayout);

        gridLayout = new QGridLayout();
        gridLayout->setSpacing(t.spacing.sm);
        gridLayout->setObjectName("gridLayout");
        gridLayout->setContentsMargins(0, 0, 0, 0);
        gridLayout->setColumnStretch(1, 1);

        label = new QLabel(MergeCFAProcess);
        label->setObjectName("label");
        label->setMinimumSize(QSize(kLabelColumnWidth, 0));
        label->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        gridLayout->addWidget(label, 0, 0, 1, 1);

        cfa0Selector = new QComboBox(MergeCFAProcess);
        cfa0Selector->setObjectName("cfa0Selector");
        cfa0Selector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        cfa0Selector->setMinimumWidth(300);
        gridLayout->addWidget(cfa0Selector, 0, 1, 1, 1);

        channel0Label = new QLabel(MergeCFAProcess);
        channel0Label->setObjectName("channel0Label");
        channel0Label->setMinimumSize(QSize(kLabelColumnWidth, 0));
        channel0Label->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        gridLayout->addWidget(channel0Label, 1, 0, 1, 1);

        cfa1Selector = new QComboBox(MergeCFAProcess);
        cfa1Selector->setObjectName("cfa1Selector");
        cfa1Selector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        gridLayout->addWidget(cfa1Selector, 1, 1, 1, 1);

        channel1Label = new QLabel(MergeCFAProcess);
        channel1Label->setObjectName("channel1Label");
        channel1Label->setMinimumSize(QSize(kLabelColumnWidth, 0));
        channel1Label->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        gridLayout->addWidget(channel1Label, 2, 0, 1, 1);

        cfa2Selector = new QComboBox(MergeCFAProcess);
        cfa2Selector->setObjectName("cfa2Selector");
        cfa2Selector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        gridLayout->addWidget(cfa2Selector, 2, 1, 1, 1);

        channel2Label = new QLabel(MergeCFAProcess);
        channel2Label->setObjectName("channel2Label");
        channel2Label->setMinimumSize(QSize(kLabelColumnWidth, 0));
        channel2Label->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        gridLayout->addWidget(channel2Label, 3, 0, 1, 1);

        cfa3Selector = new QComboBox(MergeCFAProcess);
        cfa3Selector->setObjectName("cfa3Selector");
        cfa3Selector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        gridLayout->addWidget(cfa3Selector, 3, 1, 1, 1);

        rootLayout->addLayout(gridLayout);
        rootLayout->addStretch(1);

        buttonBox = new QDialogButtonBox(MergeCFAProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(MergeCFAProcess);

        retranslateUi(MergeCFAProcess);

        MergeCFAProcess->adjustSize();
    } // setupUi

    void retranslateUi(QWidget* MergeCFAProcess)
    {
        MergeCFAProcess->setWindowTitle(QCoreApplication::translate("MergeCFAProcess", "Merge CFA", nullptr));
        channel0Label->setText(QCoreApplication::translate("MergeCFAProcess", "CFA1", nullptr));
        channel1Label->setText(QCoreApplication::translate("MergeCFAProcess", "CFA2", nullptr));
        label->setText(QCoreApplication::translate("MergeCFAProcess", "CFA0", nullptr));
        channel2Label->setText(QCoreApplication::translate("MergeCFAProcess", "CFA3", nullptr));
        label_5->setText(QCoreApplication::translate("MergeCFAProcess", "CFA Pattern", nullptr));

        cfa0Selector->setToolTip(
            QCoreApplication::translate("MergeCFAProcess", "Select the source image for CFA0.", nullptr));
        cfa1Selector->setToolTip(
            QCoreApplication::translate("MergeCFAProcess", "Select the source image for CFA1.", nullptr));
        cfa2Selector->setToolTip(
            QCoreApplication::translate("MergeCFAProcess", "Select the source image for CFA2.", nullptr));
        cfa3Selector->setToolTip(
            QCoreApplication::translate("MergeCFAProcess", "Select the source image for CFA3.", nullptr));
        patternSelector->setToolTip(
            QCoreApplication::translate("MergeCFAProcess", "Select the Bayer pattern for the merged output.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("MergeCFAProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Ok))
            btn->setToolTip(
                QCoreApplication::translate("MergeCFAProcess", "Run CFA merge with the current settings.", nullptr));
    } // retranslateUi
};

inline MergeCFAProcessUiElements buildMergeCFAProcessUi(QWidget* parent)
{
    MergeCFAProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
