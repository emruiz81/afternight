// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The spectrophotometric gain fit: three channel gains from the colour
/// samples a field's matched stars produced, anchored on a white reference.
///
/// **The fit is a repeated-median line per colour channel, evaluated at the white reference.** For
/// red and for blue, each usable sample is a point whose abscissa is the ratio its catalog spectrum
/// predicts and whose ordinate is the ratio the image measured. The line through those points is
/// the repeated-median regression - the median over points of the median over other points of the
/// pairwise slope, then the median intercept - which is robust to a sizable minority of outliers
/// without a scale estimate. Under the iterative policy the residuals are clipped at a stated number
/// of robust sigmas and the line refitted until the membership settles; under the single-pass policy
/// one line decides the membership and stands. The line's value at the white reference's catalog
/// ratio is what the image would measure for a neutral star, and one over it is the channel's raw
/// gain. The three raw gains are then divided by the largest so no channel is amplified.
///
/// **Every refusal is a verdict on the result, not a failure of the operation.** Too few usable
/// samples, a catalog colour span too narrow to condition a line, too few inliers after clipping, a
/// gain too uncertain to trust, and a non-finite fit are all things a well-formed sample set can
/// exhibit; the fit reports which one, beside every figure it computed on the way, and the caller
/// decides. The operation fails only for a malformed request or a stopped execution.
///
/// **The per-point medians run on the operation's threads.** Each point's median-over-others is
/// independent of every other point's, so the work is partitioned by point and the answer is the
/// same at any thread count. Above a stated point count the fit works on an evenly spaced subset in
/// catalog-ratio order, which bounds the quadratic cost.
///
/// **The robust scale uses the exact constant.** A median absolute deviation becomes a sigma through
/// `kMedianAbsoluteDeviationToSigma` (1.4826022185056018) rather than the rounded 1.4826.

#pragma once

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricSamples.h>
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

/// The most colour samples one fit accepts.
inline constexpr std::uint32_t kMaxSpectrophotometricFitSamples = 1000000u;

/// The most points a repeated-median line is fitted through exactly; above it, an evenly spaced
/// subset in catalog-ratio order stands in, which bounds the quadratic cost.
inline constexpr std::uint32_t kSpectrophotometricExactFitPointLimit = 1024u;

/// The default floor on usable samples.
inline constexpr std::uint32_t kDefaultSpectrophotometricMinimumSamples = 35u;

/// The default floor on the span of catalog ratios, per colour channel.
inline constexpr double kDefaultSpectrophotometricMinimumCatalogRatioSpan = 0.15;

/// The default clipping width, in robust sigmas.
inline constexpr double kDefaultSpectrophotometricClipSigmas = 3.0;

/// The default ceiling on clipping iterations.
inline constexpr std::uint32_t kDefaultSpectrophotometricMaximumIterations = 4u;

/// The default floor on every robust scale, so a perfect fit does not clip on rounding.
inline constexpr double kDefaultSpectrophotometricMinimumRobustScale = 1.0e-6;

/// The default ceiling on a gain's relative standard uncertainty.
inline constexpr double kDefaultSpectrophotometricMaximumRelativeGainUncertainty = 0.05;

/// The default inlier count below which the fit is reported as low-support.
inline constexpr std::uint32_t kDefaultSpectrophotometricLowSupportSamples = 60u;

/// How outliers are decided.
enum class SpectrophotometricOutlierPolicy : std::uint8_t
{
    /// Fit, clip the residuals at the stated sigmas, refit over the survivors, and repeat until the
    /// membership settles or the iteration ceiling is reached; then refit once more over the final
    /// membership.
    IterativeSigmaClipRefit = 0u,

    /// Fit once, clip the residuals at the stated sigmas, and keep that first line.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SinglePassRepeatedMedianMask = 1u
};

/// Whether a value names an outlier policy this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isSpectrophotometricOutlierPolicy(SpectrophotometricOutlierPolicy policy) noexcept
{
    return policy == SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit ||
           policy == SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask;
}

/// The policy's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param policy the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSpectrophotometricOutlierPolicy"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
spectrophotometricOutlierPolicyName(SpectrophotometricOutlierPolicy policy) noexcept;

/// Everything one fit is asked to do.
struct SpectrophotometricFitRequest
{
    /// The fewest usable samples a fit needs. At least one.
    std::uint32_t minimumSamples = kDefaultSpectrophotometricMinimumSamples;

    /// The smallest span of catalog ratios, per colour channel, that conditions a line. Finite and
    /// not negative.
    double minimumCatalogRatioSpan = kDefaultSpectrophotometricMinimumCatalogRatioSpan;

    /// How many robust sigmas from the line a residual may sit and be retained. Finite and positive.
    double clipSigmas = kDefaultSpectrophotometricClipSigmas;

    /// The most clipping iterations under the iterative policy. At least one.
    std::uint32_t maximumIterations = kDefaultSpectrophotometricMaximumIterations;

    /// The floor every robust scale is held at or above. Finite and positive.
    double minimumRobustScale = kDefaultSpectrophotometricMinimumRobustScale;

    /// The largest relative standard uncertainty a gain may carry and still be accepted. Finite and
    /// positive.
    double maximumRelativeGainUncertainty = kDefaultSpectrophotometricMaximumRelativeGainUncertainty;

    /// The inlier count below which an accepted fit is reported as low-support. At or above
    /// `minimumSamples`.
    std::uint32_t lowSupportSamples = kDefaultSpectrophotometricLowSupportSamples;

    /// The white reference's response gains, red, green and blue, from
    /// `whiteReferenceResponseGains`. Finite and positive. Ones anchor the fit on a flat white.
    std::array<double, 3u> whiteReferenceGains{1.0, 1.0, 1.0};

    /// How outliers are decided.
    SpectrophotometricOutlierPolicy outlierPolicy = SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit;
};

/// What the fit concluded.
enum class SpectrophotometricFitVerdict : std::uint8_t
{
    /// The gains are usable.
    Accepted = 0u,

    /// Fewer usable samples than the request's floor.
    InsufficientSamples = 1u,

    /// A colour channel's catalog ratios span less than the request's floor.
    InsufficientCatalogSpan = 2u,

    /// A colour channel kept fewer inliers than the floor, or its line could not be fitted, or the
    /// line's value at the white reference or at an inlier is not a finite positive number.
    InsufficientInliers = 3u,

    /// A gain's relative uncertainty exceeds the request's ceiling.
    ExcessiveGainUncertainty = 4u,

    /// A gain, or the normalization over the three, is not a finite positive number.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NonFiniteFit = 5u
};

/// Whether a value names a verdict this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param verdict the value to inspect.
/// @return true for every verdict, `Accepted` included.
[[nodiscard]] constexpr bool isSpectrophotometricFitVerdict(SpectrophotometricFitVerdict verdict) noexcept
{
    return static_cast<std::uint8_t>(verdict) <= static_cast<std::uint8_t>(SpectrophotometricFitVerdict::NonFiniteFit);
}

/// The verdict's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param verdict the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSpectrophotometricFitVerdict"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
spectrophotometricFitVerdictName(SpectrophotometricFitVerdict verdict) noexcept;

/// One colour channel's line and what followed from it.
///
/// Every figure is meaningful once the channel was fitted; a channel that was not keeps the
/// defaults and a zero inlier count.
struct SpectrophotometricChannelFit
{
    /// Whether the channel was fitted at all.
    bool fitted = false;

    /// The line's intercept: the image ratio at a catalog ratio of zero.
    double intercept = 0.0;

    /// The line's slope: image ratio per unit of catalog ratio.
    double slope = 0.0;

    /// The catalog ratio of the white reference, from the request's gains.
    double whiteReferenceCatalogRatio = 1.0;

    /// The line's value at the white reference: what the image measures for a neutral star.
    double fittedWhiteReferenceImageRatio = 1.0;

    /// `fittedWhiteReferenceImageRatio / whiteReferenceCatalogRatio`.
    double observedToCatalogScale = 1.0;

    /// One over the fitted white-reference image ratio, before normalization over the channels.
    double rawGain = 1.0;

    /// The gain after normalization over the three channels.
    double gain = 1.0;

    /// The standard uncertainty of `observedToCatalogScale`.
    double scaleUncertainty = 0.0;

    /// The standard uncertainty of `gain`.
    double gainUncertainty = 0.0;

    /// The median of the inliers' log residuals about the line.
    double medianLogResidual = 0.0;

    /// The root mean square of the inliers' log residuals.
    double rmsLogResidual = 0.0;

    /// The median absolute deviation of the inliers' log residuals about their median.
    double madLogResidual = 0.0;

    /// How many samples the final line was judged against and kept.
    std::uint32_t inlierCount = 0u;

    /// How many were clipped.
    std::uint32_t rejectedCount = 0u;

    /// Positions into the supplied span of the clipped samples, ascending.
    std::vector<std::uint32_t> rejectedSampleIndices{};
};

/// Everything one fit produced.
struct SpectrophotometricFitResult
{
    /// What the fit concluded. The gains are usable only under `Accepted`.
    SpectrophotometricFitVerdict verdict = SpectrophotometricFitVerdict::NonFiniteFit;

    /// The per-channel multipliers, red, green and blue, normalized so the largest is one. Ones
    /// unless the verdict is `Accepted`. The same three numbers `ColorChannelGains` in
    /// `Algorithms/Photometry/ColorCalibrationApply.h` carries, so the apply takes them as they are.
    std::array<double, 3u> gains{1.0, 1.0, 1.0};

    /// The red channel's line.
    SpectrophotometricChannelFit red{};

    /// The blue channel's line.
    SpectrophotometricChannelFit blue{};

    /// How many samples were supplied.
    std::uint32_t suppliedCount = 0u;

    /// How many supplied samples were marked accepted.
    std::uint32_t acceptedCount = 0u;

    /// How many accepted samples carried finite positive ratios on both sides and were fitted.
    std::uint32_t usableCount = 0u;

    /// The span of the usable samples' catalog red ratios.
    double catalogRedRatioSpan = 0.0;

    /// The span of the usable samples' catalog blue ratios.
    double catalogBlueRatioSpan = 0.0;

    /// The smaller of the two channels' inlier counts; zero unless the verdict is `Accepted`.
    std::uint32_t supportCount = 0u;

    /// Whether an accepted fit's support is below the request's low-support count.
    bool lowSupport = false;

    /// Positions into the supplied span of every sample either channel clipped, ascending and
    /// unique. Filled only under `Accepted`; each fitted channel's own list is in its fit.
    std::vector<std::uint32_t> rejectedSampleIndices{};

    /// Whether the gains may be applied.
    ///
    /// @return true when the verdict is `Accepted`.
    [[nodiscard]] bool accepted() const noexcept
    {
        return verdict == SpectrophotometricFitVerdict::Accepted;
    }
};

/// Fits red and blue gains relative to green from spectrophotometric colour samples.
///
/// A sample is usable when it is accepted and its six ratios are finite and positive. The request
/// is checked first; then the usable samples are counted and their catalog spans measured, and each
/// colour channel's line is fitted under the outlier policy, evaluated at the white reference and
/// turned into a raw gain with its uncertainty. The three raw gains are normalized by the largest.
/// A stage that cannot proceed sets the verdict and returns what was computed before it.
///
/// Every rank is exact and the per-point medians are independent, so the answer does not depend on
/// the admitted thread count. The per-worker slope scratch comes from the engine context's memory
/// resource; the observation lists, memberships and median buffers are ordinary heap allocations.
/// All of it is released before this returns, and the returned index lists are the only
/// allocations that outlive the call. Cancellation is checked before the fit, between clipping
/// iterations, between the two channels and inside the per-point medians. A low-support accepted
/// fit is reported through the diagnostic sink as a warning and through `lowSupport`.
///
/// @param execution the operation policy and engine-context lease.
/// @param samples the colour samples, at most `kMaxSpectrophotometricFitSamples` of them.
/// @param request the floors, the clipping rule, the white reference and the policy.
/// @return the fit and its verdict; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a request field outside its stated range;
///         `ErrorCode::LimitExceeded` above the sample ceiling; a memory-budget refusal; or
///         cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrophotometricFitResult>
fitSpectrophotometricGains(const ExecutionContext& execution, std::span<const SpectrophotometricSample> samples,
                           const SpectrophotometricFitRequest& request = {});

} // namespace an
