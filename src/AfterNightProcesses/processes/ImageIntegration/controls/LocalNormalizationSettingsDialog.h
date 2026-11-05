// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include "LocalNormalizationSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <verdigris/wobjectdefs.h>

/**
 * @brief Dialog for configuring local normalization settings.
 *
 * This dialog provides a focused interface for all local normalization
 * parameters, organized into logical groups with collapsible advanced options.
 * It includes preset support for quick configuration.
 *
 * Simplified UI layout:
 *   Main section: Preset, Reference Strategy, Grid Size, Interpolation,
 *                 BG Smoothing radius, Scale Method/Mode, Low/High Clip
 *   Advanced:     Evaluation Criteria, Background Estimator + params,
 *                 PSF settings (Auto/Gaussian/Moffat + beta spinbox),
 *                 Outlier rejection, Local Scale (experimental)
 *
 * Removed from UI (hardcoded to robust defaults in the engine mapping):
 *   - backgroundMinRoiDim (always 32), backgroundRoiOverlap (always 0.5)
 *   - backgroundSmoothingEnabled (merged: radius 0 = disabled, ≥1 = enabled)
 *   - scaleSamples (always 8192)
 */
class AFTERNIGHTPROCESSES_API LocalNormalizationSettingsDialog : public QDialog
{
    W_OBJECT(LocalNormalizationSettingsDialog)

public:
    /**
     * @brief Preset configurations for common use cases.
     */
    enum class Preset
    {
        Quick, ///< Fast processing with less local adaptivity (Grid 2)
        Standard, ///< Balanced default (Grid 4)
        Fine, ///< More local adaptivity (Grid 8)
        Custom ///< User-defined settings
    };

    explicit LocalNormalizationSettingsDialog(QWidget* parent = nullptr);
    ~LocalNormalizationSettingsDialog();

    // Preset
    Preset getPreset() const;
    void setPreset(Preset preset);
    [[nodiscard]] static Preset presetForSettings(const LocalNormalizationSettings& settings);

    // Reference settings
    QString getReferenceStrategy() const;
    void setReferenceStrategy(const QString& strategy);

    int getMaxIntegratedFrames() const;
    void setMaxIntegratedFrames(int value);

    QString getEvaluationCriteria() const;
    void setEvaluationCriteria(const QString& criteria);

    // Background model
    int getGridSize() const;
    void setGridSize(int value);
    QString getGridInterpolation() const;
    void setGridInterpolation(const QString& interpolation);
    QString getBackgroundEstimator() const;
    void setBackgroundEstimator(const QString& estimator);
    double getBackgroundPercentile() const;
    void setBackgroundPercentile(double value);
    double getSigmaClipK() const;
    void setSigmaClipK(double value);
    int getSigmaClipIterations() const;
    void setSigmaClipIterations(int value);
    int getBackgroundSmoothingRadius() const;
    void setBackgroundSmoothingRadius(int value);

    // Scale evaluation
    QString getScaleEvaluation() const;
    void setScaleEvaluation(const QString& method);

    QString getScaleMode() const;
    void setScaleMode(const QString& mode);

    // Clipping
    double getLowClip() const;
    void setLowClip(double value);

    double getHighClip() const;
    void setHighClip(double value);

    // Advanced: PSF settings (simplified: Auto / Gaussian / Moffat + beta spinbox)
    QString getPsfType() const;
    void setPsfType(const QString& psfType);

    double getMoffatBeta() const;
    void setMoffatBeta(double value);

    int getMaxStars() const;
    void setMaxStars(int value);

    double getMinDetectionSnr() const;
    void setMinDetectionSnr(double value);

    bool getAllowClusteredSources() const;
    void setAllowClusteredSources(bool value);

    // Advanced: Outlier rejection
    int getHotPixelMedianRadius() const;
    void setHotPixelMedianRadius(int value);

    int getNoiseReductionRadius() const;
    void setNoiseReductionRadius(int value);

    // Advanced: Local scale (experimental)
    bool getLocalScaleEnabled() const;
    void setLocalScaleEnabled(bool value);

    int getLocalScaleSmoothingRadius() const;
    void setLocalScaleSmoothingRadius(int value);

    /// Whether the dialog offers what only the integration's normalization analysis reads: the reference strategy and
    /// its frames, the evaluation that ranks them, the scale evaluation method with its PSF settings, and the analysis
    /// filters run over each frame before it is measured. A stack that fits each frame against its reference frame
    /// reads none of them and hides them, and the dialog says why in the sentence given. They are offered until this
    /// says otherwise.
    void setReferenceAnalysisOffered(bool offered, const QString& reason = QString());

    // Reset to defaults
    void resetToDefaults();

    // Bulk settings transfer
    void setSettings(const LocalNormalizationSettings& settings);
    [[nodiscard]] LocalNormalizationSettings getSettings() const;

private:
    void initialize();
    void populateComboBoxes();
    void connectSignals();
    void updateControlsState();
    void updateGridSizeInfo();
    void applyPreset(Preset preset);
    void checkForCustomPreset();

    void onPresetChanged(int index);
    void onReferenceStrategyChanged(const QString& text);
    void onScaleEvaluationChanged(const QString& text);
    void onBackgroundEstimatorChanged(const QString& text);
    void onPsfTypeChanged(const QString& text);
    void onLocalScaleEnabledChanged(bool checked);
    void onGridSizeChanged(int value);
    void onResetButtonClicked();

    bool m_updatingFromPreset = false;
    bool m_referenceAnalysisOffered = true;
    an::ui::imageintegration::LocalNormalizationSettingsDialogUiElements m_ui;
};
