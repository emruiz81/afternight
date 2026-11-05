// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the layer blend: five modes that decide what a stroke, a fill or a shape does to a
// coverage plane.
//
// Oracles are written from the header's definition - each sample read as a coverage, the mode's target, the
// move towards it at the opacity, all Float32 in the stated order - so a sample is compared bit for bit. The
// drawing tools' own worked values are held as well, and the plane form is held to the one-sample form.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Pointwise/LayerBlend.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stop_token>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::Sequence;

constexpr std::array<an::LayerBlendMode, 5u> kModes{an::LayerBlendMode::Normal, an::LayerBlendMode::Multiply,
                                                    an::LayerBlendMode::Screen, an::LayerBlendMode::Overlay,
                                                    an::LayerBlendMode::Erase};

constexpr float kSentinel = -7.0f;

[[nodiscard]] an::LayerBlendPlan plan(an::LayerBlendMode mode, float opacity)
{
    const an::Result<an::LayerBlendPlan> made =
        an::planLayerBlend(an::LayerBlendRequest{.mode = mode, .opacity = opacity});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// A value read as a coverage, written out from the header's words.
[[nodiscard]] float coverage(float value)
{
    if (std::isnan(value) || value < 0.0f)
    {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

/// The header's definition of one blend.
[[nodiscard]] float oracle(an::LayerBlendMode mode, float base, float source, float opacity)
{
    const float b = coverage(base);
    const float s = coverage(source);
    if (mode == an::LayerBlendMode::Erase)
    {
        return coverage(b * (1.0f - (s * opacity)));
    }
    float target = s;
    if (mode == an::LayerBlendMode::Multiply)
    {
        target = b * s;
    }
    else if (mode == an::LayerBlendMode::Screen)
    {
        target = 1.0f - ((1.0f - b) * (1.0f - s));
    }
    else if (mode == an::LayerBlendMode::Overlay)
    {
        target = b < 0.5f ? (2.0f * b * s) : (1.0f - (2.0f * (1.0f - b) * (1.0f - s)));
    }
    return coverage(b + ((target - b) * opacity));
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

/// A plane of samples drawn from [low, high).
[[nodiscard]] Plane randomPlane(std::uint32_t width, std::uint32_t height, std::uint64_t seed, float low = 0.0f,
                                float high = 1.0f, std::uint32_t padding = 0u)
{
    Plane plane(width, height, padding, kSentinel);
    Sequence sequence{seed};
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            plane.at(x, y) = low + static_cast<float>(sequence.next()) * (high - low);
        }
    }
    return plane;
}

/// A source plane that leaves about a third of its pixels uncovered.
[[nodiscard]] Plane sourcePlane(std::uint32_t width, std::uint32_t height, std::uint64_t seed)
{
    Plane plane = randomPlane(width, height, seed);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            if ((x + 2u * y) % 3u == 0u)
            {
                plane.at(x, y) = 0.0f;
            }
        }
    }
    return plane;
}

[[nodiscard]] bool sameBits(float left, float right)
{
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}

} // namespace

TEST(LayerBlendTest, ThePlanRefusesAModeOutsideItsEnumerationAndAnOpacityOutsideTheUnitInterval)
{
    EXPECT_TRUE(an::planLayerBlend(an::LayerBlendRequest{}).hasValue());
    EXPECT_EQ(an::planLayerBlend(an::LayerBlendRequest{.mode = static_cast<an::LayerBlendMode>(5u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    for (const float opacity :
         {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        EXPECT_EQ(an::planLayerBlend(an::LayerBlendRequest{.opacity = opacity}).status().code(),
                  an::ErrorCode::InvalidArgument)
            << opacity;
    }
    EXPECT_TRUE(an::planLayerBlend(an::LayerBlendRequest{.opacity = 0.0f}).hasValue());
    EXPECT_TRUE(an::isLayerBlendMode(an::LayerBlendMode::Erase));
    EXPECT_FALSE(an::isLayerBlendMode(static_cast<an::LayerBlendMode>(5u)));
}

TEST(LayerBlendTest, EveryModeIsItsDefinitionBitForBitOverAGridOfSamplesAndOpacities)
{
    std::vector<float> values;
    for (int step = 0; step <= 20; ++step)
    {
        values.push_back(static_cast<float>(step) / 20.0f);
    }
    values.insert(values.end(), {0.3333333f, 0.4999999f, 0.5000001f, 0.9999999f, 1.0e-7f});
    for (const an::LayerBlendMode mode : kModes)
    {
        for (const float opacity : {0.0f, 0.35f, 0.8f, 1.0f})
        {
            const an::LayerBlendPlan made = plan(mode, opacity);
            for (const float base : values)
            {
                for (const float source : values)
                {
                    const float blended = an::evaluateLayerBlend(made, base, source);
                    ASSERT_TRUE(sameBits(blended, oracle(mode, base, source, opacity)))
                        << static_cast<int>(mode) << " opacity " << opacity << " base " << base << " source " << source
                        << " gave " << blended;
                }
            }
        }
    }
}

TEST(LayerBlendTest, TheDrawingToolsWorkedValuesHold)
{
    // The drawing tools' own checks: one dab of a one-pixel hard brush at half flow over a single pixel.
    EXPECT_NEAR(an::evaluateLayerBlend(plan(an::LayerBlendMode::Multiply, 1.0f), 0.8f, 0.5f), 0.4f, 1.0e-6f);
    EXPECT_NEAR(an::evaluateLayerBlend(plan(an::LayerBlendMode::Screen, 1.0f), 0.2f, 0.5f), 0.6f, 1.0e-6f);
    EXPECT_NEAR(an::evaluateLayerBlend(plan(an::LayerBlendMode::Overlay, 1.0f), 0.75f, 0.25f), 0.625f, 1.0e-6f);
    // Normal reaches the source at full opacity and moves a fraction of the way at a lower one; erase removes.
    EXPECT_EQ(an::evaluateLayerBlend(plan(an::LayerBlendMode::Normal, 1.0f), 0.2f, 0.7f), 0.7f);
    EXPECT_NEAR(an::evaluateLayerBlend(plan(an::LayerBlendMode::Normal, 0.5f), 0.2f, 0.6f), 0.4f, 1.0e-7f);
    EXPECT_EQ(an::evaluateLayerBlend(plan(an::LayerBlendMode::Erase, 1.0f), 0.9f, 1.0f), 0.0f);
    EXPECT_NEAR(an::evaluateLayerBlend(plan(an::LayerBlendMode::Erase, 0.5f), 0.8f, 1.0f), 0.4f, 1.0e-7f);
}

TEST(LayerBlendTest, OverlayMultipliesADarkBaseAndScreensALightOne)
{
    const an::LayerBlendPlan overlay = plan(an::LayerBlendMode::Overlay, 1.0f);
    const float dark = std::nextafter(0.5f, 0.0f);
    EXPECT_TRUE(sameBits(an::evaluateLayerBlend(overlay, dark, 0.3f), 2.0f * dark * 0.3f));
    EXPECT_TRUE(sameBits(an::evaluateLayerBlend(overlay, 0.5f, 0.3f), 1.0f - (2.0f * 0.5f * 0.7f)));
}

TEST(LayerBlendTest, ASampleOutsideTheUnitIntervalOrNotANumberIsReadAsACoverage)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const an::LayerBlendMode mode : kModes)
    {
        const an::LayerBlendPlan made = plan(mode, 0.6f);
        for (const float base : {-1.0f, 2.0f, nan, infinity, -infinity})
        {
            for (const float source : {-0.5f, 1.5f, nan, 0.4f})
            {
                const float blended = an::evaluateLayerBlend(made, base, source);
                ASSERT_TRUE(std::isfinite(blended)) << static_cast<int>(mode);
                ASSERT_GE(blended, 0.0f);
                ASSERT_LE(blended, 1.0f);
                ASSERT_TRUE(sameBits(blended, oracle(mode, base, source, 0.6f)));
            }
        }
    }
    // A base read as one and a source read as zero: normal moves towards zero, screen keeps one.
    EXPECT_NEAR(an::evaluateLayerBlend(plan(an::LayerBlendMode::Normal, 0.25f), 3.0f, nan), 0.75f, 1.0e-7f);
    EXPECT_EQ(an::evaluateLayerBlend(plan(an::LayerBlendMode::Screen, 0.25f), 3.0f, -2.0f), 1.0f);
}

TEST(LayerBlendTest, APlaneBlendsWhatTheSourceCoversAndKeepsEveryOtherBaseSampleUnchanged)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // A base with samples a coverage would not hold, so a kept pixel is seen to be copied rather than read.
    Plane base = randomPlane(37u, 23u, 5u, -0.25f, 1.25f);
    base.at(4u, 6u) = std::numeric_limits<float>::quiet_NaN();
    const Plane source = sourcePlane(37u, 23u, 6u);
    for (const an::LayerBlendMode mode : kModes)
    {
        const an::LayerBlendPlan made = plan(mode, 0.7f);
        Plane destination(37u, 23u, 3u, kSentinel);
        const an::Result<an::LayerBlendResult> result =
            an::blendLayerPlane(execution, made, base.constView(), source.constView(), destination.view());
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        std::uint64_t covered = 0u;
        for (std::uint32_t y = 0u; y < 23u; ++y)
        {
            for (std::uint32_t x = 0u; x < 37u; ++x)
            {
                const float written = destination.at(x, y);
                if (coverage(source.at(x, y)) > 0.0f)
                {
                    ++covered;
                    ASSERT_TRUE(sameBits(written, oracle(mode, base.at(x, y), source.at(x, y), 0.7f))) << x << "," << y;
                    ASSERT_TRUE(sameBits(written, an::evaluateLayerBlend(made, base.at(x, y), source.at(x, y))));
                }
                else
                {
                    ASSERT_TRUE(sameBits(written, base.at(x, y))) << x << "," << y;
                }
            }
            // The padding after each row is not the plane's and is never written.
            for (std::uint32_t x = 37u; x < 40u; ++x)
            {
                ASSERT_EQ(destination.samples[static_cast<std::size_t>(y) * 40u + x], kSentinel);
            }
        }
        EXPECT_EQ(result->blended, covered);
        EXPECT_EQ(result->kept, 37u * 23u - covered);
    }
}

TEST(LayerBlendTest, BlendingInPlaceWritesWhatASeparateDestinationReceives)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Plane original = randomPlane(64u, 40u, 9u, 0.0f, 1.0f, 2u);
    const Plane source = sourcePlane(64u, 40u, 10u);
    for (const an::LayerBlendMode mode : kModes)
    {
        const an::LayerBlendPlan made = plan(mode, 0.45f);
        Plane separate(64u, 40u, 2u, kSentinel);
        ASSERT_TRUE(
            an::blendLayerPlane(execution, made, original.constView(), source.constView(), separate.view()).hasValue());
        Plane inPlace = original;
        const an::PlaneView<float> both = inPlace.view();
        ASSERT_TRUE(an::blendLayerPlane(execution, made, both, source.constView(), both).hasValue());
        for (std::uint32_t y = 0u; y < 40u; ++y)
        {
            for (std::uint32_t x = 0u; x < 64u; ++x)
            {
                ASSERT_TRUE(sameBits(inPlace.at(x, y), separate.at(x, y))) << x << "," << y;
            }
        }
    }
}

TEST(LayerBlendTest, ThePlaneDoesNotDependOnHowManyThreadsBlendIt)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    const Plane base = randomPlane(517u, 389u, 21u);
    const Plane source = sourcePlane(517u, 389u, 22u);
    const an::LayerBlendPlan made = plan(an::LayerBlendMode::Overlay, 0.65f);
    Plane serial(517u, 389u);
    Plane parallel(517u, 389u);
    const an::Result<an::LayerBlendResult> first =
        an::blendLayerPlane(one, made, base.constView(), source.constView(), serial.view());
    ASSERT_TRUE(first.hasValue());
    // Two thirds of the pixels are covered, so agreeing is not two untouched planes agreeing.
    EXPECT_GT(first->blended, 517u * 389u / 2u);
    ASSERT_TRUE(an::blendLayerPlane(eight, made, base.constView(), source.constView(), parallel.view()).hasValue());
    for (std::size_t index = 0u; index < serial.samples.size(); ++index)
    {
        ASSERT_TRUE(sameBits(serial.samples[index], parallel.samples[index])) << index;
    }
}

TEST(LayerBlendTest, APlaneBlendRefusesWhatItCannotDescribeAndAStoppedExecution)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::LayerBlendPlan made = plan(an::LayerBlendMode::Normal, 1.0f);
    Plane base = randomPlane(16u, 12u, 1u);
    const Plane source = sourcePlane(16u, 12u, 2u);
    Plane destination(16u, 12u, 0u, kSentinel);
    const Plane smaller = randomPlane(15u, 12u, 3u);
    EXPECT_EQ(an::blendLayerPlane(execution, made, smaller.constView(), source.constView(), destination.view())
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    // Two samples per pixel is not a coverage plane.
    std::vector<float> pairs(16u * 12u * 2u, 0.0f);
    const an::PlaneView<const float> interleaved = *an::PlaneView<const float>::create(pairs, 16u, 12u, 2u, 16u * 8u);
    EXPECT_EQ(an::blendLayerPlane(execution, made, interleaved, source.constView(), destination.view()).status().code(),
              an::ErrorCode::InvalidArgument);

    // A destination over the source, or over part of the base without being it.
    Plane shared = randomPlane(16u, 13u, 4u);
    const an::PlaneView<float> upper =
        *an::PlaneView<float>::create(std::span<float>(shared.samples).first(16u * 12u), 16u, 12u, 1u, 64u);
    const an::PlaneView<float> lower =
        *an::PlaneView<float>::create(std::span<float>(shared.samples).subspan(16u), 16u, 12u, 1u, 64u);
    EXPECT_EQ(an::blendLayerPlane(execution, made, base.constView(), upper, upper).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::blendLayerPlane(execution, made, upper, source.constView(), lower).status().code(),
              an::ErrorCode::IncompatibleArguments);
    for (const float sample : destination.samples)
    {
        ASSERT_EQ(sample, kSentinel);
    }

    EXPECT_EQ(
        an::blendLayerPlane(an::ExecutionContext{}, made, base.constView(), source.constView(), destination.view())
            .status()
            .code(),
        an::ErrorCode::NotInitialized);
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    const an::Result<an::ExecutionContext> stopped = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(stopped.hasValue());
    EXPECT_EQ(
        an::blendLayerPlane(*stopped, made, base.constView(), source.constView(), destination.view()).status().code(),
        an::ErrorCode::Cancelled);
}
