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
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class StarsFilterControlV1UiElements
{
public:
    QDialogButtonBox* buttonBox;
    QGroupBox* groupBox;
    QGroupBox* groupBox_3;
    QWidget* layoutWidget1;
    QGridLayout* gridLayout1;
    QLabel* label;
    QDoubleSpinBox* intensityMinValue;
    QSlider* intensityMinSlider;
    QLabel* label_2;
    QDoubleSpinBox* intensityMaxValue;
    QSlider* intensityMaxSlider;
    QGroupBox* groupBox_4;
    QWidget* layoutWidget2;
    QGridLayout* gridLayout1_2;
    QLabel* label_3;
    QDoubleSpinBox* qualityMinValue;
    QSlider* qualityMinSlider;
    QLabel* label_4;
    QDoubleSpinBox* qualityMaxValue;
    QSlider* qualityMaxSlider;
    QGroupBox* groupBox_5;
    QWidget* layoutWidget_2;
    QGridLayout* gridLayout1_3;
    QLabel* label_5;
    QDoubleSpinBox* radiusMinValue;
    QSlider* radiusMinSlider;
    QLabel* label_6;
    QDoubleSpinBox* radiusMaxValue;
    QSlider* radiusMaxSlider;
    QGroupBox* groupBox_2;
    QCheckBox* previewCheck;
    QWidget* layoutWidget3;
    QHBoxLayout* horizontalLayout;
    QLabel* label_7;
    QDoubleSpinBox* randomSelectionValue;
    QSlider* randomSelectionSlider;
    QWidget* layoutWidget;
    QHBoxLayout* horizontalLayout_2;
    QLabel* filteredStarsLabel;
    QLineEdit* filteredStarsTxt;
    QCheckBox* includeNeighborsCheck;

    void setupUi(QWidget* StarsFilterControlV1)
    {
        const auto& t = an::ui::tokens();

        if (StarsFilterControlV1->objectName().isEmpty())
            StarsFilterControlV1->setObjectName("StarsFilterControlV1");
        StarsFilterControlV1->resize(589, 585);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(StarsFilterControlV1->sizePolicy().hasHeightForWidth());
        StarsFilterControlV1->setSizePolicy(sizePolicy);
        StarsFilterControlV1->setMinimumSize(QSize(540, 500));
        auto* rootLayout = new QVBoxLayout(StarsFilterControlV1);
        rootLayout->setObjectName("rootLayout");
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        groupBox = new QGroupBox(StarsFilterControlV1);
        groupBox->setObjectName("groupBox");
        auto* thresholdsLayout = new QVBoxLayout(groupBox);
        thresholdsLayout->setObjectName("thresholdsLayout");
        thresholdsLayout->setSpacing(t.spacing.md);
        thresholdsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        groupBox_3 = new QGroupBox(groupBox);
        groupBox_3->setObjectName("groupBox_3");
        auto* intensityLayout = new QVBoxLayout(groupBox_3);
        intensityLayout->setObjectName("intensityLayout");
        intensityLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        layoutWidget1 = new QWidget(groupBox_3);
        layoutWidget1->setObjectName("layoutWidget1");
        gridLayout1 = new QGridLayout(layoutWidget1);
        gridLayout1->setSpacing(t.spacing.md);
        gridLayout1->setObjectName("gridLayout1");
        gridLayout1->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label = new QLabel(layoutWidget1);
        label->setObjectName("label");

        gridLayout1->addWidget(label, 0, 0, 1, 1);

        intensityMinValue = new QDoubleSpinBox(layoutWidget1);
        intensityMinValue->setObjectName("intensityMinValue");
        intensityMinValue->setSingleStep(0.100000000000000);

        gridLayout1->addWidget(intensityMinValue, 0, 1, 1, 1);

        intensityMinSlider = new QSlider(layoutWidget1);
        intensityMinSlider->setObjectName("intensityMinSlider");
        intensityMinSlider->setMaximum(1000);
        intensityMinSlider->setOrientation(Qt::Horizontal);

        gridLayout1->addWidget(intensityMinSlider, 0, 2, 1, 1);

        label_2 = new QLabel(layoutWidget1);
        label_2->setObjectName("label_2");

        gridLayout1->addWidget(label_2, 1, 0, 1, 1);

        intensityMaxValue = new QDoubleSpinBox(layoutWidget1);
        intensityMaxValue->setObjectName("intensityMaxValue");
        intensityMaxValue->setSingleStep(0.100000000000000);

        gridLayout1->addWidget(intensityMaxValue, 1, 1, 1, 1);

        intensityMaxSlider = new QSlider(layoutWidget1);
        intensityMaxSlider->setObjectName("intensityMaxSlider");
        intensityMaxSlider->setMaximum(1000);
        intensityMaxSlider->setValue(1000);
        intensityMaxSlider->setOrientation(Qt::Horizontal);

        gridLayout1->addWidget(intensityMaxSlider, 1, 2, 1, 1);
        gridLayout1->setColumnStretch(0, 1);
        gridLayout1->setColumnStretch(1, 1);
        gridLayout1->setColumnStretch(2, 5);
        intensityLayout->addWidget(layoutWidget1);
        thresholdsLayout->addWidget(groupBox_3);

        groupBox_4 = new QGroupBox(groupBox);
        groupBox_4->setObjectName("groupBox_4");
        auto* qualityLayout = new QVBoxLayout(groupBox_4);
        qualityLayout->setObjectName("qualityLayout");
        qualityLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        layoutWidget2 = new QWidget(groupBox_4);
        layoutWidget2->setObjectName("layoutWidget2");
        gridLayout1_2 = new QGridLayout(layoutWidget2);
        gridLayout1_2->setSpacing(t.spacing.md);
        gridLayout1_2->setObjectName("gridLayout1_2");
        gridLayout1_2->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label_3 = new QLabel(layoutWidget2);
        label_3->setObjectName("label_3");

        gridLayout1_2->addWidget(label_3, 0, 0, 1, 1);

        qualityMinValue = new QDoubleSpinBox(layoutWidget2);
        qualityMinValue->setObjectName("qualityMinValue");
        qualityMinValue->setSingleStep(0.100000000000000);

        gridLayout1_2->addWidget(qualityMinValue, 0, 1, 1, 1);

        qualityMinSlider = new QSlider(layoutWidget2);
        qualityMinSlider->setObjectName("qualityMinSlider");
        qualityMinSlider->setMaximum(1000);
        qualityMinSlider->setValue(0);
        qualityMinSlider->setOrientation(Qt::Horizontal);

        gridLayout1_2->addWidget(qualityMinSlider, 0, 2, 1, 1);

        label_4 = new QLabel(layoutWidget2);
        label_4->setObjectName("label_4");

        gridLayout1_2->addWidget(label_4, 1, 0, 1, 1);

        qualityMaxValue = new QDoubleSpinBox(layoutWidget2);
        qualityMaxValue->setObjectName("qualityMaxValue");
        qualityMaxValue->setSingleStep(0.100000000000000);

        gridLayout1_2->addWidget(qualityMaxValue, 1, 1, 1, 1);

        qualityMaxSlider = new QSlider(layoutWidget2);
        qualityMaxSlider->setObjectName("qualityMaxSlider");
        qualityMaxSlider->setMaximum(1000);
        qualityMaxSlider->setValue(1000);
        qualityMaxSlider->setOrientation(Qt::Horizontal);

        gridLayout1_2->addWidget(qualityMaxSlider, 1, 2, 1, 1);
        gridLayout1_2->setColumnStretch(0, 1);
        gridLayout1_2->setColumnStretch(1, 1);
        gridLayout1_2->setColumnStretch(2, 5);
        qualityLayout->addWidget(layoutWidget2);
        thresholdsLayout->addWidget(groupBox_4);

        groupBox_5 = new QGroupBox(groupBox);
        groupBox_5->setObjectName("groupBox_5");
        auto* radiusLayout = new QVBoxLayout(groupBox_5);
        radiusLayout->setObjectName("radiusLayout");
        radiusLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        layoutWidget_2 = new QWidget(groupBox_5);
        layoutWidget_2->setObjectName("layoutWidget_2");
        gridLayout1_3 = new QGridLayout(layoutWidget_2);
        gridLayout1_3->setSpacing(t.spacing.md);
        gridLayout1_3->setObjectName("gridLayout1_3");
        gridLayout1_3->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label_5 = new QLabel(layoutWidget_2);
        label_5->setObjectName("label_5");

        gridLayout1_3->addWidget(label_5, 0, 0, 1, 1);

        radiusMinValue = new QDoubleSpinBox(layoutWidget_2);
        radiusMinValue->setObjectName("radiusMinValue");
        radiusMinValue->setSingleStep(0.100000000000000);

        gridLayout1_3->addWidget(radiusMinValue, 0, 1, 1, 1);

        radiusMinSlider = new QSlider(layoutWidget_2);
        radiusMinSlider->setObjectName("radiusMinSlider");
        radiusMinSlider->setMaximum(1000);
        radiusMinSlider->setOrientation(Qt::Horizontal);

        gridLayout1_3->addWidget(radiusMinSlider, 0, 2, 1, 1);

        label_6 = new QLabel(layoutWidget_2);
        label_6->setObjectName("label_6");

        gridLayout1_3->addWidget(label_6, 1, 0, 1, 1);

        radiusMaxValue = new QDoubleSpinBox(layoutWidget_2);
        radiusMaxValue->setObjectName("radiusMaxValue");
        radiusMaxValue->setSingleStep(0.100000000000000);

        gridLayout1_3->addWidget(radiusMaxValue, 1, 1, 1, 1);

        radiusMaxSlider = new QSlider(layoutWidget_2);
        radiusMaxSlider->setObjectName("radiusMaxSlider");
        radiusMaxSlider->setMaximum(1000);
        radiusMaxSlider->setValue(1000);
        radiusMaxSlider->setOrientation(Qt::Horizontal);

        gridLayout1_3->addWidget(radiusMaxSlider, 1, 2, 1, 1);
        gridLayout1_3->setColumnStretch(0, 1);
        gridLayout1_3->setColumnStretch(1, 1);
        gridLayout1_3->setColumnStretch(2, 5);
        radiusLayout->addWidget(layoutWidget_2);
        thresholdsLayout->addWidget(groupBox_5);

        rootLayout->addWidget(groupBox, 1);

        groupBox_2 = new QGroupBox(StarsFilterControlV1);
        groupBox_2->setObjectName("groupBox_2");
        auto* starSelectionLayout = new QVBoxLayout(groupBox_2);
        starSelectionLayout->setObjectName("starSelectionLayout");
        starSelectionLayout->setSpacing(t.spacing.sm);
        starSelectionLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        layoutWidget3 = new QWidget(groupBox_2);
        layoutWidget3->setObjectName("layoutWidget3");
        horizontalLayout = new QHBoxLayout(layoutWidget3);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        label_7 = new QLabel(layoutWidget3);
        label_7->setObjectName("label_7");

        horizontalLayout->addWidget(label_7);

        randomSelectionValue = new QDoubleSpinBox(layoutWidget3);
        randomSelectionValue->setObjectName("randomSelectionValue");
        randomSelectionValue->setMaximum(100.000000000000000);
        randomSelectionValue->setSingleStep(0.500000000000000);
        randomSelectionValue->setValue(100.000000000000000);

        horizontalLayout->addWidget(randomSelectionValue);

        randomSelectionSlider = new QSlider(layoutWidget3);
        randomSelectionSlider->setObjectName("randomSelectionSlider");
        randomSelectionSlider->setMaximum(1000);
        randomSelectionSlider->setValue(1000);
        randomSelectionSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(randomSelectionSlider);
        starSelectionLayout->addWidget(layoutWidget3);

        includeNeighborsCheck = new QCheckBox(groupBox_2);
        includeNeighborsCheck->setObjectName("includeNeighborsCheck");
        starSelectionLayout->addWidget(includeNeighborsCheck);

        layoutWidget = new QWidget(groupBox_2);
        layoutWidget->setObjectName("layoutWidget");
        horizontalLayout_2 = new QHBoxLayout(layoutWidget);
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
        filteredStarsLabel = new QLabel(layoutWidget);
        filteredStarsLabel->setObjectName("filteredStarsLabel");

        horizontalLayout_2->addWidget(filteredStarsLabel);

        filteredStarsTxt = new QLineEdit(layoutWidget);
        filteredStarsTxt->setObjectName("filteredStarsTxt");
        filteredStarsTxt->setReadOnly(true);

        horizontalLayout_2->addWidget(filteredStarsTxt);

        auto* footerLayout = new QHBoxLayout();
        footerLayout->setObjectName("footerLayout");
        footerLayout->setSpacing(t.spacing.sm);
        footerLayout->addWidget(layoutWidget);
        footerLayout->addStretch();

        previewCheck = new QCheckBox(groupBox_2);
        previewCheck->setObjectName("previewCheck");
        previewCheck->setChecked(true);
        footerLayout->addWidget(previewCheck);
        starSelectionLayout->addLayout(footerLayout);

        rootLayout->addWidget(groupBox_2);

        buttonBox = new QDialogButtonBox(StarsFilterControlV1);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(StarsFilterControlV1);

        QMetaObject::connectSlotsByName(StarsFilterControlV1);
    } // setupUi

    void retranslateUi(QWidget* StarsFilterControlV1)
    {
        StarsFilterControlV1->setWindowTitle(QCoreApplication::translate("StarsFilterControlV1", "Dialog", nullptr));
        groupBox->setTitle(QCoreApplication::translate("StarsFilterControlV1", "Selection Thresholds", nullptr));
        groupBox_3->setTitle(QCoreApplication::translate("StarsFilterControlV1", "Intensity", nullptr));
        label->setText(QCoreApplication::translate("StarsFilterControlV1", "Min", nullptr));
        label_2->setText(QCoreApplication::translate("StarsFilterControlV1", "Max", nullptr));
        groupBox_4->setTitle(QCoreApplication::translate("StarsFilterControlV1", "Quality", nullptr));
        label_3->setText(QCoreApplication::translate("StarsFilterControlV1", "Min", nullptr));
        qualityMinValue->setSuffix(QString());
        label_4->setText(QCoreApplication::translate("StarsFilterControlV1", "Max", nullptr));
        qualityMaxValue->setSuffix(QString());
        groupBox_5->setTitle(QCoreApplication::translate("StarsFilterControlV1", "Radius", nullptr));
        label_5->setText(QCoreApplication::translate("StarsFilterControlV1", "Min", nullptr));
        radiusMinValue->setSuffix(QString());
        label_6->setText(QCoreApplication::translate("StarsFilterControlV1", "Max", nullptr));
        radiusMaxValue->setSuffix(QString());
        groupBox_2->setTitle(QCoreApplication::translate("StarsFilterControlV1", "Star Selection", nullptr));
        previewCheck->setText(QCoreApplication::translate("StarsFilterControlV1", "Preview", nullptr));
        label_7->setText(QCoreApplication::translate("StarsFilterControlV1", "Random selection", nullptr));
        randomSelectionValue->setSuffix(QCoreApplication::translate("StarsFilterControlV1", "%", nullptr));
        filteredStarsLabel->setText(QCoreApplication::translate("StarsFilterControlV1", "Filtered Stars:", nullptr));
        includeNeighborsCheck->setText(
            QCoreApplication::translate("StarsFilterControlV1", "Include Neighbors", nullptr));
    } // retranslateUi
};

inline StarsFilterControlV1UiElements buildStarsFilterControlV1Ui(QWidget* parent)
{
    StarsFilterControlV1UiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
