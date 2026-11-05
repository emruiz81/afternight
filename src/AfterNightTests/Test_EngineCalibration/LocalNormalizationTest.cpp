// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for local normalization: planning, sampling, estimation, fitting, scaling,
// application, persistence and the transition.

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
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

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::stop_token stopToken = {}, std::uint32_t concurrency = 3u,
                                                 an::ProgressSink* progress = nullptr,
                                                 an::BackendFallback fallback = an::BackendFallback::Allowed)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    options.stopToken = std::move(stopToken);
    options.progressSink = progress;
    options.backendFallback = fallback;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageDescriptor descriptorOf(an::SampleType sampleType, std::uint32_t width, std::uint32_t height,
                                               an::ChannelSchema schema = an::ChannelSchema::Mono)
{
    const std::uint32_t channels = an::channelCountOf(schema).value_or(1u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ColorSpace colorSpace =
        schema == an::ChannelSchema::Rgb ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified;
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = sampleType,
                                             .channelSchema = schema,
                                             .colorSpace = colorSpace,
                                             .numericDomain = an::NumericDomain::NormalizedLinear,
                                             .cfa = {},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::ImageLayout layoutOf(const an::ImageDescriptor& descriptor,
                                       an::StorageArrangement arrangement = an::StorageArrangement::Planar)
{
    const std::optional<an::ImageLayoutRequest> request = an::layoutRequestFor(descriptor, arrangement, 1u);
    EXPECT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    const std::optional<an::ValidityDescriptor> validity =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = rule});
    EXPECT_TRUE(validity.has_value());
    return *validity;
}

[[nodiscard]] an::NormalizationFrameView frameOf(const void* samples, const an::ImageLayout& layout,
                                                 const an::ImageDescriptor& descriptor,
                                                 an::ValidityRule rule = an::ValidityRule::AllValid)
{
    const an::KernelValidityView validity{.descriptor = ruleOf(rule)};
    const an::Result<an::NormalizationFrameView> made =
        an::NormalizationFrameView::create(samples, layout.totalBytes(), layout, descriptor, validity);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::NormalizationFrameView{};
}

/// A test-side pseudo-random sample in `[0.2, 0.6)`, so a fixture is deterministic and not a constant.
[[nodiscard]] float patterned(std::size_t index, std::uint32_t salt) noexcept
{
    const std::uint64_t mixed = (static_cast<std::uint64_t>(index) + 1u) * 2654435761ull + salt * 40503ull;
    return 0.2f + static_cast<float>((mixed >> 7u) % 4001u) / 10000.0f;
}

/// A plane over a frame, as the tests spell one.
[[nodiscard]] double planeAt(double base, double slopeX, double slopeY, std::uint32_t x, std::uint32_t y,
                             std::uint32_t width, std::uint32_t height) noexcept
{
    return base + slopeX * static_cast<double>(x) / static_cast<double>(width - 1u) +
           slopeY * static_cast<double>(y) / static_cast<double>(height - 1u);
}

[[nodiscard]] an::LocalNormalizationPlan planOf(const an::LocalNormalizationRequest& request, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t channels)
{
    const an::Result<an::LocalNormalizationPlan> plan = an::planLocalNormalization(request, width, height, channels);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    return plan.hasValue() ? *plan : an::LocalNormalizationPlan{};
}

/// The node grid of a plane, which is the grid the fit would ideally produce.
[[nodiscard]] std::vector<double> planeGrid(const an::LocalNormalizationPlan& plan, double base, double slopeX,
                                            double slopeY)
{
    const an::LocalNormalizationGridShape& shape = plan.shape();
    std::vector<double> values(static_cast<std::size_t>(shape.valueCount()), 0.0);
    for (std::uint32_t channel = 0u; channel < shape.channelCount; ++channel)
    {
        for (std::uint32_t ny = 0u; ny < shape.nodesPerAxis(); ++ny)
        {
            for (std::uint32_t nx = 0u; nx < shape.nodesPerAxis(); ++nx)
            {
                const double x = static_cast<double>(nx) / shape.gridSize * (shape.width - 1u);
                const double y = static_cast<double>(ny) / shape.gridSize * (shape.height - 1u);
                values[an::localNormalizationNodeIndex(shape, channel, nx, ny)] =
                    base + 0.01 * channel + slopeX * x / (shape.width - 1u) + slopeY * y / (shape.height - 1u);
            }
        }
    }
    return values;
}

/// A sink that remembers the last report and how many arrived.
class CountingProgress final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        ++reports;
        lastCompleted = report.completed;
        lastTotal = report.total;
        lastStage = report.stage;
    }

    std::uint64_t reports = 0u;
    std::uint64_t lastCompleted = 0u;
    std::uint64_t lastTotal = 0u;
    std::string lastStage;
};

TEST(LocalNormalizationTest, PlanValidatesTheRequestAndResolvesGeometry)
{
    an::LocalNormalizationRequest request;
    const an::LocalNormalizationPlan plan = planOf(request, 640u, 480u, 3u);
    ASSERT_TRUE(plan.valid());
    EXPECT_EQ(plan.shape().gridSize, 4u);
    EXPECT_EQ(plan.shape().width, 640u);
    EXPECT_EQ(plan.shape().height, 480u);
    EXPECT_EQ(plan.shape().channelCount, 3u);
    EXPECT_EQ(plan.shape().nodesPerAxis(), 5u);
    EXPECT_EQ(plan.shape().nodeCount(), 25u);
    EXPECT_EQ(plan.shape().valueCount(), 75u);
    EXPECT_EQ(plan.cellWidth(), 160u);
    EXPECT_EQ(plan.cellHeight(), 120u);
    EXPECT_EQ(plan.roiWidth(), 320u);
    EXPECT_EQ(plan.roiHeight(), 240u);
    EXPECT_EQ(plan.samplesPerNode(), 4800u);
    EXPECT_EQ(plan.scaleRoiWidth(), 160u);
    EXPECT_EQ(plan.scaleRoiHeight(), 120u);
    EXPECT_EQ(plan.samplesPerScaleNode(), 327u);
    EXPECT_EQ(plan.scaleChannel(), 1u);
    EXPECT_EQ(plan.nodeX(0u), 0u);
    EXPECT_EQ(plan.nodeX(4u), 639u);
    EXPECT_EQ(plan.nodeX(2u), 320u);
    EXPECT_EQ(plan.nodeY(4u), 479u);
    EXPECT_EQ(plan.roiOriginX(0u), 0u);
    EXPECT_EQ(plan.roiOriginX(4u), 320u);
    EXPECT_EQ(plan.roiOriginY(0u), 0u);
    EXPECT_EQ(plan.roiOriginY(4u), 240u);
    EXPECT_GT(plan.fitWorkingBytesPerWorker(), 0u);
    EXPECT_EQ(planOf(request, 640u, 480u, 1u).scaleChannel(), 0u);

    an::LocalNormalizationRequest bad = request;
    bad.gridSize = 0u;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::OutOfRange);
    bad = request;
    bad.gridSize = an::kMaxLocalNormalizationGridSize + 1u;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::OutOfRange);
    bad = request;
    bad.backgroundPercentile = 1.5;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::OutOfRange);
    bad = request;
    bad.sigmaClip = 0.1;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::OutOfRange);
    bad = request;
    bad.sigmaClipIterations = 0u;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::OutOfRange);
    bad = request;
    bad.backgroundMinRoiExtent = 4u;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::OutOfRange);
    bad = request;
    bad.backgroundRoiOverlap = 0.99;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::OutOfRange);
    bad = request;
    bad.lowClip = 0.9;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.scaleSamples = 0u;
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.interpolation = static_cast<an::LocalGridInterpolation>(7u);
    EXPECT_EQ(an::planLocalNormalization(bad, 640u, 480u, 1u).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planLocalNormalization(request, 640u, 480u, 5u).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planLocalNormalization(request, 0u, 480u, 1u).status().code(), an::ErrorCode::InvalidArgument);

    const an::LocalNormalizationGridShape shape{.gridSize = 2u, .width = 16u, .height = 12u, .channelCount = 2u};
    EXPECT_TRUE(an::checkLocalNormalizationGridShape(shape).ok());
    EXPECT_EQ(an::localNormalizationNodeIndex(shape, 1u, 2u, 1u), 14u);
    an::LocalNormalizationGridShape badShape = shape;
    badShape.gridSize = 0u;
    EXPECT_EQ(an::checkLocalNormalizationGridShape(badShape).code(), an::ErrorCode::OutOfRange);
    badShape = shape;
    badShape.width = 0u;
    EXPECT_EQ(an::checkLocalNormalizationGridShape(badShape).code(), an::ErrorCode::InvalidArgument);
    badShape = shape;
    badShape.channelCount = 0u;
    EXPECT_EQ(an::checkLocalNormalizationGridShape(badShape).code(), an::ErrorCode::InvalidArgument);
    EXPECT_STREQ(an::localGridInterpolationName(an::LocalGridInterpolation::Bicubic), "Bicubic");
    EXPECT_STREQ(an::localBackgroundEstimatorName(an::LocalBackgroundEstimator::SigmaClippedMode), "SigmaClippedMode");
    EXPECT_STREQ(an::localScaleModeName(an::LocalScaleMode::PerChannel), "PerChannel");
    EXPECT_STREQ(an::localGridSmoothingName(an::LocalGridSmoothing::Gaussian), "Gaussian");
}

TEST(LocalNormalizationTest, SamplingVisitsOnlyAdmittedSamplesInAStableOrder)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> frame(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t y = 8u; y < kHeight - 8u; ++y)
    {
        for (std::uint32_t x = 8u; x < kWidth - 8u; ++x)
        {
            frame[static_cast<std::size_t>(y) * kWidth + x] = patterned(static_cast<std::size_t>(y) * kWidth + x, 1u);
        }
    }
    frame[static_cast<std::size_t>(20u) * kWidth + 20u] = std::numeric_limits<float>::quiet_NaN();
    an::LocalNormalizationRequest request;
    request.gridSize = 2u;
    request.backgroundMinRoiExtent = 8u;
    const an::LocalNormalizationPlan plan = planOf(request, kWidth, kHeight, 1u);
    ASSERT_TRUE(plan.valid());
    EXPECT_EQ(plan.roiWidth(), kWidth);
    EXPECT_EQ(plan.roiHeight(), kHeight);
    EXPECT_EQ(plan.samplesPerNode(), 256u);

    const an::NormalizationFrameView covered =
        frameOf(frame.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    std::vector<double> first(plan.samplesPerNode(), 0.0);
    std::vector<double> second(plan.samplesPerNode(), 0.0);
    const an::Result<std::uint32_t> firstCount =
        an::sampleLocalBackgroundNode(execution, covered, plan, 0u, 0u, 0u, first);
    const an::Result<std::uint32_t> secondCount =
        an::sampleLocalBackgroundNode(execution, covered, plan, 0u, 0u, 0u, second);
    ASSERT_TRUE(firstCount.hasValue()) << firstCount.status().message();
    ASSERT_TRUE(secondCount.hasValue()) << secondCount.status().message();
    EXPECT_EQ(*firstCount, *secondCount);
    EXPECT_GT(*firstCount, 0u);
    EXPECT_LT(*firstCount, plan.samplesPerNode());
    for (std::uint32_t index = 0u; index < *firstCount; ++index)
    {
        EXPECT_EQ(first[index], second[index]);
        EXPECT_GE(first[index], 0.2);
        EXPECT_LT(first[index], 0.6001);
        EXPECT_TRUE(std::isfinite(first[index]));
    }

    std::vector<float> full(frame.size(), 0.0f);
    for (std::size_t index = 0u; index < full.size(); ++index)
    {
        full[index] = patterned(index, 2u);
    }
    const an::NormalizationFrameView everything = frameOf(full.data(), layout, descriptor);
    const an::Result<std::uint32_t> fullCount =
        an::sampleLocalBackgroundNode(execution, everything, plan, 2u, 1u, 0u, first);
    ASSERT_TRUE(fullCount.hasValue()) << fullCount.status().message();
    EXPECT_EQ(*fullCount, plan.samplesPerNode());

    EXPECT_EQ(an::sampleLocalBackgroundNode(execution, everything, plan, 3u, 0u, 0u, first).status().code(),
              an::ErrorCode::InvalidArgument);
    std::vector<double> shortSpan(plan.samplesPerNode() - 1u, 0.0);
    EXPECT_EQ(an::sampleLocalBackgroundNode(execution, everything, plan, 0u, 0u, 0u, shortSpan).status().code(),
              an::ErrorCode::IncompatibleArguments);
    const an::LocalNormalizationPlan other = planOf(request, 32u, 32u, 1u);
    EXPECT_EQ(an::sampleLocalBackgroundNode(execution, everything, other, 0u, 0u, 0u, first).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(
        an::sampleLocalBackgroundNode(an::ExecutionContext{}, everything, plan, 0u, 0u, 0u, first).status().code(),
        an::ErrorCode::NotInitialized);
}

TEST(LocalNormalizationTest, AnUndersizedClippingPassPreservesThePreviousSampleSet)
{
    std::vector<double> samples(15u, 0.0);
    samples.push_back(1.0);
    samples.push_back(2.0);
    samples.insert(samples.end(), 15u, 3.0);
    for (const auto estimator :
         {an::LocalBackgroundEstimator::SigmaClippedMedian, an::LocalBackgroundEstimator::SigmaClippedMode})
    {
        an::LocalNormalizationRequest request;
        request.estimator = estimator;
        request.sigmaClip = 0.5;
        std::vector<double> work = samples;
        std::vector<double> scratch(samples.size());
        const auto centre = an::estimateLocalBackground(request, work, scratch);
        ASSERT_TRUE(centre.hasValue()) << centre.status().message();
        // The median and mean are both 1.5. Clipping would retain only 1 and 2,
        // so both estimators must still see all 32 original samples.
        EXPECT_DOUBLE_EQ(*centre, 1.5);
        std::sort(work.begin(), work.end());
        EXPECT_EQ(work, samples);
    }
}

TEST(LocalNormalizationTest, EstimatorsMatchTheirOracles)
{
    std::vector<double> inliers;
    for (std::size_t index = 0u; index < 200u; ++index)
    {
        inliers.push_back(patterned(index, 3u));
    }
    std::vector<double> samples = inliers;
    for (std::size_t index = 0u; index < 10u; ++index)
    {
        samples.push_back(5.0);
    }
    std::vector<double> scratch(samples.size(), 0.0);
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    an::LocalNormalizationRequest request;
    request.estimator = an::LocalBackgroundEstimator::Percentile;
    request.backgroundPercentile = 0.25;
    std::vector<double> work = samples;
    an::Result<double> percentile = an::estimateLocalBackground(request, work, scratch);
    ASSERT_TRUE(percentile.hasValue()) << percentile.status().message();
    EXPECT_EQ(*percentile, sorted[52u]);

    std::vector<double> sortedInliers = inliers;
    std::sort(sortedInliers.begin(), sortedInliers.end());
    const double medianOfInliers = sortedInliers[99u] + 0.5 * (sortedInliers[100u] - sortedInliers[99u]);
    request.estimator = an::LocalBackgroundEstimator::SigmaClippedMedian;
    work = samples;
    const an::Result<double> clipped = an::estimateLocalBackground(request, work, scratch);
    ASSERT_TRUE(clipped.hasValue()) << clipped.status().message();
    EXPECT_DOUBLE_EQ(*clipped, medianOfInliers);

    request.estimator = an::LocalBackgroundEstimator::SigmaClippedMode;
    work = samples;
    const an::Result<double> mode = an::estimateLocalBackground(request, work, scratch);
    ASSERT_TRUE(mode.hasValue()) << mode.status().message();
    const double mean = std::accumulate(inliers.begin(), inliers.end(), 0.0) / static_cast<double>(inliers.size());
    const double expectedMode =
        std::clamp(3.0 * medianOfInliers - 2.0 * mean, sortedInliers.front(), sortedInliers.back());
    EXPECT_NEAR(*mode, expectedMode, 1e-12);

    std::vector<double> none;
    EXPECT_EQ(an::estimateLocalBackground(request, none, scratch).status().code(), an::ErrorCode::InvalidArgument);
    std::vector<double> shortScratch(3u, 0.0);
    work = samples;
    EXPECT_EQ(an::estimateLocalBackground(request, work, shortScratch).status().code(), an::ErrorCode::InvalidArgument);
    request.estimator = static_cast<an::LocalBackgroundEstimator>(9u);
    EXPECT_EQ(an::estimateLocalBackground(request, work, scratch).status().code(), an::ErrorCode::InvalidArgument);
    request.estimator = an::LocalBackgroundEstimator::Percentile;
    work = samples;
    work[3] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::estimateLocalBackground(request, work, scratch).status().code(), an::ErrorCode::InvalidData);
}

TEST(LocalNormalizationTest, FitFollowsAPlaneAndIsThreadCountIndependent)
{
    const an::EngineContext engine = makeEngine(8u);
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 192u;
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> frame(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            frame[static_cast<std::size_t>(y) * kWidth + x] =
                static_cast<float>(planeAt(0.3, 0.1, 0.05, x, y, kWidth, kHeight));
        }
    }
    an::LocalNormalizationRequest request;
    request.estimator = an::LocalBackgroundEstimator::SigmaClippedMedian;
    request.backgroundRoiOverlap = 0.0;
    const an::LocalNormalizationPlan plan = planOf(request, kWidth, kHeight, 1u);
    ASSERT_TRUE(plan.valid());
    const an::NormalizationFrameView view = frameOf(frame.data(), layout, descriptor);

    std::vector<double> narrow(static_cast<std::size_t>(plan.shape().valueCount()), 0.0);
    std::vector<double> wide(narrow.size(), 0.0);
    const an::ExecutionContext single = makeExecution(engine, an::ComputeBackend::Automatic, {}, 1u);
    const an::ExecutionContext several = makeExecution(engine, an::ComputeBackend::Automatic, {}, 8u);
    const an::Result<an::LocalBackgroundFitResult> narrowFit = an::fitLocalBackgroundGrid(single, view, plan, narrow);
    const an::Result<an::LocalBackgroundFitResult> wideFit = an::fitLocalBackgroundGrid(several, view, plan, wide);
    ASSERT_TRUE(narrowFit.hasValue()) << narrowFit.status().message();
    ASSERT_TRUE(wideFit.hasValue()) << wideFit.status().message();
    EXPECT_EQ(narrowFit->nodeCount, 25u);
    EXPECT_EQ(narrowFit->channelCount, 1u);
    EXPECT_EQ(narrowFit->lowSampleNodes, 0u);
    EXPECT_EQ(narrowFit->unresolvedNodes, 0u);
    EXPECT_EQ(narrowFit->neighbourFallbacks, 0u);
    EXPECT_GT(narrowFit->samplesRead, 0u);
    EXPECT_GE(wideFit->plannedWorkers, 1u);
    EXPECT_FALSE(narrowFit->smoothed);
    for (std::size_t index = 0u; index < narrow.size(); ++index)
    {
        EXPECT_EQ(narrow[index], wide[index]) << index;
    }
    for (std::uint32_t ny = 0u; ny < plan.shape().nodesPerAxis(); ++ny)
    {
        for (std::uint32_t nx = 0u; nx < plan.shape().nodesPerAxis(); ++nx)
        {
            const double centreX = plan.roiOriginX(nx) + 0.5 * (plan.roiWidth() - 1u);
            const double centreY = plan.roiOriginY(ny) + 0.5 * (plan.roiHeight() - 1u);
            const double expected = 0.3 + 0.1 * centreX / (kWidth - 1u) + 0.05 * centreY / (kHeight - 1u);
            EXPECT_NEAR(narrow[an::localNormalizationNodeIndex(plan.shape(), 0u, nx, ny)], expected, 0.006)
                << nx << ',' << ny;
        }
    }

    an::LocalNormalizationRequest smoothing = request;
    smoothing.backgroundSmoothingRadius = 1u;
    const an::LocalNormalizationPlan smoothingPlan = planOf(smoothing, kWidth, kHeight, 1u);
    std::vector<double> smoothed(narrow.size(), 0.0);
    const an::Result<an::LocalBackgroundFitResult> smoothedFit =
        an::fitLocalBackgroundGrid(several, view, smoothingPlan, smoothed);
    ASSERT_TRUE(smoothedFit.hasValue()) << smoothedFit.status().message();
    EXPECT_TRUE(smoothedFit->smoothed);
    EXPECT_NEAR(smoothed[an::localNormalizationNodeIndex(plan.shape(), 0u, 2u, 2u)],
                narrow[an::localNormalizationNodeIndex(plan.shape(), 0u, 2u, 2u)], 0.01);

    std::vector<double> wrong(narrow.size() - 1u, 0.0);
    EXPECT_EQ(an::fitLocalBackgroundGrid(several, view, plan, wrong).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::fitLocalBackgroundGrid(several, view, an::LocalNormalizationPlan{}, narrow).status().code(),
              an::ErrorCode::NotInitialized);
}

TEST(LocalNormalizationTest, FitRecoversUncoveredNodesAndCountsEveryFallback)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 512u;
    constexpr std::uint32_t kHeight = 64u;
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> frame(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < frame.size(); ++index)
    {
        frame[index] = 0.4f + 0.25f * (patterned(index, 4u) - 0.4f);
    }
    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>((kWidth + 7u) / 8u) * kHeight, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, descriptor.extent());
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x <= 300u; ++x)
        {
            ASSERT_TRUE(writable->setValid(0u, x, y, false));
        }
    }
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const an::KernelValidityView masked{.descriptor = ruleOf(an::ValidityRule::ExplicitMask), .mask = &mask};
    const an::Result<an::NormalizationFrameView> view =
        an::NormalizationFrameView::create(frame.data(), layout.totalBytes(), layout, descriptor, masked);
    ASSERT_TRUE(view.hasValue()) << view.status().message();

    an::LocalNormalizationRequest request;
    request.gridSize = 8u;
    request.backgroundRoiOverlap = 0.5;
    const an::LocalNormalizationPlan plan = planOf(request, kWidth, kHeight, 1u);
    ASSERT_TRUE(plan.valid());
    EXPECT_EQ(plan.roiWidth(), 128u);
    std::vector<double> values(static_cast<std::size_t>(plan.shape().valueCount()), 0.0);
    const an::Result<an::LocalBackgroundFitResult> fit = an::fitLocalBackgroundGrid(execution, *view, plan, values);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_GE(fit->lowSampleNodes, 3u);
    EXPECT_GE(fit->expansionFallbacks, 1u);
    EXPECT_GE(fit->neighbourFallbacks, 1u);
    EXPECT_GE(fit->unresolvedNodes, 1u);
    for (const double value : values)
    {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GT(value, 0.3);
        EXPECT_LT(value, 0.5);
    }

    std::vector<float> nothing(frame.size(), 0.0f);
    const an::NormalizationFrameView empty =
        frameOf(nothing.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    EXPECT_EQ(an::fitLocalBackgroundGrid(execution, empty, plan, values).status().code(),
              an::ErrorCode::EmptySelection);
}

TEST(LocalNormalizationTest, EvaluationReproducesPlanesAndApplyEqualsEvaluation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 40u;
    constexpr std::uint32_t kHeight = 30u;
    an::LocalNormalizationRequest request;
    request.gridSize = 3u;
    const an::LocalNormalizationPlan plan = planOf(request, kWidth, kHeight, 1u);
    const an::LocalNormalizationGridShape& shape = plan.shape();
    const std::vector<double> grid = planeGrid(plan, 0.1, 0.08, 0.06);
    // Bilinear reproduces a plane everywhere. Bicubic reproduces it wherever all four contributing
    // nodes exist; in the edge cells the clamped outer node turns the cubic into something else,
    // so there the surface is only close.
    for (const an::LocalGridInterpolation interpolation :
         {an::LocalGridInterpolation::Bilinear, an::LocalGridInterpolation::Bicubic})
    {
        for (const double x : {0.0, 3.7, 12.25, 15.5, 20.0, 25.9, 26.5, 39.0})
        {
            for (const double y : {0.0, 4.1, 10.5, 14.9, 19.0, 29.0})
            {
                const an::Result<double> value = an::evaluateLocalGrid(shape, grid, interpolation, 0u, x, y);
                ASSERT_TRUE(value.hasValue()) << value.status().message();
                const double expected = 0.1 + 0.08 * x / (kWidth - 1u) + 0.06 * y / (kHeight - 1u);
                const bool interior = x >= 13.0 && x < 26.0 && y >= 29.0 / 3.0 && y < 58.0 / 3.0;
                const double tolerance =
                    interpolation == an::LocalGridInterpolation::Bilinear || interior ? 1e-12 : 1e-2;
                EXPECT_NEAR(*value, expected, tolerance)
                    << an::localGridInterpolationName(interpolation) << ' ' << x << ',' << y;
            }
        }
    }
    EXPECT_EQ(an::evaluateLocalGrid(shape, grid, an::LocalGridInterpolation::Bicubic, 1u, 0.0, 0.0).status().code(),
              an::ErrorCode::InvalidArgument);
    std::vector<double> wrong(grid.size() + 1u, 0.0);
    EXPECT_EQ(an::evaluateLocalGrid(shape, wrong, an::LocalGridInterpolation::Bicubic, 0u, 0.0, 0.0).status().code(),
              an::ErrorCode::IncompatibleArguments);

    for (const an::SampleType sampleType : {an::SampleType::Float32, an::SampleType::Float64})
    {
        for (const an::StorageArrangement arrangement :
             {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
        {
            const an::ImageDescriptor descriptor = descriptorOf(sampleType, kWidth, kHeight, an::ChannelSchema::Rgb);
            const an::ImageLayout layout = layoutOf(descriptor, arrangement);
            const an::LocalNormalizationPlan colourPlan = planOf(request, kWidth, kHeight, 3u);
            const std::vector<double> target = planeGrid(colourPlan, 0.1, 0.08, 0.06);
            const std::vector<double> reference = planeGrid(colourPlan, 0.2, -0.04, 0.02);
            std::vector<double> scaleGrid = planeGrid(colourPlan, 1.1, 0.2, -0.1);
            const std::array<double, 3> scale{1.5, 0.75, 1.25};
            std::vector<std::byte> bytes(layout.totalBytes());
            std::vector<std::byte> output(layout.totalBytes());
            const auto put = [&](std::uint32_t x, std::uint32_t y, std::uint32_t channel, double value) {
                const std::uint32_t plane = channel / layout.samplesPerPixel();
                const std::uint32_t inPixel = channel % layout.samplesPerPixel();
                const std::uint64_t offset =
                    plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
                    (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * layout.elementSizeBytes();
                if (sampleType == an::SampleType::Float32)
                {
                    const auto narrowed = static_cast<float>(value);
                    std::memcpy(bytes.data() + offset, &narrowed, sizeof(float));
                }
                else
                {
                    std::memcpy(bytes.data() + offset, &value, sizeof(double));
                }
            };
            const auto get = [&](const std::vector<std::byte>& from, std::uint32_t x, std::uint32_t y,
                                 std::uint32_t channel) {
                const std::uint32_t plane = channel / layout.samplesPerPixel();
                const std::uint32_t inPixel = channel % layout.samplesPerPixel();
                const std::uint64_t offset =
                    plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
                    (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * layout.elementSizeBytes();
                if (sampleType == an::SampleType::Float32)
                {
                    float value = 0.0f;
                    std::memcpy(&value, from.data() + offset, sizeof(float));
                    return static_cast<double>(value);
                }
                double value = 0.0;
                std::memcpy(&value, from.data() + offset, sizeof(double));
                return value;
            };
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        put(x, y, channel, patterned(static_cast<std::size_t>(channel * kHeight + y) * kWidth + x, 5u));
                    }
                }
            }
            const an::NormalizationFrameView view = frameOf(bytes.data(), layout, descriptor);
            for (const bool useScaleGrid : {false, true})
            {
                const an::LocalNormalizationApplyRequest apply{
                    .shape = colourPlan.shape(),
                    .interpolation = an::LocalGridInterpolation::Bicubic,
                    .targetBackground = target,
                    .referenceBackground = reference,
                    .scale = scale,
                    .scaleGrid = useScaleGrid ? std::span<const double>{scaleGrid} : std::span<const double>{}};
                const an::Result<an::LocalNormalizationApplyResult> applied =
                    an::applyLocalNormalization(execution, apply, view, output.data(), layout.totalBytes(), layout);
                ASSERT_TRUE(applied.hasValue()) << applied.status().message();
                EXPECT_EQ(applied->samplesWritten, static_cast<std::uint64_t>(kWidth) * kHeight * 3u);
                EXPECT_EQ(applied->samplesTransformed, applied->samplesWritten);
                EXPECT_EQ(applied->imagePasses, 1u);
                EXPECT_GT(applied->workingBytes, 0u);
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    for (std::uint32_t y = 0u; y < kHeight; ++y)
                    {
                        for (std::uint32_t x = 0u; x < kWidth; ++x)
                        {
                            const double sample = get(bytes, x, y, channel);
                            const double targetLevel = *an::evaluateLocalGrid(
                                colourPlan.shape(), target, an::LocalGridInterpolation::Bicubic, channel, x, y);
                            const double referenceLevel = *an::evaluateLocalGrid(
                                colourPlan.shape(), reference, an::LocalGridInterpolation::Bicubic, channel, x, y);
                            const double k = useScaleGrid ? *an::evaluateLocalGrid(colourPlan.shape(), scaleGrid,
                                                                                   an::LocalGridInterpolation::Bicubic,
                                                                                   channel, x, y)
                                                          : scale[channel];
                            const double expected = k * (sample - targetLevel) + referenceLevel;
                            const double actual = get(output, x, y, channel);
                            if (sampleType == an::SampleType::Float32)
                            {
                                EXPECT_EQ(static_cast<float>(actual), static_cast<float>(expected))
                                    << x << ',' << y << ',' << channel;
                            }
                            else
                            {
                                EXPECT_EQ(actual, expected) << x << ',' << y << ',' << channel;
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST(LocalNormalizationTest, ApplyPreservesExcludedSamplesAndRefusesWhatItCannotWrite)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 4u, 1u);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::array<float, 4> source{0.0f, 0.25f, 0.5f, -0.125f};
    std::array<float, 4> output{};
    const an::LocalNormalizationGridShape shape{.gridSize = 1u, .width = 4u, .height = 1u, .channelCount = 1u};
    const std::vector<double> target(4u, 0.1);
    const std::vector<double> reference(4u, 0.3);
    const std::array<double, 1> scale{2.0};
    an::LocalNormalizationApplyRequest request{.shape = shape,
                                               .interpolation = an::LocalGridInterpolation::Bilinear,
                                               .targetBackground = target,
                                               .referenceBackground = reference,
                                               .scale = scale,
                                               .scaleGrid = {}};

    const an::NormalizationFrameView canvas =
        frameOf(source.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    const an::Result<an::LocalNormalizationApplyResult> applied =
        an::applyLocalNormalization(execution, request, canvas, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(output[0], 0.0f);
    EXPECT_EQ(output[1], static_cast<float>(2.0 * (static_cast<double>(0.25f) - 0.1) + 0.3));
    EXPECT_EQ(output[2], static_cast<float>(2.0 * (static_cast<double>(0.5f) - 0.1) + 0.3));
    EXPECT_EQ(output[3], static_cast<float>(2.0 * (static_cast<double>(-0.125f) - 0.1) + 0.3));
    EXPECT_EQ(applied->samplesWritten, 4u);
    EXPECT_EQ(applied->samplesTransformed, 3u);
    EXPECT_EQ(applied->samplesPreserved, 1u);
    EXPECT_EQ(applied->noDataCollisions, 0u);
    EXPECT_NE(applied->backend.instructionSet, an::InstructionSet::Automatic);

    const std::array<float, 4> holed{0.5f, std::numeric_limits<float>::quiet_NaN(), 0.25f, 1.0f};
    const an::NormalizationFrameView finite = frameOf(holed.data(), layout, descriptor, an::ValidityRule::FiniteOnly);
    const an::Result<an::LocalNormalizationApplyResult> finiteApplied =
        an::applyLocalNormalization(execution, request, finite, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(finiteApplied.hasValue()) << finiteApplied.status().message();
    EXPECT_TRUE(std::isnan(output[1]));
    EXPECT_EQ(finiteApplied->samplesPreserved, 1u);

    // A sample that sits exactly on its own background maps to the reference's; with a zero
    // reference that is the marker, so it is moved off it under the zero rule and not otherwise.
    const std::array<float, 4> onLevel{0.1f, 0.2f, 0.3f, 0.4f};
    const std::vector<double> levelTarget(4u, static_cast<double>(0.1f));
    const std::vector<double> zeroReference(4u, 0.0);
    request.targetBackground = levelTarget;
    request.referenceBackground = zeroReference;
    const an::NormalizationFrameView colliding =
        frameOf(onLevel.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    const an::Result<an::LocalNormalizationApplyResult> collided =
        an::applyLocalNormalization(execution, request, colliding, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(collided.hasValue()) << collided.status().message();
    EXPECT_EQ(output[0], (std::numeric_limits<float>::min)());
    EXPECT_EQ(collided->noDataCollisions, 1u);
    const an::NormalizationFrameView plainLevel = frameOf(onLevel.data(), layout, descriptor);
    const an::Result<an::LocalNormalizationApplyResult> uncollided =
        an::applyLocalNormalization(execution, request, plainLevel, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(uncollided.hasValue()) << uncollided.status().message();
    EXPECT_EQ(output[0], 0.0f);
    EXPECT_EQ(uncollided->noDataCollisions, 0u);
    request.targetBackground = target;
    request.referenceBackground = reference;

    std::array<std::uint8_t, 1> maskBytes{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, descriptor.extent());
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    ASSERT_TRUE(writableMask->setValid(0u, 2u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    const an::KernelValidityView masked{.descriptor = ruleOf(an::ValidityRule::ExplicitMask), .mask = &mask};
    const an::Result<an::NormalizationFrameView> maskedView =
        an::NormalizationFrameView::create(source.data(), layout.totalBytes(), layout, descriptor, masked);
    ASSERT_TRUE(maskedView.hasValue()) << maskedView.status().message();
    const an::Result<an::LocalNormalizationApplyResult> maskedApplied =
        an::applyLocalNormalization(execution, request, *maskedView, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(maskedApplied.hasValue()) << maskedApplied.status().message();
    EXPECT_EQ(output[2], 0.5f);
    EXPECT_EQ(output[0], static_cast<float>(2.0 * (0.0 - 0.1) + 0.3));
    EXPECT_EQ(maskedApplied->samplesPreserved, 1u);
    EXPECT_EQ(maskedApplied->path, an::NormalizationExecutionPath::Reference);
    const an::ExecutionContext forced =
        makeExecution(engine, an::ComputeBackend::Cpu, {}, 3u, nullptr, an::BackendFallback::Forbidden);
    EXPECT_EQ(an::applyLocalNormalization(forced, request, *maskedView, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::UnsupportedBackend);

    std::array<float, 4> inPlace = source;
    const an::NormalizationFrameView resident =
        frameOf(inPlace.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    ASSERT_TRUE(an::applyLocalNormalization(execution, request, resident, inPlace.data(), layout.totalBytes(), layout)
                    .hasValue());
    EXPECT_EQ(inPlace[0], 0.0f);
    EXPECT_EQ(inPlace[1],
              output[1] == output[1] ? static_cast<float>(2.0 * (static_cast<double>(0.25f) - 0.1) + 0.3) : inPlace[1]);
    std::array<float, 8> overlapping{};
    std::copy(source.begin(), source.end(), overlapping.begin());
    const an::NormalizationFrameView front = frameOf(overlapping.data(), layout, descriptor);
    EXPECT_EQ(
        an::applyLocalNormalization(execution, request, front, overlapping.data() + 1u, layout.totalBytes(), layout)
            .status()
            .code(),
        an::ErrorCode::Aliasing);
    EXPECT_EQ(an::applyLocalNormalization(execution, request, front, output.data(), layout.totalBytes() - 1u, layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    const std::optional<an::ValidityDescriptor> ranged = an::ValidityDescriptor::create(
        an::ValidityDescriptorRequest{.rule = an::ValidityRule::DomainRange, .lowestValid = 0.0, .highestValid = 1.0});
    ASSERT_TRUE(ranged.has_value());
    const an::Result<an::NormalizationFrameView> rangedView = an::NormalizationFrameView::create(
        source.data(), layout.totalBytes(), layout, descriptor, an::KernelValidityView{.descriptor = *ranged});
    ASSERT_TRUE(rangedView.hasValue());
    EXPECT_EQ(an::applyLocalNormalization(execution, request, *rangedView, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);

    an::LocalNormalizationApplyRequest wrong = request;
    const std::array<double, 2> twoScales{1.0, 1.0};
    wrong.scale = twoScales;
    EXPECT_EQ(an::applyLocalNormalization(execution, wrong, front, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    wrong = request;
    const std::array<double, 1> zeroScale{0.0};
    wrong.scale = zeroScale;
    EXPECT_EQ(an::applyLocalNormalization(execution, wrong, front, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    wrong = request;
    wrong.shape.width = 5u;
    const std::vector<double> five(4u, 0.1);
    EXPECT_EQ(an::applyLocalNormalization(execution, wrong, front, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    wrong = request;
    wrong.interpolation = static_cast<an::LocalGridInterpolation>(5u);
    EXPECT_EQ(an::applyLocalNormalization(execution, wrong, front, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    wrong = request;
    std::vector<double> holedGrid = target;
    holedGrid[1] = std::numeric_limits<double>::infinity();
    wrong.targetBackground = holedGrid;
    EXPECT_EQ(an::applyLocalNormalization(execution, wrong, front, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::applyLocalNormalization(an::ExecutionContext{}, request, front, output.data(), layout.totalBytes(), layout)
            .status()
            .code(),
        an::ErrorCode::NotInitialized);
}

TEST(LocalNormalizationTest, ReferenceAndCompiledRowsStoreTheSameBits)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext referenceExecution = makeExecution(engine, an::ComputeBackend::Reference);
    const an::ExecutionContext compiledExecution = makeExecution(engine);
    for (const an::SampleType sampleType : {an::SampleType::Float32, an::SampleType::Float64})
    {
        for (const an::LocalGridInterpolation interpolation :
             {an::LocalGridInterpolation::Bicubic, an::LocalGridInterpolation::Bilinear})
        {
            constexpr std::uint32_t kWidth = 129u;
            constexpr std::uint32_t kHeight = 7u;
            const an::ImageDescriptor descriptor = descriptorOf(sampleType, kWidth, kHeight, an::ChannelSchema::Rgb);
            const an::ImageLayout layout =
                layoutOf(descriptor, sampleType == an::SampleType::Float32 ? an::StorageArrangement::Interleaved
                                                                           : an::StorageArrangement::Planar);
            an::LocalNormalizationRequest request;
            request.gridSize = 5u;
            request.interpolation = interpolation;
            const an::LocalNormalizationPlan plan = planOf(request, kWidth, kHeight, 3u);
            std::vector<double> target(static_cast<std::size_t>(plan.shape().valueCount()), 0.0);
            std::vector<double> reference(target.size(), 0.0);
            std::vector<double> scaleGrid(target.size(), 0.0);
            for (std::size_t index = 0u; index < target.size(); ++index)
            {
                target[index] = patterned(index, 6u) * 0.25;
                reference[index] = patterned(index, 7u) * 0.3;
                scaleGrid[index] = 0.75 + patterned(index, 8u);
            }
            const std::array<double, 3> scale{1.03125, 0.875, 1.25};
            const std::size_t count = static_cast<std::size_t>(kWidth) * kHeight * 3u;
            std::vector<std::byte> bytes(layout.totalBytes());
            for (std::size_t index = 0u; index < count; ++index)
            {
                double value = index % 31u == 0u ? 0.0 : static_cast<double>(patterned(index, 9u)) - 0.35;
                if (index % 101u == 0u)
                {
                    value = std::numeric_limits<double>::quiet_NaN();
                }
                if (sampleType == an::SampleType::Float32)
                {
                    const auto narrowed = static_cast<float>(value);
                    std::memcpy(bytes.data() + index * sizeof(float), &narrowed, sizeof(float));
                }
                else
                {
                    std::memcpy(bytes.data() + index * sizeof(double), &value, sizeof(double));
                }
            }
            const an::NormalizationFrameView view =
                frameOf(bytes.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
            const an::LocalNormalizationApplyRequest apply{.shape = plan.shape(),
                                                           .interpolation = interpolation,
                                                           .targetBackground = target,
                                                           .referenceBackground = reference,
                                                           .scale = scale,
                                                           .scaleGrid = scaleGrid};
            std::vector<std::byte> referenceOut(layout.totalBytes());
            std::vector<std::byte> compiledOut(layout.totalBytes());
            const an::Result<an::LocalNormalizationApplyResult> referenceRun = an::applyLocalNormalization(
                referenceExecution, apply, view, referenceOut.data(), layout.totalBytes(), layout);
            const an::Result<an::LocalNormalizationApplyResult> compiledRun = an::applyLocalNormalization(
                compiledExecution, apply, view, compiledOut.data(), layout.totalBytes(), layout);
            ASSERT_TRUE(referenceRun.hasValue()) << referenceRun.status().message();
            ASSERT_TRUE(compiledRun.hasValue()) << compiledRun.status().message();
            EXPECT_EQ(referenceRun->path, an::NormalizationExecutionPath::Reference);
            EXPECT_EQ(referenceRun->samplesTransformed, compiledRun->samplesTransformed);
            EXPECT_EQ(referenceRun->samplesPreserved, compiledRun->samplesPreserved);
            EXPECT_EQ(referenceRun->noDataCollisions, compiledRun->noDataCollisions);
            if (compiledRun->backend.instructionSet != an::InstructionSet::Scalar)
            {
                EXPECT_EQ(compiledRun->path, an::NormalizationExecutionPath::CompiledRow);
            }
            for (std::size_t index = 0u; index < count; ++index)
            {
                if (sampleType == an::SampleType::Float32)
                {
                    float a = 0.0f;
                    float b = 0.0f;
                    std::memcpy(&a, referenceOut.data() + index * sizeof(float), sizeof(float));
                    std::memcpy(&b, compiledOut.data() + index * sizeof(float), sizeof(float));
                    if (std::isnan(a))
                    {
                        EXPECT_TRUE(std::isnan(b)) << index;
                    }
                    else
                    {
                        EXPECT_EQ(a, b) << index;
                    }
                }
                else
                {
                    double a = 0.0;
                    double b = 0.0;
                    std::memcpy(&a, referenceOut.data() + index * sizeof(double), sizeof(double));
                    std::memcpy(&b, compiledOut.data() + index * sizeof(double), sizeof(double));
                    if (std::isnan(a))
                    {
                        EXPECT_TRUE(std::isnan(b)) << index;
                    }
                    else
                    {
                        EXPECT_EQ(a, b) << index;
                    }
                }
            }
        }
    }
}

/// Two frames related by a scale and a plane, with their fitted grids.
struct ScalePair
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<float> reference;
    std::vector<float> target;
    an::LocalNormalizationPlan plan;
    std::vector<double> referenceGrid;
    std::vector<double> targetGrid;
};

[[nodiscard]] ScalePair makeScalePair(const an::ExecutionContext& execution, std::uint32_t channels, double factor,
                                      double gradient, bool varying)
{
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 192u;
    const an::ChannelSchema schema = channels == 3u ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono;
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kWidth, kHeight, schema);
    ScalePair pair{.descriptor = descriptor,
                   .layout = layoutOf(descriptor),
                   .reference = {},
                   .target = {},
                   .plan = {},
                   .referenceGrid = {},
                   .targetGrid = {}};
    const std::size_t count = static_cast<std::size_t>(kWidth) * kHeight * channels;
    pair.reference.assign(count, 0.0f);
    pair.target.assign(count, 0.0f);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = (static_cast<std::size_t>(channel) * kHeight + y) * kWidth + x;
                const double noise = 0.05 * (static_cast<double>(patterned(index, 10u + channel)) - 0.4);
                const double star = (x % 8u < 4u && y % 8u < 4u) ? 0.2 : 0.0;
                const double value = 0.3 + star + noise;
                const double k = varying ? factor + 0.4 * static_cast<double>(x) / (kWidth - 1u) : factor;
                pair.reference[index] = static_cast<float>(value);
                pair.target[index] =
                    static_cast<float>(k * value + gradient * static_cast<double>(x + y) / (kWidth + kHeight - 2u));
            }
        }
    }
    an::LocalNormalizationRequest request;
    request.gridSize = 4u;
    pair.plan = planOf(request, kWidth, kHeight, channels);
    pair.referenceGrid.assign(static_cast<std::size_t>(pair.plan.shape().valueCount()), 0.0);
    pair.targetGrid.assign(pair.referenceGrid.size(), 0.0);
    const an::NormalizationFrameView referenceView = frameOf(pair.reference.data(), pair.layout, pair.descriptor);
    const an::NormalizationFrameView targetView = frameOf(pair.target.data(), pair.layout, pair.descriptor);
    const an::Result<an::LocalBackgroundFitResult> referenceFit =
        an::fitLocalBackgroundGrid(execution, referenceView, pair.plan, pair.referenceGrid);
    const an::Result<an::LocalBackgroundFitResult> targetFit =
        an::fitLocalBackgroundGrid(execution, targetView, pair.plan, pair.targetGrid);
    EXPECT_TRUE(referenceFit.hasValue()) << referenceFit.status().message();
    EXPECT_TRUE(targetFit.hasValue()) << targetFit.status().message();
    return pair;
}

TEST(LocalNormalizationTest, ScaleEstimateRecoversTheFactorBetweenTwoFrames)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const ScalePair mono = makeScalePair(execution, 1u, 0.8, 0.02, false);
    const an::NormalizationFrameView reference = frameOf(mono.reference.data(), mono.layout, mono.descriptor);
    const an::NormalizationFrameView target = frameOf(mono.target.data(), mono.layout, mono.descriptor);
    const an::Result<an::LocalScaleEstimate> estimate =
        an::estimateLocalScale(execution, target, mono.targetGrid, reference, mono.referenceGrid, mono.plan);
    ASSERT_TRUE(estimate.hasValue()) << estimate.status().message();
    EXPECT_EQ(estimate->channelCount, 1u);
    EXPECT_NEAR(estimate->scale[0], 1.25, 0.05);
    EXPECT_GE(estimate->ratiosUsed[0], an::kMinLocalNormalizationScaleRatios);
    EXPECT_EQ(estimate->unityFallbacks, 0u);
    EXPECT_GT(estimate->workingBytes, 0u);

    const ScalePair colour = makeScalePair(execution, 3u, 0.8, 0.02, false);
    const an::NormalizationFrameView colourReference =
        frameOf(colour.reference.data(), colour.layout, colour.descriptor);
    const an::NormalizationFrameView colourTarget = frameOf(colour.target.data(), colour.layout, colour.descriptor);
    const an::Result<an::LocalScaleEstimate> linked = an::estimateLocalScale(
        execution, colourTarget, colour.targetGrid, colourReference, colour.referenceGrid, colour.plan);
    ASSERT_TRUE(linked.hasValue()) << linked.status().message();
    EXPECT_EQ(linked->measuredChannel, 1u);
    EXPECT_EQ(linked->scale[0], linked->scale[1]);
    EXPECT_EQ(linked->scale[1], linked->scale[2]);
    EXPECT_NEAR(linked->scale[1], 1.25, 0.05);
    an::LocalNormalizationRequest perChannelRequest = colour.plan.request();
    perChannelRequest.scaleMode = an::LocalScaleMode::PerChannel;
    const an::LocalNormalizationPlan perChannelPlan = planOf(perChannelRequest, 256u, 192u, 3u);
    const an::Result<an::LocalScaleEstimate> separate = an::estimateLocalScale(
        execution, colourTarget, colour.targetGrid, colourReference, colour.referenceGrid, perChannelPlan);
    ASSERT_TRUE(separate.hasValue()) << separate.status().message();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(separate->scale[channel], 1.25, 0.05) << channel;
        EXPECT_GE(separate->ratiosUsed[channel], an::kMinLocalNormalizationScaleRatios);
    }

    std::vector<double> wrong(mono.targetGrid.size() + 1u, 0.0);
    EXPECT_EQ(
        an::estimateLocalScale(execution, target, wrong, reference, mono.referenceGrid, mono.plan).status().code(),
        an::ErrorCode::IncompatibleArguments);
    std::vector<double> holed = mono.targetGrid;
    holed[0] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        an::estimateLocalScale(execution, target, holed, reference, mono.referenceGrid, mono.plan).status().code(),
        an::ErrorCode::InvalidData);
    EXPECT_EQ(an::estimateLocalScale(execution, colourTarget, mono.targetGrid, reference, mono.referenceGrid, mono.plan)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(LocalNormalizationTest, ScaleGridFollowsASpatiallyVaryingFactor)
{
    const an::EngineContext engine = makeEngine(16u);
    const an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Automatic, {}, 8u);
    const ScalePair pair = makeScalePair(execution, 1u, 0.8, 0.0, true);
    const an::NormalizationFrameView reference = frameOf(pair.reference.data(), pair.layout, pair.descriptor);
    const an::NormalizationFrameView target = frameOf(pair.target.data(), pair.layout, pair.descriptor);
    // The factor is unity at the frame's centre, which is the global scale a caller states; the
    // grid is normalized so its median node equals it and is then read for its trend.
    const std::array<double, 1> unity{1.0};
    const std::span<const double> globalScale{unity};
    std::vector<double> grid(pair.targetGrid.size(), 0.0);
    const an::Result<an::LocalScaleGridResult> fitted = an::fitLocalScaleGrid(
        execution, target, pair.targetGrid, reference, pair.referenceGrid, pair.plan, globalScale, grid);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_EQ(fitted->nodeCount, 25u);
    EXPECT_EQ(fitted->channelCount, 1u);
    EXPECT_EQ(fitted->confidenceFallbacks, 0u);
    EXPECT_EQ(fitted->normalizedChannels, 1u);
    EXPECT_TRUE(fitted->smoothed);
    EXPECT_GT(fitted->workingBytes, 0u);
    const an::LocalNormalizationGridShape& shape = pair.plan.shape();
    for (std::uint32_t ny = 0u; ny < shape.nodesPerAxis(); ++ny)
    {
        const double left = grid[an::localNormalizationNodeIndex(shape, 0u, 0u, ny)];
        const double right = grid[an::localNormalizationNodeIndex(shape, 0u, shape.gridSize, ny)];
        EXPECT_GT(left, right) << ny;
        for (std::uint32_t nx = 0u; nx < shape.nodesPerAxis(); ++nx)
        {
            const double value = grid[an::localNormalizationNodeIndex(shape, 0u, nx, ny)];
            EXPECT_GT(value, 0.7) << nx << ',' << ny;
            EXPECT_LT(value, 1.4) << nx << ',' << ny;
        }
    }

    const an::ExecutionContext single = makeExecution(engine, an::ComputeBackend::Automatic, {}, 1u);
    std::vector<double> narrow(grid.size(), 0.0);
    ASSERT_TRUE(an::fitLocalScaleGrid(single, target, pair.targetGrid, reference, pair.referenceGrid, pair.plan,
                                      globalScale, narrow)
                    .hasValue());
    for (std::size_t index = 0u; index < grid.size(); ++index)
    {
        EXPECT_EQ(narrow[index], grid[index]) << index;
    }

    an::LocalNormalizationRequest unsmoothed = pair.plan.request();
    unsmoothed.localScaleSmoothingRadius = 0u;
    const an::LocalNormalizationPlan unsmoothedPlan = planOf(unsmoothed, 256u, 192u, 1u);
    const an::Result<an::LocalScaleGridResult> plain = an::fitLocalScaleGrid(
        execution, target, pair.targetGrid, reference, pair.referenceGrid, unsmoothedPlan, globalScale, grid);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_FALSE(plain->smoothed);

    const std::array<double, 2> twoScales{1.0, 1.0};
    EXPECT_EQ(an::fitLocalScaleGrid(execution, target, pair.targetGrid, reference, pair.referenceGrid, pair.plan,
                                    twoScales, grid)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    const std::array<double, 1> zero{0.0};
    EXPECT_EQ(
        an::fitLocalScaleGrid(execution, target, pair.targetGrid, reference, pair.referenceGrid, pair.plan, zero, grid)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
}

TEST(LocalNormalizationTest, CombineTakesTheNodeMedianAndSmoothingKeepsAConstant)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::LocalNormalizationGridShape shape{.gridSize = 2u, .width = 20u, .height = 20u, .channelCount = 1u};
    const std::vector<double> a{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    const std::vector<double> b{9.0, 8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0};
    const std::vector<double> c{5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0};
    std::vector<double> combined(9u, 0.0);
    const std::array<std::span<const double>, 3> three{a, b, c};
    ASSERT_TRUE(an::combineLocalGrids(shape, three, combined).ok());
    for (std::size_t index = 0u; index < 9u; ++index)
    {
        std::array<double, 3> values{a[index], b[index], c[index]};
        std::sort(values.begin(), values.end());
        EXPECT_EQ(combined[index], values[1]) << index;
    }
    const std::array<std::span<const double>, 2> two{a, b};
    ASSERT_TRUE(an::combineLocalGrids(shape, two, combined).ok());
    EXPECT_EQ(combined[0], 5.0);
    EXPECT_EQ(combined[8], 5.0);
    std::vector<double> inPlace = a;
    const std::array<std::span<const double>, 2> self{inPlace, b};
    ASSERT_TRUE(an::combineLocalGrids(shape, self, inPlace).ok());
    EXPECT_EQ(inPlace[0], 5.0);
    const std::array<std::span<const double>, 0> none{};
    EXPECT_EQ(an::combineLocalGrids(shape, none, combined).code(), an::ErrorCode::InvalidArgument);
    std::vector<std::span<const double>> tooMany(an::kMaxLocalNormalizationGridMembers + 1u,
                                                 std::span<const double>{a});
    EXPECT_EQ(an::combineLocalGrids(shape, tooMany, combined).code(), an::ErrorCode::LimitExceeded);
    std::vector<double> holed = a;
    holed[4] = std::numeric_limits<double>::quiet_NaN();
    const std::array<std::span<const double>, 1> bad{holed};
    EXPECT_EQ(an::combineLocalGrids(shape, bad, combined).code(), an::ErrorCode::InvalidData);
    std::vector<double> shortOut(8u, 0.0);
    EXPECT_EQ(an::combineLocalGrids(shape, three, shortOut).code(), an::ErrorCode::IncompatibleArguments);

    std::vector<double> constant(9u, 0.375);
    ASSERT_TRUE(an::smoothLocalGrid(execution, shape, constant, 1u, an::LocalGridSmoothing::Gaussian).ok());
    for (const double value : constant)
    {
        EXPECT_NEAR(value, 0.375, 1e-12);
    }
    ASSERT_TRUE(an::smoothLocalGrid(execution, shape, constant, 1u, an::LocalGridSmoothing::Box).ok());
    for (const double value : constant)
    {
        EXPECT_NEAR(value, 0.375, 1e-12);
    }
    std::vector<double> spike(9u, 0.0);
    spike[4] = 1.0;
    ASSERT_TRUE(an::smoothLocalGrid(execution, shape, spike, 1u, an::LocalGridSmoothing::Box).ok());
    EXPECT_NEAR(spike[4], 1.0 / 9.0, 1e-12);
    EXPECT_NEAR(spike[0], 0.25, 1e-12);
    EXPECT_NEAR(spike[1], 1.0 / 6.0, 1e-12);
    std::vector<double> untouched = a;
    ASSERT_TRUE(an::smoothLocalGrid(execution, shape, untouched, 0u, an::LocalGridSmoothing::Box).ok());
    EXPECT_EQ(untouched, a);
    EXPECT_EQ(an::smoothLocalGrid(execution, shape, holed, 1u, an::LocalGridSmoothing::Box).code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(an::smoothLocalGrid(execution, shape, shortOut, 1u, an::LocalGridSmoothing::Box).code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::smoothLocalGrid(execution, shape, untouched, 1u, static_cast<an::LocalGridSmoothing>(4u)).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::smoothLocalGrid(an::ExecutionContext{}, shape, untouched, 1u, an::LocalGridSmoothing::Box).code(),
              an::ErrorCode::NotInitialized);
}

[[nodiscard]] an::LocalNormalizationDocument sampleDocument()
{
    an::LocalNormalizationDocument document;
    document.request.gridSize = 2u;
    document.request.backgroundPercentile = 0.3;
    document.request.lowClip = 1.0e-5;
    document.shape = an::LocalNormalizationGridShape{.gridSize = 2u, .width = 16u, .height = 12u, .channelCount = 2u};
    document.validity = an::ValidityRule::ZeroIsNoData;
    document.referenceIndex = 0u;
    document.referenceBackground.resize(18u);
    for (std::size_t index = 0u; index < 18u; ++index)
    {
        document.referenceBackground[index] = patterned(index, 11u) / 3.0;
    }
    an::LocalNormalizationFrameCoefficients first;
    first.frameIndex = 0u;
    first.background = document.referenceBackground;
    an::LocalNormalizationFrameCoefficients second;
    second.frameIndex = 3u;
    second.scale = {1.25, 0.8, 1.0, 1.0};
    second.background.resize(18u);
    second.scaleGrid.resize(18u);
    for (std::size_t index = 0u; index < 18u; ++index)
    {
        second.background[index] = patterned(index, 12u) / 7.0;
        second.scaleGrid[index] = 0.9 + patterned(index, 13u) / 3.0;
    }
    an::FrameNormalization global;
    global.method = an::NormalizationMethod::AdditiveScaling;
    global.estimator = an::ScaleEstimator::IterativeKSigma;
    global.channelCount = 2u;
    global.channels[0] = an::NormalizationChannelCoefficients{.scale = 1.03125, .offset = -0.0078125};
    global.channels[1] = an::NormalizationChannelCoefficients{.scale = 0.9, .offset = 0.001};
    global.dispersionFallbacks = 1u;
    second.global = global;
    document.frames.push_back(std::move(first));
    document.frames.push_back(std::move(second));
    return document;
}

TEST(LocalNormalizationTest, DocumentRoundTripsLosslesslyAndRefusesMalformedText)
{
    const an::LocalNormalizationDocument document = sampleDocument();
    const an::Result<std::string> text = an::formatLocalNormalizationDocument(document);
    ASSERT_TRUE(text.hasValue()) << text.status().message();
    EXPECT_NE(text->find("localnormalization=1 grid=2 extent=16x12x2 interpolation=Bicubic validity=ZeroIsNoData "
                         "reference=0 frames=2\n"),
              std::string::npos);
    const an::Result<an::LocalNormalizationDocument> parsed = an::parseLocalNormalizationDocument(*text);
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    EXPECT_EQ(parsed->shape, document.shape);
    EXPECT_EQ(parsed->validity, an::ValidityRule::ZeroIsNoData);
    EXPECT_EQ(parsed->referenceIndex, std::optional<std::uint32_t>{0u});
    EXPECT_EQ(parsed->request.backgroundPercentile, 0.3);
    EXPECT_EQ(parsed->request.lowClip, 1.0e-5);
    EXPECT_EQ(parsed->referenceBackground, document.referenceBackground);
    ASSERT_EQ(parsed->frames.size(), 2u);
    EXPECT_EQ(parsed->frames[1].frameIndex, 3u);
    EXPECT_EQ(parsed->frames[1].scale[0], 1.25);
    EXPECT_EQ(parsed->frames[1].scale[1], 0.8);
    EXPECT_EQ(parsed->frames[1].background, document.frames[1].background);
    EXPECT_EQ(parsed->frames[1].scaleGrid, document.frames[1].scaleGrid);
    EXPECT_TRUE(parsed->frames[0].scaleGrid.empty());
    EXPECT_FALSE(parsed->frames[0].global.has_value());
    ASSERT_TRUE(parsed->frames[1].global.has_value());
    EXPECT_EQ(parsed->frames[1].global->method, an::NormalizationMethod::AdditiveScaling);
    EXPECT_EQ(parsed->frames[1].global->estimator, an::ScaleEstimator::IterativeKSigma);
    EXPECT_EQ(parsed->frames[1].global->channels[0].scale, 1.03125);
    EXPECT_EQ(parsed->frames[1].global->channels[1].offset, 0.001);
    EXPECT_EQ(parsed->frames[1].global->dispersionFallbacks, 1u);
    const an::Result<std::string> again = an::formatLocalNormalizationDocument(*parsed);
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_EQ(*again, *text);

    std::string crlf;
    for (const char character : *text)
    {
        if (character == '\n')
        {
            crlf.push_back('\r');
        }
        crlf.push_back(character);
    }
    EXPECT_TRUE(an::parseLocalNormalizationDocument(crlf).hasValue());

    const auto refuses = [&](const std::string& mutated, an::ErrorCode code) {
        const an::Result<an::LocalNormalizationDocument> result = an::parseLocalNormalizationDocument(mutated);
        EXPECT_FALSE(result.hasValue()) << mutated.substr(0u, 60u);
        if (!result.hasValue())
        {
            EXPECT_EQ(result.status().code(), code) << result.status().message();
        }
    };
    refuses("", an::ErrorCode::InvalidData);
    refuses("nonsense\n", an::ErrorCode::InvalidData);
    std::string mutated = *text;
    mutated.replace(mutated.find("localnormalization=1"), 20u, "localnormalization=2");
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    const std::size_t requestLine = mutated.find("request ");
    mutated.erase(requestLine, mutated.find('\n', requestLine) - requestLine + 1u);
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    mutated.replace(mutated.find("frames=2"), 8u, "frames=3");
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    mutated.replace(mutated.find("frames=2"), 8u, "frames=2 grid=2");
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    mutated.replace(mutated.find("reference=0"), 11u, "reference=7");
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    const std::size_t values = mutated.find("values=");
    mutated.insert(values + 7u, "nan,");
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    mutated.insert(values + 7u, "0.5,");
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    mutated.append("unknown record=1\n");
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    const std::size_t globalLine = mutated.find("global ");
    const std::string moved = mutated.substr(globalLine, mutated.find('\n', globalLine) - globalLine + 1u);
    mutated.erase(globalLine, moved.size());
    mutated.insert(mutated.find("frame "), moved);
    refuses(mutated, an::ErrorCode::InvalidData);
    mutated = *text;
    mutated.replace(mutated.find("scale=1.25,0.8"), 14u, "scale=1.25,0");
    refuses(mutated, an::ErrorCode::InvalidData);
    std::string huge(static_cast<std::size_t>(an::kMaxLocalNormalizationDocumentBytes) + 1u, 'x');
    refuses(huge, an::ErrorCode::LimitExceeded);

    an::LocalNormalizationDocument bad = document;
    bad.frames.clear();
    EXPECT_EQ(an::formatLocalNormalizationDocument(bad).status().code(), an::ErrorCode::InvalidArgument);
    bad = document;
    bad.frames[1].background.pop_back();
    EXPECT_EQ(an::formatLocalNormalizationDocument(bad).status().code(), an::ErrorCode::IncompatibleArguments);
    bad = document;
    bad.frames[1].scale[0] = -1.0;
    EXPECT_EQ(an::formatLocalNormalizationDocument(bad).status().code(), an::ErrorCode::InvalidData);
    bad = document;
    bad.referenceIndex = 9u;
    EXPECT_EQ(an::formatLocalNormalizationDocument(bad).status().code(), an::ErrorCode::InvalidArgument);
    bad = document;
    bad.frames[1].global->channelCount = 1u;
    EXPECT_EQ(an::formatLocalNormalizationDocument(bad).status().code(), an::ErrorCode::InvalidArgument);
    bad = document;
    bad.frames[0].frameIndex = 5u;
    EXPECT_EQ(an::formatLocalNormalizationDocument(bad).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(LocalNormalizationTest, TransitionMarksNormalizedAndRecordsTheGrid)
{
    const an::LocalNormalizationGridShape shape{.gridSize = 4u, .width = 8u, .height = 8u, .channelCount = 1u};
    const std::vector<double> grid(25u, 0.1);
    const std::array<double, 1> scale{1.5};
    const an::LocalNormalizationApplyRequest request{.shape = shape,
                                                     .interpolation = an::LocalGridInterpolation::Bicubic,
                                                     .targetBackground = grid,
                                                     .referenceBackground = grid,
                                                     .scale = scale,
                                                     .scaleGrid = {}};
    an::ImageSemanticsRequest semantics;
    semantics.transferFunction = an::TransferFunction::Linear;
    semantics.processingState.registered = true;
    semantics.validity = ruleOf(an::ValidityRule::ZeroIsNoData);
    const an::Result<an::ImageSemantics> source = an::ImageSemantics::create(semantics);
    ASSERT_TRUE(source.hasValue());
    const an::Result<an::MetadataTransition> transition = an::localNormalizationTransition(request, *source);
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    ASSERT_TRUE(transition->semantics.has_value());
    EXPECT_TRUE(transition->semantics->processingState().normalized);
    EXPECT_TRUE(transition->semantics->processingState().registered);
    EXPECT_EQ(transition->semantics->validity().rule(), an::ValidityRule::ZeroIsNoData);
    EXPECT_EQ(transition->semantics->transferFunction(), an::TransferFunction::Linear);
    ASSERT_TRUE(transition->record.has_value());
    EXPECT_EQ(transition->record->operation(), "an::applyLocalNormalization");
    EXPECT_NE(transition->record->detail().find("grid=4"), std::string::npos);
    EXPECT_NE(transition->record->detail().find("Bicubic"), std::string::npos);
    EXPECT_NE(transition->record->detail().find("scaleGrid=none"), std::string::npos);
    EXPECT_EQ(an::checkOperationEffect(an::kLocalNormalizationEffect), an::OperationEffectError::None);

    an::LocalNormalizationApplyRequest wrong = request;
    wrong.shape.gridSize = 0u;
    EXPECT_EQ(an::localNormalizationTransition(wrong, *source).status().code(), an::ErrorCode::OutOfRange);
    wrong = request;
    wrong.scale = {};
    EXPECT_EQ(an::localNormalizationTransition(wrong, *source).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(LocalNormalizationTest, CancellationAndProgressAreHonoured)
{
    const an::EngineContext engine = makeEngine(16u);
    const ScalePair pair = makeScalePair(makeExecution(engine), 1u, 0.9, 0.01, false);
    std::vector<double> nodeSamples(pair.plan.samplesPerNode(), 0.0);
    const an::NormalizationFrameView reference = frameOf(pair.reference.data(), pair.layout, pair.descriptor);
    const an::NormalizationFrameView target = frameOf(pair.target.data(), pair.layout, pair.descriptor);
    std::vector<double> grid(pair.targetGrid.size(), 0.0);
    std::vector<float> output(pair.target.size(), 0.0f);
    const std::array<double, 1> scale{1.0};
    const an::LocalNormalizationApplyRequest apply{.shape = pair.plan.shape(),
                                                   .interpolation = an::LocalGridInterpolation::Bicubic,
                                                   .targetBackground = pair.targetGrid,
                                                   .referenceBackground = pair.referenceGrid,
                                                   .scale = scale,
                                                   .scaleGrid = {}};

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(engine, an::ComputeBackend::Automatic, stop.get_token());
    EXPECT_EQ(an::fitLocalBackgroundGrid(stopped, target, pair.plan, grid).status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::sampleLocalBackgroundNode(stopped, target, pair.plan, 0u, 0u, 0u, nodeSamples).status().code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(an::estimateLocalScale(stopped, target, pair.targetGrid, reference, pair.referenceGrid, pair.plan)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(
        an::fitLocalScaleGrid(stopped, target, pair.targetGrid, reference, pair.referenceGrid, pair.plan, scale, grid)
            .status()
            .code(),
        an::ErrorCode::Cancelled);
    EXPECT_EQ(an::applyLocalNormalization(stopped, apply, target, output.data(), pair.layout.totalBytes(), pair.layout)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);

    CountingProgress fitProgress;
    const an::ExecutionContext watchedFit = makeExecution(engine, an::ComputeBackend::Automatic, {}, 3u, &fitProgress);
    ASSERT_TRUE(an::fitLocalBackgroundGrid(watchedFit, target, pair.plan, grid).hasValue());
    EXPECT_GE(fitProgress.reports, 1u);
    EXPECT_EQ(fitProgress.lastTotal, pair.plan.shape().nodesPerAxis());
    EXPECT_EQ(fitProgress.lastCompleted, pair.plan.shape().nodesPerAxis());
    EXPECT_EQ(fitProgress.lastStage, "fitting local background");

    auto smoothRequest = pair.plan.request();
    smoothRequest.backgroundSmoothingRadius = 1u;
    const auto smoothPlan = an::planLocalNormalization(smoothRequest, pair.plan.shape().width, pair.plan.shape().height,
                                                       pair.plan.shape().channelCount);
    ASSERT_TRUE(smoothPlan.hasValue());
    CountingProgress smoothProgress;
    const auto watchedSmooth = makeExecution(engine, an::ComputeBackend::Automatic, {}, 1u, &smoothProgress);
    ASSERT_TRUE(an::fitLocalBackgroundGrid(watchedSmooth, target, *smoothPlan, grid).hasValue());
    EXPECT_EQ(smoothProgress.lastCompleted, smoothPlan->shape().nodesPerAxis() + smoothPlan->shape().channelCount);
    EXPECT_EQ(smoothProgress.lastTotal, smoothProgress.lastCompleted);
    EXPECT_EQ(watchedSmooth.operationCounters().discardedProgressReports, 0u);

    CountingProgress applyProgress;
    const an::ExecutionContext watchedApply =
        makeExecution(engine, an::ComputeBackend::Automatic, {}, 3u, &applyProgress);
    ASSERT_TRUE(
        an::applyLocalNormalization(watchedApply, apply, target, output.data(), pair.layout.totalBytes(), pair.layout)
            .hasValue());
    EXPECT_GE(applyProgress.reports, 1u);
    EXPECT_EQ(applyProgress.lastTotal, pair.plan.shape().height);
    EXPECT_EQ(applyProgress.lastCompleted, pair.plan.shape().height);
}

TEST(LocalNormalizationTest, LocalThenGlobalComposesOnTheCorrectedSamples)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const ScalePair pair = makeScalePair(execution, 1u, 0.8, 0.02, false);
    const an::NormalizationFrameView reference = frameOf(pair.reference.data(), pair.layout, pair.descriptor);
    const an::NormalizationFrameView target = frameOf(pair.target.data(), pair.layout, pair.descriptor);
    const an::Result<an::LocalScaleEstimate> scale =
        an::estimateLocalScale(execution, target, pair.targetGrid, reference, pair.referenceGrid, pair.plan);
    ASSERT_TRUE(scale.hasValue()) << scale.status().message();
    std::vector<float> corrected(pair.target.size(), 0.0f);
    const an::LocalNormalizationApplyRequest apply{.shape = pair.plan.shape(),
                                                   .interpolation = an::LocalGridInterpolation::Bicubic,
                                                   .targetBackground = pair.targetGrid,
                                                   .referenceBackground = pair.referenceGrid,
                                                   .scale = std::span<const double>{scale->scale.data(), 1u},
                                                   .scaleGrid = {}};
    ASSERT_TRUE(
        an::applyLocalNormalization(execution, apply, target, corrected.data(), pair.layout.totalBytes(), pair.layout)
            .hasValue());
    // The corrected frame is the reference to within the residual noise, so the global match on
    // the corrected samples is close to the identity while the match on the raw target is not.
    double worst = 0.0;
    for (std::size_t index = 0u; index < corrected.size(); ++index)
    {
        worst = (std::max)(worst, std::abs(static_cast<double>(corrected[index]) - pair.reference[index]));
    }
    EXPECT_LT(worst, 0.03);
    const an::NormalizationFrameView correctedView = frameOf(corrected.data(), pair.layout, pair.descriptor);
    const an::Result<an::NormalizationFrameStatistics> referenceStatistics =
        an::measureGlobalNormalization(execution, reference, an::ScaleEstimator::IterativeKSigma);
    const an::Result<an::NormalizationFrameStatistics> correctedStatistics =
        an::measureGlobalNormalization(execution, correctedView, an::ScaleEstimator::IterativeKSigma);
    ASSERT_TRUE(referenceStatistics.hasValue()) << referenceStatistics.status().message();
    ASSERT_TRUE(correctedStatistics.hasValue()) << correctedStatistics.status().message();
    const an::Result<an::NormalizationReference> combined = an::combineNormalizationReferences(
        std::span<const an::NormalizationFrameStatistics>{&*referenceStatistics, 1u});
    ASSERT_TRUE(combined.hasValue());
    const an::Result<an::FrameNormalization> global =
        an::estimateGlobalNormalization(*combined, *correctedStatistics, an::NormalizationMethod::AdditiveScaling);
    ASSERT_TRUE(global.hasValue()) << global.status().message();
    EXPECT_NEAR(global->channels[0].scale, 1.0, 0.05);
    EXPECT_NEAR(global->channels[0].offset, 0.0, 0.02);

    an::LocalNormalizationDocument document;
    document.request = pair.plan.request();
    document.shape = pair.plan.shape();
    document.validity = an::ValidityRule::AllValid;
    document.referenceBackground = pair.referenceGrid;
    an::LocalNormalizationFrameCoefficients frame;
    frame.frameIndex = 1u;
    frame.scale[0] = scale->scale[0];
    frame.background = pair.targetGrid;
    frame.global = *global;
    document.frames.push_back(std::move(frame));
    const an::Result<std::string> text = an::formatLocalNormalizationDocument(document);
    ASSERT_TRUE(text.hasValue()) << text.status().message();
    const an::Result<an::LocalNormalizationDocument> parsed = an::parseLocalNormalizationDocument(*text);
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    ASSERT_TRUE(parsed->frames[0].global.has_value());
    EXPECT_EQ(parsed->frames[0].global->channels[0].scale, global->channels[0].scale);
}

} // namespace

TEST(LocalNormalizationTest, SmoothingHonorsCancellationDeadlineAndChannelCommit)
{
    class StopAfterChannel final : public an::ProgressSink
    {
    public:
        std::stop_source stop;
        void onProgress(const an::ProgressReport&) noexcept override
        {
            stop.request_stop();
        }
    };
    const auto engine = makeEngine();
    const auto plan = an::planLocalNormalization({}, 64u, 64u, 3u);
    ASSERT_TRUE(plan.hasValue());
    std::vector<double> original(plan->shape().valueCount(), 0.1);
    original[0] = 1.0;
    for (const auto smoothing : {an::LocalGridSmoothing::Box, an::LocalGridSmoothing::Gaussian})
    {
        std::stop_source stop;
        stop.request_stop();
        auto stopped = makeExecution(engine, an::ComputeBackend::Automatic, stop.get_token());
        auto values = original;
        EXPECT_EQ(an::smoothLocalGrid(stopped, plan->shape(), values, 1u, smoothing).code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(values, original);
        an::ExecutionOptions options;
        options.deadline = std::chrono::steady_clock::now();
        auto expired = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(expired.hasValue());
        EXPECT_EQ(an::smoothLocalGrid(*expired, plan->shape(), values, 1u, smoothing).code(),
                  an::ErrorCode::DeadlineExceeded);
        EXPECT_EQ(values, original);
        stopped = {};
        *expired = {};
        StopAfterChannel sink;
        auto interrupted = makeExecution(engine, an::ComputeBackend::Automatic, sink.stop.get_token(), 1u, &sink);
        EXPECT_EQ(an::smoothLocalGrid(interrupted, plan->shape(), values, 1u, smoothing).code(),
                  an::ErrorCode::Cancelled);
        EXPECT_NE(values[0], original[0]);
        EXPECT_TRUE(std::equal(values.begin() + plan->shape().nodeCount(), values.end(),
                               original.begin() + plan->shape().nodeCount()));
    }
}

TEST(LocalNormalizationTest, FractionalRefusalsCarryTheValueAndViolatedBound)
{
    an::LocalNormalizationRequest request;
    request.backgroundPercentile = -0.125;
    auto result = an::planLocalNormalization(request, 128u, 128u, 1u);
    ASSERT_EQ(result.code(), an::ErrorCode::OutOfRange);
    ASSERT_NE(result.status().findContext(an::ContextKey::Requested), nullptr);
    EXPECT_EQ(result.status().findContext(an::ContextKey::Requested)->kind, an::ContextValueKind::Real);
    EXPECT_DOUBLE_EQ(result.status().findContext(an::ContextKey::Requested)->real, -0.125);
    EXPECT_DOUBLE_EQ(result.status().findContext(an::ContextKey::Limit)->real, 0.0);
    request = {};
    request.sigmaClip = an::kMaxLocalNormalizationSigmaClip + 0.25;
    result = an::planLocalNormalization(request, 128u, 128u, 1u);
    ASSERT_EQ(result.code(), an::ErrorCode::OutOfRange);
    EXPECT_DOUBLE_EQ(result.status().findContext(an::ContextKey::Requested)->real, request.sigmaClip);
    EXPECT_DOUBLE_EQ(result.status().findContext(an::ContextKey::Limit)->real, an::kMaxLocalNormalizationSigmaClip);
    request = {};
    request.backgroundRoiOverlap = an::kMaxLocalNormalizationRoiOverlap + 0.125;
    result = an::planLocalNormalization(request, 128u, 128u, 1u);
    ASSERT_EQ(result.code(), an::ErrorCode::OutOfRange);
    EXPECT_DOUBLE_EQ(result.status().findContext(an::ContextKey::Requested)->real, request.backgroundRoiOverlap);
    EXPECT_DOUBLE_EQ(result.status().findContext(an::ContextKey::Limit)->real, an::kMaxLocalNormalizationRoiOverlap);
}
