// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings records the integration workflow and the standalone workflow tools share: how a group is registered,
/// normalized and stacked and how its lights are calibrated, with the enumerators sessions, presets and manifests store
/// as their values. A standalone tool reads and writes these records through this header and reaches nothing of the
/// integration's model, its windows or its stages.
#pragma once

#include <QMap>
#include <QPointF>
#include <QString>
#include <QVector>

#include <array>
#include <cstddef>
#include <optional>

// Sessions, presets and manifests store the enumerators below as their values. An enumerator keeps the value it
// states whatever the declaration order, and a value no enumerator names any more is never given to another.

/// Normalization mode for V2 workflow
enum class NormalizationMode
{
    None = 0,
    Global = 1,
    Local = 2
};

/// Global normalization method (mirrors an::normalization_method)
enum class GlobalNormalizationMethod
{
    Additive = 0,
    Multiplicative = 1,
    AdditiveScaling = 2,
    MultiplicativeScaling = 3
};

/// Scale estimator method (mirrors an::scale_estimator_method)
enum class ScaleEstimatorMethod
{
    MAD = 0,
    IKSS = 1
};

/// Reference selection mode for normalization (global/local)
enum class NormalizationReferenceMode
{
    Automatic = 0, ///< Select best-quality frame automatically
    Manual = 1 ///< Use user-selected reference frame
};

/// Every value a session, preset or manifest may store for each persisted enumerator.
inline constexpr std::array kPersistedNormalizationModes = {NormalizationMode::None, NormalizationMode::Global,
                                                            NormalizationMode::Local};
inline constexpr std::array kPersistedGlobalNormalizationMethods = {
    GlobalNormalizationMethod::Additive, GlobalNormalizationMethod::Multiplicative,
    GlobalNormalizationMethod::AdditiveScaling, GlobalNormalizationMethod::MultiplicativeScaling};
inline constexpr std::array kPersistedScaleEstimatorMethods = {ScaleEstimatorMethod::MAD, ScaleEstimatorMethod::IKSS};
inline constexpr std::array kPersistedNormalizationReferenceModes = {NormalizationReferenceMode::Automatic,
                                                                     NormalizationReferenceMode::Manual};

/// The enumerator a stored value names, or the fallback when it names none of the known ones - a value another build
/// wrote, or a file edited by hand - so a read never produces a value outside the enumeration.
template <typename Enum, std::size_t Count>
[[nodiscard]] constexpr Enum persistedEnumerator(const int stored, const std::array<Enum, Count>& known,
                                                 const Enum fallback)
{
    for (const Enum candidate : known)
    {
        if (static_cast<int>(candidate) == stored)
        {
            return candidate;
        }
    }
    return fallback;
}

/// Cache mode for light frame calibration
enum class LightCalibrationCacheMode
{
    Disk, ///< Write calibrated frames to disk
    InMemory ///< Calibrate on-the-fly in memory (default)
};

struct LocalNormalizationSettings
{
    bool enabled = false;
    int gridSize = 4;
    QString gridInterpolation = QStringLiteral("Bicubic");
    bool localScaleEnabled = false;
    QString referenceStrategy = QStringLiteral("Integrate Best Frames");
    int maxIntegratedFrames = 16;
    QString evaluationCriteria = QStringLiteral("PSF Signal Weight");
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
    int localScaleSmoothingRadius = 1;
};

struct OverlapNormalizationSettings
{
    bool enabled = false;
    int minOverlapPixels = 1000;
    double minOverlapFraction = 0.01;
};

struct LocalWeightingSettings
{
    bool enabled = false;
    int windowSize = 5;
    int gridSpacing = 64;
    double snrWeightExponent = 2.0;
};

struct DrizzleSettings
{
    bool enabled = false;
    double scale = 2.0;
    double pixelFraction = 0.9;
    QString kernel = QStringLiteral("Square");
    bool cfaAware = false;
    QString cfaPattern = QStringLiteral("RGGB");
};

struct HdrExposureMergeSettings
{
    bool enabled = false;
};

enum class RegisteredOutputPolicy : unsigned char
{
    Retained = 0,
    Discardable = 1
};

struct StackingSettings
{
    // Stacking method settings
    QString stackMethod = QStringLiteral("Average");
    QString rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
    // Provenance-only label when an automatic recommendation resolves to rejectionMethod.
    QString requestedRejectionMethod;
    double sigmaLow = 3.0;
    double sigmaHigh = 3.0;
    QString weighting = QStringLiteral("None");
    bool createQualityMaps = false;
    bool createRejectionMaps = false;
    bool mergeRejectionMaps = false;
    bool spatialRejectionClustering = false;
    bool restackExcludingFlaggedFrames = false;
    QString starCountScaling = QStringLiteral("Quadratic");
    double exponentialAlpha = 2.0;
    double minWeightFloor = 0.01;
    LocalWeightingSettings localWeighting;

    // Memory settings
    bool limitMemory = false;
    int maxMemoryMb = 2048;

    // Normalization settings
    NormalizationMode normalizationMode = NormalizationMode::Global;
    GlobalNormalizationMethod globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
    ScaleEstimatorMethod scaleEstimator = ScaleEstimatorMethod::IKSS;
    OverlapNormalizationSettings overlapNormalization;
    NormalizationReferenceMode normalizationReferenceMode = NormalizationReferenceMode::Automatic;
    QString manualNormalizationReference; // canonical path (manual mode)
    LocalNormalizationSettings localNormalization;

    // Drizzle settings (lights integration only).
    DrizzleSettings drizzle;

    // Comet-aware output selection. Production comet modes remain gated until the replacement tracker is complete.
    QString cometStackingMode = QStringLiteral("Stars");

    // Final-master WCS recovery (lights integration only).
    bool solveFinalMasterIfNeeded = false;

    // Registered images are retained by default, so a resumed or recovered run finds them.
    RegisteredOutputPolicy registeredOutputPolicy = RegisteredOutputPolicy::Retained;

    // HDR multi-exposure merge settings (lights integration only).
    HdrExposureMergeSettings hdr;
};

struct RegistrationSettings
{
    // Incremented when settings change; used as a stable settings-version identifier.
    int version = 0;

    int minStarPairs = 10;
    int maxStarPairs = 2000;

    // When enabled, Registration chooses a reference from detected/cached star metrics
    // before computing transforms, then the UI mirrors that selected reference.
    bool autoReferenceDuringRegistration = true;

    // When enabled, Registration may invoke the configured plate solver for
    // frames that do not already provide usable astrometric metadata.
    bool autoSolveUnsolvedFrames = false;

    // When enabled, post-registration catalog enrichment populates photometric
    // zero point, limiting magnitude, and catalog-matched star count using the
    // Gaia catalog.  Requires plate-solved frames with valid WCS metadata.
    bool enrichWithGaiaCatalog = true;

    // Empty means auto-select.
    QString referenceImage;

    QString registrationMethod = QStringLiteral("star_matching");
    QString transformation = QStringLiteral("Homography");
    // The mode decides the contract: "linear" fits the transformation alone, "polynomial" adds the stated order and
    // "Auto" the order the matches support. A read keeps an order only for a polynomial mode and sets two-pass
    // refinement exactly when the mode refines, so the three fields cannot say two things.
    QString transformMode = QStringLiteral("Auto");
    int transformOrder = 0;
    bool twoPassRefinementEnabled = true;
    QString alignmentMode = QStringLiteral("standard");
    QString interpolation = QStringLiteral("Auto");

    struct ManualControlPoint
    {
        QPointF sourcePoint;
        QPointF referencePoint;

        [[nodiscard]] bool operator==(const ManualControlPoint& other) const = default;
    };

    enum class CometObservationOrigin
    {
        Manual,
        AcceptedAutomatic,
        Imported
    };

    struct CometTrackingObservation
    {
        QString frameId;
        QPointF sourcePosition;
        CometObservationOrigin origin = CometObservationOrigin::Manual;
        bool enabled = true;
        double apertureRadiusPixels = 0.0;
        QString note;

        [[nodiscard]] bool operator==(const CometTrackingObservation& other) const = default;
    };

    struct CometTrackingOptions
    {
        double searchRadiusPixels = 32.0;
        double minimumSnr = 3.0;
        int maxBootstrapFrames = 8;

        [[nodiscard]] bool operator==(const CometTrackingOptions& other) const = default;
    };

    struct CometFitOptions
    {
        QString model = QStringLiteral("linear");
        bool robustRejection = true;
        double outlierSigma = 3.0;

        [[nodiscard]] bool operator==(const CometFitOptions& other) const = default;
    };

    struct CometTrackingSettings
    {
        int schemaVersion = 1;
        QVector<CometTrackingObservation> observations;
        CometTrackingOptions trackingOptions;
        CometFitOptions fitOptions;
        QString outputMode = QStringLiteral("Comet");

        [[nodiscard]] bool operator==(const CometTrackingSettings& other) const = default;
    };

    struct ChromaticAberrationSettings
    {
        bool enabled = false;
        double edgeFraction = 0.55;
        QString transformType = QStringLiteral("Similarity");
        int minMatchedStars = 6;
        QString interpolation = QStringLiteral("Lanczos4");

        [[nodiscard]] bool operator==(const ChromaticAberrationSettings& other) const = default;
    };

    using ManualControlPointList = QVector<ManualControlPoint>;
    using ManualControlPointMap = QMap<QString, ManualControlPointList>;

    // Manual-registration control points keyed by non-reference frame canonical path.
    ManualControlPointMap manualControlPointsByFrame;
    std::optional<CometTrackingSettings> cometTracking;
    ChromaticAberrationSettings chromaticAberration;

    // Advanced - Star detection
    int radius = 5;
    double threshold = 1.0;
    bool amplitudeRangeEnabled = false;
    double thresholdMinimum = 0.0;
    double thresholdMaximum = 1.0;
    double roundnessThreshold = 0.5;
    bool roundnessRangeEnabled = false;
    double roundnessThresholdMaximum = 1.0;
    int convergence = 1;
    bool relaxPsfChecks = false;
    QString profileType = QStringLiteral("Gaussian");
    double minimumBeta = 1.5;

    // Advanced - Misc
    QString cfaPattern = QStringLiteral("Auto");
    QString debayerMethod = QStringLiteral("default");
};

struct LightCalibrationSettings
{
    bool cosmeticCorrection = true;
    double sigmaCold = 3.0;
    double sigmaHot = 3.0;
    bool darkOptimization = false;
    double darkMultiplicationFactor = 1.0;
    bool flatAutoLevel = true;
    double pedestal = 0.0;

    /// When true, the light frames in this group are already calibrated externally.
    /// No calibration masters (dark, bias, flat) will be applied, no cosmetic correction,
    /// dark optimization, flat normalization, or pedestal will be used. No calibration
    /// warnings will be shown, and "Save calibrated frames to disk" will skip this group.
    bool preCalibrated = false;
};
