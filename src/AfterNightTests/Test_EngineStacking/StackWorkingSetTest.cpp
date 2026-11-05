// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the stack working set: every planned buffer taken by name at its planned
// size, the refusal before anything is taken, the pass buffers that live with their pass, the
// peak that is the plan's, the handover of the result without a copy, and the prepared-state
// residency the plan counts.

#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPreparedState.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/Alignment.h>
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
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

constexpr std::uint32_t kWidth = 64u;
constexpr std::uint32_t kHeight = 48u;
constexpr std::uint32_t kFrames = 5u;

/// An engine with a stated concurrency and memory budget.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u,
                                           std::uint64_t memoryBudgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryBudgetBytes;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// One operation in an engine, optionally listened to, stoppable and narrowed.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, an::DiagnosticSink* sink = nullptr,
                                                 std::stop_token stopToken = {},
                                                 std::uint32_t concurrency = an::kUseContextConcurrency)
{
    an::ExecutionOptions options;
    options.diagnosticSink = sink;
    options.stopToken = std::move(stopToken);
    options.maxConcurrency = concurrency;
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

/// An average with sigma clipping, which holds every sample of a pixel at once.
[[nodiscard]] an::StackRequest clippedRequest(std::uint32_t frames = kFrames,
                                              const an::ImageDescriptor& descriptor = makeDescriptor())
{
    an::StackRequest request = baseRequest(frames, descriptor);
    request.rejection.method = an::StackRejection::SigmaClipping;
    return request;
}

/// A sigma-clipped average that also measures a global normalization and a local weight surface,
/// so its plan holds all four passes.
[[nodiscard]] an::StackRequest fourPassRequest(std::uint32_t frames = kFrames,
                                               const an::ImageDescriptor& descriptor = makeDescriptor())
{
    an::StackRequest request = clippedRequest(frames, descriptor);
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::AdditiveScaling;
    request.localWeighting.enabled = true;
    return request;
}

/// The checked model of a request every rule admits.
[[nodiscard]] an::StackModel makeModel(const an::StackRequest& request)
{
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    return model.has_value() ? *model : *an::StackModel::create(baseRequest());
}

/// A plan of a model on an execution, which every case here expects to exist.
[[nodiscard]] an::StackPlan makePlan(const an::ExecutionContext& execution, const an::StackModel& model,
                                     const an::StackPlanningRequest& request = an::StackPlanningRequest{})
{
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    return plan.hasValue() ? *plan : an::StackPlan{};
}

/// What an engine's resource has outstanding right now.
[[nodiscard]] std::uint64_t liveBytes(const an::EngineContext& engine)
{
    const an::MemoryResource* resource = engine.memoryResource();
    return resource != nullptr ? resource->counters().liveBytes : 0u;
}

/// The engine's resource counters, all of them.
[[nodiscard]] an::MemoryCounters counters(const an::EngineContext& engine)
{
    const an::MemoryResource* resource = engine.memoryResource();
    return resource != nullptr ? resource->counters() : an::MemoryCounters{};
}

/// The pass of a kind in a plan, if it has one.
[[nodiscard]] std::optional<an::StackPass> passOf(const an::StackPlan& plan, an::StackPassKind kind)
{
    for (std::size_t index = 0u; index < plan.passes.count; ++index)
    {
        if (plan.passes.passes[index].kind == kind)
        {
            return plan.passes.passes[index];
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
            return field.text;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
}

/// How many bytes of a span are not zero.
template <typename ElementT> [[nodiscard]] std::size_t nonZeroBytes(std::span<ElementT> values)
{
    const std::span<const std::byte> bytes = std::as_bytes(values);
    std::size_t count = 0u;
    for (const std::byte byte : bytes)
    {
        count += byte != std::byte{0} ? 1u : 0u;
    }
    return count;
}

/// A diagnostic sink that keeps every event it was told.
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

    [[nodiscard]] std::vector<Event> events() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_events;
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

/// The account of one buffer.
[[nodiscard]] an::StackBufferAccount bufferAccount(const an::StackWorkingSet& set, an::StackBuffer buffer)
{
    return set.account().buffers[static_cast<std::size_t>(buffer)];
}

// The map, accumulator and lifetime tables are constant expressions, so an executor can be written
// against them.
static_assert(an::stackBufferLifetimeOf(an::StackBuffer::LocalWeightGrids) == an::StackBufferLifetime::Persistent);
static_assert(an::stackBufferLifetimeOf(an::StackBuffer::WorkerScratch) == an::StackBufferLifetime::Pass);
static_assert(an::stackOutputMapSampleType(an::StackOutputMap::Coverage) == an::SampleType::UInt32);
static_assert(an::stackOutputMapSampleType(an::StackOutputMap::Variance) == an::SampleType::Float32);
static_assert(an::stackOutputMapCount(an::StackOutputRequest{}) == 0u);
static_assert(an::stackAccumulatorBytesPerElement(an::StackAccumulator::VarianceMoments) ==
              2u * an::kStackAccumulatorBytes);
static_assert(an::preparedStateBytesOf(std::span<const an::FrameNormalization>{}) == 0u);
static_assert(an::preparedStateBytesOf(std::span<const an::PhotometricStackWeight>{}) == 0u);
static_assert(an::preparedStateBytesOf(an::StackPreparedState{}) == 0u);

TEST(StackWorkingSetTest, RefusesAnEmptyContextAnEmptyExecutionAndAnotherContextsExecution)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(clippedRequest());
    const an::StackPlan plan = makePlan(execution, model);

    const an::EngineContext emptyEngine;
    const an::Result<an::StackWorkingSet> noEngine = an::StackWorkingSet::create(emptyEngine, execution, model, plan);
    ASSERT_FALSE(noEngine.hasValue());
    EXPECT_EQ(noEngine.status().code(), an::ErrorCode::NotInitialized);

    const an::ExecutionContext emptyExecution;
    const an::Result<an::StackWorkingSet> noExecution =
        an::StackWorkingSet::create(engine, emptyExecution, model, plan);
    ASSERT_FALSE(noExecution.hasValue());
    EXPECT_EQ(noExecution.status().code(), an::ErrorCode::NotInitialized);

    const an::EngineContext other = makeEngine();
    const an::Result<an::StackWorkingSet> foreign = an::StackWorkingSet::create(other, execution, model, plan);
    ASSERT_FALSE(foreign.hasValue());
    EXPECT_EQ(foreign.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(foreign.status(), an::ContextKey::Parameter), "execution");
    EXPECT_EQ(liveBytes(engine), 0u);
    EXPECT_EQ(liveBytes(other), 0u);
}

TEST(StackWorkingSetTest, ThePersistentStateIsTakenExactlyAsPlannedAndNothingElse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = clippedRequest();
    request.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    const an::StackPlan plan = makePlan(execution, model);
    const std::uint64_t baseline = liveBytes(engine);
    const std::uint64_t allocationsBefore = counters(engine).liveAllocations;

    {
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        an::StackWorkingSet& set = *made;
        EXPECT_TRUE(set.valid());
        EXPECT_EQ(set.contextId(), engine.id());
        ASSERT_TRUE(set.plan().has_value());
        EXPECT_EQ(*set.plan(), plan);

        // What the resource has outstanding grew by exactly the plan's persistent figure, in one
        // allocation per buffer: the image, the mask, three maps and the frame table.
        EXPECT_EQ(liveBytes(engine), baseline + plan.memory.persistentBytes);
        const an::MemoryCounters after = counters(engine);
        EXPECT_EQ(after.deniedAllocations, 0u);
        EXPECT_EQ(after.liveAllocations, allocationsBefore + 6u);

        const an::StackMemoryAccount account = set.account();
        EXPECT_EQ(account.baselineLiveBytes, baseline);
        EXPECT_EQ(account.availableBytes, an::kDefaultMemoryBudgetBytes);
        EXPECT_EQ(account.plannedPersistentBytes, plan.memory.persistentBytes);
        EXPECT_EQ(account.plannedPeakBytes, plan.memory.peakBytes);
        EXPECT_EQ(account.preparedStateBytes, 0u);
        EXPECT_EQ(account.heldBytes, plan.memory.persistentBytes);
        EXPECT_EQ(account.peakHeldBytes, plan.memory.persistentBytes);
        EXPECT_EQ(account.handedOverBytes, 0u);
        EXPECT_EQ(account.refusals, 0u);
        EXPECT_FALSE(account.passOpen);
        EXPECT_EQ(account.passesCompleted, 0u);
        EXPECT_FALSE(set.currentPass().has_value());

        const an::StackBufferAccount image = bufferAccount(set, an::StackBuffer::OutputImage);
        EXPECT_EQ(image.plannedBytes, plan.memory.outputImageBytes);
        EXPECT_EQ(image.heldBytes, plan.memory.outputImageBytes);
        EXPECT_EQ(image.allocations, 1u);
        const an::StackBufferAccount mask = bufferAccount(set, an::StackBuffer::OutputMask);
        EXPECT_EQ(mask.heldBytes, plan.memory.outputMaskBytes);
        EXPECT_EQ(mask.allocations, 1u);
        const an::StackBufferAccount maps = bufferAccount(set, an::StackBuffer::OutputMaps);
        EXPECT_EQ(maps.plannedBytes, plan.memory.outputMapBytes);
        EXPECT_EQ(maps.heldBytes, plan.memory.outputMapBytes);
        EXPECT_EQ(maps.allocations, 3u);
        const an::StackBufferAccount accumulators = bufferAccount(set, an::StackBuffer::Accumulators);
        EXPECT_EQ(accumulators.plannedBytes, 0u);
        EXPECT_EQ(accumulators.heldBytes, 0u);
        EXPECT_EQ(accumulators.allocations, 0u);
        const an::StackBufferAccount frameState = bufferAccount(set, an::StackBuffer::FrameState);
        EXPECT_EQ(frameState.heldBytes, plan.memory.frameStateBytes);
        EXPECT_EQ(frameState.allocations, 1u);
        EXPECT_EQ(bufferAccount(set, an::StackBuffer::LocalWeightGrids).heldBytes, 0u);
        for (const an::StackBuffer buffer : {an::StackBuffer::WorkerScratch, an::StackBuffer::Tile,
                                             an::StackBuffer::ReadBuffer, an::StackBuffer::MeasurementWorkspace})
        {
            EXPECT_EQ(bufferAccount(set, buffer).heldBytes, 0u) << an::stackBufferName(buffer);
            EXPECT_EQ(bufferAccount(set, buffer).allocations, 0u) << an::stackBufferName(buffer);
        }
    }
    EXPECT_EQ(liveBytes(engine), baseline);
    EXPECT_EQ(counters(engine).liveAllocations, allocationsBefore);
}

TEST(StackWorkingSetTest, EveryBufferIsZeroedAlignedAndSizedAsPlanned)
{
    // A weighted streaming average of three 8-by-4 colour frames that writes every value map
    // keeps every accumulator; each buffer is one allocation of exactly its planned size, begins
    // on a vector boundary and starts from nothing.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor color = makeDescriptor(an::SampleType::Float32, 8u, 4u, an::ChannelSchema::Rgb);
    an::StackRequest request = baseRequest(3u, color);
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.coverageMap = true;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    const an::StackPlan plan = makePlan(execution, model);
    constexpr std::size_t kElements = 8u * 4u * 3u;

    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet& set = *made;

    const std::span<std::byte> image = set.outputImage();
    EXPECT_EQ(image.size(), kElements * sizeof(float));
    EXPECT_EQ(image.size(), plan.memory.outputImageBytes);
    EXPECT_TRUE(an::isPointerAligned(image.data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_EQ(nonZeroBytes(image), 0u);
    const std::optional<an::ImageLayout> layout = set.outputImageLayout();
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->planeCount(), 3u);
    EXPECT_EQ(layout->elementSizeBytes(), 4u);
    EXPECT_EQ(layout->rowStrideBytes(), 8u * 4u);
    EXPECT_EQ(layout->totalBytes(), plan.memory.outputImageBytes);
    EXPECT_TRUE(an::layoutDescribes(model.outputDescriptor(), *layout));

    const std::optional<an::ValidityMaskView<std::uint8_t>> mask = set.outputMask();
    ASSERT_TRUE(mask.has_value());
    EXPECT_EQ(mask->width(), 8u);
    EXPECT_EQ(mask->height(), 4u);
    EXPECT_EQ(mask->planeCount(), 3u);
    EXPECT_EQ(an::countValidity(*mask).valid, 0u);
    EXPECT_EQ(an::countValidity(*mask).total, kElements);
    EXPECT_TRUE(an::isPointerAligned(mask->data(), an::kDefaultBufferAlignmentBytes));

    const std::span<std::uint32_t> coverage = set.outputCountMap(an::StackOutputMap::Coverage);
    EXPECT_EQ(coverage.size(), kElements);
    EXPECT_TRUE(an::isPointerAligned(coverage.data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_EQ(nonZeroBytes(coverage), 0u);
    for (const an::StackOutputMap map :
         {an::StackOutputMap::Variance, an::StackOutputMap::SignalToNoiseGain, an::StackOutputMap::WeightSum})
    {
        const std::span<float> values = set.outputValueMap(map);
        EXPECT_EQ(values.size(), kElements) << an::stackOutputMapName(map);
        EXPECT_TRUE(an::isPointerAligned(values.data(), an::kDefaultBufferAlignmentBytes));
        EXPECT_EQ(nonZeroBytes(values), 0u) << an::stackOutputMapName(map);
        EXPECT_TRUE(set.outputCountMap(map).empty()) << an::stackOutputMapName(map);
    }
    EXPECT_TRUE(set.outputValueMap(an::StackOutputMap::Coverage).empty());
    EXPECT_TRUE(set.outputCountMap(an::StackOutputMap::RejectionLow).empty());
    EXPECT_TRUE(set.outputCountMap(an::StackOutputMap::RejectionMerged).empty());
    EXPECT_EQ(bufferAccount(set, an::StackBuffer::OutputMaps).allocations, 4u);
    EXPECT_EQ(bufferAccount(set, an::StackBuffer::OutputMaps).heldBytes, 4u * kElements * an::kStackMapSampleBytes);

    EXPECT_EQ(set.accumulatorSums().size(), kElements);
    EXPECT_EQ(set.accumulatorCounts().size(), kElements);
    EXPECT_EQ(set.accumulatorWeightSums().size(), kElements);
    EXPECT_EQ(set.accumulatorWeightSquareSums().size(), kElements);
    EXPECT_EQ(set.accumulatorVarianceMoments().size(), 2u * kElements);
    EXPECT_TRUE(an::isPointerAligned(set.accumulatorSums().data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_TRUE(an::isPointerAligned(set.accumulatorCounts().data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_TRUE(an::isPointerAligned(set.accumulatorWeightSums().data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_TRUE(an::isPointerAligned(set.accumulatorWeightSquareSums().data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_TRUE(an::isPointerAligned(set.accumulatorVarianceMoments().data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_EQ(nonZeroBytes(set.accumulatorSums()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorCounts()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorWeightSums()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorWeightSquareSums()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorVarianceMoments()), 0u);
    EXPECT_EQ(bufferAccount(set, an::StackBuffer::Accumulators).allocations, 5u);
    EXPECT_EQ(bufferAccount(set, an::StackBuffer::Accumulators).heldBytes, plan.memory.accumulatorBytes);
    EXPECT_EQ(plan.memory.accumulatorBytes, kElements * (8u + 4u + 8u + 8u + 16u));

    EXPECT_EQ(set.frameWeights().size(), 3u * 3u);
    EXPECT_TRUE(an::isPointerAligned(set.frameWeights().data(), an::kDefaultBufferAlignmentBytes));
    EXPECT_EQ(nonZeroBytes(set.frameWeights()), 0u);
    EXPECT_TRUE(set.frameNormalization().empty());
    EXPECT_TRUE(set.localWeightGrids().empty());

    // Nothing of a pass is held before one begins.
    EXPECT_TRUE(set.workerScratch(0u).empty());
    EXPECT_TRUE(set.tile().empty());
    EXPECT_TRUE(set.readBuffer().empty());
    EXPECT_TRUE(set.measurementWorkspace().empty());
    EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes);
}

TEST(StackWorkingSetTest, ALargeSetIsZeroedAcrossChunksWhateverItsMemoryHeld)
{
    // A set many clearing chunks long is cleared on four workers. The resource's memory is dirtied
    // first, in the sizes the set then takes, so what the resource hands back holds something
    // other than zero, and every byte of every buffer - each chunk's tail included - must still
    // start from nothing.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor color = makeDescriptor(an::SampleType::Float32, 701u, 389u, an::ChannelSchema::Rgb);
    an::StackRequest request = baseRequest(3u, color);
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.coverageMap = true;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    const an::StackPlan plan = makePlan(execution, model);
    constexpr std::uint64_t kElements = 701u * 389u * 3u;
    ASSERT_GT(plan.memory.outputImageBytes, 8u * 256u * 1024u);

    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::array<std::uint64_t, 6u> sizes{kElements * 4u, kElements * 4u,  kElements * 8u,
                                              kElements * 8u, kElements * 16u, kElements * 4u};
    for (int round = 0; round < 2; ++round)
    {
        std::vector<void*> blocks;
        for (const std::uint64_t bytes : sizes)
        {
            for (int copy = 0; copy < 4; ++copy)
            {
                an::Result<void*> block = resource->allocate(bytes, an::kDefaultBufferAlignmentBytes);
                ASSERT_TRUE(block.hasValue()) << block.status().message();
                std::memset(*block, 0xA5, static_cast<std::size_t>(bytes));
                blocks.push_back(*block);
            }
        }
        std::size_t index = 0u;
        for (const std::uint64_t bytes : sizes)
        {
            for (int copy = 0; copy < 4; ++copy)
            {
                resource->deallocate(blocks[index++], bytes, an::kDefaultBufferAlignmentBytes);
            }
        }
    }

    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet& set = *made;
    EXPECT_EQ(nonZeroBytes(set.outputImage()), 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> mask = set.outputMask();
    ASSERT_TRUE(mask.has_value());
    EXPECT_EQ(an::countValidity(*mask).valid, 0u);
    EXPECT_EQ(nonZeroBytes(set.outputCountMap(an::StackOutputMap::Coverage)), 0u);
    for (const an::StackOutputMap map :
         {an::StackOutputMap::Variance, an::StackOutputMap::SignalToNoiseGain, an::StackOutputMap::WeightSum})
    {
        EXPECT_EQ(set.outputValueMap(map).size(), kElements) << an::stackOutputMapName(map);
        EXPECT_EQ(nonZeroBytes(set.outputValueMap(map)), 0u) << an::stackOutputMapName(map);
    }
    EXPECT_EQ(nonZeroBytes(set.accumulatorSums()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorCounts()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorWeightSums()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorWeightSquareSums()), 0u);
    EXPECT_EQ(nonZeroBytes(set.accumulatorVarianceMoments()), 0u);
    EXPECT_EQ(nonZeroBytes(set.frameWeights()), 0u);
}

TEST(StackWorkingSetTest, AStoppedExecutionIsRefusedBeforeAnythingIsTaken)
{
    // The buffers are cleared on the execution's workers, so an execution that is already stopped
    // is refused with its stop before the resource is asked for anything.
    const an::EngineContext engine = makeEngine();
    std::stop_source source;
    const an::ExecutionContext execution = makeExecution(engine, nullptr, source.get_token());
    const an::StackModel model = makeModel(clippedRequest());
    const an::StackPlan plan = makePlan(execution, model);
    const an::MemoryCounters before = counters(engine);

    source.request_stop();
    const an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_FALSE(made.hasValue());
    EXPECT_EQ(made.status().code(), an::ErrorCode::Cancelled);
    const an::MemoryCounters after = counters(engine);
    EXPECT_EQ(after.liveBytes, before.liveBytes);
    EXPECT_EQ(after.liveAllocations, before.liveAllocations);
    EXPECT_EQ(after.totalAllocations, before.totalAllocations);
    EXPECT_EQ(after.deniedAllocations, before.deniedAllocations);
}

TEST(StackWorkingSetTest, PassBuffersLiveWithTheirPassAndThePeakIsThePlans)
{
    // The peak-memory contract: through all four passes of a clipped, normalized and locally
    // weighted stack over whole frames, what the engine context has outstanding is exactly the
    // plan's working set for the open pass, falls back to the persistent state between passes,
    // and never exceeds the plan's peak.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(fourPassRequest());
    an::StackPlanningRequest request;
    request.maximumTileRows = 8u;
    const an::StackPlan plan = makePlan(execution, model, request);
    ASSERT_EQ(plan.passes.count, 4u);
    ASSERT_EQ(liveBytes(engine), 0u);

    {
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        an::StackWorkingSet& set = *made;
        EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes);
        EXPECT_GT(set.localWeightGrids().size(), 0u);
        EXPECT_EQ(set.localWeightGrids().size() * sizeof(float), plan.memory.localWeightGridBytes);
        EXPECT_EQ(set.frameNormalization().size(), 2u * kFrames);
        EXPECT_EQ(set.frameWeights().size(), kFrames);

        std::uint64_t ownPeak = plan.memory.persistentBytes;
        for (std::size_t index = 0u; index < plan.passes.count; ++index)
        {
            const an::StackPass& pass = plan.passes.passes[index];
            const an::Status begun = set.beginPass(execution, pass.kind);
            ASSERT_TRUE(begun.ok()) << an::stackPassKindName(pass.kind) << ": " << begun.message();
            EXPECT_EQ(set.currentPass(), pass.kind);
            const std::uint64_t composed =
                pass.kind == an::StackPassKind::MeasureNormalization ? plan.memory.normalizationScratchBytes
                : pass.kind == an::StackPassKind::Finalize           ? plan.memory.diagnosticsScratchBytes
                                                                     : 0u;
            const std::uint64_t owned = pass.workingSetBytes - composed;
            ownPeak = (std::max)(ownPeak, owned);
            EXPECT_EQ(liveBytes(engine), owned) << an::stackPassKindName(pass.kind);
            EXPECT_EQ(set.account().heldBytes, owned) << an::stackPassKindName(pass.kind);
            EXPECT_LE(liveBytes(engine), plan.memory.peakBytes);

            switch (pass.kind)
            {
            case an::StackPassKind::MeasureNormalization:
            case an::StackPassKind::MeasureLocalWeights:
                EXPECT_EQ(set.readBuffer().size(), plan.io.frameBytes);
                EXPECT_EQ(set.measurementWorkspace().size(),
                          pass.workingSetBytes - plan.memory.persistentBytes - plan.io.frameBytes - composed);
                EXPECT_TRUE(set.tile().empty());
                EXPECT_TRUE(set.workerScratch(0u).empty());
                break;
            case an::StackPassKind::Combine:
                EXPECT_EQ(set.readBuffer().size(), plan.memory.readBufferBytes);
                EXPECT_EQ(set.tile().size(), plan.memory.tileBytes);
                EXPECT_EQ(set.tile().size(), static_cast<std::size_t>(plan.memory.tileBytesPerRow) * 8u);
                EXPECT_TRUE(set.measurementWorkspace().empty());
                for (std::uint32_t worker = 0u; worker < plan.memory.workers; ++worker)
                {
                    const std::span<std::byte> scratch = set.workerScratch(worker);
                    EXPECT_EQ(scratch.size(), plan.memory.scratchBytesPerWorker) << worker;
                    EXPECT_TRUE(an::isPointerAligned(scratch.data(), an::kStackScratchAlignmentBytes)) << worker;
                }
                EXPECT_TRUE(set.workerScratch(plan.memory.workers).empty());
                EXPECT_EQ(bufferAccount(set, an::StackBuffer::WorkerScratch).heldBytes, plan.memory.scratchBytes);
                EXPECT_EQ(bufferAccount(set, an::StackBuffer::Tile).heldBytes, plan.memory.tileBytes);
                EXPECT_EQ(bufferAccount(set, an::StackBuffer::ReadBuffer).heldBytes, plan.memory.readBufferBytes);
                break;
            case an::StackPassKind::Finalize:
                EXPECT_TRUE(set.readBuffer().empty());
                EXPECT_TRUE(set.tile().empty());
                EXPECT_TRUE(set.workerScratch(0u).empty());
                EXPECT_TRUE(set.measurementWorkspace().empty());
                break;
            }

            set.endPass();
            EXPECT_FALSE(set.currentPass().has_value());
            EXPECT_EQ(set.passesCompleted(), index + 1u);
            EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes) << an::stackPassKindName(pass.kind);
            EXPECT_TRUE(set.readBuffer().empty());
            EXPECT_TRUE(set.tile().empty());
            EXPECT_TRUE(set.measurementWorkspace().empty());
        }

        EXPECT_EQ(counters(engine).peakBytes, ownPeak);
        EXPECT_EQ(set.account().peakHeldBytes, ownPeak);
        EXPECT_LT(ownPeak, plan.memory.peakBytes);
        EXPECT_EQ(set.account().refusals, 0u);
        EXPECT_EQ(counters(engine).deniedAllocations, 0u);
    }
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackWorkingSetTest, AStreamingStackHoldsAccumulatorsAndItsReadBufferOnly)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest());
    an::StackPlanningRequest request;
    request.concurrentFrameReads = 2u;
    const an::StackPlan plan = makePlan(execution, model, request);
    ASSERT_EQ(plan.strategy.strategy, an::StackAccumulationStrategy::WholeOutputAccumulators);
    ASSERT_EQ(plan.passes.count, 2u);

    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet& set = *made;
    EXPECT_EQ(set.accumulatorSums().size(), static_cast<std::size_t>(kWidth) * kHeight);
    EXPECT_EQ(set.accumulatorCounts().size(), static_cast<std::size_t>(kWidth) * kHeight);
    EXPECT_TRUE(set.accumulatorWeightSums().empty());
    EXPECT_TRUE(set.accumulatorWeightSquareSums().empty());
    EXPECT_TRUE(set.accumulatorVarianceMoments().empty());

    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Combine).ok());
    EXPECT_EQ(set.readBuffer().size(), 2u * plan.io.frameBytes);
    EXPECT_EQ(set.readBuffer().size(), plan.memory.readBufferBytes);
    EXPECT_TRUE(set.tile().empty());
    EXPECT_TRUE(set.workerScratch(0u).empty());
    EXPECT_EQ(bufferAccount(set, an::StackBuffer::WorkerScratch).plannedBytes, 0u);
    EXPECT_EQ(bufferAccount(set, an::StackBuffer::Tile).plannedBytes, 0u);
    EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes + 2u * plan.io.frameBytes);
    EXPECT_EQ(liveBytes(engine), plan.memory.peakBytes);
    set.endPass();
    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Finalize).ok());
    EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes);
    set.endPass();
    EXPECT_EQ(set.passesCompleted(), 2u);
}

TEST(StackWorkingSetTest, APeakThatNoLongerFitsIsRefusedBeforeAnythingIsTaken)
{
    // Plan against a budget the whole run exactly fits; then let another allocation take a little
    // of it between planning and creation. The set refuses with the numbers as they stand now, and
    // asks the resource for nothing on the way.
    const an::StackModel model = makeModel(clippedRequest());
    std::uint64_t peak = 0u;
    {
        const an::EngineContext unbounded = makeEngine();
        const an::ExecutionContext execution = makeExecution(unbounded);
        peak = makePlan(execution, model).memory.peakBytes;
    }
    ASSERT_GT(peak, 0u);
    const an::EngineContext engine = makeEngine(2u, peak);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackPlan plan = makePlan(execution, model);
    ASSERT_EQ(plan.memory.peakBytes, peak);
    ASSERT_EQ(plan.tile.reason, an::StackTileReason::WholeFrameFits);

    an::MemoryResource* resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    constexpr std::uint64_t kHeld = 64u;
    const an::Result<void*> block = resource->allocate(kHeld, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(block.hasValue()) << block.status().message();
    const std::uint64_t deniedBefore = counters(engine).deniedAllocations;

    const an::Result<an::StackWorkingSet> refused = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), peak);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Available), peak - kHeld);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Limit), peak);
    EXPECT_TRUE(messageNames(refused.status(), "nothing was taken"));
    EXPECT_EQ(liveBytes(engine), kHeld);
    EXPECT_EQ(counters(engine).deniedAllocations, deniedBefore);
    EXPECT_EQ(counters(engine).liveAllocations, 1u);

    resource->deallocate(*block, kHeld, an::kDefaultBufferAlignmentBytes);
    an::Result<an::StackWorkingSet> admitted = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(admitted.hasValue()) << admitted.status().message();
    EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes);
    EXPECT_EQ(admitted->account().availableBytes, peak);
    EXPECT_EQ(admitted->account().baselineLiveBytes, 0u);
    ASSERT_TRUE(admitted->beginPass(execution, an::StackPassKind::Combine).ok());
    EXPECT_EQ(liveBytes(engine), peak);
}

TEST(StackWorkingSetTest, APassThatNoLongerFitsIsRefusedBeforeItsBuffersAreTaken)
{
    const an::StackModel model = makeModel(clippedRequest());
    std::uint64_t peak = 0u;
    {
        const an::EngineContext unbounded = makeEngine();
        const an::ExecutionContext execution = makeExecution(unbounded);
        peak = makePlan(execution, model).memory.peakBytes;
    }
    const an::EngineContext engine = makeEngine(2u, peak);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackPlan plan = makePlan(execution, model);
    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet& set = *made;
    const std::uint64_t persistent = liveBytes(engine);
    EXPECT_EQ(persistent, plan.memory.persistentBytes);

    an::MemoryResource* resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    constexpr std::uint64_t kHeld = 64u;
    const an::Result<void*> block = resource->allocate(kHeld, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(block.hasValue()) << block.status().message();
    const std::uint64_t deniedBefore = counters(engine).deniedAllocations;

    const std::uint64_t passBytes = plan.memory.scratchBytes + plan.memory.tileBytes + plan.memory.readBufferBytes;
    const an::Status refused = set.beginPass(execution, an::StackPassKind::Combine);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_TRUE(messageNames(refused, "Combine"));
    EXPECT_TRUE(messageNames(refused, "nothing was taken"));
    EXPECT_EQ(countField(refused, an::ContextKey::Requested), passBytes);
    EXPECT_EQ(countField(refused, an::ContextKey::Available), peak - persistent - kHeld);
    EXPECT_EQ(countField(refused, an::ContextKey::Limit), peak);
    EXPECT_FALSE(set.currentPass().has_value());
    EXPECT_EQ(set.passesCompleted(), 0u);
    EXPECT_EQ(liveBytes(engine), persistent + kHeld);
    EXPECT_EQ(set.account().heldBytes, persistent);
    EXPECT_EQ(set.account().refusals, 1u);
    EXPECT_EQ(counters(engine).deniedAllocations, deniedBefore);
    EXPECT_TRUE(set.tile().empty());
    EXPECT_TRUE(set.readBuffer().empty());

    // A cleanup path that ends a pass that never began changes nothing.
    set.endPass();
    EXPECT_EQ(set.passesCompleted(), 0u);

    resource->deallocate(*block, kHeld, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Combine).ok());
    EXPECT_EQ(liveBytes(engine), peak);
    EXPECT_EQ(set.account().refusals, 1u);
}

TEST(StackWorkingSetTest, PassesFollowThePlanInOrder)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest());
    const an::StackPlan plan = makePlan(execution, model);
    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet& set = *made;

    const an::Status early = set.beginPass(execution, an::StackPassKind::Finalize);
    EXPECT_EQ(early.code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(messageNames(early, "Combine"));
    EXPECT_EQ(textField(early, an::ContextKey::Parameter), "kind");
    EXPECT_EQ(set.beginPass(execution, an::StackPassKind::MeasureNormalization).code(), an::ErrorCode::InvalidState);
    EXPECT_FALSE(set.currentPass().has_value());

    const an::EngineContext other = makeEngine();
    const an::ExecutionContext foreign = makeExecution(other);
    const an::Status elsewhere = set.beginPass(foreign, an::StackPassKind::Combine);
    EXPECT_EQ(elsewhere.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(elsewhere, an::ContextKey::Parameter), "execution");
    const an::ExecutionContext empty;
    EXPECT_EQ(set.beginPass(empty, an::StackPassKind::Combine).code(), an::ErrorCode::NotInitialized);

    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Combine).ok());
    EXPECT_EQ(set.currentPass(), an::StackPassKind::Combine);
    const an::Status twice = set.beginPass(execution, an::StackPassKind::Combine);
    EXPECT_EQ(twice.code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(messageNames(twice, "open"));
    EXPECT_EQ(set.account().passOpen, true);
    EXPECT_EQ(set.account().currentPass, an::StackPassKind::Combine);
    set.endPass();
    EXPECT_EQ(set.passesCompleted(), 1u);
    EXPECT_EQ(set.beginPass(execution, an::StackPassKind::Combine).code(), an::ErrorCode::InvalidState);

    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Finalize).ok());
    EXPECT_TRUE(set.readBuffer().empty());
    EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes);
    set.endPass();
    EXPECT_EQ(set.passesCompleted(), 2u);
    const an::Status done = set.beginPass(execution, an::StackPassKind::Finalize);
    EXPECT_EQ(done.code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(messageNames(done, "every planned pass"));
    set.endPass();
    EXPECT_EQ(set.passesCompleted(), 2u);
    EXPECT_EQ(set.account().refusals, 0u);
}

TEST(StackWorkingSetTest, ACancelledExecutionTakesNoPassMemory)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source source;
    const an::ExecutionContext execution = makeExecution(engine, nullptr, source.get_token());
    const an::StackModel model = makeModel(clippedRequest());
    const an::StackPlan plan = makePlan(execution, model);
    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet& set = *made;
    const std::uint64_t persistent = liveBytes(engine);

    source.request_stop();
    const an::Status cancelled = set.beginPass(execution, an::StackPassKind::Combine);
    EXPECT_EQ(cancelled.code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(set.currentPass().has_value());
    EXPECT_EQ(liveBytes(engine), persistent);
    EXPECT_TRUE(set.tile().empty());
    EXPECT_EQ(set.account().refusals, 0u);
}

TEST(StackWorkingSetTest, TheOutputImageMaskAndMapsAreHandedOverWithoutACopy)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor small = makeDescriptor(an::SampleType::Float32, 8u, 4u);
    an::StackRequest request = clippedRequest(3u, small);
    request.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    const an::StackPlan plan = makePlan(execution, model);
    constexpr std::size_t kElements = 32u;

    std::uint64_t handedOver = 0u;
    an::ImageStorageBytes image;
    an::ValidityMask mask;
    an::ImageStorageBytes coverage;
    {
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        an::StackWorkingSet& set = *made;
        const std::span<std::uint32_t> counts = set.outputCountMap(an::StackOutputMap::Coverage);
        ASSERT_EQ(counts.size(), kElements);
        for (std::uint32_t& count : counts)
        {
            count = 7u;
        }
        const std::uint64_t persistent = set.account().heldBytes;

        an::Result<an::ImageStorageBytes> releasedImage = set.releaseOutputImage();
        ASSERT_TRUE(releasedImage.hasValue()) << releasedImage.status().message();
        image = std::move(*releasedImage);
        EXPECT_TRUE(image.valid());
        EXPECT_EQ(image.capacityBytes(), plan.memory.outputImageBytes);
        ASSERT_TRUE(image.layout().has_value());
        EXPECT_TRUE(an::layoutDescribes(model.outputDescriptor(), *image.layout()));
        EXPECT_TRUE(set.outputImage().empty());
        EXPECT_FALSE(set.outputImageLayout().has_value());
        EXPECT_EQ(bufferAccount(set, an::StackBuffer::OutputImage).heldBytes, 0u);
        EXPECT_EQ(bufferAccount(set, an::StackBuffer::OutputImage).allocations, 0u);
        EXPECT_EQ(set.account().handedOverBytes, plan.memory.outputImageBytes);
        EXPECT_EQ(set.account().heldBytes, persistent - plan.memory.outputImageBytes);
        EXPECT_EQ(set.releaseOutputImage().status().code(), an::ErrorCode::InvalidState);

        an::Result<an::ValidityMask> releasedMask = set.releaseOutputMask();
        ASSERT_TRUE(releasedMask.hasValue()) << releasedMask.status().message();
        mask = std::move(*releasedMask);
        EXPECT_TRUE(mask.valid());
        EXPECT_EQ(mask.extent(), small.extent());
        EXPECT_FALSE(set.outputMask().has_value());
        EXPECT_EQ(set.releaseOutputMask().status().code(), an::ErrorCode::InvalidState);

        an::Result<an::ImageStorageBytes> releasedCoverage = set.releaseOutputMap(an::StackOutputMap::Coverage);
        ASSERT_TRUE(releasedCoverage.hasValue()) << releasedCoverage.status().message();
        coverage = std::move(*releasedCoverage);
        EXPECT_EQ(coverage.capacityBytes(), kElements * an::kStackMapSampleBytes);
        EXPECT_TRUE(set.outputCountMap(an::StackOutputMap::Coverage).empty());
        EXPECT_EQ(set.outputCountMap(an::StackOutputMap::RejectionLow).size(), kElements);
        EXPECT_EQ(bufferAccount(set, an::StackBuffer::OutputMaps).allocations, 2u);
        EXPECT_EQ(set.releaseOutputMap(an::StackOutputMap::Coverage).status().code(), an::ErrorCode::InvalidState);
        const an::Result<an::ImageStorageBytes> unrequested = set.releaseOutputMap(an::StackOutputMap::Variance);
        EXPECT_EQ(unrequested.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(textField(unrequested.status(), an::ContextKey::Parameter), "map");

        handedOver = set.account().handedOverBytes;
        EXPECT_EQ(handedOver,
                  plan.memory.outputImageBytes + plan.memory.outputMaskBytes + kElements * an::kStackMapSampleBytes);
        EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes);
    }
    // The set is gone; what was handed over is still outstanding and still readable.
    EXPECT_EQ(liveBytes(engine), handedOver);
    EXPECT_GT(engine.outstandingLeases(), 1u);

    {
        an::Result<an::ImageStorage<float>> typed = an::ImageStorage<float>::fromBytes(std::move(image));
        ASSERT_TRUE(typed.hasValue()) << typed.status().message();
        const an::Result<an::Image<float>> result =
            an::Image<float>::fromStorage(std::move(*typed), model.outputDescriptor());
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_TRUE(result->valid());

        an::Result<an::ImageStorage<std::uint32_t>> countStorage =
            an::ImageStorage<std::uint32_t>::fromBytes(std::move(coverage));
        ASSERT_TRUE(countStorage.hasValue()) << countStorage.status().message();
        const std::span<const std::uint32_t> written(countStorage->data(), kElements);
        for (const std::uint32_t count : written)
        {
            EXPECT_EQ(count, 7u);
        }
    }
    EXPECT_EQ(liveBytes(engine), plan.memory.outputMaskBytes);
    mask.release();
    EXPECT_EQ(liveBytes(engine), 0u);
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}

TEST(StackWorkingSetTest, APlanThatDoesNotDescribeTheModelIsRefusedByName)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(fourPassRequest());
    // A plan made for one worker, taken before the two-thread execution below is admitted, since
    // the engine has two threads to lend and the executions take turns.
    an::StackPlan narrowPlan;
    {
        const an::ExecutionContext narrow = makeExecution(engine, nullptr, {}, 1u);
        narrowPlan = makePlan(narrow, model);
    }
    ASSERT_EQ(narrowPlan.memory.workers, 1u);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackPlan plan = makePlan(execution, model);

    const auto refusedNaming = [&](const an::StackPlan& tampered, std::string_view figure) {
        const an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, tampered);
        ASSERT_FALSE(made.hasValue()) << figure;
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments) << figure;
        EXPECT_TRUE(messageNames(made.status(), figure)) << figure << ": " << made.status().message();
        EXPECT_EQ(textField(made.status(), an::ContextKey::Parameter), "plan") << figure;
        EXPECT_EQ(liveBytes(engine), 0u) << figure;
    };

    an::StackPlan image = plan;
    image.memory.outputImageBytes += 1u;
    refusedNaming(image, "memory.outputImageBytes");

    an::StackPlan maps = plan;
    maps.memory.outputMapBytes += 4u;
    refusedNaming(maps, "memory.outputMapBytes");

    an::StackPlan grids = plan;
    grids.memory.localWeightGridBytes = 0u;
    refusedNaming(grids, "memory.localWeightGridBytes");

    an::StackPlan scratch = plan;
    scratch.memory.scratchBytes += an::kStackScratchAlignmentBytes;
    refusedNaming(scratch, "memory.scratchBytes");

    an::StackPlan rows = plan;
    rows.tile.rows = 0u;
    refusedNaming(rows, "tile.rows");

    an::StackPlan tiles = plan;
    tiles.tile.tileCount += 1u;
    refusedNaming(tiles, "tile.tileCount");

    an::StackPlan workers = plan;
    workers.memory.workers += 1u;
    refusedNaming(workers, "memory.workers");

    an::StackPlan reads = plan;
    reads.io.concurrentFrameReads = 0u;
    refusedNaming(reads, "io.concurrentFrameReads");

    // The local-weight measurement may not come before the normalization measurement; the pass
    // out of order is the second one.
    an::StackPlan order = plan;
    std::swap(order.passes.passes[0], order.passes.passes[1]);
    refusedNaming(order, "passes[1].kind");

    an::StackPlan combine = plan;
    combine.passes.passes[2].workingSetBytes -= 1u;
    refusedNaming(combine, "passes[2].workingSetBytes");

    an::StackPlan finalize = plan;
    finalize.passes.passes[3].kind = an::StackPassKind::Combine;
    refusedNaming(finalize, "passes[3].kind");

    an::StackPlan count = plan;
    count.passes.count = 1u;
    refusedNaming(count, "passes.count");

    an::StackPlan peak = plan;
    peak.memory.peakBytes += 1u;
    refusedNaming(peak, "memory.peakBytes");

    an::StackPlan strategy = plan;
    strategy.strategy.strategy = an::StackAccumulationStrategy::WholeOutputAccumulators;
    refusedNaming(strategy, "strategy.strategy");

    // A plan of another stack, and a plan made for another worker count, are refused the same way.
    const an::StackPlan smaller =
        makePlan(execution, makeModel(fourPassRequest(kFrames, makeDescriptor(an::SampleType::Float32, 32u, 32u))));
    refusedNaming(smaller, "tile.width");
    refusedNaming(narrowPlan, "memory.workers");
}

TEST(StackWorkingSetTest, TheAccountNamesEveryBufferWithItsPlannedAndHeldBytes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(fourPassRequest());
    an::StackPlanningRequest request;
    request.maximumTileRows = 6u;
    const an::StackPlan plan = makePlan(execution, model, request);
    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet& set = *made;

    const an::StackMemoryAccount idle = set.account();
    for (std::size_t index = 0u; index < an::kStackBufferCount; ++index)
    {
        const an::StackBufferAccount& entry = idle.buffers[index];
        EXPECT_EQ(entry.buffer, static_cast<an::StackBuffer>(index));
        EXPECT_EQ(entry.lifetime, an::stackBufferLifetimeOf(entry.buffer));
        if (entry.lifetime == an::StackBufferLifetime::Persistent)
        {
            EXPECT_EQ(entry.heldBytes, entry.plannedBytes) << an::stackBufferName(entry.buffer);
        }
        else
        {
            EXPECT_EQ(entry.heldBytes, 0u) << an::stackBufferName(entry.buffer);
        }
    }
    EXPECT_EQ(idle.buffers[static_cast<std::size_t>(an::StackBuffer::OutputMask)].plannedBytes,
              plan.memory.outputMaskBytes);
    EXPECT_EQ(idle.buffers[static_cast<std::size_t>(an::StackBuffer::FrameState)].plannedBytes,
              plan.memory.frameStateBytes);
    EXPECT_EQ(idle.buffers[static_cast<std::size_t>(an::StackBuffer::LocalWeightGrids)].plannedBytes,
              plan.memory.localWeightGridBytes);
    // With no pass open, the pass buffers report the combine pass's figures.
    EXPECT_EQ(idle.buffers[static_cast<std::size_t>(an::StackBuffer::WorkerScratch)].plannedBytes,
              plan.memory.scratchBytes);
    EXPECT_EQ(idle.buffers[static_cast<std::size_t>(an::StackBuffer::Tile)].plannedBytes, plan.memory.tileBytes);
    EXPECT_EQ(idle.buffers[static_cast<std::size_t>(an::StackBuffer::ReadBuffer)].plannedBytes,
              plan.memory.readBufferBytes);
    EXPECT_EQ(idle.buffers[static_cast<std::size_t>(an::StackBuffer::MeasurementWorkspace)].plannedBytes, 0u);
    EXPECT_EQ(idle.heldBytes, plan.memory.persistentBytes);

    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::MeasureNormalization).ok());
    const an::StackMemoryAccount measuring = set.account();
    const std::optional<an::StackPass> pass = passOf(plan, an::StackPassKind::MeasureNormalization);
    ASSERT_TRUE(pass.has_value());
    const an::StackBufferAccount& read = measuring.buffers[static_cast<std::size_t>(an::StackBuffer::ReadBuffer)];
    EXPECT_EQ(read.plannedBytes, plan.io.frameBytes);
    EXPECT_EQ(read.heldBytes, plan.io.frameBytes);
    EXPECT_EQ(read.allocations, 1u);
    const an::StackBufferAccount& workspace =
        measuring.buffers[static_cast<std::size_t>(an::StackBuffer::MeasurementWorkspace)];
    EXPECT_EQ(workspace.plannedBytes, pass->workingSetBytes - plan.memory.persistentBytes - plan.io.frameBytes -
                                          plan.memory.normalizationScratchBytes);
    EXPECT_EQ(workspace.heldBytes, workspace.plannedBytes);
    EXPECT_EQ(measuring.buffers[static_cast<std::size_t>(an::StackBuffer::WorkerScratch)].plannedBytes, 0u);
    EXPECT_EQ(measuring.buffers[static_cast<std::size_t>(an::StackBuffer::Tile)].plannedBytes, 0u);
    EXPECT_TRUE(measuring.passOpen);
    EXPECT_EQ(measuring.currentPass, an::StackPassKind::MeasureNormalization);
    EXPECT_EQ(measuring.heldBytes, pass->workingSetBytes - plan.memory.normalizationScratchBytes);
    set.endPass();
    EXPECT_EQ(set.account(), [&] {
        an::StackMemoryAccount expected = idle;
        expected.peakHeldBytes = pass->workingSetBytes - plan.memory.normalizationScratchBytes;
        expected.passesCompleted = 1u;
        return expected;
    }());
}

TEST(StackWorkingSetTest, PreparedStateIsCountedFromWhatTheCallerHoldsAndNeverTaken)
{
    std::vector<an::FrameNormalization> matches(kFrames);
    EXPECT_EQ(an::preparedStateBytesOf(std::span<const an::FrameNormalization>(matches)),
              kFrames * sizeof(an::FrameNormalization));

    an::LocalNormalizationDocument document;
    document.referenceBackground.assign(75u, 0.0);
    document.frames.resize(kFrames);
    for (an::LocalNormalizationFrameCoefficients& frame : document.frames)
    {
        frame.background.assign(75u, 0.0);
        frame.scaleGrid.assign(25u, 1.0);
    }
    std::uint64_t expectedDocument = sizeof(an::LocalNormalizationDocument);
    expectedDocument += document.referenceBackground.capacity() * sizeof(double);
    expectedDocument += document.frames.capacity() * sizeof(an::LocalNormalizationFrameCoefficients);
    for (const an::LocalNormalizationFrameCoefficients& frame : document.frames)
    {
        expectedDocument += (frame.background.capacity() + frame.scaleGrid.capacity()) * sizeof(double);
    }
    EXPECT_EQ(an::preparedStateBytesOf(document), expectedDocument);
    EXPECT_GT(expectedDocument, sizeof(an::LocalNormalizationDocument) + (75u + kFrames * 100u) * sizeof(double));

    std::vector<an::PhotometricStackWeight> weights(kFrames);
    EXPECT_EQ(an::preparedStateBytesOf(std::span<const an::PhotometricStackWeight>(weights)),
              kFrames * sizeof(an::PhotometricStackWeight));

    const an::StackPreparedState prepared{
        .normalization = matches, .localNormalization = &document, .photometricWeights = weights};
    const std::uint64_t total = an::preparedStateBytesOf(prepared);
    EXPECT_EQ(total, kFrames * sizeof(an::FrameNormalization) + expectedDocument +
                         kFrames * sizeof(an::PhotometricStackWeight));
    EXPECT_EQ(an::preparedStateBytesOf(an::StackPreparedState{.normalization = matches}),
              kFrames * sizeof(an::FrameNormalization));

    // The plan counts what the caller holds; the set takes everything but that.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = clippedRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::Additive;
    request.weighting.method = an::StackWeighting::Photometric;
    const an::StackModel model = makeModel(request);
    an::StackPlanningRequest planning;
    planning.normalizationPrepared = true;
    planning.preparedStateBytes = total;
    const an::StackPlan plan = makePlan(execution, model, planning);
    ASSERT_EQ(plan.passes.count, 2u);
    EXPECT_EQ(plan.memory.preparedStateBytes, total);

    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes - total);
    const an::StackMemoryAccount account = made->account();
    EXPECT_EQ(account.preparedStateBytes, total);
    EXPECT_EQ(account.plannedPersistentBytes, plan.memory.persistentBytes);
    EXPECT_EQ(account.heldBytes, plan.memory.persistentBytes - total);
    ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::Combine).ok());
    EXPECT_EQ(liveBytes(engine), plan.memory.peakBytes - total);
}

TEST(StackWorkingSetTest, APreparedPeakIsCheckedAgainstWhatTheSetItselfTakes)
{
    // The caller's bytes are resident already and are never taken here, so what is checked against
    // the engine context is the plan's peak less them, and what the set holds through the combine
    // pass is exactly that figure.
    const an::StackModel model = makeModel(clippedRequest());
    constexpr std::uint64_t kPrepared = 4096u;
    an::StackPlanningRequest planning;
    planning.preparedStateBytes = kPrepared;
    std::uint64_t own = 0u;
    {
        const an::EngineContext unbounded = makeEngine();
        const an::ExecutionContext execution = makeExecution(unbounded);
        const an::StackPlan plan = makePlan(execution, model, planning);
        own = plan.memory.peakBytes - kPrepared;
    }
    const an::EngineContext engine = makeEngine(2u, own);
    const an::ExecutionContext execution = makeExecution(engine);
    // The planner counts the caller's bytes against the same budget, so it tiles the frame; the
    // set's own share of that plan is within the budget.
    const an::StackPlan plan = makePlan(execution, model, planning);
    EXPECT_EQ(plan.tile.reason, an::StackTileReason::BudgetBound);
    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::Combine).ok());
    EXPECT_LE(liveBytes(engine), own);
    EXPECT_EQ(liveBytes(engine), plan.memory.peakBytes - kPrepared);
    EXPECT_EQ(made->account().availableBytes, own);
}

TEST(StackWorkingSetTest, MapsAreTypedByWhatTheyCountAndRequestedByTheOutput)
{
    for (const an::StackOutputMap map : {an::StackOutputMap::RejectionLow, an::StackOutputMap::RejectionHigh,
                                         an::StackOutputMap::RejectionMerged, an::StackOutputMap::Coverage})
    {
        EXPECT_EQ(an::stackOutputMapSampleType(map), an::SampleType::UInt32) << an::stackOutputMapName(map);
    }
    for (const an::StackOutputMap map :
         {an::StackOutputMap::Variance, an::StackOutputMap::SignalToNoiseGain, an::StackOutputMap::WeightSum})
    {
        EXPECT_EQ(an::stackOutputMapSampleType(map), an::SampleType::Float32) << an::stackOutputMapName(map);
    }

    an::StackOutputRequest none;
    EXPECT_EQ(an::stackOutputMapCount(none), 0u);
    for (std::uint8_t index = 0u; index < an::kStackOutputMapCount; ++index)
    {
        EXPECT_FALSE(an::stackOutputMapRequested(none, static_cast<an::StackOutputMap>(index)));
    }

    an::StackOutputRequest merged;
    merged.rejectionMaps = an::RejectionMapForm::Merged;
    EXPECT_EQ(an::stackOutputMapCount(merged), 1u);
    EXPECT_TRUE(an::stackOutputMapRequested(merged, an::StackOutputMap::RejectionMerged));
    EXPECT_FALSE(an::stackOutputMapRequested(merged, an::StackOutputMap::RejectionLow));
    EXPECT_FALSE(an::stackOutputMapRequested(merged, an::StackOutputMap::RejectionHigh));

    an::StackOutputRequest everything;
    everything.rejectionMaps = an::RejectionMapForm::LowAndHigh;
    everything.coverageMap = true;
    everything.varianceMap = true;
    everything.signalToNoiseGainMap = true;
    everything.weightSumMap = true;
    EXPECT_EQ(an::stackOutputMapCount(everything), 6u);
    EXPECT_TRUE(an::stackOutputMapRequested(everything, an::StackOutputMap::RejectionLow));
    EXPECT_TRUE(an::stackOutputMapRequested(everything, an::StackOutputMap::RejectionHigh));
    EXPECT_FALSE(an::stackOutputMapRequested(everything, an::StackOutputMap::RejectionMerged));
    EXPECT_TRUE(an::stackOutputMapRequested(everything, an::StackOutputMap::WeightSum));
    EXPECT_FALSE(an::stackOutputMapRequested(everything, static_cast<an::StackOutputMap>(9)));
}

TEST(StackWorkingSetTest, AccumulatorsArePresentOnlyForWhatTheStackKeeps)
{
    struct Case
    {
        an::StackCombination combination;
        an::StackRejection rejection;
        an::StackWeighting weighting;
        bool gain;
        bool variance;
        std::array<bool, an::kStackAccumulatorCount> kept;
    };
    const std::array<Case, 6u> cases{{
        {an::StackCombination::Average,
         an::StackRejection::None,
         an::StackWeighting::Uniform,
         false,
         false,
         {true, true, false, false, false}},
        {an::StackCombination::Sum,
         an::StackRejection::None,
         an::StackWeighting::Explicit,
         false,
         false,
         {true, true, true, false, false}},
        {an::StackCombination::Maximum,
         an::StackRejection::None,
         an::StackWeighting::Uniform,
         true,
         false,
         {true, true, false, true, false}},
        {an::StackCombination::Average,
         an::StackRejection::None,
         an::StackWeighting::Uniform,
         false,
         true,
         {true, true, false, false, true}},
        {an::StackCombination::Average,
         an::StackRejection::SigmaClipping,
         an::StackWeighting::Uniform,
         true,
         true,
         {false, false, false, false, false}},
        {an::StackCombination::Median,
         an::StackRejection::None,
         an::StackWeighting::Uniform,
         false,
         false,
         {false, false, false, false, false}},
    }};
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const Case& item : cases)
    {
        an::StackRequest request = baseRequest(3u, makeDescriptor(an::SampleType::Float32, 8u, 4u));
        request.combination = item.combination;
        request.rejection.method = item.rejection;
        request.weighting.method = item.weighting;
        request.output.signalToNoiseGainMap = item.gain;
        request.output.varianceMap = item.variance;
        const an::StackModel model = makeModel(request);
        std::uint64_t expectedBytes = 0u;
        for (std::size_t index = 0u; index < an::kStackAccumulatorCount; ++index)
        {
            const auto accumulator = static_cast<an::StackAccumulator>(index);
            EXPECT_EQ(an::stackAccumulatorKept(model, accumulator), item.kept[index])
                << an::stackCombinationName(item.combination) << '/' << an::stackAccumulatorName(accumulator);
            expectedBytes += item.kept[index] ? 32u * an::stackAccumulatorBytesPerElement(accumulator) : 0u;
        }
        const an::StackPlan plan = makePlan(execution, model);
        EXPECT_EQ(plan.memory.accumulatorBytes, expectedBytes) << an::stackCombinationName(item.combination);

        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        EXPECT_EQ(made->accumulatorSums().empty(), !item.kept[0]);
        EXPECT_EQ(made->accumulatorCounts().empty(), !item.kept[1]);
        EXPECT_EQ(made->accumulatorWeightSums().empty(), !item.kept[2]);
        EXPECT_EQ(made->accumulatorWeightSquareSums().empty(), !item.kept[3]);
        EXPECT_EQ(made->accumulatorVarianceMoments().empty(), !item.kept[4]);
        EXPECT_EQ(bufferAccount(*made, an::StackBuffer::Accumulators).heldBytes, expectedBytes);
    }
    EXPECT_EQ(an::stackAccumulatorBytesPerElement(an::StackAccumulator::Sums), an::kStackAccumulatorBytes);
    EXPECT_EQ(an::stackAccumulatorBytesPerElement(an::StackAccumulator::Counts), an::kStackCountBytes);
    EXPECT_EQ(an::stackAccumulatorBytesPerElement(static_cast<an::StackAccumulator>(9)), 0u);
}

TEST(StackWorkingSetTest, AFloat64StackHoldsAFloat64TileAndOutput)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = clippedRequest(kFrames, makeDescriptor(an::SampleType::Float64, 16u, 8u));
    request.output.sampleType = an::SampleType::Float64;
    const an::StackModel model = makeModel(request);
    an::StackPlanningRequest planning;
    planning.maximumTileRows = 3u;
    const an::StackPlan plan = makePlan(execution, model, planning);
    ASSERT_EQ(plan.precision.tileSampleType, an::SampleType::Float64);

    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    const std::optional<an::ImageLayout> layout = made->outputImageLayout();
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->elementSizeBytes(), 8u);
    EXPECT_EQ(made->outputImage().size(), 16u * 8u * sizeof(double));
    ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::Combine).ok());
    EXPECT_EQ(made->tile().size(), static_cast<std::size_t>(3u) * 16u * kFrames * sizeof(double));
    EXPECT_EQ(made->tile().size(), plan.memory.tileBytes);
    EXPECT_EQ(made->readBuffer().size(), 16u * 8u * sizeof(double));
}

TEST(StackWorkingSetTest, TheSetReportsWhatItTookThroughTheDiagnosticSink)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(clippedRequest());
    an::StackMemoryAccount heard;
    {
        RecordingDiagnosticSink sink;
        const an::ExecutionContext execution = makeExecution(engine, &sink);
        const an::StackPlan plan = makePlan(execution, model);
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        const std::vector<RecordingDiagnosticSink::Event> created = sink.from("StackWorkingSet::create");
        ASSERT_EQ(created.size(), 1u);
        EXPECT_EQ(created[0].severity, an::DiagnosticSeverity::Info);
        EXPECT_EQ(created[0].operationId, execution.operationId());
        EXPECT_NE(created[0].message.find("Working set"), std::string::npos);
        EXPECT_NE(created[0].message.find(std::to_string(plan.memory.persistentBytes)), std::string::npos);
        EXPECT_NE(created[0].message.find("no budget bound"), std::string::npos);

        ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::Combine).ok());
        const std::vector<RecordingDiagnosticSink::Event> begun = sink.from("StackWorkingSet::beginPass");
        ASSERT_EQ(begun.size(), 1u);
        EXPECT_EQ(begun[0].operationId, execution.operationId());
        EXPECT_NE(begun[0].message.find("Pass 1 of 2, Combine"), std::string::npos);
        EXPECT_NE(begun[0].message.find(std::to_string(plan.memory.tileBytes)), std::string::npos);
        made->endPass();
        heard = made->account();
    }
    {
        const an::ExecutionContext silent = makeExecution(engine);
        const an::StackPlan plan = makePlan(silent, model);
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, silent, model, plan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        ASSERT_TRUE(made->beginPass(silent, an::StackPassKind::Combine).ok());
        made->endPass();
        EXPECT_EQ(made->account(), heard);
    }
}

TEST(StackWorkingSetTest, ASetIsMoveOnlyHoldsItsContextOpenAndReleasesOnDemand)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(clippedRequest());
    const an::StackPlan plan = makePlan(execution, model);
    const std::uint64_t leasesBefore = engine.outstandingLeases();

    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_GT(engine.outstandingLeases(), leasesBefore);
    const an::StackMemoryAccount before = made->account();
    const std::byte* const imageAddress = made->outputImage().data();

    an::StackWorkingSet moved(std::move(*made));
    EXPECT_FALSE(made->valid());
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.account(), before);
    EXPECT_EQ(moved.outputImage().data(), imageAddress);
    EXPECT_EQ(made->account(), an::StackMemoryAccount{});
    EXPECT_TRUE(made->outputImage().empty());
    EXPECT_FALSE(made->plan().has_value());
    EXPECT_EQ(made->beginPass(execution, an::StackPassKind::Combine).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(made->releaseOutputImage().status().code(), an::ErrorCode::NotInitialized);

    an::StackWorkingSet assigned;
    EXPECT_FALSE(assigned.valid());
    assigned = std::move(moved);
    EXPECT_FALSE(moved.valid());
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(liveBytes(engine), plan.memory.persistentBytes);

    assigned.release();
    EXPECT_FALSE(assigned.valid());
    EXPECT_EQ(assigned.contextId(), 0u);
    EXPECT_EQ(liveBytes(engine), 0u);
    EXPECT_EQ(engine.outstandingLeases(), leasesBefore);
    assigned.release();
    assigned.endPass();
    EXPECT_EQ(assigned.passesCompleted(), 0u);
}

TEST(StackWorkingSetTest, EveryNameTableIsClosed)
{
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::OutputImage), "OutputImage");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::OutputMask), "OutputMask");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::OutputMaps), "OutputMaps");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::Accumulators), "Accumulators");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::FrameState), "FrameState");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::LocalWeightGrids), "LocalWeightGrids");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::WorkerScratch), "WorkerScratch");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::Tile), "Tile");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::ReadBuffer), "ReadBuffer");
    EXPECT_STREQ(an::stackBufferName(an::StackBuffer::MeasurementWorkspace), "MeasurementWorkspace");
    EXPECT_STREQ(an::stackBufferName(static_cast<an::StackBuffer>(99)), "UnknownStackBuffer");
    EXPECT_TRUE(an::isStackBuffer(an::StackBuffer::MeasurementWorkspace));
    EXPECT_FALSE(an::isStackBuffer(static_cast<an::StackBuffer>(99)));
    EXPECT_EQ(an::kStackBufferCount, 10u);

    EXPECT_STREQ(an::stackBufferLifetimeName(an::StackBufferLifetime::Persistent), "Persistent");
    EXPECT_STREQ(an::stackBufferLifetimeName(an::StackBufferLifetime::Pass), "Pass");
    EXPECT_STREQ(an::stackBufferLifetimeName(static_cast<an::StackBufferLifetime>(9)), "UnknownStackBufferLifetime");
    EXPECT_TRUE(an::isStackBufferLifetime(an::StackBufferLifetime::Pass));
    EXPECT_FALSE(an::isStackBufferLifetime(static_cast<an::StackBufferLifetime>(9)));
    EXPECT_EQ(an::stackBufferLifetimeOf(an::StackBuffer::OutputImage), an::StackBufferLifetime::Persistent);
    EXPECT_EQ(an::stackBufferLifetimeOf(an::StackBuffer::MeasurementWorkspace), an::StackBufferLifetime::Pass);

    EXPECT_STREQ(an::stackOutputMapName(an::StackOutputMap::RejectionLow), "RejectionLow");
    EXPECT_STREQ(an::stackOutputMapName(an::StackOutputMap::RejectionHigh), "RejectionHigh");
    EXPECT_STREQ(an::stackOutputMapName(an::StackOutputMap::RejectionMerged), "RejectionMerged");
    EXPECT_STREQ(an::stackOutputMapName(an::StackOutputMap::Coverage), "Coverage");
    EXPECT_STREQ(an::stackOutputMapName(an::StackOutputMap::Variance), "Variance");
    EXPECT_STREQ(an::stackOutputMapName(an::StackOutputMap::SignalToNoiseGain), "SignalToNoiseGain");
    EXPECT_STREQ(an::stackOutputMapName(an::StackOutputMap::WeightSum), "WeightSum");
    EXPECT_STREQ(an::stackOutputMapName(static_cast<an::StackOutputMap>(9)), "UnknownStackOutputMap");
    EXPECT_TRUE(an::isStackOutputMap(an::StackOutputMap::WeightSum));
    EXPECT_FALSE(an::isStackOutputMap(static_cast<an::StackOutputMap>(9)));
    EXPECT_EQ(an::kStackOutputMapCount, 7u);

    EXPECT_STREQ(an::stackAccumulatorName(an::StackAccumulator::Sums), "Sums");
    EXPECT_STREQ(an::stackAccumulatorName(an::StackAccumulator::Counts), "Counts");
    EXPECT_STREQ(an::stackAccumulatorName(an::StackAccumulator::WeightSums), "WeightSums");
    EXPECT_STREQ(an::stackAccumulatorName(an::StackAccumulator::WeightSquareSums), "WeightSquareSums");
    EXPECT_STREQ(an::stackAccumulatorName(an::StackAccumulator::VarianceMoments), "VarianceMoments");
    EXPECT_STREQ(an::stackAccumulatorName(static_cast<an::StackAccumulator>(9)), "UnknownStackAccumulator");
    EXPECT_TRUE(an::isStackAccumulator(an::StackAccumulator::VarianceMoments));
    EXPECT_FALSE(an::isStackAccumulator(static_cast<an::StackAccumulator>(9)));
    EXPECT_EQ(an::kStackAccumulatorCount, 5u);
}

} // namespace
