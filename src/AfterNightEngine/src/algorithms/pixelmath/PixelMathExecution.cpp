// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the specialized evaluator, run over a whole image.
//
// An invocation is checked once, and everything that is a property of the whole image is settled
// before the first sample is written: each whole-image statistic the program requests is computed
// per target channel through the public statistics operations, under the execution's validity rule,
// and every colour conversion is planned. The walk then splits rows across the operation's workers
// and gives each worker a share of one block taken from the engine context, whole cache lines apiece.
// A program with no branch is evaluated a run of adjacent samples at a time, one instruction for the
// whole run; a program that branches, or any program on the reference backend, one sample at a time
// through the instruction loop `evaluatePixelMathPixel` runs. The two apply the same functions to
// each sample in the same order and write the same bits. A pixel is evaluated only when the mask
// accepts it and, under a value rule, every target channel is valid; a pixel that is not leaves the
// destination untouched. Each value is narrowed once to the destination representation as a code
// value - saturated, never clamped to the unit interval.
//
// One shape is common enough to be recognised and run directly: an image plus a sum of scaled
// random draws, over single-sample-per-pixel Float32 storage with nothing excluded. It evaluates the
// same products and additions in the same order with the same draws as the interpreter; forcing
// `ComputeBackend::Reference` turns it off, which is how the two are compared.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>

#include "algorithms/pixelmath/PixelMathInterpreter.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::executePixelMath";

[[nodiscard]] Status executionFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] std::uint64_t channelSampleOffset(const ImageLayout& layout, std::uint32_t channel, std::uint32_t x,
                                                std::uint32_t y) noexcept
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t within = channel % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
           static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
           (static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(layout.samplesPerPixel()) +
            static_cast<std::uint64_t>(within)) *
               static_cast<std::uint64_t>(layout.elementSizeBytes());
}

[[nodiscard]] double readSampleAsDouble(const PixelMathImageView& image, std::uint32_t channel, std::uint32_t x,
                                        std::uint32_t y) noexcept
{
    const ImageLayout& layout = *image.layout;
    const std::uint8_t* const at =
        static_cast<const std::uint8_t*>(image.samples) + channelSampleOffset(layout, channel, x, y);
    switch (image.sampleType)
    {
    case SampleType::UInt8:
        return static_cast<double>(*reinterpret_cast<const std::uint8_t*>(at));
    case SampleType::Int8:
        return static_cast<double>(*reinterpret_cast<const std::int8_t*>(at));
    case SampleType::UInt16:
        return static_cast<double>(*reinterpret_cast<const std::uint16_t*>(at));
    case SampleType::Int16:
        return static_cast<double>(*reinterpret_cast<const std::int16_t*>(at));
    case SampleType::UInt32:
        return static_cast<double>(*reinterpret_cast<const std::uint32_t*>(at));
    case SampleType::Int32:
        return static_cast<double>(*reinterpret_cast<const std::int32_t*>(at));
    case SampleType::Float32:
        return static_cast<double>(*reinterpret_cast<const float*>(at));
    case SampleType::Float64:
        return *reinterpret_cast<const double*>(at);
    }
    return 0.0;
}

[[nodiscard]] bool maskAccepts(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

[[nodiscard]] bool layoutDescribes(const ImageLayout& layout, const PixelMathImageDeclaration& declaration) noexcept
{
    return layout.extent() == declaration.extent &&
           layout.elementSizeBytes() == sampleTypeSizeBytes(declaration.sampleType);
}

[[nodiscard]] bool exactlyAliases(const PixelMathImageView& image,
                                  const PixelMathMutableImageView& destination) noexcept
{
    return image.samples == destination.samples && image.sampleType == destination.sampleType &&
           *image.layout == *destination.layout;
}

/// Every check the published entry point makes once, before any statistic is computed or any sample
/// written; the failures are the ones `executePixelMath` lists.
[[nodiscard]] Status validateInvocation(const ExecutionContext& execution, const PixelMathProgram& program,
                                        const PixelMathExecutionRequest& request)
{
    if (!execution.valid())
    {
        return executionFailure(ErrorCode::NotInitialized,
                                "the expression executor was given an empty execution context");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    if (program.instructions().empty() || !program.targetExtent().has_value())
    {
        return executionFailure(ErrorCode::InvalidArgument, "an empty expression program was executed");
    }
    if (request.images.size() != program.images().size())
    {
        return executionFailure(ErrorCode::InvalidArgument,
                                "the number of bound images does not match the expression's image count");
    }
    if (request.destination.samples == nullptr || request.destination.layout == nullptr)
    {
        return executionFailure(ErrorCode::InvalidArgument,
                                "the expression destination has a null samples or layout pointer");
    }
    if (!isSampleType(request.destination.sampleType) ||
        request.destination.layout->elementSizeBytes() != sampleTypeSizeBytes(request.destination.sampleType))
    {
        return executionFailure(ErrorCode::InvalidArgument,
                                "the expression destination's layout does not describe its representation");
    }

    const ImageExtent target = *program.targetExtent();
    const ImageExtent destination = request.destination.layout->extent();
    if (destination.width() != target.width() || destination.height() != target.height())
    {
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "the expression destination does not have the target image's extent");
    }
    if (request.destinationChannel.has_value())
    {
        if (*request.destinationChannel >= destination.channelCount())
        {
            return executionFailure(ErrorCode::InvalidArgument,
                                    "the stated destination channel is not one the destination has");
        }
    }
    else if (destination.channelCount() != target.channelCount())
    {
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "the expression destination does not have the target image's channel count");
    }

    for (std::size_t index = 0u; index < request.images.size(); ++index)
    {
        const PixelMathImageView& image = request.images[index];
        if (image.samples == nullptr || image.layout == nullptr)
        {
            return executionFailure(ErrorCode::InvalidArgument, "a bound image has a null samples or layout pointer");
        }
        if (image.sampleType != program.images()[index].sampleType ||
            !layoutDescribes(*image.layout, program.images()[index]))
        {
            return executionFailure(
                ErrorCode::InvalidArgument,
                "a bound image's layout does not describe what the expression was resolved against");
        }
        if (regionsOverlap(static_cast<const std::uint8_t*>(image.samples), image.layout->totalBytes(),
                           static_cast<std::uint8_t*>(request.destination.samples),
                           request.destination.layout->totalBytes()))
        {
            if (!exactlyAliases(image, request.destination))
            {
                return executionFailure(
                    ErrorCode::InvalidArgument,
                    "a bound image and the destination overlap without being the same image and layout");
            }
            if (!program.supportsInPlace())
            {
                return executionFailure(ErrorCode::InvalidArgument,
                                        "this expression reads samples a destination write would change, so it cannot "
                                        "be evaluated in place");
            }
        }
    }

    if (!request.validity.descriptor.needsMask())
    {
        if (request.validity.mask != nullptr || request.validity.maskPlane != 0u)
        {
            return executionFailure(ErrorCode::InvalidArgument,
                                    "a packed mask was supplied for a validity rule that does not use one");
        }
        return Status::success();
    }
    if (request.validity.mask == nullptr)
    {
        return executionFailure(ErrorCode::InvalidArgument,
                                "the explicit-mask expression invocation was given no packed mask");
    }
    if (request.validity.mask->width() != target.width() || request.validity.mask->height() != target.height() ||
        request.validity.maskPlane >= request.validity.mask->planeCount())
    {
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "the expression validity mask does not cover the target extent and selected plane");
    }
    if (regionsOverlap(request.validity.mask->data(), request.validity.mask->spannedByteCount(),
                       static_cast<std::uint8_t*>(request.destination.samples),
                       request.destination.layout->totalBytes()))
    {
        return executionFailure(ErrorCode::InvalidArgument,
                                "the expression validity mask overlaps the writable destination image");
    }
    return Status::success();
}

/// Computes one whole-image statistic of one channel.
[[nodiscard]] Result<double> computeStatistic(const ExecutionContext& execution, const PixelMathImageView& image,
                                              std::uint32_t channel, PixelMathStatistic statistic,
                                              const StatisticsSelection& selection, std::uint64_t& passes)
{
    switch (statistic)
    {
    case PixelMathStatistic::Median:
    case PixelMathStatistic::MedianAbsoluteDeviation: {
        RobustStatisticsRequest request;
        request.medianAbsoluteDeviation = statistic == PixelMathStatistic::MedianAbsoluteDeviation;
        const Result<RobustStatistics> computed = computeImageRobustStatistics(
            execution, image.sampleType, image.samples, *image.layout, channel, selection, request);
        if (!computed.hasValue())
        {
            return computed.status();
        }
        passes += computed->planeScans;
        return statistic == PixelMathStatistic::Median ? computed->median : computed->medianAbsoluteDeviation;
    }
    default:
        break;
    }

    switch (statistic)
    {
    case PixelMathStatistic::AverageAbsoluteDeviation:
    case PixelMathStatistic::MeanOfSquares:
    case PixelMathStatistic::SumOfSquares:
    case PixelMathStatistic::BiweightMidvariance:
    case PixelMathStatistic::BackgroundNoise: {
        const Result<AdvancedStatistics> computed = computeImageAdvancedStatistics(
            execution, image.sampleType, image.samples, *image.layout, channel, selection);
        if (!computed.hasValue())
        {
            return computed.status();
        }
        passes += computed->planeScans;
        switch (statistic)
        {
        case PixelMathStatistic::AverageAbsoluteDeviation:
            return computed->averageAbsoluteDeviation;
        case PixelMathStatistic::MeanOfSquares:
            return computed->meanOfSquares;
        case PixelMathStatistic::SumOfSquares:
            return computed->sumOfSquares;
        case PixelMathStatistic::BiweightMidvariance:
            return computed->biweightMidvariance;
        case PixelMathStatistic::BackgroundNoise:
            return computed->backgroundNoise;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }

    const Result<BasicStatistics> computed =
        computeImageBasicStatistics(execution, image.sampleType, image.samples, *image.layout, channel, selection);
    if (!computed.hasValue())
    {
        return computed.status();
    }
    passes += 2u;
    switch (statistic)
    {
    case PixelMathStatistic::Minimum:
        return computed->minimum;
    case PixelMathStatistic::Maximum:
        return computed->maximum;
    case PixelMathStatistic::Mean:
        return computed->mean;
    case PixelMathStatistic::StandardDeviation:
        return computed->populationStandardDeviation;
    case PixelMathStatistic::Variance:
        return computed->populationVariance;
    default:
        break;
    }
    return executionFailure(ErrorCode::Internal, "a statistic this build does not compute reached the executor");
}

/// A common non-lowerable expression shape: one image plus deterministic scaled random draws.
struct AdditiveRandomPlan
{
    /// Which bound image the expression starts from.
    std::uint32_t image = 0u;

    /// The constant each draw is multiplied by, in draw order.
    std::vector<double> scales;
};

/// Whether a program is exactly one sample read followed by `+ random() * constant` repeated.
///
/// Matched on the instructions rather than the tree, so it recognises only what the compiler
/// actually emitted: a first `PushSample` of the pixel's own channel, then groups of four -
/// `PushRandom`, `PushConstant`, `Multiply`, `Add` - with no variable, statistic or colour read.
///
/// @param program the compiled expression.
/// @return the image and the scales, or nothing when the program has any other shape.
[[nodiscard]] std::optional<AdditiveRandomPlan> matchAdditiveRandomPlan(const PixelMathProgram& program)
{
    const std::span<const PixelMathInstruction> instructions = program.instructions();
    if (instructions.size() < 5u || (instructions.size() - 1u) % 4u != 0u ||
        instructions.front().opcode != PixelMathOpcode::PushSample || !program.variableInitialValues().empty() ||
        !program.statistics().empty() || !program.colorReads().empty())
    {
        return std::nullopt;
    }
    if (instructions.front().operand >= program.sampleReads().size())
    {
        return std::nullopt;
    }
    const PixelMathSampleRead& read = program.sampleReads()[instructions.front().operand];
    if (read.selectsChannel || read.image >= program.images().size())
    {
        return std::nullopt;
    }

    AdditiveRandomPlan plan;
    plan.image = read.image;
    plan.scales.reserve((instructions.size() - 1u) / 4u);
    for (std::size_t at = 1u; at < instructions.size(); at += 4u)
    {
        const PixelMathInstruction& draw = instructions[at];
        const PixelMathInstruction& constant = instructions[at + 1u];
        const PixelMathInstruction& multiply = instructions[at + 2u];
        const PixelMathInstruction& add = instructions[at + 3u];
        if (draw.opcode != PixelMathOpcode::PushRandom || constant.opcode != PixelMathOpcode::PushConstant ||
            constant.operand >= program.constants().size() || multiply.opcode != PixelMathOpcode::ApplyBinary ||
            multiply.binary != PixelMathBinaryOperator::Multiply || add.opcode != PixelMathOpcode::ApplyBinary ||
            add.binary != PixelMathBinaryOperator::Add)
        {
            return std::nullopt;
        }
        plan.scales.push_back(program.constants()[constant.operand]);
    }
    return plan;
}

/// Whether the matched shape can skip the interpreter for this invocation: every channel written,
/// nothing excluded, and Float32 on both sides with one sample per pixel, so each row of each
/// channel is one contiguous run.
[[nodiscard]] bool canExecuteAdditiveRandomDirectly(const PixelMathExecutionRequest& request,
                                                    const AdditiveRandomPlan& plan) noexcept
{
    if (request.destinationChannel.has_value() || !request.validity.descriptor.isAllValid() ||
        request.validity.mask != nullptr || request.destination.sampleType != SampleType::Float32 ||
        plan.image >= request.images.size())
    {
        return false;
    }
    const PixelMathImageView& source = request.images[plan.image];
    return source.sampleType == SampleType::Float32 && source.layout->samplesPerPixel() == 1u &&
           request.destination.layout->samplesPerPixel() == 1u;
}

[[nodiscard]] Result<PixelMathExecutionResult> executeAdditiveRandomDirectly(const ExecutionContext& execution,
                                                                             const PixelMathExecutionRequest& request,
                                                                             const AdditiveRandomPlan& plan)
{
    const PixelMathImageView& source = request.images[plan.image];
    const ImageLayout& sourceLayout = *source.layout;
    const ImageLayout& destinationLayout = *request.destination.layout;
    const ImageExtent target = destinationLayout.extent();
    const std::uint32_t width = target.width();
    const std::uint32_t height = target.height();
    const std::uint32_t channels = target.channelCount();
    const std::uint32_t sourceChannels = sourceLayout.extent().channelCount();
    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source.samples);
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(request.destination.samples);
    const ConversionPolicy outputPolicy{.domain = DomainMapping::CodeValue};
    std::atomic<std::uint64_t> nonFiniteInputs{0u};
    std::atomic<std::uint64_t> saturatedSamples{0u};
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(float) * 2u,
                        .operationsPerItem =
                            static_cast<std::uint64_t>(width) * channels * (plan.scales.size() * 8u + 1u)};

    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t localNonFinite = 0u;
        std::uint64_t localSaturated = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const std::uint32_t sourceChannel = sourceChannels == 1u ? 0u : channel;
                const float* const input =
                    reinterpret_cast<const float*>(sourceBytes + *sourceLayout.rowByteOffset(sourceChannel, y));
                float* const output =
                    reinterpret_cast<float*>(destinationBytes + *destinationLayout.rowByteOffset(channel, y));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    double value = static_cast<double>(input[x]);
                    for (std::size_t draw = 0u; draw < plan.scales.size(); ++draw)
                    {
                        value += pixelMathRandomDraw(request.randomSeed, x, y, channel, draw) * plan.scales[draw];
                    }
                    const ConvertedSample<float> narrowed = convertSample<double, float>(value, outputPolicy);
                    output[x] = narrowed.value;
                    localNonFinite += narrowed.sourceWasNonFinite ? 1u : 0u;
                    localSaturated += narrowed.saturated ? 1u : 0u;
                }
            }
        }
        nonFiniteInputs.fetch_add(localNonFinite, std::memory_order_relaxed);
        saturatedSamples.fetch_add(localSaturated, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }

    PixelMathExecutionResult result;
    result.run.selection.total = target.sampleCount();
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total;
    result.conversion.samplesConverted = result.run.outputsWritten;
    result.conversion.nonFiniteInputs = nonFiniteInputs.load(std::memory_order_relaxed);
    result.conversion.saturatedSamples = saturatedSamples.load(std::memory_order_relaxed);
    return result;
}

/// How many values one worker's share of a run walk may hold before its runs are shortened.
constexpr std::uint64_t kRunScratchValues = 16384u;

/// Where one worker's working memory lies inside the walk's one block.
///
/// Every worker's share is a whole number of cache lines, so no two workers ever write one line: a
/// stack slot is written at every instruction of every sample, and the heap is free to put two small
/// allocations from two threads side by side.
struct WalkScratch
{
    /// How many samples one evaluation covers; one for the per-sample walk.
    std::uint32_t lanes = 1u;

    /// Stack values: the program's depth, at least one, times `lanes`.
    std::uint64_t stackValues = 0u;

    /// Variable values.
    std::uint64_t variableValues = 0u;

    /// One sample's gathered call arguments and the room above them; a run walk's only.
    std::uint64_t argumentValues = 0u;

    /// The statistics of the channel being written, at least one.
    std::uint64_t statisticValues = 0u;

    /// A run's values; a run walk's only.
    std::uint64_t resultValues = 0u;

    /// One acceptance flag per lane, after the values; a run walk's only.
    std::uint64_t flagBytes = 0u;

    /// The whole share, rounded up to cache lines.
    std::uint64_t slotBytes = 0u;
};

/// The share one worker needs for one program, walked a run or a sample at a time.
[[nodiscard]] WalkScratch walkScratchFor(const PixelMathProgram& program, bool byRun) noexcept
{
    const std::uint64_t slots = (std::max)(program.stackDepth(), std::uint32_t{1});
    const std::uint64_t variables = program.variableInitialValues().size();
    WalkScratch shape;
    shape.lanes = byRun ? static_cast<std::uint32_t>(std::clamp<std::uint64_t>(kRunScratchValues / (slots + variables),
                                                                               1u, kPixelMathRunLength))
                        : 1u;
    shape.stackValues = slots * shape.lanes;
    shape.variableValues = byRun ? variables * shape.lanes : (std::max)(variables, std::uint64_t{1});
    shape.argumentValues = byRun ? slots : 0u;
    shape.statisticValues = (std::max)(static_cast<std::uint64_t>(program.statistics().size()), std::uint64_t{1});
    shape.resultValues = byRun ? shape.lanes : 0u;
    shape.flagBytes = byRun ? shape.lanes : 0u;
    const std::uint64_t bytes =
        (shape.stackValues + shape.variableValues + shape.argumentValues + shape.statisticValues + shape.resultValues) *
            sizeof(double) +
        shape.flagBytes;
    shape.slotBytes = (bytes + kCacheLineBytes - 1u) / kCacheLineBytes * kCacheLineBytes;
    return shape;
}

/// What one worker's rows counted.
struct WalkCounts
{
    /// Pixel-channel evaluations that were selected.
    std::uint64_t valid = 0u;

    /// Samples stored.
    std::uint64_t written = 0u;

    /// Stored samples whose value was not finite.
    std::uint64_t nonFinite = 0u;

    /// Stored samples that saturated.
    std::uint64_t saturated = 0u;
};

/// Everything a row walk reads that is fixed for the execution.
struct WalkPlan
{
    /// The compiled expression.
    const PixelMathProgram* program = nullptr;

    /// The checked invocation.
    const PixelMathExecutionRequest* request = nullptr;

    /// The planned conversions, or null when the program reads no colour component.
    const PlannedColorReads* colorPlans = nullptr;

    /// Every statistic's value per target channel, statistic-major.
    std::span<const double> statisticsByChannel{};

    /// The target's width.
    std::uint32_t width = 0u;

    /// The target's channel count.
    std::uint32_t targetChannels = 0u;

    /// The first channel written.
    std::uint32_t firstChannel = 0u;

    /// How many channels are written.
    std::uint32_t channelCount = 0u;
};

/// Whether a pixel is evaluated: its mask bit, when there is a mask, and under a value rule every
/// target channel's sample.
[[nodiscard]] bool pixelAccepted(const PixelMathExecutionRequest& request, std::uint32_t targetChannels,
                                 std::uint32_t x, std::uint32_t y) noexcept
{
    bool accepted = maskAccepts(request.validity, x, y);
    if (accepted && !request.validity.descriptor.isAllValid())
    {
        for (std::uint32_t channel = 0u; channel < targetChannels && accepted; ++channel)
        {
            accepted =
                sampleIsValidByValue(request.validity.descriptor, readSampleAsDouble(request.images[0], channel, x, y));
        }
    }
    return accepted;
}

/// Narrows a run of values to the destination, `stride` elements apart, stores them and counts them.
/// Both walks store through here, a sample-at-a-time walk a run of one.
///
/// Each value is narrowed as `convertSample` narrows a code value. A Float32 destination states the conversion's rule
/// for code values rather than calling it: a value that is not finite becomes the quiet not-a-number or keeps the sign
/// of its infinity, and a finite one is held to the largest finite Float32 either way and rounded to nearest - the same
/// constants and the same finiteness test `convertSample` uses. Called per sample, one compiler
/// builds the converted sample's record in memory a byte at a time and reads it back whole, which
/// the processor cannot forward, and that stall was most of the walk.
template <ResidentSample DestinationT>
void storeRun(const double* values, std::uint32_t count, DestinationT* written, std::uint32_t stride,
              WalkCounts& counts) noexcept
{
    std::uint64_t nonFinite = 0u;
    std::uint64_t saturated = 0u;
    if constexpr (std::is_same_v<DestinationT, float>)
    {
        for (std::uint32_t sample = 0u; sample < count; ++sample)
        {
            double real = values[sample];
            float narrowed = std::numeric_limits<float>::quiet_NaN();
            if (!sampleIsFinite(real))
            {
                ++nonFinite;
                if (real == real)
                {
                    narrowed =
                        real > 0.0 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();
                }
            }
            else
            {
                if (real < kSampleMinimumOf<float>)
                {
                    real = kSampleMinimumOf<float>;
                    ++saturated;
                }
                else if (real > kSampleMaximumOf<float>)
                {
                    real = kSampleMaximumOf<float>;
                    ++saturated;
                }
                narrowed = static_cast<float>(real);
            }
            written[static_cast<std::size_t>(sample) * stride] = narrowed;
        }
    }
    else
    {
        const ConversionPolicy outputPolicy{.domain = DomainMapping::CodeValue};
        for (std::uint32_t sample = 0u; sample < count; ++sample)
        {
            const ConvertedSample<DestinationT> narrowed =
                convertSample<double, DestinationT>(values[sample], outputPolicy);
            written[static_cast<std::size_t>(sample) * stride] = narrowed.value;
            nonFinite += narrowed.sourceWasNonFinite ? 1u : 0u;
            saturated += narrowed.saturated ? 1u : 0u;
        }
    }
    counts.written += count;
    counts.valid += count;
    counts.nonFinite += nonFinite;
    counts.saturated += saturated;
}

/// Walks a range of rows one sample at a time through the single-sample instruction loop.
template <ResidentSample DestinationT>
[[nodiscard]] Status walkBySample(const WalkPlan& plan, TaskRange range, const PixelMathEvaluationScratch& scratch,
                                  double* statistics, WalkCounts& counts)
{
    const PixelMathExecutionRequest& request = *plan.request;
    const ImageLayout& destinationLayout = *request.destination.layout;
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(request.destination.samples);
    const std::uint32_t statisticCount = static_cast<std::uint32_t>(plan.program->statistics().size());
    for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
    {
        const std::uint32_t y = static_cast<std::uint32_t>(y64);
        for (std::uint32_t x = 0u; x < plan.width; ++x)
        {
            if (!pixelAccepted(request, plan.targetChannels, x, y))
            {
                continue;
            }
            for (std::uint32_t index = 0u; index < plan.channelCount; ++index)
            {
                const std::uint32_t channel = plan.firstChannel + index;
                for (std::uint32_t statistic = 0u; statistic < statisticCount; ++statistic)
                {
                    statistics[statistic] = plan.statisticsByChannel[statistic * plan.targetChannels + channel];
                }
                PixelMathEvaluationSite site;
                site.x = x;
                site.y = y;
                site.channel = channel;
                site.randomSeed = request.randomSeed;
                // The invocation was checked once before the walk began, so what runs per sample
                // is the arithmetic and nothing else.
                const Result<double> value = runPixelMathProgram(*plan.program, request.images,
                                                                 std::span<const double>(statistics, statisticCount),
                                                                 plan.colorPlans, site, scratch);
                if (!value.hasValue())
                {
                    return value.status();
                }
                const double evaluated = *value;
                storeRun(&evaluated, 1u,
                         reinterpret_cast<DestinationT*>(destinationBytes +
                                                         channelSampleOffset(destinationLayout, channel, x, y)),
                         1u, counts);
            }
        }
    }
    return Status::success();
}

/// Walks a range of rows a run of adjacent samples at a time through the run instruction loop.
///
/// Which pixels of a run are evaluated is decided before any of them is written, as the per-sample
/// walk decides each pixel before writing it, so that in place a value rule never judges a sample an
/// earlier channel's run has already replaced. The accepted pixels are evaluated as the unbroken
/// stretches they form, so a rejected pixel is never evaluated, which is also what the per-sample
/// walk does.
template <ResidentSample DestinationT>
[[nodiscard]] Status walkByRun(const WalkPlan& plan, TaskRange range, const PixelMathRunScratch& scratch,
                               double* statistics, double* results, std::uint8_t* accepted, WalkCounts& counts)
{
    const PixelMathExecutionRequest& request = *plan.request;
    const ImageLayout& destinationLayout = *request.destination.layout;
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(request.destination.samples);
    const std::uint32_t pixelStride = destinationLayout.samplesPerPixel();
    const std::uint32_t statisticCount = static_cast<std::uint32_t>(plan.program->statistics().size());
    for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
    {
        const std::uint32_t y = static_cast<std::uint32_t>(y64);
        for (std::uint32_t first = 0u; first < plan.width; first += scratch.lanes)
        {
            const std::uint32_t count = (std::min)(scratch.lanes, plan.width - first);
            std::uint32_t acceptedCount = 0u;
            for (std::uint32_t lane = 0u; lane < count; ++lane)
            {
                const bool isAccepted = pixelAccepted(request, plan.targetChannels, first + lane, y);
                accepted[lane] = isAccepted ? 1u : 0u;
                acceptedCount += isAccepted ? 1u : 0u;
            }
            if (acceptedCount == 0u)
            {
                continue;
            }
            for (std::uint32_t index = 0u; index < plan.channelCount; ++index)
            {
                const std::uint32_t channel = plan.firstChannel + index;
                for (std::uint32_t statistic = 0u; statistic < statisticCount; ++statistic)
                {
                    statistics[statistic] = plan.statisticsByChannel[statistic * plan.targetChannels + channel];
                }
                std::uint32_t lane = 0u;
                while (lane < count)
                {
                    if (accepted[lane] == 0u)
                    {
                        ++lane;
                        continue;
                    }
                    std::uint32_t end = lane + 1u;
                    while (end < count && accepted[end] != 0u)
                    {
                        ++end;
                    }
                    const PixelMathEvaluationRun run{.x = first + lane,
                                                     .count = end - lane,
                                                     .y = y,
                                                     .channel = channel,
                                                     .randomSeed = request.randomSeed};
                    const Status evaluated = runPixelMathProgramOverRun(
                        *plan.program, request.images, std::span<const double>(statistics, statisticCount),
                        plan.colorPlans, run, scratch, std::span<double>(results, run.count));
                    if (evaluated.failed())
                    {
                        return evaluated;
                    }
                    storeRun(results, run.count,
                             reinterpret_cast<DestinationT*>(destinationBytes +
                                                             channelSampleOffset(destinationLayout, channel, run.x, y)),
                             pixelStride, counts);
                    lane = end;
                }
            }
        }
    }
    return Status::success();
}

/// Walks every accepted pixel and writes the interpreter's value for each written channel.
///
/// A program with no branch is walked a run of samples at a time; one that branches, or any program
/// on the reference backend, a sample at a time. Both write the same bits. The working memory is one
/// block from the engine context, a share per worker.
///
/// @tparam DestinationT the destination's resident type.
/// @param execution the operation policy and engine-context lease.
/// @param program the compiled expression.
/// @param request the checked invocation.
/// @param colorPlans the planned conversions, or null when the program reads no colour component.
/// @param statisticsByChannel every statistic's value per target channel, statistic-major.
/// @param statisticPasses how many plane passes the statistics cost, reported back.
/// @return the counts, or the context's refusal of the working memory, or the first failure the walk
///         or the interpreter reported.
template <ResidentSample DestinationT>
[[nodiscard]] Result<PixelMathExecutionResult>
executeInto(const ExecutionContext& execution, const PixelMathProgram& program,
            const PixelMathExecutionRequest& request, const PlannedColorReads* colorPlans,
            std::span<const double> statisticsByChannel, std::uint64_t statisticPasses)
{
    const ImageExtent target = *program.targetExtent();
    const std::uint32_t width = target.width();
    const std::uint32_t height = target.height();
    const std::uint32_t targetChannels = target.channelCount();
    const std::uint32_t channelCount = request.destinationChannel.has_value() ? 1u : targetChannels;
    const WalkPlan plan{.program = &program,
                        .request = &request,
                        .colorPlans = colorPlans,
                        .statisticsByChannel = statisticsByChannel,
                        .width = width,
                        .targetChannels = targetChannels,
                        .firstChannel = request.destinationChannel.value_or(0u),
                        .channelCount = channelCount};

    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * targetChannels *
                                        (request.images.size() + 1u) * sizeof(double),
                        .operationsPerItem =
                            static_cast<std::uint64_t>(width) * channelCount * program.instructions().size()};

    const bool byRun = execution.backend() != ComputeBackend::Reference && pixelMathProgramIsStraightLine(program);
    const WalkScratch shape = walkScratchFor(program, byRun);
    const std::uint32_t workers = (std::max)(plannedWorkerCount(execution, height, cost), 1u);
    Result<plane_detail::ScopedContextMemory> taken =
        plane_detail::ScopedContextMemory::take(execution, shape.slotBytes * workers, kCacheLineBytes, kOperation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    const plane_detail::ScopedContextMemory memory = std::move(taken).value();
    std::uint8_t* const scratchBytes = static_cast<std::uint8_t*>(memory.data());

    std::atomic<std::uint64_t> validEvaluations{0u};
    std::atomic<std::uint64_t> writtenSamples{0u};
    std::atomic<std::uint64_t> nonFiniteInputs{0u};
    std::atomic<std::uint64_t> saturatedSamples{0u};

    const Status walked = parallelForWorkers(execution, height, cost, [&](WorkerTask task) -> Status {
        double* const stack =
            reinterpret_cast<double*>(scratchBytes + static_cast<std::uint64_t>(task.workerSlot) * shape.slotBytes);
        double* const variables = stack + shape.stackValues;
        double* const arguments = variables + shape.variableValues;
        double* const statistics = arguments + shape.argumentValues;
        double* const results = statistics + shape.statisticValues;
        WalkCounts counts;
        Status done = Status::success();
        if (byRun)
        {
            const PixelMathRunScratch scratch{.lanes = shape.lanes,
                                              .stack = std::span<double>(stack, shape.stackValues),
                                              .variables = std::span<double>(variables, shape.variableValues),
                                              .arguments = std::span<double>(arguments, shape.argumentValues)};
            done = walkByRun<DestinationT>(plan, task.range, scratch, statistics, results,
                                           reinterpret_cast<std::uint8_t*>(results + shape.resultValues), counts);
        }
        else
        {
            PixelMathEvaluationScratch scratch;
            scratch.stack = std::span<double>(stack, shape.stackValues);
            scratch.variables = std::span<double>(variables, shape.variableValues);
            done = walkBySample<DestinationT>(plan, task.range, scratch, statistics, counts);
        }
        if (done.failed())
        {
            return done;
        }
        validEvaluations.fetch_add(counts.valid, std::memory_order_relaxed);
        writtenSamples.fetch_add(counts.written, std::memory_order_relaxed);
        nonFiniteInputs.fetch_add(counts.nonFinite, std::memory_order_relaxed);
        saturatedSamples.fetch_add(counts.saturated, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }

    PixelMathExecutionResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height * channelCount;
    result.run.selection.valid = validEvaluations.load(std::memory_order_relaxed);
    result.run.outputsWritten = writtenSamples.load(std::memory_order_relaxed);
    result.conversion.samplesConverted = result.run.outputsWritten;
    result.conversion.nonFiniteInputs = nonFiniteInputs.load(std::memory_order_relaxed);
    result.conversion.saturatedSamples = saturatedSamples.load(std::memory_order_relaxed);
    result.statisticPasses = statisticPasses;
    return result;
}

} // namespace

Result<PixelMathExecutionResult> executePixelMath(const ExecutionContext& execution, const PixelMathProgram& program,
                                                  const PixelMathExecutionRequest& request)
{
    const Status ready = validateInvocation(execution, program, request);
    if (ready.failed())
    {
        return ready;
    }

    if (execution.backend() != ComputeBackend::Reference)
    {
        const std::optional<AdditiveRandomPlan> additiveRandom = matchAdditiveRandomPlan(program);
        if (additiveRandom.has_value() && canExecuteAdditiveRandomDirectly(request, *additiveRandom))
        {
            return executeAdditiveRandomDirectly(execution, request, *additiveRandom);
        }
    }

    const Result<PixelMathColorPlans> colorPlans = planPixelMathColors(program);
    if (!colorPlans.hasValue())
    {
        return colorPlans.status();
    }

    // A whole-image statistic is of the whole image. The execution's validity rule says which samples
    // mean something, and that is a statement about the data rather than about the destination, so it
    // selects here as well; the packed mask travels with it because every bound image shares the
    // target's extent.
    const ImageExtent target = *program.targetExtent();
    const std::uint32_t targetChannels = target.channelCount();
    const std::uint32_t statisticCount = static_cast<std::uint32_t>(program.statistics().size());
    std::vector<double> statisticsByChannel(static_cast<std::size_t>(statisticCount) * targetChannels, 0.0);
    std::uint64_t statisticPasses = 0u;

    StatisticsSelection selection;
    selection.validity = request.validity.descriptor;
    selection.mask = request.validity.mask;

    for (std::uint32_t index = 0u; index < statisticCount; ++index)
    {
        const PixelMathStatisticRequest& wanted = program.statistics()[index];
        const PixelMathImageView& image = request.images[wanted.image];
        const std::uint32_t imageChannels = image.layout->extent().channelCount();
        for (std::uint32_t channel = 0u; channel < targetChannels; ++channel)
        {
            const std::uint32_t readChannel = imageChannels == 1u ? 0u : channel;
            if (readChannel >= imageChannels)
            {
                return executionFailure(ErrorCode::IncompatibleArguments,
                                        "a whole-image statistic was asked of a channel the image does not have");
            }
            const Result<double> value =
                computeStatistic(execution, image, readChannel, wanted.statistic, selection, statisticPasses);
            if (!value.hasValue())
            {
                return value.status();
            }
            statisticsByChannel[static_cast<std::size_t>(index) * targetChannels + channel] = *value;
        }
    }

    const std::span<const double> statistics(statisticsByChannel);
    const PlannedColorReads* const planned = PixelMathColorPlanner::read(*colorPlans);
    switch (request.destination.sampleType)
    {
    case SampleType::UInt8:
        return executeInto<std::uint8_t>(execution, program, request, planned, statistics, statisticPasses);
    case SampleType::Int8:
        return executeInto<std::int8_t>(execution, program, request, planned, statistics, statisticPasses);
    case SampleType::UInt16:
        return executeInto<std::uint16_t>(execution, program, request, planned, statistics, statisticPasses);
    case SampleType::Int16:
        return executeInto<std::int16_t>(execution, program, request, planned, statistics, statisticPasses);
    case SampleType::UInt32:
        return executeInto<std::uint32_t>(execution, program, request, planned, statistics, statisticPasses);
    case SampleType::Int32:
        return executeInto<std::int32_t>(execution, program, request, planned, statistics, statisticPasses);
    case SampleType::Float32:
        return executeInto<float>(execution, program, request, planned, statistics, statisticPasses);
    case SampleType::Float64:
        return executeInto<double>(execution, program, request, planned, statistics, statisticPasses);
    }
    return executionFailure(ErrorCode::InvalidArgument, "the expression destination names no resident sample type");
}

} // namespace an
