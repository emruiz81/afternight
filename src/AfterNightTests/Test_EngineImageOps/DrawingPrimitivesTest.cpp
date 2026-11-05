// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the drawing primitives: the brush tip, dabs placed along a stroke segment, dabs screened
// into a stroke plane, and a shape's coverage feathered.
//
// Oracles are written from the header's definitions. The tip, the dab positions and the stamp are Float32
// arithmetic in a stated order, so they are restated here and compared bit for bit; the stamp is also held to a
// serial walk of its dabs, whatever the number of threads. The feather is held to a separable convolution in
// double with the Gaussian's own taps and the reflected border, and a feather wider than one pass to the
// variance and mass its passes add up to.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Spatial/DrawingPrimitives.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::Sequence;

constexpr float kSentinel = -7.0f;

[[nodiscard]] an::BrushPlan brush(const an::BrushRequest& request)
{
    const an::Result<an::BrushPlan> made = an::planBrush(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

[[nodiscard]] bool sameBits(float left, float right)
{
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}

/// A plane of samples with optional row padding, owning its storage.
struct Plane
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t stride = 0u;
    std::vector<float> samples;

    Plane(std::uint32_t w, std::uint32_t h, std::uint32_t padding = 0u, float fill = 0.0f)
        : width(w), height(h), stride(w + padding), samples(static_cast<std::size_t>(w + padding) * h, fill)
    {
    }

    [[nodiscard]] float& at(std::uint32_t x, std::uint32_t y)
    {
        return samples[static_cast<std::size_t>(y) * stride + x];
    }

    [[nodiscard]] float at(std::uint32_t x, std::uint32_t y) const
    {
        return samples[static_cast<std::size_t>(y) * stride + x];
    }

    [[nodiscard]] an::PlaneView<float> view()
    {
        return *an::PlaneView<float>::create(samples, width, height, 1u, static_cast<std::uint64_t>(stride) * 4u);
    }

    [[nodiscard]] an::PlaneView<const float> constView() const
    {
        return *an::PlaneView<const float>::create(samples, width, height, 1u, static_cast<std::uint64_t>(stride) * 4u);
    }
};

/// The header's tip: a sample's coverage at its offset from the centre.
[[nodiscard]] float tipOracle(const an::BrushRequest& request, std::uint32_t side, std::uint32_t x, std::uint32_t y)
{
    const float radius = (std::max)(request.diameter * 0.5f, 0.5f);
    const float centre = static_cast<float>(side - 1u) * 0.5f;
    const float dx = static_cast<float>(x) - centre;
    const float dy = static_cast<float>(y) - centre;
    const float distance = request.shape == an::BrushTipShape::Square ? (std::max)(std::abs(dx), std::abs(dy))
                                                                      : std::sqrt((dx * dx) + (dy * dy));
    if (distance > radius)
    {
        return 0.0f;
    }
    if (request.hardness >= 0.999f)
    {
        return 1.0f;
    }
    const float inner = radius * request.hardness;
    if (distance <= inner)
    {
        return 1.0f;
    }
    const float range = (std::max)(radius - inner, 0.0001f);
    const float t = std::clamp((distance - inner) / range, 0.0f, 1.0f);
    const float edge = std::exp(-4.0f);
    const float gaussian = std::exp(-(4.0f * t * t));
    return std::clamp((gaussian - edge) / (1.0f - edge), 0.0f, 1.0f);
}

[[nodiscard]] Plane tipOf(const an::BrushPlan& plan)
{
    Plane tip(plan.tipSide(), plan.tipSide(), 0u, kSentinel);
    const an::Status written = an::writeBrushTip(plan, tip.view());
    EXPECT_FALSE(written.failed()) << written.message();
    return tip;
}

/// The header's curve, one coordinate.
[[nodiscard]] float curve(double before, double start, double end, double after, float t)
{
    const float p0 = static_cast<float>(before);
    const float p1 = static_cast<float>(start);
    const float p2 = static_cast<float>(end);
    const float p3 = static_cast<float>(after);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (p2 - p0) * t + ((2.0f * p0) - (5.0f * p1) + (4.0f * p2) - p3) * t2 +
                   (-p0 + (3.0f * p1) - (3.0f * p2) + p3) * t3);
}

[[nodiscard]] float chord(const an::BrushPoint& from, const an::BrushPoint& to)
{
    return std::hypot(static_cast<float>(to.x - from.x), static_cast<float>(to.y - from.y));
}

/// The header's dabs for a segment.
[[nodiscard]] std::vector<an::BrushDab> dabsOracle(const an::BrushPlan& plan, const an::BrushSegment& segment)
{
    const float estimate = chord(segment.start, segment.end) + (0.25f * chord(segment.before, segment.start)) +
                           (0.25f * chord(segment.end, segment.after));
    if (estimate <= 0.001f)
    {
        return {an::BrushDab{.x = static_cast<std::int32_t>(std::lround(segment.end.x)),
                             .y = static_cast<std::int32_t>(std::lround(segment.end.y))}};
    }
    const int steps = (std::max)(1, static_cast<int>(std::ceil(estimate / plan.spacingPixels())));
    std::vector<an::BrushDab> dabs;
    for (int i = 1; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = curve(segment.before.x, segment.start.x, segment.end.x, segment.after.x, t);
        const float y = curve(segment.before.y, segment.start.y, segment.end.y, segment.after.y, t);
        dabs.push_back(an::BrushDab{.x = static_cast<std::int32_t>(std::lround(static_cast<double>(x))),
                                    .y = static_cast<std::int32_t>(std::lround(static_cast<double>(y)))});
    }
    return dabs;
}

[[nodiscard]] std::vector<an::BrushDab> placed(const an::BrushPlan& plan, const an::BrushSegment& segment)
{
    const an::Result<std::uint32_t> count = an::countBrushSegmentDabs(plan, segment);
    EXPECT_TRUE(count.hasValue()) << count.status().message();
    std::vector<an::BrushDab> dabs(count.hasValue() ? *count : 0u);
    const an::Result<std::uint32_t> written = an::placeBrushSegmentDabs(plan, segment, dabs);
    EXPECT_TRUE(written.hasValue()) << written.status().message();
    EXPECT_EQ(written.hasValue() ? *written : 0u, dabs.size());
    return dabs;
}

/// The header's stamp, walked serially: every dab in order, clipped, screened in.
void stampOracle(const Plane& tip, std::span<const an::BrushDab> dabs, float strength, Plane& stroke)
{
    const auto side = static_cast<std::int64_t>(tip.width);
    for (const an::BrushDab& dab : dabs)
    {
        const std::int64_t left = dab.x - side / 2;
        const std::int64_t top = dab.y - side / 2;
        for (std::int64_t y = (std::max<std::int64_t>)(top, 0); y < (std::min<std::int64_t>)(top + side, stroke.height);
             ++y)
        {
            for (std::int64_t x = (std::max<std::int64_t>)(left, 0);
                 x < (std::min<std::int64_t>)(left + side, stroke.width); ++x)
            {
                const float alpha =
                    tip.at(static_cast<std::uint32_t>(x - left), static_cast<std::uint32_t>(y - top)) * strength;
                if (alpha <= 0.0f)
                {
                    continue;
                }
                float& sample = stroke.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                sample = 1.0f - ((1.0f - sample) * (1.0f - alpha));
            }
        }
    }
}

[[nodiscard]] an::CoverageFeatherPlan feather(float width)
{
    const an::Result<an::CoverageFeatherPlan> made =
        an::planCoverageFeather(an::CoverageFeatherRequest{.feather = width});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// A coordinate reflected about the edge samples without repeating them, periodically.
[[nodiscard]] std::int64_t reflect(std::int64_t index, std::int64_t size)
{
    if (size == 1)
    {
        return 0;
    }
    const std::int64_t period = 2 * size - 2;
    std::int64_t folded = index % period;
    if (folded < 0)
    {
        folded += period;
    }
    return folded < size ? folded : period - folded;
}

/// A separable Gaussian of one pass, in double, with the engine's own taps.
[[nodiscard]] std::vector<double> gaussianOracle(const std::vector<double>& input, std::uint32_t width,
                                                 std::uint32_t height, double sigma)
{
    const auto radius = static_cast<std::uint32_t>(std::ceil(4.0 * sigma));
    std::vector<double> taps(2u * radius + 1u);
    EXPECT_TRUE(an::writeGaussianTaps(sigma, radius, taps).hasValue());
    std::vector<double> across(input.size(), 0.0);
    std::vector<double> output(input.size(), 0.0);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            double sum = 0.0;
            for (std::uint32_t k = 0u; k < taps.size(); ++k)
            {
                const std::int64_t at = reflect(static_cast<std::int64_t>(x) + k - radius, width);
                sum += taps[k] * input[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(at)];
            }
            across[static_cast<std::size_t>(y) * width + x] = sum;
        }
    }
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            double sum = 0.0;
            for (std::uint32_t k = 0u; k < taps.size(); ++k)
            {
                const std::int64_t at = reflect(static_cast<std::int64_t>(y) + k - radius, height);
                sum += taps[k] * across[static_cast<std::size_t>(at) * width + x];
            }
            output[static_cast<std::size_t>(y) * width + x] = sum;
        }
    }
    return output;
}

/// A shape's coverage: a filled disc with a soft rim, and a few samples a coverage would not hold.
[[nodiscard]] Plane discCoverage(std::uint32_t width, std::uint32_t height)
{
    Plane plane(width, height);
    const double cx = width * 0.45;
    const double cy = height * 0.55;
    const double radius = (std::min)(width, height) * 0.3;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double distance = std::hypot(x - cx, y - cy);
            plane.at(x, y) = static_cast<float>(std::clamp(radius - distance + 0.5, 0.0, 1.0));
        }
    }
    return plane;
}

[[nodiscard]] an::ExecutionContext stoppedExecution(const an::EngineContext& engine)
{
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

} // namespace

TEST(DrawingPrimitivesTest, ABrushPlanChecksEveryFieldAndDerivesItsTipAndSpacing)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const an::BrushRequest& refused :
         {an::BrushRequest{.diameter = 0.5f}, an::BrushRequest{.diameter = nan}, an::BrushRequest{.diameter = 1025.0f},
          an::BrushRequest{.hardness = -0.1f}, an::BrushRequest{.hardness = 1.1f},
          an::BrushRequest{.shape = static_cast<an::BrushTipShape>(2u)}, an::BrushRequest{.spacing = 0.005f},
          an::BrushRequest{.spacing = 1.5f}, an::BrushRequest{.spacing = nan}, an::BrushRequest{.flow = 1.2f},
          an::BrushRequest{.flow = nan}})
    {
        EXPECT_EQ(an::planBrush(refused).status().code(), an::ErrorCode::InvalidArgument);
    }
    // The side is the diameter rounded up to a whole number and then to an odd one.
    EXPECT_EQ(brush({.diameter = 1.0f}).tipSide(), 1u);
    EXPECT_EQ(brush({.diameter = 6.0f}).tipSide(), 7u);
    EXPECT_EQ(brush({.diameter = 6.2f}).tipSide(), 7u);
    EXPECT_EQ(brush({.diameter = 7.0f}).tipSide(), 7u);
    EXPECT_EQ(brush({.diameter = 1024.0f}).tipSide(), 1025u);
    // The spacing is the diameter's fraction, and never below one pixel.
    EXPECT_EQ(brush({.diameter = 20.0f, .spacing = 0.25f}).spacingPixels(), 5.0f);
    EXPECT_EQ(brush({.diameter = 2.0f, .spacing = 0.25f}).spacingPixels(), 1.0f);
    EXPECT_TRUE(an::isBrushTipShape(an::BrushTipShape::Square));
    EXPECT_FALSE(an::isBrushTipShape(static_cast<an::BrushTipShape>(2u)));
}

TEST(DrawingPrimitivesTest, ATipIsItsDefinitionBitForBitForBothShapesAndEveryHardness)
{
    for (const an::BrushTipShape shape : {an::BrushTipShape::Round, an::BrushTipShape::Square})
    {
        for (const float diameter : {1.0f, 2.0f, 6.0f, 9.0f, 12.5f, 31.0f})
        {
            for (const float hardness : {0.0f, 0.3f, 0.75f, 0.998f, 1.0f})
            {
                const an::BrushRequest request{.diameter = diameter, .hardness = hardness, .shape = shape};
                const an::BrushPlan plan = brush(request);
                const Plane tip = tipOf(plan);
                for (std::uint32_t y = 0u; y < plan.tipSide(); ++y)
                {
                    for (std::uint32_t x = 0u; x < plan.tipSide(); ++x)
                    {
                        ASSERT_TRUE(sameBits(tip.at(x, y), tipOracle(request, plan.tipSide(), x, y)))
                            << diameter << " " << hardness << " at " << x << "," << y << " gave " << tip.at(x, y);
                    }
                }
                // Every tip peaks at one in its centre sample.
                const std::uint32_t middle = plan.tipSide() / 2u;
                EXPECT_EQ(tip.at(middle, middle), 1.0f);
            }
        }
    }
}

TEST(DrawingPrimitivesTest, ATipCoversWhatItsShapeAndHardnessSay)
{
    // A hard round tip of even diameter is a disc reaching the ring at exactly the radius, and nothing else.
    const Plane hard = tipOf(brush({.diameter = 6.0f, .hardness = 1.0f}));
    EXPECT_EQ(hard.at(3u, 0u), 1.0f);
    EXPECT_EQ(hard.at(0u, 0u), 0.0f);
    for (const float sample : hard.samples)
    {
        EXPECT_TRUE(sample == 0.0f || sample == 1.0f);
    }
    // A square tip covers the corners a round one misses.
    const Plane square = tipOf(brush({.diameter = 9.0f, .hardness = 1.0f, .shape = an::BrushTipShape::Square}));
    const Plane round = tipOf(brush({.diameter = 9.0f, .hardness = 1.0f}));
    EXPECT_EQ(square.at(0u, 0u), 1.0f);
    EXPECT_EQ(round.at(0u, 0u), 0.0f);
    // A soft round tip falls off monotonically along a row from its centre and reaches zero at the radius, which
    // for a diameter of twenty is the last sample of the row.
    const an::BrushPlan softPlan = brush({.diameter = 20.0f, .hardness = 0.2f});
    ASSERT_EQ(softPlan.tipSide(), 21u);
    const Plane soft = tipOf(softPlan);
    for (std::uint32_t x = 11u; x < 21u; ++x)
    {
        EXPECT_LE(soft.at(x, 10u), soft.at(x - 1u, 10u)) << x;
    }
    EXPECT_GT(soft.at(19u, 10u), 0.0f);
    EXPECT_EQ(soft.at(20u, 10u), 0.0f);
}

TEST(DrawingPrimitivesTest, ATipIsWrittenOnlyToAPlaneOfThePlansSide)
{
    const an::BrushPlan plan = brush({.diameter = 9.0f});
    Plane wrong(8u, 9u);
    EXPECT_EQ(an::writeBrushTip(plan, wrong.view()).code(), an::ErrorCode::IncompatibleArguments);
    std::vector<float> pairs(9u * 9u * 2u, kSentinel);
    const an::PlaneView<float> interleaved = *an::PlaneView<float>::create(pairs, 9u, 9u, 2u, 9u * 8u);
    EXPECT_EQ(an::writeBrushTip(plan, interleaved).code(), an::ErrorCode::IncompatibleArguments);
    for (const float sample : pairs)
    {
        EXPECT_EQ(sample, kSentinel);
    }
}

TEST(DrawingPrimitivesTest, ASegmentsDabsAreItsCurveAtEqualParameterStepsRoundedToPixels)
{
    const an::BrushPlan plan = brush({.diameter = 12.0f, .spacing = 0.15f});
    const an::BrushPoint a{.x = 10.25, .y = 20.5};
    const an::BrushPoint b{.x = 31.75, .y = 24.0};
    const an::BrushPoint c{.x = 40.5, .y = 47.125};
    const an::BrushPoint d{.x = 22.0, .y = 60.875};
    for (const an::BrushSegment& segment :
         {an::makeBrushSegment(a, a, b), an::makeBrushSegment(a, b, c), an::makeBrushSegment(b, c, d),
          an::makeBrushSegment(c, d, d), an::makeBrushSegment(d, d, c)})
    {
        const std::vector<an::BrushDab> dabs = placed(plan, segment);
        const std::vector<an::BrushDab> expected = dabsOracle(plan, segment);
        ASSERT_EQ(dabs.size(), expected.size());
        for (std::size_t index = 0u; index < dabs.size(); ++index)
        {
            EXPECT_EQ(dabs[index].x, expected[index].x) << index;
            EXPECT_EQ(dabs[index].y, expected[index].y) << index;
        }
        // The last dab lies on the segment's end, and none on its start unless the segment is degenerate.
        EXPECT_EQ(dabs.back().x, static_cast<std::int32_t>(std::lround(segment.end.x)));
        EXPECT_EQ(dabs.back().y, static_cast<std::int32_t>(std::lround(segment.end.y)));
    }
    // The point after a segment is extrapolated from its ends.
    constexpr an::BrushSegment made =
        an::makeBrushSegment({.x = 1.0, .y = 2.0}, {.x = 3.0, .y = 5.0}, {.x = 4.0, .y = 9.0});
    static_assert(made.after.x == 5.0 && made.after.y == 13.0);
    // A point repeated is a degenerate segment: one dab on its end, halves rounded away from zero.
    const an::BrushPoint repeated{.x = 7.5, .y = -2.5};
    const std::vector<an::BrushDab> single = placed(plan, an::makeBrushSegment(repeated, repeated, repeated));
    ASSERT_EQ(single.size(), 1u);
    EXPECT_EQ(single[0].x, 8);
    EXPECT_EQ(single[0].y, -3);
}

TEST(DrawingPrimitivesTest, ASegmentIsRefusedWhenItsPointsAreUnusableOrItWouldPlaceTooManyDabs)
{
    const an::BrushPlan plan = brush({.diameter = 1.0f, .spacing = 1.0f});
    const an::BrushPoint origin{};
    const an::BrushPoint bad{.x = std::numeric_limits<double>::quiet_NaN(), .y = 0.0};
    EXPECT_EQ(an::countBrushSegmentDabs(plan, an::makeBrushSegment(origin, origin, bad)).status().code(),
              an::ErrorCode::InvalidArgument);
    const an::BrushPoint outside{.x = an::kMaxBrushCoordinate * 2.0, .y = 0.0};
    EXPECT_EQ(an::countBrushSegmentDabs(plan, an::makeBrushSegment(origin, origin, outside)).status().code(),
              an::ErrorCode::InvalidArgument);
    const an::BrushPoint far{.x = 4.0e6, .y = 0.0};
    EXPECT_EQ(an::countBrushSegmentDabs(plan, an::makeBrushSegment(origin, origin, far)).status().code(),
              an::ErrorCode::OutOfRange);
    const an::BrushPoint near{.x = 40.0, .y = 0.0};
    const an::Result<std::uint32_t> count = an::countBrushSegmentDabs(plan, an::makeBrushSegment(origin, origin, near));
    ASSERT_TRUE(count.hasValue());
    std::vector<an::BrushDab> tooShort(*count - 1u);
    EXPECT_EQ(an::placeBrushSegmentDabs(plan, an::makeBrushSegment(origin, origin, near), tooShort).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(DrawingPrimitivesTest, AStampScreensEveryDabInStrokeOrderClippedToThePlane)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::BrushPlan plan = brush({.diameter = 9.0f, .hardness = 0.4f, .spacing = 0.2f, .flow = 0.6f});
    const Plane tip = tipOf(plan);
    // Overlapping dabs, one hanging off the left edge, one off the bottom right, one entirely outside.
    const std::vector<an::BrushDab> dabs{{.x = 10, .y = 8},  {.x = 12, .y = 9},  {.x = 13, .y = 9}, {.x = -2, .y = 5},
                                         {.x = 30, .y = 22}, {.x = -20, .y = 4}, {.x = 12, .y = 9}};
    Plane stroke(32u, 24u, 4u);
    // A stroke already holding coverage, which the dabs add to.
    stroke.at(11u, 9u) = 0.25f;
    Plane expected = stroke;
    stampOracle(tip, dabs, 0.6f * 0.8f, expected);
    const an::Result<an::BrushStampResult> result =
        an::stampBrushDabs(execution, plan, tip.constView(), dabs, 0.8f, stroke.view());
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    for (std::size_t index = 0u; index < stroke.samples.size(); ++index)
    {
        ASSERT_TRUE(sameBits(stroke.samples[index], expected.samples[index])) << index;
    }
    EXPECT_EQ(result->dabsStamped, 6u);
    EXPECT_EQ(result->dirty.x, 0u);
    EXPECT_EQ(result->dirty.y, 1u);
    EXPECT_EQ(result->dirty.width, 32u);
    EXPECT_EQ(result->dirty.height, 23u);
    std::uint64_t covered = 0u;
    for (const an::BrushDab& dab : dabs)
    {
        for (std::int64_t y = dab.y - 4; y <= dab.y + 4; ++y)
        {
            for (std::int64_t x = dab.x - 4; x <= dab.x + 4; ++x)
            {
                if (x >= 0 && x < 32 && y >= 0 && y < 24 &&
                    tip.at(static_cast<std::uint32_t>(x - dab.x + 4), static_cast<std::uint32_t>(y - dab.y + 4)) > 0.0f)
                {
                    ++covered;
                }
            }
        }
    }
    EXPECT_EQ(result->samplesCovered, covered);
}

TEST(DrawingPrimitivesTest, AStrokeIsTheSameSampleForSampleWhateverTheNumberOfThreads)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    const an::BrushPlan plan = brush({.diameter = 23.0f, .hardness = 0.35f, .spacing = 0.08f, .flow = 0.3f});
    const Plane tip = tipOf(plan);
    // A long stroke that doubles back on itself, so many dabs reach the same pixels in many rows.
    std::vector<an::BrushPoint> points;
    Sequence sequence{77u};
    for (int index = 0; index < 24; ++index)
    {
        points.push_back(an::BrushPoint{.x = 8.0 + 240.0 * sequence.next(), .y = 8.0 + 180.0 * sequence.next()});
    }
    std::vector<an::BrushDab> dabs;
    dabs.push_back(an::BrushDab{.x = static_cast<std::int32_t>(std::lround(points[0].x)),
                                .y = static_cast<std::int32_t>(std::lround(points[0].y))});
    for (std::size_t index = 1u; index < points.size(); ++index)
    {
        const an::BrushPoint before = index >= 2u ? points[index - 2u] : points[index - 1u];
        const std::vector<an::BrushDab> segment =
            placed(plan, an::makeBrushSegment(before, points[index - 1u], points[index]));
        dabs.insert(dabs.end(), segment.begin(), segment.end());
    }
    Plane serial(256u, 196u);
    Plane parallel(256u, 196u);
    Plane expected(256u, 196u);
    stampOracle(tip, dabs, 0.3f, expected);
    // The stroke covers a good part of the plane, so agreeing is not two empty planes agreeing.
    ASSERT_GT(std::count_if(expected.samples.begin(), expected.samples.end(), [](float s) { return s > 0.0f; }),
              196 * 256 / 10);
    ASSERT_TRUE(an::stampBrushDabs(one, plan, tip.constView(), dabs, 1.0f, serial.view()).hasValue());
    ASSERT_TRUE(an::stampBrushDabs(eight, plan, tip.constView(), dabs, 1.0f, parallel.view()).hasValue());
    for (std::size_t index = 0u; index < serial.samples.size(); ++index)
    {
        ASSERT_TRUE(sameBits(serial.samples[index], expected.samples[index])) << index;
        ASSERT_TRUE(sameBits(parallel.samples[index], expected.samples[index])) << index;
    }
}

TEST(DrawingPrimitivesTest, AStampOfNoStrengthDoesNothingAndAStrokeSampleIsReadAsACoverage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::BrushPlan plan = brush({.diameter = 5.0f, .hardness = 1.0f});
    const Plane tip = tipOf(plan);
    const std::vector<an::BrushDab> dabs{{.x = 4, .y = 4}};
    Plane stroke(9u, 9u, 0u, 0.0f);
    const an::Result<an::BrushStampResult> none =
        an::stampBrushDabs(execution, plan, tip.constView(), dabs, 0.0f, stroke.view());
    ASSERT_TRUE(none.hasValue());
    EXPECT_EQ(none->dabsStamped, 0u);
    EXPECT_TRUE(none->dirty.empty());
    for (const float sample : stroke.samples)
    {
        EXPECT_EQ(sample, 0.0f);
    }
    // A sample that is not a number reads as no coverage, and one above one as full coverage.
    stroke.at(4u, 4u) = std::numeric_limits<float>::quiet_NaN();
    stroke.at(5u, 4u) = 3.0f;
    const an::BrushPlan half = brush({.diameter = 5.0f, .hardness = 1.0f, .flow = 0.5f});
    ASSERT_TRUE(an::stampBrushDabs(execution, half, tip.constView(), dabs, 1.0f, stroke.view()).hasValue());
    EXPECT_EQ(stroke.at(4u, 4u), 0.5f);
    EXPECT_EQ(stroke.at(5u, 4u), 1.0f);
}

TEST(DrawingPrimitivesTest, AStampRefusesWhatItCannotDescribeAndAStoppedExecution)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::BrushPlan plan = brush({.diameter = 5.0f});
    const Plane tip = tipOf(plan);
    const std::vector<an::BrushDab> dabs{{.x = 4, .y = 4}};
    Plane stroke(9u, 9u, 0u, 0.0f);
    for (const float pressure : {-0.1f, 1.5f, std::numeric_limits<float>::quiet_NaN()})
    {
        EXPECT_EQ(an::stampBrushDabs(execution, plan, tip.constView(), dabs, pressure, stroke.view()).status().code(),
                  an::ErrorCode::InvalidArgument);
    }
    const Plane wrongTip = tipOf(brush({.diameter = 7.0f}));
    EXPECT_EQ(an::stampBrushDabs(execution, plan, wrongTip.constView(), dabs, 1.0f, stroke.view()).status().code(),
              an::ErrorCode::IncompatibleArguments);
    // A tip that is part of the stroke plane.
    Plane shared(9u, 9u);
    const an::PlaneView<float> whole = shared.view();
    const an::PlaneView<const float> corner = *whole.constView().subplane(0u, 0u, 5u, 5u);
    EXPECT_EQ(an::stampBrushDabs(execution, plan, corner, dabs, 1.0f, whole).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(
        an::stampBrushDabs(an::ExecutionContext{}, plan, tip.constView(), dabs, 1.0f, stroke.view()).status().code(),
        an::ErrorCode::NotInitialized);
    EXPECT_EQ(
        an::stampBrushDabs(stoppedExecution(engine), plan, tip.constView(), dabs, 1.0f, stroke.view()).status().code(),
        an::ErrorCode::Cancelled);
    for (const float sample : stroke.samples)
    {
        EXPECT_EQ(sample, 0.0f);
    }
}

TEST(DrawingPrimitivesTest, AFeatherPlanStatesItsSigmaAndHowManyPassesRealizeIt)
{
    const an::CoverageFeatherPlan none = feather(0.0f);
    EXPECT_EQ(none.passes(), 0u);
    EXPECT_EQ(none.sigma(), 0.0);
    EXPECT_EQ(feather(0.4f).sigma(), 0.5);
    EXPECT_EQ(feather(3.0f).sigma(), 1.5);
    EXPECT_EQ(feather(3.0f).passes(), 1u);
    EXPECT_EQ(feather(64.0f).passes(), 1u);
    // Wider than one pass of the engine's largest stencil: the passes' variances add up to the whole.
    const an::CoverageFeatherPlan wide = feather(100.0f);
    EXPECT_EQ(wide.sigma(), 50.0);
    EXPECT_EQ(wide.passes(), 3u);
    EXPECT_NEAR(wide.passSigma() * wide.passSigma() * wide.passes(), 2500.0, 1.0e-9);
    EXPECT_EQ(feather(an::kMaxCoverageFeather).passes(), 16u);
    for (const float refused : {-1.0f, an::kMaxCoverageFeather * 2.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        EXPECT_EQ(an::planCoverageFeather(an::CoverageFeatherRequest{.feather = refused}).status().code(),
                  an::ErrorCode::InvalidArgument);
    }
}

TEST(DrawingPrimitivesTest, NoFeatherWritesTheCoverageAsItReads)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Plane coverage = discCoverage(21u, 17u);
    coverage.at(3u, 3u) = std::numeric_limits<float>::quiet_NaN();
    coverage.at(4u, 3u) = 2.0f;
    coverage.at(5u, 3u) = -1.0f;
    Plane destination(21u, 17u, 3u, kSentinel);
    const an::Result<an::CoverageFeatherResult> result =
        an::featherCoverage(execution, feather(0.0f), coverage.constView(), destination.view());
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->passes, 0u);
    EXPECT_EQ(result->samplesWritten, 21u * 17u);
    for (std::uint32_t y = 0u; y < 17u; ++y)
    {
        for (std::uint32_t x = 0u; x < 21u; ++x)
        {
            const float source = coverage.at(x, y);
            const float read = std::isnan(source) ? 0.0f : std::clamp(source, 0.0f, 1.0f);
            ASSERT_EQ(destination.at(x, y), read) << x << "," << y;
        }
    }
}

TEST(DrawingPrimitivesTest, AFeatherIsTheExactGaussianOfHalfItsWidthWithTheEdgeReflected)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const float width : {0.6f, 3.0f, 7.0f, 30.0f})
    {
        const an::CoverageFeatherPlan plan = feather(width);
        ASSERT_EQ(plan.passes(), 1u);
        const Plane coverage = discCoverage(53u, 41u);
        Plane destination(53u, 41u, 1u, kSentinel);
        const an::Result<an::CoverageFeatherResult> result =
            an::featherCoverage(execution, plan, coverage.constView(), destination.view());
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->passes, 1u);
        std::vector<double> input(53u * 41u);
        for (std::uint32_t y = 0u; y < 41u; ++y)
        {
            for (std::uint32_t x = 0u; x < 53u; ++x)
            {
                input[static_cast<std::size_t>(y) * 53u + x] = coverage.at(x, y);
            }
        }
        const std::vector<double> expected = gaussianOracle(input, 53u, 41u, plan.sigma());
        for (std::uint32_t y = 0u; y < 41u; ++y)
        {
            for (std::uint32_t x = 0u; x < 53u; ++x)
            {
                ASSERT_NEAR(destination.at(x, y), expected[static_cast<std::size_t>(y) * 53u + x], 2.0e-6)
                    << width << " at " << x << "," << y;
            }
        }
    }
}

TEST(DrawingPrimitivesTest, AFeatherWiderThanOnePassAddsTheVariancesOfItsPasses)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext execution = makeExecution(engine, 8u);
    const an::CoverageFeatherPlan plan = feather(100.0f);
    ASSERT_EQ(plan.passes(), 3u);
    // One row and one column: the other axis's reflection over a single sample leaves it unchanged, so the
    // blur along the line is the whole blur, and the line is long enough that nothing folds back.
    for (const bool row : {true, false})
    {
        const std::uint32_t length = 1001u;
        Plane impulse(row ? length : 1u, row ? 1u : length);
        impulse.samples[length / 2u] = 1.0f;
        Plane destination(row ? length : 1u, row ? 1u : length);
        const an::Result<an::CoverageFeatherResult> result =
            an::featherCoverage(execution, plan, impulse.constView(), destination.view());
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->passes, 3u);
        double mass = 0.0;
        double variance = 0.0;
        for (std::uint32_t index = 0u; index < length; ++index)
        {
            const double offset = static_cast<double>(index) - static_cast<double>(length / 2u);
            mass += destination.samples[index];
            variance += offset * offset * destination.samples[index];
        }
        EXPECT_NEAR(mass, 1.0, 1.0e-5);
        EXPECT_NEAR(variance / mass, 2500.0, 2500.0 * 0.01);
    }
}

TEST(DrawingPrimitivesTest, AFeatherReadsItsCoverageFirstAndDoesNotDependOnTheNumberOfThreads)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    Plane dirty = discCoverage(97u, 83u);
    dirty.at(10u, 10u) = std::numeric_limits<float>::quiet_NaN();
    dirty.at(50u, 40u) = 4.0f;
    dirty.at(60u, 70u) = -3.0f;
    Plane clean = dirty;
    clean.at(10u, 10u) = 0.0f;
    clean.at(50u, 40u) = 1.0f;
    clean.at(60u, 70u) = 0.0f;
    for (const float width : {5.0f, 70.0f, 100.0f})
    {
        const an::CoverageFeatherPlan plan = feather(width);
        ASSERT_GT(plan.passes(), 0u) << width;
        Plane fromDirty(97u, 83u);
        Plane fromClean(97u, 83u);
        Plane parallel(97u, 83u, 5u);
        ASSERT_TRUE(an::featherCoverage(one, plan, dirty.constView(), fromDirty.view()).hasValue());
        ASSERT_TRUE(an::featherCoverage(one, plan, clean.constView(), fromClean.view()).hasValue());
        ASSERT_TRUE(an::featherCoverage(eight, plan, dirty.constView(), parallel.view()).hasValue());
        for (std::uint32_t y = 0u; y < 83u; ++y)
        {
            for (std::uint32_t x = 0u; x < 97u; ++x)
            {
                ASSERT_TRUE(sameBits(fromDirty.at(x, y), fromClean.at(x, y))) << width << " at " << x << "," << y;
                ASSERT_TRUE(sameBits(parallel.at(x, y), fromClean.at(x, y))) << width << " at " << x << "," << y;
                ASSERT_TRUE(std::isfinite(fromClean.at(x, y)));
            }
        }
        // The disc's centre stays above its far corner however wide the blur, so the blur wrote both.
        EXPECT_GT(fromClean.at(43u, 45u), fromClean.at(96u, 0u)) << width;
        EXPECT_GT(fromClean.at(43u, 45u), 0.0f) << width;
    }
}

TEST(DrawingPrimitivesTest, AFeatherRefusesPlanesItCannotPairAndAStoppedExecution)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::CoverageFeatherPlan plan = feather(4.0f);
    const Plane coverage = discCoverage(20u, 16u);
    Plane smaller(19u, 16u, 0u, kSentinel);
    EXPECT_EQ(an::featherCoverage(execution, plan, coverage.constView(), smaller.view()).status().code(),
              an::ErrorCode::IncompatibleArguments);
    Plane shared = discCoverage(20u, 17u);
    const an::PlaneView<float> lower =
        *an::PlaneView<float>::create(std::span<float>(shared.samples).subspan(20u), 20u, 16u, 1u, 80u);
    const an::PlaneView<const float> upper =
        *an::PlaneView<const float>::create(std::span<const float>(shared.samples).first(20u * 16u), 20u, 16u, 1u, 80u);
    EXPECT_EQ(an::featherCoverage(execution, plan, upper, lower).status().code(), an::ErrorCode::IncompatibleArguments);
    Plane destination(20u, 16u, 0u, kSentinel);
    EXPECT_EQ(
        an::featherCoverage(an::ExecutionContext{}, plan, coverage.constView(), destination.view()).status().code(),
        an::ErrorCode::NotInitialized);
    EXPECT_EQ(
        an::featherCoverage(stoppedExecution(engine), plan, coverage.constView(), destination.view()).status().code(),
        an::ErrorCode::Cancelled);
    for (const float sample : destination.samples)
    {
        EXPECT_EQ(sample, kSentinel);
    }
}
