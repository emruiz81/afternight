// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Measuring a comet nucleus and finding a moving source in a sequence.
///
/// The two operations read resident samples through the same checked frame view as star detection.
/// They do not acquire a generic pixel accessor, convert an integer frame to a hidden working image,
/// or infer that zero means missing data. The frame's representation, layout and validity claim are
/// therefore the ones the caller supplied, and all eight resident representations take the same
/// widened scalar path.
///
/// Every position is an engine pixel coordinate: the first pixel's centre is (0.5, 0.5), and a
/// sample at column `c` is weighted at `c + 0.5`.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace an
{

/// Which estimate supplied a refined comet centre.
enum class CometCenterMethod : std::uint8_t
{
    /// The supplied region-of-interest centre, retained when the signal is below threshold.
    RoiCenter = 0u,

    /// The positive, background-subtracted intensity centroid.
    WeightedCentroid,

    /// A three-sample logarithmic Gaussian refinement around the local maximum.
    ///
    /// Deliberately the last enumerator, so a membership check needs no table.
    GaussianPsf
};

/// Whether a value names a comet-centre method this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the value to inspect.
/// @return true for every declared enumerator.
[[nodiscard]] constexpr bool isCometCenterMethod(CometCenterMethod method) noexcept
{
    return static_cast<std::uint8_t>(method) <= static_cast<std::uint8_t>(CometCenterMethod::GaussianPsf);
}

/// The centre method's own name.
///
/// Thread-safe and allocation-free.
///
/// @param method the method to name.
/// @return a null-terminated literal compiled into this library; `"UnknownCometCenterMethod"` for
///         an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cometCenterMethodName(CometCenterMethod method) noexcept;

/// Whether a refinement found enough signal to move the supplied centre.
enum class CometCenterOutcome : std::uint8_t
{
    /// A weighted or Gaussian centre passed the signal threshold.
    Refined = 0u,

    /// The region was measured, but its peak did not clear the requested signal-to-noise ratio.
    ///
    /// Deliberately the last enumerator, so a membership check needs no table.
    BelowThreshold
};

/// Whether a value names a centre-refinement outcome.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param outcome the value to inspect.
/// @return true for both declared outcomes.
[[nodiscard]] constexpr bool isCometCenterOutcome(CometCenterOutcome outcome) noexcept
{
    return static_cast<std::uint8_t>(outcome) <= static_cast<std::uint8_t>(CometCenterOutcome::BelowThreshold);
}

/// The largest square-region radius one centre refinement accepts, in pixels.
inline constexpr std::uint32_t kMaximumCometCenterRadius = 64u;

/// What one local centre measurement is asked to do.
struct CometCenterRefinementOptions
{
    /// Radius of the square region around the seed, in pixels. Zero or one selects one; values above
    /// sixty-four are refused.
    std::uint32_t roiRadius = 10u;

    /// The minimum peak signal divided by local robust noise. Finite and positive.
    double minimumSignalToNoise = 3.0;

    /// The largest mean full width, in pixels, eligible for Gaussian refinement. Finite and
    /// positive; a wider source retains its weighted centroid.
    double maximumCompactFullWidth = 5.0;

    /// A sample at or above this level marks the region saturated. Finite, or positive infinity to
    /// disable saturation detection.
    double saturationLevel = (std::numeric_limits<double>::infinity)();

    /// Which descriptor channel is measured, below its channel count.
    std::uint32_t channelIndex = 0u;
};

/// The local measurements and chosen centre of one comet nucleus.
struct CometCenterRefinementResult
{
    /// Whether the signal was strong enough to refine the seed.
    CometCenterOutcome outcome = CometCenterOutcome::BelowThreshold;

    /// The selected centre, in source-frame engine pixel coordinates.
    PixelCoordinate center{};

    /// The positive-signal centroid, or the seed when no positive signal existed.
    PixelCoordinate weightedCenter{};

    /// The Gaussian estimate, equal to `weightedCenter` when that route was not eligible.
    PixelCoordinate gaussianCenter{};

    /// Which estimate supplied `center`.
    CometCenterMethod method = CometCenterMethod::RoiCenter;

    /// Median of the region's border samples, in the frame's numeric domain.
    double localBackground = 0.0;

    /// Border median absolute deviation scaled to a Gaussian sigma, in the frame's numeric domain.
    double localNoise = 0.0;

    /// Peak background-subtracted signal divided by `localNoise`.
    double signalToNoise = 0.0;

    /// Mean second-moment full width at half maximum, in pixels.
    double fullWidthAtHalfMaximumPixels = 0.0;

    /// One minus the ratio of the smaller axis sigma to the larger, in [0, 1].
    double ellipticity = 0.0;

    /// Suggested circular photometry radius, in pixels.
    double apertureRadiusPixels = 0.0;

    /// Whether an admitted sample met or exceeded the requested saturation level.
    bool saturated = false;
};

/// Refines a comet centre inside one local region.
///
/// The border median and scaled median absolute deviation establish the local background and noise.
/// Positive residuals produce a weighted centroid and second moments. An unsaturated compact source
/// whose peak has two neighbours on each axis receives a logarithmic three-sample Gaussian
/// refinement. A low signal is a successful measurement with `BelowThreshold` and the unchanged
/// seed, not a failed operation.
///
/// The operation checks cancellation before validation and once per source row. It is reentrant and
/// allocates its bounded region storage from the execution's engine context.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame a checked resident frame, borrowed for the call.
/// @param seed the supplied centre in engine pixel coordinates, inside the frame's sample-centre
///        rectangle `[0.5, width - 0.5] x [0.5, height - 0.5]`.
/// @param options the region, threshold, compactness, saturation and channel choices.
/// @return the measurements; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for a default frame view, a non-finite seed, or an invalid
///         option or channel; `ErrorCode::OutOfRange` for a seed outside the sample-centre
///         rectangle; `ErrorCode::EmptySelection` when validity rejects the whole region;
///         `ErrorCode::InvalidData` for a non-finite sample the validity rule admits; or a memory,
///         cancellation or deadline failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometCenterRefinementResult>
refineCometCenter(const ExecutionContext& execution, const StarFrameView& frame, const PixelCoordinate& seed,
                  const CometCenterRefinementOptions& options = {});

/// One frame offered to blind moving-object detection.
struct CometDetectionFrame
{
    /// Stable caller identity, retained in every returned point. Identities must be unique.
    std::uint64_t frameId = 0u;

    /// Midpoint of the exposure in UTC.
    UtcInstant midpoint{0};

    /// The checked resident samples. A default view means the frame is unavailable.
    StarFrameView frame{};

    /// Whether an unavailable frame fails the set instead of being skipped.
    bool required = true;
};

/// The most frames one blind moving-object search accepts.
inline constexpr std::uint32_t kMaximumCometDetectionFrames = 512u;

/// The most connected components retained from one frame before tracks are linked.
inline constexpr std::uint32_t kMaximumCometComponentsPerFrame = 64u;

/// The most moving-object candidates one search may return.
inline constexpr std::uint32_t kMaximumCometCandidates = 1024u;

/// What blind moving-object detection is asked to find.
struct CometMovingObjectDetectionOptions
{
    /// Absolute positive residual above the temporal median required for a candidate sample.
    /// Finite and not negative.
    double minimumPositiveResidual = 1.0;

    /// Multiple of one frame's robust residual noise used as its second threshold. Finite and
    /// positive; the effective threshold is the larger of the two thresholds.
    double detectionSigma = 5.0;

    /// Smallest component residual sum divided by noise and the square root of its area. Finite
    /// and positive.
    double minimumIntegratedSignalToNoise = 5.0;

    /// Smallest eight-connected component, in pixels. At least one.
    std::uint32_t minimumComponentArea = 3u;

    /// Largest admitted component, at least `minimumComponentArea`.
    std::uint32_t maximumComponentArea = 256u;

    /// Fewest distinct frames a linked candidate must occupy. Zero or one selects two; larger values may not exceed
    /// the available frame count.
    std::uint32_t minimumDetections = 3u;

    /// Largest root-mean-square distance from a fitted linear track, in pixels. Finite and positive.
    /// A candidate is also refused when any one point lies more than one and a half times this
    /// from the track.
    double maximumTrackResidualPixels = 2.5;

    /// Smallest greatest separation between two linked positions, in pixels - the candidate's
    /// `motionPixels`. Finite and not negative.
    double minimumMotionPixels = 2.0;

    /// Most candidates returned, from one to `kMaximumCometCandidates`. Zero selects that fixed
    /// engine ceiling, so the spelling that asks for no limit still bounds the work.
    std::uint32_t maximumCandidates = 8u;

    /// Which channel is searched, below every available frame's channel count.
    std::uint32_t channelIndex = 0u;
};

/// One connected residual component participating in a moving candidate.
struct CometMovingObjectPoint
{
    /// Identity of the frame that supplied this point.
    std::uint64_t frameId = 0u;

    /// Exposure midpoint of that frame.
    UtcInstant midpoint{0};

    /// Positive-residual weighted centre, in source-frame engine pixel coordinates.
    PixelCoordinate position{};

    /// Sum of the component's positive residuals, in the frame's numeric domain.
    double integratedSignal = 0.0;

    /// Component residual sum divided by robust noise and the square root of component area.
    double integratedSignalToNoise = 0.0;

    /// Eight-connected pixels in the component.
    std::uint32_t area = 0u;
};

/// Structured detection counts for one available frame.
struct CometDetectionFrameDiagnostics
{
    /// Identity of the frame these counts describe.
    std::uint64_t frameId = 0u;

    /// Positive-residual components that passed area and integrated-signal thresholds.
    std::uint32_t componentsRetained = 0u;
};

/// One linked moving-object hypothesis.
struct CometMovingObjectCandidate
{
    /// At most one point from each frame, ordered by exposure midpoint then frame identity.
    std::vector<CometMovingObjectPoint> points{};

    /// `100 * persistence + 2 * meanIntegratedSignalToNoise + motionPixels / 2` minus ten times
    /// `rootMeanSquareResidualPixels`, used only to order this search's answer.
    double score = 0.0;

    /// Root-mean-square distance of the points from their fitted linear track, in pixels.
    double rootMeanSquareResidualPixels = 0.0;

    /// Greatest separation between two linked positions, in pixels.
    double motionPixels = 0.0;

    /// Mean integrated signal-to-noise ratio of the linked points.
    double meanIntegratedSignalToNoise = 0.0;

    /// Fraction of examined frames represented by the track, in [0, 1].
    double persistence = 0.0;
};

/// What one blind search found and what it cost.
struct CometMovingObjectDetectionResult
{
    /// Accepted hypotheses in descending score order.
    std::vector<CometMovingObjectCandidate> candidates{};

    /// Available frames actually inspected.
    std::uint32_t framesExamined = 0u;

    /// Eight-connected components whose area and integrated signal passed their thresholds.
    std::uint64_t componentsExamined = 0u;

    /// Per-frame retained-component counts in exposure order.
    std::vector<CometDetectionFrameDiagnostics> frameDiagnostics{};

    /// Largest number of engine-context bytes held by this call at once.
    std::uint64_t peakWorkingBytes = 0u;
};

/// Finds sources that move coherently through a sequence without an initial position.
///
/// Each pixel is compared with its temporal median, so static stars disappear. Positive residual
/// components are extracted per frame and linked by deterministic two-point linear hypotheses. A
/// hot pixel seen once cannot satisfy `minimumDetections`; a real moving source can. Frames must
/// share their complete image description and layout arrangement, while their validity claims may
/// differ and are honoured independently.
///
/// Work is deterministic across admitted concurrency. Cancellation is checked while the temporal
/// background, per-frame components and candidate links are built. Image-sized and temporary
/// working storage is charged to the operation's engine context; the returned value owns ordinary
/// result containers.
///
/// @param execution the operation policy and engine-context lease.
/// @param frames the sequence, in any order; returned points are ordered by time and identity.
/// @param options component thresholds, linking tolerances, result ceiling and channel.
/// @return the candidates and work counts; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for duplicate identities, a required unavailable frame,
///         invalid options, a channel the frames do not have, a `minimumDetections` above the
///         available frame count, or two available frames with one exposure midpoint;
///         `ErrorCode::IncompatibleArguments` when available frames differ in description or
///         arrangement; `ErrorCode::EmptySelection` when fewer than two frames are available;
///         `ErrorCode::LimitExceeded` above the frame or candidate ceiling;
///         `ErrorCode::InvalidData` for a non-finite sample a frame's validity rule admits; or a
///         memory, scheduling, cancellation or deadline failure. No moving candidate is a
///         successful empty result.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometMovingObjectDetectionResult>
detectMovingCometCandidates(const ExecutionContext& execution, std::span<const CometDetectionFrame> frames,
                            const CometMovingObjectDetectionOptions& options = {});

} // namespace an
