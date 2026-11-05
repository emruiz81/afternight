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
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class SharpenAlphaProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QLabel* label;
    QSlider* amplitudeSlider;
    QSlider* numOfScalesSlider;
    QSlider* amountSlider;
    QSpinBox* numOfScalesValue;
    QDoubleSpinBox* amountValue;
    QLabel* label_2;
    QDoubleSpinBox* anisotropyValue;
    QLabel* label_6;
    QLabel* label_5;
    QSlider* anisotropySlider;
    QDoubleSpinBox* amplitudeValue;
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

    void setupUi(QWidget* SharpenAlphaProcess)
    {
        const auto& t = an::ui::tokens();

        if (SharpenAlphaProcess->objectName().isEmpty())
            SharpenAlphaProcess->setObjectName("SharpenAlphaProcess");
        SharpenAlphaProcess->resize(511, 271);
        SharpenAlphaProcess->setMinimumSize(QSize(300, 200));
        SharpenAlphaProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(SharpenAlphaProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(SharpenAlphaProcess);
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
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        amplitudeSlider = new QSlider(horizontalLayoutWidget_4);
        amplitudeSlider->setObjectName("amplitudeSlider");
        amplitudeSlider->setMinimum(0);
        amplitudeSlider->setMaximum(4000);
        amplitudeSlider->setValue(1000);
        amplitudeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(amplitudeSlider, 0, 2, 1, 1);

        numOfScalesSlider = new QSlider(horizontalLayoutWidget_4);
        numOfScalesSlider->setObjectName("numOfScalesSlider");
        numOfScalesSlider->setMinimum(1);
        numOfScalesSlider->setMaximum(10);
        numOfScalesSlider->setValue(4);
        numOfScalesSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(numOfScalesSlider, 1, 2, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimum(0);
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(amountSlider, 3, 2, 1, 1);

        numOfScalesValue = new QSpinBox(horizontalLayoutWidget_4);
        numOfScalesValue->setObjectName("numOfScalesValue");
        numOfScalesValue->setMinimum(1);
        numOfScalesValue->setMaximum(10);
        numOfScalesValue->setValue(4);
        numOfScalesValue->setMinimumWidth(80);

        gridLayout->addWidget(numOfScalesValue, 1, 1, 1, 1);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMinimum(0.000000000000000);
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.050000000000000);
        amountValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        gridLayout->addWidget(amountValue, 3, 1, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        anisotropyValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        anisotropyValue->setObjectName("anisotropyValue");
        anisotropyValue->setMinimum(0.000000000000000);
        anisotropyValue->setMaximum(1.000000000000000);
        anisotropyValue->setSingleStep(0.050000000000000);
        anisotropyValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        anisotropyValue->setValue(0.500000000000000);
        anisotropyValue->setMinimumWidth(80);

        gridLayout->addWidget(anisotropyValue, 2, 1, 1, 1);

        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 3, 0, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 2, 0, 1, 1);

        anisotropySlider = new QSlider(horizontalLayoutWidget_4);
        anisotropySlider->setObjectName("anisotropySlider");
        anisotropySlider->setMinimum(0);
        anisotropySlider->setMaximum(1000);
        anisotropySlider->setValue(500);
        anisotropySlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(anisotropySlider, 2, 2, 1, 1);

        amplitudeValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amplitudeValue->setObjectName("amplitudeValue");
        amplitudeValue->setMaximum(4.000000000000000);
        amplitudeValue->setSingleStep(0.100000000000000);
        amplitudeValue->setValue(1.000000000000000);
        amplitudeValue->setMinimumWidth(80);

        gridLayout->addWidget(amplitudeValue, 0, 1, 1, 1);

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

        retranslateUi(SharpenAlphaProcess);

        QMetaObject::connectSlotsByName(SharpenAlphaProcess);
    } // setupUi

    void retranslateUi(QWidget* SharpenAlphaProcess)
    {
        SharpenAlphaProcess->setWindowTitle(
            QCoreApplication::translate("SharpenAlphaProcess", "Multiscale Sharpen Alpha", nullptr));
        label->setText(QCoreApplication::translate("SharpenAlphaProcess", "Amplitude", nullptr));
        label_2->setText(QCoreApplication::translate("SharpenAlphaProcess", "Number of Scales", nullptr));
        label_6->setText(QCoreApplication::translate("SharpenAlphaProcess", "Amount", nullptr));
        label_5->setText(QCoreApplication::translate("SharpenAlphaProcess", "Anisotropy", nullptr));
        label_3->setText(QCoreApplication::translate("SharpenAlphaProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("SharpenAlphaProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("SharpenAlphaProcess", "Preview", nullptr));

        amplitudeValue->setToolTip(QCoreApplication::translate(
            "SharpenAlphaProcess", "Amplitude of the multiscale sharpening response.", nullptr));
        amplitudeSlider->setToolTip(QCoreApplication::translate(
            "SharpenAlphaProcess", "Amplitude of the multiscale sharpening response.", nullptr));
        numOfScalesValue->setToolTip(QCoreApplication::translate(
            "SharpenAlphaProcess", "Number of wavelet scales used for sharpening.", nullptr));
        numOfScalesSlider->setToolTip(QCoreApplication::translate(
            "SharpenAlphaProcess", "Number of wavelet scales used for sharpening.", nullptr));
        anisotropyValue->setToolTip(
            QCoreApplication::translate("SharpenAlphaProcess", "Directional anisotropy factor.", nullptr));
        anisotropySlider->setToolTip(
            QCoreApplication::translate("SharpenAlphaProcess", "Directional anisotropy factor.", nullptr));
        amountValue->setToolTip(
            QCoreApplication::translate("SharpenAlphaProcess", "Blend amount for this process result.", nullptr));
        amountSlider->setToolTip(
            QCoreApplication::translate("SharpenAlphaProcess", "Blend amount for this process result.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("SharpenAlphaProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "SharpenAlphaProcess", "Select the channel to process, or RGB to process all channels together.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("SharpenAlphaProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("SharpenAlphaProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("SharpenAlphaProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline SharpenAlphaProcessUiElements buildSharpenAlphaProcessUi(QWidget* parent)
{
    SharpenAlphaProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
