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
#include <QtWidgets/QGridLayout>
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

class TightenEdgesProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QLabel* label;
    QDoubleSpinBox* valueValue;
    QSlider* valueSlider;
    QLabel* label_2;
    QDoubleSpinBox* amountValue;
    QSlider* amountSlider;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QLabel* label_3;
    QComboBox* targetImageSelector;
    QLabel* label_4;
    QComboBox* channelSelector;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* TightenEdgesProcess)
    {
        if (TightenEdgesProcess->objectName().isEmpty())
            TightenEdgesProcess->setObjectName("TightenEdgesProcess");
        const auto& t = an::ui::tokens();
        TightenEdgesProcess->resize(571, 247);
        TightenEdgesProcess->setMinimumSize(QSize(300, 200));
        TightenEdgesProcess->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(TightenEdgesProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(TightenEdgesProcess);
        horizontalLayoutWidget_4->setObjectName("horizontalLayoutWidget_4");
        mainLayout->addWidget(horizontalLayoutWidget_4);
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        gridLayout->setVerticalSpacing(t.spacing.md);
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        valueValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        valueValue->setObjectName("valueValue");
        valueValue->setMinimum(0.000000000000000);
        valueValue->setMaximum(2.000000000000000);
        valueValue->setSingleStep(0.500000000000000);
        valueValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        valueValue->setValue(0.000000000000000);
        valueValue->setMinimumWidth(80);

        gridLayout->addWidget(valueValue, 0, 1, 1, 1);

        valueSlider = new QSlider(horizontalLayoutWidget_4);
        valueSlider->setObjectName("valueSlider");
        valueSlider->setMinimum(0);
        valueSlider->setMaximum(2000);
        valueSlider->setValue(0);
        valueSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(valueSlider, 0, 2, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMinimum(0.000000000000000);
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.500000000000000);
        amountValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        gridLayout->addWidget(amountValue, 1, 1, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimum(0);
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(amountSlider, 1, 2, 1, 1);

        verticalLayout->addLayout(gridLayout);

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

        label_4 = new QLabel(horizontalLayoutWidget_4);
        label_4->setObjectName("label_4");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_4);

        channelSelector = new QComboBox(horizontalLayoutWidget_4);
        channelSelector->setObjectName("channelSelector");
        channelSelector->setMinimumWidth(150);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, channelSelector);

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
        horizontalLayout_3->setSizeConstraint(QLayout::SetDefaultConstraint);
        buttonBox = new QDialogButtonBox(horizontalLayoutWidget_4);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset);

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

        retranslateUi(TightenEdgesProcess);

        QMetaObject::connectSlotsByName(TightenEdgesProcess);
    } // setupUi

    void retranslateUi(QWidget* TightenEdgesProcess)
    {
        TightenEdgesProcess->setWindowTitle(
            QCoreApplication::translate("TightenEdgesProcess", "Tighten Edges", nullptr));
        label->setText(QCoreApplication::translate("TightenEdgesProcess", "Value", nullptr));
        label_2->setText(QCoreApplication::translate("TightenEdgesProcess", "Amount", nullptr));
        label_3->setText(QCoreApplication::translate("TightenEdgesProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("TightenEdgesProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("TightenEdgesProcess", "Preview", nullptr));

        valueValue->setToolTip(
            QCoreApplication::translate("TightenEdgesProcess", "Edge tightening intensity.", nullptr));
        valueSlider->setToolTip(
            QCoreApplication::translate("TightenEdgesProcess", "Edge tightening intensity.", nullptr));
        amountValue->setToolTip(
            QCoreApplication::translate("TightenEdgesProcess", "Blend amount for this process result.", nullptr));
        amountSlider->setToolTip(
            QCoreApplication::translate("TightenEdgesProcess", "Blend amount for this process result.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("TightenEdgesProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "TightenEdgesProcess", "Select the channel to process, or RGB to process all channels together.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("TightenEdgesProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("TightenEdgesProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("TightenEdgesProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline TightenEdgesProcessUiElements buildTightenEdgesProcessUi(QWidget* parent)
{
    TightenEdgesProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
