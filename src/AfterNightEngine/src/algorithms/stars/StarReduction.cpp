// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - direct star reduction, residual extraction and compatibility masking.
//
// Every sample is read through the default `ConversionPolicy` into a double and refused unless it
// is finite and inside the unit interval; every result is clamped to that interval, counted when
// clamping changed it, and written back through the same policy. A denominator within 1e-12 of zero
// takes the compatibility value zero instead of being divided by.
//
// Reduction has two paths and one answer. When no lightness conversion is needed and the three
// layouts are contiguous with one arrangement, the images are walked as flat runs of samples in
// blocks of 256, each block checked whole before any of it is written and each step of the equation
// applied across the block as a loop with no branch in it; otherwise every sample is addressed
// through its layout and `reduceValue` is evaluated one sample at a time. The block path takes each
// sample through the same operations in the same order, so the two agree to the bit.
//
// The protection mask is a fixed sequence on three Float32 working planes taken from the engine
// context: grey average, rescale by extrema, unscreen ratio, circular median, stretch, cross
// dilation, box smoothing and inversion. Each neighbourhood stage replicates the frame's edge and
// writes a plane the next stage reads.

#include <AfterNightEngine/Algorithms/Stars/StarReduction.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stars/StarsInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <type_traits>

namespace an
{

namespace
{

using stars_detail::failure;
using stars_detail::parameterFailure;
using stars_detail::rangeFailure;

constexpr const char* kPlanOperation = "an::planStarReduction";
constexpr const char* kReduceOperation = "an::reduceStars";
constexpr const char* kExtractOperation = "an::extractReducedStars";
constexpr const char* kMaskOperation = "an::buildSmallStarProtectionMask";
constexpr const char* kReductionTransitionOperation = "an::starReductionTransition";
constexpr const char* kExtractionTransitionOperation = "an::reducedStarExtractionTransition";
constexpr const char* kMaskTransitionOperation = "an::smallStarProtectionMaskTransition";

/// The magnitude at or below which a denominator is treated as zero.
constexpr double kDenominatorEpsilon = 1.0e-12;

/// The protection mask's circular median radius: an eleven-pixel disc.
constexpr std::uint32_t kMaskMedianRadius = 5u;

/// The protection mask's dilation radius: the pixel and its four edge neighbours.
constexpr std::uint32_t kMaskDilationRadius = 1u;

/// The protection mask's box-smoothing radius: a seven-by-seven square.
constexpr std::uint32_t kMaskBoxRadius = 3u;

/// A context allocation returned on every path.
class ContextBlock
{
public:
    /// Holds nothing until `take` succeeds.
    ContextBlock() noexcept = default;
    ContextBlock(const ContextBlock&) = delete;
    ContextBlock& operator=(const ContextBlock&) = delete;

    /// Returns the block to the resource it came from.
    ~ContextBlock()
    {
        if (m_pointer != nullptr)
        {
            m_memory->deallocate(m_pointer, m_bytes, kDefaultBufferAlignmentBytes);
        }
    }

    /// Takes one block.
    [[nodiscard]] Status take(MemoryResource* memory, std::uint64_t bytes)
    {
        if (memory == nullptr)
        {
            return failure(kMaskOperation, ErrorCode::NotInitialized, "the execution has no memory resource");
        }
        Result<void*> allocation = memory->allocate(bytes, kDefaultBufferAlignmentBytes);
        if (!allocation.hasValue())
        {
            return allocation.status();
        }
        m_memory = memory;
        m_pointer = *allocation;
        m_bytes = bytes;
        return Status::success();
    }

    /// First byte of the allocation.
    [[nodiscard]] std::byte* bytes() const noexcept
    {
        return static_cast<std::byte*>(m_pointer);
    }

private:
    /// Where the block came from.
    MemoryResource* m_memory = nullptr;

    /// The block, or null.
    void* m_pointer = nullptr;

    /// Its size.
    std::uint64_t m_bytes = 0u;
};

/// Whether the equations are defined for a channel schema: monochrome, RGB, RGBA or named channels.
[[nodiscard]] bool supportedSchema(ChannelSchema schema) noexcept
{
    return schema == ChannelSchema::Mono || schema == ChannelSchema::Rgb || schema == ChannelSchema::Rgba ||
           schema == ChannelSchema::Named;
}

/// Whether a numeric domain is a unit fraction the equations are defined over.
[[nodiscard]] bool supportedDomain(NumericDomain domain) noexcept
{
    return domain == NumericDomain::NormalizedLinear || domain == NumericDomain::EncodedNonlinear;
}

/// Checks a request against the input it will reduce: its enumerations and ranges, and for a
/// `Lightness` reduction of an RGB input, that the lightness encoding describes those samples.
[[nodiscard]] Status checkRequest(const StarReductionRequest& request, const ImageDescriptor& descriptor)
{
    if (!isStarReductionMethod(request.method))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the reduction method is not one this build implements", "request.method");
    }
    if (!isStarReductionStrength(request.strength))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the reduction strength is not one this build implements", "request.strength");
    }
    if (request.iterations < 1u || request.iterations > 3u)
    {
        return rangeFailure(kPlanOperation, "a star reduction performs between one and three iterations",
                            "request.iterations", request.iterations, 3u);
    }
    if (!std::isfinite(request.stretchFactor) || request.stretchFactor <= 0.0 || request.stretchFactor >= 1.0)
    {
        return parameterFailure(kPlanOperation, ErrorCode::OutOfRange,
                                "the stretch factor is finite and strictly between zero and one",
                                "request.stretchFactor");
    }
    if (request.method != StarReductionMethod::Lightness || descriptor.channelSchema() == ChannelSchema::Mono)
    {
        return Status::success();
    }
    if (descriptor.channelSchema() != ChannelSchema::Rgb)
    {
        return failure(kPlanOperation, ErrorCode::Unsupported,
                       "a lightness reduction is defined for monochrome or RGB samples");
    }
    const ColorEncodingError encodingError = checkColorEncoding(request.lightnessEncoding);
    if (encodingError != ColorEncodingError::None)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the lightness encoding is not a complete colour description",
                                "request.lightnessEncoding");
    }
    const std::optional<NumericDomain> encodingDomain = numericDomainOfEncoding(request.lightnessEncoding);
    const std::optional<ColorSpace> encodingSpace = colorSpaceOfEncoding(request.lightnessEncoding);
    if (request.lightnessEncoding.schema != ChannelSchema::Rgb || !encodingDomain.has_value() ||
        !encodingSpace.has_value() || *encodingDomain != descriptor.numericDomain() ||
        *encodingSpace != descriptor.colorSpace())
    {
        return failure(kPlanOperation, ErrorCode::IncompatibleArguments,
                       "the lightness encoding does not describe the target's RGB samples");
    }
    return Status::success();
}

/// The reduced or extracted image's description: the input's, restated as `EncodedNonlinear`.
[[nodiscard]] std::optional<ImageDescriptor> outputDescriptorOf(const ImageDescriptor& input) noexcept
{
    ImageDescriptorRequest request = input.toRequest();
    request.numericDomain = NumericDomain::EncodedNonlinear;
    return ImageDescriptor::create(request);
}

/// The protection mask's description: one monochrome channel of the input's extent and
/// representation, in an encoded unit domain with no colour space.
///
/// The extent is rebuilt under unbounded limits: it has the width and height of a descriptor that
/// already passed the image model's checks, and planning has no engine context whose limits it
/// could apply instead.
[[nodiscard]] std::optional<ImageDescriptor> maskDescriptorOf(const ImageDescriptor& input) noexcept
{
    EngineLimits limits{.maxImageWidth = (std::numeric_limits<std::uint32_t>::max)(),
                        .maxImageHeight = (std::numeric_limits<std::uint32_t>::max)(),
                        .maxChannelCount = (std::numeric_limits<std::uint32_t>::max)(),
                        .maxImageBytes = (std::numeric_limits<std::uint64_t>::max)(),
                        .memoryBudgetBytes = (std::numeric_limits<std::uint64_t>::max)()};
    const std::optional<ImageExtent> extent =
        ImageExtent::create(input.extent().width(), input.extent().height(), 1u, limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return ImageDescriptor::create(ImageDescriptorRequest{.extent = *extent,
                                                          .sampleType = input.sampleType(),
                                                          .channelSchema = ChannelSchema::Mono,
                                                          .colorSpace = ColorSpace::Unspecified,
                                                          .numericDomain = NumericDomain::EncodedNonlinear,
                                                          .cfa = {},
                                                          .coordinateOrigin = input.coordinateOrigin(),
                                                          .pixelAspectRatio = input.pixelAspectRatio()});
}

/// Refuses a null or misaligned image pointer with `ErrorCode::InvalidArgument`.
[[nodiscard]] Status checkPointer(const char* operation, const void* pointer, SampleType sampleType,
                                  const char* parameter)
{
    if (pointer == nullptr)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument, "image storage starts at a non-null address",
                                parameter);
    }
    const std::uint32_t alignment = sampleTypeSizeBytes(sampleType);
    if (reinterpret_cast<std::uintptr_t>(pointer) % alignment != 0u)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "image storage is aligned for its stated representation", parameter);
    }
    return Status::success();
}

/// The validation every execution call shares: the execution, the plan, three layouts against the
/// descriptors they must describe, three pointers, disjointness of the destination from both
/// inputs, the all-valid validity rule, and a cancellation check.
[[nodiscard]] Status checkCall(const char* operation, const ExecutionContext& execution, const void* first,
                               const ImageLayout& firstLayout, const void* second, const ImageLayout& secondLayout,
                               void* destination, const ImageLayout& destinationLayout, const StarReductionPlan& plan,
                               const ImageDescriptor& destinationDescriptor, const ValidityDescriptor& firstValidity,
                               const ValidityDescriptor& secondValidity)
{
    if (!execution.valid())
    {
        return failure(operation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!plan.valid())
    {
        return failure(operation, ErrorCode::InvalidArgument, "the star-reduction plan holds nothing");
    }
    if (!layoutDescribes(plan.inputDescriptor(), firstLayout) ||
        !layoutDescribes(plan.inputDescriptor(), secondLayout) ||
        !layoutDescribes(destinationDescriptor, destinationLayout))
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "an image layout does not describe the image this plan expects");
    }
    Status ready = checkPointer(operation, first, plan.inputDescriptor().sampleType(), "first");
    if (ready.failed())
    {
        return ready;
    }
    ready = checkPointer(operation, second, plan.inputDescriptor().sampleType(), "second");
    if (ready.failed())
    {
        return ready;
    }
    ready = checkPointer(operation, destination, destinationDescriptor.sampleType(), "destination");
    if (ready.failed())
    {
        return ready;
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(first), firstLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destination), destinationLayout.totalBytes()) ||
        regionsOverlap(static_cast<const std::uint8_t*>(second), secondLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return failure(operation, ErrorCode::Aliasing,
                       "the destination must be disjoint from both star-reduction inputs");
    }
    const std::array<ValidityDescriptor, 2> claims{firstValidity, secondValidity};
    Result<ValidityDescriptor> validity = operationValidity(kStarReductionEffect, claims);
    if (!validity.hasValue())
    {
        return validity.status();
    }
    return execution.checkCancellation();
}

/// One channel sample of a pixel, in any planar, interleaved or semi-planar layout: channel `c` is
/// sample `c % samplesPerPixel` of plane `c / samplesPerPixel`.
template <typename SampleT>
[[nodiscard]] const SampleT& sampleAt(const void* base, const ImageLayout& layout, std::uint32_t x, std::uint32_t y,
                                      std::uint32_t channel) noexcept
{
    const std::uint32_t samplesPerPixel = layout.samplesPerPixel();
    const std::uint32_t plane = channel / samplesPerPixel;
    const std::uint32_t withinPlane = channel % samplesPerPixel;
    const auto* bytes = static_cast<const std::byte*>(base) +
                        static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                        static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                        (static_cast<std::uint64_t>(x) * samplesPerPixel + withinPlane) * sizeof(SampleT);
    return *reinterpret_cast<const SampleT*>(bytes);
}

/// The writable form of `sampleAt`.
template <typename SampleT>
[[nodiscard]] SampleT& sampleAt(void* base, const ImageLayout& layout, std::uint32_t x, std::uint32_t y,
                                std::uint32_t channel) noexcept
{
    return const_cast<SampleT&>(sampleAt<SampleT>(static_cast<const void*>(base), layout, x, y, channel));
}

/// Reads one stored sample as a double under the default conversion policy.
///
/// @return whether the value is finite and in [0, 1], which every equation here requires.
template <typename SampleT> [[nodiscard]] bool normalizedSample(SampleT stored, double& value) noexcept
{
    const ConvertedSample<double> converted = convertSample<SampleT, double>(stored, ConversionPolicy{});
    value = converted.value;
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

/// `numerator / denominator`, or the compatibility value zero for a denominator at zero.
[[nodiscard]] double safeRatio(double numerator, double denominator) noexcept
{
    return std::abs(denominator) <= kDenominatorEpsilon ? 0.0 : numerator / denominator;
}

/// The midtones transfer function `(m - 1) x / ((2 m - 1) x - m)`, pinned to zero at and below
/// zero and to one at and above one; `m` is the value mapped to one half.
[[nodiscard]] double midtonesTransform(double midtones, double value) noexcept
{
    if (value <= 0.0)
    {
        return 0.0;
    }
    if (value >= 1.0)
    {
        return 1.0;
    }
    return ((midtones - 1.0) * value) / (((2.0 * midtones - 1.0) * value) - midtones);
}

/// Clamps a result to [0, 1], counting it in `clamped` when that changed it.
[[nodiscard]] double clampOutput(double value, std::uint64_t& clamped) noexcept
{
    const double answer = (std::clamp)(value, 0.0, 1.0);
    clamped += answer == value ? 0u : 1u;
    return answer;
}

/// The strength post-process of the two iterative methods. `Moderate` raises the value to
/// `t v + v (1 - v)` when that is larger; `Soft` then blends it as `((1 - t) t + (1 + t) v) / 2`.
[[nodiscard]] double applyStrength(double target, double value, StarReductionStrength strength) noexcept
{
    if (strength != StarReductionStrength::Strong)
    {
        value = (std::max)(value, target * value + value * (1.0 - value));
    }
    if (strength == StarReductionStrength::Soft)
    {
        value = 0.5 * ((1.0 - target) * target + (1.0 + target) * value);
    }
    return value;
}

/// One sample's reduction, the scalar authority both reduction paths are held to.
///
/// @param target the target sample, in [0, 1].
/// @param starless the starless sample, in [0, 1].
/// @param lightness the `Lightness` weight: the pixel's CIE L* over one hundred for an RGB target,
///        the target sample itself otherwise. Read by that method alone.
/// @param request the method, strength, iteration count and stretch.
/// @return the unclamped result.
[[nodiscard]] double reduceValue(double target, double starless, double lightness,
                                 const StarReductionRequest& request) noexcept
{
    switch (request.method)
    {
    case StarReductionMethod::Transfer: {
        const double midtones = 1.0 - request.stretchFactor;
        return 1.0 - safeRatio(1.0 - midtonesTransform(midtones, target), 1.0 - midtonesTransform(midtones, starless)) *
                         (1.0 - starless);
    }
    case StarReductionMethod::Halo: {
        const double midtones = 1.0 - request.stretchFactor;
        const double linear = 1.0 - safeRatio(1.0 - target, 1.0 - starless);
        const double stretched =
            1.0 - safeRatio(1.0 - midtonesTransform(midtones, target), 1.0 - midtonesTransform(midtones, starless));
        return target - 0.5 * (1.0 + target) * (linear - stretched) * (1.0 - 0.5 * starless);
    }
    case StarReductionMethod::Star: {
        double value = target * target + (1.0 - target) * starless;
        for (std::uint32_t iteration = 1u; iteration < request.iterations; ++iteration)
        {
            value = value * value + (1.0 - value) * starless;
        }
        return applyStrength(target, value, request.strength);
    }
    case StarReductionMethod::Lightness: {
        double value = lightness * target + (1.0 - lightness) * starless;
        for (std::uint32_t iteration = 1u; iteration < request.iterations; ++iteration)
        {
            value = lightness * value + (1.0 - lightness) * starless;
        }
        return applyStrength(target, value, request.strength);
    }
    }
    return target;
}

/// Samples a contiguous run is reduced in at a time.
///
/// Small enough that three blocks of `double` stay in the first-level cache beside the samples they
/// were read from, and large enough that deciding the method, the strength and the iteration count
/// once per block costs nothing a sample would notice.
constexpr std::size_t kReduceBlockSamples = 256u;

/// `applyStrength` over a block, one step across every sample before the next.
///
/// Each sample meets the same operations in the same order as `applyStrength` gives it, so each
/// result is that function's to the bit; what moves out of the sample is only the decision.
void applyStrengthToBlock(const double* target, double* value, std::size_t count,
                          StarReductionStrength strength) noexcept
{
    if (strength != StarReductionStrength::Strong)
    {
        for (std::size_t index = 0u; index < count; ++index)
        {
            value[index] = (std::max)(value[index], target[index] * value[index] + value[index] * (1.0 - value[index]));
        }
    }
    if (strength == StarReductionStrength::Soft)
    {
        for (std::size_t index = 0u; index < count; ++index)
        {
            value[index] = 0.5 * ((1.0 - target[index]) * target[index] + (1.0 + target[index]) * value[index]);
        }
    }
}

/// `reduceValue` over a block whose lightness is its own target, as a contiguous run's is.
///
/// Every loop body is the expression `reduceValue` evaluates for one sample, and an iteration is
/// taken across the whole block before the next begins, so every sample still meets its iterations
/// in order and each result is `reduceValue`'s to the bit. The difference is that no step asks per
/// sample which method, strength or iteration count applies, and each step is then a loop with no
/// branch in it that the compiler vectorizes; `reduceValue` stays the authority the strided path and
/// `StarReductionExecutionTest` hold this to.
void reduceBlock(const double* target, const double* starless, double* value, std::size_t count,
                 const StarReductionRequest& request) noexcept
{
    switch (request.method)
    {
    case StarReductionMethod::Transfer:
    case StarReductionMethod::Halo:
        for (std::size_t index = 0u; index < count; ++index)
        {
            value[index] = reduceValue(target[index], starless[index], target[index], request);
        }
        return;
    case StarReductionMethod::Star:
        for (std::size_t index = 0u; index < count; ++index)
        {
            value[index] = target[index] * target[index] + (1.0 - target[index]) * starless[index];
        }
        for (std::uint32_t iteration = 1u; iteration < request.iterations; ++iteration)
        {
            for (std::size_t index = 0u; index < count; ++index)
            {
                value[index] = value[index] * value[index] + (1.0 - value[index]) * starless[index];
            }
        }
        applyStrengthToBlock(target, value, count, request.strength);
        return;
    case StarReductionMethod::Lightness:
        for (std::size_t index = 0u; index < count; ++index)
        {
            value[index] = target[index] * target[index] + (1.0 - target[index]) * starless[index];
        }
        for (std::uint32_t iteration = 1u; iteration < request.iterations; ++iteration)
        {
            for (std::size_t index = 0u; index < count; ++index)
            {
                value[index] = target[index] * value[index] + (1.0 - target[index]) * starless[index];
            }
        }
        applyStrengthToBlock(target, value, count, request.strength);
        return;
    }
    for (std::size_t index = 0u; index < count; ++index)
    {
        value[index] = target[index];
    }
}

/// The conversion from the request's RGB encoding to CIE L*a*b* in natural units, at the
/// encoding's own white point, from which an RGB `Lightness` weight is read.
[[nodiscard]] Result<ColorConversionPlan> makeLightnessPlan(const StarReductionPlan& plan)
{
    ColorEncoding lab{.schema = ChannelSchema::Lab,
                      .primaries = RgbPrimaries::Srgb,
                      .transfer = ColorTransferFunction::Linear,
                      .whitePoint = plan.request().lightnessEncoding.whitePoint,
                      .perceptual = PerceptualEncoding::NaturalUnits};
    return planColorConversion(plan.request().lightnessEncoding, lab);
}

/// Reduces every sample of one representation, by the block path or the strided path the file's
/// opening comment describes. `lightnessPlan` is the RGB-to-L*a*b* conversion of an RGB `Lightness`
/// reduction, and null otherwise; a non-null one forces the strided path.
template <typename SampleT>
[[nodiscard]] Result<StarReductionResult>
reduceTyped(const ExecutionContext& execution, const void* target, const ImageLayout& targetLayout,
            const void* starless, const ImageLayout& starlessLayout, void* destination,
            const ImageLayout& destinationLayout, const StarReductionPlan& plan,
            const ColorConversionPlan* lightnessPlan)
{
    const std::uint32_t width = plan.inputDescriptor().extent().width();
    const std::uint32_t height = plan.inputDescriptor().extent().height();
    const std::uint32_t channels = plan.inputDescriptor().extent().channelCount();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 3u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 12u};
    const bool matchingContiguousArrangement = lightnessPlan == nullptr && targetLayout.isContiguous() &&
                                               starlessLayout.isContiguous() && destinationLayout.isContiguous() &&
                                               targetLayout.samplesPerPixel() == starlessLayout.samplesPerPixel() &&
                                               targetLayout.samplesPerPixel() == destinationLayout.samplesPerPixel() &&
                                               targetLayout.planeCount() == starlessLayout.planeCount() &&
                                               targetLayout.planeCount() == destinationLayout.planeCount();
    if (matchingContiguousArrangement)
    {
        const std::uint64_t samples = static_cast<std::uint64_t>(width) * height * channels;
        const auto* targetSamples = static_cast<const SampleT*>(target);
        const auto* starlessSamples = static_cast<const SampleT*>(starless);
        auto* destinationSamples = static_cast<SampleT*>(destination);
        const TaskCost flatCost{.bytesPerItem = sizeof(SampleT) * 3u, .operationsPerItem = 12u};
        std::atomic<std::uint64_t> clamped{0u};
        // A block is read and checked whole before any of it is written, so a refusal leaves the
        // destination written only up to an earlier block, as the contract allows.
        const Status status = parallelFor(execution, samples, flatCost, [&](TaskRange range) {
            std::array<double, kReduceBlockSamples> targetBlock;
            std::array<double, kReduceBlockSamples> starlessBlock;
            std::array<double, kReduceBlockSamples> valueBlock;
            std::uint64_t localClamped = 0u;
            for (std::uint64_t first = range.begin; first < range.end; first += kReduceBlockSamples)
            {
                const auto count =
                    static_cast<std::size_t>((std::min)(range.end - first, std::uint64_t{kReduceBlockSamples}));
                bool valid = true;
                for (std::size_t index = 0u; index < count; ++index)
                {
                    valid &= normalizedSample(targetSamples[first + index], targetBlock[index]);
                    valid &= normalizedSample(starlessSamples[first + index], starlessBlock[index]);
                }
                if (!valid)
                {
                    return failure(kReduceOperation, ErrorCode::InvalidData,
                                   "a star-reduction input sample is not a finite unit-domain value");
                }
                reduceBlock(targetBlock.data(), starlessBlock.data(), valueBlock.data(), count, plan.request());
                for (std::size_t index = 0u; index < count; ++index)
                {
                    destinationSamples[first + index] =
                        convertSample<double, SampleT>(clampOutput(valueBlock[index], localClamped), ConversionPolicy{})
                            .value;
                }
            }
            clamped.fetch_add(localClamped, std::memory_order_relaxed);
            return Status::success();
        });
        if (status.failed())
        {
            return status;
        }
        const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
        return StarReductionResult{.pixelsProcessed = pixels,
                                   .samplesWritten = samples,
                                   .clampedOutputs = clamped.load(std::memory_order_relaxed),
                                   .workingBytes = 0u,
                                   .plannedWorkers = plannedWorkerCount(execution, samples, flatCost),
                                   .imagePasses = 1u};
    }
    std::atomic<std::uint64_t> clamped{0u};
    const Status status = parallelFor(execution, height, cost, [&](TaskRange rows) {
        std::uint64_t localClamped = 0u;
        for (std::uint64_t rowIndex = rows.begin; rowIndex < rows.end; ++rowIndex)
        {
            const auto y = static_cast<std::uint32_t>(rowIndex);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                double lightness = 0.0;
                if (lightnessPlan != nullptr)
                {
                    std::array<double, 3> rgb{};
                    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        if (!normalizedSample(sampleAt<SampleT>(target, targetLayout, x, y, channel), rgb[channel]))
                        {
                            return failure(kReduceOperation, ErrorCode::InvalidData,
                                           "a target sample is not a finite unit-domain value");
                        }
                    }
                    std::array<double, 3> lab{};
                    const Status converted = convertColorPixel(*lightnessPlan, rgb, lab);
                    if (converted.failed())
                    {
                        return converted;
                    }
                    lightness = (std::clamp)(lab[0] / 100.0, 0.0, 1.0);
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    double targetValue = 0.0;
                    double starlessValue = 0.0;
                    if (!normalizedSample(sampleAt<SampleT>(target, targetLayout, x, y, channel), targetValue) ||
                        !normalizedSample(sampleAt<SampleT>(starless, starlessLayout, x, y, channel), starlessValue))
                    {
                        return failure(kReduceOperation, ErrorCode::InvalidData,
                                       "a star-reduction input sample is not a finite unit-domain value");
                    }
                    const double weight = lightnessPlan != nullptr ? lightness : targetValue;
                    const double value =
                        clampOutput(reduceValue(targetValue, starlessValue, weight, plan.request()), localClamped);
                    sampleAt<SampleT>(destination, destinationLayout, x, y, channel) =
                        convertSample<double, SampleT>(value, ConversionPolicy{}).value;
                }
            }
        }
        clamped.fetch_add(localClamped, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    return StarReductionResult{.pixelsProcessed = pixels,
                               .samplesWritten = pixels * channels,
                               .clampedOutputs = clamped.load(std::memory_order_relaxed),
                               .workingBytes = 0u,
                               .plannedWorkers = plannedWorkerCount(execution, height, cost),
                               .imagePasses = 1u};
}

/// Extracts the residual of every sample of one representation: `(r - s) / (1 - s)`, or under
/// `reverseStretch` the same ratio taken between midtones transforms at the stretch factor and at
/// its complement.
template <typename SampleT>
[[nodiscard]] Result<StarReductionResult>
extractTyped(const ExecutionContext& execution, const void* reduced, const ImageLayout& reducedLayout,
             const void* starless, const ImageLayout& starlessLayout, void* destination,
             const ImageLayout& destinationLayout, const StarReductionPlan& plan,
             const ReducedStarExtractionRequest& request)
{
    const std::uint32_t width = plan.inputDescriptor().extent().width();
    const std::uint32_t height = plan.inputDescriptor().extent().height();
    const std::uint32_t channels = plan.inputDescriptor().extent().channelCount();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 3u,
                        .operationsPerItem =
                            static_cast<std::uint64_t>(width) * channels * (request.reverseStretch ? 12u : 4u)};
    std::atomic<std::uint64_t> clamped{0u};
    const Status status = parallelFor(execution, height, cost, [&](TaskRange rows) {
        std::uint64_t localClamped = 0u;
        for (std::uint64_t rowIndex = rows.begin; rowIndex < rows.end; ++rowIndex)
        {
            const auto y = static_cast<std::uint32_t>(rowIndex);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    double reducedValue = 0.0;
                    double starlessValue = 0.0;
                    if (!normalizedSample(sampleAt<SampleT>(reduced, reducedLayout, x, y, channel), reducedValue) ||
                        !normalizedSample(sampleAt<SampleT>(starless, starlessLayout, x, y, channel), starlessValue))
                    {
                        return failure(kExtractOperation, ErrorCode::InvalidData,
                                       "a residual-extraction input is not a finite unit-domain value");
                    }
                    double value = 0.0;
                    if (!request.reverseStretch)
                    {
                        value = safeRatio(reducedValue - starlessValue, 1.0 - starlessValue);
                    }
                    else
                    {
                        reducedValue = midtonesTransform(request.stretchFactor, reducedValue);
                        starlessValue = midtonesTransform(request.stretchFactor, starlessValue);
                        value = safeRatio(reducedValue - starlessValue, 1.0 - starlessValue);
                        value = midtonesTransform(1.0 - request.stretchFactor, (std::clamp)(value, 0.0, 1.0));
                    }
                    value = clampOutput(value, localClamped);
                    sampleAt<SampleT>(destination, destinationLayout, x, y, channel) =
                        convertSample<double, SampleT>(value, ConversionPolicy{}).value;
                }
            }
        }
        clamped.fetch_add(localClamped, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    return StarReductionResult{.pixelsProcessed = pixels,
                               .samplesWritten = pixels * channels,
                               .clampedOutputs = clamped.load(std::memory_order_relaxed),
                               .workingBytes = 0u,
                               .plannedWorkers = plannedWorkerCount(execution, height, cost),
                               .imagePasses = 1u};
}

/// Runs `callable` instantiated for the C++ type of one of the eight resident representations.
template <typename Callable>
[[nodiscard]] Result<StarReductionResult> dispatchSamples(SampleType type, Callable&& callable)
{
    switch (type)
    {
    case SampleType::UInt8:
        return callable.template operator()<std::uint8_t>();
    case SampleType::Int8:
        return callable.template operator()<std::int8_t>();
    case SampleType::UInt16:
        return callable.template operator()<std::uint16_t>();
    case SampleType::Int16:
        return callable.template operator()<std::int16_t>();
    case SampleType::UInt32:
        return callable.template operator()<std::uint32_t>();
    case SampleType::Int32:
        return callable.template operator()<std::int32_t>();
    case SampleType::Float32:
        return callable.template operator()<float>();
    case SampleType::Float64:
        return callable.template operator()<double>();
    }
    return failure(kReduceOperation, ErrorCode::InvalidArgument,
                   "the sample representation is not one this build knows");
}

[[nodiscard]] double minimumMerge(double left, double right) noexcept
{
    return (std::min)(left, right);
}

[[nodiscard]] double maximumMerge(double left, double right) noexcept
{
    return (std::max)(left, right);
}

[[nodiscard]] Result<double> minimumOf(const ExecutionContext& execution, const float* values, std::uint64_t count)
{
    return parallelReduce(
        execution, count, TaskCost{.bytesPerItem = sizeof(float), .operationsPerItem = 1u},
        std::numeric_limits<double>::infinity(),
        [&](TaskRange range, double& partial) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                partial = (std::min)(partial, static_cast<double>(values[index]));
            }
            return Status::success();
        },
        &minimumMerge);
}

[[nodiscard]] Result<double> maximumOf(const ExecutionContext& execution, const float* values, std::uint64_t count)
{
    return parallelReduce(
        execution, count, TaskCost{.bytesPerItem = sizeof(float), .operationsPerItem = 1u},
        -std::numeric_limits<double>::infinity(),
        [&](TaskRange range, double& partial) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                partial = (std::max)(partial, static_cast<double>(values[index]));
            }
            return Status::success();
        },
        &maximumMerge);
}

/// Maps a plane's own minimum and maximum linearly onto `[low, high]` and clamps the result to
/// [0, 1]; a plane with no range becomes `low` throughout.
[[nodiscard]] Status rescale(const ExecutionContext& execution, float* values, std::uint64_t count, double low,
                             double high)
{
    Result<double> minimum = minimumOf(execution, values, count);
    if (!minimum.hasValue())
    {
        return minimum.status();
    }
    Result<double> maximum = maximumOf(execution, values, count);
    if (!maximum.hasValue())
    {
        return maximum.status();
    }
    const double range = *maximum - *minimum;
    return parallelFor(execution, count, TaskCost{.bytesPerItem = sizeof(float) * 2u, .operationsPerItem = 5u},
                       [&](TaskRange task) {
                           for (std::uint64_t index = task.begin; index < task.end; ++index)
                           {
                               const double scaled =
                                   std::abs(range) <= kDenominatorEpsilon
                                       ? low
                                       : low + (static_cast<double>(values[index]) - *minimum) / range * (high - low);
                               values[index] = static_cast<float>((std::clamp)(scaled, 0.0, 1.0));
                           }
                           return Status::success();
                       });
}

/// A neighbourhood coordinate replicated at the edge: clamped to `[0, limit - 1]`.
[[nodiscard]] std::uint32_t clampedCoordinate(std::int64_t value, std::uint32_t limit) noexcept
{
    if (value <= 0)
    {
        return 0u;
    }
    return static_cast<std::uint32_t>((std::min)(value, static_cast<std::int64_t>(limit) - 1));
}

/// Builds the protection mask for one representation.
///
/// In order: each pixel's channels are averaged into a grey target and a grey starless plane; each
/// is rescaled to `[-0.001, 1.001]` by its extrema and clamped to [0, 1]; the unscreen ratio
/// `(t - s) / (t (1 - s))` replaces the target plane; its eleven-pixel circular median is rescaled
/// to `[0, 0.95]`; the absolute difference between the two is stretched by the midtones transform
/// at 0.3 and rescaled to `[0, 0.75]`; that is averaged with its cross dilation, smoothed by a
/// seven-by-seven box, and written as one minus the result.
template <typename SampleT>
[[nodiscard]] Result<StarReductionResult> maskTyped(const ExecutionContext& execution, const void* target,
                                                    const ImageLayout& targetLayout, const void* starless,
                                                    const ImageLayout& starlessLayout, void* destination,
                                                    const ImageLayout& destinationLayout, const StarReductionPlan& plan)
{
    const std::uint32_t width = plan.inputDescriptor().extent().width();
    const std::uint32_t height = plan.inputDescriptor().extent().height();
    const std::uint32_t channels = plan.inputDescriptor().extent().channelCount();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    const CheckedSize total = CheckedSize{pixels} * CheckedSize{3u * sizeof(float)};
    const std::optional<std::size_t> bytes = total.toAllocationSize();
    if (!bytes.has_value())
    {
        return failure(kMaskOperation, ErrorCode::LimitExceeded,
                       "the protection mask's working planes are not representable");
    }
    ContextBlock block;
    const Status taken = block.take(execution.memoryResource(), *bytes);
    if (taken.failed())
    {
        return taken;
    }
    auto* targetGrey = reinterpret_cast<float*>(block.bytes());
    auto* starlessGrey = targetGrey + pixels;
    auto* temporary = starlessGrey + pixels;
    const TaskCost rowCost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                           .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 4u};
    Status status = parallelFor(execution, height, rowCost, [&](TaskRange rows) {
        for (std::uint64_t rowIndex = rows.begin; rowIndex < rows.end; ++rowIndex)
        {
            const auto y = static_cast<std::uint32_t>(rowIndex);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                double targetSum = 0.0;
                double starlessSum = 0.0;
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    double targetValue = 0.0;
                    double starlessValue = 0.0;
                    if (!normalizedSample(sampleAt<SampleT>(target, targetLayout, x, y, channel), targetValue) ||
                        !normalizedSample(sampleAt<SampleT>(starless, starlessLayout, x, y, channel), starlessValue))
                    {
                        return failure(kMaskOperation, ErrorCode::InvalidData,
                                       "a protection-mask input is not a finite unit-domain value");
                    }
                    targetSum += targetValue;
                    starlessSum += starlessValue;
                }
                const std::uint64_t index = static_cast<std::uint64_t>(y) * width + x;
                targetGrey[index] = static_cast<float>(targetSum / channels);
                starlessGrey[index] = static_cast<float>(starlessSum / channels);
            }
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    status = rescale(execution, targetGrey, pixels, -0.001, 1.001);
    if (status.failed())
    {
        return status;
    }
    status = rescale(execution, starlessGrey, pixels, -0.001, 1.001);
    if (status.failed())
    {
        return status;
    }
    status =
        parallelFor(execution, pixels, TaskCost{.bytesPerItem = 12u, .operationsPerItem = 6u}, [&](TaskRange task) {
            for (std::uint64_t index = task.begin; index < task.end; ++index)
            {
                targetGrey[index] = static_cast<float>(
                    (std::clamp)(safeRatio(static_cast<double>(targetGrey[index] - starlessGrey[index]),
                                           static_cast<double>(targetGrey[index]) *
                                               (1.0 - static_cast<double>(starlessGrey[index]))),
                                 0.0, 1.0));
            }
            return Status::success();
        });
    if (status.failed())
    {
        return status;
    }

    const TaskCost neighbourhoodCost{.bytesPerItem = static_cast<std::uint64_t>(width) * 82u * sizeof(float),
                                     .operationsPerItem = static_cast<std::uint64_t>(width) * 160u};
    status = parallelFor(execution, height, neighbourhoodCost, [&](TaskRange rows) {
        std::array<float, 121> neighbourhood{};
        for (std::uint64_t rowIndex = rows.begin; rowIndex < rows.end; ++rowIndex)
        {
            const auto y = static_cast<std::uint32_t>(rowIndex);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                std::size_t count = 0u;
                for (std::int32_t dy = -static_cast<std::int32_t>(kMaskMedianRadius);
                     dy <= static_cast<std::int32_t>(kMaskMedianRadius); ++dy)
                {
                    for (std::int32_t dx = -static_cast<std::int32_t>(kMaskMedianRadius);
                         dx <= static_cast<std::int32_t>(kMaskMedianRadius); ++dx)
                    {
                        if (dx * dx + dy * dy > static_cast<std::int32_t>(kMaskMedianRadius * kMaskMedianRadius))
                        {
                            continue;
                        }
                        const std::uint32_t xx = clampedCoordinate(static_cast<std::int64_t>(x) + dx, width);
                        const std::uint32_t yy = clampedCoordinate(static_cast<std::int64_t>(y) + dy, height);
                        neighbourhood[count++] = targetGrey[static_cast<std::uint64_t>(yy) * width + xx];
                    }
                }
                const std::size_t middle = count / 2u;
                std::nth_element(neighbourhood.begin(), neighbourhood.begin() + middle, neighbourhood.begin() + count);
                temporary[static_cast<std::uint64_t>(y) * width + x] = neighbourhood[middle];
            }
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    status = rescale(execution, temporary, pixels, 0.0, 0.95);
    if (status.failed())
    {
        return status;
    }
    status =
        parallelFor(execution, pixels, TaskCost{.bytesPerItem = 12u, .operationsPerItem = 10u}, [&](TaskRange task) {
            for (std::uint64_t index = task.begin; index < task.end; ++index)
            {
                targetGrey[index] = static_cast<float>(
                    midtonesTransform(0.3, std::abs(static_cast<double>(targetGrey[index] - temporary[index]))));
            }
            return Status::success();
        });
    if (status.failed())
    {
        return status;
    }
    status = rescale(execution, targetGrey, pixels, 0.0, 0.75);
    if (status.failed())
    {
        return status;
    }
    status = parallelFor(
        execution, height,
        TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * 6u * sizeof(float),
                 .operationsPerItem = static_cast<std::uint64_t>(width) * 12u},
        [&](TaskRange rows) {
            for (std::uint64_t rowIndex = rows.begin; rowIndex < rows.end; ++rowIndex)
            {
                const auto y = static_cast<std::uint32_t>(rowIndex);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    float maximum = 0.0f;
                    for (std::int32_t dy = -static_cast<std::int32_t>(kMaskDilationRadius);
                         dy <= static_cast<std::int32_t>(kMaskDilationRadius); ++dy)
                    {
                        for (std::int32_t dx = -static_cast<std::int32_t>(kMaskDilationRadius);
                             dx <= static_cast<std::int32_t>(kMaskDilationRadius); ++dx)
                        {
                            if (dx * dx + dy * dy >
                                static_cast<std::int32_t>(kMaskDilationRadius * kMaskDilationRadius))
                            {
                                continue;
                            }
                            const std::uint32_t xx = clampedCoordinate(static_cast<std::int64_t>(x) + dx, width);
                            const std::uint32_t yy = clampedCoordinate(static_cast<std::int64_t>(y) + dy, height);
                            maximum = (std::max)(maximum, targetGrey[static_cast<std::uint64_t>(yy) * width + xx]);
                        }
                    }
                    temporary[static_cast<std::uint64_t>(y) * width + x] = maximum;
                }
            }
            return Status::success();
        });
    if (status.failed())
    {
        return status;
    }
    status =
        parallelFor(execution, pixels, TaskCost{.bytesPerItem = 12u, .operationsPerItem = 2u}, [&](TaskRange task) {
            for (std::uint64_t index = task.begin; index < task.end; ++index)
            {
                targetGrey[index] = 0.5f * (targetGrey[index] + temporary[index]);
            }
            return Status::success();
        });
    if (status.failed())
    {
        return status;
    }
    status = parallelFor(
        execution, height,
        TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * 50u * sizeof(float),
                 .operationsPerItem = static_cast<std::uint64_t>(width) * 55u},
        [&](TaskRange rows) {
            for (std::uint64_t rowIndex = rows.begin; rowIndex < rows.end; ++rowIndex)
            {
                const auto y = static_cast<std::uint32_t>(rowIndex);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    double sum = 0.0;
                    std::uint32_t count = 0u;
                    for (std::int32_t dy = -static_cast<std::int32_t>(kMaskBoxRadius);
                         dy <= static_cast<std::int32_t>(kMaskBoxRadius); ++dy)
                    {
                        for (std::int32_t dx = -static_cast<std::int32_t>(kMaskBoxRadius);
                             dx <= static_cast<std::int32_t>(kMaskBoxRadius); ++dx)
                        {
                            const std::uint32_t xx = clampedCoordinate(static_cast<std::int64_t>(x) + dx, width);
                            const std::uint32_t yy = clampedCoordinate(static_cast<std::int64_t>(y) + dy, height);
                            sum += targetGrey[static_cast<std::uint64_t>(yy) * width + xx];
                            ++count;
                        }
                    }
                    temporary[static_cast<std::uint64_t>(y) * width + x] = static_cast<float>(sum / count);
                }
            }
            return Status::success();
        });
    if (status.failed())
    {
        return status;
    }
    status = parallelFor(execution, height,
                         TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * (sizeof(float) + sizeof(SampleT)),
                                  .operationsPerItem = static_cast<std::uint64_t>(width) * 2u},
                         [&](TaskRange rows) {
                             for (std::uint64_t rowIndex = rows.begin; rowIndex < rows.end; ++rowIndex)
                             {
                                 const auto y = static_cast<std::uint32_t>(rowIndex);
                                 for (std::uint32_t x = 0u; x < width; ++x)
                                 {
                                     const double value = 1.0 - temporary[static_cast<std::uint64_t>(y) * width + x];
                                     sampleAt<SampleT>(destination, destinationLayout, x, y, 0u) =
                                         convertSample<double, SampleT>(value, ConversionPolicy{}).value;
                                 }
                             }
                             return Status::success();
                         });
    if (status.failed())
    {
        return status;
    }
    return StarReductionResult{.pixelsProcessed = pixels,
                               .samplesWritten = pixels,
                               .clampedOutputs = 0u,
                               .workingBytes = *bytes,
                               .plannedWorkers = plannedWorkerCount(execution, height, rowCost),
                               .imagePasses = 12u};
}

/// Refuses a stretch factor outside the open unit interval, whether or not it will be used.
[[nodiscard]] Status checkExtractionRequest(const ReducedStarExtractionRequest& request)
{
    if (!std::isfinite(request.stretchFactor) || request.stretchFactor <= 0.0 || request.stretchFactor >= 1.0)
    {
        return parameterFailure(kExtractOperation, ErrorCode::OutOfRange,
                                "the reverse-stretch factor is finite and strictly between zero and one",
                                "request.stretchFactor");
    }
    return Status::success();
}

/// The processing meaning of a reduced or extracted image: the source's, with the validity the
/// declared effect accepts and no named transfer function or gamma.
[[nodiscard]] Result<ImageSemantics> derivedSemantics(const char* operation, const ImageSemantics& source)
{
    const std::array<ValidityDescriptor, 1> validity{source.validity()};
    Result<ValidityDescriptor> accepted = operationValidity(kStarReductionEffect, validity);
    if (!accepted.hasValue())
    {
        return accepted.status();
    }
    ImageSemanticsRequest request = source.toRequest();
    request.transferFunction = TransferFunction::Unspecified;
    request.gammaExponent.reset();
    request.validity = *accepted;
    Result<ImageSemantics> made = ImageSemantics::create(request);
    if (!made.hasValue())
    {
        return Status::failure(made.status().code(), StatusDetail{.message = made.status().message(),
                                                                  .operation = operation,
                                                                  .domain = ErrorDomain::Algorithms});
    }
    return made;
}

} // namespace

const char* starReductionMethodName(StarReductionMethod method) noexcept
{
    switch (method)
    {
    case StarReductionMethod::Transfer:
        return "Transfer";
    case StarReductionMethod::Halo:
        return "Halo";
    case StarReductionMethod::Star:
        return "Star";
    case StarReductionMethod::Lightness:
        return "Lightness";
    }
    return "UnknownStarReductionMethod";
}

const char* starReductionStrengthName(StarReductionStrength strength) noexcept
{
    switch (strength)
    {
    case StarReductionStrength::Strong:
        return "Strong";
    case StarReductionStrength::Moderate:
        return "Moderate";
    case StarReductionStrength::Soft:
        return "Soft";
    }
    return "UnknownStarReductionStrength";
}

Result<StarReductionPlan> planStarReduction(const ImageDescriptor& target, const ImageDescriptor& starless,
                                            const StarReductionRequest& request)
{
    if (!(target == starless))
    {
        return failure(kPlanOperation, ErrorCode::IncompatibleArguments,
                       "target and starless images must have the exact same description");
    }
    if (target.isMosaiced())
    {
        return failure(kPlanOperation, ErrorCode::Unsupported,
                       "star reduction has no meaning on an unresolved colour filter array");
    }
    if (!supportedSchema(target.channelSchema()))
    {
        return failure(kPlanOperation, ErrorCode::Unsupported,
                       "star reduction supports monochrome, RGB, RGBA and named channel stacks");
    }
    if (!supportedDomain(target.numericDomain()))
    {
        return failure(kPlanOperation, ErrorCode::Unsupported,
                       "star reduction is defined over a normalized or encoded unit fraction");
    }
    const Status requestReady = checkRequest(request, target);
    if (requestReady.failed())
    {
        return requestReady;
    }
    const std::optional<ImageDescriptor> output = outputDescriptorOf(target);
    const std::optional<ImageDescriptor> mask = maskDescriptorOf(target);
    if (!output.has_value() || !mask.has_value())
    {
        return failure(kPlanOperation, ErrorCode::Unsupported,
                       "the input's description cannot form a nonlinear reduction and mask output");
    }
    return StarReductionPlan{target, *output, *mask, request};
}

Result<StarReductionResult> reduceStars(const ExecutionContext& execution, const void* target,
                                        const ImageLayout& targetLayout, const void* starless,
                                        const ImageLayout& starlessLayout, void* destination,
                                        const ImageLayout& destinationLayout, const StarReductionPlan& plan,
                                        const ValidityDescriptor& targetValidity,
                                        const ValidityDescriptor& starlessValidity)
{
    if (!plan.valid())
    {
        return failure(kReduceOperation, ErrorCode::InvalidArgument, "the star-reduction plan holds nothing");
    }
    const Status ready =
        checkCall(kReduceOperation, execution, target, targetLayout, starless, starlessLayout, destination,
                  destinationLayout, plan, plan.outputDescriptor(), targetValidity, starlessValidity);
    if (ready.failed())
    {
        return ready;
    }
    std::optional<ColorConversionPlan> lightness;
    if (plan.request().method == StarReductionMethod::Lightness &&
        plan.inputDescriptor().channelSchema() == ChannelSchema::Rgb)
    {
        Result<ColorConversionPlan> made = makeLightnessPlan(plan);
        if (!made.hasValue())
        {
            return made.status();
        }
        lightness.emplace(std::move(*made));
    }
    return dispatchSamples(plan.inputDescriptor().sampleType(), [&]<typename SampleT>() {
        return reduceTyped<SampleT>(execution, target, targetLayout, starless, starlessLayout, destination,
                                    destinationLayout, plan, lightness.has_value() ? &*lightness : nullptr);
    });
}

Result<StarReductionResult> extractReducedStars(const ExecutionContext& execution, const void* reduced,
                                                const ImageLayout& reducedLayout, const void* starless,
                                                const ImageLayout& starlessLayout, void* destination,
                                                const ImageLayout& destinationLayout, const StarReductionPlan& plan,
                                                const ReducedStarExtractionRequest& request,
                                                const ValidityDescriptor& reducedValidity,
                                                const ValidityDescriptor& starlessValidity)
{
    if (!plan.valid())
    {
        return failure(kExtractOperation, ErrorCode::InvalidArgument, "the star-reduction plan holds nothing");
    }
    const Status requestReady = checkExtractionRequest(request);
    if (requestReady.failed())
    {
        return requestReady;
    }
    const Status ready =
        checkCall(kExtractOperation, execution, reduced, reducedLayout, starless, starlessLayout, destination,
                  destinationLayout, plan, plan.outputDescriptor(), reducedValidity, starlessValidity);
    if (ready.failed())
    {
        return ready;
    }
    return dispatchSamples(plan.inputDescriptor().sampleType(), [&]<typename SampleT>() {
        return extractTyped<SampleT>(execution, reduced, reducedLayout, starless, starlessLayout, destination,
                                     destinationLayout, plan, request);
    });
}

Result<StarReductionResult>
buildSmallStarProtectionMask(const ExecutionContext& execution, const void* target, const ImageLayout& targetLayout,
                             const void* starless, const ImageLayout& starlessLayout, void* destination,
                             const ImageLayout& destinationLayout, const StarReductionPlan& plan,
                             const ValidityDescriptor& targetValidity, const ValidityDescriptor& starlessValidity)
{
    if (!plan.valid())
    {
        return failure(kMaskOperation, ErrorCode::InvalidArgument, "the star-reduction plan holds nothing");
    }
    const Status ready =
        checkCall(kMaskOperation, execution, target, targetLayout, starless, starlessLayout, destination,
                  destinationLayout, plan, plan.maskDescriptor(), targetValidity, starlessValidity);
    if (ready.failed())
    {
        return ready;
    }
    return dispatchSamples(plan.inputDescriptor().sampleType(), [&]<typename SampleT>() {
        return maskTyped<SampleT>(execution, target, targetLayout, starless, starlessLayout, destination,
                                  destinationLayout, plan);
    });
}

Result<MetadataTransition> starReductionTransition(const StarReductionPlan& plan, const ImageSemantics& source)
{
    if (!plan.valid())
    {
        return failure(kReductionTransitionOperation, ErrorCode::InvalidArgument,
                       "the star-reduction plan holds nothing");
    }
    Result<ImageSemantics> semantics = derivedSemantics(kReductionTransitionOperation, source);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    std::ostringstream detail;
    detail << "method=" << starReductionMethodName(plan.request().method)
           << ",strength=" << starReductionStrengthName(plan.request().strength)
           << ",iterations=" << plan.request().iterations << ",stretch=" << plan.request().stretchFactor;
    return operationTransition(
        kStarReductionEffect,
        OperationOutcome{.operation = kReduceOperation, .detail = detail.str(), .semantics = std::move(*semantics)});
}

Result<MetadataTransition> reducedStarExtractionTransition(const StarReductionPlan& plan, const ImageSemantics& source,
                                                           const ReducedStarExtractionRequest& request)
{
    if (!plan.valid())
    {
        return failure(kExtractionTransitionOperation, ErrorCode::InvalidArgument,
                       "the star-reduction plan holds nothing");
    }
    const Status requestReady = checkExtractionRequest(request);
    if (requestReady.failed())
    {
        return requestReady;
    }
    Result<ImageSemantics> semantics = derivedSemantics(kExtractionTransitionOperation, source);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    std::ostringstream detail;
    detail << "reverseStretch=" << (request.reverseStretch ? "true" : "false") << ",stretch=" << request.stretchFactor;
    return operationTransition(
        kReducedStarExtractionEffect,
        OperationOutcome{.operation = kExtractOperation, .detail = detail.str(), .semantics = std::move(*semantics)});
}

Result<MetadataTransition> smallStarProtectionMaskTransition()
{
    Result<ImageSemantics> semantics = ImageSemantics::create(ImageSemanticsRequest{});
    if (!semantics.hasValue())
    {
        return failure(kMaskTransitionOperation, ErrorCode::Internal,
                       "the default mask semantics could not be constructed");
    }
    return operationTransition(kSmallStarProtectionMaskEffect,
                               OperationOutcome{.operation = kMaskOperation,
                                                .detail = "medianDiameter=11,dilationDiameter=3,boxDiameter=7",
                                                .semantics = std::move(*semantics)});
}

} // namespace an
