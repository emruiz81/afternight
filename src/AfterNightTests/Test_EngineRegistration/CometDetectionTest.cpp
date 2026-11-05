// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Comet centre refinement and blind moving-object detection tests.

#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <vector>

namespace
{

using registration_test::descriptorOf;
using registration_test::layoutOf;
using registration_test::makeEngine;
using registration_test::makeExecution;

void addGaussian(std::vector<float>& pixels, const std::uint32_t width, const std::uint32_t height,
                 const double centerX, const double centerY, const double amplitude, const double sigma)
{
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = (static_cast<double>(x) + 0.5) - centerX;
            const double dy = (static_cast<double>(y) + 0.5) - centerY;
            pixels[static_cast<std::size_t>(y) * width + x] +=
                static_cast<float>(amplitude * std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma)));
        }
    }
}

[[nodiscard]] an::StarFrameView frameView(const std::vector<float>& pixels, const an::ImageLayout& layout,
                                          const an::ImageDescriptor& descriptor)
{
    const an::Result<an::StarFrameView> made =
        an::StarFrameView::create(pixels.data(), pixels.size() * sizeof(float), layout, descriptor);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::StarFrameView{};
}

[[nodiscard]] constexpr an::UtcInstant atSecond(const std::int64_t second) noexcept
{
    return an::UtcInstant{.nanosecondsSinceEpoch = second * 1000000000LL};
}

} // namespace

TEST(CometContractTest, DetectionDefaultsAreTheDocumentedPolicy)
{
    const an::CometMovingObjectDetectionOptions options;
    EXPECT_DOUBLE_EQ(options.minimumPositiveResidual, 1.0);
    EXPECT_DOUBLE_EQ(options.detectionSigma, 5.0);
    EXPECT_DOUBLE_EQ(options.minimumIntegratedSignalToNoise, 5.0);
    EXPECT_EQ(options.minimumComponentArea, 3u);
    EXPECT_EQ(options.maximumComponentArea, 256u);
    EXPECT_EQ(options.minimumDetections, 3u);
    EXPECT_DOUBLE_EQ(options.maximumTrackResidualPixels, 2.5);
    EXPECT_DOUBLE_EQ(options.minimumMotionPixels, 2.0);
    EXPECT_EQ(options.maximumCandidates, 8u);
}

TEST(CometCenterTest, ACompactSourceUsesTheGaussianRefinement)
{
    constexpr std::uint32_t width = 40u;
    constexpr std::uint32_t height = 36u;
    const an::ImageDescriptor descriptor =
        descriptorOf(width, height, 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> pixels(static_cast<std::size_t>(width) * height, 12.0F);
    addGaussian(pixels, width, height, 17.25, 19.75, 160.0, 1.35);

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometCenterRefinementResult> result =
        an::refineCometCenter(execution, frameView(pixels, layout, descriptor), {.x = 16.8, .y = 20.1},
                              {.roiRadius = 7u, .minimumSignalToNoise = 5.0, .maximumCompactFullWidth = 5.0});

    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->outcome, an::CometCenterOutcome::Refined);
    EXPECT_EQ(result->method, an::CometCenterMethod::GaussianPsf);
    EXPECT_NEAR(result->center.x, 17.25, 0.12);
    EXPECT_NEAR(result->center.y, 19.75, 0.12);
    EXPECT_GT(result->signalToNoise, 20.0);
    EXPECT_GT(result->apertureRadiusPixels, 2.0);

    const an::Result<an::CometCenterRefinementResult> forcedCentroid =
        an::refineCometCenter(execution, frameView(pixels, layout, descriptor), {.x = 16.8, .y = 20.1},
                              {.roiRadius = 7u, .minimumSignalToNoise = 5.0, .maximumCompactFullWidth = 1.0});
    ASSERT_TRUE(forcedCentroid.hasValue()) << forcedCentroid.status().message();
    EXPECT_EQ(forcedCentroid->method, an::CometCenterMethod::WeightedCentroid);
}

TEST(CometCenterTest, SaturationUsesTheWeightedCenterAndLowSignalIsReported)
{
    constexpr std::uint32_t width = 28u;
    constexpr std::uint32_t height = 28u;
    const an::ImageDescriptor descriptor =
        descriptorOf(width, height, 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> pixels(static_cast<std::size_t>(width) * height, 5.0F);
    addGaussian(pixels, width, height, 13.4, 12.8, 90.0, 1.6);

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometCenterRefinementResult> saturated =
        an::refineCometCenter(execution, frameView(pixels, layout, descriptor), {.x = 13.0, .y = 13.0},
                              {.roiRadius = 6u, .minimumSignalToNoise = 3.0, .saturationLevel = 60.0});
    ASSERT_TRUE(saturated.hasValue()) << saturated.status().message();
    EXPECT_EQ(saturated->method, an::CometCenterMethod::WeightedCentroid);
    EXPECT_TRUE(saturated->saturated);

    const an::Result<an::CometCenterRefinementResult> quiet =
        an::refineCometCenter(execution, frameView(pixels, layout, descriptor), {.x = 3.5, .y = 3.5},
                              {.roiRadius = 2u, .minimumSignalToNoise = 1000.0});
    ASSERT_TRUE(quiet.hasValue()) << quiet.status().message();
    EXPECT_EQ(quiet->outcome, an::CometCenterOutcome::BelowThreshold);
    EXPECT_EQ(quiet->method, an::CometCenterMethod::RoiCenter);
}

TEST(CometCenterTest, InvalidGeometryAndCancellationAreReturnedBeforeWork)
{
    const an::ImageDescriptor descriptor =
        descriptorOf(8u, 8u, 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::vector<float> pixels(64u, 1.0F);
    an::EngineContext engine = makeEngine();

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext cancelled = makeExecution(engine, 2u, stop.get_token());
    const an::Result<an::CometCenterRefinementResult> stopped =
        an::refineCometCenter(cancelled, frameView(pixels, layout, descriptor), {.x = 4.5, .y = 4.5});
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);

    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometCenterRefinementResult> outside =
        an::refineCometCenter(execution, frameView(pixels, layout, descriptor), {.x = -0.5, .y = 4.5});
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::OutOfRange);

    const an::Result<an::CometCenterRefinementResult> zeroRadius = an::refineCometCenter(
        execution, frameView(pixels, layout, descriptor), {.x = 4.5, .y = 4.5}, {.roiRadius = 0u});
    ASSERT_TRUE(zeroRadius.hasValue()) << zeroRadius.status().message();

    const an::Result<an::CometCenterRefinementResult> oversized =
        an::refineCometCenter(execution, frameView(pixels, layout, descriptor), {.x = 4.5, .y = 4.5},
                              {.roiRadius = an::kMaximumCometCenterRadius + 1u});
    ASSERT_FALSE(oversized.hasValue());
    EXPECT_EQ(oversized.status().code(), an::ErrorCode::InvalidArgument);

    const std::vector<float> zeros(64u, 0.0F);
    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroRule.has_value());
    const an::Result<an::StarFrameView> zeroFrame =
        an::StarFrameView::create(zeros.data(), layout.totalBytes(), layout, descriptor, {.descriptor = *zeroRule});
    ASSERT_TRUE(zeroFrame.hasValue()) << zeroFrame.status().message();
    const an::Result<an::CometCenterRefinementResult> empty =
        an::refineCometCenter(execution, *zeroFrame, {.x = 4.5, .y = 4.5}, {.roiRadius = 2u});
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);
}

TEST(CometCenterTest, EveryResidentRepresentationIsReadThroughItsDeclaredView)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const auto verify = [&]<typename SampleT>(const an::SampleType sampleType) {
        constexpr std::uint32_t width = 17u;
        constexpr std::uint32_t height = 17u;
        const an::ImageDescriptor descriptor =
            descriptorOf(width, height, 1u, sampleType, an::NumericDomain::RawSensorCode);
        const an::ImageLayout layout = layoutOf(descriptor);
        std::vector<SampleT> pixels(static_cast<std::size_t>(width) * height, static_cast<SampleT>(4));
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double dx = static_cast<double>(x) - 8.0;
                const double dy = static_cast<double>(y) - 8.0;
                const double signal = 88.0 * std::exp(-(dx * dx + dy * dy) / (2.0 * 1.25 * 1.25));
                pixels[static_cast<std::size_t>(y) * width + x] = static_cast<SampleT>(4.0 + signal);
            }
        }
        const an::Result<an::StarFrameView> view =
            an::StarFrameView::create(pixels.data(), pixels.size() * sizeof(SampleT), layout, descriptor);
        ASSERT_TRUE(view.hasValue()) << view.status().message();
        const an::Result<an::CometCenterRefinementResult> result = an::refineCometCenter(
            execution, *view, {.x = 8.5, .y = 8.5}, {.roiRadius = 6u, .minimumSignalToNoise = 3.0});
        ASSERT_TRUE(result.hasValue()) << an::sampleTypeName(sampleType) << ": " << result.status().message();
        EXPECT_EQ(result->outcome, an::CometCenterOutcome::Refined) << an::sampleTypeName(sampleType);
        EXPECT_NEAR(result->center.x, 8.5, 0.1) << an::sampleTypeName(sampleType);
        EXPECT_NEAR(result->center.y, 8.5, 0.1) << an::sampleTypeName(sampleType);
    };

    verify.template operator()<std::uint8_t>(an::SampleType::UInt8);
    verify.template operator()<std::int8_t>(an::SampleType::Int8);
    verify.template operator()<std::uint16_t>(an::SampleType::UInt16);
    verify.template operator()<std::int16_t>(an::SampleType::Int16);
    verify.template operator()<std::uint32_t>(an::SampleType::UInt32);
    verify.template operator()<std::int32_t>(an::SampleType::Int32);
    verify.template operator()<float>(an::SampleType::Float32);
    verify.template operator()<double>(an::SampleType::Float64);
}

TEST(CometDetectionTest, ACoherentMovingSourceWinsOverStarsHotPixelsAndNoise)
{
    constexpr std::uint32_t width = 48u;
    constexpr std::uint32_t height = 40u;
    constexpr std::size_t frameCount = 5u;
    const an::ImageDescriptor descriptor =
        descriptorOf(width, height, 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear);
    const an::ImageLayout layout = layoutOf(descriptor);

    std::vector<std::vector<float>> storage(frameCount,
                                            std::vector<float>(static_cast<std::size_t>(width) * height, 20.0F));
    std::vector<an::CometDetectionFrame> frames;
    frames.reserve(frameCount);
    for (std::size_t frame = 0u; frame < frameCount; ++frame)
    {
        for (std::size_t index = 0u; index < storage[frame].size(); ++index)
        {
            storage[frame][index] += static_cast<float>((index * 17u + frame * 13u) % 11u) * 0.035F;
        }
        addGaussian(storage[frame], width, height, 9.5, 9.5, 70.0, 1.2);
        addGaussian(storage[frame], width, height, 34.5, 27.5, 55.0, 1.4);
        if (frame != 2u)
        {
            addGaussian(storage[frame], width, height, 12.5 + 3.0 * static_cast<double>(frame),
                        28.5 - 1.5 * static_cast<double>(frame), 95.0, 1.15);
        }
        storage[frame][static_cast<std::size_t>(4u + frame) * width + (42u - static_cast<std::uint32_t>(frame))] =
            180.0F;
        frames.push_back({.frameId = 100u + frame,
                          .midpoint = atSecond(static_cast<std::int64_t>(frame) * 10),
                          .frame = frameView(storage[frame], layout, descriptor),
                          .required = true});
    }

    an::EngineContext engine = makeEngine(8u);
    an::ExecutionContext execution = makeExecution(engine, 4u);
    const an::Result<an::CometMovingObjectDetectionResult> result =
        an::detectMovingCometCandidates(execution, frames,
                                        {.minimumPositiveResidual = 8.0,
                                         .detectionSigma = 4.0,
                                         .minimumComponentArea = 3u,
                                         .maximumComponentArea = 40u,
                                         .minimumDetections = 4u,
                                         .maximumTrackResidualPixels = 1.5,
                                         .minimumMotionPixels = 5.0,
                                         .maximumCandidates = 8u});

    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_FALSE(result->candidates.empty());
    const an::CometMovingObjectCandidate& best = result->candidates.front();
    EXPECT_GE(best.points.size(), 4u);
    EXPECT_GT(best.motionPixels, 10.0);
    EXPECT_LT(best.rootMeanSquareResidualPixels, 0.7);
    EXPECT_NEAR(best.points.front().position.x, 12.5, 0.6);
    EXPECT_NEAR(best.points.front().position.y, 28.5, 0.6);
    EXPECT_GT(best.points.front().integratedSignal, 0.0);
    EXPECT_GT(best.meanIntegratedSignalToNoise, 5.0);
    EXPECT_NEAR(best.persistence, 0.8, 0.05);
    EXPECT_EQ(result->frameDiagnostics.size(), frameCount);

    const an::Result<an::CometMovingObjectDetectionResult> filtered =
        an::detectMovingCometCandidates(execution, frames,
                                        {.minimumPositiveResidual = 8.0,
                                         .detectionSigma = 4.0,
                                         .minimumIntegratedSignalToNoise = 1.0e12,
                                         .minimumComponentArea = 3u,
                                         .maximumComponentArea = 40u,
                                         .minimumDetections = 1u,
                                         .maximumTrackResidualPixels = 1.5,
                                         .minimumMotionPixels = 5.0,
                                         .maximumCandidates = 0u});
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_TRUE(filtered->candidates.empty());

    an::EngineContext boundedEngine = makeEngine(2u, 4096u);
    an::ExecutionContext boundedExecution = makeExecution(boundedEngine, 2u);
    const an::Result<an::CometMovingObjectDetectionResult> memoryRefused =
        an::detectMovingCometCandidates(boundedExecution, frames);
    ASSERT_FALSE(memoryRefused.hasValue());
    EXPECT_EQ(memoryRefused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST(CometDetectionTest, OptionalMissingFramesAreSkippedAndRequiredOnesAreRefused)
{
    const an::CometDetectionFrame optionalMissing{
        .frameId = 7u, .midpoint = atSecond(0), .frame = {}, .required = false};
    const an::CometDetectionFrame requiredMissing{
        .frameId = 8u, .midpoint = atSecond(1), .frame = {}, .required = true};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::Result<an::CometMovingObjectDetectionResult> noFrames =
        an::detectMovingCometCandidates(execution, std::span<const an::CometDetectionFrame>(&optionalMissing, 1u));
    ASSERT_FALSE(noFrames.hasValue());
    EXPECT_EQ(noFrames.status().code(), an::ErrorCode::EmptySelection);

    const an::Result<an::CometMovingObjectDetectionResult> missing =
        an::detectMovingCometCandidates(execution, std::span<const an::CometDetectionFrame>(&requiredMissing, 1u));
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CometDetectionTest, CancellationPrecedesEvenAnEmptySequence)
{
    an::EngineContext engine = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext execution = makeExecution(engine, 2u, stop.get_token());

    const an::Result<an::CometMovingObjectDetectionResult> cancelled = an::detectMovingCometCandidates(execution, {});
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);

    an::ExecutionContext ready = makeExecution(engine);
    std::vector<an::CometDetectionFrame> tooManyFrames(static_cast<std::size_t>(an::kMaximumCometDetectionFrames) + 1u);
    const an::Result<an::CometMovingObjectDetectionResult> frameLimit =
        an::detectMovingCometCandidates(ready, tooManyFrames);
    ASSERT_FALSE(frameLimit.hasValue());
    EXPECT_EQ(frameLimit.status().code(), an::ErrorCode::LimitExceeded);

    const an::Result<an::CometMovingObjectDetectionResult> candidateLimit =
        an::detectMovingCometCandidates(ready, {}, {.maximumCandidates = an::kMaximumCometCandidates + 1u});
    ASSERT_FALSE(candidateLimit.hasValue());
    EXPECT_EQ(candidateLimit.status().code(), an::ErrorCode::LimitExceeded);
}
