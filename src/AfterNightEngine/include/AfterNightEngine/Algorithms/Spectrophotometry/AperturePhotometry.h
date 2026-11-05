// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Aperture photometry: one star's flux in each channel, above its own sky.
///
/// **A frame's validity is its bad-pixel mask.** The frame already says which samples mean something
/// - by rule or by packed mask - so a rejected sample inside the aperture is the frame's claim rather
/// than a second image the caller has to keep aligned. A sample the frame rejects is excluded from the
/// annulus and, when the request says so, refuses the aperture.
///
/// **Positions are engine pixel coordinates.** The centre of the first pixel is at one half, as it is
/// for every star this engine detects; a pixel's centre is therefore at its index plus one half, and a
/// position on a pixel centre has a fractional coordinate.
///
/// **Geometry is bounded so scratch can be.** The annulus outer radius is capped, which caps the
/// samples a measurement holds, which is what lets a batch measure thousands of stars on the
/// operation's threads with one counted scratch block per worker and no allocation per star.
///
/// No aperture correction is applied, and there is one uncertainty model, the shot noise of the
/// aperture sum plus the sky's scatter. Its shot term is the raw aperture sum in the frame's units,
/// so it assumes a gain of one.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// One frame, checked once, that a measurement reads. Defined in `Algorithms/Stars/StarDetection.h`.
///
/// Named rather than included, so a caller describing apertures does not inherit the detector's
/// include closure; both measurements take the frame by reference.
class StarFrameView;

/// The largest annulus outer radius a measurement accepts, in pixels.
///
/// The bound on per-star scratch: a worker holds at most `(2 * ceil(radius) + 3)^2` samples per
/// channel.
inline constexpr double kMaxApertureAnnulusOuterRadiusPixels = 64.0;

/// The fewest annulus samples that may survive clipping and still estimate a sky.
inline constexpr std::uint32_t kMinimumApertureBackgroundSamples = 8u;

/// How many sub-samples per axis decide a partially covered edge pixel's weight.
inline constexpr std::uint32_t kAperturePartialPixelSubsamples = 7u;

/// The most targets one batch measures.
inline constexpr std::uint32_t kMaxApertureTargets = 1000000u;

/// Where an aperture is centred.
enum class ApertureCentroidSource : std::uint8_t
{
    /// On the star as the detector measured it.
    DetectedStarCentroid = 0u,

    /// On the catalog position projected through the solution.
    CatalogProjectedPosition = 1u,

    /// On a centroid an integration workflow handed over.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    IntegrationHandoffCentroid = 2u
};

/// Whether a value names a centroid source this build reads.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param source the value to inspect.
/// @return true for one of the three declared values.
[[nodiscard]] constexpr bool isApertureCentroidSource(ApertureCentroidSource source) noexcept
{
    return static_cast<std::uint8_t>(source) <=
           static_cast<std::uint8_t>(ApertureCentroidSource::IntegrationHandoffCentroid);
}

/// How the aperture radius follows from the request and the star.
enum class ApertureRadiusPolicy : std::uint8_t
{
    /// The stated radius, whatever the star's width.
    FixedPixels = 0u,

    /// The star's full width times a scale, held inside a stated interval.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    FullWidthScaledClamped = 1u
};

/// Whether a value names a radius policy this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isApertureRadiusPolicy(ApertureRadiusPolicy policy) noexcept
{
    return policy == ApertureRadiusPolicy::FixedPixels || policy == ApertureRadiusPolicy::FullWidthScaledClamped;
}

/// How the sky annulus follows from the request and the star.
enum class AnnulusRadiusPolicy : std::uint8_t
{
    /// The stated inner and outer radii.
    FixedPixels = 0u,

    /// An inner radius of the star's full width times a scale, held at or above a floor, and an
    /// outer radius a stated width beyond it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    FullWidthScaled = 1u
};

/// Whether a value names an annulus policy this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isAnnulusRadiusPolicy(AnnulusRadiusPolicy policy) noexcept
{
    return policy == AnnulusRadiusPolicy::FixedPixels || policy == AnnulusRadiusPolicy::FullWidthScaled;
}

/// Why a measurement was refused.
enum class ApertureRejectionReason : std::uint8_t
{
    /// The measurement was accepted.
    None = 0u,

    /// The resolved radii do not nest - the annulus must start outside the aperture and end within
    /// `kMaxApertureAnnulusOuterRadiusPixels` - or a scaled policy reads a full width that is not
    /// finite and positive, or, in a batch, the target position is not finite.
    InvalidGeometry = 1u,

    /// The annulus reaches past the frame's edge.
    EdgeClipped = 2u,

    /// A channel's aperture holds a sample at or above its saturation threshold.
    Saturated = 3u,

    /// A channel's signal to noise is below the requested floor.
    LowSignalToNoise = 4u,

    /// A neighbour's mask reaches the aperture.
    Blended = 5u,

    /// A sample the frame's validity rejects lies inside the aperture, or an admitted one is not
    /// finite.
    BadPixel = 6u,

    /// Fewer annulus samples survived clipping than a sky needs.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    InsufficientBackgroundSamples = 7u
};

/// How many rejection reasons this build reports, `None` included.
inline constexpr std::size_t kApertureRejectionReasonCount =
    static_cast<std::size_t>(ApertureRejectionReason::InsufficientBackgroundSamples) + 1u;

/// Whether a value names a rejection reason this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param reason the value to inspect.
/// @return true for every reason, `None` included.
[[nodiscard]] constexpr bool isApertureRejectionReason(ApertureRejectionReason reason) noexcept
{
    return static_cast<std::size_t>(reason) < kApertureRejectionReasonCount;
}

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param reason the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownApertureRejectionReason"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* apertureRejectionReasonName(ApertureRejectionReason reason) noexcept;

/// How every aperture of one measurement is placed, sized and judged.
///
/// The fixed radii are validated whatever the policies say: the aperture radius must be positive,
/// the fixed annulus must start outside it and end within the cap, and the scaled bounds must be
/// ordered, even when a scaled policy never reads them.
struct AperturePhotometryRequest
{
    /// How the aperture radius follows from the star.
    ApertureRadiusPolicy radiusPolicy = ApertureRadiusPolicy::FullWidthScaledClamped;

    /// How the annulus follows from the star.
    AnnulusRadiusPolicy annulusPolicy = AnnulusRadiusPolicy::FullWidthScaled;

    /// The aperture radius under `FixedPixels`, in pixels. Finite and positive.
    double apertureRadiusPixels = 3.0;

    /// The full-width multiple under `FullWidthScaledClamped`. Finite and positive.
    double fullWidthRadiusScale = 2.0;

    /// The smallest scaled aperture radius, in pixels. Finite and positive.
    double minimumApertureRadiusPixels = 2.0;

    /// The largest scaled aperture radius, in pixels. At or above the minimum.
    double maximumApertureRadiusPixels = 10.0;

    /// The annulus inner radius under `FixedPixels`, in pixels. Above the aperture radius.
    double annulusInnerRadiusPixels = 7.0;

    /// The annulus outer radius under `FixedPixels`, in pixels. Above the inner radius and at or
    /// below `kMaxApertureAnnulusOuterRadiusPixels`.
    double annulusOuterRadiusPixels = 17.0;

    /// The full-width multiple of the scaled inner radius. Finite and positive.
    double annulusInnerFullWidthScale = 3.0;

    /// The floor on the scaled inner radius, in pixels. Finite and positive.
    double minimumAnnulusInnerRadiusPixels = 7.0;

    /// The scaled annulus width, in pixels. Finite and positive.
    double annulusWidthPixels = 10.0;

    /// How many standard deviations from the annulus mean a sky sample may sit and be retained.
    /// Finite and positive.
    double annulusClipSigmas = 3.0;

    /// The radius a neighbour without a usable one of its own masks, in pixels, and the radius every
    /// other target of a batch masks. Finite and not negative.
    double neighborMaskRadiusPixels = 6.0;

    /// The smallest signal to noise every channel must reach, or zero for no floor. Finite and not
    /// negative.
    double minimumSignalToNoise = 0.0;

    /// Per-channel saturation thresholds in the frame's numeric domain, or absent for none. A
    /// present value is finite.
    std::array<std::optional<double>, 3u> saturationThresholds{};

    /// Whether an edge pixel contributes by its covered fraction rather than all or nothing.
    bool usePartialPixels = true;

    /// Whether a saturated channel refuses the measurement.
    bool rejectSaturated = true;

    /// Whether a neighbour reaching the aperture refuses the measurement.
    bool rejectBlends = false;

    /// Whether a rejected sample inside the aperture refuses the measurement. False excludes the
    /// sample and its area instead.
    bool rejectBadPixels = true;

    /// Whether an annulus past the frame's edge refuses the measurement.
    bool rejectEdgeClipped = true;
};

/// One other star whose light must not enter the sky annulus.
struct ApertureNeighbor
{
    /// Where the neighbour is, in engine pixel coordinates.
    PixelCoordinate position{};

    /// How far its mask reaches, in pixels, or absent for the request's default. A value that is
    /// not finite and positive also takes the default.
    std::optional<double> maskRadiusPixels{};
};

/// One star to measure.
struct ApertureTarget
{
    /// Where the aperture is centred, in engine pixel coordinates.
    PixelCoordinate position{};

    /// The star's full width at half maximum, in pixels. Finite and positive when a scaled policy
    /// reads it.
    double fullWidthPixels = 0.0;
};

/// What one channel measured.
struct ApertureChannelMeasurement
{
    /// The aperture sum above the local sky, in the frame's numeric domain.
    double flux = 0.0;

    /// The sky level, the median of the clipped annulus.
    double localBackground = 0.0;

    /// The flux's standard uncertainty: the aperture sum's shot term, the sky variance over the
    /// aperture area and the sky estimate's own variance, in quadrature. The shot term is the raw
    /// aperture sum, floored at zero, so it assumes a gain of one.
    double uncertainty = 0.0;

    /// `flux / uncertainty`.
    double signalToNoise = 0.0;

    /// Whether a sample inside the aperture reached the channel's saturation threshold.
    bool saturated = false;
};

/// What one measurement produced.
struct AperturePhotometryMeasurement
{
    /// Where the aperture was centred.
    PixelCoordinate position{};

    /// The aperture radius that was used, in pixels.
    double apertureRadiusPixels = 0.0;

    /// The annulus inner radius that was used.
    double annulusInnerRadiusPixels = 0.0;

    /// The annulus outer radius that was used.
    double annulusOuterRadiusPixels = 0.0;

    /// How many samples the clipped annulus retained, in the first channel.
    std::uint32_t backgroundSampleCount = 0u;

    /// How many neighbours' masks reached the aperture.
    std::uint32_t overlappingNeighborCount = 0u;

    /// The covered aperture area, in pixels.
    double apertureArea = 0.0;

    /// The three channels, red, green and blue.
    std::array<ApertureChannelMeasurement, 3u> channels{};

    /// Why the measurement was refused, or `None`.
    ApertureRejectionReason rejection = ApertureRejectionReason::None;
};

/// Measures one star.
///
/// The radii follow the policies; the annulus takes every admitted finite sample between the inner
/// and outer radii that no neighbour masks, clips it once about its mean at the requested sigmas,
/// and estimates each channel's sky as the clipped median. The aperture sums every pixel whose
/// covered weight is positive, refuses or excludes a rejected sample as requested, and reports the
/// sky-subtracted flux, its uncertainty and their ratio. A refusal is a measurement whose
/// `rejection` names the reason, not a failure: a star that cannot be measured is data about the
/// field.
///
/// Thread-safe with respect to `execution` and `frame`, which are only read. Runs on the calling
/// thread and checks cancellation once, before it measures. Scratch comes from the engine
/// context's memory resource and is released before this returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked three-channel frame.
/// @param target the star to measure.
/// @param neighbors the stars whose light must not enter the annulus.
/// @param request how apertures are placed, sized and judged.
/// @return the measurement; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for an invalid frame, a request field outside its range or
///         a target that is not finite; `ErrorCode::Unsupported` for a frame that is not
///         three-channel RGB; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AperturePhotometryMeasurement>
measureAperturePhotometry(const ExecutionContext& execution, const StarFrameView& frame, const ApertureTarget& target,
                          std::span<const ApertureNeighbor> neighbors, const AperturePhotometryRequest& request = {});

/// Whether the other targets of a batch mask one another's annuli.
enum class ApertureNeighborPolicy : std::uint8_t
{
    /// Every other target is a neighbour of each.
    OtherTargets = 0u,

    /// Nothing is masked.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    None = 1u
};

/// Whether a value names a neighbour policy this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isApertureNeighborPolicy(ApertureNeighborPolicy policy) noexcept
{
    return policy == ApertureNeighborPolicy::OtherTargets || policy == ApertureNeighborPolicy::None;
}

/// What one batch did.
struct AperturePhotometryBatchResult
{
    /// How many targets were accepted.
    std::uint32_t acceptedCount = 0u;

    /// How many targets were refused, for any reason.
    std::uint32_t rejectedCount = 0u;

    /// How many targets each reason refused, indexed by `ApertureRejectionReason`.
    std::array<std::uint32_t, kApertureRejectionReasonCount> rejections{};

    /// Peak engine-context bytes held for worker scratch.
    std::uint64_t workingBytes = 0u;
};

/// Measures many stars on the operation's threads.
///
/// Each target is measured exactly as `measureAperturePhotometry` would measure it, with the other
/// targets as its neighbours when the policy says so, and its measurement is written at its own
/// position in `out`. One difference: a target whose position is not finite is a measurement
/// refused as `ApertureRejectionReason::InvalidGeometry` here, where the single call fails. Scratch
/// is one counted block per worker slot, taken once for the batch. The result does not depend on
/// how many threads ran.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked three-channel frame.
/// @param targets the stars to measure, at most `kMaxApertureTargets` of them.
/// @param request how apertures are placed, sized and judged.
/// @param neighborPolicy whether the other targets mask each annulus.
/// @param out receives one measurement per target and must have exactly the same length. It is
///        untouched by a failure found before measuring begins; a cancellation during the batch
///        leaves the measurements already made written and the rest untouched.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for an invalid frame, a request field outside its range or a
///         neighbour policy outside its range; `ErrorCode::Unsupported` for a frame that is not
///         three-channel RGB; `ErrorCode::EmptySelection` for no targets;
///         `ErrorCode::LimitExceeded` above the target ceiling; `ErrorCode::IncompatibleArguments`
///         for an output-length mismatch; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AperturePhotometryBatchResult>
measureAperturePhotometryBatch(const ExecutionContext& execution, const StarFrameView& frame,
                               std::span<const ApertureTarget> targets, const AperturePhotometryRequest& request,
                               ApertureNeighborPolicy neighborPolicy, std::span<AperturePhotometryMeasurement> out);

/// What both measurements do to the frame they read.
inline constexpr OperationEffect kAperturePhotometryEffect{.samples = SampleEffect::Reads};

} // namespace an
