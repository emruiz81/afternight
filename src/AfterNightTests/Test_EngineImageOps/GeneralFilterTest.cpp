// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the reusable general filter plans.

#include <AfterNightEngine/Algorithms/Color/AdaptiveHistogramColor.h>
#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;

/// Whether two planes hold the same bits, a NaN where the other holds a NaN with whatever payload. When two NaNs meet
/// in one operation, C++ does not fix which operand's payload the result keeps, and the compilers choose differently.
[[nodiscard]] bool sameBitsButNaNPayloads(const std::vector<float>& left, const std::vector<float>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        const bool bothNaN = std::isnan(left[index]) && std::isnan(right[index]);
        if (!bothNaN && std::bit_cast<std::uint32_t>(left[index]) != std::bit_cast<std::uint32_t>(right[index]))
        {
            return false;
        }
    }
    return true;
}

TEST(GeneralFilterTest, BoxPlanIsReusableAndUsesTheSharedBorderContract)
{
    const an::Result<an::BoxFilterPlan> planned =
        an::planBoxFilter({.radiusX = 1u, .radiusY = 1u, .borders = {.border = an::KernelBorderMode::Constant}});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->horizontalRadius(), 1u);
    EXPECT_EQ(planned->verticalRadius(), 1u);

    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 5u, 5u);
    std::vector<float> source(25u, 0.0f);
    source[12u] = 1.0f;
    std::vector<float> first(25u, -1.0f);
    std::vector<float> second(25u, -1.0f);

    const an::Result<an::SpatialFilterResult> one =
        an::filterImageBox(execution, an::SampleType::Float32, source.data(), layout, first.data(), layout, *planned);
    const an::Result<an::SpatialFilterResult> two =
        an::filterImageBox(execution, an::SampleType::Float32, source.data(), layout, second.data(), layout, *planned);
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    ASSERT_TRUE(two.hasValue()) << two.status().message();
    EXPECT_EQ(one->imagePasses, 2u);
    EXPECT_EQ(first, second);
    for (std::uint32_t y = 1u; y <= 3u; ++y)
    {
        for (std::uint32_t x = 1u; x <= 3u; ++x)
        {
            EXPECT_NEAR(first[static_cast<std::size_t>(y) * 5u + x], 1.0f / 9.0f, 1.0e-6f);
        }
    }
}

TEST(GeneralFilterTest, DetailPlanStatesHighPassAndUnsharpArithmetic)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 9u, 7u);
    const std::vector<float> source(63u, 0.25f);

    for (const an::DetailFilterMethod method : {an::DetailFilterMethod::HighPass, an::DetailFilterMethod::UnsharpMask})
    {
        an::DetailFilterRequest request{};
        request.method = method;
        request.sigmaX = 1.0;
        request.sigmaY = 1.0;
        request.amount = 1.75;
        request.offset = method == an::DetailFilterMethod::HighPass ? 0.5 : 0.0;
        const an::Result<an::DetailFilterPlan> planned = an::planDetailFilter(request);
        ASSERT_TRUE(planned.hasValue()) << planned.status().message();

        std::vector<float> destination(source.size(), -1.0f);
        const an::Result<an::SpatialFilterResult> filtered = an::filterImageDetail(
            execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned);
        ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
        const float expected = method == an::DetailFilterMethod::HighPass ? 0.5f : 0.25f;
        for (const float value : destination)
        {
            EXPECT_NEAR(value, expected, 1.0e-6f);
        }
    }
}

TEST(GeneralFilterTest, EdgeTightenUsesFourSharedDenseStencilsAndAReusablePlan)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 7u, 7u);
    std::vector<float> source(49u, 0.75f);
    for (std::uint32_t y = 0u; y < 7u; ++y)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            source[static_cast<std::size_t>(y) * 7u + x] = 0.25f;
        }
    }
    const an::Result<an::EdgeTightenPlan> planned = an::planEdgeTighten({.amount = 0.5});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();

    std::vector<float> first(source.size(), -1.0f);
    std::vector<float> second(source.size(), -1.0f);
    const an::Result<an::SpatialFilterResult> one = an::filterImageEdgeTighten(
        execution, an::SampleType::Float32, source.data(), layout, first.data(), layout, *planned);
    const an::Result<an::SpatialFilterResult> two = an::filterImageEdgeTighten(
        execution, an::SampleType::Float32, source.data(), layout, second.data(), layout, *planned);
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    ASSERT_TRUE(two.hasValue()) << two.status().message();
    EXPECT_EQ(one->imagePasses, 1u) << "the four responses of a centre come from one pass over the plane";
    EXPECT_EQ(first, second);
    EXPECT_LT(first[24u], source[24u]);
    EXPECT_FLOAT_EQ(first[22u], source[22u]);

    // The composition the operation is defined by - four dense convolutions and the passes accumulating their
    // magnitudes - is what the reference backend runs, and it says so.
    const an::ExecutionContext reference = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    std::vector<float> composed(source.size(), -1.0f);
    const an::Result<an::SpatialFilterResult> eight = an::filterImageEdgeTighten(
        reference, an::SampleType::Float32, source.data(), layout, composed.data(), layout, *planned);
    ASSERT_TRUE(eight.hasValue()) << eight.status().message();
    EXPECT_EQ(eight->imagePasses, 8u);
    EXPECT_EQ(composed, first);
}

TEST(GeneralFilterTest, EdgeTightenInOnePassWritesTheBitsOfTheEightPassComposition)
{
    // Every border rule, an invalid neighbour value, a mask, a value rule that rejects samples, non-finite samples a
    // rule admits, an integer representation and a padded row: the one pass - through the compiled interior run this
    // host's table holds and through the scalar body alone - and the reference composition write the same bits, but
    // for which payload a NaN keeps where two NaN samples meet, and count the same samples.
    const an::EngineContext engine = makeEngine(9u);
    const an::ExecutionContext fused = makeExecution(engine, 3u);
    const an::ExecutionContext reference = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    an::ExecutionOptions scalarOptions;
    scalarOptions.maxConcurrency = 3u;
    scalarOptions.backend = an::ComputeBackend::Cpu;
    scalarOptions.instructionSet = an::InstructionSet::Scalar;
    const an::Result<an::ExecutionContext> scalar = an::ExecutionContext::create(engine, scalarOptions);
    ASSERT_TRUE(scalar.hasValue()) << scalar.status().message();
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 23u;

    std::vector<float> samples = spatial_test::randomPlane(kWidth, kHeight, 41u);
    samples[5u * kWidth + 7u] = std::numeric_limits<float>::quiet_NaN();
    samples[5u * kWidth + 8u] = std::bit_cast<float>(std::uint32_t{0x7FC01234u});
    samples[9u * kWidth + 0u] = std::numeric_limits<float>::infinity();
    samples[11u * kWidth + 20u] = std::numeric_limits<float>::infinity();
    samples[14u * kWidth + 29u] = -std::numeric_limits<float>::infinity();
    samples[0u * kWidth + 12u] = -std::numeric_limits<float>::infinity();
    samples[22u * kWidth + 36u] = -0.0f;
    samples[17u * kWidth + 33u] = -0.0f;

    an::Result<an::ValidityMask> mask =
        an::ValidityMask::create(engine, *an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits()));
    ASSERT_TRUE(mask.hasValue());
    {
        std::optional<an::ValidityMaskView<std::uint8_t>> view = mask->view();
        ASSERT_TRUE(view.has_value());
        view->fill(true);
        for (std::uint32_t index = 0u; index < kWidth * kHeight; index += 7u)
        {
            ASSERT_TRUE(view->setValid(0u, index % kWidth, index / kWidth, false));
        }
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> maskView = std::as_const(*mask).view();
    ASSERT_TRUE(maskView.has_value());

    struct Rule
    {
        const char* name;
        an::SpatialSelection selection;
    };
    const std::vector<Rule> rules{
        {"every sample", {}},
        {"finite only", {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})}},
        {"a range",
         {.validity = *an::ValidityDescriptor::create(
              {.rule = an::ValidityRule::DomainRange, .lowestValid = 0.2, .highestValid = 0.8})}},
        {"a mask",
         {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}), .mask = &*maskView}},
    };
    for (const an::KernelBorderMode border :
         {an::KernelBorderMode::Constant, an::KernelBorderMode::Clamp, an::KernelBorderMode::Reflect,
          an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap})
    {
        for (const Rule& rule : rules)
        {
            SCOPED_TRACE(rule.name);
            an::EdgeTightenRequest request{.amount = 1.3, .offset = 0.01, .lowest = 0.0, .highest = 1.0};
            request.borders = {.border = border, .constantBorder = 0.125f, .invalidNeighbor = 0.375f};
            const an::Result<an::EdgeTightenPlan> planned = an::planEdgeTighten(request);
            ASSERT_TRUE(planned.hasValue()) << planned.status().message();
            for (const std::uint32_t padding : {0u, 5u})
            {
                const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u, padding);
                std::vector<float> source(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 0.5f);
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    std::copy_n(samples.begin() + static_cast<std::ptrdiff_t>(y) * kWidth, kWidth,
                                source.begin() + static_cast<std::ptrdiff_t>(y) * (kWidth + padding));
                }
                std::vector<float> one(source.size(), -7.0f);
                std::vector<float> plain(source.size(), -7.0f);
                std::vector<float> eight(source.size(), -7.0f);
                const an::Result<an::SpatialFilterResult> fast =
                    an::filterImageEdgeTighten(fused, an::SampleType::Float32, source.data(), layout, one.data(),
                                               layout, *planned, rule.selection);
                const an::Result<an::SpatialFilterResult> scalarPass =
                    an::filterImageEdgeTighten(*scalar, an::SampleType::Float32, source.data(), layout, plain.data(),
                                               layout, *planned, rule.selection);
                const an::Result<an::SpatialFilterResult> composed =
                    an::filterImageEdgeTighten(reference, an::SampleType::Float32, source.data(), layout, eight.data(),
                                               layout, *planned, rule.selection);
                ASSERT_EQ(fast.hasValue(), composed.hasValue());
                ASSERT_EQ(scalarPass.hasValue(), composed.hasValue());
                if (!fast.hasValue())
                {
                    EXPECT_EQ(fast.status().code(), composed.status().code());
                    continue;
                }
                EXPECT_EQ(scalarPass->imagePasses, 1u);
                ASSERT_EQ(one.size(), eight.size());
                EXPECT_TRUE(sameBitsButNaNPayloads(one, eight))
                    << "border " << static_cast<int>(border) << ", padding " << padding;
                EXPECT_TRUE(sameBitsButNaNPayloads(plain, eight))
                    << "the scalar body alone: border " << static_cast<int>(border) << ", padding " << padding;
                EXPECT_EQ(fast->run.selection.valid, composed->run.selection.valid);
                EXPECT_EQ(fast->run.selection.total, composed->run.selection.total);
                EXPECT_EQ(fast->run.outputsWritten, composed->run.outputsWritten);
            }
        }
    }

    // An integer image goes through the family's working planes and back.
    const an::ImageLayout integerLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<std::uint16_t> codes(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < codes.size(); ++index)
    {
        codes[index] = static_cast<std::uint16_t>((index * 2654435761u) % 65536u);
    }
    const an::Result<an::EdgeTightenPlan> integerPlan =
        an::planEdgeTighten({.amount = 0.7, .offset = 100.0, .lowest = 0.0, .highest = 65535.0});
    ASSERT_TRUE(integerPlan.hasValue());
    std::vector<std::uint16_t> oneCodes(codes.size(), 1u);
    std::vector<std::uint16_t> eightCodes(codes.size(), 1u);
    ASSERT_TRUE(an::filterImageEdgeTighten(fused, an::SampleType::UInt16, codes.data(), integerLayout, oneCodes.data(),
                                           integerLayout, *integerPlan)
                    .hasValue());
    ASSERT_TRUE(an::filterImageEdgeTighten(reference, an::SampleType::UInt16, codes.data(), integerLayout,
                                           eightCodes.data(), integerLayout, *integerPlan)
                    .hasValue());
    EXPECT_EQ(oneCodes, eightCodes);
}

TEST(GeneralFilterTest, MorphologyUsesTheNamedFootprintInsteadOfASecondBorderImplementation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 7u, 7u);
    std::vector<float> source(49u, 0.0f);
    source[24u] = 1.0f;
    std::vector<float> destination(49u, -1.0f);

    const an::Result<an::MorphologyPlan> planned = an::planMorphology({.operation = an::MorphologyOperation::Dilate,
                                                                       .shape = an::MorphologyShape::Cross,
                                                                       .radiusX = 1u,
                                                                       .radiusY = 1u,
                                                                       .border = an::KernelBorderMode::Constant});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::Result<an::SpatialFilterResult> filtered = an::filterImageMorphology(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();

    const std::array<std::size_t, 5> expected{17u, 23u, 24u, 25u, 31u};
    for (std::size_t index = 0u; index < destination.size(); ++index)
    {
        EXPECT_FLOAT_EQ(destination[index], std::ranges::find(expected, index) != expected.end() ? 1.0f : 0.0f)
            << index;
    }
}

// Fills every allocation with a finite marker, so that memory a filter reads before writing shows up in
// its output as that marker rather than as whatever an allocator happened to leave there.
class MarkedMemoryResource final : public an::MemoryResource
{
public:
    static constexpr float kMarker = 12345.0f;

    an::Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) override
    {
        void* block =
            ::operator new(static_cast<std::size_t>(bytes), std::align_val_t{static_cast<std::size_t>(alignment)});
        auto* bytesOut = static_cast<unsigned char*>(block);
        for (std::uint64_t offset = 0u; offset + sizeof(float) <= bytes; offset += sizeof(float))
        {
            std::memcpy(bytesOut + offset, &kMarker, sizeof(float));
        }
        m_liveBytes += bytes;
        ++m_liveAllocations;
        m_peakBytes = (std::max)(m_peakBytes, m_liveBytes);
        return block;
    }

    void deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept override
    {
        ::operator delete(pointer, std::align_val_t{static_cast<std::size_t>(alignment)});
        m_liveBytes -= bytes;
        --m_liveAllocations;
    }

    an::MemoryCounters counters() const noexcept override
    {
        an::MemoryCounters counters;
        counters.liveBytes = m_liveBytes;
        counters.liveAllocations = m_liveAllocations;
        counters.peakBytes = m_peakBytes;
        return counters;
    }

private:
    std::uint64_t m_liveBytes = 0u;
    std::uint64_t m_liveAllocations = 0u;
    std::uint64_t m_peakBytes = 0u;
};

// A composite or iterated morphology runs its passes through working planes taken from the engine
// context, and a centre the validity rule rejects is skipped rather than written. Under a rule that
// judges a sample by its value, the next pass judges the working plane's value at that centre, so the
// plane has to hold what the pass read there - the rejected sample - and not what the memory held.
// Every operation that passes through a working plane is run over a frame whose one rejected sample
// is not-a-number, on an engine whose memory arrives holding a finite marker.
TEST(GeneralFilterTest, CompositeMorphologyNeverReadsAWorkingPlaneItDidNotWrite)
{
    MarkedMemoryResource memory;
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    options.memoryResource = &memory;
    an::Result<an::EngineContext> engine = an::EngineContext::create(options);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    {
        const an::ExecutionContext execution = makeExecution(*engine);
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 7u, 7u);
        std::vector<float> source(49u);
        for (std::size_t index = 0u; index < source.size(); ++index)
        {
            source[index] = static_cast<float>(index % 7u) / 8.0f + static_cast<float>(index / 7u) / 64.0f;
        }
        source[24u] = std::numeric_limits<float>::quiet_NaN();
        const std::optional<an::ValidityDescriptor> finiteOnly =
            an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        ASSERT_TRUE(finiteOnly.has_value());
        const an::SpatialSelection selection{.validity = *finiteOnly};

        struct Case
        {
            an::MorphologyOperation operation;
            std::uint32_t iterations;
        };
        const std::array<Case, 6> cases{{{an::MorphologyOperation::Open, 1u},
                                         {an::MorphologyOperation::Close, 1u},
                                         {an::MorphologyOperation::TopHat, 1u},
                                         {an::MorphologyOperation::BlackHat, 1u},
                                         {an::MorphologyOperation::Erode, 2u},
                                         {an::MorphologyOperation::Dilate, 3u}}};
        for (const Case& run : cases)
        {
            const an::Result<an::MorphologyPlan> planned = an::planMorphology({.operation = run.operation,
                                                                               .radiusX = 1u,
                                                                               .radiusY = 1u,
                                                                               .iterations = run.iterations,
                                                                               .border = an::KernelBorderMode::Clamp});
            ASSERT_TRUE(planned.hasValue()) << planned.status().message();
            std::vector<float> destination(49u, -1.0f);
            const an::Result<an::SpatialFilterResult> filtered =
                an::filterImageMorphology(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                          layout, *planned, selection);
            ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
            for (std::size_t index = 0u; index < destination.size(); ++index)
            {
                EXPECT_NE(destination[index], MarkedMemoryResource::kMarker)
                    << "operation " << static_cast<int>(run.operation) << " x" << run.iterations << " at " << index;
                if (index != 24u)
                {
                    EXPECT_TRUE(std::isfinite(destination[index])) << index;
                    EXPECT_LE(std::fabs(destination[index]), 2.0f)
                        << "operation " << static_cast<int>(run.operation) << " at " << index;
                }
            }
        }
    }
    EXPECT_TRUE(engine->shutdown().ok());
}

TEST(GeneralFilterTest, MorphologyPreservesArbitraryFootprints)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 7u, 7u);
    std::vector<float> source(49u, 0.0f);
    source[24u] = 1.0f;
    std::vector<float> destination(49u, -1.0f);
    const std::array<std::uint8_t, 9u> footprint{1u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 1u};

    const an::Result<an::MorphologyPlan> planned = an::planMorphology({.operation = an::MorphologyOperation::Dilate,
                                                                       .shape = an::MorphologyShape::Custom,
                                                                       .radiusX = 1u,
                                                                       .radiusY = 1u,
                                                                       .border = an::KernelBorderMode::Constant,
                                                                       .customFootprint = footprint});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::Result<an::SpatialFilterResult> filtered = an::filterImageMorphology(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();

    const std::array<std::size_t, 4u> expected{16u, 18u, 30u, 32u};
    for (std::size_t index = 0u; index < destination.size(); ++index)
    {
        EXPECT_FLOAT_EQ(destination[index], std::ranges::find(expected, index) != expected.end() ? 1.0f : 0.0f)
            << index;
    }
}

TEST(GeneralFilterTest, ClahePlanClipsLocalHistogramsAndKeepsItsStatedRange)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 16u, 12u);
    std::vector<float> source(192u, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(index % 16u) / 15.0f;
    }
    std::vector<float> destination(source.size(), -1.0f);

    const an::Result<an::AdaptiveHistogramPlan> planned = an::planAdaptiveHistogramEqualization(
        {.tileColumns = 4u, .tileRows = 3u, .binCount = 16u, .clipLimit = 2.0, .lowest = 0.0, .highest = 1.0});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::Result<an::AdaptiveHistogramResult> filtered = an::equalizeImageAdaptiveHistogram(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered->tileCount, 12u);
    EXPECT_EQ(filtered->binCount, 16u);
    for (const float value : destination)
    {
        EXPECT_GE(value, 0.0f);
        EXPECT_LE(value, 1.0f);
    }
    EXPECT_NE(destination, source);
    for (std::uint32_t y = 1u; y < 12u; ++y)
    {
        for (std::uint32_t x = 0u; x < 16u; ++x)
        {
            EXPECT_FLOAT_EQ(destination[x], destination[static_cast<std::size_t>(y) * 16u + x]);
        }
    }
}

TEST(GeneralFilterTest, ClaheCountsANanSampleInTheFirstBinAsItCountsTheLowestValue)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 16u, 2u);
    std::vector<float> lowest(32u, 0.0f);
    for (std::size_t index = 0u; index < lowest.size(); ++index)
    {
        lowest[index] = static_cast<float>(index % 16u) / 15.0f;
    }
    lowest[5u] = 0.0f;
    std::vector<float> withNan = lowest;
    withNan[5u] = std::numeric_limits<float>::quiet_NaN();

    const an::Result<an::AdaptiveHistogramPlan> planned = an::planAdaptiveHistogramEqualization(
        {.tileColumns = 2u, .tileRows = 1u, .binCount = 16u, .clipLimit = 2.0, .lowest = 0.0, .highest = 1.0});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    std::vector<float> fromLowest(lowest.size(), -1.0f);
    std::vector<float> fromNan(lowest.size(), -1.0f);
    ASSERT_TRUE(an::equalizeImageAdaptiveHistogram(execution, an::SampleType::Float32, lowest.data(), layout,
                                                   fromLowest.data(), layout, *planned)
                    .hasValue());
    ASSERT_TRUE(an::equalizeImageAdaptiveHistogram(execution, an::SampleType::Float32, withNan.data(), layout,
                                                   fromNan.data(), layout, *planned)
                    .hasValue());
    for (std::size_t index = 0u; index < fromLowest.size(); ++index)
    {
        EXPECT_EQ(fromNan[index], fromLowest[index]) << index;
    }
}

TEST(GeneralFilterTest, PerceptualClaheChangesLabLightnessWithoutFilteringOpponentChannels)
{
    const an::Result<an::PerceptualAdaptiveHistogramPlan> planned =
        an::planPerceptualAdaptiveHistogramEqualization({.rgbEncoding = {.transfer = an::ColorTransferFunction::Srgb},
                                                         .tileColumns = 2u,
                                                         .tileRows = 2u,
                                                         .binCount = 64u,
                                                         .clipLimit = 2.0});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 4u, 4u, 3u);
    std::vector<float> source(48u, 0.0f);
    for (std::size_t index = 0u; index < 16u; ++index)
    {
        const float level = static_cast<float>(index + 1u) / 20.0f;
        source[index] = 0.7f * level + 0.05f;
        source[16u + index] = 0.4f * level + 0.03f;
        source[32u + index] = 0.2f * level + 0.01f;
    }
    std::vector<float> destination(48u, -1.0f);
    const an::Result<an::PerceptualAdaptiveHistogramResult> equalized = an::equalizeImagePerceptualAdaptiveHistogram(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned);
    ASSERT_TRUE(equalized.hasValue()) << equalized.status().message();
    EXPECT_NE(source, destination);

    bool lightnessChanged = false;
    for (std::size_t index = 0u; index < 16u; ++index)
    {
        const std::array<double, 3u> sourcePixel{source[index], source[16u + index], source[32u + index]};
        const std::array<double, 3u> destinationPixel{destination[index], destination[16u + index],
                                                      destination[32u + index]};
        std::array<double, 3u> sourceLab{};
        std::array<double, 3u> destinationLab{};
        ASSERT_TRUE(an::convertColorPixel(planned->toLab(), sourcePixel, sourceLab).ok());
        ASSERT_TRUE(an::convertColorPixel(planned->toLab(), destinationPixel, destinationLab).ok());
        lightnessChanged = lightnessChanged || std::abs(sourceLab[0u] - destinationLab[0u]) > 1.0e-3;
        EXPECT_NEAR(sourceLab[1u], destinationLab[1u], 2.0e-3);
        EXPECT_NEAR(sourceLab[2u], destinationLab[2u], 2.0e-3);
    }
    EXPECT_TRUE(lightnessChanged);
}

TEST(GeneralFilterTest, PlansRefuseUnknownOrUnboundedWork)
{
    EXPECT_EQ(an::planBoxFilter({.radiusX = an::kMaxStencilRadius + 1u}).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::planMorphology({.iterations = 0u}).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::planDetailFilter({.method = static_cast<an::DetailFilterMethod>(9u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planAdaptiveHistogramEqualization({.clipLimit = 0.0}).status().code(),
              an::ErrorCode::InvalidArgument);
    const std::array<std::uint8_t, 8u> wrongFootprint{};
    EXPECT_EQ(
        an::planMorphology({.shape = an::MorphologyShape::Custom, .customFootprint = wrongFootprint}).status().code(),
        an::ErrorCode::IncompatibleArguments);
}

} // namespace

TEST(GeneralFilterTest, EmptyClaheTileContributesIdentityInsteadOfBlack)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto layout = planarLayout(an::SampleType::Float32, 8u, 1u);
    const std::array<float, 8u> source{0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 8u> output;
    output.fill(-7.0f);
    const auto valid = an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(valid.has_value());
    const auto plan = an::planAdaptiveHistogramEqualization(
        {.tileColumns = 2u, .tileRows = 1u, .binCount = 4u, .clipLimit = 4.0, .lowest = 0.0, .highest = 1.0});
    ASSERT_TRUE(plan.hasValue());
    const auto result = an::equalizeImageAdaptiveHistogram(execution, an::SampleType::Float32, source.data(), layout,
                                                           output.data(), layout, *plan, {.validity = *valid});
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(output[0], -7.0f);
    EXPECT_EQ(output[4], 0.8125f);
    EXPECT_EQ(output[5], 0.9375f);
    EXPECT_EQ(output[6], 1.0f);
}
