// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the stack accumulators: every whole-output term updated by one frame at a
// time under the model's precision, weight, validity and overflow rules, the same bits at every
// thread count and on every compiled backend, and the finalization that turns the terms into the
// output image, its mask and its maps.

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulatorRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

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
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

#if defined(_MSC_VER) && defined(_DEBUG)
// The shared engine uses the same debug CRT; replacing this executable's operator new would
// miss allocations made inside the DLL. Track only the calling thread of the single-worker case.
thread_local std::uint64_t* allocationCount = nullptr;

int countAllocation(int kind, void*, std::size_t, int, long, const unsigned char*, int)
{
    if (allocationCount != nullptr && (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC))
        ++*allocationCount;
    return 1;
}

class CountAllocations
{
public:
    explicit CountAllocations(std::uint64_t& count)
        : m_previousHook(_CrtSetAllocHook(countAllocation)), m_previousCount(allocationCount)
    {
        allocationCount = &count;
    }
    ~CountAllocations()
    {
        allocationCount = m_previousCount;
        _CrtSetAllocHook(m_previousHook);
    }
    CountAllocations(const CountAllocations&) = delete;
    CountAllocations& operator=(const CountAllocations&) = delete;

private:
    _CRT_ALLOC_HOOK m_previousHook;
    std::uint64_t* m_previousCount;
};
#endif

constexpr std::uint32_t kWidth = 8u;
constexpr std::uint32_t kHeight = 4u;
constexpr std::uint32_t kFrames = 3u;

/// An engine with a stated concurrency.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// One operation in an engine, optionally narrowed and stoppable.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 std::uint32_t concurrency = an::kUseContextConcurrency,
                                                 std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
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

/// A descriptor of the stated representation and schema in the normalized linear domain.
[[nodiscard]] an::ImageDescriptor makeDescriptor(an::SampleType sampleType = an::SampleType::Float32,
                                                 std::uint32_t width = kWidth, std::uint32_t height = kHeight,
                                                 an::ChannelSchema schema = an::ChannelSchema::Mono)
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
        .numericDomain = an::NumericDomain::NormalizedLinear,
        .cfa = {},
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A plain average of the stated frames, which every rule admits.
[[nodiscard]] an::StackRequest baseRequest(std::uint32_t frames = kFrames,
                                           const an::ImageDescriptor& descriptor = makeDescriptor())
{
    return an::StackRequest{.input = an::StackInputDescription{.frameCount = frames, .descriptor = descriptor}};
}

/// The checked model of a request every rule admits.
[[nodiscard]] an::StackModel makeModel(const an::StackRequest& request)
{
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    return model.has_value() ? *model : *an::StackModel::create(baseRequest());
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

/// The five terms of a streaming stack, owned here and handed out as the working set hands them.
struct OwnedAccumulators
{
    std::vector<double> sums;
    std::vector<std::uint32_t> counts;
    std::vector<double> weightSums;
    std::vector<double> weightSquareSums;
    std::vector<double> varianceMoments;

    /// Every term the model keeps, zeroed, at the model's element count.
    [[nodiscard]] static OwnedAccumulators forModel(const an::StackModel& model)
    {
        const std::uint64_t elements = model.request().input.descriptor.extent().sampleCount();
        OwnedAccumulators owned;
        if (an::stackAccumulatorKept(model, an::StackAccumulator::Sums))
        {
            owned.sums.assign(static_cast<std::size_t>(elements), 0.0);
        }
        if (an::stackAccumulatorKept(model, an::StackAccumulator::Counts))
        {
            owned.counts.assign(static_cast<std::size_t>(elements), 0u);
        }
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

    /// Every term's bytes, one after another, for an exact comparison.
    [[nodiscard]] std::vector<std::byte> bytes() const
    {
        std::vector<std::byte> all;
        const auto append = [&all](const auto& values) {
            const std::span<const std::byte> raw = std::as_bytes(std::span(values));
            all.insert(all.end(), raw.begin(), raw.end());
        };
        append(sums);
        append(counts);
        append(weightSums);
        append(weightSquareSums);
        append(varianceMoments);
        return all;
    }
};

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

/// The sample a test frame holds, a function of where it is so every position differs.
[[nodiscard]] float sampleAt(std::uint32_t frame, std::uint32_t channel, std::uint32_t y, std::uint32_t x,
                             std::uint32_t width)
{
    return 0.1f * static_cast<float>(frame + 1u) + 0.01f * static_cast<float>(channel) +
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

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
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

/// The Float32 output samples of a finalized image, read back as doubles.
[[nodiscard]] std::vector<double> outputAsDoubles(std::span<const std::byte> bytes, an::SampleType type)
{
    std::vector<double> values;
    if (type == an::SampleType::Float32)
    {
        values.resize(bytes.size() / sizeof(float));
        for (std::size_t index = 0u; index < values.size(); ++index)
        {
            float value = 0.0f;
            std::memcpy(&value, bytes.data() + index * sizeof(float), sizeof(float));
            values[index] = static_cast<double>(value);
        }
    }
    else
    {
        values.resize(bytes.size() / sizeof(double));
        std::memcpy(values.data(), bytes.data(), values.size() * sizeof(double));
    }
    return values;
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

// The update table is a constant expression, so a kernel can be written against it.
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
constexpr std::optional<an::StackModel> kConstantMinimum = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
                     .combination = an::StackCombination::Minimum});
constexpr std::optional<an::StackModel> kConstantMaximum = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
                     .combination = an::StackCombination::Maximum});
constexpr std::optional<an::StackModel> kConstantSum = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
                     .combination = an::StackCombination::Sum});
static_assert(kConstantMinimum.has_value() && kConstantMaximum.has_value() && kConstantSum.has_value());
static_assert(an::stackAccumulatorUpdateOf(*kConstantMinimum) == an::StackAccumulatorUpdate::Minimum);
static_assert(an::stackAccumulatorUpdateOf(*kConstantMaximum) == an::StackAccumulatorUpdate::Maximum);
static_assert(an::stackAccumulatorUpdateOf(*kConstantSum) == an::StackAccumulatorUpdate::WeightedSum);
static_assert(an::isStackAccumulatorUpdate(an::StackAccumulatorUpdate::Maximum));
static_assert(!an::isStackAccumulatorUpdate(static_cast<an::StackAccumulatorUpdate>(3u)));

#if defined(_MSC_VER) && defined(_DEBUG)
TEST(StackAccumulatorsTest, BatchesFramesWithoutHeapBuffers)
{
    const auto engine = makeEngine(1u);
    const auto execution = makeExecution(engine);
    constexpr std::uint32_t kCount = 17u;
    const auto model = makeModel(baseRequest(kCount));
    auto accumulators = OwnedAccumulators::forModel(model);
    std::vector<OwnedBand> owners;
    owners.reserve(kCount);
    std::vector<an::StackFrameBand> bands;
    bands.reserve(kCount);
    for (std::uint32_t frame = 0u; frame < kCount; ++frame)
    {
        owners.push_back(makeBand(frame, kWidth, kHeight, 1u));
        bands.push_back(owners.back().band());
    }
    std::vector<std::uint64_t> admitted(kCount, 0u);
    const auto warmed = an::accumulateStackFrames(execution, model, accumulators.set(), bands, {}, admitted);
    ASSERT_TRUE(warmed.hasValue());
    std::uint64_t allocations = 0u;
    const auto result = [&] {
        const CountAllocations counter(allocations);
        return an::accumulateStackFrames(execution, model, accumulators.set(), bands, {}, admitted);
    }();
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(allocations, 0u);
    EXPECT_EQ(result->samplesAccumulated, kCount * kWidth * kHeight);
}

TEST(StackMethodsTest, SampleHistoryUsesPlannedScratchWithoutHeapBuffers)
{
    const auto engine = makeEngine(1u);
    const auto execution = makeExecution(engine);
    constexpr std::uint32_t kCount = 17u;
    std::vector<OwnedBand> owners;
    owners.reserve(kCount);
    std::vector<an::StackFrameBand> bands;
    for (std::uint32_t frame = 0u; frame < kCount; ++frame)
    {
        owners.push_back(makeBand(frame, kWidth, kHeight, 1u));
        bands.push_back(owners.back().band());
    }
    for (const auto combination : {an::StackCombination::Median, an::StackCombination::AutoAdaptiveWeightedAverage})
    {
        auto request = baseRequest(kCount);
        request.combination = combination;
        const auto model = makeModel(request);
        const auto needed = an::stackScratchBytesPerWorker(model);
        std::vector<std::byte> scratch(needed + an::kStackScratchAlignmentBytes);
        const auto address = reinterpret_cast<std::uintptr_t>(scratch.data());
        const auto offset = (an::kStackScratchAlignmentBytes - address % an::kStackScratchAlignmentBytes) %
                            an::kStackScratchAlignmentBytes;
        const std::array<std::span<std::byte>, 1u> slices{std::span<std::byte>(scratch.data() + offset, needed)};
        std::vector<float> output(kWidth * kHeight);
        const an::StackFinalizeTargets targets{.outputSampleType = an::SampleType::Float32,
                                               .outputImage = std::as_writable_bytes(std::span<float>(output))};
        const auto warmed = an::combineStackSampleHistory(execution, model, bands, slices, targets);
        ASSERT_TRUE(warmed.hasValue()) << warmed.status().message();
        std::uint64_t allocations = 0u;
        const auto result = [&] {
            const CountAllocations counter(allocations);
            return an::combineStackSampleHistory(execution, model, bands, slices, targets);
        }();
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(result->samplesAdmitted, kCount * kWidth * kHeight);
    }
}
#endif

} // namespace

TEST(StackAccumulatorsTest, RefusesAnEmptyExecutionAndASampleHistoryModel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest());
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    const OwnedBand band = makeBand(0u, kWidth, kHeight, 1u);

    const an::Result<an::StackAccumulationOutcome> empty =
        an::accumulateStackFrame(an::ExecutionContext{}, model, owned.set(), band.band(), {});
    EXPECT_EQ(empty.status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(owned.counts[0], 0u);

    an::StackRequest clipped = baseRequest();
    clipped.rejection.method = an::StackRejection::SigmaClipping;
    const an::StackModel history = makeModel(clipped);
    OwnedAccumulators none = OwnedAccumulators::forModel(history);
    EXPECT_TRUE(none.sums.empty());
    const an::Result<an::StackAccumulationOutcome> refused =
        an::accumulateStackFrame(execution, history, none.set(), band.band(), {});
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused.status(), "sample history")) << refused.status().message();

    std::vector<std::byte> output(static_cast<std::size_t>(kWidth) * kHeight * sizeof(float));
    const an::Result<an::StackFinalizeOutcome> finalizeEmpty = an::finalizeStackAccumulators(
        an::ExecutionContext{}, model, owned.set(), an::StackFinalizeTargets{.outputImage = output});
    EXPECT_EQ(finalizeEmpty.status().code(), an::ErrorCode::NotInitialized);
    const an::Result<an::StackFinalizeOutcome> finalizeHistory =
        an::finalizeStackAccumulators(execution, history, none.set(), an::StackFinalizeTargets{.outputImage = output});
    EXPECT_EQ(finalizeHistory.status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(StackAccumulatorsTest, ChecksTheSetAgainstTheModelByTerm)
{
    an::StackRequest request =
        baseRequest(kFrames, makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    const an::StackModel model = makeModel(request);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight * 3u;

    const an::Result<std::uint64_t> checked = an::checkStackAccumulatorSet(model, owned.set());
    ASSERT_TRUE(checked.hasValue()) << checked.status().message();
    EXPECT_EQ(*checked, kElements);

    {
        an::StackAccumulatorSet missing = owned.set();
        missing.sums = {};
        const an::Result<std::uint64_t> refused = an::checkStackAccumulatorSet(model, missing);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(refused.status(), "sums")) << refused.status().message();
    }
    {
        an::StackAccumulatorSet shortCounts = owned.set();
        shortCounts.counts = shortCounts.counts.subspan(1u);
        const an::Result<std::uint64_t> refused = an::checkStackAccumulatorSet(model, shortCounts);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(refused.status(), "counts")) << refused.status().message();
    }
    {
        an::StackAccumulatorSet shortMoments = owned.set();
        shortMoments.varianceMoments = shortMoments.varianceMoments.subspan(0u, static_cast<std::size_t>(kElements));
        const an::Result<std::uint64_t> refused = an::checkStackAccumulatorSet(model, shortMoments);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(refused.status(), "varianceMoments")) << refused.status().message();
    }
    {
        // A term the stack does not keep must not be offered: a caller that hands one over believes
        // it will be read, and it will not be.
        const an::StackModel uniform = makeModel(baseRequest());
        OwnedAccumulators plain = OwnedAccumulators::forModel(uniform);
        std::vector<double> extra(static_cast<std::size_t>(kWidth) * kHeight, 0.0);
        an::StackAccumulatorSet offered = plain.set();
        offered.weightSums = extra;
        const an::Result<std::uint64_t> refused = an::checkStackAccumulatorSet(uniform, offered);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(refused.status(), "weightSums")) << refused.status().message();
    }
    {
        // A term that does not begin on its own element boundary is refused rather than read.
        std::vector<std::byte> raw(static_cast<std::size_t>(kElements + 1u) * sizeof(double));
        an::StackAccumulatorSet unaligned = owned.set();
        unaligned.sums =
            std::span<double>(reinterpret_cast<double*>(raw.data() + 1u), static_cast<std::size_t>(kElements));
        const an::Result<std::uint64_t> refused = an::checkStackAccumulatorSet(model, unaligned);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(refused.status(), "sums")) << refused.status().message();
    }
}

TEST(StackAccumulatorsTest, RefusesABandThatDoesNotDescribeTheModel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request =
        baseRequest(kFrames, makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    const an::StackModel model = makeModel(request);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    const std::vector<double> weights(static_cast<std::size_t>(kFrames) * 3u, 1.0);

    const auto refusal = [&](const OwnedBand& band, std::span<const double> table) {
        return an::accumulateStackFrame(execution, model, owned.set(), band.band(), table).status();
    };

    {
        OwnedBand band = makeBand(0u, kWidth, kHeight, 3u);
        band.float64.assign(band.float32.begin(), band.float32.end());
        band.layout = planarLayout(an::SampleType::Float64, kWidth, kHeight, 3u);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "Float32")) << status.message();
    }
    {
        OwnedBand band = makeBand(kFrames, kWidth, kHeight, 3u);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::OutOfRange);
        EXPECT_TRUE(messageNames(status, "frameIndex")) << status.message();
        EXPECT_EQ(countField(status, an::ContextKey::Requested), kFrames);
        EXPECT_EQ(countField(status, an::ContextKey::Limit), kFrames);
    }
    {
        OwnedBand band = makeBand(0u, kWidth, 2u, 3u, kHeight - 1u);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::OutOfRange);
        EXPECT_TRUE(messageNames(status, "firstOutputRow")) << status.message();
    }
    {
        OwnedBand band = makeBand(0u, kWidth, kHeight, 1u);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "channel")) << status.message();
    }
    {
        OwnedBand band = makeBand(0u, kWidth, kHeight, 3u);
        band.addMask(kWidth, kHeight, 3u);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::InvalidArgument);
        EXPECT_TRUE(messageNames(status, "mask")) << status.message();
    }
    {
        OwnedBand band = makeBand(0u, kWidth, kHeight, 3u);
        band.descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::InvalidArgument);
        EXPECT_TRUE(messageNames(status, "mask")) << status.message();
    }
    {
        OwnedBand band = makeBand(0u, kWidth, kHeight, 3u);
        band.descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        band.addMask(kWidth - 1u, kHeight, 3u);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "mask")) << status.message();
    }
    {
        OwnedBand band = makeBand(0u, kWidth, kHeight, 3u);
        band.localWeights.assign(static_cast<std::size_t>(kWidth) * kHeight, 1.0f);
        const an::Status status = refusal(band, weights);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "localWeights")) << status.message();
    }
    {
        const OwnedBand band = makeBand(0u, kWidth, kHeight, 3u);
        const std::vector<double> shortTable(static_cast<std::size_t>(kFrames) * 3u - 1u, 1.0);
        const an::Status status = refusal(band, shortTable);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "frameWeights")) << status.message();
    }
    {
        const OwnedBand band = makeBand(1u, kWidth, kHeight, 3u);
        std::vector<double> poisoned = weights;
        poisoned[1u * 3u + 2u] = std::numeric_limits<double>::quiet_NaN();
        const an::Status status = refusal(band, poisoned);
        EXPECT_EQ(status.code(), an::ErrorCode::InvalidArgument);
        EXPECT_TRUE(messageNames(status, "frameWeights")) << status.message();
        EXPECT_EQ(countField(status, an::ContextKey::Requested), 5u);
    }
    EXPECT_EQ(owned.counts[0], 0u);
    EXPECT_EQ(owned.sums[0], 0.0);
}

TEST(StackAccumulatorsTest, AccumulatesAUniformAverageAndFinalizesTheMean)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = baseRequest();
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight;
    EXPECT_TRUE(owned.weightSums.empty());
    EXPECT_TRUE(owned.varianceMoments.empty());

    std::vector<double> expected(static_cast<std::size_t>(kElements), 0.0);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesAccumulated, kElements);
        EXPECT_EQ(outcome->samplesExcludedByValidity, 0u);
        EXPECT_EQ(outcome->samplesExcludedByWeight, 0u);
        EXPECT_EQ(outcome->elementsCovered, kElements);
        for (std::size_t index = 0u; index < expected.size(); ++index)
        {
            expected[index] += static_cast<double>(band.float32[index]) * 1.0;
        }
    }
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        EXPECT_EQ(owned.sums[index], expected[index]) << index;
        EXPECT_EQ(owned.counts[index], kFrames) << index;
    }

    std::vector<std::byte> output(static_cast<std::size_t>(kElements) * sizeof(float));
    std::vector<std::uint8_t> maskBits(an::packedMaskRowBytes(kWidth) * kHeight, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> mask = an::ValidityMaskView<std::uint8_t>::createTight(
        std::span<std::uint8_t>(maskBits), model.outputDescriptor().extent());
    ASSERT_TRUE(mask.has_value());
    std::vector<std::uint32_t> coverage(static_cast<std::size_t>(kElements), 99u);
    const an::Result<an::StackFinalizeOutcome> finalized =
        an::finalizeStackAccumulators(execution, model, owned.set(),
                                      an::StackFinalizeTargets{.outputSampleType = an::SampleType::Float32,
                                                               .outputImage = output,
                                                               .outputMask = &*mask,
                                                               .coverage = coverage});
    ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
    EXPECT_EQ(finalized->elementsWritten, kElements);
    EXPECT_EQ(finalized->elementsWithSamples, kElements);
    EXPECT_EQ(finalized->elementsWithoutSamples, 0u);
    EXPECT_EQ(finalized->nonFiniteOutputs, 0u);
    const std::vector<double> written = outputAsDoubles(output, an::SampleType::Float32);
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        EXPECT_EQ(written[index], static_cast<double>(static_cast<float>(expected[index] / kFrames))) << index;
        EXPECT_EQ(coverage[index], kFrames) << index;
        EXPECT_EQ(
            mask->test(0u, static_cast<std::uint32_t>(index % kWidth), static_cast<std::uint32_t>(index / kWidth)),
            std::optional<bool>{true})
            << index;
    }
}

TEST(StackAccumulatorsTest, WeightsFramesPerChannelAndAppliesALocalSurface)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request =
        baseRequest(kFrames, makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    request.localWeighting.enabled = true;
    request.output.coverageMap = true;
    request.output.signalToNoiseGainMap = true;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    constexpr std::uint32_t kChannels = 3u;
    constexpr std::uint64_t kPixels = static_cast<std::uint64_t>(kWidth) * kHeight;
    constexpr std::uint64_t kElements = kPixels * kChannels;
    ASSERT_EQ(owned.weightSums.size(), kElements);
    ASSERT_EQ(owned.weightSquareSums.size(), kElements);

    // Frame one's blue channel weighs nothing, and one pixel of every frame's surface is zero.
    std::vector<double> weights(static_cast<std::size_t>(kFrames) * kChannels, 0.0);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
        {
            weights[frame * kChannels + channel] = 0.5 + 0.25 * frame + 0.125 * channel;
        }
    }
    weights[1u * kChannels + 2u] = 0.0;
    constexpr std::uint32_t kZeroPixel = 5u;

    std::vector<double> expectedSums(static_cast<std::size_t>(kElements), 0.0);
    std::vector<double> expectedWeights(static_cast<std::size_t>(kElements), 0.0);
    std::vector<double> expectedSquares(static_cast<std::size_t>(kElements), 0.0);
    std::vector<std::uint32_t> expectedCounts(static_cast<std::size_t>(kElements), 0u);
    std::uint64_t expectedExcluded = 0u;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        OwnedBand band = makeBand(frame, kWidth, kHeight, kChannels);
        band.localWeights.resize(static_cast<std::size_t>(kPixels));
        for (std::uint32_t pixel = 0u; pixel < kPixels; ++pixel)
        {
            band.localWeights[pixel] = pixel == kZeroPixel ? 0.0f : 0.5f + 0.01f * static_cast<float>(pixel);
        }
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), weights);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
        {
            for (std::uint32_t pixel = 0u; pixel < kPixels; ++pixel)
            {
                const std::size_t element = static_cast<std::size_t>(channel) * kPixels + pixel;
                const double weight =
                    weights[frame * kChannels + channel] * static_cast<double>(band.localWeights[pixel]);
                if (!(weight > 0.0))
                {
                    ++expectedExcluded;
                    continue;
                }
                const double value = static_cast<double>(band.float32[element]);
                expectedSums[element] += value * weight;
                expectedWeights[element] += weight;
                expectedSquares[element] += weight * weight;
                ++expectedCounts[element];
            }
        }
        EXPECT_EQ(outcome->samplesExcludedByValidity, 0u);
        EXPECT_EQ(outcome->samplesAccumulated + outcome->samplesExcludedByWeight, kElements);
    }
    std::uint64_t excluded = 0u;
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(owned.sums[element], expectedSums[element]) << element;
        EXPECT_EQ(owned.weightSums[element], expectedWeights[element]) << element;
        EXPECT_EQ(owned.weightSquareSums[element], expectedSquares[element]) << element;
        EXPECT_EQ(owned.counts[element], expectedCounts[element]) << element;
        excluded += kFrames - expectedCounts[element];
    }
    EXPECT_EQ(excluded, expectedExcluded);
    EXPECT_EQ(owned.counts[2u * kPixels + 0u], kFrames - 1u);
    EXPECT_EQ(owned.counts[kZeroPixel], 0u);

    std::vector<std::byte> output(static_cast<std::size_t>(kElements) * sizeof(float));
    std::vector<std::uint32_t> coverage(static_cast<std::size_t>(kElements), 0u);
    std::vector<float> gain(static_cast<std::size_t>(kElements), 0.0f);
    std::vector<float> weightSum(static_cast<std::size_t>(kElements), 0.0f);
    const an::Result<an::StackFinalizeOutcome> finalized =
        an::finalizeStackAccumulators(execution, model, owned.set(),
                                      an::StackFinalizeTargets{.outputSampleType = an::SampleType::Float32,
                                                               .outputImage = output,
                                                               .coverage = coverage,
                                                               .signalToNoiseGain = gain,
                                                               .weightSum = weightSum});
    ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
    EXPECT_EQ(finalized->elementsWithoutSamples, kChannels);
    const std::vector<double> written = outputAsDoubles(output, an::SampleType::Float32);
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        if (expectedCounts[element] == 0u)
        {
            EXPECT_EQ(written[element], 0.0) << element;
            EXPECT_EQ(gain[element], 0.0f) << element;
            EXPECT_EQ(weightSum[element], 0.0f) << element;
            continue;
        }
        EXPECT_EQ(written[element],
                  static_cast<double>(static_cast<float>(expectedSums[element] / expectedWeights[element])))
            << element;
        EXPECT_EQ(coverage[element], expectedCounts[element]) << element;
        EXPECT_EQ(weightSum[element], static_cast<float>(expectedWeights[element])) << element;
        EXPECT_EQ(gain[element],
                  static_cast<float>(expectedWeights[element] * expectedWeights[element] / expectedSquares[element]))
            << element;
    }
}

TEST(StackAccumulatorsTest, AppliesEveryValidityRuleAndRefusesAnUndeclaredNonFiniteSample)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(
        baseRequest(kFrames, makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb)));
    constexpr std::uint32_t kChannels = 3u;
    constexpr std::uint64_t kPixels = static_cast<std::uint64_t>(kWidth) * kHeight;
    constexpr std::uint64_t kElements = kPixels * kChannels;
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    const float kInf = std::numeric_limits<float>::infinity();

    const auto accumulateOne = [&](OwnedBand& band, OwnedAccumulators& owned) {
        return an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
    };

    {
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        OwnedBand band = makeBand(0u, kWidth, kHeight, kChannels);
        band.descriptor = ruleOf(an::ValidityRule::FiniteOnly);
        band.float32[3] = kNaN;
        band.float32[kPixels + 4u] = kInf;
        band.float32[2u * kPixels + 5u] = -kInf;
        const an::Result<an::StackAccumulationOutcome> outcome = accumulateOne(band, owned);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesExcludedByValidity, 3u);
        EXPECT_EQ(outcome->samplesAccumulated, kElements - 3u);
        EXPECT_EQ(owned.counts[3], 0u);
        EXPECT_EQ(owned.counts[kPixels + 4u], 0u);
        EXPECT_EQ(owned.counts[2u * kPixels + 5u], 0u);
        EXPECT_EQ(owned.sums[3], 0.0);
        EXPECT_EQ(owned.counts[2], 1u);
    }
    {
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        OwnedBand band = makeBand(0u, kWidth, kHeight, kChannels);
        band.descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
        band.float32[7] = 0.0f;
        band.float32[kPixels + 1u] = -0.0f;
        const an::Result<an::StackAccumulationOutcome> outcome = accumulateOne(band, owned);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesExcludedByValidity, 2u);
        EXPECT_EQ(owned.counts[7], 0u);
        EXPECT_EQ(owned.counts[kPixels + 1u], 0u);
    }
    {
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        OwnedBand band = makeBand(0u, kWidth, kHeight, kChannels);
        band.descriptor = ruleOf(an::ValidityRule::DomainRange, 0.105, 0.2);
        std::uint64_t inside = 0u;
        for (const float value : band.float32)
        {
            inside += (value >= 0.105f && value <= 0.2f) ? 1u : 0u;
        }
        const an::Result<an::StackAccumulationOutcome> outcome = accumulateOne(band, owned);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesAccumulated, inside);
        EXPECT_EQ(outcome->samplesExcludedByValidity, kElements - inside);
        EXPECT_EQ(owned.counts[0], 0u);
    }
    {
        // One bit-plane per channel: a cleared bit excludes one channel of one pixel.
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        OwnedBand band = makeBand(0u, kWidth, kHeight, kChannels);
        band.descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        band.addMask(kWidth, kHeight, kChannels);
        band.clearBit(1u, 6u, 2u);
        band.clearBit(2u, 0u, 0u);
        const an::Result<an::StackAccumulationOutcome> outcome = accumulateOne(band, owned);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesExcludedByValidity, 2u);
        EXPECT_EQ(owned.counts[kPixels + 2u * kWidth + 6u], 0u);
        EXPECT_EQ(owned.counts[2u * kWidth + 6u], 1u);
        EXPECT_EQ(owned.counts[2u * kPixels], 0u);
        EXPECT_EQ(owned.counts[0], 1u);
    }
    {
        // One shared bit-plane: a cleared bit excludes every channel of that pixel.
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        OwnedBand band = makeBand(0u, kWidth, kHeight, kChannels);
        band.descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        band.addMask(kWidth, kHeight, 1u);
        band.clearBit(0u, 7u, 3u);
        const an::Result<an::StackAccumulationOutcome> outcome = accumulateOne(band, owned);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesExcludedByValidity, kChannels);
        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
        {
            EXPECT_EQ(owned.counts[channel * kPixels + 3u * kWidth + 7u], 0u) << channel;
        }
    }
    {
        // Under the all-valid rule a non-finite sample is a claim the frame did not keep.
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        OwnedBand band = makeBand(0u, kWidth, kHeight, kChannels);
        band.float32[kPixels + 9u] = kNaN;
        const an::Result<an::StackAccumulationOutcome> outcome = accumulateOne(band, owned);
        EXPECT_EQ(outcome.status().code(), an::ErrorCode::InvalidData);
        EXPECT_TRUE(messageNames(outcome.status(), "finite")) << outcome.status().message();
    }
}

TEST(StackAccumulatorsTest, KeepsTheExtremaAndLeavesUnreachedElementsClear)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight;

    for (const an::StackCombination combination : {an::StackCombination::Maximum, an::StackCombination::Minimum})
    {
        an::StackRequest request = baseRequest();
        request.combination = combination;
        request.output.coverageMap = true;
        const an::StackModel model = makeModel(request);
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        EXPECT_TRUE(owned.weightSums.empty());

        std::vector<double> expected(static_cast<std::size_t>(kElements), 0.0);
        std::vector<std::uint32_t> expectedCounts(static_cast<std::size_t>(kElements), 0u);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
            band.descriptor = ruleOf(an::ValidityRule::ExplicitMask);
            band.addMask(kWidth, kHeight, 1u);
            // The first pixel is never reached; the second is reached by frame one alone, whose
            // sample is negative and is kept as it is rather than clamped.
            band.clearBit(0u, 0u, 0u);
            if (frame != 1u)
            {
                band.clearBit(0u, 1u, 0u);
            }
            band.float32[1] = -0.25f * static_cast<float>(frame + 1u);
            // Frame two dips below and frame zero rises above the others at two more pixels.
            band.float32[2] = frame == 2u ? -3.0f : 0.5f;
            band.float32[3] = frame == 0u ? 7.0f : 0.5f;
            const an::Result<an::StackAccumulationOutcome> outcome =
                an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
            for (std::size_t element = 0u; element < kElements; ++element)
            {
                if (element == 0u || (element == 1u && frame != 1u))
                {
                    continue;
                }
                const double value = static_cast<double>(band.float32[element]);
                expected[element] = expectedCounts[element] == 0u ? value
                                                                  : (combination == an::StackCombination::Maximum
                                                                         ? (std::max)(expected[element], value)
                                                                         : (std::min)(expected[element], value));
                ++expectedCounts[element];
            }
        }
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            EXPECT_EQ(owned.sums[element], expected[element]) << element;
            EXPECT_EQ(owned.counts[element], expectedCounts[element]) << element;
        }
        EXPECT_EQ(owned.counts[0], 0u);
        EXPECT_EQ(owned.counts[1], 1u);
        EXPECT_EQ(owned.sums[1], -0.5);
        EXPECT_EQ(owned.sums[2], combination == an::StackCombination::Minimum ? -3.0 : 0.5);
        EXPECT_EQ(owned.sums[3], combination == an::StackCombination::Maximum ? 7.0 : 0.5);

        std::vector<std::byte> output(static_cast<std::size_t>(kElements) * sizeof(float));
        std::vector<std::uint8_t> maskBits(an::packedMaskRowBytes(kWidth) * kHeight, 0xFFu);
        const std::optional<an::ValidityMaskView<std::uint8_t>> mask = an::ValidityMaskView<std::uint8_t>::createTight(
            std::span<std::uint8_t>(maskBits), model.outputDescriptor().extent());
        ASSERT_TRUE(mask.has_value());
        std::vector<std::uint32_t> coverage(static_cast<std::size_t>(kElements), 0u);
        const an::Result<an::StackFinalizeOutcome> finalized =
            an::finalizeStackAccumulators(execution, model, owned.set(),
                                          an::StackFinalizeTargets{.outputSampleType = an::SampleType::Float32,
                                                                   .outputImage = output,
                                                                   .outputMask = &*mask,
                                                                   .coverage = coverage});
        ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
        EXPECT_EQ(finalized->elementsWithoutSamples, 1u);
        EXPECT_EQ(finalized->elementsWithSamples, kElements - 1u);
        const std::vector<double> written = outputAsDoubles(output, an::SampleType::Float32);
        EXPECT_EQ(written[0], 0.0);
        EXPECT_EQ(mask->test(0u, 0u, 0u), std::optional<bool>{false});
        EXPECT_EQ(coverage[0], 0u);
        EXPECT_EQ(written[1], -0.5);
        EXPECT_EQ(mask->test(0u, 1u, 0u), std::optional<bool>{true});
        EXPECT_EQ(coverage[1], 1u);
        EXPECT_EQ(written[2], combination == an::StackCombination::Minimum ? -3.0 : 0.5);
        EXPECT_EQ(written[3], combination == an::StackCombination::Maximum ? 7.0 : 0.5);
    }
}

TEST(StackAccumulatorsTest, TracksVarianceMomentsAndWritesTheSampleVariance)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = baseRequest(5u);
    request.output.varianceMap = true;
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight;
    ASSERT_EQ(owned.varianceMoments.size(), 2u * kElements);

    std::vector<std::vector<double>> samples(static_cast<std::size_t>(kElements));
    Lcg random(11u);
    for (std::uint32_t frame = 0u; frame < 5u; ++frame)
    {
        OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
        band.descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        band.addMask(kWidth, kHeight, 1u);
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            band.float32[element] = static_cast<float>(random.unit() * 2.0 - 0.5);
        }
        // The first pixel receives one sample in all, and the second none at all.
        if (frame != 2u)
        {
            band.clearBit(0u, 0u, 0u);
        }
        band.clearBit(0u, 1u, 0u);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            if ((element == 0u && frame != 2u) || element == 1u)
            {
                continue;
            }
            samples[element].push_back(static_cast<double>(band.float32[element]));
        }
    }

    std::vector<float> variance(static_cast<std::size_t>(kElements), -1.0f);
    std::vector<std::uint32_t> coverage(static_cast<std::size_t>(kElements), 0u);
    std::vector<std::byte> output(static_cast<std::size_t>(kElements) * sizeof(float));
    const an::Result<an::StackFinalizeOutcome> finalized =
        an::finalizeStackAccumulators(execution, model, owned.set(),
                                      an::StackFinalizeTargets{.outputSampleType = an::SampleType::Float32,
                                                               .outputImage = output,
                                                               .coverage = coverage,
                                                               .variance = variance});
    ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        const std::vector<double>& column = samples[element];
        EXPECT_EQ(coverage[element], column.size()) << element;
        if (column.size() < 2u)
        {
            EXPECT_EQ(variance[element], 0.0f) << element;
            EXPECT_EQ(owned.varianceMoments[2u * element + 1u], 0.0) << element;
            continue;
        }
        long double mean = 0.0L;
        for (const double value : column)
        {
            mean += value;
        }
        mean /= static_cast<long double>(column.size());
        long double moment = 0.0L;
        for (const double value : column)
        {
            moment += (value - mean) * (value - mean);
        }
        const long double expected = moment / static_cast<long double>(column.size() - 1u);
        EXPECT_NEAR(static_cast<double>(owned.varianceMoments[2u * element]), static_cast<double>(mean), 1e-14)
            << element;
        EXPECT_NEAR(static_cast<double>(owned.varianceMoments[2u * element + 1u]), static_cast<double>(moment), 1e-13)
            << element;
        EXPECT_NEAR(static_cast<double>(variance[element]), static_cast<double>(expected),
                    1e-6 * static_cast<double>(expected))
            << element;
    }
    EXPECT_EQ(coverage[0], 1u);
    EXPECT_EQ(coverage[1], 0u);
}

TEST(StackAccumulatorsTest, WritesTheRawWeightedSumForASumStack)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const an::SampleType outputType : {an::SampleType::Float32, an::SampleType::Float64})
    {
        an::StackRequest request = baseRequest();
        request.combination = an::StackCombination::Sum;
        request.weighting.method = an::StackWeighting::Explicit;
        request.output.sampleType = outputType;
        const an::StackModel model = makeModel(request);
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight;
        const std::array<double, kFrames> weights{2.0, 3.0, 5.0};

        std::vector<double> expected(static_cast<std::size_t>(kElements), 0.0);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            const OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
            const an::Result<an::StackAccumulationOutcome> outcome =
                an::accumulateStackFrame(execution, model, owned.set(), band.band(), weights);
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
            for (std::size_t element = 0u; element < kElements; ++element)
            {
                expected[element] += static_cast<double>(band.float32[element]) * weights[frame];
            }
        }
        std::vector<std::byte> output(static_cast<std::size_t>(kElements) * an::sampleTypeSizeBytes(outputType));
        const an::Result<an::StackFinalizeOutcome> finalized = an::finalizeStackAccumulators(
            execution, model, owned.set(),
            an::StackFinalizeTargets{.outputSampleType = outputType, .outputImage = output});
        ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
        const std::vector<double> written = outputAsDoubles(output, outputType);
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            const double narrowed = outputType == an::SampleType::Float32
                                        ? static_cast<double>(static_cast<float>(expected[element]))
                                        : expected[element];
            EXPECT_EQ(written[element], narrowed) << an::sampleTypeName(outputType) << ' ' << element;
        }
    }
}

TEST(StackAccumulatorsTest, PlacesRowBandsAtTheirOutputRowsAndReadsAnInterleavedBand)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kChannels = 3u;
    constexpr std::uint32_t kRows = 6u;
    const an::StackModel model =
        makeModel(baseRequest(2u, makeDescriptor(an::SampleType::Float32, kWidth, kRows, an::ChannelSchema::Rgb)));
    constexpr std::uint64_t kPixels = static_cast<std::uint64_t>(kWidth) * kRows;

    OwnedAccumulators whole = OwnedAccumulators::forModel(model);
    OwnedAccumulators banded = OwnedAccumulators::forModel(model);
    OwnedAccumulators interleaved = OwnedAccumulators::forModel(model);
    for (std::uint32_t frame = 0u; frame < 2u; ++frame)
    {
        const OwnedBand full = makeBand(frame, kWidth, kRows, kChannels);
        ASSERT_TRUE(an::accumulateStackFrame(execution, model, whole.set(), full.band(), {}).hasValue());

        const OwnedBand upper = makeBand(frame, kWidth, 4u, kChannels, 0u);
        const OwnedBand lower = makeBand(frame, kWidth, 2u, kChannels, 4u);
        ASSERT_TRUE(an::accumulateStackFrame(execution, model, banded.set(), upper.band(), {}).hasValue());
        const an::Result<an::StackAccumulationOutcome> lowerOutcome =
            an::accumulateStackFrame(execution, model, banded.set(), lower.band(), {});
        ASSERT_TRUE(lowerOutcome.hasValue()) << lowerOutcome.status().message();
        EXPECT_EQ(lowerOutcome->elementsCovered, static_cast<std::uint64_t>(kWidth) * 2u * kChannels);

        OwnedBand mixed;
        mixed.frameIndex = frame;
        mixed.layout = interleavedLayout(an::SampleType::Float32, kWidth, kRows, kChannels);
        mixed.float32.resize(static_cast<std::size_t>(kPixels) * kChannels);
        for (std::uint32_t pixel = 0u; pixel < kPixels; ++pixel)
        {
            for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
            {
                mixed.float32[static_cast<std::size_t>(pixel) * kChannels + channel] =
                    full.float32[static_cast<std::size_t>(channel) * kPixels + pixel];
            }
        }
        const an::Result<an::StackAccumulationOutcome> mixedOutcome =
            an::accumulateStackFrame(execution, model, interleaved.set(), mixed.band(), {});
        ASSERT_TRUE(mixedOutcome.hasValue()) << mixedOutcome.status().message();
        EXPECT_FALSE(mixedOutcome->compiledRowUsed);
    }
    EXPECT_EQ(whole.bytes(), banded.bytes());
    EXPECT_EQ(whole.bytes(), interleaved.bytes());
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        const std::size_t element = static_cast<std::size_t>(channel) * kPixels + 5u * kWidth + 3u;
        EXPECT_EQ(whole.sums[element], static_cast<double>(sampleAt(0u, channel, 5u, 3u, kWidth)) +
                                           static_cast<double>(sampleAt(1u, channel, 5u, 3u, kWidth)));
        EXPECT_EQ(whole.counts[element], 2u);
    }
}

TEST(StackAccumulatorsTest, AccumulatesTheSameBitsAtEveryThreadCount)
{
    constexpr std::uint32_t kWide = 257u;
    constexpr std::uint32_t kTall = 33u;
    an::StackRequest request =
        baseRequest(kFrames, makeDescriptor(an::SampleType::Float32, kWide, kTall, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<double> weights{0.7, 1.1, 0.9, 1.3, 0.8, 1.0, 1.2, 0.6, 1.4};

    std::optional<std::vector<std::byte>> first;
    for (const std::uint32_t workers : {1u, 2u, 4u})
    {
        const an::EngineContext engine = makeEngine(4u);
        const an::ExecutionContext execution = makeExecution(engine, workers);
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            OwnedBand band = makeBand(frame, kWide, kTall, 3u);
            band.descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
            band.float32[frame * 97u] = 0.0f;
            const an::Result<an::StackAccumulationOutcome> outcome =
                an::accumulateStackFrame(execution, model, owned.set(), band.band(), weights);
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        }
        std::vector<std::byte> bytes = owned.bytes();
        if (!first.has_value())
        {
            first = std::move(bytes);
            continue;
        }
        EXPECT_EQ(bytes, *first) << workers << " workers";
    }
}

TEST(StackAccumulatorsTest, AccumulatesEveryBandOfATileInOnePassBitForBit)
{
    // The frames of one tile, folded into the terms by one call whose frame loop sits inside the
    // row loop: the same bits as one call per frame, at any worker count, with each band's
    // admitted count reported on its own and the totals over all of them.
    constexpr std::uint32_t kWide = 257u;
    constexpr std::uint32_t kTall = 33u;
    an::StackRequest request =
        baseRequest(kFrames, makeDescriptor(an::SampleType::Float32, kWide, kTall, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<double> weights{0.7, 1.1, 0.9, 1.3, 0.8, 1.0, 1.2, 0.6, 1.4};
    std::vector<OwnedBand> bands;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        bands.push_back(makeBand(frame, kWide, kTall, 3u));
        bands.back().descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
        bands.back().float32[frame * 97u] = 0.0f;
        bands.back().float32[frame * 131u + 5u] = 0.0f;
    }
    std::vector<an::StackFrameBand> records;
    for (const OwnedBand& band : bands)
    {
        records.push_back(band.band());
    }

    const an::EngineContext engine = makeEngine(4u);
    OwnedAccumulators sequential = OwnedAccumulators::forModel(model);
    std::vector<std::uint64_t> sequentialAdmitted(kFrames, 0u);
    an::StackAccumulationOutcome sequentialTotal;
    {
        const an::ExecutionContext execution = makeExecution(engine, 2u);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            const an::Result<an::StackAccumulationOutcome> outcome =
                an::accumulateStackFrame(execution, model, sequential.set(), records[frame], weights);
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
            sequentialAdmitted[frame] = outcome->samplesAccumulated + outcome->samplesExcludedByWeight;
            sequentialTotal.samplesAccumulated += outcome->samplesAccumulated;
            sequentialTotal.samplesExcludedByValidity += outcome->samplesExcludedByValidity;
            sequentialTotal.samplesExcludedByWeight += outcome->samplesExcludedByWeight;
        }
    }
    EXPECT_EQ(sequentialTotal.samplesExcludedByValidity, 2u * kFrames);

    for (const std::uint32_t workers : {1u, 4u})
    {
        const an::ExecutionContext execution = makeExecution(engine, workers);
        OwnedAccumulators batched = OwnedAccumulators::forModel(model);
        std::vector<std::uint64_t> admitted(kFrames, 7u);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrames(execution, model, batched.set(), records, weights, admitted);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(batched.bytes(), sequential.bytes()) << workers << " workers";
        EXPECT_EQ(outcome->samplesAccumulated, sequentialTotal.samplesAccumulated);
        EXPECT_EQ(outcome->samplesExcludedByValidity, sequentialTotal.samplesExcludedByValidity);
        EXPECT_EQ(outcome->samplesExcludedByWeight, sequentialTotal.samplesExcludedByWeight);
        EXPECT_EQ(outcome->elementsCovered, static_cast<std::uint64_t>(kWide) * kTall * 3u);
        EXPECT_TRUE(outcome->compiledRowUsed);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            // Added to what the entry held, which is how an executor accumulates over tiles.
            EXPECT_EQ(admitted[frame], 7u + sequentialAdmitted[frame]) << frame;
        }
    }

    // No bands is nothing accumulated, and every refusal names what disagrees before a term is
    // written: a band of other rows, an admitted span of the wrong length, and a band the
    // single-frame checks would refuse.
    {
        const an::ExecutionContext execution = makeExecution(engine, 2u);
        OwnedAccumulators untouched = OwnedAccumulators::forModel(model);
        std::vector<std::uint64_t> admitted(kFrames, 0u);
        const an::Result<an::StackAccumulationOutcome> none =
            an::accumulateStackFrames(execution, model, untouched.set(), {}, weights, {});
        ASSERT_TRUE(none.hasValue()) << none.status().message();
        EXPECT_EQ(none->samplesAccumulated, 0u);
        EXPECT_EQ(none->elementsCovered, 0u);

        OwnedBand shorter = makeBand(1u, kWide, kTall - 1u, 3u);
        shorter.descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
        std::vector<an::StackFrameBand> mixed{records[0], shorter.band(), records[2]};
        const an::Result<an::StackAccumulationOutcome> rows =
            an::accumulateStackFrames(execution, model, untouched.set(), mixed, weights, admitted);
        ASSERT_FALSE(rows.hasValue());
        EXPECT_EQ(rows.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_NE(std::string_view(rows.status().message()).find("bands"), std::string_view::npos);

        std::vector<std::uint64_t> fewer(kFrames - 1u, 0u);
        const an::Result<an::StackAccumulationOutcome> span =
            an::accumulateStackFrames(execution, model, untouched.set(), records, weights, fewer);
        ASSERT_FALSE(span.hasValue());
        EXPECT_EQ(span.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_NE(std::string_view(span.status().message()).find("admittedPerBand"), std::string_view::npos);

        std::vector<an::StackFrameBand> wide = records;
        wide[1].sampleType = an::SampleType::Float64;
        const an::Result<an::StackAccumulationOutcome> type =
            an::accumulateStackFrames(execution, model, untouched.set(), wide, weights, admitted);
        ASSERT_FALSE(type.hasValue());
        EXPECT_NE(std::string_view(type.status().message()).find("band.sampleType"), std::string_view::npos);

        EXPECT_EQ(untouched.bytes(), OwnedAccumulators::forModel(model).bytes());
        for (const std::uint64_t count : admitted)
        {
            EXPECT_EQ(count, 0u);
        }
    }
}

TEST(StackAccumulatorsTest, CompiledRowsStoreTheReferenceBits)
{
    const std::vector<an::InstructionSet> targets = compiledTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this build and host have no compiled accumulator row to compare with the reference";
    }
    constexpr std::array<std::uint32_t, 18u> kWidths{1u,  2u,  3u,  4u,  5u,  7u,  8u,  9u,  15u,
                                                     16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 257u};
    constexpr std::array<an::ValidityRule, 5u> kRules{an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly,
                                                      an::ValidityRule::ZeroIsNoData, an::ValidityRule::ExplicitMask,
                                                      an::ValidityRule::DomainRange};
    const an::EngineContext engine = makeEngine(4u);
    Lcg random(0x5EEDu);
    std::uint64_t compared = 0u;
    std::uint64_t refusedAlike = 0u;

    for (std::uint32_t caseIndex = 0u; caseIndex < 96u; ++caseIndex)
    {
        const std::uint32_t width = kWidths[random.below(static_cast<std::uint32_t>(kWidths.size()))];
        const std::uint32_t rows = 1u + random.below(3u);
        const std::uint32_t channels = random.below(2u) == 0u ? 1u : 3u;
        const an::ValidityRule rule = kRules[random.below(static_cast<std::uint32_t>(kRules.size()))];
        const bool wide = random.below(4u) == 0u;
        const std::uint32_t kind = random.below(4u);
        const bool weighted = kind < 2u && random.below(2u) == 0u;
        const bool local = weighted && random.below(2u) == 0u;
        const bool variance = random.below(2u) == 0u;
        const bool specials =
            rule == an::ValidityRule::FiniteOnly || (rule == an::ValidityRule::AllValid && random.below(8u) == 0u);

        an::StackRequest request =
            baseRequest(2u, makeDescriptor(wide ? an::SampleType::Float64 : an::SampleType::Float32, width, rows,
                                           channels == 3u ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono));
        request.combination = kind == 0u   ? an::StackCombination::Average
                              : kind == 1u ? an::StackCombination::Sum
                              : kind == 2u ? an::StackCombination::Minimum
                                           : an::StackCombination::Maximum;
        request.weighting.method = weighted ? an::StackWeighting::Explicit : an::StackWeighting::Uniform;
        request.localWeighting.enabled = local;
        request.output.varianceMap = variance;
        request.output.signalToNoiseGainMap = weighted;
        const an::StackModel model = makeModel(request);
        const std::uint64_t pixels = static_cast<std::uint64_t>(width) * rows;

        std::vector<double> weights(static_cast<std::size_t>(2u * channels), 1.0);
        for (double& weight : weights)
        {
            weight = random.below(9u) == 0u ? 0.0 : 0.25 + random.unit();
        }
        std::vector<OwnedBand> frames;
        for (std::uint32_t frame = 0u; frame < 2u; ++frame)
        {
            OwnedBand band;
            band.frameIndex = frame;
            band.descriptor = ruleOf(rule, 0.1, 0.9);
            band.layout = planarLayout(wide ? an::SampleType::Float64 : an::SampleType::Float32, width, rows, channels);
            const std::size_t count = static_cast<std::size_t>(pixels) * channels;
            const auto sample = [&random, specials]() -> double {
                const std::uint32_t pick = random.below(24u);
                if (pick == 0u)
                {
                    return 0.0;
                }
                if (specials && pick == 1u)
                {
                    return std::numeric_limits<double>::quiet_NaN();
                }
                if (specials && pick == 2u)
                {
                    return std::numeric_limits<double>::infinity();
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
            if (local)
            {
                band.localWeights.resize(static_cast<std::size_t>(pixels));
                for (float& weight : band.localWeights)
                {
                    weight = random.below(7u) == 0u ? 0.0f : static_cast<float>(random.unit() * 2.0);
                }
            }
            frames.push_back(std::move(band));
        }

        const auto runOn = [&](an::ComputeBackend backend, an::InstructionSet set, OwnedAccumulators& owned,
                               bool& refused, an::ErrorCode& code, bool& compiled) {
            const an::ExecutionContext execution = forcedExecution(engine, backend, set, 1u + random.below(3u));
            refused = false;
            code = an::ErrorCode::Ok;
            compiled = false;
            for (const OwnedBand& band : frames)
            {
                const an::Result<an::StackAccumulationOutcome> outcome =
                    an::accumulateStackFrame(execution, model, owned.set(), band.band(), weights);
                if (!outcome.hasValue())
                {
                    refused = true;
                    code = outcome.status().code();
                    return;
                }
                EXPECT_EQ(outcome->backend.backend, backend);
                EXPECT_EQ(outcome->backend.instructionSet, set);
                compiled = outcome->compiledRowUsed;
            }
        };

        OwnedAccumulators reference = OwnedAccumulators::forModel(model);
        bool referenceRefused = false;
        an::ErrorCode referenceCode = an::ErrorCode::Ok;
        bool referenceCompiled = false;
        runOn(an::ComputeBackend::Reference, an::InstructionSet::Scalar, reference, referenceRefused, referenceCode,
              referenceCompiled);
        EXPECT_FALSE(referenceCompiled);
        const std::vector<std::byte> expected = reference.bytes();

        for (const an::InstructionSet target : targets)
        {
            OwnedAccumulators candidate = OwnedAccumulators::forModel(model);
            bool candidateRefused = false;
            an::ErrorCode candidateCode = an::ErrorCode::Ok;
            bool candidateCompiled = false;
            runOn(an::ComputeBackend::Cpu, target, candidate, candidateRefused, candidateCode, candidateCompiled);
            EXPECT_EQ(candidateRefused, referenceRefused) << "case " << caseIndex;
            EXPECT_EQ(candidateCode, referenceCode) << "case " << caseIndex;
            if (referenceRefused)
            {
                ++refusedAlike;
                continue;
            }
            EXPECT_TRUE(candidateCompiled) << "case " << caseIndex;
            EXPECT_EQ(candidate.bytes(), expected)
                << "case " << caseIndex << ": " << width << 'x' << rows << " channels " << channels << " on "
                << an::instructionSetName(target);
            ++compared;
        }
    }
    std::cout << "compared " << compared << " compiled accumulator runs with the reference, " << refusedAlike
              << " refused alike" << std::endl;
    EXPECT_GT(compared, 0u);
}

TEST(StackAccumulatorsTest, AccumulatesEveryBandOfAPlainRowTogetherBitForBit)
{
    // A plain run - an unweighted sum, every band planar and unmasked under one rule - takes every
    // band of a row in one pass that keeps a block of terms in registers. Each element still takes
    // its samples in band order, so the terms, every band's admitted count and the totals are the
    // reference's bits, on every compiled table, across a row's tail and past a group of 256 bands.
    const std::vector<an::InstructionSet> targets = compiledTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this build and host have no compiled accumulator row to compare with the reference";
    }
    constexpr std::array<an::ValidityRule, 4u> kRules{an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly,
                                                      an::ValidityRule::ZeroIsNoData, an::ValidityRule::DomainRange};
    const an::EngineContext engine = makeEngine(4u);
    Lcg random(0xBA2D5u);
    std::uint64_t compared = 0u;
    for (const std::uint32_t frames : {1u, 5u, 300u})
    {
        for (const an::ValidityRule rule : kRules)
        {
            for (const bool wide : {false, true})
            {
                const std::uint32_t width = 16u * 2u + 5u;
                const std::uint32_t rows = 3u;
                const an::StackModel model = makeModel(baseRequest(
                    frames, makeDescriptor(wide ? an::SampleType::Float64 : an::SampleType::Float32, width, rows)));
                std::vector<OwnedBand> owned;
                std::vector<an::StackFrameBand> bands;
                owned.reserve(frames);
                for (std::uint32_t frame = 0u; frame < frames; ++frame)
                {
                    OwnedBand band;
                    band.frameIndex = frame;
                    band.descriptor = ruleOf(rule, 0.1, 0.9);
                    band.layout =
                        planarLayout(wide ? an::SampleType::Float64 : an::SampleType::Float32, width, rows, 1u);
                    const std::size_t count = static_cast<std::size_t>(width) * rows;
                    const auto sample = [&random, rule]() -> double {
                        const std::uint32_t pick = random.below(24u);
                        if (pick == 0u)
                            return 0.0;
                        if (pick == 1u)
                            return -0.0;
                        if (rule == an::ValidityRule::FiniteOnly && pick == 2u)
                            return std::numeric_limits<double>::quiet_NaN();
                        if (rule == an::ValidityRule::FiniteOnly && pick == 3u)
                            return -std::numeric_limits<double>::infinity();
                        // Non-integer values, so an addition in another order would round differently.
                        return random.unit() * 1.5 - 0.25;
                    };
                    if (wide)
                    {
                        band.float64.resize(count);
                        for (double& value : band.float64)
                            value = sample();
                    }
                    else
                    {
                        band.float32.resize(count);
                        for (float& value : band.float32)
                            value = static_cast<float>(sample());
                    }
                    owned.push_back(std::move(band));
                }
                for (const OwnedBand& band : owned)
                    bands.push_back(band.band());

                const auto runOn = [&](const an::ExecutionContext& execution, OwnedAccumulators& terms,
                                       std::vector<std::uint64_t>& admitted) {
                    const an::Result<an::StackAccumulationOutcome> outcome =
                        an::accumulateStackFrames(execution, model, terms.set(), bands, {}, admitted);
                    EXPECT_TRUE(outcome.hasValue()) << outcome.status().message();
                    return outcome.hasValue() ? *outcome : an::StackAccumulationOutcome{};
                };
                OwnedAccumulators reference = OwnedAccumulators::forModel(model);
                std::vector<std::uint64_t> referenceAdmitted(frames, 3u);
                const an::StackAccumulationOutcome expected =
                    runOn(forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar, 2u),
                          reference, referenceAdmitted);
                EXPECT_FALSE(expected.compiledRowUsed);
                std::uint64_t admittedTotal = 0u;
                for (const std::uint64_t count : referenceAdmitted)
                    admittedTotal += count - 3u;
                EXPECT_EQ(admittedTotal, expected.samplesAccumulated);
                EXPECT_EQ(expected.samplesAccumulated + expected.samplesExcludedByValidity,
                          static_cast<std::uint64_t>(frames) * width * rows);
                // The reference takes the same grouped path, so it is held to sums and counts this test
                // takes itself, element by element in band order: a group that skips or repeats a band
                // is seen here whichever table runs.
                OwnedAccumulators own = OwnedAccumulators::forModel(model);
                std::vector<std::uint64_t> ownAdmitted(frames, 3u);
                for (std::uint32_t frame = 0u; frame < frames; ++frame)
                {
                    for (std::size_t element = 0u; element < own.sums.size(); ++element)
                    {
                        const double value =
                            wide ? owned[frame].float64[element] : static_cast<double>(owned[frame].float32[element]);
                        const bool admit = rule == an::ValidityRule::AllValid ||
                                           (rule == an::ValidityRule::FiniteOnly && std::isfinite(value)) ||
                                           (rule == an::ValidityRule::ZeroIsNoData && value != 0.0) ||
                                           (rule == an::ValidityRule::DomainRange && value >= 0.1 && value <= 0.9);
                        if (admit)
                        {
                            own.sums[element] += value;
                            ++own.counts[element];
                            ++ownAdmitted[frame];
                        }
                    }
                }
                EXPECT_EQ(reference.bytes(), own.bytes())
                    << frames << " frames, rule " << static_cast<int>(rule) << (wide ? ", Float64" : ", Float32");
                EXPECT_EQ(referenceAdmitted, ownAdmitted);
                for (const an::InstructionSet target : targets)
                {
                    for (const std::uint32_t workers : {1u, 3u})
                    {
                        OwnedAccumulators candidate = OwnedAccumulators::forModel(model);
                        std::vector<std::uint64_t> admitted(frames, 3u);
                        const an::StackAccumulationOutcome outcome = runOn(
                            forcedExecution(engine, an::ComputeBackend::Cpu, target, workers), candidate, admitted);
                        EXPECT_TRUE(outcome.compiledRowUsed);
                        EXPECT_EQ(candidate.bytes(), reference.bytes())
                            << frames << " frames, rule " << static_cast<int>(rule)
                            << (wide ? ", Float64" : ", Float32") << " on " << an::instructionSetName(target) << " at "
                            << workers << " workers";
                        EXPECT_EQ(admitted, referenceAdmitted);
                        EXPECT_EQ(outcome.samplesAccumulated, expected.samplesAccumulated);
                        EXPECT_EQ(outcome.samplesExcludedByValidity, expected.samplesExcludedByValidity);
                        ++compared;
                    }
                }
            }
        }
    }
    EXPECT_GT(compared, 0u);
}

TEST(StackAccumulatorsTest, APlainRowRefusesANonFiniteSampleItsRuleAdmits)
{
    // All valid admits every sample, so an infinity among many bands is an undeclared non-finite
    // sample: refused as it is one band at a time, and before any band of a later row.
    const an::EngineContext engine = makeEngine(2u);
    const an::StackModel model = makeModel(baseRequest(40u, makeDescriptor(an::SampleType::Float32, 37u, 2u)));
    std::vector<OwnedBand> owned;
    std::vector<an::StackFrameBand> bands;
    owned.reserve(40u);
    for (std::uint32_t frame = 0u; frame < 40u; ++frame)
    {
        owned.push_back(makeBand(frame, 37u, 2u, 1u));
        owned.back().descriptor = ruleOf(an::ValidityRule::AllValid);
    }
    owned[23].float32[30] = std::numeric_limits<float>::infinity();
    for (const OwnedBand& band : owned)
        bands.push_back(band.band());
    for (const an::ComputeBackend backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        an::ExecutionOptions options;
        options.backend = backend;
        const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue());
        OwnedAccumulators terms = OwnedAccumulators::forModel(model);
        std::vector<std::uint64_t> admitted(40u, 0u);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrames(*execution, model, terms.set(), bands, {}, admitted);
        ASSERT_FALSE(outcome.hasValue());
        EXPECT_EQ(outcome.status().code(), an::ErrorCode::InvalidData);
    }
}

TEST(StackAccumulatorsTest, RefusesANaNTheZeroRuleAdmitsOnEveryBackend)
{
    // Zero-is-no-data admits every sample but an exact zero, so a NaN under it is an admitted
    // sample that is not finite: the reference row refuses the band, and so must every compiled
    // row - the plain row an unweighted average takes every band of a row in, and the single-band
    // row an extreme takes - rather than leave the NaN out as if it were a zero. The NaN sits in
    // the first block of lanes on every instruction set, beside a zero and a negative zero the rule
    // does exclude, so the vector body judges it rather than the scalar tail.
    constexpr std::uint32_t kRowWidth = 37u;
    constexpr std::uint32_t kRows = 2u;
    constexpr std::uint32_t kCount = 5u;
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

    for (const an::StackCombination combination : {an::StackCombination::Average, an::StackCombination::Maximum})
    {
        for (const bool wide : {false, true})
        {
            const an::SampleType type = wide ? an::SampleType::Float64 : an::SampleType::Float32;
            an::StackRequest request = baseRequest(kCount, makeDescriptor(type, kRowWidth, kRows));
            request.combination = combination;
            const an::StackModel model = makeModel(request);
            std::vector<OwnedBand> owned;
            owned.reserve(kCount);
            for (std::uint32_t frame = 0u; frame < kCount; ++frame)
            {
                OwnedBand band = makeBand(frame, kRowWidth, kRows, 1u);
                band.descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
                if (wide)
                {
                    band.float64.assign(band.float32.begin(), band.float32.end());
                    band.float32.clear();
                    band.layout = planarLayout(an::SampleType::Float64, kRowWidth, kRows, 1u);
                }
                owned.push_back(std::move(band));
            }
            const auto put = [wide](OwnedBand& band, std::size_t index, double value) {
                if (wide)
                    band.float64[index] = value;
                else
                    band.float32[index] = static_cast<float>(value);
            };
            put(owned[0], 3u, -0.0);
            put(owned[2], 4u, 0.0);
            std::vector<an::StackFrameBand> bands;
            for (const OwnedBand& band : owned)
                bands.push_back(band.band());

            for (std::size_t index = 0u; index < executions.size(); ++index)
            {
                const an::ExecutionContext& execution = executions[index];
                SCOPED_TRACE(std::string(an::stackCombinationName(combination)) + (wide ? " Float64" : " Float32") +
                             " on execution " + std::to_string(index));
                // Without the NaN the stack runs, and a forced compiled table runs its compiled row.
                OwnedAccumulators clean = OwnedAccumulators::forModel(model);
                std::vector<std::uint64_t> cleanAdmitted(kCount, 0u);
                const an::Result<an::StackAccumulationOutcome> accumulated =
                    an::accumulateStackFrames(execution, model, clean.set(), bands, {}, cleanAdmitted);
                ASSERT_TRUE(accumulated.hasValue()) << accumulated.status().message();
                EXPECT_EQ(accumulated->samplesExcludedByValidity, 2u);
                if (index >= 2u)
                    EXPECT_TRUE(accumulated->compiledRowUsed);
            }

            put(owned[2], 1u, std::numeric_limits<double>::quiet_NaN());
            for (std::size_t index = 0u; index < executions.size(); ++index)
            {
                const an::ExecutionContext& execution = executions[index];
                SCOPED_TRACE(std::string(an::stackCombinationName(combination)) + (wide ? " Float64" : " Float32") +
                             " on execution " + std::to_string(index));
                OwnedAccumulators terms = OwnedAccumulators::forModel(model);
                std::vector<std::uint64_t> admitted(kCount, 0u);
                const an::Result<an::StackAccumulationOutcome> outcome =
                    an::accumulateStackFrames(execution, model, terms.set(), bands, {}, admitted);
                ASSERT_FALSE(outcome.hasValue()) << "the NaN was left out instead of refused";
                EXPECT_EQ(outcome.status().code(), an::ErrorCode::InvalidData);
                EXPECT_TRUE(messageNames(outcome.status(), "finite")) << outcome.status().message();
            }
        }
    }
}

TEST(StackAccumulatorsTest, RefusesAnAccumulatorThatOverflowsFloat64)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model =
        makeModel(baseRequest(kFrames, makeDescriptor(an::SampleType::Float64, kWidth, kHeight)));
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    for (std::uint32_t frame = 0u; frame < 2u; ++frame)
    {
        OwnedBand band;
        band.frameIndex = frame;
        band.layout = planarLayout(an::SampleType::Float64, kWidth, kHeight, 1u);
        band.float64.assign(static_cast<std::size_t>(kWidth) * kHeight, 1.0e308);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
        if (frame == 0u)
        {
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
            continue;
        }
        EXPECT_EQ(outcome.status().code(), an::ErrorCode::InvalidData);
        EXPECT_TRUE(messageNames(outcome.status(), "overflow")) << outcome.status().message();
    }
}

TEST(StackAccumulatorsTest, TakesNothingFromACancelledExecution)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, an::kUseContextConcurrency, stop.get_token());
    const an::StackModel model = makeModel(baseRequest());
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    const OwnedBand band = makeBand(0u, kWidth, kHeight, 1u);
    stop.request_stop();
    const an::Result<an::StackAccumulationOutcome> outcome =
        an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
    EXPECT_EQ(outcome.status().code(), an::ErrorCode::Cancelled);
    for (const std::uint32_t count : owned.counts)
    {
        EXPECT_EQ(count, 0u);
    }
    std::vector<std::byte> output(static_cast<std::size_t>(kWidth) * kHeight * sizeof(float));
    const an::Result<an::StackFinalizeOutcome> finalized =
        an::finalizeStackAccumulators(execution, model, owned.set(), an::StackFinalizeTargets{.outputImage = output});
    EXPECT_EQ(finalized.status().code(), an::ErrorCode::Cancelled);
}

TEST(StackAccumulatorsTest, FinalizesIntoEveryRequestedMapAndRefusesAMismatchedTarget)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = baseRequest();
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.coverageMap = true;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight;
    const std::array<double, kFrames> weights{1.0, 2.0, 3.0};
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const OwnedBand band = makeBand(frame, kWidth, kHeight, 1u);
        ASSERT_TRUE(an::accumulateStackFrame(execution, model, owned.set(), band.band(), weights).hasValue());
    }

    std::vector<std::byte> output(static_cast<std::size_t>(kElements) * sizeof(float));
    std::vector<std::uint32_t> coverage(static_cast<std::size_t>(kElements), 0u);
    std::vector<float> variance(static_cast<std::size_t>(kElements), 0.0f);
    std::vector<float> gain(static_cast<std::size_t>(kElements), 0.0f);
    std::vector<float> weightSum(static_cast<std::size_t>(kElements), 0.0f);
    const an::StackFinalizeTargets complete{.outputSampleType = an::SampleType::Float32,
                                            .outputImage = output,
                                            .coverage = coverage,
                                            .variance = variance,
                                            .signalToNoiseGain = gain,
                                            .weightSum = weightSum};
    const an::Result<an::StackFinalizeOutcome> finalized =
        an::finalizeStackAccumulators(execution, model, owned.set(), complete);
    ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(coverage[element], kFrames);
        EXPECT_EQ(weightSum[element], 6.0f);
        EXPECT_EQ(gain[element], static_cast<float>(36.0 / 14.0));
        EXPECT_GT(variance[element], 0.0f);
    }

    const auto refusal = [&](const an::StackFinalizeTargets& targets) {
        return an::finalizeStackAccumulators(execution, model, owned.set(), targets).status();
    };
    {
        an::StackFinalizeTargets missing = complete;
        missing.variance = {};
        const an::Status status = refusal(missing);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "variance")) << status.message();
    }
    {
        an::StackFinalizeTargets shortMap = complete;
        shortMap.coverage = std::span<std::uint32_t>(coverage).subspan(1u);
        const an::Status status = refusal(shortMap);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "coverage")) << status.message();
    }
    {
        an::StackFinalizeTargets shortImage = complete;
        shortImage.outputImage = std::span<std::byte>(output).subspan(4u);
        const an::Status status = refusal(shortImage);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "outputImage")) << status.message();
    }
    {
        an::StackFinalizeTargets wrongType = complete;
        wrongType.outputSampleType = an::SampleType::Float64;
        const an::Status status = refusal(wrongType);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "outputSampleType")) << status.message();
    }
    {
        std::vector<std::uint8_t> bits(an::packedMaskRowBytes(kWidth) * (kHeight - 1u), 0u);
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(kWidth, kHeight - 1u, 1u, an::defaultEngineLimits());
        ASSERT_TRUE(extent.has_value());
        const std::optional<an::ValidityMaskView<std::uint8_t>> mask =
            an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>(bits), *extent);
        ASSERT_TRUE(mask.has_value());
        an::StackFinalizeTargets wrongMask = complete;
        wrongMask.outputMask = &*mask;
        const an::Status status = refusal(wrongMask);
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "outputMask")) << status.message();
    }
    {
        // A map the stack was not asked for is refused rather than silently left as it was.
        const an::StackModel plain = makeModel(baseRequest());
        OwnedAccumulators bare = OwnedAccumulators::forModel(plain);
        const an::Status status =
            an::finalizeStackAccumulators(execution, plain, bare.set(),
                                          an::StackFinalizeTargets{.outputImage = output, .weightSum = weightSum})
                .status();
        EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(status, "weightSum")) << status.message();
    }
}

TEST(StackAccumulatorsTest, WritesTheFloat64OutputFromAFloat64Tile)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = baseRequest(kFrames, makeDescriptor(an::SampleType::Float64, kWidth, kHeight));
    request.output.sampleType = an::SampleType::Float64;
    const an::StackModel model = makeModel(request);
    OwnedAccumulators owned = OwnedAccumulators::forModel(model);
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight;

    std::vector<double> expected(static_cast<std::size_t>(kElements), 0.0);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        OwnedBand band;
        band.frameIndex = frame;
        band.layout = planarLayout(an::SampleType::Float64, kWidth, kHeight, 1u);
        band.float64.resize(static_cast<std::size_t>(kElements));
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            band.float64[element] = 1.0 / 3.0 + 1e-12 * static_cast<double>(element) + 0.1 * frame;
            expected[element] += band.float64[element] * 1.0;
        }
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, owned.set(), band.band(), {});
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    }
    std::vector<std::byte> output(static_cast<std::size_t>(kElements) * sizeof(double));
    const an::Result<an::StackFinalizeOutcome> finalized = an::finalizeStackAccumulators(
        execution, model, owned.set(),
        an::StackFinalizeTargets{.outputSampleType = an::SampleType::Float64, .outputImage = output});
    ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
    const std::vector<double> written = outputAsDoubles(output, an::SampleType::Float64);
    for (std::size_t element = 0u; element < kElements; ++element)
    {
        EXPECT_EQ(written[element], expected[element] / kFrames) << element;
    }
}

TEST(StackAccumulatorsTest, RunsThroughTheWorkingSetEndToEnd)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request =
        baseRequest(kFrames, makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.coverageMap = true;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, *plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet set = std::move(*made);
    constexpr std::uint32_t kChannels = 3u;
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight * kChannels;

    const std::span<double> table = set.frameWeights();
    ASSERT_EQ(table.size(), static_cast<std::size_t>(kFrames) * kChannels);
    for (std::size_t index = 0u; index < table.size(); ++index)
    {
        table[index] = 1.0 + 0.5 * static_cast<double>(index);
    }
    const an::StackAccumulatorSet accumulators{.sums = set.accumulatorSums(),
                                               .counts = set.accumulatorCounts(),
                                               .weightSums = set.accumulatorWeightSums(),
                                               .weightSquareSums = set.accumulatorWeightSquareSums(),
                                               .varianceMoments = set.accumulatorVarianceMoments()};
    const an::Result<std::uint64_t> checked = an::checkStackAccumulatorSet(model, accumulators);
    ASSERT_TRUE(checked.hasValue()) << checked.status().message();
    EXPECT_EQ(*checked, kElements);

    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Combine).ok());
    std::vector<double> expected(static_cast<std::size_t>(kElements), 0.0);
    std::vector<double> expectedWeights(static_cast<std::size_t>(kElements), 0.0);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const OwnedBand band = makeBand(frame, kWidth, kHeight, kChannels);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(execution, model, accumulators, band.band(), table);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        for (std::size_t element = 0u; element < kElements; ++element)
        {
            const double weight = table[frame * kChannels + element / (static_cast<std::size_t>(kWidth) * kHeight)];
            expected[element] += static_cast<double>(band.float32[element]) * weight;
            expectedWeights[element] += weight;
        }
    }
    set.endPass();

    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Finalize).ok());
    std::optional<an::ValidityMaskView<std::uint8_t>> mask = set.outputMask();
    ASSERT_TRUE(mask.has_value());
    const an::Result<an::StackFinalizeOutcome> finalized = an::finalizeStackAccumulators(
        execution, model, accumulators,
        an::StackFinalizeTargets{.outputSampleType = an::SampleType::Float32,
                                 .outputImage = set.outputImage(),
                                 .outputMask = &*mask,
                                 .coverage = set.outputCountMap(an::StackOutputMap::Coverage),
                                 .weightSum = set.outputValueMap(an::StackOutputMap::WeightSum)});
    ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
    EXPECT_EQ(finalized->elementsWithSamples, kElements);
    set.endPass();

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
        EXPECT_EQ(samples[element], static_cast<float>(expected[element] / expectedWeights[element])) << element;
    }
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        EXPECT_EQ(mask->test(channel, kWidth - 1u, kHeight - 1u), std::optional<bool>{true}) << channel;
    }
    const std::span<const std::uint32_t> coverage = set.outputCountMap(an::StackOutputMap::Coverage);
    EXPECT_EQ(coverage[0], kFrames);
}

TEST(StackAccumulatorsTest, ReportsTheBackendItRanAndNamesEveryUpdate)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(baseRequest());
    const OwnedBand band = makeBand(0u, kWidth, kHeight, 1u);
    {
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        const an::ExecutionContext reference =
            forcedExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar);
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(reference, model, owned.set(), band.band(), {});
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->backend.backend, an::ComputeBackend::Reference);
        EXPECT_EQ(outcome->backend.instructionSet, an::InstructionSet::Scalar);
        EXPECT_FALSE(outcome->compiledRowUsed);
    }
    {
        OwnedAccumulators owned = OwnedAccumulators::forModel(model);
        const an::ExecutionContext automatic = makeExecution(engine);
        const an::Result<an::BackendSelection> selected = an::selectBackend(automatic);
        ASSERT_TRUE(selected.hasValue());
        const an::Result<an::StackAccumulationOutcome> outcome =
            an::accumulateStackFrame(automatic, model, owned.set(), band.band(), {});
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->backend, *selected);
        EXPECT_EQ(outcome->compiledRowUsed, selected->backend != an::ComputeBackend::Reference &&
                                                selected->instructionSet != an::InstructionSet::Scalar);
    }

    EXPECT_STREQ(an::stackAccumulatorUpdateName(an::StackAccumulatorUpdate::WeightedSum), "WeightedSum");
    EXPECT_STREQ(an::stackAccumulatorUpdateName(an::StackAccumulatorUpdate::Minimum), "Minimum");
    EXPECT_STREQ(an::stackAccumulatorUpdateName(an::StackAccumulatorUpdate::Maximum), "Maximum");
    EXPECT_STREQ(an::stackAccumulatorUpdateName(static_cast<an::StackAccumulatorUpdate>(7u)),
                 "UnknownStackAccumulatorUpdate");
}
