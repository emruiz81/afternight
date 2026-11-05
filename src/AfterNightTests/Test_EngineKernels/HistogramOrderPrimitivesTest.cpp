// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for bounded-scratch histogram, selection, quantile, median, and MAD primitives.

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext
orderEngine(std::uint32_t concurrency, std::uint64_t memoryBudgetBytes = an::defaultEngineLimits().memoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryBudgetBytes;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext orderExecution(const an::EngineContext& engine, std::uint32_t concurrency)
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

/// The ascending selected values a test compares an exact selection against.
[[nodiscard]] std::vector<double> sortedOracle(std::span<const float> samples)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const float sample : samples)
    {
        values.push_back(static_cast<double>(sample));
    }
    std::sort(values.begin(), values.end());
    return values;
}

/// The same quantile convention the primitive publishes, evaluated on an already sorted oracle.
[[nodiscard]] double oracleQuantile(const std::vector<double>& sorted, double quantile,
                                    an::QuantileInterpolation interpolation)
{
    const double position = quantile * static_cast<double>(sorted.size() - 1u);
    const double floored = std::floor(position);
    const std::size_t lower = static_cast<std::size_t>(floored);
    const double fraction = position - floored;
    switch (interpolation)
    {
    case an::QuantileInterpolation::LowerRank:
        return sorted[lower];
    case an::QuantileInterpolation::NearestRank:
        return sorted[static_cast<std::size_t>(std::floor(position + 0.5))];
    case an::QuantileInterpolation::Linear:
        break;
    }
    if (fraction == 0.0)
    {
        return sorted[lower];
    }
    return sorted[lower] + fraction * (sorted[lower + 1u] - sorted[lower]);
}

/// A deterministic spread of values with repeats, ties, and negative samples.
[[nodiscard]] std::vector<float> orderSamples(std::size_t count)
{
    std::vector<float> samples(count, 0.0f);
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    for (std::size_t index = 0u; index < count; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const std::uint32_t bits = static_cast<std::uint32_t>(state >> 33u);
        samples[index] = static_cast<float>(bits % 1024u) / 512.0f - 1.0f;
    }
    return samples;
}

/// A deterministic spread with the entropy of real image data: distinct values across several
/// exponents, which is what makes a digit descent pay for every digit rather than two.
[[nodiscard]] std::vector<float> denseOrderSamples(std::size_t count)
{
    std::vector<float> samples(count, 0.0f);
    std::uint64_t state = 0x243f6a8885a308d3ull;
    for (std::size_t index = 0u; index < count; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        // A full-mantissa fraction in [0, 1), shifted so the samples straddle zero the way a
        // calibrated frame does.
        const std::uint32_t bits = static_cast<std::uint32_t>(state >> 32u);
        const double unit = static_cast<double>(bits) / 4294967296.0;
        samples[index] = static_cast<float>(unit * 1.5 - 0.25);
    }
    return samples;
}

[[nodiscard]] an::PlaneView<const float> constPlane(std::span<const float> samples, std::uint32_t width,
                                                    std::uint32_t height, std::uint32_t strideSamples)
{
    const std::optional<an::PlaneView<const float>> view = an::PlaneView<const float>::create(
        samples, width, height, 1u, static_cast<std::uint64_t>(strideSamples) * sizeof(float));
    EXPECT_TRUE(view.has_value());
    return *view;
}

[[nodiscard]] an::PlaneView<float> writablePlane(std::span<float> samples, std::uint32_t width, std::uint32_t height,
                                                 std::uint32_t strideSamples)
{
    const std::optional<an::PlaneView<float>> view = an::PlaneView<float>::create(
        samples, width, height, 1u, static_cast<std::uint64_t>(strideSamples) * sizeof(float));
    EXPECT_TRUE(view.has_value());
    return *view;
}

/// The window order statistic, computed one pixel at a time by an independent scalar oracle.
[[nodiscard]] float oracleWindowValue(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                                      std::uint32_t stride, std::uint32_t x, std::uint32_t y,
                                      const an::OrderWindowRequest& request)
{
    std::vector<double> window;
    for (std::int64_t offsetY = -static_cast<std::int64_t>(request.radiusY);
         offsetY <= static_cast<std::int64_t>(request.radiusY); ++offsetY)
    {
        for (std::int64_t offsetX = -static_cast<std::int64_t>(request.radiusX);
             offsetX <= static_cast<std::int64_t>(request.radiusX); ++offsetX)
        {
            std::int64_t sampleX = static_cast<std::int64_t>(x) + offsetX;
            std::int64_t sampleY = static_cast<std::int64_t>(y) + offsetY;
            const bool outside = sampleX < 0 || sampleY < 0 || sampleX >= static_cast<std::int64_t>(width) ||
                                 sampleY >= static_cast<std::int64_t>(height);
            if (outside && request.border == an::KernelBorderMode::Constant)
            {
                window.push_back(static_cast<double>(request.constantBorder));
                continue;
            }
            sampleX = (std::max<std::int64_t>)(0, (std::min<std::int64_t>)(sampleX, width - 1));
            sampleY = (std::max<std::int64_t>)(0, (std::min<std::int64_t>)(sampleY, height - 1));
            window.push_back(static_cast<double>(
                samples[static_cast<std::size_t>(sampleY) * stride + static_cast<std::size_t>(sampleX)]));
        }
    }
    std::sort(window.begin(), window.end());
    return static_cast<float>(oracleQuantile(window, request.quantile, request.interpolation));
}

/// One selection of the plane these scan-cost cases share.
[[nodiscard]] an::Result<an::PlaneOrderStatistics>
selectFrom(const an::ExecutionContext& execution, std::span<const float> samples, const an::ImageLayout& layout,
           std::span<const double> quantiles, std::span<double> values, bool deviation)
{
    return an::selectPlaneOrderStatistics(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                              .samples = samples.data(),
                              .layout = &layout,
                              .plane = 0u,
                              .sampleInPixel = 0u},
        quantiles, values, an::PlaneOrderRequest{.medianAbsoluteDeviation = deviation}, an::KernelValidityView{});
}

} // namespace

TEST(HistogramOrderPrimitivesTest, ACompiledBracketCountRanksExactlyAsTheBlockLoopDoes)
{
    // A selection counts its bracket's bins with the compiled run of the instruction set it runs on, and with its own
    // loops on the reference backend. Every quantile, the median and its deviation, the extremes and the number of
    // plane reads are the same either way - over a frame with real-image entropy, a sky between far outliers, repeated
    // values on both sides of zero and a plane of signed zeros and subnormals, at a length no block divides, and over
    // each of them again through a value window, which only the compiled run counts a block at a time.
    constexpr std::uint32_t kWidth = 1021u;
    constexpr std::uint32_t kHeight = 37u;
    constexpr std::size_t kCount = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<std::vector<float>> planes;
    planes.push_back(denseOrderSamples(kCount));
    planes.push_back(orderSamples(kCount));
    std::vector<float> sky(kCount, 0.0f);
    std::uint64_t state = 0x5851f42d4c957f2dull;
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double unit = static_cast<double>(static_cast<std::uint32_t>(state >> 32u)) / 4294967296.0;
        sky[index] = index % 997u == 0u ? static_cast<float>(1.0e6 * unit) : static_cast<float>(0.12 + 0.002 * unit);
    }
    planes.push_back(std::move(sky));
    std::vector<float> zeros(kCount, 0.0f);
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        const float smallest = std::numeric_limits<float>::denorm_min();
        const std::array<float, 6u> values{-0.0f, 0.0f, smallest, -smallest, 3.0f * smallest, 1.0f};
        zeros[index] = values[(index * 7u + index / 13u) % values.size()];
    }
    planes.push_back(std::move(zeros));
    // Zeros of either sign as the smallest values of one plane and the largest of another: the first zero in the
    // samples' order is a negative zero at the second sample, and the first zero of a lane of four or of eight
    // samples a positive one, so an extreme that keeps the first of a lane's equal values rather than the first in
    // the samples' order carries the wrong sign; the rest are scattered.
    std::vector<float> zerosBelow(kCount, 0.0f);
    std::vector<float> zerosAbove(kCount, 0.0f);
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const std::uint32_t draw = static_cast<std::uint32_t>(state >> 40u);
        const float magnitude = 0.25f + static_cast<float>(draw % 1000u) / 2000.0f;
        float zero = (draw & 1u) != 0u ? -0.0f : 0.0f;
        bool isZero = draw % 5u == 0u;
        if (index < 9u)
        {
            isZero = index == 1u || index == 4u || index == 8u;
            zero = index == 1u ? -0.0f : 0.0f;
        }
        zerosBelow[index] = isZero ? zero : magnitude;
        zerosAbove[index] = isZero ? zero : -magnitude;
    }
    planes.push_back(std::move(zerosBelow));
    planes.push_back(std::move(zerosAbove));
    // Two clusters a window keeps and a third it leaves out, placed so that the median lies between the kept clusters
    // and the deviations of the samples left out fall among the deviations the median absolute deviation's bin
    // gathers: a gather that decided membership on the ordered value alone would take them.
    std::vector<float> clusters(kCount, 0.0f);
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double unit = static_cast<double>(static_cast<std::uint32_t>(state >> 32u)) / 4294967296.0;
        if (index % 3u == 0u)
            clusters[index] = static_cast<float>(0.005 * unit);
        else if (index % 3u == 1u)
            clusters[index] = static_cast<float>(0.005 + 0.001 * unit);
        else
            clusters[index] = static_cast<float>(1.5 + 0.1 * unit);
    }
    planes.push_back(std::move(clusters));
    // A plane whose top percent lies within a bin's width of a plateau at seven tenths, with a sparse half percent
    // below it and three percent above it that a window to seven tenths leaves out and a hold to it would read as the
    // plateau: the bin the quantile just below the top gathers holds the plateau and the distinct values under it,
    // so a gather that read a sample left out as its held value would take it.
    std::vector<float> edge(kCount, 0.0f);
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const std::uint32_t draw = static_cast<std::uint32_t>(state >> 40u);
        const double unit = static_cast<double>(static_cast<std::uint32_t>(state >> 8u) & 0xffffffu) / 16777216.0;
        const std::uint32_t slot = draw % 1000u;
        if (slot < 3u)
            edge[index] = 0.7f;
        else if (slot < 10u)
            edge[index] = 0.7f - static_cast<float>(1u + static_cast<std::uint32_t>(unit * 400.0)) * 1.2e-7f;
        else if (slot < 15u)
            edge[index] = static_cast<float>(0.5 + 0.19 * unit);
        else if (slot < 45u)
            edge[index] = 1.5f;
        else
            edge[index] = static_cast<float>(0.3 + 0.19 * unit);
    }
    planes.push_back(std::move(edge));
    // What each plane's windowed variant reads through: the middle half of its values, but for the zero planes,
    // whose window admits every value so that the compiled count and extremes read them, the clusters, whose window
    // leaves the lowest cluster out, and the edge, whose window ends at the plateau its hold ends at.
    struct Reading
    {
        std::optional<an::KernelValueWindow> window;
        std::optional<an::KernelValueWindow> hold;
    };
    std::vector<Reading> readings(planes.size());
    readings[4] = Reading{.window = an::KernelValueWindow{.lowest = -1.0, .highest = 1.0}, .hold = std::nullopt};
    readings[5] = readings[4];
    readings[6] = Reading{.window = an::KernelValueWindow{.lowest = 0.005, .highest = 2.0}, .hold = std::nullopt};
    readings[7] = Reading{.window = an::KernelValueWindow{.lowest = 0.0, .highest = 0.7},
                          .hold = an::KernelValueWindow{.lowest = 0.0, .highest = 0.7}};

    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (const an::InstructionSet candidate : {an::InstructionSet::Avx2, an::InstructionSet::Avx512Skylake})
    {
        if (an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            targets.push_back(candidate);
        }
    }
    ASSERT_FALSE(targets.empty()) << "this host runs no compiled count to hold to the block loop";

    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);
    const auto selectOn = [&](const std::vector<float>& samples, an::ComputeBackend backend,
                              an::InstructionSet instructionSet, std::vector<double>& values,
                              const std::optional<an::KernelValueWindow>& window,
                              const std::optional<an::KernelValueWindow>& hold) {
        an::ExecutionOptions options;
        options.maxConcurrency = 4u;
        options.backend = backend;
        options.instructionSet = instructionSet;
        options.backendFallback = an::BackendFallback::Forbidden;
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        EXPECT_TRUE(execution.hasValue()) << execution.status().message();
        constexpr std::array<double, 8u> kQuantiles{0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 0.99, 1.0};
        values.assign(kQuantiles.size(), -1.0);
        an::KernelPlaneSelection selection{.originX = 0u, .originY = 0u, .width = kWidth, .height = kHeight};
        selection.valueWindow = window;
        return an::selectPlaneOrderStatisticsOver(*execution,
                                                  an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                                                                        .samples = samples.data(),
                                                                        .layout = &layout,
                                                                        .plane = 0u,
                                                                        .sampleInPixel = 0u},
                                                  kQuantiles, values,
                                                  an::PlaneOrderRequest{.medianAbsoluteDeviation = true, .hold = hold},
                                                  an::KernelValidityView{}, selection);
    };
    // Each plane as it is, through a value window, and held: the window and the hold are the same quarter-to-three-
    // quarter interval, so the one leaves out what the other reads as its ends.
    for (std::size_t variant = 0u; variant < planes.size() * 3u; ++variant)
    {
        const std::size_t planeIndex = variant % planes.size();
        SCOPED_TRACE(variant);
        std::vector<double> retained;
        std::optional<an::KernelValueWindow> window;
        std::optional<an::KernelValueWindow> hold;
        if (variant >= planes.size())
        {
            // Half the plane's values on each side of its median are left out, or held.
            const std::vector<double> all = sortedOracle(planes[planeIndex]);
            const an::KernelValueWindow quarters{.lowest = all[all.size() / 4u], .highest = all[all.size() * 3u / 4u]};
            if (variant >= 2u * planes.size())
            {
                hold = quarters;
            }
            else
            {
                window = readings[planeIndex].window.value_or(quarters);
                hold = readings[planeIndex].hold;
            }
        }
        for (const float sample : planes[planeIndex])
        {
            if (!window.has_value() || (sample >= window->lowest && sample <= window->highest))
            {
                const double value = static_cast<double>(sample);
                retained.push_back(hold.has_value() ? (std::min)((std::max)(value, hold->lowest), hold->highest)
                                                    : value);
            }
        }
        std::vector<double> expectedValues;
        const an::Result<an::PlaneOrderStatistics> expected =
            selectOn(planes[planeIndex], an::ComputeBackend::Reference, an::InstructionSet::Automatic, expectedValues,
                     window, hold);
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        std::vector<double> sorted = retained;
        std::sort(sorted.begin(), sorted.end());
        EXPECT_EQ(expected->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
        for (const an::InstructionSet target : targets)
        {
            SCOPED_TRACE(static_cast<int>(target));
            std::vector<double> values;
            const an::Result<an::PlaneOrderStatistics> compiled =
                selectOn(planes[planeIndex], an::ComputeBackend::Cpu, target, values, window, hold);
            ASSERT_TRUE(compiled.hasValue()) << compiled.status().message();
            for (std::size_t index = 0u; index < values.size(); ++index)
            {
                EXPECT_EQ(std::bit_cast<std::uint64_t>(values[index]),
                          std::bit_cast<std::uint64_t>(expectedValues[index]))
                    << "quantile " << index;
            }
            EXPECT_EQ(std::bit_cast<std::uint64_t>(compiled->median), std::bit_cast<std::uint64_t>(expected->median));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(compiled->medianAbsoluteDeviation),
                      std::bit_cast<std::uint64_t>(expected->medianAbsoluteDeviation));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(compiled->minimum), std::bit_cast<std::uint64_t>(expected->minimum));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(compiled->maximum), std::bit_cast<std::uint64_t>(expected->maximum));
            EXPECT_EQ(compiled->planeScans, expected->planeScans);
        }
    }
}

TEST(HistogramOrderPrimitivesTest, AHeldSelectionRanksEveryAdmittedSampleAsHeldToItsInterval)
{
    // A hold admits no sample and turns none away: the rule, the mask and the value window decide on the stored
    // value, and an admitted sample outside the hold is ordered as its nearer end. Over a sky large enough for the
    // sampled bracket, with samples below zero, above one, not a number and infinite, and over a dense plane
    // straddling zero - packed, with padded rows and under a mask, through the all-valid rule, the finite-only rule
    // read as its window, a range rule and a value window, held to the unit interval and to an interval inside the
    // sky that turns most of it into two plateaus - every quantile, the median, the deviation, the extremes and the
    // count are those of the held values, bit for bit, at one worker and at four; a packed plane held costs the scans
    // its held copy costs; and a hold that is not a finite ordered interval is refused by both entry points.
    constexpr std::uint32_t kWidth = 1600u;
    constexpr std::uint32_t kHeight = 1400u;
    constexpr std::size_t kCount = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<float> sky(kCount, 0.0f);
    std::uint64_t state = 0x2545f4914f6cdd1dull;
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double unit = static_cast<double>(static_cast<std::uint32_t>(state >> 32u)) / 4294967296.0;
        sky[index] = static_cast<float>(0.1 + 0.004 * unit);
        if (index % 4099u == 0u)
            sky[index] = 1.0f;
        else if (index % 5003u == 0u)
            sky[index] = 1.5f;
        else if (index % 9973u == 0u)
            sky[index] = 0.0f;
        else if (index % 7919u == 0u)
            sky[index] = -0.25f;
        else if (index % 15013u == 0u)
            sky[index] = std::numeric_limits<float>::quiet_NaN();
        else if (index % 20011u == 0u)
            sky[index] = std::numeric_limits<float>::infinity();
        else if (index % 30011u == 0u)
            sky[index] = -std::numeric_limits<float>::infinity();
    }
    constexpr std::uint32_t kDenseWidth = 1021u;
    constexpr std::uint32_t kDenseHeight = 37u;
    const std::vector<float> dense = denseOrderSamples(static_cast<std::size_t>(kDenseWidth) * kDenseHeight);
    constexpr std::uint32_t kPadding = 7u;
    std::vector<float> padded(static_cast<std::size_t>(kWidth + kPadding) * kHeight, -9.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        std::copy_n(sky.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * kWidth), kWidth,
                    padded.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * (kWidth + kPadding)));
    }

    an::ValidityDescriptorRequest finiteRequest;
    finiteRequest.rule = an::ValidityRule::FiniteOnly;
    an::ValidityDescriptorRequest rangeRequest;
    rangeRequest.rule = an::ValidityRule::DomainRange;
    rangeRequest.lowestValid = -0.2;
    rangeRequest.highestValid = 1.2;
    an::ValidityDescriptorRequest maskRequest;
    maskRequest.rule = an::ValidityRule::ExplicitMask;
    const an::ValidityDescriptor finiteOnly = *an::ValidityDescriptor::create(finiteRequest);
    const an::ValidityDescriptor ranged = *an::ValidityDescriptor::create(rangeRequest);
    const an::ValidityDescriptor byMask = *an::ValidityDescriptor::create(maskRequest);
    // The mask admits the finite samples inside the range rule's interval, and leaves every third row out.
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(kHeight) * ((kWidth + 7u) / 8u), 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const float value = sky[static_cast<std::size_t>(y) * kWidth + x];
            ASSERT_TRUE(writableMask->setValid(0u, x, y, y % 3u != 2u && value >= -0.2f && value <= 1.2f));
        }
    }
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();

    struct Subject
    {
        const char* name;
        const std::vector<float>* samples;
        an::ImageLayout layout;
        std::uint32_t width;
        std::uint32_t height;
        an::KernelValidityView validity;
        /// Whether the oracle admits one stored value, before any value window.
        bool (*admits)(float, const an::ValidityMaskView<const std::uint8_t>*, std::uint32_t, std::uint32_t);
        bool packed;
    };
    const auto finiteAdmits = [](float value, const an::ValidityMaskView<const std::uint8_t>*, std::uint32_t,
                                 std::uint32_t) { return std::isfinite(value); };
    const auto everyAdmits = [](float, const an::ValidityMaskView<const std::uint8_t>*, std::uint32_t, std::uint32_t) {
        return true;
    };
    const auto rangeAdmits = [](float value, const an::ValidityMaskView<const std::uint8_t>*, std::uint32_t,
                                std::uint32_t) { return value >= -0.2f && value <= 1.2f; };
    const auto maskAdmits = [](float, const an::ValidityMaskView<const std::uint8_t>* bits, std::uint32_t x,
                               std::uint32_t y) { return bits->test(0u, x, y).value_or(false); };
    const std::array<Subject, 5u> subjects{
        Subject{"packed sky, finite only", &sky, singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth),
                kWidth, kHeight, an::KernelValidityView{.descriptor = finiteOnly}, finiteAdmits, true},
        Subject{"dense plane, all valid", &dense,
                singlePlaneLayout(an::SampleType::Float32, kDenseWidth, kDenseHeight, kDenseWidth), kDenseWidth,
                kDenseHeight, an::KernelValidityView{}, everyAdmits, true},
        Subject{"packed sky, range rule", &sky, singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth),
                kWidth, kHeight, an::KernelValidityView{.descriptor = ranged}, rangeAdmits, true},
        Subject{"padded sky, finite only", &padded,
                singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth + kPadding), kWidth, kHeight,
                an::KernelValidityView{.descriptor = finiteOnly}, finiteAdmits, false},
        Subject{"masked sky", &sky, singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth), kWidth,
                kHeight, an::KernelValidityView{.descriptor = byMask, .mask = &mask}, maskAdmits, false}};
    const std::array<an::KernelValueWindow, 2u> holds{an::KernelValueWindow{.lowest = 0.0, .highest = 1.0},
                                                      an::KernelValueWindow{.lowest = 0.1005, .highest = 0.1035}};
    const std::array<std::optional<an::KernelValueWindow>, 2u> windows{
        std::nullopt, an::KernelValueWindow{.lowest = 0.1, .highest = 0.104}};
    constexpr std::array<double, 5u> kQuantiles{0.0, 0.1, 0.5, 0.9, 1.0};
    const auto same = [](double left, double right) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    };
    const auto deviationOf = [](const std::vector<double>& sorted, double median) {
        std::vector<double> deviations;
        deviations.reserve(sorted.size());
        for (const double value : sorted)
            deviations.push_back(std::abs(value - median));
        std::sort(deviations.begin(), deviations.end());
        return oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear);
    };

    an::EngineContext engine = orderEngine(4u);
    for (const std::uint32_t workers : {4u, 1u})
    {
        SCOPED_TRACE(workers);
        an::ExecutionContext execution = orderExecution(engine, workers);
        for (const Subject& subject : subjects)
        {
            SCOPED_TRACE(subject.name);
            const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                               .samples = subject.samples->data(),
                                               .layout = &subject.layout,
                                               .plane = 0u,
                                               .sampleInPixel = 0u};
            const std::uint64_t rowSamples = subject.layout.rowStrideBytes() / sizeof(float);
            for (const an::KernelValueWindow& hold : holds)
            {
                SCOPED_TRACE(hold.lowest);
                for (const std::optional<an::KernelValueWindow>& window : windows)
                {
                    SCOPED_TRACE(window.has_value());
                    std::vector<double> held;
                    for (std::uint32_t y = 0u; y < subject.height; ++y)
                    {
                        for (std::uint32_t x = 0u; x < subject.width; ++x)
                        {
                            const float stored = (*subject.samples)[static_cast<std::size_t>(y) * rowSamples + x];
                            if (!subject.admits(stored, &mask, x, y))
                                continue;
                            const double value = static_cast<double>(stored);
                            if (window.has_value() && !(value >= window->lowest && value <= window->highest))
                                continue;
                            held.push_back((std::min)((std::max)(value, hold.lowest), hold.highest));
                        }
                    }
                    std::vector<double> sorted = held;
                    std::sort(sorted.begin(), sorted.end());
                    ASSERT_FALSE(sorted.empty());

                    an::KernelPlaneSelection selection{
                        .originX = 0u, .originY = 0u, .width = subject.width, .height = subject.height};
                    selection.valueWindow = window;
                    std::vector<double> values(kQuantiles.size(), -1.0);
                    const an::Result<an::PlaneOrderStatistics> ranked = an::selectPlaneOrderStatisticsOver(
                        execution, source, kQuantiles, values,
                        an::PlaneOrderRequest{.medianAbsoluteDeviation = true, .hold = hold}, subject.validity,
                        selection);
                    ASSERT_TRUE(ranked.hasValue()) << ranked.status().message();
                    EXPECT_EQ(ranked->run.selection.valid, sorted.size());
                    EXPECT_TRUE(same(ranked->minimum, sorted.front()));
                    EXPECT_TRUE(same(ranked->maximum, sorted.back()));
                    const double median = oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear);
                    EXPECT_TRUE(same(ranked->median, median)) << ranked->median << " " << median;
                    EXPECT_TRUE(same(ranked->medianAbsoluteDeviation, deviationOf(sorted, median)))
                        << ranked->medianAbsoluteDeviation;
                    for (std::size_t index = 0u; index < kQuantiles.size(); ++index)
                    {
                        EXPECT_TRUE(same(values[index],
                                         oracleQuantile(sorted, kQuantiles[index], an::QuantileInterpolation::Linear)))
                            << "quantile " << index << " " << values[index];
                    }

                    // A range rule admits a held copy's outliers, which it turned away in the source, so only a rule
                    // that admits the same samples of both is compared.
                    if (!subject.packed || window.has_value() ||
                        subject.validity.descriptor.rule() == an::ValidityRule::DomainRange)
                        continue;
                    // The same plane held before it is read, under the same rule, costs the same reads.
                    std::vector<float> heldCopy = *subject.samples;
                    for (float& value : heldCopy)
                    {
                        if (std::isfinite(value))
                        {
                            value = static_cast<float>(
                                (std::min)((std::max)(static_cast<double>(value), hold.lowest), hold.highest));
                        }
                    }
                    const an::KernelPlaneSource copySource{.sampleType = an::SampleType::Float32,
                                                           .samples = heldCopy.data(),
                                                           .layout = &subject.layout,
                                                           .plane = 0u,
                                                           .sampleInPixel = 0u};
                    std::vector<double> copyValues(kQuantiles.size(), -1.0);
                    const an::Result<an::PlaneOrderStatistics> ofCopy = an::selectPlaneOrderStatisticsOver(
                        execution, copySource, kQuantiles, copyValues,
                        an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, subject.validity, selection);
                    ASSERT_TRUE(ofCopy.hasValue()) << ofCopy.status().message();
                    if (hold.lowest == 0.0 && hold.highest == 1.0)
                    {
                        // Held to the unit interval the copy's samples are the held values exactly, so the ranks and
                        // the reads agree; an interval whose ends no Float32 holds narrows the copy's, so only the
                        // reads are compared.
                        EXPECT_TRUE(same(ofCopy->median, ranked->median));
                        EXPECT_TRUE(same(ofCopy->medianAbsoluteDeviation, ranked->medianAbsoluteDeviation));
                    }
                    EXPECT_EQ(ofCopy->planeScans, ranked->planeScans);
                    EXPECT_EQ(ofCopy->peakScratchBytes, ranked->peakScratchBytes);
                }
            }
        }
    }

    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = sky.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    an::ExecutionContext execution = orderExecution(engine, 4u);
    for (const an::KernelValueWindow& malformed :
         {an::KernelValueWindow{.lowest = 0.5, .highest = 0.25},
          an::KernelValueWindow{.lowest = std::numeric_limits<double>::quiet_NaN(), .highest = 1.0},
          an::KernelValueWindow{.lowest = 0.0, .highest = std::numeric_limits<double>::infinity()}})
    {
        const an::Result<an::PlaneOrderStatistics> whole =
            an::selectPlaneOrderStatistics(execution, source, {}, {}, an::PlaneOrderRequest{.hold = malformed},
                                           an::KernelValidityView{.descriptor = finiteOnly});
        ASSERT_FALSE(whole.hasValue());
        EXPECT_EQ(whole.status().code(), an::ErrorCode::InvalidArgument);
        const an::Result<an::PlaneOrderStatistics> over = an::selectPlaneOrderStatisticsOver(
            execution, source, {}, {}, an::PlaneOrderRequest{.hold = malformed},
            an::KernelValidityView{.descriptor = finiteOnly},
            an::KernelPlaneSelection{.originX = 0u, .originY = 0u, .width = kWidth, .height = kHeight});
        ASSERT_FALSE(over.hasValue());
        EXPECT_EQ(over.status().code(), an::ErrorCode::InvalidArgument);
    }
}

TEST(HistogramOrderPrimitivesTest, AnIntervalRuleRanksAsTheWindowItKeepsAndAsTheMaskThatStatesIt)
{
    // A range rule keeps one closed interval of widened values, and a finite-only rule over a floating representation
    // keeps the finite ones, so each is read as the all-valid samples inside that interval: the plane reads and the
    // ranks of the value window stating it, and the selection of a mask stating the same samples. Over a sky large
    // enough for the sampled bracket, with clipped, negative, infinite and not-a-number samples the rules leave out,
    // alone and narrowed again by a value window; the compaction and the histogram read the same samples too.
    constexpr std::uint32_t kWidth = 1600u;
    constexpr std::uint32_t kHeight = 1400u;
    constexpr std::size_t kCount = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<float> samples(kCount, 0.0f);
    std::uint64_t state = 0x2545f4914f6cdd1dull;
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double unit = static_cast<double>(static_cast<std::uint32_t>(state >> 32u)) / 4294967296.0;
        samples[index] = static_cast<float>(0.1 + 0.004 * unit);
        if (index % 4099u == 0u)
        {
            samples[index] = 1.0f;
        }
        else if (index % 9973u == 0u)
        {
            samples[index] = 0.0f;
        }
        else if (index % 7919u == 0u)
        {
            samples[index] = -0.25f;
        }
        else if (index % 15013u == 0u)
        {
            samples[index] = std::numeric_limits<float>::quiet_NaN();
        }
        else if (index % 20011u == 0u)
        {
            samples[index] = std::numeric_limits<float>::infinity();
        }
        else if (index % 30011u == 0u)
        {
            samples[index] = -std::numeric_limits<float>::infinity();
        }
    }

    an::ValidityDescriptorRequest rangeRequest;
    rangeRequest.rule = an::ValidityRule::DomainRange;
    rangeRequest.lowestValid = std::nextafter(0.0, 1.0);
    rangeRequest.highestValid = std::nextafter(1.0, 0.0);
    an::ValidityDescriptorRequest finiteRequest;
    finiteRequest.rule = an::ValidityRule::FiniteOnly;
    an::ValidityDescriptorRequest maskRequest;
    maskRequest.rule = an::ValidityRule::ExplicitMask;
    const an::ValidityDescriptor byMask = *an::ValidityDescriptor::create(maskRequest);
    struct KeptInterval
    {
        an::ValidityDescriptor rule;
        double lowest = 0.0;
        double highest = 0.0;
    };
    const double largest = static_cast<double>((std::numeric_limits<float>::max)());
    const std::array<KeptInterval, 2u> rules{
        KeptInterval{.rule = *an::ValidityDescriptor::create(rangeRequest),
                     .lowest = std::nextafter(0.0, 1.0),
                     .highest = std::nextafter(1.0, 0.0)},
        KeptInterval{.rule = *an::ValidityDescriptor::create(finiteRequest), .lowest = -largest, .highest = largest}};

    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    const an::KernelPlaneSelection whole{.originX = 0u, .originY = 0u, .width = kWidth, .height = kHeight};
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);
    constexpr std::array<double, 5u> kQuantiles{0.0, 0.1, 0.5, 0.9, 1.0};
    const auto select = [&](const an::KernelValidityView& validity, const std::optional<an::KernelValueWindow>& window,
                            std::vector<double>& values) {
        values.assign(kQuantiles.size(), -1.0);
        an::KernelPlaneSelection selection = whole;
        selection.valueWindow = window;
        return an::selectPlaneOrderStatisticsOver(execution, source, kQuantiles, values,
                                                  an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, validity,
                                                  selection);
    };
    const auto same = [](double left, double right) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    };

    for (const KeptInterval& kept : rules)
    {
        SCOPED_TRACE(static_cast<int>(kept.rule.rule()));
        std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(kHeight) * ((kWidth + 7u) / 8u), 0u);
        const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
            an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
        ASSERT_TRUE(writableMask.has_value());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const double value = samples[static_cast<std::size_t>(y) * kWidth + x];
                ASSERT_TRUE(writableMask->setValid(0u, x, y, value >= kept.lowest && value <= kept.highest));
            }
        }
        const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
        const an::KernelValidityView ruled{.descriptor = kept.rule};
        const an::KernelValidityView masked{.descriptor = byMask, .mask = &mask};

        for (const bool narrowed : {false, true})
        {
            SCOPED_TRACE(narrowed);
            std::optional<an::KernelValueWindow> window;
            double lowest = kept.lowest;
            double highest = kept.highest;
            if (narrowed)
            {
                window = an::KernelValueWindow{.lowest = 0.1005, .highest = 0.1035};
                lowest = (std::max)(lowest, window->lowest);
                highest = (std::min)(highest, window->highest);
            }
            std::vector<float> retained;
            for (const float sample : samples)
            {
                if (sample >= lowest && sample <= highest)
                {
                    retained.push_back(sample);
                }
            }
            std::vector<double> ruledValues;
            std::vector<double> windowedValues;
            std::vector<double> maskedValues;
            const an::Result<an::PlaneOrderStatistics> byRule = select(ruled, window, ruledValues);
            const an::Result<an::PlaneOrderStatistics> byWindow = select(
                an::KernelValidityView{}, an::KernelValueWindow{.lowest = lowest, .highest = highest}, windowedValues);
            const an::Result<an::PlaneOrderStatistics> byMaskBits = select(masked, window, maskedValues);
            ASSERT_TRUE(byRule.hasValue()) << byRule.status().message();
            ASSERT_TRUE(byWindow.hasValue()) << byWindow.status().message();
            ASSERT_TRUE(byMaskBits.hasValue()) << byMaskBits.status().message();

            const std::vector<double> sorted = sortedOracle(retained);
            EXPECT_EQ(byRule->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
            EXPECT_EQ(byRule->run.selection, (an::ValidityCount{.valid = retained.size(), .total = kCount}));
            EXPECT_EQ(byWindow->run.selection, byRule->run.selection);
            EXPECT_EQ(byMaskBits->run.selection, byRule->run.selection);
            for (std::size_t index = 0u; index < kQuantiles.size(); ++index)
            {
                EXPECT_TRUE(same(ruledValues[index], windowedValues[index])) << "quantile " << index;
                EXPECT_TRUE(same(ruledValues[index], maskedValues[index])) << "quantile " << index;
            }
            for (const an::PlaneOrderStatistics* other : {&*byWindow, &*byMaskBits})
            {
                EXPECT_TRUE(same(byRule->median, other->median));
                EXPECT_TRUE(same(byRule->medianAbsoluteDeviation, other->medianAbsoluteDeviation));
                EXPECT_TRUE(same(byRule->minimum, other->minimum));
                EXPECT_TRUE(same(byRule->maximum, other->maximum));
            }
            // Read as the window it keeps, the rule costs what that window costs.
            EXPECT_EQ(byRule->planeScans, byWindow->planeScans);
            EXPECT_EQ(byRule->peakScratchBytes, byWindow->peakScratchBytes);
        }

        std::vector<double> ruledSamples(kCount, -1.0);
        std::vector<double> maskedSamples(kCount, -1.0);
        const an::Result<an::PlaneCompactionResult> ruledCompaction =
            an::compactSelectedPlaneSamples(execution, source, ruled, whole, ruledSamples);
        const an::Result<an::PlaneCompactionResult> maskedCompaction =
            an::compactSelectedPlaneSamples(execution, source, masked, whole, maskedSamples);
        ASSERT_TRUE(ruledCompaction.hasValue()) << ruledCompaction.status().message();
        ASSERT_TRUE(maskedCompaction.hasValue()) << maskedCompaction.status().message();
        EXPECT_EQ(ruledCompaction->run.selection, maskedCompaction->run.selection);
        EXPECT_EQ(ruledCompaction->run.outputsWritten, maskedCompaction->run.outputsWritten);
        EXPECT_EQ(ruledSamples, maskedSamples);

        std::vector<std::uint64_t> ruledBins(64u, 0u);
        std::vector<std::uint64_t> maskedBins(64u, 0u);
        const an::Result<an::PlaneHistogramResult> ruledHistogram =
            an::accumulatePlaneHistogramOver(execution, source, ruledBins, an::PlaneHistogramRequest{}, ruled, whole);
        const an::Result<an::PlaneHistogramResult> maskedHistogram =
            an::accumulatePlaneHistogramOver(execution, source, maskedBins, an::PlaneHistogramRequest{}, masked, whole);
        ASSERT_TRUE(ruledHistogram.hasValue()) << ruledHistogram.status().message();
        ASSERT_TRUE(maskedHistogram.hasValue()) << maskedHistogram.status().message();
        EXPECT_EQ(ruledBins, maskedBins);
        EXPECT_EQ(ruledHistogram->run.selection, maskedHistogram->run.selection);
        EXPECT_EQ(ruledHistogram->belowRange, maskedHistogram->belowRange);
        EXPECT_EQ(ruledHistogram->aboveRange, maskedHistogram->aboveRange);
        EXPECT_EQ(ruledHistogram->notANumber, maskedHistogram->notANumber);
    }
}

TEST(HistogramOrderPrimitivesTest, HistogramCountsEveryBinAcrossAPaddedMaskedTail)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kStride = 16u;
    std::array<float, kStride> samples{};
    for (std::uint32_t index = 0u; index < kWidth; ++index)
    {
        samples[index] = static_cast<float>(index) / 12.0f;
    }
    samples[3] = -0.5f;
    samples[4] = 1.5f;
    samples[5] = std::numeric_limits<float>::quiet_NaN();
    samples[kWidth] = 0.25f;

    std::array<std::uint8_t, 2u> maskBytes{};
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    EXPECT_TRUE(writableMask->setValid(0u, 0u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    an::ValidityDescriptorRequest validityRequest;
    validityRequest.rule = an::ValidityRule::ExplicitMask;
    const an::ValidityDescriptor validity = *an::ValidityDescriptor::create(validityRequest);

    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kStride);
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);
    std::array<std::uint64_t, 4u> bins{};
    const an::Result<an::PlaneHistogramResult> counted = an::accumulatePlaneHistogram(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                              .samples = samples.data(),
                              .layout = &layout,
                              .plane = 0u,
                              .sampleInPixel = 0u},
        bins, an::PlaneHistogramRequest{}, an::KernelValidityView{.descriptor = validity, .mask = &mask});
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();

    EXPECT_EQ(counted->run.selection.total, 13u);
    EXPECT_EQ(counted->run.selection.valid, 12u);
    EXPECT_EQ(counted->belowRange, 1u);
    EXPECT_EQ(counted->aboveRange, 1u);
    EXPECT_EQ(counted->notANumber, 1u);
    EXPECT_EQ(counted->run.outputsWritten, 9u);
    EXPECT_EQ(bins[0] + bins[1] + bins[2] + bins[3], 9u);
    EXPECT_DOUBLE_EQ(counted->lowest, 0.0);
    EXPECT_DOUBLE_EQ(counted->highest, 1.0);
    // The interval's high endpoint belongs to the final bin rather than to a bin past the end.
    EXPECT_EQ(bins[3], 4u);
}

TEST(HistogramOrderPrimitivesTest, HistogramIsIdenticalAtEveryAdmittedConcurrency)
{
    constexpr std::uint32_t kWidth = 257u;
    constexpr std::uint32_t kHeight = 9u;
    const std::vector<float> samples = orderSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);

    std::array<std::uint64_t, 64u> expected{};
    std::uint64_t expectedChunks = 0u;
    for (const std::uint32_t concurrency : {1u, 2u, 4u})
    {
        an::ExecutionContext execution = orderExecution(engine, concurrency);
        std::array<std::uint64_t, 64u> bins{};
        const an::Result<an::PlaneHistogramResult> counted = an::accumulatePlaneHistogram(
            execution,
            an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                                  .samples = samples.data(),
                                  .layout = &layout,
                                  .plane = 0u,
                                  .sampleInPixel = 0u},
            bins, an::PlaneHistogramRequest{.lowest = -1.0, .highest = 1.0}, an::KernelValidityView{});
        ASSERT_TRUE(counted.hasValue()) << counted.status().message();
        if (concurrency == 1u)
        {
            expected = bins;
            expectedChunks = counted->deterministicChunks;
        }
        EXPECT_EQ(bins, expected) << "concurrency " << concurrency;
        EXPECT_EQ(counted->deterministicChunks, expectedChunks) << "concurrency " << concurrency;
        EXPECT_LE(counted->partialBytes, an::kMaxHistogramPartialBytes);
    }
}

TEST(HistogramOrderPrimitivesTest, HistogramReadsEveryResidentRepresentationOverItsFullScale)
{
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const auto check = [&]<typename SampleT>(std::array<SampleT, 4u> samples, double lowest, double highest) {
        const an::SampleType type = an::kSampleTypeOf<SampleT>;
        const an::ImageLayout layout = singlePlaneLayout(type, 4u, 1u, 4u);
        std::array<std::uint64_t, 2u> bins{};
        const an::Result<an::PlaneHistogramResult> counted = an::accumulatePlaneHistogram(
            execution,
            an::KernelPlaneSource{
                .sampleType = type, .samples = samples.data(), .layout = &layout, .plane = 0u, .sampleInPixel = 0u},
            bins, an::PlaneHistogramRequest{.domain = an::HistogramDomain::RepresentationFullScale},
            an::KernelValidityView{});
        ASSERT_TRUE(counted.hasValue()) << an::sampleTypeName(type) << ": " << counted.status().message();
        EXPECT_DOUBLE_EQ(counted->lowest, lowest) << an::sampleTypeName(type);
        EXPECT_DOUBLE_EQ(counted->highest, highest) << an::sampleTypeName(type);
        EXPECT_EQ(bins[0] + bins[1], 4u) << an::sampleTypeName(type);
    };

    check(std::array<std::uint8_t, 4u>{0u, 1u, 200u, 255u}, 0.0, 255.0);
    check(std::array<std::int8_t, 4u>{-128, -1, 0, 127}, -128.0, 127.0);
    check(std::array<std::uint16_t, 4u>{0u, 1u, 40000u, 65535u}, 0.0, 65535.0);
    check(std::array<std::int16_t, 4u>{-32768, -1, 0, 32767}, -32768.0, 32767.0);
    check(std::array<std::uint32_t, 4u>{0u, 1u, 40000u, 4294967295u}, 0.0, 4294967295.0);
    check(std::array<std::int32_t, 4u>{-2147483647 - 1, -1, 0, 2147483647}, -2147483648.0, 2147483647.0);
    check(std::array<float, 4u>{0.0f, 0.25f, 0.75f, 1.0f}, 0.0, 1.0);
    check(std::array<double, 4u>{0.0, 0.25, 0.75, 1.0}, 0.0, 1.0);
}

TEST(HistogramOrderPrimitivesTest, HistogramMapsAnExtremeFiniteIntervalWithoutOverflow)
{
    const double maximum = (std::numeric_limits<double>::max)();
    const std::array<double, 6u> samples{-maximum, -maximum / 2.0, 0.0, maximum / 2.0, std::nextafter(maximum, 0.0),
                                         maximum};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float64, 6u, 1u, 6u);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    std::array<std::uint64_t, 4u> bins{};

    const an::Result<an::PlaneHistogramResult> counted = an::accumulatePlaneHistogram(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float64,
                              .samples = samples.data(),
                              .layout = &layout,
                              .plane = 0u,
                              .sampleInPixel = 0u},
        bins, an::PlaneHistogramRequest{.lowest = -maximum, .highest = maximum}, an::KernelValidityView{});
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(bins, (std::array<std::uint64_t, 4u>{1u, 1u, 1u, 3u}));
}

TEST(HistogramOrderPrimitivesTest, HistogramRefusesABinSpanOrIntervalItsContractExcludes)
{
    constexpr std::uint32_t kWidth = 4u;
    const std::array<float, kWidth> samples{0.0f, 0.25f, 0.5f, 0.75f};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};

    std::array<std::uint64_t, 4u> bins{};
    EXPECT_EQ(an::accumulatePlaneHistogram(execution, source, std::span<std::uint64_t>{}, an::PlaneHistogramRequest{},
                                           an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::accumulatePlaneHistogram(execution, source, bins,
                                           an::PlaneHistogramRequest{.lowest = 1.0, .highest = 0.0},
                                           an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::accumulatePlaneHistogram(
                  execution, source, bins,
                  an::PlaneHistogramRequest{.lowest = 0.0, .highest = std::numeric_limits<double>::infinity()},
                  an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    std::vector<std::uint64_t> tooMany(static_cast<std::size_t>(an::kMaxHistogramBins) + 1u, 0u);
    EXPECT_EQ(
        an::accumulatePlaneHistogram(execution, source, tooMany, an::PlaneHistogramRequest{}, an::KernelValidityView{})
            .status()
            .code(),
        an::ErrorCode::OutOfRange);
}

TEST(HistogramOrderPrimitivesTest, SelectionMatchesASortedOracleWithoutPerSampleScratch)
{
    // Large enough that a working set proportional to the sample count would be obvious: the
    // descent's tables and its one gather buffer are fixed costs, and the plane is not.
    constexpr std::uint32_t kWidth = 401u;
    constexpr std::uint32_t kHeight = 257u;
    const std::vector<float> samples = orderSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> sorted = sortedOracle(samples);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);

    const std::array<double, 5u> quantiles{0.0, 0.25, 0.5, 0.75, 1.0};
    std::array<double, 5u> values{};
    const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                              .samples = samples.data(),
                              .layout = &layout,
                              .plane = 0u,
                              .sampleInPixel = 0u},
        quantiles, values, an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, an::KernelValidityView{});
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();

    EXPECT_EQ(selected->run.selection.valid, sorted.size());
    for (std::size_t index = 0u; index < quantiles.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(values[index], oracleQuantile(sorted, quantiles[index], an::QuantileInterpolation::Linear))
            << "quantile " << quantiles[index];
    }
    EXPECT_DOUBLE_EQ(selected->minimum, sorted.front());
    EXPECT_DOUBLE_EQ(selected->maximum, sorted.back());
    EXPECT_DOUBLE_EQ(selected->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));

    std::vector<double> deviations;
    deviations.reserve(sorted.size());
    for (const double value : sorted)
    {
        deviations.push_back(std::abs(value - selected->median));
    }
    std::sort(deviations.begin(), deviations.end());
    EXPECT_DOUBLE_EQ(selected->medianAbsoluteDeviation,
                     oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear));

    // The point of the primitive: its working memory is bounded by the digit and gather contracts
    // and never by the number of samples, as a per-call vector would be.
    EXPECT_LT(selected->peakScratchBytes, static_cast<std::uint64_t>(sorted.size()) * sizeof(float));
    EXPECT_LE(selected->peakScratchBytes, an::kMaxHistogramPartialBytes);
}

TEST(HistogramOrderPrimitivesTest, SelectionGathersBothZerosIntoTheBinThatStartsAtZero)
{
    // The bin the median falls in holds a thousand zeros of each sign and a thousand small positive
    // values - few enough to gather, and more than one value - and the median's rank lies among the
    // zeros. A negative zero has to be gathered with the positive ones it equals, or the rank lands among
    // samples the gather never wrote. The same plane two higher is ranked first, so the scratch the
    // gather reuses holds values of its own rather than the zeros a fresh allocation reads as.
    constexpr std::uint32_t kWidth = 401u;
    constexpr std::uint32_t kHeight = 257u;
    const std::size_t count = static_cast<std::size_t>(kWidth) * kHeight;
    constexpr std::size_t kZeros = 2000u;
    constexpr std::size_t kSmall = 1000u;
    const std::size_t negatives = (count - 1u) / 2u - 1500u;
    std::vector<float> samples;
    samples.reserve(count);
    std::uint32_t state = 0x2545F491u;
    const auto uniform = [&state](float low, float high) {
        state = state * 1664525u + 1013904223u;
        return low + static_cast<float>(state >> 8u) * 0x1p-24f * (high - low);
    };
    samples.push_back(-1.0f);
    while (samples.size() < negatives)
        samples.push_back(-uniform(1.0e-2f, 1.0f));
    for (std::size_t zero = 0u; zero < kZeros; ++zero)
        samples.push_back(zero % 2u == 0u ? -0.0f : 0.0f);
    for (std::size_t small = 0u; small < kSmall; ++small)
        samples.push_back(uniform(1.1e-3f, 3.8e-3f));
    samples.push_back(1.0f);
    while (samples.size() < count)
        samples.push_back(uniform(1.0e-2f, 1.0f));
    // Interleave the populations, so no chunk of the plane holds one kind alone.
    for (std::size_t index = count - 1u; index > 0u; --index)
    {
        state = state * 1664525u + 1013904223u;
        std::swap(samples[index], samples[state % (index + 1u)]);
    }
    const std::vector<double> sorted = sortedOracle(samples);
    ASSERT_EQ(sorted[(count - 1u) / 2u], 0.0);
    std::vector<float> shifted(samples);
    for (float& value : shifted)
        value += 2.0f;
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    const auto source = [&layout](const std::vector<float>& plane) {
        return an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                                     .samples = plane.data(),
                                     .layout = &layout,
                                     .plane = 0u,
                                     .sampleInPixel = 0u};
    };
    for (const std::uint32_t concurrency : {1u, 4u})
    {
        an::EngineContext engine = orderEngine(concurrency);
        an::ExecutionContext execution = orderExecution(engine, concurrency);
        ASSERT_TRUE(an::selectPlaneOrderStatistics(execution, source(shifted), {}, {}, an::PlaneOrderRequest{},
                                                   an::KernelValidityView{})
                        .hasValue());
        const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
            execution, source(samples), {}, {}, an::PlaneOrderRequest{.medianAbsoluteDeviation = true},
            an::KernelValidityView{});
        ASSERT_TRUE(selected.hasValue()) << selected.status().message();
        EXPECT_EQ(selected->median, 0.0) << concurrency;
    }
}

TEST(HistogramOrderPrimitivesTest, SelectionCostsAFewPlaneScansRatherThanOnePerDigitOfTheKey)
{
    // What a selection costs is the number of times it reads the plane. On data with the entropy of
    // a real frame the top digits of a sixty-four-bit key are the sign and the exponent, which every
    // sample shares, so a descent that fixes one digit per read spends its first reads eliminating
    // almost nothing and pays one read per digit. The samples' own range says where the rank is in
    // one narrowing instead. A median alone is one counting pass and one narrowed gather, and a median
    // with its deviation is one read that counts the brackets of both and one that gathers both.
    constexpr std::uint32_t kWidth = 1024u;
    constexpr std::uint32_t kHeight = 1024u;
    const std::vector<float> samples = denseOrderSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> sorted = sortedOracle(samples);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);

    const an::Result<an::PlaneOrderStatistics> both =
        selectFrom(execution, samples, layout, std::span<const double>{}, std::span<double>{}, true);
    ASSERT_TRUE(both.hasValue()) << both.status().message();
    EXPECT_EQ(both->planeScans, 2u) << "a median and a deviation cost one count and one gather together";
    EXPECT_DOUBLE_EQ(both->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));

    const an::Result<an::PlaneOrderStatistics> medianOnly =
        selectFrom(execution, samples, layout, std::span<const double>{}, std::span<double>{}, false);
    ASSERT_TRUE(medianOnly.hasValue()) << medianOnly.status().message();
    EXPECT_LE(medianOnly->planeScans, 3u) << "a median alone counts once and gathers one narrowed bracket";
    EXPECT_DOUBLE_EQ(medianOnly->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
}

TEST(HistogramOrderPrimitivesTest, AskingForTheMedianAsAQuantileCostsNoFurtherDescent)
{
    // The median is always resolved because the deviation is defined around it, so a caller that
    // also names one half must not pay for it twice. Robust statistics and autostretch both ask
    // this way.
    constexpr std::uint32_t kWidth = 401u;
    constexpr std::uint32_t kHeight = 257u;
    const std::vector<float> samples = orderSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);

    const an::Result<an::PlaneOrderStatistics> without =
        selectFrom(execution, samples, layout, std::span<const double>{}, std::span<double>{}, false);
    ASSERT_TRUE(without.hasValue()) << without.status().message();

    const std::array<double, 1u> quantiles{0.5};
    std::array<double, 1u> values{};
    const an::Result<an::PlaneOrderStatistics> with = selectFrom(execution, samples, layout, quantiles, values, false);
    ASSERT_TRUE(with.hasValue()) << with.status().message();

    EXPECT_EQ(with->planeScans, without->planeScans) << "one half was descended for a second time";
    EXPECT_DOUBLE_EQ(values[0], with->median);
}

TEST(HistogramOrderPrimitivesTest, SelectionIsExactWhenEverySelectedSampleIsTheSameValue)
{
    // The narrowing interval collapses to a point here, which is the case a range-based step has
    // to answer without dividing by an empty span.
    constexpr std::uint32_t kWidth = 97u;
    constexpr std::uint32_t kHeight = 41u;
    const std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, -0.375f);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);

    const std::array<double, 3u> quantiles{0.0, 0.5, 1.0};
    std::array<double, 3u> values{};
    const an::Result<an::PlaneOrderStatistics> selected =
        selectFrom(execution, samples, layout, quantiles, values, true);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_DOUBLE_EQ(selected->minimum, -0.375);
    EXPECT_DOUBLE_EQ(selected->maximum, -0.375);
    EXPECT_DOUBLE_EQ(selected->median, -0.375);
    EXPECT_DOUBLE_EQ(selected->medianAbsoluteDeviation, 0.0);
    for (const double value : values)
    {
        EXPECT_DOUBLE_EQ(value, -0.375);
    }
}

TEST(HistogramOrderPrimitivesTest, SelectionIsExactWhenOneValueHoldsMoreSamplesThanTheGatherCeiling)
{
    // A narrowing step cannot finish when the bracket it lands on is one repeated value wider than
    // the gather buffer, so the exact descent still has to carry it. Half the plane is one value
    // and the rest is spread, which puts the median inside that tie.
    constexpr std::uint32_t kWidth = 401u;
    constexpr std::uint32_t kHeight = 257u;
    const std::size_t count = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<float> samples = orderSamples(count);
    for (std::size_t index = 0u; index < count / 2u + 1u; ++index)
    {
        samples[index] = 0.25f;
    }
    const std::vector<double> sorted = sortedOracle(samples);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);

    const std::array<double, 3u> quantiles{0.25, 0.5, 0.9};
    std::array<double, 3u> values{};
    const an::Result<an::PlaneOrderStatistics> selected =
        selectFrom(execution, samples, layout, quantiles, values, true);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_DOUBLE_EQ(selected->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
    for (std::size_t index = 0u; index < quantiles.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(values[index], oracleQuantile(sorted, quantiles[index], an::QuantileInterpolation::Linear))
            << "quantile " << quantiles[index];
    }

    std::vector<double> deviations;
    deviations.reserve(sorted.size());
    for (const double value : sorted)
    {
        deviations.push_back(std::abs(value - selected->median));
    }
    std::sort(deviations.begin(), deviations.end());
    EXPECT_DOUBLE_EQ(selected->medianAbsoluteDeviation,
                     oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear));
}

TEST(HistogramOrderPrimitivesTest, SelectionIsIdenticalAtEveryAdmittedConcurrency)
{
    constexpr std::uint32_t kWidth = 199u;
    constexpr std::uint32_t kHeight = 11u;
    const std::vector<float> samples = orderSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    an::EngineContext engine = orderEngine(4u);

    double expectedMedian = 0.0;
    double expectedMad = 0.0;
    std::uint64_t expectedChunks = 0u;
    for (const std::uint32_t concurrency : {1u, 2u, 4u})
    {
        an::ExecutionContext execution = orderExecution(engine, concurrency);
        const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
            execution,
            an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                                  .samples = samples.data(),
                                  .layout = &layout,
                                  .plane = 0u,
                                  .sampleInPixel = 0u},
            std::span<const double>{}, std::span<double>{}, an::PlaneOrderRequest{.medianAbsoluteDeviation = true},
            an::KernelValidityView{});
        ASSERT_TRUE(selected.hasValue()) << selected.status().message();
        if (concurrency == 1u)
        {
            expectedMedian = selected->median;
            expectedMad = selected->medianAbsoluteDeviation;
            expectedChunks = selected->deterministicChunks;
        }
        EXPECT_EQ(selected->median, expectedMedian) << "concurrency " << concurrency;
        EXPECT_EQ(selected->medianAbsoluteDeviation, expectedMad) << "concurrency " << concurrency;
        EXPECT_EQ(selected->deterministicChunks, expectedChunks) << "concurrency " << concurrency;
    }
}

TEST(HistogramOrderPrimitivesTest, SelectionHonorsEveryInterpolationRule)
{
    constexpr std::uint32_t kWidth = 4u;
    const std::array<float, kWidth> samples{1.0f, 2.0f, 3.0f, 10.0f};
    const std::vector<double> sorted{1.0, 2.0, 3.0, 10.0};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};

    for (const an::QuantileInterpolation interpolation :
         {an::QuantileInterpolation::LowerRank, an::QuantileInterpolation::NearestRank,
          an::QuantileInterpolation::Linear})
    {
        const std::array<double, 3u> quantiles{0.1, 0.5, 0.9};
        std::array<double, 3u> values{};
        const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
            execution, source, quantiles, values, an::PlaneOrderRequest{.interpolation = interpolation},
            an::KernelValidityView{});
        ASSERT_TRUE(selected.hasValue()) << selected.status().message();
        for (std::size_t index = 0u; index < quantiles.size(); ++index)
        {
            EXPECT_DOUBLE_EQ(values[index], oracleQuantile(sorted, quantiles[index], interpolation))
                << "quantile " << quantiles[index];
        }
    }
}

TEST(HistogramOrderPrimitivesTest, SelectionReadsEveryResidentRepresentation)
{
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const auto check = [&]<typename SampleT>(std::array<SampleT, 5u> samples, double median) {
        const an::SampleType type = an::kSampleTypeOf<SampleT>;
        const an::ImageLayout layout = singlePlaneLayout(type, 5u, 1u, 5u);
        const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
            execution,
            an::KernelPlaneSource{
                .sampleType = type, .samples = samples.data(), .layout = &layout, .plane = 0u, .sampleInPixel = 0u},
            std::span<const double>{}, std::span<double>{}, an::PlaneOrderRequest{}, an::KernelValidityView{});
        ASSERT_TRUE(selected.hasValue()) << an::sampleTypeName(type) << ": " << selected.status().message();
        EXPECT_DOUBLE_EQ(selected->median, median) << an::sampleTypeName(type);
    };

    check(std::array<std::uint8_t, 5u>{5u, 1u, 255u, 0u, 9u}, 5.0);
    check(std::array<std::int8_t, 5u>{5, -128, 127, 0, 9}, 5.0);
    check(std::array<std::uint16_t, 5u>{5u, 1u, 65535u, 0u, 9u}, 5.0);
    check(std::array<std::int16_t, 5u>{5, -32768, 32767, 0, 9}, 5.0);
    check(std::array<std::uint32_t, 5u>{5u, 1u, 4294967295u, 0u, 9u}, 5.0);
    check(std::array<std::int32_t, 5u>{5, -2147483647 - 1, 2147483647, 0, 9}, 5.0);
    check(std::array<float, 5u>{0.5f, 0.125f, 1.0f, 0.0f, 0.75f}, 0.5);
    check(std::array<double, 5u>{0.5, 0.125, 1.0, 0.0, 0.75}, 0.5);
}

TEST(HistogramOrderPrimitivesTest, SelectionRefusesRequestsOutsideItsStatedContract)
{
    constexpr std::uint32_t kWidth = 4u;
    const std::array<float, kWidth> samples{1.0f, 2.0f, 3.0f, 4.0f};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};

    const std::array<double, 2u> quantiles{0.25, 0.5};
    std::array<double, 1u> tooFew{};
    EXPECT_EQ(an::selectPlaneOrderStatistics(execution, source, quantiles, tooFew, an::PlaneOrderRequest{},
                                             an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    const std::array<double, 1u> outside{1.5};
    std::array<double, 1u> values{};
    EXPECT_EQ(an::selectPlaneOrderStatistics(execution, source, outside, values, an::PlaneOrderRequest{},
                                             an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);

    const an::PlaneOrderRequest unknownInterpolation{.interpolation = static_cast<an::QuantileInterpolation>(255u)};
    EXPECT_FALSE(an::isQuantileInterpolation(unknownInterpolation.interpolation));
    EXPECT_EQ(an::selectPlaneOrderStatistics(execution, source, outside, values, unknownInterpolation,
                                             an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    const std::vector<double> tooMany(static_cast<std::size_t>(an::kMaxOrderStatisticRanks) + 1u, 0.5);
    std::vector<double> manyValues(tooMany.size(), 0.0);
    EXPECT_EQ(an::selectPlaneOrderStatistics(execution, source, tooMany, manyValues, an::PlaneOrderRequest{},
                                             an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);

    an::ValidityDescriptorRequest excludeAll;
    excludeAll.rule = an::ValidityRule::ZeroIsNoData;
    const std::array<float, kWidth> zeros{0.0f, 0.0f, 0.0f, 0.0f};
    const an::KernelPlaneSource empty{.sampleType = an::SampleType::Float32,
                                      .samples = zeros.data(),
                                      .layout = &layout,
                                      .plane = 0u,
                                      .sampleInPixel = 0u};
    EXPECT_EQ(an::selectPlaneOrderStatistics(
                  execution, empty, std::span<const double>{}, std::span<double>{}, an::PlaneOrderRequest{},
                  an::KernelValidityView{.descriptor = *an::ValidityDescriptor::create(excludeAll)})
                  .status()
                  .code(),
              an::ErrorCode::EmptySelection);
}

TEST(HistogramOrderPrimitivesTest, SelectionRefusesWritableAliasingBeforeItScans)
{
    std::array<double, 4u> samples{1.0, 2.0, 3.0, 4.0};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float64, 4u, 1u, 4u);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float64,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    const std::array<double, 1u> quantile{0.5};
    const std::array<double, 4u> originalSamples = samples;
    EXPECT_EQ(an::selectPlaneOrderStatistics(execution, source, quantile, std::span<double>{samples.data(), 1u},
                                             an::PlaneOrderRequest{}, an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(samples, originalSamples);

    std::array<double, 3u> shared{0.25, 0.75, -9.0};
    const std::array<double, 3u> originalShared = shared;
    EXPECT_EQ(an::selectPlaneOrderStatistics(execution, source, std::span<const double>{shared.data(), 2u},
                                             std::span<double>{shared.data() + 1u, 2u}, an::PlaneOrderRequest{},
                                             an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(shared, originalShared);
}

TEST(HistogramOrderPrimitivesTest, OrderStatisticsRejectSelectedNonFiniteSamplesConsistently)
{
    const std::array<float, 3u> samples{1.0f, std::numeric_limits<float>::infinity(), 3.0f};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, 3u, 1u, 3u);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    EXPECT_EQ(an::selectPlaneOrderStatistics(execution, source, std::span<const double>{}, std::span<double>{},
                                             an::PlaneOrderRequest{}, an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);

    an::ValidityDescriptorRequest finiteRequest;
    finiteRequest.rule = an::ValidityRule::FiniteOnly;
    const an::ValidityDescriptor finiteOnly = *an::ValidityDescriptor::create(finiteRequest);
    const an::Result<an::PlaneOrderStatistics> selected =
        an::selectPlaneOrderStatistics(execution, source, std::span<const double>{}, std::span<double>{},
                                       an::PlaneOrderRequest{}, an::KernelValidityView{.descriptor = finiteOnly});
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_DOUBLE_EQ(selected->median, 2.0);

    const std::array<float, 3u> windowSamples{1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f};
    std::array<float, 3u> destination{-7.0f, -7.0f, -7.0f};
    EXPECT_EQ(an::selectWindowOrderStatistic(
                  execution, constPlane(windowSamples, 3u, 1u, 3u), writablePlane(destination, 3u, 1u, 3u),
                  an::OrderWindowRequest{.radiusX = 0u, .radiusY = 0u}, an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(destination, (std::array<float, 3u>{-7.0f, -7.0f, -7.0f}));

    const an::Result<an::OrderWindowResult> filtered = an::selectWindowOrderStatistic(
        execution, constPlane(windowSamples, 3u, 1u, 3u), writablePlane(destination, 3u, 1u, 3u),
        an::OrderWindowRequest{.radiusX = 0u, .radiusY = 0u}, an::KernelValidityView{.descriptor = finiteOnly});
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(destination, (std::array<float, 3u>{1.0f, -7.0f, 3.0f}));
}

TEST(HistogramOrderPrimitivesTest, WindowOrderStatisticRefusesAWindowAboveItsStatedCeiling)
{
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> destination(samples.size(), 0.0f);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);

    const std::uint32_t radius = an::kMaxOrderWindowSamples;
    const an::Result<an::OrderWindowResult> refused = an::selectWindowOrderStatistic(
        execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(destination, kWidth, kHeight, kWidth),
        an::OrderWindowRequest{.radiusX = radius, .radiusY = radius}, an::KernelValidityView{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::OutOfRange);

    const an::Result<an::OrderWindowResult> accepted = an::selectWindowOrderStatistic(
        execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(destination, kWidth, kHeight, kWidth),
        an::OrderWindowRequest{.radiusX = 1u, .radiusY = 1u}, an::KernelValidityView{});
    ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
    EXPECT_EQ(accepted->windowSamples, 9u);
    EXPECT_LE(accepted->windowSamples, an::kMaxOrderWindowSamples);

    const an::Result<an::OrderWindowResult> unknownInterpolation = an::selectWindowOrderStatistic(
        execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(destination, kWidth, kHeight, kWidth),
        an::OrderWindowRequest{.interpolation = static_cast<an::QuantileInterpolation>(255u)},
        an::KernelValidityView{});
    ASSERT_FALSE(unknownInterpolation.hasValue());
    EXPECT_EQ(unknownInterpolation.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(HistogramOrderPrimitivesTest, WindowMedianMatchesAScalarOracleOnTinyOddAndLargeWindows)
{
    constexpr std::uint32_t kWidth = 23u;
    constexpr std::uint32_t kHeight = 17u;
    const std::vector<float> samples = orderSamples(static_cast<std::size_t>(kWidth) * kHeight);
    std::vector<float> destination(samples.size(), -7.0f);
    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);

    const std::array<an::OrderWindowRequest, 4u> requests{
        an::OrderWindowRequest{.radiusX = 0u, .radiusY = 0u}, an::OrderWindowRequest{.radiusX = 1u, .radiusY = 2u},
        an::OrderWindowRequest{.radiusX = 3u, .radiusY = 3u, .quantile = 0.25},
        an::OrderWindowRequest{
            .radiusX = 5u, .radiusY = 5u, .border = an::KernelBorderMode::Constant, .constantBorder = 0.5f}};
    for (const an::OrderWindowRequest& request : requests)
    {
        std::fill(destination.begin(), destination.end(), -7.0f);
        const an::Result<an::OrderWindowResult> filtered = an::selectWindowOrderStatistic(
            execution, constPlane(samples, kWidth, kHeight, kWidth),
            writablePlane(destination, kWidth, kHeight, kWidth), request, an::KernelValidityView{});
        ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
        EXPECT_EQ(filtered->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_FLOAT_EQ(destination[static_cast<std::size_t>(y) * kWidth + x],
                                oracleWindowValue(samples, kWidth, kHeight, kWidth, x, y, request))
                    << "radius " << request.radiusX << "x" << request.radiusY << " at " << x << "," << y;
            }
        }
    }
}

TEST(HistogramOrderPrimitivesTest, WindowOrderStatisticSkipsInvalidCentersAndExcludesInvalidNeighbors)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 1u;
    const std::array<float, kWidth> samples{1.0f, 0.0f, 5.0f, 0.0f, 9.0f};
    std::array<float, kWidth> destination{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::ZeroIsNoData;
    const an::ValidityDescriptor validity = *an::ValidityDescriptor::create(request);

    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const an::Result<an::OrderWindowResult> filtered = an::selectWindowOrderStatistic(
        execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(destination, kWidth, kHeight, kWidth),
        an::OrderWindowRequest{.radiusX = 1u, .radiusY = 0u}, an::KernelValidityView{.descriptor = validity});
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();

    EXPECT_EQ(filtered->run.selection.total, 5u);
    EXPECT_EQ(filtered->run.selection.valid, 3u);
    EXPECT_EQ(filtered->run.outputsWritten, 3u);
    EXPECT_FLOAT_EQ(destination[0], 1.0f);
    EXPECT_FLOAT_EQ(destination[1], -1.0f);
    EXPECT_FLOAT_EQ(destination[2], 5.0f);
    EXPECT_FLOAT_EQ(destination[3], -1.0f);
    EXPECT_FLOAT_EQ(destination[4], 9.0f);
}

TEST(HistogramOrderPrimitivesTest, WindowOrderStatisticRefusesOverlappingBuffers)
{
    constexpr std::uint32_t kWidth = 6u;
    std::vector<float> samples(kWidth, 1.0f);
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    const an::Result<an::OrderWindowResult> refused = an::selectWindowOrderStatistic(
        execution, constPlane(samples, kWidth, 1u, kWidth), writablePlane(samples, kWidth, 1u, kWidth),
        an::OrderWindowRequest{}, an::KernelValidityView{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(HistogramOrderPrimitivesTest, SelectionOverARectangleRanksOnlyThatRectangleWhereItLies)
{
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 11u;
    constexpr std::uint32_t kStride = 41u;
    const std::vector<float> samples = orderSamples(static_cast<std::size_t>(kStride) * kHeight);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kStride);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    // A horizontal origin that is not a multiple of eight is the case a temporary mask would be
    // built for; the rectangle keeps the source strides and the mask keeps the source coordinates.
    const an::KernelPlaneSelection rectangle{.originX = 3u, .originY = 2u, .width = 19u, .height = 6u};

    std::vector<float> selected;
    for (std::uint32_t y = rectangle.originY; y < rectangle.originY + rectangle.height; ++y)
    {
        for (std::uint32_t x = rectangle.originX; x < rectangle.originX + rectangle.width; ++x)
        {
            selected.push_back(samples[static_cast<std::size_t>(y) * kStride + x]);
        }
    }
    const std::vector<double> sorted = sortedOracle(selected);

    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);
    const std::array<double, 3u> quantiles{0.0, 0.25, 1.0};
    std::array<double, 3u> values{};
    const an::Result<an::PlaneOrderStatistics> ranked = an::selectPlaneOrderStatisticsOver(
        execution, source, quantiles, values, an::PlaneOrderRequest{.medianAbsoluteDeviation = true},
        an::KernelValidityView{}, rectangle);
    ASSERT_TRUE(ranked.hasValue()) << ranked.status().message();
    EXPECT_EQ(ranked->run.selection, (an::ValidityCount{.valid = selected.size(), .total = selected.size()}));
    EXPECT_DOUBLE_EQ(ranked->minimum, sorted.front());
    EXPECT_DOUBLE_EQ(ranked->maximum, sorted.back());
    EXPECT_DOUBLE_EQ(ranked->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
    for (std::size_t index = 0u; index < quantiles.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(values[index], oracleQuantile(sorted, quantiles[index], an::QuantileInterpolation::Linear));
    }
    std::vector<double> deviations;
    deviations.reserve(sorted.size());
    for (const double value : sorted)
    {
        deviations.push_back(std::abs(value - ranked->median));
    }
    std::sort(deviations.begin(), deviations.end());
    EXPECT_DOUBLE_EQ(ranked->medianAbsoluteDeviation,
                     oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear));
    // Working memory is bounded by the digit and gather contracts rather than by the rectangle;
    // for a rectangle this small that ceiling is larger than the samples, which is the point.
    EXPECT_LE(ranked->peakScratchBytes, an::kMaxHistogramPartialBytes);
}

TEST(HistogramOrderPrimitivesTest, SelectionOverAValueIntervalRanksOnlyWhatTheIntervalRetains)
{
    constexpr std::uint32_t kWidth = 257u;
    const std::vector<float> samples = orderSamples(kWidth);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    an::KernelPlaneSelection windowed{.originX = 0u, .originY = 0u, .width = kWidth, .height = 1u};
    windowed.valueWindow = an::KernelValueWindow{.lowest = -0.25, .highest = 0.5};

    std::vector<float> retained;
    for (const float sample : samples)
    {
        if (sample >= -0.25f && sample <= 0.5f)
        {
            retained.push_back(sample);
        }
    }
    ASSERT_FALSE(retained.empty());
    ASSERT_LT(retained.size(), samples.size());
    const std::vector<double> sorted = sortedOracle(retained);

    an::EngineContext engine = orderEngine(3u);
    an::ExecutionContext execution = orderExecution(engine, 3u);
    const an::Result<an::PlaneOrderStatistics> ranked =
        an::selectPlaneOrderStatisticsOver(execution, source, std::span<const double>{}, std::span<double>{},
                                           an::PlaneOrderRequest{}, an::KernelValidityView{}, windowed);
    ASSERT_TRUE(ranked.hasValue()) << ranked.status().message();
    EXPECT_EQ(ranked->run.selection, (an::ValidityCount{.valid = retained.size(), .total = kWidth}));
    EXPECT_DOUBLE_EQ(ranked->minimum, sorted.front());
    EXPECT_DOUBLE_EQ(ranked->maximum, sorted.back());
    EXPECT_DOUBLE_EQ(ranked->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
}

TEST(HistogramOrderPrimitivesTest, SelectionRefusesARectangleOrIntervalItsContractExcludes)
{
    constexpr std::uint32_t kWidth = 8u;
    const std::vector<float> samples(kWidth, 1.0f);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);

    const an::Result<an::PlaneOrderStatistics> empty = an::selectPlaneOrderStatisticsOver(
        execution, source, std::span<const double>{}, std::span<double>{}, an::PlaneOrderRequest{},
        an::KernelValidityView{}, an::KernelPlaneSelection{.originX = 2u, .originY = 0u, .width = 0u, .height = 1u});
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);

    const an::Result<an::PlaneOrderStatistics> outside = an::selectPlaneOrderStatisticsOver(
        execution, source, std::span<const double>{}, std::span<double>{}, an::PlaneOrderRequest{},
        an::KernelValidityView{}, an::KernelPlaneSelection{.originX = 4u, .originY = 0u, .width = 6u, .height = 1u});
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::InvalidArgument);

    an::KernelPlaneSelection inverted{.originX = 0u, .originY = 0u, .width = kWidth, .height = 1u};
    inverted.valueWindow = an::KernelValueWindow{.lowest = 1.0, .highest = 0.0};
    const an::Result<an::PlaneOrderStatistics> refused =
        an::selectPlaneOrderStatisticsOver(execution, source, std::span<const double>{}, std::span<double>{},
                                           an::PlaneOrderRequest{}, an::KernelValidityView{}, inverted);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);

    an::KernelPlaneSelection nothing{.originX = 0u, .originY = 0u, .width = kWidth, .height = 1u};
    nothing.valueWindow = an::KernelValueWindow{.lowest = 100.0, .highest = 200.0};
    const an::Result<an::PlaneOrderStatistics> none =
        an::selectPlaneOrderStatisticsOver(execution, source, std::span<const double>{}, std::span<double>{},
                                           an::PlaneOrderRequest{}, an::KernelValidityView{}, nothing);
    ASSERT_FALSE(none.hasValue());
    EXPECT_EQ(none.status().code(), an::ErrorCode::EmptySelection);
}

TEST(HistogramOrderPrimitivesTest, CompactionCountsWithoutWritingAndThenGathersInSourceOrder)
{
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 4u;
    constexpr std::uint32_t kStride = 13u;
    std::vector<float> samples(static_cast<std::size_t>(kStride) * kHeight, -7.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            samples[static_cast<std::size_t>(y) * kStride + x] = static_cast<float>(y * 10u + x);
        }
    }
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kStride);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    const an::KernelPlaneSelection rectangle{.originX = 1u, .originY = 1u, .width = 5u, .height = 3u};

    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);
    const an::Result<an::PlaneCompactionResult> counted =
        an::compactSelectedPlaneSamples(execution, source, an::KernelValidityView{}, rectangle, std::span<double>{});
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->run.selection, (an::ValidityCount{.valid = 15u, .total = 15u}));
    EXPECT_EQ(counted->run.outputsWritten, 0u);
    EXPECT_DOUBLE_EQ(counted->minimum, 11.0);
    EXPECT_DOUBLE_EQ(counted->maximum, 35.0);

    std::vector<double> gathered(20u, -1.0);
    const an::Result<an::PlaneCompactionResult> compacted =
        an::compactSelectedPlaneSamples(execution, source, an::KernelValidityView{}, rectangle, gathered);
    ASSERT_TRUE(compacted.hasValue()) << compacted.status().message();
    EXPECT_EQ(compacted->run.outputsWritten, 15u);
    std::size_t index = 0u;
    for (std::uint32_t y = 1u; y < 4u; ++y)
    {
        for (std::uint32_t x = 1u; x < 6u; ++x)
        {
            EXPECT_DOUBLE_EQ(gathered[index], static_cast<double>(y * 10u + x)) << "at " << index;
            ++index;
        }
    }
    // Nothing past the selected count is touched, so a caller sizing its own buffer keeps whatever
    // it put in the tail.
    EXPECT_DOUBLE_EQ(gathered[15], -1.0);
}

TEST(HistogramOrderPrimitivesTest, CompactionRefusesAShortOrAliasingDestination)
{
    constexpr std::uint32_t kWidth = 6u;
    const std::vector<double> samples(kWidth, 2.0);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float64, kWidth, 1u, kWidth);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float64,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    const an::KernelPlaneSelection whole{.originX = 0u, .originY = 0u, .width = kWidth, .height = 1u};
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);

    std::vector<double> tooShort(kWidth - 1u, 0.0);
    const an::Result<an::PlaneCompactionResult> refused =
        an::compactSelectedPlaneSamples(execution, source, an::KernelValidityView{}, whole, tooShort);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    const an::StatusContextField* required = refused.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* available = refused.status().findContext(an::ContextKey::Available);
    ASSERT_NE(required, nullptr);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(required->count, kWidth);
    EXPECT_EQ(available->count, kWidth - 1u);

    std::vector<double> aliasing(samples);
    const an::KernelPlaneSource aliasingSource{.sampleType = an::SampleType::Float64,
                                               .samples = aliasing.data(),
                                               .layout = &layout,
                                               .plane = 0u,
                                               .sampleInPixel = 0u};
    const an::Result<an::PlaneCompactionResult> overlapping =
        an::compactSelectedPlaneSamples(execution, aliasingSource, an::KernelValidityView{}, whole, aliasing);
    ASSERT_FALSE(overlapping.hasValue());
    EXPECT_EQ(overlapping.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(HistogramOrderPrimitivesTest, HistogramOverARectangleAndIntervalCountsOnlyWhatBothAdmit)
{
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 6u;
    constexpr std::uint32_t kStride = 19u;
    std::vector<float> samples(static_cast<std::size_t>(kStride) * kHeight, -3.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            samples[static_cast<std::size_t>(y) * kStride + x] = static_cast<float>((y * kWidth + x) % 8u) / 8.0f;
        }
    }
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kStride);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    const an::KernelPlaneSelection rectangle{.originX = 3u, .originY = 1u, .width = 9u, .height = 4u};

    an::EngineContext engine = orderEngine(4u);
    an::ExecutionContext execution = orderExecution(engine, 4u);
    // Filled with a sentinel rather than zeroed, because the bins are replaced and not accumulated
    // into: a merge that added to whatever the caller left there would pass on a zeroed array.
    std::array<std::uint64_t, 8u> bins{};
    bins.fill(97u);
    const an::Result<an::PlaneHistogramResult> counted = an::accumulatePlaneHistogramOver(
        execution, source, bins, an::PlaneHistogramRequest{.lowest = 0.0, .highest = 1.0}, an::KernelValidityView{},
        rectangle);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->run.selection, (an::ValidityCount{.valid = 36u, .total = 36u}));

    std::array<std::uint64_t, 8u> expected{};
    for (std::uint32_t y = 1u; y < 5u; ++y)
    {
        for (std::uint32_t x = 3u; x < 12u; ++x)
        {
            const double value = static_cast<double>(samples[static_cast<std::size_t>(y) * kStride + x]);
            ++expected[static_cast<std::size_t>(value * 8.0)];
        }
    }
    EXPECT_EQ(bins, expected);

    // The value interval narrows which samples participate; the histogram interval still decides
    // which bin they land in, and the two are separate numbers.
    an::KernelPlaneSelection narrowed = rectangle;
    narrowed.valueWindow = an::KernelValueWindow{.lowest = 0.25, .highest = 0.5};
    std::array<std::uint64_t, 8u> windowed{};
    windowed.fill(97u);
    const an::Result<an::PlaneHistogramResult> retained = an::accumulatePlaneHistogramOver(
        execution, source, windowed, an::PlaneHistogramRequest{.lowest = 0.0, .highest = 1.0}, an::KernelValidityView{},
        narrowed);
    ASSERT_TRUE(retained.hasValue()) << retained.status().message();
    EXPECT_EQ(retained->run.selection.valid, expected[2] + expected[3] + expected[4]);
    EXPECT_EQ(retained->run.selection.total, 36u);
    for (std::size_t bin = 0u; bin < windowed.size(); ++bin)
    {
        const bool retainedBin = bin >= 2u && bin <= 4u;
        EXPECT_EQ(windowed[bin], retainedBin ? expected[bin] : std::uint64_t{0}) << "bin " << bin;
    }
}

TEST(HistogramOrderPrimitivesTest, HistogramRefusesARectangleItsContractExcludes)
{
    constexpr std::uint32_t kWidth = 8u;
    const std::vector<float> samples(kWidth, 0.5f);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, 1u, kWidth);
    const an::KernelPlaneSource source{.sampleType = an::SampleType::Float32,
                                       .samples = samples.data(),
                                       .layout = &layout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
    an::EngineContext engine = orderEngine(2u);
    an::ExecutionContext execution = orderExecution(engine, 2u);
    std::array<std::uint64_t, 4u> bins{9u, 9u, 9u, 9u};

    const an::Result<an::PlaneHistogramResult> empty = an::accumulatePlaneHistogramOver(
        execution, source, bins, an::PlaneHistogramRequest{}, an::KernelValidityView{},
        an::KernelPlaneSelection{.originX = 2u, .originY = 0u, .width = 0u, .height = 1u});
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);

    const an::Result<an::PlaneHistogramResult> outside = an::accumulatePlaneHistogramOver(
        execution, source, bins, an::PlaneHistogramRequest{}, an::KernelValidityView{},
        an::KernelPlaneSelection{.originX = 5u, .originY = 0u, .width = 5u, .height = 1u});
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::InvalidArgument);

    // A refused call leaves the caller's counters as they were, whichever check refused it.
    EXPECT_EQ(bins, (std::array<std::uint64_t, 4u>{9u, 9u, 9u, 9u}));
}

TEST(HistogramOrderPrimitivesTest, SelectionKeepsExactRanksAtRoundedBinBoundariesAndExtremeScales)
{
    constexpr std::uint32_t width = 8192u;
    const auto layout = singlePlaneLayout(an::SampleType::Float64, width, 1u, width);
    auto engine = orderEngine(16u);
    const std::array<double, 7u> quantiles{0.125, 0.25, 0.499, 0.5, 0.501, 0.75, 0.875};
    for (unsigned domain = 0u; domain < 5u; ++domain)
    {
        std::vector<double> samples(width);
        for (std::uint32_t index = 0u; index < width; ++index)
        {
            const std::uint32_t position = (index * 337u) % 2048u;
            const double fraction = static_cast<double>(position) / 2048.0;
            double value = fraction * 2.0 - 0.75;
            if (domain == 1u)
                value = 1.0 + position * std::numeric_limits<double>::epsilon();
            else if (domain == 2u)
                value = (static_cast<double>(position) - 1024.0) * std::numeric_limits<double>::denorm_min();
            else if (domain == 3u)
                value = (fraction - 0.5) * 1.0e308;
            else if (domain == 4u)
                value = (fraction - 0.5) * 2.0 * (std::numeric_limits<double>::max)();
            samples[index] = std::nextafter(value, index % 2u == 0u ? 0.0 : 1.0);
        }
        if (domain == 4u)
        {
            samples.front() = -(std::numeric_limits<double>::max)();
            samples.back() = (std::numeric_limits<double>::max)();
        }
        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        const an::KernelPlaneSource source{
            .sampleType = an::SampleType::Float64, .samples = samples.data(), .layout = &layout};
        for (const auto concurrency : {1u, 4u, 16u})
        {
            auto execution = orderExecution(engine, concurrency);
            for (const auto interpolation : {an::QuantileInterpolation::LowerRank,
                                             an::QuantileInterpolation::NearestRank, an::QuantileInterpolation::Linear})
            {
                SCOPED_TRACE(::testing::Message() << "domain " << domain << ", workers " << concurrency
                                                  << ", interpolation " << static_cast<int>(interpolation));
                std::array<double, quantiles.size()> values{};
                const auto selected = an::selectPlaneOrderStatistics(
                    execution, source, quantiles, values,
                    an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true},
                    an::KernelValidityView{});
                ASSERT_TRUE(selected.hasValue()) << selected.status().message();
                EXPECT_EQ(selected->run.selection.valid, samples.size());
                EXPECT_EQ(selected->median, oracleQuantile(sorted, 0.5, interpolation));
                for (std::size_t index = 0u; index < quantiles.size(); ++index)
                    EXPECT_EQ(values[index], oracleQuantile(sorted, quantiles[index], interpolation));
                std::vector<double> deviations;
                for (const auto sample : samples)
                    deviations.push_back(std::abs(sample - selected->median));
                std::sort(deviations.begin(), deviations.end());
                EXPECT_EQ(selected->medianAbsoluteDeviation, oracleQuantile(deviations, 0.5, interpolation));
                EXPECT_LE(selected->peakScratchBytes, an::kMaxHistogramPartialBytes);
            }
        }
    }
}

TEST(HistogramOrderPrimitivesTest, DenseInteriorRanksRefineWithoutRepeatedDigitDescents)
{
    constexpr std::uint32_t width = 1024u;
    constexpr std::uint32_t height = 1024u;
    auto samples = denseOrderSamples(static_cast<std::size_t>(width) * height);
    for (auto& value : samples)
        value = 0.53127f + value * 0.001f;
    samples.front() = 0.0f;
    samples.back() = 1.0f;
    const auto sorted = sortedOracle(samples);
    const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto selected = selectFrom(execution, samples, layout, {}, {}, true);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
    std::vector<double> deviations;
    for (const auto sample : samples)
        deviations.push_back(std::abs(static_cast<double>(sample) - selected->median));
    std::sort(deviations.begin(), deviations.end());
    EXPECT_EQ(selected->medianAbsoluteDeviation, oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear));
    EXPECT_LE(selected->planeScans, 7u);
    EXPECT_LE(selected->peakScratchBytes, an::kMaxHistogramPartialBytes);
}

TEST(HistogramOrderPrimitivesTest, LargeContiguousRanksUseTwoScansWithinTheCombinedScratchCeiling)
{
    // A median and its deviation over a large plane every sample of which is selected are counted together in one
    // read and gathered together in another, at every width.
    auto engine = orderEngine(16u);
    constexpr std::uint32_t width = 4096u;
    for (const auto height : {1024u, 4096u})
    {
        const std::uint32_t count = width * height;
        std::vector<float> samples(count);
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            // Multiplication by an odd number permutes a power-of-two range. Every integer
            // is exactly representable as Float32. Moving its two endpoints farther away
            // makes the histogram dense without changing either central rank or central MAD.
            const std::uint32_t value = (index * 337u) & (count - 1u);
            samples[index] = value == 0u           ? -static_cast<float>(count)
                             : value == count - 1u ? 2.0f * static_cast<float>(count) + 64.0f
                                                   : static_cast<float>(value);
        }
        const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
        for (const auto concurrency : {1u, 4u, 16u})
        {
            SCOPED_TRACE(::testing::Message() << "samples " << count << ", workers " << concurrency);
            auto execution = orderExecution(engine, concurrency);
            const auto selected = selectFrom(execution, samples, layout, {}, {}, true);
            ASSERT_TRUE(selected.hasValue()) << selected.status().message();
            EXPECT_EQ(selected->median, static_cast<double>(count - 1u) * 0.5);
            EXPECT_EQ(selected->medianAbsoluteDeviation, static_cast<double>(count) * 0.25);
            EXPECT_EQ(selected->run.selection.valid, count);
            EXPECT_EQ(selected->planeScans, 2u);
            EXPECT_LE(selected->peakScratchBytes, an::kMaxHistogramPartialBytes);
            EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        }
    }
}

TEST(HistogramOrderPrimitivesTest, ASkyBetweenFarOutliersRanksItsMedianAndDeviationInOneCountAndOneGather)
{
    // A calibrated light: a narrow sky, a few hot samples far above it and a few cold ones far below.
    // Bins spread over the whole range put the sky in a handful of them, so the rank's bin needs a
    // second count and every increment of the first lands on the same few counters. Brackets around
    // the median and around where its deviation's rank lies on either side of it are counted in one
    // read and their bins gathered in another. Rows padded past the image must cost no more than
    // tightly packed ones. An odd width leaves every chunk and row a few samples past a whole number of
    // the blocks a scan tests together.
    constexpr std::uint32_t width = 2051u;
    constexpr std::uint32_t height = 1536u;
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    const auto uniform = [&state] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) * 0x1.0p-53;
    };
    std::vector<float> packed(static_cast<std::size_t>(width) * height);
    for (float& value : packed)
    {
        const double kind = uniform();
        if (kind < 0.01)
            value = static_cast<float>(0.5 + 14.5 * uniform());
        else if (kind < 0.015)
            value = static_cast<float>(-5.0 * uniform());
        else
            value = static_cast<float>(0.0273 + 0.004 * (uniform() + uniform() + uniform() + uniform() - 2.0));
    }
    const auto sorted = sortedOracle(packed);
    const double median = oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear);
    std::vector<double> deviations;
    for (const double value : sorted)
        deviations.push_back(std::abs(value - median));
    std::sort(deviations.begin(), deviations.end());
    const double deviation = oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear);

    auto engine = orderEngine(16u);
    for (const std::uint32_t stride : {width, width + 13u})
    {
        std::vector<float> samples(static_cast<std::size_t>(stride) * height, -1000.0f);
        for (std::uint32_t y = 0u; y < height; ++y)
            std::copy_n(packed.begin() + static_cast<std::ptrdiff_t>(y) * width, width,
                        samples.begin() + static_cast<std::ptrdiff_t>(y) * stride);
        const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, stride);
        std::uint64_t scans = 0u;
        for (const auto concurrency : {1u, 4u, 16u})
        {
            SCOPED_TRACE(::testing::Message() << "row stride " << stride << ", workers " << concurrency);
            auto execution = orderExecution(engine, concurrency);
            const auto selected = selectFrom(execution, samples, layout, {}, {}, true);
            ASSERT_TRUE(selected.hasValue()) << selected.status().message();
            EXPECT_EQ(selected->median, median);
            EXPECT_EQ(selected->medianAbsoluteDeviation, deviation);
            EXPECT_EQ(selected->minimum, sorted.front());
            EXPECT_EQ(selected->maximum, sorted.back());
            EXPECT_EQ(selected->planeScans, 2u) << "one count of every bracket, then one gather of every bin";
            if (scans != 0u)
                EXPECT_EQ(selected->planeScans, scans) << "the cost depends on the width it ran at";
            scans = selected->planeScans;
            EXPECT_LE(selected->peakScratchBytes, an::kMaxHistogramPartialBytes);
            EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        }
    }
}

TEST(HistogramOrderPrimitivesTest, ARankItsSampledBracketMissesIsStillExact)
{
    // The selection reads a bracket around a rank from 4096 pixels whose positions step through the
    // plane by the golden ratio's fraction of it. A plane holding outliers at exactly those places
    // puts the bracket nowhere near the plane's own median and deviation - below both ranks, then above
    // them - so each falls outside it and the selection has to narrow over the plane's range instead.
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 1100u;
    constexpr std::uint64_t probes = 4096u;
    for (const float outlier : {0.125f, 0.875f})
    {
        SCOPED_TRACE(::testing::Message() << "outliers at " << outlier);
        std::vector<float> samples(static_cast<std::size_t>(width) * height);
        for (std::size_t index = 0u; index < samples.size(); ++index)
            samples[index] = 0.5f + 0.001f * static_cast<float>((index * 337u) % 1009u) / 1009.0f;
        for (std::uint64_t probe = 0u; probe < probes; ++probe)
        {
            const double step = 0.5 + static_cast<double>(probe) * 0.6180339887498949;
            const auto position =
                (std::min)(static_cast<std::size_t>((step - std::floor(step)) * static_cast<double>(samples.size())),
                           samples.size() - 1u);
            samples[position] = outlier + 0.0625f * static_cast<float>(probe) / static_cast<float>(probes);
        }
        const auto sorted = sortedOracle(samples);
        const double median = oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear);
        std::vector<double> deviations;
        for (const double value : sorted)
            deviations.push_back(std::abs(value - median));
        std::sort(deviations.begin(), deviations.end());
        const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
        auto engine = orderEngine(4u);
        auto execution = orderExecution(engine, 4u);
        const auto selected = selectFrom(execution, samples, layout, {}, {}, true);
        ASSERT_TRUE(selected.hasValue()) << selected.status().message();
        EXPECT_EQ(selected->median, median);
        EXPECT_EQ(selected->medianAbsoluteDeviation,
                  oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear));
        EXPECT_GT(selected->planeScans, 5u) << "the sampled bracket held both ranks after all";
    }
}

TEST(HistogramOrderPrimitivesTest, AColumnPatternDoesNotMoveTheSampledBracket)
{
    // Pixels taken at equal spacing through a plane whose width divides that spacing all lie in the
    // same few columns. Two columns that differ from the rest of a power-of-two-wide frame, as a
    // gradient or a bad column does, must not decide where the selection looks for its ranks.
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 2048u;
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const std::size_t x = index % width;
        const float column = (x == 512u || x == 1536u) ? 0.25f : 0.0f;
        samples[index] = 0.5f + column + 0.01f * static_cast<float>((index * 337u) % 1009u) / 1009.0f;
    }
    const auto sorted = sortedOracle(samples);
    const double median = oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear);
    std::vector<double> deviations;
    for (const double value : sorted)
        deviations.push_back(std::abs(value - median));
    std::sort(deviations.begin(), deviations.end());
    const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto selected = selectFrom(execution, samples, layout, {}, {}, true);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->median, median);
    EXPECT_EQ(selected->medianAbsoluteDeviation, oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear));
    EXPECT_LE(selected->planeScans, 5u) << "the sampled bracket missed a rank";
}

TEST(HistogramOrderPrimitivesTest, AMedianJustAboveAPlateauAtTheBracketsFirstValueIsExact)
{
    // Nearly half of a clamped frame can lie on one value, and a sampled bracket then begins on that
    // value while the median lies a few samples above it, in the bracket's first bin with the whole
    // plateau. Every sample of the plateau is inside the bracket and counted with that bin.
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 1024u;
    constexpr std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<float> samples(count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const std::size_t position = (index * 337u) % count;
        if (position < count / 2u - 64u)
            samples[index] = 0.25f;
        else if (position < count / 2u + 64u)
            samples[index] = std::nextafter(0.25f, 1.0f) + 1.0e-7f * static_cast<float>(position - (count / 2u - 64u));
        else
            samples[index] = 0.5f + 0.25f * static_cast<float>(position - count / 2u) / static_cast<float>(count);
    }
    const auto sorted = sortedOracle(samples);
    const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto selected = selectFrom(execution, samples, layout, {}, {}, false);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_GT(sorted[count / 2u - 1u], 0.25);
    EXPECT_EQ(selected->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
}

TEST(HistogramOrderPrimitivesTest, AMedianBetweenTwoSeparatedPopulationsTakesItsSuccessorFromTheSameGather)
{
    // An even count's median interpolates between the largest sample of the lower population and the
    // smallest of the upper one. Whatever bin holds the first, the second lies in another, and the
    // read that gathered the first has already seen it.
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 1024u;
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    const std::size_t half = samples.size() / 2u;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const std::size_t position = (index * 337u) % samples.size();
        const float within = 0.1f * static_cast<float>(position % half) / static_cast<float>(half);
        samples[index] = position < half ? 0.1f + within : 0.6f + within;
    }
    const auto sorted = sortedOracle(samples);
    const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto selected = selectFrom(execution, samples, layout, {}, {}, false);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
    EXPECT_LT(sorted[sorted.size() / 2u - 1u], 0.5);
    EXPECT_GT(sorted[sorted.size() / 2u], 0.5);
    EXPECT_LE(selected->planeScans, 3u) << "the successor was resolved by a further descent";

    // A mask sends the same samples through the walk that tests every pixel's bit, which keeps its own successor.
    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(width / 8u) * height);
    const auto maskExtent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    const auto writableMask = an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    an::ValidityDescriptorRequest validityRequest;
    validityRequest.rule = an::ValidityRule::ExplicitMask;
    const auto masked = an::selectPlaneOrderStatistics(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float32,
                              .samples = samples.data(),
                              .layout = &layout,
                              .plane = 0u,
                              .sampleInPixel = 0u},
        {}, {}, an::PlaneOrderRequest{},
        an::KernelValidityView{.descriptor = *an::ValidityDescriptor::create(validityRequest), .mask = &mask});
    ASSERT_TRUE(masked.hasValue()) << masked.status().message();
    EXPECT_EQ(masked->median, selected->median);
    EXPECT_LE(masked->planeScans, 3u) << "the masked successor was resolved by a further descent";
}

TEST(HistogramOrderPrimitivesTest, LargeSelectionRefusalReturnsAllScratchAndLeavesQuantilesUntouched)
{
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 1024u;
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.5f);
    const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
    auto engine = orderEngine(4u, 1024u * 1024u);
    auto execution = orderExecution(engine, 4u);
    const std::array<double, 1u> quantiles{0.25};
    std::array<double, 1u> values{-123.0};
    const auto selected = selectFrom(execution, samples, layout, quantiles, values, true);
    ASSERT_FALSE(selected.hasValue());
    EXPECT_EQ(selected.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(values.front(), -123.0);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(engine.memoryResource()->counters().liveAllocations, 0u);
}

TEST(HistogramOrderPrimitivesTest, QuantizedPopulationsResolveRepeatedRanksWithoutDigitDescents)
{
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 1024u;
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0u; index < samples.size(); ++index)
        samples[index] = static_cast<float>(16384u + ((index * 337u) % 63u) * 16u) / 65535.0f;
    const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
    const an::KernelPlaneSource source{
        .sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout};
    auto engine = orderEngine(16u);
    for (bool clipped : {false, true})
    {
        an::KernelPlaneSelection window{.width = width, .height = height};
        // Choose bounds by level rather than by the permutation's source position.
        if (clipped)
            window.valueWindow = an::KernelValueWindow{.lowest = 16512.0f / 65535.0f, .highest = 17248.0f / 65535.0f};
        std::vector<double> sorted;
        for (float sample : samples)
            if (!clipped || (sample >= window.valueWindow->lowest && sample <= window.valueWindow->highest))
                sorted.push_back(sample);
        std::sort(sorted.begin(), sorted.end());
        const double median = oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear);
        std::vector<double> deviations;
        for (double sample : sorted)
            deviations.push_back(std::abs(sample - median));
        std::sort(deviations.begin(), deviations.end());
        for (auto concurrency : {1u, 16u})
        {
            auto execution = orderExecution(engine, concurrency);
            const auto result = an::selectPlaneOrderStatisticsOver(
                execution, source, {}, {}, an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, {}, window);
            ASSERT_TRUE(result.hasValue()) << result.status().message();
            EXPECT_EQ(result->median, median);
            EXPECT_EQ(result->medianAbsoluteDeviation,
                      oracleQuantile(deviations, 0.5, an::QuantileInterpolation::Linear));
            EXPECT_EQ(result->run.selection.valid, sorted.size());
            EXPECT_LE(result->planeScans, 5u);
            EXPECT_LE(result->peakScratchBytes, an::kMaxHistogramPartialBytes);
            EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        }
    }
}

TEST(HistogramOrderPrimitivesTest, RepeatedValueDifferentialCampaignKeepsAdjacentRanksAndSelectionExact)
{
    constexpr std::uint32_t width = 32768u;
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto layout = singlePlaneLayout(an::SampleType::Float64, width, 1u, width);
    std::uint64_t state = 0xa92d75c615f327b1ull;
    const auto random = [&]() {
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        return state;
    };
    const std::array<double, 5u> quantiles{0.25, 0.499, 0.5, 0.501, 0.75};
    for (unsigned trial = 0u; trial < 96u; ++trial)
    {
        SCOPED_TRACE(trial);
        const unsigned levels = 2u + trial % 7u;
        std::vector<double> samples(width);
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            const auto level = trial % 4u == 0u ? index % levels : random() % levels;
            samples[index] =
                trial % 3u == 0u ? static_cast<double>(level) * 1.0e-290 : static_cast<double>(level) * 0.125 - 0.5;
        }
        if (trial % 3u == 1u)
        {
            samples.front() = -1.0e100;
            samples.back() = 1.0e100;
        }
        an::KernelPlaneSelection window{.width = width, .height = 1u};
        if (trial % 2u != 0u)
            window.valueWindow = an::KernelValueWindow{.lowest = -0.5, .highest = 1.0e-289};
        std::vector<double> sorted;
        for (double sample : samples)
            if (!window.valueWindow || (sample >= window.valueWindow->lowest && sample <= window.valueWindow->highest))
                sorted.push_back(sample);
        std::sort(sorted.begin(), sorted.end());
        const an::KernelPlaneSource source{
            .sampleType = an::SampleType::Float64, .samples = samples.data(), .layout = &layout};
        std::array<double, quantiles.size()> values{};
        const auto result = an::selectPlaneOrderStatisticsOver(
            execution, source, quantiles, values, an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, {}, window);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
        for (std::size_t index = 0u; index < quantiles.size(); ++index)
            EXPECT_EQ(values[index], oracleQuantile(sorted, quantiles[index], an::QuantileInterpolation::Linear));
        for (double& sample : sorted)
            sample = std::abs(sample - result->median);
        std::sort(sorted.begin(), sorted.end());
        EXPECT_EQ(result->medianAbsoluteDeviation, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
    }
}

TEST(HistogramOrderPrimitivesTest, MaskedRectangleRanksExactlyWhatTheSamePackedSamplesRank)
{
    // A masked rectangle is read where it lies, one row run and one mask byte at a time, while the same selected
    // values packed into one contiguous row are read by the contiguous scans. Both have to rank exactly what
    // sorting ranks, bit for bit, at one worker and several. The populations put repeats, extremes and values on
    // bin boundaries among masked and admitted samples alike, and the pixels nothing selects hold values a scan
    // must never see: non-finite ones, and extremes that would widen the interval if they were counted.
    constexpr std::uint32_t kWidth = 157u;
    constexpr std::uint32_t kHeight = 41u;
    constexpr std::uint32_t kStride = 163u;
    const an::KernelPlaneSelection rectangle{.originX = 5u, .originY = 3u, .width = 139u, .height = 35u};
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kStride);
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    an::ValidityDescriptorRequest validityRequest;
    validityRequest.rule = an::ValidityRule::ExplicitMask;
    const an::ValidityDescriptor validity = *an::ValidityDescriptor::create(validityRequest);
    std::uint64_t state = 0x5851f42d4c957f2dull;
    const auto random = [&]() {
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        return state;
    };
    // Ranks near both ends land in the first and last bins of a pass, where its interval's endpoints are mapped.
    const std::array<double, 9u> quantiles{0.001, 0.02, 0.1, 0.25, 0.5, 0.75, 0.9, 0.98, 0.999};
    const std::array<an::QuantileInterpolation, 3u> rules{an::QuantileInterpolation::Linear,
                                                          an::QuantileInterpolation::LowerRank,
                                                          an::QuantileInterpolation::NearestRank};
    for (unsigned trial = 0u; trial < 24u; ++trial)
    {
        SCOPED_TRACE(trial);
        std::vector<float> samples(static_cast<std::size_t>(kStride) * kHeight,
                                   std::numeric_limits<float>::quiet_NaN());
        for (float& sample : samples)
        {
            const auto bits = static_cast<std::uint32_t>(random() >> 40u);
            if (trial % 3u == 0u)
                sample = static_cast<float>(bits % 129u) / 64.0f - 1.0f;
            else if (trial % 3u == 1u)
                sample = static_cast<float>(bits) / 8388608.0f - 1.0f;
            else
                sample = static_cast<float>((bits % 4096u) + ((bits >> 12u) % 4096u)) / 4096.0f - 1.0f;
        }
        std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(an::packedMaskRowBytes(kWidth) * kHeight),
                                            std::uint8_t{0});
        const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
            an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
        ASSERT_TRUE(writableMask.has_value());
        std::vector<float> selected;
        std::vector<std::size_t> admittedAt;
        std::uint64_t ordinal = 0u;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                float& sample = samples[static_cast<std::size_t>(y) * kStride + x];
                const bool inside = x >= rectangle.originX && x < rectangle.originX + rectangle.width &&
                                    y >= rectangle.originY && y < rectangle.originY + rectangle.height;
                bool admitted = false;
                if (inside)
                {
                    const std::uint64_t position = ordinal++;
                    admitted = trial % 2u == 0u ? position % 17u != 0u : random() % 3u != 0u;
                    // Whole excluded bytes and whole admitted ones, beside the partial bytes.
                    if (trial % 4u == 1u && x % 16u < 8u && y % 5u == 0u)
                        admitted = false;
                }
                ASSERT_TRUE(writableMask->setValid(0u, x, y, admitted));
                if (admitted)
                {
                    selected.push_back(sample);
                    admittedAt.push_back(static_cast<std::size_t>(y) * kStride + x);
                }
                else if (inside && (x + y) % 11u == 0u)
                    sample = (x + y) % 2u == 0u ? std::numeric_limits<float>::infinity() : -3.0e38f;
            }
        }
        // The selection's own extremes sit on admitted pixels in the first and last rows the rectangle reads.
        if (trial % 3u == 2u && !selected.empty())
        {
            selected.front() = 2.0f;
            samples[admittedAt.front()] = 2.0f;
            selected.back() = -2.0f;
            samples[admittedAt.back()] = -2.0f;
        }
        const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
        const std::vector<double> sorted = sortedOracle(selected);
        std::vector<double> deviationsFromOracle;
        const an::QuantileInterpolation interpolation = rules[trial % rules.size()];
        const double oracleMedian = oracleQuantile(sorted, 0.5, interpolation);
        for (const double value : sorted)
            deviationsFromOracle.push_back(std::abs(value - oracleMedian));
        std::sort(deviationsFromOracle.begin(), deviationsFromOracle.end());

        const an::ImageLayout packedLayout =
            singlePlaneLayout(an::SampleType::Float32, static_cast<std::uint32_t>(selected.size()), 1u,
                              static_cast<std::uint32_t>(selected.size()));
        const an::KernelPlaneSource masked{
            .sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout};
        const an::KernelPlaneSource packed{
            .sampleType = an::SampleType::Float32, .samples = selected.data(), .layout = &packedLayout};
        const an::PlaneOrderRequest request{.interpolation = interpolation, .medianAbsoluteDeviation = true};
        for (const std::uint32_t workers : {1u, 4u})
        {
            SCOPED_TRACE(workers);
            an::EngineContext engine = orderEngine(workers);
            an::ExecutionContext execution = orderExecution(engine, workers);
            std::array<double, quantiles.size()> maskedValues{};
            std::array<double, quantiles.size()> packedValues{};
            const an::Result<an::PlaneOrderStatistics> fromMask = an::selectPlaneOrderStatisticsOver(
                execution, masked, quantiles, maskedValues, request,
                an::KernelValidityView{.descriptor = validity, .mask = &mask}, rectangle);
            const an::Result<an::PlaneOrderStatistics> fromPacked =
                an::selectPlaneOrderStatistics(execution, packed, quantiles, packedValues, request, {});
            ASSERT_TRUE(fromMask.hasValue()) << fromMask.status().message();
            ASSERT_TRUE(fromPacked.hasValue()) << fromPacked.status().message();
            EXPECT_EQ(fromMask->run.selection.valid, selected.size());
            EXPECT_EQ(fromMask->minimum, sorted.front());
            EXPECT_EQ(fromMask->maximum, sorted.back());
            EXPECT_EQ(fromMask->median, oracleMedian);
            EXPECT_EQ(fromMask->medianAbsoluteDeviation, oracleQuantile(deviationsFromOracle, 0.5, interpolation));
            for (std::size_t index = 0u; index < quantiles.size(); ++index)
            {
                EXPECT_EQ(maskedValues[index], oracleQuantile(sorted, quantiles[index], interpolation)) << index;
                EXPECT_EQ(std::bit_cast<std::uint64_t>(maskedValues[index]),
                          std::bit_cast<std::uint64_t>(packedValues[index]))
                    << index;
            }
            EXPECT_EQ(std::bit_cast<std::uint64_t>(fromMask->median), std::bit_cast<std::uint64_t>(fromPacked->median));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(fromMask->medianAbsoluteDeviation),
                      std::bit_cast<std::uint64_t>(fromPacked->medianAbsoluteDeviation));
        }
    }
}

TEST(HistogramOrderPrimitivesTest, ExactNormalizedGridVerifiesUnprobedValuesAndEveryInterpolation)
{
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 1024u;
    const auto layout = singlePlaneLayout(an::SampleType::Float32, width, height, width);
    auto engine = orderEngine(16u);
    auto execution = orderExecution(engine, 16u);
    const std::array<double, 5u> quantiles{0.0, 0.1245, 0.5, 0.876, 1.0};
    for (unsigned trial = 0u; trial < 8u; ++trial)
    {
        SCOPED_TRACE(trial);
        std::vector<float> samples(static_cast<std::size_t>(width) * height);
        for (std::size_t index = 0u; index < samples.size(); ++index)
            samples[index] = static_cast<float>(8192u + ((index * 337u) % 127u) * 4u) / 65535.0f;
        // These positions are absent from the evenly spaced admission probes. A probe is
        // only a performance hint; overlooking either value would produce an incorrect rank.
        if (trial == 1u || trial == 3u)
            samples[12345u] = std::nextafter(0.5f, 1.0f);
        if (trial == 2u)
            samples[12345u] = 1.0f / 65535.0f;
        if (trial == 4u)
            samples[12345u] = std::numeric_limits<float>::quiet_NaN();
        an::KernelPlaneSelection window{.width = width, .height = height};
        if (trial == 5u)
            samples[12345u] = -0.0f;
        if (trial == 6u)
            samples[12345u] = 0.0f;
        if (trial == 7u)
            samples.front() = -0.0f;
        if (trial == 3u || trial == 4u)
            window.valueWindow = an::KernelValueWindow{.lowest = 0.1, .highest = 0.2};
        std::vector<double> sorted;
        for (float value : samples)
            if (!window.valueWindow || (value >= window.valueWindow->lowest && value <= window.valueWindow->highest))
                sorted.push_back(value);
        std::sort(sorted.begin(), sorted.end());
        const an::KernelPlaneSource source{
            .sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout};
        for (auto interpolation : {an::QuantileInterpolation::LowerRank, an::QuantileInterpolation::NearestRank,
                                   an::QuantileInterpolation::Linear})
        {
            std::array<double, quantiles.size()> values{};
            const auto result = an::selectPlaneOrderStatisticsOver(
                execution, source, quantiles, values,
                an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true}, {}, window);
            ASSERT_TRUE(result.hasValue()) << result.status().message();
            EXPECT_EQ(result->run.selection.valid, sorted.size());
            EXPECT_EQ(result->median, oracleQuantile(sorted, 0.5, interpolation));
            for (std::size_t index = 0u; index < quantiles.size(); ++index)
                EXPECT_EQ(values[index], oracleQuantile(sorted, quantiles[index], interpolation));
            std::vector<double> deviations;
            for (double value : sorted)
                deviations.push_back(std::abs(value - result->median));
            std::sort(deviations.begin(), deviations.end());
            EXPECT_EQ(result->medianAbsoluteDeviation, oracleQuantile(deviations, 0.5, interpolation));
            if (trial >= 5u)
                EXPECT_EQ(std::signbit(values.front()), trial != 6u);
            if (trial == 0u || trial == 3u || trial == 4u || trial == 6u)
                EXPECT_EQ(result->planeScans, 2u);
            else
                EXPECT_GT(result->planeScans, 2u);
            EXPECT_LE(result->peakScratchBytes, an::kMaxHistogramPartialBytes);
        }
    }
}

namespace
{

/// A deterministic frame of integer codes shaped like a sky: a background a few hundredths of the representation
/// wide, stars and hot pixels anywhere above and below it, and both ends of the representation - black and
/// saturated - present, so ranks near either end of the order and codes repeated many times over are all exercised.
template <typename SampleT> [[nodiscard]] std::vector<SampleT> integerSky(std::size_t count, std::uint64_t seed)
{
    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
    const double span = highest - lowest;
    std::uint64_t state = seed;
    const auto next = [&state] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(static_cast<std::uint32_t>(state >> 32u)) / 4294967296.0;
    };
    std::vector<SampleT> samples(count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double draw = next();
        double value = lowest;
        if (draw < 0.002)
            value = lowest;
        else if (draw < 0.004)
            value = highest;
        else if (draw < 0.03)
            value = lowest + span * next();
        else
            value = lowest + 0.08 * span + (next() + next() + next() + next() - 2.0) * 0.01 * span;
        samples[index] = static_cast<SampleT>((std::min)((std::max)(std::round(value), lowest), highest));
    }
    return samples;
}

/// The ascending values a selection of integer samples orders: those `admitted` keeps, each read as `held` reads it.
template <typename SampleT, typename AdmitT, typename HoldT>
[[nodiscard]] std::vector<double> sortedSelection(const std::vector<SampleT>& samples, AdmitT&& admitted, HoldT&& held)
{
    std::vector<double> values;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        if (admitted(index, static_cast<double>(samples[index])))
            values.push_back(held(static_cast<double>(samples[index])));
    }
    std::sort(values.begin(), values.end());
    return values;
}

/// The ascending absolute deviations of sorted values from a centre.
[[nodiscard]] std::vector<double> sortedDeviations(const std::vector<double>& sorted, double centre)
{
    std::vector<double> deviations;
    deviations.reserve(sorted.size());
    for (const double value : sorted)
        deviations.push_back(std::abs(value - centre));
    std::sort(deviations.begin(), deviations.end());
    return deviations;
}

/// Every quantile a code-table case asks for: both ends, ranks near each end and the interior.
constexpr std::array<double, 9u> kCodeTableQuantiles{0.0, 0.001, 0.02, 0.1245, 0.5, 0.876, 0.98, 0.999, 1.0};

/// Checks one selection of an eight- or sixteen-bit plane against its sorted oracle under every interpolation rule.
template <typename SelectT>
void expectExactAgainst(const std::vector<double>& sorted, SelectT&& select, std::uint64_t expectedScans,
                        const char* label)
{
    ASSERT_FALSE(sorted.empty()) << label;
    for (const auto interpolation : {an::QuantileInterpolation::LowerRank, an::QuantileInterpolation::NearestRank,
                                     an::QuantileInterpolation::Linear})
    {
        std::array<double, kCodeTableQuantiles.size()> values{};
        const an::Result<an::PlaneOrderStatistics> result = select(interpolation, std::span<double>{values});
        ASSERT_TRUE(result.hasValue()) << label << ": " << result.status().message();
        EXPECT_EQ(result->run.selection.valid, sorted.size()) << label;
        EXPECT_EQ(result->minimum, sorted.front()) << label;
        EXPECT_EQ(result->maximum, sorted.back()) << label;
        EXPECT_EQ(result->median, oracleQuantile(sorted, 0.5, interpolation)) << label;
        for (std::size_t index = 0u; index < values.size(); ++index)
            EXPECT_EQ(values[index], oracleQuantile(sorted, kCodeTableQuantiles[index], interpolation))
                << label << " at " << kCodeTableQuantiles[index];
        EXPECT_EQ(result->medianAbsoluteDeviation,
                  oracleQuantile(sortedDeviations(sorted, result->median), 0.5, interpolation))
            << label;
        EXPECT_EQ(result->planeScans, expectedScans) << label;
        EXPECT_LE(result->peakScratchBytes, an::kMaxHistogramPartialBytes) << label;
    }
}

/// One component of a frame whose pixels interleave `components` samples.
[[nodiscard]] an::ImageLayout interleavedLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                                std::uint32_t components)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, components, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = an::sampleTypeSizeBytes(type),
                                                       .samplesPerPixel = components,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// The whole-plane selection of every representation with a code table, checked against its oracle.
template <typename SampleT> void expectWholePlaneRanksFromOneRead(std::uint64_t seed)
{
    constexpr std::uint32_t kWidth = 613u;
    constexpr std::uint32_t kHeight = 271u;
    const std::vector<SampleT> samples = integerSky<SampleT>(static_cast<std::size_t>(kWidth) * kHeight, seed);
    const an::ImageLayout layout = singlePlaneLayout(an::kSampleTypeOf<SampleT>, kWidth, kHeight, kWidth);
    const std::vector<double> sorted =
        sortedSelection(samples, [](std::size_t, double) { return true; }, [](double value) { return value; });
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    expectExactAgainst(
        sorted,
        [&](an::QuantileInterpolation interpolation, std::span<double> values) {
            return an::selectPlaneOrderStatistics(
                execution,
                an::KernelPlaneSource{
                    .sampleType = an::kSampleTypeOf<SampleT>, .samples = samples.data(), .layout = &layout},
                kCodeTableQuantiles, values,
                an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true},
                an::KernelValidityView{});
        },
        1u, an::sampleTypeName(an::kSampleTypeOf<SampleT>));
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

} // namespace

TEST(HistogramOrderPrimitivesTest, EightAndSixteenBitPlanesRankExactlyFromOneReadOfACodeTable)
{
    // An eight- or sixteen-bit plane holds at most 256 or 65,536 distinct values, so a table of how often each code
    // occurs answers every rank exactly: the extremes, every quantile under every interpolation rule, the median and
    // its absolute deviation, each the value an ascending sort places at the rank, from a single read of the plane.
    expectWholePlaneRanksFromOneRead<std::uint16_t>(0x9e3779b97f4a7c15ull);
    expectWholePlaneRanksFromOneRead<std::int16_t>(0x243f6a8885a308d3ull);
    expectWholePlaneRanksFromOneRead<std::uint8_t>(0x5851f42d4c957f2dull);
    expectWholePlaneRanksFromOneRead<std::int8_t>(0x14057b7ef767814full);
}

TEST(HistogramOrderPrimitivesTest, ACodeTableReadsEveryRuleWindowHoldMaskAndRectangleAsTheSamplesAre)
{
    // The rule, the value window and the hold decide on the stored value alone, so a code table applies them once per
    // code; a mask decides pixel by pixel and is read as the table is counted. Each selection orders exactly the
    // samples, held exactly as, the general selection does - for a signed representation too, whose codes are
    // counted from its smallest value - and still reads the plane once.
    constexpr std::uint32_t kWidth = 640u;
    constexpr std::uint32_t kHeight = 300u;
    const std::vector<std::uint16_t> samples =
        integerSky<std::uint16_t>(static_cast<std::size_t>(kWidth) * kHeight, 0xd1b54a32d192ed03ull);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::UInt16, kWidth, kHeight, kWidth);
    const an::KernelPlaneSource source{
        .sampleType = an::SampleType::UInt16, .samples = samples.data(), .layout = &layout};
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto identity = [](double value) { return value; };
    const auto ruleOf = [](an::ValidityDescriptorRequest request) { return *an::ValidityDescriptor::create(request); };

    // Zero is no data.
    {
        const an::KernelValidityView validity{
            .descriptor = ruleOf(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData})};
        const auto sorted = sortedSelection(samples, [](std::size_t, double value) { return value != 0.0; }, identity);
        expectExactAgainst(
            sorted,
            [&](an::QuantileInterpolation interpolation, std::span<double> values) {
                return an::selectPlaneOrderStatistics(
                    execution, source, kCodeTableQuantiles, values,
                    an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true}, validity);
            },
            1u, "zero is no data");
    }
    // The interval strictly inside the representation, as a statistics window leaves the clipped samples out.
    {
        const an::KernelValidityView validity{
            .descriptor = ruleOf(an::ValidityDescriptorRequest{
                .rule = an::ValidityRule::DomainRange, .lowestValid = 0.5, .highestValid = 65534.5})};
        const auto sorted = sortedSelection(
            samples, [](std::size_t, double value) { return value > 0.0 && value < 65535.0; }, identity);
        expectExactAgainst(
            sorted,
            [&](an::QuantileInterpolation interpolation, std::span<double> values) {
                return an::selectPlaneOrderStatistics(
                    execution, source, kCodeTableQuantiles, values,
                    an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true}, validity);
            },
            1u, "the unclipped range");
    }
    // A rectangle and a value window around the sky, read through a hold whose ends are not codes.
    {
        constexpr std::uint32_t kOriginX = 37u;
        constexpr std::uint32_t kOriginY = 11u;
        constexpr std::uint32_t kRegionWidth = 401u;
        constexpr std::uint32_t kRegionHeight = 203u;
        const an::KernelValueWindow window{.lowest = 4500.0, .highest = 6000.0};
        const an::KernelValueWindow hold{.lowest = 5100.25, .highest = 5300.75};
        const auto inside = [&](std::size_t index, double value) {
            const std::size_t x = index % kWidth;
            const std::size_t y = index / kWidth;
            return x >= kOriginX && x < kOriginX + kRegionWidth && y >= kOriginY && y < kOriginY + kRegionHeight &&
                   value >= window.lowest && value <= window.highest;
        };
        const auto heldTo = [&](double value) { return (std::min)((std::max)(value, hold.lowest), hold.highest); };
        an::KernelPlaneSelection region{
            .originX = kOriginX, .originY = kOriginY, .width = kRegionWidth, .height = kRegionHeight};
        region.valueWindow = window;
        expectExactAgainst(
            sortedSelection(samples, inside, identity),
            [&](an::QuantileInterpolation interpolation, std::span<double> values) {
                return an::selectPlaneOrderStatisticsOver(
                    execution, source, kCodeTableQuantiles, values,
                    an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true}, {}, region);
            },
            1u, "a rectangle and a window");
        expectExactAgainst(
            sortedSelection(samples, inside, heldTo),
            [&](an::QuantileInterpolation interpolation, std::span<double> values) {
                return an::selectPlaneOrderStatisticsOver(execution, source, kCodeTableQuantiles, values,
                                                          an::PlaneOrderRequest{.interpolation = interpolation,
                                                                                .medianAbsoluteDeviation = true,
                                                                                .hold = hold},
                                                          {}, region);
            },
            1u, "a rectangle, a window and a hold");
    }
    // A packed mask clearing every seventh pixel and a block of rows.
    {
        std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(kWidth / 8u) * kHeight);
        const auto maskExtent = an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
        ASSERT_TRUE(maskExtent.has_value());
        const auto writableMask = an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
        ASSERT_TRUE(writableMask.has_value());
        const auto kept = [&](std::size_t index) {
            const std::size_t y = index / kWidth;
            return index % 7u != 3u && (y < 40u || y >= 90u);
        };
        for (std::uint32_t y = 0u; y < kHeight; ++y)
            for (std::uint32_t x = 0u; x < kWidth; ++x)
                ASSERT_TRUE(writableMask->setValid(0u, x, y, kept(static_cast<std::size_t>(y) * kWidth + x)));
        const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
        const an::KernelValidityView validity{
            .descriptor = ruleOf(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask}), .mask = &mask};
        expectExactAgainst(
            sortedSelection(
                samples, [&](std::size_t index, double) { return kept(index); }, identity),
            [&](an::QuantileInterpolation interpolation, std::span<double> values) {
                return an::selectPlaneOrderStatistics(
                    execution, source, kCodeTableQuantiles, values,
                    an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true}, validity);
            },
            1u, "a packed mask");
    }
    // One component of interleaved signed pixels.
    {
        constexpr std::uint32_t kComponents = 3u;
        const std::vector<std::int16_t> interleaved =
            integerSky<std::int16_t>(static_cast<std::size_t>(kWidth) * kHeight * kComponents, 0x2545f4914f6cdd1dull);
        const an::ImageLayout pixels = interleavedLayout(an::SampleType::Int16, kWidth, kHeight, kComponents);
        std::vector<std::int16_t> component;
        for (std::size_t index = 1u; index < interleaved.size(); index += kComponents)
            component.push_back(interleaved[index]);
        expectExactAgainst(
            sortedSelection(
                component, [](std::size_t, double) { return true; }, identity),
            [&](an::QuantileInterpolation interpolation, std::span<double> values) {
                return an::selectPlaneOrderStatistics(
                    execution,
                    an::KernelPlaneSource{.sampleType = an::SampleType::Int16,
                                          .samples = interleaved.data(),
                                          .layout = &pixels,
                                          .plane = 0u,
                                          .sampleInPixel = 1u},
                    kCodeTableQuantiles, values,
                    an::PlaneOrderRequest{.interpolation = interpolation, .medianAbsoluteDeviation = true},
                    an::KernelValidityView{});
            },
            1u, "an interleaved component");
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(HistogramOrderPrimitivesTest, ACodeTableRanksTheSameAtEveryWidth)
{
    // Every count is exact and the partition follows the plane alone, so the answer, the partition and the cost are
    // the same whichever number of workers counted it.
    constexpr std::uint32_t kWidth = 1031u;
    constexpr std::uint32_t kHeight = 517u;
    const std::vector<std::uint16_t> samples =
        integerSky<std::uint16_t>(static_cast<std::size_t>(kWidth) * kHeight, 0x6a09e667f3bcc909ull);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::UInt16, kWidth, kHeight, kWidth);
    std::optional<std::vector<std::uint64_t>> baseline;
    for (const std::uint32_t workers : {1u, 3u, 8u, 17u})
    {
        auto engine = orderEngine(workers);
        auto execution = orderExecution(engine, workers);
        std::array<double, kCodeTableQuantiles.size()> values{};
        const auto result = an::selectPlaneOrderStatistics(
            execution,
            an::KernelPlaneSource{.sampleType = an::SampleType::UInt16, .samples = samples.data(), .layout = &layout},
            kCodeTableQuantiles, values, an::PlaneOrderRequest{.medianAbsoluteDeviation = true},
            an::KernelValidityView{});
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        std::vector<std::uint64_t> bits{std::bit_cast<std::uint64_t>(result->median),
                                        std::bit_cast<std::uint64_t>(result->medianAbsoluteDeviation),
                                        std::bit_cast<std::uint64_t>(result->minimum),
                                        std::bit_cast<std::uint64_t>(result->maximum),
                                        result->run.selection.valid,
                                        result->deterministicChunks,
                                        result->planeScans,
                                        result->peakScratchBytes};
        for (const double value : values)
            bits.push_back(std::bit_cast<std::uint64_t>(value));
        if (baseline)
            EXPECT_EQ(bits, *baseline) << workers << " workers";
        else
            baseline = bits;
        EXPECT_EQ(result->planeScans, 1u);
        EXPECT_LE(result->plannedWorkers, workers);
    }
}

TEST(HistogramOrderPrimitivesTest, ACodeTableThatAdmitsNothingIsAnEmptySelectionAndReturnsItsScratch)
{
    constexpr std::uint32_t kWidth = 512u;
    constexpr std::uint32_t kHeight = 256u;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < samples.size(); ++index)
        samples[index] = static_cast<std::uint16_t>(1000u + index % 3000u);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::UInt16, kWidth, kHeight, kWidth);
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    an::KernelPlaneSelection region{.originX = 0u, .originY = 0u, .width = kWidth, .height = kHeight};
    region.valueWindow = an::KernelValueWindow{.lowest = 61000.0, .highest = 62000.0};
    std::array<double, 1u> values{-7.0};
    const auto result = an::selectPlaneOrderStatisticsOver(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::UInt16, .samples = samples.data(), .layout = &layout},
        std::array<double, 1u>{0.25}, values, an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, {}, region);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.status().code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(values.front(), -7.0) << "a refused selection writes no quantile";
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(HistogramOrderPrimitivesTest, APlaneSmallerThanItsCodeTableKeepsTheGeneralSelection)
{
    // Clearing and walking 65,536 counters costs more than narrowing a few thousand samples, so a plane with fewer
    // samples than its representation has codes is ranked by the general selection - exactly as well.
    constexpr std::uint32_t kWidth = 211u;
    constexpr std::uint32_t kHeight = 97u;
    const std::vector<std::uint16_t> samples =
        integerSky<std::uint16_t>(static_cast<std::size_t>(kWidth) * kHeight, 0xbb67ae8584caa73bull);
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::UInt16, kWidth, kHeight, kWidth);
    const std::vector<double> sorted =
        sortedSelection(samples, [](std::size_t, double) { return true; }, [](double value) { return value; });
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto result = an::selectPlaneOrderStatistics(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::UInt16, .samples = samples.data(), .layout = &layout}, {},
        {}, an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, an::KernelValidityView{});
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->median, oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear));
    EXPECT_EQ(result->medianAbsoluteDeviation,
              oracleQuantile(sortedDeviations(sorted, result->median), 0.5, an::QuantileInterpolation::Linear));
    EXPECT_GT(result->planeScans, 1u);
}

namespace
{

/// The counts of one histogram, and what fell outside its bins.
struct CountedHistogram
{
    std::vector<std::uint64_t> bins;
    an::ValidityCount selection{};
    std::uint64_t below = 0u;
    std::uint64_t above = 0u;
    std::uint64_t notANumber = 0u;
};

/// Counts one plane's histogram over a rectangle and a value window, under a rule and an optional mask.
[[nodiscard]] CountedHistogram countHistogram(const an::ExecutionContext& execution,
                                              const an::KernelPlaneSource& source, std::size_t binCount,
                                              const an::PlaneHistogramRequest& request,
                                              const an::KernelValidityView& validity,
                                              const an::KernelPlaneSelection& selection)
{
    CountedHistogram counted;
    counted.bins.assign(binCount, 7u);
    const an::Result<an::PlaneHistogramResult> result =
        an::accumulatePlaneHistogramOver(execution, source, counted.bins, request, validity, selection);
    EXPECT_TRUE(result.hasValue()) << result.status().message();
    if (result.hasValue())
    {
        EXPECT_LE(result->partialBytes, an::kMaxHistogramPartialBytes);
        counted.selection = result->run.selection;
        counted.below = result->belowRange;
        counted.above = result->aboveRange;
        counted.notANumber = result->notANumber;
    }
    return counted;
}

/// Checks that an integer plane's histograms count as the same values held in Float64 do, under every rule, window,
/// mask and rectangle, for a histogram over the representation, one over an interval inside it and one over the
/// interval a display histogram asks for.
template <typename SampleT> void expectHistogramsCountAsFloat64Does(std::uint64_t seed)
{
    constexpr std::uint32_t kWidth = 640u;
    constexpr std::uint32_t kHeight = 300u;
    const std::vector<SampleT> samples = integerSky<SampleT>(static_cast<std::size_t>(kWidth) * kHeight, seed);
    const std::vector<double> widened(samples.begin(), samples.end());
    const an::ImageLayout integerLayout = singlePlaneLayout(an::kSampleTypeOf<SampleT>, kWidth, kHeight, kWidth);
    const an::ImageLayout floatLayout = singlePlaneLayout(an::SampleType::Float64, kWidth, kHeight, kWidth);
    const an::KernelPlaneSource integerSource{
        .sampleType = an::kSampleTypeOf<SampleT>, .samples = samples.data(), .layout = &integerLayout};
    const an::KernelPlaneSource floatSource{
        .sampleType = an::SampleType::Float64, .samples = widened.data(), .layout = &floatLayout};
    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
    const double span = highest - lowest;
    auto engine = orderEngine(4u);
    auto execution = orderExecution(engine, 4u);
    const auto ruleOf = [](an::ValidityDescriptorRequest request) { return *an::ValidityDescriptor::create(request); };

    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(kWidth / 8u) * kHeight);
    const auto maskExtent = an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    const auto writableMask = an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
        for (std::uint32_t x = 0u; x < kWidth; ++x)
            ASSERT_TRUE(writableMask->setValid(0u, x, y, (x + 3u * y) % 7u != 3u && (y < 40u || y >= 90u)));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();

    const std::vector<std::pair<const char*, an::KernelValidityView>> rules{
        {"every sample", an::KernelValidityView{}},
        {"zero is no data", an::KernelValidityView{.descriptor = ruleOf(an::ValidityDescriptorRequest{
                                                       .rule = an::ValidityRule::ZeroIsNoData})}},
        {"the unclipped range",
         an::KernelValidityView{
             .descriptor = ruleOf(an::ValidityDescriptorRequest{
                 .rule = an::ValidityRule::DomainRange, .lowestValid = lowest + 0.5, .highestValid = highest - 0.5})}},
        {"a packed mask", an::KernelValidityView{.descriptor = ruleOf(an::ValidityDescriptorRequest{
                                                     .rule = an::ValidityRule::ExplicitMask}),
                                                 .mask = &mask}}};
    an::KernelPlaneSelection whole{.originX = 0u, .originY = 0u, .width = kWidth, .height = kHeight};
    an::KernelPlaneSelection region{.originX = 37u, .originY = 11u, .width = 401u, .height = 203u};
    region.valueWindow = an::KernelValueWindow{.lowest = lowest + 0.05 * span, .highest = lowest + 0.12 * span};
    // The representation's own interval in 256 bins, an interval inside it that leaves samples on both sides, and the
    // 256 bins a display histogram lays over the representation and a bin's width past its top.
    const std::vector<std::pair<std::size_t, an::PlaneHistogramRequest>> requests{
        {256u, an::PlaneHistogramRequest{.lowest = lowest, .highest = highest}},
        {100u, an::PlaneHistogramRequest{.lowest = lowest + 0.07 * span, .highest = lowest + 0.09 * span}},
        {256u, an::PlaneHistogramRequest{.lowest = lowest, .highest = lowest + span * 256.0 / 255.0}}};
    for (const auto& [ruleName, validity] : rules)
    {
        for (const an::KernelPlaneSelection& selection : {whole, region})
        {
            for (const auto& [binCount, request] : requests)
            {
                const CountedHistogram integer =
                    countHistogram(execution, integerSource, binCount, request, validity, selection);
                const CountedHistogram floating =
                    countHistogram(execution, floatSource, binCount, request, validity, selection);
                const std::string label = std::string(an::sampleTypeName(an::kSampleTypeOf<SampleT>)) + ", " +
                                          ruleName + (selection.valueWindow.has_value() ? ", a region" : "") + ", " +
                                          std::to_string(binCount) + " bins from " + std::to_string(request.lowest);
                EXPECT_EQ(integer.bins, floating.bins) << label;
                EXPECT_EQ(integer.selection, floating.selection) << label;
                EXPECT_EQ(integer.below, floating.below) << label;
                EXPECT_EQ(integer.above, floating.above) << label;
                EXPECT_EQ(integer.notANumber, floating.notANumber) << label;
            }
        }
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

} // namespace

TEST(HistogramOrderPrimitivesTest, AnEightOrSixteenBitHistogramCountsAsTheSameValuesHeldInFloat64Do)
{
    // An eight- or sixteen-bit plane is counted per code and each code classified once; a Float64 plane holding the
    // same values is classified sample by sample. Every bin, every sample below, above or outside the selection, is
    // the same under every rule, mask, rectangle and value window.
    expectHistogramsCountAsFloat64Does<std::uint16_t>(0xbb67ae8584caa73bull);
    expectHistogramsCountAsFloat64Does<std::int16_t>(0x3c6ef372fe94f82bull);
    expectHistogramsCountAsFloat64Does<std::uint8_t>(0xa54ff53a5f1d36f1ull);
    expectHistogramsCountAsFloat64Does<std::int8_t>(0x510e527fade682d1ull);
}

namespace
{

/// The shapes a large plane's values are drawn from: continuous - distinct values almost everywhere - and mostly inside
/// the unit interval, where a display holds them.
enum class LargeShape : std::uint8_t
{
    /// A narrow noisy background, stars above it and cold samples below it.
    Sky,

    /// A uniform spread past both ends of the unit interval.
    Dense,

    /// A background with a long exponential tail above it.
    Skewed,

    /// A Cauchy spread whose tails reach far past both ends.
    Heavy,

    /// Two separated populations, so that the median lies between them.
    Bimodal
};

/// One value of a shape, from a running state.
[[nodiscard]] double drawLarge(LargeShape shape, std::uint64_t& state)
{
    const auto uniform = [&state] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) * 0x1.0p-53;
    };
    const auto spread = [&uniform] { return uniform() + uniform() + uniform() + uniform() - 2.0; };
    switch (shape)
    {
    case LargeShape::Sky: {
        const double kind = uniform();
        if (kind < 0.01)
            return 0.05 + 0.95 * uniform();
        if (kind < 0.015)
            return -0.01 * uniform();
        return 0.03 + 0.0035 * spread();
    }
    case LargeShape::Dense:
        return uniform() * 1.5 - 0.25;
    case LargeShape::Skewed:
        return 0.1 - 0.05 * std::log(1.0 - uniform());
    case LargeShape::Heavy:
        return 0.5 + 0.01 * std::tan(3.141592653589793 * (uniform() - 0.5));
    case LargeShape::Bimodal:
        return (uniform() < 0.5 ? 0.2 : 0.6) + 0.01 * spread();
    }
    return 0.0;
}

/// A value of the unit interval's scale as a representation stores it: as it is in floating point, and spread over
/// most of an integer representation's codes, so that integer samples stay as distinct as floating ones.
template <typename SampleT> [[nodiscard]] SampleT storedLarge(double value)
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<SampleT>(value);
    }
    else
    {
        const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
        const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
        const double scaled = lowest + (value + 0.5) * 0.5 * (highest - lowest);
        return static_cast<SampleT>((std::min)((std::max)(std::round(scaled), lowest), highest));
    }
}

/// How one large case reads its plane: a value window and a hold, in the unit interval's scale, and whether samples
/// that are not finite are planted and the finite-only rule leaves them out.
struct LargeReading
{
    const char* name = "";
    std::optional<an::KernelValueWindow> window{};
    std::optional<an::KernelValueWindow> hold{};
    bool finiteOnly = false;
};

/// One large case: a shape, how it is read, the rule a quantile between two ranks takes, and the plane's geometry.
struct LargeCase
{
    LargeShape shape = LargeShape::Sky;
    LargeReading reading{};
    an::QuantileInterpolation interpolation = an::QuantileInterpolation::Linear;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t stride = 0u;
};

/// A plane of a large case, its rows `stride` samples apart with the padding past each row holding values the
/// selection must never read, and the ascending values a selection of it orders.
template <typename SampleT> struct LargePlane
{
    std::vector<SampleT> samples;
    an::ImageLayout layout;
    std::vector<double> sorted;
    std::optional<an::KernelValueWindow> window;
    std::optional<an::KernelValueWindow> hold;
    an::ValidityDescriptor rule;
};

template <typename SampleT> [[nodiscard]] LargePlane<SampleT> largePlane(const LargeCase& large, std::uint64_t seed)
{
    const auto stored = [](double value) { return static_cast<double>(storedLarge<SampleT>(value)); };
    LargePlane<SampleT> plane{
        .samples = std::vector<SampleT>(static_cast<std::size_t>(large.stride) * large.height),
        .layout = singlePlaneLayout(an::kSampleTypeOf<SampleT>, large.width, large.height, large.stride),
        .sorted = {},
        .window = std::nullopt,
        .hold = std::nullopt,
        .rule = an::ValidityDescriptor{}};
    if (large.reading.window.has_value())
        plane.window = an::KernelValueWindow{.lowest = stored(large.reading.window->lowest),
                                             .highest = stored(large.reading.window->highest)};
    if (large.reading.hold.has_value())
        plane.hold = an::KernelValueWindow{.lowest = stored(large.reading.hold->lowest),
                                           .highest = stored(large.reading.hold->highest)};
    const bool planted = large.reading.finiteOnly && std::is_floating_point_v<SampleT>;
    if (planted)
    {
        an::ValidityDescriptorRequest finiteRequest;
        finiteRequest.rule = an::ValidityRule::FiniteOnly;
        plane.rule = *an::ValidityDescriptor::create(finiteRequest);
    }
    std::uint64_t state = seed;
    for (std::uint32_t y = 0u; y < large.height; ++y)
    {
        for (std::uint32_t x = 0u; x < large.stride; ++x)
        {
            SampleT& sample = plane.samples[static_cast<std::size_t>(y) * large.stride + x];
            if (x >= large.width)
            {
                // A sample past the row that a selection read would move every statistic.
                sample = storedLarge<SampleT>(x % 2u == 0u ? -1.0e3 : 1.0e3);
                continue;
            }
            sample = storedLarge<SampleT>(drawLarge(large.shape, state));
            const std::size_t index = static_cast<std::size_t>(y) * large.width + x;
            if constexpr (std::is_floating_point_v<SampleT>)
            {
                if (planted && index % 4099u == 17u)
                    sample = std::numeric_limits<SampleT>::quiet_NaN();
                else if (planted && index % 7919u == 3u)
                    sample = std::numeric_limits<SampleT>::infinity();
                else if (planted && index % 6007u == 5u)
                    sample = -std::numeric_limits<SampleT>::infinity();
            }
            const double value = static_cast<double>(sample);
            if (!std::isfinite(value))
                continue;
            if (plane.window.has_value() && !(value >= plane.window->lowest && value <= plane.window->highest))
                continue;
            plane.sorted.push_back(plane.hold.has_value()
                                       ? (std::min)((std::max)(value, plane.hold->lowest), plane.hold->highest)
                                       : value);
        }
    }
    std::sort(plane.sorted.begin(), plane.sorted.end());
    return plane;
}

/// The median and its absolute deviation of one large plane, as one execution selects them.
template <typename SampleT>
[[nodiscard]] an::Result<an::PlaneOrderStatistics> selectLarge(const an::ExecutionContext& execution,
                                                               const LargePlane<SampleT>& plane, const LargeCase& large)
{
    an::KernelPlaneSelection selection{.originX = 0u, .originY = 0u, .width = large.width, .height = large.height};
    selection.valueWindow = plane.window;
    return an::selectPlaneOrderStatisticsOver(execution,
                                              an::KernelPlaneSource{.sampleType = an::kSampleTypeOf<SampleT>,
                                                                    .samples = plane.samples.data(),
                                                                    .layout = &plane.layout,
                                                                    .plane = 0u,
                                                                    .sampleInPixel = 0u},
                                              std::span<const double>{}, std::span<double>{},
                                              an::PlaneOrderRequest{.interpolation = large.interpolation,
                                                                    .medianAbsoluteDeviation = true,
                                                                    .hold = plane.hold},
                                              an::KernelValidityView{.descriptor = plane.rule}, selection);
}

/// Whether two doubles are the same bits.
[[nodiscard]] bool sameBits(double left, double right)
{
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

/// Checks one large case of one representation against its sorted oracle, at one width.
template <typename SampleT>
void expectLargeCase(an::EngineContext& engine, const LargeCase& large, std::uint64_t seed,
                     std::optional<std::uint64_t> expectedScans, const std::string& label)
{
    const LargePlane<SampleT> plane = largePlane<SampleT>(large, seed);
    ASSERT_FALSE(plane.sorted.empty()) << label;
    const double median = oracleQuantile(plane.sorted, 0.5, large.interpolation);
    const double deviation = oracleQuantile(sortedDeviations(plane.sorted, median), 0.5, large.interpolation);
    std::optional<an::PlaneOrderStatistics> first;
    for (const std::uint32_t concurrency : {4u, 1u, 16u})
    {
        SCOPED_TRACE(::testing::Message() << label << ", workers " << concurrency);
        auto execution = orderExecution(engine, concurrency);
        const an::Result<an::PlaneOrderStatistics> selected = selectLarge(execution, plane, large);
        ASSERT_TRUE(selected.hasValue()) << selected.status().message();
        EXPECT_EQ(selected->run.selection.valid, plane.sorted.size());
        EXPECT_EQ(selected->minimum, plane.sorted.front());
        EXPECT_EQ(selected->maximum, plane.sorted.back());
        EXPECT_TRUE(sameBits(selected->median, median)) << selected->median << " against " << median;
        EXPECT_TRUE(sameBits(selected->medianAbsoluteDeviation, deviation))
            << selected->medianAbsoluteDeviation << " against " << deviation;
        if (expectedScans.has_value())
            EXPECT_EQ(selected->planeScans, *expectedScans);
        EXPECT_LE(selected->peakScratchBytes, an::kMaxHistogramPartialBytes);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        if (first.has_value())
        {
            EXPECT_TRUE(sameBits(selected->median, first->median));
            EXPECT_TRUE(sameBits(selected->medianAbsoluteDeviation, first->medianAbsoluteDeviation));
            EXPECT_TRUE(sameBits(selected->minimum, first->minimum));
            EXPECT_TRUE(sameBits(selected->maximum, first->maximum));
            EXPECT_EQ(selected->planeScans, first->planeScans) << "the cost depends on the width it ran at";
        }
        else
        {
            first = *selected;
        }
        if (concurrency == 4u && large.shape != LargeShape::Sky)
            break;
    }
}

/// The large cases every representation is checked over.
[[nodiscard]] std::vector<LargeCase> largeCases()
{
    using an::QuantileInterpolation;
    const LargeReading plain{.name = "plain"};
    const LargeReading held{.name = "held to the unit interval",
                            .hold = an::KernelValueWindow{.lowest = 0.0, .highest = 1.0}};
    const LargeReading windowed{.name = "a window", .window = an::KernelValueWindow{.lowest = -0.005, .highest = 0.5}};
    const LargeReading finite{.name = "finite only", .finiteOnly = true};
    const LargeReading windowedAndHeld{.name = "a window and a hold",
                                       .window = an::KernelValueWindow{.lowest = 0.1, .highest = 0.4},
                                       .hold = an::KernelValueWindow{.lowest = 0.101, .highest = 0.35}};
    // An odd count of pixels in packed rows, and an even count in rows padded past the image.
    constexpr std::uint32_t kOddWidth = 1031u;
    constexpr std::uint32_t kOddHeight = 1021u;
    constexpr std::uint32_t kEvenWidth = 1024u;
    constexpr std::uint32_t kEvenHeight = 1030u;
    return {
        {LargeShape::Sky, plain, QuantileInterpolation::Linear, kOddWidth, kOddHeight, kOddWidth},
        {LargeShape::Sky, held, QuantileInterpolation::Linear, kEvenWidth, kEvenHeight, kEvenWidth + 11u},
        {LargeShape::Sky, windowed, QuantileInterpolation::NearestRank, kOddWidth, kOddHeight, kOddWidth},
        {LargeShape::Sky, finite, QuantileInterpolation::LowerRank, kEvenWidth, kEvenHeight, kEvenWidth},
        {LargeShape::Sky, finite, QuantileInterpolation::Linear, kEvenWidth, kEvenHeight, kEvenWidth + 5u},
        {LargeShape::Dense, plain, QuantileInterpolation::Linear, kEvenWidth, kEvenHeight, kEvenWidth},
        {LargeShape::Dense, held, QuantileInterpolation::NearestRank, kOddWidth, kOddHeight, kOddWidth + 3u},
        {LargeShape::Skewed, plain, QuantileInterpolation::Linear, kOddWidth, kOddHeight, kOddWidth},
        {LargeShape::Skewed, windowedAndHeld, QuantileInterpolation::Linear, kEvenWidth, kEvenHeight, kEvenWidth},
        {LargeShape::Heavy, plain, QuantileInterpolation::Linear, kEvenWidth, kEvenHeight, kEvenWidth},
        {LargeShape::Heavy, held, QuantileInterpolation::LowerRank, kOddWidth, kOddHeight, kOddWidth},
    };
}

/// Every large case over one representation, each its median and deviation exact and read as often as stated.
template <typename SampleT> void expectLargeCases(std::uint64_t seed, std::optional<std::uint64_t> expectedScans)
{
    auto engine = orderEngine(16u);
    const std::vector<LargeCase> cases = largeCases();
    for (std::size_t index = 0u; index < cases.size(); ++index)
    {
        const LargeCase& large = cases[index];
        const std::string label = std::string(an::sampleTypeName(an::kSampleTypeOf<SampleT>)) + ", case " +
                                  std::to_string(index) + ", " + large.reading.name;
        expectLargeCase<SampleT>(engine, large, seed + index, expectedScans, label);
    }
}

} // namespace

TEST(HistogramOrderPrimitivesTest, AMedianAndItsDeviationOverALargeUnmaskedFloat32PlaneAreCountedAndGatheredInTwoReads)
{
    // The median's bracket and the two brackets its absolute deviation's rank lies in - one below the median and one
    // above it, where the median's distance from its own deviation would put them - are read from a sample of the
    // plane and counted together in one read; the bins holding the median and the bins whose samples could be the
    // deviation's rank are gathered together in a second. The answers are exact: every one the value an ascending
    // sort places at its rank, the median, its deviation, the extremes and the count, over a sky with stars and cold
    // samples, a dense spread, a skewed and a heavy-tailed one, held, through a window, with samples that are not
    // finite left out by the finite-only rule, in packed and padded rows, under every interpolation rule, and the same
    // at every width. A plane of another representation without a code table is narrowed as before, to the same
    // answers.
    expectLargeCases<float>(0x9e3779b97f4a7c15ull, 2u);
    expectLargeCases<double>(0x243f6a8885a308d3ull, std::nullopt);
    expectLargeCases<std::int32_t>(0x5851f42d4c957f2dull, std::nullopt);
    expectLargeCases<std::uint32_t>(0x14057b7ef767814full, std::nullopt);
}

TEST(HistogramOrderPrimitivesTest, ATwoReadSelectionCountsAndGathersAsItsBlockLoopsDo)
{
    // The two reads count and gather Float32 samples with the compiled runs of the instruction set they run on, and
    // with their own loops on the reference backend; the median, its deviation, the extremes - a zero of either sign
    // among them - and the cost are the same bits either way, plain, held and through the finite-only rule's window.
    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (const an::InstructionSet candidate : {an::InstructionSet::Avx2, an::InstructionSet::Avx512Skylake})
    {
        if (an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
            targets.push_back(candidate);
    }
    ASSERT_FALSE(targets.empty()) << "this host runs no compiled count to hold to the block loop";
    const LargeReading plain{.name = "plain"};
    const LargeReading held{.name = "held", .hold = an::KernelValueWindow{.lowest = 0.0, .highest = 1.0}};
    const LargeReading finite{.name = "finite only", .finiteOnly = true};
    an::EngineContext engine = orderEngine(4u);
    for (const LargeReading& reading : {plain, held, finite})
    {
        const LargeCase large{.shape = LargeShape::Sky,
                              .reading = reading,
                              .interpolation = an::QuantileInterpolation::Linear,
                              .width = 1031u,
                              .height = 1021u,
                              .stride = 1031u};
        LargePlane<float> plane = largePlane<float>(large, 0x3c6ef372fe94f82bull);
        if (!reading.hold.has_value())
        {
            // The cold samples become zeros of either sign, so the smallest value is a zero. The first zero in the
            // samples' order is a negative one at the second sample, and the first zero of a lane of four or of eight
            // samples a positive one, so an extreme that keeps the first of a lane's equal values rather than the
            // first in the samples' order carries the wrong sign.
            for (float& sample : plane.samples)
            {
                if (sample < 0.0f && std::isfinite(sample))
                    sample = (std::bit_cast<std::uint32_t>(sample) & 4u) != 0u ? -0.0f : 0.0f;
            }
            for (std::size_t index = 0u; index < 9u; ++index)
                plane.samples[index] = index == 1u ? -0.0f : (index == 4u || index == 8u ? 0.0f : 0.03f);
        }
        SCOPED_TRACE(reading.name);
        const auto selectOn = [&](an::ComputeBackend backend, an::InstructionSet instructionSet) {
            an::ExecutionOptions options;
            options.maxConcurrency = 4u;
            options.backend = backend;
            options.instructionSet = instructionSet;
            options.backendFallback = an::BackendFallback::Forbidden;
            an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
            EXPECT_TRUE(execution.hasValue()) << execution.status().message();
            return selectLarge(*execution, plane, large);
        };
        const an::Result<an::PlaneOrderStatistics> expected =
            selectOn(an::ComputeBackend::Reference, an::InstructionSet::Automatic);
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        EXPECT_EQ(expected->planeScans, 2u);
        if (!reading.hold.has_value())
            EXPECT_TRUE(sameBits(expected->minimum, -0.0)) << "the smallest value is the first zero, a negative one";
        for (const an::InstructionSet target : targets)
        {
            SCOPED_TRACE(static_cast<int>(target));
            const an::Result<an::PlaneOrderStatistics> compiled = selectOn(an::ComputeBackend::Cpu, target);
            ASSERT_TRUE(compiled.hasValue()) << compiled.status().message();
            EXPECT_TRUE(sameBits(compiled->median, expected->median));
            EXPECT_TRUE(sameBits(compiled->medianAbsoluteDeviation, expected->medianAbsoluteDeviation));
            EXPECT_TRUE(sameBits(compiled->minimum, expected->minimum));
            EXPECT_TRUE(sameBits(compiled->maximum, expected->maximum));
            EXPECT_EQ(compiled->run.selection.valid, expected->run.selection.valid);
            EXPECT_EQ(compiled->planeScans, expected->planeScans);
        }
    }
}

TEST(HistogramOrderPrimitivesTest, APlaneTheTwoReadsCannotSettleIsRankedAsExactlyByTheNarrowing)
{
    // What two reads cannot settle is handed to the narrowing and comes out as exact: samples on the sixteen-bit grid,
    // whose repeated values the sample of the plane shows, are never given to the two reads; a median in the gap
    // between two populations leaves the brackets no room between them; a plateau holding the median puts more samples
    // in the median's bin than the gather holds, and one holding the deviation's rank more in its candidates, which the
    // first read shows - and the read it took is counted in the cost.
    auto engine = orderEngine(16u);
    const LargeReading plain{.name = "plain"};
    expectLargeCase<float>(engine,
                           LargeCase{.shape = LargeShape::Bimodal,
                                     .reading = plain,
                                     .interpolation = an::QuantileInterpolation::Linear,
                                     .width = 1024u,
                                     .height = 1030u,
                                     .stride = 1024u},
                           0x6a09e667f3bcc908ull, std::nullopt, "a median between two populations");
    expectLargeCase<double>(
        engine,
        LargeCase{.shape = LargeShape::Skewed,
                  .reading = LargeReading{.name = "a hold whose plateau is the deviation's rank",
                                          .window = an::KernelValueWindow{.lowest = 0.1, .highest = 0.4},
                                          .hold = an::KernelValueWindow{.lowest = 0.12, .highest = 0.3}},
                  .interpolation = an::QuantileInterpolation::Linear,
                  .width = 1031u,
                  .height = 1021u,
                  .stride = 1031u},
        0xbb67ae8584caa73bull, std::nullopt, "a plateau at the deviation's rank");

    constexpr std::uint32_t kWidth = 2048u;
    constexpr std::uint32_t kHeight = 1024u;
    constexpr std::size_t kCount = static_cast<std::size_t>(kWidth) * kHeight;
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    auto execution = orderExecution(engine, 16u);
    const auto expectExact = [&](const std::vector<float>& samples, const char* label) {
        SCOPED_TRACE(label);
        const std::vector<double> sorted = sortedOracle(samples);
        const double median = oracleQuantile(sorted, 0.5, an::QuantileInterpolation::Linear);
        const an::Result<an::PlaneOrderStatistics> selected =
            selectFrom(execution, samples, layout, std::span<const double>{}, std::span<double>{}, true);
        EXPECT_TRUE(selected.hasValue()) << selected.status().message();
        if (!selected.hasValue())
            return std::uint64_t{0};
        EXPECT_TRUE(sameBits(selected->median, median));
        EXPECT_TRUE(sameBits(selected->medianAbsoluteDeviation,
                             oracleQuantile(sortedDeviations(sorted, median), 0.5, an::QuantileInterpolation::Linear)));
        EXPECT_EQ(selected->minimum, sorted.front());
        EXPECT_EQ(selected->maximum, sorted.back());
        EXPECT_LE(selected->peakScratchBytes, an::kMaxHistogramPartialBytes);
        return selected->planeScans;
    };

    std::uint64_t state = 0x510e527fade682d1ull;
    const auto uniform = [&state] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) * 0x1.0p-53;
    };
    // Every code of a sixteen-bit sky, held as Float32: the sample of the plane repeats its values, so the selection
    // narrows the plane as it narrows its Float64 copy, which two reads never take, and at the same cost - it does not
    // read the plane twice first, even where, as here, a code's bin would fit the gather.
    std::vector<float> coded(kCount);
    for (float& sample : coded)
        sample = static_cast<float>(std::round((0.03 + 0.0035 * (uniform() + uniform() + uniform() - 1.5)) * 65535.0) /
                                    65535.0);
    const std::uint64_t codedScans = expectExact(coded, "codes on the sixteen-bit grid");
    const std::vector<double> codedCopy(coded.begin(), coded.end());
    const an::ImageLayout copyLayout = singlePlaneLayout(an::SampleType::Float64, kWidth, kHeight, kWidth);
    const an::Result<an::PlaneOrderStatistics> copied = an::selectPlaneOrderStatistics(
        execution,
        an::KernelPlaneSource{.sampleType = an::SampleType::Float64,
                              .samples = codedCopy.data(),
                              .layout = &copyLayout,
                              .plane = 0u,
                              .sampleInPixel = 0u},
        {}, {}, an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, an::KernelValidityView{});
    ASSERT_TRUE(copied.hasValue()) << copied.status().message();
    EXPECT_GT(copied->planeScans, 2u);
    EXPECT_EQ(codedScans, copied->planeScans) << "a plane of codes is narrowed as its Float64 copy is";

    // Three percent of the plane on the value the median takes, the rest continuous on either side of it: the
    // median's bin holds the plateau, more than the gather does, so the first read is followed by the narrowing.
    std::vector<float> plateau(kCount);
    for (std::size_t index = 0u; index < kCount; ++index)
        plateau[index] = index % 33u == 0u ? 0.5f : static_cast<float>(0.5 + 0.02 * (uniform() - 0.5));
    EXPECT_GT(expectExact(plateau, "a plateau at the median"), 2u);
}

TEST(HistogramOrderPrimitivesTest, ATwoReadSelectionRefusesWhatTheNarrowingRefuses)
{
    // A large plane read in two reads refuses what any selection refuses, and returns everything it took: a selected
    // sample that is not finite, a window that admits nothing, and a memory budget its scratch does not fit.
    constexpr std::uint32_t kWidth = 2048u;
    constexpr std::uint32_t kHeight = 1024u;
    std::vector<float> samples = denseOrderSamples(static_cast<std::size_t>(kWidth) * kHeight);
    samples[samples.size() / 3u] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout layout = singlePlaneLayout(an::SampleType::Float32, kWidth, kHeight, kWidth);
    {
        auto engine = orderEngine(4u);
        auto execution = orderExecution(engine, 4u);
        const an::Result<an::PlaneOrderStatistics> refused =
            selectFrom(execution, samples, layout, std::span<const double>{}, std::span<double>{}, true);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);

        an::KernelPlaneSelection nothing{.originX = 0u, .originY = 0u, .width = kWidth, .height = kHeight};
        nothing.valueWindow = an::KernelValueWindow{.lowest = 7.0, .highest = 8.0};
        const an::Result<an::PlaneOrderStatistics> empty = an::selectPlaneOrderStatisticsOver(
            execution,
            an::KernelPlaneSource{.sampleType = an::SampleType::Float32, .samples = samples.data(), .layout = &layout},
            {}, {}, an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, {}, nothing);
        ASSERT_FALSE(empty.hasValue());
        EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    }
    samples[samples.size() / 3u] = 0.5f;
    auto engine = orderEngine(4u, 1024u * 1024u);
    auto execution = orderExecution(engine, 4u);
    const an::Result<an::PlaneOrderStatistics> refused =
        selectFrom(execution, samples, layout, std::span<const double>{}, std::span<double>{}, true);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(engine.memoryResource()->counters().liveAllocations, 0u);
}
