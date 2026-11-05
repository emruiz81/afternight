// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the star record's ordering, the frame summary, and the masks a list paints.

#include <AfterNightEngine/Algorithms/Stars/StarMask.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 2u)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::DetectedStar makeStar(double x, double y, double widthX, double widthY, double amplitude,
                                        bool saturated = false)
{
    an::DetectedStar star{};
    star.positionX = x;
    star.positionY = y;
    star.psf.fullWidthX = widthX;
    star.psf.fullWidthY = widthY;
    star.psf.amplitude = amplitude;
    star.psf.saturated = saturated;
    star.metrics.roundness = widthX != 0.0 ? widthY / widthX : 0.0;
    star.metrics.meanFullWidth = 0.5 * (widthX + widthY);
    star.metrics.circularity = amplitude / (0.1 + std::abs(widthX - widthY));
    star.instrumentalMagnitude = -2.5 * std::log10(amplitude);
    return star;
}

[[nodiscard]] an::ImageLayout maskLayoutOf(std::uint32_t width, std::uint32_t height,
                                           an::SampleType sampleType = an::SampleType::Float32)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest descriptorRequest{
        .extent = extent.value_or(an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits()).value()),
        .sampleType = sampleType,
        .channelSchema = an::ChannelSchema::Mono,
        .colorSpace = an::ColorSpace::Unspecified,
        .numericDomain = an::NumericDomain::NormalizedLinear,
        .cfa = {},
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(descriptorRequest);
    EXPECT_TRUE(descriptor.has_value());
    const std::optional<an::ImageLayoutRequest> layoutRequest =
        an::layoutRequestFor(*descriptor, an::StorageArrangement::Planar, 1u);
    EXPECT_TRUE(layoutRequest.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*layoutRequest, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

} // namespace

TEST(StarProfileTest, NamesEveryModelAndSortKey)
{
    EXPECT_STREQ(an::psfModelName(an::PsfModel::Gaussian), "Gaussian");
    EXPECT_STREQ(an::psfModelName(an::PsfModel::MoffatFixedBeta), "MoffatFixedBeta");
    EXPECT_STREQ(an::psfModelName(an::PsfModel::MoffatFreeBeta), "MoffatFreeBeta");
    EXPECT_STREQ(an::psfModelName(static_cast<an::PsfModel>(9)), "UnknownPsfModel");
    EXPECT_FALSE(an::isPsfModel(static_cast<an::PsfModel>(9)));

    for (std::uint8_t index = 0u; index <= static_cast<std::uint8_t>(an::StarSortKey::MeanFullWidth); ++index)
    {
        const auto key = static_cast<an::StarSortKey>(index);
        EXPECT_TRUE(an::isStarSortKey(key));
        EXPECT_STRNE(an::starSortKeyName(key), "UnknownStarSortKey");
    }
    EXPECT_STREQ(an::starSortKeyName(static_cast<an::StarSortKey>(99)), "UnknownStarSortKey");
}

TEST(StarProfileTest, SortsStablyAndPutsAnAbsentValueLastAscending)
{
    std::vector<an::DetectedStar> stars{makeStar(10.0, 1.0, 4.0, 4.0, 0.5), makeStar(5.0, 2.0, 4.0, 4.0, 0.5),
                                        makeStar(5.0, 3.0, 4.0, 4.0, 0.5), makeStar(1.0, 4.0, 4.0, 4.0, 0.5)};
    ASSERT_TRUE(an::sortStars(stars, an::StarSortKey::PositionX).ok());
    EXPECT_DOUBLE_EQ(stars[0].positionX, 1.0);
    // Equal keys keep the order they had, so the two at five come back in their original order.
    EXPECT_DOUBLE_EQ(stars[1].positionY, 2.0);
    EXPECT_DOUBLE_EQ(stars[2].positionY, 3.0);
    EXPECT_DOUBLE_EQ(stars[3].positionX, 10.0);

    ASSERT_TRUE(an::sortStars(stars, an::StarSortKey::PositionX, an::StarSortDirection::Descending).ok());
    EXPECT_DOUBLE_EQ(stars[0].positionX, 10.0);
    EXPECT_DOUBLE_EQ(stars[3].positionX, 1.0);

    std::vector<an::DetectedStar> mixed{makeStar(1.0, 1.0, 4.0, 4.0, 0.5), makeStar(2.0, 2.0, 4.0, 4.0, 0.5)};
    mixed[0].instrumentalMagnitude.reset();
    ASSERT_TRUE(an::sortStars(mixed, an::StarSortKey::InstrumentalMagnitude).ok());
    EXPECT_TRUE(mixed[0].instrumentalMagnitude.has_value());
    EXPECT_FALSE(mixed[1].instrumentalMagnitude.has_value());
    ASSERT_TRUE(an::sortStars(mixed, an::StarSortKey::InstrumentalMagnitude, an::StarSortDirection::Descending).ok());
    EXPECT_FALSE(mixed[0].instrumentalMagnitude.has_value());

    EXPECT_EQ(an::sortStars(stars, static_cast<an::StarSortKey>(99)).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::sortStars(stars, an::StarSortKey::PositionX, static_cast<an::StarSortDirection>(9)).code(),
              an::ErrorCode::InvalidArgument);
}

TEST(StarProfileTest, SummarisesWithMediansAndExcludesSaturatedStarsFromTheQuality)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StarChannelStatistics channel{};
    channel.background = 0.05;
    channel.backgroundNoise = 0.01;
    channel.channelIndex = 0u;

    const std::vector<an::DetectedStar> stars{makeStar(1.0, 1.0, 4.0, 4.0, 0.2), makeStar(2.0, 2.0, 5.0, 4.0, 0.3),
                                              makeStar(3.0, 3.0, 6.0, 6.0, 0.4),
                                              makeStar(4.0, 4.0, 30.0, 30.0, 5.0, true)};
    const an::Result<an::StarFieldSummary> summary = an::summarizeStarField(execution, stars, channel);
    ASSERT_TRUE(summary.hasValue()) << summary.status().message();
    EXPECT_EQ(summary->starCount, 4u);
    EXPECT_EQ(summary->saturatedStarCount, 1u);
    ASSERT_TRUE(summary->medianFullWidth.has_value());
    // Widths 4, 4.5, 6 and 30: the median of four values is the average of the middle two.
    EXPECT_DOUBLE_EQ(*summary->medianFullWidth, 0.5 * (4.5 + 6.0));
    ASSERT_TRUE(summary->medianRoundness.has_value());
    // Roundnesses 0.8, 1, 1 and 1: the middle two are both one.
    EXPECT_DOUBLE_EQ(*summary->medianRoundness, 1.0);
    ASSERT_TRUE(summary->quality.has_value());
    // Three unsaturated stars of widths 4, 4.5 and 6 with roundnesses 1, 0.8 and 1.
    EXPECT_DOUBLE_EQ(*summary->quality, 3.0 * 1.0 / 4.5);
    ASSERT_TRUE(summary->medianSignalToNoise.has_value());
    EXPECT_DOUBLE_EQ(*summary->medianSignalToNoise, 0.3 / 0.01);
    EXPECT_DOUBLE_EQ(summary->channel.background, 0.05);

    const an::Result<an::StarFieldSummary> empty = an::summarizeStarField(execution, {}, channel);
    ASSERT_TRUE(empty.hasValue()) << empty.status().message();
    EXPECT_EQ(empty->starCount, 0u);
    EXPECT_FALSE(empty->medianFullWidth.has_value());

    const an::ExecutionContext none;
    EXPECT_EQ(an::summarizeStarField(none, stars, channel).status().code(), an::ErrorCode::NotInitialized);
}

TEST(StarProfileTest, SummaryDoesNotDependOnTheListOrder)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StarChannelStatistics channel{};
    channel.backgroundNoise = 0.02;
    std::vector<an::DetectedStar> stars;
    for (std::uint32_t index = 0u; index < 17u; ++index)
    {
        stars.push_back(makeStar(index, index, 3.0 + 0.31 * index, 2.5 + 0.29 * index, 0.1 + 0.03 * index));
    }
    const an::Result<an::StarFieldSummary> forward = an::summarizeStarField(execution, stars, channel);
    ASSERT_TRUE(forward.hasValue());
    std::reverse(stars.begin(), stars.end());
    const an::Result<an::StarFieldSummary> backward = an::summarizeStarField(execution, stars, channel);
    ASSERT_TRUE(backward.hasValue());
    EXPECT_DOUBLE_EQ(*forward->medianFullWidth, *backward->medianFullWidth);
    EXPECT_DOUBLE_EQ(*forward->medianRoundness, *backward->medianRoundness);
    EXPECT_DOUBLE_EQ(*forward->quality, *backward->quality);
    EXPECT_DOUBLE_EQ(*forward->medianSignalToNoise, *backward->medianSignalToNoise);
}

TEST(StarMaskTest, NamesEveryShapeAndEvaluatesItsOwnFormula)
{
    for (std::uint8_t index = 0u; index <= static_cast<std::uint8_t>(an::StarMaskShape::Quadratic); ++index)
    {
        const auto shape = static_cast<an::StarMaskShape>(index);
        EXPECT_TRUE(an::isStarMaskShape(shape));
        EXPECT_STRNE(an::starMaskShapeName(shape), "UnknownStarMaskShape");
    }
    EXPECT_STREQ(an::starMaskShapeName(static_cast<an::StarMaskShape>(9)), "UnknownStarMaskShape");

    const double radius = 3.0;
    const an::Result<double> bellCentre = an::starMaskCoverage(an::StarMaskShape::Bell, 0.0, radius);
    ASSERT_TRUE(bellCentre.hasValue());
    EXPECT_DOUBLE_EQ(*bellCentre, 1.0);
    const an::Result<double> bell = an::starMaskCoverage(an::StarMaskShape::Bell, 2.0, radius);
    ASSERT_TRUE(bell.hasValue());
    EXPECT_NEAR(*bell, std::exp(-4.0 / 18.0), 1e-12);
    const an::Result<double> truncated = an::starMaskCoverage(an::StarMaskShape::TruncatedBell, radius, radius);
    ASSERT_TRUE(truncated.hasValue());
    EXPECT_DOUBLE_EQ(*truncated, 1.0);
    const an::Result<double> linear = an::starMaskCoverage(an::StarMaskShape::Linear, 3.0 * radius, radius);
    ASSERT_TRUE(linear.hasValue());
    EXPECT_DOUBLE_EQ(*linear, 0.0);
    // The truncated linear shape's unclamped centre is 1.5. The painting contract clamps
    // it to full coverage, keeps the plateau through one radius, then falls to zero at three.
    for (const double scale : {0.125, 2.0, 17.0})
    {
        const auto centre = an::starMaskCoverage(an::StarMaskShape::TruncatedLinear, 0.0, scale);
        const auto plateau = an::starMaskCoverage(an::StarMaskShape::TruncatedLinear, scale, scale);
        const auto slope = an::starMaskCoverage(an::StarMaskShape::TruncatedLinear, 1.5 * scale, scale);
        const auto edge = an::starMaskCoverage(an::StarMaskShape::TruncatedLinear, 3.0 * scale, scale);
        ASSERT_TRUE(centre.hasValue());
        ASSERT_TRUE(plateau.hasValue());
        ASSERT_TRUE(slope.hasValue());
        ASSERT_TRUE(edge.hasValue());
        EXPECT_DOUBLE_EQ(*centre, 1.0);
        EXPECT_DOUBLE_EQ(*plateau, 1.0);
        EXPECT_DOUBLE_EQ(*slope, 0.75);
        EXPECT_DOUBLE_EQ(*edge, 0.0);
    }
    const an::Result<double> cubic = an::starMaskCoverage(an::StarMaskShape::Cubic, 1.5 * radius, radius);
    ASSERT_TRUE(cubic.hasValue());
    EXPECT_NEAR(*cubic, 1.0 - 0.125, 1e-12);
    const an::Result<double> quadratic = an::starMaskCoverage(an::StarMaskShape::Quadratic, 1.5 * radius, radius);
    ASSERT_TRUE(quadratic.hasValue());
    EXPECT_NEAR(*quadratic, 1.0 - 0.0625, 1e-12);

    EXPECT_EQ(an::starMaskCoverage(static_cast<an::StarMaskShape>(9), 0.0, 1.0).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::starMaskCoverage(an::StarMaskShape::Bell, -1.0, 1.0).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::starMaskCoverage(an::StarMaskShape::Bell, 1.0, 0.0).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(StarMaskTest, PaintsShapesAgainstTheirOwnClosedForm)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    const an::ImageLayout layout = maskLayoutOf(kWidth, kHeight);
    std::vector<float> mask(static_cast<std::size_t>(kWidth) * kHeight, 7.0f);

    const std::vector<an::DetectedStar> stars{makeStar(20.5, 24.5, 5.0, 4.0, 0.5)};
    const an::StarMaskShapeRequest request{.shape = an::StarMaskShape::TruncatedBell};
    const an::Result<an::StarMaskResult> painted =
        an::paintShapedStarMask(execution, stars, request, an::SampleType::Float32, mask.data(), layout);
    ASSERT_TRUE(painted.hasValue()) << painted.status().message();
    EXPECT_EQ(painted->paintedStars, 1u);
    EXPECT_EQ(painted->skippedStars, 0u);
    EXPECT_GT(painted->coveredSamples, 0u);

    // The painting covers the square of three radii about the centre and evaluates the shape at
    // every sample of it, which is the painted footprint; outside that square nothing is
    // written. A bell has not quite reached zero at three radii, so the square's edge is a step,
    // and the test states that rather than smoothing it over.
    const double radius = 5.0;
    const double reach = 3.0 * radius;
    double worst = 0.0;
    for (std::uint32_t row = 0u; row < kHeight; ++row)
    {
        for (std::uint32_t column = 0u; column < kWidth; ++column)
        {
            const double offsetX = static_cast<double>(column) + 0.5 - 20.5;
            const double offsetY = static_cast<double>(row) + 0.5 - 24.5;
            const bool inside = std::abs(offsetX) <= reach && std::abs(offsetY) <= reach;
            double wanted = 0.0;
            if (inside)
            {
                const an::Result<double> expected =
                    an::starMaskCoverage(an::StarMaskShape::TruncatedBell, std::hypot(offsetX, offsetY), radius);
                ASSERT_TRUE(expected.hasValue());
                wanted = *expected;
            }
            worst =
                (std::max)(worst, std::abs(static_cast<double>(mask[static_cast<std::size_t>(row) * kWidth + column]) -
                                           wanted));
        }
    }
    EXPECT_LT(worst, 1e-6);
}

TEST(StarMaskTest, MergesOverlappingStarsByTheLargestContribution)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 48u;
    constexpr std::uint32_t kHeight = 48u;
    const an::ImageLayout layout = maskLayoutOf(kWidth, kHeight);
    std::vector<float> both(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> reversed = both;

    std::vector<an::DetectedStar> stars{makeStar(20.5, 24.5, 6.0, 6.0, 0.5), makeStar(26.5, 24.5, 4.0, 4.0, 0.5)};
    const an::StarMaskShapeRequest request{.shape = an::StarMaskShape::Linear};
    ASSERT_TRUE(
        an::paintShapedStarMask(execution, stars, request, an::SampleType::Float32, both.data(), layout).hasValue());
    std::reverse(stars.begin(), stars.end());
    ASSERT_TRUE(an::paintShapedStarMask(execution, stars, request, an::SampleType::Float32, reversed.data(), layout)
                    .hasValue());
    EXPECT_EQ(both, reversed);

    // Where the two overlap, the sample is the larger of the two coverages rather than their sum.
    const std::size_t between = static_cast<std::size_t>(24u) * kWidth + 23u;
    const an::Result<double> left = an::starMaskCoverage(an::StarMaskShape::Linear, 3.0, 6.0);
    const an::Result<double> right = an::starMaskCoverage(an::StarMaskShape::Linear, 3.0, 4.0);
    ASSERT_TRUE(left.hasValue());
    ASSERT_TRUE(right.hasValue());
    EXPECT_NEAR(static_cast<double>(both[between]), (std::max)(*left, *right), 1e-6);
}

TEST(StarMaskTest, HonoursTheSizeWindowAndTheAdjustments)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    const an::ImageLayout layout = maskLayoutOf(kWidth, kHeight);
    std::vector<float> mask(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);

    const std::vector<an::DetectedStar> stars{makeStar(16.0, 16.0, 1.0, 1.0, 0.5),
                                              makeStar(48.0, 48.0, 40.0, 40.0, 0.5),
                                              makeStar(32.0, 32.0, 5.0, 5.0, 0.5)};
    const an::StarMaskShapeRequest window{.shape = an::StarMaskShape::Linear, .minimumSize = 4.0, .maximumSize = 25.0};
    const an::Result<an::StarMaskResult> painted =
        an::paintShapedStarMask(execution, stars, window, an::SampleType::Float32, mask.data(), layout);
    ASSERT_TRUE(painted.hasValue()) << painted.status().message();
    EXPECT_EQ(painted->paintedStars, 1u);
    EXPECT_EQ(painted->skippedStars, 2u);
    EXPECT_DOUBLE_EQ(static_cast<double>(mask[16u * kWidth + 16u]), 0.0);
    EXPECT_GT(static_cast<double>(mask[32u * kWidth + 32u]), 0.0);

    std::vector<float> grown(mask.size(), 0.0f);
    const an::StarMaskShapeRequest larger{
        .shape = an::StarMaskShape::Linear, .minimumSize = 4.0, .maximumSize = 25.0, .sizeScale = 1.5};
    ASSERT_TRUE(
        an::paintShapedStarMask(execution, stars, larger, an::SampleType::Float32, grown.data(), layout).hasValue());
    std::size_t small = 0u;
    std::size_t big = 0u;
    for (std::size_t index = 0u; index < mask.size(); ++index)
    {
        small += mask[index] > 0.0f ? 1u : 0u;
        big += grown[index] > 0.0f ? 1u : 0u;
    }
    EXPECT_GT(big, small);
}

TEST(StarMaskTest, PaintsSolidEllipsesAtTheStatedCoverage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    const an::ImageLayout layout = maskLayoutOf(kWidth, kHeight);
    std::vector<float> mask(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);

    // A round star: the ellipse is a disc of radius twice its full width.
    const std::vector<an::DetectedStar> stars{makeStar(48.0, 48.0, 5.0, 5.0, 0.5)};
    const an::Result<an::StarMaskResult> painted = an::paintEllipticalStarMask(
        execution, stars, an::StarMaskEllipseRequest{}, an::SampleType::Float32, mask.data(), layout);
    ASSERT_TRUE(painted.hasValue()) << painted.status().message();
    EXPECT_EQ(painted->paintedStars, 1u);
    const double expectedRadius = 0.5 * (5.0 * 4.0);
    const double expectedArea = std::numbers::pi * expectedRadius * expectedRadius;
    EXPECT_NEAR(static_cast<double>(painted->coveredSamples), expectedArea, 0.06 * expectedArea);
    EXPECT_DOUBLE_EQ(static_cast<double>(mask[48u * kWidth + 48u]), 1.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(mask[48u * kWidth + 4u]), 0.0);

    // An elongated star covers less, in the ratio of its axes.
    std::vector<float> narrow(mask.size(), 0.0f);
    const std::vector<an::DetectedStar> flat{makeStar(48.0, 48.0, 5.0, 2.5, 0.5)};
    const an::Result<an::StarMaskResult> flattened = an::paintEllipticalStarMask(
        execution, flat, an::StarMaskEllipseRequest{}, an::SampleType::Float32, narrow.data(), layout);
    ASSERT_TRUE(flattened.hasValue());
    EXPECT_NEAR(static_cast<double>(flattened->coveredSamples), 0.5 * expectedArea, 0.08 * expectedArea);
}

TEST(StarMaskTest, RefusesADestinationItCannotWrite)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = maskLayoutOf(32u, 32u);
    std::vector<float> mask(32u * 32u, 0.0f);
    const std::vector<an::DetectedStar> stars{makeStar(16.0, 16.0, 5.0, 5.0, 0.5)};
    const an::StarMaskShapeRequest request{};

    const an::ExecutionContext none;
    EXPECT_EQ(
        an::paintShapedStarMask(none, stars, request, an::SampleType::Float32, mask.data(), layout).status().code(),
        an::ErrorCode::NotInitialized);
    EXPECT_EQ(
        an::paintShapedStarMask(execution, stars, request, an::SampleType::Float32, nullptr, layout).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::paintShapedStarMask(execution, stars, request, an::SampleType::UInt16, mask.data(), layout).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::paintShapedStarMask(execution, stars, request, an::SampleType::Float64, mask.data(), layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    an::StarMaskShapeRequest badWindow = request;
    badWindow.maximumSize = 1.0;
    badWindow.minimumSize = 4.0;
    EXPECT_EQ(an::paintShapedStarMask(execution, stars, badWindow, an::SampleType::Float32, mask.data(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    an::StarMaskEllipseRequest badCoverage;
    badCoverage.coverageThreshold = 2.0;
    EXPECT_EQ(an::paintEllipticalStarMask(execution, stars, badCoverage, an::SampleType::Float32, mask.data(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(StarMaskTest, BothPaintingsAreCentredOnDetectedPixelCentres)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto layout = maskLayoutOf(33u, 33u);
    const std::vector<an::DetectedStar> stars{makeStar(16.5, 16.5, 3.0, 2.0, 0.5)};
    for (const bool ellipse : {false, true})
    {
        std::vector<float> mask(33u * 33u);
        const auto painted =
            ellipse ? an::paintEllipticalStarMask(execution, stars, {}, an::SampleType::Float32, mask.data(), layout)
                    : an::paintShapedStarMask(execution, stars, {}, an::SampleType::Float32, mask.data(), layout);
        ASSERT_TRUE(painted.hasValue());
        double mass = 0.0, momentX = 0.0, momentY = 0.0;
        for (std::uint32_t y = 0u; y < 33u; ++y)
            for (std::uint32_t x = 0u; x < 33u; ++x)
            {
                const double value = mask[y * 33u + x];
                mass += value;
                momentX += value * (static_cast<double>(x) + 0.5);
                momentY += value * (static_cast<double>(y) + 0.5);
            }
        ASSERT_GT(mass, 0.0);
        EXPECT_NEAR(momentX / mass, stars[0].positionX, 1.0e-12);
        EXPECT_NEAR(momentY / mass, stars[0].positionY, 1.0e-12);
    }
}
