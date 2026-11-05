// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the local weight surface: the node grid a frame is measured on, the
// measurement of each node's level and signal-to-noise quality from four integral images over the
// planner's workspace, the cross-frame finalization that turns level and quality into a weight in
// place, the bilinear read of that weight at a pixel, and the band that feeds the accumulators.
// The oracle is the same rule written directly over the window, so the integral images are held to
// the arithmetic they summarize.

#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 40u;
constexpr std::uint32_t kHeight = 24u;
constexpr std::uint32_t kFrames = 3u;
constexpr std::uint32_t kSpacing = 8u;
constexpr std::uint32_t kWindow = 5u;
constexpr double kExponent = 2.0;

/// An engine with a stated concurrency.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// One operation in an engine, optionally narrowed and stoppable.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 std::uint32_t concurrency = an::kUseContextConcurrency,
                                                 std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// The bytes an engine has outstanding right now.
[[nodiscard]] std::uint64_t liveBytes(const an::EngineContext& engine)
{
    return engine.memoryResource()->counters().liveBytes;
}

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
}

/// A descriptor of the stated representation, shape and schema in the normalized linear domain.
[[nodiscard]] an::ImageDescriptor makeDescriptor(an::SampleType sampleType = an::SampleType::Float32,
                                                 std::uint32_t width = kWidth, std::uint32_t height = kHeight,
                                                 an::ChannelSchema schema = an::ChannelSchema::Mono)
{
    const std::uint32_t channels = an::channelCountOf(schema).value_or(1u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{
        .extent = *extent,
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

/// An average over the stated frames with a local weighting of the stated window and spacing.
[[nodiscard]] an::StackRequest requestOf(const an::ImageDescriptor& descriptor = makeDescriptor(),
                                         std::uint32_t frames = kFrames, std::uint32_t window = kWindow,
                                         std::uint32_t spacing = kSpacing, double exponent = kExponent)
{
    an::StackRequest request{.input = an::StackInputDescription{.frameCount = frames, .descriptor = descriptor}};
    request.localWeighting.enabled = true;
    request.localWeighting.windowSize = window;
    request.localWeighting.gridSpacing = spacing;
    request.localWeighting.snrExponent = exponent;
    return request;
}

/// The checked model of a request every rule admits.
[[nodiscard]] an::StackModel makeModel(const an::StackRequest& request)
{
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    return model.has_value() ? *model : *an::StackModel::create(requestOf());
}

/// A layout of one plane per channel, rows of exactly the width.
[[nodiscard]] an::ImageLayout planarLayout(an::SampleType sampleType, std::uint32_t width, std::uint32_t rows,
                                           std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, rows, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = an::sampleTypeSizeBytes(sampleType),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A validity descriptor of one rule.
[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value_or(an::ValidityDescriptor{});
}

/// A small deterministic generator, so a sweep is the same on every host.
class Lcg
{
public:
    explicit Lcg(std::uint64_t seed) noexcept : m_state(seed) {}

    [[nodiscard]] double unit() noexcept
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(m_state >> 11u) / static_cast<double>(1ull << 53u);
    }

private:
    std::uint64_t m_state;
};

/// One whole frame, owned here: planar Float32 samples, its layout, its rule and an optional mask.
struct OwnedFrame
{
    std::vector<float> samples;
    std::optional<an::ImageLayout> layout;
    an::ValidityDescriptor descriptor{};
    std::vector<std::uint8_t> maskBits;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 1u;

    void create(std::uint32_t frameWidth, std::uint32_t frameHeight, std::uint32_t frameChannels, an::ValidityRule rule)
    {
        width = frameWidth;
        height = frameHeight;
        channels = frameChannels;
        samples.assign(static_cast<std::size_t>(width) * height * channels, 0.0f);
        layout = planarLayout(an::SampleType::Float32, width, height, channels);
        descriptor = ruleOf(rule);
    }

    [[nodiscard]] float& at(std::uint32_t channel, std::uint32_t x, std::uint32_t y)
    {
        return samples[(static_cast<std::size_t>(channel) * height + y) * width + x];
    }

    [[nodiscard]] float at(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        return samples[(static_cast<std::size_t>(channel) * height + y) * width + x];
    }

    /// Gives the frame a packed mask of the stated plane count, every bit set.
    void addMask(std::uint32_t planes)
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width, height, planes, an::defaultEngineLimits());
        ASSERT_TRUE(extent.has_value());
        const std::uint64_t rowBytes = an::packedMaskRowBytes(width);
        maskBits.assign(static_cast<std::size_t>(rowBytes * height * planes), 0xFFu);
        mask = an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>(maskBits), *extent);
        ASSERT_TRUE(mask.has_value());
    }

    /// Clears one bit of the mask.
    void clearBit(std::uint32_t plane, std::uint32_t x, std::uint32_t y)
    {
        ASSERT_TRUE(mask.has_value());
        const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask->planeStrideBytes() +
                                     static_cast<std::uint64_t>(y) * mask->rowStrideBytes() + x / 8u;
        maskBits[static_cast<std::size_t>(offset)] &= static_cast<std::uint8_t>(~(1u << (x % 8u)));
    }

    /// Whether the mask admits a pixel of a plane, or true without a mask.
    [[nodiscard]] bool maskAdmits(std::uint32_t plane, std::uint32_t x, std::uint32_t y) const
    {
        if (!mask.has_value())
        {
            return true;
        }
        const std::uint32_t maskPlane = mask->planeCount() == 1u ? 0u : plane;
        return mask->test(maskPlane, x, y).value_or(false);
    }

    [[nodiscard]] an::LocalWeightFrameSource source() const
    {
        return an::LocalWeightFrameSource{
            .sampleType = an::SampleType::Float32,
            .samples = samples.data(),
            .layout = layout.has_value() ? &*layout : nullptr,
            .validity = an::StackBandValidity{.descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr}};
    }
};

/// Fills a frame with a smooth field plus deterministic noise, and a few exact zeros.
void fillFrame(OwnedFrame& frame, std::uint64_t seed, double zeroEvery = 0.0)
{
    Lcg random(seed);
    for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < frame.height; ++y)
        {
            for (std::uint32_t x = 0u; x < frame.width; ++x)
            {
                const double field = 0.2 + 0.3 * std::sin(0.31 * x + 0.17 * y + 0.5 * channel) +
                                     0.25 * std::exp(-0.02 * ((x - 20.0) * (x - 20.0) + (y - 12.0) * (y - 12.0)));
                double value = field + 0.02 * (random.unit() - 0.5);
                if (zeroEvery > 0.0 && random.unit() < zeroEvery)
                {
                    value = 0.0;
                }
                frame.at(channel, x, y) = static_cast<float>(value);
            }
        }
    }
}

/// The scalar oracle of one node's level and quality, written straight over the window.
struct OracleNode
{
    double level = std::numeric_limits<double>::quiet_NaN();
    double quality = 0.0;
    std::uint64_t count = 0u;
};

/// Reflects an index into a range the way the derivative does at the borders.
[[nodiscard]] std::int64_t reflect101(std::int64_t index, std::int64_t extent)
{
    if (extent == 1)
    {
        return 0;
    }
    while (index < 0 || index >= extent)
    {
        index = index < 0 ? -index : 2 * extent - 2 - index;
    }
    return index;
}

/// The oracle over one frame: luminance, admission, gradient and the window rule per node.
[[nodiscard]] std::vector<OracleNode> oracleGrid(const OwnedFrame& frame, const an::LocalWeightGridShape& shape,
                                                 std::uint32_t window, double exponent)
{
    const std::uint32_t width = frame.width;
    const std::uint32_t height = frame.height;
    std::vector<double> luminance(static_cast<std::size_t>(width) * height, 0.0);
    std::vector<std::uint8_t> admitted(luminance.size(), 0u);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint32_t used = frame.channels >= 3u ? 3u : 1u;
            bool valid = true;
            for (std::uint32_t channel = 0u; channel < used; ++channel)
            {
                valid = valid && an::sampleIsValidByValue(frame.descriptor, frame.at(channel, x, y)) &&
                        frame.maskAdmits(channel, x, y);
            }
            const double value = frame.channels >= 3u ? 0.2126 * frame.at(0u, x, y) + 0.7152 * frame.at(1u, x, y) +
                                                            0.0722 * frame.at(2u, x, y)
                                                      : static_cast<double>(frame.at(0u, x, y));
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            admitted[index] = valid ? 1u : 0u;
            luminance[index] = valid ? value : 0.0;
        }
    }
    const auto lum = [&](std::int64_t x, std::int64_t y) {
        return luminance[static_cast<std::size_t>(reflect101(y, height)) * width +
                         static_cast<std::size_t>(reflect101(x, width))];
    };
    std::vector<double> gradient(luminance.size(), 0.0);
    for (std::int64_t y = 0; y < static_cast<std::int64_t>(height); ++y)
    {
        for (std::int64_t x = 0; x < static_cast<std::int64_t>(width); ++x)
        {
            const double gx = -lum(x - 1, y - 1) + lum(x + 1, y - 1) - 2.0 * lum(x - 1, y) + 2.0 * lum(x + 1, y) -
                              lum(x - 1, y + 1) + lum(x + 1, y + 1);
            const double gy = -lum(x - 1, y - 1) - 2.0 * lum(x, y - 1) - lum(x + 1, y - 1) + lum(x - 1, y + 1) +
                              2.0 * lum(x, y + 1) + lum(x + 1, y + 1);
            gradient[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = std::sqrt(gx * gx + gy * gy);
        }
    }

    std::vector<OracleNode> nodes(static_cast<std::size_t>(shape.nodeCount()));
    const std::int64_t half = window / 2u;
    for (std::uint32_t nodeY = 0u; nodeY < shape.nodesY; ++nodeY)
    {
        for (std::uint32_t nodeX = 0u; nodeX < shape.nodesX; ++nodeX)
        {
            const std::int64_t centreX = shape.nodeColumn(nodeX);
            const std::int64_t centreY = shape.nodeRow(nodeY);
            const std::int64_t x0 = (std::max<std::int64_t>)(0, centreX - half);
            const std::int64_t y0 = (std::max<std::int64_t>)(0, centreY - half);
            const std::int64_t x1 = (std::min<std::int64_t>)(width, centreX + half + 1);
            const std::int64_t y1 = (std::min<std::int64_t>)(height, centreY + half + 1);
            double count = 0.0;
            double sum = 0.0;
            double sumSquares = 0.0;
            double gradientSum = 0.0;
            for (std::int64_t y = y0; y < y1; ++y)
            {
                for (std::int64_t x = x0; x < x1; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                    if (admitted[index] == 0u)
                    {
                        continue;
                    }
                    count += 1.0;
                    sum += luminance[index];
                    sumSquares += luminance[index] * luminance[index];
                    gradientSum += gradient[index];
                }
            }
            OracleNode& node = nodes[static_cast<std::size_t>(nodeY) * shape.nodesX + nodeX];
            node.count = static_cast<std::uint64_t>(count);
            if (count < 3.0)
            {
                continue;
            }
            const double mean = sum / count;
            const double meanSquares = sumSquares / count;
            const double variance = (std::max)(0.0, meanSquares - mean * mean);
            const double gradientMean = gradientSum / count;
            const double signal = (std::max)(mean, 0.0) + 0.5 * gradientMean;
            const double noise = std::sqrt(variance + 1.0e-6);
            double quality = std::pow(std::clamp(signal / (noise + 1.0e-6), 1.0e-3, 1.0e6), exponent);
            if (!std::isfinite(quality))
            {
                quality = 1.0;
            }
            node.level = mean;
            node.quality = quality;
        }
    }
    return nodes;
}

/// Compares a measured grid with the oracle, node by node.
void expectGridMatches(std::span<const float> grid, const std::vector<OracleNode>& oracle, std::string_view what)
{
    ASSERT_EQ(grid.size(), oracle.size() * an::kLocalWeightNodeValues) << what;
    for (std::size_t node = 0u; node < oracle.size(); ++node)
    {
        const float level = grid[node * an::kLocalWeightNodeValues];
        const float quality = grid[node * an::kLocalWeightNodeValues + 1u];
        if (std::isnan(oracle[node].level))
        {
            EXPECT_TRUE(std::isnan(level)) << what << " node " << node;
            EXPECT_EQ(quality, 0.0f) << what << " node " << node;
            continue;
        }
        EXPECT_NEAR(level, static_cast<float>(oracle[node].level), 1e-6 * (std::max)(1.0, std::abs(oracle[node].level)))
            << what << " node " << node;
        EXPECT_NEAR(quality, static_cast<float>(oracle[node].quality), 1e-5 * (std::max)(1.0, oracle[node].quality))
            << what << " node " << node;
    }
}

/// Measures one frame into a fresh grid with a fresh workspace, expecting success.
[[nodiscard]] std::vector<float> measure(const an::ExecutionContext& execution, const an::StackModel& model,
                                         const OwnedFrame& frame, an::LocalWeightMeasurement* measurement = nullptr)
{
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    EXPECT_TRUE(shape.has_value());
    std::vector<float> grid(static_cast<std::size_t>(shape->valuesPerFrame()), -7.0f);
    std::vector<std::byte> workspace(
        static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, model.request().input.frameCount)));
    const an::Result<an::LocalWeightMeasurement> made =
        an::measureLocalWeightGrid(execution, model, frame.source(), workspace, grid);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (made.hasValue() && measurement != nullptr)
    {
        *measurement = *made;
    }
    return grid;
}

TEST(LocalWeightsTest, TheShapeCountsNodesLikeThePlannerAndClampsNodeCoordinatesToTheFrame)
{
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::create(kWidth, kHeight, kSpacing);
    ASSERT_TRUE(shape.has_value());
    EXPECT_EQ(shape->width, kWidth);
    EXPECT_EQ(shape->height, kHeight);
    EXPECT_EQ(shape->spacing, kSpacing);
    EXPECT_EQ(shape->nodesX, an::localWeightGridNodesAlong(kWidth, kSpacing));
    EXPECT_EQ(shape->nodesY, an::localWeightGridNodesAlong(kHeight, kSpacing));
    // Forty pixels at spacing eight: nodes at 0, 8, 16, 24 and 32, and one more at the far edge,
    // 39; twenty-four rows: 0, 8, 16 and the edge at 23.
    EXPECT_EQ(shape->nodesX, 6u);
    EXPECT_EQ(shape->nodesY, 4u);
    EXPECT_EQ(shape->nodeCount(), 24u);
    EXPECT_EQ(shape->valuesPerFrame(), 24u * an::kLocalWeightNodeValues);
    EXPECT_EQ(shape->nodeColumn(0u), 0u);
    EXPECT_EQ(shape->nodeColumn(1u), 8u);
    EXPECT_EQ(shape->nodeColumn(4u), 32u);
    EXPECT_EQ(shape->nodeColumn(5u), 39u);
    EXPECT_EQ(shape->nodeRow(2u), 16u);
    EXPECT_EQ(shape->nodeRow(3u), 23u);

    // An extent that does not land on a node gets one more at the far edge, clamped to the last
    // pixel; a one-pixel axis has one node at zero.
    const std::optional<an::LocalWeightGridShape> odd = an::LocalWeightGridShape::create(37u, 1u, 16u);
    ASSERT_TRUE(odd.has_value());
    EXPECT_EQ(odd->nodesX, 4u);
    EXPECT_EQ(odd->nodesY, 1u);
    EXPECT_EQ(odd->nodeColumn(3u), 36u);
    EXPECT_EQ(odd->nodeRow(0u), 0u);

    EXPECT_FALSE(an::LocalWeightGridShape::create(0u, kHeight, kSpacing).has_value());
    EXPECT_FALSE(an::LocalWeightGridShape::create(kWidth, 0u, kSpacing).has_value());
    EXPECT_FALSE(
        an::LocalWeightGridShape::create(kWidth, kHeight, an::kMinimumLocalWeightGridSpacing - 1u).has_value());

    const an::StackModel model = makeModel(requestOf());
    const std::optional<an::LocalWeightGridShape> ofModel = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(ofModel.has_value());
    EXPECT_EQ(*ofModel, *shape);
    an::StackRequest plain = requestOf();
    plain.localWeighting.enabled = false;
    EXPECT_FALSE(an::LocalWeightGridShape::of(makeModel(plain)).has_value());
    static_assert(an::kLocalWeightNodeValues * sizeof(float) == an::kLocalWeightNodeBytes);
}

TEST(LocalWeightsTest, TheWorkspaceCoversTheIntegralImagesAndTheFinalizationAndIsWhatThePlanReserves)
{
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::create(kWidth, kHeight, kSpacing);
    ASSERT_TRUE(shape.has_value());
    const std::uint64_t integral =
        static_cast<std::uint64_t>(kWidth + 1u) * (kHeight + 1u) * an::kLocalWeightIntegralImages * sizeof(double);
    EXPECT_EQ(an::localWeightMeasurementWorkspaceBytes(*shape, kFrames), integral);
    // Enough frames make the finalization's per-frame scratch the larger term.
    const std::uint32_t many = static_cast<std::uint32_t>(integral / an::kLocalWeightFinalizeBytesPerFrame) + 5u;
    EXPECT_EQ(an::localWeightMeasurementWorkspaceBytes(*shape, many),
              static_cast<std::uint64_t>(many) * an::kLocalWeightFinalizeBytesPerFrame);
    static_assert(an::localWeightMeasurementWorkspaceBytes(
                      an::LocalWeightGridShape{.width = 4u, .height = 4u, .spacing = 4u, .nodesX = 1u, .nodesY = 1u},
                      1u) == 25u * an::kLocalWeightIntegralImages * sizeof(double));

    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf());
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->memory.measurementWorkspaceBytes, an::localWeightMeasurementWorkspaceBytes(*shape, kFrames));
    EXPECT_EQ(plan->memory.localWeightGridBytes, kFrames * shape->nodeCount() * an::kLocalWeightNodeBytes);
}

TEST(LocalWeightsTest, AFlatFrameMeasuresItsLevelAndTheFloorQuality)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf());
    OwnedFrame frame;
    frame.create(kWidth, kHeight, 1u, an::ValidityRule::AllValid);
    std::fill(frame.samples.begin(), frame.samples.end(), 0.25f);

    an::LocalWeightMeasurement measurement;
    const std::vector<float> grid = measure(execution, model, frame, &measurement);
    EXPECT_EQ(measurement.totalPixels, static_cast<std::uint64_t>(kWidth) * kHeight);
    EXPECT_EQ(measurement.admittedPixels, measurement.totalPixels);
    EXPECT_EQ(measurement.nodeCount, 24u);
    EXPECT_EQ(measurement.measuredNodes, 24u);
    EXPECT_EQ(liveBytes(engine), 0u);

    // No variance and no gradient: the noise is the floor alone and the quality is the clamped
    // ratio raised to the exponent.
    const double noise = std::sqrt(1.0e-6);
    const double expectedQuality = std::pow(std::clamp(0.25 / (noise + 1.0e-6), 1.0e-3, 1.0e6), kExponent);
    for (std::size_t node = 0u; node < 24u; ++node)
    {
        EXPECT_FLOAT_EQ(grid[node * 2u], 0.25f) << node;
        EXPECT_NEAR(grid[node * 2u + 1u], static_cast<float>(expectedQuality), 1e-3 * expectedQuality) << node;
    }
}

TEST(LocalWeightsTest, MeasurementMatchesTheScalarOracleUnderAValidityRule)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf());
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());

    // Exact zeros are no-data under the stated rule, so the window counts exclude them; a corner
    // node whose window is wholly zero measures nothing.
    OwnedFrame frame;
    frame.create(kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
    fillFrame(frame, 11u, 0.15);
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            frame.at(0u, x, y) = 0.0f;
        }
    }
    const std::vector<OracleNode> oracle = oracleGrid(frame, *shape, kWindow, kExponent);
    ASSERT_TRUE(std::isnan(oracle[0].level));
    std::uint64_t admitted = 0u;
    for (const float value : frame.samples)
    {
        admitted += value != 0.0f ? 1u : 0u;
    }

    an::LocalWeightMeasurement measurement;
    const std::vector<float> grid = measure(execution, model, frame, &measurement);
    expectGridMatches(grid, oracle, "zero-is-no-data");
    EXPECT_EQ(measurement.admittedPixels, admitted);
    EXPECT_EQ(measurement.measuredNodes, 23u);
    EXPECT_EQ(liveBytes(engine), 0u);

    // The finite-only rule admits everything here, and a wider window at a finer grid still
    // matches the oracle at the borders, where the window is clipped.
    OwnedFrame everything;
    everything.create(kWidth, kHeight, 1u, an::ValidityRule::FiniteOnly);
    fillFrame(everything, 23u);
    const an::StackModel wide = makeModel(requestOf(makeDescriptor(), kFrames, 9u, 4u, 1.5));
    const std::optional<an::LocalWeightGridShape> wideShape = an::LocalWeightGridShape::of(wide);
    ASSERT_TRUE(wideShape.has_value());
    const std::vector<float> wideGrid = measure(execution, wide, everything);
    expectGridMatches(wideGrid, oracleGrid(everything, *wideShape, 9u, 1.5), "wide window");
}

TEST(LocalWeightsTest, ColourFramesAreMeasuredOnTheirLuminance)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model =
        makeModel(requestOf(makeDescriptor(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb)));
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    OwnedFrame frame;
    frame.create(kWidth, kHeight, 3u, an::ValidityRule::ZeroIsNoData);
    fillFrame(frame, 5u, 0.05);
    const std::vector<float> grid = measure(execution, model, frame);
    expectGridMatches(grid, oracleGrid(frame, *shape, kWindow, kExponent), "rgb");
}

TEST(LocalWeightsTest, AnExplicitMaskExcludesPixelsAndIsRequiredByItsRule)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf());
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());

    OwnedFrame frame;
    frame.create(kWidth, kHeight, 1u, an::ValidityRule::ExplicitMask);
    fillFrame(frame, 31u);
    frame.addMask(1u);
    for (std::uint32_t y = 6u; y < 10u; ++y)
    {
        for (std::uint32_t x = 6u; x < 11u; ++x)
        {
            frame.clearBit(0u, x, y);
        }
    }
    an::LocalWeightMeasurement measurement;
    const std::vector<float> grid = measure(execution, model, frame, &measurement);
    expectGridMatches(grid, oracleGrid(frame, *shape, kWindow, kExponent), "masked");
    EXPECT_EQ(measurement.admittedPixels, measurement.totalPixels - 20u);

    OwnedFrame unmasked;
    unmasked.create(kWidth, kHeight, 1u, an::ValidityRule::ExplicitMask);
    std::vector<float> scratchGrid(static_cast<std::size_t>(shape->valuesPerFrame()));
    std::vector<std::byte> workspace(
        static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, kFrames)));
    const an::Result<an::LocalWeightMeasurement> missing =
        an::measureLocalWeightGrid(execution, model, unmasked.source(), workspace, scratchGrid);
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(messageNames(missing.status(), "mask")) << missing.status().message();

    OwnedFrame unwanted;
    unwanted.create(kWidth, kHeight, 1u, an::ValidityRule::AllValid);
    unwanted.addMask(1u);
    const an::Result<an::LocalWeightMeasurement> extra =
        an::measureLocalWeightGrid(execution, model, unwanted.source(), workspace, scratchGrid);
    ASSERT_FALSE(extra.hasValue());
    EXPECT_EQ(extra.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(LocalWeightsTest, IntegerFramesAreWidenedAsTheyAreStored)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf(makeDescriptor(an::SampleType::UInt16)));
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(kWidth) * kHeight, 1000u);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight, 1u);
    const an::LocalWeightFrameSource source{
        .sampleType = an::SampleType::UInt16,
        .samples = samples.data(),
        .layout = &layout,
        .validity = an::StackBandValidity{.descriptor = ruleOf(an::ValidityRule::AllValid)}};
    std::vector<float> grid(static_cast<std::size_t>(shape->valuesPerFrame()));
    std::vector<std::byte> workspace(
        static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, kFrames)));
    const an::Result<an::LocalWeightMeasurement> made =
        an::measureLocalWeightGrid(execution, model, source, workspace, grid);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_FLOAT_EQ(grid[0], 1000.0f);
    EXPECT_FLOAT_EQ(grid[2u * 7u], 1000.0f);

    // A representation that is neither the model's nor the tile width it is widened to is refused,
    // as is a UInt16 frame handed as Float64.
    const an::LocalWeightFrameSource wrong{.sampleType = an::SampleType::Float64,
                                           .samples = samples.data(),
                                           .layout = &layout,
                                           .validity = source.validity};
    const an::Result<an::LocalWeightMeasurement> refused =
        an::measureLocalWeightGrid(execution, model, wrong, workspace, grid);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused.status(), "sampleType")) << refused.status().message();
}

TEST(LocalWeightsTest, TheBitsDoNotDependOnTheThreadCount)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::StackModel model =
        makeModel(requestOf(makeDescriptor(an::SampleType::Float32, 61u, 47u), kFrames, 7u, 4u));
    OwnedFrame frame;
    frame.create(61u, 47u, 1u, an::ValidityRule::ZeroIsNoData);
    fillFrame(frame, 77u, 0.1);
    std::vector<float> narrow;
    std::vector<float> wide;
    {
        const an::ExecutionContext execution = makeExecution(engine, 1u);
        narrow = measure(execution, model, frame);
    }
    {
        const an::ExecutionContext execution = makeExecution(engine, 4u);
        wide = measure(execution, model, frame);
    }
    ASSERT_EQ(narrow.size(), wide.size());
    EXPECT_EQ(std::memcmp(narrow.data(), wide.data(), narrow.size() * sizeof(float)), 0);
}

TEST(LocalWeightsTest, FinalizationFollowsTheConsistencyRuleInPlace)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // Four frames on a tiny grid of two nodes - a five-pixel row at spacing four - so every rule
    // can be written by hand.
    const an::StackModel model = makeModel(requestOf(makeDescriptor(an::SampleType::Float32, 5u, 1u), 4u, 3u, 4u));
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    ASSERT_EQ(shape->nodeCount(), 2u);
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    // Node 0: four active frames with one deviant level and one poor quality.
    // Node 1: one active frame, which is too few to judge, so every frame keeps weight one.
    const std::array<float, 4u> level{0.50f, 0.52f, 0.90f, 0.49f};
    const std::array<float, 4u> quality{100.0f, 10.0f, 100.0f, 90.0f};
    std::vector<float> grids(static_cast<std::size_t>(4u * shape->valuesPerFrame()), kNaN);
    for (std::uint32_t frame = 0u; frame < 4u; ++frame)
    {
        float* const grid = grids.data() + frame * shape->valuesPerFrame();
        grid[0] = level[frame];
        grid[1] = quality[frame];
        grid[2] = frame == 1u ? 0.3f : kNaN;
        grid[3] = frame == 1u ? 50.0f : 0.0f;
    }

    std::vector<std::byte> workspace(static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, 4u)));
    const an::Result<an::LocalWeightingSummary> summary =
        an::finalizeLocalWeightGrids(execution, model, grids, workspace);
    ASSERT_TRUE(summary.hasValue()) << summary.status().message();
    EXPECT_EQ(summary->windowSize, 3u);
    EXPECT_EQ(summary->gridSpacing, 4u);
    EXPECT_DOUBLE_EQ(summary->snrExponent, kExponent);
    EXPECT_EQ(summary->frameCount, 4u);
    EXPECT_EQ(summary->nodeCount, 2u);
    EXPECT_EQ(summary->activeNodes, 1u);
    EXPECT_EQ(summary->weightedFrameNodes, 4u);
    EXPECT_EQ(liveBytes(engine), 0u);

    // The rule: the median level and quality across the active frames, a deviation-based sigma
    // floored at two per cent of the median level, a quality ratio clamped to [0.25, 1], a
    // Gaussian consistency at twice the sigma, and the product clamped to [0.1, 1].
    std::vector<double> levels(level.begin(), level.end());
    std::vector<double> qualities(quality.begin(), quality.end());
    std::sort(levels.begin(), levels.end());
    std::sort(qualities.begin(), qualities.end());
    const double medianLevel = 0.5 * (levels[1] + levels[2]);
    const double medianQuality = 0.5 * (qualities[1] + qualities[2]);
    std::vector<double> deviations;
    for (const float value : level)
    {
        deviations.push_back(std::abs(static_cast<double>(value) - medianLevel));
    }
    std::sort(deviations.begin(), deviations.end());
    double sigma = 1.4826 * 0.5 * (deviations[1] + deviations[2]);
    sigma = (std::max)(sigma, (std::max)(0.01, std::abs(medianLevel) * 0.02));
    double sum = 0.0;
    double sumSquares = 0.0;
    for (std::uint32_t frame = 0u; frame < 4u; ++frame)
    {
        const double ratio =
            std::clamp(static_cast<double>(quality[frame]) / (std::max)(medianQuality, 1.0e-6), 0.25, 1.0);
        const double residual = (static_cast<double>(level[frame]) - medianLevel) / (2.0 * sigma);
        const double consistency = std::exp(-0.5 * residual * residual);
        const double expected = std::clamp(ratio * consistency, 0.1, 1.0);
        const float weight = grids[frame * shape->valuesPerFrame()];
        EXPECT_NEAR(weight, static_cast<float>(expected), 1e-6) << frame;
        EXPECT_FLOAT_EQ(grids[frame * shape->valuesPerFrame() + 1u], quality[frame]) << frame;
        EXPECT_FLOAT_EQ(grids[frame * shape->valuesPerFrame() + 2u], 1.0f) << frame;
        sum += static_cast<double>(weight) + 1.0;
        sumSquares += static_cast<double>(weight) * weight + 1.0;
    }
    // The deviant frame is held at the floor, the poor-quality frame sits below the consistent
    // ones, and nothing at the judged node reaches one because every frame is off the median.
    EXPECT_FLOAT_EQ(grids[2u * shape->valuesPerFrame()], 0.1f);
    EXPECT_LT(grids[1u * shape->valuesPerFrame()], grids[0u * shape->valuesPerFrame()]);
    EXPECT_LT(grids[0u * shape->valuesPerFrame()], 1.0f);
    EXPECT_DOUBLE_EQ(summary->minimumWeight, static_cast<double>(grids[2u * shape->valuesPerFrame()]));
    EXPECT_DOUBLE_EQ(summary->maximumWeight, 1.0);
    EXPECT_NEAR(summary->meanWeight, sum / 8.0, 1e-12);
    const double variance = sumSquares / 8.0 - (sum / 8.0) * (sum / 8.0);
    EXPECT_NEAR(summary->weightStandardDeviation, std::sqrt((std::max)(variance, 0.0)), 1e-12);

    // A set of grids that is not one per frame of the model is refused by name.
    const an::Result<an::LocalWeightingSummary> again =
        an::finalizeLocalWeightGrids(execution, model, std::span<float>(grids).subspan(0u, 3u), workspace);
    ASSERT_FALSE(again.hasValue());
    EXPECT_EQ(again.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(again.status(), "grids")) << again.status().message();
}

TEST(LocalWeightsTest, SamplingIsBilinearBetweenNodesAndABandFeedsTheAccumulators)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // A 9x9 frame at spacing 4 has three nodes per axis at columns 0, 4 and 8.
    const an::StackModel model = makeModel(requestOf(makeDescriptor(an::SampleType::Float32, 9u, 9u), 2u, 3u, 4u));
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    ASSERT_EQ(shape->nodeCount(), 9u);
    std::vector<float> grid(static_cast<std::size_t>(shape->valuesPerFrame()), 0.0f);
    const auto node = [&grid](std::uint32_t nodeX, std::uint32_t nodeY) -> float& {
        return grid[(static_cast<std::size_t>(nodeY) * 3u + nodeX) * an::kLocalWeightNodeValues];
    };
    node(0u, 0u) = 1.0f;
    node(1u, 0u) = 0.5f;
    node(2u, 0u) = 0.25f;
    node(0u, 1u) = 0.8f;
    node(1u, 1u) = 0.4f;
    node(2u, 1u) = 0.2f;
    node(0u, 2u) = 0.6f;
    node(1u, 2u) = 0.3f;
    node(2u, 2u) = 0.1f;

    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 0.0, 0.0), 1.0f);
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 4.0, 0.0), 0.5f);
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 8.0, 8.0), 0.1f);
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 2.0, 0.0), 0.75f);
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 0.0, 2.0), 0.9f);
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 2.0, 2.0), 0.25f * (1.0f + 0.5f + 0.8f + 0.4f));
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 6.0, 6.0), 0.25f * (0.4f + 0.2f + 0.3f + 0.1f));
    // Outside the frame the read clamps to the edge.
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, -3.0, -3.0), 1.0f);
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, grid, 20.0, 4.0), 0.2f);
    // A grid that does not describe the shape reads as one, the neutral weight.
    EXPECT_FLOAT_EQ(an::sampleLocalWeight(*shape, std::span<const float>(grid).subspan(0u, 4u), 2.0, 2.0), 1.0f);

    std::vector<float> band(static_cast<std::size_t>(9u) * 3u, -1.0f);
    const an::Status filled = an::fillLocalWeightBand(*shape, grid, 3u, 3u, band);
    ASSERT_TRUE(filled.ok()) << filled.message();
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 9u; ++x)
        {
            EXPECT_FLOAT_EQ(band[static_cast<std::size_t>(y) * 9u + x],
                            an::sampleLocalWeight(*shape, grid, static_cast<double>(x), static_cast<double>(3u + y)))
                << x << ',' << y;
        }
    }
    std::vector<float> shortBand(8u);
    const an::Status refused = an::fillLocalWeightBand(*shape, grid, 3u, 3u, shortBand);
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused, "weights")) << refused.message();
    const an::Status outside = an::fillLocalWeightBand(*shape, grid, 7u, 3u, band);
    EXPECT_EQ(outside.code(), an::ErrorCode::OutOfRange);

    // The band is exactly what the accumulators take as a frame's local weights.
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    an::Result<an::StackWorkingSet> set = an::StackWorkingSet::create(engine, execution, model, *plan);
    ASSERT_TRUE(set.hasValue()) << set.status().message();
    std::vector<float> samples(static_cast<std::size_t>(9u) * 3u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 9u, 3u, 1u);
    const an::StackFrameBand frame{.sampleType = an::SampleType::Float32,
                                   .samples = samples.data(),
                                   .layout = &layout,
                                   .frameIndex = 0u,
                                   .firstOutputRow = 3u,
                                   .validity = an::StackBandValidity{.descriptor = ruleOf(an::ValidityRule::AllValid)},
                                   .localWeights = band};
    const an::StackAccumulatorSet accumulators{.sums = set->accumulatorSums(),
                                               .counts = set->accumulatorCounts(),
                                               .weightSums = set->accumulatorWeightSums(),
                                               .weightSquareSums = set->accumulatorWeightSquareSums(),
                                               .varianceMoments = set->accumulatorVarianceMoments()};
    std::fill(accumulators.sums.begin(), accumulators.sums.end(), 0.0);
    std::fill(accumulators.counts.begin(), accumulators.counts.end(), 0u);
    std::fill(accumulators.weightSums.begin(), accumulators.weightSums.end(), 0.0);
    std::fill(set->frameWeights().begin(), set->frameWeights().end(), 1.0);
    const an::Result<an::StackAccumulationOutcome> outcome =
        an::accumulateStackFrame(execution, model, accumulators, frame, set->frameWeights());
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_EQ(outcome->samplesAccumulated, 27u);
    EXPECT_DOUBLE_EQ(accumulators.weightSums[static_cast<std::size_t>(3u) * 9u + 4u], static_cast<double>(band[4]));
    EXPECT_DOUBLE_EQ(accumulators.sums[static_cast<std::size_t>(5u) * 9u + 8u],
                     0.5 * static_cast<double>(band[2u * 9u + 8u]));
}

TEST(LocalWeightsTest, MismatchedShapesAndModelsWithoutASurfaceAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf());
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    OwnedFrame frame;
    frame.create(kWidth, kHeight, 1u, an::ValidityRule::AllValid);
    fillFrame(frame, 3u);
    std::vector<float> grid(static_cast<std::size_t>(shape->valuesPerFrame()));
    std::vector<std::byte> workspace(
        static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, kFrames)));

    std::vector<float> shortGrid(grid.size() - 1u);
    const an::Result<an::LocalWeightMeasurement> gridSize =
        an::measureLocalWeightGrid(execution, model, frame.source(), workspace, shortGrid);
    ASSERT_FALSE(gridSize.hasValue());
    EXPECT_EQ(gridSize.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(gridSize.status(), "grid")) << gridSize.status().message();

    std::vector<std::byte> shortWorkspace(workspace.size() - 8u);
    const an::Result<an::LocalWeightMeasurement> workspaceSize =
        an::measureLocalWeightGrid(execution, model, frame.source(), shortWorkspace, grid);
    ASSERT_FALSE(workspaceSize.hasValue());
    EXPECT_EQ(workspaceSize.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(workspaceSize.status(), "workspace")) << workspaceSize.status().message();

    OwnedFrame small;
    small.create(kWidth - 1u, kHeight, 1u, an::ValidityRule::AllValid);
    const an::Result<an::LocalWeightMeasurement> extent =
        an::measureLocalWeightGrid(execution, model, small.source(), workspace, grid);
    ASSERT_FALSE(extent.hasValue());
    EXPECT_EQ(extent.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(extent.status(), "layout")) << extent.status().message();

    an::LocalWeightFrameSource nullSamples = frame.source();
    nullSamples.samples = nullptr;
    const an::Result<an::LocalWeightMeasurement> pointer =
        an::measureLocalWeightGrid(execution, model, nullSamples, workspace, grid);
    ASSERT_FALSE(pointer.hasValue());
    EXPECT_EQ(pointer.status().code(), an::ErrorCode::InvalidArgument);

    an::StackRequest plain = requestOf();
    plain.localWeighting.enabled = false;
    const an::StackModel unweighted = makeModel(plain);
    const an::Result<an::LocalWeightMeasurement> noSurface =
        an::measureLocalWeightGrid(execution, unweighted, frame.source(), workspace, grid);
    ASSERT_FALSE(noSurface.hasValue());
    EXPECT_EQ(noSurface.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(noSurface.status(), "local weighting")) << noSurface.status().message();
    std::vector<float> grids(static_cast<std::size_t>(kFrames) * shape->valuesPerFrame(), 1.0f);
    const an::Result<an::LocalWeightingSummary> noFinalize =
        an::finalizeLocalWeightGrids(execution, unweighted, grids, workspace);
    ASSERT_FALSE(noFinalize.hasValue());
    EXPECT_EQ(noFinalize.status().code(), an::ErrorCode::IncompatibleArguments);

    const an::Result<an::LocalWeightingSummary> gridsSize =
        an::finalizeLocalWeightGrids(execution, model, std::span<float>(grids).subspan(2u), workspace);
    ASSERT_FALSE(gridsSize.hasValue());
    EXPECT_EQ(gridsSize.status().code(), an::ErrorCode::IncompatibleArguments);
    const an::Result<an::LocalWeightingSummary> finalizeWorkspace =
        an::finalizeLocalWeightGrids(execution, model, grids, std::span<std::byte>(workspace).subspan(0u, 8u));
    ASSERT_FALSE(finalizeWorkspace.hasValue());
    EXPECT_EQ(finalizeWorkspace.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(finalizeWorkspace.status(), "workspace")) << finalizeWorkspace.status().message();
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(LocalWeightsTest, CancellationAndAnEmptyExecutionAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(requestOf());
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    OwnedFrame frame;
    frame.create(kWidth, kHeight, 1u, an::ValidityRule::AllValid);
    fillFrame(frame, 9u);
    std::vector<float> grid(static_cast<std::size_t>(shape->valuesPerFrame()));
    std::vector<float> grids(static_cast<std::size_t>(kFrames) * shape->valuesPerFrame(), 1.0f);
    std::vector<std::byte> workspace(
        static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, kFrames)));

    const an::ExecutionContext nothing;
    const an::Result<an::LocalWeightMeasurement> unheld =
        an::measureLocalWeightGrid(nothing, model, frame.source(), workspace, grid);
    ASSERT_FALSE(unheld.hasValue());
    EXPECT_EQ(unheld.status().code(), an::ErrorCode::NotInitialized);
    const an::Result<an::LocalWeightingSummary> unheldFinalize =
        an::finalizeLocalWeightGrids(nothing, model, grids, workspace);
    ASSERT_FALSE(unheldFinalize.hasValue());
    EXPECT_EQ(unheldFinalize.status().code(), an::ErrorCode::NotInitialized);

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(engine, an::kUseContextConcurrency, stop.get_token());
    const an::Result<an::LocalWeightMeasurement> cancelled =
        an::measureLocalWeightGrid(stopped, model, frame.source(), workspace, grid);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);
    const an::Result<an::LocalWeightingSummary> cancelledFinalize =
        an::finalizeLocalWeightGrids(stopped, model, grids, workspace);
    ASSERT_FALSE(cancelledFinalize.hasValue());
    EXPECT_EQ(cancelledFinalize.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(LocalWeightsTest, TheWorkingSetsBuffersCarryTheWholeMeasurementPass)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf());
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    ASSERT_EQ(plan->passes.passes[0].kind, an::StackPassKind::MeasureLocalWeights);

    std::array<OwnedFrame, kFrames> frames;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        frames[frame].create(kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
        fillFrame(frames[frame], 100u + frame, 0.05);
    }

    an::Result<an::StackWorkingSet> set = an::StackWorkingSet::create(engine, execution, model, *plan);
    ASSERT_TRUE(set.hasValue()) << set.status().message();
    const an::Status begun = set->beginPass(execution, an::StackPassKind::MeasureLocalWeights);
    ASSERT_TRUE(begun.ok()) << begun.message();
    const std::uint64_t held = liveBytes(engine);
    std::span<float> grids = set->localWeightGrids();
    ASSERT_EQ(grids.size(), static_cast<std::size_t>(kFrames) * shape->valuesPerFrame());
    std::span<std::byte> workspace = set->measurementWorkspace();
    ASSERT_GE(workspace.size(), an::localWeightMeasurementWorkspaceBytes(*shape, kFrames));

    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const an::Result<an::LocalWeightMeasurement> measured = an::measureLocalWeightGrid(
            execution, model, frames[frame].source(), workspace,
            grids.subspan(static_cast<std::size_t>(frame) * shape->valuesPerFrame(), shape->valuesPerFrame()));
        ASSERT_TRUE(measured.hasValue()) << frame << ": " << measured.status().message();
        EXPECT_EQ(liveBytes(engine), held) << frame;
    }
    const an::Result<an::LocalWeightingSummary> summary =
        an::finalizeLocalWeightGrids(execution, model, grids, workspace);
    ASSERT_TRUE(summary.hasValue()) << summary.status().message();
    EXPECT_EQ(summary->frameCount, kFrames);
    EXPECT_EQ(summary->nodeCount, shape->nodeCount());
    EXPECT_EQ(summary->activeNodes, shape->nodeCount());
    EXPECT_GT(summary->weightedFrameNodes, 0u);
    EXPECT_LE(summary->maximumWeight, 1.0);
    EXPECT_GE(summary->minimumWeight, 0.1);
    EXPECT_EQ(liveBytes(engine), held);
    set->endPass();

    // Every frame-node now holds a weight in [0.1, 1], read through the sampler.
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const std::span<const float> grid =
            grids.subspan(static_cast<std::size_t>(frame) * shape->valuesPerFrame(), shape->valuesPerFrame());
        for (std::uint32_t y = 0u; y < kHeight; y += 5u)
        {
            for (std::uint32_t x = 0u; x < kWidth; x += 7u)
            {
                const float weight = an::sampleLocalWeight(*shape, grid, x, y);
                EXPECT_GE(weight, 0.1f) << frame;
                EXPECT_LE(weight, 1.0f) << frame;
            }
        }
    }
}

} // namespace
