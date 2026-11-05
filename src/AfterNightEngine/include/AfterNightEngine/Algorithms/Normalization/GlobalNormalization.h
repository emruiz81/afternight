// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Global normalization: measure, match, solve and apply as separate steps.
///
/// Bringing every frame of a set to one reference's level and spread is four decisions: which samples
/// of a frame are measured, which robust estimators describe it, how one frame's estimators become an
/// affine match against the reference, and how that affine is written back. Each is its own entry
/// point here. A measurement is a value a caller may keep, combine or serialize; a match is a pair of
/// numbers per channel that any apply call accepts; and the overlap solver consumes explicitly
/// measured pairs rather than discovering them, so the geometry that finds overlaps stays with
/// registration.
///
/// Every step reads the frame's own validity claim - a registered canvas states
/// `ValidityRule::ZeroIsNoData` for its uncovered border - and nothing infers one from the pixels.
/// Medians are exact ranks rather than histogram-interpolated percentiles. A channel with no
/// deviation reports its median as its iterative k-sigma location. A multiplicative match against a
/// zero reference location is refused rather than written as an all-zero frame, and a transformed
/// sample that lands on the zero no-data marker is moved off it rather than written as zero.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image's samples currently mean. Defined in `Image/ImageSemantics.h`.
class ImageSemantics;

/// The checked metadata change a completed operation applies. Defined in `Image/ImageMetadata.h`.
struct MetadataTransition;

/// The most channels one normalization measures, matches or applies.
///
/// Four covers every fixed schema this engine names. A `ChannelSchema::Named` image with more
/// channels is refused at the frame view rather than truncated.
inline constexpr std::uint32_t kMaxNormalizationChannels = 4u;

/// The most frames one overlap system may relate.
///
/// The solver holds a dense Float64 system one row and column per non-reference frame, so this
/// bounds its memory at about one hundred and twenty-eight mebibytes. It is a ceiling on one
/// solve, not on a stacking set: a caller with a larger set solves it in connected groups.
inline constexpr std::uint32_t kMaxOverlapNormalizationFrames = 4096u;

/// How many median absolute deviations either side of the median the clipped estimator retains.
inline constexpr double kIterativeKSigmaClipWidth = 6.0;

/// The factor applied to the clipped biweight scale so it reads as a standard deviation.
inline constexpr double kIterativeKSigmaScaleCorrection = 0.991;

/// A location or dispersion at or below this keeps its pair out of a multiplicative overlap system.
inline constexpr double kOverlapValueEpsilon = 1.0e-12;

/// Which apply implementation actually wrote the destination.
enum class NormalizationExecutionPath : std::uint8_t
{
    /// The scalar oracle, including every packed-mask case.
    Reference,

    /// The selected CPU table's contiguous-row implementation.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CompiledRow
};

/// Whether a value names an execution path this build implements.
///
/// @param path the value to inspect.
/// @return true for every implemented path.
[[nodiscard]] constexpr bool isNormalizationExecutionPath(NormalizationExecutionPath path) noexcept
{
    return static_cast<std::uint8_t>(path) <= static_cast<std::uint8_t>(NormalizationExecutionPath::CompiledRow);
}

/// The path's own name.
///
/// @param path the path to name. A value outside the enumeration is named
///        `"UnknownNormalizationExecutionPath"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* normalizationExecutionPathName(NormalizationExecutionPath path) noexcept;

/// Which candidate overlap pairs an overlap system is built from.
enum class OverlapGraphPolicy : std::uint8_t
{
    /// Every candidate. The authoritative answer, and the one a sparse solve falls back to.
    AllPairs,

    /// Every candidate touching the reference, plus a maximum spanning tree over overlap area.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ReferenceStarSpanningTree
};

/// Whether a value names a graph policy this build implements.
///
/// @param policy the value to inspect.
/// @return true for every implemented policy.
[[nodiscard]] constexpr bool isOverlapGraphPolicy(OverlapGraphPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <=
           static_cast<std::uint8_t>(OverlapGraphPolicy::ReferenceStarSpanningTree);
}

/// The policy's own name.
///
/// @param policy the policy to name. A value outside the enumeration is named
///        `"UnknownOverlapGraphPolicy"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* overlapGraphPolicyName(OverlapGraphPolicy policy) noexcept;

/// What an overlap solve does when a subsidiary system cannot be solved.
///
/// The dispersion system and the dispersion-scaled additive system are subsidiary: a method that
/// needs them can substitute unity and the plain additive offsets, or refuse - which lets a caller
/// retry a degenerate sparse graph over every pair.
enum class OverlapDegeneracyPolicy : std::uint8_t
{
    /// Return `ErrorCode::InvalidData` naming the system that failed.
    Refuse,

    /// Substitute unity dispersion and the plain additive offsets, and count the substitution.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SubstituteIdentity
};

/// Whether a value names a degeneracy policy this build implements.
///
/// @param policy the value to inspect.
/// @return true for every implemented policy.
[[nodiscard]] constexpr bool isOverlapDegeneracyPolicy(OverlapDegeneracyPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(OverlapDegeneracyPolicy::SubstituteIdentity);
}

/// The policy's own name.
///
/// @param policy the policy to name. A value outside the enumeration is named
///        `"UnknownOverlapDegeneracyPolicy"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* overlapDegeneracyPolicyName(OverlapDegeneracyPolicy policy) noexcept;

/// A frame whose storage, description and validity have been checked together.
///
/// The samples and the optional mask are borrowed and must outlive this view and every call
/// receiving it; the layout and the descriptor are copied into it, so what describes the frame is
/// the view's and only the memory is the caller's. Only Float32 and Float64 frames of at most
/// `kMaxNormalizationChannels` channels in a linear, raw or physically scaled numeric domain are
/// admitted; an integer boundary frame is converted through `Image/ImageConversion.h` first.
///
/// The validity is the frame's own claim and it is what every step reads: a registered canvas
/// states `ValidityRule::ZeroIsNoData` for its uncovered border, a calibrated frame states
/// `ValidityRule::FiniteOnly` for the samples a zero flat produced, and nothing here infers either
/// from the pixels.
class NormalizationFrameView
{
public:
    /// A view holding nothing.
    NormalizationFrameView() noexcept = default;

    /// Checks and borrows one frame.
    ///
    /// @param samples where the image storage starts. Must not be null and must be aligned for the
    ///        sample type.
    /// @param bufferBytes bytes available from `samples`, covering every byte named by `layout`.
    /// @param layout where its samples sit, planar or interleaved.
    /// @param descriptor what the samples mean.
    /// @param validity which samples participate, with a covering mask when the rule needs one.
    ///        `maskPlane` must be zero: a shared mask uses that plane, and a per-channel mask is
    ///        addressed by each sample's channel automatically.
    /// @return the checked view; `ErrorCode::Unsupported` for an integer representation, a
    ///         nonlinear encoded domain, or more channels than `kMaxNormalizationChannels`;
    ///         `ErrorCode::InvalidArgument` for null or misaligned storage, a layout that does not
    ///         describe the descriptor or is semi-planar, a byte extent short of the layout, a
    ///         non-zero `maskPlane`, a mask missing for a rule that reads one or supplied for a
    ///         rule that does not; `ErrorCode::IncompatibleArguments` for a mask whose extent or
    ///         plane count does not cover the frame.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<NormalizationFrameView>
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
    NormalizationFrameView(const void* samples, std::uint64_t bufferBytes, const ImageLayout& layout,
                           const ImageDescriptor& descriptor, const KernelValidityView& validity) noexcept
        : m_samples(samples), m_bufferBytes(bufferBytes), m_layout(layout), m_descriptor(descriptor),
          m_validity(validity)
    {
    }

    /// The first sample, or null for an empty view.
    const void* m_samples = nullptr;

    /// Bytes available from `m_samples`, or zero for an empty view.
    std::uint64_t m_bufferBytes = 0u;

    /// The layout, held by value: it describes the samples rather than borrowing anything of them,
    /// and an image hands one out as a value, so a view that pointed at one would outlive it.
    std::optional<ImageLayout> m_layout{};

    /// The description, held by value, for the reason the layout is.
    std::optional<ImageDescriptor> m_descriptor{};

    /// The validity inputs, whose optional mask is borrowed too.
    KernelValidityView m_validity{};
};

/// The robust description of one channel that normalization matches on.
///
/// `median` and `medianAbsoluteDeviation` are always exact ranks of the admitted samples.
/// `location` and `scale` are what the requested estimator produced: the same two numbers under
/// `ScaleEstimator::MedianAbsoluteDeviation`, and the clipped median and corrected biweight scale
/// under `ScaleEstimator::IterativeKSigma`. A channel whose deviation is zero has no dispersion to
/// speak of: its location is its median and its scale is zero, which the match step treats as an
/// absent dispersion rather than as a factor. Under the clipped estimator a channel whose clipped
/// samples have no deviation keeps their median as its location and reports a scale of zero too.
struct NormalizationChannelStatistics
{
    /// How many samples the validity rule admitted, and how many pixels the region held.
    ValidityCount selection{};

    /// The exact median of the admitted samples.
    double median = 0.0;

    /// The exact median of the absolute deviations from `median`.
    double medianAbsoluteDeviation = 0.0;

    /// The estimator's location.
    double location = 0.0;

    /// The estimator's dispersion, zero when the channel has none.
    double scale = 0.0;

    /// How many samples the clipping interval retained, out of how many were admitted. Both zero
    /// under the deviation estimator, and when the channel's deviation is zero so nothing was
    /// clipped.
    ValidityCount clippedSelection{};
};

/// One frame measured for normalization.
///
/// A value a caller keeps: it borrows nothing, so a set of frames can be measured once, combined
/// into a reference and matched without any frame remaining resident.
struct NormalizationFrameStatistics
{
    /// Which estimator produced `location` and `scale`.
    ScaleEstimator estimator = ScaleEstimator::MedianAbsoluteDeviation;

    /// How many leading entries of `channels` are meaningful.
    std::uint32_t channelCount = 0u;

    /// Each channel's description, in channel order.
    std::array<NormalizationChannelStatistics, kMaxNormalizationChannels> channels{};

    /// How many times the selection was read, which is what the measurement cost.
    std::uint64_t planeScans = 0u;

    /// The most engine memory held at once.
    std::uint64_t peakScratchBytes = 0u;

    /// True when every channel was ranked from a gathered copy of its admitted samples; false when
    /// the engine budget refused a gather and that channel was ranked where it lies instead.
    bool compacted = false;
};

/// The target every frame is matched to.
///
/// One or more measured frames, combined per channel by the median of their medians, deviations,
/// locations and scales. A reference built from one frame carries that frame's numbers exactly,
/// so matching the frame against it yields the identity for every method.
struct NormalizationReference
{
    /// The estimator every member was measured with.
    ScaleEstimator estimator = ScaleEstimator::MedianAbsoluteDeviation;

    /// How many leading entries of `channels` are meaningful.
    std::uint32_t channelCount = 0u;

    /// How many frames were combined.
    std::uint32_t memberCount = 0u;

    /// Each channel's combined description. The selection counts are summed over the members.
    std::array<NormalizationChannelStatistics, kMaxNormalizationChannels> channels{};
};

/// The affine one channel is written through: `sample * scale + offset`.
struct NormalizationChannelCoefficients
{
    /// The factor. Finite and never zero.
    double scale = 1.0;

    /// The term added after scaling. Finite, and exactly zero under a multiplicative method.
    double offset = 0.0;
};

/// One frame's match against the reference.
///
/// The coefficients any apply call accepts. It carries the method that produced it because the
/// two multiplicative methods write the product alone, which preserves the sign of a zero; a caller
/// that reuses a serialized match states the same method again.
struct FrameNormalization
{
    /// How the frame is brought to the reference.
    NormalizationMethod method = NormalizationMethod::Additive;

    /// Which estimator the match was derived from.
    ScaleEstimator estimator = ScaleEstimator::MedianAbsoluteDeviation;

    /// How many leading entries of `channels` are meaningful.
    std::uint32_t channelCount = 0u;

    /// Each channel's scale and offset, in channel order.
    std::array<NormalizationChannelCoefficients, kMaxNormalizationChannels> channels{};

    /// Channels whose dispersion ratio was replaced by one because a scale was zero.
    std::uint32_t dispersionFallbacks = 0u;

    /// Channels whose multiplicative location ratio was replaced by one because the frame's
    /// location was zero.
    std::uint32_t locationFallbacks = 0u;
};

/// The match that changes nothing.
///
/// What a primary reference frame is assigned without measuring it against the combined reference. A
/// caller pinning a frame states it here rather than the engine deciding.
///
/// The estimator is retained for provenance; applying identity coefficients does not measure it.
///
/// @param method the method the identity is spelled for.
/// @param channelCount how many channels, at most `kMaxNormalizationChannels`.
/// @param estimator the estimator used for the surrounding frame set.
/// @return the identity; `ErrorCode::InvalidArgument` for an unknown method or estimator, or a channel count
///         outside its range.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameNormalization>
identityNormalization(NormalizationMethod method, std::uint32_t channelCount,
                      ScaleEstimator estimator = ScaleEstimator::MedianAbsoluteDeviation);

/// The maximum scratch held while measuring one whole channel for global normalization.
///
/// Channels run sequentially. The compact measurement and its bounded fallback both fit this
/// figure. Thread-safe and allocation-free except for a failure it describes.
///
/// @param width the measured width in pixels, greater than zero.
/// @param height the measured height in pixels, greater than zero.
/// @param sampleType Float32 or Float64, as the measurement reads it.
/// @param workers the admitted concurrency, greater than zero.
/// @return the scratch bytes; `ErrorCode::InvalidArgument` for an empty geometry, a representation
///         other than Float32 and Float64, or zero workers; `ErrorCode::LimitExceeded` when the
///         workspace size cannot be represented.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint64_t> globalNormalizationWorkingBytes(std::uint32_t width,
                                                                                         std::uint32_t height,
                                                                                         SampleType sampleType,
                                                                                         std::uint32_t workers);

/// Measures one frame for normalization.
///
/// Each channel is ranked exactly under the frame's stated validity, so a registered canvas that
/// states `ValidityRule::ZeroIsNoData` measures only its covered samples and the uncovered border
/// biases nothing. Under `ScaleEstimator::IterativeKSigma` the samples within
/// `kIterativeKSigmaClipWidth` deviations of the median are ranked again and their corrected
/// biweight scale is accumulated over a partition that does not depend on the thread count, so the
/// result is bit-identical at every admitted width.
///
/// Each channel's admitted samples are first gathered into engine memory - the size
/// `globalNormalizationWorkingBytes` reports - and ranked there; when the engine budget refuses that
/// copy, the channel is ranked where it lies instead, with the same estimators and more passes.
/// Channels run one after another, and progress is reported once per channel.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame.
/// @param estimator which location and dispersion to produce.
/// @param region the rectangle to measure, or nothing for the whole frame. Must lie within it.
/// @return the measurement; `ErrorCode::NotInitialized` without an execution lease;
///         `ErrorCode::InvalidArgument` for an empty view, unknown estimator or region outside the
///         frame; `ErrorCode::EmptySelection` when the region is empty or a channel admits
///         nothing; `ErrorCode::InvalidData` when an admitted sample is not finite; a memory-budget
///         refusal only when ranking in place is refused as well; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NormalizationFrameStatistics>
measureGlobalNormalization(const ExecutionContext& execution, const NormalizationFrameView& frame,
                           ScaleEstimator estimator, std::optional<StatisticsRegion> region = {});

/// Combines measured frames into the reference the others are matched to.
///
/// Every member must share one estimator and channel count. Each channel's median, deviation,
/// location and scale is the median over the members, with an even count averaging the two middle
/// values.
///
/// @param members the measured reference frames. Must not be empty; at most 4096.
/// @return the reference; `ErrorCode::InvalidArgument` for no members, or a first member holding
///         no channels, more than `kMaxNormalizationChannels` or an unknown estimator;
///         `ErrorCode::LimitExceeded` for more than 4096 members;
///         `ErrorCode::IncompatibleArguments` when members disagree in estimator or channel count.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NormalizationReference>
combineNormalizationReferences(std::span<const NormalizationFrameStatistics> members);

/// Derives one frame's match against the reference.
///
/// The additive offset is the difference of medians and the multiplicative factor their ratio.
/// The two scaling methods take the ratio of the estimator's scales and, for the additive one,
/// the offset that moves the frame's scaled location onto the reference's. A zero scale on either
/// side leaves the dispersion ratio at one; a zero frame location leaves the multiplicative factor
/// at one; both are counted. A zero reference location under a multiplicative method would erase
/// the frame and is refused rather than written.
///
/// @param reference the combined target.
/// @param frame the measured frame, with the reference's estimator and channel count.
/// @param method how the frame is to be matched.
/// @return the match; `ErrorCode::InvalidArgument` for an unknown method or a frame with no
///         channels or more than `kMaxNormalizationChannels`; `ErrorCode::IncompatibleArguments`
///         when the frame and reference disagree in estimator or channel count;
///         `ErrorCode::InvalidData` for a non-finite statistic, or a scale that comes out zero or
///         a coefficient that comes out non-finite, which is what a multiplicative match against
///         a zero reference location produces.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameNormalization>
estimateGlobalNormalization(const NormalizationReference& reference, const NormalizationFrameStatistics& frame,
                            NormalizationMethod method);

/// One pair of frames measured over the samples they share.
///
/// `a` and `b` describe the two frames over the same set of shared positions, so their locations
/// and scales are directly comparable. `sharedSamples` is how many positions both frames admitted
/// in their first channel, which is the weight the solver gives the pair.
struct OverlapMeasurement
{
    /// The first frame's index in the set being solved.
    std::uint32_t frameA = 0u;

    /// The second frame's index in the set being solved.
    std::uint32_t frameB = 0u;

    /// How many leading entries of `a` and `b` are meaningful.
    std::uint32_t channelCount = 0u;

    /// Which estimator produced the locations and scales.
    ScaleEstimator estimator = ScaleEstimator::MedianAbsoluteDeviation;

    /// Positions both frames admitted in their first channel.
    std::uint64_t sharedSamples = 0u;

    /// The first frame over the shared positions, per channel.
    std::array<NormalizationChannelStatistics, kMaxNormalizationChannels> a{};

    /// The second frame over the shared positions, per channel.
    std::array<NormalizationChannelStatistics, kMaxNormalizationChannels> b{};

    /// How many times the selections were read.
    std::uint64_t planeScans = 0u;

    /// The most engine memory held at once, including the two shared-validity masks.
    std::uint64_t peakScratchBytes = 0u;
};

/// Measures two frames over the positions they share.
///
/// The caller states where the overlap lies in each frame; the two rectangles must have one size.
/// A position participates in a channel only when both frames admit it there under their own
/// validity, so a no-data border in either frame is excluded from both. Two packed masks the size
/// of the frames are taken from the engine context for the call and released with it. Each channel
/// of each frame is then measured as `measureGlobalNormalization` measures one, and progress is
/// reported once per channel.
///
/// @param execution the operation policy and engine-context lease.
/// @param frameA the first frame's index in the set being solved.
/// @param a the first frame.
/// @param regionA where the overlap lies in the first frame.
/// @param frameB the second frame's index in the set being solved.
/// @param b the second frame.
/// @param regionB where the overlap lies in the second frame.
/// @param estimator which location and dispersion to produce.
/// @return the pair measurement; `ErrorCode::NotInitialized` without an execution lease;
///         `ErrorCode::InvalidArgument` for an empty view, unknown estimator, equal indices or a
///         region outside its frame; `ErrorCode::IncompatibleArguments` when the frames disagree in
///         channel count or the rectangles in size; `ErrorCode::EmptySelection` when no position
///         is shared in some channel; `ErrorCode::InvalidData` for a shared non-finite sample; a
///         memory-budget refusal; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the
///         execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OverlapMeasurement>
measureOverlapStatistics(const ExecutionContext& execution, std::uint32_t frameA, const NormalizationFrameView& a,
                         const StatisticsRegion& regionA, std::uint32_t frameB, const NormalizationFrameView& b,
                         const StatisticsRegion& regionB, ScaleEstimator estimator);

/// One geometrically overlapping pair a caller has found, before it is measured.
struct OverlapCandidate
{
    /// The first frame's index.
    std::uint32_t frameA = 0u;

    /// The second frame's index.
    std::uint32_t frameB = 0u;

    /// How many pixels the geometric overlap covers, which ranks the candidate.
    std::uint64_t overlapPixels = 0u;
};

/// What a pair selection kept.
struct OverlapPairSelection
{
    /// How many candidates were selected.
    std::uint32_t selectedCount = 0u;

    /// How many selected candidates touch the reference.
    std::uint32_t referenceEdges = 0u;

    /// How many selected candidates form the spanning tree, zero under `OverlapGraphPolicy::AllPairs`.
    std::uint32_t spanningTreeEdges = 0u;

    /// Whether the selected candidates connect every frame.
    bool connected = false;
};

/// Chooses which candidate pairs to measure.
///
/// Under `OverlapGraphPolicy::ReferenceStarSpanningTree` every candidate touching the reference is
/// kept, then candidates are taken in descending overlap area - ties by frame indices - into a
/// maximum spanning tree. The selection is deterministic and does not read the frames.
///
/// @param execution the operation policy and engine-context lease, whose memory holds the
///        component table for the call.
/// @param candidates every geometrically overlapping pair.
/// @param frameCount how many frames the indices name, at most `kMaxOverlapNormalizationFrames`.
/// @param referenceIndex the frame every other is related to, below `frameCount`.
/// @param policy which candidates to keep.
/// @param selected receives one non-zero byte per kept candidate; must be exactly as long as
///        `candidates`.
/// @return what was kept; `ErrorCode::NotInitialized` without an execution lease;
///         `ErrorCode::InvalidArgument` for an unknown policy, an empty set, an index outside the
///         set, a candidate relating a frame to itself, or a `selected` span of the wrong length;
///         `ErrorCode::LimitExceeded` above the frame ceiling; or a memory-budget refusal.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OverlapPairSelection>
selectOverlapPairs(const ExecutionContext& execution, std::span<const OverlapCandidate> candidates,
                   std::uint32_t frameCount, std::uint32_t referenceIndex, OverlapGraphPolicy policy,
                   std::span<std::uint8_t> selected);

/// What one overlap solve did.
struct OverlapSolveReport
{
    /// How many frames received a match.
    std::uint32_t frameCount = 0u;

    /// How many pairs carried at least one shared sample.
    std::uint32_t usablePairs = 0u;

    /// Channels whose dispersion system was replaced by unity factors.
    std::uint32_t dispersionSubstitutions = 0u;

    /// Channels whose dispersion-scaled additive system was replaced by the plain offsets.
    std::uint32_t additiveScalingSubstitutions = 0u;

    /// How many overlap systems were attempted, counting a subsidiary one that was found
    /// disconnected or singular and substituted.
    std::uint32_t systemsSolved = 0u;
};

/// Solves one set of frames' matches from their pairwise overlap measurements.
///
/// Per channel, the offsets that best reconcile every pair's locations and the factors that best
/// reconcile them multiplicatively are each the solution of a weighted least-squares system with
/// the reference fixed at the identity and each pair weighted by its shared sample count. The
/// scaling methods solve the same multiplicative system over the pairs' scales for the dispersion
/// factors, and the additive-scaling method then solves the additive system over the dispersion
/// scaled locations; the multiplicative-scaling factor is the dispersion factor times the location
/// factor. Each system is a dense Float64 elimination with partial pivoting, taken from the engine
/// context for the call. A pair takes part in a system only when it shares samples and its two
/// values are finite - and, in a multiplicative system, both above `kOverlapValueEpsilon`; a system
/// whose taking-part pairs do not connect every frame to the reference is refused rather than
/// solved. Progress is reported once per channel. Coefficients are staged in context memory and
/// committed only on success; every failure leaves `frames` unchanged, including cancellation and
/// an allocation refusal.
///
/// @param execution the operation policy and engine-context lease.
/// @param pairs the measured pairs, all with one channel count and estimator. Must not be empty.
/// @param referenceIndex the frame that keeps the identity, below `frames.size()`.
/// @param method how every frame is matched.
/// @param degeneracy what to do when a subsidiary system cannot be solved.
/// @param frames receives one match per frame; its length is the frame count, at most
///        `kMaxOverlapNormalizationFrames`.
/// @return the report; `ErrorCode::NotInitialized` without an execution lease;
///         `ErrorCode::InvalidArgument` for an unknown method or policy, no frames, no pairs, a
///         reference outside the set, a pair naming a frame outside it or itself, or a first pair
///         holding no channels or more than `kMaxNormalizationChannels` or naming an unknown
///         estimator;
///         `ErrorCode::IncompatibleArguments` when pairs disagree in channel count or estimator;
///         `ErrorCode::LimitExceeded` above the frame ceiling; `ErrorCode::InvalidData` when a
///         required system - or, under `OverlapDegeneracyPolicy::Refuse`, a subsidiary one - is
///         disconnected or singular, or a scale comes out zero or a coefficient non-finite; a
///         memory-budget refusal; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the
///         execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OverlapSolveReport>
solveOverlapNormalization(const ExecutionContext& execution, std::span<const OverlapMeasurement> pairs,
                          std::uint32_t referenceIndex, NormalizationMethod method, OverlapDegeneracyPolicy degeneracy,
                          std::span<FrameNormalization> frames);

/// What one completed apply reports.
struct NormalizationApplyResult
{
    /// The implementation and instruction set that actually wrote the samples.
    BackendSelection backend{};

    /// Whether the scalar oracle or a compiled row ran.
    NormalizationExecutionPath path = NormalizationExecutionPath::Reference;

    /// Samples written, transformed or copied.
    std::uint64_t samplesWritten = 0u;

    /// Samples the validity rule admitted and the affine was applied to.
    std::uint64_t samplesTransformed = 0u;

    /// Samples the validity rule excluded and copied unchanged.
    std::uint64_t samplesPreserved = 0u;

    /// Transformed samples that landed exactly on the zero marker under
    /// `ValidityRule::ZeroIsNoData` and were moved to the smallest normal magnitude.
    std::uint64_t noDataCollisions = 0u;

    /// Workers the scheduler could use for the row tasks.
    std::uint32_t plannedWorkers = 0u;

    /// Full destination passes, always one.
    std::uint32_t imagePasses = 0u;
};

/// The published effect of applying a normalization.
///
/// It preserves geometry, astrometry, source cards and the frame's validity claim, and restates
/// the processing meaning as normalized.
inline constexpr OperationEffect kGlobalNormalizationEffect{.samples = SampleEffect::Writes,
                                                            .semantics = SemanticEffect::Restates,
                                                            .validity =
                                                                ValidityTransition{.policy = ValidityPolicy::Preserve}};

/// Writes one frame through its match.
///
/// Every admitted sample becomes `sample * scale + offset` - or the product alone under a
/// multiplicative method - computed in Float64 and narrowed once to the resident type. Every sample
/// the validity rule excludes is copied unchanged, so a no-data marker survives. A transformed sample
/// that lands exactly on zero under `ValidityRule::ZeroIsNoData` would become no-data downstream; it
/// is moved to the smallest normal magnitude with the sign of the scaled sample and counted. The
/// destination may be the source itself, laid out with the source's own strides, or
/// storage disjoint from it; any other overlap is refused. Rows under a value rule whose
/// destination shares the source's planar or interleaved arrangement run the selected CPU table's
/// compiled row when that instruction set has one; a packed mask, a differing arrangement, the
/// reference backend or an instruction set without a compiled row runs the scalar oracle. Both
/// store the same bits, and the result names the path that ran.
///
/// @param execution the operation policy and engine-context lease.
/// @param normalization the match, with the frame's channel count.
/// @param source the checked frame. `ValidityRule::DomainRange` is refused, because the stated
///        range describes the untransformed values.
/// @param destination where output storage starts, or `source.samples()` for an in-place write.
/// @param destinationBytes bytes available from `destination`, covering every byte named by
///        `destinationLayout`.
/// @param destinationLayout where the output samples sit, describing the same image as the source.
/// @return execution counters and the actual backend; `ErrorCode::NotInitialized` without a lease;
///         `ErrorCode::InvalidArgument` for an empty view, an unknown method or estimator, a match
///         with no channels or too many, a non-finite or zero scale or non-finite offset, or null,
///         misaligned or undersized destination storage; `ErrorCode::IncompatibleArguments` when
///         the match and frame disagree in channel count or the destination layout's extent,
///         element size or arrangement does not describe the frame; `ErrorCode::Unsupported` for a
///         range rule; `ErrorCode::Aliasing` for an overlap other than an exact in-place write;
///         `ErrorCode::UnsupportedBackend` when the execution requires the CPU backend with
///         fallback forbidden and only the scalar oracle can serve the call;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NormalizationApplyResult>
applyGlobalNormalization(const ExecutionContext& execution, const FrameNormalization& normalization,
                         const NormalizationFrameView& source, void* destination, std::uint64_t destinationBytes,
                         const ImageLayout& destinationLayout);

/// Builds the metadata transition for a normalized result.
///
/// The new meaning marks the frame as normalized and changes nothing else about it: the transfer
/// function, every other processing flag and the validity claim carry forward, and provenance
/// records the method, the estimator and every channel's coefficients so a later caller never
/// infers them from the pixels.
///
/// @param normalization the match that was applied.
/// @param source the frame's current processing meaning.
/// @return the checked transition ready for `Image::transition`; `ErrorCode::InvalidArgument` for
///         a match `applyGlobalNormalization` would refuse on its own - no channels or too many, an
///         unknown method or estimator, a zero or non-finite scale, a non-finite offset - or
///         whatever rebuilding the semantics or provenance reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
globalNormalizationTransition(const FrameNormalization& normalization, const ImageSemantics& source);

} // namespace an
