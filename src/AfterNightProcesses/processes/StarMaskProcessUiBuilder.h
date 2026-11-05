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
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class StarMaskProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QSlider* percentIncreaseSlider;
    QLabel* label_5;
    QLabel* label_6;
    QSpinBox* minSizeValue;
    QSlider* minSizeSlider;
    QSpinBox* pixelIncreaseValue;
    QDoubleSpinBox* percentIncreaseValue;
    QSlider* pixelIncreaseSlider;
    QLabel* label_7;
    QLabel* label_8;
    QSpinBox* maxSizeValue;
    QSlider* maxSizeSlider;
    QHBoxLayout* horizontalLayout;
    QLabel* label_9;
    QComboBox* starShapeSelector;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QLabel* label_3;
    QComboBox* targetImageSelector;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* StarMaskProcess)
    {
        const auto& t = an::ui::tokens();

        if (StarMaskProcess->objectName().isEmpty())
            StarMaskProcess->setObjectName("StarMaskProcess");
        StarMaskProcess->resize(760, 360);
        horizontalLayoutWidget_4 = StarMaskProcess;
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
        percentIncreaseSlider = new QSlider(horizontalLayoutWidget_4);
        percentIncreaseSlider->setObjectName("percentIncreaseSlider");
        percentIncreaseSlider->setMinimum(10);
        percentIncreaseSlider->setMaximum(1000);
        percentIncreaseSlider->setValue(500);
        percentIncreaseSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(percentIncreaseSlider, 2, 2, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 2, 0, 1, 1);

        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 4, 0, 1, 1);

        minSizeValue = new QSpinBox(horizontalLayoutWidget_4);
        minSizeValue->setObjectName("minSizeValue");
        minSizeValue->setValue(2);
        minSizeValue->setMinimumWidth(80);

        gridLayout->addWidget(minSizeValue, 4, 1, 1, 1);

        minSizeSlider = new QSlider(horizontalLayoutWidget_4);
        minSizeSlider->setObjectName("minSizeSlider");
        minSizeSlider->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
        minSizeSlider->setMinimum(0);
        minSizeSlider->setMaximum(99);
        minSizeSlider->setValue(2);
        minSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(minSizeSlider, 4, 2, 1, 1);

        pixelIncreaseValue = new QSpinBox(horizontalLayoutWidget_4);
        pixelIncreaseValue->setObjectName("pixelIncreaseValue");
        pixelIncreaseValue->setMinimumWidth(80);

        gridLayout->addWidget(pixelIncreaseValue, 3, 1, 1, 1);

        percentIncreaseValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        percentIncreaseValue->setObjectName("percentIncreaseValue");
        percentIncreaseValue->setMinimum(0.010000000000000);
        percentIncreaseValue->setMaximum(1.000000000000000);
        percentIncreaseValue->setSingleStep(0.050000000000000);
        percentIncreaseValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        percentIncreaseValue->setValue(0.500000000000000);
        percentIncreaseValue->setMinimumWidth(80);

        gridLayout->addWidget(percentIncreaseValue, 2, 1, 1, 1);

        pixelIncreaseSlider = new QSlider(horizontalLayoutWidget_4);
        pixelIncreaseSlider->setObjectName("pixelIncreaseSlider");
        pixelIncreaseSlider->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
        pixelIncreaseSlider->setMinimum(0);
        pixelIncreaseSlider->setMaximum(99);
        pixelIncreaseSlider->setValue(0);
        pixelIncreaseSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(pixelIncreaseSlider, 3, 2, 1, 1);

        label_7 = new QLabel(horizontalLayoutWidget_4);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 3, 0, 1, 1);

        label_8 = new QLabel(horizontalLayoutWidget_4);
        label_8->setObjectName("label_8");

        gridLayout->addWidget(label_8, 5, 0, 1, 1);

        maxSizeValue = new QSpinBox(horizontalLayoutWidget_4);
        maxSizeValue->setObjectName("maxSizeValue");
        maxSizeValue->setValue(25);
        maxSizeValue->setMinimumWidth(80);

        gridLayout->addWidget(maxSizeValue, 5, 1, 1, 1);

        maxSizeSlider = new QSlider(horizontalLayoutWidget_4);
        maxSizeSlider->setObjectName("maxSizeSlider");
        maxSizeSlider->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
        maxSizeSlider->setMinimum(0);
        gridLayout->setColumnStretch(0, 2);
        gridLayout->setColumnStretch(1, 1);
        gridLayout->setColumnStretch(2, 7);

        maxSizeSlider->setMaximum(99);
        maxSizeSlider->setValue(25);
        maxSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(maxSizeSlider, 5, 2, 1, 1);

        verticalLayout->addLayout(gridLayout);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        label_9 = new QLabel(horizontalLayoutWidget_4);
        label_9->setObjectName("label_9");

        horizontalLayout->addWidget(label_9);

        starShapeSelector = new QComboBox(horizontalLayoutWidget_4);
        starShapeSelector->setObjectName("starShapeSelector");

        horizontalLayout->addWidget(starShapeSelector);

        horizontalLayout->setStretch(1, 1);

        verticalLayout->addLayout(horizontalLayout);

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

        retranslateUi(StarMaskProcess);

        QMetaObject::connectSlotsByName(StarMaskProcess);
    } // setupUi

    void retranslateUi(QWidget* StarMaskProcess)
    {
        StarMaskProcess->setWindowTitle(QCoreApplication::translate("StarMaskProcess", "Star Mask", nullptr));
        label_5->setText(QCoreApplication::translate("StarMaskProcess", "Percent Increase", nullptr));
        label_6->setText(QCoreApplication::translate("StarMaskProcess", "Min Size", nullptr));
        label_7->setText(QCoreApplication::translate("StarMaskProcess", "Pixel Increase", nullptr));
        label_8->setText(QCoreApplication::translate("StarMaskProcess", "Max Size", nullptr));
        label_9->setText(QCoreApplication::translate("StarMaskProcess", "Star Shape", nullptr));
        label_3->setText(QCoreApplication::translate("StarMaskProcess", "Target Image", nullptr));
        preview->setText(QCoreApplication::translate("StarMaskProcess", "Preview", nullptr));

        // Tooltips
#if QT_CONFIG(tooltip)
        percentIncreaseValue->setToolTip(QCoreApplication::translate(
            "StarMaskProcess", "Grow each star mask region by this percentage of its radius.", nullptr));
        percentIncreaseSlider->setToolTip(percentIncreaseValue->toolTip());
        pixelIncreaseValue->setToolTip(QCoreApplication::translate(
            "StarMaskProcess", "Grow each star mask region by this fixed number of pixels.", nullptr));
        pixelIncreaseSlider->setToolTip(pixelIncreaseValue->toolTip());
        minSizeValue->setToolTip(QCoreApplication::translate(
            "StarMaskProcess", "Minimum star radius in pixels to include in the mask.", nullptr));
        minSizeSlider->setToolTip(minSizeValue->toolTip());
        maxSizeValue->setToolTip(QCoreApplication::translate(
            "StarMaskProcess", "Maximum star radius in pixels to include in the mask.", nullptr));
        maxSizeSlider->setToolTip(maxSizeValue->toolTip());
        starShapeSelector->setToolTip(QCoreApplication::translate(
            "StarMaskProcess", "Shape of the brightness falloff applied to each star in the mask.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("StarMaskProcess", "Image to apply the star mask process to.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("StarMaskProcess", "Enable real-time preview of the star mask.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("StarMaskProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("StarMaskProcess",
                                                        "Reset all parameters to their default values.", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline StarMaskProcessUiElements buildStarMaskProcessUi(QWidget* parent)
{
    StarMaskProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
