// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for detecting stars channel by channel under a stated policy.
//
// The frames are colour fields whose channels are planted independently: stars in every channel, stars
// in red alone - a frame through a filter that leaves green dark - and stars in none. The policy's
// decision is then held to what the overlay decided: green first, red when green gives too few, the
// better of the two kept, and the first of equal counts.

#include <AfterNightEngine/Algorithms/Stars/StarDetectionChannels.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include "StarFieldFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 384u;
constexpr std::uint32_t kHeight = 384u;

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 8u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 4u,
                                                 std::stop_token stopToken = {}, an::ProgressSink* progress = nullptr)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    options.progressSink = progress;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageDescriptor descriptorOf(std::uint32_t channels)
{
    const an::ChannelSchema schema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(kWidth, kHeight, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{
        .extent = extent.value_or(an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits()).value()),
        .sampleType = an::SampleType::Float32,
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

[[nodiscard]] an::ImageLayout layoutOf(const an::ImageDescriptor& descriptor)
{
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(descriptor, an::StorageArrangement::Planar, 1u);
    EXPECT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// One plane: a planted field with stars, or the same sky with none.
[[nodiscard]] std::vector<float> plane(bool stars, std::uint64_t seed)
{
    an::test::StarFieldRequest request;
    request.width = kWidth;
    request.height = kHeight;
    request.cell = stars ? 40u : kWidth * 2u;
    request.seed = seed;
    return an::test::makeStarField(request).samples;
}

/// A frame whose planes are planted as stated, and the layout and descriptor it is read through.
struct Frame
{
    std::vector<float> samples;
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;

    Frame(std::initializer_list<bool> planted)
        : descriptor(descriptorOf(static_cast<std::uint32_t>(planted.size()))), layout(layoutOf(descriptor))
    {
        std::uint64_t seed = 11u;
        for (const bool stars : planted)
        {
            const std::vector<float> one = plane(stars, seed++);
            samples.insert(samples.end(), one.begin(), one.end());
        }
    }

    [[nodiscard]] an::StarFrameView view() const
    {
        an::Result<an::StarFrameView> made =
            an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor);
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? *made : an::StarFrameView{};
    }
};

/// A tangent-plane solution of the test frames, about an arcsecond a pixel.
[[nodiscard]] an::WcsSolution solutionOf(std::uint32_t width, std::uint32_t height,
                                         const an::SipDistortion& distortion = {})
{
    const an::Result<an::WcsSolution> made = an::WcsSolution::create(
        an::WcsSolutionRequest{.referencePixel = {.x = width / 2.0, .y = height / 2.0},
                               .referencePoint = {.rightAscensionDegrees = 83.8, .declinationDegrees = -5.4},
                               .linear = {.cd11 = -2.8e-4, .cd12 = 0.0, .cd21 = 0.0, .cd22 = 2.8e-4},
                               .projection = an::WcsProjection::Gnomonic,
                               .imageWidth = width,
                               .imageHeight = height,
                               .distortion = distortion});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// The overlay's own detection parameters.
[[nodiscard]] an::StarDetectionRequest overlayRequest()
{
    an::StarDetectionRequest request;
    request.searchRadius = 5u;
    request.noiseSigma = 1.0;
    request.minimumRoundness = 0.4;
    request.maximumStars = 5000u;
    return request;
}

/// A progress sink that keeps every report in order.
class RecordingProgress final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        m_reports.emplace_back(report.completed, report.total);
    }

    [[nodiscard]] const std::vector<std::pair<std::uint64_t, std::uint64_t>>& reports() const noexcept
    {
        return m_reports;
    }

private:
    std::vector<std::pair<std::uint64_t, std::uint64_t>> m_reports;
};

} // namespace

TEST(StarDetectionChannelsTest, TheOverlayPolicyTriesGreenThenRed)
{
    const an::StarDetectionChannelPolicy colour = an::greenThenRedChannelPolicy(descriptorOf(3u));
    EXPECT_EQ(colour.channels, (std::vector<std::uint32_t>{1u, 0u}));
    EXPECT_EQ(colour.sufficientStars, an::kDefaultSufficientDetectionStars);
    EXPECT_EQ(an::kDefaultSufficientDetectionStars, 25u);

    const an::StarDetectionChannelPolicy mono = an::greenThenRedChannelPolicy(descriptorOf(1u));
    EXPECT_EQ(mono.channels, (std::vector<std::uint32_t>{0u}));
    EXPECT_EQ(mono.sufficientStars, 25u);
}

TEST(StarDetectionChannelsTest, StopsAtTheFirstChannelThatGivesEnough)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Frame frame{true, true, true};
    const an::WcsSolution solution = solutionOf(kWidth, kHeight);

    const an::Result<an::StarDetectionChannelResult> result = an::detectStarsByChannelPolicy(
        execution, frame.view(), overlayRequest(), an::greenThenRedChannelPolicy(frame.descriptor), &solution);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->channel, 1u);
    EXPECT_TRUE(result->sufficient);
    ASSERT_EQ(result->attempts.size(), 1u);
    EXPECT_EQ(result->attempts[0].channel, 1u);
    EXPECT_GE(result->attempts[0].detectedStars, 25u);
    EXPECT_EQ(result->attempts[0].countedStars, result->attempts[0].detectedStars);
    EXPECT_EQ(result->detection.stars.size(), result->attempts[0].detectedStars);

    // The kept attempt is exactly the detection of its channel.
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(overlayRequest(), frame.descriptor, frame.layout, 1u);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    an::StarDetectionResult direct;
    ASSERT_TRUE(an::detectStars(execution, frame.view(), *plan, direct).ok());
    ASSERT_EQ(direct.stars.size(), result->detection.stars.size());
    for (std::size_t index = 0u; index < direct.stars.size(); ++index)
    {
        EXPECT_EQ(direct.stars[index].positionX, result->detection.stars[index].positionX);
        EXPECT_EQ(direct.stars[index].positionY, result->detection.stars[index].positionY);
    }
}

TEST(StarDetectionChannelsTest, TriesTheNextChannelWhenOneGivesTooFewAndKeepsTheBetter)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Frame frame{true, false, false};

    const an::Result<an::StarDetectionChannelResult> result = an::detectStarsByChannelPolicy(
        execution, frame.view(), overlayRequest(), an::greenThenRedChannelPolicy(frame.descriptor));
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_EQ(result->attempts.size(), 2u);
    EXPECT_EQ(result->attempts[0].channel, 1u);
    EXPECT_LT(result->attempts[0].countedStars, 25u);
    EXPECT_EQ(result->attempts[1].channel, 0u);
    EXPECT_GE(result->attempts[1].countedStars, 25u);
    EXPECT_EQ(result->channel, 0u);
    EXPECT_TRUE(result->sufficient);
    EXPECT_EQ(result->detection.stars.size(), result->attempts[1].detectedStars);
}

TEST(StarDetectionChannelsTest, KeepsTheFirstOfEqualCountsWhenNoChannelGivesEnough)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Frame frame{false, false, false};

    const an::Result<an::StarDetectionChannelResult> result = an::detectStarsByChannelPolicy(
        execution, frame.view(), overlayRequest(), an::greenThenRedChannelPolicy(frame.descriptor));
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_EQ(result->attempts.size(), 2u);
    EXPECT_EQ(result->attempts[0].countedStars, result->attempts[1].countedStars);
    EXPECT_EQ(result->channel, 1u);
    EXPECT_FALSE(result->sufficient);

    // A threshold of zero is met by any attempt, so the search stops after the first.
    an::StarDetectionChannelPolicy eager = an::greenThenRedChannelPolicy(frame.descriptor);
    eager.sufficientStars = 0u;
    const an::Result<an::StarDetectionChannelResult> first =
        an::detectStarsByChannelPolicy(execution, frame.view(), overlayRequest(), eager);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(first->attempts.size(), 1u);
    EXPECT_TRUE(first->sufficient);

    // A one-channel frame tries its one channel.
    const Frame mono{true};
    const an::Result<an::StarDetectionChannelResult> single = an::detectStarsByChannelPolicy(
        execution, mono.view(), overlayRequest(), an::greenThenRedChannelPolicy(mono.descriptor));
    ASSERT_TRUE(single.hasValue()) << single.status().message();
    ASSERT_EQ(single->attempts.size(), 1u);
    EXPECT_EQ(single->channel, 0u);
}

TEST(StarDetectionChannelsTest, CountsOnlyTheStarsTheSolutionPlacesOnTheSky)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Frame frame{true, true, true};

    // A distortion that overflows away from the reference pixel leaves those stars without a direction.
    an::SipDistortionRequest wild;
    wild.referencePixel = {.x = kWidth / 2.0, .y = kHeight / 2.0};
    wild.forwardX.order = 2u;
    wild.forwardX.coefficients[2][0] = 1e305;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(wild);
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();
    const an::WcsSolution solution = solutionOf(kWidth, kHeight, *distortion);

    an::StarDetectionChannelPolicy policy{.channels = {1u}, .sufficientStars = 25u};
    const an::Result<an::StarDetectionChannelResult> result =
        an::detectStarsByChannelPolicy(execution, frame.view(), overlayRequest(), policy, &solution);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_EQ(result->attempts.size(), 1u);
    std::uint64_t placed = 0u;
    for (const an::DetectedStar& star : result->detection.stars)
    {
        placed += solution.pixelToWorld({.x = star.positionX, .y = star.positionY}).hasValue() ? 1u : 0u;
    }
    EXPECT_GT(result->attempts[0].detectedStars, placed);
    EXPECT_GT(placed, 0u);
    EXPECT_EQ(result->attempts[0].countedStars, placed);
}

TEST(StarDetectionChannelsTest, RefusesAPolicyOrAFrameItCannotUse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Frame frame{true, true, true};

    const auto run = [&](const an::StarDetectionChannelPolicy& policy, const an::WcsSolution* solution = nullptr) {
        return an::detectStarsByChannelPolicy(execution, frame.view(), overlayRequest(), policy, solution);
    };
    EXPECT_EQ(run(an::StarDetectionChannelPolicy{.channels = {}}).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(run(an::StarDetectionChannelPolicy{.channels = {1u, 0u, 1u}}).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(run(an::StarDetectionChannelPolicy{.channels = {1u, 3u}}).code(), an::ErrorCode::OutOfRange);

    const an::WcsSolution other = solutionOf(kWidth * 2u, kHeight);
    EXPECT_EQ(run(an::greenThenRedChannelPolicy(frame.descriptor), &other).code(),
              an::ErrorCode::IncompatibleArguments);

    an::StarDetectionRequest bad = overlayRequest();
    bad.searchRadius = 0u;
    EXPECT_EQ(
        an::detectStarsByChannelPolicy(execution, frame.view(), bad, an::greenThenRedChannelPolicy(frame.descriptor))
            .code(),
        an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::detectStarsByChannelPolicy(execution, an::StarFrameView{}, overlayRequest(),
                                             an::greenThenRedChannelPolicy(frame.descriptor))
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::detectStarsByChannelPolicy(an::ExecutionContext{}, frame.view(), overlayRequest(),
                                             an::greenThenRedChannelPolicy(frame.descriptor))
                  .code(),
              an::ErrorCode::NotInitialized);
}

TEST(StarDetectionChannelsTest, ReportsProgressAcrossItsAttemptsAndFinishesTheTotal)
{
    const an::EngineContext engine = makeEngine();
    const Frame frame{true, true, true};

    // A search that stops after one of two channels still reaches its total.
    RecordingProgress progress;
    const an::ExecutionContext execution = makeExecution(engine, 4u, {}, &progress);
    const an::Result<an::StarDetectionChannelResult> result = an::detectStarsByChannelPolicy(
        execution, frame.view(), overlayRequest(), an::greenThenRedChannelPolicy(frame.descriptor));
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_EQ(result->attempts.size(), 1u);
    ASSERT_FALSE(progress.reports().empty());
    for (std::size_t index = 0u; index < progress.reports().size(); ++index)
    {
        EXPECT_EQ(progress.reports()[index].second, 8u);
        if (index > 0u)
        {
            EXPECT_GE(progress.reports()[index].first, progress.reports()[index - 1u].first);
        }
    }
    EXPECT_EQ(progress.reports().back().first, 8u);

    // Two attempts move through both shares.
    const Frame redOnly{true, false, false};
    RecordingProgress both;
    const an::ExecutionContext second = makeExecution(engine, 4u, {}, &both);
    const an::Result<an::StarDetectionChannelResult> twice = an::detectStarsByChannelPolicy(
        second, redOnly.view(), overlayRequest(), an::greenThenRedChannelPolicy(redOnly.descriptor));
    ASSERT_TRUE(twice.hasValue()) << twice.status().message();
    ASSERT_EQ(twice->attempts.size(), 2u);
    ASSERT_FALSE(both.reports().empty());
    EXPECT_TRUE(std::any_of(both.reports().begin(), both.reports().end(),
                            [](const auto& report) { return report.first == 4u; }));
    EXPECT_EQ(both.reports().back().first, 8u);
}

TEST(StarDetectionChannelsTest, StopsWhenTheCallerAsksItToAndAnswersAlikeAtEveryWidth)
{
    const an::EngineContext engine = makeEngine();
    const Frame frame{true, false, true};

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext cancelled = makeExecution(engine, 4u, stop.get_token());
    const an::Result<an::StarDetectionChannelResult> refused = an::detectStarsByChannelPolicy(
        cancelled, frame.view(), overlayRequest(), an::greenThenRedChannelPolicy(frame.descriptor));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_TRUE(refused.status().isCancellation());

    const an::ExecutionContext narrow = makeExecution(engine, 1u);
    const an::ExecutionContext wide = makeExecution(engine, 6u);
    const an::Result<an::StarDetectionChannelResult> one = an::detectStarsByChannelPolicy(
        narrow, frame.view(), overlayRequest(), an::greenThenRedChannelPolicy(frame.descriptor));
    const an::Result<an::StarDetectionChannelResult> six = an::detectStarsByChannelPolicy(
        wide, frame.view(), overlayRequest(), an::greenThenRedChannelPolicy(frame.descriptor));
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    ASSERT_TRUE(six.hasValue()) << six.status().message();
    EXPECT_EQ(one->attempts, six->attempts);
    EXPECT_EQ(one->channel, six->channel);
    ASSERT_EQ(one->detection.stars.size(), six->detection.stars.size());
    for (std::size_t index = 0u; index < one->detection.stars.size(); ++index)
    {
        EXPECT_EQ(one->detection.stars[index].positionX, six->detection.stars[index].positionX);
    }
}
