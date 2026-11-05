// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "FrameId.h"

#include <functional>
#include <optional>
#include <QString>
#include <vector>

// Forward declaration for filter-aware scoring
struct FilterStatistics;

/**
 * @brief Absolute quality metrics for a single frame.
 *
 * These are the raw metrics computed during star profiling, stored as absolute
 * values. For filter-fair comparison, use FilterStatisticsEngine to normalize
 * these to z-scores relative to the filter group baseline.
 *
 * @see FilterStatistics, NormalizedQualityMetrics
 */
struct FrameQualityMetrics
{
    int stars = 0; ///< Number of detected stars
    double fwhm = 0.0; ///< Full Width at Half Maximum (pixels)
    double snr = 0.0; ///< Median star Signal-to-Noise Ratio
    double eccentricity = 0.0; ///< Star elongation [0=round, 1=elongated]
    double photometric_zero_point = 0.0; ///< Transparency proxy; higher is better
    double zero_point_scatter = 0.0; ///< Zero-point fit scatter / uncertainty
    double astrometric_rms_pixels =
        0.0; ///< Astrometric registration residual in pixels for photometric weighting parity
    double astrometric_rms_arcsec = 0.0; ///< Astrometric registration residual; lower is better
    double limiting_magnitude = 0.0; ///< Faintest reliable catalog/star depth proxy
    int catalog_matched_stars = 0; ///< Catalog matches contributing to the solution
    bool succeeded = true; ///< Whether registration succeeded
};

// This struct is threaded through the whole registration UI by value, so two objects that
// disagree about its size read every field after the first at the wrong offset - wrong numbers,
// or an access violation with no usable stack. That is exactly what an incremental build
// produces when it leaves one object compiled against an older revision of this header, and it
// cost this project a fabricated "never grow FrameQualityMetrics" rule before it was diagnosed
// (docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md). The tag below makes the linker reject
// such a mix with LNK2038 instead, and the assertion makes the tag impossible to forget: change
// the struct and it fires until both numbers are updated, which is what changes the tag.
#define AN_FRAME_QUALITY_METRICS_SIZE 80
#define AN_FRAME_QUALITY_METRICS_ALIGN 8

static_assert(sizeof(FrameQualityMetrics) == AN_FRAME_QUALITY_METRICS_SIZE &&
                  alignof(FrameQualityMetrics) == AN_FRAME_QUALITY_METRICS_ALIGN,
              "FrameQualityMetrics changed size: update AN_FRAME_QUALITY_METRICS_SIZE/ALIGN so "
              "objects built against the old layout still fail to link.");

#if defined(_MSC_VER)
#define AN_FQ_STR_(x) #x
#define AN_FQ_STR(x) AN_FQ_STR_(x)
#define AN_FQ_LAYOUT_TAG AN_FQ_STR(AN_FRAME_QUALITY_METRICS_SIZE) "/" AN_FQ_STR(AN_FRAME_QUALITY_METRICS_ALIGN)
#pragma detect_mismatch("FrameQualityMetricsLayout", AN_FQ_LAYOUT_TAG)
#undef AN_FQ_LAYOUT_TAG
#undef AN_FQ_STR
#undef AN_FQ_STR_
#endif

/**
 * @brief Frame-level noise statistics measured during star detection.
 *
 * A separate type from FrameQualityMetrics because it has a different audience: these values are
 * consumed only by the persistence and stacking-weight paths, while FrameQualityMetrics is what
 * the registration UI displays, sorts and scores.
 *
 * It was also split off because growing FrameQualityMetrics appeared to crash the registration
 * tab. That was a stale object file, not a defect in either struct, and adding fields to either
 * one is an ordinary change today - see docs/dev/FRAME_WEIGHTING_INPUTS.md.
 *
 * They are the only registration metrics that respond to transparency, and the sole input to noise
 * weighting: w = 1 / (scale^2 * noise^2), with scale = scale_reference / scale_frame.
 */
struct FrameNoiseStatistics
{
    double background_noise = 0.0; ///< Background noise sigma of the frame
    double global_mad = 0.0; ///< Median absolute deviation; normalization-scale fallback
    double global_scale = 0.0; ///< IKSS scale estimator; preferred normalization scale

    [[nodiscard]] bool hasBackgroundNoise() const noexcept
    {
        return background_noise > 0.0;
    }

    /// True when the values are sufficient for noise weighting (it needs a scale as well, and
    /// falls back from IKSS to MAD).
    [[nodiscard]] bool isUsableForWeighting() const noexcept
    {
        return hasBackgroundNoise() && (global_scale > 0.0 || global_mad > 0.0);
    }
};

/**
 * @brief Compute quality score from absolute metrics.
 *
 * This function uses absolute metric values for scoring. For filter-fair
 * comparison across different filter types, use the overload that accepts
 * FilterStatistics, or use FilterStatisticsEngine::computeQualityScore().
 *
 * @note FWHM varies by filter (narrowband typically larger than broadband).
 *       This score is best for comparing frames of the SAME filter type.
 *
 * @param metrics The frame's quality metrics.
 * @return Quality score (higher = better).
 *
 * @see FilterStatisticsEngine::computeQualityScore()
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API double frameQualityScore(const FrameQualityMetrics& metrics);

/**
 * @brief Compute quality score with filter-relative normalization.
 *
 * When filter statistics are provided, metrics are converted to z-scores
 * relative to the filter group, enabling fair comparison across filters.
 *
 * @param metrics The frame's quality metrics.
 * @param filterStats Optional baseline statistics for the frame's filter.
 * @return Quality score (higher = better).
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API double frameQualityScore(const FrameQualityMetrics& metrics,
                                                               const std::optional<FilterStatistics>& filterStats);

[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<FrameId>
selectBestReferenceFrame(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates);

enum class AutoReferenceMode
{
    AbsoluteAll,
    AbsoluteSingleFilter,
    FilterNormalizedAcrossFilters
};

struct AutoReferenceSelection
{
    AutoReferenceMode mode = AutoReferenceMode::AbsoluteAll;
    QString filterName; // Used when mode == AbsoluteSingleFilter
};

struct AutoReferenceDecision
{
    std::optional<FrameId> best;
    AutoReferenceMode modeUsed = AutoReferenceMode::AbsoluteAll;
    int namedFilterCount = 0;
    int excludedUnknownCount = 0;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<FrameId>
selectBestReferenceFrameAuto(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates,
                             const std::function<QString(const FrameId&)>& getFilter,
                             const AutoReferenceSelection& selection);

/**
 * @brief Automatically choose the best reference frame using a sensible default strategy.
 *
 * If the candidate set contains multiple named filters, uses filter-normalized scoring across filters
 * (and excludes unknown-filter frames from that normalized pool). Otherwise, falls back to absolute scoring.
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API AutoReferenceDecision
selectBestReferenceFrameAutoSmart(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates,
                                  const std::function<QString(const FrameId&)>& getFilter);

/**
 * @brief Detects outlier frames based on quality scores.
 *
 * Uses sigma clipping on the calculated quality scores to identify frames
 * that deviate significantly from the median/mean.
 *
 * @param candidates List of frames and their metrics.
 * @param sigmaLow Sigma threshold for lower bound (scores below mean - sigma * stddev).
 * @return List of FrameIds identified as outliers.
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<FrameId>
detectOutliers(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates, double sigmaLow = 3.0);

/**
 * @brief Rank frames by quality score (best to worst) for normalization reference selection.
 *
 * Uses filter-relative scoring when filter statistics are available within the provided set.
 * Returns an empty vector if no metrics are available for any frame.
 */
[[nodiscard]] std::vector<FrameId>
rankFramesByQualityScore(const std::vector<FrameId>& frameIds,
                         const std::function<std::optional<FrameQualityMetrics>(const FrameId&)>& getMetrics,
                         const std::function<QString(const FrameId&)>& getFilter);
