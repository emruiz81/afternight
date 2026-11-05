// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the median and order filters, and for the two implementations of them.

#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// The median of a window taken by sorting, which is the definition the two implementations answer.
///
/// @param samples the plane, row-major and tightly packed.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param x the centre column.
/// @param y the centre row.
/// @param radius half the square window.
/// @return the middle sample of the clamped window.
[[nodiscard]] float clampedWindowMedian(const std::vector<float>& samples, std::int64_t width, std::int64_t height,
                                        std::int64_t x, std::int64_t y, std::int64_t radius)
{
    std::vector<float> window;
    for (std::int64_t row = y - radius; row <= y + radius; ++row)
    {
        for (std::int64_t column = x - radius; column <= x + radius; ++column)
        {
            const std::int64_t clampedRow = (std::min)((std::max)(row, std::int64_t{0}), height - 1);
            const std::int64_t clampedColumn = (std::min)((std::max)(column, std::int64_t{0}), width - 1);
            window.push_back(samples[static_cast<std::size_t>(clampedRow * width + clampedColumn)]);
        }
    }
    std::sort(window.begin(), window.end());
    return window[window.size() / 2u];
}

TEST(OrderFilterTest, TheMethodNamesAndMembershipAreThePublishedOnes)
{
    EXPECT_STREQ(an::orderFilterMethodName(an::OrderFilterMethod::Automatic), "Automatic");
    EXPECT_STREQ(an::orderFilterMethodName(an::OrderFilterMethod::SelectionKernel), "SelectionKernel");
    EXPECT_STREQ(an::orderFilterMethodName(an::OrderFilterMethod::SortingNetwork), "SortingNetwork");
    EXPECT_STREQ(an::orderFilterMethodName(static_cast<an::OrderFilterMethod>(9)), "UnknownOrderFilterMethod");
    EXPECT_TRUE(an::isOrderFilterMethod(an::OrderFilterMethod::SortingNetwork));
    EXPECT_FALSE(an::isOrderFilterMethod(static_cast<an::OrderFilterMethod>(3)));

    EXPECT_TRUE(an::hasMedianNetwork(1u, 1u));
    EXPECT_TRUE(an::hasMedianNetwork(2u, 2u));
    EXPECT_TRUE(an::hasMedianNetwork(3u, 3u));
    EXPECT_TRUE(an::hasMedianNetwork(4u, 4u));
    EXPECT_FALSE(an::hasMedianNetwork(5u, 5u));
    EXPECT_FALSE(an::hasMedianNetwork(0u, 0u));
    EXPECT_FALSE(an::hasMedianNetwork(1u, 4u));
    EXPECT_FALSE(an::hasMedianNetwork(4u, 1u));
}

TEST(OrderFilterTest, TheTwoImplementationsProduceTheSamePicture)
{
    constexpr std::uint32_t kWidth = 41u;
    constexpr std::uint32_t kHeight = 33u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 67u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    for (const std::uint32_t radius : {1u, 2u, 3u, 4u})
    {
        std::vector<float> viaNetwork(source.size(), 0.0f);
        std::vector<float> viaSelection(source.size(), 0.0f);
        an::OrderFilterRequest request{.radiusX = radius, .radiusY = radius, .border = an::KernelBorderMode::Clamp};

        request.method = an::OrderFilterMethod::SortingNetwork;
        const an::Result<an::OrderFilterResult> network = an::filterImageOrderStatistic(
            execution, an::SampleType::Float32, source.data(), layout, viaNetwork.data(), layout, request);
        ASSERT_TRUE(network.hasValue()) << network.status().message();
        EXPECT_EQ(network.value().method, an::OrderFilterMethod::SortingNetwork);
        EXPECT_EQ(network.value().windowSamples, (2u * radius + 1u) * (2u * radius + 1u));
        EXPECT_GT(network.value().networkComparators, 0u);
        EXPECT_EQ(network.value().networkOutputs, source.size());
        EXPECT_EQ(network.value().selectionOutputs, 0u);

        request.method = an::OrderFilterMethod::SelectionKernel;
        const an::Result<an::OrderFilterResult> selection = an::filterImageOrderStatistic(
            execution, an::SampleType::Float32, source.data(), layout, viaSelection.data(), layout, request);
        ASSERT_TRUE(selection.hasValue()) << selection.status().message();
        EXPECT_EQ(selection.value().method, an::OrderFilterMethod::SelectionKernel);
        EXPECT_EQ(selection.value().networkComparators, 0u);
        EXPECT_EQ(selection.value().networkOutputs, 0u);

        for (std::size_t index = 0u; index < source.size(); ++index)
        {
            ASSERT_FLOAT_EQ(viaNetwork[index], viaSelection[index]) << "radius " << radius << " at " << index;
        }
    }
}

TEST(OrderFilterTest, TheRetainedNetworkSortsEveryZeroOneWindow)
{
    // The zero-one principle: a network built only from compare-exchange produces the right rank
    // for every input exactly when it does for every sequence of zeros and ones. For a window of
    // nine that is five hundred and twelve sequences, and they fit in one image - each window laid
    // out in its own three-by-three block with a spacer column between blocks, so no two windows
    // overlap and the centre row needs no border rule at all.
    constexpr std::uint32_t kPatterns = 512u;
    constexpr std::uint32_t kStride = 4u;
    constexpr std::uint32_t kWidth = kPatterns * kStride;
    constexpr std::uint32_t kHeight = 3u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t pattern = 0u; pattern < kPatterns; ++pattern)
    {
        for (std::uint32_t bit = 0u; bit < 9u; ++bit)
        {
            const std::uint32_t row = bit / 3u;
            const std::uint32_t column = pattern * kStride + 1u + (bit % 3u);
            source[static_cast<std::size_t>(row) * kWidth + column] = ((pattern >> bit) & 1u) != 0u ? 1.0f : 0.0f;
        }
    }

    std::vector<float> destination(source.size(), -1.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::OrderFilterRequest request{.radiusX = 1u, .radiusY = 1u, .method = an::OrderFilterMethod::SortingNetwork};
    const an::Result<an::OrderFilterResult> filtered = an::filterImageOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered.value().method, an::OrderFilterMethod::SortingNetwork);
    EXPECT_EQ(filtered.value().networkComparators, 30u);

    for (std::uint32_t pattern = 0u; pattern < kPatterns; ++pattern)
    {
        std::uint32_t ones = 0u;
        for (std::uint32_t bit = 0u; bit < 9u; ++bit)
        {
            ones += (pattern >> bit) & 1u;
        }
        const float expected = ones >= 5u ? 1.0f : 0.0f;
        const std::size_t centre = static_cast<std::size_t>(kWidth) + pattern * kStride + 2u;
        ASSERT_FLOAT_EQ(destination[centre], expected) << "pattern " << pattern;
    }
}

TEST(OrderFilterTest, TheAnswerIsTheMiddleOfTheSortedWindow)
{
    constexpr std::uint32_t kWidth = 19u;
    constexpr std::uint32_t kHeight = 15u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 71u);
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::OrderFilterRequest request{.radiusX = 2u, .radiusY = 2u, .border = an::KernelBorderMode::Clamp};
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                              destination.data(), layout, request)
                    .hasValue());

    for (std::int64_t y = 0; y < kHeight; ++y)
    {
        for (std::int64_t x = 0; x < kWidth; ++x)
        {
            const float expected = clampedWindowMedian(source, kWidth, kHeight, x, y, 2);
            ASSERT_FLOAT_EQ(destination[static_cast<std::size_t>(y * kWidth + x)], expected) << x << "," << y;
        }
    }
}

TEST(OrderFilterTest, NotApplicableTruncatesBorderWindowsToTheImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 3u, 3u);
    an::OrderFilterRequest request{.radiusX = 1u, .radiusY = 1u, .border = an::KernelBorderMode::NotApplicable};

    const an::Result<an::OrderFilterResult> filtered = an::filterImageOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered->method, an::OrderFilterMethod::Automatic);
    EXPECT_EQ(filtered->networkOutputs, 1u);
    EXPECT_EQ(filtered->selectionOutputs, 8u);
    EXPECT_FLOAT_EQ(destination[0], 3.0f);
    EXPECT_FLOAT_EQ(destination[4], 5.0f);
    EXPECT_FLOAT_EQ(destination[8], 7.0f);

    const std::vector<float> automatic = destination;
    request.method = an::OrderFilterMethod::SelectionKernel;
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                              destination.data(), layout, request)
                    .hasValue());
    EXPECT_EQ(destination, automatic);

    request.method = an::OrderFilterMethod::SortingNetwork;
    EXPECT_EQ(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                            destination.data(), layout, request)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);
}

TEST(OrderFilterTest, AWindowTheEngineBeingReplacedOverflowsIsServedExactly)
{
    // A seventeen by seventeen window is two hundred and eighty-nine samples, more than a two
    // hundred and fifty-five element gather holds; this ranks it and says so.
    constexpr std::uint32_t kWidth = 40u;
    constexpr std::uint32_t kHeight = 40u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 79u);
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::OrderFilterRequest request{.radiusX = 8u, .radiusY = 8u, .border = an::KernelBorderMode::Clamp};
    const an::Result<an::OrderFilterResult> filtered = an::filterImageOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered.value().windowSamples, 289u);
    EXPECT_EQ(filtered.value().method, an::OrderFilterMethod::SelectionKernel);

    for (std::int64_t y = 0; y < kHeight; y += 7)
    {
        for (std::int64_t x = 0; x < kWidth; x += 7)
        {
            const float expected = clampedWindowMedian(source, kWidth, kHeight, x, y, 8);
            ASSERT_FLOAT_EQ(destination[static_cast<std::size_t>(y * kWidth + x)], expected) << x << "," << y;
        }
    }
}

TEST(OrderFilterTest, AWindowAboveTheStatedCeilingIsRefusedBeforeAnythingIsRead)
{
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::OrderFilterRequest request{.radiusX = 16u, .radiusY = 16u};
    const an::Status refused = an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                                             destination.data(), layout, request)
                                   .status();
    EXPECT_EQ(refused.code(), an::ErrorCode::OutOfRange);
    const an::StatusContextField* const requested = refused.findContext(an::ContextKey::Requested);
    const an::StatusContextField* const limit = refused.findContext(an::ContextKey::Limit);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(requested->count, 33u * 33u);
    EXPECT_EQ(limit->count, an::kMaxOrderWindowSamples);
}

TEST(OrderFilterTest, EveryRepresentationRanksTheSameWindow)
{
    // The rank is taken in one processing domain rather than by a helper per stored type, so every
    // representation carrying the same numbers answers the same rank.
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 7u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::OrderFilterRequest request{.radiusX = 1u, .radiusY = 1u, .border = an::KernelBorderMode::Clamp};

    std::vector<float> pattern(static_cast<std::size_t>(kWidth) * kHeight, 100.0f);
    pattern[kWidth * 3u + 4u] = 120.0f;
    pattern[kWidth * 3u + 5u] = 50.0f;

    const auto expected = [&](std::int64_t x, std::int64_t y) {
        return clampedWindowMedian(pattern, kWidth, kHeight, x, y, 1);
    };

    {
        std::vector<float> destination(pattern.size(), 0.0f);
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
        ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, pattern.data(), layout,
                                                  destination.data(), layout, request)
                        .hasValue());
        for (std::int64_t y = 0; y < kHeight; ++y)
        {
            for (std::int64_t x = 0; x < kWidth; ++x)
            {
                ASSERT_FLOAT_EQ(destination[static_cast<std::size_t>(y * kWidth + x)], expected(x, y));
            }
        }
    }

    const auto checkIntegers = [&]<typename SampleT>(an::SampleType type) {
        std::vector<SampleT> source(pattern.size(), SampleT{});
        for (std::size_t index = 0u; index < pattern.size(); ++index)
        {
            source[index] = static_cast<SampleT>(pattern[index]);
        }
        std::vector<SampleT> destination(pattern.size(), SampleT{});
        const an::ImageLayout layout = planarLayout(type, kWidth, kHeight);
        ASSERT_TRUE(
            an::filterImageOrderStatistic(execution, type, source.data(), layout, destination.data(), layout, request)
                .hasValue());
        for (std::int64_t y = 0; y < kHeight; ++y)
        {
            for (std::int64_t x = 0; x < kWidth; ++x)
            {
                ASSERT_EQ(destination[static_cast<std::size_t>(y * kWidth + x)], static_cast<SampleT>(expected(x, y)))
                    << an::sampleTypeName(type) << " at " << x << "," << y;
            }
        }
    };
    checkIntegers.template operator()<std::uint8_t>(an::SampleType::UInt8);
    checkIntegers.template operator()<std::int8_t>(an::SampleType::Int8);
    checkIntegers.template operator()<std::uint16_t>(an::SampleType::UInt16);
    checkIntegers.template operator()<std::int16_t>(an::SampleType::Int16);
    checkIntegers.template operator()<std::uint32_t>(an::SampleType::UInt32);
    checkIntegers.template operator()<std::int32_t>(an::SampleType::Int32);
    checkIntegers.template operator()<double>(an::SampleType::Float64);
}

TEST(OrderFilterTest, TheExtremeQuantilesAreTheWindowMinimumAndMaximum)
{
    constexpr std::uint32_t kWidth = 11u;
    constexpr std::uint32_t kHeight = 9u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 83u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<float> eroded(source.size(), 0.0f);
    std::vector<float> dilated(source.size(), 0.0f);
    an::OrderFilterRequest request{.radiusX = 1u, .radiusY = 1u, .border = an::KernelBorderMode::Clamp};
    request.quantile = 0.0;
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout, eroded.data(),
                                              layout, request)
                    .hasValue());
    request.quantile = 1.0;
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout, dilated.data(),
                                              layout, request)
                    .hasValue());

    for (std::int64_t y = 0; y < kHeight; ++y)
    {
        for (std::int64_t x = 0; x < kWidth; ++x)
        {
            float smallest = std::numeric_limits<float>::infinity();
            float largest = -std::numeric_limits<float>::infinity();
            for (std::int64_t row = y - 1; row <= y + 1; ++row)
            {
                for (std::int64_t column = x - 1; column <= x + 1; ++column)
                {
                    const std::int64_t clampedRow =
                        (std::min)((std::max)(row, std::int64_t{0}), static_cast<std::int64_t>(kHeight) - 1);
                    const std::int64_t clampedColumn =
                        (std::min)((std::max)(column, std::int64_t{0}), static_cast<std::int64_t>(kWidth) - 1);
                    const float sample = source[static_cast<std::size_t>(clampedRow * kWidth + clampedColumn)];
                    smallest = (std::min)(smallest, sample);
                    largest = (std::max)(largest, sample);
                }
            }
            const std::size_t index = static_cast<std::size_t>(y * kWidth + x);
            ASSERT_FLOAT_EQ(eroded[index], smallest) << x << "," << y;
            ASSERT_FLOAT_EQ(dilated[index], largest) << x << "," << y;
        }
    }
}

TEST(OrderFilterTest, AMedianRemovesAnIsolatedSpikeAndABlendSoftensIt)
{
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 9u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    source[kWidth * 4u + 4u] = 1.0f;
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::OrderFilterRequest request{.radiusX = 1u, .radiusY = 1u, .border = an::KernelBorderMode::Clamp};
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                              destination.data(), layout, request)
                    .hasValue());
    EXPECT_FLOAT_EQ(destination[kWidth * 4u + 4u], 0.25f);

    request.amount = 0.5;
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                              destination.data(), layout, request)
                    .hasValue());
    EXPECT_FLOAT_EQ(destination[kWidth * 4u + 4u], 0.625f);

    request.amount = 0.0;
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                              destination.data(), layout, request)
                    .hasValue());
    EXPECT_FLOAT_EQ(destination[kWidth * 4u + 4u], 1.0f);
}

TEST(OrderFilterTest, MoreThanOnePassReadsWhatThePassBeforeItWrote)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 11u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 89u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<float> once(source.size(), 0.0f);
    std::vector<float> twice(source.size(), 0.0f);
    std::vector<float> chained(source.size(), 0.0f);
    an::OrderFilterRequest request{.radiusX = 1u, .radiusY = 1u, .border = an::KernelBorderMode::Clamp};
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout, once.data(),
                                              layout, request)
                    .hasValue());
    ASSERT_TRUE(an::filterImageOrderStatistic(execution, an::SampleType::Float32, once.data(), layout, chained.data(),
                                              layout, request)
                    .hasValue());

    request.iterations = 2u;
    const an::Result<an::OrderFilterResult> repeated = an::filterImageOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, twice.data(), layout, request);
    ASSERT_TRUE(repeated.hasValue()) << repeated.status().message();
    EXPECT_EQ(repeated.value().iterations, 2u);
    EXPECT_EQ(repeated.value().filter.imagePasses, 2u);
    EXPECT_GT(repeated.value().filter.workingBytes, 0u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        ASSERT_FLOAT_EQ(twice[index], chained[index]) << "at " << index;
    }
}

TEST(OrderFilterTest, AnInvalidWindowSampleIsLeftOutRatherThanSubstituted)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 5u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(index);
    }
    source[12] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> destination(source.size(), -1.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    an::SpatialSelection selection{};
    selection.validity = *finiteOnly;

    const an::OrderFilterRequest request{.radiusX = 1u, .radiusY = 1u, .border = an::KernelBorderMode::Clamp};
    const an::Result<an::OrderFilterResult> filtered = an::filterImageOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request, selection);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered.value().method, an::OrderFilterMethod::SelectionKernel);
    EXPECT_FLOAT_EQ(destination[12], -1.0f);

    // The window around the centre above the rejected one holds eight remaining samples - 1, 2, 3,
    // 6, 7, 8, 11 and 13 - so the linear rule between the two middle ranks answers 6.5. Substituting
    // for the rejected sample instead of leaving it out would move that.
    EXPECT_FLOAT_EQ(destination[7], 6.5f);
    EXPECT_EQ(filtered.value().filter.run.outputsWritten, source.size() - 1u);
}

TEST(OrderFilterTest, ANetworkAskedForWhereNoneExistsIsRefusedRatherThanReplaced)
{
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::OrderFilterRequest request{.radiusX = 5u, .radiusY = 5u, .method = an::OrderFilterMethod::SortingNetwork};
    EXPECT_EQ(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                            destination.data(), layout, request)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);

    request.radiusX = 1u;
    request.radiusY = 1u;
    request.quantile = 0.25;
    EXPECT_EQ(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                            destination.data(), layout, request)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);

    // The same request under Automatic is served, by the implementation that can serve it.
    request.method = an::OrderFilterMethod::Automatic;
    const an::Result<an::OrderFilterResult> served = an::filterImageOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request);
    ASSERT_TRUE(served.hasValue()) << served.status().message();
    EXPECT_EQ(served.value().method, an::OrderFilterMethod::SelectionKernel);
}

TEST(OrderFilterTest, TheRefusalsAnOrderFilterOwesItsCaller)
{
    constexpr std::uint32_t kWidth = 6u;
    constexpr std::uint32_t kHeight = 6u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const auto run = [&](const an::OrderFilterRequest& request) {
        return an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                             destination.data(), layout, request)
            .status()
            .code();
    };

    an::OrderFilterRequest request{};
    request.iterations = 0u;
    EXPECT_EQ(run(request), an::ErrorCode::OutOfRange);

    request = an::OrderFilterRequest{};
    request.iterations = an::kMaxOrderFilterIterations + 1u;
    EXPECT_EQ(run(request), an::ErrorCode::OutOfRange);

    request = an::OrderFilterRequest{};
    request.quantile = 1.5;
    EXPECT_EQ(run(request), an::ErrorCode::OutOfRange);

    request = an::OrderFilterRequest{};
    request.amount = -0.5;
    EXPECT_EQ(run(request), an::ErrorCode::InvalidArgument);

    request = an::OrderFilterRequest{};
    request.border = static_cast<an::KernelBorderMode>(9);
    EXPECT_EQ(run(request), an::ErrorCode::InvalidArgument);

    request = an::OrderFilterRequest{};
    request.interpolation = static_cast<an::QuantileInterpolation>(9);
    EXPECT_EQ(run(request), an::ErrorCode::InvalidArgument);

    request = an::OrderFilterRequest{};
    request.method = static_cast<an::OrderFilterMethod>(9);
    EXPECT_EQ(run(request), an::ErrorCode::InvalidArgument);

    request = an::OrderFilterRequest{};
    EXPECT_EQ(an::filterImageOrderStatistic(execution, an::SampleType::Float32, source.data(), layout, source.data(),
                                            layout, request)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

} // namespace
