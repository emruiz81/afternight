// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Finding the stars in one frame and measuring each of them.
///
/// A detection is three steps a caller can see. `StarFrameView::create` checks the frame once and
/// borrows its samples. `planStarDetection` validates the request against the frame's description
/// without reading a sample, and says which channel and rectangle will be searched and roughly
/// what the search will hold in memory. `detectStars` then runs the candidate scan and one
/// `fitStarPsf` per surviving candidate, and returns the stars, a `StarFieldSummary` and a report of
/// counts. `measureStarChannel` and `measureStarFieldFullWidth` are named compositions for a caller
/// that wants only a channel's statistics or only the seeing figure.
///
/// Positions are engine pixel coordinates - the first pixel's centre is (0.5, 0.5) - as
/// `DetectedStar` states. The scan runs on a single-channel Float32 working plane whatever the
/// frame's representation. A sample the frame's validity rule rejects is replaced by the measured
/// sky before smoothing, so a no-data border does not become an edge in the smoothed surface; a
/// sample the rule admits and that is not a number is refused rather than smoothed.
///
/// The answer does not depend on the admitted thread count.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Stars/PsfFit.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The standard deviation, in pixels, of the smoothing the candidate scan runs against.
///
/// The square root of ten thirds: the standard deviation three box averages realize when they are
/// asked for two, because the widths they can be built from cannot reach two. The narrower kernel
/// finds more local maxima than a true Gaussian of two would, which makes it the more sensitive
/// surface: on a planted field of six thousand nine hundred stars reaching down to the detection
/// threshold the detector finds 6,303, with no false detection.
///
/// The scan's derived quantities are calibrated against it: the amplitude estimate divides by it,
/// and the fitted box radius follows from second-derivative zero crossings of a surface smoothed by
/// exactly this much.
inline constexpr double kDefaultStarSmoothingSigma = 1.8257418583505538;

/// The fraction of the frame's dynamic range above which a plateau is treated as saturated.
inline constexpr double kDefaultStarSaturationFraction = 0.7;

/// The fraction of the dynamic range within which a plateau counts as flat.
inline constexpr double kDefaultStarSaturationPlateauFraction = 0.1;

/// The number of noise standard deviations the candidate threshold sits above the sky.
///
/// Five, multiplied by the caller's `noiseSigma`, and named here once.
inline constexpr double kStarDetectionThresholdSigmas = 5.0;

/// How the candidate scan's smoothed surface is produced.
///
/// The surface decides where the local maxima are and how wide the fitted box is, so the choice
/// changes which candidates exist. It is stated rather than chosen by the engine.
enum class StarDetectionSmoothing : std::uint8_t
{
    /// The Gaussian itself, as two passes of sampled and renormalised taps.
    ///
    /// The default, at `kDefaultStarSmoothingSigma`. It is the more accurate surface and the faster
    /// one - the separable kernel dispatches to an instruction-set row where the running-sum
    /// approximation does not - and at that sigma it is also the more sensitive one.
    Gaussian = 0u,

    /// Three box averages along each axis.
    ///
    /// It realizes a standard deviation of its own rather than the one it is asked for, which is
    /// why it is not the default: asking the exact Gaussian for the realized figure gives the same
    /// sensitivity at roughly half the cost.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RepeatedBox = 1u
};

/// Whether a value names a smoothing this build produces.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param smoothing the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isStarDetectionSmoothing(StarDetectionSmoothing smoothing) noexcept
{
    return smoothing == StarDetectionSmoothing::Gaussian || smoothing == StarDetectionSmoothing::RepeatedBox;
}

/// The smoothing's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param smoothing the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownStarDetectionSmoothing"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starDetectionSmoothingName(StarDetectionSmoothing smoothing) noexcept;

/// Why a fitted candidate was not accepted as a star.
///
/// Every rejection is counted under one of these, so a caller that found too few stars can see
/// which check refused them rather than guess.
enum class StarRejectionReason : std::uint8_t
{
    /// The fit produced a non-finite full width.
    NonFiniteWidth = 0u,

    /// The fit produced a non-finite centre.
    NonFinitePosition,

    /// The background-subtracted box sum was not positive, so no magnitude exists.
    NoMagnitude,

    /// A colour frame's candidate fitted to a width at or below one pixel, which is interpolation
    /// rather than a star.
    WidthBelowColourFloor,

    /// The fit produced a width at or below zero.
    NonPositiveWidth,

    /// The axis ratio fell outside the requested roundness window.
    RoundnessOutsideWindow,

    /// The fitted width was larger than the scan's own width estimate allows.
    WidthAboveEstimate,

    /// The residual was too large a fraction of the amplitude for an unsaturated star.
    ResidualTooLarge,

    /// The amplitude fell outside the requested amplitude window.
    AmplitudeOutsideWindow,

    /// A Moffat exponent below the requested minimum, which is a profile with no wings.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    BetaBelowMinimum
};

/// How many rejection reasons this build counts.
inline constexpr std::size_t kStarRejectionReasonCount =
    static_cast<std::size_t>(StarRejectionReason::BetaBelowMinimum) + 1u;

/// Whether a value names a rejection reason this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param reason the value to inspect.
/// @return true for a declared reason; false for an out-of-range cast.
[[nodiscard]] constexpr bool isStarRejectionReason(StarRejectionReason reason) noexcept
{
    return static_cast<std::size_t>(reason) < kStarRejectionReasonCount;
}

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param reason the reason to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownStarRejectionReason"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starRejectionReasonName(StarRejectionReason reason) noexcept;

/// What a caller asks star detection for.
///
/// Every field that changes which stars come back is here, and nothing that does not: a plate scale
/// is stated on the fit request, and measuring the other channels is a separate call.
struct StarDetectionRequest
{
    /// Half the width of the box a local maximum has to dominate, in pixels. One to thirty-two.
    std::uint32_t searchRadius = 5u;

    /// How many background-noise standard deviations the detection threshold stands above the sky,
    /// before the fixed factor of five. Finite and positive.
    double noiseSigma = 1.0;

    /// The smallest axis ratio an accepted star may have. In (0, 1].
    double minimumRoundness = 0.5;

    /// The largest axis ratio an accepted star may have. At or above `minimumRoundness`, and at or
    /// below one.
    double maximumRoundness = 1.0;

    /// Which shape to fit each candidate with.
    PsfModel model = PsfModel::Gaussian;

    /// The Moffat exponent a fixed-beta fit holds, and the smallest exponent a free-beta fit may
    /// produce and still be accepted. Finite, positive, at or below `kMoffatBetaCeiling`.
    double minimumBeta = 1.5;

    /// The smallest fitted amplitude an accepted star may have, in the frame's numeric domain.
    /// Finite and not negative.
    ///
    /// The window is applied only when at least one of the two bounds is above zero; both zero
    /// means no amplitude window. Once applied, both bounds are, so a positive minimum with a zero
    /// maximum refuses every star whose amplitude is positive.
    double minimumAmplitude = 0.0;

    /// The largest fitted amplitude an accepted star may have. Finite and not negative, and at or
    /// above `minimumAmplitude` when it is positive.
    double maximumAmplitude = 0.0;

    /// How many units of solver iteration ceiling each fit is allowed. One to sixteen.
    std::uint32_t convergence = 1u;

    /// Whether the scan's shape checks and the residual check are relaxed.
    ///
    /// Skips the candidate scan's symmetry and local-amplitude tests and the fitted width-estimate
    /// and residual checks. Fitted amplitude limits, Moffat beta, finite geometry, magnitude and
    /// roundness checks remain mandatory, regardless of which optional check would have failed.
    bool relaxedChecks = false;

    /// The most stars to return, or absent for every star that passes.
    ///
    /// A present limit changes the work as well as the answer: candidates are fitted in descending
    /// order of their scan brightness estimate, in rounds, and the rounds stop as soon as the limit
    /// is met, so a frame with a hundred thousand candidates does not fit all of them to return
    /// five hundred. Must be positive and at or below `kMaxStarsPerFrame`.
    std::optional<std::uint32_t> maximumStars{};

    /// The standard deviation of the smoothing the candidate scan runs against, in pixels.
    ///
    /// Finite, above zero, and at or below sixteen. Moving it away from
    /// `kDefaultStarSmoothingSigma` changes the derived box radii and the amplitude estimates as
    /// well as the maxima: a narrower kernel finds more and fainter sources and measures each of
    /// them a little less precisely.
    double smoothingSigma = kDefaultStarSmoothingSigma;

    /// How that surface is produced.
    StarDetectionSmoothing smoothing = StarDetectionSmoothing::Gaussian;

    /// Whether the detection also measures the channel's deviation and robust scale.
    ///
    /// False by default. Those two estimators are most of the cost of a complete channel
    /// measurement and no part of a detection reads either, so a caller that wants them - frame
    /// weighting and normalization do - asks for them, and one that wants only stars does not pay
    /// for them. `measureStarChannel` produces them for any channel without detecting on it.
    bool measureRobustScale = false;

    /// The sample value that is full scale in the frame's numeric domain. Finite and positive.
    ///
    /// The dynamic range is this minus the measured sky, and the saturation decisions are
    /// fractions of that range. A caller whose frame is scaled differently states what full scale is
    /// instead of having it assumed.
    double fullScale = 1.0;

    /// Arcseconds one pixel subtends, when the caller knows it.
    ///
    /// Present fills the arcsecond widths of every star and the summary's arcsecond seeing figure.
    /// Finite and positive when present.
    std::optional<double> arcsecondsPerPixel{};

    /// The rectangle to search, or absent for the whole frame.
    ///
    /// Statistics are measured inside the same rectangle, so a region is a smaller frame rather
    /// than a window onto a larger one's sky. Must lie inside the frame and be wider than
    /// `4 * searchRadius + 8` on each side, which is also required of the whole frame when this is
    /// absent.
    std::optional<StatisticsRegion> region{};
};

/// One frame, checked once, that a detection reads.
///
/// It borrows the caller's samples for the life of the call, and it copies the two things that
/// describe them: the layout and the descriptor are held by value, so the only lifetime a caller
/// has to keep is the one it already has - the samples'. What the view proves is that the layout
/// describes the buffer in a planar or interleaved arrangement, that the storage is aligned for its
/// representation, that the frame is linear and demosaiced, and that the validity claim is coherent
/// with the descriptor; what it does not do is copy a sample.
class StarFrameView
{
public:
    /// A view holding nothing.
    StarFrameView() noexcept = default;

    /// Checks one frame and borrows its samples.
    ///
    /// @param samples where the image storage starts. Must not be null.
    /// @param bufferBytes bytes available from `samples`, covering every byte named by `layout`.
    /// @param layout where its samples sit. Copied into the view.
    /// @param descriptor what the samples mean. Copied into the view.
    /// @param validity which samples participate. `maskPlane` must be zero; the channel being
    ///        detected in selects the plane automatically.
    /// @return the checked view; `ErrorCode::InvalidArgument` for a null buffer, a layout that does
    ///         not describe the frame or is semi-planar, an insufficient byte extent, misaligned
    ///         storage, a nonzero `maskPlane`, or a validity claim whose mask is missing or
    ///         unwanted; `ErrorCode::IncompatibleArguments` for a mask that does not cover the
    ///         frame's pixels or has neither one plane nor one per channel;
    ///         `ErrorCode::Unsupported` for a nonlinear encoded domain or a mosaiced descriptor,
    ///         neither of which is a surface stars are measured on.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<StarFrameView>
    create(const void* samples, std::uint64_t bufferBytes, const ImageLayout& layout, const ImageDescriptor& descriptor,
           const KernelValidityView& validity = {});

    /// Whether this view borrows a checked frame.
    ///
    /// @return false only for a default-constructed view.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_samples != nullptr;
    }

    /// Where the samples start.
    ///
    /// @return the borrowed address, or null for an empty view.
    [[nodiscard]] const void* samples() const noexcept
    {
        return m_samples;
    }

    /// Bytes the caller proved are available from `samples()`.
    ///
    /// @return zero for an empty view, otherwise at least `layout().totalBytes()`.
    [[nodiscard]] std::uint64_t bufferBytes() const noexcept
    {
        return m_bufferBytes;
    }

    /// Where the samples sit.
    ///
    /// @return the checked layout, which this view holds. The view must be valid.
    [[nodiscard]] const ImageLayout& layout() const noexcept
    {
        return *m_layout;
    }

    /// What the samples mean.
    ///
    /// @return the checked description, which this view holds. The view must be valid.
    [[nodiscard]] const ImageDescriptor& descriptor() const noexcept
    {
        return *m_descriptor;
    }

    /// Which samples participate.
    ///
    /// @return the validity rule and optional borrowed mask.
    [[nodiscard]] const KernelValidityView& validity() const noexcept
    {
        return m_validity;
    }

private:
    /// Keeps values already checked by `create`.
    StarFrameView(const void* samples, std::uint64_t bufferBytes, const ImageLayout& layout,
                  const ImageDescriptor& descriptor, const KernelValidityView& validity) noexcept
        : m_samples(samples), m_bufferBytes(bufferBytes), m_layout(layout), m_descriptor(descriptor),
          m_validity(validity)
    {
    }

    /// The first sample, or null for an empty view.
    const void* m_samples = nullptr;

    /// Bytes available from `m_samples`.
    std::uint64_t m_bufferBytes = 0u;

    /// The layout, held by value: it describes the samples rather than borrowing anything of them,
    /// and an image hands one out as a value, so a view that pointed at one would outlive it.
    std::optional<ImageLayout> m_layout{};

    /// The description, held by value, for the reason the layout is.
    std::optional<ImageDescriptor> m_descriptor{};

    /// The borrowed validity claim.
    KernelValidityView m_validity{};
};

/// A validated detection: what was asked for, and what will actually run.
///
/// Trivially copyable and allocation-free. It exists so that the answers a caller needs before the
/// frame is read - how much working memory this will take, how wide the boxes may get, whether the
/// region is usable - are available without running anything, and so a repeated detection over a
/// sequence of like frames validates its parameters once.
class StarDetectionPlan
{
public:
    /// A plan describing no detection, which no execution accepts.
    StarDetectionPlan() noexcept = default;

    /// Whether this plan was built from a validated request.
    ///
    /// @return false only for a default-constructed plan.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_valid;
    }

    /// What was asked for.
    ///
    /// @return the validated request, unchanged.
    [[nodiscard]] const StarDetectionRequest& request() const noexcept
    {
        return m_request;
    }

    /// The rectangle that will be searched, resolved against the frame.
    ///
    /// @return the region, which is the whole frame when the request stated none.
    [[nodiscard]] const StatisticsRegion& region() const noexcept
    {
        return m_region;
    }

    /// The channel that will be detected in.
    ///
    /// @return the resolved index: the `channel` `planStarDetection` was given, or, when it was
    ///         given none, channel one - green in an RGB frame - for a frame of three or more
    ///         channels and channel zero otherwise.
    [[nodiscard]] std::uint32_t channelIndex() const noexcept
    {
        return m_channelIndex;
    }

    /// Whether the frame's representation and arrangement force a working copy of the channel.
    ///
    /// @return false only when the frame is already Float32 with one sample per pixel and tight
    ///         rows, in which case the channel's plane can be read where it lies. A validity rule
    ///         that rejects samples makes the detection copy anyway, because the rejected samples
    ///         are substituted before the plane is smoothed and nothing writes to the caller's
    ///         frame.
    [[nodiscard]] bool copiesDetectionPlane() const noexcept
    {
        return m_copiesDetectionPlane;
    }

    /// An upper bound on the detection's own engine-context blocks for any admitted concurrency.
    ///
    /// Counts two planes and widest-box fit scratch for every worker the engine permits. Actual
    /// allocations use the admitted concurrency and widest observed box. Standard-library result
    /// containers and nested operations' temporary storage are outside this bound.
    ///
    /// @return the conservative bound in bytes, including all concurrently held fit scratch.
    [[nodiscard]] std::uint64_t workingBytesUpperBound() const noexcept
    {
        return m_workingBytesUpperBound;
    }

    /// The threshold, in pixels, that the candidate scan's derived box radius is capped at.
    ///
    /// @return the smaller of `kMaxPsfBoxRadius` and half the frame's shorter side, but never below
    ///         the search radius. The scan additionally keeps each box inside the frame.
    [[nodiscard]] std::uint32_t maximumBoxRadius() const noexcept
    {
        return m_maximumBoxRadius;
    }

private:
    friend AFTERNIGHTENGINE_API Result<StarDetectionPlan> planStarDetection(const StarDetectionRequest& request,
                                                                            const ImageDescriptor& descriptor,
                                                                            const ImageLayout& layout,
                                                                            std::optional<std::uint32_t> channel);

    /// The validated request.
    StarDetectionRequest m_request{};

    /// The resolved search rectangle.
    StatisticsRegion m_region{};

    /// The resolved detection channel.
    std::uint32_t m_channelIndex = 0u;

    /// Whether a working copy of the channel is needed.
    bool m_copiesDetectionPlane = true;

    /// The working-memory ceiling.
    std::uint64_t m_workingBytesUpperBound = 0u;

    /// The derived box-radius ceiling.
    std::uint32_t m_maximumBoxRadius = 0u;

    /// Whether a validated request built this plan.
    bool m_valid = false;
};

/// Where one detection spent itself, and what it threw away.
///
/// Counts, not durations: a duration is a property of the machine and belongs in a trace event,
/// while these are properties of the frame and the parameters and are the same on every host.
struct StarDetectionReport
{
    /// Candidates kept after duplicate rejection and the frame ceiling: the ones eligible to be
    /// fitted.
    std::uint64_t candidates = 0u;

    /// Candidates the scan produced before duplicate rejection and the frame ceiling.
    std::uint64_t rawCandidates = 0u;

    /// Candidates dropped because a nearer-listed candidate already claimed their neighbourhood.
    std::uint64_t duplicateCandidates = 0u;

    /// Candidates the fit was actually run on. Below `candidates` when a star limit stopped the
    /// rounds early.
    std::uint64_t fittedCandidates = 0u;

    /// Fits `fitStarPsf` refused - too few unsaturated samples, a model that could not be evaluated
    /// at the initial guess, or a width or angle no star has.
    std::uint64_t fitFailures = 0u;

    /// Fits that stopped without a convergence test passing - at the iteration ceiling or with a
    /// collapsed trust region - which are discarded rather than accepted.
    std::uint64_t unconvergedFits = 0u;

    /// How many candidates each rejection reason refused, indexed by `StarRejectionReason`.
    std::array<std::uint64_t, kStarRejectionReasonCount> rejections{};

    /// The largest box radius any fitted candidate used, in pixels.
    std::uint32_t largestBoxRadius = 0u;

    /// Engine-context bytes of the detection's own working planes and fit scratch, which it holds
    /// together. Memory the composed statistics, smoothing and summary take is not counted here.
    std::uint64_t peakWorkingBytes = 0u;

    /// Whether the frame's candidate ceiling was reached, so the scan stopped listing.
    bool candidateCeilingReached = false;
};

/// One frame's stars, what they say about the frame, and how the detection went.
struct StarDetectionResult
{
    /// The accepted stars, ordered by ascending instrumental magnitude - brightest first - with
    /// stars that have no magnitude last, in candidate order among themselves.
    std::vector<DetectedStar> stars{};

    /// What those stars say about the frame.
    StarFieldSummary summary{};

    /// What the detection did.
    StarDetectionReport report{};
};

/// Validates a detection request against one frame's description, without reading a sample.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param request what to detect and how.
/// @param descriptor what the frame is.
/// @param layout where the frame's samples sit.
/// @param channel which channel to detect in, or absent to resolve it from the descriptor.
/// @return the plan; `ErrorCode::InvalidArgument` for an unknown model or smoothing, or a
///         floating-point request field outside its stated range; `ErrorCode::OutOfRange`, carrying
///         the value and the limit where there is one, for a search radius, convergence or star
///         limit outside its range, a channel index the frame does not have, a region that does
///         not lie inside the frame, or a searched rectangle no wider than `4 * searchRadius + 8`
///         on either side; `ErrorCode::IncompatibleArguments` when the layout does not describe the
///         descriptor in a planar or interleaved arrangement; or `ErrorCode::Unsupported` for a
///         nonlinear encoded or mosaiced frame.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarDetectionPlan>
planStarDetection(const StarDetectionRequest& request, const ImageDescriptor& descriptor, const ImageLayout& layout,
                  std::optional<std::uint32_t> channel = std::nullopt);

/// Measures the robust statistics of one channel of a frame.
///
/// The same one traversal the detector runs on its own channel, exposed by name so a caller that
/// wants the other channels' noise, deviation and scale asks for them instead of paying for them
/// on every detection. The rule, region and mask are the frame's own.
///
/// Thread-safe with respect to `execution` and the frame, which are only read.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame.
/// @param channelIndex which channel, below the frame's channel count.
/// @param region the rectangle to measure, or absent for the whole frame.
/// @return the channel's statistics; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a default frame view or a region outside the frame;
///         `ErrorCode::OutOfRange` for a channel the frame does not have;
///         `ErrorCode::EmptySelection` for an empty region or when the rule admits no sample;
///         `ErrorCode::InvalidData` for an admitted sample that is not finite; or a cancellation,
///         backend or memory-budget failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarChannelStatistics>
measureStarChannel(const ExecutionContext& execution, const StarFrameView& frame, std::uint32_t channelIndex,
                   const std::optional<StatisticsRegion>& region = std::nullopt);

/// Finds and measures the stars in one frame.
///
/// Four stages, in order: the detection channel becomes a Float32 working plane; that plane is
/// measured and smoothed; the smoothed plane is scanned in parallel bands for local maxima that
/// dominate a search box, are backed by enough bright neighbours in the unsmoothed plane, and pass
/// the scan's symmetry checks; and each surviving candidate is fitted by `fitStarPsf` and either
/// accepted or counted under a rejection reason.
///
/// The result does not depend on how many threads the operation was admitted. The scan's bands are
/// merged in scan order and duplicate rejection is applied to that merged sequence, the fits are
/// stored by candidate index rather than by completion order, and the accepted stars are ordered by
/// magnitude with ties broken by candidate index.
///
/// Thread-safe with respect to `execution` and `frame`, which are only read. The working planes and
/// the per-worker fit scratch come from the execution's engine-context memory resource and are
/// released before the call returns, on every path; the candidate lists and fit results are
/// ordinary heap containers proportional to what the scan found. Heap exhaustion returns
/// `ErrorCode::AllocationFailed` and leaves `out` unchanged. Cancellation is checked at band,
/// round and candidate boundaries; a cancelled call reports the failure and leaves `out`
/// unchanged. Progress is reported at the end of each of the four stages, as one to four of four.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame to search.
/// @param plan the validated detection. It must have been built from this frame's own
///        `descriptor()` and `layout()`: only its channel and region are checked against the frame.
/// @param out where the stars, summary and report go. Untouched unless the call succeeds.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a default frame view or plan;
///         `ErrorCode::IncompatibleArguments` when the plan's channel or region does not fit this
///         frame; `ErrorCode::EmptySelection` when the validity rule admits no sample of the
///         searched region; `ErrorCode::InvalidData` for an admitted sample that is not finite or
///         a measured sky no threshold can be built from; `ErrorCode::Unsupported` for an
///         interleaved Float64 frame, whose channel this detector does not gather; or an
///         allocation, memory-budget, cancellation or deadline failure. A frame in which no star
///         is found is a success with an empty list.
[[nodiscard]] AFTERNIGHTENGINE_API Status detectStars(const ExecutionContext& execution, const StarFrameView& frame,
                                                      const StarDetectionPlan& plan, StarDetectionResult& out);

/// Searches one channel of a frame for stars with the channel's statistics the caller already measured.
///
/// The detection above with its measurement stage taken from `measured` rather than taken again: the
/// sky, the noise and the largest sample the threshold and the dynamic range are built from, and the
/// figures the summary reports, are `measured`'s. Given what `measureStarChannel` reports for this
/// frame's plan channel and region, the result is the one the detection above gives under a request
/// that asks for the robust scale, star for star and figure for figure, so a caller that wants that
/// measurement anyway - a registration that records the robust scale beside the stars - can take it
/// on another thread while the previous frame is searched.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame to search.
/// @param plan the validated detection, built from this frame's own descriptor and layout.
/// @param measured the channel's statistics, as `measureStarChannel` reports them for the plan's
///        channel and region of this frame.
/// @param out where the stars, summary and report go. Untouched unless the call succeeds.
/// @return what the detection above returns; and `ErrorCode::IncompatibleArguments` when `measured`
///         describes another channel than the plan's.
[[nodiscard]] AFTERNIGHTENGINE_API Status detectStars(const ExecutionContext& execution, const StarFrameView& frame,
                                                      const StarDetectionPlan& plan,
                                                      const StarChannelStatistics& measured, StarDetectionResult& out);

/// The median full width of a frame's stars, in pixels, for a caller that wants only that.
///
/// A named composition of `planStarDetection`, `detectStars` and the summary, with the star limit
/// the seeing figure needs rather than every star in the frame. It exists because measuring the
/// seeing is what several callers actually want and writing it out each time invites each of them
/// to choose a different star limit.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame.
/// @param request the detection parameters. Its `maximumStars` is honoured; absent means the
///        default ceiling of five hundred and twelve.
/// @param channel which channel to measure in, or absent to resolve it from the descriptor.
/// @return the median full width in pixels; `ErrorCode::InvalidArgument` for a default frame view;
///         `ErrorCode::EmptySelection` when the frame yielded no star with a positive width;
///         otherwise every failure `planStarDetection` and `detectStars` report.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
measureStarFieldFullWidth(const ExecutionContext& execution, const StarFrameView& frame,
                          const StarDetectionRequest& request = {},
                          std::optional<std::uint32_t> channel = std::nullopt);

/// The star limit `measureStarFieldFullWidth` uses when the request states none.
inline constexpr std::uint32_t kDefaultFullWidthStarLimit = 512u;

/// What `detectStars` and the operations beside it do to the frame they read.
inline constexpr OperationEffect kStarDetectionEffect{.samples = SampleEffect::Reads};

} // namespace an
