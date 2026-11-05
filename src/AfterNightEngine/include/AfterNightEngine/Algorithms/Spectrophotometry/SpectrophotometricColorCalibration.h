// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Spectrophotometric colour calibration: the plan that composes a response
/// model, a white reference and a catalog into one recipe, the operation that runs it over a frame,
/// and the transition that records what it did.
///
/// **The plan is built once and the frame is calibrated many times.** `planSpectrophotometricColorCalibration`
/// compiles the response, integrates the white reference into the fit's anchor, checks every
/// request field and fixes the identity that the result's lineage will carry. Calibrating a frame is
/// then a composition of published operations - detect, select, preflight, predict, sample, fit,
/// measure the sky, apply - each reachable on its own, with progress once per stage and cancellation
/// checked between them and inside each.
///
/// **Nothing is written until everything before the apply has succeeded.** A preflight the quality
/// rule refuses, a fit whose verdict is not `Accepted`, a stopped execution: each returns before the
/// destination is touched, and the failure names the verdict it was refused on. The apply's own
/// clipping policy is the last thing that decides whether samples are written.
///
/// **What the result means is stated once, by the caller, through a transition.** The frame's
/// metadata is not read here: `SpectrophotometricCalibrationTransition.h` builds the one transition
/// a calibrated image takes from the identity this plan carries, and the caller hands it to
/// `Image::transition`. That builder lives next door because it reaches the whole metadata model
/// and the plan must not.
///
/// **Its recipe carries its own version name**, because the conventions of the stages it composes
/// are part of its answer: aperture positions with the first pixel centre at one half, a sky window
/// that is not clamped to the unit interval, a floating-only apply, and the fit's exact
/// median-absolute-deviation constant.

#pragma once

#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectralResponse.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricCalibrationIdentity.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricSamples.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/WcsPreflight.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How stars are found in the green channel. Defined in `Algorithms/Stars/StarDetection.h`.
///
/// Named rather than included, and passed by reference rather than held by value, so a caller that
/// only fits and applies from samples it already has does not inherit the detector's include
/// closure.
struct StarDetectionRequest;

/// How many stages `calibrateSpectrophotometricColor` reports progress over.
inline constexpr std::uint64_t kSpectrophotometricCalibrationStageCount = 8u;

/// The default ceiling on stars detected in the green channel.
inline constexpr std::uint32_t kDefaultSpectrophotometricMaximumStars = 2000u;

/// Whether and where the sky is measured for neutralization.
struct SpectrophotometricBackgroundRequest
{
    /// Whether the sky is measured and the offsets derived from it. False writes zero offsets.
    bool neutralize = true;

    /// The rectangle to measure the sky in, or absent for the whole frame.
    std::optional<StatisticsRegion> region{};

    /// Where the neutralized background goes.
    ColorCalibrationBackgroundMode mode = ColorCalibrationBackgroundMode::SharedPedestal;
};

/// Everything one spectrophotometric colour calibration is asked to do, beyond the response, the
/// white reference and the catalog the plan is built from.
struct SpectrophotometricColorCalibrationRequest
{
    /// The most stars kept from the green channel's detection, brightest first. One to
    /// `kMaxStarsPerFrame`.
    std::uint32_t maximumStars = kDefaultSpectrophotometricMaximumStars;

    /// Which detected stars become preflight sources.
    PreflightSourceSelection sourceSelection{};

    /// The re-match against the catalog.
    WcsPreflightRequest preflight{};

    /// Whether the preflight report must be acceptable before any aperture is placed. False reports
    /// an unacceptable verdict as a warning and continues.
    bool requireAcceptableWcsQuality = true;

    /// The rule the report is held to.
    WcsQualityThresholds qualityThresholds{};

    /// How apertures are placed, sized and judged. Its quality-gate fields are replaced by the two
    /// above when the plan is built.
    SampleConstructionRequest samples{};

    /// The fit's floors, clipping rule and policy. Its white-reference gains are replaced by the
    /// plan's when it is built.
    SpectrophotometricFitRequest fit{};

    /// Whether and where the sky is neutralized.
    SpectrophotometricBackgroundRequest background{};

    /// What happens to a calibrated sample that leaves the representable interval.
    ColorCalibrationClipping clipping = ColorCalibrationClipping::ClampToPixelRange;

    /// The frame's filter name, when the caller knows it. A narrowband name refuses the plan by
    /// name unless `allowNarrowbandInput` says otherwise. Empty says the filter is unknown, which
    /// is not a refusal.
    std::string filterName{};

    /// Whether a narrowband filter name is admitted, for a caller that has a narrowband
    /// observation model to go with it.
    bool allowNarrowbandInput = false;
};

/// One recipe, built once and applied to any number of frames.
struct SpectrophotometricColorCalibrationPlan
{
    /// The response compiled onto the catalog's grid.
    PreparedResponse response{};

    /// What the white reference measured as through the response, red, green and blue.
    std::array<double, 3u> whiteReferenceCounts{};

    /// The gains that make those counts neutral, which anchor the fit.
    std::array<double, 3u> whiteReferenceGains{1.0, 1.0, 1.0};

    /// The request as resolved: the quality gate and the white-reference gains copied into the
    /// stages that read them.
    SpectrophotometricColorCalibrationRequest request{};

    /// What the result's lineage will name.
    SpectrophotometricCalibrationIdentity identity{};
};

/// Builds the recipe.
///
/// The request is checked field by field; a narrowband filter name is refused unless admitted; the
/// response is compiled through `prepareResponse` onto the manifest's grid; the white reference is
/// integrated through `integrateWhiteReference` and a count that is not finite and positive is
/// refused; the gate and the gains are copied into the sample and fit requests; the identity is
/// taken from the profile, the white reference and the manifest; and the fit request is checked by
/// running the fit over no samples. The aperture request is not checked here: the sample builder
/// checks it when a calibration runs.
///
/// Runs on the calling thread and allocates the plan.
///
/// @param execution the operation policy and engine-context lease.
/// @param profile the three curves and the kind.
/// @param whiteReference the spectrum a neutral colour is defined against.
/// @param observation the sky and the passbands.
/// @param manifest the manifest of the catalog the calibration will read, for its grid and identity.
/// @param request every stage's parameters.
/// @return the plan; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a request field outside its range - the message names
///         the field - a manifest grid the response cannot be compiled onto, or a malformed curve,
///         white reference or observation; `ErrorCode::Unsupported` for a
///         narrowband filter name that was not admitted or a narrowband integration over a
///         monochrome sensor with RGB filters; `ErrorCode::InvalidData` when a white-reference
///         count is not finite and positive; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrophotometricColorCalibrationPlan>
planSpectrophotometricColorCalibration(const ExecutionContext& execution, const ResponseProfile& profile,
                                       const WhiteReferenceSpectrum& whiteReference,
                                       const ObservationModel& observation, const SpectrumCatalogManifest& manifest,
                                       const SpectrophotometricColorCalibrationRequest& request = {});

/// What the catalog predicts for every preflight match.
struct MatchedColorPrediction
{
    /// One prediction per match, in match order, as the sample builder takes them. Unavailable
    /// where the integration's status was not `Integrated`.
    std::vector<CatalogColorReference> colors{};

    /// What the batch did.
    SyntheticPhotometryBatchResult batch{};
};

/// Predicts the catalog colour of every preflight match through a prepared response.
///
/// The matches' identifiers are integrated as one batch through `integrateSyntheticPhotometryBatch`
/// and each `Integrated` prediction becomes an available reference; every other status becomes an
/// unavailable one, which the sample builder counts as `CatalogColorUnavailable`.
///
/// @param execution the operation policy and engine-context lease.
/// @param response the compiled response, on the catalog's grid.
/// @param catalog the catalog holding the spectra.
/// @param matches the preflight matches.
/// @return the references and the batch counts; `ErrorCode::EmptySelection` for no matches; and
///         every failure the batch reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MatchedColorPrediction>
predictMatchedCatalogColors(const ExecutionContext& execution, const PreparedResponse& response,
                            const SpectrumCatalog& catalog, std::span<const PreflightMatch> matches);

/// What one apply, with its sky measurement, produced.
struct SpectrophotometricApplyResult
{
    /// The offsets that were written, zero when nothing was neutralized.
    ColorChannelOffsets offsets{0.0, 0.0, 0.0};

    /// The sky estimate the offsets came from, present when the background was neutralized.
    std::optional<ColorBackgroundEstimate> background{};

    /// What the apply wrote.
    ColorCalibrationApplyResult apply{};
};

/// Measures the sky when asked, derives the offsets and writes the gains.
///
/// `estimateColorBackground`, `colorCalibrationOffsets` and `applyColorCalibration` in that order,
/// under the frame's own validity rule, on the operation's threads. The tail every
/// spectrophotometric calibration ends with, reachable on its own for a caller that already holds
/// its gains.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked three-channel linear floating RGB source.
/// @param destination where the calibrated samples go, laid out exactly as the frame. May equal
///        `frame.samples()`.
/// @param destinationBytes bytes available from `destination`, at least the layout's total.
/// @param gains the per-channel multipliers. Finite and positive.
/// @param background whether and where the sky is neutralized.
/// @param clipping what happens to a sample that leaves the representable interval.
/// @return the offsets, the sky estimate and the apply counts; `ErrorCode::NotInitialized` when
///         `execution` holds nothing; `ErrorCode::InvalidArgument` for an invalid frame, a missing
///         or undersized destination, or a background mode outside its range;
///         `ErrorCode::Unsupported` for a frame that is not three-channel linear floating RGB;
///         every failure the composed operations report; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrophotometricApplyResult>
applySpectrophotometricGains(const ExecutionContext& execution, const StarFrameView& frame, void* destination,
                             std::uint64_t destinationBytes, const ColorChannelGains& gains,
                             const SpectrophotometricBackgroundRequest& background,
                             ColorCalibrationClipping clipping = ColorCalibrationClipping::ClampToPixelRange);

/// What a calibration from ready samples produced.
struct SpectrophotometricSampleCalibrationResult
{
    /// The gain fit.
    SpectrophotometricFitResult fit{};

    /// The sky, the offsets and what the apply wrote.
    SpectrophotometricApplyResult applied{};
};

/// Fits the plan's gains from samples a caller already holds and writes them.
///
/// `fitSpectrophotometricGains` under the plan's fit request, refused by name when its verdict is
/// not `Accepted`, then `applySpectrophotometricGains` under the plan's background and clipping.
/// Nothing is written unless the fit is accepted.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the recipe.
/// @param frame the checked three-channel linear floating RGB source.
/// @param destination where the calibrated samples go, laid out exactly as the frame. May equal
///        `frame.samples()`.
/// @param destinationBytes bytes available from `destination`, at least the layout's total.
/// @param samples the colour samples.
/// @return the fit and what was written; the frame and destination refusals
///         `applySpectrophotometricGains` states, made before the fit runs;
///         `ErrorCode::InvalidData` when the fit's verdict is not `Accepted`, carrying the verdict's
///         name; and every failure the composed operations report.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrophotometricSampleCalibrationResult>
calibrateSpectrophotometricColorFromSamples(const ExecutionContext& execution,
                                            const SpectrophotometricColorCalibrationPlan& plan,
                                            const StarFrameView& frame, void* destination,
                                            std::uint64_t destinationBytes,
                                            std::span<const SpectrophotometricSample> samples);

/// Everything one whole calibration produced.
struct SpectrophotometricColorCalibrationResult
{
    /// How many stars the green channel's detection accepted.
    std::uint32_t detectedStars = 0u;

    /// How many of them the selection kept as preflight sources.
    std::uint32_t preflightSources = 0u;

    /// The preflight's report and matches.
    WcsPreflightResult preflight{};

    /// The verdict the report was held to.
    WcsQualityVerdict qualityVerdict = WcsQualityVerdict::Acceptable;

    /// What the catalog prediction batch did.
    SyntheticPhotometryBatchResult prediction{};

    /// The colour samples and their counts.
    SampleConstructionResult samples{};

    /// The gain fit.
    SpectrophotometricFitResult fit{};

    /// The sky, the offsets and what the apply wrote.
    SpectrophotometricApplyResult applied{};
};

/// Detects, matches, predicts, samples, fits and applies a spectrophotometric colour calibration to
/// one RGB frame.
///
/// In order: the frame and destination are checked; stars are detected in the green channel through
/// `planStarDetection` and `detectStars` under the plan's ceiling; the selection turns them into
/// preflight sources; `runWcsPreflight` re-matches them against the catalog; the report is held to
/// `checkWcsQuality`, refusing by name when required and warning otherwise; the matches' catalog
/// colours are predicted through `predictMatchedCatalogColors`; `buildSpectrophotometricSamples`
/// measures every match; `fitSpectrophotometricGains` fits them, refused by name when its verdict is
/// not `Accepted`; and `applySpectrophotometricGains` measures the sky and writes the frame. Progress
/// is reported once per stage over `kSpectrophotometricCalibrationStageCount` and cancellation is
/// checked between stages and inside each.
///
/// The frame must be three-channel linear RGB in a floating representation; a nonlinear encoding
/// has no photometry to fit, and an integer frame converts first. The destination shares the frame's
/// layout and may be the frame itself. Nothing is written unless every stage before the apply
/// succeeds. The frame's metadata is not read here: the caller derives the result's transition from
/// `spectrophotometricCalibrationTransition`.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the recipe.
/// @param frame the checked three-channel linear floating RGB frame.
/// @param destination where the calibrated samples go, laid out exactly as the frame. May equal
///        `frame.samples()`.
/// @param destinationBytes bytes available from `destination`, at least the layout's total.
/// @param solution the frame's world-coordinate solution, which must state the frame's size.
/// @param catalog the spectrum catalog, on the plan's grid.
/// @param detection how stars are found; its `maximumStars` is replaced by the plan's.
/// @return every stage's result; `ErrorCode::NotInitialized` when `execution` holds nothing or the
///         catalog handle is empty; `ErrorCode::InvalidArgument` for an invalid frame, an undersized
///         destination or a catalog whose grid is not the plan's; `ErrorCode::Unsupported` for a
///         frame that is not three-channel linear floating RGB; `ErrorCode::EmptySelection` when
///         no star is detected, no source is selected or no match is made; `ErrorCode::InvalidState`
///         when the quality gate refuses, carrying the verdict's name; `ErrorCode::InvalidData` when
///         the fit's verdict is not `Accepted`, carrying its name; and every failure the composed
///         operations report.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrophotometricColorCalibrationResult>
calibrateSpectrophotometricColor(const ExecutionContext& execution, const SpectrophotometricColorCalibrationPlan& plan,
                                 const StarFrameView& frame, void* destination, std::uint64_t destinationBytes,
                                 const WcsSolution& solution, const SpectrumCatalog& catalog,
                                 const StarDetectionRequest& detection);

} // namespace an
