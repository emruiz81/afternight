// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LocalNormalizationSettingsDialog.h"

#include <cmath>

#include <verdigris/wobjectimpl.h>
#include <QPushButton>

W_OBJECT_IMPL(LocalNormalizationSettingsDialog)

namespace
{
[[nodiscard]] QString normalizedSettingText(const QString& value)
{
    QString normalized = value.trimmed().toCaseFolded();
    normalized.replace(QLatin1Char('_'), QLatin1Char(' '));
    normalized.replace(QLatin1Char('-'), QLatin1Char(' '));
    return normalized.simplified();
}

[[nodiscard]] bool sameSettingText(const QString& left, const QString& right)
{
    return normalizedSettingText(left) == normalizedSettingText(right);
}

[[nodiscard]] bool sameSettingDouble(double left, double right)
{
    return std::abs(left - right) <= 1e-9;
}

[[nodiscard]] LocalNormalizationSettings settingsForPreset(LocalNormalizationSettingsDialog::Preset preset)
{
    LocalNormalizationSettings settings;

    switch (preset)
    {
    case LocalNormalizationSettingsDialog::Preset::Quick:
        settings.gridSize = 2;
        settings.referenceStrategy = QStringLiteral("Single Best Frame");
        break;
    case LocalNormalizationSettingsDialog::Preset::Standard:
        settings.gridSize = 4;
        settings.referenceStrategy = QStringLiteral("Integrate Best Frames");
        break;
    case LocalNormalizationSettingsDialog::Preset::Fine:
        settings.gridSize = 8;
        settings.referenceStrategy = QStringLiteral("Integrate Best Frames");
        break;
    case LocalNormalizationSettingsDialog::Preset::Custom:
        break;
    }

    return settings;
}

[[nodiscard]] bool matchesPresetSettings(const LocalNormalizationSettings& settings,
                                         const LocalNormalizationSettings& preset)
{
    return settings.gridSize == preset.gridSize &&
           sameSettingText(settings.gridInterpolation, preset.gridInterpolation) &&
           settings.localScaleEnabled == preset.localScaleEnabled &&
           sameSettingText(settings.referenceStrategy, preset.referenceStrategy) &&
           settings.maxIntegratedFrames == preset.maxIntegratedFrames &&
           sameSettingText(settings.evaluationCriteria, preset.evaluationCriteria) &&
           sameSettingText(settings.backgroundEstimator, preset.backgroundEstimator) &&
           sameSettingDouble(settings.backgroundPercentile, preset.backgroundPercentile) &&
           sameSettingDouble(settings.sigmaClipK, preset.sigmaClipK) &&
           settings.sigmaClipIterations == preset.sigmaClipIterations &&
           settings.backgroundSmoothingRadius == preset.backgroundSmoothingRadius &&
           sameSettingText(settings.scaleEvaluation, preset.scaleEvaluation) &&
           sameSettingText(settings.scaleMode, preset.scaleMode) && sameSettingText(settings.psfType, preset.psfType) &&
           sameSettingDouble(settings.moffatBeta, preset.moffatBeta) && settings.maxStars == preset.maxStars &&
           sameSettingDouble(settings.minDetectionSnr, preset.minDetectionSnr) &&
           settings.allowClusteredSources == preset.allowClusteredSources &&
           sameSettingDouble(settings.lowClip, preset.lowClip) &&
           sameSettingDouble(settings.highClip, preset.highClip) &&
           settings.hotPixelMedianRadius == preset.hotPixelMedianRadius &&
           settings.noiseReductionRadius == preset.noiseReductionRadius &&
           settings.localScaleSmoothingRadius == preset.localScaleSmoothingRadius;
}
} // namespace

LocalNormalizationSettingsDialog::LocalNormalizationSettingsDialog(QWidget* parent) : QDialog(parent)
{
    m_ui = an::ui::imageintegration::buildLocalNormalizationSettingsDialogUi(this);
    setWindowModality(Qt::WindowModal);
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    initialize();
}

LocalNormalizationSettingsDialog::~LocalNormalizationSettingsDialog() {}

void LocalNormalizationSettingsDialog::initialize()
{
    populateComboBoxes();
    connectSignals();
    resetToDefaults();
    updateControlsState();
    updateGridSizeInfo();
}

void LocalNormalizationSettingsDialog::populateComboBoxes()
{
    // Presets
    m_ui.presetSelector->addItem(tr("Quick (Grid 2)"));
    m_ui.presetSelector->addItem(tr("Standard (Grid 4)"));
    m_ui.presetSelector->addItem(tr("Fine (Grid 8)"));
    m_ui.presetSelector->addItem(tr("Custom"));

    // Reference strategy
    m_ui.referenceStrategy->addItem(tr("Explicit Reference"));
    m_ui.referenceStrategy->addItem(tr("Single Best Frame"));
    m_ui.referenceStrategy->addItem(tr("Integrate Best Frames"));

    // Evaluation criteria
    m_ui.evaluationCriteria->addItem(tr("PSF Signal Weight"));
    m_ui.evaluationCriteria->addItem(tr("Quality Score"));
    m_ui.evaluationCriteria->addItem(tr("PSF SNR"));
    m_ui.evaluationCriteria->addItem(tr("M* (Background)"));
    m_ui.evaluationCriteria->addItem(tr("Median"));
    m_ui.evaluationCriteria->addItem(tr("Star Count"));

    // Scale evaluation
    m_ui.scaleEvaluation->addItem(tr("PSF Flux"));
    m_ui.scaleEvaluation->addItem(tr("Residual Ratio"));

    // Scale mode
    m_ui.scaleMode->addItem(tr("Linked RGB"));
    m_ui.scaleMode->addItem(tr("Per-Channel"));

    // PSF type (simplified: Auto / Gaussian / Moffat)
    m_ui.psfType->addItem(tr("Auto"));
    m_ui.psfType->addItem(tr("Gaussian"));
    m_ui.psfType->addItem(tr("Moffat"));

    // Grid interpolation
    m_ui.gridInterpolation->addItem(tr("Bilinear"));
    m_ui.gridInterpolation->addItem(tr("Bicubic"));

    // Background estimator
    m_ui.backgroundEstimator->addItem(tr("Percentile"));
    m_ui.backgroundEstimator->addItem(tr("Sigma Clipped Median"));
    m_ui.backgroundEstimator->addItem(tr("Sigma Clipped Mode"));
}

void LocalNormalizationSettingsDialog::connectSignals()
{
    // Preset changes
    QObject::connect(m_ui.presetSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &LocalNormalizationSettingsDialog::onPresetChanged);

    // Reference strategy changes affect max frames visibility
    QObject::connect(m_ui.referenceStrategy, &QComboBox::currentTextChanged, this,
                     &LocalNormalizationSettingsDialog::onReferenceStrategyChanged);

    // Scale evaluation changes affect PSF settings visibility
    QObject::connect(m_ui.scaleEvaluation, &QComboBox::currentTextChanged, this,
                     &LocalNormalizationSettingsDialog::onScaleEvaluationChanged);

    // Background estimator affects dependent controls
    QObject::connect(m_ui.backgroundEstimator, &QComboBox::currentTextChanged, this,
                     &LocalNormalizationSettingsDialog::onBackgroundEstimatorChanged);

    // PSF type changes affect moffat beta visibility
    QObject::connect(m_ui.psfType, &QComboBox::currentTextChanged, this,
                     &LocalNormalizationSettingsDialog::onPsfTypeChanged);

    // Local scale enabled affects smoothing visibility
    QObject::connect(m_ui.localScaleEnabled, &QCheckBox::toggled, this,
                     &LocalNormalizationSettingsDialog::onLocalScaleEnabledChanged);

    // Grid size changes update info label
    QObject::connect(m_ui.gridSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     &LocalNormalizationSettingsDialog::onGridSizeChanged);

    // Advanced group box toggle
    QObject::connect(m_ui.chkEnableAdvanced, &QCheckBox::toggled, this,
                     &LocalNormalizationSettingsDialog::updateControlsState);

    // Reset button
    QObject::connect(m_ui.resetButton, &QPushButton::clicked, this,
                     &LocalNormalizationSettingsDialog::onResetButtonClicked);

    // Any setting change should check if we need to switch to Custom preset
    auto checkCustom = [this]() { checkForCustomPreset(); };

    QObject::connect(m_ui.referenceStrategy, &QComboBox::currentTextChanged, this, checkCustom);
    QObject::connect(m_ui.maxIntegratedFrames, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.evaluationCriteria, &QComboBox::currentTextChanged, this, checkCustom);
    QObject::connect(m_ui.gridSize, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.gridInterpolation, &QComboBox::currentTextChanged, this, checkCustom);
    QObject::connect(m_ui.backgroundEstimator, &QComboBox::currentTextChanged, this, checkCustom);
    QObject::connect(m_ui.backgroundPercentile, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     checkCustom);
    QObject::connect(m_ui.sigmaClipK, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.sigmaClipIterations, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.backgroundSmoothingRadius, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.scaleEvaluation, &QComboBox::currentTextChanged, this, checkCustom);
    QObject::connect(m_ui.scaleMode, &QComboBox::currentTextChanged, this, checkCustom);
    QObject::connect(m_ui.psfType, &QComboBox::currentTextChanged, this, checkCustom);
    QObject::connect(m_ui.moffatBeta, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.maxStars, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.minDetectionSnr, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.allowClusteredSources, &QCheckBox::toggled, this, checkCustom);
    QObject::connect(m_ui.lowClip, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.highClip, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.hotPixelMedianRadius, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.noiseReductionRadius, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
    QObject::connect(m_ui.localScaleEnabled, &QCheckBox::toggled, this, checkCustom);
    QObject::connect(m_ui.localScaleSmoothingRadius, QOverload<int>::of(&QSpinBox::valueChanged), this, checkCustom);
}

void LocalNormalizationSettingsDialog::updateControlsState()
{
    const bool advancedEnabled = m_ui.chkEnableAdvanced->isChecked();

    // Reference strategy controls
    const QString strategy = m_ui.referenceStrategy->currentText();
    const bool usesEvaluation = !strategy.contains(QStringLiteral("Explicit"));
    m_ui.referenceSettingsLabel->setEnabled(advancedEnabled);
    m_ui.evaluationCriteriaLabel->setEnabled(advancedEnabled && usesEvaluation);
    m_ui.evaluationCriteria->setEnabled(advancedEnabled && usesEvaluation);

    const bool integrate = strategy.contains(QStringLiteral("Integrate"));
    m_ui.maxIntegratedFramesLabel->setEnabled(integrate);
    m_ui.maxIntegratedFrames->setEnabled(integrate);

    // Background estimator controls
    const QString estimator = m_ui.backgroundEstimator->currentText();
    const bool percentileMode = estimator.contains(QStringLiteral("Percentile"), Qt::CaseInsensitive);
    m_ui.backgroundEstimatorSettingsLabel->setEnabled(advancedEnabled);
    m_ui.backgroundEstimatorLabel->setEnabled(advancedEnabled);
    m_ui.backgroundEstimator->setEnabled(advancedEnabled);
    m_ui.backgroundPercentileLabel->setEnabled(advancedEnabled && percentileMode);
    m_ui.backgroundPercentile->setEnabled(advancedEnabled && percentileMode);

    const bool sigmaMode = estimator.contains(QStringLiteral("Sigma"), Qt::CaseInsensitive);
    m_ui.sigmaClipKLabel->setEnabled(advancedEnabled && sigmaMode);
    m_ui.sigmaClipK->setEnabled(advancedEnabled && sigmaMode);
    m_ui.sigmaClipIterationsLabel->setEnabled(advancedEnabled && sigmaMode);
    m_ui.sigmaClipIterations->setEnabled(advancedEnabled && sigmaMode);

    // PSF settings only relevant for PSF Flux evaluation
    const bool psfFlux = m_ui.scaleEvaluation->currentText().contains(QStringLiteral("PSF"));
    m_ui.psfSettingsLabel->setEnabled(advancedEnabled && psfFlux);
    m_ui.psfTypeLabel->setEnabled(advancedEnabled && psfFlux);
    m_ui.psfType->setEnabled(advancedEnabled && psfFlux);
    m_ui.maxStarsLabel->setEnabled(advancedEnabled && psfFlux);
    m_ui.maxStars->setEnabled(advancedEnabled && psfFlux);
    m_ui.minDetectionSnrLabel->setEnabled(advancedEnabled && psfFlux);
    m_ui.minDetectionSnr->setEnabled(advancedEnabled && psfFlux);
    m_ui.allowClusteredSourcesLabel->setEnabled(advancedEnabled && psfFlux);
    m_ui.allowClusteredSources->setEnabled(advancedEnabled && psfFlux);

    // Moffat beta only visible when PSF type is Moffat (and PSF Flux active)
    const bool moffatActive = psfFlux && m_ui.psfType->currentText().contains(QStringLiteral("Moffat"));
    m_ui.moffatBetaLabel->setEnabled(advancedEnabled && moffatActive);
    m_ui.moffatBeta->setEnabled(advancedEnabled && moffatActive);
    m_ui.moffatBetaLabel->setVisible(moffatActive && m_referenceAnalysisOffered);
    m_ui.moffatBeta->setVisible(moffatActive && m_referenceAnalysisOffered);

    m_ui.outlierSettingsLabel->setEnabled(advancedEnabled);
    m_ui.hotPixelMedianRadiusLabel->setEnabled(advancedEnabled);
    m_ui.hotPixelMedianRadius->setEnabled(advancedEnabled);
    m_ui.noiseReductionRadiusLabel->setEnabled(advancedEnabled);
    m_ui.noiseReductionRadius->setEnabled(advancedEnabled);
    const std::initializer_list<QWidget*> referenceAnalysis = {m_ui.referenceGroupBox,
                                                               m_ui.referenceSettingsLabel,
                                                               m_ui.evaluationCriteriaLabel,
                                                               m_ui.evaluationCriteria,
                                                               m_ui.scaleEvaluationLabel,
                                                               m_ui.scaleEvaluation,
                                                               m_ui.psfSettingsLabel,
                                                               m_ui.psfTypeLabel,
                                                               m_ui.psfType,
                                                               m_ui.maxStarsLabel,
                                                               m_ui.maxStars,
                                                               m_ui.minDetectionSnrLabel,
                                                               m_ui.minDetectionSnr,
                                                               m_ui.allowClusteredSourcesLabel,
                                                               m_ui.allowClusteredSources,
                                                               m_ui.outlierSettingsLabel,
                                                               m_ui.hotPixelMedianRadiusLabel,
                                                               m_ui.hotPixelMedianRadius,
                                                               m_ui.noiseReductionRadiusLabel,
                                                               m_ui.noiseReductionRadius};
    for (QWidget* widget : referenceAnalysis)
    {
        widget->setVisible(m_referenceAnalysisOffered);
    }

    m_ui.experimentalSettingsLabel->setEnabled(advancedEnabled);
    m_ui.localScaleEnabledLabel->setEnabled(advancedEnabled);
    m_ui.localScaleEnabled->setEnabled(advancedEnabled);

    // Local scale smoothing
    const bool localScale = m_ui.localScaleEnabled->isChecked();
    m_ui.localScaleSmoothingRadiusLabel->setEnabled(advancedEnabled && localScale);
    m_ui.localScaleSmoothingRadius->setEnabled(advancedEnabled && localScale);
}

void LocalNormalizationSettingsDialog::updateGridSizeInfo()
{
    int g = m_ui.gridSize->value();
    int nodes = (g + 1) * (g + 1);
    m_ui.gridSizeInfoLabel->setText(tr("(%1x%1 = %2 nodes)").arg(g + 1).arg(nodes));
}

void LocalNormalizationSettingsDialog::applyPreset(Preset preset)
{
    m_updatingFromPreset = true;

    switch (preset)
    {
    case Preset::Quick:
        m_ui.gridSize->setValue(2);
        m_ui.gridInterpolation->setCurrentText(tr("Bicubic"));
        m_ui.backgroundEstimator->setCurrentText(tr("Sigma Clipped Median"));
        m_ui.backgroundPercentile->setValue(0.25);
        m_ui.sigmaClipK->setValue(3.0);
        m_ui.sigmaClipIterations->setValue(3);
        m_ui.backgroundSmoothingRadius->setValue(1);
        m_ui.referenceStrategy->setCurrentText(tr("Single Best Frame"));
        m_ui.scaleEvaluation->setCurrentText(tr("PSF Flux"));
        m_ui.scaleMode->setCurrentText(tr("Linked RGB"));
        m_ui.chkEnableAdvanced->setChecked(false);
        break;

    case Preset::Standard:
        m_ui.gridSize->setValue(4);
        m_ui.gridInterpolation->setCurrentText(tr("Bicubic"));
        m_ui.backgroundEstimator->setCurrentText(tr("Sigma Clipped Median"));
        m_ui.backgroundPercentile->setValue(0.25);
        m_ui.sigmaClipK->setValue(3.0);
        m_ui.sigmaClipIterations->setValue(3);
        m_ui.backgroundSmoothingRadius->setValue(1);
        m_ui.referenceStrategy->setCurrentText(tr("Integrate Best Frames"));
        m_ui.maxIntegratedFrames->setValue(16);
        m_ui.evaluationCriteria->setCurrentText(tr("PSF Signal Weight"));
        m_ui.scaleEvaluation->setCurrentText(tr("PSF Flux"));
        m_ui.scaleMode->setCurrentText(tr("Linked RGB"));
        m_ui.chkEnableAdvanced->setChecked(false);
        break;

    case Preset::Fine:
        m_ui.gridSize->setValue(8);
        m_ui.gridInterpolation->setCurrentText(tr("Bicubic"));
        m_ui.backgroundEstimator->setCurrentText(tr("Sigma Clipped Median"));
        m_ui.backgroundPercentile->setValue(0.25);
        m_ui.sigmaClipK->setValue(3.0);
        m_ui.sigmaClipIterations->setValue(3);
        m_ui.backgroundSmoothingRadius->setValue(1);
        m_ui.referenceStrategy->setCurrentText(tr("Integrate Best Frames"));
        m_ui.maxIntegratedFrames->setValue(16);
        m_ui.evaluationCriteria->setCurrentText(tr("PSF Signal Weight"));
        m_ui.scaleEvaluation->setCurrentText(tr("PSF Flux"));
        m_ui.scaleMode->setCurrentText(tr("Linked RGB"));
        m_ui.chkEnableAdvanced->setChecked(false);
        break;

    case Preset::Custom:
        // Do nothing - user is customizing
        break;
    }

    m_updatingFromPreset = false;
    updateControlsState();
    updateGridSizeInfo();
}

void LocalNormalizationSettingsDialog::checkForCustomPreset()
{
    if (m_updatingFromPreset)
        return;

    // If user changes any setting, switch to Custom preset
    int currentIndex = m_ui.presetSelector->currentIndex();
    if (currentIndex != static_cast<int>(Preset::Custom))
    {
        m_updatingFromPreset = true;
        m_ui.presetSelector->setCurrentIndex(static_cast<int>(Preset::Custom));
        m_updatingFromPreset = false;
    }
}

void LocalNormalizationSettingsDialog::onPresetChanged(int index)
{
    if (m_updatingFromPreset)
        return;

    applyPreset(static_cast<Preset>(index));
}

void LocalNormalizationSettingsDialog::onReferenceStrategyChanged(const QString& text)
{
    Q_UNUSED(text)
    updateControlsState();
}

void LocalNormalizationSettingsDialog::onScaleEvaluationChanged(const QString& text)
{
    Q_UNUSED(text)
    updateControlsState();
}

void LocalNormalizationSettingsDialog::onBackgroundEstimatorChanged(const QString& text)
{
    Q_UNUSED(text)
    updateControlsState();
}

void LocalNormalizationSettingsDialog::onPsfTypeChanged(const QString& text)
{
    Q_UNUSED(text)
    updateControlsState();
}

void LocalNormalizationSettingsDialog::onLocalScaleEnabledChanged(bool checked)
{
    Q_UNUSED(checked)
    updateControlsState();
}

void LocalNormalizationSettingsDialog::onGridSizeChanged(int value)
{
    Q_UNUSED(value)
    updateGridSizeInfo();
}

void LocalNormalizationSettingsDialog::onResetButtonClicked()
{
    resetToDefaults();
}

void LocalNormalizationSettingsDialog::setReferenceAnalysisOffered(bool offered, const QString& reason)
{
    m_referenceAnalysisOffered = offered;
    m_ui.referenceAnalysisNote->setText(offered ? QString() : reason);
    m_ui.referenceAnalysisNote->setVisible(!offered && !reason.isEmpty());
    updateControlsState();
}

void LocalNormalizationSettingsDialog::resetToDefaults()
{
    m_updatingFromPreset = true;

    // Set Standard preset as default
    m_ui.presetSelector->setCurrentIndex(static_cast<int>(Preset::Standard));

    // Reference
    m_ui.referenceStrategy->setCurrentText(tr("Integrate Best Frames"));
    m_ui.maxIntegratedFrames->setValue(16);
    m_ui.evaluationCriteria->setCurrentText(tr("PSF Signal Weight"));

    // Background model
    m_ui.gridSize->setValue(4);
    m_ui.gridInterpolation->setCurrentText(tr("Bicubic"));
    m_ui.backgroundEstimator->setCurrentText(tr("Sigma Clipped Median"));
    m_ui.backgroundPercentile->setValue(0.25);
    m_ui.sigmaClipK->setValue(3.0);
    m_ui.sigmaClipIterations->setValue(3);
    m_ui.backgroundSmoothingRadius->setValue(1);

    // Scale evaluation
    m_ui.scaleEvaluation->setCurrentText(tr("PSF Flux"));
    m_ui.scaleMode->setCurrentText(tr("Linked RGB"));

    // Clipping
    m_ui.lowClip->setValue(4.5e-05);
    m_ui.highClip->setValue(0.85);

    // Advanced: PSF
    m_ui.psfType->setCurrentText(tr("Auto"));
    m_ui.moffatBeta->setValue(4.0);
    m_ui.maxStars->setValue(24576);
    m_ui.minDetectionSnr->setValue(40.0);
    m_ui.allowClusteredSources->setChecked(true);

    // Advanced: Outlier
    m_ui.hotPixelMedianRadius->setValue(0);
    m_ui.noiseReductionRadius->setValue(0);

    // Advanced: Local scale
    m_ui.localScaleEnabled->setChecked(false);
    m_ui.localScaleSmoothingRadius->setValue(1);

    // Collapse advanced by default
    m_ui.chkEnableAdvanced->setChecked(false);

    m_updatingFromPreset = false;
    updateControlsState();
    updateGridSizeInfo();
}

// Preset getter/setter
LocalNormalizationSettingsDialog::Preset LocalNormalizationSettingsDialog::getPreset() const
{
    return static_cast<Preset>(m_ui.presetSelector->currentIndex());
}

void LocalNormalizationSettingsDialog::setPreset(Preset preset)
{
    m_ui.presetSelector->setCurrentIndex(static_cast<int>(preset));
    applyPreset(preset);
}

LocalNormalizationSettingsDialog::Preset
LocalNormalizationSettingsDialog::presetForSettings(const LocalNormalizationSettings& settings)
{
    for (const Preset preset : {Preset::Quick, Preset::Standard, Preset::Fine})
    {
        if (matchesPresetSettings(settings, settingsForPreset(preset)))
        {
            return preset;
        }
    }

    return Preset::Custom;
}

// Reference getters/setters
QString LocalNormalizationSettingsDialog::getReferenceStrategy() const
{
    return m_ui.referenceStrategy->currentText();
}

void LocalNormalizationSettingsDialog::setReferenceStrategy(const QString& strategy)
{
    m_ui.referenceStrategy->setCurrentText(strategy);
    updateControlsState();
}

int LocalNormalizationSettingsDialog::getMaxIntegratedFrames() const
{
    return m_ui.maxIntegratedFrames->value();
}

void LocalNormalizationSettingsDialog::setMaxIntegratedFrames(int value)
{
    m_ui.maxIntegratedFrames->setValue(value);
}

QString LocalNormalizationSettingsDialog::getEvaluationCriteria() const
{
    return m_ui.evaluationCriteria->currentText();
}

void LocalNormalizationSettingsDialog::setEvaluationCriteria(const QString& criteria)
{
    m_ui.evaluationCriteria->setCurrentText(criteria);
}

// Background model
int LocalNormalizationSettingsDialog::getGridSize() const
{
    return m_ui.gridSize->value();
}

void LocalNormalizationSettingsDialog::setGridSize(int value)
{
    m_ui.gridSize->setValue(value);
    updateGridSizeInfo();
}

QString LocalNormalizationSettingsDialog::getGridInterpolation() const
{
    return m_ui.gridInterpolation->currentText();
}

void LocalNormalizationSettingsDialog::setGridInterpolation(const QString& interpolation)
{
    const QString normalized = interpolation.trimmed().toLower();
    if (normalized == QStringLiteral("bilinear"))
    {
        m_ui.gridInterpolation->setCurrentText(tr("Bilinear"));
        return;
    }
    if (normalized == QStringLiteral("bicubic"))
    {
        m_ui.gridInterpolation->setCurrentText(tr("Bicubic"));
        return;
    }
    m_ui.gridInterpolation->setCurrentText(interpolation);
}

QString LocalNormalizationSettingsDialog::getBackgroundEstimator() const
{
    return m_ui.backgroundEstimator->currentText();
}

void LocalNormalizationSettingsDialog::setBackgroundEstimator(const QString& estimator)
{
    const QString normalized = estimator.trimmed().toLower();
    if (normalized == QStringLiteral("percentile"))
    {
        m_ui.backgroundEstimator->setCurrentText(tr("Percentile"));
    }
    else if (normalized == QStringLiteral("sigma clipped median") ||
             normalized == QStringLiteral("sigma_clipped_median"))
    {
        m_ui.backgroundEstimator->setCurrentText(tr("Sigma Clipped Median"));
    }
    else if (normalized == QStringLiteral("sigma clipped mode") || normalized == QStringLiteral("sigma_clipped_mode"))
    {
        m_ui.backgroundEstimator->setCurrentText(tr("Sigma Clipped Mode"));
    }
    else
    {
        m_ui.backgroundEstimator->setCurrentText(estimator);
    }
    updateControlsState();
}

double LocalNormalizationSettingsDialog::getBackgroundPercentile() const
{
    return m_ui.backgroundPercentile->value();
}

void LocalNormalizationSettingsDialog::setBackgroundPercentile(double value)
{
    m_ui.backgroundPercentile->setValue(value);
}

double LocalNormalizationSettingsDialog::getSigmaClipK() const
{
    return m_ui.sigmaClipK->value();
}

void LocalNormalizationSettingsDialog::setSigmaClipK(double value)
{
    m_ui.sigmaClipK->setValue(value);
}

int LocalNormalizationSettingsDialog::getSigmaClipIterations() const
{
    return m_ui.sigmaClipIterations->value();
}

void LocalNormalizationSettingsDialog::setSigmaClipIterations(int value)
{
    m_ui.sigmaClipIterations->setValue(value);
}

int LocalNormalizationSettingsDialog::getBackgroundSmoothingRadius() const
{
    return m_ui.backgroundSmoothingRadius->value();
}

void LocalNormalizationSettingsDialog::setBackgroundSmoothingRadius(int value)
{
    m_ui.backgroundSmoothingRadius->setValue(value);
}

// Scale evaluation
QString LocalNormalizationSettingsDialog::getScaleEvaluation() const
{
    return m_ui.scaleEvaluation->currentText();
}

void LocalNormalizationSettingsDialog::setScaleEvaluation(const QString& method)
{
    m_ui.scaleEvaluation->setCurrentText(method);
    updateControlsState();
}

QString LocalNormalizationSettingsDialog::getScaleMode() const
{
    return m_ui.scaleMode->currentText();
}

void LocalNormalizationSettingsDialog::setScaleMode(const QString& mode)
{
    m_ui.scaleMode->setCurrentText(mode);
}

// Clipping
double LocalNormalizationSettingsDialog::getLowClip() const
{
    return m_ui.lowClip->value();
}

void LocalNormalizationSettingsDialog::setLowClip(double value)
{
    m_ui.lowClip->setValue(value);
}

double LocalNormalizationSettingsDialog::getHighClip() const
{
    return m_ui.highClip->value();
}

void LocalNormalizationSettingsDialog::setHighClip(double value)
{
    m_ui.highClip->setValue(value);
}

// Advanced: PSF (simplified)
QString LocalNormalizationSettingsDialog::getPsfType() const
{
    return m_ui.psfType->currentText();
}

void LocalNormalizationSettingsDialog::setPsfType(const QString& type)
{
    // A text the list does not hold leaves the selection as it is.
    m_ui.psfType->setCurrentText(type);
    updateControlsState();
}

double LocalNormalizationSettingsDialog::getMoffatBeta() const
{
    return m_ui.moffatBeta->value();
}

void LocalNormalizationSettingsDialog::setMoffatBeta(double value)
{
    m_ui.moffatBeta->setValue(value);
}

int LocalNormalizationSettingsDialog::getMaxStars() const
{
    return m_ui.maxStars->value();
}

void LocalNormalizationSettingsDialog::setMaxStars(int value)
{
    m_ui.maxStars->setValue(value);
}

double LocalNormalizationSettingsDialog::getMinDetectionSnr() const
{
    return m_ui.minDetectionSnr->value();
}

void LocalNormalizationSettingsDialog::setMinDetectionSnr(double value)
{
    m_ui.minDetectionSnr->setValue(value);
}

bool LocalNormalizationSettingsDialog::getAllowClusteredSources() const
{
    return m_ui.allowClusteredSources->isChecked();
}

void LocalNormalizationSettingsDialog::setAllowClusteredSources(bool value)
{
    m_ui.allowClusteredSources->setChecked(value);
}

// Advanced: Outlier
int LocalNormalizationSettingsDialog::getHotPixelMedianRadius() const
{
    return m_ui.hotPixelMedianRadius->value();
}

void LocalNormalizationSettingsDialog::setHotPixelMedianRadius(int value)
{
    m_ui.hotPixelMedianRadius->setValue(value);
}

int LocalNormalizationSettingsDialog::getNoiseReductionRadius() const
{
    return m_ui.noiseReductionRadius->value();
}

void LocalNormalizationSettingsDialog::setNoiseReductionRadius(int value)
{
    m_ui.noiseReductionRadius->setValue(value);
}

// Advanced: Local scale
bool LocalNormalizationSettingsDialog::getLocalScaleEnabled() const
{
    return m_ui.localScaleEnabled->isChecked();
}

void LocalNormalizationSettingsDialog::setLocalScaleEnabled(bool value)
{
    m_ui.localScaleEnabled->setChecked(value);
    updateControlsState();
}

int LocalNormalizationSettingsDialog::getLocalScaleSmoothingRadius() const
{
    return m_ui.localScaleSmoothingRadius->value();
}

void LocalNormalizationSettingsDialog::setLocalScaleSmoothingRadius(int value)
{
    m_ui.localScaleSmoothingRadius->setValue(value);
}

void LocalNormalizationSettingsDialog::setSettings(const LocalNormalizationSettings& s)
{
    const bool wasUpdatingFromPreset = m_updatingFromPreset;
    m_updatingFromPreset = true;

    setGridSize(s.gridSize);
    setGridInterpolation(s.gridInterpolation);
    setLocalScaleEnabled(s.localScaleEnabled);
    setReferenceStrategy(s.referenceStrategy);
    setMaxIntegratedFrames(s.maxIntegratedFrames);
    setEvaluationCriteria(s.evaluationCriteria);
    setBackgroundEstimator(s.backgroundEstimator);
    setBackgroundPercentile(s.backgroundPercentile);
    setSigmaClipK(s.sigmaClipK);
    setSigmaClipIterations(s.sigmaClipIterations);
    setBackgroundSmoothingRadius(s.backgroundSmoothingRadius);
    setScaleEvaluation(s.scaleEvaluation);
    setScaleMode(s.scaleMode);
    setPsfType(s.psfType);
    setMoffatBeta(s.moffatBeta);
    setMaxStars(s.maxStars);
    setMinDetectionSnr(s.minDetectionSnr);
    setAllowClusteredSources(s.allowClusteredSources);
    setLowClip(s.lowClip);
    setHighClip(s.highClip);
    setHotPixelMedianRadius(s.hotPixelMedianRadius);
    setNoiseReductionRadius(s.noiseReductionRadius);
    setLocalScaleSmoothingRadius(s.localScaleSmoothingRadius);

    m_ui.presetSelector->setCurrentIndex(static_cast<int>(presetForSettings(getSettings())));
    m_updatingFromPreset = wasUpdatingFromPreset;
    updateControlsState();
    updateGridSizeInfo();
}

LocalNormalizationSettings LocalNormalizationSettingsDialog::getSettings() const
{
    LocalNormalizationSettings s;
    s.gridSize = getGridSize();
    s.gridInterpolation = getGridInterpolation();
    s.localScaleEnabled = getLocalScaleEnabled();
    s.referenceStrategy = getReferenceStrategy();
    s.maxIntegratedFrames = getMaxIntegratedFrames();
    s.evaluationCriteria = getEvaluationCriteria();
    s.backgroundEstimator = getBackgroundEstimator();
    s.backgroundPercentile = getBackgroundPercentile();
    s.sigmaClipK = getSigmaClipK();
    s.sigmaClipIterations = getSigmaClipIterations();
    s.backgroundSmoothingRadius = getBackgroundSmoothingRadius();
    s.scaleEvaluation = getScaleEvaluation();
    s.scaleMode = getScaleMode();
    s.psfType = getPsfType();
    s.moffatBeta = getMoffatBeta();
    s.maxStars = getMaxStars();
    s.minDetectionSnr = getMinDetectionSnr();
    s.allowClusteredSources = getAllowClusteredSources();
    s.lowClip = getLowClip();
    s.highClip = getHighClip();
    s.hotPixelMedianRadius = getHotPixelMedianRadius();
    s.noiseReductionRadius = getNoiseReductionRadius();
    s.localScaleSmoothingRadius = getLocalScaleSmoothingRadius();
    return s;
}
