// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <stop_token>
#include <chrono>
#include <random>
#include <numbers>

#include <gtest/gtest.h>

namespace
{

an::DrizzleModel accumulationModel(an::DrizzleKernel kernel, std::uint32_t width = 17u, std::uint32_t height = 13u,
                                   an::SampleType type = an::SampleType::Float32, double scale = 1.5)
{
    const auto extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    const auto image = an::ImageDescriptor::create({.extent = *extent,
                                                    .sampleType = type,
                                                    .channelSchema = an::ChannelSchema::Mono,
                                                    .colorSpace = an::ColorSpace::Unspecified,
                                                    .numericDomain = an::NumericDomain::NormalizedLinear,
                                                    .cfa = {},
                                                    .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                    .pixelAspectRatio = 1.0});
    an::DrizzleRequest request{.input = *image, .frameCount = 3u};
    request.parameters.kernel = kernel;
    request.parameters.scale = scale;
    request.parameters.pixelFraction = 0.9;
    request.output.coverageMap = true;
    return *an::DrizzleModel::create(request);
}

template <typename T>
an::DrizzleSampleBand sampleBand(const an::ImageExtent& extent, const std::vector<T>& samples, an::SampleType type)
{
    auto layout =
        an::ImageLayout::create({extent, sizeof(T), 1u, extent.channelCount(), 1u}, an::defaultEngineLimits());
    return {*layout, std::as_bytes(std::span{samples}), type};
}

struct Accumulators
{
    explicit Accumulators(const an::ImageExtent& extent)
        : sums(extent.sampleCount()), weights(sums.size()), coverage(sums.size()), last(sums.size())
    {
    }
    an::DrizzleAccumulatorBand band(std::uint32_t first, std::uint32_t count)
    {
        return {first, count, sums, weights, coverage, last};
    }
    std::vector<double> sums, weights;
    std::vector<std::uint32_t> coverage, last;
};

TEST(DrizzleAccumulationTest, EveryKernelMatchesReferenceAcrossWorkersAndPreservesFluxAndCoverage)
{
    for (const auto kernel : {an::DrizzleKernel::Square, an::DrizzleKernel::Point, an::DrizzleKernel::Turbo,
                              an::DrizzleKernel::Gaussian, an::DrizzleKernel::Lanczos2, an::DrizzleKernel::Lanczos3})
    {
        const auto model = accumulationModel(kernel);
        const auto& source = model.request().input.extent();
        const auto& output = model.outputDescriptor().extent();
        std::vector<float> samples(source.sampleCount(), 0.25f);
        const auto pixels = sampleBand(source, samples, an::SampleType::Float32);
        std::optional<an::DrizzleKernelTable> table;
        if (kernel == an::DrizzleKernel::Lanczos2 || kernel == an::DrizzleKernel::Lanczos3)
            table = *an::DrizzleKernelTable::create(kernel);
        auto engine = an::EngineContext::create({.maxConcurrency = 4u});
        auto reference =
            an::ExecutionContext::create(*engine, {.maxConcurrency = 1u, .backend = an::ComputeBackend::Reference});
        auto geometry = an::DrizzleGeometryPlan::create(*reference, *an::DrizzleMapDescriptor::create(model, {}));
        auto map = an::generateDrizzlePixmap(*reference, *geometry, {.storage = an::DrizzleMapStorage::Dense});
        ASSERT_TRUE(map.hasValue());
        auto view = *map->view();
        Accumulators baseline(output);
        auto result = an::accumulateDrizzleBand(*reference, model, {pixels}, view, baseline.band(0u, output.height()),
                                                table ? &*table : nullptr);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->implementation, an::DrizzleAccumulationImplementation::Reference);
        EXPECT_EQ(result->selected, samples.size());
        EXPECT_GT(result->contributions, 0u);
        for (std::uint32_t threads : {1u, 2u, 3u})
        {
            auto execution = an::ExecutionContext::create(*engine, {.maxConcurrency = threads});
            Accumulators optimized(output);
            for (std::uint32_t frame = 0u; frame < 3u; ++frame)
            {
                result = an::accumulateDrizzleBand(*execution, model, {pixels, 0u, frame}, view,
                                                   optimized.band(0u, output.height()), table ? &*table : nullptr);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
            }
            EXPECT_EQ(result->implementation, an::DrizzleAccumulationImplementation::OwnedRows);
            for (std::size_t i = 0u; i < baseline.sums.size(); ++i)
            {
                EXPECT_NEAR(optimized.sums[i], baseline.sums[i] * 3.0, 1e-10);
                EXPECT_NEAR(optimized.weights[i], baseline.weights[i] * 3.0, 1e-10);
                EXPECT_EQ(optimized.coverage[i], baseline.coverage[i] * 3u);
                if (optimized.weights[i] > 0.0)
                    EXPECT_NEAR(optimized.sums[i] / optimized.weights[i], 0.25 / (1.5 * 1.5), 1e-12);
            }
        }
    }
}

TEST(DrizzleAccumulationTest, RecordedRowBoundsChangeNoTermAndSkipRowsThatCannotReachAStrip)
{
    // The same band through a view that carries per-row bounds and one that does not, for the
    // two kernels whose reach differs most, at one and four workers: every term bit-identical.
    // A row whose bounds say it reaches nothing is skipped without being read, and a row whose
    // bounds are wider than its coordinates is merely visited, so a loose bound is never wrong.
    for (const auto kernel : {an::DrizzleKernel::Point, an::DrizzleKernel::Square, an::DrizzleKernel::Lanczos3})
        for (const std::uint32_t workers : {1u, 4u})
        {
            SCOPED_TRACE(static_cast<int>(kernel));
            SCOPED_TRACE(workers);
            const auto model = accumulationModel(kernel, 23u, 19u);
            const auto source = model.request().input.extent(), canvas = model.outputDescriptor().extent();
            std::vector<float> samples(source.sampleCount()), x(samples.size()), y(samples.size());
            std::mt19937 generator(41u);
            std::uniform_real_distribution<float> jitter(-0.3f, 0.3f);
            for (std::uint32_t row = 0u; row < source.height(); ++row)
                for (std::uint32_t column = 0u; column < source.width(); ++column)
                {
                    const auto i = row * source.width() + column;
                    samples[i] = 0.25f + 0.5f * static_cast<float>(i % 7u) / 7.0f;
                    x[i] = static_cast<float>(column) * 1.5f + 0.3f + jitter(generator);
                    y[i] = static_cast<float>(row) * 1.5f + 0.7f + jitter(generator);
                }
            std::vector<float> bounds(2u * source.height());
            for (std::uint32_t row = 0u; row < source.height(); ++row)
            {
                float low = std::numeric_limits<float>::infinity(), high = -low;
                for (std::uint32_t column = 0u; column < source.width(); ++column)
                {
                    low = (std::min)(low, y[row * source.width() + column]);
                    high = (std::max)(high, y[row * source.width() + column]);
                }
                bounds[2u * row] = low;
                bounds[2u * row + 1u] = high;
            }
            std::optional<an::DrizzleKernelTable> table;
            if (kernel == an::DrizzleKernel::Lanczos3)
                table = *an::DrizzleKernelTable::create(kernel);
            auto engine = an::EngineContext::create({.maxConcurrency = workers});
            auto execution = an::ExecutionContext::create(*engine, {});
            const auto run = [&](std::span<const float> rowBounds, Accumulators& output) {
                const auto map = an::DrizzlePixmapView::create(source, canvas, 0u, source.height(), 0u, source.height(),
                                                               x, y, rowBounds);
                ASSERT_TRUE(map.hasValue()) << map.status().message();
                const auto report =
                    an::accumulateDrizzleBand(*execution, model, {sampleBand(source, samples, an::SampleType::Float32)},
                                              *map, output.band(0u, canvas.height()), table ? &*table : nullptr);
                ASSERT_TRUE(report.hasValue()) << report.status().message();
                EXPECT_EQ(report->selected, samples.size());
            };
            Accumulators scanned(canvas), recorded(canvas), loose(canvas);
            run({}, scanned);
            run(bounds, recorded);
            std::vector<float> looseBounds = bounds;
            for (std::uint32_t row = 0u; row < source.height(); ++row)
            {
                looseBounds[2u * row] -= 5.0f;
                looseBounds[2u * row + 1u] += 5.0f;
            }
            run(looseBounds, loose);
            EXPECT_EQ(scanned.sums, recorded.sums);
            EXPECT_EQ(scanned.weights, recorded.weights);
            EXPECT_EQ(scanned.coverage, recorded.coverage);
            EXPECT_EQ(scanned.sums, loose.sums);
            EXPECT_EQ(scanned.weights, loose.weights);
        }
}

TEST(DrizzleAccumulationTest, CollidingPointsRetainSourceOrderAndCountEachFrameOnce)
{
    const auto model = accumulationModel(an::DrizzleKernel::Point, 3u, 2u, an::SampleType::Float64, 1.0);
    const auto extent = model.request().input.extent();
    std::vector<double> samples{1e16, 1.0, -1e16, 2.0, 0.0, -1.0};
    const auto pixels = sampleBand(extent, samples, an::SampleType::Float64);
    std::vector<float> x(6u, 0.0f), y(6u, 0.0f);
    auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 2u, 0u, 2u, x, y);
    for (std::uint32_t threads : {1u, 2u, 4u})
    {
        auto engine = an::EngineContext::create({.maxConcurrency = threads});
        auto execution = an::ExecutionContext::create(*engine, {});
        Accumulators output(extent);
        const auto result = an::accumulateDrizzleBand(*execution, model, {pixels}, map, output.band(0u, 2u));
        ASSERT_TRUE(result.hasValue());
        EXPECT_DOUBLE_EQ(output.sums[0], 1.0);
        EXPECT_DOUBLE_EQ(output.weights[0], 6.0);
        EXPECT_EQ(output.coverage[0], 1u);
        EXPECT_EQ(result->contributions, 6u);
        EXPECT_EQ(result->covered, 1u);
    }
}

TEST(DrizzleAccumulationTest, RefusesBadInputsAndAliasingBeforeWriting)
{
    const auto model = accumulationModel(an::DrizzleKernel::Point, 3u, 2u, an::SampleType::Float64, 1.0);
    const auto extent = model.request().input.extent();
    std::vector<double> samples(6u, 0.0);
    auto pixels = sampleBand(extent, samples, an::SampleType::Float64);
    std::vector<float> x(6u, 0.0f), y(6u, 0.0f);
    auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 2u, 0u, 2u, x, y);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    Accumulators output(extent);
    auto destination = output.band(0u, 2u);
    auto input = an::DrizzleInputBand{pixels};
    input.frameWeights[0] = -1.0;
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, destination).status().code(),
              an::ErrorCode::InvalidArgument);
    input.frameWeights[0] = 1.0;
    samples.back() = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, destination).status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(output.weights, std::vector<double>(6u, 0.0));
    samples.back() = 0.0;
    destination.sums = samples;
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, destination).status().code(),
              an::ErrorCode::Aliasing);
    destination = output.band(0u, 2u);
    destination.weightSums = destination.sums;
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, destination).status().code(),
              an::ErrorCode::Aliasing);
}

TEST(DrizzleAccumulationTest, LanczosTableKeepsSignedLobesAndChecksDistance)
{
    for (const auto kernel : {an::DrizzleKernel::Lanczos2, an::DrizzleKernel::Lanczos3})
    {
        const auto table = an::DrizzleKernelTable::create(kernel);
        ASSERT_TRUE(table.hasValue());
        EXPECT_FLOAT_EQ(table->lookup(0.0f).value(), 1.0f);
        EXPECT_LT(table->lookup(1.5f).value(), 0.0f);
        EXPECT_EQ(table->lookup(-1.5f).value(), table->lookup(1.5f).value());
        EXPECT_EQ(table->lookup(100000.0f).value(), 0.0f);
        EXPECT_EQ(table->lookup(std::numeric_limits<float>::infinity()).status().code(),
                  an::ErrorCode::InvalidArgument);
    }
    EXPECT_EQ(an::DrizzleKernelTable::create(an::DrizzleKernel::Point).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DrizzleAccumulationTest, OverlapPreservesAreaUnderOrientationAndTranslation)
{
    const an::DrizzleFootprint square{{{-0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, -0.5f}, {-0.5f, -0.5f}}};
    EXPECT_DOUBLE_EQ(an::drizzleSquareOverlap(square, 0, 0).value(), 1.0);
    EXPECT_DOUBLE_EQ(an::drizzleSquareOverlap(square, 1, 0).value(), 0.0);
    auto reversed = square;
    std::reverse(reversed.begin(), reversed.end());
    EXPECT_DOUBLE_EQ(an::drizzleSquareOverlap(reversed, 0, 0).value(), 1.0);
    const an::DrizzleFootprint enormous{{{-1e30f, 1e30f}, {1e30f, 1e30f}, {1e30f, -1e30f}, {-1e30f, -1e30f}}};
    EXPECT_DOUBLE_EQ(an::drizzleSquareOverlap(enormous, 0, 0).value(), 1.0);
}

TEST(DrizzleAccumulationTest, BoundedSourceAndDestinationTilesMatchWholeFrameBitForBit)
{
    for (const auto kernel : {an::DrizzleKernel::Point, an::DrizzleKernel::Square, an::DrizzleKernel::Turbo,
                              an::DrizzleKernel::Gaussian, an::DrizzleKernel::Lanczos2, an::DrizzleKernel::Lanczos3})
    {
        const auto model = accumulationModel(kernel, 19u, 11u);
        const auto source = model.request().input.extent(), output = model.outputDescriptor().extent();
        std::vector<float> samples(source.sampleCount());
        for (std::size_t i = 0u; i < samples.size(); ++i)
            samples[i] = static_cast<float>((i * 31u) % 127u) / 127.0f;
        auto engine = an::EngineContext::create({.maxConcurrency = 4u});
        auto execution = an::ExecutionContext::create(*engine, {});
        const auto geometry =
            *an::DrizzleGeometryPlan::create(*execution, *an::DrizzleMapDescriptor::create(model, {}));
        auto dense = an::generateDrizzlePixmap(*execution, geometry, {.storage = an::DrizzleMapStorage::Dense});
        std::optional<an::DrizzleKernelTable> table;
        if (kernel == an::DrizzleKernel::Lanczos2 || kernel == an::DrizzleKernel::Lanczos3)
            table = *an::DrizzleKernelTable::create(kernel);
        const auto* coefficients = table ? &*table : nullptr;
        Accumulators baseline(output);
        ASSERT_TRUE(an::accumulateDrizzleBand(*execution, model, {sampleBand(source, samples, an::SampleType::Float32)},
                                              *dense->view(), baseline.band(0u, output.height()), coefficients)
                        .hasValue());
        for (std::uint32_t first = 0u; first < output.height(); first += 3u)
        {
            const auto rows = (std::min)(3u, output.height() - first);
            Accumulators tile(*an::ImageExtent::create(output.width(), rows, 1u, execution->limits()));
            for (std::uint32_t row = 0u; row < source.height(); row += 4u)
            {
                const auto count = (std::min)(4u, source.height() - row);
                auto map = an::generateDrizzlePixmap(*execution, geometry,
                                                     {.firstRow = row, .rowCount = count, .haloRows = 1u});
                const auto extent = *an::ImageExtent::create(source.width(), count, 1u, execution->limits());
                std::vector<float> band(samples.begin() + row * source.width(),
                                        samples.begin() + (row + count) * source.width());
                const auto result = an::accumulateDrizzleBand(*execution, model,
                                                              {sampleBand(extent, band, an::SampleType::Float32), row},
                                                              *map->view(), tile.band(first, rows), coefficients);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
            }
            for (std::size_t i = 0u; i < tile.sums.size(); ++i)
            {
                const auto index = first * output.width() + i;
                EXPECT_EQ(tile.sums[i], baseline.sums[index]);
                EXPECT_EQ(tile.weights[i], baseline.weights[index]);
                EXPECT_EQ(tile.coverage[i], baseline.coverage[index]);
            }
        }
    }
}

TEST(DrizzleAccumulationTest, EveryValidityRuleAndNonFinitePolicyIsExplicit)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    for (const auto rule : {an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly, an::ValidityRule::ZeroIsNoData,
                            an::ValidityRule::ExplicitMask, an::ValidityRule::DomainRange})
    {
        auto request = accumulationModel(an::DrizzleKernel::Point, 5u, 1u, an::SampleType::Float64, 1.0).request();
        an::ValidityDescriptorRequest validity{.rule = rule};
        if (rule == an::ValidityRule::DomainRange)
        {
            validity.lowestValid = 0.0;
            validity.highestValid = 1.0;
        }
        request.validity = *an::ValidityDescriptor::create(validity);
        if (rule == an::ValidityRule::ExplicitMask)
            request.maskExtent = request.input.extent();
        request.parameters.nonFinite = an::DrizzleNonFinitePolicy::Exclude;
        const auto model = *an::DrizzleModel::create(request);
        const auto extent = request.input.extent();
        std::vector<double> samples{0.0, 0.5, -2.0, std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::quiet_NaN()};
        std::vector<float> x{0, 1, 2, 3, 4}, y(5u, 0.0f);
        auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 1u, 0u, 1u, x, y);
        const std::array<std::uint8_t, 1u> bits{0x02u};
        const auto mask = an::ValidityMaskView<const std::uint8_t>::create(bits, extent, 1u, 1u);
        Accumulators output(extent);
        an::DrizzleInputBand input{sampleBand(extent, samples, an::SampleType::Float64)};
        if (rule == an::ValidityRule::ExplicitMask)
            input.mask = &*mask;
        auto result = an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 1u));
        ASSERT_TRUE(result.hasValue());
        const auto expected =
            rule == an::ValidityRule::ExplicitMask
                ? 1u
                : (rule == an::ValidityRule::ZeroIsNoData || rule == an::ValidityRule::DomainRange ? 2u : 3u);
        EXPECT_EQ(result->selected, expected);
        EXPECT_EQ(result->excluded, 5u - expected);
        EXPECT_EQ(result->nonFinite,
                  rule == an::ValidityRule::AllValid || rule == an::ValidityRule::ZeroIsNoData ? 2u : 0u);
        EXPECT_EQ(output.coverage[0],
                  rule == an::ValidityRule::ZeroIsNoData || rule == an::ValidityRule::ExplicitMask ? 0u : 1u);
    }
}

template <typename T> void checkResidentType(an::SampleType type)
{
    const auto model = accumulationModel(an::DrizzleKernel::Point, 3u, 1u, type, 1.0);
    const auto extent = model.request().input.extent();
    std::vector<T> samples{T{0}, T{17}, (std::numeric_limits<T>::max)()};
    if constexpr (std::is_floating_point_v<T>)
        samples[2] = T{42};
    std::vector<float> x{0, 1, 2}, y(3u, 0.0f);
    const auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 1u, 0u, 1u, x, y);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    Accumulators output(extent);
    const auto before = execution->memoryResource()->counters();
    const auto result =
        an::accumulateDrizzleBand(*execution, model, {sampleBand(extent, samples, type)}, map, output.band(0u, 1u));
    ASSERT_TRUE(result.hasValue());
    for (std::size_t i = 0u; i < 3u; ++i)
        EXPECT_EQ(output.sums[i], static_cast<double>(samples[i]));
    EXPECT_EQ(execution->memoryResource()->counters().liveBytes, before.liveBytes);
}

TEST(DrizzleAccumulationTest, EveryResidentRepresentationWidensWithoutDomainConversionOrAllocation)
{
    checkResidentType<std::uint8_t>(an::SampleType::UInt8);
    checkResidentType<std::int8_t>(an::SampleType::Int8);
    checkResidentType<std::uint16_t>(an::SampleType::UInt16);
    checkResidentType<std::int16_t>(an::SampleType::Int16);
    checkResidentType<std::uint32_t>(an::SampleType::UInt32);
    checkResidentType<std::int32_t>(an::SampleType::Int32);
    checkResidentType<float>(an::SampleType::Float32);
    checkResidentType<double>(an::SampleType::Float64);
}

TEST(DrizzleAccumulationTest, CancellationAndDeadlineReturnTheirCodesIncludingAfterPartialWrites)
{
    struct StopSink : an::TraceSink
    {
        std::stop_source stop;
        void onTrace(const an::TraceEvent&) noexcept override
        {
            stop.request_stop();
        }
    } sink;
    const auto model = accumulationModel(an::DrizzleKernel::Point, 3u, 2u, an::SampleType::Float32, 1.0);
    const auto extent = model.request().input.extent();
    std::vector<float> samples(6u, 1.0f), x{0, 1, 2, 0, 1, 2}, y{0, 0, 0, 1, 1, 1};
    const auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 2u, 0u, 2u, x, y);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {.stopToken = sink.stop.get_token(), .traceSink = &sink});
    Accumulators output(extent);
    const auto input = an::DrizzleInputBand{sampleBand(extent, samples, an::SampleType::Float32)};
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 2u)).status().code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(output.weights, (std::vector<double>{1, 1, 1, 0, 0, 0}));
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 2u)).status().code(),
              an::ErrorCode::Cancelled);
    *execution = an::ExecutionContext{};
    auto expired =
        an::ExecutionContext::create(*engine, {.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1)});
    EXPECT_EQ(an::accumulateDrizzleBand(*expired, model, input, map, output.band(0u, 2u)).status().code(),
              an::ErrorCode::DeadlineExceeded);
}

TEST(DrizzleAccumulationTest, DegenerateNonConvexAndNonFiniteDropsAreRefused)
{
    an::DrizzleFootprint corners{};
    EXPECT_EQ(an::drizzleSquareOverlap(corners, 0, 0).status().code(), an::ErrorCode::InvalidData);
    corners = {{{0, 0}, {1, 1}, {0, 1}, {1, 0}}};
    EXPECT_EQ(an::drizzleSquareOverlap(corners, 0, 0).status().code(), an::ErrorCode::InvalidData);
    corners[0].x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(an::drizzleSquareOverlap(corners, 0, 0).status().code(), an::ErrorCode::InvalidData);
}

TEST(DrizzleAccumulationTest, CfaRoutingUsesEveryBayerPatternAndGlobalCropPhase)
{
    for (auto pattern : {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
        for (std::uint32_t phase = 0u; phase < 4u; ++phase)
        {
            auto request = accumulationModel(an::DrizzleKernel::Point, 3u, 3u, an::SampleType::Float32, 1.0).request();
            auto description = request.input.toRequest();
            description.numericDomain = an::NumericDomain::RawSensorCode;
            description.cfa = *an::CfaDescriptor::create(pattern, phase % 2u, phase / 2u);
            request.input = *an::ImageDescriptor::create(description);
            request.parameters.channelWeights = {2.0, 3.0, 5.0};
            const auto model = *an::DrizzleModel::create(request);
            const auto source = request.input.extent(), output = model.outputDescriptor().extent();
            const auto bandExtent = *an::ImageExtent::create(3u, 2u, 1u, an::defaultEngineLimits());
            std::vector<float> samples{1, 2, 3, 4, 5, 6}, x{0, 1, 2, 0, 1, 2}, y{1, 1, 1, 2, 2, 2};
            const auto map = *an::DrizzlePixmapView::create(source, output, 1u, 3u, 1u, 3u, x, y);
            an::DrizzleInputBand input{sampleBand(bandExtent, samples, an::SampleType::Float32), 1u};
            input.frameWeights = {7.0, 11.0, 13.0, 1.0};
            auto engine = an::EngineContext::create({.maxConcurrency = 3u});
            auto execution = an::ExecutionContext::create(*engine, {});
            Accumulators accumulated(output);
            const auto result = an::accumulateDrizzleBand(*execution, model, input, map, accumulated.band(0u, 3u));
            ASSERT_TRUE(result.hasValue());
            EXPECT_EQ(result->contributions, 6u);
            for (std::uint32_t yy = 1u; yy < 3u; ++yy)
                for (std::uint32_t xx = 0u; xx < 3u; ++xx)
                    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const auto expectedChannel = static_cast<std::uint32_t>(description.cfa.colorAt(xx, yy));
                        const auto index = channel * 9u + yy * 3u + xx;
                        const double weight = channel == expectedChannel ? request.parameters.channelWeights[channel] *
                                                                               input.frameWeights[channel]
                                                                         : 0.0;
                        EXPECT_EQ(accumulated.weights[index], weight);
                        EXPECT_EQ(accumulated.sums[index], weight * samples[(yy - 1u) * 3u + xx]);
                        EXPECT_EQ(accumulated.coverage[index], channel == expectedChannel ? 1u : 0u);
                    }
        }
}

TEST(DrizzleAccumulationTest, PaddedPlanarInterleavedAndMixedChannelsApplyAllWeightFactors)
{
    for (std::uint32_t perPixel : {1u, 2u, 4u})
    {
        auto request = accumulationModel(an::DrizzleKernel::Point, 3u, 2u, an::SampleType::Float32, 1.0).request();
        auto description = request.input.toRequest();
        description.extent = *an::ImageExtent::create(3u, 2u, 4u, an::defaultEngineLimits());
        description.channelSchema = an::ChannelSchema::Rgba;
        request.input = *an::ImageDescriptor::create(description);
        auto weightDescription = description;
        weightDescription.channelSchema = an::ChannelSchema::Rgba;
        request.pixelWeights = *an::ImageDescriptor::create(weightDescription);
        auto flatDescription = request.input.toRequest();
        flatDescription.extent = *an::ImageExtent::create(3u, 2u, 1u, an::defaultEngineLimits());
        flatDescription.channelSchema = an::ChannelSchema::Mono;
        request.flatWeights = *an::ImageDescriptor::create(flatDescription);
        request.parameters.weightScale = 2.0;
        request.parameters.channelWeights = {2.0, 3.0, 5.0};
        const auto checkedModel = an::DrizzleModel::create(request);
        ASSERT_TRUE(checkedModel.hasValue()) << checkedModel.status().message();
        const auto model = *checkedModel;
        const auto extent = request.input.extent();
        const auto rowStride = (3u * perPixel + 2u) * sizeof(float), planeStride = rowStride * 2u + 16u;
        const auto layout = *an::ImageLayout::createWithStrides({extent, 4u, perPixel, 4u / perPixel, 4u}, rowStride,
                                                                planeStride, an::defaultEngineLimits());
        std::vector<float> samples(layout.totalBytes() / 4u, std::numeric_limits<float>::quiet_NaN());
        std::vector<float> pixelWeights(samples.size(), -1.0f), flats(6u, 0.5f);
        for (std::uint32_t channel = 0u; channel < 4u; ++channel)
            for (std::uint32_t y = 0u; y < 2u; ++y)
                for (std::uint32_t x = 0u; x < 3u; ++x)
                {
                    const auto offset =
                        channel / perPixel * planeStride / 4u + y * rowStride / 4u + x * perPixel + channel % perPixel;
                    samples[offset] = static_cast<float>(channel + 1u);
                    pixelWeights[offset] = channel == 2u ? 0.0f : static_cast<float>(channel + 1u);
                }
        const an::DrizzleSampleBand weights{layout, std::as_bytes(std::span{pixelWeights}), an::SampleType::Float32};
        const auto flat = sampleBand(flatDescription.extent, flats, an::SampleType::Float32);
        an::DrizzleInputBand input{{layout, std::as_bytes(std::span{samples}), an::SampleType::Float32}};
        input.pixelWeights = &weights;
        input.flatWeights = &flat;
        input.frameWeights = {2, 4, 8, 16};
        std::vector<float> x{0, 1, 2, 0, 1, 2}, y{0, 0, 0, 1, 1, 1};
        const auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 2u, 0u, 2u, x, y);
        auto engine = an::EngineContext::create({.maxConcurrency = 2u});
        auto execution = an::ExecutionContext::create(*engine, {});
        Accumulators output(extent);
        auto result = an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 2u));
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->selected, 18u);
        EXPECT_EQ(result->excluded, 6u);
        for (std::uint32_t channel = 0u; channel < 4u; ++channel)
            for (std::size_t i = 0u; i < 6u; ++i)
            {
                const double factor = channel == 2u
                                          ? 0.0
                                          : (channel + 1u) * input.frameWeights[channel] *
                                                (channel < 3u ? request.parameters.channelWeights[channel] : 1.0);
                EXPECT_EQ(output.weights[channel * 6u + i], factor);
                EXPECT_EQ(output.sums[channel * 6u + i], factor * (channel + 1u));
            }
        const auto previous = output.sums;
        flats.back() = 0.0f;
        EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 2u)).status().code(),
                  an::ErrorCode::InvalidData);
        EXPECT_EQ(output.sums, previous);
    }
}

TEST(DrizzleAccumulationTest, InvalidMapsHugeCoordinatesTiesAndOverflowHaveDefinedOutcomes)
{
    const auto model = accumulationModel(an::DrizzleKernel::Point, 3u, 2u, an::SampleType::Float64, 1.0);
    const auto extent = model.request().input.extent();
    std::vector<double> samples(6u, 1.0);
    std::vector<float> x{
        -0.5f, 0.5f, -0.499f, std::numeric_limits<float>::max(), std::numeric_limits<float>::quiet_NaN(), 1.0f};
    std::vector<float> y(6u, 0.0f);
    const auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 2u, 0u, 2u, x, y);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    Accumulators output(extent);
    an::DrizzleInputBand input{sampleBand(extent, samples, an::SampleType::Float64)};
    auto result = an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 2u));
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result->contributions, 3u);
    EXPECT_EQ(output.weights[0], 1.0);
    EXPECT_EQ(output.weights[1], 2.0);
    samples.back() = (std::numeric_limits<double>::max)();
    input.frameWeights[0] = 2.0;
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 2u)).status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(output.weights[1], 2.0);
    samples.back() = 1.0;
    input.frameWeights[0] = 1.0;
    output.sums[1] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, model, input, map, output.band(0u, 2u)).status().code(),
              an::ErrorCode::InvalidData);
}

TEST(DrizzleAccumulationTest, CurvedAndReflectedFootprintsAgreeAcrossTailsAndWorkerPartitions)
{
    std::mt19937 random(71u);
    for (std::uint32_t width : {2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u})
        for (auto kernel : {an::DrizzleKernel::Square, an::DrizzleKernel::Gaussian, an::DrizzleKernel::Lanczos3})
        {
            const auto model = accumulationModel(kernel, width, 9u, an::SampleType::Float32, 1.0);
            const auto extent = model.request().input.extent();
            std::vector<float> samples(extent.sampleCount()), x(samples.size()), y(samples.size());
            for (std::uint32_t row = 0u; row < 9u; ++row)
                for (std::uint32_t column = 0u; column < width; ++column)
                {
                    const auto i = row * width + column;
                    samples[i] = static_cast<float>(random() % 1000u) / 1000.0f;
                    x[i] = static_cast<float>(width - 1u - column + 0.19 * std::sin(row + column * 0.2));
                    y[i] = static_cast<float>(row + 0.3 * std::sin(column * 0.13));
                }
            auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 9u, 0u, 9u, x, y);
            std::optional<an::DrizzleKernelTable> table;
            if (kernel == an::DrizzleKernel::Lanczos3)
                table = *an::DrizzleKernelTable::create(kernel);
            auto engine = an::EngineContext::create({.maxConcurrency = 4u});
            Accumulators oracle(extent), baseline(extent);
            for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
                for (std::uint32_t threads : {1u, 2u, 4u})
                {
                    auto execution =
                        an::ExecutionContext::create(*engine, {.maxConcurrency = threads, .backend = backend});
                    Accumulators output(extent);
                    auto result = an::accumulateDrizzleBand(*execution, model,
                                                            {sampleBand(extent, samples, an::SampleType::Float32)}, map,
                                                            output.band(0u, 9u), table ? &*table : nullptr);
                    ASSERT_TRUE(result.hasValue()) << result.status().message();
                    if (backend == an::ComputeBackend::Reference)
                        oracle = output;
                    else
                    {
                        if (threads == 1u)
                            baseline = output;
                        EXPECT_EQ(output.sums, baseline.sums);
                        EXPECT_EQ(output.weights, baseline.weights);
                        EXPECT_EQ(output.coverage, baseline.coverage);
                        for (std::size_t i = 0u; i < samples.size(); ++i)
                        {
                            EXPECT_NEAR(output.sums[i], oracle.sums[i], 1e-10);
                            EXPECT_NEAR(output.weights[i], oracle.weights[i], 1e-10);
                            EXPECT_EQ(output.coverage[i], oracle.coverage[i]);
                        }
                    }
                }
        }
}

TEST(DrizzleAccumulationTest, SignedLobesRemainNegativeAndMissingHaloFailsBeforeWrites)
{
    auto request = accumulationModel(an::DrizzleKernel::Lanczos2, 4u, 3u, an::SampleType::Float32, 1.0).request();
    request.parameters.pixelFraction = 1.0;
    const auto model = *an::DrizzleModel::create(request);
    const auto extent = request.input.extent();
    std::vector<float> samples(12u, 2.0f), x(12u, std::numeric_limits<float>::quiet_NaN()), y(x);
    x[0] = y[0] = 0.25f;
    const auto map = *an::DrizzlePixmapView::create(extent, extent, 0u, 3u, 0u, 3u, x, y);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto table = *an::DrizzleKernelTable::create(an::DrizzleKernel::Lanczos2);
    Accumulators output(extent);
    ASSERT_TRUE(an::accumulateDrizzleBand(*execution, model, {sampleBand(extent, samples, an::SampleType::Float32)},
                                          map, output.band(0u, 3u), &table)
                    .hasValue());
    EXPECT_LT(output.weights[2], 0.0);
    EXPECT_EQ(output.sums[2], 2.0 * output.weights[2]);
    EXPECT_EQ(output.coverage[2], 1u);
    request.parameters.kernel = an::DrizzleKernel::Square;
    const auto square = *an::DrizzleModel::create(request);
    const auto bandExtent = *an::ImageExtent::create(4u, 1u, 1u, execution->limits());
    std::vector<float> band(4u, 1.0f);
    const auto missing =
        *an::DrizzlePixmapView::create(extent, extent, 1u, 2u, 1u, 2u, std::span{x}.first(4u), std::span{y}.first(4u));
    const auto previous = output.sums;
    EXPECT_EQ(an::accumulateDrizzleBand(*execution, square, {sampleBand(bandExtent, band, an::SampleType::Float32), 1u},
                                        missing, output.band(0u, 3u))
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(output.sums, previous);
}

TEST(DrizzleAccumulationTest, TightTraversalRetainsFullCanvasSupportAtSmallDropsAndMaximumScale)
{
    for (const auto kernel : {an::DrizzleKernel::Square, an::DrizzleKernel::Point, an::DrizzleKernel::Turbo,
                              an::DrizzleKernel::Gaussian, an::DrizzleKernel::Lanczos2, an::DrizzleKernel::Lanczos3})
        for (double scale : {1.0, 4.0})
            for (double fraction : {0.05, 1.0})
            {
                SCOPED_TRACE(static_cast<int>(kernel));
                SCOPED_TRACE(scale);
                SCOPED_TRACE(fraction);
                auto request = accumulationModel(kernel, 3u, 3u, an::SampleType::Float32, scale).request();
                request.parameters.pixelFraction = fraction;
                const auto model = *an::DrizzleModel::create(request);
                const auto source = request.input.extent(), canvas = model.outputDescriptor().extent();
                std::vector<float> samples(9u, 1.0f), x(9u), y(9u);
                for (std::uint32_t i = 0u; i < 9u; ++i)
                {
                    x[i] = static_cast<float>((i % 3u) * scale + 0.125);
                    y[i] = static_cast<float>((i / 3u) * scale + 0.375);
                }
                const auto map = *an::DrizzlePixmapView::create(source, canvas, 0u, 3u, 0u, 3u, x, y);
                std::optional<an::DrizzleKernelTable> table;
                if (kernel == an::DrizzleKernel::Lanczos2 || kernel == an::DrizzleKernel::Lanczos3)
                    table = *an::DrizzleKernelTable::create(kernel);
                std::vector<double> oracle(canvas.sampleCount(), 0.0);
                const double width = scale * fraction, sigma = width / 2.3548;
                for (std::uint32_t i = 0u; i < 9u; ++i)
                {
                    an::DrizzleFootprint corners{};
                    double area = 1.0;
                    if (kernel == an::DrizzleKernel::Square)
                    {
                        corners = *an::interpolateDrizzleCorners(map, i % 3u, i / 3u, fraction * 0.5);
                        area = 0.0;
                        for (std::size_t edge = 1u; edge < 3u; ++edge)
                            area += (static_cast<double>(corners[edge].x) - corners[0].x) *
                                        (static_cast<double>(corners[edge + 1u].y) - corners[0].y) -
                                    (static_cast<double>(corners[edge].y) - corners[0].y) *
                                        (static_cast<double>(corners[edge + 1u].x) - corners[0].x);
                        area = std::abs(area) * 0.5;
                    }
                    const auto axis = [&](double pixel, double centre) {
                        const double distance = pixel - centre;
                        if (kernel == an::DrizzleKernel::Point)
                            return pixel == std::round(centre) ? 1.0 : 0.0;
                        if (kernel == an::DrizzleKernel::Turbo)
                            return (std::max)(0.0, (std::min)(width * 0.5, distance + 0.5) -
                                                       (std::max)(-width * 0.5, distance - 0.5));
                        if (table)
                            return static_cast<double>(*table->lookup(static_cast<float>(distance / width)));
                        if (pixel < std::floor(centre - 3.0 * sigma) || pixel > std::ceil(centre + 3.0 * sigma))
                            return 0.0;
                        return std::exp(-0.5 * distance * distance / (sigma * sigma));
                    };
                    for (std::uint32_t yy = 0u; yy < canvas.height(); ++yy)
                        for (std::uint32_t xx = 0u; xx < canvas.width(); ++xx)
                        {
                            double coefficient = kernel == an::DrizzleKernel::Square
                                                     ? *an::drizzleSquareOverlap(corners, xx, yy) / area
                                                     : axis(xx, x[i]) * axis(yy, y[i]);
                            if (kernel == an::DrizzleKernel::Turbo)
                                coefficient /= width * width;
                            if (kernel == an::DrizzleKernel::Gaussian)
                                coefficient /= 2.0 * std::numbers::pi * sigma * sigma;
                            if ((table || kernel == an::DrizzleKernel::Gaussian) && std::abs(coefficient) < 1e-10)
                                coefficient = 0.0;
                            oracle[yy * canvas.width() + xx] += coefficient;
                        }
                }
                auto engine = an::EngineContext::create({.maxConcurrency = 4u});
                for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
                {
                    auto execution = an::ExecutionContext::create(*engine, {.backend = backend});
                    Accumulators output(canvas);
                    ASSERT_TRUE(an::accumulateDrizzleBand(*execution, model,
                                                          {sampleBand(source, samples, an::SampleType::Float32)}, map,
                                                          output.band(0u, canvas.height()), table ? &*table : nullptr)
                                    .hasValue());
                    for (std::size_t i = 0u; i < oracle.size(); ++i)
                    {
                        EXPECT_NEAR(output.weights[i], oracle[i], 1e-10);
                        EXPECT_NEAR(output.sums[i], oracle[i] / (scale * scale), 1e-10);
                    }
                }
            }
}

} // namespace
