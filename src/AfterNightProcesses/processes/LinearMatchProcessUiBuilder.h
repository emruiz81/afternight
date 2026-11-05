// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class LinearMatchProcessUiElements
{
public:
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QComboBox* referenceImageSelector;
    QHBoxLayout* horizontalLayout_3;
    QLabel* label_2;
    QDoubleSpinBox* rejectLowValue;
    QSlider* rejectLowSlider;
    QHBoxLayout* horizontalLayout_4;
    QLabel* label_3;
    QDoubleSpinBox* rejectHighValue;
    QSlider* rejectHighSlider;
    QHBoxLayout* horizontalLayout_5;
    QLabel* label_4;
    QComboBox* targetImageSelector;
    QHBoxLayout* horizontalLayout_2;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* LinearMatchProcess)
    {
        const auto& t = an::ui::tokens();

        if (LinearMatchProcess->objectName().isEmpty())
            LinearMatchProcess->setObjectName("LinearMatchProcess");
        LinearMatchProcess->resize(460, 280);
        LinearMatchProcess->setMinimumSize(QSize(460, 280));
        LinearMatchProcess->setMaximumSize(QSize(460, 280));
        verticalLayoutWidget = LinearMatchProcess;
        verticalLayout = new QVBoxLayout(LinearMatchProcess);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout->setSpacing(t.spacing.md);
        verticalLayout->setSizeConstraint(QLayout::SetFixedSize);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setSpacing(t.spacing.sm);
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(verticalLayoutWidget);
        label->setObjectName("label");
        label->setMinimumSize(QSize(110, 0));
        label->setMaximumSize(QSize(110, 16777215));

        horizontalLayout->addWidget(label);

        referenceImageSelector = new QComboBox(verticalLayoutWidget);
        referenceImageSelector->setObjectName("referenceImageSelector");
        referenceImageSelector->setMinimumWidth(150);
        referenceImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout->addWidget(referenceImageSelector);

        horizontalLayout->setStretch(1, 1);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        horizontalLayout_3->setSpacing(t.spacing.sm);
        horizontalLayout_3->setContentsMargins(0, 0, 0, 0);
        label_2 = new QLabel(verticalLayoutWidget);
        label_2->setObjectName("label_2");
        label_2->setMinimumSize(QSize(110, 0));
        label_2->setMaximumSize(QSize(110, 16777215));

        horizontalLayout_3->addWidget(label_2);

        rejectLowValue = new QDoubleSpinBox(verticalLayoutWidget);
        rejectLowValue->setObjectName("rejectLowValue");
        // Three decimals: the slider moves in thousandths.
        rejectLowValue->setDecimals(3);
        rejectLowValue->setMaximum(1.000000000000000);
        rejectLowValue->setSingleStep(0.010000000000000);
        rejectLowValue->setMinimumWidth(80);

        horizontalLayout_3->addWidget(rejectLowValue);

        rejectLowSlider = new QSlider(verticalLayoutWidget);
        rejectLowSlider->setObjectName("rejectLowSlider");
        rejectLowSlider->setMaximum(1000);
        rejectLowSlider->setOrientation(Qt::Horizontal);
        rejectLowSlider->setMinimumWidth(150);

        horizontalLayout_3->addWidget(rejectLowSlider);

        horizontalLayout_3->setStretch(2, 5);

        verticalLayout->addLayout(horizontalLayout_3);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        horizontalLayout_4->setSpacing(t.spacing.sm);
        horizontalLayout_4->setContentsMargins(0, 0, 0, 0);
        label_3 = new QLabel(verticalLayoutWidget);
        label_3->setObjectName("label_3");
        label_3->setMinimumSize(QSize(110, 0));
        label_3->setMaximumSize(QSize(110, 16777215));

        horizontalLayout_4->addWidget(label_3);

        rejectHighValue = new QDoubleSpinBox(verticalLayoutWidget);
        rejectHighValue->setObjectName("rejectHighValue");
        rejectHighValue->setDecimals(3);
        rejectHighValue->setMaximum(1.000000000000000);
        rejectHighValue->setSingleStep(0.010000000000000);
        rejectHighValue->setValue(0.920000000000000);
        rejectHighValue->setMinimumWidth(80);

        horizontalLayout_4->addWidget(rejectHighValue);

        rejectHighSlider = new QSlider(verticalLayoutWidget);
        rejectHighSlider->setObjectName("rejectHighSlider");
        rejectHighSlider->setMaximum(1000);
        rejectHighSlider->setValue(920);
        rejectHighSlider->setOrientation(Qt::Horizontal);
        rejectHighSlider->setMinimumWidth(150);

        horizontalLayout_4->addWidget(rejectHighSlider);

        horizontalLayout_4->setStretch(2, 5);

        verticalLayout->addLayout(horizontalLayout_4);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalLayout_5->setSpacing(t.spacing.sm);
        horizontalLayout_5->setContentsMargins(0, 0, 0, 0);
        label_4 = new QLabel(verticalLayoutWidget);
        label_4->setObjectName("label_4");
        label_4->setMinimumSize(QSize(110, 0));
        label_4->setMaximumSize(QSize(110, 16777215));

        horizontalLayout_5->addWidget(label_4);

        targetImageSelector = new QComboBox(verticalLayoutWidget);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout_5->addWidget(targetImageSelector);

        horizontalLayout_5->setStretch(1, 1);

        verticalLayout->addLayout(horizontalLayout_5);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(-1, t.spacing.lg, -1, -1);
        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);

        horizontalLayout_2->addWidget(buttonBox);

        verticalLayout->addLayout(horizontalLayout_2);

        fixed_dialog_ui::applyStandardControlHeights(LinearMatchProcess);

        retranslateUi(LinearMatchProcess);

        QMetaObject::connectSlotsByName(LinearMatchProcess);
    } // setupUi

    void retranslateUi(QWidget* LinearMatchProcess)
    {
        LinearMatchProcess->setWindowTitle(QCoreApplication::translate("LinearMatchProcess", "Linear Match", nullptr));
        label->setText(QCoreApplication::translate("LinearMatchProcess", "Reference Image", nullptr));
        label_2->setText(QCoreApplication::translate("LinearMatchProcess", "Reject low", nullptr));
        label_3->setText(QCoreApplication::translate("LinearMatchProcess", "Reject high", nullptr));
        label_4->setText(QCoreApplication::translate("LinearMatchProcess", "Target Image", nullptr));

        referenceImageSelector->setToolTip(QCoreApplication::translate(
            "LinearMatchProcess", "Select the reference image whose linear statistics will be matched.", nullptr));
        rejectLowValue->setToolTip(QCoreApplication::translate(
            "LinearMatchProcess", "Lower rejection threshold used during linear matching.", nullptr));
        rejectLowSlider->setToolTip(QCoreApplication::translate(
            "LinearMatchProcess", "Lower rejection threshold used during linear matching.", nullptr));
        rejectHighValue->setToolTip(QCoreApplication::translate(
            "LinearMatchProcess", "Upper rejection threshold used during linear matching.", nullptr));
        rejectHighSlider->setToolTip(QCoreApplication::translate(
            "LinearMatchProcess", "Upper rejection threshold used during linear matching.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("LinearMatchProcess", "Choose the open image to process.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("LinearMatchProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
    } // retranslateUi
};

inline LinearMatchProcessUiElements buildLinearMatchProcessUi(QWidget* parent)
{
    LinearMatchProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
