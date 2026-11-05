// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Denoising that decides what a sample should have been by looking at the
/// other places in the frame that look like it.
///
/// The operations beside this one, in `NoiseReduction.h`, all decide that a sample is wrong from its
/// own neighbourhood. The two here decide it from the whole frame: a patch around a pixel is compared
/// against every patch in a search window, the ones that match are collected into a group, and what
/// that group has in common is signal while what it does not is noise. That is a different contract
/// from a neighbourhood filter and it is why these two do not live in that file.
///
/// **A noise level is a number in the image's own units, and full scale is stated.** The patch
/// estimator's tuning is not scale-free: it chooses a patch size, a similar-patch count and a
/// covariance weight from tables written in the code values of an eight-bit image, and those numbers
/// are intrinsic to the published method rather than an artefact of somebody's test corpus. The
/// caller states `sigma` and `whiteLevel` in one consistent unit system, the tuning scale is formed
/// once from the two, and nothing is inferred from the pixels, so the same request over the same
/// picture means the same thing whether it is stored as UInt16 or as Float32.
///
/// **The tiling is a property of the image, not of the machine.** The tile grid comes from the extent
/// and the pass's own boundary, never from the host's core count, so the result is bitwise identical
/// at any admitted concurrency.
///
/// **Working memory is bounded before the first patch.** Every byte comes from the operation's
/// engine context in blocks sized from the plan, a refusal carries the requested, available and
/// limit counts, and what was actually held is reported.
///
/// The patch estimator is adapted from Marc Lebrun's NL-Bayes implementation published on IPOL, and
/// the guided shrinkage is written from the published DA3D method; each implementation file states
/// what it carries and under which licence.

#pragma once

#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

class ExecutionContext;

/// The scale the patch estimator's parameter tables are written on.
///
/// The published method selects its patch size, its similar-patch count and its covariance weight at
/// noise levels of ten, fifteen, twenty, thirty, thirty-five, forty, fifty, seventy and eighty. Those
/// are code values of an eight-bit image, and they are part of the method rather than a tuning this
/// engine is free to rescale. A caller's `sigma` and `whiteLevel` are mapped onto this scale once,
/// which is the whole reason `whiteLevel` has to be stated.
inline constexpr double kPatchDenoiseTuningScale = 255.0;

/// The smallest full scale a caller may state.
///
/// Below this the tuning scale is large enough that a sigma of any plausible size lands past the
/// last row of every table, which makes the request meaningless rather than merely aggressive.
inline constexpr double kMinimumPatchDenoiseWhiteLevel = 1.0e-6;

/// The largest noise level, on the tuning scale, the estimator is defined for.
///
/// The tables stop selecting at eighty and the covariance weight stops at seventy, so a request
/// above this is answering questions the method never asked. Twice the scale is well past any real
/// frame and is refused rather than clamped.
inline constexpr double kMaximumPatchDenoiseTunedSigma = 2.0 * kPatchDenoiseTuningScale;

/// The most strengthen-and-subtract iterations a request may ask for.
///
/// Each iteration is a complete two-step denoise of the frame, so eight is already an hour of work
/// on a large frame and past the point where the iteration count is the wrong parameter to raise.
inline constexpr std::uint32_t kMaximumPatchDenoiseIterations = 8u;

/// The channel counts the patch estimator is defined for.
///
/// One, three and four. The colour decorrelation the method applies before the patch search is
/// written for a monochrome frame, an RGB frame and a four-channel colour-filter-array quadruple,
/// and nothing sensible happens to a two- or five-channel frame, so one is refused by name.
inline constexpr std::uint32_t kMaximumPatchDenoiseChannels = 4u;

/// The largest number of tiles either operation divides a frame into.
///
/// A tile carries a boundary on every side that is denoised and then discarded, so tiles that are
/// small relative to their boundary spend most of their work on samples nobody keeps. This ceiling
/// is high enough that a large frame still spreads across every worker of any host this engine
/// targets, and low enough that the boundary overhead cannot run away.
inline constexpr std::uint32_t kMaximumPatchDenoiseTiles = 256u;

/// Which finishing stage runs after the patch estimator.
enum class PatchDenoiseFinishing : std::uint8_t
{
    /// The estimator's own answer is the result.
    None = 0u,

    /// The guided dual-domain stage runs over the estimator's answer, with the original frame as its
    /// noisy input and the estimate as its guide.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    GuidedDualDomain = 1u
};

/// How a guided shrinkage treats the channels of a colour frame.
enum class GuidedShrinkageChannelCoupling : std::uint8_t
{
    /// Every channel carries its own spectrum through the shrinkage, which is what the method does.
    Independent = 0u,

    /// The channels are averaged into one plane, shrunk once, and the answer is written to every
    /// channel.
    ///
    /// For a colour frame whose noise is monochromatic, on which the independent path produces
    /// coloured artefacts. It is asked for rather than decided by a scan of the pixels.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Shared = 1u
};

/// The tuning one pass of the patch estimator runs with.
///
/// Derived from a noise level on the tuning scale and a channel count, and reported rather than
/// hidden: the parameters decide what the operation costs and what it can possibly find, and a
/// caller that cannot see them cannot tell an aggressive request from a conservative one.
///
/// Trivially copyable and allocation-free.
struct PatchDenoisePassParameters
{
    /// The side of a square patch, in pixels: three, five or seven, as the tables select from the
    /// noise level, the pass and the channel count.
    std::uint32_t patchSize = 0u;

    /// How many similar patches the group holds.
    std::uint32_t similarPatches = 0u;

    /// The side of the square search window around a reference patch, in pixels. Always odd.
    std::uint32_t searchWindow = 0u;

    /// How wide a boundary a tile keeps around itself, in pixels, so that a patch near a tile edge
    /// still has a full search window.
    std::uint32_t boundary = 0u;

    /// The step between two reference patches, in pixels.
    std::uint32_t referenceStep = 0u;

    /// How far below the noise power a group's spread must fall before it is declared featureless
    /// and replaced by its mean.
    float homogeneityGamma = 0.0f;

    /// The multiple of the noise variance added to the covariance diagonal.
    float covarianceWeight = 0.0f;

    /// The distance below which a near patch is kept as similar, on the second pass. Zero on the
    /// first, which keeps a fixed count instead.
    float similarityDistance = 0.0f;

    /// Whether this pass takes the featureless shortcut at all.
    bool homogeneousArea = false;
};

/// What a caller asks the patch denoiser for.
struct PatchDenoiseRequest
{
    /// The standard deviation of the noise, **in the image's own sample units**.
    ///
    /// Read only when `measureNoise` is false. Must be finite and above zero, and must land at or
    /// below `kMaximumPatchDenoiseTunedSigma` once mapped onto the tuning scale.
    double sigma = 0.01;

    /// Whether to measure the noise level from the image instead of taking `sigma`.
    ///
    /// The estimate is the median over rows of a sigma-clipped standard deviation of first
    /// differences along each row, divided by the square root of two, averaged over channels.
    /// `estimateImageNoise` is the same computation as a call in its own right.
    bool measureNoise = false;

    /// What a fully exposed sample is, in the image's own sample units.
    ///
    /// The other half of stating a noise level. It fixes the tuning scale
    /// `kPatchDenoiseTuningScale / whiteLevel`, which is what maps a request onto the method's own
    /// tables, and the result is held inside `[0, whiteLevel]` - so a negative sample, such as one
    /// left by a background subtraction, comes back as zero. Must be finite and at least
    /// `kMinimumPatchDenoiseWhiteLevel`.
    double whiteLevel = 1.0;

    /// Whether the first pass takes the featureless-group shortcut.
    bool homogeneousAreaFirstPass = true;

    /// Whether the second pass takes it.
    bool homogeneousAreaSecondPass = true;

    /// How many strengthen-and-subtract iterations run.
    ///
    /// One is a plain denoise. Above one, each iteration mixes `reinjection` of the previous answer
    /// with the rest of the original frame, denoises that, and subtracts the mixed input back out -
    /// the published "strengthen, operate, subtract" loop. Must be between one and
    /// `kMaximumPatchDenoiseIterations`.
    std::uint32_t iterations = 1u;

    /// How much of the previous iteration's answer is mixed back in, within `(0, 1)`.
    ///
    /// Read only when `iterations` is above one. The subtraction that follows divides by
    /// `1 - reinjection`, so a value at or above one is refused rather than producing an infinity.
    double reinjection = 0.5;

    /// Whether the frame is passed through a variance-stabilising transform first.
    ///
    /// The Anscombe transform, at unit gain and with no Gaussian term, turns Poisson-dominated noise,
    /// whose variance follows the signal, into something with a constant variance the estimator's
    /// single sigma can describe; the way back is Makitalo and Foi's closed-form approximation of its
    /// exact unbiased inverse.
    /// It is refused together with `iterations` above one and with a finishing stage, because the
    /// stabilised frame is not the frame either of those is defined on.
    bool varianceStabilisation = false;

    /// Which finishing stage runs after the estimator.
    PatchDenoiseFinishing finishing = PatchDenoiseFinishing::None;

    /// The noise level the finishing stage runs at, in the image's own sample units, or zero to use
    /// the noise measured from the original frame.
    ///
    /// Read only when a finishing stage runs. The stage wants the noise level of its *noisy* input,
    /// which is the original frame rather than the estimate it is guided by, so this defaults to the
    /// measurement rather than to `sigma`.
    double finishingSigma = 0.0;

    /// What stands in for a sample the validity rule rejects, in the image's own sample units.
    ///
    /// A patch estimator has no border policy: it does not read a neighbour of a sample, it reads a
    /// rectangle around it and compares that rectangle against others, so there is nothing to
    /// substitute *per neighbour*. The rejected sample is therefore replaced once, before the
    /// estimator runs, and this is what replaces it. The sample's own destination is still left
    /// untouched on the way out, so the substitute never reaches the caller's image. Must be finite.
    double invalidSample = 0.0;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A validated patch-denoise request, with everything that does not depend on the samples derived.
///
/// Trivially copyable and allocation-free.
class PatchDenoisePlan
{
public:
    /// A plan describing no denoise, which no execution accepts.
    PatchDenoisePlan() noexcept = default;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const PatchDenoiseRequest& request() const noexcept;

    /// How many channels the frame carries.
    ///
    /// @return the channel count, which is zero for a plan describing no denoise.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t channelCount() const noexcept;

    /// What multiplies a sample in the caller's units to reach the tuning scale.
    ///
    /// @return `kPatchDenoiseTuningScale / whiteLevel`.
    [[nodiscard]] AFTERNIGHTENGINE_API double tuningScale() const noexcept;

    /// The tuning both passes run with, for one noise level on the tuning scale.
    ///
    /// Deterministic and allocation-free, so the tables can be examined without an image. A noise
    /// level that the request did not state - because it will be measured - still has an answer
    /// here, which is what lets a caller show what a measured level would cost.
    ///
    /// @param tunedSigma the noise level on the tuning scale.
    /// @param secondPass false for the basic estimate, true for the final one.
    /// @return the parameters that pass would run with. All zero for a plan describing no denoise,
    ///         or for a non-finite, negative or above-ceiling noise level.
    [[nodiscard]] AFTERNIGHTENGINE_API PatchDenoisePassParameters passParameters(double tunedSigma,
                                                                                 bool secondPass) const noexcept;

    /// How many tiles a frame of this shape is divided into, along each axis.
    ///
    /// Derived from the extent and the pass's boundary and nothing else, which is what makes the
    /// answer independent of how many threads ran it. A tile's interior is four boundaries or 256
    /// samples, whichever is more; a frame that holds fewer than sixteen such tiles takes the largest
    /// interior, down to four boundaries, that gives it sixteen, so a small frame still spreads over
    /// several workers. A frame too small to hold one tile with its boundary yields one tile in that
    /// axis.
    ///
    /// @param width the frame width in pixels.
    /// @param height the frame height in pixels.
    /// @param boundary the pass's boundary, from `passParameters`.
    /// @param tilesX how many tiles across, written on return.
    /// @param tilesY how many tiles down, written on return.
    AFTERNIGHTENGINE_API static void tileGrid(std::uint32_t width, std::uint32_t height, std::uint32_t boundary,
                                              std::uint32_t& tilesX, std::uint32_t& tilesY) noexcept;

    /// How many times the operation walks the whole image.
    ///
    /// @return two passes per iteration, plus one for the finishing stage when one runs.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<PatchDenoisePlan> planPatchDenoise(const PatchDenoiseRequest& request,
                                                                          std::uint32_t channelCount);

    /// Records pieces validated by the friend builder.
    ///
    /// @param request the noise level, the scale, the iterations and the finishing stage.
    /// @param channelCount how many channels the frame carries.
    PatchDenoisePlan(const PatchDenoiseRequest& request, std::uint32_t channelCount) noexcept;

    /// The request this plan records.
    PatchDenoiseRequest m_request{};

    /// How many channels the frame carries.
    std::uint32_t m_channelCount = 0u;

    /// What multiplies a caller's sample to reach the tuning scale.
    double m_tuningScale = 0.0;
};

/// What one patch-denoise operation did.
struct PatchDenoiseResult
{
    /// Pixel selection, writes, working memory, layout path and planned workers, summed over the
    /// channels the operation touched.
    SpatialFilterResult filter{};

    /// How many stored samples the operation actually moved.
    std::uint64_t samplesChanged = 0u;

    /// How many samples the validity rule rejected and the substitute stood in for.
    std::uint64_t substitutedSamples = 0u;

    /// The noise level measured from the frame that came in, in the caller's units, after rejected
    /// samples were replaced by `PatchDenoiseRequest::invalidSample`.
    double inputNoise = 0.0;

    /// The noise level measured from the frame that went out, in the caller's units.
    ///
    /// The number that says whether the request did anything.
    double outputNoise = 0.0;

    /// The noise level the first iteration was asked to run at, in the caller's units: the stated
    /// `sigma`, or `inputNoise` when the noise is measured.
    ///
    /// Under variance stabilisation the estimator runs instead at the level measured on the
    /// stabilised frame, which is on a different scale and is not reported.
    double effectiveSigma = 0.0;

    /// How many strengthen-and-subtract iterations ran.
    std::uint32_t iterationsRun = 0u;

    /// Whether the iteration loop stopped early because the noise estimate rose by more than one
    /// percent from one iteration to the next.
    ///
    /// The loop then keeps the previous iteration's answer. Reporting the stop is what lets a caller
    /// tell three iterations that converged from three that were abandoned after one.
    bool convergenceStopped = false;

    /// How many patch groups the estimator formed, over both passes and every tile.
    std::uint64_t patchGroups = 0u;

    /// How many groups were declared featureless and replaced by their mean.
    std::uint64_t homogeneousGroups = 0u;

    /// How many covariance matrices were not positive definite, so the group kept its noisy values.
    ///
    /// The honest measure of how far a stated sigma is from the frame's own.
    std::uint64_t inversionFailures = 0u;

    /// How many tiles the frame was divided into, on the first pass.
    std::uint32_t tileCount = 0u;

    /// Whether a finishing stage ran.
    bool finishingRan = false;

    /// How many patches the finishing stage placed.
    std::uint64_t finishingPatches = 0u;
};

/// Validates a request and derives what does not depend on the samples.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the noise level, the scale, the iterations and the finishing stage.
/// @param channelCount how many channels the frame carries.
/// @return the plan; `ErrorCode::InvalidArgument` for a non-finite or non-positive sigma when one is
///         stated, a non-finite white level or one below `kMinimumPatchDenoiseWhiteLevel`, a
///         reinjection outside `(0, 1)` when the loop runs, a non-finite or negative finishing
///         sigma, a non-finite invalid-sample substitute, a finishing or output field outside its
///         enumeration, or a write-back policy naming `DomainMapping::FullScale`;
///         `ErrorCode::OutOfRange` for zero iterations, more than `kMaximumPatchDenoiseIterations`,
///         a channel count of zero or above `kMaximumPatchDenoiseChannels`, or a stated sigma that
///         lands above `kMaximumPatchDenoiseTunedSigma` on the tuning scale, each carrying the value
///         and its limit, or for a numeric field that cannot be represented by the Float32
///         estimator; `ErrorCode::Unsupported` for a channel count of two; and
///         `ErrorCode::IncompatibleArguments` for variance stabilisation asked for together with an
///         iteration loop or a finishing stage.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PatchDenoisePlan> planPatchDenoise(const PatchDenoiseRequest& request,
                                                                             std::uint32_t channelCount);

/// Measures the background noise level of an image, in its own sample units.
///
/// Along each row of each channel, the first differences are taken, clipped three times at five
/// standard deviations, and their standard deviation kept; the median over rows, divided by the
/// square root of two, is the channel's estimate, and the channels are averaged. The estimate is
/// computed on the stored samples rather than on a rescaled copy of them. A difference participates
/// only when both original samples are finite and admitted by the validity rule. Rejected pixels
/// never create artificial edges to zero.
/// A channel with no usable differences contributes zero to the channel mean.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are.
/// @param source where the samples start. Must not be null.
/// @param layout where they sit.
/// @param selection which samples mean something.
/// @return the estimate, in the image's own units; `ErrorCode::NotInitialized` for an execution
///         that holds nothing; `ErrorCode::InvalidArgument` for a null pointer, a representation
///         this build does not know, a layout that does not describe it, or a selection whose mask
///         does not match its rule; `ErrorCode::IncompatibleArguments` for a mask that does not
///         cover the image; `ErrorCode::OutOfRange` for a frame narrower than three pixels, which
///         has no first differences to measure; or the memory resource's refusal or a cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> estimateImageNoise(const ExecutionContext& execution,
                                                                     SampleType sampleType, const void* source,
                                                                     const ImageLayout& layout,
                                                                     const SpatialSelection& selection = {});

/// Denoises an image by grouping the patches of it that look alike.
///
/// The untyped entry point, and the one the exported library carries. Two passes run over the frame.
/// The first collects, for each reference patch, the closest patches in a search window around it,
/// and replaces the group by its Bayesian estimate under the stated noise level - which is the
/// group's barycentre plus a covariance-weighted correction of each patch towards it. The second
/// repeats the search on the first pass's answer, which is quiet enough for the distances to mean
/// something, and estimates the noisy patches under the covariance the basic ones imply.
///
/// The colour decorrelation, the tile boundary, the aggregation weights and the parameter tables are
/// the published method's. What is this engine's is that the tuning scale is stated rather than
/// assumed, the tiling does not depend on the thread count, every byte of working memory comes from
/// the engine context, and a cancelled operation writes nothing.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at tile and iteration
/// boundaries, and leaves the destination untouched on every failure. After the last cancellation
/// checkpoint the final write-back is an uninterruptible commit and the operation succeeds.
///
/// Progress is reported through the execution's sink as the tiles of each estimator pass and of the
/// finishing stage finish, in a total of `PatchDenoisePlan::imagePasses`: the completed count moves as
/// each pass ends, and ends at the total when the iteration loop stops early.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its channel count must be the plan's.
/// @param destination where the denoised samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return what the operation did; `ErrorCode::InvalidArgument` for a plan that has not been
///         validated, a null pointer, or a channel count other than the plan's;
///         `ErrorCode::IncompatibleArguments` for disagreeing extents, for buffers that share a
///         byte, or for a promise Float32 cannot keep; `ErrorCode::OutOfRange` for a frame narrower
///         than three pixels, a frame too small to hold one search window at the noise level the
///         estimator runs at, a measured noise level past `kMaximumPatchDenoiseTunedSigma` on the
///         tuning scale, or, with a finishing stage, a frame smaller than its transform;
///         `ErrorCode::InvalidData` for a measured noise level that is not finite;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         or the memory resource's refusal, carrying the requested, available and limit byte
///         counts.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PatchDenoiseResult>
denoiseImagePatches(const ExecutionContext& execution, SampleType sampleType, const void* source,
                    const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                    const PatchDenoisePlan& plan, const SpatialSelection& selection = {});

/// Denoises a checked typed image view by grouping the patches of it that look alike.
///
/// The typed spelling of `denoiseImagePatches`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return whatever `denoiseImagePatches` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<PatchDenoiseResult>
denoisePatches(const ExecutionContext& execution, const ImageView<const SampleT>& source,
               const ImageView<SampleT>& destination, const PatchDenoisePlan& plan,
               const SpatialSelection& selection = {})
{
    return denoiseImagePatches(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                               destination.layout(), plan, selection);
}

/// What a caller asks the guided dual-domain stage for.
struct GuidedShrinkageRequest
{
    /// The standard deviation of the noise in the *noisy* input, in the image's own sample units.
    ///
    /// Not the noise of the guide, which is by construction lower. Must be finite and above zero.
    double sigma = 0.01;

    /// What a fully exposed sample is, in the image's own sample units.
    ///
    /// The stage's spatial and range weights are dimensionless multiples of `sigma`, so it needs no
    /// tuning scale of its own; the white level is here because the result is held inside
    /// `[0, whiteLevel]`, which is the same rule the estimator applies. Must be finite and at least
    /// `kMinimumPatchDenoiseWhiteLevel`.
    double whiteLevel = 1.0;

    /// Half the side of a patch, in pixels.
    ///
    /// The transform runs over the next power of two at or above `2 * radius + 1`, so a radius of
    /// thirty-one - the published default - gives a sixty-four by sixty-four transform. Must be at
    /// least one and at most `kMaximumGuidedShrinkageRadius`.
    std::uint32_t radius = 31u;

    /// The standard deviation of the spatial half of the patch weight, in pixels.
    ///
    /// Must be finite and above zero.
    double spatialSigma = 14.0;

    /// What multiplies the noise variance in the range half of the patch weight.
    ///
    /// Must be finite and above zero. Below one the weight is stricter than the noise, which is what
    /// keeps a patch from spanning an edge.
    double rangeFactor = 0.7;

    /// How much aggregation weight every pixel must have accumulated before the stage stops.
    ///
    /// The stage repeatedly finds the least-covered pixel and places a patch there, so this decides
    /// how many patches it places and is the whole cost control. Must be finite and above zero.
    double coverageThreshold = 2.0;

    /// How the channels of a colour frame are treated.
    GuidedShrinkageChannelCoupling channelCoupling = GuidedShrinkageChannelCoupling::Independent;

    /// What stands in for a sample the validity rule rejects, in the image's own sample units.
    ///
    /// The same rule the patch estimator applies, and for the same reason: a patch is a rectangle
    /// rather than a neighbourhood, so a rejected sample is replaced once before the stage runs and
    /// its own destination sample is left untouched afterwards. Must be finite.
    double invalidSample = 0.0;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// The widest patch half-extent the guided stage accepts.
///
/// Sixty-three gives a one-hundred-and-twenty-eight-sample transform per channel per patch, which is
/// where the transform rather than the search starts to dominate the run. A larger request is
/// refused rather than accepted and regretted.
inline constexpr std::uint32_t kMaximumGuidedShrinkageRadius = 63u;

/// A validated guided-shrinkage request, with the transform geometry derived.
///
/// Trivially copyable and allocation-free.
class GuidedShrinkagePlan
{
public:
    /// A plan describing no shrinkage, which no execution accepts.
    GuidedShrinkagePlan() noexcept = default;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const GuidedShrinkageRequest& request() const noexcept;

    /// The side of the square transform, in samples.
    ///
    /// @return the next power of two at or above `2 * radius + 1`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t transformSize() const noexcept;

    /// How many times the operation walks the whole image.
    ///
    /// @return one: the stage places patches until every pixel is covered, which is one pass in the
    ///         sense the neighbourhood family counts them.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<GuidedShrinkagePlan> planGuidedShrinkage(const GuidedShrinkageRequest& request);

    /// Records pieces validated by the friend builder.
    ///
    /// @param request the noise level, the patch geometry and the coverage threshold.
    explicit GuidedShrinkagePlan(const GuidedShrinkageRequest& request) noexcept;

    /// The request this plan records.
    GuidedShrinkageRequest m_request{};

    /// The side of the square transform, in samples.
    std::uint32_t m_transformSize = 0u;
};

/// What one guided-shrinkage operation did.
struct GuidedShrinkageResult
{
    /// Pixel selection, writes, working memory, layout path and planned workers.
    SpatialFilterResult filter{};

    /// How many stored samples the operation actually moved.
    std::uint64_t samplesChanged = 0u;

    /// How many samples the validity rule rejected and the substitute stood in for.
    std::uint64_t substitutedSamples = 0u;

    /// How many patches the stage placed, over every tile.
    std::uint64_t patchesPlaced = 0u;

    /// How many of those were placed by the low-coverage path, where the patch weight was too small
    /// for a transform to say anything and the guide was accepted as it stood.
    std::uint64_t guideAcceptedPatches = 0u;

    /// How many tiles the frame was divided into.
    std::uint32_t tileCount = 0u;

    /// The side of the square transform, in samples.
    std::uint32_t transformSize = 0u;
};

/// Validates a guided-shrinkage request and derives its transform geometry.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the noise level, the patch geometry and the coverage threshold.
/// @return the plan; `ErrorCode::InvalidArgument` for a non-finite or non-positive sigma, spatial
///         sigma, range factor or coverage threshold, a white level below
///         `kMinimumPatchDenoiseWhiteLevel`, a coupling or output field outside its enumeration, or
///         a write-back policy naming `DomainMapping::FullScale`; or `ErrorCode::OutOfRange` for a
///         radius of zero or above `kMaximumGuidedShrinkageRadius`, carrying the radius and the
///         limit, or for a numeric field or derived weight that cannot be represented by the
///         Float32 estimator.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GuidedShrinkagePlan>
planGuidedShrinkage(const GuidedShrinkageRequest& request);

/// Sharpens a denoised frame back up by shrinking the noisy frame's spectrum where the guide says
/// there is nothing.
///
/// The untyped entry point, and the one the exported library carries. The stage repeatedly finds the
/// pixel with the least aggregation weight, extracts a patch of both frames around it, fits and
/// subtracts a plane so the patch is flat, builds a bilateral weight from the guide, transforms the
/// weighted noisy patch, attenuates each frequency by how much power the guide has there against the
/// noise, transforms back and aggregates. Because it only visits pixels that are not yet covered, it
/// concentrates on detail and skips flat regions, which is what lets its patches be large.
///
/// It is public in its own right, not only as the patch estimator's finishing stage: the method
/// improves the output of any denoiser, and the guide it is handed does not have to have come from
/// this engine.
///
/// Thread-safe with respect to `execution`, both sources and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at tile and patch
/// boundaries, and leaves the destination untouched on every failure. After the last cancellation
/// checkpoint the final write-back is an uninterruptible commit and the operation succeeds.
///
/// Progress is reported through the execution's sink as each tile of the frame finishes, in a total of
/// the frame's tiles.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on all three images.
/// @param noisy where the original noisy image's samples start. Must not be null.
/// @param noisyLayout where they sit.
/// @param guide where the denoised guide's samples start. Must not be null.
/// @param guideLayout where they sit. Its extent must be the noisy image's.
/// @param destination where the result goes. Must not be null, and must not overlap either source.
/// @param destinationLayout where they sit. Its extent must be the noisy image's.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return what the operation did; `ErrorCode::InvalidArgument` for a plan that has not been
///         validated or a null pointer; `ErrorCode::IncompatibleArguments` when either source is not
///         the destination's picture, when the destination shares a byte with either source, or for
///         a promise Float32 cannot keep; `ErrorCode::OutOfRange` for a frame too small to hold one
///         transform;
///         `ErrorCode::Unsupported` for more than `kMaximumPatchDenoiseChannels` channels;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::ExternalFailure` when the transform library declines a plan; or the memory
///         resource's refusal.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GuidedShrinkageResult>
shrinkImageWithGuide(const ExecutionContext& execution, SampleType sampleType, const void* noisy,
                     const ImageLayout& noisyLayout, const void* guide, const ImageLayout& guideLayout,
                     void* destination, const ImageLayout& destinationLayout, const GuidedShrinkagePlan& plan,
                     const SpatialSelection& selection = {});

/// Sharpens a checked typed image view back up under a guide.
///
/// The typed spelling of `shrinkImageWithGuide`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param noisy the checked read-only view of the original frame.
/// @param guide the checked read-only view of the denoised frame.
/// @param destination the checked writable image view.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return whatever `shrinkImageWithGuide` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<GuidedShrinkageResult>
shrinkWithGuide(const ExecutionContext& execution, const ImageView<const SampleT>& noisy,
                const ImageView<const SampleT>& guide, const ImageView<SampleT>& destination,
                const GuidedShrinkagePlan& plan, const SpatialSelection& selection = {})
{
    return shrinkImageWithGuide(execution, kSampleTypeOf<SampleT>, noisy.data(), noisy.layout(), guide.data(),
                                guide.layout(), destination.data(), destination.layout(), plan, selection);
}

/// What every operation in this file does to the image it writes.
///
/// Patch denoising changes samples and nothing else. The extent, the representation and the channel
/// schema are the source's; a denoised frame is still a frame of the same thing; nothing moves a
/// star; and no source card and no output decision is invalidated by it.
///
/// Validity is preserved, and that is a claim about the arithmetic rather than a default: a sample
/// the rule rejects is replaced by a local substitute before the estimator runs, because a patch
/// estimator has no border policy to answer a meaningless neighbour with, and its own destination
/// sample is left untouched on the way out. The operation can therefore neither create nor destroy
/// meaninglessness.
inline constexpr OperationEffect kPatchDenoiseEffect{};

} // namespace an
