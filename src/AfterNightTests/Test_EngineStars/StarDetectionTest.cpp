// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract, accuracy and determinism tests for star detection.

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include "StarFieldFixture.h"
#include "../HeapFailure.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 8u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
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

[[nodiscard]] an::ImageDescriptor descriptorOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels = 1u,
                                               an::SampleType sampleType = an::SampleType::Float32)
{
    const an::ChannelSchema schema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{
        .extent = extent.value_or(an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits()).value()),
        .sampleType = sampleType,
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

/// A progress sink that only counts what it was told.
class CountingProgress final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        ++m_reports;
        m_lastCompleted = report.completed;
        m_lastTotal = report.total;
    }

    [[nodiscard]] std::uint32_t reports() const noexcept
    {
        return m_reports;
    }

    [[nodiscard]] std::uint64_t lastCompleted() const noexcept
    {
        return m_lastCompleted;
    }

    [[nodiscard]] std::uint64_t lastTotal() const noexcept
    {
        return m_lastTotal;
    }

private:
    std::uint32_t m_reports = 0u;
    std::uint64_t m_lastCompleted = 0u;
    std::uint64_t m_lastTotal = 0u;
};

} // namespace

TEST(StarDetectionTest, NamesEveryEnumeratorAndRefusesAnythingElse)
{
    EXPECT_STREQ(an::starDetectionSmoothingName(an::StarDetectionSmoothing::Gaussian), "Gaussian");
    EXPECT_STREQ(an::starDetectionSmoothingName(an::StarDetectionSmoothing::RepeatedBox), "RepeatedBox");
    EXPECT_STREQ(an::starDetectionSmoothingName(static_cast<an::StarDetectionSmoothing>(9)),
                 "UnknownStarDetectionSmoothing");
    EXPECT_FALSE(an::isStarDetectionSmoothing(static_cast<an::StarDetectionSmoothing>(9)));

    for (std::uint8_t index = 0u; index < an::kStarRejectionReasonCount; ++index)
    {
        const auto reason = static_cast<an::StarRejectionReason>(index);
        EXPECT_TRUE(an::isStarRejectionReason(reason));
        EXPECT_STRNE(an::starRejectionReasonName(reason), "UnknownStarRejectionReason");
    }
    EXPECT_STREQ(an::starRejectionReasonName(static_cast<an::StarRejectionReason>(99)), "UnknownStarRejectionReason");
}

TEST(StarDetectionTest, PlanValidatesEveryRequestField)
{
    const an::ImageDescriptor descriptor = descriptorOf(256u, 256u);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::StarDetectionRequest good;
    ASSERT_TRUE(an::planStarDetection(good, descriptor, layout).hasValue());

    const auto refused = [&](auto mutate, an::ErrorCode expected) {
        an::StarDetectionRequest request = good;
        mutate(request);
        const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, descriptor, layout);
        EXPECT_FALSE(plan.hasValue());
        EXPECT_EQ(plan.status().code(), expected);
    };
    refused([](an::StarDetectionRequest& r) { r.model = static_cast<an::PsfModel>(7); },
            an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.smoothing = static_cast<an::StarDetectionSmoothing>(7); },
            an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.searchRadius = 0u; }, an::ErrorCode::OutOfRange);
    refused([](an::StarDetectionRequest& r) { r.searchRadius = 33u; }, an::ErrorCode::OutOfRange);
    refused([](an::StarDetectionRequest& r) { r.convergence = 0u; }, an::ErrorCode::OutOfRange);
    refused([](an::StarDetectionRequest& r) { r.noiseSigma = 0.0; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.minimumRoundness = 1.5; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.maximumRoundness = 0.2; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.minimumBeta = 0.0; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.smoothingSigma = 0.0; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.smoothingSigma = 17.0; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.fullScale = 0.0; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.arcsecondsPerPixel = -1.0; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.maximumStars = 0u; }, an::ErrorCode::OutOfRange);
    refused([](an::StarDetectionRequest& r) { r.maximumAmplitude = -1.0; }, an::ErrorCode::InvalidArgument);
    refused([](an::StarDetectionRequest& r) { r.minimumAmplitude = 2.0, r.maximumAmplitude = 1.0; },
            an::ErrorCode::InvalidArgument);
    refused(
        [](an::StarDetectionRequest& r) {
            r.region = an::StatisticsRegion{.originX = 0u, .originY = 0u, .width = 4000u, .height = 10u};
        },
        an::ErrorCode::OutOfRange);
    refused(
        [](an::StarDetectionRequest& r) {
            r.region = an::StatisticsRegion{.originX = 0u, .originY = 0u, .width = 12u, .height = 12u};
        },
        an::ErrorCode::OutOfRange);

    EXPECT_EQ(an::planStarDetection(good, descriptor, layout, 3u).status().code(), an::ErrorCode::OutOfRange);
}

// A searched rectangle with a side no longer than four search radii and eight pixels leaves the scan no room, and is
// refused with the shorter side and the length it must exceed. Its message said the rectangle was wider than the
// search radius needs, the opposite of why it was refused.
TEST(StarDetectionTest, ARectangleTooSmallForTheSearchRadiusIsRefusedSayingWhy)
{
    const an::ImageDescriptor descriptor = descriptorOf(256u, 120u);
    const an::ImageLayout layout = layoutOf(descriptor);
    an::StarDetectionRequest request;
    request.searchRadius = 28u;
    const an::Result<an::StarDetectionPlan> refused = an::planStarDetection(request, descriptor, layout);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::OutOfRange);
    const std::string message = refused.status().message();
    EXPECT_NE(message.find("too small for the search radius"), std::string::npos) << message;
    const an::StatusContextField* const side = refused.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* const limit = refused.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(side, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(side->count, 120u);
    EXPECT_EQ(limit->count, 4u * 28u + 8u);

    request.searchRadius = 27u;
    EXPECT_TRUE(an::planStarDetection(request, descriptor, layout).hasValue());
}

TEST(StarDetectionTest, PlanResolvesTheChannelTheDescriptorImplies)
{
    const an::ImageDescriptor mono = descriptorOf(128u, 128u, 1u);
    const an::Result<an::StarDetectionPlan> monoPlan =
        an::planStarDetection(an::StarDetectionRequest{}, mono, layoutOf(mono));
    ASSERT_TRUE(monoPlan.hasValue()) << monoPlan.status().message();
    EXPECT_EQ(monoPlan->channelIndex(), 0u);
    EXPECT_FALSE(monoPlan->copiesDetectionPlane());
    EXPECT_GT(monoPlan->workingBytesUpperBound(), 0u);
    EXPECT_EQ(monoPlan->region().width, 128u);

    const an::ImageDescriptor colour = descriptorOf(128u, 128u, 3u);
    const an::Result<an::StarDetectionPlan> colourPlan =
        an::planStarDetection(an::StarDetectionRequest{}, colour, layoutOf(colour));
    ASSERT_TRUE(colourPlan.hasValue()) << colourPlan.status().message();
    EXPECT_EQ(colourPlan->channelIndex(), 1u);

    const an::ImageDescriptor integer = descriptorOf(128u, 128u, 1u, an::SampleType::UInt16);
    const an::Result<an::StarDetectionPlan> integerPlan =
        an::planStarDetection(an::StarDetectionRequest{}, integer, layoutOf(integer));
    ASSERT_TRUE(integerPlan.hasValue()) << integerPlan.status().message();
    EXPECT_TRUE(integerPlan->copiesDetectionPlane());
}

TEST(StarDetectionTest, FrameViewRefusesWhatItCannotMeasure)
{
    const an::ImageDescriptor descriptor = descriptorOf(64u, 64u);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> samples(64u * 64u, 0.0f);
    EXPECT_EQ(an::StarFrameView::create(nullptr, layout.totalBytes(), layout, descriptor).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::StarFrameView::create(samples.data(), layout.totalBytes() - 1u, layout, descriptor).status().code(),
              an::ErrorCode::InvalidArgument);

    an::ImageDescriptorRequest encoded = descriptor.toRequest();
    encoded.numericDomain = an::NumericDomain::EncodedNonlinear;
    const std::optional<an::ImageDescriptor> encodedDescriptor = an::ImageDescriptor::create(encoded);
    ASSERT_TRUE(encodedDescriptor.has_value());
    EXPECT_EQ(
        an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, *encodedDescriptor).status().code(),
        an::ErrorCode::Unsupported);

    const an::Result<an::StarFrameView> good =
        an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(good.hasValue()) << good.status().message();
    EXPECT_TRUE(good->valid());
    EXPECT_EQ(good->samples(), samples.data());
    EXPECT_EQ(good->bufferBytes(), layout.totalBytes());
}

TEST(StarDetectionTest, AFrameViewKeepsWhatDescribesTheFrameRatherThanPointingAtIt)
{
    std::vector<float> samples(64u * 64u, 0.0f);
    an::ImageDescriptor descriptor = descriptorOf(64u, 64u);
    an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> view =
        an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(view.hasValue()) << view.status().message();

    // The samples are borrowed and the description is copied, so the only lifetime a caller keeps
    // is the buffer's. Both of these used to be what the view read through.
    descriptor = descriptorOf(32u, 16u);
    layout = layoutOf(descriptor);

    EXPECT_EQ(view->layout().extent().width(), 64u);
    EXPECT_EQ(view->layout().extent().height(), 64u);
    EXPECT_EQ(view->descriptor().extent().width(), 64u);
    EXPECT_EQ(view->samples(), samples.data());
}

TEST(StarDetectionTest, FindsThePlantedFieldWithoutLosingStarsOrInventingThem)
{
    const an::EngineContext engine = makeEngine();
    CountingProgress progress;
    const an::ExecutionContext execution = makeExecution(engine, 4u, {}, &progress);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 512u;
    fieldRequest.height = 512u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    ASSERT_GE(field.stars.size(), 60u);

    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    an::StarDetectionResult detected;
    const an::Status ran = an::detectStars(execution, *frame, *plan, detected);
    ASSERT_TRUE(ran.ok()) << ran.message();

    const an::test::StarFieldScore score = an::test::scoreStarField(field, detected.stars);
    std::cout << "detection on a planted field: planted " << field.stars.size() << ", detected "
              << detected.stars.size() << ", recall " << score.recall() << ", precision " << score.precision()
              << ", position rms " << score.positionRootMeanSquare << " px (worst " << score.positionWorst
              << "), full-width bias " << score.fullWidthBias << ", full-width rms " << score.fullWidthRootMeanSquare
              << '\n';
    EXPECT_GE(score.recall(), 0.95);
    EXPECT_GE(score.precision(), 0.95);
    EXPECT_LT(score.positionRootMeanSquare, 0.1);
    EXPECT_LT(std::abs(score.fullWidthBias), 0.05);
    EXPECT_LT(score.fullWidthRootMeanSquare, 0.08);

    ASSERT_TRUE(detected.summary.medianFullWidth.has_value());
    ASSERT_TRUE(detected.summary.medianRoundness.has_value());
    ASSERT_TRUE(detected.summary.quality.has_value());
    EXPECT_EQ(detected.summary.starCount, detected.stars.size());
    EXPECT_NEAR(detected.summary.channel.background, fieldRequest.background, 5.0 * fieldRequest.noise);
    EXPECT_GT(detected.summary.channel.backgroundNoise, 0.0);
    EXPECT_GT(detected.report.candidates, 0u);
    EXPECT_GE(detected.report.fittedCandidates, detected.stars.size());
    EXPECT_GT(detected.report.peakWorkingBytes, 0u);
    EXPECT_FALSE(detected.report.candidateCeilingReached);
    EXPECT_GE(progress.reports(), 4u);
    EXPECT_EQ(progress.lastCompleted(), 4u);
    EXPECT_EQ(progress.lastTotal(), 4u);

    // The stars come back brightest first.
    for (std::size_t index = 1u; index < detected.stars.size(); ++index)
    {
        ASSERT_TRUE(detected.stars[index - 1u].instrumentalMagnitude.has_value());
        ASSERT_TRUE(detected.stars[index].instrumentalMagnitude.has_value());
        EXPECT_LE(*detected.stars[index - 1u].instrumentalMagnitude, *detected.stars[index].instrumentalMagnitude);
    }
}

TEST(StarDetectionTest, GivesTheSameAnswerAtEveryAdmittedWidth)
{
    const an::EngineContext engine = makeEngine(8u);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 384u;
    fieldRequest.height = 320u;
    fieldRequest.seed = 11u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    an::StarDetectionResult single;
    an::StarDetectionResult wide;
    {
        const an::ExecutionContext narrow = makeExecution(engine, 1u);
        ASSERT_TRUE(an::detectStars(narrow, *frame, *plan, single).ok());
    }
    {
        const an::ExecutionContext broad = makeExecution(engine, 8u);
        ASSERT_TRUE(an::detectStars(broad, *frame, *plan, wide).ok());
    }
    ASSERT_EQ(single.stars.size(), wide.stars.size());
    EXPECT_EQ(single.report.candidates, wide.report.candidates);
    EXPECT_EQ(single.report.duplicateCandidates, wide.report.duplicateCandidates);
    for (std::size_t index = 0u; index < single.stars.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(single.stars[index].positionX, wide.stars[index].positionX) << index;
        EXPECT_DOUBLE_EQ(single.stars[index].positionY, wide.stars[index].positionY) << index;
        EXPECT_DOUBLE_EQ(single.stars[index].psf.fullWidthX, wide.stars[index].psf.fullWidthX) << index;
        EXPECT_DOUBLE_EQ(single.stars[index].psf.amplitude, wide.stars[index].psf.amplitude) << index;
    }
    ASSERT_TRUE(single.summary.medianFullWidth.has_value());
    ASSERT_TRUE(wide.summary.medianFullWidth.has_value());
    EXPECT_DOUBLE_EQ(*single.summary.medianFullWidth, *wide.summary.medianFullWidth);
}

TEST(StarDetectionTest, ADetectionGivenTheChannelsMeasurementIsTheOneThatTakesIt)
{
    // A caller that measures the channel itself and hands the measurement over gets what a detection asking for the
    // robust scale gives, star for star and figure for figure. An odd and an even sample count both reach the
    // median's two middle ranks.
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext execution = makeExecution(engine, 4u);
    for (const std::uint32_t width : {384u, 383u})
    {
        an::test::StarFieldRequest fieldRequest;
        fieldRequest.width = width;
        fieldRequest.height = 321u;
        fieldRequest.seed = width;
        const an::test::StarField field = an::test::makeStarField(fieldRequest);
        const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
        const an::ImageLayout layout = layoutOf(descriptor);
        const an::Result<an::StarFrameView> frame =
            an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
        ASSERT_TRUE(frame.hasValue()) << frame.status().message();

        an::StarDetectionRequest complete;
        complete.measureRobustScale = true;
        const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(complete, descriptor, layout);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        an::StarDetectionResult measuring;
        ASSERT_TRUE(an::detectStars(execution, *frame, *plan, measuring).ok());
        const an::Result<an::StarChannelStatistics> measured =
            an::measureStarChannel(execution, *frame, plan->channelIndex(), plan->region());
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
        an::StarDetectionResult given;
        ASSERT_TRUE(an::detectStars(execution, *frame, *plan, *measured, given).ok());

        EXPECT_EQ(given.summary.channel.background, measuring.summary.channel.background) << width;
        EXPECT_EQ(given.summary.channel.backgroundNoise, measuring.summary.channel.backgroundNoise) << width;
        EXPECT_EQ(given.summary.channel.maximum, measuring.summary.channel.maximum) << width;
        EXPECT_EQ(given.summary.channel.admittedSamples, measuring.summary.channel.admittedSamples) << width;
        EXPECT_EQ(given.summary.channel.medianAbsoluteDeviation, measuring.summary.channel.medianAbsoluteDeviation);
        EXPECT_EQ(given.summary.channel.robustScale, measuring.summary.channel.robustScale);
        ASSERT_GT(measuring.stars.size(), 20u);
        ASSERT_EQ(given.stars.size(), measuring.stars.size()) << width;
        for (std::size_t index = 0u; index < measuring.stars.size(); ++index)
        {
            EXPECT_EQ(given.stars[index].positionX, measuring.stars[index].positionX) << index;
            EXPECT_EQ(given.stars[index].positionY, measuring.stars[index].positionY) << index;
            EXPECT_EQ(given.stars[index].psf.amplitude, measuring.stars[index].psf.amplitude) << index;
            EXPECT_EQ(given.stars[index].psf.fullWidthX, measuring.stars[index].psf.fullWidthX) << index;
            EXPECT_EQ(given.stars[index].signalToNoise, measuring.stars[index].signalToNoise) << index;
            EXPECT_EQ(given.stars[index].instrumentalMagnitude, measuring.stars[index].instrumentalMagnitude) << index;
        }
        EXPECT_EQ(given.summary.medianFullWidth, measuring.summary.medianFullWidth);
        EXPECT_EQ(given.summary.medianSignalToNoise, measuring.summary.medianSignalToNoise);
        EXPECT_EQ(given.summary.quality, measuring.summary.quality);
        EXPECT_EQ(given.report.candidates, measuring.report.candidates);

        // The sky a detection measures for itself is the same median, maximum and count; its noise is the same
        // estimator walked row by row, which rounds in its last place apart from the complete measurement's - so a
        // caller that wants the complete measurement's figures hands it over rather than asking for the sky alone.
        an::StarDetectionRequest skyOnly;
        const an::Result<an::StarDetectionPlan> skyPlan = an::planStarDetection(skyOnly, descriptor, layout);
        ASSERT_TRUE(skyPlan.hasValue());
        an::StarDetectionResult sky;
        ASSERT_TRUE(an::detectStars(execution, *frame, *skyPlan, sky).ok());
        EXPECT_EQ(sky.summary.channel.background, measured->background);
        EXPECT_EQ(sky.summary.channel.maximum, measured->maximum);
        EXPECT_EQ(sky.summary.channel.admittedSamples, measured->admittedSamples);
        EXPECT_NEAR(sky.summary.channel.backgroundNoise, measured->backgroundNoise, 1e-14 * measured->backgroundNoise);
        EXPECT_FALSE(sky.summary.channel.robustScale.has_value());
    }

    // A measurement of another channel than the plan's is refused before anything is searched.
    const an::ImageDescriptor colour = descriptorOf(96u, 80u, 3u);
    const an::ImageLayout colourLayout = layoutOf(colour);
    std::vector<float> samples(static_cast<std::size_t>(96u) * 80u * 3u, 0.1f);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] += static_cast<float>(index % 13u) * 0.001f;
    }
    const an::Result<an::StarFrameView> colourFrame =
        an::StarFrameView::create(samples.data(), colourLayout.totalBytes(), colourLayout, colour);
    ASSERT_TRUE(colourFrame.hasValue());
    const an::Result<an::StarDetectionPlan> greenPlan =
        an::planStarDetection(an::StarDetectionRequest{}, colour, colourLayout, 1u);
    ASSERT_TRUE(greenPlan.hasValue());
    const an::Result<an::StarChannelStatistics> red =
        an::measureStarChannel(execution, *colourFrame, 0u, greenPlan->region());
    ASSERT_TRUE(red.hasValue());
    an::StarDetectionResult refused;
    refused.report.candidates = 7u;
    EXPECT_EQ(an::detectStars(execution, *colourFrame, *greenPlan, *red, refused).code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(refused.report.candidates, 7u);
}

TEST(StarDetectionTest, BothSmoothingsFindTheSameFieldAndSayWhichRan)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 384u;
    fieldRequest.height = 384u;
    fieldRequest.seed = 23u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    an::test::StarFieldScore exactScore{};
    an::test::StarFieldScore boxScore{};
    for (const an::StarDetectionSmoothing smoothing :
         {an::StarDetectionSmoothing::Gaussian, an::StarDetectionSmoothing::RepeatedBox})
    {
        an::StarDetectionRequest request;
        request.smoothing = smoothing;
        const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, descriptor, layout);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_EQ(plan->request().smoothing, smoothing);
        an::StarDetectionResult detected;
        ASSERT_TRUE(an::detectStars(execution, *frame, *plan, detected).ok());
        const an::test::StarFieldScore score = an::test::scoreStarField(field, detected.stars);
        EXPECT_GE(score.recall(), 0.9) << an::starDetectionSmoothingName(smoothing);
        EXPECT_GE(score.precision(), 0.9) << an::starDetectionSmoothingName(smoothing);
        (smoothing == an::StarDetectionSmoothing::Gaussian ? exactScore : boxScore) = score;
    }
    std::cout << "smoothing comparison: exact recall " << exactScore.recall() << " precision " << exactScore.precision()
              << " position rms " << exactScore.positionRootMeanSquare << "; repeated box recall " << boxScore.recall()
              << " precision " << boxScore.precision() << " position rms " << boxScore.positionRootMeanSquare << '\n';
}

TEST(StarDetectionTest, HonoursAStarLimitByBrightness)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 512u;
    fieldRequest.height = 512u;
    fieldRequest.seed = 31u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    an::StarDetectionResult all;
    {
        const an::Result<an::StarDetectionPlan> plan =
            an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
        ASSERT_TRUE(plan.hasValue());
        ASSERT_TRUE(an::detectStars(execution, *frame, *plan, all).ok());
    }
    ASSERT_GT(all.stars.size(), 12u);

    an::StarDetectionRequest limitedRequest;
    limitedRequest.maximumStars = 10u;
    const an::Result<an::StarDetectionPlan> limitedPlan = an::planStarDetection(limitedRequest, descriptor, layout);
    ASSERT_TRUE(limitedPlan.hasValue());
    an::StarDetectionResult limited;
    ASSERT_TRUE(an::detectStars(execution, *frame, *limitedPlan, limited).ok());
    EXPECT_LE(limited.stars.size(), 10u);
    EXPECT_GT(limited.stars.size(), 0u);
    // A limit fits fewer candidates than the whole frame has, which is the point of the rounds.
    EXPECT_LT(limited.report.fittedCandidates, all.report.fittedCandidates);
}

TEST(StarDetectionTest, SearchesOnlyTheStatedRegion)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 512u;
    fieldRequest.height = 512u;
    fieldRequest.seed = 41u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    an::StarDetectionRequest request;
    request.region = an::StatisticsRegion{.originX = 128u, .originY = 128u, .width = 256u, .height = 256u};
    const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, descriptor, layout);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->region().originX, 128u);
    an::StarDetectionResult detected;
    ASSERT_TRUE(an::detectStars(execution, *frame, *plan, detected).ok());
    ASSERT_GT(detected.stars.size(), 4u);
    for (const an::DetectedStar& star : detected.stars)
    {
        EXPECT_GE(star.positionX, 128.0);
        EXPECT_GE(star.positionY, 128.0);
        EXPECT_LE(star.positionX, 384.0);
        EXPECT_LE(star.positionY, 384.0);
    }
}

TEST(StarDetectionTest, SubstitutesTheSkyForSamplesTheValidityRuleRejects)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 320u;
    fieldRequest.height = 320u;
    fieldRequest.seed = 53u;
    an::test::StarField field = an::test::makeStarField(fieldRequest);
    // A no-data border, exactly the shape a registered frame carries.
    for (std::uint32_t row = 0u; row < fieldRequest.height; ++row)
    {
        for (std::uint32_t column = 0u; column < fieldRequest.width; ++column)
        {
            if (row < 24u || column < 24u || row >= fieldRequest.height - 24u || column >= fieldRequest.width - 24u)
            {
                field.samples[static_cast<std::size_t>(row) * fieldRequest.width + column] = 0.0f;
            }
        }
    }

    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroRule.has_value());
    const an::KernelValidityView validity{.descriptor = *zeroRule};
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor, validity);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());
    an::StarDetectionResult detected;
    ASSERT_TRUE(an::detectStars(execution, *frame, *plan, detected).ok());

    // Nothing is found on the border, and the interior stars are still all found.
    for (const an::DetectedStar& star : detected.stars)
    {
        EXPECT_GT(star.positionX, 20.0);
        EXPECT_GT(star.positionY, 20.0);
        EXPECT_LT(star.positionX, static_cast<double>(fieldRequest.width) - 20.0);
        EXPECT_LT(star.positionY, static_cast<double>(fieldRequest.height) - 20.0);
    }
    std::size_t interior = 0u;
    for (const an::test::PlantedStar& planted : field.stars)
    {
        if (planted.x > 32.0 && planted.y > 32.0 && planted.x < fieldRequest.width - 32.0 &&
            planted.y < fieldRequest.height - 32.0)
        {
            ++interior;
        }
    }
    EXPECT_GE(detected.stars.size(), interior * 9u / 10u);
}

TEST(StarDetectionTest, CountsWhyItRefusedCandidates)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 384u;
    fieldRequest.height = 384u;
    fieldRequest.seed = 67u;
    fieldRequest.minimumRoundness = 0.35;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    an::StarDetectionRequest request;
    request.minimumRoundness = 0.9;
    const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());
    an::StarDetectionResult detected;
    ASSERT_TRUE(an::detectStars(execution, *frame, *plan, detected).ok());
    EXPECT_GT(detected.report.rejections[static_cast<std::size_t>(an::StarRejectionReason::RoundnessOutsideWindow)],
              0u);
    for (const an::DetectedStar& star : detected.stars)
    {
        EXPECT_GE(star.metrics.roundness, 0.9);
    }
}

TEST(StarDetectionTest, StopsWhenTheCallerAsksItTo)
{
    const an::EngineContext engine = makeEngine();
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 512u;
    fieldRequest.height = 512u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue());
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());

    std::stop_source source;
    source.request_stop();
    const an::ExecutionContext execution = makeExecution(engine, 4u, source.get_token());
    an::StarDetectionResult detected;
    detected.stars.push_back(an::DetectedStar{});
    const an::Status ran = an::detectStars(execution, *frame, *plan, detected);
    EXPECT_EQ(ran.code(), an::ErrorCode::Cancelled);
    // A cancelled call leaves what the caller already had.
    EXPECT_EQ(detected.stars.size(), 1u);
}

TEST(StarDetectionTest, RefusesAnExecutionOrFrameItCannotUse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 192u;
    fieldRequest.height = 192u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue());
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());
    an::StarDetectionResult detected;

    const an::ExecutionContext empty;
    EXPECT_EQ(an::detectStars(empty, *frame, *plan, detected).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::detectStars(execution, an::StarFrameView{}, *plan, detected).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::detectStars(execution, *frame, an::StarDetectionPlan{}, detected).code(),
              an::ErrorCode::InvalidArgument);

    // A plan for a larger frame does not describe this one.
    const an::ImageDescriptor larger = descriptorOf(512u, 512u);
    const an::Result<an::StarDetectionPlan> largerPlan =
        an::planStarDetection(an::StarDetectionRequest{}, larger, layoutOf(larger));
    ASSERT_TRUE(largerPlan.hasValue());
    an::StarDetectionRequest regioned;
    regioned.region = an::StatisticsRegion{.originX = 0u, .originY = 0u, .width = 400u, .height = 400u};
    const an::Result<an::StarDetectionPlan> regionedPlan = an::planStarDetection(regioned, larger, layoutOf(larger));
    ASSERT_TRUE(regionedPlan.hasValue());
    EXPECT_EQ(an::detectStars(execution, *frame, *regionedPlan, detected).code(), an::ErrorCode::IncompatibleArguments);
}

TEST(StarDetectionTest, MeasuresAChannelWithoutDetectingOnIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 256u;
    fieldRequest.height = 256u;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue());

    const an::Result<an::StarChannelStatistics> statistics = an::measureStarChannel(execution, *frame, 0u);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->channelIndex, 0u);
    EXPECT_NEAR(statistics->background, fieldRequest.background, 5.0 * fieldRequest.noise);
    EXPECT_GT(statistics->backgroundNoise, 0.0);
    EXPECT_GT(statistics->medianAbsoluteDeviation, 0.0);
    EXPECT_GT(statistics->robustScale, 0.0);
    EXPECT_GT(statistics->maximum, statistics->background);

    EXPECT_EQ(an::measureStarChannel(execution, *frame, 3u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::measureStarChannel(execution, an::StarFrameView{}, 0u).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(StarDetectionTest, ReadsTheSeeingFromTheFrameItIsAskedFor)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 384u;
    fieldRequest.height = 384u;
    fieldRequest.minimumSigma = 1.9;
    fieldRequest.maximumSigma = 2.1;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue());

    const an::Result<double> seeing = an::measureStarFieldFullWidth(execution, *frame);
    ASSERT_TRUE(seeing.hasValue()) << seeing.status().message();
    double plantedSum = 0.0;
    for (const an::test::PlantedStar& planted : field.stars)
    {
        plantedSum += planted.meanFullWidth();
    }
    const double plantedMean = plantedSum / static_cast<double>(field.stars.size());
    EXPECT_NEAR(*seeing, plantedMean, 0.25);

    EXPECT_EQ(an::measureStarFieldFullWidth(execution, an::StarFrameView{}).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(StarDetectionTest, FindsNothingInAnEmptySkyAndSaysSo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 192u;
    constexpr std::uint32_t kHeight = 192u;
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] =
            static_cast<float>(0.05 + 0.002 * an::test::fieldNormal(static_cast<std::uint64_t>(index) + 3u));
    }
    const an::ImageDescriptor descriptor = descriptorOf(kWidth, kHeight);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue());
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());

    an::StarDetectionResult detected;
    const an::Status ran = an::detectStars(execution, *frame, *plan, detected);
    ASSERT_TRUE(ran.ok()) << ran.message();
    EXPECT_TRUE(detected.stars.empty());
    EXPECT_EQ(detected.summary.starCount, 0u);
    EXPECT_FALSE(detected.summary.medianFullWidth.has_value());
    EXPECT_FALSE(detected.summary.quality.has_value());
    EXPECT_GT(detected.summary.channel.backgroundNoise, 0.0);
    EXPECT_EQ(an::measureStarFieldFullWidth(execution, *frame).status().code(), an::ErrorCode::EmptySelection);
}

TEST(StarDetectionTest, IntegerDetectionKeepsCodeUnitsWithOptionalStatistics)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto descriptor = descriptorOf(128u, 128u, 3u, an::SampleType::UInt16);
    const auto validity = an::ValidityDescriptor::create(
        {.rule = an::ValidityRule::DomainRange, .lowestValid = 500.0, .highestValid = 40000.0});
    for (const auto arrangement : {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
    {
        const auto layout =
            an::ImageLayout::create(*an::layoutRequestFor(descriptor, arrangement, 1u), an::defaultEngineLimits());
        ASSERT_TRUE(layout.has_value());
        std::vector<std::uint16_t> samples(128u * 128u * 3u);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < 128u; ++y)
            {
                for (std::uint32_t x = 0u; x < 128u; ++x)
                {
                    const double dx = static_cast<double>(x) - 64.0;
                    const double dy = static_cast<double>(y) - 64.0;
                    const std::size_t at = arrangement == an::StorageArrangement::Planar
                                               ? channel * 128u * 128u + y * 128u + x
                                               : (y * 128u + x) * 3u + channel;
                    samples[at] = static_cast<std::uint16_t>(1000u + (x * 17u + y * 13u) % 13u +
                                                             20000.0 * std::exp(-(dx * dx + dy * dy) / 18.0));
                }
            }
        }
        const auto frame = an::StarFrameView::create(samples.data(), layout->totalBytes(), *layout, descriptor,
                                                     {.descriptor = *validity});
        ASSERT_TRUE(frame.hasValue());
        for (const bool robust : {false, true})
        {
            an::StarDetectionRequest request;
            request.fullScale = 65535.0;
            request.minimumAmplitude = 10000.0;
            request.maximumAmplitude = 30000.0;
            request.measureRobustScale = robust;
            const auto plan = an::planStarDetection(request, descriptor, *layout);
            ASSERT_TRUE(plan.hasValue());
            an::StarDetectionResult result;
            ASSERT_TRUE(an::detectStars(execution, *frame, *plan, result).ok());
            ASSERT_EQ(result.stars.size(), 1u);
            EXPECT_NEAR(result.stars[0].positionX, 64.5, 0.1);
            EXPECT_NEAR(result.stars[0].positionY, 64.5, 0.1);
            EXPECT_NEAR(result.summary.channel.background, 1006.0, 10.0);
        }
    }
}

TEST(StarDetectionTest, SkyMeasurementUsesTheSelectedMaskChannel)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto descriptor = descriptorOf(64u, 64u, 3u);
    const auto layout = layoutOf(descriptor);
    std::vector<float> samples(64u * 64u * 3u);
    for (std::size_t i = 0u; i < samples.size(); ++i)
    {
        samples[i] = 0.1f + static_cast<float>(i % 7u) * 0.0001f;
    }
    const auto rule = an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    for (const std::uint32_t selected : {1u, 2u})
    {
        auto mask = an::ValidityMask::create(engine, descriptor.extent());
        ASSERT_TRUE(mask.hasValue());
        auto writable = mask->view();
        for (std::uint32_t y = 0u; y < 64u; ++y)
        {
            for (std::uint32_t x = 0u; x < 64u; ++x)
            {
                ASSERT_TRUE(writable->setValid(selected, x, y, true));
            }
        }
        const auto inputMask = std::as_const(*mask).view();
        const auto frame = an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor,
                                                     {.descriptor = *rule, .mask = &*inputMask});
        ASSERT_TRUE(frame.hasValue());
        for (const bool robust : {false, true})
        {
            an::StarDetectionRequest request;
            request.measureRobustScale = robust;
            const auto plan = an::planStarDetection(request, descriptor, layout, selected);
            ASSERT_TRUE(plan.hasValue());
            an::StarDetectionResult result;
            const auto status = an::detectStars(execution, *frame, *plan, result);
            ASSERT_TRUE(status.ok()) << status.message();
            EXPECT_EQ(result.summary.channel.admittedSamples, 64u * 64u);
        }
    }
}

TEST(StarDetectionTest, WorkingBoundCoversConcurrentFits)
{
    const auto engine = makeEngine(64u);
    const auto execution = makeExecution(engine, 64u);
    const auto descriptor = descriptorOf(128u, 128u);
    const auto layout = layoutOf(descriptor);
    std::vector<float> samples(128u * 128u);
    for (std::uint32_t y = 0u; y < 128u; ++y)
        for (std::uint32_t x = 0u; x < 128u; ++x)
        {
            const double dx = static_cast<double>(x) - 64.0;
            const double dy = static_cast<double>(y) - 64.0;
            samples[y * 128u + x] = static_cast<float>(0.05 + ((x * 17u + y * 13u) % 13u) * 0.00001 +
                                                       0.5 * std::exp(-(dx * dx + dy * dy) / 200.0));
        }
    const auto frame = an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor);
    an::StarDetectionRequest request;
    request.relaxedChecks = true;
    const auto plan = an::planStarDetection(request, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());
    an::StarDetectionResult result;
    ASSERT_TRUE(an::detectStars(execution, *frame, *plan, result).ok());
    ASSERT_FALSE(result.stars.empty());
    EXPECT_LE(result.report.peakWorkingBytes, plan->workingBytesUpperBound());
}

TEST(StarDetectionTest, HeapExhaustionReturnsFailureWithoutPublishingAPartialResult)
{
    if (!an::test::HeapFailure::kAvailable)
        GTEST_SKIP() << "shared CRT heap injection requires MSVC Debug";
    const auto engine = makeEngine(1u);
    const auto execution = makeExecution(engine, 1u);
    an::test::StarFieldRequest request;
    request.width = 128u;
    request.height = 128u;
    const auto field = an::test::makeStarField(request);
    const auto descriptor = descriptorOf(128u, 128u);
    const auto layout = layoutOf(descriptor);
    const auto frame = an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    const auto plan = an::planStarDetection({}, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());
    an::StarDetectionResult warmup;
    ASSERT_TRUE(an::detectStars(execution, *frame, *plan, warmup).ok());
    std::size_t failures = 0u;
    bool completed = false;
    for (std::size_t after = 0u; after < 256u; ++after)
    {
        an::StarDetectionResult result;
        result.report.peakWorkingBytes = 123u;
        an::Status status;
        std::size_t refused = 0u;
        {
            an::test::HeapFailure heap(after);
            status = an::detectStars(execution, *frame, *plan, result);
            refused = heap.refused();
        }
        if (status.ok() && refused == 0u)
        {
            completed = true;
            break;
        }
        if (status.failed())
        {
            ++failures;
            EXPECT_EQ(status.code(), an::ErrorCode::AllocationFailed);
            EXPECT_TRUE(result.stars.empty());
            EXPECT_EQ(result.report.peakWorkingBytes, 123u);
        }
    }
    EXPECT_TRUE(completed);
    EXPECT_GT(failures, 4u);
}

TEST(StarDetectionTest, RelaxingFitQualityStillEnforcesTheAmplitudeWindow)
{
    auto engine = makeEngine();
    auto execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 320u;
    fieldRequest.height = 320u;
    const auto field = an::test::makeStarField(fieldRequest);
    const auto descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const auto layout = layoutOf(descriptor);
    const auto frame = an::StarFrameView::create(field.samples.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(frame.hasValue());
    an::StarDetectionRequest request;
    request.relaxedChecks = true;
    request.maximumAmplitude = 1.0e-10;
    const auto plan = an::planStarDetection(request, descriptor, layout);
    ASSERT_TRUE(plan.hasValue());
    an::StarDetectionResult detected;
    ASSERT_TRUE(an::detectStars(execution, *frame, *plan, detected).ok());
    EXPECT_TRUE(detected.stars.empty());
}
