// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour samples a spectrophotometric fit is made of: one matched star's
/// measured channel ratios beside the ratios its catalog spectrum predicts.
///
/// **The catalog side is an input, not a computation this makes.** What a spectrum predicts for a
/// given camera and filter set is the response model's business (`SpectralResponse.h`, through
/// `predictMatchedCatalogColors` in `SpectrophotometricColorCalibration.h`); this builder takes one
/// predicted ratio triple per match and refuses a sample whose triple is absent or not a ratio.
/// That is what lets the image side be measured, tested and timed on its own.
///
/// **The aperture measurements run as one batch.** Every match is measured on the operation's
/// threads with the other matches as its neighbours, and the samples are then assembled in match
/// order, so the result is the same at any thread count and the image is walked once.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/AperturePhotometry.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/WcsPreflight.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// Why a sample was not accepted.
enum class SampleRejectionReason : std::uint8_t
{
    /// The sample was accepted.
    None = 0u,

    /// The catalog holds no record with the match's identifier.
    MissingCatalogSource = 1u,

    /// No usable predicted ratio triple was supplied for the match.
    CatalogColorUnavailable = 2u,

    /// The aperture measurement refused the star; `apertureRejection` says why.
    ApertureRejected = 3u,

    /// The measured green flux is not positive, so no ratio exists.
    NonPositiveGreenFlux = 4u,

    /// A measured ratio is not a finite number.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NonFiniteRatio = 5u
};

/// How many rejection reasons this build reports, `None` included.
inline constexpr std::size_t kSampleRejectionReasonCount =
    static_cast<std::size_t>(SampleRejectionReason::NonFiniteRatio) + 1u;

/// Whether a value names a rejection reason this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param reason the value to inspect.
/// @return true for every reason, `None` included.
[[nodiscard]] constexpr bool isSampleRejectionReason(SampleRejectionReason reason) noexcept
{
    return static_cast<std::size_t>(reason) < kSampleRejectionReasonCount;
}

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param reason the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSampleRejectionReason"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* sampleRejectionReasonName(SampleRejectionReason reason) noexcept;

/// What a star's catalog spectrum predicts for the camera, relative to green.
struct CatalogColorReference
{
    /// Red, green and blue relative to green. Green is one. Every value finite and positive when
    /// `available`.
    std::array<double, 3u> relativeToGreen{1.0, 1.0, 1.0};

    /// Whether a prediction exists for this match.
    bool available = false;
};

/// One matched star with both sides of its colour.
struct SpectrophotometricSample
{
    /// The record's Gaia identifier.
    std::uint64_t sourceIdentifier = 0u;

    /// The record's position in the catalog, meaningful when the catalog held it.
    std::uint32_t recordIndex = 0u;

    /// Where the source was detected.
    PixelCoordinate detectedPosition{};

    /// Where the catalog record projected.
    PixelCoordinate projectedPosition{};

    /// The distance between them, in pixels.
    double residualPixels = 0.0;

    /// The same distance in arcseconds.
    double residualArcseconds = 0.0;

    /// Whether the sample is usable by a fit.
    bool accepted = false;

    /// Why it is not, or `None`.
    SampleRejectionReason rejection = SampleRejectionReason::None;

    /// Why the aperture refused it, when it did.
    ApertureRejectionReason apertureRejection = ApertureRejectionReason::None;

    /// The aperture measurement, whether or not it was accepted.
    AperturePhotometryMeasurement aperture{};

    /// The measured channel fluxes relative to green. Ones until measured.
    std::array<double, 3u> imageRelativeToGreen{1.0, 1.0, 1.0};

    /// The predicted channel fluxes relative to green. Ones until supplied.
    std::array<double, 3u> catalogRelativeToGreen{1.0, 1.0, 1.0};
};

/// Everything one sample construction is asked to do.
struct SampleConstructionRequest
{
    /// Whether the preflight report must be acceptable before any aperture is placed.
    bool requireAcceptableWcsQuality = true;

    /// The rule the report is held to when required.
    WcsQualityThresholds qualityThresholds{};

    /// Whether the other matches mask each star's annulus.
    bool includeNeighbors = true;

    /// Where each aperture is centred. `CatalogProjectedPosition` centres it on the projected
    /// record; the other two values centre it on the detected position, the only other centroid a
    /// preflight match carries.
    ApertureCentroidSource centroidSource = ApertureCentroidSource::DetectedStarCentroid;

    /// How apertures are placed, sized and judged.
    AperturePhotometryRequest aperture{};
};

/// Everything one sample construction produced.
struct SampleConstructionResult
{
    /// One sample per preflight match, in match order.
    std::vector<SpectrophotometricSample> samples{};

    /// How many were accepted.
    std::uint32_t acceptedCount = 0u;

    /// How many were refused.
    std::uint32_t rejectedCount = 0u;

    /// How many each reason refused, indexed by `SampleRejectionReason`.
    std::array<std::uint32_t, kSampleRejectionReasonCount> rejections{};

    /// How many the aperture refused for each of its reasons, indexed by `ApertureRejectionReason`,
    /// counting only samples that reached the aperture check - a sample already refused for its
    /// catalog record or prediction is not counted here.
    std::array<std::uint32_t, kApertureRejectionReasonCount> apertureRejections{};

    /// Peak engine-context bytes held for aperture scratch.
    std::uint64_t workingBytes = 0u;
};

/// Builds the colour samples of every preflight match.
///
/// The preflight report is held to the quality rule when asked, before any aperture is placed. Each
/// match is then looked up by identifier, given its supplied prediction, measured through
/// `measureAperturePhotometryBatch` at its detected or projected position, and turned into channel
/// ratios relative to green. A match that fails any step is a sample whose `rejection` says which -
/// the first of catalog record, prediction, aperture, green flux and ratio to refuse it; the
/// construction itself succeeds unless the gate refuses or an operation fails. With the gate
/// required, a preflight with no matches is refused by the gate before the empty-match check.
///
/// Progress is reported in three steps: before the apertures are measured, after them, and when
/// the samples are assembled. Thread-safe with respect to `execution`, `frame`, `preflight`,
/// `catalog` and `catalogColors`, which are only read.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked three-channel frame the apertures are measured on.
/// @param preflight the matches and the report they produced.
/// @param catalog the spectrum catalog the matches name records in.
/// @param catalogColors one prediction per match, in match order, exactly as long as the matches.
/// @param request every stage's parameters.
/// @return the samples and counts; `ErrorCode::NotInitialized` when `execution` holds nothing or
///         the catalog handle is empty; `ErrorCode::InvalidArgument` for an invalid frame, a
///         centroid source or aperture request outside its range, or a prediction list that is not
///         as long as the matches; `ErrorCode::Unsupported` for a frame that is not three-channel
///         RGB; `ErrorCode::InvalidState` when the quality gate refuses, carrying the verdict's name;
///         `ErrorCode::EmptySelection` for no matches; and every failure the aperture batch reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SampleConstructionResult>
buildSpectrophotometricSamples(const ExecutionContext& execution, const StarFrameView& frame,
                               const WcsPreflightResult& preflight, const SpectrumCatalog& catalog,
                               std::span<const CatalogColorReference> catalogColors,
                               const SampleConstructionRequest& request = {});

} // namespace an
