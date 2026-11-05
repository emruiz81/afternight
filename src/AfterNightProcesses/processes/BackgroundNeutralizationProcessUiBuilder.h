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
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class BackgroundNeutralizationProcessUiElements
{
public:
    QWidget* layoutWidget;
    QHBoxLayout* horizontalLayout_4;
    QLabel* label_12;
    QComboBox* backgroundReferenceImageSelector;
    QWidget* layoutWidget_3;
    QHBoxLayout* horizontalLayout_6;
    QLabel* label_15;
    QDoubleSpinBox* backgroundUpperLimitValue;
    QSlider* backgroundUpperLimitSlider;
    QWidget* layoutWidget_2;
    QHBoxLayout* horizontalLayout_5;
    QLabel* label_14;
    QDoubleSpinBox* backgroundLowerLimitValue;
    QSlider* backgroundLowerLimitSlider;
    QGroupBox* backgroundRoiGroupBox;
    QPushButton* backgroundRoiSelectionButton;
    QWidget* layoutWidget_4;
    QGridLayout* gridLayout_3;
    QSpinBox* backgroundRoiLeftValue;
    QSpinBox* backgroundRoiWidthValue;
    QLabel* label_16;
    QLabel* label_17;
    QLabel* label_18;
    QSpacerItem* horizontalSpacer_5;
    QLabel* label_19;
    QSpinBox* backgroundRoiHeightValue;
    QSpinBox* backgroundRoiTopValue;
    QSpacerItem* horizontalSpacer_6;
    QSpacerItem* horizontalSpacer_7;
    QSpacerItem* horizontalSpacer_8;
    QDialogButtonBox* buttonBox;
    QWidget* widget;
    QHBoxLayout* horizontalLayout;
    QLabel* label_3;
    QComboBox* targetImageSelector;

    void setupUi(QWidget* BackgroundNeutralizationProcess)
    {
        constexpr int kDialogWidth = 500;
        constexpr int kDialogHeight = 360;
        constexpr int kRoiSpinBoxWidth = 88;
        const auto& t = an::ui::tokens();

        if (BackgroundNeutralizationProcess->objectName().isEmpty())
            BackgroundNeutralizationProcess->setObjectName("BackgroundNeutralizationProcess");
        BackgroundNeutralizationProcess->resize(kDialogWidth, kDialogHeight);
        BackgroundNeutralizationProcess->setMinimumSize(QSize(kDialogWidth, kDialogHeight));
        BackgroundNeutralizationProcess->setMaximumSize(QSize(kDialogWidth, kDialogHeight));

        auto* rootLayout = new QVBoxLayout(BackgroundNeutralizationProcess);
        rootLayout->setContentsMargins(t.spacing.lg, t.spacing.md, t.spacing.lg, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

        layoutWidget = new QWidget(BackgroundNeutralizationProcess);
        layoutWidget->setObjectName("layoutWidget");
        horizontalLayout_4 = new QHBoxLayout(layoutWidget);
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        horizontalLayout_4->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_4->setSpacing(t.spacing.md);
        label_12 = new QLabel(layoutWidget);
        label_12->setObjectName("label_12");

        horizontalLayout_4->addWidget(label_12);

        backgroundReferenceImageSelector = new QComboBox(layoutWidget);
        backgroundReferenceImageSelector->setObjectName("backgroundReferenceImageSelector");
        backgroundReferenceImageSelector->setMinimumWidth(150);
        backgroundReferenceImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout_4->addWidget(backgroundReferenceImageSelector);

        horizontalLayout_4->setStretch(1, 1);
        rootLayout->addWidget(layoutWidget);

        layoutWidget_2 = new QWidget(BackgroundNeutralizationProcess);
        layoutWidget_2->setObjectName("layoutWidget_2");
        horizontalLayout_5 = new QHBoxLayout(layoutWidget_2);
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalLayout_5->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_5->setSpacing(t.spacing.md);
        label_14 = new QLabel(layoutWidget_2);
        label_14->setObjectName("label_14");

        horizontalLayout_5->addWidget(label_14);

        backgroundLowerLimitValue = new QDoubleSpinBox(layoutWidget_2);
        backgroundLowerLimitValue->setObjectName("backgroundLowerLimitValue");
        backgroundLowerLimitValue->setDecimals(6);
        backgroundLowerLimitValue->setMaximum(1.000000000000000);
        backgroundLowerLimitValue->setSingleStep(0.050000000000000);
        backgroundLowerLimitValue->setMinimumWidth(80);

        horizontalLayout_5->addWidget(backgroundLowerLimitValue);

        backgroundLowerLimitSlider = new QSlider(layoutWidget_2);
        backgroundLowerLimitSlider->setObjectName("backgroundLowerLimitSlider");
        backgroundLowerLimitSlider->setMaximum(1000);
        backgroundLowerLimitSlider->setOrientation(Qt::Horizontal);
        backgroundLowerLimitSlider->setMinimumWidth(150);

        horizontalLayout_5->addWidget(backgroundLowerLimitSlider);

        horizontalLayout_5->setStretch(1, 1);
        horizontalLayout_5->setStretch(2, 3);
        rootLayout->addWidget(layoutWidget_2);

        layoutWidget_3 = new QWidget(BackgroundNeutralizationProcess);
        layoutWidget_3->setObjectName("layoutWidget_3");
        horizontalLayout_6 = new QHBoxLayout(layoutWidget_3);
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        horizontalLayout_6->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_6->setSpacing(t.spacing.md);
        label_15 = new QLabel(layoutWidget_3);
        label_15->setObjectName("label_15");

        horizontalLayout_6->addWidget(label_15);

        backgroundUpperLimitValue = new QDoubleSpinBox(layoutWidget_3);
        backgroundUpperLimitValue->setObjectName("backgroundUpperLimitValue");
        backgroundUpperLimitValue->setDecimals(6);
        backgroundUpperLimitValue->setMaximum(1.000000000000000);
        backgroundUpperLimitValue->setSingleStep(0.050000000000000);
        backgroundUpperLimitValue->setValue(0.100000000000000);
        backgroundUpperLimitValue->setMinimumWidth(80);

        horizontalLayout_6->addWidget(backgroundUpperLimitValue);

        backgroundUpperLimitSlider = new QSlider(layoutWidget_3);
        backgroundUpperLimitSlider->setObjectName("backgroundUpperLimitSlider");
        backgroundUpperLimitSlider->setMaximum(1000);
        backgroundUpperLimitSlider->setValue(100);
        backgroundUpperLimitSlider->setOrientation(Qt::Horizontal);
        backgroundUpperLimitSlider->setMinimumWidth(150);

        horizontalLayout_6->addWidget(backgroundUpperLimitSlider);

        horizontalLayout_6->setStretch(1, 1);
        horizontalLayout_6->setStretch(2, 3);
        rootLayout->addWidget(layoutWidget_3);

        backgroundRoiGroupBox = new QGroupBox(BackgroundNeutralizationProcess);
        backgroundRoiGroupBox->setObjectName("backgroundRoiGroupBox");
        backgroundRoiGroupBox->setCheckable(true);
        backgroundRoiGroupBox->setChecked(false);

        auto* backgroundRoiLayout = new QVBoxLayout(backgroundRoiGroupBox);
        backgroundRoiLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        backgroundRoiLayout->setSpacing(t.spacing.sm);

        layoutWidget_4 = new QWidget(backgroundRoiGroupBox);
        layoutWidget_4->setObjectName("layoutWidget_4");
        gridLayout_3 = new QGridLayout(layoutWidget_4);
        gridLayout_3->setObjectName("gridLayout_3");
        gridLayout_3->setContentsMargins(0, 0, 0, 0);
        gridLayout_3->setHorizontalSpacing(t.spacing.md);
        gridLayout_3->setVerticalSpacing(t.spacing.sm);

        label_19 = new QLabel(layoutWidget_4);
        label_19->setObjectName("label_19");
        gridLayout_3->addWidget(label_19, 0, 0, 1, 1);

        backgroundRoiLeftValue = new QSpinBox(layoutWidget_4);
        backgroundRoiLeftValue->setObjectName("backgroundRoiLeftValue");
        backgroundRoiLeftValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiLeftValue->setMaximumWidth(kRoiSpinBoxWidth);
        gridLayout_3->addWidget(backgroundRoiLeftValue, 0, 1, 1, 1);

        label_17 = new QLabel(layoutWidget_4);
        label_17->setObjectName("label_17");
        gridLayout_3->addWidget(label_17, 0, 3, 1, 1);

        backgroundRoiTopValue = new QSpinBox(layoutWidget_4);
        backgroundRoiTopValue->setObjectName("backgroundRoiTopValue");
        backgroundRoiTopValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiTopValue->setMaximumWidth(kRoiSpinBoxWidth);
        gridLayout_3->addWidget(backgroundRoiTopValue, 0, 5, 1, 1);

        label_18 = new QLabel(layoutWidget_4);
        label_18->setObjectName("label_18");
        gridLayout_3->addWidget(label_18, 1, 0, 1, 1);

        backgroundRoiWidthValue = new QSpinBox(layoutWidget_4);
        backgroundRoiWidthValue->setObjectName("backgroundRoiWidthValue");
        backgroundRoiWidthValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiWidthValue->setMaximumWidth(kRoiSpinBoxWidth);
        gridLayout_3->addWidget(backgroundRoiWidthValue, 1, 1, 1, 1);

        label_16 = new QLabel(layoutWidget_4);
        label_16->setObjectName("label_16");
        gridLayout_3->addWidget(label_16, 1, 3, 1, 1);

        backgroundRoiHeightValue = new QSpinBox(layoutWidget_4);
        backgroundRoiHeightValue->setObjectName("backgroundRoiHeightValue");
        backgroundRoiHeightValue->setMinimumWidth(kRoiSpinBoxWidth);
        backgroundRoiHeightValue->setMaximumWidth(kRoiSpinBoxWidth);
        gridLayout_3->addWidget(backgroundRoiHeightValue, 1, 5, 1, 1);

        horizontalSpacer_7 = new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        gridLayout_3->addItem(horizontalSpacer_7, 0, 2, 1, 1);

        horizontalSpacer_8 = new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        gridLayout_3->addItem(horizontalSpacer_8, 1, 2, 1, 1);

        horizontalSpacer_6 = new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        gridLayout_3->addItem(horizontalSpacer_6, 0, 6, 1, 1);

        horizontalSpacer_5 = new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        gridLayout_3->addItem(horizontalSpacer_5, 1, 6, 1, 1);

        gridLayout_3->setColumnStretch(1, 2);
        gridLayout_3->setColumnStretch(2, 1);
        gridLayout_3->setColumnStretch(5, 2);
        gridLayout_3->setColumnStretch(6, 1);

        backgroundRoiLayout->addWidget(layoutWidget_4);

        backgroundRoiSelectionButton = new QPushButton(backgroundRoiGroupBox);
        backgroundRoiSelectionButton->setObjectName("backgroundRoiSelectionButton");
        backgroundRoiSelectionButton->setCheckable(true);
        auto* roiButtonLayout = new QHBoxLayout();
        roiButtonLayout->setContentsMargins(0, 0, 0, 0);
        roiButtonLayout->addStretch(1);
        roiButtonLayout->addWidget(backgroundRoiSelectionButton);
        backgroundRoiLayout->addLayout(roiButtonLayout);

        rootLayout->addWidget(backgroundRoiGroupBox);

        widget = new QWidget(BackgroundNeutralizationProcess);
        widget->setObjectName("widget");
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout->setSpacing(t.spacing.md);
        label_3 = new QLabel(widget);
        label_3->setObjectName("label_3");

        horizontalLayout->addWidget(label_3);

        targetImageSelector = new QComboBox(widget);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout->addWidget(targetImageSelector);

        horizontalLayout->setStretch(1, 1);
        rootLayout->addWidget(widget);

        rootLayout->addItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

        buttonBox = new QDialogButtonBox(BackgroundNeutralizationProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(BackgroundNeutralizationProcess);

        retranslateUi(BackgroundNeutralizationProcess);

        QMetaObject::connectSlotsByName(BackgroundNeutralizationProcess);
    } // setupUi

    void retranslateUi(QWidget* BackgroundNeutralizationProcess)
    {
        BackgroundNeutralizationProcess->setWindowTitle(
            QCoreApplication::translate("BackgroundNeutralizationProcess", "Background Neutralization", nullptr));
        label_12->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Reference image:", nullptr));
        label_15->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Upper Limit:", nullptr));
        label_14->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Lower Limit:", nullptr));
        backgroundRoiGroupBox->setTitle(
            QCoreApplication::translate("BackgroundNeutralizationProcess", "Region of interest", nullptr));
        backgroundRoiSelectionButton->setText(
            QCoreApplication::translate("BackgroundNeutralizationProcess", "From Selection", nullptr));
        label_16->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Height:", nullptr));
        label_17->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Top:", nullptr));
        label_18->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Width:", nullptr));
        label_19->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Left:", nullptr));
        label_3->setText(QCoreApplication::translate("BackgroundNeutralizationProcess", "Target Image ", nullptr));

        backgroundReferenceImageSelector->setToolTip(
            QCoreApplication::translate("BackgroundNeutralizationProcess",
                                        "Select the image used to sample background reference values.", nullptr));
        backgroundLowerLimitValue->setToolTip(QCoreApplication::translate(
            "BackgroundNeutralizationProcess", "Lower intensity threshold for background sampling.", nullptr));
        backgroundLowerLimitSlider->setToolTip(QCoreApplication::translate(
            "BackgroundNeutralizationProcess", "Lower intensity threshold for background sampling.", nullptr));
        backgroundUpperLimitValue->setToolTip(QCoreApplication::translate(
            "BackgroundNeutralizationProcess", "Upper intensity threshold for background sampling.", nullptr));
        backgroundUpperLimitSlider->setToolTip(QCoreApplication::translate(
            "BackgroundNeutralizationProcess", "Upper intensity threshold for background sampling.", nullptr));
        backgroundRoiSelectionButton->setToolTip(QCoreApplication::translate(
            "BackgroundNeutralizationProcess", "Use an interactive image selection as background ROI.", nullptr));
        targetImageSelector->setToolTip(QCoreApplication::translate("BackgroundNeutralizationProcess",
                                                                    "Choose the open image to process.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Cancel))
            btn->setToolTip(QCoreApplication::translate("BackgroundNeutralizationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("BackgroundNeutralizationProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline BackgroundNeutralizationProcessUiElements buildBackgroundNeutralizationProcessUi(QWidget* parent)
{
    BackgroundNeutralizationProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
