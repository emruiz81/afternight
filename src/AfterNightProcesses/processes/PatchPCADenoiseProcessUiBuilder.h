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

class PatchPCADenoiseProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QSpinBox* lookupSizeValue;
    QSlider* lookupSizeSlider;
    QSlider* strenghtSlider;
    QSpinBox* patchSizeValue;
    QLabel* label_2;
    QLabel* label_5;
    QLabel* label_6;
    QLabel* label_7;
    QSlider* patchSizeSlider;
    QDoubleSpinBox* strenghtValue;
    QSlider* spatialSamplingSlider;
    QSpinBox* spatialSamplingValue;
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

    void setupUi(QWidget* PatchPCADenoiseProcess)
    {
        const auto& t = an::ui::tokens();

        if (PatchPCADenoiseProcess->objectName().isEmpty())
            PatchPCADenoiseProcess->setObjectName("PatchPCADenoiseProcess");
        PatchPCADenoiseProcess->resize(649, 311);
        PatchPCADenoiseProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(PatchPCADenoiseProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(PatchPCADenoiseProcess);
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
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        gridLayout->setHorizontalSpacing(t.spacing.md);
        gridLayout->setVerticalSpacing(t.spacing.sm);
        lookupSizeValue = new QSpinBox(horizontalLayoutWidget_4);
        lookupSizeValue->setObjectName("lookupSizeValue");
        lookupSizeValue->setMinimum(2);
        lookupSizeValue->setMaximum(21);
        lookupSizeValue->setValue(8);
        lookupSizeValue->setMinimumWidth(80);

        gridLayout->addWidget(lookupSizeValue, 2, 1, 1, 1);

        lookupSizeSlider = new QSlider(horizontalLayoutWidget_4);
        lookupSizeSlider->setObjectName("lookupSizeSlider");
        lookupSizeSlider->setMinimumSize(QSize(150, 0));
        lookupSizeSlider->setMinimum(2);
        lookupSizeSlider->setMaximum(21);
        lookupSizeSlider->setPageStep(10);
        lookupSizeSlider->setValue(8);
        lookupSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(lookupSizeSlider, 2, 2, 1, 1);

        strenghtSlider = new QSlider(horizontalLayoutWidget_4);
        strenghtSlider->setObjectName("strenghtSlider");
        strenghtSlider->setMinimumSize(QSize(150, 0));
        strenghtSlider->setMaximum(1600);
        strenghtSlider->setValue(180);
        strenghtSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(strenghtSlider, 0, 2, 1, 1);

        patchSizeValue = new QSpinBox(horizontalLayoutWidget_4);
        patchSizeValue->setObjectName("patchSizeValue");
        patchSizeValue->setMinimum(2);
        patchSizeValue->setMaximum(21);
        patchSizeValue->setValue(7);
        patchSizeValue->setMinimumWidth(80);

        gridLayout->addWidget(patchSizeValue, 1, 1, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 0, 0, 1, 1);

        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 2, 0, 1, 1);

        label_7 = new QLabel(horizontalLayoutWidget_4);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 3, 0, 1, 1);

        patchSizeSlider = new QSlider(horizontalLayoutWidget_4);
        patchSizeSlider->setObjectName("patchSizeSlider");
        patchSizeSlider->setMinimumSize(QSize(150, 0));
        patchSizeSlider->setMinimum(2);
        patchSizeSlider->setMaximum(21);
        patchSizeSlider->setPageStep(10);
        patchSizeSlider->setValue(7);
        patchSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(patchSizeSlider, 1, 2, 1, 1);

        strenghtValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        strenghtValue->setObjectName("strenghtValue");
        strenghtValue->setMaximum(16.000000000000000);
        strenghtValue->setSingleStep(0.010000000000000);
        strenghtValue->setValue(1.800000000000000);
        strenghtValue->setMinimumWidth(80);

        gridLayout->addWidget(strenghtValue, 0, 1, 1, 1);

        spatialSamplingSlider = new QSlider(horizontalLayoutWidget_4);
        spatialSamplingSlider->setObjectName("spatialSamplingSlider");
        spatialSamplingSlider->setMinimumSize(QSize(150, 0));
        spatialSamplingSlider->setMinimum(1);
        spatialSamplingSlider->setMaximum(16);
        spatialSamplingSlider->setValue(7);
        spatialSamplingSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(spatialSamplingSlider, 3, 2, 1, 1);

        spatialSamplingValue = new QSpinBox(horizontalLayoutWidget_4);
        spatialSamplingValue->setObjectName("spatialSamplingValue");
        spatialSamplingValue->setMinimum(1);
        spatialSamplingValue->setMaximum(16);
        spatialSamplingValue->setValue(7);
        spatialSamplingValue->setMinimumWidth(80);

        gridLayout->addWidget(spatialSamplingValue, 3, 1, 1, 1);

        verticalLayout->addLayout(gridLayout);

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

        retranslateUi(PatchPCADenoiseProcess);

        QMetaObject::connectSlotsByName(PatchPCADenoiseProcess);
    } // setupUi

    void retranslateUi(QWidget* PatchPCADenoiseProcess)
    {
        PatchPCADenoiseProcess->setWindowTitle(
            QCoreApplication::translate("PatchPCADenoiseProcess", "Patch PCA Denoise", nullptr));
        label_2->setText(QCoreApplication::translate("PatchPCADenoiseProcess", "Patch Size", nullptr));
        label_5->setText(QCoreApplication::translate("PatchPCADenoiseProcess", "Strength", nullptr));
        label_6->setText(QCoreApplication::translate("PatchPCADenoiseProcess", "Lookup Size", nullptr));
        label_7->setText(QCoreApplication::translate("PatchPCADenoiseProcess", "Spatial Sampling", nullptr));
        label_3->setText(QCoreApplication::translate("PatchPCADenoiseProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("PatchPCADenoiseProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("PatchPCADenoiseProcess", "Preview", nullptr));

        strenghtValue->setToolTip(QCoreApplication::translate(
            "PatchPCADenoiseProcess", "Denoise strength. Higher values produce stronger smoothing.", nullptr));
        strenghtSlider->setToolTip(QCoreApplication::translate(
            "PatchPCADenoiseProcess", "Denoise strength. Higher values produce stronger smoothing.", nullptr));
        patchSizeValue->setToolTip(QCoreApplication::translate(
            "PatchPCADenoiseProcess", "Patch size used for PCA denoising neighborhoods.", nullptr));
        patchSizeSlider->setToolTip(QCoreApplication::translate(
            "PatchPCADenoiseProcess", "Patch size used for PCA denoising neighborhoods.", nullptr));
        lookupSizeValue->setToolTip(
            QCoreApplication::translate("PatchPCADenoiseProcess", "Search window size for similar patches.", nullptr));
        lookupSizeSlider->setToolTip(
            QCoreApplication::translate("PatchPCADenoiseProcess", "Search window size for similar patches.", nullptr));
        spatialSamplingValue->setToolTip(QCoreApplication::translate(
            "PatchPCADenoiseProcess", "Spatial sampling factor controlling patch comparison density.", nullptr));
        spatialSamplingSlider->setToolTip(QCoreApplication::translate(
            "PatchPCADenoiseProcess", "Spatial sampling factor controlling patch comparison density.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("PatchPCADenoiseProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "PatchPCADenoiseProcess", "Select the channel to process, or RGB to process all channels together.",
            nullptr));
        preview->setToolTip(
            QCoreApplication::translate("PatchPCADenoiseProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("PatchPCADenoiseProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("PatchPCADenoiseProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline PatchPCADenoiseProcessUiElements buildPatchPCADenoiseProcessUi(QWidget* parent)
{
    PatchPCADenoiseProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    parent->setMinimumSize(QSize(649, 311));
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
