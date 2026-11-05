// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the stack planner: the accumulation strategy, the precision, the tile shape
// under every budget, the passes, the byte accounting, the read plan, the backend and the
// diagnostics that explain each of them.

#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
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

/// One operation in an engine, optionally listened to and narrowed.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, an::DiagnosticSink* sink = nullptr,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::uint32_t concurrency = an::kUseContextConcurrency)
{
    an::ExecutionOptions options;
    options.diagnosticSink = sink;
    options.backend = backend;
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

/// The checked model of a request every rule admits.
[[nodiscard]] an::StackModel makeModel(const an::StackRequest& request)
{
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    return model.has_value() ? *model : *an::StackModel::create(baseRequest());
}

/// An average with sigma clipping, which holds every sample of a pixel at once.
[[nodiscard]] an::StackRequest clippedRequest(std::uint32_t frames = kFrames,
                                              const an::ImageDescriptor& descriptor = makeDescriptor())
{
    an::StackRequest request = baseRequest(frames, descriptor);
    request.rejection.method = an::StackRejection::SigmaClipping;
    return request;
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

/// A diagnostic sink that keeps every event it was told.
class RecordingDiagnosticSink final : public an::DiagnosticSink
{
public:
    struct Event
    {
        an::DiagnosticSeverity severity = an::DiagnosticSeverity::Info;
        an::ErrorDomain domain = an::ErrorDomain::Unspecified;
        std::uint64_t operationId = 0u;
        std::string operation;
        std::string message;
    };

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_events.push_back(Event{.severity = event.severity,
                                 .domain = event.domain,
                                 .operationId = event.operationId,
                                 .operation = event.operation,
                                 .message = event.message});
    }

    [[nodiscard]] std::vector<Event> events() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_events;
    }

    [[nodiscard]] std::size_t count(an::DiagnosticSeverity severity) const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        std::size_t count = 0u;
        for (const Event& event : m_events)
        {
            count += event.severity == severity ? 1u : 0u;
        }
        return count;
    }

    [[nodiscard]] bool mentions(std::string_view fragment) const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        for (const Event& event : m_events)
        {
            if (event.message.find(fragment) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

private:
    mutable std::mutex m_guard;
    std::vector<Event> m_events;
};

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

/// The kinds of a plan's passes, in order.
[[nodiscard]] std::vector<an::StackPassKind> passKinds(const an::StackPlan& plan)
{
    std::vector<an::StackPassKind> kinds;
    for (std::size_t index = 0u; index < plan.passes.count; ++index)
    {
        kinds.push_back(plan.passes.passes[index].kind);
    }
    return kinds;
}

// The strategy, the tile sample type and the scratch contract are constant expressions, so a
// kernel can be written against them.
constexpr std::optional<an::ImageExtent> kConstantExtent =
    an::ImageExtent::create(16u, 16u, 1u, an::defaultEngineLimits());
static_assert(kConstantExtent.has_value());
constexpr std::optional<an::ImageDescriptor> kConstantDescriptor =
    an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *kConstantExtent,
                                                           .sampleType = an::SampleType::UInt16,
                                                           .channelSchema = an::ChannelSchema::Mono,
                                                           .colorSpace = an::ColorSpace::Unspecified,
                                                           .numericDomain = an::NumericDomain::NormalizedLinear,
                                                           .cfa = {},
                                                           .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                           .pixelAspectRatio = 1.0});
static_assert(kConstantDescriptor.has_value());
constexpr std::optional<an::StackModel> kConstantClipped = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 5u, .descriptor = *kConstantDescriptor},
                     .rejection = an::StackRejectionRequest{.method = an::StackRejection::SigmaClipping}});
static_assert(kConstantClipped.has_value());
static_assert(an::stackAccumulationStrategyOf(*kConstantClipped) == an::StackAccumulationStrategy::TileSampleHistory);
static_assert(an::stackStrategyReasonOf(*kConstantClipped) == an::StackStrategyReason::RejectionNeedsSampleHistory);
static_assert(an::stackTileSampleTypeOf(an::SampleType::UInt16) == an::SampleType::Float32);
static_assert(an::stackTileSampleTypeOf(an::SampleType::Float64) == an::SampleType::Float64);
static_assert(an::stackScratchBytesPerWorker(*kConstantClipped) ==
              3u * an::kStackScratchAlignmentBytes + 5u * an::kStackRejectionRowRecordBytes);
static_assert(an::stackGesdtMaximumOutliers(5u, 0.3) == 2u);
static_assert(an::stackGesdtMaximumOutliers(8u, 0.25) == 2u);
static_assert(an::stackGesdtMaximumOutliers(7u, 0.25) == 2u);
static_assert(an::stackGesdtMaximumOutliers(3u, 0.01) == 1u);
static_assert(an::localWeightGridNodesAlong(1u, 64u) == 1u);
static_assert(an::localWeightGridNodesAlong(64u, 64u) == 2u);
static_assert(an::localWeightGridNodesAlong(65u, 64u) == 2u);
static_assert(an::localWeightGridNodesAlong(129u, 64u) == 3u);

TEST(StackPlannerTest, RefusesAnExecutionThatHoldsNothing)
{
    const an::ExecutionContext empty;
    const an::Result<an::StackPlan> plan = an::planStack(empty, makeModel(baseRequest()), an::StackPlanningRequest{});
    ASSERT_FALSE(plan.hasValue());
    EXPECT_EQ(plan.status().code(), an::ErrorCode::NotInitialized);
}

TEST(StackPlannerTest, RefusesAnUnknownAccessAndNoConcurrentReads)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest());

    an::StackPlanningRequest unknownAccess;
    unknownAccess.access = static_cast<an::StackSourceAccess>(200);
    const an::Result<an::StackPlan> refusedAccess = an::planStack(execution, model, unknownAccess);
    ASSERT_FALSE(refusedAccess.hasValue());
    EXPECT_EQ(refusedAccess.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(refusedAccess.status(), an::ContextKey::Parameter), "access");

    an::StackPlanningRequest noReads;
    noReads.concurrentFrameReads = 0u;
    const an::Result<an::StackPlan> refusedReads = an::planStack(execution, model, noReads);
    ASSERT_FALSE(refusedReads.hasValue());
    EXPECT_EQ(refusedReads.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(refusedReads.status(), an::ContextKey::Parameter), "concurrentFrameReads");
}

TEST(StackPlannerTest, StreamingCombinationsAccumulateOverTheWholeOutputAndTheRestHoldSampleHistory)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    struct Case
    {
        an::StackCombination combination;
        an::StackRejection rejection;
        an::StackAccumulationStrategy strategy;
        an::StackStrategyReason reason;
    };
    const std::array<Case, 8u> cases{{
        {an::StackCombination::Average, an::StackRejection::None,
         an::StackAccumulationStrategy::WholeOutputAccumulators, an::StackStrategyReason::StreamingCombination},
        {an::StackCombination::Sum, an::StackRejection::None, an::StackAccumulationStrategy::WholeOutputAccumulators,
         an::StackStrategyReason::StreamingCombination},
        {an::StackCombination::Minimum, an::StackRejection::None,
         an::StackAccumulationStrategy::WholeOutputAccumulators, an::StackStrategyReason::StreamingCombination},
        {an::StackCombination::Maximum, an::StackRejection::None,
         an::StackAccumulationStrategy::WholeOutputAccumulators, an::StackStrategyReason::StreamingCombination},
        {an::StackCombination::Median, an::StackRejection::None, an::StackAccumulationStrategy::TileSampleHistory,
         an::StackStrategyReason::MedianNeedsSampleHistory},
        {an::StackCombination::Average, an::StackRejection::SigmaClipping,
         an::StackAccumulationStrategy::TileSampleHistory, an::StackStrategyReason::RejectionNeedsSampleHistory},
        {an::StackCombination::Average, an::StackRejection::GeneralizedEsdTest,
         an::StackAccumulationStrategy::TileSampleHistory, an::StackStrategyReason::RejectionNeedsSampleHistory},
        {an::StackCombination::AutoAdaptiveWeightedAverage, an::StackRejection::None,
         an::StackAccumulationStrategy::TileSampleHistory, an::StackStrategyReason::AutoAdaptiveNeedsSampleHistory},
    }};
    for (const Case& item : cases)
    {
        an::StackRequest request = baseRequest();
        request.combination = item.combination;
        request.rejection.method = item.rejection;
        const an::StackModel model = makeModel(request);
        EXPECT_EQ(an::stackAccumulationStrategyOf(model), item.strategy) << an::stackCombinationName(item.combination);
        EXPECT_EQ(an::stackStrategyReasonOf(model), item.reason) << an::stackCombinationName(item.combination);

        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_EQ(plan->strategy.strategy, item.strategy) << an::stackCombinationName(item.combination);
        EXPECT_EQ(plan->strategy.reason, item.reason) << an::stackCombinationName(item.combination);
    }
}

TEST(StackPlannerTest, PrecisionFollowsTheStorageAndTheAccumulationIsAlwaysFloat64)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::array<an::SampleType, 8u> kTypes{
        an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
        an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};
    for (const an::SampleType type : kTypes)
    {
        an::StackRequest request = baseRequest(kFrames, makeDescriptor(type));
        request.output.sampleType = an::SampleType::Float64;
        const an::StackModel model = makeModel(request);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << an::sampleTypeName(type) << ": " << plan.status().message();

        const an::StackPrecisionPlan& precision = plan->precision;
        EXPECT_EQ(precision.inputSampleType, type);
        EXPECT_EQ(precision.outputSampleType, an::SampleType::Float64);
        EXPECT_EQ(precision.accumulation, an::ComputePrecision::Float64);
        EXPECT_EQ(precision.justification, an::Float64Justification::StackingAccumulation);
        if (type == an::SampleType::Float64)
        {
            EXPECT_EQ(precision.tileSampleType, an::SampleType::Float64);
            EXPECT_EQ(precision.reason, an::StackPrecisionReason::StoredInFloat64);
        }
        else
        {
            EXPECT_EQ(precision.tileSampleType, an::SampleType::Float32) << an::sampleTypeName(type);
            EXPECT_EQ(precision.reason, an::StackPrecisionReason::DefaultFloat32) << an::sampleTypeName(type);
        }
        EXPECT_EQ(
            an::checkComputePrecision(type, model.outputContract(), precision.accumulation, precision.justification),
            an::PrecisionError::None)
            << an::sampleTypeName(type);
    }
}

TEST(StackPlannerTest, AnUnboundedBudgetPlansOneWholeFrameTile)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(clippedRequest());

    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->tile.width, kWidth);
    EXPECT_EQ(plan->tile.rows, kHeight);
    EXPECT_EQ(plan->tile.tileCount, 1u);
    EXPECT_EQ(plan->tile.requestedMaximumRows, 0u);
    EXPECT_EQ(plan->tile.reason, an::StackTileReason::WholeFrameUnbounded);
    EXPECT_FALSE(plan->memory.budgetBounded);
    EXPECT_EQ(plan->memory.budgetBytes, an::kDefaultMemoryBudgetBytes);
    EXPECT_EQ(plan->memory.availableBytes, an::kDefaultMemoryBudgetBytes);

    const std::vector<an::StackPassKind> kinds = passKinds(*plan);
    ASSERT_EQ(kinds.size(), 2u);
    EXPECT_EQ(kinds[0], an::StackPassKind::Combine);
    EXPECT_EQ(kinds[1], an::StackPassKind::Finalize);
    const std::optional<an::StackPass> combine = passOf(*plan, an::StackPassKind::Combine);
    ASSERT_TRUE(combine.has_value());
    EXPECT_EQ(combine->tileCount, 1u);
    EXPECT_EQ(combine->frameReads, kFrames);
    EXPECT_EQ(combine->inputBytes, plan->io.distinctInputBytes);
    const std::optional<an::StackPass> finalize = passOf(*plan, an::StackPassKind::Finalize);
    ASSERT_TRUE(finalize.has_value());
    EXPECT_EQ(finalize->frameReads, 0u);
    EXPECT_EQ(finalize->inputBytes, 0u);
    EXPECT_EQ(finalize->workingSetBytes, plan->memory.persistentBytes + plan->memory.diagnosticsScratchBytes);
    EXPECT_EQ(plan->io.inputPasses, 1u);
    EXPECT_EQ(plan->io.readsPerFrame, 1u);
    EXPECT_EQ(plan->io.inputMultiplier, 1u);
}

TEST(StackPlannerTest, ACallerRowCapSplitsTheFrameAndMultipliesOnlySampleHistoryReads)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackPlanningRequest request;
    request.maximumTileRows = 5u;

    const an::Result<an::StackPlan> history = an::planStack(execution, makeModel(clippedRequest()), request);
    ASSERT_TRUE(history.hasValue()) << history.status().message();
    EXPECT_EQ(history->tile.rows, 5u);
    EXPECT_EQ(history->tile.tileCount, 10u);
    EXPECT_EQ(history->tile.requestedMaximumRows, 5u);
    EXPECT_EQ(history->tile.reason, an::StackTileReason::CallerRowCap);
    const std::optional<an::StackPass> historyCombine = passOf(*history, an::StackPassKind::Combine);
    ASSERT_TRUE(historyCombine.has_value());
    EXPECT_EQ(historyCombine->tileCount, 10u);
    EXPECT_EQ(historyCombine->frameReads, 10u * kFrames);
    EXPECT_EQ(historyCombine->inputBytes, 10u * history->io.distinctInputBytes);
    EXPECT_EQ(history->io.readsPerFrame, 10u);
    EXPECT_EQ(history->io.inputMultiplier, 10u);
    EXPECT_EQ(history->io.logicalInputBytes, 10u * history->io.distinctInputBytes);

    const an::Result<an::StackPlan> streaming = an::planStack(execution, makeModel(baseRequest()), request);
    ASSERT_TRUE(streaming.hasValue()) << streaming.status().message();
    EXPECT_EQ(streaming->tile.rows, 5u);
    EXPECT_EQ(streaming->tile.tileCount, 10u);
    const std::optional<an::StackPass> streamingCombine = passOf(*streaming, an::StackPassKind::Combine);
    ASSERT_TRUE(streamingCombine.has_value());
    EXPECT_EQ(streamingCombine->frameReads, kFrames);
    EXPECT_EQ(streamingCombine->inputBytes, streaming->io.distinctInputBytes);
    EXPECT_EQ(streaming->io.readsPerFrame, 1u);
    EXPECT_EQ(streaming->io.inputMultiplier, 1u);
}

TEST(StackPlannerTest, RegionAccessReadsEachTileOnceWithoutMultiplyingTheInput)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackPlanningRequest request;
    request.access = an::StackSourceAccess::Region;
    request.maximumTileRows = 8u;

    const an::Result<an::StackPlan> history = an::planStack(execution, makeModel(clippedRequest()), request);
    ASSERT_TRUE(history.hasValue()) << history.status().message();
    EXPECT_EQ(history->tile.tileCount, 6u);
    const std::optional<an::StackPass> combine = passOf(*history, an::StackPassKind::Combine);
    ASSERT_TRUE(combine.has_value());
    EXPECT_EQ(combine->frameReads, 6u * kFrames);
    EXPECT_EQ(combine->inputBytes, history->io.distinctInputBytes);
    EXPECT_EQ(history->io.readsPerFrame, 6u);
    EXPECT_EQ(history->io.inputMultiplier, 1u);
    EXPECT_EQ(history->io.access, an::StackSourceAccess::Region);

    // A region read buffer holds one band, not one frame.
    const std::uint64_t bandBytes =
        static_cast<std::uint64_t>(kWidth) * 8u * an::sampleTypeSizeBytes(an::SampleType::Float32);
    EXPECT_EQ(history->memory.readBufferBytes, bandBytes);

    const an::Result<an::StackPlan> streaming = an::planStack(execution, makeModel(baseRequest()), request);
    ASSERT_TRUE(streaming.hasValue()) << streaming.status().message();
    const std::optional<an::StackPass> streamingCombine = passOf(*streaming, an::StackPassKind::Combine);
    ASSERT_TRUE(streamingCombine.has_value());
    EXPECT_EQ(streamingCombine->frameReads, 6u * kFrames);
    EXPECT_EQ(streamingCombine->inputBytes, streaming->io.distinctInputBytes);
}

TEST(StackPlannerTest, RegionAccessBatchesAStreamingTileAndBandsASampleHistoryTile)
{
    // Under region access the executor holds every frame's band of a tile at once and folds them
    // into the terms in one pass whose frame loop is inside the row loop, so a streaming tile
    // needs no band to stay cache-resident and stays the whole frame; a sample-history tile is
    // written by the source path and read back by the column kernels, and it stays cache-resident
    // in one band per admitted worker, each targeting kStackCacheBandBytes.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor large = makeDescriptor(an::SampleType::Float32, 4096u, 4096u);
    const an::StackModel model = makeModel(baseRequest(kFrames, large));

    an::StackPlanningRequest request;
    request.access = an::StackSourceAccess::Region;
    const an::Result<an::StackPlan> streaming = an::planStack(execution, model, request);
    ASSERT_TRUE(streaming.hasValue()) << streaming.status().message();
    EXPECT_EQ(streaming->tile.rows, 4096u);
    EXPECT_EQ(streaming->tile.tileCount, 1u);
    EXPECT_EQ(streaming->tile.reason, an::StackTileReason::WholeFrameUnbounded);
    // The tile holds one band per frame at the tile width, which is what one call over every
    // frame reads; a whole-frame streaming plan holds no tile at all for a frame stored at the
    // tile width.
    EXPECT_EQ(streaming->memory.tileBytesPerRow,
              static_cast<std::uint64_t>(4096u) * kFrames * an::sampleTypeSizeBytes(an::SampleType::Float32));
    EXPECT_EQ(streaming->memory.tileBytes, streaming->memory.tileBytesPerRow * 4096u);
    EXPECT_EQ(streaming->io.readsPerFrame, 1u);
    EXPECT_EQ(streaming->io.inputMultiplier, 1u);
    const an::Result<an::StackPlan> whole = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();
    EXPECT_EQ(whole->memory.tileBytesPerRow, 0u);
    EXPECT_EQ(whole->tile.reason, an::StackTileReason::WholeFrameUnbounded);

    // A caller cap still splits a streaming region tile.
    request.maximumTileRows = 64u;
    const an::Result<an::StackPlan> capped = an::planStack(execution, model, request);
    ASSERT_TRUE(capped.hasValue()) << capped.status().message();
    EXPECT_EQ(capped->tile.rows, 64u);
    EXPECT_EQ(capped->tile.reason, an::StackTileReason::CallerRowCap);
    EXPECT_EQ(capped->io.readsPerFrame, 64u);
    request.maximumTileRows = 0u;

    // The sample-history tile: every frame's row of the tile plus its share of the output.
    const an::StackModel clipped = makeModel(clippedRequest(kFrames, large));
    const std::uint64_t perRow = static_cast<std::uint64_t>(4096u) * kFrames * 4u + 4096u * 4u;
    const auto expectedRows =
        static_cast<std::uint32_t>(an::kStackCacheBandBytes / perRow) * execution.admittedConcurrency();
    ASSERT_GT(expectedRows, 0u);
    const an::Result<an::StackPlan> history = an::planStack(execution, clipped, request);
    ASSERT_TRUE(history.hasValue()) << history.status().message();
    EXPECT_EQ(history->tile.rows, expectedRows);
    EXPECT_EQ(history->tile.tileCount, (4096u + expectedRows - 1u) / expectedRows);
    EXPECT_EQ(history->tile.reason, an::StackTileReason::CacheBand);
    EXPECT_EQ(history->tile.requestedMaximumRows, 0u);
    EXPECT_EQ(history->io.readsPerFrame, history->tile.tileCount);
    EXPECT_EQ(history->io.inputMultiplier, 1u);
    EXPECT_LE(history->memory.tileBytes + history->memory.outputImageBytes / 4096u * history->tile.rows,
              an::kStackCacheBandBytes * execution.admittedConcurrency());

    // A caller cap below the band decides; one above it is a maximum the band stays under.
    request.maximumTileRows = expectedRows / 2u;
    const an::Result<an::StackPlan> under = an::planStack(execution, clipped, request);
    ASSERT_TRUE(under.hasValue()) << under.status().message();
    EXPECT_EQ(under->tile.rows, expectedRows / 2u);
    EXPECT_EQ(under->tile.reason, an::StackTileReason::CallerRowCap);
    request.maximumTileRows = expectedRows * 4u;
    const an::Result<an::StackPlan> over = an::planStack(execution, clipped, request);
    ASSERT_TRUE(over.hasValue()) << over.status().message();
    EXPECT_EQ(over->tile.rows, expectedRows);
    EXPECT_EQ(over->tile.reason, an::StackTileReason::CacheBand);
    EXPECT_EQ(over->tile.requestedMaximumRows, expectedRows * 4u);
    request.maximumTileRows = 0u;

    // Under whole-frame access a history tile stays whole, and a frame the band would not split
    // stays whole under region access.
    const an::Result<an::StackPlan> historyWhole = an::planStack(execution, clipped, an::StackPlanningRequest{});
    ASSERT_TRUE(historyWhole.hasValue()) << historyWhole.status().message();
    EXPECT_EQ(historyWhole->tile.reason, an::StackTileReason::WholeFrameUnbounded);
    const an::Result<an::StackPlan> small = an::planStack(execution, makeModel(clippedRequest()), request);
    ASSERT_TRUE(small.hasValue()) << small.status().message();
    EXPECT_EQ(small->tile.rows, kHeight);
    EXPECT_EQ(small->tile.reason, an::StackTileReason::WholeFrameUnbounded);
}

TEST(StackPlannerTest, ASourcesGranularityWidensARegionTileWhereTheBudgetAllows)
{
    // A source that decodes pieces spanning many rows - a compressed file's subblocks - decodes a piece again for
    // every tile that reaches it, so under region access the tile is widened to the rows one piece spans, past the
    // cache band of a sample history. The budget and the caller's cap still decide a narrower tile, and whole-frame
    // access, which reads each frame once, has no use for it.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor large = makeDescriptor(an::SampleType::Float32, 4096u, 4096u);
    const an::StackModel clipped = makeModel(clippedRequest(kFrames, large));

    an::StackPlanningRequest request;
    request.access = an::StackSourceAccess::Region;
    const an::Result<an::StackPlan> banded = an::planStack(execution, clipped, request);
    ASSERT_TRUE(banded.hasValue()) << banded.status().message();
    ASSERT_EQ(banded->tile.reason, an::StackTileReason::CacheBand);
    const std::uint32_t band = banded->tile.rows;
    ASSERT_LT(band * 8u, 4096u);

    request.sourceGranularityRows = band * 4u;
    const an::Result<an::StackPlan> widened = an::planStack(execution, clipped, request);
    ASSERT_TRUE(widened.hasValue()) << widened.status().message();
    EXPECT_EQ(widened->tile.rows, band * 4u);
    EXPECT_EQ(widened->tile.reason, an::StackTileReason::SourceGranularity);
    EXPECT_EQ(widened->tile.tileCount, (4096u + band * 4u - 1u) / (band * 4u));
    EXPECT_EQ(widened->io.readsPerFrame, widened->tile.tileCount);
    EXPECT_EQ(widened->memory.tileBytes, widened->memory.tileBytesPerRow * band * 4u);

    // A granularity the band already covers changes nothing, and one past the frame is the whole frame.
    request.sourceGranularityRows = band;
    const an::Result<an::StackPlan> covered = an::planStack(execution, clipped, request);
    ASSERT_TRUE(covered.hasValue()) << covered.status().message();
    EXPECT_EQ(covered->tile, banded->tile);
    request.sourceGranularityRows = 5000u;
    const an::Result<an::StackPlan> whole = an::planStack(execution, clipped, request);
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();
    EXPECT_EQ(whole->tile.rows, 4096u);
    EXPECT_EQ(whole->tile.tileCount, 1u);
    EXPECT_EQ(whole->tile.reason, an::StackTileReason::WholeFrameUnbounded);

    // A budget that holds fewer rows than a piece spans decides the tile; a piece it holds is still widened to.
    request.sourceGranularityRows = 4096u;
    request.workingSetLimitBytes = widened->memory.peakBytes;
    const an::Result<an::StackPlan> bounded = an::planStack(execution, clipped, request);
    ASSERT_TRUE(bounded.hasValue()) << bounded.status().message();
    EXPECT_EQ(bounded->tile.reason, an::StackTileReason::BudgetBound);
    EXPECT_LT(bounded->tile.rows, 4096u);
    EXPECT_GT(bounded->tile.rows, band * 2u);
    EXPECT_LE(bounded->memory.peakBytes, request.workingSetLimitBytes);
    request.sourceGranularityRows = band * 2u;
    const an::Result<an::StackPlan> held = an::planStack(execution, clipped, request);
    ASSERT_TRUE(held.hasValue()) << held.status().message();
    EXPECT_EQ(held->tile.rows, band * 2u);
    EXPECT_EQ(held->tile.reason, an::StackTileReason::SourceGranularity);
    request.workingSetLimitBytes = 0u;

    // The caller's cap below the piece decides.
    request.sourceGranularityRows = band * 4u;
    request.maximumTileRows = band * 2u;
    const an::Result<an::StackPlan> capped = an::planStack(execution, clipped, request);
    ASSERT_TRUE(capped.hasValue()) << capped.status().message();
    EXPECT_EQ(capped->tile.rows, band * 2u);
    EXPECT_EQ(capped->tile.reason, an::StackTileReason::CallerRowCap);
    request.maximumTileRows = 0u;

    // Whole-frame access reads each frame once whatever its pieces.
    request.access = an::StackSourceAccess::WholeFrame;
    const an::Result<an::StackPlan> frames = an::planStack(execution, clipped, request);
    ASSERT_TRUE(frames.hasValue()) << frames.status().message();
    EXPECT_EQ(frames->tile.rows, 4096u);
    EXPECT_EQ(frames->tile.reason, an::StackTileReason::WholeFrameUnbounded);
}

TEST(StackPlannerTest, RegionAccessSizesTheTermsOfAnElementwiseStackWithTheTile)
{
    // Under region access a mean or an extremum that nothing weighs or normalizes is final for a
    // tile's rows once the tile is combined, so its terms hold one tile: they are part of a row's
    // cost when a budget chooses the tile, and part of the persistent state once it is chosen.
    const an::StackModel model = makeModel(baseRequest());
    const std::uint64_t termsPerRow = static_cast<std::uint64_t>(kWidth) * (8u + 4u);
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::StackPlan> whole = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();
    EXPECT_EQ(whole->memory.accumulatorBytes, termsPerRow * kHeight);

    an::StackPlanningRequest region;
    region.access = an::StackSourceAccess::Region;
    region.maximumTileRows = 2u;
    const an::Result<an::StackPlan> capped = an::planStack(execution, model, region);
    ASSERT_TRUE(capped.hasValue()) << capped.status().message();
    EXPECT_EQ(capped->tile.rows, 2u);
    EXPECT_EQ(capped->memory.accumulatorBytes, termsPerRow * 2u);
    EXPECT_EQ(capped->memory.persistentBytes,
              whole->memory.persistentBytes - whole->memory.accumulatorBytes + capped->memory.accumulatorBytes);
    const std::optional<an::StackPass> combine = passOf(*capped, an::StackPassKind::Combine);
    ASSERT_TRUE(combine.has_value());
    EXPECT_EQ(combine->workingSetBytes, capped->memory.persistentBytes + capped->memory.scratchBytes +
                                            capped->memory.tileBytes + capped->memory.readBufferBytes);

    // A budget chooses the rows with the terms counted per row, and the plan it makes fits.
    const std::uint64_t perRow = capped->memory.tileBytesPerRow + capped->memory.readBufferBytes / 2u + termsPerRow;
    const std::uint64_t fixed =
        capped->memory.persistentBytes - capped->memory.accumulatorBytes + capped->memory.scratchBytes;
    const std::uint64_t budget = fixed + 5u * perRow + perRow / 2u;
    const an::EngineContext bounded = makeEngine(2u, budget);
    const an::ExecutionContext boundedExecution = makeExecution(bounded);
    region.maximumTileRows = 0u;
    const an::Result<an::StackPlan> fitted = an::planStack(boundedExecution, model, region);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_EQ(fitted->tile.rows, 5u);
    EXPECT_EQ(fitted->tile.reason, an::StackTileReason::BudgetBound);
    EXPECT_EQ(fitted->memory.accumulatorBytes, termsPerRow * 5u);
    EXPECT_LE(fitted->memory.peakBytes, budget);
    EXPECT_GT(fitted->memory.peakBytes + perRow, budget);

    // A sum keeps the whole output's terms under region access.
    an::StackRequest sum = baseRequest();
    sum.combination = an::StackCombination::Sum;
    region.maximumTileRows = 2u;
    const an::Result<an::StackPlan> summed = an::planStack(execution, makeModel(sum), region);
    ASSERT_TRUE(summed.hasValue()) << summed.status().message();
    EXPECT_EQ(summed->memory.accumulatorBytes, termsPerRow * kHeight);
}

TEST(StackPlannerTest, EveryBufferOfASmallStackIsAccountedByName)
{
    // 8 by 4 monochrome Float32 frames, three of them, averaged without rejection: the output image
    // is 128 bytes, its packed mask one byte per row, the whole-output sum 256 and the count 128,
    // and the frame table three doubles followed by the executor's bookkeeping - two counters per
    // frame, a four-bin coverage histogram and one slice record per worker, 112 bytes for three
    // frames on two workers.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor small = makeDescriptor(an::SampleType::Float32, 8u, 4u);

    const an::Result<an::StackPlan> plain =
        an::planStack(execution, makeModel(baseRequest(3u, small)), an::StackPlanningRequest{});
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_EQ(plain->memory.outputImageBytes, 128u);
    EXPECT_EQ(plain->memory.outputMaskBytes, an::packedMaskRowBytes(8u) * 4u);
    EXPECT_EQ(plain->memory.outputMapBytes, 0u);
    EXPECT_EQ(plain->memory.accumulatorBytes, 256u + 128u);
    // Two counters and a histogram bin per frame, a slice record per worker, and a band record
    // per frame, which every strategy keeps for the tile's bands.
    constexpr std::uint64_t kBookkeeping =
        2u * 3u * 8u + 4u * 8u + 2u * an::kStackWorkerSliceRecordBytes + 3u * an::kStackBandRecordBytes;
    EXPECT_EQ(an::stackFrameBookkeepingBytes(makeModel(baseRequest(3u, small)), 2u), kBookkeeping);
    EXPECT_EQ(plain->memory.frameStateBytes, 3u * 8u + kBookkeeping);
    EXPECT_EQ(plain->memory.localWeightGridBytes, 0u);
    EXPECT_EQ(plain->memory.preparedStateBytes, 0u);
    EXPECT_EQ(plain->memory.persistentBytes, 128u + 4u + 384u + 24u + kBookkeeping);
    EXPECT_EQ(plain->memory.scratchBytesPerWorker, 0u);
    EXPECT_EQ(plain->memory.scratchBytes, 0u);
    EXPECT_EQ(plain->memory.tileBytesPerRow, 0u);
    EXPECT_EQ(plain->memory.tileBytes, 0u);
    EXPECT_EQ(plain->memory.readBufferBytes, 128u);
    EXPECT_EQ(plain->memory.peakBytes, plain->memory.persistentBytes + 128u);
    EXPECT_EQ(plain->io.frameBytes, 128u);
    EXPECT_EQ(plain->io.distinctInputBytes, 3u * 128u);

    // Every requested map is one four-byte sample per output element, the variance map also needs
    // two running moments per element, the signal-to-noise gain map a sum of squared weights, and a
    // weighted stack a weight sum.
    an::StackRequest mapped = baseRequest(3u, small);
    mapped.output.coverageMap = true;
    mapped.output.varianceMap = true;
    mapped.output.signalToNoiseGainMap = true;
    mapped.output.weightSumMap = true;
    mapped.weighting.method = an::StackWeighting::Explicit;
    const an::Result<an::StackPlan> maps = an::planStack(execution, makeModel(mapped), an::StackPlanningRequest{});
    ASSERT_TRUE(maps.hasValue()) << maps.status().message();
    EXPECT_EQ(maps->memory.outputMapBytes, 4u * 32u * an::kStackMapSampleBytes);
    EXPECT_EQ(maps->memory.accumulatorBytes, 256u + 128u + 256u + 256u + 512u);

    // A rejection writes its maps directly and holds no whole-output accumulator; two maps are two
    // planes.
    an::StackRequest clipped = clippedRequest(3u, small);
    clipped.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
    const an::Result<an::StackPlan> rejected = an::planStack(execution, makeModel(clipped), an::StackPlanningRequest{});
    ASSERT_TRUE(rejected.hasValue()) << rejected.status().message();
    EXPECT_EQ(rejected->memory.outputMapBytes, 2u * 32u * an::kStackMapSampleBytes);
    EXPECT_EQ(rejected->memory.accumulatorBytes, 0u);
    EXPECT_EQ(rejected->memory.tileBytesPerRow, 8u * 3u * 4u);
    EXPECT_EQ(rejected->memory.tileBytes, 4u * 8u * 3u * 4u);

    // A global normalization keeps an offset and a scale per frame and channel beside the weights.
    an::StackRequest normalized = baseRequest(3u, small);
    normalized.normalization.scope = an::StackNormalizationScope::Global;
    normalized.normalization.globalMethod = an::NormalizationMethod::Additive;
    const an::Result<an::StackPlan> coefficients =
        an::planStack(execution, makeModel(normalized), an::StackPlanningRequest{});
    ASSERT_TRUE(coefficients.hasValue()) << coefficients.status().message();
    EXPECT_EQ(coefficients->memory.frameStateBytes, 3u * 8u + 3u * 2u * 8u + kBookkeeping);

    // Three channels triple every per-element figure and the mask planes.
    const an::ImageDescriptor color = makeDescriptor(an::SampleType::Float32, 8u, 4u, an::ChannelSchema::Rgb);
    const an::Result<an::StackPlan> rgb =
        an::planStack(execution, makeModel(baseRequest(3u, color)), an::StackPlanningRequest{});
    ASSERT_TRUE(rgb.hasValue()) << rgb.status().message();
    EXPECT_EQ(rgb->memory.outputImageBytes, 3u * 128u);
    EXPECT_EQ(rgb->memory.outputMaskBytes, 3u * 4u);
    EXPECT_EQ(rgb->memory.accumulatorBytes, 3u * 384u);
    EXPECT_EQ(rgb->memory.frameStateBytes, 3u * 3u * 8u + kBookkeeping);
}

TEST(StackPlannerTest, ScratchFollowsTheRejectionFamilyAndTheWorkers)
{
    constexpr std::uint64_t kAlign = an::kStackScratchAlignmentBytes;
    struct Case
    {
        an::StackCombination combination;
        an::StackRejection rejection;
        an::StackWeighting weighting;
        std::uint64_t alignedTerms;
    };
    // Five frames: every scalar term is under one alignment unit, so the scratch is the term count,
    // except the lane workspaces of the two column combinations, which hold kStackSampleColumnLanes
    // columns of five doubles each - five units for the median's selection workspace and ten for the
    // auto-adaptive mean's samples and weights.
    const std::array<Case, 10u> cases{{
        {an::StackCombination::Average, an::StackRejection::None, an::StackWeighting::Uniform, 0u},
        {an::StackCombination::Sum, an::StackRejection::None, an::StackWeighting::Explicit, 0u},
        {an::StackCombination::Median, an::StackRejection::None, an::StackWeighting::Uniform, 6u},
        {an::StackCombination::AutoAdaptiveWeightedAverage, an::StackRejection::None, an::StackWeighting::Uniform, 11u},
        {an::StackCombination::Average, an::StackRejection::SigmaClipping, an::StackWeighting::Uniform, 3u},
        {an::StackCombination::Average, an::StackRejection::MadSigmaClipping, an::StackWeighting::Uniform, 3u},
        {an::StackCombination::Average, an::StackRejection::PercentileClipping, an::StackWeighting::Uniform, 3u},
        {an::StackCombination::Average, an::StackRejection::WinsorizedSigmaClipping, an::StackWeighting::Uniform, 3u},
        {an::StackCombination::Average, an::StackRejection::MedianSigmaReplacement, an::StackWeighting::Explicit, 4u},
        {an::StackCombination::Average, an::StackRejection::LinearFitClipping, an::StackWeighting::Uniform, 5u},
    }};
    for (const Case& item : cases)
    {
        an::StackRequest request = baseRequest();
        request.combination = item.combination;
        request.rejection.method = item.rejection;
        request.weighting.method = item.weighting;
        const bool columnRecords = item.rejection == an::StackRejection::None &&
                                   (item.combination == an::StackCombination::Median ||
                                    item.combination == an::StackCombination::AutoAdaptiveWeightedAverage);
        EXPECT_EQ(an::stackScratchBytesPerWorker(makeModel(request)),
                  item.alignedTerms * kAlign +
                      (item.rejection != an::StackRejection::None ? kFrames * an::kStackRejectionRowRecordBytes : 0u) +
                      (columnRecords ? kFrames * an::kStackColumnFrameRecordBytes : 0u))
            << an::stackCombinationName(item.combination) << '/' << an::stackRejectionName(item.rejection);
    }

    // The generalized deviate test also holds its candidate records, so its scratch grows with the
    // outlier fraction and with the frame count.
    an::StackRequest gesdt = baseRequest();
    gesdt.rejection.method = an::StackRejection::GeneralizedEsdTest;
    const std::uint64_t fewOutliers = an::stackScratchBytesPerWorker(makeModel(gesdt));
    EXPECT_EQ(fewOutliers, 6u * kAlign + kFrames * an::kStackRejectionRowRecordBytes);
    gesdt.rejection.gesdt.maximumOutlierFraction = 0.9;
    const std::uint64_t moreOutliers = an::stackScratchBytesPerWorker(makeModel(gesdt));
    EXPECT_GT(moreOutliers, fewOutliers);
    gesdt.input.frameCount = 1000u;
    EXPECT_GT(an::stackScratchBytesPerWorker(makeModel(gesdt)), moreOutliers);

    // The plan multiplies the per-worker figure by the workers the operation was admitted. The two
    // executions take turns, because the engine has two threads to lend and an operation admitted
    // while another holds them would be narrowed by that rather than by its request.
    const an::EngineContext engine = makeEngine(2u);
    {
        const an::ExecutionContext wide = makeExecution(engine);
        const an::Result<an::StackPlan> widePlan =
            an::planStack(wide, makeModel(clippedRequest()), an::StackPlanningRequest{});
        ASSERT_TRUE(widePlan.hasValue()) << widePlan.status().message();
        EXPECT_EQ(widePlan->memory.workers, 2u);
        EXPECT_EQ(widePlan->backend.workers, 2u);
        EXPECT_EQ(widePlan->memory.scratchBytesPerWorker, 3u * kAlign + kFrames * an::kStackRejectionRowRecordBytes);
        EXPECT_EQ(widePlan->memory.scratchBytes, 2u * (3u * kAlign + kFrames * an::kStackRejectionRowRecordBytes));
    }
    {
        const an::ExecutionContext narrow = makeExecution(engine, nullptr, an::ComputeBackend::Automatic, 1u);
        const an::Result<an::StackPlan> narrowPlan =
            an::planStack(narrow, makeModel(clippedRequest()), an::StackPlanningRequest{});
        ASSERT_TRUE(narrowPlan.hasValue()) << narrowPlan.status().message();
        EXPECT_EQ(narrowPlan->memory.workers, 1u);
        EXPECT_EQ(narrowPlan->memory.scratchBytes, 3u * kAlign + kFrames * an::kStackRejectionRowRecordBytes);
    }
}

TEST(StackPlannerTest, ABoundedBudgetChoosesTheLargestBandThatFits)
{
    // Plan once without a bound to learn the fixed cost and the per-row cost, then bound the budget
    // to hold exactly ten rows and a little change.
    const an::StackModel model = makeModel(clippedRequest());
    an::StackMemoryPlan unbounded;
    {
        const an::EngineContext engine = makeEngine();
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        unbounded = plan->memory;
    }
    ASSERT_GT(unbounded.tileBytesPerRow, 0u);
    const std::uint64_t fixed = unbounded.persistentBytes + unbounded.scratchBytes + unbounded.readBufferBytes;
    const std::uint64_t budget = fixed + 10u * unbounded.tileBytesPerRow + unbounded.tileBytesPerRow / 2u;

    const an::EngineContext engine = makeEngine(2u, budget);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_TRUE(plan->memory.budgetBounded);
    EXPECT_EQ(plan->memory.budgetBytes, budget);
    EXPECT_EQ(plan->memory.availableBytes, budget);
    EXPECT_EQ(plan->tile.rows, 10u);
    EXPECT_EQ(plan->tile.tileCount, 5u);
    EXPECT_EQ(plan->tile.reason, an::StackTileReason::BudgetBound);
    EXPECT_EQ(plan->memory.tileBytes, 10u * unbounded.tileBytesPerRow);
    EXPECT_LE(plan->memory.peakBytes, budget);
    EXPECT_GT(plan->memory.peakBytes + unbounded.tileBytesPerRow, budget);
    for (std::size_t index = 0u; index < plan->passes.count; ++index)
    {
        EXPECT_LE(plan->passes.passes[index].workingSetBytes, budget);
    }

    // A budget that holds the whole frame keeps one tile and says the frame fits.
    const an::EngineContext roomy =
        makeEngine(2u, fixed + static_cast<std::uint64_t>(kHeight) * unbounded.tileBytesPerRow);
    const an::ExecutionContext roomyExecution = makeExecution(roomy);
    const an::Result<an::StackPlan> whole = an::planStack(roomyExecution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();
    EXPECT_EQ(whole->tile.rows, kHeight);
    EXPECT_EQ(whole->tile.tileCount, 1u);
    EXPECT_EQ(whole->tile.reason, an::StackTileReason::WholeFrameFits);
}

TEST(StackPlannerTest, ABudgetBelowThePersistentStateOrOneRowIsRefusedWithTheNumbers)
{
    const an::StackModel model = makeModel(clippedRequest());
    an::StackMemoryPlan unbounded;
    {
        const an::EngineContext engine = makeEngine();
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        unbounded = plan->memory;
    }

    {
        const an::EngineContext engine = makeEngine(2u, unbounded.persistentBytes - 1u);
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_FALSE(plan.hasValue());
        EXPECT_EQ(plan.status().code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_EQ(countField(plan.status(), an::ContextKey::Requested), unbounded.persistentBytes);
        EXPECT_EQ(countField(plan.status(), an::ContextKey::Available), unbounded.persistentBytes - 1u);
        EXPECT_EQ(countField(plan.status(), an::ContextKey::Limit), unbounded.persistentBytes - 1u);
        EXPECT_TRUE(messageNames(plan.status(), "persistent"));
    }

    {
        const std::uint64_t oneRow =
            unbounded.persistentBytes + unbounded.scratchBytes + unbounded.readBufferBytes + unbounded.tileBytesPerRow;
        const an::EngineContext engine = makeEngine(2u, oneRow - 1u);
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_FALSE(plan.hasValue());
        EXPECT_EQ(plan.status().code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_EQ(countField(plan.status(), an::ContextKey::Requested), oneRow);
        EXPECT_EQ(countField(plan.status(), an::ContextKey::Available), oneRow - 1u);
        EXPECT_TRUE(messageNames(plan.status(), "row"));

        const an::EngineContext exact = makeEngine(2u, oneRow);
        const an::ExecutionContext exactExecution = makeExecution(exact);
        const an::Result<an::StackPlan> fits = an::planStack(exactExecution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(fits.hasValue()) << fits.status().message();
        EXPECT_EQ(fits->tile.rows, 1u);
        EXPECT_EQ(fits->tile.tileCount, kHeight);
        EXPECT_EQ(fits->memory.peakBytes, oneRow);
    }
}

TEST(StackPlannerTest, AWholeFrameReadThatDoesNotFitNamesRegionAccessAsTheWayForward)
{
    // A streaming combination over whole frames has nothing to tile: the accumulators are the
    // output's size and the read buffer is a frame's. The refusal says so and what would bound it.
    const an::StackModel model = makeModel(baseRequest());
    an::StackMemoryPlan unbounded;
    {
        const an::EngineContext engine = makeEngine();
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        unbounded = plan->memory;
        EXPECT_EQ(unbounded.tileBytesPerRow, 0u);
    }
    const std::uint64_t needed = unbounded.persistentBytes + unbounded.readBufferBytes;
    const an::EngineContext engine = makeEngine(2u, needed - 1u);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_FALSE(plan.hasValue());
    EXPECT_EQ(plan.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(countField(plan.status(), an::ContextKey::Requested), needed);
    EXPECT_TRUE(messageNames(plan.status(), "region"));

    // Region access bounds the read buffer to a band and the same budget plans.
    an::StackPlanningRequest region;
    region.access = an::StackSourceAccess::Region;
    const an::Result<an::StackPlan> banded = an::planStack(execution, model, region);
    ASSERT_TRUE(banded.hasValue()) << banded.status().message();
    EXPECT_LT(banded->tile.rows, kHeight);
    EXPECT_EQ(banded->tile.reason, an::StackTileReason::BudgetBound);
    EXPECT_LE(banded->memory.peakBytes, needed - 1u);
}

TEST(StackPlannerTest, OutstandingBytesShrinkWhatTheContextCanOffer)
{
    const an::StackModel model = makeModel(clippedRequest());
    an::StackMemoryPlan unbounded;
    {
        const an::EngineContext engine = makeEngine();
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        unbounded = plan->memory;
    }
    const std::uint64_t fixed = unbounded.persistentBytes + unbounded.scratchBytes + unbounded.readBufferBytes;
    const std::uint64_t budget = fixed + 10u * unbounded.tileBytesPerRow;
    const an::EngineContext engine = makeEngine(2u, budget);
    const an::ExecutionContext execution = makeExecution(engine);

    an::MemoryResource* resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t held = 3u * unbounded.tileBytesPerRow;
    const an::Result<void*> block = resource->allocate(held, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(block.hasValue()) << block.status().message();

    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    resource->deallocate(*block, held, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->memory.budgetBytes, budget);
    EXPECT_EQ(plan->memory.availableBytes, budget - held);
    EXPECT_EQ(plan->tile.rows, 7u);
    EXPECT_LE(plan->memory.peakBytes, budget - held);
}

TEST(StackPlannerTest, ACallerLimitNarrowsTheBudgetAndCannotWidenIt)
{
    const an::StackModel model = makeModel(clippedRequest());
    an::StackMemoryPlan unbounded;
    {
        const an::EngineContext engine = makeEngine();
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        unbounded = plan->memory;
    }
    const std::uint64_t fixed = unbounded.persistentBytes + unbounded.scratchBytes + unbounded.readBufferBytes;

    {
        const an::EngineContext engine = makeEngine();
        const an::ExecutionContext execution = makeExecution(engine);
        an::StackPlanningRequest request;
        request.workingSetLimitBytes = fixed + 4u * unbounded.tileBytesPerRow;
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, request);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_TRUE(plan->memory.budgetBounded);
        EXPECT_EQ(plan->memory.budgetBytes, request.workingSetLimitBytes);
        EXPECT_EQ(plan->memory.availableBytes, request.workingSetLimitBytes);
        EXPECT_EQ(plan->tile.rows, 4u);
        EXPECT_EQ(plan->tile.reason, an::StackTileReason::BudgetBound);
    }

    {
        const std::uint64_t contextBudget = fixed + 6u * unbounded.tileBytesPerRow;
        const an::EngineContext engine = makeEngine(2u, contextBudget);
        const an::ExecutionContext execution = makeExecution(engine);
        an::StackPlanningRequest request;
        request.workingSetLimitBytes = fixed + 20u * unbounded.tileBytesPerRow;
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, request);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_EQ(plan->memory.budgetBytes, contextBudget);
        EXPECT_EQ(plan->tile.rows, 6u);
    }
}

TEST(StackPlannerTest, MeasurementPassesAppearOnlyForWhatIsNotPrepared)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::AdditiveScaling;
    request.localWeighting.enabled = true;
    const an::StackModel model = makeModel(request);

    const an::Result<an::StackPlan> everything = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(everything.hasValue()) << everything.status().message();
    const std::vector<an::StackPassKind> kinds = passKinds(*everything);
    ASSERT_EQ(kinds.size(), 4u);
    EXPECT_EQ(kinds[0], an::StackPassKind::MeasureNormalization);
    EXPECT_EQ(kinds[1], an::StackPassKind::MeasureLocalWeights);
    EXPECT_EQ(kinds[2], an::StackPassKind::Combine);
    EXPECT_EQ(kinds[3], an::StackPassKind::Finalize);
    EXPECT_EQ(everything->io.inputPasses, 3u);
    const std::optional<an::StackPass> normalization = passOf(*everything, an::StackPassKind::MeasureNormalization);
    ASSERT_TRUE(normalization.has_value());
    EXPECT_EQ(normalization->frameReads, kFrames);
    EXPECT_EQ(normalization->inputBytes, everything->io.distinctInputBytes);
    EXPECT_EQ(normalization->tileCount, 0u);
    EXPECT_GE(normalization->workingSetBytes, everything->memory.persistentBytes + everything->io.frameBytes);
    const std::optional<an::StackPass> local = passOf(*everything, an::StackPassKind::MeasureLocalWeights);
    ASSERT_TRUE(local.has_value());
    EXPECT_EQ(local->frameReads, kFrames);
    EXPECT_GE(local->workingSetBytes, everything->memory.persistentBytes + everything->io.frameBytes);
    EXPECT_EQ(everything->io.frameReads, 3u * kFrames);
    EXPECT_EQ(everything->io.logicalInputBytes, 3u * everything->io.distinctInputBytes);
    EXPECT_GT(everything->memory.measurementWorkspaceBytes, 0u);
    EXPECT_GT(everything->memory.localWeightGridBytes, 0u);
    EXPECT_EQ(everything->memory.localWeightGridBytes,
              static_cast<std::uint64_t>(kFrames) *
                  an::localWeightGridNodesAlong(kWidth, an::kDefaultLocalWeightGridSpacing) *
                  an::localWeightGridNodesAlong(kHeight, an::kDefaultLocalWeightGridSpacing) *
                  an::kLocalWeightNodeBytes);

    an::StackPlanningRequest prepared;
    prepared.normalizationPrepared = true;
    prepared.localWeightsPrepared = true;
    prepared.preparedStateBytes = 4096u;
    const an::Result<an::StackPlan> nothing = an::planStack(execution, model, prepared);
    ASSERT_TRUE(nothing.hasValue()) << nothing.status().message();
    const std::vector<an::StackPassKind> remaining = passKinds(*nothing);
    ASSERT_EQ(remaining.size(), 2u);
    EXPECT_EQ(remaining[0], an::StackPassKind::Combine);
    EXPECT_EQ(remaining[1], an::StackPassKind::Finalize);
    EXPECT_EQ(nothing->io.inputPasses, 1u);
    EXPECT_EQ(nothing->memory.preparedStateBytes, 4096u);
    EXPECT_EQ(nothing->memory.persistentBytes, everything->memory.persistentBytes + 4096u);
    EXPECT_EQ(nothing->memory.measurementWorkspaceBytes, 0u);

    an::StackPlanningRequest half;
    half.normalizationPrepared = true;
    const an::Result<an::StackPlan> weightsOnly = an::planStack(execution, model, half);
    ASSERT_TRUE(weightsOnly.hasValue()) << weightsOnly.status().message();
    const std::vector<an::StackPassKind> halfKinds = passKinds(*weightsOnly);
    ASSERT_EQ(halfKinds.size(), 3u);
    EXPECT_EQ(halfKinds[0], an::StackPassKind::MeasureLocalWeights);
}

TEST(StackPlannerTest, AMeasurementPassThatDoesNotFitIsRefusedByName)
{
    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::Additive;
    const an::StackModel model = makeModel(request);

    an::StackPlanningRequest region;
    region.access = an::StackSourceAccess::Region;
    std::uint64_t measurement = 0u;
    std::uint64_t persistent = 0u;
    {
        const an::EngineContext engine = makeEngine();
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, region);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const std::optional<an::StackPass> pass = passOf(*plan, an::StackPassKind::MeasureNormalization);
        ASSERT_TRUE(pass.has_value());
        measurement = pass->workingSetBytes;
        persistent = plan->memory.persistentBytes;
        EXPECT_GT(measurement, persistent);
    }
    // The combine pass reads bands under region access, so a budget between the persistent state
    // and the whole-frame measurement pass fails on the measurement pass and says which.
    const an::EngineContext engine = makeEngine(2u, measurement - 1u);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, region);
    ASSERT_FALSE(plan.hasValue());
    EXPECT_EQ(plan.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(countField(plan.status(), an::ContextKey::Requested), measurement);
    EXPECT_TRUE(messageNames(plan.status(), "MeasureNormalization"));
}

TEST(StackPlannerTest, ConcurrentFrameReadsMultiplyTheReadBuffer)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(clippedRequest());

    const an::Result<an::StackPlan> one = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    an::StackPlanningRequest two;
    two.concurrentFrameReads = 2u;
    const an::Result<an::StackPlan> twice = an::planStack(execution, model, two);
    ASSERT_TRUE(twice.hasValue()) << twice.status().message();
    EXPECT_EQ(one->memory.readBufferBytes, one->io.frameBytes);
    EXPECT_EQ(twice->memory.readBufferBytes, 2u * one->memory.readBufferBytes);
    EXPECT_EQ(twice->io.concurrentFrameReads, 2u);
    EXPECT_EQ(twice->memory.peakBytes, one->memory.peakBytes + one->memory.readBufferBytes);
}

TEST(StackPlannerTest, TheBackendIsWhatTheDispatchTableAnswers)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(clippedRequest());

    {
        const an::ExecutionContext automatic = makeExecution(engine);
        const an::Result<an::StackPlan> chosen = an::planStack(automatic, model, an::StackPlanningRequest{});
        ASSERT_TRUE(chosen.hasValue()) << chosen.status().message();
        EXPECT_EQ(chosen->backend.requestedBackend, an::ComputeBackend::Automatic);
        EXPECT_EQ(chosen->backend.selection.backend, an::ComputeBackend::Cpu);
        EXPECT_EQ(chosen->backend.selection.instructionSet, an::instructionSetOfDispatchedCode(automatic));
        EXPECT_FALSE(chosen->backend.fellBack);
        EXPECT_EQ(chosen->backend.reductionDeterminism, an::ReductionDeterminism::DecisionFeeding);
    }
    {
        const an::ExecutionContext reference = makeExecution(engine, nullptr, an::ComputeBackend::Reference);
        const an::Result<an::StackPlan> scalar = an::planStack(reference, model, an::StackPlanningRequest{});
        ASSERT_TRUE(scalar.hasValue()) << scalar.status().message();
        EXPECT_EQ(scalar->backend.requestedBackend, an::ComputeBackend::Reference);
        EXPECT_EQ(scalar->backend.selection.backend, an::ComputeBackend::Reference);
        EXPECT_EQ(scalar->backend.selection.instructionSet, an::InstructionSet::Scalar);
        EXPECT_FALSE(scalar->backend.fellBack);
    }
}

TEST(StackPlannerTest, AnOverflowingAccountingIsRefusedAsALimit)
{
    // A frame set whose whole-frame tile reads do not fit sixty-four bits: 65536 by 65536 samples,
    // the most frames a stack admits, and one-row tiles that re-read every frame in full.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model =
        makeModel(clippedRequest(an::kMaxStackFrames, makeDescriptor(an::SampleType::Float32, 65536u, 65536u)));

    an::StackPlanningRequest request;
    request.maximumTileRows = 1u;
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, request);
    ASSERT_FALSE(plan.hasValue());
    EXPECT_EQ(plan.status().code(), an::ErrorCode::LimitExceeded);
}

TEST(StackPlannerTest, ThePlanExplainsItselfThroughTheDiagnosticSink)
{
    const an::EngineContext engine = makeEngine();
    an::StackPlanningRequest request;
    request.maximumTileRows = 8u;

    {
        RecordingDiagnosticSink sink;
        const an::ExecutionContext execution = makeExecution(engine, &sink);
        const an::Result<an::StackPlan> plan = an::planStack(execution, makeModel(clippedRequest()), request);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();

        // One sentence per decision at Info, and a warning that whole-frame reads are multiplied.
        EXPECT_GE(sink.count(an::DiagnosticSeverity::Info), 6u);
        EXPECT_EQ(sink.count(an::DiagnosticSeverity::Warning), 1u);
        EXPECT_TRUE(sink.mentions("TileSampleHistory"));
        EXPECT_TRUE(sink.mentions("Float64"));
        EXPECT_TRUE(sink.mentions("CallerRowCap"));
        EXPECT_TRUE(sink.mentions("Combine"));
        EXPECT_TRUE(sink.mentions("Finalize"));
        EXPECT_TRUE(sink.mentions("persistent"));
        EXPECT_TRUE(sink.mentions(an::computeBackendName(plan->backend.selection.backend)));
        EXPECT_TRUE(sink.mentions("6 tiles"));
        for (const RecordingDiagnosticSink::Event& event : sink.events())
        {
            EXPECT_EQ(event.operationId, execution.operationId());
            if (event.operation == "an::planStack")
            {
                EXPECT_EQ(event.domain, an::ErrorDomain::Algorithms);
            }
        }
    }

    // Region access removes the amplification and its warning, and a plan made in silence is the
    // same plan. The executions take turns on the engine's two threads.
    request.access = an::StackSourceAccess::Region;
    an::StackPlan heard;
    {
        RecordingDiagnosticSink quiet;
        const an::ExecutionContext listened = makeExecution(engine, &quiet);
        const an::Result<an::StackPlan> region = an::planStack(listened, makeModel(clippedRequest()), request);
        ASSERT_TRUE(region.hasValue()) << region.status().message();
        EXPECT_EQ(quiet.count(an::DiagnosticSeverity::Warning), 0u);
        EXPECT_GE(quiet.count(an::DiagnosticSeverity::Info), 6u);
        heard = *region;
    }
    {
        const an::ExecutionContext silent = makeExecution(engine);
        const an::Result<an::StackPlan> unheard = an::planStack(silent, makeModel(clippedRequest()), request);
        ASSERT_TRUE(unheard.hasValue()) << unheard.status().message();
        EXPECT_EQ(*unheard, heard);
    }
}

TEST(StackPlannerTest, ThePlanIsAValue)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(clippedRequest());

    const an::Result<an::StackPlan> first = an::planStack(execution, model, an::StackPlanningRequest{});
    const an::Result<an::StackPlan> second = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(first.hasValue() && second.hasValue());
    EXPECT_EQ(*first, *second);

    an::StackPlanningRequest capped;
    capped.maximumTileRows = 4u;
    const an::Result<an::StackPlan> third = an::planStack(execution, model, capped);
    ASSERT_TRUE(third.hasValue());
    EXPECT_NE(*first, *third);

    an::StackPlan copy = *first;
    copy.tile.rows = 1u;
    EXPECT_NE(copy, *first);
    EXPECT_EQ(an::StackPlanningRequest{}, an::StackPlanningRequest{});
}

TEST(StackPlannerTest, EveryNameTableIsClosed)
{
    EXPECT_STREQ(an::stackSourceAccessName(an::StackSourceAccess::WholeFrame), "WholeFrame");
    EXPECT_STREQ(an::stackSourceAccessName(an::StackSourceAccess::Region), "Region");
    EXPECT_STREQ(an::stackSourceAccessName(static_cast<an::StackSourceAccess>(9)), "UnknownStackSourceAccess");
    EXPECT_TRUE(an::isStackSourceAccess(an::StackSourceAccess::Region));
    EXPECT_FALSE(an::isStackSourceAccess(static_cast<an::StackSourceAccess>(9)));

    EXPECT_STREQ(an::stackAccumulationStrategyName(an::StackAccumulationStrategy::WholeOutputAccumulators),
                 "WholeOutputAccumulators");
    EXPECT_STREQ(an::stackAccumulationStrategyName(an::StackAccumulationStrategy::TileSampleHistory),
                 "TileSampleHistory");
    EXPECT_STREQ(an::stackAccumulationStrategyName(static_cast<an::StackAccumulationStrategy>(9)),
                 "UnknownStackAccumulationStrategy");

    EXPECT_STREQ(an::stackStrategyReasonName(an::StackStrategyReason::StreamingCombination), "StreamingCombination");
    EXPECT_STREQ(an::stackStrategyReasonName(an::StackStrategyReason::RejectionNeedsSampleHistory),
                 "RejectionNeedsSampleHistory");
    EXPECT_STREQ(an::stackStrategyReasonName(an::StackStrategyReason::MedianNeedsSampleHistory),
                 "MedianNeedsSampleHistory");
    EXPECT_STREQ(an::stackStrategyReasonName(an::StackStrategyReason::AutoAdaptiveNeedsSampleHistory),
                 "AutoAdaptiveNeedsSampleHistory");
    EXPECT_STREQ(an::stackStrategyReasonName(static_cast<an::StackStrategyReason>(9)), "UnknownStackStrategyReason");

    EXPECT_STREQ(an::stackTileReasonName(an::StackTileReason::WholeFrameUnbounded), "WholeFrameUnbounded");
    EXPECT_STREQ(an::stackTileReasonName(an::StackTileReason::WholeFrameFits), "WholeFrameFits");
    EXPECT_STREQ(an::stackTileReasonName(an::StackTileReason::BudgetBound), "BudgetBound");
    EXPECT_STREQ(an::stackTileReasonName(an::StackTileReason::CacheBand), "CacheBand");
    EXPECT_STREQ(an::stackTileReasonName(an::StackTileReason::SourceGranularity), "SourceGranularity");
    EXPECT_STREQ(an::stackTileReasonName(an::StackTileReason::CallerRowCap), "CallerRowCap");
    EXPECT_STREQ(an::stackTileReasonName(static_cast<an::StackTileReason>(9)), "UnknownStackTileReason");

    EXPECT_STREQ(an::stackPrecisionReasonName(an::StackPrecisionReason::DefaultFloat32), "DefaultFloat32");
    EXPECT_STREQ(an::stackPrecisionReasonName(an::StackPrecisionReason::StoredInFloat64), "StoredInFloat64");
    EXPECT_STREQ(an::stackPrecisionReasonName(static_cast<an::StackPrecisionReason>(9)), "UnknownStackPrecisionReason");

    EXPECT_STREQ(an::stackPassKindName(an::StackPassKind::MeasureNormalization), "MeasureNormalization");
    EXPECT_STREQ(an::stackPassKindName(an::StackPassKind::MeasureLocalWeights), "MeasureLocalWeights");
    EXPECT_STREQ(an::stackPassKindName(an::StackPassKind::Combine), "Combine");
    EXPECT_STREQ(an::stackPassKindName(an::StackPassKind::Finalize), "Finalize");
    EXPECT_STREQ(an::stackPassKindName(static_cast<an::StackPassKind>(9)), "UnknownStackPassKind");
}

} // namespace

TEST(StackPlannerTest, SampleHistoryCacheBandsUseAdmittedWorkersWithoutWideningLimits)
{
    const an::EngineContext engine = makeEngine(16u);
    for (const auto schema : {an::ChannelSchema::Mono, an::ChannelSchema::Rgb})
    {
        an::StackRequest request = baseRequest(8u, makeDescriptor(an::SampleType::Float32, 8288u, 5644u, schema));
        request.combination = an::StackCombination::Median;
        const an::StackModel model = makeModel(request);
        std::uint32_t previousRows = 0u;
        for (const auto workers : {1u, 2u, 4u, 16u})
        {
            const an::ExecutionContext execution =
                makeExecution(engine, nullptr, an::ComputeBackend::Automatic, workers);
            an::StackPlanningRequest planning;
            planning.access = an::StackSourceAccess::Region;
            const auto plan = an::planStack(execution, model, planning);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            EXPECT_EQ(plan->memory.workers, workers);
            EXPECT_GE(plan->tile.rows * an::channelCountOf(schema).value_or(1u), workers);
            EXPECT_GT(plan->tile.rows, previousRows);
            previousRows = plan->tile.rows;

            planning.maximumTileRows = 1u;
            const auto capped = an::planStack(execution, model, planning);
            ASSERT_TRUE(capped.hasValue()) << capped.status().message();
            EXPECT_EQ(capped->tile.rows, 1u);
            EXPECT_EQ(capped->tile.reason,
                      plan->tile.rows > 1u ? an::StackTileReason::CallerRowCap : plan->tile.reason);

            // Region access has no whole-frame read buffer. A two-row hard budget stays a
            // two-row budget even when the admitted worker count would prefer a wider band.
            planning.maximumTileRows = 0u;
            const auto perRow = plan->memory.tileBytesPerRow + plan->memory.readBufferBytes / plan->tile.rows;
            planning.workingSetLimitBytes = plan->memory.persistentBytes + plan->memory.scratchBytes + 2u * perRow;
            const auto bounded = an::planStack(execution, model, planning);
            ASSERT_TRUE(bounded.hasValue()) << bounded.status().message();
            EXPECT_LE(bounded->tile.rows, 2u);
            EXPECT_LE(bounded->memory.peakBytes, planning.workingSetLimitBytes);
        }
    }
}

TEST(StackPlannerTest, ARowLargerThanTheCacheAllowanceStillBoundsTheBand)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = baseRequest(8u, makeDescriptor(an::SampleType::Float32, 65536u, 64u));
    request.combination = an::StackCombination::Median;
    const auto model = makeModel(request);
    an::StackPlanningRequest planning;
    planning.access = an::StackSourceAccess::Region;
    const auto plan = an::planStack(execution, model, planning);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_GT(plan->memory.tileBytesPerRow, an::kStackCacheBandBytes);
    EXPECT_EQ(plan->tile.rows, execution.admittedConcurrency());
    EXPECT_EQ(plan->tile.reason, an::StackTileReason::CacheBand);
}
