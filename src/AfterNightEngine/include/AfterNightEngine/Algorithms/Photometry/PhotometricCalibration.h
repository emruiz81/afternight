// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Placing measured stars on a catalog and fitting one photometric zero point.
///
/// **A catalog states its band once, so the fit has nothing to guess.** `SkyCatalog` carries one
/// photometric band for every entry it holds, and a cross-match against it produces measurements in
/// that band. The zero-point fit therefore takes numbers and returns numbers; which band they are in
/// is a fact about the catalog the caller matched against, and it is not repeated per row. A match
/// list that mixed bands would leave the fit to pick one, which is a guess rather than a rule.
///
/// **Matching is the catalog domain's, not a second copy of it.** `crossMatchDetectedStars` projects
/// each star through the frame's world-coordinate solution and hands the positions to
/// `matchCatalogSources`, which is what makes a photometric cross-match and a plate-solve enrichment
/// agree about which star is which.
///
/// **Missing is not malformed.** A star whose flux is not positive has no magnitude and is counted
/// rather than fitted; a catalog magnitude that is not a finite number is a malformed catalog and is
/// refused by name.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogEnrichment.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most matches one zero-point fit accepts.
///
/// A ceiling rather than a capacity: past it the fit's working set is no longer a few kilobytes,
/// and a caller with more matched stars than this has a catalog problem rather than a fit problem.
inline constexpr std::uint32_t kMaxZeroPointMatches = 1000000u;

/// The floor on the robust scale used to clip zero-point outliers, in magnitudes.
inline constexpr double kDefaultMinimumZeroPointRobustSigma = 0.02;

/// The floor on the clipping half-width, in magnitudes.
inline constexpr double kDefaultMinimumZeroPointClipThreshold = 0.05;

/// The smallest standard deviation a fitted profile is read at when its volume stands for a flux.
///
/// In pixels. A fit narrower than this is interpolation rather than a star, and its volume would be
/// dominated by the width error rather than the amplitude.
inline constexpr double kMinimumPsfFluxSigmaPixels = 0.5;

/// One star's instrumental flux beside the catalog magnitude of the entry it matched.
///
/// Both numbers are in the units the match produced them in: the flux in the frame's numeric
/// domain summed over the star's fitted profile or box, the magnitude in the catalog's stated band.
struct PhotometricMatch
{
    /// Background-subtracted instrumental flux, in the frame's numeric domain.
    ///
    /// A value that is not positive is a legitimate measurement outcome - a candidate whose box held
    /// no light above its own sky - and is counted rather than fitted.
    double instrumentalFlux = 0.0;

    /// The matched entry's catalog magnitude, in the catalog's band. Must be finite.
    double catalogMagnitude = 0.0;

    /// Whether two matches are the same two numbers.
    ///
    /// @param left one match.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const PhotometricMatch& left,
                                                   const PhotometricMatch& right) noexcept = default;
};

/// The clipping rule the zero-point fit re-decides its sample under.
struct ZeroPointFitRequest
{
    /// How many times the median and robust scale are re-decided over the retained sample.
    ///
    /// One to sixteen, three by default; clipping stops early at a round that removes nothing.
    std::uint32_t maximumClipIterations = 3u;

    /// How many robust scales from the median a zero point may sit and still be retained. Finite and
    /// positive.
    double clipSigmas = 3.0;

    /// Floor on the robust scale, in magnitudes. Finite and not negative.
    double minimumRobustSigma = kDefaultMinimumZeroPointRobustSigma;

    /// Floor on the clipping half-width, in magnitudes. Finite and not negative.
    double minimumClipThreshold = kDefaultMinimumZeroPointClipThreshold;
};

/// One fitted photometric zero point and how it was reached.
struct ZeroPointFitResult
{
    /// The zero point, in magnitudes: the mean of `catalogMagnitude + 2.5 * log10(flux)` over the
    /// retained matches.
    double zeroPoint = 0.0;

    /// The standard uncertainty of `zeroPoint`: the residual root mean square divided by the square
    /// root of the retained count, and zero for a single retained match.
    double zeroPointStandardUncertainty = 0.0;

    /// Root mean square of the retained zero points about the mean, in magnitudes.
    double rmsResidual = 0.0;

    /// How many matches were supplied.
    std::uint32_t suppliedCount = 0u;

    /// How many supplied matches carried a positive finite flux and therefore had a zero point.
    std::uint32_t usableCount = 0u;

    /// How many usable matches the clipping retained and the zero point was averaged over.
    std::uint32_t retainedCount = 0u;

    /// How many clipping rounds were decided, at least one.
    std::uint32_t clipIterations = 0u;

    /// The robust scale the last round clipped at, in magnitudes, after its floor.
    double finalRobustSigma = 0.0;

    /// Peak engine-context bytes held for the per-match zero points.
    std::uint64_t workingBytes = 0u;
};

/// Fits one photometric zero point from matched stars.
///
/// Each usable match contributes `catalogMagnitude + 2.5 * log10(instrumentalFlux)`. Up to
/// `maximumClipIterations` rounds then take the median and the median absolute deviation of the
/// retained values, scale the deviation to a sigma with its stated floor, and retain the values
/// within `clipSigmas` of the median or within the clipping floor, whichever is wider. A round that
/// starts with two values or fewer, that removes nothing, or that would remove everything ends the
/// clipping; the answer is the mean of what remains.
///
/// Every rank is exact and every sum follows input order, so the result does not depend on the
/// admitted thread count. The per-match zero points come from the engine context's memory
/// resource and are released before this returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param matches the matched stars, at most `kMaxZeroPointMatches` of them.
/// @param request the clipping rule.
/// @return the fit; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a request field outside its stated range;
///         `ErrorCode::LimitExceeded` above the match ceiling; `ErrorCode::InvalidData` for a
///         catalog magnitude that is not finite, carrying the first such match's index in
///         `ContextKey::Completed`; `ErrorCode::EmptySelection` when no match carries a positive
///         finite flux; a memory-budget refusal; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ZeroPointFitResult>
fitPhotometricZeroPoint(const ExecutionContext& execution, std::span<const PhotometricMatch> matches,
                        const ZeroPointFitRequest& request = {});

/// Which of a detected star's measurements stands for its flux.
///
/// Two answers exist because both are in use: a zero-point fit takes whatever flux the caller
/// supplies, and the colour path integrates the fitted Gaussian. Stated rather than inferred, because
/// the two differ by a shape-dependent factor and a fit that mixed them would be wrong by it.
enum class StarFluxEstimator : std::uint8_t
{
    /// The volume under the fitted profile: `amplitude * 2 * pi * sigmaX * sigmaY`, each sigma's
    /// magnitude held at or above `kMinimumPsfFluxSigmaPixels` and a negative amplitude read as
    /// zero. A Moffat profile's scales are read the same way, through the same Gaussian formula.
    PsfVolume = 0u,

    /// The background-subtracted sum over the fitted box, recovered from the star's instrumental
    /// magnitude as `10^(-0.4 * magnitude)`. Absent when the star has no magnitude.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    BoxSum = 1u
};

/// Whether a value names an estimator this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param estimator the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isStarFluxEstimator(StarFluxEstimator estimator) noexcept
{
    return estimator == StarFluxEstimator::PsfVolume || estimator == StarFluxEstimator::BoxSum;
}

/// The estimator's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param estimator the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownStarFluxEstimator"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starFluxEstimatorName(StarFluxEstimator estimator) noexcept;

/// One detected star's instrumental flux under a stated estimator.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param star the star as the detector measured it.
/// @param estimator which measurement stands for the flux.
/// @return the flux in the frame's numeric domain, or absent when the estimator's inputs are not
///         finite, the star has no magnitude under `BoxSum`, or the estimator is out of range. A
///         present value is zero under `PsfVolume` for a fit whose amplitude is zero or negative,
///         and is never negative.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> starInstrumentalFlux(const DetectedStar& star,
                                                                              StarFluxEstimator estimator) noexcept;

/// What a photometric cross-match is asked to do.
struct PhotometricCrossMatchRequest
{
    /// The match radius, magnitude rule and candidate ceiling, handed to the catalog domain.
    CatalogMatchRequest match{};

    /// Which measurement of each star stands for its flux.
    StarFluxEstimator fluxEstimator = StarFluxEstimator::PsfVolume;
};

/// Everything one photometric cross-match produced.
struct PhotometricCrossMatchResult
{
    /// The one-to-one correspondences, ordered by star position and then entry position.
    CatalogMatchResult matches{};

    /// One photometric measurement per correspondence, in the same order, for the entries that
    /// carry a magnitude. A correspondence whose entry has no magnitude produces no measurement and
    /// is counted in `magnitudelessMatchCount`.
    std::vector<PhotometricMatch> photometry{};

    /// Which correspondence each photometric measurement came from, as a position into `matches`.
    std::vector<std::uint32_t> photometryMatchIndices{};

    /// Stars the world-coordinate solution could not place on the sky, which were matched against
    /// nothing.
    std::uint32_t unplacedStarCount = 0u;

    /// Correspondences whose catalog entry carries no magnitude.
    std::uint32_t magnitudelessMatchCount = 0u;

    /// Correspondences whose star yields no flux under the requested estimator.
    std::uint32_t fluxlessMatchCount = 0u;
};

/// Places detected stars on the sky and matches them one to one against a catalog.
///
/// Each star's position is projected through the solution; a star the solution cannot place is
/// carried as unpositioned so the match stays index-aligned with the star list. The positioned
/// stars are handed to `matchCatalogSources`, and each correspondence whose entry carries a
/// magnitude becomes a `PhotometricMatch` pairing the star's flux with that magnitude.
///
/// The catalog's stated band is the band of every measurement returned. Thread-safe with respect
/// to `execution`, `stars`, `solution` and `catalog`, which are only read. Cancellation is checked
/// at bounded star boundaries and inside the match.
///
/// @param execution the operation policy and engine-context lease.
/// @param solution the frame's world-coordinate solution.
/// @param stars the detected stars, at most `kMaxCatalogMatchSources` of them.
/// @param catalog the catalog to match against.
/// @param request the match rule and the flux estimator.
/// @return the correspondences and measurements; `ErrorCode::NotInitialized` when `execution` holds
///         nothing or the catalog handle did not come from a successful build;
///         `ErrorCode::EmptySelection` for no stars; `ErrorCode::LimitExceeded` for more than
///         `kMaxCatalogMatchSources` stars; `ErrorCode::InvalidArgument` for an out-of-range
///         estimator or match request; and every failure `matchCatalogSources` reports. A field
///         that matches nothing is a success with no measurements.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PhotometricCrossMatchResult>
crossMatchDetectedStars(const ExecutionContext& execution, const WcsSolution& solution,
                        std::span<const DetectedStar> stars, const SkyCatalog& catalog,
                        const PhotometricCrossMatchRequest& request = {});

} // namespace an
