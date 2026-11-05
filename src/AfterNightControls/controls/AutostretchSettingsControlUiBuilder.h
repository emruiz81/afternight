// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
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
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class AutostretchSettingsControlUiElements
{
public:
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QGroupBox* groupBox;
    QWidget* layoutWidget_2;
    QGridLayout* gridLayout;
    QCheckBox* linkedChannels;
    QSlider* shadowsClippingSlider;
    QLabel* label;
    QSlider* targetBackgroundSlider;
    QLabel* label_2;
    QCheckBox* neutralizeBackground;
    QDoubleSpinBox* targetBackgroundValue;
    QDoubleSpinBox* shadowsClippingValue;
    QCheckBox* colorCalibration;
    QHBoxLayout* horizontalLayout_2;
    QGroupBox* groupBox_2;
    QWidget* layoutWidget_1;
    QGridLayout* gridLayout_2;
    QLabel* label_4;
    QLabel* label_7;
    QSlider* saturationSlider;
    QDoubleSpinBox* saturationBackgroundValue;
    QSlider* brightnessSlider;
    QLabel* label_6;
    QLabel* label_3;
    QSlider* contrastSlider;
    QSlider* saturationBackgroundSlider;
    QDoubleSpinBox* saturationValue;
    QSpinBox* brightnessValue;
    QSpinBox* contrastValue;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* autostretchBanner;
    QSpacerItem* horizontalSpacer;
    QPushButton* applyStretchButton;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* AutostretchSettingsControl)
    {
        if (AutostretchSettingsControl->objectName().isEmpty())
            AutostretchSettingsControl->setObjectName("AutostretchSettingsControl");
        AutostretchSettingsControl->setFixedSize(QSize(500, 550));
        AutostretchSettingsControl->setSizePolicy(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Fixed);
        const auto& t = an::ui::tokens();
        auto* mainLayout = new QVBoxLayout(AutostretchSettingsControl);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        verticalLayoutWidget = new QWidget(AutostretchSettingsControl);
        verticalLayoutWidget->setObjectName("verticalLayoutWidget");
        mainLayout->addWidget(verticalLayoutWidget);
        verticalLayout = new QVBoxLayout(verticalLayoutWidget);
        verticalLayout->setSpacing(t.spacing.md);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        groupBox = new QGroupBox(verticalLayoutWidget);
        groupBox->setObjectName("groupBox");
        groupBox->setMinimumSize(QSize(400, 190));
        auto* groupBoxLayout = new QVBoxLayout(groupBox);
        groupBoxLayout->setObjectName("groupBoxLayout");
        groupBoxLayout->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        groupBoxLayout->setSpacing(0);
        layoutWidget_2 = new QWidget(groupBox);
        layoutWidget_2->setObjectName("layoutWidget_2");
        groupBoxLayout->addWidget(layoutWidget_2);
        gridLayout = new QGridLayout(layoutWidget_2);
        gridLayout->setObjectName("gridLayout");
        gridLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        linkedChannels = new QCheckBox(layoutWidget_2);
        linkedChannels->setObjectName("linkedChannels");

        gridLayout->addWidget(linkedChannels, 2, 0, 1, 1);

        shadowsClippingSlider = new QSlider(layoutWidget_2);
        shadowsClippingSlider->setObjectName("shadowsClippingSlider");
        shadowsClippingSlider->setMinimum(-10000);
        shadowsClippingSlider->setMaximum(0);
        shadowsClippingSlider->setValue(-2800);
        shadowsClippingSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(shadowsClippingSlider, 1, 2, 1, 1);

        label = new QLabel(layoutWidget_2);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        targetBackgroundSlider = new QSlider(layoutWidget_2);
        targetBackgroundSlider->setObjectName("targetBackgroundSlider");
        targetBackgroundSlider->setMinimum(0);
        targetBackgroundSlider->setMaximum(1000);
        targetBackgroundSlider->setValue(250);
        targetBackgroundSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(targetBackgroundSlider, 0, 2, 1, 1);

        label_2 = new QLabel(layoutWidget_2);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        neutralizeBackground = new QCheckBox(layoutWidget_2);
        neutralizeBackground->setObjectName("neutralizeBackground");

        gridLayout->addWidget(neutralizeBackground, 3, 0, 1, 1);

        targetBackgroundValue = new QDoubleSpinBox(layoutWidget_2);
        targetBackgroundValue->setObjectName("targetBackgroundValue");
        targetBackgroundValue->setMinimum(0.000000000000000);
        targetBackgroundValue->setMaximum(1.000000000000000);
        targetBackgroundValue->setSingleStep(0.020000000000000);
        targetBackgroundValue->setValue(0.250000000000000);

        gridLayout->addWidget(targetBackgroundValue, 0, 1, 1, 1);

        shadowsClippingValue = new QDoubleSpinBox(layoutWidget_2);
        shadowsClippingValue->setObjectName("shadowsClippingValue");
        shadowsClippingValue->setMinimum(-10.000000000000000);
        shadowsClippingValue->setMaximum(0.000000000000000);
        shadowsClippingValue->setSingleStep(0.200000000000000);
        shadowsClippingValue->setValue(-2.800000000000000);

        gridLayout->addWidget(shadowsClippingValue, 1, 1, 1, 1);

        colorCalibration = new QCheckBox(layoutWidget_2);
        colorCalibration->setObjectName("colorCalibration");

        gridLayout->addWidget(colorCalibration, 4, 0, 1, 1);

        horizontalLayout->addWidget(groupBox);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        groupBox_2 = new QGroupBox(verticalLayoutWidget);
        groupBox_2->setObjectName("groupBox_2");
        groupBox_2->setMinimumSize(QSize(400, 120));
        groupBox_2->setCheckable(false);
        groupBox_2->setChecked(false);
        auto* groupBox2Layout = new QVBoxLayout(groupBox_2);
        groupBox2Layout->setObjectName("groupBox2Layout");
        groupBox2Layout->setContentsMargins(0, t.spacing.sm, 0, 0);
        groupBox2Layout->setSpacing(0);
        layoutWidget_1 = new QWidget(groupBox_2);
        layoutWidget_1->setObjectName("layoutWidget_1");
        groupBox2Layout->addWidget(layoutWidget_1);
        gridLayout_2 = new QGridLayout(layoutWidget_1);
        gridLayout_2->setObjectName("gridLayout_2");
        gridLayout_2->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label_4 = new QLabel(layoutWidget_1);
        label_4->setObjectName("label_4");

        gridLayout_2->addWidget(label_4, 1, 0, 1, 1);

        label_7 = new QLabel(layoutWidget_1);
        label_7->setObjectName("label_7");

        gridLayout_2->addWidget(label_7, 3, 0, 1, 1);

        saturationSlider = new QSlider(layoutWidget_1);
        saturationSlider->setObjectName("saturationSlider");
        saturationSlider->setMinimum(-1000);
        saturationSlider->setMaximum(2000);
        saturationSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout_2->addWidget(saturationSlider, 0, 2, 1, 1);

        saturationBackgroundValue = new QDoubleSpinBox(layoutWidget_1);
        saturationBackgroundValue->setObjectName("saturationBackgroundValue");
        saturationBackgroundValue->setMaximum(2.000000000000000);
        saturationBackgroundValue->setSingleStep(0.020000000000000);
        saturationBackgroundValue->setValue(1.000000000000000);

        gridLayout_2->addWidget(saturationBackgroundValue, 1, 1, 1, 1);

        brightnessSlider = new QSlider(layoutWidget_1);
        brightnessSlider->setObjectName("brightnessSlider");
        brightnessSlider->setMinimum(-150);
        brightnessSlider->setMaximum(150);
        brightnessSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout_2->addWidget(brightnessSlider, 2, 2, 1, 1);

        label_6 = new QLabel(layoutWidget_1);
        label_6->setObjectName("label_6");

        gridLayout_2->addWidget(label_6, 2, 0, 1, 1);

        label_3 = new QLabel(layoutWidget_1);
        label_3->setObjectName("label_3");

        gridLayout_2->addWidget(label_3, 0, 0, 1, 1);

        contrastSlider = new QSlider(layoutWidget_1);
        contrastSlider->setObjectName("contrastSlider");
        contrastSlider->setMinimum(-100);
        contrastSlider->setMaximum(100);
        contrastSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout_2->addWidget(contrastSlider, 3, 2, 1, 1);

        saturationBackgroundSlider = new QSlider(layoutWidget_1);
        saturationBackgroundSlider->setObjectName("saturationBackgroundSlider");
        saturationBackgroundSlider->setMaximum(2000);
        saturationBackgroundSlider->setValue(1000);
        saturationBackgroundSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout_2->addWidget(saturationBackgroundSlider, 1, 2, 1, 1);

        saturationValue = new QDoubleSpinBox(layoutWidget_1);
        saturationValue->setObjectName("saturationValue");
        saturationValue->setMinimum(-1.000000000000000);
        saturationValue->setMaximum(2.000000000000000);
        saturationValue->setSingleStep(0.050000000000000);

        gridLayout_2->addWidget(saturationValue, 0, 1, 1, 1);

        brightnessValue = new QSpinBox(layoutWidget_1);
        brightnessValue->setObjectName("brightnessValue");
        brightnessValue->setMinimum(-150);
        brightnessValue->setMaximum(150);

        gridLayout_2->addWidget(brightnessValue, 2, 1, 1, 1);

        contrastValue = new QSpinBox(layoutWidget_1);
        contrastValue->setObjectName("contrastValue");
        contrastValue->setMinimum(-100);
        contrastValue->setMaximum(100);

        gridLayout_2->addWidget(contrastValue, 3, 1, 1, 1);

        horizontalLayout_2->addWidget(groupBox_2);

        verticalLayout->addLayout(horizontalLayout_2);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        autostretchBanner = new QCheckBox(verticalLayoutWidget);
        autostretchBanner->setObjectName("autostretchBanner");
        autostretchBanner->setChecked(false);

        horizontalLayout_4->addWidget(autostretchBanner);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer);

        applyStretchButton = new QPushButton(verticalLayoutWidget);
        applyStretchButton->setObjectName("applyStretchButton");

        horizontalLayout_4->addWidget(applyStretchButton);

        verticalLayout->addLayout(horizontalLayout_4);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Orientation::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Close |
                                      QDialogButtonBox::StandardButton::Reset);

        horizontalLayout_3->addWidget(buttonBox);

        verticalLayout->addLayout(horizontalLayout_3);

        verticalLayout->setStretch(0, 1);
        verticalLayout->setStretch(1, 1);

        retranslateUi(AutostretchSettingsControl);

        QMetaObject::connectSlotsByName(AutostretchSettingsControl);
    } // setupUi

    void retranslateUi(QWidget* AutostretchSettingsControl)
    {
        AutostretchSettingsControl->setWindowTitle(
            QCoreApplication::translate("AutostretchSettingsControl", "Dialog", nullptr));
        groupBox->setTitle(QCoreApplication::translate("AutostretchSettingsControl", "Pre Processing", nullptr));
        linkedChannels->setText(QCoreApplication::translate("AutostretchSettingsControl", "Linked channels", nullptr));
        label->setText(QCoreApplication::translate("AutostretchSettingsControl", "Target Background", nullptr));
        label_2->setText(QCoreApplication::translate("AutostretchSettingsControl", "Shadows Clipping", nullptr));
        neutralizeBackground->setText(
            QCoreApplication::translate("AutostretchSettingsControl", "Neutralize Background", nullptr));
        colorCalibration->setText(
            QCoreApplication::translate("AutostretchSettingsControl", "Color Calibration", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("AutostretchSettingsControl", "Post Processing", nullptr));
        label_4->setText(QCoreApplication::translate("AutostretchSettingsControl", "Sat Bg.", nullptr));
        label_7->setText(QCoreApplication::translate("AutostretchSettingsControl", "Contrast", nullptr));
        label_6->setText(QCoreApplication::translate("AutostretchSettingsControl", "Brightness", nullptr));
        label_3->setText(QCoreApplication::translate("AutostretchSettingsControl", "Saturation", nullptr));
        autostretchBanner->setText(
            QCoreApplication::translate("AutostretchSettingsControl", "Print Autostretch Banner", nullptr));
        applyStretchButton->setText(
            QCoreApplication::translate("AutostretchSettingsControl", "Apply Stretch", nullptr));
    } // retranslateUi
};

inline AutostretchSettingsControlUiElements buildAutostretchSettingsControlUi(QWidget* parent)
{
    AutostretchSettingsControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
