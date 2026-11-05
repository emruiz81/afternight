// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

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
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class GRAYCstorationProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QSlider* regularitySlider;
    QLabel* label_5;
    QDoubleSpinBox* regularityValue;
    QDoubleSpinBox* amountValue;
    QSlider* spatialStepSizeSlider;
    QSlider* iterationsSlider;
    QLabel* label_6;
    QSlider* noiseScaleSlider;
    QLabel* label_7;
    QLabel* label_8;
    QSpinBox* amplitudeValue;
    QSlider* sharpnessSlider;
    QSlider* anisotropySlider;
    QSpinBox* iterationsValue;
    QDoubleSpinBox* noiseScaleValue;
    QDoubleSpinBox* anisotropyValue;
    QSlider* amplitudeSlider;
    QLabel* label_9;
    QLabel* label_10;
    QDoubleSpinBox* spatialStepSizeValue;
    QDoubleSpinBox* precisionValue;
    QLabel* label_11;
    QSlider* amountSlider;
    QDoubleSpinBox* sharpnessValue;
    QLabel* label_2;
    QSlider* angularStepSizeSlider;
    QSlider* precisionSlider;
    QLabel* label_12;
    QSpinBox* angularStepSizeValue;
    QLabel* label_13;
    QGridLayout* gridLayout_3;
    QLabel* label_15;
    QComboBox* interpolationSelector;
    QSpacerItem* horizontalSpacer_4;
    QCheckBox* fastApproximation;
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

    void setupUi(QWidget* GRAYCstorationProcess)
    {
        const auto& t = an::ui::tokens();

        if (GRAYCstorationProcess->objectName().isEmpty())
            GRAYCstorationProcess->setObjectName("GRAYCstorationProcess");
        GRAYCstorationProcess->resize(989, 577);
        GRAYCstorationProcess->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(GRAYCstorationProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(GRAYCstorationProcess);
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
        regularitySlider = new QSlider(horizontalLayoutWidget_4);
        regularitySlider->setObjectName("regularitySlider");
        regularitySlider->setMinimumSize(QSize(150, 0));
        regularitySlider->setMaximum(3000);
        regularitySlider->setValue(1100);
        regularitySlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(regularitySlider, 8, 2, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 0, 0, 1, 1);

        regularityValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        regularityValue->setObjectName("regularityValue");
        regularityValue->setMaximum(3.000000000000000);
        regularityValue->setSingleStep(0.050000000000000);
        regularityValue->setValue(1.100000000000000);

        gridLayout->addWidget(regularityValue, 8, 1, 1, 1);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.010000000000000);
        amountValue->setValue(1.000000000000000);

        gridLayout->addWidget(amountValue, 12, 1, 1, 1);

        spatialStepSizeSlider = new QSlider(horizontalLayoutWidget_4);
        spatialStepSizeSlider->setObjectName("spatialStepSizeSlider");
        spatialStepSizeSlider->setMinimumSize(QSize(150, 0));
        spatialStepSizeSlider->setMinimum(100);
        spatialStepSizeSlider->setMaximum(1000);
        spatialStepSizeSlider->setValue(800);
        spatialStepSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(spatialStepSizeSlider, 9, 2, 1, 1);

        iterationsSlider = new QSlider(horizontalLayoutWidget_4);
        iterationsSlider->setObjectName("iterationsSlider");
        iterationsSlider->setMinimumSize(QSize(150, 0));
        iterationsSlider->setMinimum(1);
        iterationsSlider->setMaximum(10);
        iterationsSlider->setPageStep(10);
        iterationsSlider->setValue(1);
        iterationsSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(iterationsSlider, 0, 2, 1, 1);

        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 2, 0, 1, 1);

        noiseScaleSlider = new QSlider(horizontalLayoutWidget_4);
        noiseScaleSlider->setObjectName("noiseScaleSlider");
        noiseScaleSlider->setMinimumSize(QSize(150, 0));
        noiseScaleSlider->setMaximum(3000);
        noiseScaleSlider->setValue(600);
        noiseScaleSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(noiseScaleSlider, 7, 2, 1, 1);

        label_7 = new QLabel(horizontalLayoutWidget_4);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 7, 0, 1, 1);

        label_8 = new QLabel(horizontalLayoutWidget_4);
        label_8->setObjectName("label_8");

        gridLayout->addWidget(label_8, 8, 0, 1, 1);

        amplitudeValue = new QSpinBox(horizontalLayoutWidget_4);
        amplitudeValue->setObjectName("amplitudeValue");
        amplitudeValue->setMinimum(0);
        amplitudeValue->setMaximum(100);
        amplitudeValue->setSingleStep(1);
        amplitudeValue->setValue(60);

        gridLayout->addWidget(amplitudeValue, 1, 1, 1, 1);

        sharpnessSlider = new QSlider(horizontalLayoutWidget_4);
        sharpnessSlider->setObjectName("sharpnessSlider");
        sharpnessSlider->setMinimumSize(QSize(150, 0));
        sharpnessSlider->setMaximum(2500);
        sharpnessSlider->setValue(700);
        sharpnessSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(sharpnessSlider, 2, 2, 1, 1);

        anisotropySlider = new QSlider(horizontalLayoutWidget_4);
        anisotropySlider->setObjectName("anisotropySlider");
        anisotropySlider->setMinimumSize(QSize(150, 0));
        anisotropySlider->setMaximum(1000);
        anisotropySlider->setValue(300);
        anisotropySlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(anisotropySlider, 6, 2, 1, 1);

        iterationsValue = new QSpinBox(horizontalLayoutWidget_4);
        iterationsValue->setObjectName("iterationsValue");
        iterationsValue->setMinimum(1);
        iterationsValue->setMaximum(10);
        iterationsValue->setValue(1);

        gridLayout->addWidget(iterationsValue, 0, 1, 1, 1);

        noiseScaleValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        noiseScaleValue->setObjectName("noiseScaleValue");
        noiseScaleValue->setMaximum(3.000000000000000);
        noiseScaleValue->setSingleStep(0.050000000000000);
        noiseScaleValue->setValue(0.600000000000000);

        gridLayout->addWidget(noiseScaleValue, 7, 1, 1, 1);

        anisotropyValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        anisotropyValue->setObjectName("anisotropyValue");
        anisotropyValue->setMaximum(1.000000000000000);
        anisotropyValue->setSingleStep(0.050000000000000);
        anisotropyValue->setValue(0.300000000000000);

        gridLayout->addWidget(anisotropyValue, 6, 1, 1, 1);

        amplitudeSlider = new QSlider(horizontalLayoutWidget_4);
        amplitudeSlider->setObjectName("amplitudeSlider");
        amplitudeSlider->setMinimumSize(QSize(150, 0));
        amplitudeSlider->setMinimum(0);
        amplitudeSlider->setMaximum(100);
        amplitudeSlider->setPageStep(10);
        amplitudeSlider->setValue(60);
        amplitudeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(amplitudeSlider, 1, 2, 1, 1);

        label_9 = new QLabel(horizontalLayoutWidget_4);
        label_9->setObjectName("label_9");

        gridLayout->addWidget(label_9, 9, 0, 1, 1);

        label_10 = new QLabel(horizontalLayoutWidget_4);
        label_10->setObjectName("label_10");

        gridLayout->addWidget(label_10, 6, 0, 1, 1);

        spatialStepSizeValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        spatialStepSizeValue->setObjectName("spatialStepSizeValue");
        spatialStepSizeValue->setMinimum(0.100000000000000);
        spatialStepSizeValue->setMaximum(1.000000000000000);
        spatialStepSizeValue->setSingleStep(0.010000000000000);
        spatialStepSizeValue->setValue(0.800000000000000);

        gridLayout->addWidget(spatialStepSizeValue, 9, 1, 1, 1);

        precisionValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        precisionValue->setObjectName("precisionValue");
        precisionValue->setMinimum(0.100000000000000);
        precisionValue->setMaximum(10.000000000000000);
        precisionValue->setSingleStep(0.100000000000000);
        precisionValue->setValue(2.000000000000000);

        gridLayout->addWidget(precisionValue, 11, 1, 1, 1);

        label_11 = new QLabel(horizontalLayoutWidget_4);
        label_11->setObjectName("label_11");

        gridLayout->addWidget(label_11, 12, 0, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimumSize(QSize(150, 0));
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(amountSlider, 12, 2, 1, 1);

        sharpnessValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        sharpnessValue->setObjectName("sharpnessValue");
        sharpnessValue->setMaximum(2.500000000000000);
        sharpnessValue->setSingleStep(0.010000000000000);
        sharpnessValue->setValue(0.700000000000000);

        gridLayout->addWidget(sharpnessValue, 2, 1, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        angularStepSizeSlider = new QSlider(horizontalLayoutWidget_4);
        angularStepSizeSlider->setObjectName("angularStepSizeSlider");
        angularStepSizeSlider->setMinimumSize(QSize(150, 0));
        angularStepSizeSlider->setMinimum(1);
        angularStepSizeSlider->setMaximum(90);
        angularStepSizeSlider->setValue(30);
        angularStepSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(angularStepSizeSlider, 10, 2, 1, 1);

        precisionSlider = new QSlider(horizontalLayoutWidget_4);
        precisionSlider->setObjectName("precisionSlider");
        precisionSlider->setMinimumSize(QSize(150, 0));
        precisionSlider->setMinimum(100);
        precisionSlider->setMaximum(10000);
        precisionSlider->setValue(2000);
        precisionSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(precisionSlider, 11, 2, 1, 1);

        label_12 = new QLabel(horizontalLayoutWidget_4);
        label_12->setObjectName("label_12");

        gridLayout->addWidget(label_12, 11, 0, 1, 1);

        angularStepSizeValue = new QSpinBox(horizontalLayoutWidget_4);
        angularStepSizeValue->setObjectName("angularStepSizeValue");
        angularStepSizeValue->setMinimum(1);
        angularStepSizeValue->setMaximum(90);
        angularStepSizeValue->setValue(30);

        gridLayout->addWidget(angularStepSizeValue, 10, 1, 1, 1);

        label_13 = new QLabel(horizontalLayoutWidget_4);
        label_13->setObjectName("label_13");

        gridLayout->addWidget(label_13, 10, 0, 1, 1);

        verticalLayout->addLayout(gridLayout);

        gridLayout_3 = new QGridLayout();
        gridLayout_3->setObjectName("gridLayout_3");
        label_15 = new QLabel(horizontalLayoutWidget_4);
        label_15->setObjectName("label_15");

        gridLayout_3->addWidget(label_15, 0, 0, 1, 1);

        interpolationSelector = new QComboBox(horizontalLayoutWidget_4);
        interpolationSelector->setObjectName("interpolationSelector");

        gridLayout_3->addWidget(interpolationSelector, 0, 1, 1, 1);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        gridLayout_3->addItem(horizontalSpacer_4, 0, 2, 1, 1);

        verticalLayout->addLayout(gridLayout_3);

        fastApproximation = new QCheckBox(horizontalLayoutWidget_4);
        fastApproximation->setObjectName("fastApproximation");

        verticalLayout->addWidget(fastApproximation);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        label_3 = new QLabel(horizontalLayoutWidget_4);
        label_3->setObjectName("label_3");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label_3);

        targetImageSelector = new QComboBox(horizontalLayoutWidget_4);
        targetImageSelector->setObjectName("targetImageSelector");

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, targetImageSelector);

        label_4 = new QLabel(horizontalLayoutWidget_4);
        label_4->setObjectName("label_4");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_4);

        channelSelector = new QComboBox(horizontalLayoutWidget_4);
        channelSelector->setObjectName("channelSelector");

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

        retranslateUi(GRAYCstorationProcess);

        QMetaObject::connectSlotsByName(GRAYCstorationProcess);
    } // setupUi

    void retranslateUi(QWidget* GRAYCstorationProcess)
    {
        GRAYCstorationProcess->setWindowTitle(
            QCoreApplication::translate("GRAYCstorationProcess", "GRAYCstoration", nullptr));
        label_5->setText(QCoreApplication::translate("GRAYCstorationProcess", "Iterations", nullptr));
        label_6->setText(QCoreApplication::translate("GRAYCstorationProcess", "Sharpness", nullptr));
        label_7->setText(QCoreApplication::translate("GRAYCstorationProcess", "Noise Scale", nullptr));
        label_8->setText(QCoreApplication::translate("GRAYCstorationProcess", "Regularity", nullptr));
        label_9->setText(QCoreApplication::translate("GRAYCstorationProcess", "Spatial Step Size", nullptr));
        label_10->setText(QCoreApplication::translate("GRAYCstorationProcess", "Anisotropy", nullptr));
        label_11->setText(QCoreApplication::translate("GRAYCstorationProcess", "Amount", nullptr));
        label_2->setText(QCoreApplication::translate("GRAYCstorationProcess", "Amplitude", nullptr));
        label_12->setText(QCoreApplication::translate("GRAYCstorationProcess", "Precision", nullptr));
        label_13->setText(QCoreApplication::translate("GRAYCstorationProcess", "Angular Step Size", nullptr));
        label_15->setText(QCoreApplication::translate("GRAYCstorationProcess", "Interpolation", nullptr));
        fastApproximation->setText(QCoreApplication::translate("GRAYCstorationProcess", "Fast Approximation", nullptr));
        label_3->setText(QCoreApplication::translate("GRAYCstorationProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("GRAYCstorationProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("GRAYCstorationProcess", "Preview", nullptr));
    } // retranslateUi
};

inline GRAYCstorationProcessUiElements buildGRAYCstorationProcessUi(QWidget* parent)
{
    GRAYCstorationProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
