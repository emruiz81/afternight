// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

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
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class BackgroundExtractionProcessUiElements
{
public:
    static constexpr int kLabelColumnWidth = 140;

    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;

    QComboBox* interpolationMethod = nullptr;
    QComboBox* correctionMode = nullptr;
    QSpinBox* degreeOrder = nullptr;
    QSlider* smoothingSlider = nullptr;
    QDoubleSpinBox* smoothingValue = nullptr;
    QCheckBox* outputBackgroundModel = nullptr;
    QCheckBox* addDither = nullptr;
    QCheckBox* previewCheckbox = nullptr;
    QComboBox* previewDisplaySelector = nullptr;
    QPushButton* previewButton = nullptr;

    QSpinBox* samplesPerLineValue = nullptr;
    QSlider* samplesPerLineSlider = nullptr;
    QDoubleSpinBox* gridToleranceValue = nullptr;
    QSlider* gridToleranceSlider = nullptr;
    QPushButton* generateSamplesButton = nullptr;
    QPushButton* clearSamplesButton = nullptr;

    QComboBox* targetImageSelector = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    void setupUi(QWidget* BackgroundExtractionProcess)
    {
        const auto& t = an::ui::tokens();
        if (BackgroundExtractionProcess->objectName().isEmpty())
            BackgroundExtractionProcess->setObjectName("BackgroundExtractionProcess");
        BackgroundExtractionProcess->resize(980, 700);
        BackgroundExtractionProcess->setMinimumSize(QSize(600, 420));
        BackgroundExtractionProcess->setWindowTitle(
            QCoreApplication::translate("BackgroundExtractionProcess", "Background Extraction", nullptr));

        horizontalLayoutMain = new QHBoxLayout(BackgroundExtractionProcess);
        horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

        const auto split =
            rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, BackgroundExtractionProcess,
                                                    QMargins(t.spacing.lg, t.spacing.sm, t.spacing.lg, t.spacing.sm));
        auto* controlsLayout = split.controlsLayout;
        auto* controlsPanel = split.controlsPanel;
        previewLayout = split.previewLayout;

        auto* sampleGroup = new QGroupBox(
            QCoreApplication::translate("BackgroundExtractionProcess", "Sample generation", nullptr), controlsPanel);
        auto* sampleGroupLayout = new QVBoxLayout(sampleGroup);
        sampleGroupLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        sampleGroupLayout->setSpacing(t.spacing.sm);

        auto* sampleGrid = new QGridLayout();
        sampleGrid->setHorizontalSpacing(t.spacing.sm);
        sampleGrid->setVerticalSpacing(t.spacing.sm);
        sampleGrid->setColumnStretch(1, 1);

        auto* samplesLabel = new QLabel(
            QCoreApplication::translate("BackgroundExtractionProcess", "Samples per Line", nullptr), sampleGroup);
        samplesLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sampleGrid->addWidget(samplesLabel, 0, 0, 1, 1);

        samplesPerLineSlider = new QSlider(Qt::Horizontal, sampleGroup);
        samplesPerLineSlider->setObjectName(QStringLiteral("samplesPerLineSlider"));
        samplesPerLineSlider->setMinimum(50);
        samplesPerLineSlider->setMaximum(1000);
        samplesPerLineSlider->setValue(200);
        samplesPerLineSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sampleGrid->addWidget(samplesPerLineSlider, 0, 1, 1, 1);

        samplesPerLineValue = new QSpinBox(sampleGroup);
        samplesPerLineValue->setObjectName(QStringLiteral("samplesPerLineValue"));
        samplesPerLineValue->setMinimum(5);
        samplesPerLineValue->setMaximum(100);
        samplesPerLineValue->setValue(20);
        samplesPerLineValue->setMinimumWidth(80);
        sampleGrid->addWidget(samplesPerLineValue, 0, 2, 1, 1);

        auto* toleranceLabel = new QLabel(
            QCoreApplication::translate("BackgroundExtractionProcess", "Grid Tolerance", nullptr), sampleGroup);
        toleranceLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sampleGrid->addWidget(toleranceLabel, 1, 0, 1, 1);

        gridToleranceSlider = new QSlider(Qt::Horizontal, sampleGroup);
        gridToleranceSlider->setObjectName(QStringLiteral("gridToleranceSlider"));
        gridToleranceSlider->setMinimum(1);
        gridToleranceSlider->setMaximum(600);
        gridToleranceSlider->setValue(200);
        gridToleranceSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sampleGrid->addWidget(gridToleranceSlider, 1, 1, 1, 1);

        gridToleranceValue = new QDoubleSpinBox(sampleGroup);
        gridToleranceValue->setObjectName(QStringLiteral("gridToleranceValue"));
        gridToleranceValue->setMinimum(0.01);
        gridToleranceValue->setMaximum(6.0);
        gridToleranceValue->setSingleStep(0.1);
        gridToleranceValue->setValue(2.0);
        gridToleranceValue->setMinimumWidth(80);
        sampleGrid->addWidget(gridToleranceValue, 1, 2, 1, 1);

        sampleGroupLayout->addLayout(sampleGrid);

        auto* sampleButtonsLayout = new QHBoxLayout();
        generateSamplesButton = new QPushButton(
            QCoreApplication::translate("BackgroundExtractionProcess", "Generate Samples", nullptr), sampleGroup);
        generateSamplesButton->setObjectName(QStringLiteral("generateSamplesButton"));
        sampleButtonsLayout->addWidget(generateSamplesButton);

        clearSamplesButton = new QPushButton(
            QCoreApplication::translate("BackgroundExtractionProcess", "Clear Samples", nullptr), sampleGroup);
        clearSamplesButton->setObjectName(QStringLiteral("clearSamplesButton"));
        sampleButtonsLayout->addWidget(clearSamplesButton);
        sampleButtonsLayout->addStretch(1);
        sampleGroupLayout->addLayout(sampleButtonsLayout);

        controlsLayout->addWidget(sampleGroup);

        auto* backgroundGroup =
            new QGroupBox(QCoreApplication::translate("BackgroundExtractionProcess", "Background Extraction", nullptr),
                          controlsPanel);
        auto* backgroundLayout = new QVBoxLayout(backgroundGroup);
        backgroundLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        backgroundLayout->setSpacing(t.spacing.sm);

        auto* methodGrid = new QGridLayout();
        methodGrid->setHorizontalSpacing(t.spacing.sm);
        methodGrid->setVerticalSpacing(t.spacing.sm);
        methodGrid->setColumnStretch(1, 1);

        auto* interpolationLabel =
            new QLabel(QCoreApplication::translate("BackgroundExtractionProcess", "Interpolation Method", nullptr),
                       backgroundGroup);
        interpolationLabel->setMinimumSize(QSize(kLabelColumnWidth, 0));
        interpolationLabel->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        methodGrid->addWidget(interpolationLabel, 0, 0, 1, 1);

        interpolationMethod = new QComboBox(backgroundGroup);
        interpolationMethod->setObjectName(QStringLiteral("interpolationMethod"));
        interpolationMethod->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        methodGrid->addWidget(interpolationMethod, 0, 1, 1, 2);

        auto* correctionLabel = new QLabel(
            QCoreApplication::translate("BackgroundExtractionProcess", "Correction", nullptr), backgroundGroup);
        correctionLabel->setMinimumSize(QSize(kLabelColumnWidth, 0));
        correctionLabel->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        methodGrid->addWidget(correctionLabel, 1, 0, 1, 1);

        correctionMode = new QComboBox(backgroundGroup);
        correctionMode->setObjectName(QStringLiteral("correctionMode"));
        correctionMode->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        methodGrid->addWidget(correctionMode, 1, 1, 1, 2);

        backgroundLayout->addLayout(methodGrid);

        auto* smoothingLayout = new QHBoxLayout();
        auto* smoothingLabel = new QLabel(
            QCoreApplication::translate("BackgroundExtractionProcess", "Smoothing", nullptr), backgroundGroup);
        smoothingLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        smoothingLayout->addWidget(smoothingLabel);

        smoothingSlider = new QSlider(Qt::Horizontal, backgroundGroup);
        smoothingSlider->setObjectName(QStringLiteral("smoothingSlider"));
        smoothingSlider->setMaximum(1000);
        smoothingSlider->setValue(500);
        smoothingSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        smoothingLayout->addWidget(smoothingSlider);

        smoothingValue = new QDoubleSpinBox(backgroundGroup);
        smoothingValue->setObjectName(QStringLiteral("smoothingValue"));
        smoothingValue->setMaximum(1.0);
        smoothingValue->setSingleStep(0.01);
        smoothingValue->setValue(0.5);
        smoothingValue->setMinimumWidth(80);
        smoothingLayout->addWidget(smoothingValue);
        backgroundLayout->addLayout(smoothingLayout);

        auto* degreeLayout = new QHBoxLayout();
        auto* degreeLabel = new QLabel(
            QCoreApplication::translate("BackgroundExtractionProcess", "Degree Order", nullptr), backgroundGroup);
        degreeLabel->setMinimumSize(QSize(kLabelColumnWidth, 0));
        degreeLabel->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        degreeLayout->addWidget(degreeLabel);

        degreeOrder = new QSpinBox(backgroundGroup);
        degreeOrder->setObjectName(QStringLiteral("degreeOrder"));
        degreeOrder->setMinimum(1);
        degreeOrder->setMaximum(4);
        degreeOrder->setValue(4);
        degreeOrder->setMinimumWidth(80);
        degreeLayout->addWidget(degreeOrder);
        degreeLayout->addStretch(1);
        backgroundLayout->addLayout(degreeLayout);

        auto* optionsLayout = new QHBoxLayout();
        outputBackgroundModel = new QCheckBox(
            QCoreApplication::translate("BackgroundExtractionProcess", "Output Background Model", nullptr),
            backgroundGroup);
        outputBackgroundModel->setObjectName(QStringLiteral("outputBackgroundModel"));
        optionsLayout->addWidget(outputBackgroundModel);

        addDither = new QCheckBox(QCoreApplication::translate("BackgroundExtractionProcess", "Add dither", nullptr),
                                  backgroundGroup);
        addDither->setObjectName(QStringLiteral("addDither"));
        optionsLayout->addWidget(addDither);
        optionsLayout->addStretch(1);
        backgroundLayout->addLayout(optionsLayout);

        auto* previewModeLayout = new QHBoxLayout();
        auto* previewModeLabel = new QLabel(
            QCoreApplication::translate("BackgroundExtractionProcess", "Preview Display", nullptr), backgroundGroup);
        previewModeLabel->setMinimumSize(QSize(kLabelColumnWidth, 0));
        previewModeLabel->setMaximumSize(QSize(kLabelColumnWidth, 16777215));
        previewModeLayout->addWidget(previewModeLabel);

        previewDisplaySelector = new QComboBox(backgroundGroup);
        previewDisplaySelector->setObjectName(QStringLiteral("previewDisplaySelector"));
        previewDisplaySelector->setMinimumWidth(180);
        previewDisplaySelector->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        previewDisplaySelector->addItems(
            {QCoreApplication::translate("BackgroundExtractionProcess", "Corrected image", nullptr),
             QCoreApplication::translate("BackgroundExtractionProcess", "Gradient model", nullptr)});
        previewDisplaySelector->setToolTip(QCoreApplication::translate(
            "BackgroundExtractionProcess",
            "Choose whether the preview shows the corrected image or the extracted gradient model.", nullptr));
        previewModeLayout->addWidget(previewDisplaySelector);
        previewModeLayout->addStretch(1);
        backgroundLayout->addLayout(previewModeLayout);

        previewButton = new QPushButton(QCoreApplication::translate("BackgroundExtractionProcess", "Preview", nullptr),
                                        backgroundGroup);
        previewButton->setObjectName(QStringLiteral("previewButton"));
        previewButton->setProperty("anButtonVariant", QStringLiteral("primary"));
        previewButton->setMinimumSize(QSize(104, 34));
        previewButton->setToolTip(QCoreApplication::translate(
            "BackgroundExtractionProcess", "Update the preview image using the current parameters and sample points.",
            nullptr));
        backgroundLayout->addWidget(previewButton, 0, Qt::AlignLeft);

        controlsLayout->addWidget(backgroundGroup);
        controlsLayout->addStretch(1);

        auto* previewRow = new QHBoxLayout();
        previewRow->setSpacing(t.spacing.md);

        previewCheckbox = new QCheckBox(
            QCoreApplication::translate("BackgroundExtractionProcess", "Enable preview", nullptr), controlsPanel);
        previewCheckbox->setObjectName(QStringLiteral("previewCheckbox"));
        previewCheckbox->setChecked(true);
        previewCheckbox->setToolTip(QCoreApplication::translate("BackgroundExtractionProcess",
                                                                "Show or hide the real-time preview pane.", nullptr));
        previewRow->addWidget(previewCheckbox);
        previewRow->addStretch(1);

        controlsLayout->addLayout(previewRow);

        auto* selectorLayout = new QFormLayout();
        auto* targetLabel = new QLabel(
            QCoreApplication::translate("BackgroundExtractionProcess", "Target Image", nullptr), controlsPanel);
        selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

        targetImageSelector = new QComboBox(controlsPanel);
        targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        selectorLayout->setWidget(0, QFormLayout::FieldRole, targetImageSelector);
        controlsLayout->addLayout(selectorLayout);

        buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                         controlsPanel);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        controlsLayout->addWidget(buttonBox);
    } // setupUi
};

inline BackgroundExtractionProcessUiElements buildBackgroundExtractionProcessUi(QWidget* parent)
{
    BackgroundExtractionProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
