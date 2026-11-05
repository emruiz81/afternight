// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for deterministic moments, extrema, count, and norm primitives.

#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext
reductionEngine(std::uint32_t concurrency,
                std::uint64_t memoryBudgetBytes = an::defaultEngineLimits().memoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryBudgetBytes;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext reductionExecution(const an::EngineContext& engine, std::uint32_t concurrency)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout singlePlaneLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                                std::uint32_t strideSamples)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = 1u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(strideSamples) * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

template <typename SampleT>
void expectResidentRepresentation(const std::array<SampleT, 4u>& samples, double minimum, double maximum, double sum,
                                  double l1, double squaredL2)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const an::ImageLayout layout = singlePlaneLayout(type, 4u, 1u, 4u);
    an::EngineContext engine = reductionEngine(2u);
    an::ExecutionContext execution = reductionExecution(engine, 2u);
    const an::Result<an::ReductionStatistics> reduced = an::reducePlaneStatistics(
        execution,
        an::KernelPlaneSource{
            .sampleType = type, .samples = samples.data(), .layout = &layout, .plane = 0u, .sampleInPixel = 0u},
        an::KernelValidityView{});
    ASSERT_TRUE(reduced.hasValue()) << an::sampleTypeName(type) << ": " << reduced.status().message();
    EXPECT_DOUBLE_EQ(reduced->minimum, minimum) << an::sampleTypeName(type);
    EXPECT_DOUBLE_EQ(reduced->maximum, maximum) << an::sampleTypeName(type);
    EXPECT_DOUBLE_EQ(reduced->sum, sum) << an::sampleTypeName(type);
    EXPECT_DOUBLE_EQ(reduced->l1Norm, l1) << an::sampleTypeName(type);
    EXPECT_DOUBLE_EQ(reduced->l2Norm, std::sqrt(squaredL2)) << an::sampleTypeName(type);
}

[[nodiscard]] std::vector<an::InstructionSet> reductionVectorTargets()
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

[[nodiscard]] an::ExecutionContext forcedReductionExecution(const an::EngineContext& engine, an::ComputeBackend backend,
                                                            an::InstructionSet instructionSet)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = backend;
    options.instructionSet = instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

} // namespace

TEST(ReductionPrimitivesTest, StableMomentsExtremaCountAndNormsHonorAPaddedMaskedTail)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kStride = 16u;
    std::array<float, kStride> samples{};
    for (std::uint32_t index = 0u; index < kWidth; ++index)
    {
        samples[index] = static_cast<float>(index + 1u);
    }
    samples[kWidth] = 100000.0f;

    std::array<std::uint8_t, 2u> maskBytes{};
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    EXPECT_TRUE(writableMask->setValid(0u, 0u, 0u, false));
    EXPECT_TRUE(writableMask->setValid(0u, 12u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    an::ValidityDescriptorRequest validityRequest;
    validityRequest.rule = an::ValidityRule::ExplicitMask;
    const an::ValidityDescriptor validity = *an::ValidityDescriptor::create(validityRequest);

    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kStride);
    an::EngineContext engine = reductionEngine(4u);
    an::ExecutionContext execution = reductionExecution(engine, 4u);
    const an::Result<an::ReductionStatistics> reduced =
        an::reducePlaneStatistics(execution,
                                  an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                                                        .samples = samples.data(),
                                                        .layout = &layout,
                                                        .plane = 0u,
                                                        .sampleInPixel = 0u},
                                  an::KernelValidityView{.descriptor = validity, .mask = &mask});
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_EQ(reduced->run.selection, (an::ValidityCount{.valid = 11u, .total = 13u}));
    EXPECT_DOUBLE_EQ(reduced->minimum, 2.0);
    EXPECT_DOUBLE_EQ(reduced->maximum, 12.0);
    EXPECT_DOUBLE_EQ(reduced->sum, 77.0);
    EXPECT_DOUBLE_EQ(reduced->mean, 7.0);
    EXPECT_DOUBLE_EQ(reduced->sumSquaredDeviations, 110.0);
    EXPECT_DOUBLE_EQ(reduced->populationVariance, 10.0);
    EXPECT_DOUBLE_EQ(reduced->l1Norm, 77.0);
    EXPECT_DOUBLE_EQ(reduced->l2Norm, std::sqrt(649.0));
    EXPECT_DOUBLE_EQ(reduced->infinityNorm, 12.0);
    EXPECT_GT(reduced->sumError.absoluteError, 0.0);
    EXPECT_GT(reduced->sumError.sumAbsoluteTerms, reduced->l1Norm);
    EXPECT_GT(reduced->centeredMomentError.sumAbsoluteTerms, reduced->sumSquaredDeviations);
    EXPECT_GT(reduced->squaredL2Error.sumAbsoluteTerms, 649.0);
    EXPECT_EQ(reduced->sumError.maximumAdditionDepth, reduced->plan.maximumAdditionDepth);
    EXPECT_EQ(reduced->centeredMomentError.maximumAdditionDepth, reduced->plan.maximumAdditionDepth);
    EXPECT_GT(reduced->plan.deterministicChunks, 0u);
    EXPECT_GT(reduced->plan.plannedWorkers, 0u);
}

TEST(ReductionPrimitivesTest, TheApprovedPartitionProducesIdenticalBitsAtEveryThreadCount)
{
    constexpr std::uint32_t kWidth = 4099u;
    std::array<float, kWidth> samples{};
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>((index % 29u) - 14.0);
    }
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    std::optional<an::ReductionStatistics> baseline;
    for (const std::uint32_t threads : {1u, 2u, 4u})
    {
        an::EngineContext engine = reductionEngine(threads);
        an::ExecutionContext execution = reductionExecution(engine, threads);
        const an::Result<an::ReductionStatistics> reduced =
            an::reducePlaneStatistics(execution,
                                      an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                                                            .samples = samples.data(),
                                                            .layout = &layout,
                                                            .plane = 0u,
                                                            .sampleInPixel = 0u},
                                      an::KernelValidityView{});
        ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
        EXPECT_EQ(reduced->plan.plannedWorkers, (std::min)(threads, 3u));
        if (!baseline.has_value())
        {
            baseline = *reduced;
            continue;
        }
        EXPECT_EQ(std::bit_cast<std::uint64_t>(reduced->sum), std::bit_cast<std::uint64_t>(baseline->sum));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(reduced->mean), std::bit_cast<std::uint64_t>(baseline->mean));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(reduced->sumSquaredDeviations),
                  std::bit_cast<std::uint64_t>(baseline->sumSquaredDeviations));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(reduced->l2Norm), std::bit_cast<std::uint64_t>(baseline->l2Norm));
        EXPECT_EQ(reduced->plan.deterministicChunks, baseline->plan.deterministicChunks);
    }
}

TEST(ReductionPrimitivesTest, AllEightResidentRepresentationsAreReducedWithoutAWorkingImageConversion)
{
    expectResidentRepresentation(std::array<std::uint8_t, 4u>{1u, 2u, 3u, 4u}, 1.0, 4.0, 10.0, 10.0, 30.0);
    expectResidentRepresentation(std::array<std::int8_t, 4u>{-2, -1, 1, 2}, -2.0, 2.0, 0.0, 6.0, 10.0);
    expectResidentRepresentation(std::array<std::uint16_t, 4u>{1u, 2u, 3u, 4u}, 1.0, 4.0, 10.0, 10.0, 30.0);
    expectResidentRepresentation(std::array<std::int16_t, 4u>{-2, -1, 1, 2}, -2.0, 2.0, 0.0, 6.0, 10.0);
    expectResidentRepresentation(std::array<std::uint32_t, 4u>{1u, 2u, 3u, 4u}, 1.0, 4.0, 10.0, 10.0, 30.0);
    expectResidentRepresentation(std::array<std::int32_t, 4u>{-2, -1, 1, 2}, -2.0, 2.0, 0.0, 6.0, 10.0);
    expectResidentRepresentation(std::array<float, 4u>{-2.0f, -1.0f, 1.0f, 2.0f}, -2.0, 2.0, 0.0, 6.0, 10.0);
    expectResidentRepresentation(std::array<double, 4u>{-2.0, -1.0, 1.0, 2.0}, -2.0, 2.0, 0.0, 6.0, 10.0);
}

TEST(ReductionPrimitivesTest, AnInterleavedComponentIsSelectedWithoutRepacking)
{
    constexpr std::uint32_t kWidth = 4u;
    constexpr std::uint32_t kHeight = 2u;
    constexpr std::uint32_t kChannels = 3u;
    constexpr std::uint32_t kStride = 14u;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(kWidth, kHeight, kChannels, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = kChannels,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::createWithStrides(
        request, kStride * sizeof(float), kStride * kHeight * sizeof(float), an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());

    std::array<float, kStride * kHeight> samples{};
    samples.fill(-999.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
            {
                samples[y * kStride + x * kChannels + channel] = static_cast<float>(100u * y + 10u * x + channel);
            }
        }
    }

    an::EngineContext engine = reductionEngine(2u);
    an::ExecutionContext execution = reductionExecution(engine, 2u);
    const an::Result<an::ReductionStatistics> reduced =
        an::reducePlaneStatistics(execution,
                                  an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                                                        .samples = samples.data(),
                                                        .layout = &*layout,
                                                        .plane = 0u,
                                                        .sampleInPixel = 1u},
                                  an::KernelValidityView{});
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_EQ(reduced->run.selection, (an::ValidityCount{.valid = 8u, .total = 8u}));
    EXPECT_DOUBLE_EQ(reduced->minimum, 1.0);
    EXPECT_DOUBLE_EQ(reduced->maximum, 131.0);
    EXPECT_DOUBLE_EQ(reduced->sum, 528.0);
    EXPECT_DOUBLE_EQ(reduced->mean, 66.0);
}

TEST(ReductionPrimitivesTest, ContextMemoryBudgetRefusesTheDeterministicPartials)
{
    const std::array<float, 4u> samples{1.0f, 2.0f, 3.0f, 4.0f};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, 4u, 1u, 4u);
    an::EngineContext engine = reductionEngine(1u, 1u);
    an::ExecutionContext execution = reductionExecution(engine, 1u);

    const an::Result<an::ReductionStatistics> refused = an::reducePlaneStatistics(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout},
        an::KernelValidityView{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST(ReductionPrimitivesTest, AnEmptySelectionIsAResultFailureRatherThanANan)
{
    const std::array<float, 8u> samples{};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, 8u, 1u, 8u);
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::ZeroIsNoData;
    an::EngineContext engine = reductionEngine(2u);
    an::ExecutionContext execution = reductionExecution(engine, 2u);
    const an::Result<an::ReductionStatistics> reduced = an::reducePlaneStatistics(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout},
        an::KernelValidityView{.descriptor = *an::ValidityDescriptor::create(request)});
    ASSERT_FALSE(reduced.hasValue());
    EXPECT_EQ(reduced.status().code(), an::ErrorCode::EmptySelection);
}

TEST(ReductionPrimitivesTest, EveryCompiledVectorTargetStaysWithinTheRecordedTreesAndReportsItsOwnCode)
{
    const std::vector<an::InstructionSet> targets = reductionVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    constexpr std::uint32_t kWidth = 8191u;
    std::array<float, kWidth> samples{};
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>(static_cast<int>(index % 101u) - 50) * 0.03125f;
    }
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    const an::KernelPlaneSource view{
        .sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout};
    an::EngineContext engine = reductionEngine(4u);
    an::ExecutionContext reference =
        forcedReductionExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar);
    const an::Result<an::ReductionStatistics> oracle =
        an::reducePlaneStatistics(reference, view, an::KernelValidityView{});
    ASSERT_TRUE(oracle.hasValue()) << oracle.status().message();
    for (const an::InstructionSet target : targets)
    {
        an::ExecutionContext vectorized = forcedReductionExecution(engine, an::ComputeBackend::Cpu, target);
        ASSERT_EQ(an::instructionSetOfDispatchedCode(vectorized), target);
        const an::Result<an::ReductionStatistics> actual =
            an::reducePlaneStatistics(vectorized, view, an::KernelValidityView{});
        ASSERT_TRUE(actual.hasValue()) << an::instructionSetName(target) << ": " << actual.status().message();
        EXPECT_LE(std::abs(actual->sum - oracle->sum), actual->sumError.absoluteError + oracle->sumError.absoluteError);
        EXPECT_LE(std::abs(actual->sumSquaredDeviations - oracle->sumSquaredDeviations),
                  actual->centeredMomentError.absoluteError + oracle->centeredMomentError.absoluteError);
        EXPECT_DOUBLE_EQ(actual->minimum, oracle->minimum);
        EXPECT_DOUBLE_EQ(actual->maximum, oracle->maximum);
        EXPECT_DOUBLE_EQ(actual->infinityNorm, oracle->infinityNorm);
    }
}

TEST(ReductionPrimitivesTest, VectorRunsRejectSelectedNonFiniteValuesAndTheNamedRuleExcludesThem)
{
    const std::vector<an::InstructionSet> targets = reductionVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    constexpr std::uint32_t kWidth = 17u;
    std::array<float, kWidth> samples{};
    for (std::uint32_t index = 0u; index < kWidth; ++index)
    {
        samples[index] = static_cast<float>(index + 1u);
    }
    samples[1] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    const an::KernelPlaneSource view{
        .sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout};
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::FiniteOnly;
    const an::ValidityDescriptor excludeNonFinite = *an::ValidityDescriptor::create(request);
    an::EngineContext engine = reductionEngine(4u);
    for (const an::InstructionSet target : targets)
    {
        an::ExecutionContext execution = forcedReductionExecution(engine, an::ComputeBackend::Cpu, target);
        const an::Result<an::ReductionStatistics> refused =
            an::reducePlaneStatistics(execution, view, an::KernelValidityView{});
        ASSERT_FALSE(refused.hasValue()) << an::instructionSetName(target);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData) << an::instructionSetName(target);

        const an::Result<an::ReductionStatistics> reduced =
            an::reducePlaneStatistics(execution, view, an::KernelValidityView{.descriptor = excludeNonFinite});
        ASSERT_TRUE(reduced.hasValue()) << an::instructionSetName(target) << ": " << reduced.status().message();
        EXPECT_EQ(reduced->run.selection, (an::ValidityCount{.valid = 16u, .total = 17u}));
        EXPECT_DOUBLE_EQ(reduced->sum, 151.0);
    }
}
