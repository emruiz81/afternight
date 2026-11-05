// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <QStringList>

#include <optional>

#include "StackingSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <verdigris/wobjectdefs.h>

class LocalNormalizationSettingsDialog;

/**
 * @brief Summary of sub-pixel dithering quality from registration transforms.
 *
 * Computed by analyzing the fractional-pixel components of the translation
 * offsets from frame registration transforms.  Passed to the dialog so that
 * the recommendation engine can assess whether drizzle will actually improve
 * resolution for this stacking group.
 */
struct DrizzleAnalysis
{
    bool available = false; ///< True if transform data was loaded successfully
    int framesWithMetrics = 0; ///< Number of frames that have registration metric rows
    int framesWithTransforms = 0; ///< Number of frames that have usable registration transforms
    int framesWithUnsupportedTransforms = 0; ///< Number of frames with transforms unsupported by analysis
    int totalFrames = 0; ///< Total frames in the stacking group
    double subPixelCoverage = 0.0; ///< Uniformity of fractional offsets in [0,1], higher = better dithering
    double meanFractionalOffset = 0.0; ///< Mean absolute fractional pixel offset
    int imageMegapixels = 0; ///< Approximate image resolution in megapixels
    double medianFwhmPx = 0.0; ///< Median star FWHM in pixels across registered frames (0 = unknown)
};

/**
 * @brief Compute the recommended pixel fraction (drop size) for drizzle integration.
 *
 * Uses sub-pixel dither coverage, frame count, drizzle scale factor, and
 * (optionally) seeing / FWHM to derive the smallest pixel fraction that the
 * data actually supports.  A smaller pixfrac increases resolution but
 * amplifies noise and requires better dithering.
 *
 * @param analysis         Pre-computed drizzle viability analysis
 * @param scale            Drizzle scale factor (e.g. 2.0 for 2× upsampling)
 * @param medianFwhmPx     Median star FWHM in pixels (0 = skip seeing limit)
 * @return Optimal pixfrac in [0.1, 1.0]; 1.0 means drizzle adds no resolution
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API double computeOptimalPixfrac(const DrizzleAnalysis& analysis, double scale,
                                                                   double medianFwhmPx = 0.0);

/**
 * @brief Recommended stacking settings based on frame type and count.
 *
 * Computed by getRecommendedSettings() using astrophotography best practices.
 * The recommendation considers:
 * - Frame type (BIAS/DARK need no normalization, FLAT needs multiplicative, etc.)
 * - Frame count (sigma-based rejection needs 8+ frames for reliable statistics)
 * - Sigma thresholds (more conservative with fewer frames)
 * - Sub-pixel dithering analysis for drizzle recommendations (LIGHT frames)
 */
struct StackingRecommendation
{
    QString stackMethod;
    QString normalizationMode;
    QString rejectionMethod;
    double sigmaLow = 3.0;
    double sigmaHigh = 3.0;
    QString weightingMethod; ///< Weighting method (LIGHT frames only; empty = no recommendation)
    bool localNormalization = false; ///< Whether local normalization is recommended

    // Drizzle recommendation (LIGHT frames only)
    enum class DrizzleVerdict
    {
        NotApplicable,
        NotRecommended,
        Possible,
        Recommended
    };
    DrizzleVerdict drizzleVerdict = DrizzleVerdict::NotApplicable;
    double drizzleScale = 2.0; ///< Recommended scale factor when drizzle is suggested
    double drizzlePixfrac = 0.9; ///< Recommended pixel fraction (drop size) for drizzle
    QString drizzleReason; ///< Human-readable drizzle explanation

    QString reason; ///< Human-readable explanation of the core recommendation

    // Secondary/Alternative suggestion
    struct Alternative
    {
        bool available = false;
        QString label; // e.g. "Aggressive Rejection"
        QString rejectionMethod;
        double sigmaLow = 3.0;
        double sigmaHigh = 3.0;
        bool localNormalization = false;
        QString reason;
    } secondary;
};

class AFTERNIGHTPROCESSES_API StackingSettingsDialog : public QDialog
{
    W_OBJECT(StackingSettingsDialog)

public:
    enum class FrameType
    {
        BIAS,
        DARK,
        FLAT,
        LIGHT
    };

    explicit StackingSettingsDialog(FrameType frameType, const QString& groupLabel, QWidget* parent = nullptr);
    ~StackingSettingsDialog();

    /**
     * @brief Set the frame count for this group, enabling intelligent recommendations.
     *
     * When set (count > 0), the "Recommended Settings" button in the dialog becomes
     * active. Clicking it shows a popup explaining the optimal algorithm for the given
     * frame type and count, and applies the settings if the user accepts.
     *
     * @param count Number of frames in the current group (0 = unknown/disable button)
     */
    void setFrameCount(int count);

    /**
     * @brief Provide pre-computed drizzle dithering analysis for this stacking group.
     *
     * When set with available=true, the recommendation popup includes a drizzle
     * section that assesses whether the sub-pixel offset distribution supports
     * resolution improvement via drizzle integration.
     *
     * @param analysis Pre-computed drizzle viability analysis
     */
    void setDrizzleAnalysis(const DrizzleAnalysis& analysis);

    /**
     * @brief Compute recommended stacking settings for a given frame type and count.
     *
     * Uses astrophotography best practices to select optimal algorithms:
     * - 2-7 frames: No rejection (insufficient statistics for sigma-based methods)
     * - 8-14 frames: Sigma Clipping (basic sigma estimates become reliable)
     * - 15-24 frames: Winsorized Sigma (robust outlier replacement)
     * - 25-39 frames: Linear Fit (excellent for satellite/plane trail removal)
     * - 40+ frames: GESDT (optimal with large datasets)
     *
     * @param frameType The type of calibration/integration frame
     * @param frameCount Number of frames in the group
     * @return StackingRecommendation with method, rejection, normalization, and explanation
     */
    [[nodiscard]] static StackingRecommendation getRecommendedSettings(FrameType frameType, int frameCount,
                                                                       const DrizzleAnalysis& drizzleAnalysis = {});

    /**
     * @brief Collect current settings from the dialog's widgets.
     */
    [[nodiscard]] StackingSettings getCurrentSettings() const;

    // Getters for the selected values
    QString getStackMethod() const;
    QString getNormalizationMode() const;
    QString getRejectionMethod() const;
    double getSigmaLow() const;
    double getSigmaHigh() const;
    QString getWeighting() const;
    bool getCreateRejectionMaps() const;
    bool getMergeRejectionMaps() const;
    bool getSpatialRejectionClustering() const;
    bool getRestackExcludingFlaggedFrames() const;
    bool getCreateQualityMaps() const;
    RegisteredOutputPolicy getRegisteredOutputPolicy() const;

    // Weighting parameter getters (LIGHT frames only)
    QString getStarCountScaling() const;
    double getExponentialAlpha() const;
    double getMinWeightFloor() const;
    /// Always false: the registration measures one star width per frame, so no channel can be weighed by its own.
    bool getPerChannelFwhmWeighting() const;
    bool getLocalWeightingEnabled() const;
    int getLocalWeightingWindowSize() const;
    int getLocalWeightingGridSpacing() const;
    double getLocalWeightingSnrWeightExponent() const;

    // Drizzle parameter getters (LIGHT frames only)
    bool getDrizzleEnabled() const;
    double getDrizzleScale() const;
    QString getDrizzleKernel() const;
    double getDrizzlePixfrac() const;
    bool getSolveFinalMasterIfNeeded() const;
    bool getHdrExposureMergeEnabled() const;

    // Global normalization reference getters (LIGHT frames only)
    QString getNormalizationReferenceMode() const;
    QString getManualNormalizationReference() const;

    // Global normalization option getters: the scale estimator for every frame type, overlap normalization for lights
    ScaleEstimatorMethod getScaleEstimator() const;
    bool getOverlapNormalizationEnabled() const;
    int getOverlapMinimumPixels() const;
    double getOverlapMinimumFraction() const;

    // Local normalization parameter getters (LIGHT frames only)
    bool getLocalNormalizationEnabled() const;
    QString getLocalNormalizationReferenceStrategy() const;
    int getLocalNormalizationMaxIntegratedFrames() const;
    QString getLocalNormalizationEvaluationCriteria() const;
    int getLocalNormalizationGridSize() const;
    QString getLocalNormalizationScaleEvaluation() const;
    QString getLocalNormalizationScaleMode() const;
    QString getLocalNormalizationGridInterpolation() const;
    QString getLocalNormalizationBackgroundEstimator() const;
    double getLocalNormalizationBackgroundPercentile() const;
    double getLocalNormalizationSigmaClipK() const;
    int getLocalNormalizationSigmaClipIterations() const;
    int getLocalNormalizationBackgroundSmoothingRadius() const;
    QString getLocalNormalizationPsfType() const;
    double getLocalNormalizationMoffatBeta() const;
    int getLocalNormalizationMaxStars() const;
    double getLocalNormalizationMinDetectionSnr() const;
    bool getLocalNormalizationAllowClusteredSources() const;
    double getLocalNormalizationLowClip() const;
    double getLocalNormalizationHighClip() const;
    int getLocalNormalizationHotPixelMedianRadius() const;
    int getLocalNormalizationNoiseReductionRadius() const;
    bool getLocalNormalizationLocalScaleEnabled() const;
    int getLocalNormalizationLocalScaleSmoothingRadius() const;

    // Setters for the values
    void setStackMethod(const QString& method);
    void setNormalizationMode(const QString& mode);
    void setRejectionMethod(const QString& method);
    void setSigmaLow(double value);
    void setSigmaHigh(double value);
    void setWeighting(const QString& weighting);
    void setPhotometricWeightingAvailable(bool available);
    void setCreateRejectionMaps(bool value);
    void setMergeRejectionMaps(bool value);
    void setSpatialRejectionClustering(bool value);
    void setRestackExcludingFlaggedFrames(bool value);
    void setCreateQualityMaps(bool value);
    void setRegisteredOutputPolicy(RegisteredOutputPolicy policy);

    // Weighting parameter setters (LIGHT frames only)
    void setStarCountScaling(const QString& scaling);
    void setExponentialAlpha(double value);
    void setMinWeightFloor(double value);
    /// Leaves the control off whatever is stored, as `getPerChannelFwhmWeighting` says, so accepting the dialog lets
    /// a stored choice go.
    void setPerChannelFwhmWeighting(bool value);
    void setLocalWeightingEnabled(bool value);
    void setLocalWeightingWindowSize(int value);
    void setLocalWeightingGridSpacing(int value);
    void setLocalWeightingSnrWeightExponent(double value);

    // Drizzle parameter setters (LIGHT frames only)
    void setDrizzleEnabled(bool enabled);
    void setDrizzleScale(double scale);
    void setDrizzleKernel(const QString& kernel);
    void setDrizzlePixfrac(double value);
    void setSolveFinalMasterIfNeeded(bool enabled);
    void setHdrExposureMergeEnabled(bool enabled);
    void setCometStackingMode(const QString& mode);

    // Global normalization reference setters (LIGHT frames only)
    void setNormalizationReferenceMode(const QString& mode);
    void setManualNormalizationReference(const QString& path);
    void setNormalizationReferenceCandidates(const QStringList& candidates);

    // Global normalization option setters: the scale estimator for every frame type, overlap normalization for lights
    void setScaleEstimator(ScaleEstimatorMethod method);
    void setOverlapNormalizationEnabled(bool enabled);
    void setOverlapMinimumPixels(int value);
    void setOverlapMinimumFraction(double value);

    // Local normalization parameter setters (LIGHT frames only)
    void setLocalNormalizationEnabled(bool enabled);
    void setLocalNormalizationReferenceStrategy(const QString& strategy);
    void setLocalNormalizationMaxIntegratedFrames(int value);
    void setLocalNormalizationEvaluationCriteria(const QString& criteria);
    void setLocalNormalizationGridSize(int value);
    void setLocalNormalizationScaleEvaluation(const QString& method);
    void setLocalNormalizationScaleMode(const QString& mode);
    void setLocalNormalizationGridInterpolation(const QString& interpolation);
    void setLocalNormalizationBackgroundEstimator(const QString& estimator);
    void setLocalNormalizationBackgroundPercentile(double value);
    void setLocalNormalizationSigmaClipK(double value);
    void setLocalNormalizationSigmaClipIterations(int value);
    void setLocalNormalizationBackgroundSmoothingRadius(int value);
    void setLocalNormalizationPsfType(const QString& psfType);
    void setLocalNormalizationMoffatBeta(double value);
    void setLocalNormalizationMaxStars(int value);
    void setLocalNormalizationMinDetectionSnr(double value);
    void setLocalNormalizationAllowClusteredSources(bool value);
    void setLocalNormalizationLowClip(double value);
    void setLocalNormalizationHighClip(double value);
    void setLocalNormalizationHotPixelMedianRadius(int value);
    void setLocalNormalizationNoiseReductionRadius(int value);
    void setLocalNormalizationLocalScaleEnabled(bool value);
    void setLocalNormalizationLocalScaleSmoothingRadius(int value);

    // Reset to default values
    void resetToDefaults();

    // Get frame type
    FrameType getFrameType() const
    {
        return m_frameType;
    }

    // Signals
void applyToAllRequested() W_SIGNAL(applyToAllRequested) void bestNormalizationReferenceRequested()
    W_SIGNAL(bestNormalizationReferenceRequested) void automaticNormalizationReferenceDisplayRequested()
        W_SIGNAL(automaticNormalizationReferenceDisplayRequested)

            private : void initialize();
    void applyTooltips();
    void ensureWeightingOptionsDialog();
    void openWeightingOptionsDialog();
    [[nodiscard]] bool isWeightingOptionsAvailable() const;
    void updateRejectionControlsState();
    void updateRejectionLabels();
    void updateWeightingControlsState();
    void updateHdrControlsState();
    void updateDrizzleInfoLabel();
    void updateLocalNormalizationControlsState();
    void updateNormalizationReferenceControlsState();
    /// Offers the scale estimator where the normalization that runs measures a scale, and overlap normalization and its
    /// floors under global normalization.
    void updateNormalizationOptionsState();
    /// Shows, in the Normalization choice, the global match local normalization runs while it is on, keeping the method
    /// the group stores for when it is turned off.
    void showGlobalMethodSetByLocalNormalization(bool shown);
    [[nodiscard]] bool globalMethodSetByLocalNormalizationShown() const;
    [[nodiscard]] LocalNormalizationSettings currentLocalNormalizationSettings() const;
    void applyLocalNormalizationSettings(const LocalNormalizationSettings& settings);
    void openLocalNormalizationDialog();
    void on_resetButton_clicked();
    void on_applyToAllButton_clicked();
    void showRecommendedSettingsPopup();
    /// Sets the dialog's controls to a recommendation the person chose to apply.
    void applyRecommendation(const StackingRecommendation& rec);
    void refreshPhotometricWeightingItem();
    [[nodiscard]] int photometricWeightingIndex() const;

    FrameType m_frameType;
    QString m_groupLabel;
    int m_frameCount = 0;
    DrizzleAnalysis m_drizzleAnalysis;
    QString m_cometStackingMode = QStringLiteral("Stars");
    /// The global method the group stores, kept while local normalization sets the one that runs.
    QString m_globalNormalizationChoice;

    struct WeightingOptionsSnapshot
    {
        QString starCountScaling = QStringLiteral("Quadratic");
        double exponentialAlpha = 2.0;
        double minWeightFloor = 0.01;
        bool perChannelFwhmWeighting = false;
        bool localWeightingEnabled = false;
        int localWeightingWindowSize = 5;
        int localWeightingGridSpacing = 64;
        double localWeightingSnrWeightExponent = 2.0;
    };

    // Local normalization settings (stored separately since they're in a sub-dialog)
    struct LocalNormSettings
    {
        QString referenceStrategy = QStringLiteral("Integrate Best Frames");
        int maxIntegratedFrames = 16;
        QString evaluationCriteria = QStringLiteral("PSF Signal Weight");
        int gridSize = 4;
        QString gridInterpolation = QStringLiteral("Bicubic");
        QString backgroundEstimator = QStringLiteral("Sigma Clipped Median");
        double backgroundPercentile = 0.25;
        double sigmaClipK = 3.0;
        int sigmaClipIterations = 3;
        int backgroundSmoothingRadius = 1;
        QString scaleEvaluation = QStringLiteral("PSF Flux");
        QString scaleMode = QStringLiteral("Linked RGB");
        QString psfType = QStringLiteral("Auto");
        double moffatBeta = 4.0;
        int maxStars = 24576;
        double minDetectionSnr = 40.0;
        bool allowClusteredSources = true;
        double lowClip = 4.5e-05;
        double highClip = 0.85;
        int hotPixelMedianRadius = 0;
        int noiseReductionRadius = 0;
        bool localScaleEnabled = false;
        int localScaleSmoothingRadius = 1;
        int preset = 1; // Standard
    } m_localNormSettings;
    bool m_photometricWeightingAvailable = true;
    QDialog* m_weightingOptionsDialog = nullptr;
    std::optional<WeightingOptionsSnapshot> m_weightingOptionsSnapshot;

private:
    void on_stackMethodSelector_currentTextChanged(const QString& text);
    void on_rejectionMethodSelector_currentTextChanged(const QString& text);
    void on_weightingSelector_currentTextChanged(const QString& text);
    void on_starCountScaling_currentTextChanged(const QString& text);
    void on_hdrExposureMergeEnabled_toggled(bool checked);
    an::ui::imageintegration::StackingSettingsDialogUiElements m_ui;
};
