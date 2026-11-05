// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The vocabulary a plan and a backend use to agree on one kernel.
///
/// A kernel is selected once, outside its sample loop. The selection can only be checked when the
/// facts which distinguish implementations are values rather than assumptions: stored and compute
/// representations, numeric domain and arrangement, stride and alias rules, validity, borders,
/// accuracy, backend, and instruction set. This header is that small value vocabulary.
///
/// The registry contains implementations, not global state. It is an immutable span over constants
/// compiled into the engine, so two contexts see the same build capabilities and a forced
/// conformance run cannot race with lazy registration. A lookup is exact: an unavailable backend or
/// instruction set returns no registration rather than quietly running a different one.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <cstdint>
#include <span>

namespace an
{

/// The memory-access pattern shared by a group of kernels.
///
/// Families are deliberately about access shape rather than astronomy domains. A convolution and
/// a local statistic are both stencils; a drizzle deposit and a star-sample collection are both
/// gather/scatter work. That is what lets their scheduling and backend constraints be reused.
enum class KernelFamily : std::uint8_t
{
    /// No known family. The default and the answer for an unknown operation.
    Unknown = 0u,

    /// Copies, conversions, channel movement, and layout packing.
    Transfer = 1u,

    /// Independent per-sample arithmetic, including fused expressions.
    Pointwise = 2u,

    /// Many inputs combined into one or a small fixed result.
    Reduction = 3u,

    /// Histograms, selection, quantiles, medians, and related order work.
    HistogramOrder = 4u,

    /// Neighborhood work with borders, halos, or separable passes.
    Stencil = 5u,

    /// Coordinate-driven sampling and image warps.
    Resample = 6u,

    /// Indexed reads or writes, including deterministic conflict handling.
    GatherScatter = 7u,

    /// Planned transforms such as FFT and multiscale decompositions.
    Transform = 8u,

    /// Tiled or streamed work whose contract includes passes and I/O overlap.
    StackStream = 9u
};

/// Whether a value names one of the usable kernel families.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param family the value to test.
/// @return true for every member except `KernelFamily::Unknown`.
[[nodiscard]] constexpr bool isKernelFamily(KernelFamily family) noexcept
{
    return static_cast<std::uint8_t>(family) >= static_cast<std::uint8_t>(KernelFamily::Transfer) &&
           static_cast<std::uint8_t>(family) <= static_cast<std::uint8_t>(KernelFamily::StackStream);
}

/// The family's stable name for diagnostics, manifests, and tests.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param family the family to name.
/// @return the enumerator spelling, or `"UnknownKernelFamily"` for `Unknown` and values outside
///         the enumeration. The returned string has static storage duration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* kernelFamilyName(KernelFamily family) noexcept;

/// The versioned operations currently represented by the kernel registry.
///
/// These names identify numeric contracts, not user-facing algorithms. A new version of an
/// existing operation keeps the enumerator and changes `KernelId::version`; a genuinely different
/// operation receives a new enumerator.
enum class KernelOperation : std::uint16_t
{
    /// No known operation. The default invalid value.
    Unknown = 0u,

    /// Float32 plane copy, preserving invalid samples by leaving their outputs untouched.
    TransferCopy = 1u,

    /// Float32 scale followed by bias, with a defined double intermediate and Float32 narrowing.
    PointwiseAffine = 2u,

    /// Row-major Float32 sum accumulated into Float64.
    ReductionSum = 3u,

    /// Exact bounded-domain histogram into caller-owned bins.
    Histogram = 4u,

    /// Exact zero-based order statistic using caller-owned scratch.
    OrderStatistic = 5u,

    /// Direct three-by-three Float32 stencil with clamp or constant borders.
    Stencil3x3 = 6u,

    /// Nearest-neighbor sampling at pixel-center coordinates.
    ResampleNearest = 7u,

    /// Indexed nearest-neighbor reads into a linear output run.
    Gather = 8u,

    /// Indexed nearest-neighbor writes with an explicit deterministic conflict rule.
    Scatter = 9u,

    /// Linear interpolation of each filter colour over the sites that recorded it.
    DemosaicBilinear = 10u,

    /// Gradient-weighted green with colour-difference red and blue over a two-by-two mosaic.
    DemosaicFast = 11u,

    /// Ratio-corrected demosaicing over a two-by-two mosaic.
    DemosaicRcd = 12u,

    /// Aliasing-minimised demosaicing over a two-by-two mosaic, with a stated clip point.
    DemosaicAmaze = 13u,

    /// Three-by-three weighted average over a tabulated mosaic.
    DemosaicXTransFast = 14u,

    /// Directional Markesteijn interpolation over a tabulated mosaic, one or three passes.
    DemosaicMarkesteijn = 15u,

    /// The shared three-by-three border rule every demosaic method leaves its outer band to.
    DemosaicBorder = 16u,

    /// One colour pixel per repeating block, from the like-filtered sites inside it.
    CfaSuperPixel = 17u,

    /// One plane per position in a mosaic's repeating block, and its exact inverse.
    CfaBlockPlanes = 18u,

    /// Directional linear minimum mean-square-error Bayer interpolation.
    DemosaicLmmse = 19u,

    /// Variable-number-of-gradients Bayer interpolation.
    DemosaicVng4 = 20u,

    /// Luminance-only adaptive homogeneity-directed Bayer interpolation.
    DemosaicLuminanceAhd = 21u,

    /// One validated pointwise program, evaluated pixel by pixel at its declared width.
    ///
    /// Unlike every operation above, the identifier does not fix the arithmetic: the program travels as
    /// the operation's arguments. `Algorithms/Pointwise/PointwiseProgramKernel.h` states the plan, the
    /// argument structure and the bindings a caller and a backend agree on, and the scalar oracle is
    /// `evaluatePointwisePixel`.
    PointwiseProgram = 22u
};

/// Whether a value names an operation this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param operation the value to test.
/// @return true for every member except `KernelOperation::Unknown`.
[[nodiscard]] constexpr bool isKernelOperation(KernelOperation operation) noexcept
{
    return static_cast<std::uint16_t>(operation) >= static_cast<std::uint16_t>(KernelOperation::TransferCopy) &&
           static_cast<std::uint16_t>(operation) <= static_cast<std::uint16_t>(KernelOperation::PointwiseProgram);
}

/// The access family an operation belongs to.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param operation the operation to classify.
/// @return its family, or `KernelFamily::Unknown` for an unknown value.
[[nodiscard]] constexpr KernelFamily kernelFamilyOf(KernelOperation operation) noexcept
{
    switch (operation)
    {
    case KernelOperation::TransferCopy:
        return KernelFamily::Transfer;
    case KernelOperation::PointwiseAffine:
    case KernelOperation::PointwiseProgram:
        return KernelFamily::Pointwise;
    case KernelOperation::ReductionSum:
        return KernelFamily::Reduction;
    case KernelOperation::Histogram:
    case KernelOperation::OrderStatistic:
        return KernelFamily::HistogramOrder;
    case KernelOperation::Stencil3x3:
        return KernelFamily::Stencil;
    case KernelOperation::ResampleNearest:
        return KernelFamily::Resample;
    case KernelOperation::Gather:
    case KernelOperation::Scatter:
        return KernelFamily::GatherScatter;
    case KernelOperation::DemosaicBilinear:
    case KernelOperation::DemosaicFast:
    case KernelOperation::DemosaicRcd:
    case KernelOperation::DemosaicAmaze:
    case KernelOperation::DemosaicXTransFast:
    case KernelOperation::DemosaicMarkesteijn:
    case KernelOperation::DemosaicBorder:
    case KernelOperation::CfaSuperPixel:
    case KernelOperation::DemosaicLmmse:
    case KernelOperation::DemosaicVng4:
    case KernelOperation::DemosaicLuminanceAhd:
        return KernelFamily::Stencil;
    case KernelOperation::CfaBlockPlanes:
        return KernelFamily::Transfer;
    case KernelOperation::Unknown:
        return KernelFamily::Unknown;
    }
    return KernelFamily::Unknown;
}

/// The operation's stable name for diagnostics, manifests, and tests.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param operation the operation to name.
/// @return the enumerator spelling, or `"UnknownKernelOperation"` for an unknown value. The
///         returned string has static storage duration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* kernelOperationName(KernelOperation operation) noexcept;

/// One revision of one numeric operation.
///
/// The version changes when the scalar formula, exceptional-value behavior, coordinate rule, or
/// another part of the result contract changes. Backend optimization alone does not change it.
struct KernelId
{
    /// Which operation this is.
    KernelOperation operation = KernelOperation::Unknown;

    /// Which revision of that operation's contract. Zero is invalid; versions start at one.
    std::uint16_t version = 0u;

    /// Two IDs agree only when both operation and version agree.
    friend constexpr bool operator==(const KernelId& left, const KernelId& right) noexcept = default;
};

/// Whether an operation ID is usable.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param id the ID to test.
/// @return true when the operation is known and the version is nonzero.
[[nodiscard]] constexpr bool isKernelId(const KernelId& id) noexcept
{
    return isKernelOperation(id.operation) && id.version != 0u;
}

/// How many samples from one image pixel a kernel accepts at once.
///
/// The same ordered vocabulary is used by capabilities and plans: `SingleChannel` is the narrowest
/// request, `MatchingChannels` also covers a one-channel pair, and `Any` is the widest capability.
enum class KernelChannelPolicy : std::uint8_t
{
    /// Exactly one sample per pixel: a planar channel or a monochrome plane.
    SingleChannel,

    /// Inputs and outputs may have several samples per pixel, and their counts must agree.
    MatchingChannels,

    /// Any checked channel shape the operation-specific plan accepts.
    Any
};

/// Which row strides an implementation accepts.
enum class KernelStridePolicy : std::uint8_t
{
    /// Rows must follow one another with no padding.
    ContiguousOnly,

    /// A checked positive row stride may contain padding after the logical row.
    RowStrided
};

/// Which overlap between input and output is legal.
enum class KernelAliasPolicy : std::uint8_t
{
    /// No input and output byte may overlap.
    Disjoint,

    /// Disjoint buffers and exact in-place identity are legal; partial overlap is not.
    ExactInPlace
};

/// Which validity descriptions an implementation can honor directly.
enum class KernelValidityCapability : std::uint8_t
{
    /// Only an all-valid input.
    AllValidOnly,

    /// All-valid and every rule decided from a sample value; no packed mask.
    ValueRules,

    /// Every value rule and an explicit packed mask.
    ValueRulesAndMask
};

/// How an implementation's numeric error is measured against the operation's scalar oracle.
enum class KernelAccuracyMetric : std::uint8_t
{
    /// The result must equal the oracle exactly under the operation's stated representation.
    Exact,

    /// Maximum distance in representable floating-point values.
    Ulp,

    /// Maximum absolute numeric difference.
    Absolute,

    /// Maximum difference relative to the oracle magnitude.
    Relative
};

/// The accepted numeric difference from the scalar oracle.
struct KernelAccuracyContract
{
    /// Which comparison is made.
    KernelAccuracyMetric metric = KernelAccuracyMetric::Exact;

    /// The inclusive maximum difference in the metric's units. Zero for `Exact`.
    double maximumError = 0.0;

    /// Two accuracy contracts agree when both the metric and its limit agree.
    friend constexpr bool operator==(const KernelAccuracyContract& left,
                                     const KernelAccuracyContract& right) noexcept = default;
};

/// Which border modes a kernel invocation may select.
enum class KernelBorderCapability : std::uint8_t
{
    /// The operation does not read outside an input footprint and has no border choice.
    None,

    /// Both edge clamping and a caller-stated constant are implemented.
    ClampAndConstant,

    /// Every mode below, both reflections and the periodic one included.
    ///
    /// A neighborhood operation needs the reflections a single-sample lookup does not: a
    /// convolution clamped at an edge biases its result toward the edge sample, which is why
    /// reflection is what image filtering conventionally uses there. Coordinate sampling keeps the
    /// narrower capability, so a plan asking a nearest-neighbor lookup to reflect is refused rather
    /// than quietly clamped.
    EveryBorderMode
};

/// The border behavior selected by one plan.
///
/// The four resolvable modes name a source coordinate for one outside a footprint; `Constant` names
/// no coordinate at all and supplies a value instead, which is why an implementation treats it
/// separately rather than as a fifth index rule.
enum class KernelBorderMode : std::uint8_t
{
    /// No border behavior applies to this operation.
    NotApplicable,

    /// Coordinates outside the input use the nearest edge sample.
    Clamp,

    /// Coordinates outside the input use a caller-stated constant.
    Constant,

    /// The input is mirrored about a line half a sample outside it, so the edge sample repeats.
    ///
    /// For a row `a b c d`, index -1 reads `a` and index -2 reads `b`.
    Reflect,

    /// The input is mirrored about the edge sample, which is therefore not repeated.
    ///
    /// For a row `a b c d`, index -1 reads `b` and index -2 reads `c`. This is the rule image
    /// filtering conventionally uses, and the one that leaves a linear ramp linear across an edge.
    ReflectExcludingEdge,

    /// The input repeats periodically, so index -1 reads the last sample of the row.
    Wrap
};

/// Whether a border mode answers an outside coordinate with a coordinate inside the input.
///
/// `KernelBorderMode::Constant` supplies a value rather than a coordinate and `NotApplicable`
/// supplies neither, so an implementation handles those two separately from the index rules.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param border the mode to test.
/// @return true when the mode names an inside coordinate for every outside one.
[[nodiscard]] constexpr bool kernelBorderResolvesCoordinate(KernelBorderMode border) noexcept
{
    return border == KernelBorderMode::Clamp || border == KernelBorderMode::Reflect ||
           border == KernelBorderMode::ReflectExcludingEdge || border == KernelBorderMode::Wrap;
}

/// Whether a value names one of the border behaviors a plan may select.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param border the value to test.
/// @return true for every member, `NotApplicable` included, and false outside the enumeration.
[[nodiscard]] constexpr bool isKernelBorderMode(KernelBorderMode border) noexcept
{
    return static_cast<std::uint8_t>(border) <= static_cast<std::uint8_t>(KernelBorderMode::Wrap);
}

/// The inside coordinate one border rule answers an outside coordinate with.
///
/// One function rather than one per kernel, because two implementations of the same border rule
/// that differ by a sample at an edge are the hardest kind of difference to see: every interior
/// pixel agrees. It is closed-form rather than iterative, so an index far outside the input costs
/// the same as one just outside it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param index the coordinate, which may be negative or beyond the input.
/// @param size how many samples the axis holds. Must be positive; a size that is not is returned as
///        the answer.
/// @param border the rule. `Constant` and `NotApplicable` name no coordinate.
/// @return the inside coordinate, or `size` when the mode supplies a value instead of a coordinate
///         and the index is outside. An index already inside is always returned unchanged.
[[nodiscard]] constexpr std::int64_t kernelBorderIndex(std::int64_t index, std::int64_t size,
                                                       KernelBorderMode border) noexcept
{
    if (index >= 0 && index < size)
    {
        return index;
    }
    if (size <= 0)
    {
        return size;
    }
    switch (border)
    {
    case KernelBorderMode::Clamp:
        return index < 0 ? 0 : size - 1;
    case KernelBorderMode::Reflect: {
        const std::int64_t period = 2 * size;
        std::int64_t folded = index % period;
        if (folded < 0)
        {
            folded += period;
        }
        return folded < size ? folded : period - 1 - folded;
    }
    case KernelBorderMode::ReflectExcludingEdge: {
        if (size == 1)
        {
            return 0;
        }
        const std::int64_t period = 2 * size - 2;
        std::int64_t folded = index % period;
        if (folded < 0)
        {
            folded += period;
        }
        return folded < size ? folded : period - folded;
    }
    case KernelBorderMode::Wrap: {
        std::int64_t folded = index % size;
        if (folded < 0)
        {
            folded += size;
        }
        return folded;
    }
    case KernelBorderMode::Constant:
    case KernelBorderMode::NotApplicable:
        break;
    }
    return size;
}

/// How coordinate values are interpreted.
enum class KernelCoordinateConvention : std::uint8_t
{
    /// The operation has no coordinate input.
    None,

    /// Integer coordinates name pixel centers; nearest sampling rounds by `floor(value + 0.5)`.
    PixelCenters
};

/// Where temporary working memory comes from.
enum class KernelScratchPolicy : std::uint8_t
{
    /// The kernel needs no scratch memory.
    None,

    /// The caller supplies a bounded span whose required size is validated before work starts.
    CallerProvided,

    /// A device implementation states the bytes on its prepared kernel; the device allocates,
    /// accounts and supplies that temporary resource for each accepted run.
    DeviceAllocated
};

/// How an implementation uses the operation's admitted threads.
enum class KernelParallelPolicy : std::uint8_t
{
    /// The authoritative reference walks in one deterministic calling-thread order.
    Serial,

    /// Work is partitioned through the operation's task arena.
    OperationArena
};

/// Where an implementation observes cancellation.
enum class KernelCancellationPolicy : std::uint8_t
{
    /// The body has no checkpoint because it is bounded constant work.
    None,

    /// Before output and at each row or bounded linear chunk.
    PerRowOrChunk
};

/// What one registered implementation is able to execute.
///
/// This is operation metadata rather than a run-time shape. The corresponding choices for one
/// invocation live in `KernelPlanKey` and are matched through `kernelContractSupports`.
struct KernelCapabilities
{
    /// Representation read from the primary input buffer.
    SampleType inputSample = SampleType::Float32;

    /// Representation written to the primary output or scalar result.
    SampleType outputSample = SampleType::Float32;

    /// Representation in which the operation's arithmetic is defined.
    SampleType computeSample = SampleType::Float32;

    /// Meaning required of input magnitudes.
    NumericDomain inputDomain = NumericDomain::NormalizedLinear;

    /// Meaning produced for output magnitudes.
    NumericDomain outputDomain = NumericDomain::NormalizedLinear;

    /// Arrangement required of image inputs.
    StorageArrangement inputArrangement = StorageArrangement::Planar;

    /// Arrangement required of image outputs.
    StorageArrangement outputArrangement = StorageArrangement::Planar;

    /// Accepted channel shape.
    KernelChannelPolicy channels = KernelChannelPolicy::SingleChannel;

    /// Widest stride behavior accepted. `RowStrided` also accepts contiguous rows.
    KernelStridePolicy strides = KernelStridePolicy::ContiguousOnly;

    /// Smallest byte alignment required of every primary buffer.
    std::uint32_t minimumAlignmentBytes = 1u;

    /// Widest legal overlap. `ExactInPlace` also accepts disjoint buffers.
    KernelAliasPolicy alias = KernelAliasPolicy::Disjoint;

    /// Widest validity mechanism the implementation honors.
    KernelValidityCapability validity = KernelValidityCapability::AllValidOnly;

    /// Border choices the operation understands.
    KernelBorderCapability borders = KernelBorderCapability::None;

    /// Samples read beyond every edge of a produced tile. Zero for pointwise operations.
    std::uint32_t haloRadius = 0u;

    /// Meaning of coordinate values, or `None` when there are none.
    KernelCoordinateConvention coordinates = KernelCoordinateConvention::None;

    /// How temporary memory is supplied.
    KernelScratchPolicy scratch = KernelScratchPolicy::None;

    /// How admitted concurrency is used.
    KernelParallelPolicy parallelism = KernelParallelPolicy::Serial;

    /// Where stop and deadline requests are observed.
    KernelCancellationPolicy cancellation = KernelCancellationPolicy::PerRowOrChunk;

    /// Required agreement with the scalar operation contract.
    KernelAccuracyContract accuracy{};

    /// Whether a logical run whose size is not a vector width is handled without padding.
    bool tails = false;

    /// Whether executing this operation requires persistent state built by a reusable plan.
    bool persistentPlan = false;
};

/// The operation-level contract shared by every implementation of one kernel ID.
struct KernelContract
{
    /// The versioned numeric operation.
    KernelId id{};

    /// The combinations this implementation shape accepts.
    KernelCapabilities capabilities{};

    /// Stable scalar scenario name used to identify the conformance oracle.
    ///
    /// Borrowed from static storage and never null in the registry. A sink or manifest that keeps
    /// it may keep the pointer for the process lifetime.
    const char* referenceScenarioId = "";
};

/// The choices a planner fixes before dispatching one kernel.
///
/// It deliberately carries no pointers, extents, or strides. Those are invocation data; this is
/// the reusable cache and dispatch identity for the kinds of buffers and policies involved.
struct KernelPlanKey
{
    /// Which operation revision is planned.
    KernelId id{};

    /// Primary input representation.
    SampleType inputSample = SampleType::Float32;

    /// Primary output or scalar-result representation.
    SampleType outputSample = SampleType::Float32;

    /// Arithmetic representation.
    SampleType computeSample = SampleType::Float32;

    /// Input numeric meaning.
    NumericDomain inputDomain = NumericDomain::NormalizedLinear;

    /// Output numeric meaning.
    NumericDomain outputDomain = NumericDomain::NormalizedLinear;

    /// Input image arrangement.
    StorageArrangement inputArrangement = StorageArrangement::Planar;

    /// Output image arrangement.
    StorageArrangement outputArrangement = StorageArrangement::Planar;

    /// Channel shape selected by the plan.
    KernelChannelPolicy channels = KernelChannelPolicy::SingleChannel;

    /// Stride behavior the invocation may use.
    KernelStridePolicy stride = KernelStridePolicy::ContiguousOnly;

    /// Alignment in bytes guaranteed for every primary buffer. A nonzero power of two.
    std::uint32_t bufferAlignmentBytes = 1u;

    /// Overlap the invocation may use.
    KernelAliasPolicy alias = KernelAliasPolicy::Disjoint;

    /// Validity rule carried by the input.
    ValidityRule validity = ValidityRule::AllValid;

    /// Border behavior selected for this invocation.
    KernelBorderMode border = KernelBorderMode::NotApplicable;

    /// Samples the invocation reads beyond every edge of a produced tile.
    std::uint32_t haloRadius = 0u;

    /// Meaning of coordinate inputs, or `None` when the operation has none.
    KernelCoordinateConvention coordinates = KernelCoordinateConvention::None;

    /// How the invocation supplies temporary memory.
    KernelScratchPolicy scratch = KernelScratchPolicy::None;

    /// Whether the invocation depends on persistent reusable plan state.
    bool persistentPlan = false;

    /// Accuracy the plan requires.
    KernelAccuracyContract accuracy{};

    /// Two plan keys agree when every planning choice agrees.
    friend constexpr bool operator==(const KernelPlanKey& left, const KernelPlanKey& right) noexcept = default;
};

/// The exact implementation lookup key.
struct KernelDispatchKey
{
    /// The operation and buffer-policy choices.
    KernelPlanKey plan{};

    /// The implementation family to force. Never silently substituted during lookup.
    ComputeBackend backend = ComputeBackend::Reference;

    /// The compiled instruction set to force.
    InstructionSet instructionSet = InstructionSet::Scalar;

    /// Two dispatch keys agree when plan, backend, and instruction set all agree.
    friend constexpr bool operator==(const KernelDispatchKey& left, const KernelDispatchKey& right) noexcept = default;
};

/// One implementation advertised by the immutable kernel registry.
struct KernelRegistration
{
    /// The operation and capability metadata.
    KernelContract contract{};

    /// Which backend owns this implementation.
    ComputeBackend backend = ComputeBackend::Reference;

    /// Which instruction set its compiled code declares.
    InstructionSet instructionSet = InstructionSet::Scalar;
};

/// The canonical plan accepted by a contract.
///
/// Chooses disjoint buffers, all-valid input, the widest stride the implementation supports, and
/// clamp borders for a contract whose borders are `KernelBorderCapability::ClampAndConstant`.
/// Callers copy and narrow this value to describe a particular plan; `kernelContractSupports`
/// checks the result.
///
/// Thread-safe, reentrant, allocation-free, and usable in a constant expression.
///
/// @param contract the operation and its capabilities.
/// @return a plan key carrying all of the contract's numeric and memory choices, with Clamp for
///         every contract that supports borders.
[[nodiscard]] constexpr KernelPlanKey kernelPlanFor(const KernelContract& contract) noexcept
{
    return KernelPlanKey{.id = contract.id,
                         .inputSample = contract.capabilities.inputSample,
                         .outputSample = contract.capabilities.outputSample,
                         .computeSample = contract.capabilities.computeSample,
                         .inputDomain = contract.capabilities.inputDomain,
                         .outputDomain = contract.capabilities.outputDomain,
                         .inputArrangement = contract.capabilities.inputArrangement,
                         .outputArrangement = contract.capabilities.outputArrangement,
                         .channels = contract.capabilities.channels,
                         .stride = contract.capabilities.strides,
                         .bufferAlignmentBytes = contract.capabilities.minimumAlignmentBytes,
                         .alias = KernelAliasPolicy::Disjoint,
                         .validity = ValidityRule::AllValid,
                         .border = contract.capabilities.borders != KernelBorderCapability::None
                                       ? KernelBorderMode::Clamp
                                       : KernelBorderMode::NotApplicable,
                         .haloRadius = contract.capabilities.haloRadius,
                         .coordinates = contract.capabilities.coordinates,
                         .scratch = contract.capabilities.scratch,
                         .persistentPlan = contract.capabilities.persistentPlan,
                         .accuracy = contract.capabilities.accuracy};
}

/// Whether one contract can execute a planned combination.
///
/// The comparison is capability-aware: a wider channel policy accepts a narrower plan, a
/// row-strided implementation accepts contiguous input, exact-in-place capability accepts disjoint
/// buffers, mask capability accepts the cheaper value rules, stronger buffer alignment satisfies a
/// weaker requirement (both must be powers of two), a border capability accepts any mode it lists,
/// and an implementation whose error ceiling is at or below the plan's satisfies it when both state
/// the same metric with finite, non-negative ceilings. Every numeric type, domain, arrangement,
/// operation version, coordinate, halo, scratch, and persistent-plan choice is otherwise exact, and
/// a contract whose own identifier is not usable supports nothing.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param contract the registered capability to test.
/// @param plan the choices a planner made.
/// @return true when the implementation can run that plan without fallback or reinterpretation.
[[nodiscard]] AFTERNIGHTENGINE_API bool kernelContractSupports(const KernelContract& contract,
                                                               const KernelPlanKey& plan) noexcept;

/// Every kernel implementation compiled into the registry.
///
/// The span and its elements have static storage duration and are immutable. It holds one
/// version-one scalar reference registration for each operation from `TransferCopy` to `Scatter`;
/// the demosaic, CFA and pointwise-program operations have identifiers but no registration here.
/// An implementation for another backend is added beside these as a registration of its own rather
/// than replacing one or mutating the span.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @return the complete registry in a stable order: by operation, then backend, then instruction
///         set.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const KernelRegistration> kernelRegistry() noexcept;

/// Finds the implementation for one exact dispatch request.
///
/// No fallback is performed. A caller that permits fallback changes its plan or backend request in
/// the planner, records that decision, and asks again; lookup itself never makes an unavailable
/// forced backend appear to have run.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer, when present, has static
/// storage duration.
///
/// @param key the planned operation, backend, and compiled instruction set.
/// @return the matching registration, or null when none can serve every field of the key.
[[nodiscard]] AFTERNIGHTENGINE_API const KernelRegistration*
findKernelRegistration(const KernelDispatchKey& key) noexcept;

} // namespace an
