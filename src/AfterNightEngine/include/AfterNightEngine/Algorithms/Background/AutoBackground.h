/// @file
/// Finding the background of a frame nobody has placed a sample on.
///
/// `Algorithms/Background/BackgroundModel.h` fits a surface to sites a caller chose. This file
/// answers the other question: given only the frame, which of its samples are background, and what
/// surface do they describe? Nothing is placed by hand, so the operation has to decide for itself
/// what belongs to the sky and what belongs to the objects, and it does that by fitting, measuring
/// how far each sample sits from the fit, keeping the ones a robust rejection rule accepts, and
/// fitting again until the accepted set stops moving.
///
/// **The estimate runs on a downsampled picture and the correction does not.** An illumination
/// gradient is by definition the lowest spatial frequency in the frame, so estimating it at full
/// resolution measures the same surface a great deal more slowly. The working picture is the frame
/// reduced by a stated integer factor with an area average, the surface is estimated there, and the
/// correction reads that small surface through a bilinear interpolation rather than a
/// full-resolution copy of it. That is what "fused" means here: at no point does a picture-sized
/// model plane exist unless the caller asked for one.
///
/// **Structure protection is a stated rule, not a heuristic that fires silently.** A galaxy large
/// enough to cover a quarter of the frame is not an outlier that a sigma rule will reject; it is a
/// smooth bright region the model will happily follow, and following it removes the galaxy. So the
/// residual above a stated threshold is grown by a stated amount and the result excluded from every
/// later fit, and the operation reports how many samples that removed.
///
/// **What stopped the iteration is reported.** A run that converged and a run that reached its
/// iteration ceiling produce a surface and mean different things, and a caller that cannot tell them
/// apart cannot choose an iteration count.
///
/// **The validity rule is resolved at the door.** A sample the rule rejects takes no part in the
/// block average that produces its working sample, and a working sample whose block holds no admitted
/// sample takes the channel's mean of the samples the rule admits. Everything after the reduction is
/// arithmetic over a picture with no meaningless samples in it.
///
/// **Derived from Siril's AutoGradientRemoval.** The automatic estimate is a derived work of
/// AutoGradientRemoval 1.0.0, a script of the Siril project by Cyril Richard
/// (https://gitlab.com/free-astro/siril-scripts, `processing/AutoGradientRemoval.py`):
/// Copyright (C) 2026 Cyril Richard, distributed under the GNU General Public License, version 3
/// or (at your option) any later version, GPL-3.0-or-later. The method is the script's - the
/// reduction by block averages, the inpainted low-pass and polynomial models, the rejection around
/// the median and the scaled median absolute deviation of the residuals, the structure protection
/// and the bilinear return to full resolution. What is this engine's is the restatement over its planes,
/// the validity rule resolved at the reduction, the reported figures, the correction fused into the
/// write, the surface and the protection drawn from the fit the correction uses, the threshold in the
/// image's own units, and the protection kept across rounds where the script recomputes it each
/// round. This header and its implementation are distributed under the same licence, and the credit
/// for the method is its author's;
/// `docs/dev/AUTO_GRADIENT_REMOVAL_PROVENANCE.md` records the revision that was reviewed.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Algorithms/Background/BackgroundModel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <span>

namespace an
{

class ExecutionContext;

/// The most robust-rejection iterations one estimate may run.
///
/// Past this the accepted set is not going to settle, and a run that needs more iterations needs a
/// different model or a different rejection than more of the same one.
inline constexpr std::uint32_t kMaxAutoBackgroundIterations = 64u;

/// The coarsest working picture an estimate may reduce the frame to, as a linear factor.
///
/// Eight, which is sixty-four times fewer samples. The factor must be a power of two. Where it does
/// not divide the frame's extent, the last block of a row or column is partial and averages only the
/// samples it covers.
inline constexpr std::uint32_t kMaxAutoBackgroundDownsample = 8u;

/// The largest fraction of the working picture's shorter side the model may smooth over, in per
/// cent.
///
/// Ten. Above it the low-pass is averaging over a fifth of the picture and has stopped describing a
/// gradient.
inline constexpr double kMaxAutoBackgroundScale = 10.0;

/// The smallest such fraction.
///
/// Below one per cent the low-pass follows the stars rather than the sky.
inline constexpr double kMinAutoBackgroundScale = 1.0;

/// The largest multiple of the model radius the final smoothing may use.
inline constexpr double kMaxAutoBackgroundSmoothness = 3.0;

/// How many box averages one low-pass applies along each axis.
///
/// Three, for the reason the Gaussian family gives for the same number: one box is visibly square,
/// two are triangular, and three are within a few parts in a thousand of a Gaussian of the same
/// second moment.
inline constexpr std::uint32_t kAutoBackgroundLowpassPasses = 3u;

/// How many times a masked low-pass refills the rejected samples before the model is read out.
///
/// Each fill replaces the rejected samples by the current smoothing and smooths again, which is how
/// a hole left by a rejected object is filled by the illumination around it rather than by the
/// object itself. Ten is well past the point where another fill moves the surface.
inline constexpr std::uint32_t kAutoBackgroundFillIterations = 10u;

/// The smallest fraction of the working picture a rejection rule may leave accepted.
///
/// A rejection that keeps almost nothing has stopped describing the background and started
/// describing one corner of it. So when a round leaves fewer samples accepted than this fraction of
/// the working picture, or than `kMinAutoBackgroundKeptSamples` if that is more, the accepted set is
/// replaced by the samples whose residual against the current surface is at or below the percentile
/// that count marks, less any the structure protection has excluded.
inline constexpr double kMinAutoBackgroundKeptFraction = 0.02;

/// The fewest samples a rejection rule may leave accepted, whatever the fraction says.
inline constexpr std::uint32_t kMinAutoBackgroundKeptSamples = 16u;

/// Which surface an automatic estimate fits to the samples it accepts.
enum class AutoBackgroundModel : std::uint8_t
{
    /// A repeated box low-pass over the accepted samples, with the rejected ones filled in from
    /// their surroundings first.
    ///
    /// It follows the illumination wherever it actually goes, including shapes no polynomial of a
    /// sensible degree can represent - a corner glow, an amplifier glow, a trail. The default, and the
    /// reason this domain has a low-pass of its own: the smoothing radius here is
    /// a fraction of the picture rather than a filter radius, which is outside what the stencil
    /// family's tap arrays are declared for, and a moving-window average is the one form whose cost
    /// does not grow with it.
    InpaintedLowpass = 0u,

    /// A polynomial of a stated total degree, fitted by least squares to every accepted sample.
    ///
    /// Stiffer, so it cannot follow an object the rejection failed to remove, and correspondingly
    /// unable to describe an illumination that is not a low-order surface.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Polynomial = 1u
};

/// Whether a value names an automatic model this build implements.
///
/// @param model the value to inspect.
/// @return true for both models.
[[nodiscard]] constexpr bool isAutoBackgroundModel(AutoBackgroundModel model) noexcept
{
    return model == AutoBackgroundModel::InpaintedLowpass || model == AutoBackgroundModel::Polynomial;
}

/// The name of one automatic model, for a diagnostic a person reads.
///
/// @param model which model.
/// @return a static string, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* autoBackgroundModelName(AutoBackgroundModel model) noexcept;

/// What a caller asks an automatic background estimate for.
struct AutoBackgroundRequest
{
    /// Which surface is fitted to the accepted samples.
    AutoBackgroundModel model = AutoBackgroundModel::InpaintedLowpass;

    /// How far the low-pass reaches, as a percentage of the working picture's shorter side.
    ///
    /// Within `[kMinAutoBackgroundScale, kMaxAutoBackgroundScale]`. The plan's model radius is this
    /// percentage of the working picture's shorter side, rounded and at least one pixel. Read by
    /// `AutoBackgroundModel::InpaintedLowpass`, by the final smoothing of either model, and by the
    /// structure protection's growth.
    double scale = 5.0;

    /// How far the final smoothing reaches, as a multiple of the model radius.
    ///
    /// Within `[0, kMaxAutoBackgroundSmoothness]`; zero smooths nothing. It exists because the
    /// estimate is a robust fit rather than an interpolation, so the surface it lands on can carry
    /// a small step where the accepted set changed, and a step in a background model is visible in
    /// the corrected frame.
    double smoothness = 1.0;

    /// The linear factor the frame is reduced by before the surface is estimated.
    ///
    /// A power of two within `[1, kMaxAutoBackgroundDownsample]`. One estimates at full resolution,
    /// which costs a great deal more and measures the same surface.
    std::uint32_t downsample = 4u;

    /// The total degree of the polynomial, read by that model alone.
    ///
    /// Within `[1, kMaxBackgroundPolynomialDegree]`.
    std::uint32_t polynomialDegree = 2u;

    /// Whether bright extended structure is grown and excluded from every fit after the first.
    bool protectStructures = true;

    /// How far above the residual's own centre a sample has to sit to seed the protected region, in
    /// the image's own units.
    ///
    /// Must be finite and not negative. It is in the units the frame is stored in, like every other
    /// threshold in this engine.
    double protectionThreshold = 0.05;

    /// How far the seeded region is grown, within `[0, 1]`.
    ///
    /// It sets both the radius the seeds are spread over and the fraction of that spread which
    /// still counts as protected, so one number moves the protection from "the bright cores" to
    /// "the object and its halo".
    double protectionAmount = 0.5;

    /// How many rejection iterations to run, at most.
    ///
    /// Within `[1, kMaxAutoBackgroundIterations]`.
    std::uint32_t maxIterations = 20u;

    /// The fraction of the working picture whose acceptance may change between two iterations and
    /// still count as settled.
    ///
    /// Must be finite and not negative; zero runs every iteration. The run stops after the first
    /// iteration other than the first in which fewer than this fraction of the working samples
    /// changed membership, so a run allowed a single iteration never reports convergence.
    double convergence = 1.0e-4;

    /// How many robust standard deviations below the residual's centre a sample may sit and still
    /// be accepted.
    ///
    /// The centre is the median of the residuals the previous round accepted, and the deviation is
    /// their median absolute deviation from it scaled by `kMedianAbsoluteDeviationToSigma`.
    ///
    /// Must be finite and above zero. It is larger than the upper multiplier by default and
    /// deliberately: the background is what is left when the objects are removed, and objects are
    /// bright, so the distribution being rejected from is one-sided.
    double lowRejection = 4.0;

    /// How many such deviations above that centre a sample may sit.
    ///
    /// Must be finite and above zero.
    double highRejection = 2.0;

    /// How the estimated surface is removed and what replaces it.
    ///
    /// `BackgroundLevel::SampleMean` is refused, because an automatic estimate places no sites, and
    /// `BackgroundLevel::ModelMedian` is the median of the surface over the working picture. The
    /// evaluation stride is not read: an automatic model is a picture rather than a formula and is
    /// always interpolated from the working picture it was estimated on. The output policy is
    /// checked by the estimate and the removal rather than by `planAutoBackground`.
    BackgroundApplyRequest apply{};
};

/// A validated automatic-background request, with everything the picture decides resolved.
///
/// Trivially copyable and allocation-free.
class AutoBackgroundPlan
{
public:
    /// A plan describing no estimate, which no execution accepts.
    AutoBackgroundPlan() noexcept = default;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const AutoBackgroundRequest& request() const noexcept;

    /// How many pixels one row of the working picture holds.
    ///
    /// @return the reduced width, or zero for a plan describing no estimate.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t workingWidth() const noexcept;

    /// How many rows the working picture holds.
    ///
    /// @return the reduced height, or zero for a plan describing no estimate.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t workingHeight() const noexcept;

    /// How many pixels one row of the frame holds.
    ///
    /// @return the full width, or zero for a plan describing no estimate.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t width() const noexcept;

    /// How many rows the frame holds.
    ///
    /// @return the full height, or zero for a plan describing no estimate.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t height() const noexcept;

    /// The factor the frame is actually reduced by.
    ///
    /// It is the requested factor unless either side of the frame is shorter than it, in which case
    /// it is the largest smaller power of two that neither side is shorter than.
    ///
    /// @return the factor, or zero for a plan describing no estimate.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t downsample() const noexcept;

    /// How far the low-pass reaches on the working picture, in pixels.
    ///
    /// @return the radius, which is at least one for a plan describing an estimate.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t modelRadius() const noexcept;

    /// How far the final smoothing reaches on the working picture, in pixels.
    ///
    /// @return the radius, which is zero when no smoothing runs.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t smoothingRadius() const noexcept;

    /// How many coefficients the polynomial model would fit.
    ///
    /// @return the term count, or zero under `AutoBackgroundModel::InpaintedLowpass`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t termCount() const noexcept;

    /// How many times the operation walks the working picture, at most.
    ///
    /// The number that says what a plan costs before a clock is read. It counts the low-pass passes
    /// of every fill of every iteration, which is what dominates the estimate, and it is an upper
    /// bound because a run that converges early does fewer.
    ///
    /// @return the pass count.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t workingPasses() const noexcept;

    /// How many bytes of context memory an estimate over this picture holds at once.
    ///
    /// Six Float32 planes of the working picture, two one-byte flag planes (three with structure
    /// protection), one Float64 per working sample for the rank statistics and, for the polynomial
    /// model, its per-chunk normal-equation accumulators. The channels are estimated one at a time
    /// and share all of it, so the figure does not grow with the channel count. The number a caller
    /// checks against a budget before starting.
    ///
    /// @return the byte count, or zero for a plan describing no estimate.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t workingBytes() const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<AutoBackgroundPlan>
    planAutoBackground(const AutoBackgroundRequest& request, std::uint32_t width, std::uint32_t height);

    /// Records pieces validated by the friend builder.
    ///
    /// @param request the model, the scale and the rejection rules.
    /// @param width how many pixels one row of the frame holds.
    /// @param height how many rows the frame holds.
    /// @param downsample the resolved reduction factor.
    AutoBackgroundPlan(const AutoBackgroundRequest& request, std::uint32_t width, std::uint32_t height,
                       std::uint32_t downsample) noexcept;

    /// The request this plan records.
    AutoBackgroundRequest m_request{};

    /// How many pixels one row of the frame holds.
    std::uint32_t m_width = 0u;

    /// How many rows the frame holds.
    std::uint32_t m_height = 0u;

    /// The resolved reduction factor.
    std::uint32_t m_downsample = 0u;

    /// How many pixels one row of the working picture holds.
    std::uint32_t m_workingWidth = 0u;

    /// How many rows the working picture holds.
    std::uint32_t m_workingHeight = 0u;

    /// The low-pass radius on the working picture.
    std::uint32_t m_modelRadius = 0u;

    /// The final smoothing radius on the working picture.
    std::uint32_t m_smoothingRadius = 0u;

    /// How many polynomial coefficients the model holds.
    std::uint32_t m_termCount = 0u;

    /// Whether this plan was built by a validated request.
    bool m_valid = false;
};

/// Validates an automatic-background request against a picture.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the model, the scale, the protection and the rejection rules.
/// @param width how many pixels one row of the frame holds. Must not be zero.
/// @param height how many rows the frame holds. Must not be zero.
/// @return the plan; `ErrorCode::InvalidArgument` for a zero extent, a model, correction or level
///         rule outside its enumeration, `BackgroundLevel::SampleMean`, a number that is not
///         finite, a negative protection threshold or convergence bound, a rejection multiplier or
///         division guard that is not above zero, a negative dither, a clamp whose endpoints are out
///         of order, or a reduction factor that is zero, above `kMaxAutoBackgroundDownsample` or not
///         a power of two; `ErrorCode::OutOfRange` for a scale, smoothness or protection amount
///         outside its interval, an iteration count outside `[1, kMaxAutoBackgroundIterations]`, a
///         polynomial degree outside `[1, kMaxBackgroundPolynomialDegree]` under that model, or a
///         scale whose model radius exceeds sixteen times `kMaxBackgroundEvaluationStride` pixels.
///         The refusals of a reduction factor, an iteration count, a degree, a model radius, and a
///         scale or smoothness above its ceiling carry the requested value and the limit, the last
///         two truncated to whole numbers.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AutoBackgroundPlan>
planAutoBackground(const AutoBackgroundRequest& request, std::uint32_t width, std::uint32_t height);

/// What one automatic background estimate did.
struct AutoBackgroundResult
{
    /// Pixel selection and writes, summed over the channels.
    ///
    /// An estimate counts every sample of every channel as selected and every model-plane sample as
    /// written; a removal counts the samples the validity rule admitted, which are the ones it wrote.
    KernelRunResult run{};

    /// How many channels were estimated.
    std::uint32_t channelCount = 0u;

    /// The most iterations any one channel ran.
    std::uint32_t iterationsRun = 0u;

    /// Whether every channel stopped because its accepted set settled rather than because it ran
    /// out of iterations.
    bool converged = false;

    /// How many working samples the rejection rule considered, which is the working picture's size
    /// summed over the channels.
    std::uint64_t consideredSamples = 0u;

    /// How many of them the last iteration accepted as background, summed over the channels.
    ///
    /// The number that says whether the estimate saw a sky: a fraction near one is an empty field,
    /// and a fraction near the floor is a frame the rejection could barely find a background in.
    std::uint64_t acceptedSamples = 0u;

    /// How many working samples the structure protection excluded, summed over the channels.
    std::uint64_t protectedSamples = 0u;

    /// How many rounds had to fall back to the darkest fraction because the rejection interval left
    /// too little accepted, summed over the channels.
    std::uint32_t rejectionFallbacks = 0u;

    /// The level a removal put back, one entry per channel and the rest left at zero. An estimate
    /// leaves every entry at zero.
    std::array<double, kMaxBackgroundChannels> appliedLevel{};

    /// How many stored samples a removal actually moved; zero for an estimate.
    std::uint64_t samplesChanged = 0u;

    /// How many divisions met the guard rather than the surface; zero for an estimate and for every
    /// subtraction.
    std::uint64_t guardedSamples = 0u;

    /// How many corrected values a stated clamp had to move; zero for an estimate.
    std::uint64_t clampedSamples = 0u;

    /// How many pixels one row of the working picture held.
    std::uint32_t workingWidth = 0u;

    /// How many rows it held.
    std::uint32_t workingHeight = 0u;

    /// How many times the operation walked the working picture.
    std::uint64_t workingPasses = 0u;

    /// How many times it walked the frame itself.
    std::uint64_t imagePasses = 0u;

    /// Context memory held at once, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// The width the arithmetic ran at.
    ///
    /// The working planes are Float32, as everywhere else in this family; the fit's own linear
    /// algebra accumulates in Float64, which is a property of that solve rather than of the picture.
    ComputePrecision precision = ComputePrecision::Float32;
};

/// Estimates the background of an image and draws it into caller-owned planes.
///
/// The untyped entry point, and the one the exported library carries. Each channel is reduced,
/// estimated, and written out at full resolution through a bilinear interpolation of the working
/// surface between block centres, held at the edge value beyond the outermost centres. What a
/// caller asks for when it wants to see the gradient, keep it, or apply it to another frame of the
/// same session.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at iteration, pass and
/// row-block boundaries, and is not failure-atomic with respect to the planes it writes.
///
/// Progress is reported through the execution's sink after the first fit and after every rejection
/// round of every channel, in a total of the request's round ceiling plus one for every channel, and
/// each channel's share ends full when its fit converges early.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the source samples are.
/// @param source where they start. Must not be null.
/// @param sourceLayout where they sit. Its extent must be the plan's picture.
/// @param plan the validated request.
/// @param models one plane per channel, in channel order, each of the frame's extent.
/// @param protection one plane per channel for the grown structure mask, in channel order, or an
///        empty span for a run that produces none. Each working sample the protection excluded is
///        written as one over the whole block it stands for, and every other sample as zero. A
///        request that does not protect structures leaves these planes untouched.
/// @param selection which samples mean something.
/// @return what the estimate did; `ErrorCode::NotInitialized` for an empty execution or a plan
///         `planAutoBackground` did not produce; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` for a stopped execution; `ErrorCode::InvalidArgument` for a
///         representation this build does not know, a null source, a layout whose element size is
///         not the representation's, an explicit-mask rule without its mask or a mask given to a
///         rule that reads none, or a write-back policy in the request that is malformed or not
///         `DomainMapping::CodeValue`; `ErrorCode::IncompatibleArguments` for an output promise
///         Float32 arithmetic cannot keep, a mask that does not cover the image, a picture that is
///         not the plan's, an image of more than `kMaxBackgroundChannels` channels, a model or
///         protection plane count that is not the channel count, or a plane that is not the
///         frame's extent; `ErrorCode::EmptySelection` when the validity rule leaves a channel with
///         no sample to estimate from, or the structure protection leaves a round with no working
///         sample to fit; `ErrorCode::InvalidData` when the polynomial model's accepted samples
///         determine no surface; or the engine context's refusal of the working memory.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AutoBackgroundResult>
estimateImageAutoBackground(const ExecutionContext& execution, SampleType sampleType, const void* source,
                            const ImageLayout& sourceLayout, const AutoBackgroundPlan& plan,
                            std::span<const PlaneView<float>> models, std::span<const PlaneView<float>> protection = {},
                            const SpatialSelection& selection = {});

/// Estimates the background of a checked typed image view.
///
/// The typed spelling of `estimateImageAutoBackground`, which owns every check and every
/// computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param plan the validated request.
/// @param models one plane per channel, in channel order.
/// @param protection one plane per channel for the structure mask, or an empty span.
/// @param selection which samples mean something.
/// @return whatever `estimateImageAutoBackground` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<AutoBackgroundResult>
estimateAutoBackground(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                       const AutoBackgroundPlan& plan, std::span<const PlaneView<float>> models,
                       std::span<const PlaneView<float>> protection = {}, const SpatialSelection& selection = {})
{
    return estimateImageAutoBackground(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), plan, models,
                                       protection, selection);
}

/// Estimates the background of an image and removes it, without ever drawing it at full resolution.
///
/// The untyped entry point, and the one the exported library carries. The estimate is the one
/// `estimateImageAutoBackground` makes; the correction reads the working surface through the same
/// bilinear interpolation as it writes each row, so the largest thing this call holds is a handful
/// of planes of the reduced picture whatever the frame's size.
///
/// A sample the validity rule rejects takes no part in the estimate and leaves its destination
/// sample untouched. Every other corrected value is computed in Float64, narrowed to Float32, and
/// written through the request's write-back conversion.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at iteration, pass and
/// row-block boundaries, and is not failure-atomic.
///
/// Progress is reported through the execution's sink after the first fit and after every rejection
/// round of every channel, in a total of the request's round ceiling plus one for every channel, and
/// each channel's share ends full when its fit converges early.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the frame's samples start. Must not be null.
/// @param sourceLayout where they sit. Its extent must be the plan's picture.
/// @param destination where the corrected samples go. Must not be null. It may be the source
///        exactly.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return what the estimate and the correction did; the refusals `estimateImageAutoBackground`
///         documents, other than those about model and protection planes, which this call does not
///         take; `ErrorCode::InvalidArgument` for a null destination or a destination layout whose
///         element size is not the representation's; `ErrorCode::IncompatibleArguments` for a
///         destination that is not the source's picture; `ErrorCode::Aliasing` for buffers that
///         overlap without being the same; or `ErrorCode::EmptySelection` when
///         `BackgroundLevel::SourceMean` finds no sample the validity rule admits.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AutoBackgroundResult>
removeImageAutoBackground(const ExecutionContext& execution, SampleType sampleType, const void* source,
                          const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                          const AutoBackgroundPlan& plan, const SpatialSelection& selection = {});

/// Estimates and removes the background of a checked typed image view.
///
/// The typed spelling of `removeImageAutoBackground`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return whatever `removeImageAutoBackground` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<AutoBackgroundResult>
removeAutoBackground(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                     const ImageView<SampleT>& destination, const AutoBackgroundPlan& plan,
                     const SpatialSelection& selection = {})
{
    return removeImageAutoBackground(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                     destination.data(), destination.layout(), plan, selection);
}

/// Estimates the background of an image once, removes it, and draws what that one estimate found into caller-owned
/// planes.
///
/// The untyped entry point, and the one the exported library carries. It is `removeImageAutoBackground` and
/// `estimateImageAutoBackground` over one fit: a caller that shows or keeps the corrected frame, the surface and the
/// structure the fit was kept away from pays for the estimate, which is the expensive part, once. Each channel is
/// reduced and estimated once; its surface and its protection are drawn into the planes as
/// `estimateImageAutoBackground` draws them, and the channel is then corrected as `removeImageAutoBackground` corrects
/// it. Every corrected sample, every surface sample and every protection sample is the one those two calls write for
/// the same plan, source and selection, value for value.
///
/// A request that does not protect structures leaves the protection planes untouched, as the estimate does. Given no
/// planes at all, it is `removeImageAutoBackground`.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only read. Runs on the
/// threads the operation was admitted, checks cancellation at iteration, pass and row-block boundaries, and is not
/// failure-atomic with respect to the destination or the planes.
///
/// Progress is reported as `removeImageAutoBackground` reports it.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the frame's samples start. Must not be null.
/// @param sourceLayout where they sit. Its extent must be the plan's picture.
/// @param destination where the corrected samples go. Must not be null. It may be the source exactly.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated request.
/// @param models one plane per channel for the surface, in channel order, each of the frame's extent; or an empty
///        span for a run that draws none. Must overlap neither the source nor the destination.
/// @param protection one plane per channel for the grown structure mask, in channel order, written as
///        `estimateImageAutoBackground` writes it; or an empty span for a run that draws none. Must overlap neither
///        the source nor the destination.
/// @param selection which samples mean something.
/// @return what the estimate and the correction did, as `removeImageAutoBackground` reports it; the refusals it
///         documents; `ErrorCode::IncompatibleArguments` for a model or protection plane count that is neither zero
///         nor the channel count, or a plane that is not the frame's extent; or `ErrorCode::Aliasing` for a plane that
///         overlaps the source or the destination.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AutoBackgroundResult> removeAndDrawImageAutoBackground(
    const ExecutionContext& execution, SampleType sampleType, const void* source, const ImageLayout& sourceLayout,
    void* destination, const ImageLayout& destinationLayout, const AutoBackgroundPlan& plan,
    std::span<const PlaneView<float>> models, std::span<const PlaneView<float>> protection = {},
    const SpatialSelection& selection = {});

/// Estimates the background of a checked typed image view once, removes it and draws what it found.
///
/// The typed spelling of `removeAndDrawImageAutoBackground`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated request.
/// @param models one plane per channel for the surface, or an empty span.
/// @param protection one plane per channel for the structure mask, or an empty span.
/// @param selection which samples mean something.
/// @return whatever `removeAndDrawImageAutoBackground` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<AutoBackgroundResult>
removeAndDrawAutoBackground(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                            const ImageView<SampleT>& destination, const AutoBackgroundPlan& plan,
                            std::span<const PlaneView<float>> models, std::span<const PlaneView<float>> protection = {},
                            const SpatialSelection& selection = {})
{
    return removeAndDrawImageAutoBackground(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                            destination.data(), destination.layout(), plan, models, protection,
                                            selection);
}

/// What an automatic estimate does to the image it reads.
///
/// It writes planes the caller owns rather than an image, so it reads samples and changes nothing
/// about the description, the meaning, the solution, the cards or the output decision of anything.
inline constexpr OperationEffect kAutoBackgroundEstimateEffect{.samples = SampleEffect::Reads};

} // namespace an
