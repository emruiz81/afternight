// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Why the detector smooths the way it does, and where one detection spends itself.
//
// The first case is a real acceptance: it plants a field with hot pixels and nebulosity, scores both
// smoothing surfaces against the truth that produced it, and holds the default to being at least as
// good as the repeated-box surface on every axis. The second is a probe with no assertion at all,
// because the durations it prints are properties of the machine. Run the probe by name:
//
//     Test_EngineStars --gtest_also_run_disabled_tests --gtest_filter=*StageTiming*

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "StarFieldFixture.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// A trace sink that turns matched spans into durations.
class SpanTimer final : public an::TraceSink
{
public:
    void onTrace(const an::TraceEvent& event) noexcept override
    {
        const auto now = std::chrono::steady_clock::now();
        if (event.kind == an::TraceEventKind::SpanBegin)
        {
            m_open[std::string(event.name)] = now;
        }
        else if (event.kind == an::TraceEventKind::SpanEnd)
        {
            const auto found = m_open.find(std::string(event.name));
            if (found != m_open.end())
            {
                m_spans.emplace_back(found->first,
                                     std::chrono::duration<double, std::milli>(now - found->second).count());
                m_open.erase(found);
            }
        }
    }

    [[nodiscard]] const std::vector<std::pair<std::string, double>>& spans() const noexcept
    {
        return m_spans;
    }

private:
    std::map<std::string, std::chrono::steady_clock::time_point> m_open;
    std::vector<std::pair<std::string, double>> m_spans;
};

/// The descriptor and layout of one tight single-channel Float32 frame.
struct FrameShape
{
    /// What the frame is.
    an::ImageDescriptor descriptor;

    /// Where its samples sit.
    an::ImageLayout layout;
};

/// Builds the shape of one tight Float32 mono frame.
[[nodiscard]] std::optional<FrameShape> shapeOf(std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageLayoutRequest> layoutRequest =
        an::layoutRequestFor(*descriptor, an::StorageArrangement::Planar, 1u);
    if (!layoutRequest.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*layoutRequest, an::defaultEngineLimits());
    if (!layout.has_value())
    {
        return std::nullopt;
    }
    return FrameShape{.descriptor = *descriptor, .layout = *layout};
}

} // namespace

TEST(StarDetectionTest, TheDefaultSurfaceIsAtLeastAsGoodAsTheRepeatedBoxOne)
{
    // Three box averages asked for a standard deviation of two realize this instead. Detecting on an
    // exact Gaussian of the width the boxes actually realize is what makes the default surface's
    // sensitivity the repeated boxes' rather than a coincidence.
    const an::Result<an::GaussianPlan> box =
        an::planRealtimePreviewGaussianBlur(an::RealtimePreviewGaussianRequest{.sigmaX = 2.0, .sigmaY = 2.0});
    ASSERT_TRUE(box.hasValue()) << box.status().message();
    EXPECT_NEAR(box->realizedSigmaX(), an::kDefaultStarSmoothingSigma, 1e-9);
    EXPECT_EQ(an::StarDetectionRequest{}.smoothing, an::StarDetectionSmoothing::Gaussian);
    EXPECT_DOUBLE_EQ(an::StarDetectionRequest{}.smoothingSigma, an::kDefaultStarSmoothingSigma);

    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 8u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();

    // A field a detector can be wrong about: stars reaching down to the threshold, single-sample
    // defects that are not stars, and smooth structure whose own gradients look like faint ones.
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 2048u;
    fieldRequest.height = 2048u;
    fieldRequest.cell = 24u;
    fieldRequest.minimumAmplitude = 0.004;
    fieldRequest.maximumAmplitude = 0.40;
    fieldRequest.hotPixels = 400u;
    fieldRequest.nebulosity = 0.06;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    const std::optional<FrameShape> shape = shapeOf(fieldRequest.width, fieldRequest.height);
    ASSERT_TRUE(shape.has_value());
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), shape->layout.totalBytes(), shape->layout, shape->descriptor);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    std::size_t reachable = 0u;
    for (const an::test::PlantedStar& planted : field.stars)
    {
        if (planted.amplitude > an::kStarDetectionThresholdSigmas * fieldRequest.noise)
        {
            ++reachable;
        }
    }
    ASSERT_GT(reachable, 6000u);

    const auto score = [&](an::StarDetectionSmoothing smoothing, double sigma) {
        an::StarDetectionRequest request;
        request.smoothing = smoothing;
        request.smoothingSigma = sigma;
        const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, shape->descriptor, shape->layout);
        EXPECT_TRUE(plan.hasValue()) << plan.status().message();
        an::ExecutionOptions options;
        options.maxConcurrency = 4u;
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, options);
        EXPECT_TRUE(execution.hasValue());
        an::StarDetectionResult detected;
        EXPECT_TRUE(an::detectStars(*execution, *frame, *plan, detected).ok());
        return an::test::scoreStarField(field, detected.stars);
    };

    const an::test::StarFieldScore boxes = score(an::StarDetectionSmoothing::RepeatedBox, 2.0);
    const an::test::StarFieldScore chosen = score(an::StarDetectionSmoothing::Gaussian, an::kDefaultStarSmoothingSigma);
    const double boxRecall = static_cast<double>(boxes.matched) / static_cast<double>(reachable);
    const double chosenRecall = static_cast<double>(chosen.matched) / static_cast<double>(reachable);
    std::cout << "smoothing acceptance on " << field.stars.size() << " planted stars (" << reachable
              << " above the threshold): repeated-box surface matched " << boxes.matched << ", spurious "
              << boxes.spurious << ", recall " << boxRecall << ", precision " << boxes.precision() << ", width rms "
              << boxes.fullWidthRootMeanSquare << "; default surface matched " << chosen.matched << ", spurious "
              << chosen.spurious << ", recall " << chosenRecall << ", precision " << chosen.precision()
              << ", width rms " << chosen.fullWidthRootMeanSquare << '\n';

    // The two surfaces are equivalent on this field to within the noise of the comparison, which
    // is what admits the faster one as the default: neither loses stars to the other, and the exact
    // kernel measures the widths of the ones it finds a little more precisely. A one per cent
    // margin is the size of the disagreement seen across seeds and field sizes; a real regression
    // in sensitivity is far larger than that, and the real-capture rows of the benchmark catalogue
    // are what watch for one.
    EXPECT_GE(static_cast<double>(chosen.matched), 0.99 * static_cast<double>(boxes.matched));
    EXPECT_GE(chosen.precision(), boxes.precision() - 1.0e-3);
    EXPECT_LE(chosen.fullWidthRootMeanSquare, boxes.fullWidthRootMeanSquare);
    EXPECT_GT(chosenRecall, 0.85);
    EXPECT_GT(chosen.precision(), 0.99);
}

TEST(StarDetectionTest, DISABLED_StageTiming)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 16u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();

    for (const std::uint32_t side : {1024u, 3008u, 8288u})
    {
        const std::uint32_t height = side == 8288u ? 5644u : (side == 3008u ? 3004u : 1024u);
        an::test::StarFieldRequest fieldRequest;
        fieldRequest.width = side;
        fieldRequest.height = height;
        const an::test::StarField field = an::test::makeStarField(fieldRequest);
        const std::optional<FrameShape> shape = shapeOf(side, height);
        ASSERT_TRUE(shape.has_value());
        const an::Result<an::StarFrameView> frame = an::StarFrameView::create(
            field.samples.data(), shape->layout.totalBytes(), shape->layout, shape->descriptor);
        ASSERT_TRUE(frame.hasValue()) << frame.status().message();
        const an::Result<an::StarDetectionPlan> plan =
            an::planStarDetection(an::StarDetectionRequest{}, shape->descriptor, shape->layout);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();

        for (std::uint32_t warm = 0u; warm < 2u; ++warm)
        {
            an::ExecutionOptions warmOptions;
            warmOptions.maxConcurrency = 16u;
            an::Result<an::ExecutionContext> warmExecution = an::ExecutionContext::create(*engine, warmOptions);
            ASSERT_TRUE(warmExecution.hasValue());
            an::StarDetectionResult ignored;
            ASSERT_TRUE(an::detectStars(*warmExecution, *frame, *plan, ignored).ok());
        }

        SpanTimer timer;
        an::ExecutionOptions options;
        options.maxConcurrency = 16u;
        options.traceSink = &timer;
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, options);
        ASSERT_TRUE(execution.hasValue()) << execution.status().message();
        an::StarDetectionResult detected;
        const auto started = std::chrono::steady_clock::now();
        ASSERT_TRUE(an::detectStars(*execution, *frame, *plan, detected).ok());
        const double total =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        std::cout << side << 'x' << height << ": total " << total << " ms, " << detected.stars.size() << " stars from "
                  << detected.report.candidates << " candidates";
        for (const auto& [name, milliseconds] : timer.spans())
        {
            std::cout << ", " << name << ' ' << milliseconds << " ms";
        }
        std::cout << '\n';
    }
}
