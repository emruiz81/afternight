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
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class NLBayesProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QLabel* label_5;
    QDoubleSpinBox* sigmaValue;
    QSlider* sigmaSlider;
    QCheckBox* useArea1;
    QCheckBox* useArea2;
    QCheckBox* da3d;
    QCheckBox* anscombe;
    QCheckBox* autoSigma;
    QHBoxLayout* horizontalLayout_sos;
    QLabel* label_sos;
    QSpinBox* sosIterations;
    QHBoxLayout* horizontalLayout_rho;
    QLabel* label_rho;
    QDoubleSpinBox* sosRhoValue;
    QSlider* sosRhoSlider;
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

    void setupUi(QWidget* NLBayesProcess)
    {
        const auto& t = an::ui::tokens();

        if (NLBayesProcess->objectName().isEmpty())
            NLBayesProcess->setObjectName("NLBayesProcess");
        NLBayesProcess->resize(775, 432);
        NLBayesProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(NLBayesProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(NLBayesProcess);
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
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setSpacing(t.spacing.sm);
        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        horizontalLayout->addWidget(label_5);

        sigmaValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        sigmaValue->setObjectName("sigmaValue");
        sigmaValue->setDecimals(4);
        sigmaValue->setMinimum(0.001000000000000);
        sigmaValue->setMaximum(50.000000000000000);
        sigmaValue->setSingleStep(0.100000000000000);
        sigmaValue->setValue(5.000000000000000);
        sigmaValue->setMinimumWidth(80);

        horizontalLayout->addWidget(sigmaValue);

        sigmaSlider = new QSlider(horizontalLayoutWidget_4);
        sigmaSlider->setObjectName("sigmaSlider");
        sigmaSlider->setMinimum(1);
        sigmaSlider->setMaximum(5000);
        sigmaSlider->setValue(500);
        sigmaSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(sigmaSlider);

        verticalLayout->addLayout(horizontalLayout);

        autoSigma = new QCheckBox(horizontalLayoutWidget_4);
        autoSigma->setObjectName("autoSigma");
        autoSigma->setChecked(true);

        verticalLayout->addWidget(autoSigma);

        useArea1 = new QCheckBox(horizontalLayoutWidget_4);
        useArea1->setObjectName("useArea1");
        useArea1->setChecked(true);

        verticalLayout->addWidget(useArea1);

        useArea2 = new QCheckBox(horizontalLayoutWidget_4);
        useArea2->setObjectName("useArea2");

        verticalLayout->addWidget(useArea2);

        da3d = new QCheckBox(horizontalLayoutWidget_4);
        da3d->setObjectName("da3d");

        verticalLayout->addWidget(da3d);

        anscombe = new QCheckBox(horizontalLayoutWidget_4);
        anscombe->setObjectName("anscombe");

        verticalLayout->addWidget(anscombe);

        horizontalLayout_sos = new QHBoxLayout();
        horizontalLayout_sos->setObjectName("horizontalLayout_sos");
        horizontalLayout_sos->setSpacing(t.spacing.sm);
        label_sos = new QLabel(horizontalLayoutWidget_4);
        label_sos->setObjectName("label_sos");

        horizontalLayout_sos->addWidget(label_sos);

        sosIterations = new QSpinBox(horizontalLayoutWidget_4);
        sosIterations->setObjectName("sosIterations");
        sosIterations->setMinimum(1);
        sosIterations->setMaximum(10);
        sosIterations->setValue(1);
        sosIterations->setMinimumWidth(80);

        horizontalLayout_sos->addWidget(sosIterations);

        verticalLayout->addLayout(horizontalLayout_sos);

        horizontalLayout_rho = new QHBoxLayout();
        horizontalLayout_rho->setObjectName("horizontalLayout_rho");
        horizontalLayout_rho->setSpacing(t.spacing.sm);
        label_rho = new QLabel(horizontalLayoutWidget_4);
        label_rho->setObjectName("label_rho");

        horizontalLayout_rho->addWidget(label_rho);

        sosRhoValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        sosRhoValue->setObjectName("sosRhoValue");
        sosRhoValue->setMinimum(0.010000000000000);
        sosRhoValue->setMaximum(0.990000000000000);
        sosRhoValue->setSingleStep(0.050000000000000);
        sosRhoValue->setValue(0.800000000000000);
        sosRhoValue->setMinimumWidth(80);

        horizontalLayout_rho->addWidget(sosRhoValue);

        sosRhoSlider = new QSlider(horizontalLayoutWidget_4);
        sosRhoSlider->setObjectName("sosRhoSlider");
        sosRhoSlider->setMinimum(10);
        sosRhoSlider->setMaximum(990);
        sosRhoSlider->setPageStep(10);
        sosRhoSlider->setValue(800);
        sosRhoSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_rho->addWidget(sosRhoSlider);

        verticalLayout->addLayout(horizontalLayout_rho);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setSpacing(t.spacing.sm);
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        horizontalLayout_2->addWidget(label_2);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.050000000000000);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        horizontalLayout_2->addWidget(amountValue);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMaximum(1000);
        amountSlider->setPageStep(10);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_2->addWidget(amountSlider);

        verticalLayout->addLayout(horizontalLayout_2);

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

        retranslateUi(NLBayesProcess);

        QMetaObject::connectSlotsByName(NLBayesProcess);
    } // setupUi

    void retranslateUi(QWidget* NLBayesProcess)
    {
        NLBayesProcess->setWindowTitle(QCoreApplication::translate("NLBayesProcess", "Non Local Bayes", nullptr));
        label_5->setText(QCoreApplication::translate("NLBayesProcess", "Sigma", nullptr));
        autoSigma->setText(QCoreApplication::translate("NLBayesProcess", "Automatic Sigma Estimation", nullptr));
        useArea1->setText(QCoreApplication::translate("NLBayesProcess", "1\302\260 Step Homogeneous Area", nullptr));
        useArea2->setText(QCoreApplication::translate("NLBayesProcess", "2\302\260 Step Homogeneous Area", nullptr));
        da3d->setText(
            QCoreApplication::translate("NLBayesProcess", "Data Adaptive Dual Domain Denoise (DA3D)", nullptr));
        anscombe->setText(
            QCoreApplication::translate("NLBayesProcess", "Anscombe Variance-Stabilizing Transform", nullptr));
        label_sos->setText(QCoreApplication::translate("NLBayesProcess", "SOS Iterations", nullptr));
        label_rho->setText(QCoreApplication::translate("NLBayesProcess", "SOS Rho", nullptr));
        label_2->setText(QCoreApplication::translate("NLBayesProcess", "Amount", nullptr));
        label_3->setText(QCoreApplication::translate("NLBayesProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("NLBayesProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("NLBayesProcess", "Preview", nullptr));

        sigmaValue->setToolTip(QCoreApplication::translate(
            "NLBayesProcess",
            "Noise standard deviation used by NL-Bayes. Disabled when automatic estimation is active.", nullptr));
        sigmaSlider->setToolTip(QCoreApplication::translate(
            "NLBayesProcess",
            "Noise standard deviation used by NL-Bayes. Disabled when automatic estimation is active.", nullptr));
        autoSigma->setToolTip(QCoreApplication::translate(
            "NLBayesProcess", "Automatically estimate the noise level from the image. Uncheck to set sigma manually.",
            nullptr));
        useArea1->setToolTip(
            QCoreApplication::translate("NLBayesProcess", "Enable first-stage homogeneous area estimation.", nullptr));
        useArea2->setToolTip(
            QCoreApplication::translate("NLBayesProcess", "Enable second-stage homogeneous area estimation.", nullptr));
        da3d->setToolTip(QCoreApplication::translate(
            "NLBayesProcess",
            "Enable Data Adaptive Dual Domain denoising refinement. Disabled when Anscombe VST is active.", nullptr));
        anscombe->setToolTip(QCoreApplication::translate("NLBayesProcess",
                                                         "Apply Anscombe variance-stabilizing transform for Poisson "
                                                         "(shot) noise. Disables DA3D and forces SOS to 1.",
                                                         nullptr));
        sosIterations->setToolTip(
            QCoreApplication::translate("NLBayesProcess",
                                        "Number of Strength Optimized Subtractive (SOS) iterations. More iterations "
                                        "can improve results at the cost of speed.",
                                        nullptr));
        sosRhoValue->setToolTip(QCoreApplication::translate(
            "NLBayesProcess", "SOS mixing parameter. Higher values converge faster but may diverge.", nullptr));
        sosRhoSlider->setToolTip(QCoreApplication::translate(
            "NLBayesProcess", "SOS mixing parameter. Higher values converge faster but may diverge.", nullptr));
        amountValue->setToolTip(QCoreApplication::translate(
            "NLBayesProcess", "Blend amount of the denoised result. 1.0 applies full strength.", nullptr));
        amountSlider->setToolTip(QCoreApplication::translate(
            "NLBayesProcess", "Blend amount of the denoised result. 1.0 applies full strength.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("NLBayesProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "NLBayesProcess", "Select the channel to process, or RGB to process all channels together.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("NLBayesProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("NLBayesProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("NLBayesProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline NLBayesProcessUiElements buildNLBayesProcessUi(QWidget* parent)
{
    NLBayesProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
