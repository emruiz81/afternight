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

class TotalVariationDenoiseProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout_7;
    QLabel* label_7;
    QComboBox* algorithmSelector;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QDoubleSpinBox* weightValue;
    QSlider* weightSlider;
    QHBoxLayout* horizontalLayout_2;
    QLabel* label_2;
    QDoubleSpinBox* epsValue;
    QSlider* epsSlider;
    QHBoxLayout* horizontalLayout_5;
    QLabel* label_5;
    QSpinBox* maxIterationsValue;
    QSlider* maxIterationsSlider;
    QCheckBox* isotropic;
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

    void setupUi(QWidget* TotalVariationDenoiseProcess)
    {
        const auto& t = an::ui::tokens();

        if (TotalVariationDenoiseProcess->objectName().isEmpty())
            TotalVariationDenoiseProcess->setObjectName("TotalVariationDenoiseProcess");
        TotalVariationDenoiseProcess->resize(623, 372);
        TotalVariationDenoiseProcess->setMinimumSize(QSize(300, 200));
        TotalVariationDenoiseProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(TotalVariationDenoiseProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(TotalVariationDenoiseProcess);
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
        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName("horizontalLayout_7");
        horizontalLayout_7->setSpacing(t.spacing.sm);
        label_7 = new QLabel(horizontalLayoutWidget_4);
        label_7->setObjectName("label_7");

        horizontalLayout_7->addWidget(label_7);

        algorithmSelector = new QComboBox(horizontalLayoutWidget_4);
        algorithmSelector->setObjectName("algorithmSelector");
        algorithmSelector->setMinimumWidth(150);
        algorithmSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout_7->addWidget(algorithmSelector);

        horizontalLayout_7->setStretch(1, 1);

        verticalLayout->addLayout(horizontalLayout_7);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setSpacing(t.spacing.sm);
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        horizontalLayout->addWidget(label);

        weightValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        weightValue->setObjectName("weightValue");
        weightValue->setDecimals(4);
        weightValue->setMinimum(0.000000000000000);
        weightValue->setMaximum(1.000000000000000);
        weightValue->setSingleStep(0.005000000000000);
        weightValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        weightValue->setValue(0.500000000000000);
        weightValue->setMinimumWidth(80);

        horizontalLayout->addWidget(weightValue);

        weightSlider = new QSlider(horizontalLayoutWidget_4);
        weightSlider->setObjectName("weightSlider");
        weightSlider->setMinimum(0);
        weightSlider->setMaximum(1000);
        weightSlider->setValue(500);
        weightSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(weightSlider);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setSpacing(t.spacing.sm);
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        horizontalLayout_2->addWidget(label_2);

        epsValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        epsValue->setObjectName("epsValue");
        epsValue->setDecimals(4);
        epsValue->setMinimum(0.000000000000000);
        epsValue->setMaximum(1.000000000000000);
        epsValue->setSingleStep(0.001000000000000);
        epsValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        epsValue->setValue(0.001000000000000);
        epsValue->setMinimumWidth(80);

        horizontalLayout_2->addWidget(epsValue);

        epsSlider = new QSlider(horizontalLayoutWidget_4);
        epsSlider->setObjectName("epsSlider");
        epsSlider->setMinimum(0);
        epsSlider->setMaximum(1000);
        epsSlider->setValue(1);
        epsSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_2->addWidget(epsSlider);

        verticalLayout->addLayout(horizontalLayout_2);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalLayout_5->setSpacing(t.spacing.sm);
        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        horizontalLayout_5->addWidget(label_5);

        maxIterationsValue = new QSpinBox(horizontalLayoutWidget_4);
        maxIterationsValue->setObjectName("maxIterationsValue");
        maxIterationsValue->setMinimum(1);
        maxIterationsValue->setMaximum(500);
        maxIterationsValue->setValue(100);
        maxIterationsValue->setMinimumWidth(80);

        horizontalLayout_5->addWidget(maxIterationsValue);

        maxIterationsSlider = new QSlider(horizontalLayoutWidget_4);
        maxIterationsSlider->setObjectName("maxIterationsSlider");
        maxIterationsSlider->setEnabled(true);
        maxIterationsSlider->setMinimum(1);
        maxIterationsSlider->setMaximum(500);
        maxIterationsSlider->setValue(100);
        maxIterationsSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_5->addWidget(maxIterationsSlider);

        verticalLayout->addLayout(horizontalLayout_5);

        isotropic = new QCheckBox(horizontalLayoutWidget_4);
        isotropic->setObjectName("isotropic");
        isotropic->setChecked(true);

        verticalLayout->addWidget(isotropic);

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

        retranslateUi(TotalVariationDenoiseProcess);

        QMetaObject::connectSlotsByName(TotalVariationDenoiseProcess);
    } // setupUi

    void retranslateUi(QWidget* TotalVariationDenoiseProcess)
    {
        TotalVariationDenoiseProcess->setWindowTitle(
            QCoreApplication::translate("TotalVariationDenoiseProcess", "Total Variation Denoise", nullptr));
        label_7->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Algorithm ", nullptr));
        label->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Weight", nullptr));
        label_2->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Epsilon", nullptr));
        label_5->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Max Iterations", nullptr));
        isotropic->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Isotropic", nullptr));
        label_3->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("TotalVariationDenoiseProcess", "Preview", nullptr));

        algorithmSelector->setToolTip(
            QCoreApplication::translate("TotalVariationDenoiseProcess",
                                        "<p style='margin:0;'>Select the total-variation optimization algorithm.</p>"
                                        "<p style='margin:4px 0 0 0;'>"
                                        "\u2022 Bregman \u2014 robust denoising with iterative regularization<br>"
                                        "\u2022 Chambolle \u2014 fast primal-dual style TV denoising</p>",
                                        nullptr));
        weightValue->setToolTip(
            QCoreApplication::translate("TotalVariationDenoiseProcess",
                                        "Regularization weight. Higher values produce stronger smoothing.", nullptr));
        weightSlider->setToolTip(
            QCoreApplication::translate("TotalVariationDenoiseProcess",
                                        "Regularization weight. Higher values produce stronger smoothing.", nullptr));
        epsValue->setToolTip(QCoreApplication::translate("TotalVariationDenoiseProcess",
                                                         "Stopping tolerance for optimization iterations.", nullptr));
        epsSlider->setToolTip(QCoreApplication::translate("TotalVariationDenoiseProcess",
                                                          "Stopping tolerance for optimization iterations.", nullptr));
        maxIterationsValue->setToolTip(
            QCoreApplication::translate("TotalVariationDenoiseProcess", "Maximum optimization iterations.", nullptr));
        maxIterationsSlider->setToolTip(
            QCoreApplication::translate("TotalVariationDenoiseProcess", "Maximum optimization iterations.", nullptr));
        isotropic->setToolTip(
            QCoreApplication::translate("TotalVariationDenoiseProcess",
                                        "Enable isotropic total-variation regularization when supported.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("TotalVariationDenoiseProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "TotalVariationDenoiseProcess", "Select the channel to process, or RGB to process all channels together.",
            nullptr));
        preview->setToolTip(QCoreApplication::translate("TotalVariationDenoiseProcess",
                                                        "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("TotalVariationDenoiseProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("TotalVariationDenoiseProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline TotalVariationDenoiseProcessUiElements buildTotalVariationDenoiseProcessUi(QWidget* parent)
{
    TotalVariationDenoiseProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(623, 372));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
