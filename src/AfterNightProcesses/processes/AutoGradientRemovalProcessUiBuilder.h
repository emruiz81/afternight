// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>

#include <QtCore/QCoreApplication>
#include <QtCore/QSize>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class AutoGradientRemovalProcessUiElements
{
public:
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;

    QDoubleSpinBox* scaleValue = nullptr;
    QSlider* scaleSlider = nullptr;
    QDoubleSpinBox* smoothnessValue = nullptr;
    QSlider* smoothnessSlider = nullptr;
    QComboBox* downsampleComboBox = nullptr;

    QCheckBox* structureProtection = nullptr;
    QDoubleSpinBox* protectionThresholdValue = nullptr;
    QSlider* protectionThresholdSlider = nullptr;
    QDoubleSpinBox* protectionAmountValue = nullptr;
    QSlider* protectionAmountSlider = nullptr;

    QCheckBox* simplifiedModel = nullptr;
    QSpinBox* polynomialDegreeValue = nullptr;
    QSlider* polynomialDegreeSlider = nullptr;

    QComboBox* correctionModeComboBox = nullptr;
    QComboBox* previewDisplaySelector = nullptr;
    QPushButton* previewButton = nullptr;
    QCheckBox* outputGradientModel = nullptr;
    QCheckBox* outputProtectionMask = nullptr;
    QCheckBox* previewCheckbox = nullptr;

    QComboBox* targetImageSelector = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    void setupUi(QWidget* autoGradientRemovalProcess)
    {
        const auto& t = an::ui::tokens();

        if (autoGradientRemovalProcess->objectName().isEmpty())
        {
            autoGradientRemovalProcess->setObjectName(QStringLiteral("AutoGradientRemovalProcess"));
        }
        autoGradientRemovalProcess->setWindowTitle(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Auto Gradient Removal", nullptr));
        autoGradientRemovalProcess->resize(980, 700);
        autoGradientRemovalProcess->setMinimumSize(QSize(600, 420));

        horizontalLayoutMain = new QHBoxLayout(autoGradientRemovalProcess);
        horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);

        const auto split =
            rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, autoGradientRemovalProcess,
                                                    QMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md));
        auto* controlsPanel = split.controlsPanel;
        auto* controlsLayout = split.controlsLayout;
        previewLayout = split.previewLayout;
        controlsLayout->setSpacing(t.spacing.md);

        buildModelGroup(controlsPanel, controlsLayout, t);
        buildProtectionGroup(controlsPanel, controlsLayout, t);
        buildCorrectionGroup(controlsPanel, controlsLayout, t);
        buildOutputsGroup(controlsPanel, controlsLayout, t);

        controlsLayout->addStretch(1);
        buildPreviewAndTargetRows(controlsPanel, controlsLayout, t);

        buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                         controlsPanel);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        controlsLayout->addWidget(buttonBox);

        const int controlsWidth = controlsPanel->sizeHint().width() + t.spacing.lg;
        const int previewPaneWidth =
            (std::max)(rt_preview_ui::kMinPreviewPaneWidth, autoGradientRemovalProcess->width() - controlsWidth);
        split.mainSplitter->setSizes({controlsWidth, previewPaneWidth});
    }

private:
    void buildModelGroup(QWidget* parent, QVBoxLayout* controlsLayout, const an::ui::DesignTokens& t)
    {
        auto* modelGroup = new QGroupBox(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Background model", nullptr), parent);
        auto* modelLayout = new QVBoxLayout(modelGroup);
        modelLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        modelLayout->setSpacing(t.spacing.sm);

        auto* modelGrid = new QGridLayout();
        modelGrid->setHorizontalSpacing(t.spacing.sm);
        modelGrid->setVerticalSpacing(t.spacing.sm);
        modelGrid->setColumnStretch(1, 1);

        auto* scaleLabel =
            new QLabel(QCoreApplication::translate("AutoGradientRemovalProcess", "Scale", nullptr), modelGroup);
        modelGrid->addWidget(scaleLabel, 0, 0, 1, 1);

        scaleSlider = new QSlider(Qt::Horizontal, modelGroup);
        scaleSlider->setObjectName(QStringLiteral("scaleSlider"));
        scaleSlider->setTracking(false);
        modelGrid->addWidget(scaleSlider, 0, 1, 1, 1);

        scaleValue = new QDoubleSpinBox(modelGroup);
        scaleValue->setObjectName(QStringLiteral("scaleValue"));
        scaleValue->setDecimals(1);
        scaleValue->setMinimum(1.0);
        scaleValue->setMaximum(10.0);
        scaleValue->setSingleStep(0.5);
        scaleValue->setValue(5.0);
        scaleValue->setMinimumWidth(84);
        modelGrid->addWidget(scaleValue, 0, 2, 1, 1);

        auto* smoothnessLabel =
            new QLabel(QCoreApplication::translate("AutoGradientRemovalProcess", "Smoothness", nullptr), modelGroup);
        modelGrid->addWidget(smoothnessLabel, 1, 0, 1, 1);

        smoothnessSlider = new QSlider(Qt::Horizontal, modelGroup);
        smoothnessSlider->setObjectName(QStringLiteral("smoothnessSlider"));
        smoothnessSlider->setTracking(false);
        modelGrid->addWidget(smoothnessSlider, 1, 1, 1, 1);

        smoothnessValue = new QDoubleSpinBox(modelGroup);
        smoothnessValue->setObjectName(QStringLiteral("smoothnessValue"));
        smoothnessValue->setDecimals(2);
        smoothnessValue->setMinimum(0.0);
        smoothnessValue->setMaximum(3.0);
        smoothnessValue->setSingleStep(0.1);
        smoothnessValue->setValue(1.0);
        smoothnessValue->setMinimumWidth(84);
        modelGrid->addWidget(smoothnessValue, 1, 2, 1, 1);

        auto* downsampleLabel =
            new QLabel(QCoreApplication::translate("AutoGradientRemovalProcess", "Downsample", nullptr), modelGroup);
        modelGrid->addWidget(downsampleLabel, 2, 0, 1, 1);

        downsampleComboBox = new QComboBox(modelGroup);
        downsampleComboBox->setObjectName(QStringLiteral("downsampleComboBox"));
        downsampleComboBox->addItems(
            {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("8")});
        downsampleComboBox->setCurrentText(QStringLiteral("4"));
        modelGrid->addWidget(downsampleComboBox, 2, 1, 1, 2);

        modelLayout->addLayout(modelGrid);

        simplifiedModel = new QCheckBox(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Simplified polynomial model", nullptr),
            modelGroup);
        simplifiedModel->setObjectName(QStringLiteral("simplifiedModel"));
        simplifiedModel->setChecked(false);
        modelLayout->addWidget(simplifiedModel);

        auto* degreeLayout = new QHBoxLayout();
        degreeLayout->setSpacing(t.spacing.sm);

        auto* degreeLabel = new QLabel(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Polynomial degree", nullptr), modelGroup);
        degreeLayout->addWidget(degreeLabel);

        polynomialDegreeSlider = new QSlider(Qt::Horizontal, modelGroup);
        polynomialDegreeSlider->setObjectName(QStringLiteral("polynomialDegreeSlider"));
        polynomialDegreeSlider->setMinimum(1);
        polynomialDegreeSlider->setMaximum(6);
        polynomialDegreeSlider->setValue(2);
        polynomialDegreeSlider->setTracking(false);
        degreeLayout->addWidget(polynomialDegreeSlider, 1);

        polynomialDegreeValue = new QSpinBox(modelGroup);
        polynomialDegreeValue->setObjectName(QStringLiteral("polynomialDegreeValue"));
        polynomialDegreeValue->setMinimum(1);
        polynomialDegreeValue->setMaximum(6);
        polynomialDegreeValue->setValue(2);
        polynomialDegreeValue->setMinimumWidth(84);
        degreeLayout->addWidget(polynomialDegreeValue);

        modelLayout->addLayout(degreeLayout);
        controlsLayout->addWidget(modelGroup);
    }

    void buildProtectionGroup(QWidget* parent, QVBoxLayout* controlsLayout, const an::ui::DesignTokens& t)
    {
        auto* protectionGroup = new QGroupBox(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Structure protection", nullptr), parent);
        auto* protectionLayout = new QVBoxLayout(protectionGroup);
        protectionLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        protectionLayout->setSpacing(t.spacing.sm);

        structureProtection = new QCheckBox(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Protect bright structures", nullptr),
            protectionGroup);
        structureProtection->setObjectName(QStringLiteral("structureProtection"));
        structureProtection->setChecked(true);
        protectionLayout->addWidget(structureProtection);

        auto* protectionGrid = new QGridLayout();
        protectionGrid->setHorizontalSpacing(t.spacing.sm);
        protectionGrid->setVerticalSpacing(t.spacing.sm);
        protectionGrid->setColumnStretch(1, 1);

        auto* thresholdLabel = new QLabel(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Threshold", nullptr), protectionGroup);
        protectionGrid->addWidget(thresholdLabel, 0, 0, 1, 1);

        protectionThresholdSlider = new QSlider(Qt::Horizontal, protectionGroup);
        protectionThresholdSlider->setObjectName(QStringLiteral("protectionThresholdSlider"));
        protectionThresholdSlider->setTracking(false);
        protectionGrid->addWidget(protectionThresholdSlider, 0, 1, 1, 1);

        protectionThresholdValue = new QDoubleSpinBox(protectionGroup);
        protectionThresholdValue->setObjectName(QStringLiteral("protectionThresholdValue"));
        protectionThresholdValue->setDecimals(3);
        protectionThresholdValue->setMinimum(0.0);
        protectionThresholdValue->setMaximum(1.0);
        protectionThresholdValue->setSingleStep(0.005);
        protectionThresholdValue->setValue(0.05);
        protectionThresholdValue->setMinimumWidth(84);
        protectionGrid->addWidget(protectionThresholdValue, 0, 2, 1, 1);

        auto* amountLabel =
            new QLabel(QCoreApplication::translate("AutoGradientRemovalProcess", "Amount", nullptr), protectionGroup);
        protectionGrid->addWidget(amountLabel, 1, 0, 1, 1);

        protectionAmountSlider = new QSlider(Qt::Horizontal, protectionGroup);
        protectionAmountSlider->setObjectName(QStringLiteral("protectionAmountSlider"));
        protectionAmountSlider->setTracking(false);
        protectionGrid->addWidget(protectionAmountSlider, 1, 1, 1, 1);

        protectionAmountValue = new QDoubleSpinBox(protectionGroup);
        protectionAmountValue->setObjectName(QStringLiteral("protectionAmountValue"));
        protectionAmountValue->setDecimals(2);
        protectionAmountValue->setMinimum(0.0);
        protectionAmountValue->setMaximum(1.0);
        protectionAmountValue->setSingleStep(0.05);
        protectionAmountValue->setValue(0.5);
        protectionAmountValue->setMinimumWidth(84);
        protectionGrid->addWidget(protectionAmountValue, 1, 2, 1, 1);

        protectionLayout->addLayout(protectionGrid);
        controlsLayout->addWidget(protectionGroup);
    }

    void buildCorrectionGroup(QWidget* parent, QVBoxLayout* controlsLayout, const an::ui::DesignTokens& t)
    {
        auto* correctionGroup =
            new QGroupBox(QCoreApplication::translate("AutoGradientRemovalProcess", "Correction", nullptr), parent);
        auto* correctionLayout = new QVBoxLayout(correctionGroup);
        correctionLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        correctionLayout->setSpacing(t.spacing.sm);

        auto* correctionGrid = new QGridLayout();
        correctionGrid->setHorizontalSpacing(t.spacing.sm);
        correctionGrid->setVerticalSpacing(t.spacing.sm);
        correctionGrid->setColumnStretch(1, 1);

        auto* modeLabel =
            new QLabel(QCoreApplication::translate("AutoGradientRemovalProcess", "Mode", nullptr), correctionGroup);
        correctionGrid->addWidget(modeLabel, 0, 0, 1, 1);

        correctionModeComboBox = new QComboBox(correctionGroup);
        correctionModeComboBox->setObjectName(QStringLiteral("correctionModeComboBox"));
        correctionModeComboBox->addItems(
            {QCoreApplication::translate("AutoGradientRemovalProcess", "Subtraction", nullptr),
             QCoreApplication::translate("AutoGradientRemovalProcess", "Division", nullptr)});
        correctionGrid->addWidget(correctionModeComboBox, 0, 1, 1, 2);

        auto* previewLabel = new QLabel(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Preview display", nullptr), correctionGroup);
        correctionGrid->addWidget(previewLabel, 1, 0, 1, 1);

        previewDisplaySelector = new QComboBox(correctionGroup);
        previewDisplaySelector->setObjectName(QStringLiteral("previewDisplaySelector"));
        previewDisplaySelector->addItems(
            {QCoreApplication::translate("AutoGradientRemovalProcess", "Corrected image", nullptr),
             QCoreApplication::translate("AutoGradientRemovalProcess", "Gradient model", nullptr),
             QCoreApplication::translate("AutoGradientRemovalProcess", "Protection mask", nullptr)});
        correctionGrid->addWidget(previewDisplaySelector, 1, 1, 1, 2);

        correctionLayout->addLayout(correctionGrid);

        previewButton = new QPushButton(QCoreApplication::translate("AutoGradientRemovalProcess", "Preview", nullptr),
                                        correctionGroup);
        previewButton->setObjectName(QStringLiteral("previewButton"));
        previewButton->setProperty("anButtonVariant", QStringLiteral("primary"));
        correctionLayout->addWidget(previewButton, 0, Qt::AlignLeft);

        controlsLayout->addWidget(correctionGroup);
    }

    void buildOutputsGroup(QWidget* parent, QVBoxLayout* controlsLayout, const an::ui::DesignTokens& t)
    {
        auto* outputsGroup =
            new QGroupBox(QCoreApplication::translate("AutoGradientRemovalProcess", "Outputs", nullptr), parent);
        outputsGroup->setObjectName(QStringLiteral("outputsGroup"));
        auto* outputsLayout = new QVBoxLayout(outputsGroup);
        outputsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        outputsLayout->setSpacing(t.spacing.sm);

        outputGradientModel = new QCheckBox(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Create gradient model image", nullptr),
            outputsGroup);
        outputGradientModel->setObjectName(QStringLiteral("outputGradientModel"));
        outputGradientModel->setChecked(false);
        outputsLayout->addWidget(outputGradientModel);

        outputProtectionMask = new QCheckBox(
            QCoreApplication::translate("AutoGradientRemovalProcess", "Create protection mask image", nullptr),
            outputsGroup);
        outputProtectionMask->setObjectName(QStringLiteral("outputProtectionMask"));
        outputProtectionMask->setChecked(false);
        outputsLayout->addWidget(outputProtectionMask);

        controlsLayout->addWidget(outputsGroup);
    }

    void buildPreviewAndTargetRows(QWidget* parent, QVBoxLayout* controlsLayout, const an::ui::DesignTokens& t)
    {
        auto* previewRow = new QHBoxLayout();
        previewRow->setSpacing(t.spacing.md);

        previewCheckbox =
            new QCheckBox(QCoreApplication::translate("AutoGradientRemovalProcess", "Enable preview", nullptr), parent);
        previewCheckbox->setObjectName(QStringLiteral("previewCheckbox"));
        previewCheckbox->setChecked(true);
        previewRow->addWidget(previewCheckbox);
        previewRow->addStretch(1);
        controlsLayout->addLayout(previewRow);

        auto* targetLayout = new QFormLayout();
        targetLayout->setHorizontalSpacing(t.spacing.md);
        targetLayout->setVerticalSpacing(t.spacing.sm);

        auto* targetLabel =
            new QLabel(QCoreApplication::translate("AutoGradientRemovalProcess", "Target image", nullptr), parent);
        targetLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

        targetImageSelector = new QComboBox(parent);
        targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetLayout->setWidget(0, QFormLayout::FieldRole, targetImageSelector);

        controlsLayout->addLayout(targetLayout);
    }
};

inline AutoGradientRemovalProcessUiElements buildAutoGradientRemovalProcessUi(QWidget* parent)
{
    AutoGradientRemovalProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
