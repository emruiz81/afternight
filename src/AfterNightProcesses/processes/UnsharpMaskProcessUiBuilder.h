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
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class UnsharpMaskProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QDoubleSpinBox* radiusValue;
    QSlider* radiusSlider;
    QHBoxLayout* horizontalLayout_2;
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

    void setupUi(QWidget* UnsharpMaskProcess)
    {
        const auto& t = an::ui::tokens();

        if (UnsharpMaskProcess->objectName().isEmpty())
            UnsharpMaskProcess->setObjectName("UnsharpMaskProcess");
        UnsharpMaskProcess->resize(504, 249);
        UnsharpMaskProcess->setMinimumSize(QSize(300, 200));
        UnsharpMaskProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(UnsharpMaskProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(UnsharpMaskProcess);
        horizontalLayoutWidget_4->setObjectName("horizontalLayoutWidget_4");
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->addWidget(horizontalLayoutWidget_4);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SetFixedSize);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        horizontalLayout->addWidget(label);

        radiusValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        radiusValue->setObjectName("radiusValue");
        radiusValue->setMinimum(0.000000000000000);
        radiusValue->setMaximum(10.000000000000000);
        radiusValue->setSingleStep(0.100000000000000);
        radiusValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        radiusValue->setValue(1.000000000000000);
        radiusValue->setMinimumWidth(80);

        horizontalLayout->addWidget(radiusValue);

        radiusSlider = new QSlider(horizontalLayoutWidget_4);
        radiusSlider->setObjectName("radiusSlider");
        radiusSlider->setMinimum(0);
        radiusSlider->setMaximum(1000);
        radiusSlider->setValue(100);
        radiusSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(radiusSlider);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        horizontalLayout_2->addWidget(label_2);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMinimum(-10.000000000000000);
        amountValue->setMaximum(10.000000000000000);
        amountValue->setSingleStep(0.100000000000000);
        amountValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        horizontalLayout_2->addWidget(amountValue);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimum(-1000);
        amountSlider->setMaximum(1000);
        amountSlider->setValue(100);
        amountSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_2->addWidget(amountSlider);

        verticalLayout->addLayout(horizontalLayout_2);

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

        retranslateUi(UnsharpMaskProcess);

        QMetaObject::connectSlotsByName(UnsharpMaskProcess);
    } // setupUi

    void retranslateUi(QWidget* UnsharpMaskProcess)
    {
        UnsharpMaskProcess->setWindowTitle(QCoreApplication::translate("UnsharpMaskProcess", "Unsharp Mask", nullptr));
        label->setText(QCoreApplication::translate("UnsharpMaskProcess", "Radius", nullptr));
        label_2->setText(QCoreApplication::translate("UnsharpMaskProcess", "Amount", nullptr));
        label_3->setText(QCoreApplication::translate("UnsharpMaskProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("UnsharpMaskProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("UnsharpMaskProcess", "Preview", nullptr));

        radiusValue->setToolTip(QCoreApplication::translate("UnsharpMaskProcess",
                                                            "Gaussian blur radius used by the unsharp mask.", nullptr));
        radiusSlider->setToolTip(QCoreApplication::translate(
            "UnsharpMaskProcess", "Gaussian blur radius used by the unsharp mask.", nullptr));
        amountValue->setToolTip(QCoreApplication::translate(
            "UnsharpMaskProcess", "Sharpening amount applied after edge extraction.", nullptr));
        amountSlider->setToolTip(QCoreApplication::translate(
            "UnsharpMaskProcess", "Sharpening amount applied after edge extraction.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("UnsharpMaskProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "UnsharpMaskProcess", "Select the channel to process, or RGB to process all channels together.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("UnsharpMaskProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("UnsharpMaskProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("UnsharpMaskProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline UnsharpMaskProcessUiElements buildUnsharpMaskProcessUi(QWidget* parent)
{
    UnsharpMaskProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
