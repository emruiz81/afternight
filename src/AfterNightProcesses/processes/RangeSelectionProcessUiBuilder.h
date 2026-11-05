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
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class RangeSelectionProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QDoubleSpinBox* lowerLimitValue;
    QSlider* lowerLimitSlider;
    QHBoxLayout* horizontalLayout_2;
    QLabel* label_2;
    QDoubleSpinBox* upperLimitValue;
    QSlider* upperLimitSlider;
    QCheckBox* linkRangeLimits;
    QGridLayout* gridLayout;
    QLabel* label_4;
    QDoubleSpinBox* fuzzinessValue;
    QSlider* fuzzinessSlider;
    QLabel* label_5;
    QDoubleSpinBox* smoothnessValue;
    QSlider* smoothnessSlider;
    QHBoxLayout* horizontalLayout_7;
    QCheckBox* screening;
    QCheckBox* lightness;
    QCheckBox* invert;
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

    void setupUi(QWidget* RangeSelectionProcess)
    {
        if (RangeSelectionProcess->objectName().isEmpty())
            RangeSelectionProcess->setObjectName("RangeSelectionProcess");
        const auto& t = an::ui::tokens();
        RangeSelectionProcess->resize(898, 415);
        RangeSelectionProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(RangeSelectionProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(RangeSelectionProcess);
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
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        horizontalLayout->addWidget(label);

        lowerLimitValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        lowerLimitValue->setObjectName("lowerLimitValue");
        lowerLimitValue->setDecimals(6);
        lowerLimitValue->setMaximum(1.000000000000000);
        lowerLimitValue->setSingleStep(0.050000000000000);
        lowerLimitValue->setStepType(QAbstractSpinBox::DefaultStepType);

        horizontalLayout->addWidget(lowerLimitValue);

        lowerLimitSlider = new QSlider(horizontalLayoutWidget_4);
        lowerLimitSlider->setObjectName("lowerLimitSlider");
        lowerLimitSlider->setMinimumSize(QSize(200, 0));
        lowerLimitSlider->setMaximum(1000);
        lowerLimitSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(lowerLimitSlider);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        horizontalLayout_2->addWidget(label_2);

        upperLimitValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        upperLimitValue->setObjectName("upperLimitValue");
        upperLimitValue->setDecimals(6);
        upperLimitValue->setMaximum(1.000000000000000);
        upperLimitValue->setSingleStep(0.050000000000000);
        upperLimitValue->setStepType(QAbstractSpinBox::DefaultStepType);
        upperLimitValue->setValue(1.000000000000000);

        horizontalLayout_2->addWidget(upperLimitValue);

        upperLimitSlider = new QSlider(horizontalLayoutWidget_4);
        upperLimitSlider->setObjectName("upperLimitSlider");
        upperLimitSlider->setMaximum(1000);
        upperLimitSlider->setValue(1000);
        upperLimitSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_2->addWidget(upperLimitSlider);

        verticalLayout->addLayout(horizontalLayout_2);

        linkRangeLimits = new QCheckBox(horizontalLayoutWidget_4);
        linkRangeLimits->setObjectName("linkRangeLimits");
        linkRangeLimits->setEnabled(false);
        linkRangeLimits->setChecked(false);

        verticalLayout->addWidget(linkRangeLimits);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        label_4 = new QLabel(horizontalLayoutWidget_4);
        label_4->setObjectName("label_4");

        gridLayout->addWidget(label_4, 0, 0, 1, 1);

        fuzzinessValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        fuzzinessValue->setObjectName("fuzzinessValue");
        fuzzinessValue->setDecimals(2);
        fuzzinessValue->setMaximum(1.000000000000000);
        fuzzinessValue->setSingleStep(0.050000000000000);
        fuzzinessValue->setStepType(QAbstractSpinBox::DefaultStepType);

        gridLayout->addWidget(fuzzinessValue, 0, 1, 1, 1);

        fuzzinessSlider = new QSlider(horizontalLayoutWidget_4);
        fuzzinessSlider->setObjectName("fuzzinessSlider");
        fuzzinessSlider->setMaximum(1000);
        fuzzinessSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(fuzzinessSlider, 0, 2, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 1, 0, 1, 1);

        smoothnessValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        smoothnessValue->setObjectName("smoothnessValue");
        smoothnessValue->setDecimals(2);
        // The engine's exact Gaussian reaches four deviations, at most 128 pixels.
        smoothnessValue->setMaximum(32.000000000000000);
        smoothnessValue->setSingleStep(1.000000000000000);
        smoothnessValue->setStepType(QAbstractSpinBox::DefaultStepType);

        gridLayout->addWidget(smoothnessValue, 1, 1, 1, 1);

        smoothnessSlider = new QSlider(horizontalLayoutWidget_4);
        smoothnessSlider->setObjectName("smoothnessSlider");
        smoothnessSlider->setMaximum(320);
        smoothnessSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(smoothnessSlider, 1, 2, 1, 1);

        verticalLayout->addLayout(gridLayout);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName("horizontalLayout_7");
        horizontalLayout_7->setContentsMargins(0, 0, 0, 0);
        screening = new QCheckBox(horizontalLayoutWidget_4);
        screening->setObjectName("screening");
        screening->setChecked(false);

        horizontalLayout_7->addWidget(screening);

        lightness = new QCheckBox(horizontalLayoutWidget_4);
        lightness->setObjectName("lightness");
        lightness->setChecked(true);

        horizontalLayout_7->addWidget(lightness);

        invert = new QCheckBox(horizontalLayoutWidget_4);
        invert->setObjectName("invert");
        invert->setChecked(false);

        horizontalLayout_7->addWidget(invert);

        verticalLayout->addLayout(horizontalLayout_7);

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
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok | QDialogButtonBox::Reset);

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

        retranslateUi(RangeSelectionProcess);

        QMetaObject::connectSlotsByName(RangeSelectionProcess);
    } // setupUi

    void retranslateUi(QWidget* RangeSelectionProcess)
    {
        RangeSelectionProcess->setWindowTitle(
            QCoreApplication::translate("RangeSelectionProcess", "Range Selection", nullptr));
        label->setText(QCoreApplication::translate("RangeSelectionProcess", "Lower limit", nullptr));
        label_2->setText(QCoreApplication::translate("RangeSelectionProcess", "Upper limit", nullptr));
        linkRangeLimits->setText(QCoreApplication::translate("RangeSelectionProcess", "Link Range Limits", nullptr));
        label_4->setText(QCoreApplication::translate("RangeSelectionProcess", "Fuzziness", nullptr));
        label_5->setText(QCoreApplication::translate("RangeSelectionProcess", "Smoothness", nullptr));
        screening->setText(QCoreApplication::translate("RangeSelectionProcess", "Screening", nullptr));
        lightness->setText(QCoreApplication::translate("RangeSelectionProcess", "Lightness", nullptr));
        invert->setText(QCoreApplication::translate("RangeSelectionProcess", "Invert", nullptr));
        label_3->setText(QCoreApplication::translate("RangeSelectionProcess", "Target Image ", nullptr));
        preview->setText(QCoreApplication::translate("RangeSelectionProcess", "Preview", nullptr));

        lowerLimitValue->setToolTip(
            QCoreApplication::translate("RangeSelectionProcess", "Set the lower selection threshold.", nullptr));
        upperLimitValue->setToolTip(
            QCoreApplication::translate("RangeSelectionProcess", "Set the upper selection threshold.", nullptr));
        fuzzinessValue->setToolTip(QCoreApplication::translate(
            "RangeSelectionProcess", "Set transition softness around selected range boundaries.", nullptr));
        smoothnessValue->setToolTip(QCoreApplication::translate(
            "RangeSelectionProcess", "Set smoothing applied to the generated mask.", nullptr));
        linkRangeLimits->setToolTip(QCoreApplication::translate(
            "RangeSelectionProcess", "Keep lower and upper limits linked during adjustment.", nullptr));
        screening->setToolTip(QCoreApplication::translate("RangeSelectionProcess",
                                                          "Enable screening mode for the selection result.", nullptr));
        lightness->setToolTip(QCoreApplication::translate(
            "RangeSelectionProcess", "Use lightness-based selection instead of per-channel thresholds.", nullptr));
        invert->setToolTip(
            QCoreApplication::translate("RangeSelectionProcess", "Invert the selection mask output.", nullptr));
        targetImageSelector->setToolTip(QCoreApplication::translate(
            "RangeSelectionProcess", "Choose the open image or sequence to process.", nullptr));
        preview->setToolTip(QCoreApplication::translate("RangeSelectionProcess",
                                                        "Enable real-time preview while editing parameters.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Cancel))
            btn->setToolTip(QCoreApplication::translate(
                "RangeSelectionProcess", "Cancel and close this dialog without applying changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Ok))
            btn->setToolTip(QCoreApplication::translate("RangeSelectionProcess",
                                                        "Apply range selection with the current settings.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("RangeSelectionProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline RangeSelectionProcessUiElements buildRangeSelectionProcessUi(QWidget* parent)
{
    RangeSelectionProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
