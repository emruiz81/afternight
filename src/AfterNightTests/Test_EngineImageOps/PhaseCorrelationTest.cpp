// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the translation measurement.
//
// The synthetic cases exercise the algorithm's whole promise: an exact cyclic shift is recovered to
// within numeric noise because the mathematics is exact for it, a windowed realistic shift is
// recovered to a fraction of a pixel, a sub-pixel displacement lands between the integers, and the
// degenerate inputs - identical frames, structureless frames, unrelated frames - report their
// nature through `response` rather than through a wrong number that looks like a shift. The
// refusals pin the stated input domain: one channel, one extent, eight pixels a side, numbers.

#include <AfterNightEngine/Algorithms/Registration/PhaseCorrelation.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stop_token>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// A frame of soft synthetic stars at stated positions.
///
/// Each star is a separable raised profile, so a fractional position moves real energy between
/// pixels and a sub-pixel measurement has something true to find.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param offsetX how far every star is displaced to the right.
/// @param offsetY how far every star is displaced down.
/// @return the samples, row-major.
[[nodiscard]] std::vector<float> starField(std::uint32_t width, std::uint32_t height, double offsetX, double offsetY)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.05f);
    const double centres[][2] = {{14.0, 11.0}, {37.0, 9.0}, {22.0, 30.0}, {45.0, 41.0}, {9.0, 44.0}, {52.0, 24.0}};
    for (const auto& centre : centres)
    {
        const double starX = centre[0] + offsetX;
        const double starY = centre[1] + offsetY;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double dx = static_cast<double>(x) - starX;
                const double dy = static_cast<double>(y) - starY;
                const double radius = dx * dx + dy * dy;
                if (radius < 16.0)
                {
                    samples[static_cast<std::size_t>(y) * width + x] +=
                        static_cast<float>(0.8 * std::exp(-radius / 3.0));
                }
            }
        }
    }
    return samples;
}

TEST(PhaseCorrelationTest, AnExactCyclicShiftIsMeasuredToNumericNoise)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 64u;
    const std::vector<float> reference = randomPlane(kSide, kSide, 17u);
    // The target's content sits five pixels right and three up of the reference's, wrapping at the
    // frame edge - the one displacement the transform's periodic world describes exactly.
    std::vector<float> target(reference.size(), 0.0f);
    for (std::uint32_t y = 0u; y < kSide; ++y)
    {
        for (std::uint32_t x = 0u; x < kSide; ++x)
        {
            const std::uint32_t fromX = (x + kSide - 5u) % kSide;
            const std::uint32_t fromY = (y + 3u) % kSide;
            target[static_cast<std::size_t>(y) * kSide + x] =
                reference[static_cast<std::size_t>(fromY) * kSide + fromX];
        }
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);

    const an::Result<an::PhaseCorrelationResult> measured = an::computeImagePhaseCorrelation(
        execution, an::SampleType::Float32, reference.data(), layout, target.data(), layout,
        an::PhaseCorrelationRequest{.subtractMean = true, .window = an::PhaseCorrelationWindow::None});
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    // The content moved (+5, -3), so the translation that takes the target back is (-5, +3).
    EXPECT_NEAR(measured->translationX, -5.0, 0.05);
    EXPECT_NEAR(measured->translationY, 3.0, 0.05);
    EXPECT_GT(measured->response, 0.9);
    EXPECT_GT(measured->peakValue, 0.8);
    EXPECT_GT(measured->workingBytes, 0u);
    EXPECT_GE(measured->plannedWorkers, 1u);
    EXPECT_EQ(measured->precision, an::ComputePrecision::Float32);
}

TEST(PhaseCorrelationTest, TheHannWindowRecoversARealisticShift)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 64u;
    const std::vector<float> reference = starField(kSide, kSide, 0.0, 0.0);
    const std::vector<float> target = starField(kSide, kSide, 3.0, -2.0);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);

    const an::Result<an::PhaseCorrelationResult> measured = an::computeImagePhaseCorrelation(
        execution, an::SampleType::Float32, reference.data(), layout, target.data(), layout);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(measured->translationX, -3.0, 0.25);
    EXPECT_NEAR(measured->translationY, 2.0, 0.25);
    EXPECT_GT(measured->response, 0.2);
}

TEST(PhaseCorrelationTest, ASubPixelShiftLandsBetweenTheBracketingIntegers)
{
    // The refinement is the raw weighted centroid of the five-by-five
    // neighbourhood, whose negative sidelobes cancel most of a half-pixel peak split. What it
    // promises - and all it promises - is a fraction on the right side of the right integer, and
    // that is what this pins; a sharper estimator would be a different, stated algorithm.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 64u;
    const std::vector<float> reference = starField(kSide, kSide, 0.0, 0.0);
    const std::vector<float> target = starField(kSide, kSide, 2.5, 0.0);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);

    const an::Result<an::PhaseCorrelationResult> measured = an::computeImagePhaseCorrelation(
        execution, an::SampleType::Float32, reference.data(), layout, target.data(), layout);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_GE(measured->translationX, -3.0);
    EXPECT_LE(measured->translationX, -2.0);
    EXPECT_NEAR(measured->translationY, 0.0, 0.2);
    EXPECT_GT(std::abs(measured->translationX - std::round(measured->translationX)), 1.0e-3)
        << "the refinement produced a fraction rather than an exact integer";
}

TEST(PhaseCorrelationTest, IdenticalFramesMeasureZeroWithFullResponse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 48u;
    const std::vector<float> frame = starField(kSide, kSide, 0.0, 0.0);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);

    const an::Result<an::PhaseCorrelationResult> measured = an::computeImagePhaseCorrelation(
        execution, an::SampleType::Float32, frame.data(), layout, frame.data(), layout);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(measured->translationX, 0.0, 0.01);
    EXPECT_NEAR(measured->translationY, 0.0, 0.01);
    EXPECT_GT(measured->response, 0.95);
    EXPECT_EQ(measured->peakX, kSide / 2u);
    EXPECT_EQ(measured->peakY, kSide / 2u);
}

TEST(PhaseCorrelationTest, AStructurelessPairReportsAResponseOfZero)
{
    // A whitening that divides an empty spectrum by itself fails this input with a non-finite
    // translation error. Here the empty bin is stated to contribute nothing, so the measurement
    // succeeds and says there was nothing to measure.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 32u;
    const std::vector<float> flat(static_cast<std::size_t>(kSide) * kSide, 0.625f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);

    const an::Result<an::PhaseCorrelationResult> measured =
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, flat.data(), layout, flat.data(), layout);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_EQ(measured->response, 0.0);
    EXPECT_EQ(measured->peakValue, 0.0);
    EXPECT_TRUE(std::isfinite(measured->translationX));
    EXPECT_TRUE(std::isfinite(measured->translationY));
}

TEST(PhaseCorrelationTest, UnrelatedFramesReportALowResponse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 64u;
    const std::vector<float> one = randomPlane(kSide, kSide, 7u);
    const std::vector<float> other = randomPlane(kSide, kSide, 8u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);

    const an::Result<an::PhaseCorrelationResult> measured =
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, one.data(), layout, other.data(), layout);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_LT(measured->response, 0.2);
}

TEST(PhaseCorrelationTest, TheMeasurementMeansTheSameThingInCodeValuesAsInFloat)
{
    // The same content as UInt16 codes and as their exact Float32 rendering widens to identical
    // working planes, so the two measurements must agree exactly.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 32u;
    const std::vector<float> shape = starField(kSide, kSide, 0.0, 0.0);
    const std::vector<float> shifted = starField(kSide, kSide, 2.0, 1.0);
    std::vector<std::uint16_t> narrowReference(shape.size(), 0u);
    std::vector<std::uint16_t> narrowTarget(shape.size(), 0u);
    std::vector<float> wideReference(shape.size(), 0.0f);
    std::vector<float> wideTarget(shape.size(), 0.0f);
    for (std::size_t index = 0u; index < shape.size(); ++index)
    {
        narrowReference[index] = static_cast<std::uint16_t>(shape[index] * 30000.0f);
        narrowTarget[index] = static_cast<std::uint16_t>(shifted[index] * 30000.0f);
        wideReference[index] = static_cast<float>(narrowReference[index]);
        wideTarget[index] = static_cast<float>(narrowTarget[index]);
    }
    const an::ImageLayout narrowLayout = planarLayout(an::SampleType::UInt16, kSide, kSide);
    const an::ImageLayout wideLayout = planarLayout(an::SampleType::Float32, kSide, kSide);

    const an::Result<an::PhaseCorrelationResult> narrow = an::computeImagePhaseCorrelation(
        execution, an::SampleType::UInt16, narrowReference.data(), narrowLayout, narrowTarget.data(), narrowLayout);
    const an::Result<an::PhaseCorrelationResult> wide = an::computeImagePhaseCorrelation(
        execution, an::SampleType::Float32, wideReference.data(), wideLayout, wideTarget.data(), wideLayout);
    ASSERT_TRUE(narrow.hasValue()) << narrow.status().message();
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_EQ(narrow->translationX, wide->translationX);
    EXPECT_EQ(narrow->translationY, wide->translationY);
    EXPECT_EQ(narrow->response, wide->response);
}

TEST(PhaseCorrelationTest, TheInputDomainIsRefusedByName)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> frame(static_cast<std::size_t>(16u) * 16u * 3u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 16u, 16u);

    // A null frame on either side.
    EXPECT_EQ(
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, nullptr, layout, frame.data(), layout)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, frame.data(), layout, nullptr, layout)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
    // A colour frame.
    const an::ImageLayout colour = planarLayout(an::SampleType::Float32, 16u, 16u, 3u);
    EXPECT_EQ(
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, frame.data(), colour, frame.data(), colour)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
    // Two different extents.
    const an::ImageLayout narrower = planarLayout(an::SampleType::Float32, 12u, 16u);
    EXPECT_EQ(an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, frame.data(), layout, frame.data(),
                                               narrower)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // A frame below the stated floor.
    const an::ImageLayout tiny = planarLayout(an::SampleType::Float32, 7u, 16u);
    EXPECT_EQ(
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, frame.data(), tiny, frame.data(), tiny)
            .status()
            .code(),
        an::ErrorCode::OutOfRange);
    // An unknown window.
    EXPECT_EQ(an::computeImagePhaseCorrelation(
                  execution, an::SampleType::Float32, frame.data(), layout, frame.data(), layout,
                  an::PhaseCorrelationRequest{.window = static_cast<an::PhaseCorrelationWindow>(9)})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(PhaseCorrelationTest, ASampleThatIsNotANumberIsRefusedRatherThanTransformed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 16u;
    std::vector<float> frame(static_cast<std::size_t>(kSide) * kSide, 0.5f);
    frame[40] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);

    EXPECT_EQ(
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, frame.data(), layout, frame.data(), layout)
            .status()
            .code(),
        an::ErrorCode::InvalidData);
}

TEST(PhaseCorrelationTest, ACancelledOperationStopsAtACheckpoint)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    stop.request_stop();

    const std::vector<float> frame(static_cast<std::size_t>(16u) * 16u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 16u, 16u);
    EXPECT_EQ(an::computeImagePhaseCorrelation(*execution, an::SampleType::Float32, frame.data(), layout, frame.data(),
                                               layout)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
}

TEST(PhaseCorrelationTest, TheTypedSpellingForwardsToTheUntypedEntryPoint)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 32u;
    const std::vector<float> reference = starField(kSide, kSide, 0.0, 0.0);
    const std::vector<float> target = starField(kSide, kSide, 1.0, 0.0);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);
    const std::optional<an::ImageView<const float>> referenceView =
        an::ImageView<const float>::create(std::span<const float>{reference.data(), reference.size()}, layout);
    const std::optional<an::ImageView<const float>> targetView =
        an::ImageView<const float>::create(std::span<const float>{target.data(), target.size()}, layout);
    ASSERT_TRUE(referenceView.has_value());
    ASSERT_TRUE(targetView.has_value());

    const an::Result<an::PhaseCorrelationResult> measured =
        an::computePhaseCorrelation<float>(execution, *referenceView, *targetView);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(measured->translationX, -1.0, 0.25);
}

TEST(PhaseCorrelationTest, OddExtentsAreServedUnderTheSameContract)
{
    // The transform library serves any length, so an awkward extent is a slower measurement rather
    // than a different one. This pins the engine's own centring convention for odd sides.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 45u;
    constexpr std::uint32_t kHeight = 33u;
    const std::vector<float> reference = randomPlane(kWidth, kHeight, 29u);
    std::vector<float> target(reference.size(), 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::uint32_t fromX = (x + kWidth - 4u) % kWidth;
            target[static_cast<std::size_t>(y) * kWidth + x] = reference[static_cast<std::size_t>(y) * kWidth + fromX];
        }
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::Result<an::PhaseCorrelationResult> measured = an::computeImagePhaseCorrelation(
        execution, an::SampleType::Float32, reference.data(), layout, target.data(), layout,
        an::PhaseCorrelationRequest{.window = an::PhaseCorrelationWindow::None});
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(measured->translationX, -4.0, 0.1);
    EXPECT_NEAR(measured->translationY, 0.0, 0.1);
}

} // namespace
