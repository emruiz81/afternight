// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Statistical stretch to a target median.
///
/// The stretch moves an image's median to a stated target by one rational transfer curve, after
/// cutting everything at or below a black point. It is planned in three steps a caller may take one
/// at a time: `analyzeImageStatisticalStretch` measures the image's minimum and median and estimates
/// its lower-half spread; `computeStatisticalStretchClipStatistics` turns those into black points
/// and counts what they clip; and `stretchImageStatistically` writes the result. The analysis is a
/// plain value, so a preview that tries several targets over one image measures it once.
///
/// For each linked group or channel, the minimum and the median are exact, over every selected
/// sample of the group; the median of an even count is the lower middle value plus half the
/// distance to the upper one. The spread is estimated from a bounded sample, as
/// `StatisticalStretchChannelAnalysis::lowerHalfSigma` describes. The black point is
/// `median - blackpointSigma * lowerHalfSigma`, raised to the minimum when below it and capped at
/// 0.99, or the minimum itself when `noBlackClip` is set.
///
/// A sample `v` is rescaled to `r = (v - black) / (1 - black)`, a divisor of at least 1e-12, and `r`
/// is held to `[0, 1]`: everything at or below the black point maps to zero, which is what the clip
/// count reports, and nothing above one exceeds it. `r` then maps to
/// `(m - 1) t r / (m (t + r - 1) - t r)`, where `m` is the rescaled median and `t` the target, which
/// sends the median to the target; a denominator closer to zero than 1e-12 is replaced by 1e-12.
/// The optional highlight curve, the optional normalization and a final clamp to `[0, 1]` follow,
/// and a zero result is positive zero. Everything is computed in double and narrowed to the working
/// image's representation.
///
/// Every pass runs on the operation's threads, and each merges only counts and extremes, so every
/// result is the same at any thread count. Only mono and RGB Float32 or Float64 working images are
/// accepted, and their samples are expected to be normalized.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>

namespace an
{

/// The largest deterministic sample the lower-half spread of one group is estimated from.
///
/// The groups are sampled one after another, so an analysis holds at most 3.2 MB of sample in
/// Float64, charged to the engine context and reported by the analysis.
inline constexpr std::uint64_t kMaxStatisticalStretchSamplesPerChannel = 400000u;

/// Robust values precomputed for one linked group or one image channel.
struct StatisticalStretchChannelAnalysis
{
    /// Smallest selected value of the group.
    double minimum = 0.0;

    /// Median of every selected value of the group.
    double median = 0.0;

    /// Median absolute deviation of the lower half of the group's bounded sample, about that half's
    /// own median, scaled to normal-distribution sigma by 1.4826.
    ///
    /// The sample holds every selected candidate whose index is a multiple of
    /// `ceil(candidates / kMaxStatisticalStretchSamplesPerChannel)`, and the first selected
    /// candidate. The candidates are the pixels for a mono or unlinked group, and every channel of
    /// every pixel in channel-major order for linked RGB. The lower half is every sampled value at or
    /// below the sample's median.
    double lowerHalfSigma = 0.0;

    /// How many values the spread's sample retained.
    std::uint64_t sampledValues = 0u;
};

/// Reusable statistics for one image and one linkedness choice.
///
/// The record owns no samples and holds no source pointer. It is a cache value: a caller reusing it
/// is responsible for including the source image's pixel generation, validity generation and the
/// `linked` choice in that cache key. Width, height, channel count and representation are recorded
/// so an accidental reuse across a differently shaped image is still refused.
struct StatisticalStretchAnalysis
{
    /// Whether all channel samples formed one shared statistical population.
    bool linked = true;

    /// Resident representation analyzed, Float32 or Float64.
    SampleType sampleType = SampleType::Float32;

    /// Source width in pixels.
    std::uint32_t width = 0u;

    /// Source height in pixels.
    std::uint32_t height = 0u;

    /// One for monochrome or three for RGB.
    std::uint32_t channelCount = 0u;

    /// Pixel count before and after the source validity rule was applied.
    ValidityCount selection{};

    /// One entry per channel for an unlinked RGB analysis; for a linked one, the shared group, in
    /// entry zero and, for RGB, copied into entries one and two. Monochrome uses entry zero only.
    std::array<StatisticalStretchChannelAnalysis, 3u> channels{};

    /// The most context memory held at once while producing this analysis: the exact selection's
    /// working memory or the spread's sample buffer, whichever is larger.
    std::uint64_t peakScratchBytes = 0u;
};

/// The requested statistical stretch and its optional finishing passes.
struct StatisticalStretchParameters
{
    /// Whether RGB channels share one median, lower-half sigma and black point.
    bool linked = true;

    /// Output assigned to the analyzed median. Must be finite and strictly between zero and one.
    double targetMedian = 0.3;

    /// Divide every transformed channel by the image-wide transformed maximum before clamping.
    bool normalize = false;

    /// Apply the six-point highlight curve after the statistical transform.
    bool applyCurves = false;

    /// Highlight-curve boost in `[0, 1]`. Validated whatever `applyCurves` says, and used only when
    /// it is true.
    double curvesBoost = 0.0;

    /// Number of lower-half sigmas below the median used as the black point. Must be finite and not
    /// negative. The black point is then raised to the minimum when below it and capped at 0.99.
    double blackpointSigma = 5.0;

    /// Use the minimum as the black point, disabling sigma clipping: only samples equal to the
    /// minimum are cut.
    bool noBlackClip = false;
};

/// Black points and clipping counts derived for one request.
struct StatisticalStretchClipStatistics
{
    /// One black point per channel; all three entries agree for linked RGB and only entry zero is
    /// meaningful for monochrome.
    std::array<double, 3u> blackPoints{};

    /// Valid pixels with at least one channel at or below its black point.
    std::uint64_t clippedPixels = 0u;

    /// Valid pixels considered. Invalid pixels are neither clipped nor transformed.
    std::uint64_t totalPixels = 0u;

    /// Whether the black points came from one shared channel population.
    bool linked = true;

    /// Whether the sampled minima rather than sigma clipping supplied the black points.
    bool noBlackClip = false;
};

/// Counts, analysis and numeric decisions from one statistical stretch.
struct StatisticalStretchResult
{
    /// Pixels considered and admitted, and channel samples written.
    KernelRunResult run{};

    /// Analysis used, whether supplied by the caller or computed by this invocation.
    StatisticalStretchAnalysis analysis{};

    /// Black points and the number of valid pixels they clipped.
    StatisticalStretchClipStatistics clipping{};

    /// Image-wide transformed maximum divided out when normalization ran, or one otherwise.
    double normalizationDivisor = 1.0;
};

/// Builds the reusable analysis for a statistical stretch.
///
/// The minimum and the median are exact order statistics of every selected value, taken by counting
/// rather than sorting, and the spread comes from the bounded sample the analysis record describes.
/// For linked RGB all three channels form one population; unlinked RGB analyzes each channel. A
/// pixel participates only when the optional mask and every channel's value-validity rule admit it.
///
/// Float32 and Float64 normalized working images are accepted. The source may be planar,
/// interleaved or mixed and may carry padding. Scratch is owned by the engine context, bounded
/// independently of the image, and released before return. Every pass runs on the operation's
/// threads and observes cancellation.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64.
/// @param sourceSamples first byte of the source image.
/// @param sourceLayout source geometry and strides, with one or three channels.
/// @param linked whether RGB channels form one statistical population.
/// @param validity source validity and optional packed mask.
/// @return the reusable analysis; `ErrorCode::NotInitialized` for an empty execution context;
///         `ErrorCode::IncompatibleArguments` for a representation other than Float32 or Float64,
///         a channel count other than one or three, or a mask that does not cover the source;
///         `ErrorCode::InvalidArgument` for a null or misaligned pointer, a layout whose element
///         width is not the representation's, or a packed mask that is missing or not needed;
///         `ErrorCode::EmptySelection` for an empty image or when no pixel participates;
///         `ErrorCode::InvalidData` for a selected value that is not finite; or the engine context's
///         refusal of the sample memory, the execution's cancellation, or its backend-selection
///         failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StatisticalStretchAnalysis>
analyzeImageStatisticalStretch(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
                               const ImageLayout& sourceLayout, bool linked, const KernelValidityView& validity = {});

/// Derives black points and counts the valid pixels they clip.
///
/// `analysis` must describe this source and linkedness. Reusing analysis after changing the
/// source's pixels or validity is a caller cache error that cannot be detected from a borrowed
/// pointer - only a change in the number of valid pixels is noticed - so the source generations
/// belong in the caller's cache key. The count runs on the operation's threads and observes
/// cancellation.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, matching `analysis.sampleType`.
/// @param sourceSamples first byte of the analyzed source.
/// @param sourceLayout source geometry and strides.
/// @param analysis reusable statistics for this source.
/// @param blackpointSigma finite non-negative lower-half-sigma multiplier.
/// @param noBlackClip whether to use each sampled minimum instead.
/// @param validity the same validity used to build `analysis`.
/// @return black points and counts; `ErrorCode::IncompatibleArguments` when `analysis` records a
///         different representation, extent or channel count, or a different number of valid
///         pixels than the source now has; `ErrorCode::InvalidArgument` for a `blackpointSigma`
///         that is not finite and non-negative, or an analysis without finite statistics;
///         `ErrorCode::InvalidData` for a selected value that is not finite; or any source failure
///         `analyzeImageStatisticalStretch` names other than an empty selection.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StatisticalStretchClipStatistics>
computeStatisticalStretchClipStatistics(const ExecutionContext& execution, SampleType sampleType,
                                        const void* sourceSamples, const ImageLayout& sourceLayout,
                                        const StatisticalStretchAnalysis& analysis, double blackpointSigma,
                                        bool noBlackClip, const KernelValidityView& validity = {});

/// Applies the bounded statistical stretch to a mono or RGB working image.
///
/// When `precomputed` is null the operation builds its analysis first; otherwise it validates and
/// copies the supplied record, and a record whose valid-pixel count no longer matches the source is
/// refused before anything is written. The clip statistics are counted while the destination is
/// written. The source and destination may use different planar, interleaved or mixed layouts and
/// may alias exactly. Every partial overlap is refused. Invalid pixels leave the destination
/// untouched. A cancellation or selected non-finite value may leave earlier rows written, except
/// that normalization's read-only maximum pass completes before its first write.
///
/// The transform first cuts everything at or below each group's black point and moves its analyzed
/// median to `targetMedian`, optionally applies the six-point curve, optionally normalizes by the
/// transformed image-wide maximum, and finally clamps to `[0, 1]`.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by source and destination.
/// @param sourceSamples first byte of the source image.
/// @param sourceLayout source geometry and strides, with one or three channels.
/// @param destinationSamples first writable byte of the destination.
/// @param destinationLayout destination geometry and strides, equal in extent to the source.
/// @param parameters linkedness, target, clipping and optional finishing passes.
/// @param precomputed reusable analysis for this exact source and validity, or null.
/// @param validity source validity and optional packed mask.
/// @return counts and every numeric decision; any failure `analyzeImageStatisticalStretch` or
///         `computeStatisticalStretchClipStatistics` names; `ErrorCode::InvalidArgument` for a null
///         or misaligned destination, a destination layout whose element width is not the
///         representation's, a mask that overlaps the destination, or a target, curve boost or
///         black-point sigma outside its range; `ErrorCode::IncompatibleArguments` for a destination
///         of another extent, a partial overlap, or a supplied analysis of another linkedness; or
///         `ErrorCode::InvalidData` when the transform produces a value that is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StatisticalStretchResult>
stretchImageStatistically(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
                          const ImageLayout& sourceLayout, void* destinationSamples,
                          const ImageLayout& destinationLayout, const StatisticalStretchParameters& parameters,
                          const StatisticalStretchAnalysis* precomputed = nullptr,
                          const KernelValidityView& validity = {});

/// What `stretchImageStatistically` does to an image it writes.
///
/// The samples are written and their meaning is restated, because a stretched sample no longer
/// stands for the quantity it measured; the validity is preserved, because invalid pixels are left
/// untouched.
inline constexpr OperationEffect kStatisticalStretchEffect{.samples = SampleEffect::Writes,
                                                           .semantics = SemanticEffect::Restates,
                                                           .validity =
                                                               ValidityTransition{.policy = ValidityPolicy::Preserve}};

} // namespace an
