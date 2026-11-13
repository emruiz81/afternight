// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>
#include "components/AnCollapsibleSection.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::imageintegration
{

class StackingSettingsDialogUiElements
{
public:
    QVBoxLayout* verticalLayout_main = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* groupLabel = nullptr;
    QFrame* separator1 = nullptr;
    an::ui::AnCollapsibleSection* stackSettingsGroupBox = nullptr;
    QVBoxLayout* verticalLayout_stack = nullptr;
    QHBoxLayout* horizontalLayout_stackMethod = nullptr;
    QLabel* label_stackMethod = nullptr;
    QComboBox* stackMethodSelector = nullptr;
    QHBoxLayout* horizontalLayout_hdrExposureMerge = nullptr;
    QCheckBox* hdrExposureMergeEnabled = nullptr;
    QLabel* hdrExposureMergeInfoLabel = nullptr;
    QHBoxLayout* horizontalLayout_normalization = nullptr;
    QLabel* label_normalization = nullptr;
    QComboBox* normalizationMode = nullptr;
    QHBoxLayout* horizontalLayout_scaleEstimator = nullptr;
    QLabel* label_scaleEstimator = nullptr;
    QComboBox* scaleEstimatorSelector = nullptr;
    QHBoxLayout* horizontalLayout_normReferenceMode = nullptr;
    QLabel* label_normReferenceMode = nullptr;
    QComboBox* normalizationReferenceMode = nullptr;
    QHBoxLayout* horizontalLayout_normReferenceFrame = nullptr;
    QLabel* label_normReferenceFrame = nullptr;
    QComboBox* normalizationReferenceFrame = nullptr;
    QPushButton* normalizationReferenceBestButton = nullptr;
    QCheckBox* overlapNormalizationEnabled = nullptr;
    QHBoxLayout* horizontalLayout_overlapMinimumPixels = nullptr;
    QLabel* overlapMinimumPixelsLabel = nullptr;
    QSpinBox* overlapMinimumPixelsValue = nullptr;
    QHBoxLayout* horizontalLayout_overlapMinimumFraction = nullptr;
    QLabel* overlapMinimumFractionLabel = nullptr;
    QDoubleSpinBox* overlapMinimumFractionValue = nullptr;
    QHBoxLayout* horizontalLayout_localNormalization = nullptr;
    QCheckBox* localNormalizationEnabled = nullptr;
    QPushButton* localNormalizationConfigureButton = nullptr;
    QLabel* localNormalizationPresetLabel = nullptr;
    an::ui::AnCollapsibleSection* rejectionSettingsGroupBox = nullptr;
    QVBoxLayout* verticalLayout_rejection = nullptr;
    QHBoxLayout* horizontalLayout_rejectionMethod = nullptr;
    QLabel* label_rejectionMethod = nullptr;
    QComboBox* rejectionMethodSelector = nullptr;
    QHBoxLayout* horizontalLayout_sigmaLow = nullptr;
    QLabel* sigmaLowLabel = nullptr;
    QDoubleSpinBox* sigmaLowValue = nullptr;
    QHBoxLayout* horizontalLayout_sigmaHigh = nullptr;
    QLabel* sigmaHighLabel = nullptr;
    QDoubleSpinBox* sigmaHighValue = nullptr;
    QHBoxLayout* horizontalLayout_weighting = nullptr;
    QLabel* weightingLabel = nullptr;
    QComboBox* weightingSelector = nullptr;
    QPushButton* weightingOptionsButton = nullptr;
    an::ui::AnCollapsibleSection* weightingOptionsGroupBox = nullptr;
    QVBoxLayout* verticalLayout_weightingOptions = nullptr;
    QHBoxLayout* horizontalLayout_starCountScaling = nullptr;
    QLabel* starCountScalingLabel = nullptr;
    QComboBox* starCountScalingSelector = nullptr;
    QHBoxLayout* horizontalLayout_exponentialAlpha = nullptr;
    QLabel* exponentialAlphaLabel = nullptr;
    QDoubleSpinBox* exponentialAlphaValue = nullptr;
    QHBoxLayout* horizontalLayout_minWeightFloor = nullptr;
    QLabel* minWeightFloorLabel = nullptr;
    QDoubleSpinBox* minWeightFloorValue = nullptr;
    QCheckBox* localWeightingEnabled = nullptr;
    QHBoxLayout* horizontalLayout_localWeightingWindowSize = nullptr;
    QLabel* localWeightingWindowSizeLabel = nullptr;
    QSpinBox* localWeightingWindowSizeValue = nullptr;
    QHBoxLayout* horizontalLayout_localWeightingGridSpacing = nullptr;
    QLabel* localWeightingGridSpacingLabel = nullptr;
    QSpinBox* localWeightingGridSpacingValue = nullptr;
    QHBoxLayout* horizontalLayout_localWeightingSnrExponent = nullptr;
    QLabel* localWeightingSnrExponentLabel = nullptr;
    QDoubleSpinBox* localWeightingSnrExponentValue = nullptr;
    QHBoxLayout* horizontalLayout_rejectionMaps = nullptr;
    QCheckBox* createRejectionMaps = nullptr;
    QCheckBox* mergeRejectionMaps = nullptr;
    QCheckBox* spatialRejectionClustering = nullptr;
    QCheckBox* restackExcludingFlaggedFrames = nullptr;
    QCheckBox* createQualityMaps = nullptr;
    QCheckBox* discardRegisteredOutputsAfterStacking = nullptr;
    QCheckBox* solveFinalMasterIfNeeded = nullptr;
    an::ui::AnCollapsibleSection* drizzleSettingsGroupBox = nullptr;
    QVBoxLayout* verticalLayout_drizzle = nullptr;
    QCheckBox* chkEnableDrizzle = nullptr;
    QHBoxLayout* horizontalLayout_drizzleScale = nullptr;
    QLabel* drizzleScaleLabel = nullptr;
    QComboBox* drizzleScaleSelector = nullptr;
    QHBoxLayout* horizontalLayout_drizzleKernel = nullptr;
    QLabel* drizzleKernelLabel = nullptr;
    QComboBox* drizzleKernelSelector = nullptr;
    QHBoxLayout* horizontalLayout_drizzlePixfrac = nullptr;
    QLabel* drizzlePixfracLabel = nullptr;
    QDoubleSpinBox* drizzlePixfracValue = nullptr;
    QLabel* drizzleInfoLabel = nullptr;
    QFrame* separator2 = nullptr;
    QHBoxLayout* horizontalLayout_toolButtons = nullptr;
    QPushButton* recommendedButton = nullptr;
    QPushButton* resetButton = nullptr;
    QPushButton* applyToAllButton = nullptr;
    QHBoxLayout* horizontalLayout_dialogButtons = nullptr;
    QPushButton* cancelButton = nullptr;
    QPushButton* okButton = nullptr;

    void setupUi(QDialog* StackingSettingsDialog)
    {
        const auto& t = an::ui::tokens();
        StackingSettingsDialog->setObjectName("StackingSettingsDialog");
        StackingSettingsDialog->resize(500, 580);
        verticalLayout_main = new QVBoxLayout(StackingSettingsDialog);
        verticalLayout_main->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
        verticalLayout_main->setSpacing(t.spacing.lg);
        verticalLayout_main->setObjectName("verticalLayout_main");
        titleLabel = new QLabel(StackingSettingsDialog);
        titleLabel->setFont(t.typography.heading3);
        titleLabel->setObjectName("titleLabel");
        titleLabel->setTextFormat(Qt::PlainText);
        verticalLayout_main->addWidget(titleLabel);
        groupLabel = new QLabel(StackingSettingsDialog);
        groupLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        groupLabel->setText("");
        groupLabel->setWordWrap(true);
        groupLabel->setObjectName("groupLabel");
        verticalLayout_main->addWidget(groupLabel);
        separator1 = new QFrame(StackingSettingsDialog);
        separator1->setFrameShape(QFrame::Shape::HLine);
        separator1->setFrameShadow(QFrame::Shadow::Sunken);
        separator1->setObjectName("separator1");
        verticalLayout_main->addWidget(separator1);
        stackSettingsGroupBox = new an::ui::AnCollapsibleSection(QString(), StackingSettingsDialog);
        stackSettingsGroupBox->setObjectName("stackSettingsGroupBox");
        verticalLayout_stack = new QVBoxLayout();
        stackSettingsGroupBox->setContentLayout(verticalLayout_stack);
        verticalLayout_stack->setObjectName("verticalLayout_stack");
        horizontalLayout_stackMethod = new QHBoxLayout();
        horizontalLayout_stackMethod->setObjectName("horizontalLayout_stackMethod");
        label_stackMethod = new QLabel(stackSettingsGroupBox->contentWidget());
        label_stackMethod->setMinimumSize(QSize(120, 0));
        label_stackMethod->setObjectName("label_stackMethod");
        horizontalLayout_stackMethod->addWidget(label_stackMethod);
        stackMethodSelector = new QComboBox(stackSettingsGroupBox->contentWidget());
        stackMethodSelector->setMinimumSize(QSize(200, 0));
        stackMethodSelector->setObjectName("stackMethodSelector");
        horizontalLayout_stackMethod->addWidget(stackMethodSelector);
        auto* spacerItem = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_stackMethod->addItem(spacerItem);
        verticalLayout_stack->addLayout(horizontalLayout_stackMethod);
        horizontalLayout_hdrExposureMerge = new QHBoxLayout();
        horizontalLayout_hdrExposureMerge->setObjectName("horizontalLayout_hdrExposureMerge");
        hdrExposureMergeEnabled = new QCheckBox(stackSettingsGroupBox->contentWidget());
        hdrExposureMergeEnabled->setObjectName("hdrExposureMergeEnabled");
        horizontalLayout_hdrExposureMerge->addWidget(hdrExposureMergeEnabled);
        hdrExposureMergeInfoLabel = new QLabel(stackSettingsGroupBox->contentWidget());
        hdrExposureMergeInfoLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        hdrExposureMergeInfoLabel->setFont(t.typography.caption);
        hdrExposureMergeInfoLabel->setWordWrap(true);
        hdrExposureMergeInfoLabel->setObjectName("hdrExposureMergeInfoLabel");
        horizontalLayout_hdrExposureMerge->addWidget(hdrExposureMergeInfoLabel, 1);
        verticalLayout_stack->addLayout(horizontalLayout_hdrExposureMerge);
        horizontalLayout_normalization = new QHBoxLayout();
        horizontalLayout_normalization->setObjectName("horizontalLayout_normalization");
        label_normalization = new QLabel(stackSettingsGroupBox->contentWidget());
        label_normalization->setMinimumSize(QSize(120, 0));
        label_normalization->setObjectName("label_normalization");
        horizontalLayout_normalization->addWidget(label_normalization);
        normalizationMode = new QComboBox(stackSettingsGroupBox->contentWidget());
        normalizationMode->setMinimumSize(QSize(200, 0));
        normalizationMode->setObjectName("normalizationMode");
        horizontalLayout_normalization->addWidget(normalizationMode);
        auto* spacerItem1 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_normalization->addItem(spacerItem1);
        verticalLayout_stack->addLayout(horizontalLayout_normalization);
        horizontalLayout_scaleEstimator = new QHBoxLayout();
        horizontalLayout_scaleEstimator->setObjectName("horizontalLayout_scaleEstimator");
        label_scaleEstimator = new QLabel(stackSettingsGroupBox->contentWidget());
        label_scaleEstimator->setMinimumSize(QSize(120, 0));
        label_scaleEstimator->setObjectName("label_scaleEstimator");
        horizontalLayout_scaleEstimator->addWidget(label_scaleEstimator);
        scaleEstimatorSelector = new QComboBox(stackSettingsGroupBox->contentWidget());
        scaleEstimatorSelector->setMinimumSize(QSize(200, 0));
        scaleEstimatorSelector->setObjectName("scaleEstimatorSelector");
        horizontalLayout_scaleEstimator->addWidget(scaleEstimatorSelector);
        auto* spacerItem1a = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_scaleEstimator->addItem(spacerItem1a);
        verticalLayout_stack->addLayout(horizontalLayout_scaleEstimator);
        horizontalLayout_normReferenceMode = new QHBoxLayout();
        horizontalLayout_normReferenceMode->setObjectName("horizontalLayout_normReferenceMode");
        label_normReferenceMode = new QLabel(stackSettingsGroupBox->contentWidget());
        label_normReferenceMode->setMinimumSize(QSize(120, 0));
        label_normReferenceMode->setObjectName("label_normReferenceMode");
        horizontalLayout_normReferenceMode->addWidget(label_normReferenceMode);
        normalizationReferenceMode = new QComboBox(stackSettingsGroupBox->contentWidget());
        normalizationReferenceMode->setMinimumSize(QSize(200, 0));
        normalizationReferenceMode->setObjectName("normalizationReferenceMode");
        horizontalLayout_normReferenceMode->addWidget(normalizationReferenceMode);
        auto* spacerItem2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_normReferenceMode->addItem(spacerItem2);
        verticalLayout_stack->addLayout(horizontalLayout_normReferenceMode);
        horizontalLayout_normReferenceFrame = new QHBoxLayout();
        horizontalLayout_normReferenceFrame->setObjectName("horizontalLayout_normReferenceFrame");
        label_normReferenceFrame = new QLabel(stackSettingsGroupBox->contentWidget());
        label_normReferenceFrame->setMinimumSize(QSize(120, 0));
        label_normReferenceFrame->setObjectName("label_normReferenceFrame");
        horizontalLayout_normReferenceFrame->addWidget(label_normReferenceFrame);
        normalizationReferenceFrame = new QComboBox(stackSettingsGroupBox->contentWidget());
        normalizationReferenceFrame->setMinimumSize(QSize(200, 0));
        normalizationReferenceFrame->setObjectName("normalizationReferenceFrame");
        horizontalLayout_normReferenceFrame->addWidget(normalizationReferenceFrame);
        normalizationReferenceBestButton = new QPushButton(stackSettingsGroupBox->contentWidget());
        normalizationReferenceBestButton->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        normalizationReferenceBestButton->setObjectName("normalizationReferenceBestButton");
        horizontalLayout_normReferenceFrame->addWidget(normalizationReferenceBestButton);
        auto* spacerItem3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_normReferenceFrame->addItem(spacerItem3);
        verticalLayout_stack->addLayout(horizontalLayout_normReferenceFrame);
        overlapNormalizationEnabled = new QCheckBox(stackSettingsGroupBox->contentWidget());
        overlapNormalizationEnabled->setObjectName("overlapNormalizationEnabled");
        verticalLayout_stack->addWidget(overlapNormalizationEnabled);
        horizontalLayout_overlapMinimumPixels = new QHBoxLayout();
        horizontalLayout_overlapMinimumPixels->setObjectName("horizontalLayout_overlapMinimumPixels");
        overlapMinimumPixelsLabel = new QLabel(stackSettingsGroupBox->contentWidget());
        overlapMinimumPixelsLabel->setMinimumSize(QSize(120, 0));
        overlapMinimumPixelsLabel->setObjectName("overlapMinimumPixelsLabel");
        horizontalLayout_overlapMinimumPixels->addWidget(overlapMinimumPixelsLabel);
        overlapMinimumPixelsValue = new QSpinBox(stackSettingsGroupBox->contentWidget());
        overlapMinimumPixelsValue->setMinimumSize(QSize(200, 0));
        overlapMinimumPixelsValue->setMinimum(0);
        overlapMinimumPixelsValue->setMaximum(100000000);
        overlapMinimumPixelsValue->setSingleStep(100);
        overlapMinimumPixelsValue->setProperty("value", 1000);
        overlapMinimumPixelsValue->setObjectName("overlapMinimumPixelsValue");
        horizontalLayout_overlapMinimumPixels->addWidget(overlapMinimumPixelsValue);
        auto* spacerItem3a = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_overlapMinimumPixels->addItem(spacerItem3a);
        verticalLayout_stack->addLayout(horizontalLayout_overlapMinimumPixels);
        horizontalLayout_overlapMinimumFraction = new QHBoxLayout();
        horizontalLayout_overlapMinimumFraction->setObjectName("horizontalLayout_overlapMinimumFraction");
        overlapMinimumFractionLabel = new QLabel(stackSettingsGroupBox->contentWidget());
        overlapMinimumFractionLabel->setMinimumSize(QSize(120, 0));
        overlapMinimumFractionLabel->setObjectName("overlapMinimumFractionLabel");
        horizontalLayout_overlapMinimumFraction->addWidget(overlapMinimumFractionLabel);
        overlapMinimumFractionValue = new QDoubleSpinBox(stackSettingsGroupBox->contentWidget());
        overlapMinimumFractionValue->setMinimumSize(QSize(200, 0));
        overlapMinimumFractionValue->setDecimals(3);
        overlapMinimumFractionValue->setMinimum(0.0);
        overlapMinimumFractionValue->setMaximum(1.0);
        overlapMinimumFractionValue->setSingleStep(0.01);
        overlapMinimumFractionValue->setProperty("value", 0.01);
        overlapMinimumFractionValue->setObjectName("overlapMinimumFractionValue");
        horizontalLayout_overlapMinimumFraction->addWidget(overlapMinimumFractionValue);
        auto* spacerItem3b = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_overlapMinimumFraction->addItem(spacerItem3b);
        verticalLayout_stack->addLayout(horizontalLayout_overlapMinimumFraction);
        horizontalLayout_localNormalization = new QHBoxLayout();
        horizontalLayout_localNormalization->setObjectName("horizontalLayout_localNormalization");
        localNormalizationEnabled = new QCheckBox(stackSettingsGroupBox->contentWidget());
        localNormalizationEnabled->setObjectName("localNormalizationEnabled");
        horizontalLayout_localNormalization->addWidget(localNormalizationEnabled);
        localNormalizationConfigureButton = new QPushButton(stackSettingsGroupBox->contentWidget());
        localNormalizationConfigureButton->setObjectName("localNormalizationConfigureButton");
        horizontalLayout_localNormalization->addWidget(localNormalizationConfigureButton);
        localNormalizationPresetLabel = new QLabel(stackSettingsGroupBox->contentWidget());
        localNormalizationPresetLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        localNormalizationPresetLabel->setFont(t.typography.caption);
        localNormalizationPresetLabel->setObjectName("localNormalizationPresetLabel");
        horizontalLayout_localNormalization->addWidget(localNormalizationPresetLabel);
        auto* spacerItem4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_localNormalization->addItem(spacerItem4);
        verticalLayout_stack->addLayout(horizontalLayout_localNormalization);
        verticalLayout_main->addWidget(stackSettingsGroupBox);
        rejectionSettingsGroupBox = new an::ui::AnCollapsibleSection(QString(), StackingSettingsDialog);
        rejectionSettingsGroupBox->setObjectName("rejectionSettingsGroupBox");
        verticalLayout_rejection = new QVBoxLayout();
        rejectionSettingsGroupBox->setContentLayout(verticalLayout_rejection);
        verticalLayout_rejection->setObjectName("verticalLayout_rejection");
        horizontalLayout_rejectionMethod = new QHBoxLayout();
        horizontalLayout_rejectionMethod->setObjectName("horizontalLayout_rejectionMethod");
        label_rejectionMethod = new QLabel(rejectionSettingsGroupBox->contentWidget());
        label_rejectionMethod->setMinimumSize(QSize(120, 0));
        label_rejectionMethod->setObjectName("label_rejectionMethod");
        horizontalLayout_rejectionMethod->addWidget(label_rejectionMethod);
        rejectionMethodSelector = new QComboBox(rejectionSettingsGroupBox->contentWidget());
        rejectionMethodSelector->setMinimumSize(QSize(200, 0));
        rejectionMethodSelector->setObjectName("rejectionMethodSelector");
        horizontalLayout_rejectionMethod->addWidget(rejectionMethodSelector);
        auto* spacerItem5 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_rejectionMethod->addItem(spacerItem5);
        verticalLayout_rejection->addLayout(horizontalLayout_rejectionMethod);
        horizontalLayout_sigmaLow = new QHBoxLayout();
        horizontalLayout_sigmaLow->setObjectName("horizontalLayout_sigmaLow");
        sigmaLowLabel = new QLabel(rejectionSettingsGroupBox->contentWidget());
        sigmaLowLabel->setMinimumSize(QSize(120, 0));
        sigmaLowLabel->setObjectName("sigmaLowLabel");
        horizontalLayout_sigmaLow->addWidget(sigmaLowLabel);
        sigmaLowValue = new QDoubleSpinBox(rejectionSettingsGroupBox->contentWidget());
        sigmaLowValue->setMinimumSize(QSize(200, 0));
        sigmaLowValue->setDecimals(2);
        sigmaLowValue->setMinimum(0.01);
        sigmaLowValue->setMaximum(10.0);
        sigmaLowValue->setSingleStep(0.1);
        sigmaLowValue->setProperty("value", 3.0);
        sigmaLowValue->setObjectName("sigmaLowValue");
        horizontalLayout_sigmaLow->addWidget(sigmaLowValue);
        auto* spacerItem6 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_sigmaLow->addItem(spacerItem6);
        verticalLayout_rejection->addLayout(horizontalLayout_sigmaLow);
        horizontalLayout_sigmaHigh = new QHBoxLayout();
        horizontalLayout_sigmaHigh->setObjectName("horizontalLayout_sigmaHigh");
        sigmaHighLabel = new QLabel(rejectionSettingsGroupBox->contentWidget());
        sigmaHighLabel->setMinimumSize(QSize(120, 0));
        sigmaHighLabel->setObjectName("sigmaHighLabel");
        horizontalLayout_sigmaHigh->addWidget(sigmaHighLabel);
        sigmaHighValue = new QDoubleSpinBox(rejectionSettingsGroupBox->contentWidget());
        sigmaHighValue->setMinimumSize(QSize(200, 0));
        sigmaHighValue->setDecimals(2);
        sigmaHighValue->setMinimum(0.01);
        sigmaHighValue->setMaximum(10.0);
        sigmaHighValue->setSingleStep(0.1);
        sigmaHighValue->setProperty("value", 3.0);
        sigmaHighValue->setObjectName("sigmaHighValue");
        horizontalLayout_sigmaHigh->addWidget(sigmaHighValue);
        auto* spacerItem7 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_sigmaHigh->addItem(spacerItem7);
        verticalLayout_rejection->addLayout(horizontalLayout_sigmaHigh);
        horizontalLayout_weighting = new QHBoxLayout();
        horizontalLayout_weighting->setObjectName("horizontalLayout_weighting");
        weightingLabel = new QLabel(rejectionSettingsGroupBox->contentWidget());
        weightingLabel->setMinimumSize(QSize(120, 0));
        weightingLabel->setObjectName("weightingLabel");
        horizontalLayout_weighting->addWidget(weightingLabel);
        weightingSelector = new QComboBox(rejectionSettingsGroupBox->contentWidget());
        weightingSelector->setMinimumSize(QSize(200, 0));
        weightingSelector->setObjectName("weightingSelector");
        horizontalLayout_weighting->addWidget(weightingSelector);
        weightingOptionsButton = new QPushButton(rejectionSettingsGroupBox->contentWidget());
        weightingOptionsButton->setObjectName("weightingOptionsButton");
        horizontalLayout_weighting->addWidget(weightingOptionsButton);
        auto* spacerItem8 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_weighting->addItem(spacerItem8);
        verticalLayout_rejection->addLayout(horizontalLayout_weighting);
        weightingOptionsGroupBox =
            new an::ui::AnCollapsibleSection(QString(), rejectionSettingsGroupBox->contentWidget());
        weightingOptionsGroupBox->setObjectName("weightingOptionsGroupBox");
        weightingOptionsGroupBox->setVisible(false);
        verticalLayout_weightingOptions = new QVBoxLayout();
        weightingOptionsGroupBox->setContentLayout(verticalLayout_weightingOptions);
        verticalLayout_weightingOptions->setObjectName("verticalLayout_weightingOptions");
        horizontalLayout_starCountScaling = new QHBoxLayout();
        horizontalLayout_starCountScaling->setObjectName("horizontalLayout_starCountScaling");
        starCountScalingLabel = new QLabel(weightingOptionsGroupBox->contentWidget());
        starCountScalingLabel->setMinimumSize(QSize(120, 0));
        starCountScalingLabel->setObjectName("starCountScalingLabel");
        horizontalLayout_starCountScaling->addWidget(starCountScalingLabel);
        starCountScalingSelector = new QComboBox(weightingOptionsGroupBox->contentWidget());
        starCountScalingSelector->setMinimumSize(QSize(200, 0));
        starCountScalingSelector->setObjectName("starCountScalingSelector");
        horizontalLayout_starCountScaling->addWidget(starCountScalingSelector);
        auto* spacerItem9 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_starCountScaling->addItem(spacerItem9);
        verticalLayout_weightingOptions->addLayout(horizontalLayout_starCountScaling);
        horizontalLayout_exponentialAlpha = new QHBoxLayout();
        horizontalLayout_exponentialAlpha->setObjectName("horizontalLayout_exponentialAlpha");
        exponentialAlphaLabel = new QLabel(weightingOptionsGroupBox->contentWidget());
        exponentialAlphaLabel->setMinimumSize(QSize(120, 0));
        exponentialAlphaLabel->setObjectName("exponentialAlphaLabel");
        horizontalLayout_exponentialAlpha->addWidget(exponentialAlphaLabel);
        exponentialAlphaValue = new QDoubleSpinBox(weightingOptionsGroupBox->contentWidget());
        exponentialAlphaValue->setMinimumSize(QSize(200, 0));
        exponentialAlphaValue->setDecimals(2);
        exponentialAlphaValue->setMinimum(0.1);
        exponentialAlphaValue->setMaximum(10.0);
        exponentialAlphaValue->setSingleStep(0.1);
        exponentialAlphaValue->setProperty("value", 2.0);
        exponentialAlphaValue->setObjectName("exponentialAlphaValue");
        horizontalLayout_exponentialAlpha->addWidget(exponentialAlphaValue);
        auto* spacerItem10 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_exponentialAlpha->addItem(spacerItem10);
        verticalLayout_weightingOptions->addLayout(horizontalLayout_exponentialAlpha);
        horizontalLayout_minWeightFloor = new QHBoxLayout();
        horizontalLayout_minWeightFloor->setObjectName("horizontalLayout_minWeightFloor");
        minWeightFloorLabel = new QLabel(weightingOptionsGroupBox->contentWidget());
        minWeightFloorLabel->setMinimumSize(QSize(120, 0));
        minWeightFloorLabel->setObjectName("minWeightFloorLabel");
        horizontalLayout_minWeightFloor->addWidget(minWeightFloorLabel);
        minWeightFloorValue = new QDoubleSpinBox(weightingOptionsGroupBox->contentWidget());
        minWeightFloorValue->setMinimumSize(QSize(200, 0));
        minWeightFloorValue->setDecimals(3);
        minWeightFloorValue->setMinimum(0.0);
        minWeightFloorValue->setMaximum(1.0);
        minWeightFloorValue->setSingleStep(0.01);
        minWeightFloorValue->setProperty("value", 0.01);
        minWeightFloorValue->setObjectName("minWeightFloorValue");
        horizontalLayout_minWeightFloor->addWidget(minWeightFloorValue);
        auto* spacerItem11 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_minWeightFloor->addItem(spacerItem11);
        verticalLayout_weightingOptions->addLayout(horizontalLayout_minWeightFloor);
        localWeightingEnabled = new QCheckBox(weightingOptionsGroupBox->contentWidget());
        localWeightingEnabled->setObjectName("localWeightingEnabled");
        verticalLayout_weightingOptions->addWidget(localWeightingEnabled);
        horizontalLayout_localWeightingWindowSize = new QHBoxLayout();
        horizontalLayout_localWeightingWindowSize->setObjectName("horizontalLayout_localWeightingWindowSize");
        localWeightingWindowSizeLabel = new QLabel(weightingOptionsGroupBox->contentWidget());
        localWeightingWindowSizeLabel->setMinimumSize(QSize(120, 0));
        localWeightingWindowSizeLabel->setObjectName("localWeightingWindowSizeLabel");
        horizontalLayout_localWeightingWindowSize->addWidget(localWeightingWindowSizeLabel);
        localWeightingWindowSizeValue = new QSpinBox(weightingOptionsGroupBox->contentWidget());
        localWeightingWindowSizeValue->setMinimumSize(QSize(200, 0));
        localWeightingWindowSizeValue->setMinimum(3);
        localWeightingWindowSizeValue->setMaximum(31);
        localWeightingWindowSizeValue->setSingleStep(2);
        localWeightingWindowSizeValue->setProperty("value", 5);
        localWeightingWindowSizeValue->setObjectName("localWeightingWindowSizeValue");
        horizontalLayout_localWeightingWindowSize->addWidget(localWeightingWindowSizeValue);
        auto* spacerItem11a = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_localWeightingWindowSize->addItem(spacerItem11a);
        verticalLayout_weightingOptions->addLayout(horizontalLayout_localWeightingWindowSize);
        horizontalLayout_localWeightingGridSpacing = new QHBoxLayout();
        horizontalLayout_localWeightingGridSpacing->setObjectName("horizontalLayout_localWeightingGridSpacing");
        localWeightingGridSpacingLabel = new QLabel(weightingOptionsGroupBox->contentWidget());
        localWeightingGridSpacingLabel->setMinimumSize(QSize(120, 0));
        localWeightingGridSpacingLabel->setObjectName("localWeightingGridSpacingLabel");
        horizontalLayout_localWeightingGridSpacing->addWidget(localWeightingGridSpacingLabel);
        localWeightingGridSpacingValue = new QSpinBox(weightingOptionsGroupBox->contentWidget());
        localWeightingGridSpacingValue->setMinimumSize(QSize(200, 0));
        localWeightingGridSpacingValue->setMinimum(4);
        localWeightingGridSpacingValue->setMaximum(512);
        localWeightingGridSpacingValue->setSingleStep(4);
        localWeightingGridSpacingValue->setProperty("value", 64);
        localWeightingGridSpacingValue->setObjectName("localWeightingGridSpacingValue");
        horizontalLayout_localWeightingGridSpacing->addWidget(localWeightingGridSpacingValue);
        auto* spacerItem11b = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_localWeightingGridSpacing->addItem(spacerItem11b);
        verticalLayout_weightingOptions->addLayout(horizontalLayout_localWeightingGridSpacing);
        horizontalLayout_localWeightingSnrExponent = new QHBoxLayout();
        horizontalLayout_localWeightingSnrExponent->setObjectName("horizontalLayout_localWeightingSnrExponent");
        localWeightingSnrExponentLabel = new QLabel(weightingOptionsGroupBox->contentWidget());
        localWeightingSnrExponentLabel->setMinimumSize(QSize(120, 0));
        localWeightingSnrExponentLabel->setObjectName("localWeightingSnrExponentLabel");
        horizontalLayout_localWeightingSnrExponent->addWidget(localWeightingSnrExponentLabel);
        localWeightingSnrExponentValue = new QDoubleSpinBox(weightingOptionsGroupBox->contentWidget());
        localWeightingSnrExponentValue->setMinimumSize(QSize(200, 0));
        localWeightingSnrExponentValue->setDecimals(2);
        localWeightingSnrExponentValue->setMinimum(0.1);
        localWeightingSnrExponentValue->setMaximum(8.0);
        localWeightingSnrExponentValue->setSingleStep(0.1);
        localWeightingSnrExponentValue->setProperty("value", 2.0);
        localWeightingSnrExponentValue->setObjectName("localWeightingSnrExponentValue");
        horizontalLayout_localWeightingSnrExponent->addWidget(localWeightingSnrExponentValue);
        auto* spacerItem11c = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_localWeightingSnrExponent->addItem(spacerItem11c);
        verticalLayout_weightingOptions->addLayout(horizontalLayout_localWeightingSnrExponent);
        horizontalLayout_rejectionMaps = new QHBoxLayout();
        horizontalLayout_rejectionMaps->setObjectName("horizontalLayout_rejectionMaps");
        createRejectionMaps = new QCheckBox(rejectionSettingsGroupBox->contentWidget());
        createRejectionMaps->setObjectName("createRejectionMaps");
        horizontalLayout_rejectionMaps->addWidget(createRejectionMaps);
        mergeRejectionMaps = new QCheckBox(rejectionSettingsGroupBox->contentWidget());
        mergeRejectionMaps->setObjectName("mergeRejectionMaps");
        horizontalLayout_rejectionMaps->addWidget(mergeRejectionMaps);
        verticalLayout_rejection->addLayout(horizontalLayout_rejectionMaps);
        spatialRejectionClustering = new QCheckBox(rejectionSettingsGroupBox->contentWidget());
        spatialRejectionClustering->setObjectName("spatialRejectionClustering");
        verticalLayout_rejection->addWidget(spatialRejectionClustering);
        restackExcludingFlaggedFrames = new QCheckBox(rejectionSettingsGroupBox->contentWidget());
        restackExcludingFlaggedFrames->setObjectName("restackExcludingFlaggedFrames");
        verticalLayout_rejection->addWidget(restackExcludingFlaggedFrames);
        verticalLayout_main->addWidget(rejectionSettingsGroupBox);
        createQualityMaps = new QCheckBox(StackingSettingsDialog);
        createQualityMaps->setObjectName("createQualityMaps");
        verticalLayout_main->addWidget(createQualityMaps);
        discardRegisteredOutputsAfterStacking = new QCheckBox(StackingSettingsDialog);
        discardRegisteredOutputsAfterStacking->setObjectName("discardRegisteredOutputsAfterStacking");
        verticalLayout_main->addWidget(discardRegisteredOutputsAfterStacking);
        solveFinalMasterIfNeeded = new QCheckBox(StackingSettingsDialog);
        solveFinalMasterIfNeeded->setObjectName("solveFinalMasterIfNeeded");
        verticalLayout_main->addWidget(solveFinalMasterIfNeeded);
        drizzleSettingsGroupBox = new an::ui::AnCollapsibleSection(QString(), StackingSettingsDialog);
        drizzleSettingsGroupBox->setObjectName("drizzleSettingsGroupBox");
        verticalLayout_drizzle = new QVBoxLayout();
        drizzleSettingsGroupBox->setContentLayout(verticalLayout_drizzle);
        verticalLayout_drizzle->setObjectName("verticalLayout_drizzle");

        chkEnableDrizzle = new QCheckBox(drizzleSettingsGroupBox->contentWidget());
        chkEnableDrizzle->setObjectName("chkEnableDrizzle");
        verticalLayout_drizzle->addWidget(chkEnableDrizzle);

        horizontalLayout_drizzleScale = new QHBoxLayout();
        horizontalLayout_drizzleScale->setObjectName("horizontalLayout_drizzleScale");
        drizzleScaleLabel = new QLabel(drizzleSettingsGroupBox->contentWidget());
        drizzleScaleLabel->setMinimumSize(QSize(120, 0));
        drizzleScaleLabel->setObjectName("drizzleScaleLabel");
        horizontalLayout_drizzleScale->addWidget(drizzleScaleLabel);
        drizzleScaleSelector = new QComboBox(drizzleSettingsGroupBox->contentWidget());
        drizzleScaleSelector->setMinimumSize(QSize(200, 0));
        drizzleScaleSelector->setObjectName("drizzleScaleSelector");
        horizontalLayout_drizzleScale->addWidget(drizzleScaleSelector);
        auto* spacerItem12 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_drizzleScale->addItem(spacerItem12);
        verticalLayout_drizzle->addLayout(horizontalLayout_drizzleScale);
        horizontalLayout_drizzleKernel = new QHBoxLayout();
        horizontalLayout_drizzleKernel->setObjectName("horizontalLayout_drizzleKernel");
        drizzleKernelLabel = new QLabel(drizzleSettingsGroupBox->contentWidget());
        drizzleKernelLabel->setMinimumSize(QSize(120, 0));
        drizzleKernelLabel->setObjectName("drizzleKernelLabel");
        horizontalLayout_drizzleKernel->addWidget(drizzleKernelLabel);
        drizzleKernelSelector = new QComboBox(drizzleSettingsGroupBox->contentWidget());
        drizzleKernelSelector->setMinimumSize(QSize(200, 0));
        drizzleKernelSelector->setObjectName("drizzleKernelSelector");
        horizontalLayout_drizzleKernel->addWidget(drizzleKernelSelector);
        auto* spacerItem13 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_drizzleKernel->addItem(spacerItem13);
        verticalLayout_drizzle->addLayout(horizontalLayout_drizzleKernel);
        horizontalLayout_drizzlePixfrac = new QHBoxLayout();
        horizontalLayout_drizzlePixfrac->setObjectName("horizontalLayout_drizzlePixfrac");
        drizzlePixfracLabel = new QLabel(drizzleSettingsGroupBox->contentWidget());
        drizzlePixfracLabel->setMinimumSize(QSize(120, 0));
        drizzlePixfracLabel->setObjectName("drizzlePixfracLabel");
        horizontalLayout_drizzlePixfrac->addWidget(drizzlePixfracLabel);
        drizzlePixfracValue = new QDoubleSpinBox(drizzleSettingsGroupBox->contentWidget());
        drizzlePixfracValue->setMinimumSize(QSize(200, 0));
        drizzlePixfracValue->setDecimals(2);
        drizzlePixfracValue->setMinimum(0.1);
        drizzlePixfracValue->setMaximum(1.0);
        drizzlePixfracValue->setSingleStep(0.05);
        drizzlePixfracValue->setProperty("value", 0.9);
        drizzlePixfracValue->setObjectName("drizzlePixfracValue");
        horizontalLayout_drizzlePixfrac->addWidget(drizzlePixfracValue);
        auto* spacerItem14 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_drizzlePixfrac->addItem(spacerItem14);
        verticalLayout_drizzle->addLayout(horizontalLayout_drizzlePixfrac);
        drizzleInfoLabel = new QLabel(drizzleSettingsGroupBox->contentWidget());
        drizzleInfoLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        drizzleInfoLabel->setFont(t.typography.caption);
        drizzleInfoLabel->setWordWrap(true);
        drizzleInfoLabel->setObjectName("drizzleInfoLabel");
        verticalLayout_drizzle->addWidget(drizzleInfoLabel);
        verticalLayout_main->addWidget(drizzleSettingsGroupBox);
        auto* spacerItem15 = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);
        verticalLayout_main->addItem(spacerItem15);
        separator2 = new QFrame(StackingSettingsDialog);
        separator2->setFrameShape(QFrame::Shape::HLine);
        separator2->setFrameShadow(QFrame::Shadow::Sunken);
        separator2->setObjectName("separator2");
        verticalLayout_main->addWidget(separator2);
        horizontalLayout_toolButtons = new QHBoxLayout();
        horizontalLayout_toolButtons->setSpacing(t.spacing.sm);
        horizontalLayout_toolButtons->setObjectName("horizontalLayout_toolButtons");
        recommendedButton = new QPushButton(StackingSettingsDialog);
        recommendedButton->setObjectName("recommendedButton");
        horizontalLayout_toolButtons->addWidget(recommendedButton);
        auto* spacerItem16 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_toolButtons->addItem(spacerItem16);
        resetButton = new QPushButton(StackingSettingsDialog);
        resetButton->setObjectName("resetButton");
        horizontalLayout_toolButtons->addWidget(resetButton);
        applyToAllButton = new QPushButton(StackingSettingsDialog);
        applyToAllButton->setObjectName("applyToAllButton");
        horizontalLayout_toolButtons->addWidget(applyToAllButton);
        verticalLayout_main->addLayout(horizontalLayout_toolButtons);
        horizontalLayout_dialogButtons = new QHBoxLayout();
        horizontalLayout_dialogButtons->setSpacing(t.spacing.sm);
        horizontalLayout_dialogButtons->setObjectName("horizontalLayout_dialogButtons");
        auto* spacerItem17 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_dialogButtons->addItem(spacerItem17);
        cancelButton = new QPushButton(StackingSettingsDialog);
        cancelButton->setObjectName("cancelButton");
        horizontalLayout_dialogButtons->addWidget(cancelButton);
        okButton = new QPushButton(StackingSettingsDialog);
        okButton->setDefault(true);
        okButton->setObjectName("okButton");
        okButton->setProperty("buttonVariant", QStringLiteral("primary"));
        horizontalLayout_dialogButtons->addWidget(okButton);
        verticalLayout_main->addLayout(horizontalLayout_dialogButtons);

        const auto configureResponsiveRow = [&t](QHBoxLayout* row, QLabel* label, QWidget* field) {
            if (!row || !label || !field)
            {
                return;
            }

            label->setMinimumSize(QSize(120, 0));
            label->setMaximumSize(QSize(120, 16777215));
            label->setSizePolicy(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Preferred);

            field->setMinimumSize(QSize(0, 0));
            field->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);

            row->setSpacing(t.spacing.sm);
            row->setStretch(0, 0);
            row->setStretch(1, 1);

            if (row->count() > 2)
            {
                const int trailingIndex = row->count() - 1;
                if (auto* spacer = row->itemAt(trailingIndex)->spacerItem())
                {
                    spacer->changeSize(0, 0, QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Minimum);
                    row->setStretch(trailingIndex, 0);
                }
            }
        };

        configureResponsiveRow(horizontalLayout_stackMethod, label_stackMethod, stackMethodSelector);
        configureResponsiveRow(horizontalLayout_normalization, label_normalization, normalizationMode);
        configureResponsiveRow(horizontalLayout_scaleEstimator, label_scaleEstimator, scaleEstimatorSelector);
        configureResponsiveRow(horizontalLayout_normReferenceMode, label_normReferenceMode, normalizationReferenceMode);
        configureResponsiveRow(horizontalLayout_normReferenceFrame, label_normReferenceFrame,
                               normalizationReferenceFrame);
        configureResponsiveRow(horizontalLayout_overlapMinimumPixels, overlapMinimumPixelsLabel,
                               overlapMinimumPixelsValue);
        configureResponsiveRow(horizontalLayout_overlapMinimumFraction, overlapMinimumFractionLabel,
                               overlapMinimumFractionValue);
        configureResponsiveRow(horizontalLayout_rejectionMethod, label_rejectionMethod, rejectionMethodSelector);
        configureResponsiveRow(horizontalLayout_sigmaLow, sigmaLowLabel, sigmaLowValue);
        configureResponsiveRow(horizontalLayout_sigmaHigh, sigmaHighLabel, sigmaHighValue);
        configureResponsiveRow(horizontalLayout_weighting, weightingLabel, weightingSelector);
        configureResponsiveRow(horizontalLayout_starCountScaling, starCountScalingLabel, starCountScalingSelector);
        configureResponsiveRow(horizontalLayout_exponentialAlpha, exponentialAlphaLabel, exponentialAlphaValue);
        configureResponsiveRow(horizontalLayout_minWeightFloor, minWeightFloorLabel, minWeightFloorValue);
        configureResponsiveRow(horizontalLayout_localWeightingWindowSize, localWeightingWindowSizeLabel,
                               localWeightingWindowSizeValue);
        configureResponsiveRow(horizontalLayout_localWeightingGridSpacing, localWeightingGridSpacingLabel,
                               localWeightingGridSpacingValue);
        configureResponsiveRow(horizontalLayout_localWeightingSnrExponent, localWeightingSnrExponentLabel,
                               localWeightingSnrExponentValue);
        configureResponsiveRow(horizontalLayout_drizzleScale, drizzleScaleLabel, drizzleScaleSelector);
        configureResponsiveRow(horizontalLayout_drizzleKernel, drizzleKernelLabel, drizzleKernelSelector);
        configureResponsiveRow(horizontalLayout_drizzlePixfrac, drizzlePixfracLabel, drizzlePixfracValue);

        stackSettingsGroupBox->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Preferred);
        rejectionSettingsGroupBox->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Preferred);
        drizzleSettingsGroupBox->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Preferred);
        weightingOptionsGroupBox->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Preferred);

        // Expand all cards by default, except drizzle which stays collapsed
        stackSettingsGroupBox->setExpanded(true);
        rejectionSettingsGroupBox->setExpanded(true);
        weightingOptionsGroupBox->setExpanded(true);

        retranslateUi(StackingSettingsDialog);
        QObject::connect(okButton, &QPushButton::clicked, StackingSettingsDialog, &QDialog::accept);
        QObject::connect(cancelButton, &QPushButton::clicked, StackingSettingsDialog, &QDialog::reject);
        QMetaObject::connectSlotsByName(StackingSettingsDialog);
    }

    void retranslateUi(QDialog* StackingSettingsDialog)
    {
        StackingSettingsDialog->setWindowTitle(
            QCoreApplication::translate("StackingSettingsDialog", "Stacking Settings"));
        titleLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Stacking Settings for:"));
        stackSettingsGroupBox->setTitle(QCoreApplication::translate("StackingSettingsDialog", "Stack Settings"));
        label_stackMethod->setText(QCoreApplication::translate("StackingSettingsDialog", "Stack Method:"));
        hdrExposureMergeEnabled->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Merge mixed exposure lengths into one HDR master. This first release disables "
                                      "normalization, drizzle, and rejection-map outputs for the group."));
        hdrExposureMergeEnabled->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Enable HDR Exposure Merge"));
        hdrExposureMergeInfoLabel->setText(QCoreApplication::translate(
            "StackingSettingsDialog", "Buckets frames by exposure time, stacks each bucket, then blends highlights "
                                      "from shorter exposures into the long-exposure master."));
        label_normalization->setText(QCoreApplication::translate("StackingSettingsDialog", "Normalization:"));
        label_scaleEstimator->setText(QCoreApplication::translate("StackingSettingsDialog", "Scale Estimator:"));
        label_normReferenceMode->setText(QCoreApplication::translate("StackingSettingsDialog", "Reference Selection:"));
        label_normReferenceFrame->setText(QCoreApplication::translate("StackingSettingsDialog", "Reference Frame:"));
        normalizationReferenceBestButton->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Analyze this stacking group and preselect the best manual reference frame."));
        normalizationReferenceBestButton->setText(QCoreApplication::translate("StackingSettingsDialog", "Best"));
        overlapNormalizationEnabled->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Enable Overlap Normalization"));
        overlapMinimumPixelsLabel->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Min Overlap Pixels:"));
        overlapMinimumFractionLabel->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Min Overlap Fraction:"));
        localNormalizationEnabled->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Enable local normalization to compensate for spatially varying gradients and "
                                      "exposure differences. Overrides global normalization."));
        localNormalizationEnabled->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Enable Local Normalization"));
        localNormalizationConfigureButton->setToolTip(
            QCoreApplication::translate("StackingSettingsDialog", "Configure local normalization settings"));
        localNormalizationConfigureButton->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Configure..."));
        localNormalizationPresetLabel->setText(
            QCoreApplication::translate("StackingSettingsDialog", "(Standard preset)"));
        rejectionSettingsGroupBox->setTitle(
            QCoreApplication::translate("StackingSettingsDialog", "Rejection Settings"));
        label_rejectionMethod->setText(QCoreApplication::translate("StackingSettingsDialog", "Rejection Method:"));
        sigmaLowLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Sigma Low:"));
        sigmaHighLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Sigma High:"));
        weightingLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Weighting:"));
        weightingOptionsButton->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Open the detailed weighting controls for the selected weighting mode."));
        weightingOptionsButton->setText(QCoreApplication::translate("StackingSettingsDialog", "Weighting Options..."));
        weightingOptionsGroupBox->setTitle(QCoreApplication::translate("StackingSettingsDialog", "Weighting Options"));
        starCountScalingLabel->setToolTip(
            QCoreApplication::translate("StackingSettingsDialog", "Method to convert star counts to weights"));
        starCountScalingLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Star Count Scaling:"));
        starCountScalingSelector->setToolTip(
            QCoreApplication::translate("StackingSettingsDialog", "Linear: simple linear scaling\n"
                                                                  "Quadratic: more aggressive, Siril default\n"
                                                                  "Exponential: smooth exponential falloff\n"
                                                                  "Roundness Weighted: weight stars by roundness"));
        exponentialAlphaLabel->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Alpha parameter for exponential scaling (higher = steeper falloff)"));
        exponentialAlphaLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Exponential Alpha:"));
        exponentialAlphaValue->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Alpha parameter for exponential scaling (higher = steeper falloff)"));
        minWeightFloorLabel->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Minimum weight floor (0.0 to 1.0) to prevent complete exclusion of frames"));
        minWeightFloorLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Min Weight Floor:"));
        minWeightFloorValue->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Minimum weight floor (0.0 to 1.0) to prevent complete exclusion of frames"));
        localWeightingEnabled->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog",
            "Estimate a downsampled quality grid for each frame and apply spatially varying weights during stacking."));
        localWeightingEnabled->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Enable Per-Pixel Weighting"));
        localWeightingWindowSizeLabel->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Window size used to estimate local signal and variance. Odd values only."));
        localWeightingWindowSizeLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Window Size:"));
        localWeightingWindowSizeValue->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Window size used to estimate local signal and variance. Odd values only."));
        localWeightingGridSpacingLabel->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Distance in pixels between local-weight samples. Larger values use less memory "
                                      "but react more slowly to local defects."));
        localWeightingGridSpacingLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Grid Spacing:"));
        localWeightingGridSpacingValue->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Distance in pixels between local-weight samples. Larger values use less memory "
                                      "but react more slowly to local defects."));
        localWeightingSnrExponentLabel->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog",
            "Exponent applied to the local SNR proxy before combining it with temporal consistency."));
        localWeightingSnrExponentLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "SNR Exponent:"));
        localWeightingSnrExponentValue->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog",
            "Exponent applied to the local SNR proxy before combining it with temporal consistency."));
        createRejectionMaps->setText(QCoreApplication::translate("StackingSettingsDialog", "Create Rejection Maps"));
        mergeRejectionMaps->setText(QCoreApplication::translate("StackingSettingsDialog", "Merge Rejection Maps"));
        spatialRejectionClustering->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog",
            "Detect connected clusters in the rejection mask and include them in workflow reports."));
        spatialRejectionClustering->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Analyze Spatial Rejection Clusters"));
        restackExcludingFlaggedFrames->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "After detecting clustered artifacts, automatically re-stack without frames "
                                      "whose rejection rates are extreme outliers. Disabled for drizzle and HDR."));
        restackExcludingFlaggedFrames->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Re-Stack Excluding Flagged Frames"));
        createQualityMaps->setText(QCoreApplication::translate("StackingSettingsDialog", "Create Quality Maps"));
        discardRegisteredOutputsAfterStacking->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Allow Registered Images to Be Discarded"));
        solveFinalMasterIfNeeded->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "If the integrated master finishes without valid propagated WCS, plate-solve the "
                                      "saved master and rewrite its astrometric metadata before preview generation."));
        solveFinalMasterIfNeeded->setText(
            QCoreApplication::translate("StackingSettingsDialog", "Plate-Solve Final Master If Needed"));
        drizzleSettingsGroupBox->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Enable drizzle (Variable-Pixel Linear Reconstruction) for resolution "
                                      "enhancement. Requires many dithered frames for best results."));
        drizzleSettingsGroupBox->setTitle(QCoreApplication::translate("StackingSettingsDialog", "Drizzle"));
        chkEnableDrizzle->setText(QCoreApplication::translate("StackingSettingsDialog", "Enable Drizzle"));
        drizzleScaleLabel->setToolTip(
            QCoreApplication::translate("StackingSettingsDialog", "Output scale factor (2.0 = double resolution)"));
        drizzleScaleLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Scale:"));
        drizzleScaleSelector->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Output scale factor. Higher scales need more input frames."));
        drizzleKernelLabel->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Method for distributing input pixel flux onto output grid"));
        drizzleKernelLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Kernel:"));
        drizzleKernelSelector->setToolTip(QCoreApplication::translate("StackingSettingsDialog",
                                                                      "Square: Accurate polygon overlap (recommended)\n"
                                                                      "Point: Fast, all flux to nearest pixel\n"
                                                                      "Turbo: Fast axis-aligned approximation\n"
                                                                      "Gaussian: Smooth 2D Gaussian distribution\n"
                                                                      "Lanczos2: High-quality interpolation\n"
                                                                      "Lanczos3: Highest quality interpolation"));
        drizzlePixfracLabel->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog",
            "Pixel shrinkage factor (0.1-1.0). Smaller values reduce correlated noise but need more frames."));
        drizzlePixfracLabel->setText(QCoreApplication::translate("StackingSettingsDialog", "Pixel Fraction:"));
        drizzlePixfracValue->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "1.0 = no shrinkage (shift-and-add), 0.5 = half size. Start with 0.9 and "
                                      "decrease if you have many well-dithered frames."));
        drizzleInfoLabel->setText(QCoreApplication::translate(
            "StackingSettingsDialog", "ℹ️ Drizzle works best with 10+ dithered frames. Output will be 2× larger."));
        recommendedButton->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Show recommended stacking settings based on your frame count and type"));
        recommendedButton->setText(QCoreApplication::translate("StackingSettingsDialog", "Recommended Settings..."));
        resetButton->setToolTip(
            QCoreApplication::translate("StackingSettingsDialog", "Reset all settings to default values"));
        resetButton->setText(QCoreApplication::translate("StackingSettingsDialog", "Reset to Defaults"));
        applyToAllButton->setToolTip(QCoreApplication::translate(
            "StackingSettingsDialog", "Apply these settings to all master frames of the same type"));
        applyToAllButton->setText(QCoreApplication::translate("StackingSettingsDialog", "Apply to All Groups"));
        cancelButton->setText(QCoreApplication::translate("StackingSettingsDialog", "Cancel"));
        okButton->setText(QCoreApplication::translate("StackingSettingsDialog", "OK"));
    }
};

inline StackingSettingsDialogUiElements buildStackingSettingsDialogUi(QDialog* parent)
{
    StackingSettingsDialogUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::imageintegration
