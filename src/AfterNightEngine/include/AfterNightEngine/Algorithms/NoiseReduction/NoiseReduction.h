// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The noise this engine removes itself, as opposed to the noise it removes
/// through somebody else's algorithm.
///
/// Four operations live here and they have one thing in common: each of them decides that a sample is
/// wrong and replaces it, rather than smoothing everything and hoping the wrong samples were the ones
/// that moved. What differs is how "wrong" is decided - by how far a neighbour is in value as well as
/// in distance, by a colour that no astronomical source produces, by a whole row sitting at the wrong
/// level, or by one sample standing away from its neighbourhood.
///
/// **A threshold is written in the image's own units.** This is the rule the whole file is arranged
/// around: a range sigma, a rejection threshold and a white level are all numbers in the
/// representation the caller handed over, nothing is rescaled on the way in, and a write-back policy
/// asking for `DomainMapping::FullScale` is refused by name. A sigma that means something in the
/// caller's units is never applied to samples converted to another scale.
///
/// **What an operation replaced is a number it reports.** Every operation here reports how many
/// samples it moved, so a caller can tell a threshold that did nothing from one that rewrote a tenth
/// of the frame.
///
/// **The neighbourhood contract is `Algorithms/Spatial`'s, unchanged.** The bilateral filter and the
/// impulse rejector read neighbours, so they take the same `SpatialSelection`, resolve borders through
/// the same `StencilBorderPolicy`, and leave a rejected centre untouched. There is no second border
/// rule and no second validity rule in this file.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
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

/// What one noise-reduction operation did, beyond what every neighbourhood filter reports.
struct NoiseReductionResult
{
    /// Pixel selection, writes, working memory, layout path and planned workers, summed over the
    /// channels the operation touched.
    SpatialFilterResult filter{};

    /// How many samples the operation actually moved.
    ///
    /// It separates a threshold that did nothing from one that rewrote a tenth of the frame, which is
    /// the difference between a parameter a person can tune and one they can only guess at. A sample
    /// written back unchanged is not counted.
    std::uint64_t samplesChanged = 0u;
};

/// The widest half-extent a bilateral window may have.
///
/// A bilateral filter cannot separate into two passes - its weights depend on the centre sample, so
/// the horizontal answer is not reusable down a column - and it therefore costs the whole window per
/// output sample. Thirty-two is a sixty-five by sixty-five neighbourhood, which is past the point
/// where the cost is the reason to reach for a different algorithm rather than a wider window.
inline constexpr std::uint32_t kMaxBilateralRadius = 32u;

/// How many spatial standard deviations a bilateral window covers when the caller states no radius.
///
/// One and a half, which is the imaging library's own derivation of a window radius from a spatial
/// sigma.
inline constexpr double kBilateralSpatialTruncation = 1.5;

/// The narrowest sigma either half of a bilateral weight is built for.
///
/// Below this the weight is a delta and the filter is the identity, which is a result a caller
/// almost never means to ask for. A sigma at or below this is refused rather than silently doing
/// nothing.
inline constexpr double kMinimumBilateralSigma = 1.0e-6;

/// What a caller asks a bilateral filter for.
struct BilateralRequest
{
    /// How far, in pixels, a neighbour may be before distance alone stops it contributing. Must be
    /// finite and above `kMinimumBilateralSigma`.
    double spatialSigma = 2.0;

    /// How far, **in the image's own sample units**, a neighbour may be in value before its
    /// difference alone stops it contributing. Must be finite and above `kMinimumBilateralSigma`.
    ///
    /// The samples are read as they are stored and this number is compared against them, so a range
    /// sigma of two thousand means two thousand code values whatever the representation, and one
    /// request means the same thing over a Float32 frame and over the identical picture stored as
    /// UInt16.
    double rangeSigma = 0.05;

    /// Half the window, in pixels, or zero to derive it from `spatialSigma`.
    ///
    /// The derivation is `round(kBilateralSpatialTruncation * spatialSigma)`, the imaging library's
    /// own, and never below one. A stated radius above `kMaxBilateralRadius` is refused.
    std::uint32_t radius = 0u;

    /// How a neighbour outside the image and a neighbour the validity rule rejected are supplied.
    ///
    /// The default reflects about the edge rather than clamping, which is both the imaging library's
    /// default and what keeps an edge from pulling the filter towards itself.
    StencilBorderPolicy borders{.border = KernelBorderMode::ReflectExcludingEdge};

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A validated bilateral filter: what was asked for, and the window that will actually run.
///
/// Trivially copyable and allocation-free.
class BilateralPlan
{
public:
    /// A plan describing no filter, which no execution accepts.
    BilateralPlan() noexcept = default;

    /// Records a request and derives the window it implies.
    ///
    /// Public for the reason `GaussianPlan`'s constructor is: the free builder below is where the
    /// rules live, and this only derives what a checked request implies.
    ///
    /// @param request the two sigmas, the window, the border and the output policy.
    AFTERNIGHTENGINE_API explicit BilateralPlan(const BilateralRequest& request) noexcept;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const BilateralRequest& request() const noexcept;

    /// Half the window, in pixels, that will actually run.
    ///
    /// @return the stated radius, or the one the spatial sigma implies.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t radius() const noexcept;

    /// How many samples of the window are inside the circular footprint.
    ///
    /// The filter reads a disc rather than a square, as the imaging library does, so this is what one
    /// output sample actually costs and it is well below the square of the diameter.
    ///
    /// @return the sample count, which is zero for a plan describing no filter.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t windowSamples() const noexcept;

    /// How many times the filter walks the whole image.
    ///
    /// @return one: a bilateral filter does not separate, so there is nothing to do in two passes.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// The request this plan records.
    BilateralRequest m_request{};

    /// The window half-extent, derived when the request stated none.
    std::uint32_t m_radius = 0u;

    /// How many window positions lie inside the circular footprint.
    std::uint32_t m_windowSamples = 0u;
};

/// Validates a request and derives the window that would run it.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the two sigmas, the window, the border and the output policy.
/// @return the plan; `ErrorCode::InvalidArgument` for a non-finite sigma, a sigma at or below
///         `kMinimumBilateralSigma`, an unknown or inapplicable border, a non-finite border or
///         invalid-neighbour value, an output contract or conversion field outside its enumeration,
///         or a write-back policy naming `DomainMapping::FullScale`; or `ErrorCode::OutOfRange` when
///         the stated or derived radius is above `kMaxBilateralRadius`, carrying the radius and the
///         limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BilateralPlan> planBilateralFilter(const BilateralRequest& request);

/// Smooths every channel of an image without smoothing across its edges.
///
/// The untyped entry point, and the one the exported library carries. Every neighbour inside the
/// circular window contributes a weight that is the product of two Gaussians - one in distance, one
/// in the difference between its value and the centre's - so a neighbour on the other side of an edge
/// contributes almost nothing and the edge survives a blur that removes the noise beside it.
///
/// The arrangement, working planes, validity, border and write-back rules are
/// `convolveImageSeparable`'s. **The range weight is computed on the stored numbers**, which is the
/// whole point of this entry point existing rather than an adapter to somebody else's: the same
/// request over the same picture means the same thing whether it is stored as Float32 or as UInt16.
///
/// It reproduces the imaging library's bilateral filter - its circular window and radius rule, its
/// reflecting default border, and a range weight evaluated rather than read from a table - and not
/// scikit-image's bilateral filter, which is a different filter.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at row boundaries, and
/// is not failure-atomic.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the filtered samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated window and sigmas.
/// @param selection which samples mean something.
/// @return the filter counts and how many stored samples changed; the failures
///         `convolveImageSeparable` documents, plus
///         `ErrorCode::InvalidArgument` for a plan that has not been validated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NoiseReductionResult>
filterImageBilateral(const ExecutionContext& execution, SampleType sampleType, const void* source,
                     const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                     const BilateralPlan& plan, const SpatialSelection& selection = {});

/// Smooths a checked typed image view without smoothing across its edges.
///
/// The typed spelling of `filterImageBilateral`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated window and sigmas.
/// @param selection which samples mean something.
/// @return whatever `filterImageBilateral` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<NoiseReductionResult>
filterBilateral(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                const ImageView<SampleT>& destination, const BilateralPlan& plan,
                const SpatialSelection& selection = {})
{
    return filterImageBilateral(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                                destination.layout(), plan, selection);
}

/// How a chromatic-noise reduction decides what the middle channel should have been.
///
/// All four suppress a green cast, which is the one an astronomical colour image acquires from its
/// sensor and its filters rather than from the sky. They differ in what they consider the green
/// channel's legitimate ceiling.
enum class ChromaticNoiseMethod : std::uint8_t
{
    /// Green is held at or below the mean of red and blue. The strongest of the four, and the one
    /// that costs the most real green.
    AverageNeutral = 0u,

    /// Green is held at or below the larger of red and blue. Gentler, and the usual first choice.
    MaximumNeutral = 1u,

    /// Green is mixed towards its unsuppressed value in proportion to the larger of red and blue, so
    /// a bright pixel keeps its green and a dark one loses it.
    MaximumMask = 2u,

    /// The same mix, with the mask taken from the sum of red and blue rather than their maximum.
    AdditiveMask = 3u
};

/// Whether a value names one of the chromatic-noise methods this build implements.
///
/// @param method the value to inspect.
/// @return true for all four methods.
[[nodiscard]] constexpr bool isChromaticNoiseMethod(ChromaticNoiseMethod method) noexcept
{
    return static_cast<std::uint8_t>(method) <= static_cast<std::uint8_t>(ChromaticNoiseMethod::AdditiveMask);
}

/// The stable spelling of a chromatic-noise method, for a report or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param method the method to name.
/// @return a static string, or `"Unknown"` for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* chromaticNoiseMethodName(ChromaticNoiseMethod method) noexcept;

/// What a caller asks a chromatic-noise reduction for.
struct ChromaticNoiseRequest
{
    /// Which ceiling the green channel is held to.
    ChromaticNoiseMethod method = ChromaticNoiseMethod::AverageNeutral;

    /// How strongly the two mask methods suppress green, within `[0, 1]`.
    ///
    /// Green becomes `green * ((1 - amount) * (1 - mask) + mask)`, so one applies the mask outright
    /// and zero leaves green as it was. Checked but not read by the two neutral methods, which have
    /// no mixture to control.
    double amount = 1.0;

    /// The sample value that means fully bright, in the image's own units. Must be finite and
    /// positive.
    ///
    /// The two mask methods need to know where "bright" is, because their mask is a fraction of full
    /// scale. The caller says what full scale is once, and the arithmetic happens on the stored
    /// numbers, so the operation's meaning does not depend on the storage.
    double whiteLevel = 1.0;

    /// Whether to put back the perceptual lightness the suppression removed.
    ///
    /// A green suppression darkens a pixel as well as decolouring it. With this set the operation
    /// converts to CIE Lab before and after, keeps the original lightness and the suppressed opponent
    /// channels, and converts back - which is why it costs three colour conversions per pixel.
    bool preserveLightness = false;

    /// The source and destination RGB encoding. Read only when `preserveLightness` is set, and it
    /// must then have exactly `ChannelSchema::Rgb`.
    ColorEncoding rgbEncoding{.transfer = ColorTransferFunction::Srgb};

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A validated chromatic-noise reduction, and the colour round trip it will make.
///
/// It has no default constructor, for the reason `PerceptualAdaptiveHistogramPlan` has none: a
/// colour conversion is not something that exists in an unset state, and an execution establishes
/// that a plan was validated by validating its request again rather than by inspecting the plan.
class ChromaticNoisePlan
{
public:
    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const ChromaticNoiseRequest& request() const noexcept;

    /// Whether this plan puts the original lightness back.
    ///
    /// @return the request's `preserveLightness`.
    [[nodiscard]] AFTERNIGHTENGINE_API bool preservesLightness() const noexcept;

    /// Conversion into natural-unit Lab.
    ///
    /// Built whether or not lightness is preserved, because a plan that held a conversion only
    /// sometimes would need a caller to ask which case it was in before reading it.
    ///
    /// @return the checked forward colour plan.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorConversionPlan& toLab() const noexcept;

    /// Conversion from natural-unit Lab back to the source encoding.
    ///
    /// @return the checked inverse colour plan.
    [[nodiscard]] AFTERNIGHTENGINE_API const ColorConversionPlan& fromLab() const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<ChromaticNoisePlan>
    planChromaticNoiseReduction(const ChromaticNoiseRequest& request);

    /// Records pieces validated by the friend builder.
    ///
    /// @param request the method, amount, white level and lightness policy.
    /// @param toLab checked conversion into natural-unit Lab under the request's white point.
    /// @param fromLab checked conversion back to the exact source encoding.
    ChromaticNoisePlan(const ChromaticNoiseRequest& request, const ColorConversionPlan& toLab,
                       const ColorConversionPlan& fromLab) noexcept;

    /// The request this plan records.
    ChromaticNoiseRequest m_request{};

    /// RGB to natural-unit Lab.
    ColorConversionPlan m_toLab;

    /// Natural-unit Lab to RGB.
    ColorConversionPlan m_fromLab;
};

/// Validates a chromatic-noise request and derives the colour round trip it needs.
///
/// Thread-safe. Allocates only a failure description.
///
/// @param request the method, amount, white level, lightness policy, encoding and output policy.
/// @return the plan; `ErrorCode::InvalidArgument` for a method outside the enumeration, a non-finite
///         or out-of-range amount, a non-finite or non-positive white level, a conversion field
///         outside its enumeration, or a write-back policy naming `DomainMapping::FullScale`; or
///         `ErrorCode::IncompatibleArguments` when lightness is preserved and the encoding is not a
///         valid three-channel RGB one.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ChromaticNoisePlan>
planChromaticNoiseReduction(const ChromaticNoiseRequest& request);

/// Suppresses the green cast of a three-channel image.
///
/// Reads the three channels of one pixel, decides what the middle one should have been, and writes
/// all three back. Red and blue are unchanged, apart from the hold below, unless lightness is
/// preserved, in which case all three move so that the pixel keeps the brightness it had.
///
/// The result is held inside `[0, whiteLevel]`, because a lightness round trip through Lab can leave
/// an RGB triple outside the interval it started in and a caller reaching for chromatic-noise
/// reduction is not asking to be handed one.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its extent must have exactly three channels.
/// @param destination where the reduced samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated method and colour round trip.
/// @param selection which samples mean something. A rejected pixel is left untouched in all three
///        channels.
/// @return the counts; the failures `convolveImageSeparable` documents, plus
///         `ErrorCode::IncompatibleArguments` for an image that does not have three channels, and
///         `ErrorCode::InvalidArgument` for a plan that has not been validated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NoiseReductionResult>
reduceImageChromaticNoise(const ExecutionContext& execution, SampleType sampleType, const void* source,
                          const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                          const ChromaticNoisePlan& plan, const SpatialSelection& selection = {});

/// What a caller asks a banding suppression for.
///
/// Banding is a whole row or column sitting at the wrong level - read noise that correlates along an
/// axis of the sensor, or an amplifier glow that was subtracted unevenly. It is not a neighbourhood
/// defect, so it is not answered by a neighbourhood filter: the level of each row is measured
/// robustly, compared with the level of the channel, and the difference is added back.
struct BandingRequest
{
    /// How much of the measured correction is applied. Must be finite and not negative; one applies
    /// it outright and zero leaves the image alone.
    double amount = 1.0;

    /// Whether to measure each row over its darker samples only.
    ///
    /// A row crossing a bright nebula has a higher median than a row that does not, and correcting
    /// for that would carve a dark line through the object. With this set, the samples at or above
    /// `background + globalScale / highlightSigma` are left out of the row measurement.
    bool protectHighlights = false;

    /// How many robust scales above the background the highlight rejection point sits, as a divisor.
    ///
    /// The point is `background + scale / highlightSigma`, so a larger number rejects more. Must be
    /// finite and positive. Read only when `protectHighlights` is set.
    double highlightSigma = 1.0;

    /// Whether the banding runs down columns rather than along rows.
    bool verticalBanding = false;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// Levels the rows or columns of an image against its own background.
///
/// Each channel is measured on its own. The channel's background is its median; each line's level is
/// its own median, optionally over its darker samples; the correction for a line is the difference
/// between the two, shifted so that the smallest correction in that channel is zero and scaled by
/// the amount. The shift makes the correction additive rather than signed, so it cannot pull a line
/// below the floor of an integer representation.
///
/// **The shift is taken per channel**, so the correction applied to the blue channel of an RGB frame
/// does not depend on how badly the red channel is banded. A line from which highlight protection
/// rejects every sample takes part in neither the minimum correction nor the write: its destination
/// samples remain untouched.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the levelled samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param request the amount, the highlight policy and the axis.
/// @param selection which samples mean something. A rejected sample is left out of every measurement
///        and its destination is left untouched.
/// @return the counts; the failures `convolveImageSeparable` documents, plus
///         `ErrorCode::InvalidArgument` for a non-finite or negative amount or a non-positive
///         highlight sigma, and `ErrorCode::EmptySelection` when a channel has no sample the rule
///         admits.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NoiseReductionResult>
suppressImageBanding(const ExecutionContext& execution, SampleType sampleType, const void* source,
                     const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                     const BandingRequest& request, const SpatialSelection& selection = {});

/// What a caller asks an impulse rejection for.
///
/// An impulse is one sample standing away from every sample around it - a cosmic ray, a hot or dead
/// photosite, a decoding glitch. It is found by comparing each sample with the median of its
/// neighbourhood and replacing only the ones that stand too far away, which is what separates this
/// from a median filter: a median filter replaces every sample and softens the image, this one
/// leaves all but a handful exactly as they were.
struct ImpulseNoiseRequest
{
    /// Half the neighbourhood the median is taken over, at least one.
    std::uint32_t radius = 1u;

    /// Whether to replace samples standing above their neighbourhood.
    bool rejectHigh = true;

    /// How far above the neighbourhood median a sample must stand, as a fraction of its own
    /// magnitude, before it is replaced. Must be finite and within `[0, 1)`.
    ///
    /// The comparison is `(sample - median) / |sample| > threshold`. A sample of exactly zero has no
    /// relative deviation, so it is left alone.
    double highThreshold = 0.5;

    /// Whether to replace samples standing below their neighbourhood.
    bool rejectLow = false;

    /// How far below the neighbourhood median a sample must stand, as a fraction of the median,
    /// before it is replaced. Must be finite and within `[0, 1)`.
    ///
    /// The comparison is `(median - sample) / |median| > threshold`, with the same rule for a median
    /// of exactly zero.
    double lowThreshold = 0.5;

    /// How a window sample outside the image is supplied.
    ///
    /// The mode and the constant are forwarded to `filterImageOrderStatistic`, which leaves a
    /// neighbour the validity rule rejected out of the window rather than substituting it, so
    /// `invalidNeighbor` is never read here - though it must still be finite unless the mode is
    /// `KernelBorderMode::NotApplicable`. That mode is accepted here and nowhere else in this file:
    /// it names a truncated window, every outside sample omitted rather than substituted.
    StencilBorderPolicy borders{.border = KernelBorderMode::ReflectExcludingEdge};

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// Replaces the samples of an image that stand away from their own neighbourhood.
///
/// The neighbourhood median comes from `filterImageOrderStatistic`, so the window ceiling, the
/// border rule and the exact rank are that operation's and there is no second order statistic in
/// the engine; the median image it writes, in the source's representation, is held for the call.
/// Every sample the thresholds do not name is copied across exactly, including through an integer
/// representation, so a rejection that finds nothing is the identity.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the corrected samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param request the window, which deviations to reject and how far is too far.
/// @param selection which samples mean something.
/// @return the counts, with `samplesChanged` naming how many samples were replaced; the failures
///         `filterImageOrderStatistic` documents, plus `ErrorCode::InvalidArgument` for a zero
///         radius, a threshold outside `[0, 1)`, a border mode outside its enumeration, or a
///         non-finite border or invalid-neighbour value under a mode other than `NotApplicable`, and
///         `ErrorCode::LimitExceeded` when the neighbourhood median of the picture cannot be
///         described as one image.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NoiseReductionResult>
rejectImageImpulseNoise(const ExecutionContext& execution, SampleType sampleType, const void* source,
                        const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                        const ImpulseNoiseRequest& request, const SpatialSelection& selection = {});

/// What every operation in this file does to the image it writes.
///
/// Removing noise changes samples and nothing else. The extent, the representation and the channel
/// schema are the source's; a denoised frame is still a frame of the same thing; nothing moves a
/// star; and no source card and no output decision is invalidated by it.
///
/// Validity is preserved, and that is a claim about the arithmetic: a meaningless neighbour is
/// answered by the border policy instead of being spread, a meaningless centre keeps the sample it
/// had, and no operation here creates a meaningless sample.
inline constexpr OperationEffect kNoiseReductionEffect{};

} // namespace an
