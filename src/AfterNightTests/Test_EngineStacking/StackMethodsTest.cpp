// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the stack methods: what a sum means in the output's numeric domain, the
// streaming combinations finalized through the method, and the two sample-history combinations -
// the exact median and the auto-adaptive weighted mean - written straight into the output from
// every frame's band, the same bits at every thread count and on every compiled backend.

#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethodRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 8u;
constexpr std::uint32_t kHeight = 4u;
constexpr std::uint32_t kFrames = 5u;

/// An engine with a stated concurrency.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// One operation in an engine, optionally narrowed, listened to and stoppable.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 std::uint32_t concurrency = an::kUseContextConcurrency,
                                                 an::DiagnosticSink* sink = nullptr, std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.diagnosticSink = sink;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// One operation forced onto one implementation, with no fallback permitted.
[[nodiscard]] an::ExecutionContext forcedExecution(const an::EngineContext& engine, an::ComputeBackend backend,
                                                   an::InstructionSet instructionSet, std::uint32_t concurrency = 2u)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    options.instructionSet = instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A descriptor of the stated representation, schema and numeric domain.
[[nodiscard]] an::ImageDescriptor makeDescriptor(an::SampleType sampleType = an::SampleType::Float32,
                                                 std::uint32_t width = kWidth, std::uint32_t height = kHeight,
                                                 an::ChannelSchema schema = an::ChannelSchema::Mono,
                                                 an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const std::uint32_t channels = an::channelCountOf(schema).value_or(1u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{
        .extent = *extent,
        .sampleType = sampleType,
        .channelSchema = schema,
        .colorSpace = schema == an::ChannelSchema::Rgb ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified,
        .numericDomain = domain,
        .cfa = {},
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A stack of the stated combination over the stated frames.
[[nodiscard]] an::StackRequest requestOf(an::StackCombination combination, std::uint32_t frames = kFrames,
                                         const an::ImageDescriptor& descriptor = makeDescriptor())
{
    return an::StackRequest{.input = an::StackInputDescription{.frameCount = frames, .descriptor = descriptor},
                            .combination = combination};
}

/// The checked model of a request every rule admits.
[[nodiscard]] an::StackModel makeModel(const an::StackRequest& request)
{
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    return model.has_value() ? *model : *an::StackModel::create(requestOf(an::StackCombination::Average));
}

/// A layout of one plane per channel, rows of exactly the width.
[[nodiscard]] an::ImageLayout planarLayout(an::SampleType sampleType, std::uint32_t width, std::uint32_t rows,
                                           std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, rows, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = an::sampleTypeSizeBytes(sampleType),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A layout of one plane holding every channel of a pixel together.
[[nodiscard]] an::ImageLayout interleavedLayout(an::SampleType sampleType, std::uint32_t width, std::uint32_t rows,
                                                std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, rows, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = an::sampleTypeSizeBytes(sampleType),
                                         .samplesPerPixel = channels,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A validity descriptor of one rule, with the stated range under `DomainRange`.
[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule, double lowest = 0.0, double highest = 1.0)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    if (rule == an::ValidityRule::DomainRange)
    {
        request.lowestValid = lowest;
        request.highestValid = highest;
    }
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value_or(an::ValidityDescriptor{});
}

/// One frame's band, owned here: the samples, the layout, the rule, the mask and the local weights.
struct OwnedBand
{
    std::vector<float> float32;
    std::vector<double> float64;
    std::optional<an::ImageLayout> layout;
    an::ValidityDescriptor descriptor{};
    std::vector<std::uint8_t> maskBits;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    std::vector<float> localWeights;
    std::uint32_t frameIndex = 0u;
    std::uint32_t firstOutputRow = 0u;

    [[nodiscard]] an::StackFrameBand band() const
    {
        const bool wide = !float64.empty();
        return an::StackFrameBand{
            .sampleType = wide ? an::SampleType::Float64 : an::SampleType::Float32,
            .samples = wide ? static_cast<const void*>(float64.data()) : static_cast<const void*>(float32.data()),
            .layout = layout.has_value() ? &*layout : nullptr,
            .frameIndex = frameIndex,
            .firstOutputRow = firstOutputRow,
            .validity = an::StackBandValidity{.descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr},
            .localWeights = localWeights};
    }

    /// The sample at one position, whatever the representation.
    [[nodiscard]] double at(std::size_t index) const
    {
        return float64.empty() ? static_cast<double>(float32[index]) : float64[index];
    }

    /// Writes one sample, whatever the representation.
    void set(std::size_t index, double value)
    {
        if (float64.empty())
        {
            float32[index] = static_cast<float>(value);
        }
        else
        {
            float64[index] = value;
        }
    }

    /// Gives the band a packed mask of the stated plane count, every bit set.
    void addMask(std::uint32_t width, std::uint32_t rows, std::uint32_t planes)
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width, rows, planes, an::defaultEngineLimits());
        ASSERT_TRUE(extent.has_value());
        const std::uint64_t rowBytes = an::packedMaskRowBytes(width);
        maskBits.assign(static_cast<std::size_t>(rowBytes * rows * planes), 0xFFu);
        mask = an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>(maskBits), *extent);
        ASSERT_TRUE(mask.has_value());
    }

    /// Clears one bit of the mask.
    void clearBit(std::uint32_t plane, std::uint32_t x, std::uint32_t y)
    {
        ASSERT_TRUE(mask.has_value());
        const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask->planeStrideBytes() +
                                     static_cast<std::uint64_t>(y) * mask->rowStrideBytes() + x / 8u;
        maskBits[static_cast<std::size_t>(offset)] &= static_cast<std::uint8_t>(~(1u << (x % 8u)));
    }
};

/// A small deterministic generator, so a sweep is the same on every host.
class Lcg
{
public:
    explicit Lcg(std::uint64_t seed) noexcept : m_state(seed) {}

    [[nodiscard]] std::uint64_t next() noexcept
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return m_state >> 11u;
    }

    [[nodiscard]] std::uint32_t below(std::uint32_t bound) noexcept
    {
        return static_cast<std::uint32_t>(next() % bound);
    }

    [[nodiscard]] double unit() noexcept
    {
        return static_cast<double>(next() & 0xFFFFFFull) / static_cast<double>(0x1000000ull);
    }

private:
    std::uint64_t m_state;
};

/// The sample a test frame holds, a function of where it is so that no two positions agree and the
/// frames are not in rank order.
[[nodiscard]] float sampleAt(std::uint32_t frame, std::uint32_t channel, std::uint32_t y, std::uint32_t x,
                             std::uint32_t width)
{
    const std::uint32_t rank = (frame * 7u + x * 3u + y) % 11u;
    return 0.05f * static_cast<float>(rank + 1u) + 0.01f * static_cast<float>(channel) +
           0.001f * static_cast<float>(y * width + x);
}

/// A planar Float32 band of the stated shape filled by `sampleAt`, under the all-valid rule.
[[nodiscard]] OwnedBand makeBand(std::uint32_t frame, std::uint32_t width, std::uint32_t rows, std::uint32_t channels,
                                 std::uint32_t firstOutputRow = 0u)
{
    OwnedBand owned;
    owned.frameIndex = frame;
    owned.firstOutputRow = firstOutputRow;
    owned.layout = planarLayout(an::SampleType::Float32, width, rows, channels);
    owned.float32.resize(static_cast<std::size_t>(width) * rows * channels);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < rows; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                owned.float32[(static_cast<std::size_t>(channel) * rows + y) * width + x] =
                    sampleAt(frame, channel, firstOutputRow + y, x, width);
            }
        }
    }
    return owned;
}

/// Every frame's band of one whole stack, planar Float32, filled by `sampleAt`.
[[nodiscard]] std::vector<OwnedBand> makeHistory(std::uint32_t frames, std::uint32_t width, std::uint32_t rows,
                                                 std::uint32_t channels)
{
    std::vector<OwnedBand> history;
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        history.push_back(makeBand(frame, width, rows, channels));
    }
    return history;
}

/// The bands of an owned history, as the operation borrows them.
[[nodiscard]] std::vector<an::StackFrameBand> bandsOf(const std::vector<OwnedBand>& history)
{
    std::vector<an::StackFrameBand> bands;
    for (const OwnedBand& owned : history)
    {
        bands.push_back(owned.band());
    }
    return bands;
}

/// The five terms of a streaming stack, owned here.
struct OwnedAccumulators
{
    std::vector<double> sums;
    std::vector<std::uint32_t> counts;
    std::vector<double> weightSums;
    std::vector<double> weightSquareSums;
    std::vector<double> varianceMoments;

    [[nodiscard]] static OwnedAccumulators forModel(const an::StackModel& model)
    {
        const std::uint64_t elements = model.request().input.descriptor.extent().sampleCount();
        OwnedAccumulators owned;
        owned.sums.assign(static_cast<std::size_t>(elements), 0.0);
        owned.counts.assign(static_cast<std::size_t>(elements), 0u);
        if (an::stackAccumulatorKept(model, an::StackAccumulator::WeightSums))
        {
            owned.weightSums.assign(static_cast<std::size_t>(elements), 0.0);
        }
        if (an::stackAccumulatorKept(model, an::StackAccumulator::WeightSquareSums))
        {
            owned.weightSquareSums.assign(static_cast<std::size_t>(elements), 0.0);
        }
        if (an::stackAccumulatorKept(model, an::StackAccumulator::VarianceMoments))
        {
            owned.varianceMoments.assign(static_cast<std::size_t>(2u * elements), 0.0);
        }
        return owned;
    }

    [[nodiscard]] an::StackAccumulatorSet set()
    {
        return an::StackAccumulatorSet{.sums = sums,
                                       .counts = counts,
                                       .weightSums = weightSums,
                                       .weightSquareSums = weightSquareSums,
                                       .varianceMoments = varianceMoments};
    }
};

/// Where a finalization or a sample-history combination writes, owned here: the image at the
/// model's output representation, a mask over the output, and every map the model requests.
struct OwnedTargets
{
    an::SampleType outputType = an::SampleType::Float32;
    std::vector<std::byte> image;
    std::vector<std::uint8_t> maskBits;
    std::optional<an::ValidityMaskView<std::uint8_t>> mask;
    std::vector<std::uint32_t> coverage;
    std::vector<float> variance;
    std::vector<float> gain;
    std::vector<float> weightSum;

    [[nodiscard]] static OwnedTargets forModel(const an::StackModel& model, bool withMask = true)
    {
        const an::StackRequest& request = model.request();
        const an::ImageExtent& extent = request.input.descriptor.extent();
        const std::uint64_t elements = extent.sampleCount();
        OwnedTargets owned;
        owned.outputType = request.output.sampleType;
        owned.image.assign(static_cast<std::size_t>(elements * an::sampleTypeSizeBytes(owned.outputType)),
                           std::byte{0x5A});
        if (withMask)
        {
            const std::uint64_t rowBytes = an::packedMaskRowBytes(extent.width());
            owned.maskBits.assign(static_cast<std::size_t>(rowBytes * extent.height() * extent.channelCount()), 0u);
            owned.mask =
                an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>(owned.maskBits), extent);
            EXPECT_TRUE(owned.mask.has_value());
        }
        if (request.output.coverageMap)
        {
            owned.coverage.assign(static_cast<std::size_t>(elements), 77u);
        }
        if (request.output.varianceMap)
        {
            owned.variance.assign(static_cast<std::size_t>(elements), -1.0f);
        }
        if (request.output.signalToNoiseGainMap)
        {
            owned.gain.assign(static_cast<std::size_t>(elements), -1.0f);
        }
        if (request.output.weightSumMap)
        {
            owned.weightSum.assign(static_cast<std::size_t>(elements), -1.0f);
        }
        return owned;
    }

    [[nodiscard]] an::StackFinalizeTargets targets()
    {
        return an::StackFinalizeTargets{.outputSampleType = outputType,
                                        .outputImage = image,
                                        .outputMask = mask.has_value() ? &*mask : nullptr,
                                        .coverage = coverage,
                                        .variance = variance,
                                        .signalToNoiseGain = gain,
                                        .weightSum = weightSum};
    }

    /// The output samples read back as doubles.
    [[nodiscard]] std::vector<double> values() const
    {
        std::vector<double> read;
        if (outputType == an::SampleType::Float32)
        {
            read.resize(image.size() / sizeof(float));
            for (std::size_t index = 0u; index < read.size(); ++index)
            {
                float value = 0.0f;
                std::memcpy(&value, image.data() + index * sizeof(float), sizeof(float));
                read[index] = static_cast<double>(value);
            }
        }
        else
        {
            read.resize(image.size() / sizeof(double));
            std::memcpy(read.data(), image.data(), read.size() * sizeof(double));
        }
        return read;
    }

    /// Every written byte, one after another, for an exact comparison.
    [[nodiscard]] std::vector<std::byte> bytes() const
    {
        std::vector<std::byte> all(image.begin(), image.end());
        const auto append = [&all](const auto& values) {
            const std::span<const std::byte> raw = std::as_bytes(std::span(values));
            all.insert(all.end(), raw.begin(), raw.end());
        };
        append(maskBits);
        append(coverage);
        append(variance);
        append(gain);
        append(weightSum);
        return all;
    }
};

/// The per-worker scratch of a sample-history stack, owned here and aligned as the working set
/// aligns it.
struct OwnedScratch
{
    std::vector<std::vector<std::byte>> blocks;
    std::vector<std::span<std::byte>> spans;

    [[nodiscard]] static OwnedScratch forModel(const an::StackModel& model, std::uint32_t workers,
                                               std::uint64_t bytesPerWorker = 0u, std::uint64_t misalignBy = 0u)
    {
        const std::uint64_t bytes = bytesPerWorker == 0u ? an::stackScratchBytesPerWorker(model) : bytesPerWorker;
        OwnedScratch owned;
        for (std::uint32_t worker = 0u; worker < workers; ++worker)
        {
            owned.blocks.emplace_back(static_cast<std::size_t>(bytes + 2u * an::kStackScratchAlignmentBytes));
        }
        for (std::vector<std::byte>& block : owned.blocks)
        {
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(block.data());
            const std::uintptr_t aligned = (address + an::kStackScratchAlignmentBytes - 1u) /
                                           an::kStackScratchAlignmentBytes * an::kStackScratchAlignmentBytes;
            std::byte* const begin = block.data() + (aligned - address) + misalignBy;
            owned.spans.emplace_back(begin, static_cast<std::size_t>(bytes));
        }
        return owned;
    }

    [[nodiscard]] std::span<const std::span<std::byte>> view() const
    {
        return spans;
    }
};

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
}

/// The first text-valued context field under a key, if the failure carries one.
[[nodiscard]] std::optional<std::string> textField(const an::Status& status, an::ContextKey key)
{
    if (status.detail() == nullptr)
    {
        return std::nullopt;
    }
    for (const an::StatusContextField& field : status.detail()->context)
    {
        if (field.key == key && field.kind == an::ContextValueKind::Text)
        {
            return std::string(field.text);
        }
    }
    return std::nullopt;
}

/// The first count-valued context field under a key, if the failure carries one.
[[nodiscard]] std::optional<std::uint64_t> countField(const an::Status& status, an::ContextKey key)
{
    if (status.detail() == nullptr)
    {
        return std::nullopt;
    }
    for (const an::StatusContextField& field : status.detail()->context)
    {
        if (field.key == key && field.kind == an::ContextValueKind::Count)
        {
            return field.count;
        }
    }
    return std::nullopt;
}

/// The compiled instruction sets this build contains and this host can run.
[[nodiscard]] std::vector<an::InstructionSet> compiledTargets()
{
    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(index);
        if (candidate != an::InstructionSet::Automatic && candidate != an::InstructionSet::Scalar &&
            an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            targets.push_back(candidate);
        }
    }
    return targets;
}

/// A sink that keeps every event it hears.
class RecordingDiagnosticSink final : public an::DiagnosticSink
{
public:
    struct Event
    {
        an::DiagnosticSeverity severity = an::DiagnosticSeverity::Info;
        std::uint64_t operationId = 0u;
        std::string operation;
        std::string message;
    };

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_events.push_back(Event{.severity = event.severity,
                                 .operationId = event.operationId,
                                 .operation = event.operation,
                                 .message = event.message});
    }

    [[nodiscard]] std::vector<Event> from(std::string_view operation) const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        std::vector<Event> matching;
        for (const Event& event : m_events)
        {
            if (event.operation == operation)
            {
                matching.push_back(event);
            }
        }
        return matching;
    }

private:
    mutable std::mutex m_guard;
    std::vector<Event> m_events;
};

/// The exact median of a column: the middle sample, or the mean of the two middle ones.
[[nodiscard]] double medianOracle(std::vector<double> column)
{
    if (column.empty())
    {
        return 0.0;
    }
    std::sort(column.begin(), column.end());
    const std::size_t half = column.size() / 2u;
    if (column.size() % 2u != 0u)
    {
        return column[half];
    }
    return (column[half - 1u] + column[half]) * 0.5;
}

/// The auto-adaptive weighted mean of a column, as the adopted algorithm computes it over the
/// samples scaled by `kAutoAdaptiveSampleScale`, in its own operation order.
[[nodiscard]] double autoAdaptiveOracle(const std::vector<double>& column)
{
    const double scale = an::kAutoAdaptiveSampleScale;
    const std::size_t count = column.size();
    if (count == 0u)
    {
        return 0.0;
    }
    const double n = static_cast<double>(count);
    double sum = 0.0;
    for (const double value : column)
    {
        sum = sum + value * scale;
    }
    double average = sum / n;
    if (count < an::kAutoAdaptiveMinimumSamples)
    {
        return average / scale;
    }
    std::vector<double> weights(count, 0.0);
    for (std::uint32_t iteration = 0u; iteration < an::kAutoAdaptiveIterations; ++iteration)
    {
        double squares = 0.0;
        for (const double value : column)
        {
            const double deviation = value * scale - average;
            squares = squares + deviation * deviation;
        }
        const double sigma = std::sqrt(squares / n);
        if (iteration == 0u && sigma <= an::kAutoAdaptiveSmallSigma)
        {
            return average / scale;
        }
        for (std::size_t index = 0u; index < count; ++index)
        {
            const double distance = std::fabs(column[index] * scale - average);
            const double ratio = distance / sigma;
            weights[index] = 1.0 / (1.0 + ratio * ratio);
        }
        double total = 0.0;
        for (const double weight : weights)
        {
            total = total + weight;
        }
        double next = 0.0;
        for (std::size_t index = 0u; index < count; ++index)
        {
            next = next + (column[index] * scale * weights[index]) / total;
        }
        average = next;
    }
    return average / scale;
}

/// The sample variance of a column in two passes, zero below two samples.
[[nodiscard]] double varianceOracle(const std::vector<double>& column)
{
    if (column.size() < 2u)
    {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : column)
    {
        sum = sum + value;
    }
    const double mean = sum / static_cast<double>(column.size());
    double squares = 0.0;
    for (const double value : column)
    {
        const double deviation = value - mean;
        squares = squares + deviation * deviation;
    }
    return squares / static_cast<double>(column.size() - 1u);
}

/// The samples of one output element across an owned history, in frame order, every rule admitted.
[[nodiscard]] std::vector<double> columnOf(const std::vector<OwnedBand>& history, std::size_t element)
{
    std::vector<double> column;
    for (const OwnedBand& owned : history)
    {
        column.push_back(owned.at(element));
    }
    return column;
}

/// Runs the sample-history combination of a model over owned bands with fresh scratch.
[[nodiscard]] an::Result<an::StackSampleHistoryOutcome> combine(const an::ExecutionContext& execution,
                                                                const an::StackModel& model,
                                                                const std::vector<OwnedBand>& history,
                                                                OwnedTargets& targets)
{
    const std::vector<an::StackFrameBand> bands = bandsOf(history);
    const OwnedScratch scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
    return an::combineStackSampleHistory(execution, model, bands, scratch.view(), targets.targets());
}

// The domain table and the combination table are constant expressions, so a kernel and an executor
// can be written against them.
constexpr std::optional<an::ImageExtent> kConstantExtent =
    an::ImageExtent::create(16u, 16u, 1u, an::defaultEngineLimits());
static_assert(kConstantExtent.has_value());
constexpr std::optional<an::ImageDescriptor> kConstantDescriptor =
    an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *kConstantExtent,
                                                           .sampleType = an::SampleType::Float32,
                                                           .channelSchema = an::ChannelSchema::Mono,
                                                           .colorSpace = an::ColorSpace::Unspecified,
                                                           .numericDomain = an::NumericDomain::NormalizedLinear,
                                                           .cfa = {},
                                                           .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                           .pixelAspectRatio = 1.0});
static_assert(kConstantDescriptor.has_value());
constexpr std::optional<an::StackModel> kConstantSum = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
                     .combination = an::StackCombination::Sum});
constexpr std::optional<an::StackModel> kConstantMedian = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
                     .combination = an::StackCombination::Median});
constexpr std::optional<an::StackModel> kConstantAdaptive = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
                     .combination = an::StackCombination::AutoAdaptiveWeightedAverage});
constexpr std::optional<an::StackModel> kConstantClipped = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
                     .rejection = an::StackRejectionRequest{.method = an::StackRejection::SigmaClipping}});
static_assert(kConstantSum.has_value() && kConstantMedian.has_value() && kConstantAdaptive.has_value() &&
              kConstantClipped.has_value());
static_assert(an::stackMethodOutputDomain(*kConstantSum, an::StackSumDomain::NormalizedToPeak) ==
              an::NumericDomain::NormalizedLinear);
static_assert(an::stackMethodOutputDomain(*kConstantSum, an::StackSumDomain::Raw) == an::NumericDomain::PhysicalScaled);
static_assert(an::stackMethodOutputDomain(*kConstantMedian, an::StackSumDomain::Raw) ==
              an::NumericDomain::NormalizedLinear);
static_assert(an::stackMethodOutputDescriptor(*kConstantSum, an::StackSumDomain::Raw).has_value());
static_assert(an::stackSampleHistoryCombinationOf(*kConstantMedian) == an::StackSampleHistoryCombination::Median);
static_assert(an::stackSampleHistoryCombinationOf(*kConstantAdaptive) ==
              an::StackSampleHistoryCombination::AutoAdaptiveWeightedAverage);
static_assert(!an::stackSampleHistoryCombinationOf(*kConstantSum).has_value());
static_assert(!an::stackSampleHistoryCombinationOf(*kConstantClipped).has_value());
static_assert(an::isStackSumDomain(an::StackSumDomain::Raw));
static_assert(!an::isStackSumDomain(static_cast<an::StackSumDomain>(2u)));
static_assert(an::isStackSampleHistoryCombination(an::StackSampleHistoryCombination::AutoAdaptiveWeightedAverage));
static_assert(!an::isStackSampleHistoryCombination(static_cast<an::StackSampleHistoryCombination>(2u)));
static_assert(an::kDefaultStackSumDomain == an::StackSumDomain::NormalizedToPeak);
static_assert(an::kAutoAdaptiveIterations == 5u);
static_assert(an::kAutoAdaptiveSmallSigma == 5.0);
static_assert(an::kAutoAdaptiveMinimumSamples == 3u);
static_assert(an::kStackSampleColumnLanes == 8u);
static_assert(an::stackScratchBytesPerWorker(*kConstantMedian) ==
              an::alignedStackScratchTerm(3u * sizeof(double)) +
                  an::alignedStackScratchTerm(an::kStackSampleColumnLanes * 3u * sizeof(double)) +
                  an::alignedStackScratchTerm(3u * an::kStackColumnFrameRecordBytes));
static_assert(an::stackScratchBytesPerWorker(*kConstantAdaptive) ==
              an::alignedStackScratchTerm(3u * sizeof(double)) +
                  an::alignedStackScratchTerm(2u * an::kStackSampleColumnLanes * 3u * sizeof(double)) +
                  an::alignedStackScratchTerm(3u * an::kStackColumnFrameRecordBytes));

} // namespace

TEST(StackMethodsTest, NamesTheSumDomainsAndRestatesTheOutputDomain)
{
    EXPECT_STREQ(an::stackSumDomainName(an::StackSumDomain::NormalizedToPeak), "NormalizedToPeak");
    EXPECT_STREQ(an::stackSumDomainName(an::StackSumDomain::Raw), "Raw");
    EXPECT_STREQ(an::stackSumDomainName(static_cast<an::StackSumDomain>(9u)), "UnknownStackSumDomain");
    EXPECT_STREQ(an::stackSampleHistoryCombinationName(an::StackSampleHistoryCombination::Median), "Median");
    EXPECT_STREQ(an::stackSampleHistoryCombinationName(an::StackSampleHistoryCombination::AutoAdaptiveWeightedAverage),
                 "AutoAdaptiveWeightedAverage");
    EXPECT_STREQ(an::stackSampleHistoryCombinationName(static_cast<an::StackSampleHistoryCombination>(9u)),
                 "UnknownStackSampleHistoryCombination");

    // Every combination but the sum keeps the input's domain whatever the sum domain says.
    for (const an::StackCombination combination :
         {an::StackCombination::Average, an::StackCombination::Median, an::StackCombination::Minimum,
          an::StackCombination::Maximum, an::StackCombination::AutoAdaptiveWeightedAverage})
    {
        const an::StackModel model = makeModel(requestOf(combination));
        for (const an::StackSumDomain domain : {an::StackSumDomain::NormalizedToPeak, an::StackSumDomain::Raw})
        {
            EXPECT_EQ(an::stackMethodOutputDomain(model, domain), an::NumericDomain::NormalizedLinear)
                << an::stackCombinationName(combination);
            const std::optional<an::ImageDescriptor> descriptor = an::stackMethodOutputDescriptor(model, domain);
            ASSERT_TRUE(descriptor.has_value());
            EXPECT_EQ(*descriptor, model.outputDescriptor());
            EXPECT_TRUE(an::checkStackSumDomain(model, domain).ok());
        }
    }

    // The sum's table: a fraction of the peak is a fraction of full scale, a raw sum is a linear
    // quantity in the input's units, and a raw sum of encoded samples is nothing this engine names.
    struct Row
    {
        an::NumericDomain input;
        an::ChannelSchema schema;
        std::optional<an::NumericDomain> normalized;
        std::optional<an::NumericDomain> raw;
    };
    const std::array<Row, 4u> rows{{
        {an::NumericDomain::NormalizedLinear, an::ChannelSchema::Rgb, an::NumericDomain::NormalizedLinear,
         an::NumericDomain::PhysicalScaled},
        {an::NumericDomain::EncodedNonlinear, an::ChannelSchema::Rgb, an::NumericDomain::EncodedNonlinear,
         std::nullopt},
        {an::NumericDomain::RawSensorCode, an::ChannelSchema::Mono, an::NumericDomain::NormalizedLinear,
         an::NumericDomain::PhysicalScaled},
        {an::NumericDomain::PhysicalScaled, an::ChannelSchema::Rgb, an::NumericDomain::NormalizedLinear,
         an::NumericDomain::PhysicalScaled},
    }};
    for (const Row& row : rows)
    {
        const an::StackModel model =
            makeModel(requestOf(an::StackCombination::Sum, 3u,
                                makeDescriptor(an::SampleType::Float32, kWidth, kHeight, row.schema, row.input)));
        EXPECT_EQ(an::stackMethodOutputDomain(model, an::StackSumDomain::NormalizedToPeak), row.normalized)
            << an::numericDomainName(row.input);
        EXPECT_EQ(an::stackMethodOutputDomain(model, an::StackSumDomain::Raw), row.raw)
            << an::numericDomainName(row.input);
        const std::optional<an::ImageDescriptor> normalized =
            an::stackMethodOutputDescriptor(model, an::StackSumDomain::NormalizedToPeak);
        ASSERT_TRUE(normalized.has_value());
        EXPECT_EQ(normalized->numericDomain(), *row.normalized);
        EXPECT_EQ(normalized->sampleType(), an::SampleType::Float32);
        EXPECT_EQ(normalized->extent(), model.outputDescriptor().extent());
        EXPECT_TRUE(an::checkStackSumDomain(model, an::StackSumDomain::NormalizedToPeak).ok());

        const std::optional<an::ImageDescriptor> raw = an::stackMethodOutputDescriptor(model, an::StackSumDomain::Raw);
        const an::Status rawCheck = an::checkStackSumDomain(model, an::StackSumDomain::Raw);
        EXPECT_EQ(raw.has_value(), row.raw.has_value());
        EXPECT_EQ(rawCheck.ok(), row.raw.has_value()) << rawCheck.message();
        if (raw.has_value())
        {
            EXPECT_EQ(raw->numericDomain(), *row.raw);
        }
        else
        {
            EXPECT_EQ(rawCheck.code(), an::ErrorCode::IncompatibleArguments);
            EXPECT_EQ(textField(rawCheck, an::ContextKey::Parameter), std::optional<std::string>{"sumDomain"});
            EXPECT_TRUE(messageNames(rawCheck, "EncodedNonlinear")) << rawCheck.message();
        }
    }

    // A value outside the enumeration is refused first, whatever the combination.
    const an::Status unknown = an::checkStackSumDomain(makeModel(requestOf(an::StackCombination::Average)),
                                                       static_cast<an::StackSumDomain>(7u));
    EXPECT_EQ(unknown.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(unknown, an::ContextKey::Parameter), std::optional<std::string>{"sumDomain"});
    EXPECT_FALSE(an::stackMethodOutputDomain(*kConstantSum, static_cast<an::StackSumDomain>(7u)).has_value());
}

TEST(StackMethodsTest, NormalizesASumToItsPeakAndWritesTheRawSumOnRequest)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = requestOf(an::StackCombination::Sum, 3u);
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    constexpr std::size_t kElements = static_cast<std::size_t>(kWidth) * kHeight;

    RecordingDiagnosticSink sink;
    const an::ExecutionContext execution = makeExecution(engine, an::kUseContextConcurrency, &sink);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    std::vector<double> sums(kElements, 0.0);
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        const OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            sums[element] += static_cast<double>(band.float32[element]);
        }
    }
    const double peak = *std::max_element(sums.begin(), sums.end());
    ASSERT_GT(peak, 1.0);

    OwnedTargets normalized = OwnedTargets::forModel(model);
    const an::Result<an::StackMethodFinalizeOutcome> peaked = an::finalizeStackMethod(
        execution, model, an::StackSumDomain::NormalizedToPeak, owned.set(), normalized.targets());
    ASSERT_TRUE(peaked.hasValue()) << peaked.status().message();
    EXPECT_EQ(peaked->sumDomain, an::StackSumDomain::NormalizedToPeak);
    EXPECT_EQ(peaked->outputDomain, an::NumericDomain::NormalizedLinear);
    EXPECT_TRUE(peaked->normalizedToPeak);
    EXPECT_EQ(peaked->peakSum, peak);
    EXPECT_EQ(peaked->finalize.elementsWritten, kElements);
    EXPECT_EQ(peaked->finalize.elementsWithSamples, kElements);
    EXPECT_EQ(peaked->finalize.nonFiniteOutputs, 0u);
    const std::vector<double> peakedValues = normalized.values();
    double largest = 0.0;
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(peakedValues[element], static_cast<double>(static_cast<float>(sums[element] / peak))) << element;
        largest = (std::max)(largest, peakedValues[element]);
        EXPECT_EQ(normalized.coverage[element], 3u);
    }
    EXPECT_EQ(largest, 1.0);
    EXPECT_EQ(normalized.mask->countValid(), kElements);
    const std::vector<RecordingDiagnosticSink::Event> said = sink.from("an::finalizeStackMethod");
    ASSERT_EQ(said.size(), 1u);
    EXPECT_EQ(said[0].severity, an::DiagnosticSeverity::Info);
    EXPECT_EQ(said[0].operationId, execution.operationId());
    EXPECT_NE(said[0].message.find("peak"), std::string::npos);

    OwnedTargets raw = OwnedTargets::forModel(model);
    const an::Result<an::StackMethodFinalizeOutcome> plain =
        an::finalizeStackMethod(execution, model, an::StackSumDomain::Raw, owned.set(), raw.targets());
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_EQ(plain->sumDomain, an::StackSumDomain::Raw);
    EXPECT_EQ(plain->outputDomain, an::NumericDomain::PhysicalScaled);
    EXPECT_FALSE(plain->normalizedToPeak);
    EXPECT_EQ(plain->peakSum, 0.0);
    const std::vector<double> rawValues = raw.values();
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(rawValues[element], static_cast<double>(static_cast<float>(sums[element]))) << element;
    }

    // The raw sum of encoded samples has no domain, and is refused before anything is written.
    const an::StackModel encoded =
        makeModel(requestOf(an::StackCombination::Sum, 3u,
                            makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Mono,
                                           an::NumericDomain::EncodedNonlinear)));
    OwnedAccumulators encodedTerms = OwnedAccumulators::forModel(encoded);
    OwnedTargets untouched = OwnedTargets::forModel(encoded);
    const std::vector<std::byte> before = untouched.bytes();
    const an::Result<an::StackMethodFinalizeOutcome> refused =
        an::finalizeStackMethod(execution, encoded, an::StackSumDomain::Raw, encodedTerms.set(), untouched.targets());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(textField(refused.status(), an::ContextKey::Parameter), std::optional<std::string>{"sumDomain"});
    EXPECT_EQ(untouched.bytes(), before);
}

TEST(StackMethodsTest, NormalizesAWeightedSumAndKeepsTheWeightMapsUnscaled)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = requestOf(an::StackCombination::Sum, 3u);
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.weightSumMap = true;
    request.output.signalToNoiseGainMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<double> weights{0.5, 2.0, 1.0};
    constexpr std::size_t kElements = static_cast<std::size_t>(kWidth) * kHeight;

    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    std::vector<double> sums(kElements, 0.0);
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        const OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), weights);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            sums[element] += static_cast<double>(band.float32[element]) * weights[frame];
        }
    }
    const double peak = *std::max_element(sums.begin(), sums.end());

    OwnedTargets targets = OwnedTargets::forModel(model);
    const an::Result<an::StackMethodFinalizeOutcome> outcome =
        an::finalizeStackMethod(execution, model, an::kDefaultStackSumDomain, owned.set(), targets.targets());
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_TRUE(outcome->normalizedToPeak);
    EXPECT_EQ(outcome->peakSum, peak);
    const std::vector<double> values = targets.values();
    const double weightSum = 3.5;
    const double weightSquareSum = 0.25 + 4.0 + 1.0;
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(values[element], static_cast<double>(static_cast<float>(sums[element] / peak))) << element;
        EXPECT_EQ(targets.weightSum[element], static_cast<float>(weightSum)) << element;
        EXPECT_EQ(targets.gain[element], static_cast<float>(weightSum * weightSum / weightSquareSum)) << element;
    }
}

TEST(StackMethodsTest, WritesTheSumsUnscaledWhenNoPeakIsPositive)
{
    const an::EngineContext engine = makeEngine();
    RecordingDiagnosticSink sink;
    const an::ExecutionContext execution = makeExecution(engine, an::kUseContextConcurrency, &sink);
    an::StackRequest request = requestOf(an::StackCombination::Sum, 2u);
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    constexpr std::size_t kElements = static_cast<std::size_t>(kWidth) * kHeight;

    // Every sample below zero: the sums have no positive peak and are written as they are.
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    std::vector<double> sums(kElements, 0.0);
    for (std::uint32_t frame = 0u; frame < 2u; ++frame)
    {
        OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            band.float32[element] = -band.float32[element];
            sums[element] += static_cast<double>(band.float32[element]);
        }
        const an::Result<an::StackAccumulationOutcome> accumulated =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
        ASSERT_TRUE(accumulated.hasValue()) << accumulated.status().message();
    }
    OwnedTargets targets = OwnedTargets::forModel(model);
    const an::Result<an::StackMethodFinalizeOutcome> outcome =
        an::finalizeStackMethod(execution, model, an::StackSumDomain::NormalizedToPeak, owned.set(), targets.targets());
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_FALSE(outcome->normalizedToPeak);
    EXPECT_EQ(outcome->peakSum, *std::max_element(sums.begin(), sums.end()));
    EXPECT_LT(outcome->peakSum, 0.0);
    EXPECT_EQ(outcome->outputDomain, an::NumericDomain::NormalizedLinear);
    const std::vector<double> values = targets.values();
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(values[element], static_cast<double>(static_cast<float>(sums[element]))) << element;
    }
    const std::vector<RecordingDiagnosticSink::Event> said = sink.from("an::finalizeStackMethod");
    ASSERT_EQ(said.size(), 1u);
    EXPECT_EQ(said[0].severity, an::DiagnosticSeverity::Warning);
    EXPECT_NE(said[0].message.find("peak"), std::string::npos);

    // No element received a sample: there is no peak at all, and every element is zero and clear.
    OwnedAccumulators empty = OwnedAccumulators::forModel(model);
    for (std::uint32_t frame = 0u; frame < 2u; ++frame)
    {
        OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
        band.descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
        std::fill(band.float32.begin(), band.float32.end(), 0.0f);
        const an::Result<an::StackAccumulationOutcome> accumulated =
            an::accumulateStackFrame(execution, model, empty.set(), band.band(), {});
        ASSERT_TRUE(accumulated.hasValue()) << accumulated.status().message();
    }
    OwnedTargets nothing = OwnedTargets::forModel(model);
    const an::Result<an::StackMethodFinalizeOutcome> none =
        an::finalizeStackMethod(execution, model, an::StackSumDomain::NormalizedToPeak, empty.set(), nothing.targets());
    ASSERT_TRUE(none.hasValue()) << none.status().message();
    EXPECT_FALSE(none->normalizedToPeak);
    EXPECT_EQ(none->peakSum, 0.0);
    EXPECT_EQ(none->finalize.elementsWithoutSamples, kElements);
    EXPECT_EQ(nothing.mask->countValid(), 0u);
    for (const double value : nothing.values())
    {
        EXPECT_EQ(value, 0.0);
    }
}

TEST(StackMethodsTest, FinalizesEveryOtherCombinationAsTheAccumulatorsDo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const an::StackCombination combination :
         {an::StackCombination::Average, an::StackCombination::Minimum, an::StackCombination::Maximum})
    {
        an::StackRequest request = requestOf(
            combination, 3u, makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
        request.output.coverageMap = true;
        request.output.varianceMap = true;
        const an::StackModel model = makeModel(request);
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        for (std::uint32_t frame = 0u; frame < 3u; ++frame)
        {
            const OwnedBand band = makeBand(frame, kWidth, kHeight, 3u);
            const an::Result<an::StackAccumulationOutcome> accumulated =
                an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
            ASSERT_TRUE(accumulated.hasValue()) << accumulated.status().message();
        }
        OwnedTargets direct = OwnedTargets::forModel(model);
        const an::Result<an::StackFinalizeOutcome> expected =
            an::finalizeStackAccumulators(execution, model, owned.set(), direct.targets());
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        for (const an::StackSumDomain domain : {an::StackSumDomain::NormalizedToPeak, an::StackSumDomain::Raw})
        {
            OwnedTargets through = OwnedTargets::forModel(model);
            const an::Result<an::StackMethodFinalizeOutcome> outcome =
                an::finalizeStackMethod(execution, model, domain, owned.set(), through.targets());
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
            EXPECT_EQ(outcome->finalize, *expected) << an::stackCombinationName(combination);
            EXPECT_EQ(outcome->sumDomain, domain);
            EXPECT_EQ(outcome->outputDomain, an::NumericDomain::NormalizedLinear);
            EXPECT_FALSE(outcome->normalizedToPeak);
            EXPECT_EQ(outcome->peakSum, 0.0);
            EXPECT_EQ(through.bytes(), direct.bytes()) << an::stackCombinationName(combination);
        }
    }

    // A sample-history stack has no terms to finalize.
    const an::StackModel median = makeModel(requestOf(an::StackCombination::Median));
    OwnedAccumulators none;
    OwnedTargets targets = OwnedTargets::forModel(median);
    const an::Result<an::StackMethodFinalizeOutcome> refused =
        an::finalizeStackMethod(execution, median, an::kDefaultStackSumDomain, none.set(), targets.targets());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused.status(), "sample history")) << refused.status().message();
}

TEST(StackMethodsTest, RefusesAnEmptyExecutionAndAModelTheKernelsDoNotCombine)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ExecutionContext empty;
    const an::StackModel median = makeModel(requestOf(an::StackCombination::Median, 3u));
    const std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 1u);
    OwnedTargets targets = OwnedTargets::forModel(median);
    {
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(empty, median, history, targets);
        EXPECT_EQ(outcome.status().code(), an::ErrorCode::NotInitialized);
    }
    {
        OwnedAccumulators owned = OwnedAccumulators::forModel(makeModel(requestOf(an::StackCombination::Sum, 3u)));
        OwnedTargets sumTargets = OwnedTargets::forModel(makeModel(requestOf(an::StackCombination::Sum, 3u)));
        const an::Result<an::StackMethodFinalizeOutcome> outcome =
            an::finalizeStackMethod(empty, makeModel(requestOf(an::StackCombination::Sum, 3u)),
                                    an::kDefaultStackSumDomain, owned.set(), sumTargets.targets());
        EXPECT_EQ(outcome.status().code(), an::ErrorCode::NotInitialized);
    }

    // A streaming combination has no sample history to combine.
    {
        const an::StackModel average = makeModel(requestOf(an::StackCombination::Average, 3u));
        OwnedTargets averageTargets = OwnedTargets::forModel(average);
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, average, history, averageTargets);
        EXPECT_EQ(outcome.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(outcome.status(), an::ContextKey::Parameter), std::optional<std::string>{"model"});
        EXPECT_TRUE(messageNames(outcome.status(), "whole-output accumulators")) << outcome.status().message();
        EXPECT_FALSE(an::stackSampleHistoryCombinationOf(average).has_value());
    }

    // The ordinary sample-history entry delegates an unweighted rejection to its estimator.
    {
        an::StackRequest request = requestOf(an::StackCombination::Average, 3u);
        request.rejection.method = an::StackRejection::SigmaClipping;
        const an::StackModel clipped = makeModel(request);
        OwnedTargets clippedTargets = OwnedTargets::forModel(clipped);
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, clipped, history, clippedTargets);
        EXPECT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_FALSE(an::stackSampleHistoryCombinationOf(clipped).has_value());
    }
    EXPECT_EQ(an::stackSampleHistoryCombinationOf(median), an::StackSampleHistoryCombination::Median);
    EXPECT_EQ(
        an::stackSampleHistoryCombinationOf(makeModel(requestOf(an::StackCombination::AutoAdaptiveWeightedAverage))),
        an::StackSampleHistoryCombination::AutoAdaptiveWeightedAverage);
}

TEST(StackMethodsTest, RefusesASampleHistoryThatDoesNotDescribeTheModel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request =
        requestOf(an::StackCombination::Median, 3u,
                  makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedBand> good = makeHistory(3u, kWidth, kHeight, 3u);
    OwnedTargets targets = OwnedTargets::forModel(model);
    const std::vector<std::byte> untouched = targets.bytes();
    const OwnedScratch scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
    const an::Result<std::uint64_t> checkedTargets = an::checkStackFinalizeTargets(model, targets.targets());
    ASSERT_TRUE(checkedTargets.hasValue()) << checkedTargets.status().message();
    EXPECT_EQ(*checkedTargets, static_cast<std::uint64_t>(kWidth) * kHeight * 3u);
    for (const OwnedBand& owned : good)
    {
        EXPECT_TRUE(an::checkStackFrameBand(model, owned.band()).ok());
    }

    const auto expectRefusal = [&](const std::vector<an::StackFrameBand>& bands,
                                   std::span<const std::span<std::byte>> workerScratch,
                                   const an::StackFinalizeTargets& where, an::ErrorCode code, const char* parameter) {
        const an::Result<an::StackSampleHistoryOutcome> outcome =
            an::combineStackSampleHistory(execution, model, bands, workerScratch, where);
        ASSERT_FALSE(outcome.hasValue()) << parameter;
        EXPECT_EQ(outcome.status().code(), code) << parameter << ": " << outcome.status().message();
        EXPECT_EQ(textField(outcome.status(), an::ContextKey::Parameter), std::optional<std::string>{parameter})
            << outcome.status().message();
        EXPECT_TRUE(messageNames(outcome.status(), parameter)) << outcome.status().message();
    };

    // Too few frames, with the counts.
    {
        std::vector<an::StackFrameBand> bands = bandsOf(good);
        bands.pop_back();
        expectRefusal(bands, scratch.view(), targets.targets(), an::ErrorCode::IncompatibleArguments, "frames");
        const an::Result<an::StackSampleHistoryOutcome> outcome =
            an::combineStackSampleHistory(execution, model, bands, scratch.view(), targets.targets());
        EXPECT_EQ(countField(outcome.status(), an::ContextKey::Requested), std::optional<std::uint64_t>{2u});
        EXPECT_EQ(countField(outcome.status(), an::ContextKey::Limit), std::optional<std::uint64_t>{3u});
    }
    // A frame out of its place.
    {
        std::vector<an::StackFrameBand> bands = bandsOf(good);
        bands[1].frameIndex = 0u;
        expectRefusal(bands, scratch.view(), targets.targets(), an::ErrorCode::InvalidArgument, "frames.frameIndex");
    }
    // A band that lands inside the output but not where the others do.
    {
        std::vector<OwnedBand> history;
        for (std::uint32_t frame = 0u; frame < 3u; ++frame)
        {
            history.push_back(makeBand(frame, kWidth, 2u, 3u, frame == 2u ? 2u : 0u));
        }
        expectRefusal(bandsOf(history), scratch.view(), targets.targets(), an::ErrorCode::IncompatibleArguments,
                      "frames.firstOutputRow");
    }
    // A band of another shape.
    {
        std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 3u);
        history[2] = makeBand(2u, kWidth, kHeight - 2u, 3u);
        expectRefusal(bandsOf(history), scratch.view(), targets.targets(), an::ErrorCode::IncompatibleArguments,
                      "frames.layout");
    }
    // A band at another width than the plan's tile, refused by the band check.
    {
        std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 3u);
        history[0].float64.assign(history[0].float32.begin(), history[0].float32.end());
        history[0].float32.clear();
        history[0].layout = planarLayout(an::SampleType::Float64, kWidth, kHeight, 3u);
        expectRefusal(bandsOf(history), scratch.view(), targets.targets(), an::ErrorCode::IncompatibleArguments,
                      "band.sampleType");
    }
    // A mask under a rule that reads none.
    {
        std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 3u);
        history[1].addMask(kWidth, kHeight, 1u);
        expectRefusal(bandsOf(history), scratch.view(), targets.targets(), an::ErrorCode::InvalidArgument,
                      "band.validity.mask");
    }
    // Local weights for a stack that applies none.
    {
        std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 3u);
        history[1].localWeights.assign(static_cast<std::size_t>(kWidth) * kHeight, 1.0f);
        expectRefusal(bandsOf(history), scratch.view(), targets.targets(), an::ErrorCode::IncompatibleArguments,
                      "band.localWeights");
    }
    // Fewer scratch slices than workers, a short slice and a misaligned one.
    {
        const OwnedScratch few = OwnedScratch::forModel(model, execution.admittedConcurrency() - 1u);
        expectRefusal(bandsOf(good), few.view(), targets.targets(), an::ErrorCode::IncompatibleArguments,
                      "workerScratch");
        const OwnedScratch shortSlices =
            OwnedScratch::forModel(model, execution.admittedConcurrency(), an::stackScratchBytesPerWorker(model) - 8u);
        expectRefusal(bandsOf(good), shortSlices.view(), targets.targets(), an::ErrorCode::IncompatibleArguments,
                      "workerScratch");
        const OwnedScratch misaligned = OwnedScratch::forModel(model, execution.admittedConcurrency(), 0u, 8u);
        expectRefusal(bandsOf(good), misaligned.view(), targets.targets(), an::ErrorCode::InvalidArgument,
                      "workerScratch");
    }
    // Targets that do not describe the output.
    {
        an::StackFinalizeTargets wrongType = targets.targets();
        wrongType.outputSampleType = an::SampleType::Float64;
        expectRefusal(bandsOf(good), scratch.view(), wrongType, an::ErrorCode::IncompatibleArguments,
                      "outputSampleType");
        std::vector<float> variance(static_cast<std::size_t>(kWidth) * kHeight * 3u, 0.0f);
        an::StackFinalizeTargets unrequested = targets.targets();
        unrequested.variance = variance;
        expectRefusal(bandsOf(good), scratch.view(), unrequested, an::ErrorCode::IncompatibleArguments, "variance");
        an::StackFinalizeTargets missing = targets.targets();
        missing.coverage = {};
        expectRefusal(bandsOf(good), scratch.view(), missing, an::ErrorCode::IncompatibleArguments, "coverage");
    }
    EXPECT_EQ(targets.bytes(), untouched);
}

TEST(StackMethodsTest, CombinesTheMedianOfEveryColumn)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const std::uint32_t frames : {kFrames, 8u, 4u, 1u})
    {
        an::StackRequest request =
            requestOf(an::StackCombination::Median, frames,
                      makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
        request.output.coverageMap = true;
        request.output.varianceMap = true;
        request.output.signalToNoiseGainMap = true;
        request.output.weightSumMap = true;
        const an::StackModel model = makeModel(request);
        const std::vector<OwnedBand> history = makeHistory(frames, kWidth, kHeight, 3u);
        OwnedTargets targets = OwnedTargets::forModel(model);
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        constexpr std::size_t kElements = static_cast<std::size_t>(kWidth) * kHeight * 3u;
        EXPECT_EQ(outcome->elementsCombined, kElements);
        EXPECT_EQ(outcome->elementsWithSamples, kElements);
        EXPECT_EQ(outcome->elementsWithoutSamples, 0u);
        EXPECT_EQ(outcome->samplesAdmitted, kElements * frames);
        EXPECT_EQ(outcome->samplesExcludedByValidity, 0u);
        const std::vector<double> values = targets.values();
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            const std::vector<double> column = columnOf(history, element);
            EXPECT_EQ(values[element], static_cast<double>(static_cast<float>(medianOracle(column))))
                << frames << " frames, element " << element;
            EXPECT_EQ(targets.coverage[element], frames);
            EXPECT_EQ(targets.variance[element], static_cast<float>(varianceOracle(column))) << element;
            EXPECT_EQ(targets.gain[element], static_cast<float>(frames));
            EXPECT_EQ(targets.weightSum[element], static_cast<float>(frames));
        }
        EXPECT_EQ(targets.mask->countValid(), kElements);
    }

    // An even column takes the mean of its two middle samples exactly.
    {
        const an::StackModel model = makeModel(requestOf(an::StackCombination::Median, 4u));
        std::vector<OwnedBand> history = makeHistory(4u, kWidth, kHeight, 1u);
        const std::array<float, 4u> samples{0.75f, 0.125f, 0.5f, 0.25f};
        for (std::uint32_t frame = 0u; frame < 4u; ++frame)
        {
            history[frame].float32[0] = samples[frame];
        }
        OwnedTargets targets = OwnedTargets::forModel(model);
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(targets.values()[0], 0.375);
    }
}

TEST(StackMethodsTest, AppliesEveryValidityRuleToTheColumns)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = requestOf(an::StackCombination::Median, 4u);
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    constexpr std::size_t kElements = static_cast<std::size_t>(kWidth) * kHeight;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();

    // Each frame states its own rule, and the column of an element is what every rule admitted.
    std::vector<OwnedBand> history = makeHistory(4u, kWidth, kHeight, 1u);
    history[0].descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
    history[0].set(0u, 0.0);
    history[0].set(5u, -0.0);
    history[1].descriptor = ruleOf(an::ValidityRule::FiniteOnly);
    history[1].set(1u, nan);
    history[1].set(2u, infinity);
    history[1].set(3u, -infinity);
    history[2].descriptor = ruleOf(an::ValidityRule::ExplicitMask);
    history[2].addMask(kWidth, kHeight, 1u);
    history[2].clearBit(0u, 4u, 0u);
    history[2].clearBit(0u, 0u, 1u);
    history[3].descriptor = ruleOf(an::ValidityRule::DomainRange, 0.1, 0.45);
    history[3].set(6u, 0.05);
    history[3].set(7u, 0.9);
    // One element that no frame admits.
    history[0].set(9u, 0.0);
    history[1].set(9u, nan);
    history[2].clearBit(0u, 1u, 1u);
    history[3].set(9u, 2.0);

    OwnedTargets targets = OwnedTargets::forModel(model);
    const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    const std::vector<double> values = targets.values();
    std::uint64_t admitted = 0u;
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        std::vector<double> column;
        const std::uint32_t x = static_cast<std::uint32_t>(element % kWidth);
        const std::uint32_t y = static_cast<std::uint32_t>(element / kWidth);
        if (history[0].at(element) != 0.0)
        {
            column.push_back(history[0].at(element));
        }
        if (std::isfinite(history[1].at(element)))
        {
            column.push_back(history[1].at(element));
        }
        if (history[2].mask->test(0u, x, y) == std::optional<bool>{true})
        {
            column.push_back(history[2].at(element));
        }
        if (history[3].at(element) >= 0.1 && history[3].at(element) <= 0.45)
        {
            column.push_back(history[3].at(element));
        }
        admitted += column.size();
        EXPECT_EQ(values[element], static_cast<double>(static_cast<float>(medianOracle(column)))) << element;
        EXPECT_EQ(targets.coverage[element], column.size()) << element;
        EXPECT_EQ(targets.mask->test(0u, x, y), std::optional<bool>{!column.empty()}) << element;
    }
    EXPECT_EQ(outcome->samplesAdmitted, admitted);
    EXPECT_EQ(outcome->samplesExcludedByValidity, kElements * 4u - admitted);
    EXPECT_EQ(outcome->elementsWithoutSamples, 1u);
    EXPECT_EQ(outcome->elementsWithSamples, kElements - 1u);
    EXPECT_EQ(values[9], 0.0);
    EXPECT_EQ(targets.coverage[9], 0u);

    // A per-channel mask and a shared mask both select.
    {
        an::StackRequest rgb =
            requestOf(an::StackCombination::Median, 3u,
                      makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
        rgb.output.coverageMap = true;
        const an::StackModel rgbModel = makeModel(rgb);
        std::vector<OwnedBand> masked = makeHistory(3u, kWidth, kHeight, 3u);
        masked[0].descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        masked[0].addMask(kWidth, kHeight, 3u);
        masked[0].clearBit(2u, 3u, 3u);
        masked[1].descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        masked[1].addMask(kWidth, kHeight, 1u);
        masked[1].clearBit(0u, 3u, 3u);
        OwnedTargets rgbTargets = OwnedTargets::forModel(rgbModel);
        const an::Result<an::StackSampleHistoryOutcome> rgbOutcome = combine(execution, rgbModel, masked, rgbTargets);
        ASSERT_TRUE(rgbOutcome.hasValue()) << rgbOutcome.status().message();
        const std::size_t pixel = static_cast<std::size_t>(3u) * kWidth + 3u;
        EXPECT_EQ(rgbTargets.coverage[pixel], 2u);
        EXPECT_EQ(rgbTargets.coverage[kElements + pixel], 2u);
        EXPECT_EQ(rgbTargets.coverage[2u * kElements + pixel], 1u);
        EXPECT_EQ(rgbOutcome->samplesExcludedByValidity, 4u);
    }

    // A sample the rule admits and that is not a number is a frame's broken claim, not a value.
    {
        std::vector<OwnedBand> broken = makeHistory(4u, kWidth, kHeight, 1u);
        broken[2].set(7u, nan);
        OwnedTargets brokenTargets = OwnedTargets::forModel(model);
        const an::Result<an::StackSampleHistoryOutcome> refused = combine(execution, model, broken, brokenTargets);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
        EXPECT_TRUE(messageNames(refused.status(), "finite")) << refused.status().message();
    }
}

TEST(StackMethodsTest, ColumnsRefuseANaNTheZeroRuleAdmitsOnEveryBackend)
{
    // Zero-is-no-data admits every sample but an exact zero, so a NaN under it is an admitted
    // sample that is not finite: the reference row refuses the history, and so must every compiled
    // column row - the general one, and the eight-frame Float32 median, which hands a vector holding
    // a non-finite sample to the general one - rather than leave the NaN out as if it were a zero.
    // The NaN sits in the first vector of lanes on every instruction set, beside a zero and a
    // negative zero the rule does exclude, so the vector body judges it rather than the scalar tail.
    constexpr std::uint32_t kRowWidth = 37u;
    constexpr std::uint32_t kRows = 2u;
    const std::vector<an::InstructionSet> targets = compiledTargets();
    // Every execution below is held at once, each lent two threads.
    const an::EngineContext engine = makeEngine(2u * static_cast<std::uint32_t>(2u + targets.size()));
    an::ExecutionOptions automatic;
    automatic.maxConcurrency = 2u;
    automatic.backend = an::ComputeBackend::Automatic;
    std::vector<an::ExecutionContext> executions;
    executions.push_back(forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar));
    an::Result<an::ExecutionContext> automaticExecution = an::ExecutionContext::create(engine, automatic);
    ASSERT_TRUE(automaticExecution.hasValue()) << automaticExecution.status().message();
    executions.push_back(std::move(*automaticExecution));
    for (const an::InstructionSet target : targets)
        executions.push_back(forcedExecution(engine, an::ComputeBackend::Cpu, target));

    struct Shape
    {
        an::StackCombination combination;
        std::uint32_t frames;
        bool wide;
    };
    constexpr std::array<Shape, 4u> kShapes{Shape{an::StackCombination::Median, 3u, false},
                                            Shape{an::StackCombination::Median, 8u, false},
                                            Shape{an::StackCombination::Median, 3u, true},
                                            Shape{an::StackCombination::AutoAdaptiveWeightedAverage, 4u, false}};
    for (const Shape& shape : kShapes)
    {
        const an::SampleType type = shape.wide ? an::SampleType::Float64 : an::SampleType::Float32;
        const an::StackModel model =
            makeModel(requestOf(shape.combination, shape.frames, makeDescriptor(type, kRowWidth, kRows)));
        std::vector<OwnedBand> history = makeHistory(shape.frames, kRowWidth, kRows, 1u);
        for (OwnedBand& band : history)
        {
            band.descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
            if (shape.wide)
            {
                band.float64.assign(band.float32.begin(), band.float32.end());
                band.float32.clear();
                band.layout = planarLayout(an::SampleType::Float64, kRowWidth, kRows, 1u);
            }
        }
        history[0].set(2u, 0.0);
        history[2].set(3u, -0.0);

        for (std::size_t index = 0u; index < executions.size(); ++index)
        {
            const an::ExecutionContext& execution = executions[index];
            SCOPED_TRACE(std::string(an::stackCombinationName(shape.combination)) + " of " +
                         std::to_string(shape.frames) + (shape.wide ? " Float64" : " Float32") +
                         " frames on execution " + std::to_string(index));
            // Without the NaN the history combines, and a forced compiled table runs its compiled row.
            OwnedTargets clean = OwnedTargets::forModel(model);
            const an::Result<an::StackSampleHistoryOutcome> combined = combine(execution, model, history, clean);
            ASSERT_TRUE(combined.hasValue()) << combined.status().message();
            EXPECT_EQ(combined->samplesExcludedByValidity, 2u);
            if (index >= 2u)
                EXPECT_TRUE(combined->compiledRowUsed);
        }

        history[1].set(1u, std::numeric_limits<double>::quiet_NaN());
        for (std::size_t index = 0u; index < executions.size(); ++index)
        {
            const an::ExecutionContext& execution = executions[index];
            SCOPED_TRACE(std::string(an::stackCombinationName(shape.combination)) + " of " +
                         std::to_string(shape.frames) + (shape.wide ? " Float64" : " Float32") +
                         " frames on execution " + std::to_string(index));
            OwnedTargets targets = OwnedTargets::forModel(model);
            const an::Result<an::StackSampleHistoryOutcome> refused = combine(execution, model, history, targets);
            ASSERT_FALSE(refused.hasValue()) << "the NaN was left out instead of refused";
            EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
            EXPECT_TRUE(messageNames(refused.status(), "finite")) << refused.status().message();
        }
    }
}

TEST(StackMethodsTest, CombinesTheAutoAdaptiveMeanAgainstTheAdoptedAlgorithm)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kMany = 7u;
    an::StackRequest request =
        requestOf(an::StackCombination::AutoAdaptiveWeightedAverage, kMany,
                  makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.output.coverageMap = true;
    request.output.varianceMap = true;
    const an::StackModel model = makeModel(request);
    constexpr std::size_t kElements = static_cast<std::size_t>(kWidth) * kHeight * 3u;

    // Six frames around a level and one far above it: the outlier is down-weighted.
    Lcg random(0x7A11u);
    std::vector<OwnedBand> history = makeHistory(kMany, kWidth, kHeight, 3u);
    for (std::uint32_t frame = 0u; frame < kMany; ++frame)
    {
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            const double level = 0.2 + 0.001 * static_cast<double>(element % 97u);
            history[frame].set(element, frame == 3u ? level + 0.5 : level + 0.01 * (random.unit() - 0.5));
        }
    }
    OwnedTargets targets = OwnedTargets::forModel(model);
    const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_EQ(outcome->elementsWithSamples, kElements);
    EXPECT_EQ(outcome->samplesAdmitted, kElements * kMany);
    const std::vector<double> values = targets.values();
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        const std::vector<double> column = columnOf(history, element);
        const double expected = autoAdaptiveOracle(column);
        EXPECT_EQ(values[element], static_cast<double>(static_cast<float>(expected))) << element;
        double plain = 0.0;
        double bulk = 0.0;
        for (std::size_t frame = 0u; frame < column.size(); ++frame)
        {
            plain += column[frame] / static_cast<double>(column.size());
            bulk += frame == 3u ? 0.0 : column[frame] / static_cast<double>(column.size() - 1u);
        }
        EXPECT_LT(std::fabs(values[element] - bulk), std::fabs(plain - bulk)) << element;
        EXPECT_EQ(targets.coverage[element], kMany);
        EXPECT_EQ(targets.variance[element], static_cast<float>(varianceOracle(column))) << element;
    }

    // Fewer than three samples take the plain mean; a column whose spread is below the small-sigma
    // escape takes the plain mean; a column whose largest sample is not positive still combines to
    // its robust mean; and a sample above one is scaled like every other.
    {
        an::StackRequest small = requestOf(an::StackCombination::AutoAdaptiveWeightedAverage, 2u);
        const an::StackModel two = makeModel(small);
        std::vector<OwnedBand> pair = makeHistory(2u, kWidth, kHeight, 1u);
        pair[0].set(0u, 0.25);
        pair[1].set(0u, 0.75);
        OwnedTargets pairTargets = OwnedTargets::forModel(two);
        ASSERT_TRUE(combine(execution, two, pair, pairTargets).hasValue());
        EXPECT_EQ(pairTargets.values()[0], static_cast<double>(static_cast<float>(autoAdaptiveOracle({0.25, 0.75}))));
        EXPECT_EQ(pairTargets.values()[0], 0.5);
    }
    {
        const an::StackModel five = makeModel(requestOf(an::StackCombination::AutoAdaptiveWeightedAverage, 5u));
        std::vector<OwnedBand> shapes = makeHistory(5u, kWidth, kHeight, 1u);
        const std::array<double, 5u> flat{0.5, 0.5, 0.5, 0.5, 0.5};
        const std::array<double, 5u> negative{-0.02, -0.01, -0.03, -0.015, -0.4};
        const std::array<double, 5u> bright{1.5, 1.4, 1.6, 1.45, 3.0};
        for (std::uint32_t frame = 0u; frame < 5u; ++frame)
        {
            shapes[frame].set(0u, flat[frame]);
            shapes[frame].set(1u, negative[frame]);
            shapes[frame].set(2u, bright[frame]);
        }
        OwnedTargets shapeTargets = OwnedTargets::forModel(five);
        ASSERT_TRUE(combine(execution, five, shapes, shapeTargets).hasValue());
        const std::vector<double> combined = shapeTargets.values();
        EXPECT_EQ(combined[0], 0.5);
        const std::vector<double> negativeColumn(negative.begin(), negative.end());
        EXPECT_EQ(combined[1], static_cast<double>(static_cast<float>(autoAdaptiveOracle(negativeColumn))));
        EXPECT_LT(combined[1], 0.0);
        const double negativePlain = (-0.02 - 0.01 - 0.03 - 0.015 - 0.4) / 5.0;
        const double negativeBulk = (-0.02 - 0.01 - 0.03 - 0.015) / 4.0;
        EXPECT_LT(std::fabs(combined[1] - negativeBulk), std::fabs(negativePlain - negativeBulk));
        const std::vector<double> brightColumn(bright.begin(), bright.end());
        EXPECT_EQ(combined[2], static_cast<double>(static_cast<float>(autoAdaptiveOracle(brightColumn))));
        EXPECT_LT(combined[2], 1.7);
    }
}

TEST(StackMethodsTest, RefusesAResultThatLeavesTheFiniteRange)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf(an::StackCombination::AutoAdaptiveWeightedAverage, 3u,
                                                     makeDescriptor(an::SampleType::Float64, kWidth, kHeight)));
    std::vector<OwnedBand> history;
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        OwnedBand band;
        band.frameIndex = frame;
        band.layout = planarLayout(an::SampleType::Float64, kWidth, kHeight, 1u);
        band.float64.assign(static_cast<std::size_t>(kWidth) * kHeight, 0.5);
        band.float64[3] = 1.0e308 * (frame == 1u ? -1.0 : 1.0);
        history.push_back(std::move(band));
    }
    OwnedTargets targets = OwnedTargets::forModel(model);
    const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
    EXPECT_EQ(outcome.status().code(), an::ErrorCode::InvalidData);
    EXPECT_TRUE(messageNames(outcome.status(), "finite")) << outcome.status().message();
}

TEST(StackMethodsTest, PlacesRowBandsAtTheirOutputRowsAndReadsInterleavedBands)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request =
        requestOf(an::StackCombination::Median, 3u,
                  makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);

    OwnedTargets whole = OwnedTargets::forModel(model);
    const std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 3u);
    ASSERT_TRUE(combine(execution, model, history, whole).hasValue());

    // Two bands of two rows each, placed at their output rows, produce the whole output.
    OwnedTargets banded = OwnedTargets::forModel(model);
    for (const std::uint32_t firstRow : {0u, 2u})
    {
        std::vector<OwnedBand> band;
        for (std::uint32_t frame = 0u; frame < 3u; ++frame)
        {
            band.push_back(makeBand(frame, kWidth, 2u, 3u, firstRow));
        }
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, band, banded);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->elementsCombined, static_cast<std::uint64_t>(kWidth) * 2u * 3u);
    }
    EXPECT_EQ(banded.bytes(), whole.bytes());

    // Interleaved bands read through their layout, on the scalar row, to the same bytes.
    std::vector<OwnedBand> interleaved;
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        OwnedBand owned;
        owned.frameIndex = frame;
        owned.layout = interleavedLayout(an::SampleType::Float32, kWidth, kHeight, 3u);
        owned.float32.resize(static_cast<std::size_t>(kWidth) * kHeight * 3u);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    owned.float32[(static_cast<std::size_t>(y) * kWidth + x) * 3u + channel] =
                        sampleAt(frame, channel, y, x, kWidth);
                }
            }
        }
        interleaved.push_back(std::move(owned));
    }
    OwnedTargets mixed = OwnedTargets::forModel(model);
    const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, interleaved, mixed);
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_FALSE(outcome->compiledRowUsed);
    EXPECT_EQ(mixed.bytes(), whole.bytes());
}

TEST(StackMethodsTest, CombinesTheSameBitsAtEveryThreadCount)
{
    constexpr std::uint32_t kWide = 257u;
    constexpr std::uint32_t kTall = 33u;
    constexpr std::uint32_t kMany = 6u;
    for (const an::StackCombination combination :
         {an::StackCombination::Median, an::StackCombination::AutoAdaptiveWeightedAverage})
    {
        an::StackRequest request = requestOf(
            combination, kMany, makeDescriptor(an::SampleType::Float32, kWide, kTall, an::ChannelSchema::Rgb));
        request.output.coverageMap = true;
        request.output.varianceMap = true;
        const an::StackModel model = makeModel(request);
        Lcg random(0xC0FFEEu);
        std::vector<OwnedBand> history = makeHistory(kMany, kWide, kTall, 3u);
        for (OwnedBand& owned : history)
        {
            owned.descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
            for (float& value : owned.float32)
            {
                value = random.below(13u) == 0u ? 0.0f : static_cast<float>(random.unit());
            }
        }
        std::optional<std::vector<std::byte>> first;
        for (const std::uint32_t workers : {1u, 2u, 4u})
        {
            const an::EngineContext engine = makeEngine(4u);
            const an::ExecutionContext execution = makeExecution(engine, workers);
            OwnedTargets targets = OwnedTargets::forModel(model);
            const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
            std::vector<std::byte> bytes = targets.bytes();
            if (!first.has_value())
            {
                first = std::move(bytes);
                continue;
            }
            EXPECT_EQ(bytes, *first) << an::stackCombinationName(combination) << " at " << workers << " workers";
        }
    }
}

TEST(StackMethodsTest, CompiledRowsStoreTheReferenceBits)
{
    const std::vector<an::InstructionSet> targets = compiledTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this build and host have no compiled column row to compare with the reference";
    }
    constexpr std::array<std::uint32_t, 18u> kWidths{1u,  2u,  3u,  4u,  5u,  7u,  8u,  9u,  15u,
                                                     16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 257u};
    constexpr std::array<an::ValidityRule, 5u> kRules{an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly,
                                                      an::ValidityRule::ZeroIsNoData, an::ValidityRule::ExplicitMask,
                                                      an::ValidityRule::DomainRange};
    const an::EngineContext engine = makeEngine(4u);
    Lcg random(0x5EED5u);
    std::uint64_t compared = 0u;
    std::uint64_t refusedAlike = 0u;

    for (std::uint32_t caseIndex = 0u; caseIndex < 96u; ++caseIndex)
    {
        const std::uint32_t width = kWidths[random.below(static_cast<std::uint32_t>(kWidths.size()))];
        const std::uint32_t rows = 1u + random.below(3u);
        const std::uint32_t channels = random.below(2u) == 0u ? 1u : 3u;
        const std::uint32_t frames = random.below(5u) == 0u ? 17u + random.below(30u) : 1u + random.below(12u);
        const bool wide = random.below(4u) == 0u;
        const bool wideOutput = random.below(3u) == 0u;
        const bool adaptive = random.below(2u) == 0u;
        const bool specials = random.below(6u) == 0u;

        an::StackRequest request = requestOf(
            adaptive ? an::StackCombination::AutoAdaptiveWeightedAverage : an::StackCombination::Median, frames,
            makeDescriptor(wide ? an::SampleType::Float64 : an::SampleType::Float32, width, rows,
                           channels == 3u ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono));
        request.output.sampleType = wideOutput ? an::SampleType::Float64 : an::SampleType::Float32;
        request.output.coverageMap = random.below(2u) == 0u;
        request.output.varianceMap = random.below(2u) == 0u;
        request.output.signalToNoiseGainMap = random.below(3u) == 0u;
        request.output.weightSumMap = random.below(3u) == 0u;
        const an::StackModel model = makeModel(request);
        const std::uint64_t pixels = static_cast<std::uint64_t>(width) * rows;

        std::vector<OwnedBand> history;
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            const an::ValidityRule rule = kRules[random.below(static_cast<std::uint32_t>(kRules.size()))];
            OwnedBand band;
            band.frameIndex = frame;
            band.descriptor = ruleOf(rule, 0.1, 0.9);
            band.layout = planarLayout(wide ? an::SampleType::Float64 : an::SampleType::Float32, width, rows, channels);
            const std::size_t count = static_cast<std::size_t>(pixels) * channels;
            const bool frameSpecials =
                rule == an::ValidityRule::FiniteOnly || (specials && rule == an::ValidityRule::AllValid);
            const auto sample = [&random, frameSpecials]() -> double {
                const std::uint32_t pick = random.below(24u);
                if (pick == 0u)
                {
                    return 0.0;
                }
                if (frameSpecials && pick == 1u)
                {
                    return std::numeric_limits<double>::quiet_NaN();
                }
                if (frameSpecials && pick == 2u)
                {
                    return std::numeric_limits<double>::infinity();
                }
                if (pick == 3u)
                {
                    return -0.0;
                }
                return random.unit() * 1.5 - 0.25;
            };
            if (wide)
            {
                band.float64.resize(count);
                for (double& value : band.float64)
                {
                    value = sample();
                }
            }
            else
            {
                band.float32.resize(count);
                for (float& value : band.float32)
                {
                    value = static_cast<float>(sample());
                }
            }
            if (rule == an::ValidityRule::ExplicitMask)
            {
                band.addMask(width, rows, random.below(2u) == 0u ? 1u : channels);
                for (std::uint8_t& byte : band.maskBits)
                {
                    byte = static_cast<std::uint8_t>(random.below(256u));
                }
            }
            history.push_back(std::move(band));
        }
        const std::vector<an::StackFrameBand> bands = bandsOf(history);

        const auto runOn = [&](an::ComputeBackend backend, an::InstructionSet set, OwnedTargets& where, bool& refused,
                               an::ErrorCode& code, an::StackSampleHistoryOutcome& outcome) {
            const an::ExecutionContext execution = forcedExecution(engine, backend, set, 1u + random.below(3u));
            const OwnedScratch scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
            refused = false;
            code = an::ErrorCode::Ok;
            const an::Result<an::StackSampleHistoryOutcome> result =
                an::combineStackSampleHistory(execution, model, bands, scratch.view(), where.targets());
            if (!result.hasValue())
            {
                refused = true;
                code = result.status().code();
                return;
            }
            EXPECT_EQ(result->backend.backend, backend);
            EXPECT_EQ(result->backend.instructionSet, set);
            outcome = *result;
        };

        OwnedTargets reference = OwnedTargets::forModel(model);
        bool referenceRefused = false;
        an::ErrorCode referenceCode = an::ErrorCode::Ok;
        an::StackSampleHistoryOutcome referenceOutcome;
        runOn(an::ComputeBackend::Reference, an::InstructionSet::Scalar, reference, referenceRefused, referenceCode,
              referenceOutcome);
        EXPECT_FALSE(referenceOutcome.compiledRowUsed);
        const std::vector<std::byte> expected = reference.bytes();

        for (const an::InstructionSet target : targets)
        {
            OwnedTargets candidate = OwnedTargets::forModel(model);
            bool candidateRefused = false;
            an::ErrorCode candidateCode = an::ErrorCode::Ok;
            an::StackSampleHistoryOutcome candidateOutcome;
            runOn(an::ComputeBackend::Cpu, target, candidate, candidateRefused, candidateCode, candidateOutcome);
            EXPECT_EQ(candidateRefused, referenceRefused) << "case " << caseIndex;
            EXPECT_EQ(candidateCode, referenceCode) << "case " << caseIndex;
            if (referenceRefused)
            {
                ++refusedAlike;
                continue;
            }
            EXPECT_TRUE(candidateOutcome.compiledRowUsed) << "case " << caseIndex;
            EXPECT_EQ(candidateOutcome.samplesAdmitted, referenceOutcome.samplesAdmitted) << "case " << caseIndex;
            EXPECT_EQ(candidateOutcome.samplesExcludedByValidity, referenceOutcome.samplesExcludedByValidity)
                << "case " << caseIndex;
            EXPECT_EQ(candidateOutcome.elementsWithSamples, referenceOutcome.elementsWithSamples)
                << "case " << caseIndex;
            EXPECT_EQ(candidate.bytes(), expected)
                << "case " << caseIndex << ": " << (adaptive ? "adaptive " : "median ") << width << 'x' << rows
                << " channels " << channels << " frames " << frames << " on " << an::instructionSetName(target);
            ++compared;
        }
    }
    std::cout << "compared " << compared << " compiled column runs with the reference, " << refusedAlike
              << " refused alike" << std::endl;
    EXPECT_GT(compared, 0u);
}

TEST(StackMethodsTest, EightFrameMedianCoversBinaryColumnsAndFloatingExtremes)
{
    const an::EngineContext engine = makeEngine(4u);
    constexpr std::uint32_t kColumns = 273u;
    const std::array<float, 8u> extremes{std::numeric_limits<float>::max(),
                                         -std::numeric_limits<float>::max(),
                                         std::numeric_limits<float>::denorm_min(),
                                         -std::numeric_limits<float>::denorm_min(),
                                         0.0f,
                                         -0.0f,
                                         1.0f,
                                         std::nextafter(1.0f, 2.0f)};
    for (const auto output : {an::SampleType::Float32, an::SampleType::Float64})
        for (const auto rule :
             {an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly, an::ValidityRule::ZeroIsNoData})
        {
            auto request =
                requestOf(an::StackCombination::Median, 8u, makeDescriptor(an::SampleType::Float32, kColumns, 1u));
            request.output.sampleType = output;
            request.output.coverageMap = true;
            request.output.signalToNoiseGainMap = true;
            request.output.weightSumMap = true;
            const auto model = makeModel(request);
            auto history = makeHistory(8u, kColumns, 1u, 1u);
            for (std::uint32_t frame = 0u; frame < 8u; ++frame)
            {
                history[frame].descriptor = ruleOf(rule);
                for (std::uint32_t column = 0u; column < kColumns; ++column)
                    history[frame].float32[column] =
                        column < 256u ? ((column & (1u << frame)) != 0u ? 1.0f : 0.0f)
                                      : extremes[(column - 256u + (column == 272u ? frame : 0u)) % extremes.size()];
            }
            if (rule == an::ValidityRule::FiniteOnly)
                history[0].float32[264u] = std::numeric_limits<float>::quiet_NaN();
            auto reference = OwnedTargets::forModel(model);
            const auto expected = [&] {
                const auto execution =
                    forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar, 4u);
                return combine(execution, model, history, reference);
            }();
            ASSERT_TRUE(expected.hasValue()) << expected.status().message();
            for (const auto target : compiledTargets())
            {
                SCOPED_TRACE(std::to_string(static_cast<int>(output)) + "/" + std::to_string(static_cast<int>(rule)) +
                             "/" + an::instructionSetName(target));
                const auto execution = forcedExecution(engine, an::ComputeBackend::Cpu, target, 4u);
                auto targets = OwnedTargets::forModel(model);
                const auto result = combine(execution, model, history, targets);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_TRUE(result->compiledRowUsed);
                const auto actualBytes = targets.bytes();
                const auto expectedBytes = reference.bytes();
                for (std::size_t byte = 0u; byte < actualBytes.size(); ++byte)
                    EXPECT_EQ(actualBytes[byte], expectedBytes[byte]) << "byte " << byte;
                EXPECT_EQ(targets.coverage, reference.coverage);
                EXPECT_EQ(targets.gain, reference.gain);
                EXPECT_EQ(targets.weightSum, reference.weightSum);
                EXPECT_EQ(result->samplesAdmitted, expected->samplesAdmitted);
                EXPECT_EQ(result->samplesExcludedByValidity, expected->samplesExcludedByValidity);
                EXPECT_EQ(result->elementsWithSamples, expected->elementsWithSamples);
            }
        }
}

TEST(StackMethodsTest, TakesNothingFromACancelledExecution)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source source;
    source.request_stop();
    const an::ExecutionContext execution =
        makeExecution(engine, an::kUseContextConcurrency, nullptr, source.get_token());
    const an::StackModel model = makeModel(requestOf(an::StackCombination::Median, 3u));
    const std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 1u);
    OwnedTargets targets = OwnedTargets::forModel(model);
    const std::vector<std::byte> before = targets.bytes();
    const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
    EXPECT_EQ(outcome.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(targets.bytes(), before);

    const an::StackModel sum = makeModel(requestOf(an::StackCombination::Sum, 3u));
    OwnedAccumulators owned = OwnedAccumulators::forModel(sum);
    OwnedTargets sumTargets = OwnedTargets::forModel(sum);
    const an::Result<an::StackMethodFinalizeOutcome> finalized =
        an::finalizeStackMethod(execution, sum, an::kDefaultStackSumDomain, owned.set(), sumTargets.targets());
    EXPECT_EQ(finalized.status().code(), an::ErrorCode::Cancelled);
}

TEST(StackMethodsTest, WritesTheFloat64OutputFromAFloat64Tile)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request =
        requestOf(an::StackCombination::Median, 4u, makeDescriptor(an::SampleType::Float64, kWidth, kHeight));
    request.output.sampleType = an::SampleType::Float64;
    request.output.varianceMap = true;
    const an::StackModel model = makeModel(request);
    std::vector<OwnedBand> history;
    Lcg random(0xD0B1Eu);
    for (std::uint32_t frame = 0u; frame < 4u; ++frame)
    {
        OwnedBand band;
        band.frameIndex = frame;
        band.layout = planarLayout(an::SampleType::Float64, kWidth, kHeight, 1u);
        band.float64.resize(static_cast<std::size_t>(kWidth) * kHeight);
        for (double& value : band.float64)
        {
            value = random.unit() + 1.0e-12 * random.unit();
        }
        history.push_back(std::move(band));
    }
    OwnedTargets targets = OwnedTargets::forModel(model);
    ASSERT_EQ(targets.outputType, an::SampleType::Float64);
    const an::Result<an::StackSampleHistoryOutcome> outcome = combine(execution, model, history, targets);
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    const std::vector<double> values = targets.values();
    for (std::size_t element = 0u; element < values.size(); ++element)
    {
        const std::vector<double> column = columnOf(history, element);
        EXPECT_EQ(values[element], medianOracle(column)) << element;
        EXPECT_EQ(targets.variance[element], static_cast<float>(varianceOracle(column))) << element;
    }
}

TEST(StackMethodsTest, RunsThroughTheWorkingSetEndToEnd)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request =
        requestOf(an::StackCombination::Median, kFrames,
                  makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.output.coverageMap = true;
    request.output.varianceMap = true;
    const an::StackModel model = makeModel(request);
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    ASSERT_EQ(plan->strategy.strategy, an::StackAccumulationStrategy::TileSampleHistory);
    ASSERT_EQ(plan->tile.rows, kHeight);
    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, *plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet set = std::move(*made);
    constexpr std::uint32_t kChannels = 3u;
    constexpr std::size_t kElements = static_cast<std::size_t>(kWidth) * kHeight * kChannels;
    constexpr std::size_t kBandBytes = kElements * sizeof(float);

    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Combine).ok());
    const std::span<std::byte> tile = set.tile();
    ASSERT_EQ(tile.size(), kBandBytes * kFrames);
    const std::vector<OwnedBand> history = makeHistory(kFrames, kWidth, kHeight, kChannels);
    std::vector<an::ImageLayout> layouts;
    std::vector<an::StackFrameBand> bands;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        std::memcpy(tile.data() + frame * kBandBytes, history[frame].float32.data(), kBandBytes);
        layouts.push_back(planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels));
    }
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        bands.push_back(an::StackFrameBand{.sampleType = an::SampleType::Float32,
                                           .samples = tile.data() + frame * kBandBytes,
                                           .layout = &layouts[frame],
                                           .frameIndex = frame,
                                           .firstOutputRow = 0u});
    }
    std::vector<std::span<std::byte>> scratch;
    for (std::uint32_t worker = 0u; worker < plan->memory.workers; ++worker)
    {
        scratch.push_back(set.workerScratch(worker));
        ASSERT_EQ(scratch.back().size(), plan->memory.scratchBytesPerWorker);
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> mask = set.outputMask();
    ASSERT_TRUE(mask.has_value());
    const an::Result<an::StackSampleHistoryOutcome> outcome = an::combineStackSampleHistory(
        execution, model, bands, scratch,
        an::StackFinalizeTargets{.outputSampleType = an::SampleType::Float32,
                                 .outputImage = set.outputImage(),
                                 .outputMask = &*mask,
                                 .coverage = set.outputCountMap(an::StackOutputMap::Coverage),
                                 .variance = set.outputValueMap(an::StackOutputMap::Variance)});
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_EQ(outcome->elementsWithSamples, kElements);
    set.endPass();
    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Finalize).ok());
    set.endPass();

    const std::span<const std::uint32_t> coverage = set.outputCountMap(an::StackOutputMap::Coverage);
    EXPECT_EQ(coverage[kElements - 1u], kFrames);
    an::Result<an::ImageStorageBytes> image = set.releaseOutputImage();
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    an::Result<an::ImageStorage<float>> typed = an::ImageStorage<float>::fromBytes(std::move(*image));
    ASSERT_TRUE(typed.hasValue()) << typed.status().message();
    const an::Result<an::Image<float>> result =
        an::Image<float>::fromStorage(std::move(*typed), model.outputDescriptor());
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    const float* const samples = result->storage().data();
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(samples[element], static_cast<float>(medianOracle(columnOf(history, element)))) << element;
    }
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        EXPECT_EQ(mask->test(channel, kWidth - 1u, kHeight - 1u), std::optional<bool>{true}) << channel;
    }
}

TEST(StackMethodsTest, ReportsTheBackendItRan)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(requestOf(an::StackCombination::Median, 3u));
    const std::vector<OwnedBand> history = makeHistory(3u, kWidth, kHeight, 1u);
    {
        OwnedTargets targets = OwnedTargets::forModel(model);
        const an::ExecutionContext reference =
            forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar);
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(reference, model, history, targets);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->backend.backend, an::ComputeBackend::Reference);
        EXPECT_EQ(outcome->backend.instructionSet, an::InstructionSet::Scalar);
        EXPECT_FALSE(outcome->compiledRowUsed);
    }
    {
        OwnedTargets targets = OwnedTargets::forModel(model);
        const an::ExecutionContext automatic = makeExecution(engine);
        const an::Result<an::BackendSelection> selected = an::selectBackend(automatic);
        ASSERT_TRUE(selected.hasValue());
        const an::Result<an::StackSampleHistoryOutcome> outcome = combine(automatic, model, history, targets);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->backend, *selected);
        EXPECT_EQ(outcome->compiledRowUsed, selected->backend != an::ComputeBackend::Reference &&
                                                selected->instructionSet != an::InstructionSet::Scalar);
    }
}

TEST(StackMethodsTest, TheScratchContractHoldsAVectorOfColumns)
{
    constexpr std::uint64_t kAlign = an::kStackScratchAlignmentBytes;
    for (const std::uint32_t frames : {1u, 5u, 33u, 200u})
    {
        const std::uint64_t column = an::alignedStackScratchTerm(frames * sizeof(double));
        const std::uint64_t lanes = an::alignedStackScratchTerm(an::kStackSampleColumnLanes * frames * sizeof(double));
        const std::uint64_t records = an::alignedStackScratchTerm(frames * an::kStackColumnFrameRecordBytes);
        EXPECT_EQ(an::stackScratchBytesPerWorker(makeModel(requestOf(an::StackCombination::Median, frames))),
                  column + lanes + records)
            << frames;
        EXPECT_EQ(an::stackScratchBytesPerWorker(
                      makeModel(requestOf(an::StackCombination::AutoAdaptiveWeightedAverage, frames))),
                  column + an::alignedStackScratchTerm(2u * an::kStackSampleColumnLanes * frames * sizeof(double)) +
                      records)
            << frames;
        EXPECT_EQ(an::stackScratchBytesPerWorker(makeModel(requestOf(an::StackCombination::Median, frames))) % kAlign,
                  0u);
    }
    // The rejection families keep their scalar terms.
    an::StackRequest clipped = requestOf(an::StackCombination::Average, 5u);
    clipped.rejection.method = an::StackRejection::SigmaClipping;
    EXPECT_EQ(an::stackScratchBytesPerWorker(makeModel(clipped)), 3u * kAlign + 5u * an::kStackRejectionRowRecordBytes);
}

TEST(StackRejectionTest, EveryFamilyRejectsAnExtremeAndReplacementKeepsItsCount)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (std::uint8_t mode = 1u; mode <= 7u; ++mode)
    {
        auto request = requestOf(an::StackCombination::Average, 8u);
        request.rejection.method = static_cast<an::StackRejection>(mode);
        request.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
        request.output.coverageMap = true;
        request.output.varianceMap = true;
        request.output.weightSumMap = true;
        request.output.signalToNoiseGainMap = true;
        const auto model = makeModel(request);
        auto history = makeHistory(8u, kWidth, kHeight, 1u);
        for (std::uint32_t frame = 0u; frame < 8u; ++frame)
            std::fill(history[frame].float32.begin(), history[frame].float32.end(), frame == 3u ? 100.0f : 1.0f);
        auto targets = OwnedTargets::forModel(model);
        auto scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
        const auto bands = bandsOf(history);
        const auto elements = targets.coverage.size();
        std::vector<std::uint32_t> low(elements, 99u), high(elements, 99u);
        const auto result =
            an::combineRejectedStackSampleHistory(execution, model, bands, scratch.view(), targets.targets(), {},
                                                  an::StackRejectionMaps{.low = low, .high = high});
        ASSERT_TRUE(result.hasValue()) << an::stackRejectionName(request.rejection.method) << result.status().message();
        const auto expectedCount = request.rejection.method == an::StackRejection::MedianSigmaReplacement ? 8u : 7u;
        for (std::size_t i = 0u; i < elements; ++i)
        {
            EXPECT_EQ(targets.values()[i], 1.0) << mode;
            EXPECT_EQ(targets.coverage[i], expectedCount);
            EXPECT_EQ(targets.weightSum[i], static_cast<float>(expectedCount));
            EXPECT_EQ(targets.gain[i], static_cast<float>(expectedCount));
            EXPECT_EQ(targets.variance[i], 0.0f);
            EXPECT_EQ(low[i], 0u);
            EXPECT_EQ(high[i], 1u);
        }
        EXPECT_EQ(result->mappedHigh, elements);
        EXPECT_EQ(result->mappedLow, 0u);
        EXPECT_EQ(result->scratchBytesPerWorker, an::stackScratchBytesPerWorker(model));
        EXPECT_FALSE(result->history.compiledRowUsed);
    }
}

TEST(StackRejectionTest, ValidityAndDegenerateColumnsAreExplicitForEveryFamily)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (std::uint8_t mode = 1u; mode <= 7u; ++mode)
    {
        auto request = requestOf(an::StackCombination::Average, 5u);
        request.rejection.method = static_cast<an::StackRejection>(mode);
        request.output.coverageMap = true;
        const auto model = makeModel(request);
        auto history = makeHistory(5u, kWidth, kHeight, 1u);
        for (auto& frame : history)
        {
            std::fill(frame.float32.begin(), frame.float32.end(), 0.0f);
            frame.descriptor = ruleOf(an::ValidityRule::FiniteOnly);
            frame.float32[1] = std::numeric_limits<float>::quiet_NaN();
            frame.float32[2] = std::numeric_limits<float>::infinity();
            frame.float32[3] = 0.5f;
        }
        history[0].float32[1] = 0.75f;
        auto targets = OwnedTargets::forModel(model);
        const auto result = combine(execution, model, history, targets);
        ASSERT_TRUE(result.hasValue()) << mode << result.status().message();
        const auto values = targets.values();
        EXPECT_EQ(values[0], 0.0);
        EXPECT_EQ(targets.coverage[0], 5u);
        EXPECT_EQ(values[1], 0.75);
        EXPECT_EQ(targets.coverage[1], 1u);
        EXPECT_EQ(values[2], 0.0);
        EXPECT_EQ(targets.coverage[2], 0u);
        EXPECT_EQ(values[3], 0.5);
        EXPECT_EQ(targets.coverage[3], 5u);
        EXPECT_EQ(targets.maskBits[0] & 15u, 11u);
        history[0].descriptor = {};
        const auto invalid = combine(execution, model, history, targets);
        EXPECT_EQ(invalid.status().code(), an::ErrorCode::InvalidData);
    }
}

TEST(StackRejectionTest, ReorderedSamplesKeepTheirFrameAndLocalWeights)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (const auto method : {an::StackRejection::LinearFitClipping, an::StackRejection::GeneralizedEsdTest,
                              an::StackRejection::MedianSigmaReplacement, an::StackRejection::PercentileClipping})
    {
        auto request = requestOf(an::StackCombination::Average, 8u);
        request.rejection.method = method;
        request.weighting.method = an::StackWeighting::Explicit;
        request.localWeighting.enabled = true;
        request.output.coverageMap = true;
        request.output.weightSumMap = true;
        const auto model = makeModel(request);
        auto history = makeHistory(8u, kWidth, kHeight, 1u);
        const std::array<float, 8> values{1.02f, 100.0f, 1.0f, 0.98f, 1.01f, 0.99f, 1.03f, 0.97f};
        const std::array<double, 8> weights{1.0, 500.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
        for (std::size_t frame = 0u; frame < history.size(); ++frame)
        {
            std::fill(history[frame].float32.begin(), history[frame].float32.end(), values[frame]);
            history[frame].localWeights.assign(history[frame].float32.size(), 0.5f);
        }
        auto targets = OwnedTargets::forModel(model);
        auto scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
        const auto result =
            an::combineRejectedStackSampleHistory(execution, model, bandsOf(history), scratch.view(), targets.targets(),
                                                  weights, {}, an::StackRejectionCombinationPolicy::SurvivorValues);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        // Removing families reject the bright frame; its dominant weight must disappear too.
        if (method != an::StackRejection::MedianSigmaReplacement)
        {
            double sum = 0.0, total = 0.0;
            for (std::size_t frame = 0u; frame < values.size(); ++frame)
                if (frame != 1u && !(method == an::StackRejection::LinearFitClipping && frame == 6u))
                {
                    sum += values[frame] * weights[frame] * 0.5;
                    total += weights[frame] * 0.5;
                }
            EXPECT_EQ(targets.values()[0], static_cast<float>(sum / total));
            EXPECT_EQ(targets.weightSum[0], static_cast<float>(total));
            EXPECT_EQ(targets.coverage[0], method == an::StackRejection::LinearFitClipping ? 6u : 7u);
        }
        else
        {
            EXPECT_EQ(targets.coverage[0], 8u);
            EXPECT_NEAR(targets.values()[0], 1.0, 0.02);
        }
    }
}

TEST(StackRejectionTest, RejectsMalformedMapsWeightsAndScratchBeforeWriting)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    auto request = requestOf(an::StackCombination::Average, 5u);
    request.rejection.method = an::StackRejection::SigmaClipping;
    request.output.rejectionMaps = an::RejectionMapForm::Merged;
    const auto model = makeModel(request);
    const auto history = makeHistory(5u, kWidth, kHeight, 1u);
    const auto bands = bandsOf(history);
    auto targets = OwnedTargets::forModel(model);
    const auto original = targets.bytes();
    auto scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
    std::vector<std::uint32_t> merged(kWidth * kHeight, 99u);
    const an::StackRejectionMaps maps{.merged = merged};
    auto result = an::combineRejectedStackSampleHistory(execution, model, bands, scratch.view(), targets.targets());
    EXPECT_EQ(result.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(targets.bytes(), original);
    const std::array<double, 1> weights{1.0};
    result = an::combineRejectedStackSampleHistory(execution, model, bands, scratch.view(), targets.targets(), weights,
                                                   maps);
    EXPECT_EQ(result.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(targets.bytes(), original);
    ASSERT_GT(scratch.spans.size(), 1u);
    scratch.spans[1] = scratch.spans[0];
    result =
        an::combineRejectedStackSampleHistory(execution, model, bands, scratch.view(), targets.targets(), {}, maps);
    EXPECT_EQ(result.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(targets.bytes(), original);
    scratch.spans[0] = scratch.spans[0].first(1u);
    result =
        an::combineRejectedStackSampleHistory(execution, model, bands, scratch.view(), targets.targets(), {}, maps);
    EXPECT_EQ(result.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(targets.bytes(), original);
}

TEST(StackRejectionTest, EveryFamilyAgreesAcrossThreadsAndFloat64Tiles)
{
    const auto engine = makeEngine(4u);
    for (std::uint8_t mode = 1u; mode <= 7u; ++mode)
    {
        for (const bool wide : {false, true})
        {
            const auto type = wide ? an::SampleType::Float64 : an::SampleType::Float32;
            auto request =
                requestOf(an::StackCombination::Average, 17u, makeDescriptor(type, 19u, 5u, an::ChannelSchema::Rgb));
            request.rejection.method = static_cast<an::StackRejection>(mode);
            request.output.sampleType = type;
            request.output.coverageMap = true;
            request.output.varianceMap = true;
            const auto model = makeModel(request);
            auto history = makeHistory(17u, 19u, 5u, 3u);
            if (wide)
                for (auto& frame : history)
                {
                    frame.float64.assign(frame.float32.begin(), frame.float32.end());
                    frame.float32.clear();
                    frame.layout = planarLayout(type, 19u, 5u, 3u);
                }
            std::vector<std::byte> expected;
            for (const auto width : {1u, 2u, 4u})
            {
                const auto execution = makeExecution(engine, width);
                auto targets = OwnedTargets::forModel(model);
                const auto result = combine(execution, model, history, targets);
                ASSERT_TRUE(result.hasValue()) << mode << result.status().message();
                if (expected.empty())
                    expected = targets.bytes();
                else
                    EXPECT_EQ(targets.bytes(), expected);
            }
        }
    }
}

TEST(StackRejectionTest, MatchesRecordedResultsAndFirstRejectionCounts)
{
    // Recorded reference results of the compiled rejection routines, including full-population rank
    // statistics, cumulative guards, histogram MAD and replacement first-event counters.
    const std::array<std::array<float, 12>, 5> samples{
        {{0.04f, 0.97f, 0.50f, 0.51f, 0.48f, 0.52f, 0.49f, 0.53f, 0.47f, 0.50f, 0.51f, 0.49f},
         {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 2.0f, 4.0f},
         {0.95f, 1.05f, 1.15f, 0.85f, 0.96f, 1.04f, 1.14f, 0.86f, 0.97f, 1.03f, 1.13f, 0.87f},
         {0.0f, 0.2f, 0.19f, 0.21f, 0.4f, 0.22f, 0.18f, 0.23f, 0.17f, 0.24f, 0.16f, 0.8f},
         {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}}};
    struct Golden
    {
        std::uint32_t fixture;
        std::uint8_t mode;
        double value;
        std::uint32_t count;
        std::uint64_t low, high, mappedLow, mappedHigh;
    };
    const Golden goldens[] = {{0u, 1u, 0.5, 8u, 2u, 2u, 2u, 2u},
                              {0u, 2u, 0.5, 6u, 3u, 3u, 3u, 3u},
                              {0u, 3u, 0.5, 12u, 5u, 5u, 5u, 5u},
                              {0u, 4u, 0.5, 8u, 2u, 2u, 2u, 2u},
                              {0u, 5u, 0.49375000596046448, 8u, 1u, 3u, 1u, 3u},
                              {0u, 6u, 0.5, 10u, 1u, 1u, 1u, 1u},
                              {0u, 7u, 0.5, 10u, 1u, 1u, 1u, 1u},
                              {1u, 1u, 0.55000001192092896, 10u, 0u, 2u, 0u, 2u},
                              {1u, 2u, 0.60000002384185791, 7u, 2u, 3u, 2u, 3u},
                              {1u, 3u, 0.61874997615814209, 12u, 7u, 8u, 6u, 6u},
                              {1u, 4u, 0.55000001192092896, 10u, 0u, 2u, 0u, 2u},
                              {1u, 5u, 0.40000000596046448, 7u, 1u, 4u, 1u, 4u},
                              {1u, 6u, 0.64999997615814209, 2u, 5u, 5u, 5u, 5u},
                              {1u, 7u, 0.55000001192092896, 10u, 0u, 2u, 0u, 2u},
                              {2u, 1u, 1, 12u, 0u, 0u, 0u, 0u},
                              {2u, 2u, 1, 6u, 3u, 3u, 3u, 3u},
                              {2u, 3u, 1, 12u, 0u, 0u, 0u, 0u},
                              {2u, 4u, 1, 12u, 0u, 0u, 0u, 0u},
                              {2u, 5u, 1, 8u, 2u, 2u, 2u, 2u},
                              {2u, 6u, 0.95333331823348999, 9u, 0u, 3u, 0u, 3u},
                              {2u, 7u, 1, 12u, 0u, 0u, 0u, 0u},
                              {3u, 1u, 0.20000000298023224, 9u, 1u, 2u, 1u, 2u},
                              {3u, 2u, 0.20000000298023224, 7u, 2u, 3u, 2u, 3u},
                              {3u, 3u, 0.20250000059604645, 12u, 6u, 7u, 6u, 6u},
                              {3u, 4u, 0.20000000298023224, 9u, 1u, 2u, 1u, 2u},
                              {3u, 5u, 0.19499999284744263, 8u, 2u, 2u, 2u, 2u},
                              {3u, 6u, 0.19499999284744263, 6u, 2u, 4u, 2u, 4u},
                              {3u, 7u, 0.20000000298023224, 9u, 1u, 2u, 1u, 2u},
                              {4u, 1u, 1, 12u, 0u, 0u, 0u, 0u},
                              {4u, 2u, 1, 12u, 0u, 0u, 0u, 0u},
                              {4u, 3u, 1, 12u, 0u, 0u, 0u, 0u},
                              {4u, 4u, 1, 12u, 0u, 0u, 0u, 0u},
                              {4u, 5u, 1, 12u, 0u, 0u, 0u, 0u},
                              {4u, 6u, 1, 12u, 0u, 0u, 0u, 0u},
                              {4u, 7u, 1, 12u, 0u, 0u, 0u, 0u}};
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (const auto& golden : goldens)
    {
        SCOPED_TRACE(std::to_string(golden.fixture) + ":" + std::to_string(golden.mode));
        auto request = requestOf(an::StackCombination::Average, 12u, makeDescriptor(an::SampleType::Float32, 1u, 1u));
        request.rejection.method = static_cast<an::StackRejection>(golden.mode);
        request.rejection.sigma = an::SigmaClipThresholds{.low = 1.5, .high = 1.5};
        request.output.coverageMap = true;
        request.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
        const auto model = makeModel(request);
        auto history = makeHistory(12u, 1u, 1u, 1u);
        for (std::size_t frame = 0u; frame < history.size(); ++frame)
            history[frame].float32[0] = samples[golden.fixture][frame];
        auto targets = OwnedTargets::forModel(model);
        auto scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
        std::array<std::uint32_t, 1> low{}, high{};
        const auto result =
            an::combineRejectedStackSampleHistory(execution, model, bandsOf(history), scratch.view(), targets.targets(),
                                                  {}, an::StackRejectionMaps{.low = low, .high = high});
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(targets.values()[0], golden.value);
        EXPECT_EQ(targets.coverage[0], golden.count);
        EXPECT_EQ(result->rejectedLow, golden.low);
        EXPECT_EQ(result->rejectedHigh, golden.high);
        EXPECT_EQ(low[0], golden.mappedLow);
        EXPECT_EQ(high[0], golden.mappedHigh);
    }
}

TEST(StackRejectionTest, GesdMomentDecisionsMatchReferenceAcrossDistributions)
{
    const auto engine = makeEngine(2u);
    const auto reference = forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar, 1u);
    const auto optimized = makeExecution(engine, 1u);
    Lcg random(0x675364u);
    for (std::uint32_t trial = 0u; trial < 192u; ++trial)
    {
        const auto count = 3u + random.below(254u);
        auto request = requestOf(an::StackCombination::Average, count);
        request.rejection.method = an::StackRejection::GeneralizedEsdTest;
        request.rejection.gesdt.maximumOutlierFraction = 0.05 + random.unit() * 0.9;
        request.rejection.gesdt.significance = 0.001 + random.unit() * 0.5;
        const auto model = makeModel(request);
        std::vector<float> source(count);
        std::vector<an::StackSampleDisposition> flags(count, an::StackSampleDisposition::Kept);
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            const double scale = trial % 4u == 0u ? 1e-6 : (trial % 4u == 1u ? 1e5 : 0.2);
            source[i] = static_cast<float>(0.5 + scale * (random.unit() - 0.5));
            if (i % 23u == 0u)
                source[i] += static_cast<float>(scale * 4.0);
            if (trial % 7u == 0u)
                source[i] = 0.5f;
            if (i % 31u == 0u)
                flags[i] = an::StackSampleDisposition::Missing;
        }
        auto refSamples = source;
        auto optSamples = source;
        auto refFlags = flags;
        auto optFlags = flags;
        std::vector<an::StackGesdCandidate> refCandidates(count);
        std::vector<an::StackGesdCandidate> optCandidates(count);
        auto scratch = OwnedScratch::forModel(model, 2u);
        const auto expected = an::evaluateStackGesdColumn(reference, model, std::span(refSamples), std::span(refFlags),
                                                          scratch.spans[0], std::span(refCandidates));
        const auto result = an::evaluateStackGesdColumn(optimized, model, std::span(optSamples), std::span(optFlags),
                                                        scratch.spans[1], std::span(optCandidates));
        ASSERT_TRUE(expected.hasValue()) << trial;
        ASSERT_TRUE(result.hasValue()) << trial;
        EXPECT_EQ(optSamples, refSamples) << trial;
        EXPECT_EQ(optFlags, refFlags) << trial;
        EXPECT_EQ(result->rejection.rejectedLow, expected->rejection.rejectedLow) << trial;
        EXPECT_EQ(result->rejection.rejectedHigh, expected->rejection.rejectedHigh) << trial;
        EXPECT_EQ(result->rejection.surviving, expected->rejection.surviving) << trial;
        ASSERT_EQ(result->candidatesWritten, expected->candidatesWritten) << trial;
        for (std::uint32_t i = 0u; i < result->candidatesWritten; ++i)
        {
            EXPECT_EQ(optCandidates[i].frameIndex, refCandidates[i].frameIndex) << trial << '/' << i;
            EXPECT_EQ(optCandidates[i].side, refCandidates[i].side) << trial << '/' << i;
            EXPECT_EQ(optCandidates[i].significant, refCandidates[i].significant) << trial << '/' << i;
            EXPECT_EQ(optCandidates[i].confirmed, refCandidates[i].confirmed) << trial << '/' << i;
        }
    }
}

TEST(StackRejectionTest, CompiledRowsMatchTheOneColumnEstimatorBitForBit)
{
    // Every family with a compiled row judges and combines a Float32 row at a time on a compiled
    // table and one column at a time on the reference one. Every such family, frame count the row
    // serves, value rule and output width is held to the reference bit for bit, over histories
    // with gross outliers, ties between frames - which decide the frame a ranking removes -
    // negative zeros, excluded samples, columns with nothing admitted and columns the percentile
    // judges down to nothing - which the row writes as the general combination does - and widths
    // that end in a partial block or never fill one.
    const std::vector<an::InstructionSet> targets = compiledTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this build and host have no compiled rejection row to compare with the reference";
    }
    constexpr std::array<an::StackRejection, 6u> kFamilies{
        an::StackRejection::SigmaClipping,           an::StackRejection::MedianSigmaReplacement,
        an::StackRejection::WinsorizedSigmaClipping, an::StackRejection::LinearFitClipping,
        an::StackRejection::PercentileClipping,      an::StackRejection::GeneralizedEsdTest};
    constexpr std::array<an::ValidityRule, 3u> kRules{an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly,
                                                      an::ValidityRule::ZeroIsNoData};
    const an::EngineContext engine = makeEngine(4u);
    Lcg random(0xB10C5u);
    std::uint64_t compared = 0u;
    for (const an::StackRejection family : kFamilies)
    {
        for (std::uint32_t frames = an::kMinimumRejectionFrames; frames <= 8u; ++frames)
        {
            for (const an::ValidityRule rule : kRules)
            {
                const std::uint32_t width = 1u + random.below(40u);
                const std::uint32_t rows = 1u + random.below(3u);
                const std::uint32_t channels = random.below(3u) == 0u ? 3u : 1u;
                an::StackRequest request =
                    requestOf(an::StackCombination::Average, frames,
                              makeDescriptor(an::SampleType::Float32, width, rows,
                                             channels == 3u ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono));
                request.rejection.method = family;
                request.rejection.sigma =
                    an::SigmaClipThresholds{.low = 1.2 + random.unit(), .high = 1.2 + random.unit()};
                request.rejection.medianFractions =
                    an::MedianClipFractions{.low = 0.005 + 0.1 * random.unit(), .high = 0.005 + 0.1 * random.unit()};
                request.rejection.gesdt = an::GesdtParameters{.maximumOutlierFraction = 0.1 + 0.6 * random.unit(),
                                                              .significance = 0.02 + 0.2 * random.unit()};
                request.output.sampleType = random.below(3u) == 0u ? an::SampleType::Float64 : an::SampleType::Float32;
                request.output.coverageMap = true;
                request.output.signalToNoiseGainMap = true;
                request.output.weightSumMap = true;
                request.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
                const an::StackModel model = makeModel(request);
                const std::size_t samples = static_cast<std::size_t>(width) * rows * channels;
                std::vector<OwnedBand> history = makeHistory(frames, width, rows, channels);
                std::vector<float> base(samples);
                for (float& value : base)
                    value = static_cast<float>(0.2 + 0.6 * random.unit());
                for (std::uint32_t frame = 0u; frame < frames; ++frame)
                {
                    OwnedBand& owned = history[frame];
                    const auto made = an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = rule});
                    ASSERT_TRUE(made.has_value());
                    owned.descriptor = *made;
                    for (std::size_t index = 0u; index < samples; ++index)
                    {
                        const std::uint32_t kind = random.below(20u);
                        float value = base[index] + static_cast<float>(0.01 * (random.unit() - 0.5));
                        if (kind == 0u)
                            value = 0.97f;
                        else if (kind == 1u)
                            value = 0.02f;
                        else if (kind == 2u)
                            value = base[index];
                        else if (kind == 3u && rule == an::ValidityRule::ZeroIsNoData)
                            value = 0.0f;
                        else if (kind == 3u && rule == an::ValidityRule::FiniteOnly)
                            value = std::numeric_limits<float>::quiet_NaN();
                        else if (kind == 4u)
                            value = -0.0f;
                        // Every frame excluded at the first sample leaves that column with
                        // nothing admitted. A negative median, and an even count split into two
                        // distant halves, have the percentile remove every sample of a column,
                        // and the halves tie within themselves.
                        if (index == 0u && rule == an::ValidityRule::ZeroIsNoData)
                            value = 0.0f;
                        else if (index == 0u && rule == an::ValidityRule::FiniteOnly)
                            value = std::numeric_limits<float>::infinity();
                        else if (index == 1u)
                            value = -value;
                        else if (index == 2u)
                            value = frame % 2u == 0u ? 0.3f : 0.7f;
                        owned.float32[index] = value;
                    }
                }
                struct Run
                {
                    OwnedTargets targets;
                    std::vector<std::uint32_t> low;
                    std::vector<std::uint32_t> high;
                    std::vector<std::uint64_t> admitted;
                    std::vector<std::uint64_t> rejected;
                    an::Result<an::StackRejectionOutcome> outcome{an::Status::failure(an::ErrorCode::Internal)};
                };
                const auto run = [&](const an::ExecutionContext& execution) {
                    Run result{.targets = OwnedTargets::forModel(model),
                               .low = std::vector<std::uint32_t>(samples, 9u),
                               .high = std::vector<std::uint32_t>(samples, 9u),
                               .admitted = std::vector<std::uint64_t>(frames, 0u),
                               .rejected = std::vector<std::uint64_t>(frames, 0u)};
                    std::vector<std::uint64_t> workerRows(
                        static_cast<std::size_t>(execution.admittedConcurrency()) * frames * 2u, 0u);
                    const OwnedScratch scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
                    result.outcome = an::combineRejectedStackSampleHistory(
                        execution, model, bandsOf(history), scratch.view(), result.targets.targets(), {},
                        an::StackRejectionMaps{.low = result.low, .high = result.high},
                        an::StackRejectionCombinationPolicy::SirilCompatible,
                        an::StackRejectionFrameCounters{
                            .admitted = result.admitted, .rejected = result.rejected, .workerRows = workerRows});
                    return result;
                };
                const Run expected = run(forcedExecution(engine, an::ComputeBackend::Reference,
                                                         an::InstructionSet::Scalar, 1u + random.below(4u)));
                for (const an::InstructionSet target : targets)
                {
                    SCOPED_TRACE(std::string(an::stackRejectionName(family)) + " frames " + std::to_string(frames) +
                                 " rule " + std::to_string(static_cast<int>(rule)) + " set " +
                                 std::to_string(static_cast<int>(target)));
                    const Run actual =
                        run(forcedExecution(engine, an::ComputeBackend::Cpu, target, 1u + random.below(4u)));
                    ASSERT_EQ(actual.outcome.hasValue(), expected.outcome.hasValue());
                    if (!expected.outcome.hasValue())
                    {
                        EXPECT_EQ(actual.outcome.status().code(), expected.outcome.status().code());
                        continue;
                    }
                    EXPECT_EQ(actual.targets.image, expected.targets.image);
                    EXPECT_EQ(actual.targets.maskBits, expected.targets.maskBits);
                    EXPECT_EQ(actual.targets.coverage, expected.targets.coverage);
                    EXPECT_EQ(actual.targets.gain, expected.targets.gain);
                    EXPECT_EQ(actual.targets.weightSum, expected.targets.weightSum);
                    EXPECT_EQ(actual.low, expected.low);
                    EXPECT_EQ(actual.high, expected.high);
                    EXPECT_EQ(actual.admitted, expected.admitted);
                    EXPECT_EQ(actual.rejected, expected.rejected);
                    EXPECT_EQ(actual.outcome->history.samplesAdmitted, expected.outcome->history.samplesAdmitted);
                    EXPECT_EQ(actual.outcome->history.samplesExcludedByValidity,
                              expected.outcome->history.samplesExcludedByValidity);
                    EXPECT_EQ(actual.outcome->history.elementsWithSamples,
                              expected.outcome->history.elementsWithSamples);
                    EXPECT_EQ(actual.outcome->rejectedLow, expected.outcome->rejectedLow);
                    EXPECT_EQ(actual.outcome->rejectedHigh, expected.outcome->rejectedHigh);
                    EXPECT_EQ(actual.outcome->mappedLow, expected.outcome->mappedLow);
                    EXPECT_EQ(actual.outcome->mappedHigh, expected.outcome->mappedHigh);
                    EXPECT_EQ(actual.outcome->samplesCombined, expected.outcome->samplesCombined);
                    EXPECT_FALSE(expected.outcome->history.compiledRowUsed);
                    EXPECT_TRUE(actual.outcome->history.compiledRowUsed);
                    ++compared;
                }
            }
        }
    }
    std::cout << "compared " << compared << " compiled rejection row runs with the reference" << std::endl;
    EXPECT_GT(compared, 0u);
}

TEST(StackRejectionTest, CompiledRowsRefuseWhatTheOneColumnPathRefuses)
{
    // An admitted sample that is not finite, and statistics that leave the Float32 range, stop the
    // compiled row with the refusal the one-column path returns for the same history.
    const std::vector<an::InstructionSet> targets = compiledTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this build and host have no compiled rejection row to compare with the reference";
    }
    constexpr std::array<an::StackRejection, 6u> kFamilies{
        an::StackRejection::SigmaClipping,           an::StackRejection::MedianSigmaReplacement,
        an::StackRejection::WinsorizedSigmaClipping, an::StackRejection::LinearFitClipping,
        an::StackRejection::PercentileClipping,      an::StackRejection::GeneralizedEsdTest};
    constexpr std::uint32_t kFrames = 6u;
    constexpr std::uint32_t kWidth = 21u;
    const an::EngineContext engine = makeEngine(2u);
    for (const an::StackRejection family : kFamilies)
    {
        for (const bool overflow : {false, true})
        {
            an::StackRequest request =
                requestOf(an::StackCombination::Average, kFrames, makeDescriptor(an::SampleType::Float32, kWidth, 1u));
            request.rejection.method = family;
            const an::StackModel model = makeModel(request);
            std::vector<OwnedBand> history = makeHistory(kFrames, kWidth, 1u, 1u);
            for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                    history[frame].float32[x] = 0.5f + 0.01f * static_cast<float>((frame * 7u + x) % 5u);
            // A column late in the row, so the compiled row has combined whole blocks before it stops.
            // Opposed extremes overflow every family's deviation or fit except the percentile's,
            // whose median of them is zero; equal extremes overflow the sum of its middle two.
            for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
                history[frame].float32[17u] = !overflow ? (frame == 3u ? std::numeric_limits<float>::infinity() : 0.5f)
                                              : family == an::StackRejection::PercentileClipping
                                                  ? 3.0e38f
                                                  : (frame % 2u == 0u ? 3.0e38f : -3.0e38f);
            const auto run = [&](const an::ExecutionContext& execution) {
                OwnedTargets owned = OwnedTargets::forModel(model);
                const OwnedScratch scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
                return an::combineRejectedStackSampleHistory(execution, model, bandsOf(history), scratch.view(),
                                                             owned.targets(), {}, an::StackRejectionMaps{},
                                                             an::StackRejectionCombinationPolicy::SirilCompatible);
            };
            const auto expected =
                run(forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar, 1u));
            ASSERT_FALSE(expected.hasValue());
            EXPECT_EQ(expected.status().code(), an::ErrorCode::InvalidData);
            for (const an::InstructionSet target : targets)
            {
                SCOPED_TRACE(std::string(an::stackRejectionName(family)) + (overflow ? " overflow" : " non-finite") +
                             " set " + std::to_string(static_cast<int>(target)));
                const auto actual = run(forcedExecution(engine, an::ComputeBackend::Cpu, target, 1u));
                ASSERT_FALSE(actual.hasValue());
                EXPECT_EQ(actual.status().code(), expected.status().code());
                EXPECT_STREQ(actual.status().message(), expected.status().message());
            }
        }
    }
}

TEST(StackRejectionTest, CompiledRowsRefuseANaNTheZeroRuleAdmits)
{
    // Zero-is-no-data admits every sample but an exact zero, so a NaN under it is an admitted
    // sample that is not finite: the one-column path refuses the history, and the compiled row of
    // every family must refuse it with the same answer rather than leave the NaN out of its column
    // as if it were a zero - whether the backend is left to choose or a compiled table is forced.
    constexpr std::array<an::StackRejection, 6u> kFamilies{
        an::StackRejection::SigmaClipping,           an::StackRejection::MedianSigmaReplacement,
        an::StackRejection::WinsorizedSigmaClipping, an::StackRejection::LinearFitClipping,
        an::StackRejection::PercentileClipping,      an::StackRejection::GeneralizedEsdTest};
    constexpr std::uint32_t kFrames = 6u;
    constexpr std::uint32_t kWidth = 21u;
    const std::vector<an::InstructionSet> targets = compiledTargets();
    // Every execution below is held at once, each lent one thread.
    const an::EngineContext engine = makeEngine(static_cast<std::uint32_t>(2u + targets.size()));
    an::ExecutionOptions automatic;
    automatic.maxConcurrency = 1u;
    automatic.backend = an::ComputeBackend::Automatic;
    std::vector<an::ExecutionContext> executions;
    an::Result<an::ExecutionContext> automaticExecution = an::ExecutionContext::create(engine, automatic);
    ASSERT_TRUE(automaticExecution.hasValue()) << automaticExecution.status().message();
    executions.push_back(std::move(*automaticExecution));
    for (const an::InstructionSet target : targets)
        executions.push_back(forcedExecution(engine, an::ComputeBackend::Cpu, target, 1u));
    const an::ExecutionContext reference =
        forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar, 1u);
    for (const an::StackRejection family : kFamilies)
    {
        an::StackRequest request =
            requestOf(an::StackCombination::Average, kFrames, makeDescriptor(an::SampleType::Float32, kWidth, 1u));
        request.rejection.method = family;
        const an::StackModel model = makeModel(request);
        std::vector<OwnedBand> history = makeHistory(kFrames, kWidth, 1u, 1u);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            history[frame].descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
            for (std::uint32_t x = 0u; x < kWidth; ++x)
                history[frame].float32[x] = 0.5f + 0.01f * static_cast<float>((frame * 7u + x) % 5u);
        }
        // A zero and a negative zero the rule excludes, and the NaN in a column late in the row, so
        // the compiled row has combined whole blocks before it reaches it.
        history[0].float32[4] = 0.0f;
        history[5].float32[9] = -0.0f;
        const auto run = [&](const an::ExecutionContext& execution) {
            OwnedTargets owned = OwnedTargets::forModel(model);
            const OwnedScratch scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
            return an::combineRejectedStackSampleHistory(execution, model, bandsOf(history), scratch.view(),
                                                         owned.targets(), {}, an::StackRejectionMaps{},
                                                         an::StackRejectionCombinationPolicy::SirilCompatible);
        };
        for (std::size_t index = 0u; index < targets.size(); ++index)
        {
            SCOPED_TRACE(std::string(an::stackRejectionName(family)) + " without the NaN, set " +
                         std::to_string(static_cast<int>(targets[index])));
            const auto clean = run(executions[index + 1u]);
            ASSERT_TRUE(clean.hasValue()) << clean.status().message();
            EXPECT_TRUE(clean->history.compiledRowUsed);
        }

        history[3].float32[17] = std::numeric_limits<float>::quiet_NaN();
        const auto expected = run(reference);
        ASSERT_FALSE(expected.hasValue());
        EXPECT_EQ(expected.status().code(), an::ErrorCode::InvalidData);
        for (std::size_t index = 0u; index < executions.size(); ++index)
        {
            SCOPED_TRACE(std::string(an::stackRejectionName(family)) + " on execution " + std::to_string(index));
            const auto actual = run(executions[index]);
            ASSERT_FALSE(actual.hasValue()) << "the NaN was left out instead of refused";
            EXPECT_EQ(actual.status().code(), expected.status().code());
            EXPECT_STREQ(actual.status().message(), expected.status().message());
        }
    }
}

TEST(StackRejectionTest, ShortHistoryMediansMatchEveryPermutation)
{
    const auto engine = makeEngine(1u);
    const auto execution = makeExecution(engine, 1u);
    const auto check = [&]<typename T>(an::SampleType type) {
        for (std::uint32_t count = 2u; count <= 8u; ++count)
        {
            auto request = requestOf(an::StackCombination::Average, 8u, makeDescriptor(type));
            request.rejection.method = an::StackRejection::PercentileClipping;
            request.rejection.medianFractions = {.low = 0.02, .high = 0.02};
            const auto model = makeModel(request);
            auto scratch = OwnedScratch::forModel(model, 1u);
            std::vector<T> order(count);
            for (std::uint32_t i = 0u; i < count; ++i)
                order[i] = static_cast<T>((i + 1u) / 16.0);
            const T median = static_cast<T>((count + 1u) / 32.0);
            do
            {
                auto samples = order;
                std::vector<an::StackSampleDisposition> flags(count, an::StackSampleDisposition::Kept);
                const auto result = an::rejectStackSampleColumn(execution, model, std::span(samples), std::span(flags),
                                                                scratch.spans[0]);
                ASSERT_TRUE(result.hasValue());
                EXPECT_EQ(samples, order);
                for (std::uint32_t i = 0u; i < count; ++i)
                {
                    const auto expected = order[i] < median   ? an::StackSampleDisposition::Low
                                          : order[i] > median ? an::StackSampleDisposition::High
                                                              : an::StackSampleDisposition::Kept;
                    ASSERT_EQ(flags[i], expected) << count << '/' << i;
                }
            } while (std::next_permutation(order.begin(), order.end()));
        }
    };
    check.template operator()<float>(an::SampleType::Float32);
    check.template operator()<double>(an::SampleType::Float64);
}

TEST(StackRejectionTest, ColumnFailureIsAtomicAndMissingValuesAreNotJudged)
{
    const auto engine = makeEngine(3u);
    auto request = requestOf(an::StackCombination::Average, 8u);
    request.rejection.method = an::StackRejection::GeneralizedEsdTest;
    const auto model = makeModel(request);
    std::array<float, 5> samples{0.0f, 1.0f, 1.0f, 1.0f, std::numeric_limits<float>::quiet_NaN()};
    std::array<an::StackSampleDisposition, 5> flags{};
    flags.fill(an::StackSampleDisposition::Kept);
    auto scratch = OwnedScratch::forModel(model, 1u);
    const auto execution = makeExecution(engine, 1u);
    auto result = an::rejectStackSampleColumn(execution, model, std::span(samples), std::span(flags), scratch.spans[0]);
    EXPECT_EQ(result.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(flags[0], an::StackSampleDisposition::Kept);
    EXPECT_EQ(samples[0], 0.0f);
    flags[4] = an::StackSampleDisposition::Missing;
    result = an::rejectStackSampleColumn(execution, model, std::span(samples), std::span(flags), scratch.spans[0]);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result->admitted, 4u);
    EXPECT_EQ(flags[4], an::StackSampleDisposition::Missing);
    EXPECT_TRUE(std::isnan(samples[4]));
    flags.fill(an::StackSampleDisposition::Kept);
    std::stop_source stop;
    stop.request_stop();
    const auto stopped = makeExecution(engine, 1u, nullptr, stop.get_token());
    result = an::rejectStackSampleColumn(stopped, model, std::span(samples), std::span(flags), scratch.spans[0]);
    EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(flags[0], an::StackSampleDisposition::Kept);
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const auto expired = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(expired.hasValue());
    result = an::rejectStackSampleColumn(*expired, model, std::span(samples), std::span(flags), scratch.spans[0]);
    EXPECT_EQ(result.status().code(), an::ErrorCode::DeadlineExceeded);
}

TEST(StackRejectionTest, GesdReportsProvisionalSidesSeparatelyFromBackwardConfirmation)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine, 1u);
    auto request = requestOf(an::StackCombination::Average, 8u);
    request.rejection.method = an::StackRejection::GeneralizedEsdTest;
    request.rejection.gesdt.maximumOutlierFraction = 0.5;
    const auto model = makeModel(request);
    auto scratch = OwnedScratch::forModel(model, 1u);
    // The first low endpoint is not significant on its own. The following high endpoint
    // confirms both; the next two provisional removals remain survivors.
    std::array<float, 8> samples{100.0f, -10.0f, 1.0f,  -100.0f,
                                 0.0f,   10.0f,  -1.0f, std::numeric_limits<float>::quiet_NaN()};
    const auto original = samples;
    std::array<an::StackSampleDisposition, 8> flags{};
    flags.back() = an::StackSampleDisposition::Missing;
    std::array<an::StackGesdCandidate, 5> candidates{};
    candidates.back().frameIndex = 987u;
    const auto before = engine.memoryResource()->counters();
    const auto result = an::evaluateStackGesdColumn(execution, model, std::span(samples), std::span(flags),
                                                    scratch.spans[0], std::span(candidates));
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->candidatesWritten, 4u);
    EXPECT_EQ(result->rejection.admitted, 7u);
    EXPECT_EQ(result->rejection.surviving, 5u);
    const std::array<std::uint32_t, 4> indices{3u, 0u, 1u, 5u};
    for (std::size_t i = 0u; i < indices.size(); ++i)
    {
        EXPECT_EQ(candidates[i].frameIndex, indices[i]);
        EXPECT_EQ(candidates[i].side,
                  i % 2u == 0u ? an::StackGesdCandidateSide::Low : an::StackGesdCandidateSide::High);
        EXPECT_EQ(candidates[i].significant, i == 1u);
        EXPECT_EQ(candidates[i].confirmed, i < 2u);
    }
    EXPECT_EQ(candidates.back().frameIndex, 987u);
    EXPECT_EQ(flags[3], an::StackSampleDisposition::Low);
    EXPECT_EQ(flags[0], an::StackSampleDisposition::High);
    EXPECT_EQ(flags[1], an::StackSampleDisposition::Kept);
    EXPECT_EQ(flags.back(), an::StackSampleDisposition::Missing);
    EXPECT_EQ(std::memcmp(samples.data(), original.data(), sizeof(samples)), 0);
    EXPECT_EQ(engine.memoryResource()->counters().totalAllocations, before.totalAllocations);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before.liveBytes);
}

TEST(StackRejectionTest, GesdTraceKeepsFloat64SamplesAndHasNoCandidatesForDegenerateColumns)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine, 1u);
    auto request =
        requestOf(an::StackCombination::Average, 8u, makeDescriptor(an::SampleType::Float64, kWidth, kHeight));
    request.rejection.method = an::StackRejection::GeneralizedEsdTest;
    request.rejection.gesdt.maximumOutlierFraction = 0.9;
    const auto model = makeModel(request);
    auto scratch = OwnedScratch::forModel(model, 1u);
    for (const std::vector<double>& source :
         {std::vector<double>{}, std::vector<double>{0.0}, std::vector<double>{0.0, 1.0}, std::vector<double>(8u, 0.5)})
    {
        auto samples = source;
        std::vector<an::StackSampleDisposition> flags(samples.size());
        std::array<an::StackGesdCandidate, 8> candidates{};
        candidates[0].frameIndex = 987u;
        const auto result = an::evaluateStackGesdColumn(execution, model, std::span(samples), std::span(flags),
                                                        scratch.spans[0], std::span(candidates));
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(result->candidatesWritten, 0u);
        EXPECT_EQ(candidates[0].frameIndex, 987u);
        EXPECT_EQ(samples, source);
    }
    std::array<double, 5> samples{0.0, 1.000000000001, 2.000000000001, 3.000000000001, 100.000000000001};
    const auto original = samples;
    std::array<an::StackSampleDisposition, 5> flags{};
    std::array<an::StackGesdCandidate, 5> candidates{};
    const auto result = an::evaluateStackGesdColumn(execution, model, std::span(samples), std::span(flags),
                                                    scratch.spans[0], std::span(candidates));
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result->candidatesWritten, 3u);
    EXPECT_EQ(candidates[0].frameIndex, 4u);
    EXPECT_EQ(samples, original);
}

TEST(StackRejectionTest, GesdTraceRefusesInvalidBuffersAndFailuresWithoutPublishing)
{
    const auto engine = makeEngine(3u);
    const auto execution = makeExecution(engine, 1u);
    auto request = requestOf(an::StackCombination::Average, 8u);
    request.rejection.method = an::StackRejection::GeneralizedEsdTest;
    request.rejection.gesdt.maximumOutlierFraction = 0.5;
    const auto model = makeModel(request);
    auto scratch = OwnedScratch::forModel(model, 1u);
    std::array<float, 8> samples{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 100.0f};
    const auto original = samples;
    std::array<an::StackSampleDisposition, 8> flags{};
    std::array<an::StackGesdCandidate, 4> candidates{};
    candidates[0].frameIndex = 987u;
    const auto evaluate = [&](const an::ExecutionContext& context, const an::StackModel& selected,
                              std::span<an::StackGesdCandidate> output) {
        return an::evaluateStackGesdColumn(context, selected, std::span(samples), std::span(flags), scratch.spans[0],
                                           output);
    };
    EXPECT_EQ(evaluate(execution, model, std::span(candidates).first(3u)).status().code(),
              an::ErrorCode::IncompatibleArguments);
    const auto scratchCandidates = std::span(reinterpret_cast<an::StackGesdCandidate*>(scratch.spans[0].data()), 4u);
    EXPECT_EQ(evaluate(execution, model, scratchCandidates).status().code(), an::ErrorCode::InvalidArgument);
    const auto sampleCandidates = std::span(reinterpret_cast<an::StackGesdCandidate*>(samples.data()), 4u);
    EXPECT_EQ(evaluate(execution, model, sampleCandidates).status().code(), an::ErrorCode::InvalidArgument);
    alignas(an::StackGesdCandidate) std::array<std::byte, 5u * sizeof(an::StackGesdCandidate)> unaligned{};
    const auto misaligned = std::span(reinterpret_cast<an::StackGesdCandidate*>(unaligned.data() + 1u), 4u);
    EXPECT_EQ(evaluate(execution, model, misaligned).status().code(), an::ErrorCode::InvalidArgument);
    const an::ExecutionContext empty;
    const auto emptyRefused = evaluate(empty, model, candidates);
    EXPECT_EQ(emptyRefused.code(), an::ErrorCode::NotInitialized);
    EXPECT_STREQ(emptyRefused.status().operation(), "an::evaluateStackGesdColumn");
    ASSERT_NE(emptyRefused.status().findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(emptyRefused.status().findContext(an::ContextKey::Parameter)->text, "execution");
    const auto scratchRefused = an::rejectStackSampleColumn(execution, model, std::span(samples), std::span(flags), {});
    EXPECT_STREQ(scratchRefused.status().operation(), "an::rejectStackSampleColumn");
    ASSERT_NE(scratchRefused.status().findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(scratchRefused.status().findContext(an::ContextKey::Parameter)->text, "scratch");
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const auto expired = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(expired.hasValue());
    EXPECT_EQ(evaluate(*expired, model, candidates).status().code(), an::ErrorCode::DeadlineExceeded);
    request.rejection.method = an::StackRejection::SigmaClipping;
    EXPECT_EQ(evaluate(execution, makeModel(request), candidates).status().code(),
              an::ErrorCode::IncompatibleArguments);
    std::stop_source stop;
    stop.request_stop();
    const auto stopped = makeExecution(engine, 1u, nullptr, stop.get_token());
    EXPECT_EQ(evaluate(stopped, model, candidates).status().code(), an::ErrorCode::Cancelled);
    samples.back() = std::numeric_limits<float>::infinity();
    EXPECT_EQ(evaluate(execution, model, candidates).status().code(), an::ErrorCode::InvalidData);
    EXPECT_TRUE(std::isinf(samples.back()));
    samples.back() = original.back();
    EXPECT_EQ(samples, original);
    EXPECT_EQ(candidates[0].frameIndex, 987u);
    for (const auto flag : flags)
        EXPECT_EQ(flag, an::StackSampleDisposition::Kept);
}

TEST(StackRejectionTest, WorkingSetScratchAndRejectionMapsStayInsideThePlan)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (std::uint8_t mode = 1u; mode <= 7u; ++mode)
    {
        auto request = requestOf(an::StackCombination::Average, 33u);
        request.rejection.method = static_cast<an::StackRejection>(mode);
        request.output.coverageMap = true;
        request.output.rejectionMaps = an::RejectionMapForm::Merged;
        const auto model = makeModel(request);
        const auto plan = an::planStack(execution, model, {});
        ASSERT_TRUE(plan.hasValue());
        auto allocated = an::StackWorkingSet::create(engine, execution, model, *plan);
        ASSERT_TRUE(allocated.hasValue());
        auto& set = *allocated;
        ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Combine).ok());
        const auto held = engine.memoryResource()->counters();
        auto mask = set.outputMask();
        ASSERT_TRUE(mask.has_value());
        const auto layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
        const auto bytesPerBand = kWidth * kHeight * sizeof(float);
        std::vector<an::StackFrameBand> bands;
        const auto tile = set.tile();
        for (std::uint32_t frame = 0u; frame < 33u; ++frame)
        {
            auto* samples = reinterpret_cast<float*>(tile.data() + frame * bytesPerBand);
            std::fill_n(samples, kWidth * kHeight, frame == 20u ? 100.0f : 1.0f);
            bands.push_back(an::StackFrameBand{.samples = samples, .layout = &layout, .frameIndex = frame});
        }
        std::vector<std::span<std::byte>> scratch;
        for (std::uint32_t worker = 0u; worker < plan->memory.workers; ++worker)
            scratch.push_back(set.workerScratch(worker));
        const auto output = an::StackFinalizeTargets{.outputImage = set.outputImage(),
                                                     .outputMask = &*mask,
                                                     .coverage = set.outputCountMap(an::StackOutputMap::Coverage)};
        const auto merged = set.outputCountMap(an::StackOutputMap::RejectionMerged);
        const auto result =
            an::combineRejectedStackSampleHistory(execution, model, bands, scratch, output, {}, {.merged = merged});
        ASSERT_TRUE(result.hasValue()) << mode << result.status().message();
        EXPECT_EQ(engine.memoryResource()->counters().totalAllocations, held.totalAllocations);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, held.liveBytes);
        EXPECT_EQ(result->scratchBytesPerWorker, plan->memory.scratchBytesPerWorker);
        EXPECT_EQ(merged[0], 1u);
        EXPECT_EQ(*reinterpret_cast<const float*>(set.outputImage().data()), 1.0f);
        set.endPass();
    }
}

TEST(StackRejectionTest, NamesTheAdoptedRangeAndExhaustedColumnFallbacks)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    auto request = requestOf(an::StackCombination::Average, 12u, makeDescriptor(an::SampleType::Float32, 1u, 1u));
    request.rejection.method = an::StackRejection::MedianSigmaReplacement;
    request.rejection.sigma = {.low = 1.5, .high = 1.5};
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.coverageMap = true;
    const auto model = makeModel(request);
    const std::array<float, 12> values{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 2.0f, 4.0f};
    std::array<double, 12> weights;
    weights.fill(1.0);
    auto history = makeHistory(12u, 1u, 1u, 1u);
    for (std::size_t i = 0u; i < history.size(); ++i)
        history[i].float32[0] = values[i];
    auto scratch = OwnedScratch::forModel(model, execution.admittedConcurrency());
    auto adopted = OwnedTargets::forModel(model);
    auto survivors = OwnedTargets::forModel(model);
    ASSERT_TRUE(an::combineRejectedStackSampleHistory(execution, model, bandsOf(history), scratch.view(),
                                                      adopted.targets(), weights)
                    .hasValue());
    ASSERT_TRUE(an::combineRejectedStackSampleHistory(execution, model, bandsOf(history), scratch.view(),
                                                      survivors.targets(), weights, {},
                                                      an::StackRejectionCombinationPolicy::SurvivorValues)
                    .hasValue());
    // Every replacement converged between original samples; the adopted range reread finds none.
    EXPECT_EQ(adopted.values()[0], 0.0);
    EXPECT_EQ(survivors.values()[0], 0.61874997615814209);
    EXPECT_EQ(adopted.coverage[0], 12u);

    request.rejection.method = an::StackRejection::PercentileClipping;
    request.weighting.method = an::StackWeighting::Uniform;
    const auto negativeModel = makeModel(request);
    for (auto& frame : history)
        frame.float32[0] = -1.0f;
    scratch = OwnedScratch::forModel(negativeModel, execution.admittedConcurrency());
    adopted = OwnedTargets::forModel(negativeModel);
    survivors = OwnedTargets::forModel(negativeModel);
    const auto fallback = an::combineRejectedStackSampleHistory(execution, negativeModel, bandsOf(history),
                                                                scratch.view(), adopted.targets());
    ASSERT_TRUE(fallback.hasValue());
    ASSERT_TRUE(an::combineRejectedStackSampleHistory(execution, negativeModel, bandsOf(history), scratch.view(),
                                                      survivors.targets(), {}, {},
                                                      an::StackRejectionCombinationPolicy::SurvivorValues)
                    .hasValue());
    EXPECT_EQ(fallback->mappedLow, 12u);
    EXPECT_EQ(adopted.values()[0], -1.0);
    EXPECT_EQ(adopted.coverage[0], 12u);
    EXPECT_EQ(survivors.values()[0], 0.0);
    EXPECT_EQ(survivors.coverage[0], 0u);
    EXPECT_EQ(survivors.maskBits[0], 0u);
}

TEST(StackRejectionTest, HistoryRefusalNamesTheRejectedCombination)
{
    const auto engine = makeEngine(1u);
    const auto execution = makeExecution(engine, 1u);
    auto request = requestOf(an::StackCombination::Average, 8u);
    request.rejection.method = an::StackRejection::SigmaClipping;
    const auto model = makeModel(request);
    const auto refused = an::combineRejectedStackSampleHistory(execution, model, {}, {}, {});
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_STREQ(refused.status().operation(), "an::combineRejectedStackSampleHistory");
}

TEST(StackRejectionTest, GeneralFloatMedianDoesNotOverflowItsMiddlePair)
{
    const auto engine = makeEngine(1u);
    const auto execution = makeExecution(engine, 1u);
    auto request = requestOf(an::StackCombination::Average, 10u);
    request.rejection.method = an::StackRejection::PercentileClipping;
    const auto model = makeModel(request);
    std::array<float, 10> samples;
    samples.fill((std::numeric_limits<float>::max)() * 0.75f);
    const auto original = samples;
    std::array<an::StackSampleDisposition, 10> flags;
    flags.fill(an::StackSampleDisposition::Kept);
    auto scratch = OwnedScratch::forModel(model, 1u);
    const auto result =
        an::rejectStackSampleColumn(execution, model, std::span(samples), std::span(flags), scratch.spans[0]);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(samples, original);
    EXPECT_EQ(result->surviving, 10u);
}
