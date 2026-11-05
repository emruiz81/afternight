// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the order filter over a structuring element: a whole element ranks as the
// rectangular filter does, its extreme ranks are the morphological erosion and dilation by the same
// element, an element that leaves its centre out ranks its members alone, and every other rule is the
// rectangular filter's.

#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// Every cell of a window marked, row-major.
[[nodiscard]] std::vector<std::uint8_t> wholeElement(std::uint32_t radiusX, std::uint32_t radiusY)
{
    return std::vector<std::uint8_t>(static_cast<std::size_t>(2u * radiusX + 1u) * (2u * radiusY + 1u), 1u);
}

/// The rank a sorted window answers under the linear rule, which is the definition the filter answers.
///
/// @param window the members' samples.
/// @param quantile the fraction.
/// @return the selected value.
[[nodiscard]] double linearRank(std::vector<float> window, double quantile)
{
    std::sort(window.begin(), window.end());
    const double position = quantile * static_cast<double>(window.size() - 1u);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const double fraction = position - static_cast<double>(lower);
    if (fraction == 0.0)
    {
        return window[lower];
    }
    return window[lower] + fraction * (static_cast<double>(window[lower + 1u]) - window[lower]);
}

/// The members of an element around a centre that lie inside the plane, read as the filter reads
/// them: the cell at row `r` and column `c` of the element is the sample `c - radiusX` columns across
/// and `r - radiusY` rows down from the centre.
[[nodiscard]] std::vector<float> membersInside(const std::vector<float>& samples, std::int64_t width,
                                               std::int64_t height, std::int64_t x, std::int64_t y,
                                               std::span<const std::uint8_t> element, std::int64_t radiusX,
                                               std::int64_t radiusY)
{
    std::vector<float> members;
    const std::int64_t across = 2 * radiusX + 1;
    for (std::int64_t row = 0; row < 2 * radiusY + 1; ++row)
    {
        for (std::int64_t column = 0; column < across; ++column)
        {
            if (element[static_cast<std::size_t>(row * across + column)] == 0u)
            {
                continue;
            }
            const std::int64_t sourceX = x + column - radiusX;
            const std::int64_t sourceY = y + row - radiusY;
            if (sourceX >= 0 && sourceX < width && sourceY >= 0 && sourceY < height)
            {
                members.push_back(samples[static_cast<std::size_t>(sourceY * width + sourceX)]);
            }
        }
    }
    return members;
}

TEST(FootprintOrderFilterTest, AWholeElementRanksAsTheRectangularFilterDoes)
{
    constexpr std::uint32_t kWidth = 23u;
    constexpr std::uint32_t kHeight = 17u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 101u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    for (const std::uint32_t radius : {1u, 2u, 3u})
    {
        for (const double quantile : {0.0, 0.3, 0.5, 1.0})
        {
            for (const an::KernelBorderMode border : {an::KernelBorderMode::Clamp, an::KernelBorderMode::NotApplicable,
                                                      an::KernelBorderMode::ReflectExcludingEdge})
            {
                const std::vector<std::uint8_t> element = wholeElement(radius, radius + 1u);
                std::vector<float> rectangular(source.size(), -1.0f);
                std::vector<float> footprint(source.size(), -2.0f);
                const an::Result<an::OrderFilterResult> expected = an::filterImageOrderStatistic(
                    execution, an::SampleType::Float32, source.data(), layout, rectangular.data(), layout,
                    {.radiusX = radius,
                     .radiusY = radius + 1u,
                     .quantile = quantile,
                     .border = border,
                     .method = an::OrderFilterMethod::SelectionKernel});
                ASSERT_TRUE(expected.hasValue()) << expected.status().message();
                const an::Result<an::OrderFilterResult> filtered = an::filterImageFootprintOrderStatistic(
                    execution, an::SampleType::Float32, source.data(), layout, footprint.data(), layout,
                    {.radiusX = radius,
                     .radiusY = radius + 1u,
                     .footprint = element,
                     .quantile = quantile,
                     .border = border});
                ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
                EXPECT_EQ(filtered->method, an::OrderFilterMethod::SelectionKernel);
                EXPECT_EQ(filtered->windowSamples, element.size());
                EXPECT_EQ(filtered->networkComparators, 0u);
                EXPECT_EQ(filtered->networkOutputs, 0u);
                EXPECT_EQ(filtered->selectionOutputs, source.size());
                EXPECT_EQ(filtered->filter.run.outputsWritten, source.size());
                ASSERT_EQ(rectangular, footprint) << "radius " << radius << ", quantile " << quantile;
            }
        }
    }
}

TEST(FootprintOrderFilterTest, TheExtremeRanksAreTheErosionAndDilationByTheSameElement)
{
    constexpr std::uint32_t kWidth = 19u;
    constexpr std::uint32_t kHeight = 14u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 211u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    // A diamond, a cross without its centre, and an asymmetric element whose reflection is another
    // element - which a filter reading the cell as the sample across from the centre would confuse.
    const std::array<std::vector<std::uint8_t>, 3u> elements{
        std::vector<std::uint8_t>{0u, 0u, 1u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, 1u, 1u, 1u,
                                  1u, 1u, 0u, 1u, 1u, 1u, 0u, 0u, 0u, 1u, 0u, 0u},
        std::vector<std::uint8_t>{0u, 1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u},
        std::vector<std::uint8_t>{1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 1u}};
    const std::array<std::uint32_t, 3u> radii{2u, 1u, 1u};
    for (std::size_t index = 0u; index < elements.size(); ++index)
    {
        const std::span<const std::uint8_t> element = elements[index];
        const std::uint32_t radius = radii[index];
        for (const bool erode : {true, false})
        {
            std::vector<float> morphology(source.size(), -1.0f);
            std::vector<float> ranked(source.size(), -2.0f);
            // The neutral border: every sample outside the image is above the minimum and below the
            // maximum it is taken with, which is what leaving it out means to an extreme rank.
            const an::Result<an::MorphologyPlan> planned = an::planMorphology(
                {.operation = erode ? an::MorphologyOperation::Erode : an::MorphologyOperation::Dilate,
                 .shape = an::MorphologyShape::Custom,
                 .radiusX = radius,
                 .radiusY = radius,
                 .border = an::KernelBorderMode::Constant,
                 .constantBorder = erode ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max(),
                 .customFootprint = element});
            ASSERT_TRUE(planned.hasValue()) << planned.status().message();
            ASSERT_TRUE(an::filterImageMorphology(execution, an::SampleType::Float32, source.data(), layout,
                                                  morphology.data(), layout, *planned)
                            .hasValue());
            const an::Result<an::OrderFilterResult> filtered = an::filterImageFootprintOrderStatistic(
                execution, an::SampleType::Float32, source.data(), layout, ranked.data(), layout,
                {.radiusX = radius,
                 .radiusY = radius,
                 .footprint = element,
                 .quantile = erode ? 0.0 : 1.0,
                 .border = an::KernelBorderMode::NotApplicable});
            ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
            ASSERT_EQ(morphology, ranked) << "element " << index << (erode ? ", erosion" : ", dilation");
        }
    }
}

TEST(FootprintOrderFilterTest, EveryRankOfAnElementIsTheRankOfItsMembers)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 11u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 307u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    // A disc of radius two: the cells within two of the centre.
    std::vector<std::uint8_t> disc(25u, 0u);
    for (std::int64_t row = -2; row <= 2; ++row)
    {
        for (std::int64_t column = -2; column <= 2; ++column)
        {
            disc[static_cast<std::size_t>((row + 2) * 5 + column + 2)] = row * row + column * column <= 4 ? 1u : 0u;
        }
    }

    for (const double quantile : {0.25, 0.5, 0.8})
    {
        std::vector<float> ranked(source.size(), -1.0f);
        const an::Result<an::OrderFilterResult> filtered = an::filterImageFootprintOrderStatistic(
            execution, an::SampleType::Float32, source.data(), layout, ranked.data(), layout,
            {.radiusX = 2u,
             .radiusY = 2u,
             .footprint = disc,
             .quantile = quantile,
             .border = an::KernelBorderMode::NotApplicable});
        ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
        EXPECT_EQ(filtered->windowSamples, 13u);
        for (std::int64_t y = 0; y < kHeight; ++y)
        {
            for (std::int64_t x = 0; x < kWidth; ++x)
            {
                const double expected = linearRank(membersInside(source, kWidth, kHeight, x, y, disc, 2, 2), quantile);
                ASSERT_FLOAT_EQ(ranked[static_cast<std::size_t>(y * kWidth + x)], static_cast<float>(expected))
                    << x << "," << y << " at " << quantile;
            }
        }
    }
}

TEST(FootprintOrderFilterTest, AnElementThatLeavesItsCentreOutRanksItsMembersAlone)
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
    source[12] = 100.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    // The centre's four neighbours: 7, 11, 13 and 17, whose median under the linear rule is 12 - the
    // centre's own 100 takes no part.
    const std::array<std::uint8_t, 9u> ring{0u, 1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u};
    std::vector<float> ranked(source.size(), -1.0f);
    const an::Result<an::OrderFilterResult> filtered = an::filterImageFootprintOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, ranked.data(), layout,
        {.radiusX = 1u, .radiusY = 1u, .footprint = ring, .border = an::KernelBorderMode::NotApplicable});
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_FLOAT_EQ(ranked[12], 12.0f);
    EXPECT_EQ(filtered->windowSamples, 4u);

    // An element of one cell up and to the left has no member inside the image at the top-left
    // corner: that centre has no window, is not written, and is not counted.
    const std::array<std::uint8_t, 9u> corner{1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    std::vector<float> shifted(source.size(), -1.0f);
    const an::Result<an::OrderFilterResult> moved = an::filterImageFootprintOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, shifted.data(), layout,
        {.radiusX = 1u, .radiusY = 1u, .footprint = corner, .border = an::KernelBorderMode::NotApplicable});
    ASSERT_TRUE(moved.hasValue()) << moved.status().message();
    EXPECT_FLOAT_EQ(shifted[0], -1.0f);
    EXPECT_FLOAT_EQ(shifted[6], source[0]);
    EXPECT_FLOAT_EQ(shifted[24], source[18]);
    EXPECT_EQ(moved->filter.run.outputsWritten, (kWidth - 1u) * (kHeight - 1u));
}

TEST(FootprintOrderFilterTest, PassesReadWhatThePassBeforeWroteAndABlendIsAppliedOnEach)
{
    constexpr std::uint32_t kWidth = 15u;
    constexpr std::uint32_t kHeight = 12u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 409u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::array<std::uint8_t, 9u> plus{0u, 1u, 0u, 1u, 1u, 1u, 0u, 1u, 0u};
    const an::FootprintOrderFilterRequest once{.radiusX = 1u,
                                               .radiusY = 1u,
                                               .footprint = plus,
                                               .quantile = 0.0,
                                               .border = an::KernelBorderMode::NotApplicable,
                                               .amount = 0.5};

    std::vector<float> first(source.size(), 0.0f);
    std::vector<float> second(source.size(), 0.0f);
    ASSERT_TRUE(an::filterImageFootprintOrderStatistic(execution, an::SampleType::Float32, source.data(), layout,
                                                       first.data(), layout, once)
                    .hasValue());
    ASSERT_TRUE(an::filterImageFootprintOrderStatistic(execution, an::SampleType::Float32, first.data(), layout,
                                                       second.data(), layout, once)
                    .hasValue());

    an::FootprintOrderFilterRequest twice = once;
    twice.iterations = 2u;
    std::vector<float> both(source.size(), 0.0f);
    const an::Result<an::OrderFilterResult> filtered = an::filterImageFootprintOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, both.data(), layout, twice);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered->iterations, 2u);
    EXPECT_EQ(filtered->filter.run.outputsWritten, source.size());
    EXPECT_EQ(both, second);

    // The blend is half the source and half the rank, sample by sample.
    for (std::int64_t y = 0; y < kHeight; ++y)
    {
        for (std::int64_t x = 0; x < kWidth; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y * kWidth + x);
            const double minimum = linearRank(membersInside(source, kWidth, kHeight, x, y, plus, 1, 1), 0.0);
            ASSERT_NEAR(first[index], 0.5 * minimum + 0.5 * source[index], 1e-6) << x << "," << y;
        }
    }
}

TEST(FootprintOrderFilterTest, EveryRepresentationRanksTheSameElement)
{
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 7u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<std::uint16_t> wide(static_cast<std::size_t>(kWidth) * kHeight, 0u);
    std::vector<std::int8_t> narrow(wide.size(), 0);
    std::vector<float> reference(wide.size(), 0.0f);
    for (std::size_t index = 0u; index < wide.size(); ++index)
    {
        const auto value = static_cast<int>((index * 37u) % 101u);
        wide[index] = static_cast<std::uint16_t>(value * 600);
        narrow[index] = static_cast<std::int8_t>(value - 50);
        reference[index] = static_cast<float>(value);
    }
    const std::array<std::uint8_t, 9u> diagonals{1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u, 1u};
    const an::FootprintOrderFilterRequest request{.radiusX = 1u,
                                                  .radiusY = 1u,
                                                  .footprint = diagonals,
                                                  .quantile = 0.5,
                                                  .border = an::KernelBorderMode::NotApplicable};

    std::vector<float> referenceOut(reference.size(), 0.0f);
    std::vector<std::uint16_t> wideOut(wide.size(), 0u);
    std::vector<std::int8_t> narrowOut(narrow.size(), 0);
    ASSERT_TRUE(an::filterImageFootprintOrderStatistic(execution, an::SampleType::Float32, reference.data(),
                                                       planarLayout(an::SampleType::Float32, kWidth, kHeight),
                                                       referenceOut.data(),
                                                       planarLayout(an::SampleType::Float32, kWidth, kHeight), request)
                    .hasValue());
    ASSERT_TRUE(an::filterImageFootprintOrderStatistic(execution, an::SampleType::UInt16, wide.data(),
                                                       planarLayout(an::SampleType::UInt16, kWidth, kHeight),
                                                       wideOut.data(),
                                                       planarLayout(an::SampleType::UInt16, kWidth, kHeight), request)
                    .hasValue());
    ASSERT_TRUE(an::filterImageFootprintOrderStatistic(
                    execution, an::SampleType::Int8, narrow.data(), planarLayout(an::SampleType::Int8, kWidth, kHeight),
                    narrowOut.data(), planarLayout(an::SampleType::Int8, kWidth, kHeight), request)
                    .hasValue());
    for (std::size_t index = 0u; index < reference.size(); ++index)
    {
        // An even window takes the mean of its two middle members, which rounds to the nearest even
        // code on the way back into an integer representation.
        EXPECT_EQ(static_cast<double>(wideOut[index]), std::nearbyint(referenceOut[index] * 600.0)) << index;
        EXPECT_EQ(static_cast<double>(narrowOut[index]), std::nearbyint(referenceOut[index] - 50.0)) << index;
    }
}

TEST(FootprintOrderFilterTest, AnInvalidMemberIsLeftOutAndAnInvalidCentreIsNotWritten)
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
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    an::SpatialSelection selection{};
    selection.validity = *finiteOnly;

    const std::array<std::uint8_t, 9u> plus{0u, 1u, 0u, 1u, 1u, 1u, 0u, 1u, 0u};
    std::vector<float> ranked(source.size(), -1.0f);
    const an::Result<an::OrderFilterResult> filtered = an::filterImageFootprintOrderStatistic(
        execution, an::SampleType::Float32, source.data(), layout, ranked.data(), layout,
        {.radiusX = 1u, .radiusY = 1u, .footprint = plus, .border = an::KernelBorderMode::NotApplicable}, selection);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_FLOAT_EQ(ranked[12], -1.0f);
    // The centre below the rejected one keeps 16, 18, 17 and 22 of its plus: a median of 17.5.
    EXPECT_FLOAT_EQ(ranked[17], 17.5f);
    EXPECT_EQ(filtered->filter.run.outputsWritten, source.size() - 1u);
}

TEST(FootprintOrderFilterTest, TheRefusalsTheFilterOwesItsCaller)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(6u, 5u, 503u);
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 6u, 5u);
    const std::vector<std::uint8_t> nine = wholeElement(1u, 1u);
    const auto refusal = [&](const an::FootprintOrderFilterRequest& request) {
        const an::Result<an::OrderFilterResult> filtered = an::filterImageFootprintOrderStatistic(
            execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request);
        EXPECT_FALSE(filtered.hasValue());
        return filtered.hasValue() ? an::ErrorCode::Ok : filtered.status().code();
    };

    const std::vector<std::uint8_t> eight(8u, 1u);
    EXPECT_EQ(refusal({.radiusX = 1u, .radiusY = 1u, .footprint = eight}), an::ErrorCode::IncompatibleArguments);
    const std::vector<std::uint8_t> empty(9u, 0u);
    EXPECT_EQ(refusal({.radiusX = 1u, .radiusY = 1u, .footprint = empty}), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal({.radiusX = 1u, .radiusY = 1u, .footprint = nine, .quantile = 1.5}), an::ErrorCode::OutOfRange);
    EXPECT_EQ(refusal({.radiusX = 1u, .radiusY = 1u, .footprint = nine, .amount = -0.1}),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal({.radiusX = 1u, .radiusY = 1u, .footprint = nine, .iterations = 0u}), an::ErrorCode::OutOfRange);
    EXPECT_EQ(
        refusal({.radiusX = 1u, .radiusY = 1u, .footprint = nine, .iterations = an::kMaxOrderFilterIterations + 1u}),
        an::ErrorCode::OutOfRange);
    EXPECT_EQ(refusal({.radiusX = 1u,
                       .radiusY = 1u,
                       .footprint = nine,
                       .border = an::KernelBorderMode::Constant,
                       .constantBorder = std::numeric_limits<float>::infinity()}),
              an::ErrorCode::InvalidArgument);
    const std::vector<std::uint8_t> wide(static_cast<std::size_t>(2u * an::kMaxStencilRadius + 3u), 1u);
    EXPECT_EQ(refusal({.radiusX = an::kMaxStencilRadius + 1u, .radiusY = 0u, .footprint = wide}),
              an::ErrorCode::OutOfRange);
    const std::vector<std::uint8_t> crowded(33u * 33u, 1u);
    EXPECT_EQ(refusal({.radiusX = 16u, .radiusY = 16u, .footprint = crowded}), an::ErrorCode::OutOfRange);

    // The destination is untouched by a request refused before a sample is read.
    EXPECT_TRUE(std::all_of(destination.begin(), destination.end(), [](float value) { return value == 0.0f; }));
}

} // namespace
