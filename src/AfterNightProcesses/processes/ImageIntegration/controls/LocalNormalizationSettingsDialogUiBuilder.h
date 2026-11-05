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

class LocalNormalizationSettingsDialogUiElements
{
public:
    QVBoxLayout* verticalLayout_main = nullptr;
    QLabel* infoLabel = nullptr;
    QLabel* referenceAnalysisNote = nullptr;
    QHBoxLayout* horizontalLayout_preset = nullptr;
    QLabel* presetLabel = nullptr;
    QComboBox* presetSelector = nullptr;
    QFrame* separatorPreset = nullptr;
    QScrollArea* scrollArea = nullptr;
    QWidget* scrollAreaContents = nullptr;
    QVBoxLayout* verticalLayout_scroll = nullptr;
    an::ui::AnCollapsibleSection* referenceGroupBox = nullptr;
    QFormLayout* formLayout_reference = nullptr;
    QLabel* referenceStrategyLabel = nullptr;
    QComboBox* referenceStrategy = nullptr;
    QLabel* maxIntegratedFramesLabel = nullptr;
    QSpinBox* maxIntegratedFrames = nullptr;
    an::ui::AnCollapsibleSection* backgroundGroupBox = nullptr;
    QFormLayout* formLayout_background = nullptr;
    QLabel* gridSizeLabel = nullptr;
    QHBoxLayout* horizontalLayout_gridSize = nullptr;
    QSpinBox* gridSize = nullptr;
    QLabel* gridSizeInfoLabel = nullptr;
    QLabel* gridInterpolationLabel = nullptr;
    QComboBox* gridInterpolation = nullptr;
    QLabel* backgroundSmoothingRadiusLabel = nullptr;
    QSpinBox* backgroundSmoothingRadius = nullptr;
    an::ui::AnCollapsibleSection* scaleGroupBox = nullptr;
    QFormLayout* formLayout_scale = nullptr;
    QLabel* scaleEvaluationLabel = nullptr;
    QComboBox* scaleEvaluation = nullptr;
    QLabel* scaleModeLabel = nullptr;
    QComboBox* scaleMode = nullptr;
    an::ui::AnCollapsibleSection* clippingGroupBox = nullptr;
    QFormLayout* formLayout_clipping = nullptr;
    QLabel* lowClipLabel = nullptr;
    QDoubleSpinBox* lowClip = nullptr;
    QLabel* highClipLabel = nullptr;
    QDoubleSpinBox* highClip = nullptr;
    an::ui::AnCollapsibleSection* advancedGroupBox = nullptr;
    QCheckBox* chkEnableAdvanced = nullptr;
    QVBoxLayout* verticalLayout_advanced = nullptr;
    QLabel* referenceSettingsLabel = nullptr;
    QFormLayout* formLayout_referenceAdvanced = nullptr;
    QLabel* evaluationCriteriaLabel = nullptr;
    QComboBox* evaluationCriteria = nullptr;
    QLabel* backgroundEstimatorSettingsLabel = nullptr;
    QFormLayout* formLayout_backgroundEstimator = nullptr;
    QLabel* backgroundEstimatorLabel = nullptr;
    QComboBox* backgroundEstimator = nullptr;
    QLabel* backgroundPercentileLabel = nullptr;
    QDoubleSpinBox* backgroundPercentile = nullptr;
    QLabel* sigmaClipKLabel = nullptr;
    QDoubleSpinBox* sigmaClipK = nullptr;
    QLabel* sigmaClipIterationsLabel = nullptr;
    QSpinBox* sigmaClipIterations = nullptr;
    QLabel* psfSettingsLabel = nullptr;
    QFormLayout* formLayout_psf = nullptr;
    QLabel* psfTypeLabel = nullptr;
    QComboBox* psfType = nullptr;
    QLabel* moffatBetaLabel = nullptr;
    QDoubleSpinBox* moffatBeta = nullptr;
    QLabel* maxStarsLabel = nullptr;
    QSpinBox* maxStars = nullptr;
    QLabel* minDetectionSnrLabel = nullptr;
    QDoubleSpinBox* minDetectionSnr = nullptr;
    QLabel* allowClusteredSourcesLabel = nullptr;
    QCheckBox* allowClusteredSources = nullptr;
    QLabel* outlierSettingsLabel = nullptr;
    QFormLayout* formLayout_outlier = nullptr;
    QLabel* hotPixelMedianRadiusLabel = nullptr;
    QSpinBox* hotPixelMedianRadius = nullptr;
    QLabel* noiseReductionRadiusLabel = nullptr;
    QSpinBox* noiseReductionRadius = nullptr;
    QLabel* experimentalSettingsLabel = nullptr;
    QFormLayout* formLayout_experimental = nullptr;
    QLabel* localScaleEnabledLabel = nullptr;
    QCheckBox* localScaleEnabled = nullptr;
    QLabel* localScaleSmoothingRadiusLabel = nullptr;
    QSpinBox* localScaleSmoothingRadius = nullptr;
    QFrame* separatorButtons = nullptr;
    QHBoxLayout* horizontalLayout_buttons = nullptr;
    QPushButton* resetButton = nullptr;
    QPushButton* cancelButton = nullptr;
    QPushButton* okButton = nullptr;

    void setupUi(QDialog* LocalNormalizationSettingsDialog)
    {
        const auto& t = an::ui::tokens();
        LocalNormalizationSettingsDialog->setObjectName("LocalNormalizationSettingsDialog");
        LocalNormalizationSettingsDialog->resize(480, 520);
        LocalNormalizationSettingsDialog->setSizeGripEnabled(true);
        verticalLayout_main = new QVBoxLayout(LocalNormalizationSettingsDialog);
        verticalLayout_main->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
        verticalLayout_main->setSpacing(t.spacing.md);
        verticalLayout_main->setObjectName("verticalLayout_main");
        infoLabel = new QLabel(LocalNormalizationSettingsDialog);
        infoLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        infoLabel->setFont(t.typography.caption);
        infoLabel->setWordWrap(true);
        infoLabel->setObjectName("infoLabel");
        verticalLayout_main->addWidget(infoLabel);
        referenceAnalysisNote = new QLabel(LocalNormalizationSettingsDialog);
        referenceAnalysisNote->setProperty("anStatusTone", QStringLiteral("secondary"));
        referenceAnalysisNote->setFont(t.typography.caption);
        referenceAnalysisNote->setWordWrap(true);
        referenceAnalysisNote->setVisible(false);
        referenceAnalysisNote->setObjectName("referenceAnalysisNote");
        verticalLayout_main->addWidget(referenceAnalysisNote);
        horizontalLayout_preset = new QHBoxLayout();
        horizontalLayout_preset->setObjectName("horizontalLayout_preset");
        presetLabel = new QLabel(LocalNormalizationSettingsDialog);
        presetLabel->setMinimumSize(QSize(100, 0));
        presetLabel->setObjectName("presetLabel");
        horizontalLayout_preset->addWidget(presetLabel);
        presetSelector = new QComboBox(LocalNormalizationSettingsDialog);
        presetSelector->setMinimumSize(QSize(200, 0));
        presetSelector->setObjectName("presetSelector");
        horizontalLayout_preset->addWidget(presetSelector);
        auto* spacerItem = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_preset->addItem(spacerItem);
        verticalLayout_main->addLayout(horizontalLayout_preset);
        separatorPreset = new QFrame(LocalNormalizationSettingsDialog);
        separatorPreset->setFrameShape(QFrame::Shape::HLine);
        separatorPreset->setFrameShadow(QFrame::Shadow::Sunken);
        separatorPreset->setObjectName("separatorPreset");
        verticalLayout_main->addWidget(separatorPreset);
        scrollArea = new QScrollArea(LocalNormalizationSettingsDialog);
        scrollArea->setFrameShape(QFrame::Shape::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollArea->setObjectName("scrollArea");
        scrollAreaContents = new QWidget();
        scrollAreaContents->setObjectName("scrollAreaContents");
        verticalLayout_scroll = new QVBoxLayout(scrollAreaContents);
        verticalLayout_scroll->setContentsMargins(0, 0, t.spacing.xs, 0);
        verticalLayout_scroll->setSpacing(t.spacing.sm);
        verticalLayout_scroll->setObjectName("verticalLayout_scroll");
        referenceGroupBox = new an::ui::AnCollapsibleSection(QString(), scrollAreaContents);
        referenceGroupBox->setObjectName("referenceGroupBox");
        formLayout_reference = new QFormLayout();
        referenceGroupBox->setContentLayout(formLayout_reference);
        formLayout_reference->setObjectName("formLayout_reference");
        referenceStrategyLabel = new QLabel(referenceGroupBox->contentWidget());
        referenceStrategyLabel->setObjectName("referenceStrategyLabel");
        formLayout_reference->setWidget(0, QFormLayout::ItemRole::LabelRole, referenceStrategyLabel);
        referenceStrategy = new QComboBox(referenceGroupBox->contentWidget());
        referenceStrategy->setMinimumSize(QSize(180, 0));
        referenceStrategy->setObjectName("referenceStrategy");
        formLayout_reference->setWidget(0, QFormLayout::ItemRole::FieldRole, referenceStrategy);
        maxIntegratedFramesLabel = new QLabel(referenceGroupBox->contentWidget());
        maxIntegratedFramesLabel->setObjectName("maxIntegratedFramesLabel");
        formLayout_reference->setWidget(1, QFormLayout::ItemRole::LabelRole, maxIntegratedFramesLabel);
        maxIntegratedFrames = new QSpinBox(referenceGroupBox->contentWidget());
        maxIntegratedFrames->setMinimumSize(QSize(180, 0));
        maxIntegratedFrames->setMinimum(1);
        maxIntegratedFrames->setMaximum(256);
        maxIntegratedFrames->setProperty("value", 16);
        maxIntegratedFrames->setObjectName("maxIntegratedFrames");
        formLayout_reference->setWidget(1, QFormLayout::ItemRole::FieldRole, maxIntegratedFrames);
        verticalLayout_scroll->addWidget(referenceGroupBox);
        backgroundGroupBox = new an::ui::AnCollapsibleSection(QString(), scrollAreaContents);
        backgroundGroupBox->setObjectName("backgroundGroupBox");
        formLayout_background = new QFormLayout();
        backgroundGroupBox->setContentLayout(formLayout_background);
        formLayout_background->setObjectName("formLayout_background");
        gridSizeLabel = new QLabel(backgroundGroupBox->contentWidget());
        gridSizeLabel->setObjectName("gridSizeLabel");
        formLayout_background->setWidget(0, QFormLayout::ItemRole::LabelRole, gridSizeLabel);
        horizontalLayout_gridSize = new QHBoxLayout();
        horizontalLayout_gridSize->setObjectName("horizontalLayout_gridSize");
        gridSize = new QSpinBox(backgroundGroupBox->contentWidget());
        gridSize->setMinimumSize(QSize(80, 0));
        gridSize->setMinimum(1);
        gridSize->setMaximum(32);
        gridSize->setProperty("value", 4);
        gridSize->setObjectName("gridSize");
        horizontalLayout_gridSize->addWidget(gridSize);
        gridSizeInfoLabel = new QLabel(backgroundGroupBox->contentWidget());
        gridSizeInfoLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        gridSizeInfoLabel->setObjectName("gridSizeInfoLabel");
        horizontalLayout_gridSize->addWidget(gridSizeInfoLabel);
        auto* spacerItem1 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_gridSize->addItem(spacerItem1);
        formLayout_background->setLayout(0, QFormLayout::ItemRole::FieldRole, horizontalLayout_gridSize);
        gridInterpolationLabel = new QLabel(backgroundGroupBox->contentWidget());
        gridInterpolationLabel->setObjectName("gridInterpolationLabel");
        formLayout_background->setWidget(1, QFormLayout::ItemRole::LabelRole, gridInterpolationLabel);
        gridInterpolation = new QComboBox(backgroundGroupBox->contentWidget());
        gridInterpolation->setMinimumSize(QSize(180, 0));
        gridInterpolation->setObjectName("gridInterpolation");
        formLayout_background->setWidget(1, QFormLayout::ItemRole::FieldRole, gridInterpolation);
        backgroundSmoothingRadiusLabel = new QLabel(backgroundGroupBox->contentWidget());
        backgroundSmoothingRadiusLabel->setObjectName("backgroundSmoothingRadiusLabel");
        formLayout_background->setWidget(2, QFormLayout::ItemRole::LabelRole, backgroundSmoothingRadiusLabel);
        backgroundSmoothingRadius = new QSpinBox(backgroundGroupBox->contentWidget());
        backgroundSmoothingRadius->setMinimumSize(QSize(180, 0));
        backgroundSmoothingRadius->setMinimum(0);
        backgroundSmoothingRadius->setMaximum(10);
        backgroundSmoothingRadius->setProperty("value", 1);
        backgroundSmoothingRadius->setObjectName("backgroundSmoothingRadius");
        formLayout_background->setWidget(2, QFormLayout::ItemRole::FieldRole, backgroundSmoothingRadius);
        verticalLayout_scroll->addWidget(backgroundGroupBox);
        scaleGroupBox = new an::ui::AnCollapsibleSection(QString(), scrollAreaContents);
        scaleGroupBox->setObjectName("scaleGroupBox");
        formLayout_scale = new QFormLayout();
        scaleGroupBox->setContentLayout(formLayout_scale);
        formLayout_scale->setObjectName("formLayout_scale");
        scaleEvaluationLabel = new QLabel(scaleGroupBox->contentWidget());
        scaleEvaluationLabel->setObjectName("scaleEvaluationLabel");
        formLayout_scale->setWidget(0, QFormLayout::ItemRole::LabelRole, scaleEvaluationLabel);
        scaleEvaluation = new QComboBox(scaleGroupBox->contentWidget());
        scaleEvaluation->setMinimumSize(QSize(180, 0));
        scaleEvaluation->setObjectName("scaleEvaluation");
        formLayout_scale->setWidget(0, QFormLayout::ItemRole::FieldRole, scaleEvaluation);
        scaleModeLabel = new QLabel(scaleGroupBox->contentWidget());
        scaleModeLabel->setObjectName("scaleModeLabel");
        formLayout_scale->setWidget(1, QFormLayout::ItemRole::LabelRole, scaleModeLabel);
        scaleMode = new QComboBox(scaleGroupBox->contentWidget());
        scaleMode->setMinimumSize(QSize(180, 0));
        scaleMode->setObjectName("scaleMode");
        formLayout_scale->setWidget(1, QFormLayout::ItemRole::FieldRole, scaleMode);
        verticalLayout_scroll->addWidget(scaleGroupBox);
        clippingGroupBox = new an::ui::AnCollapsibleSection(QString(), scrollAreaContents);
        clippingGroupBox->setObjectName("clippingGroupBox");
        formLayout_clipping = new QFormLayout();
        clippingGroupBox->setContentLayout(formLayout_clipping);
        formLayout_clipping->setObjectName("formLayout_clipping");
        lowClipLabel = new QLabel(clippingGroupBox->contentWidget());
        lowClipLabel->setObjectName("lowClipLabel");
        formLayout_clipping->setWidget(0, QFormLayout::ItemRole::LabelRole, lowClipLabel);
        lowClip = new QDoubleSpinBox(clippingGroupBox->contentWidget());
        lowClip->setMinimumSize(QSize(180, 0));
        lowClip->setDecimals(6);
        lowClip->setMinimum(0.0);
        lowClip->setMaximum(1.0);
        lowClip->setSingleStep(1e-05);
        lowClip->setProperty("value", 4.5e-05);
        lowClip->setObjectName("lowClip");
        formLayout_clipping->setWidget(0, QFormLayout::ItemRole::FieldRole, lowClip);
        highClipLabel = new QLabel(clippingGroupBox->contentWidget());
        highClipLabel->setObjectName("highClipLabel");
        formLayout_clipping->setWidget(1, QFormLayout::ItemRole::LabelRole, highClipLabel);
        highClip = new QDoubleSpinBox(clippingGroupBox->contentWidget());
        highClip->setMinimumSize(QSize(180, 0));
        highClip->setDecimals(3);
        highClip->setMinimum(0.0);
        highClip->setMaximum(1.0);
        highClip->setSingleStep(0.01);
        highClip->setProperty("value", 0.85);
        highClip->setObjectName("highClip");
        formLayout_clipping->setWidget(1, QFormLayout::ItemRole::FieldRole, highClip);
        verticalLayout_scroll->addWidget(clippingGroupBox);
        advancedGroupBox = new an::ui::AnCollapsibleSection(QString(), scrollAreaContents);
        advancedGroupBox->setObjectName("advancedGroupBox");
        verticalLayout_advanced = new QVBoxLayout();
        advancedGroupBox->setContentLayout(verticalLayout_advanced);
        verticalLayout_advanced->setObjectName("verticalLayout_advanced");
        chkEnableAdvanced = new QCheckBox(advancedGroupBox->contentWidget());
        chkEnableAdvanced->setObjectName("chkEnableAdvanced");
        verticalLayout_advanced->addWidget(chkEnableAdvanced);
        referenceSettingsLabel = new QLabel(advancedGroupBox->contentWidget());
        referenceSettingsLabel->setFont(t.typography.heading3);
        referenceSettingsLabel->setObjectName("referenceSettingsLabel");
        verticalLayout_advanced->addWidget(referenceSettingsLabel);
        formLayout_referenceAdvanced = new QFormLayout();
        formLayout_referenceAdvanced->setObjectName("formLayout_referenceAdvanced");
        evaluationCriteriaLabel = new QLabel(advancedGroupBox->contentWidget());
        evaluationCriteriaLabel->setObjectName("evaluationCriteriaLabel");
        formLayout_referenceAdvanced->setWidget(0, QFormLayout::ItemRole::LabelRole, evaluationCriteriaLabel);
        evaluationCriteria = new QComboBox(advancedGroupBox->contentWidget());
        evaluationCriteria->setMinimumSize(QSize(160, 0));
        evaluationCriteria->setObjectName("evaluationCriteria");
        formLayout_referenceAdvanced->setWidget(0, QFormLayout::ItemRole::FieldRole, evaluationCriteria);
        verticalLayout_advanced->addLayout(formLayout_referenceAdvanced);
        backgroundEstimatorSettingsLabel = new QLabel(advancedGroupBox->contentWidget());
        backgroundEstimatorSettingsLabel->setFont(t.typography.heading3);
        backgroundEstimatorSettingsLabel->setObjectName("backgroundEstimatorSettingsLabel");
        verticalLayout_advanced->addWidget(backgroundEstimatorSettingsLabel);
        formLayout_backgroundEstimator = new QFormLayout();
        formLayout_backgroundEstimator->setObjectName("formLayout_backgroundEstimator");
        backgroundEstimatorLabel = new QLabel(advancedGroupBox->contentWidget());
        backgroundEstimatorLabel->setObjectName("backgroundEstimatorLabel");
        formLayout_backgroundEstimator->setWidget(0, QFormLayout::ItemRole::LabelRole, backgroundEstimatorLabel);
        backgroundEstimator = new QComboBox(advancedGroupBox->contentWidget());
        backgroundEstimator->setMinimumSize(QSize(160, 0));
        backgroundEstimator->setObjectName("backgroundEstimator");
        formLayout_backgroundEstimator->setWidget(0, QFormLayout::ItemRole::FieldRole, backgroundEstimator);
        backgroundPercentileLabel = new QLabel(advancedGroupBox->contentWidget());
        backgroundPercentileLabel->setObjectName("backgroundPercentileLabel");
        formLayout_backgroundEstimator->setWidget(1, QFormLayout::ItemRole::LabelRole, backgroundPercentileLabel);
        backgroundPercentile = new QDoubleSpinBox(advancedGroupBox->contentWidget());
        backgroundPercentile->setMinimumSize(QSize(160, 0));
        backgroundPercentile->setDecimals(3);
        backgroundPercentile->setMinimum(0.0);
        backgroundPercentile->setMaximum(1.0);
        backgroundPercentile->setSingleStep(0.01);
        backgroundPercentile->setProperty("value", 0.25);
        backgroundPercentile->setObjectName("backgroundPercentile");
        formLayout_backgroundEstimator->setWidget(1, QFormLayout::ItemRole::FieldRole, backgroundPercentile);
        sigmaClipKLabel = new QLabel(advancedGroupBox->contentWidget());
        sigmaClipKLabel->setObjectName("sigmaClipKLabel");
        formLayout_backgroundEstimator->setWidget(2, QFormLayout::ItemRole::LabelRole, sigmaClipKLabel);
        sigmaClipK = new QDoubleSpinBox(advancedGroupBox->contentWidget());
        sigmaClipK->setMinimumSize(QSize(160, 0));
        sigmaClipK->setDecimals(2);
        sigmaClipK->setMinimum(0.1);
        sigmaClipK->setMaximum(10.0);
        sigmaClipK->setSingleStep(0.1);
        sigmaClipK->setProperty("value", 3.0);
        sigmaClipK->setObjectName("sigmaClipK");
        formLayout_backgroundEstimator->setWidget(2, QFormLayout::ItemRole::FieldRole, sigmaClipK);
        sigmaClipIterationsLabel = new QLabel(advancedGroupBox->contentWidget());
        sigmaClipIterationsLabel->setObjectName("sigmaClipIterationsLabel");
        formLayout_backgroundEstimator->setWidget(3, QFormLayout::ItemRole::LabelRole, sigmaClipIterationsLabel);
        sigmaClipIterations = new QSpinBox(advancedGroupBox->contentWidget());
        sigmaClipIterations->setMinimumSize(QSize(160, 0));
        sigmaClipIterations->setMinimum(1);
        sigmaClipIterations->setMaximum(20);
        sigmaClipIterations->setProperty("value", 3);
        sigmaClipIterations->setObjectName("sigmaClipIterations");
        formLayout_backgroundEstimator->setWidget(3, QFormLayout::ItemRole::FieldRole, sigmaClipIterations);
        verticalLayout_advanced->addLayout(formLayout_backgroundEstimator);
        psfSettingsLabel = new QLabel(advancedGroupBox->contentWidget());
        psfSettingsLabel->setFont(t.typography.heading3);
        psfSettingsLabel->setObjectName("psfSettingsLabel");
        verticalLayout_advanced->addWidget(psfSettingsLabel);
        formLayout_psf = new QFormLayout();
        formLayout_psf->setObjectName("formLayout_psf");
        psfTypeLabel = new QLabel(advancedGroupBox->contentWidget());
        psfTypeLabel->setObjectName("psfTypeLabel");
        formLayout_psf->setWidget(0, QFormLayout::ItemRole::LabelRole, psfTypeLabel);
        psfType = new QComboBox(advancedGroupBox->contentWidget());
        psfType->setMinimumSize(QSize(160, 0));
        psfType->setObjectName("psfType");
        formLayout_psf->setWidget(0, QFormLayout::ItemRole::FieldRole, psfType);
        moffatBetaLabel = new QLabel(advancedGroupBox->contentWidget());
        moffatBetaLabel->setObjectName("moffatBetaLabel");
        formLayout_psf->setWidget(1, QFormLayout::ItemRole::LabelRole, moffatBetaLabel);
        moffatBeta = new QDoubleSpinBox(advancedGroupBox->contentWidget());
        moffatBeta->setMinimumSize(QSize(160, 0));
        moffatBeta->setDecimals(1);
        moffatBeta->setMinimum(1.0);
        moffatBeta->setMaximum(10.0);
        moffatBeta->setSingleStep(0.5);
        moffatBeta->setProperty("value", 4.0);
        moffatBeta->setObjectName("moffatBeta");
        formLayout_psf->setWidget(1, QFormLayout::ItemRole::FieldRole, moffatBeta);
        maxStarsLabel = new QLabel(advancedGroupBox->contentWidget());
        maxStarsLabel->setObjectName("maxStarsLabel");
        formLayout_psf->setWidget(2, QFormLayout::ItemRole::LabelRole, maxStarsLabel);
        maxStars = new QSpinBox(advancedGroupBox->contentWidget());
        maxStars->setMinimumSize(QSize(160, 0));
        maxStars->setMinimum(0);
        maxStars->setMaximum(200000);
        maxStars->setProperty("value", 24576);
        maxStars->setObjectName("maxStars");
        formLayout_psf->setWidget(2, QFormLayout::ItemRole::FieldRole, maxStars);
        minDetectionSnrLabel = new QLabel(advancedGroupBox->contentWidget());
        minDetectionSnrLabel->setObjectName("minDetectionSnrLabel");
        formLayout_psf->setWidget(3, QFormLayout::ItemRole::LabelRole, minDetectionSnrLabel);
        minDetectionSnr = new QDoubleSpinBox(advancedGroupBox->contentWidget());
        minDetectionSnr->setMinimumSize(QSize(160, 0));
        minDetectionSnr->setDecimals(1);
        minDetectionSnr->setMinimum(1.0);
        minDetectionSnr->setMaximum(1000.0);
        minDetectionSnr->setSingleStep(1.0);
        minDetectionSnr->setProperty("value", 40.0);
        minDetectionSnr->setObjectName("minDetectionSnr");
        formLayout_psf->setWidget(3, QFormLayout::ItemRole::FieldRole, minDetectionSnr);
        allowClusteredSourcesLabel = new QLabel(advancedGroupBox->contentWidget());
        allowClusteredSourcesLabel->setObjectName("allowClusteredSourcesLabel");
        formLayout_psf->setWidget(4, QFormLayout::ItemRole::LabelRole, allowClusteredSourcesLabel);
        allowClusteredSources = new QCheckBox(advancedGroupBox->contentWidget());
        allowClusteredSources->setChecked(true);
        allowClusteredSources->setObjectName("allowClusteredSources");
        formLayout_psf->setWidget(4, QFormLayout::ItemRole::FieldRole, allowClusteredSources);
        verticalLayout_advanced->addLayout(formLayout_psf);
        outlierSettingsLabel = new QLabel(advancedGroupBox->contentWidget());
        outlierSettingsLabel->setFont(t.typography.heading3);
        outlierSettingsLabel->setObjectName("outlierSettingsLabel");
        verticalLayout_advanced->addWidget(outlierSettingsLabel);
        formLayout_outlier = new QFormLayout();
        formLayout_outlier->setObjectName("formLayout_outlier");
        hotPixelMedianRadiusLabel = new QLabel(advancedGroupBox->contentWidget());
        hotPixelMedianRadiusLabel->setObjectName("hotPixelMedianRadiusLabel");
        formLayout_outlier->setWidget(0, QFormLayout::ItemRole::LabelRole, hotPixelMedianRadiusLabel);
        hotPixelMedianRadius = new QSpinBox(advancedGroupBox->contentWidget());
        hotPixelMedianRadius->setMinimumSize(QSize(160, 0));
        hotPixelMedianRadius->setMinimum(0);
        hotPixelMedianRadius->setMaximum(10);
        hotPixelMedianRadius->setProperty("value", 0);
        hotPixelMedianRadius->setObjectName("hotPixelMedianRadius");
        formLayout_outlier->setWidget(0, QFormLayout::ItemRole::FieldRole, hotPixelMedianRadius);
        noiseReductionRadiusLabel = new QLabel(advancedGroupBox->contentWidget());
        noiseReductionRadiusLabel->setObjectName("noiseReductionRadiusLabel");
        formLayout_outlier->setWidget(1, QFormLayout::ItemRole::LabelRole, noiseReductionRadiusLabel);
        noiseReductionRadius = new QSpinBox(advancedGroupBox->contentWidget());
        noiseReductionRadius->setMinimumSize(QSize(160, 0));
        noiseReductionRadius->setMinimum(0);
        noiseReductionRadius->setMaximum(10);
        noiseReductionRadius->setProperty("value", 0);
        noiseReductionRadius->setObjectName("noiseReductionRadius");
        formLayout_outlier->setWidget(1, QFormLayout::ItemRole::FieldRole, noiseReductionRadius);
        verticalLayout_advanced->addLayout(formLayout_outlier);
        experimentalSettingsLabel = new QLabel(advancedGroupBox->contentWidget());
        experimentalSettingsLabel->setFont(t.typography.heading3);
        experimentalSettingsLabel->setObjectName("experimentalSettingsLabel");
        verticalLayout_advanced->addWidget(experimentalSettingsLabel);
        formLayout_experimental = new QFormLayout();
        formLayout_experimental->setObjectName("formLayout_experimental");
        localScaleEnabledLabel = new QLabel(advancedGroupBox->contentWidget());
        localScaleEnabledLabel->setObjectName("localScaleEnabledLabel");
        formLayout_experimental->setWidget(0, QFormLayout::ItemRole::LabelRole, localScaleEnabledLabel);
        localScaleEnabled = new QCheckBox(advancedGroupBox->contentWidget());
        localScaleEnabled->setObjectName("localScaleEnabled");
        formLayout_experimental->setWidget(0, QFormLayout::ItemRole::FieldRole, localScaleEnabled);
        localScaleSmoothingRadiusLabel = new QLabel(advancedGroupBox->contentWidget());
        localScaleSmoothingRadiusLabel->setObjectName("localScaleSmoothingRadiusLabel");
        formLayout_experimental->setWidget(1, QFormLayout::ItemRole::LabelRole, localScaleSmoothingRadiusLabel);
        localScaleSmoothingRadius = new QSpinBox(advancedGroupBox->contentWidget());
        localScaleSmoothingRadius->setMinimumSize(QSize(160, 0));
        localScaleSmoothingRadius->setMinimum(0);
        localScaleSmoothingRadius->setMaximum(10);
        localScaleSmoothingRadius->setProperty("value", 1);
        localScaleSmoothingRadius->setObjectName("localScaleSmoothingRadius");
        formLayout_experimental->setWidget(1, QFormLayout::ItemRole::FieldRole, localScaleSmoothingRadius);
        verticalLayout_advanced->addLayout(formLayout_experimental);
        verticalLayout_scroll->addWidget(advancedGroupBox);
        auto* spacerItem2 = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);
        verticalLayout_scroll->addItem(spacerItem2);
        scrollArea->setWidget(scrollAreaContents);
        verticalLayout_main->addWidget(scrollArea);
        separatorButtons = new QFrame(LocalNormalizationSettingsDialog);
        separatorButtons->setFrameShape(QFrame::Shape::HLine);
        separatorButtons->setFrameShadow(QFrame::Shadow::Sunken);
        separatorButtons->setObjectName("separatorButtons");
        verticalLayout_main->addWidget(separatorButtons);
        horizontalLayout_buttons = new QHBoxLayout();
        horizontalLayout_buttons->setSpacing(t.spacing.sm);
        horizontalLayout_buttons->setObjectName("horizontalLayout_buttons");
        resetButton = new QPushButton(LocalNormalizationSettingsDialog);
        resetButton->setObjectName("resetButton");
        horizontalLayout_buttons->addWidget(resetButton);
        auto* spacerItem3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_buttons->addItem(spacerItem3);
        cancelButton = new QPushButton(LocalNormalizationSettingsDialog);
        cancelButton->setObjectName("cancelButton");
        horizontalLayout_buttons->addWidget(cancelButton);
        okButton = new QPushButton(LocalNormalizationSettingsDialog);
        okButton->setDefault(true);
        okButton->setObjectName("okButton");
        okButton->setProperty("buttonVariant", QStringLiteral("primary"));
        horizontalLayout_buttons->addWidget(okButton);
        verticalLayout_main->addLayout(horizontalLayout_buttons);

        // Default card states: reference/background expanded, others collapsed
        referenceGroupBox->setExpanded(true);
        backgroundGroupBox->setExpanded(true);
        scaleGroupBox->setExpanded(false);
        clippingGroupBox->setExpanded(false);
        advancedGroupBox->setExpanded(false);

        retranslateUi(LocalNormalizationSettingsDialog);
        QObject::connect(okButton, &QPushButton::clicked, LocalNormalizationSettingsDialog, &QDialog::accept);
        QObject::connect(cancelButton, &QPushButton::clicked, LocalNormalizationSettingsDialog, &QDialog::reject);
        QMetaObject::connectSlotsByName(LocalNormalizationSettingsDialog);
    }

    void retranslateUi(QDialog* LocalNormalizationSettingsDialog)
    {
        LocalNormalizationSettingsDialog->setWindowTitle(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Local Normalization Settings"));
        infoLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                                       "Local normalization compensates for spatially varying "
                                                       "gradients and exposure differences between frames."));
        presetLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Preset:"));
        presetSelector->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog",
            "Quick presets for common use cases. Select \"Custom\" to manually configure all options."));
        referenceGroupBox->setTitle(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Reference Frame"));
        referenceStrategyLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Strategy:"));
        referenceStrategy->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog", "How to select or build the reference frame for normalization."));
        maxIntegratedFramesLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Max Frames:"));
        maxIntegratedFrames->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog",
            "Maximum number of best frames to integrate when using \"Integrate Best Frames\" strategy."));
        backgroundGroupBox->setTitle(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Background Model"));
        gridSizeLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Grid Size:"));
        gridSize->setToolTip(
            QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                        "Number of grid cells per axis. Smaller = larger regions, less local "
                                        "adaptivity. Larger = more local adaptivity but risk of noise imprinting."));
        gridSizeInfoLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "(5x5 = 25 nodes)"));
        gridInterpolationLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Interpolation:"));
        gridInterpolation->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog", "Interpolation used when sampling local-normalization grids."));
        backgroundSmoothingRadiusLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "BG Smoothing:"));
        backgroundSmoothingRadius->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog", "Smoothing radius for background grid nodes. 0 disables smoothing."));
        scaleGroupBox->setTitle(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Scale Evaluation"));
        scaleEvaluationLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Method:"));
        scaleEvaluation->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog", "How to compute the scale factor between frames. PSF Flux uses star "
                                                "measurements; Residual Ratio uses image statistics."));
        scaleModeLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Mode:"));
        scaleMode->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog",
            "Linked RGB: compute one scale factor from luminance/green (more color-stable). Per-Channel: compute "
            "independently per channel (better for narrowband)."));
        clippingGroupBox->setTitle(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Clipping"));
        lowClipLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Low Clip:"));
        lowClip->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog",
            "Pixels below this value are replaced with local background estimate to prevent artifacts."));
        highClipLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "High Clip:"));
        highClip->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog",
            "Pixels above this value are excluded from statistics to avoid saturation artifacts."));
        advancedGroupBox->setTitle(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Advanced Options"));
        chkEnableAdvanced->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Enable Advanced Options"));
        referenceSettingsLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Reference Settings"));
        evaluationCriteriaLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Evaluation:"));
        evaluationCriteria->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog", "Metric used to rank frames when selecting the best reference."));
        backgroundEstimatorSettingsLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Background Estimator"));
        backgroundEstimatorLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Estimator:"));
        backgroundEstimator->setToolTip(QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                                                    "Background estimator for each grid node."));
        backgroundPercentileLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Percentile:"));
        backgroundPercentile->setToolTip(QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                                                     "Percentile used when Estimator is Percentile."));
        sigmaClipKLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Sigma Clip K:"));
        sigmaClipK->setToolTip(QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                                           "Sigma threshold when using sigma-clipped estimators."));
        sigmaClipIterationsLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Sigma Iterations:"));
        sigmaClipIterations->setToolTip(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Maximum sigma-clipping iterations."));
        psfSettingsLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "PSF Flux Settings"));
        psfTypeLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "PSF Type:"));
        psfType->setToolTip(QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                                        "Point spread function model for star flux measurement."));
        moffatBetaLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Moffat Beta:"));
        moffatBeta->setToolTip(
            QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                        "Beta parameter for the Moffat PSF model. Lower values have broader wings."));
        maxStarsLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Max Stars:"));
        maxStars->setToolTip(QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                                         "Maximum number of stars to use for scale evaluation."));
        minDetectionSnrLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Min SNR:"));
        minDetectionSnr->setToolTip(
            QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                        "Stars below this signal-to-noise ratio are left out of the flux scale. It "
                                        "changes neither which stars are detected nor how references are ranked."));
        allowClusteredSourcesLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Allow Clustered:"));
        allowClusteredSources->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog",
            "Include stars in crowded regions. Disable for cleaner measurements in dense fields."));
        allowClusteredSources->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Include clustered sources"));
        outlierSettingsLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Outlier Rejection"));
        hotPixelMedianRadiusLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Hot Pixel Radius:"));
        hotPixelMedianRadius->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog", "Median filter radius for hot/cold pixel removal. 0 = disabled."));
        noiseReductionRadiusLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Noise Reduction:"));
        noiseReductionRadius->setToolTip(
            QCoreApplication::translate("LocalNormalizationSettingsDialog",
                                        "Gaussian blur radius to stabilize detection on noisy frames. 0 = disabled."));
        experimentalSettingsLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Local Scale (Experimental)"));
        localScaleEnabledLabel->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Enable:"));
        localScaleEnabled->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog",
            "Enable spatially varying scale k(x,y) instead of global scale. Experimental feature."));
        localScaleEnabled->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Use local scale k(x,y)"));
        localScaleSmoothingRadiusLabel->setText(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Smoothing:"));
        localScaleSmoothingRadius->setToolTip(QCoreApplication::translate(
            "LocalNormalizationSettingsDialog", "Smoothing radius for local scale grid to prevent noise imprinting."));
        resetButton->setToolTip(
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Reset all settings to default values"));
        resetButton->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Reset to Defaults"));
        cancelButton->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "Cancel"));
        okButton->setText(QCoreApplication::translate("LocalNormalizationSettingsDialog", "OK"));
    }
};

inline LocalNormalizationSettingsDialogUiElements buildLocalNormalizationSettingsDialogUi(QDialog* parent)
{
    LocalNormalizationSettingsDialogUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::imageintegration
