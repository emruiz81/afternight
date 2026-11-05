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

class WaveletDenoiseProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QDoubleSpinBox* amountValue;
    QSpinBox* iterationsValue;
    QSlider* iterationsSlider;
    QSlider* thresholdSlider;
    QSpinBox* scalesValue;
    QLabel* label_2;
    QLabel* label_5;
    QLabel* label_6;
    QLabel* label_7;
    QSlider* scalesSlider;
    QDoubleSpinBox* thresholdValue;
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

    void setupUi(QWidget* WaveletDenoiseProcess)
    {
        const auto& t = an::ui::tokens();

        if (WaveletDenoiseProcess->objectName().isEmpty())
            WaveletDenoiseProcess->setObjectName("WaveletDenoiseProcess");
        WaveletDenoiseProcess->resize(620, 341);
        WaveletDenoiseProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(WaveletDenoiseProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(WaveletDenoiseProcess);
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
        verticalLayout->setSpacing(t.spacing.md);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        gridLayout->setHorizontalSpacing(t.spacing.md);
        gridLayout->setVerticalSpacing(t.spacing.sm);
        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.010000000000000);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        gridLayout->addWidget(amountValue, 3, 1, 1, 1);

        iterationsValue = new QSpinBox(horizontalLayoutWidget_4);
        iterationsValue->setObjectName("iterationsValue");
        iterationsValue->setMinimum(1);
        iterationsValue->setMaximum(32);
        iterationsValue->setValue(10);
        iterationsValue->setMinimumWidth(80);

        gridLayout->addWidget(iterationsValue, 2, 1, 1, 1);

        iterationsSlider = new QSlider(horizontalLayoutWidget_4);
        iterationsSlider->setObjectName("iterationsSlider");
        iterationsSlider->setMinimumSize(QSize(150, 0));
        iterationsSlider->setMinimum(1);
        iterationsSlider->setMaximum(32);
        iterationsSlider->setPageStep(10);
        iterationsSlider->setValue(10);
        iterationsSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(iterationsSlider, 2, 2, 1, 1);

        thresholdSlider = new QSlider(horizontalLayoutWidget_4);
        thresholdSlider->setObjectName("thresholdSlider");
        thresholdSlider->setMinimumSize(QSize(150, 0));
        thresholdSlider->setMaximum(1000);
        thresholdSlider->setValue(140);
        thresholdSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(thresholdSlider, 0, 2, 1, 1);

        scalesValue = new QSpinBox(horizontalLayoutWidget_4);
        scalesValue->setObjectName("scalesValue");
        scalesValue->setMinimum(2);
        scalesValue->setMaximum(10);
        scalesValue->setValue(6);
        scalesValue->setMinimumWidth(80);

        gridLayout->addWidget(scalesValue, 1, 1, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 0, 0, 1, 1);

        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 2, 0, 1, 1);

        label_7 = new QLabel(horizontalLayoutWidget_4);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 3, 0, 1, 1);

        scalesSlider = new QSlider(horizontalLayoutWidget_4);
        scalesSlider->setObjectName("scalesSlider");
        scalesSlider->setMinimumSize(QSize(150, 0));
        scalesSlider->setMinimum(2);
        scalesSlider->setMaximum(10);
        scalesSlider->setPageStep(10);
        scalesSlider->setValue(6);
        scalesSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(scalesSlider, 1, 2, 1, 1);

        thresholdValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        thresholdValue->setObjectName("thresholdValue");
        thresholdValue->setMaximum(10.000000000000000);
        thresholdValue->setSingleStep(0.010000000000000);
        thresholdValue->setValue(1.400000000000000);
        thresholdValue->setMinimumWidth(80);

        gridLayout->addWidget(thresholdValue, 0, 1, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimumSize(QSize(150, 0));
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(amountSlider, 3, 2, 1, 1);

        verticalLayout->addLayout(gridLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        formLayout->setHorizontalSpacing(t.spacing.md);
        formLayout->setVerticalSpacing(t.spacing.sm);
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
        channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, channelSelector);

        verticalLayout->addLayout(formLayout);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        horizontalLayout_4->setSpacing(t.spacing.sm);
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
        horizontalLayout_3->setSpacing(t.spacing.sm);
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

        retranslateUi(WaveletDenoiseProcess);

        QMetaObject::connectSlotsByName(WaveletDenoiseProcess);
    } // setupUi

    void retranslateUi(QWidget* WaveletDenoiseProcess)
    {
        WaveletDenoiseProcess->setWindowTitle(
            QCoreApplication::translate("WaveletDenoiseProcess", "Wavelet Denoise", nullptr));
        label_2->setText(QCoreApplication::translate("WaveletDenoiseProcess", "Scales", nullptr));
        label_5->setText(QCoreApplication::translate("WaveletDenoiseProcess", "Threshold", nullptr));
        label_6->setText(QCoreApplication::translate("WaveletDenoiseProcess", "Iterations", nullptr));
        label_7->setText(QCoreApplication::translate("WaveletDenoiseProcess", "Amount", nullptr));
        label_3->setText(QCoreApplication::translate("WaveletDenoiseProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("WaveletDenoiseProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("WaveletDenoiseProcess", "Preview", nullptr));

        thresholdValue->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess",
            "Threshold for wavelet noise suppression. Higher values remove more low-amplitude detail.", nullptr));
        thresholdSlider->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess",
            "Threshold for wavelet noise suppression. Higher values remove more low-amplitude detail.", nullptr));
        scalesValue->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess", "Number of wavelet scales to process. More scales affect larger structures.",
            nullptr));
        scalesSlider->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess", "Number of wavelet scales to process. More scales affect larger structures.",
            nullptr));
        iterationsValue->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess", "Number of denoising iterations. More iterations increase smoothing.", nullptr));
        iterationsSlider->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess", "Number of denoising iterations. More iterations increase smoothing.", nullptr));
        amountValue->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess", "Blend amount of the denoised result. 1.0 applies full strength.", nullptr));
        amountSlider->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess", "Blend amount of the denoised result. 1.0 applies full strength.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("WaveletDenoiseProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "WaveletDenoiseProcess", "Select the channel to process, or RGB to process all channels together.",
            nullptr));
        preview->setToolTip(
            QCoreApplication::translate("WaveletDenoiseProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("WaveletDenoiseProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("WaveletDenoiseProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline WaveletDenoiseProcessUiElements buildWaveletDenoiseProcessUi(QWidget* parent)
{
    WaveletDenoiseProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(620, 341));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
