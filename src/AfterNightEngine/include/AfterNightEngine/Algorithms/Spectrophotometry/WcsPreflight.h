// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whether a frame's world-coordinate solution is good enough to place
/// apertures by, decided by re-matching detected stars against the spectrum catalog.
///
/// **The solution is typed and the projection is the astrometry domain's.** A `WcsSolution` projects
/// without a lock and reports its own distortion; the preflight asks it rather than re-deriving the
/// answer from cards.
///
/// **A quality report and its verdict are two things.** `WcsQualityReport` is what was measured;
/// `checkWcsQuality` is the rule it is held to, returning the first reason it fails so a caller can
/// say which one rather than parsing a sentence.
///
/// **Sources come from the star domain.** `makePreflightSources` turns detected stars into the
/// positions, widths and signal-to-noise figures a re-match reads, under a stated selection. There
/// is one detector, and its relaxed mode is the stated way to ask it for more.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most detected sources one preflight matches.
inline constexpr std::uint32_t kMaxPreflightSources = 100000u;

/// The default ceiling on sources a selection keeps.
inline constexpr std::uint32_t kDefaultMaxPreflightSources = 4096u;

/// Where a quality report's evidence came from.
enum class WcsQualitySource : std::uint8_t
{
    /// Nothing measured it.
    None = 0u,

    /// The residuals a plate solver recorded when it produced the solution.
    PersistedSolverResiduals = 1u,

    /// The residuals an integration's registration recorded.
    ImageIntegrationRegistration = 2u,

    /// A re-match of detected stars against the catalog, made here.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    PreflightRematch = 3u
};

/// Whether a value names a quality source this build reads.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param source the value to inspect.
/// @return true for one of the four declared values.
[[nodiscard]] constexpr bool isWcsQualitySource(WcsQualitySource source) noexcept
{
    return static_cast<std::uint8_t>(source) <= static_cast<std::uint8_t>(WcsQualitySource::PreflightRematch);
}

/// Which distortion model a solution carries.
enum class WcsDistortionModel : std::uint8_t
{
    /// No solution was examined.
    None = 0u,

    /// A tangent plane with no polynomial.
    Tangent = 1u,

    /// A tangent plane with a polynomial distortion.
    TangentSip = 2u,

    /// A model this build does not project.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Unsupported = 3u
};

/// Whether a value names a distortion model this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the value to inspect.
/// @return true for one of the four declared values.
[[nodiscard]] constexpr bool isWcsDistortionModel(WcsDistortionModel model) noexcept
{
    return static_cast<std::uint8_t>(model) <= static_cast<std::uint8_t>(WcsDistortionModel::Unsupported);
}

/// What was measured about a solution's fitness to place apertures.
///
/// The identity strings are tokens compared byte for byte by whoever reuses the report; they say
/// which catalog, epoch, model and solution the residuals belong to.
struct WcsQualityReport
{
    /// Where the evidence came from.
    WcsQualitySource source = WcsQualitySource::None;

    /// Which distortion the solution carries.
    WcsDistortionModel distortionModel = WcsDistortionModel::None;

    /// Whether the frame's centre projects to the sky and back to a finite pixel.
    bool centerProjectionValid = true;

    /// Whether every corner of the frame projects to a finite direction.
    bool cornerProjectionValid = true;

    /// Whether the inverse projection is available, which is what the centre round trip proves.
    bool inverseProjectionSupported = true;

    /// Whether the solution came from outside and may not describe the frame as it is now.
    bool staleExternalWcs = false;

    /// How many stars matched.
    std::uint32_t matchedStarCount = 0u;

    /// How many of the frame's four quadrants hold a match.
    std::uint32_t skyQuadrantsCovered = 0u;

    /// The median residual, in pixels, or absent with no matches.
    std::optional<double> medianResidualPixels{};

    /// The root-mean-square residual, in pixels, or absent.
    std::optional<double> rmsResidualPixels{};

    /// The largest residual, in pixels, or absent.
    std::optional<double> maximumResidualPixels{};

    /// The median residual, in arcseconds, or absent.
    std::optional<double> medianResidualArcseconds{};

    /// The root-mean-square residual, in arcseconds, or absent.
    std::optional<double> rmsResidualArcseconds{};

    /// The largest residual, in arcseconds, or absent.
    std::optional<double> maximumResidualArcseconds{};

    /// The residual the report is held to, in pixels.
    double residualThresholdPixels = 3.0;

    /// The residual the report is held to, in arcseconds.
    double residualThresholdArcseconds = 3.0;

    /// The astrometric catalog family the residuals were measured against.
    std::string astrometricCatalogFamilyId{};

    /// That catalog's manifest identity.
    std::string astrometricCatalogManifestId{};

    /// The observation epoch the catalog positions were corrected to, as text.
    std::string observationEpoch{};

    /// The proper-motion model that corrected them, as text.
    std::string properMotionModelVersion{};

    /// The solution's identity, as text.
    std::string wcsSolutionId{};
};

/// The rule a quality report is held to, beyond the thresholds the report carries.
struct WcsQualityThresholds
{
    /// The fewest matched stars an acceptable report has.
    std::uint32_t minimumMatchedStars = 20u;

    /// The fewest quadrants an acceptable report covers.
    std::uint32_t minimumSkyQuadrants = 3u;

    /// The maximum residual is held to this multiple of the RMS threshold.
    double maximumResidualFactor = 2.0;
};

/// Why a quality report is not acceptable.
enum class WcsQualityVerdict : std::uint8_t
{
    /// The report is acceptable.
    Acceptable = 0u,

    /// The report has no evidence source.
    NoProvenance = 1u,

    /// The solution's distortion model is not one this build projects.
    UnsupportedDistortion = 2u,

    /// The centre, a corner or the inverse projection failed.
    IncompleteProjection = 3u,

    /// The solution is stale and was not re-matched.
    StaleWithoutRematch = 4u,

    /// Fewer stars matched than the rule needs.
    TooFewMatchedStars = 5u,

    /// Fewer quadrants hold a match than the rule needs.
    PoorSkyCoverage = 6u,

    /// A residual figure the rule reads - the RMS or maximum, in pixels or arcseconds - is absent
    /// or not finite.
    IncompleteResiduals = 7u,

    /// The RMS residual exceeds its threshold in pixels or arcseconds.
    RmsResidualExceeded = 8u,

    /// The maximum residual exceeds its threshold in pixels or arcseconds.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    MaximumResidualExceeded = 9u
};

/// Whether a value names a verdict this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param verdict the value to inspect.
/// @return true for every verdict, `Acceptable` included.
[[nodiscard]] constexpr bool isWcsQualityVerdict(WcsQualityVerdict verdict) noexcept
{
    return static_cast<std::uint8_t>(verdict) <= static_cast<std::uint8_t>(WcsQualityVerdict::MaximumResidualExceeded);
}

/// The verdict's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param verdict the value to name.
/// @return a null-terminated string literal compiled into this library; `"UnknownWcsQualityVerdict"`
///         for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* wcsQualityVerdictName(WcsQualityVerdict verdict) noexcept;

/// Whether a report is acceptable, and if not, the first reason it is not.
///
/// The order is a contract: provenance, distortion, projection, staleness, star count, quadrants,
/// residual presence, RMS, maximum.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param report what was measured.
/// @param thresholds the rule beyond the report's own thresholds.
/// @return `WcsQualityVerdict::Acceptable`, or the first rule that refused.
[[nodiscard]] AFTERNIGHTENGINE_API WcsQualityVerdict
checkWcsQuality(const WcsQualityReport& report, const WcsQualityThresholds& thresholds = {}) noexcept;

/// One detected source as a re-match reads it.
struct PreflightSource
{
    /// Where the source is, in engine pixel coordinates.
    PixelCoordinate position{};

    /// Its full width at half maximum, in pixels, or zero when unmeasured.
    double fullWidthPixels = 0.0;

    /// Its signal to noise, or zero when unmeasured.
    double signalToNoise = 0.0;
};

/// Which detected stars become preflight sources.
struct PreflightSourceSelection
{
    /// The smallest signal to noise admitted. Finite and not negative.
    double minimumSignalToNoise = 5.0;

    /// Whether a star with a clipped core is refused.
    bool rejectSaturated = true;

    /// Whether the sources are ordered by descending signal to noise before the ceiling applies.
    /// False keeps the detector's order.
    bool sortBySignalToNoise = true;

    /// The most sources kept, or absent for every admitted star.
    std::optional<std::uint32_t> maximumSources{kDefaultMaxPreflightSources};
};

/// Turns detected stars into preflight sources under a stated selection.
///
/// A star with no finite position is skipped; a star with no finite positive signal-to-noise figure
/// has zero and is refused by any positive floor. A source's width is the star's mean full width
/// when that is finite and positive, else the mean of its two profile widths when both are, else
/// zero. When sorted, the order is descending signal to noise, then ascending row, then ascending
/// column, and the sort is stable, so exact ties keep the detector's order.
///
/// Thread-safe and reentrant. Allocates only the returned list.
///
/// @param stars the detected stars.
/// @param selection which of them, in which order, how many.
/// @return the sources; `ErrorCode::InvalidArgument` for a non-finite or negative floor or a zero
///         ceiling.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<PreflightSource>>
makePreflightSources(std::span<const DetectedStar> stars, const PreflightSourceSelection& selection = {});

/// How the catalog magnitude limit is chosen when the caller does not fix it.
struct PreflightAutoMagnitude
{
    /// Whether the limit is stepped up from `minimumLimit` until the target density is reached.
    bool enabled = false;

    /// The brightest limit tried first. Finite, positive, and at or below
    /// `WcsPreflightRequest::magnitudeLimit`.
    double minimumLimit = 8.0;

    /// How much the limit moves per step. Finite and positive.
    double step = 0.5;

    /// How many catalog sources to reach, at least one, or absent for twice the detected count held
    /// between sixty-four and four thousand and ninety-six - five hundred and twelve when there are
    /// no detected sources.
    std::optional<std::uint32_t> targetSourceCount{};
};

/// Everything one preflight is asked to do.
struct WcsPreflightRequest
{
    /// The largest separation at which a detection and a catalog star match, in arcseconds. Finite
    /// and positive.
    double matchRadiusArcseconds = 5.0;

    /// The faintest G magnitude admitted, or the ceiling when the automatic limit is enabled. Finite.
    double magnitudeLimit = 16.0;

    /// Whether and how the limit is chosen automatically.
    PreflightAutoMagnitude autoMagnitude{};

    /// The residual the report is held to, in pixels. Finite and positive.
    double residualThresholdPixels = 3.0;

    /// The residual the report is held to, in arcseconds. Finite and positive.
    double residualThresholdArcseconds = 3.0;

    /// Whether the solution came from outside and is being re-matched for that reason.
    bool staleExternalWcs = true;

    /// The epoch to move catalog positions to, in Julian years. Finite.
    double observationEpochYear = 2016.0;

    /// The astrometric catalog family the report names.
    std::string astrometricCatalogFamilyId{};

    /// That catalog's manifest identity.
    std::string astrometricCatalogManifestId{};

    /// The observation epoch the report names, as text.
    std::string observationEpoch{};

    /// The proper-motion model the report names, as text.
    std::string properMotionModelVersion{};

    /// The solution's identity, as text.
    std::string wcsSolutionId{};
};

/// One accepted correspondence between a detected source and a catalog record.
struct PreflightMatch
{
    /// The source's position in the source list.
    std::uint32_t sourceIndex = 0u;

    /// The record's position in the catalog.
    std::uint32_t recordIndex = 0u;

    /// The record's Gaia identifier.
    std::uint64_t sourceIdentifier = 0u;

    /// Where the source was detected.
    PixelCoordinate detectedPosition{};

    /// Where the catalog record projected.
    PixelCoordinate projectedPosition{};

    /// The distance between them, in pixels.
    double residualPixels = 0.0;

    /// The same distance in arcseconds, at the solution's mean scale.
    double residualArcseconds = 0.0;

    /// The source's full width, in pixels.
    double fullWidthPixels = 0.0;

    /// The source's signal to noise.
    double signalToNoise = 0.0;
};

/// Everything one preflight produced.
struct WcsPreflightResult
{
    /// What was measured.
    WcsQualityReport report{};

    /// The correspondences, ordered by source position.
    std::vector<PreflightMatch> matches{};

    /// How many catalog records the region query answered with.
    std::uint32_t catalogCandidates = 0u;

    /// How many of them projected inside the frame.
    std::uint32_t projectedInsideFrame = 0u;

    /// The magnitude limit that was used, which the automatic rule may have chosen.
    double selectedMagnitudeLimit = 0.0;

    /// How many source-to-record pairs were inside the radius before one-to-one selection.
    std::uint32_t candidatePairs = 0u;
};

/// Re-matches detected sources against the spectrum catalog under a solution and reports its quality.
///
/// In order: the solution's centre round trip and corner projections are tested; the catalog is
/// queried over the frame's cone at the fixed or automatically chosen limit; every record is moved
/// to the observation epoch by its own proper motion and projected; records inside the frame are
/// paired with sources within the match radius; pairs are taken in increasing residual - ties by
/// source index, then identifier - one per source and one per record; and the report's counts,
/// quadrants and residual figures follow from the accepted pairs.
///
/// The pairs are counted before any is held, so a field with more than
/// `kDefaultMaxCatalogMatchCandidates` candidate pairs is refused before the pair table is taken. A
/// solution that yields no field centre, no query radius or no finite positive pixel scale is not a
/// failure: the result carries the projection checks and no matches.
///
/// Thread-safe with respect to `execution`, `solution`, `sources` and `catalog`, which are only
/// read. Cancellation is checked at bounded source and record boundaries. The candidate pairs and
/// the selection flags come from the engine context's memory resource and are released before this
/// returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param solution the frame's world-coordinate solution, which must state the frame's size.
/// @param sources the detected sources, at most `kMaxPreflightSources` of them.
/// @param catalog the spectrum catalog.
/// @param request every stage's parameters.
/// @return the report and matches; `ErrorCode::NotInitialized` when `execution` holds nothing or
///         the catalog handle is empty; `ErrorCode::InvalidArgument` for a request field outside its
///         range or a solution with no frame size; `ErrorCode::LimitExceeded` above the source
///         ceiling, or when the candidate pairs exceed `kDefaultMaxCatalogMatchCandidates`, carrying
///         the requested and permitted counts; a memory-budget refusal; or cancellation. A frame
///         that matches nothing is a success whose report says so.
[[nodiscard]] AFTERNIGHTENGINE_API Result<WcsPreflightResult> runWcsPreflight(const ExecutionContext& execution,
                                                                              const WcsSolution& solution,
                                                                              std::span<const PreflightSource> sources,
                                                                              const SpectrumCatalog& catalog,
                                                                              const WcsPreflightRequest& request = {});

} // namespace an
