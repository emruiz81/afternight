// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ProfiledStarMaskCreatorControlUiElements
{
public:
    QDialogButtonBox* buttonBox;
    QGroupBox* binaryAreasGroup;
    QWidget* widget;
    QGridLayout* gridLayout;
    QLabel* label;
    QDoubleSpinBox* scaleFactorValue;
    QSlider* scaleFactorSlider;
    QLabel* label_2;
    QDoubleSpinBox* softenEdgesValue;
    QSlider* softenEdgesSlider;
    QGroupBox* shapedAreasGroup;
    QWidget* widget1;
    QGridLayout* gridLayout_2;
    QLabel* label_3;
    QDoubleSpinBox* percentPixelIncreaseValue;
    QSlider* percentPixelIncreaseSlider;
    QLabel* label_4;
    QSpinBox* pixelIncreaseValue;
    QSlider* pixelIncreaseSlider;
    QWidget* widget2;
    QHBoxLayout* horizontalLayout;
    QLabel* label_5;
    QComboBox* shapeSelector;

    void setupUi(QWidget* ProfiledStarMaskCreatorControl)
    {
        const auto& t = an::ui::tokens();

        if (ProfiledStarMaskCreatorControl->objectName().isEmpty())
            ProfiledStarMaskCreatorControl->setObjectName("ProfiledStarMaskCreatorControl");
        ProfiledStarMaskCreatorControl->resize(450, 300);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(ProfiledStarMaskCreatorControl->sizePolicy().hasHeightForWidth());
        ProfiledStarMaskCreatorControl->setSizePolicy(sizePolicy);
        ProfiledStarMaskCreatorControl->setMinimumSize(QSize(420, 280));

        auto* rootLayout = new QVBoxLayout(ProfiledStarMaskCreatorControl);
        rootLayout->setObjectName("rootLayout");
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        binaryAreasGroup = new QGroupBox(ProfiledStarMaskCreatorControl);
        binaryAreasGroup->setObjectName("binaryAreasGroup");
        binaryAreasGroup->setCheckable(true);
        auto* binaryLayout = new QVBoxLayout(binaryAreasGroup);
        binaryLayout->setObjectName("binaryLayout");
        binaryLayout->setSpacing(t.spacing.sm);
        binaryLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        widget = new QWidget(binaryAreasGroup);
        widget->setObjectName("widget");
        gridLayout = new QGridLayout(widget);
        gridLayout->setObjectName("gridLayout");
        gridLayout->setHorizontalSpacing(t.spacing.md);
        gridLayout->setVerticalSpacing(t.spacing.sm);
        gridLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(widget);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        scaleFactorValue = new QDoubleSpinBox(widget);
        scaleFactorValue->setObjectName("scaleFactorValue");
        scaleFactorValue->setMaximum(2.000000000000000);
        scaleFactorValue->setSingleStep(0.010000000000000);
        scaleFactorValue->setValue(1.000000000000000);

        gridLayout->addWidget(scaleFactorValue, 0, 1, 1, 1);

        scaleFactorSlider = new QSlider(widget);
        scaleFactorSlider->setObjectName("scaleFactorSlider");
        scaleFactorSlider->setMaximum(2000);
        scaleFactorSlider->setValue(1000);
        scaleFactorSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(scaleFactorSlider, 0, 2, 1, 1);

        label_2 = new QLabel(widget);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        softenEdgesValue = new QDoubleSpinBox(widget);
        softenEdgesValue->setObjectName("softenEdgesValue");
        softenEdgesValue->setMaximum(10.000000000000000);
        softenEdgesValue->setSingleStep(0.010000000000000);

        gridLayout->addWidget(softenEdgesValue, 1, 1, 1, 1);

        softenEdgesSlider = new QSlider(widget);
        softenEdgesSlider->setObjectName("softenEdgesSlider");
        softenEdgesSlider->setMaximum(1000);
        softenEdgesSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(softenEdgesSlider, 1, 2, 1, 1);
        gridLayout->setColumnStretch(0, 2);
        gridLayout->setColumnStretch(1, 1);
        gridLayout->setColumnStretch(2, 6);
        binaryLayout->addWidget(widget);

        shapedAreasGroup = new QGroupBox(ProfiledStarMaskCreatorControl);
        shapedAreasGroup->setObjectName("shapedAreasGroup");
        shapedAreasGroup->setCheckable(true);
        shapedAreasGroup->setChecked(false);
        auto* shapedLayout = new QVBoxLayout(shapedAreasGroup);
        shapedLayout->setObjectName("shapedLayout");
        shapedLayout->setSpacing(t.spacing.sm);
        shapedLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        widget2 = new QWidget(shapedAreasGroup);
        widget2->setObjectName("widget2");
        horizontalLayout = new QHBoxLayout(widget2);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout->setSpacing(t.spacing.sm);
        label_5 = new QLabel(widget2);
        label_5->setObjectName("label_5");
        horizontalLayout->addWidget(label_5);
        shapeSelector = new QComboBox(widget2);
        shapeSelector->setObjectName("shapeSelector");
        horizontalLayout->addWidget(shapeSelector);
        horizontalLayout->setStretch(0, 1);
        horizontalLayout->setStretch(1, 2);
        shapedLayout->addWidget(widget2);

        widget1 = new QWidget(shapedAreasGroup);
        widget1->setObjectName("widget1");
        gridLayout_2 = new QGridLayout(widget1);
        gridLayout_2->setObjectName("gridLayout_2");
        gridLayout_2->setHorizontalSpacing(t.spacing.md);
        gridLayout_2->setVerticalSpacing(t.spacing.sm);
        gridLayout_2->setContentsMargins(0, 0, 0, 0);
        label_3 = new QLabel(widget1);
        label_3->setObjectName("label_3");

        gridLayout_2->addWidget(label_3, 0, 0, 1, 1);

        percentPixelIncreaseValue = new QDoubleSpinBox(widget1);
        percentPixelIncreaseValue->setObjectName("percentPixelIncreaseValue");
        percentPixelIncreaseValue->setMaximum(1.000000000000000);
        percentPixelIncreaseValue->setSingleStep(0.010000000000000);
        percentPixelIncreaseValue->setValue(0.500000000000000);

        gridLayout_2->addWidget(percentPixelIncreaseValue, 0, 1, 1, 1);

        percentPixelIncreaseSlider = new QSlider(widget1);
        percentPixelIncreaseSlider->setObjectName("percentPixelIncreaseSlider");
        percentPixelIncreaseSlider->setMaximum(1000);
        percentPixelIncreaseSlider->setValue(500);
        percentPixelIncreaseSlider->setOrientation(Qt::Horizontal);

        gridLayout_2->addWidget(percentPixelIncreaseSlider, 0, 2, 1, 1);

        label_4 = new QLabel(widget1);
        label_4->setObjectName("label_4");

        gridLayout_2->addWidget(label_4, 1, 0, 1, 1);

        pixelIncreaseValue = new QSpinBox(widget1);
        pixelIncreaseValue->setObjectName("pixelIncreaseValue");
        pixelIncreaseValue->setMaximum(20);

        gridLayout_2->addWidget(pixelIncreaseValue, 1, 1, 1, 1);

        pixelIncreaseSlider = new QSlider(widget1);
        pixelIncreaseSlider->setObjectName("pixelIncreaseSlider");
        pixelIncreaseSlider->setMaximum(20);
        pixelIncreaseSlider->setOrientation(Qt::Horizontal);

        gridLayout_2->addWidget(pixelIncreaseSlider, 1, 2, 1, 1);
        gridLayout_2->setColumnStretch(0, 2);
        gridLayout_2->setColumnStretch(1, 1);
        gridLayout_2->setColumnStretch(2, 6);
        shapedLayout->addWidget(widget1);

        rootLayout->addWidget(binaryAreasGroup);
        rootLayout->addWidget(shapedAreasGroup, 1);

        buttonBox = new QDialogButtonBox(ProfiledStarMaskCreatorControl);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(ProfiledStarMaskCreatorControl);

        QMetaObject::connectSlotsByName(ProfiledStarMaskCreatorControl);
    } // setupUi

    void retranslateUi(QWidget* ProfiledStarMaskCreatorControl)
    {
        ProfiledStarMaskCreatorControl->setWindowTitle(
            QCoreApplication::translate("ProfiledStarMaskCreatorControl", "Dialog", nullptr));
        binaryAreasGroup->setTitle(
            QCoreApplication::translate("ProfiledStarMaskCreatorControl", "Binary Areas", nullptr));
        label->setText(QCoreApplication::translate("ProfiledStarMaskCreatorControl", "Scale Factor", nullptr));
        label_2->setText(QCoreApplication::translate("ProfiledStarMaskCreatorControl", "Soften Edges", nullptr));
        shapedAreasGroup->setTitle(
            QCoreApplication::translate("ProfiledStarMaskCreatorControl", "Shaped Areas", nullptr));
        label_3->setText(QCoreApplication::translate("ProfiledStarMaskCreatorControl", "% Pixel Increase", nullptr));
        label_4->setText(QCoreApplication::translate("ProfiledStarMaskCreatorControl", "Pixel Increase", nullptr));
        label_5->setText(QCoreApplication::translate("ProfiledStarMaskCreatorControl", "Shape", nullptr));
    } // retranslateUi
};

inline ProfiledStarMaskCreatorControlUiElements buildProfiledStarMaskCreatorControlUi(QWidget* parent)
{
    ProfiledStarMaskCreatorControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
