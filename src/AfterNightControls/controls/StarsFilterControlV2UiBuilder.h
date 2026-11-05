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

class StarsFilterControlV2UiElements
{
public:
    QDialogButtonBox* buttonBox;
    QGroupBox* groupBox;
    QGroupBox* groupBox_3;
    QWidget* layoutWidget1;
    QGridLayout* gridLayout1;
    QLabel* label;
    QDoubleSpinBox* magMinValue;
    QSlider* magMinSlider;
    QLabel* label_2;
    QDoubleSpinBox* magMaxValue;
    QSlider* magMaxSlider;
    QGroupBox* groupBox_4;
    QWidget* layoutWidget2;
    QGridLayout* gridLayout1_2;
    QLabel* label_3;
    QDoubleSpinBox* fwhmxMinValue;
    QSlider* fwhmxMinSlider;
    QLabel* label_4;
    QDoubleSpinBox* fwhmxMaxValue;
    QSlider* fwhmxMaxSlider;
    QGroupBox* groupBox_5;
    QWidget* layoutWidget_2;
    QGridLayout* gridLayout1_3;
    QLabel* label_5;
    QDoubleSpinBox* fwhmyMinValue;
    QSlider* fwhmyMinSlider;
    QLabel* label_6;
    QDoubleSpinBox* fwhmyMaxValue;
    QSlider* fwhmyMaxSlider;
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

    void setupUi(QWidget* StarsFilterControlV2)
    {
        const auto& t = an::ui::tokens();

        if (StarsFilterControlV2->objectName().isEmpty())
            StarsFilterControlV2->setObjectName("StarsFilterControlV2");
        StarsFilterControlV2->resize(589, 585);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(StarsFilterControlV2->sizePolicy().hasHeightForWidth());
        StarsFilterControlV2->setSizePolicy(sizePolicy);
        StarsFilterControlV2->setMinimumSize(QSize(540, 500));
        auto* rootLayout = new QVBoxLayout(StarsFilterControlV2);
        rootLayout->setObjectName("rootLayout");
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        groupBox = new QGroupBox(StarsFilterControlV2);
        groupBox->setObjectName("groupBox");
        auto* thresholdsLayout = new QVBoxLayout(groupBox);
        thresholdsLayout->setObjectName("thresholdsLayout");
        thresholdsLayout->setSpacing(t.spacing.md);
        thresholdsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        groupBox_3 = new QGroupBox(groupBox);
        groupBox_3->setObjectName("groupBox_3");
        auto* magLayout = new QVBoxLayout(groupBox_3);
        magLayout->setObjectName("magLayout");
        magLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        layoutWidget1 = new QWidget(groupBox_3);
        layoutWidget1->setObjectName("layoutWidget1");
        gridLayout1 = new QGridLayout(layoutWidget1);
        gridLayout1->setSpacing(t.spacing.md);
        gridLayout1->setObjectName("gridLayout1");
        gridLayout1->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label = new QLabel(layoutWidget1);
        label->setObjectName("label");

        gridLayout1->addWidget(label, 0, 0, 1, 1);

        magMinValue = new QDoubleSpinBox(layoutWidget1);
        magMinValue->setObjectName("magMinValue");
        magMinValue->setSingleStep(0.100000000000000);

        gridLayout1->addWidget(magMinValue, 0, 1, 1, 1);

        magMinSlider = new QSlider(layoutWidget1);
        magMinSlider->setObjectName("magMinSlider");
        magMinSlider->setMaximum(1000);
        magMinSlider->setOrientation(Qt::Horizontal);

        gridLayout1->addWidget(magMinSlider, 0, 2, 1, 1);

        label_2 = new QLabel(layoutWidget1);
        label_2->setObjectName("label_2");

        gridLayout1->addWidget(label_2, 1, 0, 1, 1);

        magMaxValue = new QDoubleSpinBox(layoutWidget1);
        magMaxValue->setObjectName("magMaxValue");
        magMaxValue->setSingleStep(0.100000000000000);

        gridLayout1->addWidget(magMaxValue, 1, 1, 1, 1);

        magMaxSlider = new QSlider(layoutWidget1);
        magMaxSlider->setObjectName("magMaxSlider");
        magMaxSlider->setMaximum(1000);
        magMaxSlider->setValue(1000);
        magMaxSlider->setOrientation(Qt::Horizontal);

        gridLayout1->addWidget(magMaxSlider, 1, 2, 1, 1);
        gridLayout1->setColumnStretch(0, 1);
        gridLayout1->setColumnStretch(1, 1);
        gridLayout1->setColumnStretch(2, 5);
        magLayout->addWidget(layoutWidget1);
        thresholdsLayout->addWidget(groupBox_3);

        groupBox_4 = new QGroupBox(groupBox);
        groupBox_4->setObjectName("groupBox_4");
        auto* fwhmxLayout = new QVBoxLayout(groupBox_4);
        fwhmxLayout->setObjectName("fwhmxLayout");
        fwhmxLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        layoutWidget2 = new QWidget(groupBox_4);
        layoutWidget2->setObjectName("layoutWidget2");
        gridLayout1_2 = new QGridLayout(layoutWidget2);
        gridLayout1_2->setSpacing(t.spacing.md);
        gridLayout1_2->setObjectName("gridLayout1_2");
        gridLayout1_2->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label_3 = new QLabel(layoutWidget2);
        label_3->setObjectName("label_3");

        gridLayout1_2->addWidget(label_3, 0, 0, 1, 1);

        fwhmxMinValue = new QDoubleSpinBox(layoutWidget2);
        fwhmxMinValue->setObjectName("fwhmxMinValue");
        fwhmxMinValue->setSingleStep(0.100000000000000);

        gridLayout1_2->addWidget(fwhmxMinValue, 0, 1, 1, 1);

        fwhmxMinSlider = new QSlider(layoutWidget2);
        fwhmxMinSlider->setObjectName("fwhmxMinSlider");
        fwhmxMinSlider->setMaximum(1000);
        fwhmxMinSlider->setValue(0);
        fwhmxMinSlider->setOrientation(Qt::Horizontal);

        gridLayout1_2->addWidget(fwhmxMinSlider, 0, 2, 1, 1);

        label_4 = new QLabel(layoutWidget2);
        label_4->setObjectName("label_4");

        gridLayout1_2->addWidget(label_4, 1, 0, 1, 1);

        fwhmxMaxValue = new QDoubleSpinBox(layoutWidget2);
        fwhmxMaxValue->setObjectName("fwhmxMaxValue");
        fwhmxMaxValue->setSingleStep(0.100000000000000);

        gridLayout1_2->addWidget(fwhmxMaxValue, 1, 1, 1, 1);

        fwhmxMaxSlider = new QSlider(layoutWidget2);
        fwhmxMaxSlider->setObjectName("fwhmxMaxSlider");
        fwhmxMaxSlider->setMaximum(1000);
        fwhmxMaxSlider->setValue(1000);
        fwhmxMaxSlider->setOrientation(Qt::Horizontal);

        gridLayout1_2->addWidget(fwhmxMaxSlider, 1, 2, 1, 1);
        gridLayout1_2->setColumnStretch(0, 1);
        gridLayout1_2->setColumnStretch(1, 1);
        gridLayout1_2->setColumnStretch(2, 5);
        fwhmxLayout->addWidget(layoutWidget2);
        thresholdsLayout->addWidget(groupBox_4);

        groupBox_5 = new QGroupBox(groupBox);
        groupBox_5->setObjectName("groupBox_5");
        auto* fwhmyLayout = new QVBoxLayout(groupBox_5);
        fwhmyLayout->setObjectName("fwhmyLayout");
        fwhmyLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        layoutWidget_2 = new QWidget(groupBox_5);
        layoutWidget_2->setObjectName("layoutWidget_2");
        gridLayout1_3 = new QGridLayout(layoutWidget_2);
        gridLayout1_3->setSpacing(t.spacing.md);
        gridLayout1_3->setObjectName("gridLayout1_3");
        gridLayout1_3->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label_5 = new QLabel(layoutWidget_2);
        label_5->setObjectName("label_5");

        gridLayout1_3->addWidget(label_5, 0, 0, 1, 1);

        fwhmyMinValue = new QDoubleSpinBox(layoutWidget_2);
        fwhmyMinValue->setObjectName("fwhmyMinValue");
        fwhmyMinValue->setSingleStep(0.100000000000000);

        gridLayout1_3->addWidget(fwhmyMinValue, 0, 1, 1, 1);

        fwhmyMinSlider = new QSlider(layoutWidget_2);
        fwhmyMinSlider->setObjectName("fwhmyMinSlider");
        fwhmyMinSlider->setMaximum(1000);
        fwhmyMinSlider->setOrientation(Qt::Horizontal);

        gridLayout1_3->addWidget(fwhmyMinSlider, 0, 2, 1, 1);

        label_6 = new QLabel(layoutWidget_2);
        label_6->setObjectName("label_6");

        gridLayout1_3->addWidget(label_6, 1, 0, 1, 1);

        fwhmyMaxValue = new QDoubleSpinBox(layoutWidget_2);
        fwhmyMaxValue->setObjectName("fwhmyMaxValue");
        fwhmyMaxValue->setSingleStep(0.100000000000000);

        gridLayout1_3->addWidget(fwhmyMaxValue, 1, 1, 1, 1);

        fwhmyMaxSlider = new QSlider(layoutWidget_2);
        fwhmyMaxSlider->setObjectName("fwhmyMaxSlider");
        fwhmyMaxSlider->setMaximum(1000);
        fwhmyMaxSlider->setValue(1000);
        fwhmyMaxSlider->setOrientation(Qt::Horizontal);

        gridLayout1_3->addWidget(fwhmyMaxSlider, 1, 2, 1, 1);
        gridLayout1_3->setColumnStretch(0, 1);
        gridLayout1_3->setColumnStretch(1, 1);
        gridLayout1_3->setColumnStretch(2, 5);
        fwhmyLayout->addWidget(layoutWidget_2);
        thresholdsLayout->addWidget(groupBox_5);

        rootLayout->addWidget(groupBox, 1);

        groupBox_2 = new QGroupBox(StarsFilterControlV2);
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

        buttonBox = new QDialogButtonBox(StarsFilterControlV2);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok | QDialogButtonBox::Reset);
        rootLayout->addWidget(buttonBox);

        retranslateUi(StarsFilterControlV2);

        QMetaObject::connectSlotsByName(StarsFilterControlV2);
    } // setupUi

    void retranslateUi(QWidget* StarsFilterControlV2)
    {
        StarsFilterControlV2->setWindowTitle(QCoreApplication::translate("StarsFilterControlV2", "Dialog", nullptr));
        groupBox->setTitle(QCoreApplication::translate("StarsFilterControlV2", "Selection Thresholds", nullptr));
        groupBox_3->setTitle(QCoreApplication::translate("StarsFilterControlV2", "Mag", nullptr));
        label->setText(QCoreApplication::translate("StarsFilterControlV2", "Min", nullptr));
        label_2->setText(QCoreApplication::translate("StarsFilterControlV2", "Max", nullptr));
        groupBox_4->setTitle(QCoreApplication::translate("StarsFilterControlV2", "FWHMx", nullptr));
        label_3->setText(QCoreApplication::translate("StarsFilterControlV2", "Min", nullptr));
        fwhmxMinValue->setSuffix(QCoreApplication::translate("StarsFilterControlV2", "px", nullptr));
        label_4->setText(QCoreApplication::translate("StarsFilterControlV2", "Max", nullptr));
        fwhmxMaxValue->setSuffix(QCoreApplication::translate("StarsFilterControlV2", "px", nullptr));
        groupBox_5->setTitle(QCoreApplication::translate("StarsFilterControlV2", "FWHMy", nullptr));
        label_5->setText(QCoreApplication::translate("StarsFilterControlV2", "Min", nullptr));
        fwhmyMinValue->setSuffix(QCoreApplication::translate("StarsFilterControlV2", "px", nullptr));
        label_6->setText(QCoreApplication::translate("StarsFilterControlV2", "Max", nullptr));
        fwhmyMaxValue->setSuffix(QCoreApplication::translate("StarsFilterControlV2", "px", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("StarsFilterControlV2", "Star Selection", nullptr));
        previewCheck->setText(QCoreApplication::translate("StarsFilterControlV2", "Preview", nullptr));
        label_7->setText(QCoreApplication::translate("StarsFilterControlV2", "Random selection", nullptr));
        randomSelectionValue->setSuffix(QCoreApplication::translate("StarsFilterControlV2", "%", nullptr));
        filteredStarsLabel->setText(QCoreApplication::translate("StarsFilterControlV2", "Filtered Stars:", nullptr));
        includeNeighborsCheck->setText(
            QCoreApplication::translate("StarsFilterControlV2", "Include Neighbors", nullptr));
    } // retranslateUi
};

inline StarsFilterControlV2UiElements buildStarsFilterControlV2Ui(QWidget* parent)
{
    StarsFilterControlV2UiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
