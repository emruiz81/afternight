// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for exact right-angle orientation plans.
//
// An orientation is the one geometry operation that resamples nothing: every destination sample is
// one source sample. So the questions are where each sample goes, whether its bytes arrive unchanged,
// whether a mosaic's filters travel with the samples they measured, and whether a validity rule
// travels with them too. The expected positions are written out here from the tag's definition
// rather than read from the plan, so a matrix that is wrong in the engine is wrong against this file.

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;

/// Every orientation, in the order the Exif tag numbers them.
constexpr std::array<an::ImageOrientation, 8u> kOrientations{
    an::ImageOrientation::Identity,   an::ImageOrientation::MirrorHorizontal,
    an::ImageOrientation::Rotate180,  an::ImageOrientation::MirrorVertical,
    an::ImageOrientation::Transpose,  an::ImageOrientation::Rotate90Clockwise,
    an::ImageOrientation::Transverse, an::ImageOrientation::Rotate270Clockwise};

/// A camera table, as the pattern suite uses one wherever a tabulated mosaic is needed.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// A position in an image.
struct Position
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
};

/// A source description with one channel meaning per channel count.
[[nodiscard]] an::ImageDescriptor describe(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                           an::SampleType type, const an::CfaDescriptor& cfa = {},
                                           double pixelAspectRatio = 1.0)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> made =
        an::ImageDescriptor::create({.extent = *extent,
                                     .sampleType = type,
                                     .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::RawSensorCode,
                                     .cfa = cfa,
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = pixelAspectRatio});
    EXPECT_TRUE(made.has_value());
    return *made;
}

/// Where the sample an orientation writes at a destination position is read from, written out from
/// the tag's definition independently of any plan.
[[nodiscard]] Position sourceOf(an::ImageOrientation orientation, Position destination, std::uint32_t sourceWidth,
                                std::uint32_t sourceHeight)
{
    const std::uint32_t x = destination.x;
    const std::uint32_t y = destination.y;
    const std::uint32_t lastX = sourceWidth - 1u;
    const std::uint32_t lastY = sourceHeight - 1u;
    switch (orientation)
    {
    case an::ImageOrientation::Identity:
        return {x, y};
    case an::ImageOrientation::MirrorHorizontal:
        return {lastX - x, y};
    case an::ImageOrientation::Rotate180:
        return {lastX - x, lastY - y};
    case an::ImageOrientation::MirrorVertical:
        return {x, lastY - y};
    case an::ImageOrientation::Transpose:
        return {y, x};
    case an::ImageOrientation::Rotate90Clockwise:
        return {y, lastY - x};
    case an::ImageOrientation::Transverse:
        return {lastX - y, lastY - x};
    case an::ImageOrientation::Rotate270Clockwise:
        return {lastX - y, x};
    }
    return {};
}

/// A writable view of a mask the test owns.
[[nodiscard]] an::ValidityMaskView<std::uint8_t> viewOf(an::ValidityMask& mask)
{
    const std::optional<an::ValidityMaskView<std::uint8_t>> view = mask.view();
    EXPECT_TRUE(view.has_value());
    return *view;
}

/// Orients a one-plane Float32 picture stored row-major without padding.
[[nodiscard]] std::vector<float> orientPlane(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                             const std::vector<float>& samples, std::uint32_t width,
                                             std::uint32_t height, an::ImageOrientation orientation)
{
    const an::Result<an::GeometryPlan> plan =
        an::planOrientation(describe(width, height, 1u, an::SampleType::Float32), orientation);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    if (!plan.hasValue())
    {
        return {};
    }
    const an::ImageExtent extent = plan->destinationDescriptor().extent();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, width, height);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, extent.width(), extent.height());
    std::vector<float> destination(static_cast<std::size_t>(extent.width()) * extent.height(), -1.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    EXPECT_TRUE(mask.hasValue());
    if (!mask.hasValue())
    {
        return {};
    }
    const an::ValidityMaskView<std::uint8_t> maskView = viewOf(*mask);
    const an::Result<an::GeometryResult> ran =
        an::transformImageGeometry(execution, an::SampleType::Float32, samples.data(), sourceLayout, destination.data(),
                                   destinationLayout, *plan, {}, maskView);
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    return destination;
}

TEST(GeometryOrientationTest, EachOrientationPutsTheStoredPictureWhereItsTagSays)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // 1 2 3
    // 4 5 6
    const std::vector<float> picture{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    struct Expected
    {
        an::ImageOrientation orientation;
        std::uint32_t width;
        std::uint32_t height;
        std::vector<float> samples;
    };
    const std::array<Expected, 8u> expected{{
        {an::ImageOrientation::Identity, 3u, 2u, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}},
        {an::ImageOrientation::MirrorHorizontal, 3u, 2u, {3.0f, 2.0f, 1.0f, 6.0f, 5.0f, 4.0f}},
        {an::ImageOrientation::Rotate180, 3u, 2u, {6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f}},
        {an::ImageOrientation::MirrorVertical, 3u, 2u, {4.0f, 5.0f, 6.0f, 1.0f, 2.0f, 3.0f}},
        {an::ImageOrientation::Transpose, 2u, 3u, {1.0f, 4.0f, 2.0f, 5.0f, 3.0f, 6.0f}},
        {an::ImageOrientation::Rotate90Clockwise, 2u, 3u, {4.0f, 1.0f, 5.0f, 2.0f, 6.0f, 3.0f}},
        {an::ImageOrientation::Transverse, 2u, 3u, {6.0f, 3.0f, 5.0f, 2.0f, 4.0f, 1.0f}},
        {an::ImageOrientation::Rotate270Clockwise, 2u, 3u, {3.0f, 6.0f, 2.0f, 5.0f, 1.0f, 4.0f}},
    }};
    for (const Expected& entry : expected)
    {
        const an::Result<an::GeometryPlan> plan =
            an::planOrientation(describe(3u, 2u, 1u, an::SampleType::Float32), entry.orientation);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_EQ(plan->operation(), an::GeometryOperation::Orient);
        EXPECT_EQ(plan->destinationDescriptor().extent().width(), entry.width);
        EXPECT_EQ(plan->destinationDescriptor().extent().height(), entry.height);
        EXPECT_EQ(orientPlane(engine, execution, picture, 3u, 2u, entry.orientation), entry.samples)
            << static_cast<unsigned>(entry.orientation);
    }
}

TEST(GeometryOrientationTest, TagsAndRawFlipCodesNameTheirOrientationsAndTheTurnsAgree)
{
    for (std::uint32_t tag = 1u; tag <= 8u; ++tag)
    {
        EXPECT_EQ(an::imageOrientationFromExif(tag), kOrientations[tag - 1u]) << tag;
    }
    EXPECT_FALSE(an::imageOrientationFromExif(0u).has_value());
    EXPECT_FALSE(an::imageOrientationFromExif(9u).has_value());

    constexpr std::array<an::ImageOrientation, 8u> kFlips{
        an::ImageOrientation::Identity,          an::ImageOrientation::MirrorHorizontal,
        an::ImageOrientation::MirrorVertical,    an::ImageOrientation::Rotate180,
        an::ImageOrientation::Transpose,         an::ImageOrientation::Rotate270Clockwise,
        an::ImageOrientation::Rotate90Clockwise, an::ImageOrientation::Transverse};
    for (std::int32_t flip = 0; flip < 8; ++flip)
    {
        EXPECT_EQ(an::imageOrientationFromRawFlip(flip), kFlips[static_cast<std::size_t>(flip)]) << flip;
    }
    EXPECT_FALSE(an::imageOrientationFromRawFlip(-1).has_value());
    EXPECT_FALSE(an::imageOrientationFromRawFlip(8).has_value());

    for (const an::ImageOrientation orientation : kOrientations)
    {
        EXPECT_TRUE(an::isImageOrientation(orientation));
        EXPECT_EQ(an::imageOrientationSwapsAxes(orientation), static_cast<std::uint8_t>(orientation) >= 4u);
    }
    EXPECT_FALSE(an::isImageOrientation(static_cast<an::ImageOrientation>(8u)));
    EXPECT_FALSE(an::imageOrientationSwapsAxes(static_cast<an::ImageOrientation>(8u)));

    // A two by three picture turned and mirrored each way, and the affine spellings of the same
    // turns in the geometry suite agree with it.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> picture{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    EXPECT_EQ(orientPlane(engine, execution, picture, 2u, 3u, an::ImageOrientation::MirrorHorizontal),
              (std::vector<float>{2.0f, 1.0f, 4.0f, 3.0f, 6.0f, 5.0f}));
    EXPECT_EQ(orientPlane(engine, execution, picture, 2u, 3u, an::ImageOrientation::MirrorVertical),
              (std::vector<float>{5.0f, 6.0f, 3.0f, 4.0f, 1.0f, 2.0f}));
    EXPECT_EQ(orientPlane(engine, execution, picture, 2u, 3u, an::ImageOrientation::Rotate180),
              (std::vector<float>{6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f}));
    EXPECT_EQ(orientPlane(engine, execution, picture, 2u, 3u, an::ImageOrientation::Rotate90Clockwise),
              (std::vector<float>{5.0f, 3.0f, 1.0f, 6.0f, 4.0f, 2.0f}));
    EXPECT_EQ(orientPlane(engine, execution, picture, 2u, 3u, an::ImageOrientation::Rotate270Clockwise),
              (std::vector<float>{2.0f, 4.0f, 6.0f, 1.0f, 3.0f, 5.0f}));
}

TEST(GeometryOrientationTest, EveryMosaicSampleStaysUnderTheFilterItWasMeasuredThrough)
{
    constexpr std::array<an::CfaPattern, 4u> kPatterns{an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg,
                                                       an::CfaPattern::Gbrg};
    // Odd sizes are the ones a fixed pattern table gets wrong, so both parities of both axes appear.
    constexpr std::array<Position, 4u> kSizes{{{5u, 3u}, {4u, 4u}, {1u, 2u}, {6u, 7u}}};
    for (const an::CfaPattern pattern : kPatterns)
    {
        for (std::uint32_t phaseY = 0u; phaseY < 2u; ++phaseY)
        {
            for (std::uint32_t phaseX = 0u; phaseX < 2u; ++phaseX)
            {
                const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, phaseX, phaseY);
                ASSERT_TRUE(cfa.has_value());
                for (const Position size : kSizes)
                {
                    for (const an::ImageOrientation orientation : kOrientations)
                    {
                        const an::Result<an::GeometryPlan> plan = an::planOrientation(
                            describe(size.x, size.y, 1u, an::SampleType::UInt16, *cfa), orientation);
                        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                        const an::CfaDescriptor& oriented = plan->destinationDescriptor().cfa();
                        EXPECT_TRUE(oriented.isMosaiced());
                        EXPECT_FALSE(oriented.hasFilterTable());
                        EXPECT_EQ(oriented.originX(), 0u);
                        EXPECT_EQ(oriented.originY(), 0u);
                        const an::ImageExtent extent = plan->destinationDescriptor().extent();
                        for (std::uint32_t y = 0u; y < extent.height(); ++y)
                        {
                            for (std::uint32_t x = 0u; x < extent.width(); ++x)
                            {
                                const Position from = sourceOf(orientation, {x, y}, size.x, size.y);
                                ASSERT_EQ(oriented.colorAt(x, y), cfa->colorAt(from.x, from.y))
                                    << an::cfaPatternName(pattern) << " phase " << phaseX << "," << phaseY << " size "
                                    << size.x << "x" << size.y << " orientation " << static_cast<unsigned>(orientation)
                                    << " at " << x << "," << y;
                            }
                        }
                    }
                }
            }
        }
    }

    constexpr std::array<Position, 3u> kTablePhases{{{0u, 0u}, {1u, 4u}, {5u, 5u}}};
    constexpr std::array<Position, 3u> kTableSizes{{{7u, 5u}, {6u, 6u}, {13u, 8u}}};
    for (const Position phase : kTablePhases)
    {
        const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(kCameraTable, phase.x, phase.y);
        ASSERT_TRUE(cfa.has_value());
        for (const Position size : kTableSizes)
        {
            for (const an::ImageOrientation orientation : kOrientations)
            {
                const an::Result<an::GeometryPlan> plan =
                    an::planOrientation(describe(size.x, size.y, 1u, an::SampleType::UInt16, *cfa), orientation);
                ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                const an::CfaDescriptor& oriented = plan->destinationDescriptor().cfa();
                EXPECT_EQ(oriented.pattern(), an::CfaPattern::XTrans);
                EXPECT_TRUE(oriented.hasFilterTable());
                EXPECT_EQ(oriented.originX(), 0u);
                EXPECT_EQ(oriented.originY(), 0u);
                const an::ImageExtent extent = plan->destinationDescriptor().extent();
                for (std::uint32_t y = 0u; y < extent.height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < extent.width(); ++x)
                    {
                        const Position from = sourceOf(orientation, {x, y}, size.x, size.y);
                        ASSERT_EQ(oriented.colorAt(x, y), cfa->colorAt(from.x, from.y))
                            << "table phase " << phase.x << "," << phase.y << " size " << size.x << "x" << size.y
                            << " orientation " << static_cast<unsigned>(orientation) << " at " << x << "," << y;
                    }
                }
            }
        }
    }

    // The samples move with the description: a turned mosaic holds each measurement where its
    // restated filter says it is.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::optional<an::CfaDescriptor> bayer = an::CfaDescriptor::create(an::CfaPattern::Grbg, 1u, 0u);
    ASSERT_TRUE(bayer.has_value());
    const an::ImageDescriptor mosaic = describe(5u, 3u, 1u, an::SampleType::UInt16, *bayer);
    const an::Result<an::GeometryPlan> turned = an::planOrientation(mosaic, an::ImageOrientation::Rotate90Clockwise);
    ASSERT_TRUE(turned.hasValue()) << turned.status().message();
    std::vector<std::uint16_t> samples(15u);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<std::uint16_t>(1000u + index);
    }
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::UInt16, 5u, 3u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::UInt16, 3u, 5u);
    std::vector<std::uint16_t> destination(15u, 0u);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    const an::Result<an::GeometryResult> ran =
        an::transformImageGeometry(execution, an::SampleType::UInt16, samples.data(), sourceLayout, destination.data(),
                                   destinationLayout, *turned, {}, viewOf(*mask));
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    for (std::uint32_t y = 0u; y < 5u; ++y)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            const Position from = sourceOf(an::ImageOrientation::Rotate90Clockwise, {x, y}, 5u, 3u);
            EXPECT_EQ(destination[static_cast<std::size_t>(y) * 3u + x],
                      samples[static_cast<std::size_t>(from.y) * 5u + from.x]);
            EXPECT_EQ(turned->destinationDescriptor().cfa().colorAt(x, y), bayer->colorAt(from.x, from.y));
        }
    }
}

TEST(GeometryOrientationTest, StoredBytesMoveExactlyForEveryRepresentationAndArrangement)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<an::SampleType, 8u> kTypes{
        an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
        an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 5u;
    constexpr std::uint32_t kChannels = 3u;
    const auto fillPattern = [](std::vector<std::uint8_t>& bytes) {
        for (std::size_t index = 0u; index < bytes.size(); ++index)
        {
            bytes[index] = static_cast<std::uint8_t>((index * 31u + 7u) & 0xFFu);
        }
    };
    for (const an::SampleType type : kTypes)
    {
        const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
        for (const an::ImageOrientation orientation : kOrientations)
        {
            const an::Result<an::GeometryPlan> plan =
                an::planOrientation(describe(kWidth, kHeight, kChannels, type), orientation);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const an::ImageExtent extent = plan->destinationDescriptor().extent();
            const std::uint64_t outputs = static_cast<std::uint64_t>(extent.width()) * extent.height() * kChannels;

            // Three planar channels, padded differently on each side.
            {
                const an::ImageLayout sourceLayout = planarLayout(type, kWidth, kHeight, kChannels, 3u);
                const an::ImageLayout destinationLayout =
                    planarLayout(type, extent.width(), extent.height(), kChannels, 5u);
                std::vector<std::uint8_t> source(
                    static_cast<std::size_t>(sourceLayout.planeStrideBytes() * sourceLayout.planeCount()), 0u);
                fillPattern(source);
                std::vector<std::uint8_t> destination(
                    static_cast<std::size_t>(destinationLayout.planeStrideBytes() * destinationLayout.planeCount()),
                    0xEEu);
                an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extent);
                ASSERT_TRUE(mask.hasValue());
                const an::ValidityMaskView<std::uint8_t> maskView = viewOf(*mask);
                const an::Result<an::GeometryResult> ran =
                    an::transformImageGeometry(execution, type, source.data(), sourceLayout, destination.data(),
                                               destinationLayout, *plan, {}, maskView);
                ASSERT_TRUE(ran.hasValue()) << ran.status().message();
                EXPECT_TRUE(ran->exactStoredValues);
                EXPECT_EQ(ran->run.outputsWritten, outputs);
                EXPECT_EQ(an::countValidity(maskView.constView()).valid, outputs);
                for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
                {
                    for (std::uint32_t y = 0u; y < extent.height(); ++y)
                    {
                        for (std::uint32_t x = 0u; x < extent.width(); ++x)
                        {
                            const Position from = sourceOf(orientation, {x, y}, kWidth, kHeight);
                            const std::uint64_t to = *destinationLayout.sampleByteOffset(channel, y, x, 0u);
                            const std::uint64_t at = *sourceLayout.sampleByteOffset(channel, from.y, from.x, 0u);
                            ASSERT_EQ(std::memcmp(destination.data() + to, source.data() + at, elementBytes), 0)
                                << static_cast<unsigned>(type) << " orientation " << static_cast<unsigned>(orientation)
                                << " planar channel " << channel << " at " << x << "," << y;
                        }
                        const std::uint64_t rowStart = *destinationLayout.rowByteOffset(channel, y);
                        for (std::uint64_t byte = rowStart + static_cast<std::uint64_t>(extent.width()) * elementBytes;
                             byte < rowStart + destinationLayout.rowStrideBytes(); ++byte)
                        {
                            ASSERT_EQ(destination[static_cast<std::size_t>(byte)], 0xEEu) << "padding at " << byte;
                        }
                    }
                }
            }

            // Three interleaved samples per pixel in one plane.
            {
                const an::ImageLayout sourceLayout = interleavedLayout(type, kWidth, kHeight, kChannels);
                const an::ImageLayout destinationLayout =
                    interleavedLayout(type, extent.width(), extent.height(), kChannels);
                std::vector<std::uint8_t> source(
                    static_cast<std::size_t>(sourceLayout.rowStrideBytes() * sourceLayout.extent().height()), 0u);
                fillPattern(source);
                std::vector<std::uint8_t> destination(
                    static_cast<std::size_t>(destinationLayout.rowStrideBytes() * destinationLayout.extent().height()),
                    0xEEu);
                an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
                ASSERT_TRUE(mask.hasValue());
                const an::ValidityMaskView<std::uint8_t> maskView = viewOf(*mask);
                const an::Result<an::GeometryResult> ran =
                    an::transformImageGeometry(execution, type, source.data(), sourceLayout, destination.data(),
                                               destinationLayout, *plan, {}, maskView);
                ASSERT_TRUE(ran.hasValue()) << ran.status().message();
                EXPECT_TRUE(ran->exactStoredValues);
                for (std::uint32_t y = 0u; y < extent.height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < extent.width(); ++x)
                    {
                        const Position from = sourceOf(orientation, {x, y}, kWidth, kHeight);
                        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
                        {
                            const std::uint64_t to = *destinationLayout.sampleByteOffset(0u, y, x, channel);
                            const std::uint64_t at = *sourceLayout.sampleByteOffset(0u, from.y, from.x, channel);
                            ASSERT_EQ(std::memcmp(destination.data() + to, source.data() + at, elementBytes), 0)
                                << static_cast<unsigned>(type) << " orientation " << static_cast<unsigned>(orientation)
                                << " interleaved channel " << channel << " at " << x << "," << y;
                        }
                    }
                }
            }
        }
    }
}

TEST(GeometryOrientationTest, SourceValidityMovesWithItsSamples)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 4u;
    constexpr std::uint32_t kHeight = 3u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(index + 1u);
    }
    source[5u] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::Result<an::ValidityMask> sourceMask = an::ValidityMask::create(engine, sourceLayout.extent());
    ASSERT_TRUE(sourceMask.hasValue());
    const an::ValidityMaskView<std::uint8_t> sourceMaskView = viewOf(*sourceMask);
    sourceMaskView.fill(true);
    static_cast<void>(sourceMaskView.setValid(0u, 0u, 0u, false));
    static_cast<void>(sourceMaskView.setValid(0u, 3u, 2u, false));
    static_cast<void>(sourceMaskView.setValid(0u, 2u, 1u, false));
    const an::ValidityMaskView<const std::uint8_t> sourceMaskConst = sourceMaskView.constView();
    const an::ValidityDescriptor explicitMask =
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    const an::ValidityDescriptor finiteOnly = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});

    for (const an::ImageOrientation orientation : kOrientations)
    {
        const an::Result<an::GeometryPlan> plan =
            an::planOrientation(describe(kWidth, kHeight, 1u, an::SampleType::Float32), orientation);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const an::ImageExtent extent = plan->destinationDescriptor().extent();
        const an::ImageLayout destinationLayout =
            planarLayout(an::SampleType::Float32, extent.width(), extent.height());

        std::vector<float> destination(static_cast<std::size_t>(extent.width()) * extent.height(), -7.0f);
        an::Result<an::ValidityMask> destinationMask = an::ValidityMask::create(engine, destinationLayout.extent());
        ASSERT_TRUE(destinationMask.hasValue());
        const an::ValidityMaskView<std::uint8_t> destinationMaskView = viewOf(*destinationMask);
        const an::Result<an::GeometryResult> masked = an::transformImageGeometry(
            execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(), destinationLayout,
            *plan, {.validity = explicitMask, .mask = &sourceMaskConst}, destinationMaskView);
        ASSERT_TRUE(masked.hasValue()) << masked.status().message();
        EXPECT_TRUE(masked->exactStoredValues);
        std::uint64_t expectedValid = 0u;
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                const Position from = sourceOf(orientation, {x, y}, kWidth, kHeight);
                const bool valid = sourceMaskConst.test(0u, from.x, from.y).value_or(false);
                const std::size_t index = static_cast<std::size_t>(y) * extent.width() + x;
                EXPECT_EQ(destinationMaskView.test(0u, x, y).value_or(!valid), valid) << x << "," << y;
                if (valid)
                {
                    EXPECT_EQ(std::memcmp(&destination[index],
                                          &source[static_cast<std::size_t>(from.y) * kWidth + from.x], sizeof(float)),
                              0);
                    ++expectedValid;
                }
                else
                {
                    EXPECT_EQ(destination[index], -7.0f);
                }
            }
        }
        EXPECT_EQ(masked->run.outputsWritten, expectedValid);

        std::vector<float> finite(destination.size(), -7.0f);
        an::Result<an::ValidityMask> finiteMask = an::ValidityMask::create(engine, destinationLayout.extent());
        ASSERT_TRUE(finiteMask.hasValue());
        const an::ValidityMaskView<std::uint8_t> finiteMaskView = viewOf(*finiteMask);
        const an::Result<an::GeometryResult> finiteRan =
            an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout, finite.data(),
                                       destinationLayout, *plan, {.validity = finiteOnly}, finiteMaskView);
        ASSERT_TRUE(finiteRan.hasValue()) << finiteRan.status().message();
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                const Position from = sourceOf(orientation, {x, y}, kWidth, kHeight);
                const float value = source[static_cast<std::size_t>(from.y) * kWidth + from.x];
                const std::size_t index = static_cast<std::size_t>(y) * extent.width() + x;
                EXPECT_EQ(finiteMaskView.test(0u, x, y).value_or(false), std::isfinite(value));
                if (std::isfinite(value))
                {
                    EXPECT_EQ(finite[index], value);
                }
                else
                {
                    EXPECT_EQ(finite[index], -7.0f);
                }
            }
        }
    }
}

TEST(GeometryOrientationTest, AxisSwappingOrientationsInvertThePixelAspectAndKeepTheOrigin)
{
    const an::ImageDescriptor wide = describe(5u, 3u, 1u, an::SampleType::Float32, {}, 2.0);
    an::ImageDescriptorRequest bottomUpRequest = wide.toRequest();
    bottomUpRequest.coordinateOrigin = an::CoordinateOrigin::BottomLeft;
    const std::optional<an::ImageDescriptor> bottomUp = an::ImageDescriptor::create(bottomUpRequest);
    ASSERT_TRUE(bottomUp.has_value());
    for (const an::ImageOrientation orientation : kOrientations)
    {
        const bool swaps = an::imageOrientationSwapsAxes(orientation);
        const an::Result<an::GeometryPlan> plan = an::planOrientation(*bottomUp, orientation);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const an::ImageDescriptor& destination = plan->destinationDescriptor();
        EXPECT_EQ(destination.extent().width(), swaps ? 3u : 5u);
        EXPECT_EQ(destination.extent().height(), swaps ? 5u : 3u);
        EXPECT_DOUBLE_EQ(destination.pixelAspectRatio(), swaps ? 0.5 : 2.0);
        EXPECT_EQ(destination.coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
        EXPECT_EQ(destination.sampleType(), an::SampleType::Float32);
        EXPECT_EQ(destination.extent().channelCount(), 1u);
    }
}

TEST(GeometryOrientationTest, TheResultDoesNotDependOnTheThreadWidth)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext narrow = makeExecution(engine, 1u);
    const an::ExecutionContext wide = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 257u;
    constexpr std::uint32_t kHeight = 131u;
    const std::vector<float> picture = spatial_test::randomPlane(kWidth, kHeight, 0x5EEDu);
    for (const an::ImageOrientation orientation :
         {an::ImageOrientation::Rotate90Clockwise, an::ImageOrientation::MirrorHorizontal,
          an::ImageOrientation::Transverse, an::ImageOrientation::Rotate180})
    {
        const std::vector<float> one = orientPlane(engine, narrow, picture, kWidth, kHeight, orientation);
        EXPECT_EQ(one, orientPlane(engine, wide, picture, kWidth, kHeight, orientation))
            << static_cast<unsigned>(orientation);
    }
}

TEST(GeometryOrientationTest, AnUnknownOrientationAndAPlanThatIsNoneOfTheEightAreRefusedBeforeWriting)
{
    EXPECT_EQ(an::planOrientation(describe(4u, 4u, 1u, an::SampleType::Float32), static_cast<an::ImageOrientation>(8u))
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor square = describe(4u, 4u, 1u, an::SampleType::Float32);
    const an::ImageLayout squareLayout = planarLayout(an::SampleType::Float32, 4u, 4u);
    const std::vector<float> source(16u, 1.0f);

    // A translation is not a turn or a mirror, whatever the operation says.
    const an::GeometryPlan translated{
        an::GeometryOperation::Orient, square, square, {1, 0, 1, 0, 1, 0, 0, 0, 1}, {}, 0u, 0u, {}};
    std::vector<float> destination(16u, -7.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, squareLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    const an::Result<an::GeometryResult> refused =
        an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), squareLayout, destination.data(),
                                   squareLayout, translated, {}, viewOf(*mask));
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(destination, std::vector<float>(16u, -7.0f));

    // A quarter turn's matrix over a destination that was never turned is refused too.
    const an::ImageDescriptor oblong = describe(4u, 2u, 1u, an::SampleType::Float32);
    const an::Result<an::GeometryPlan> turned = an::planOrientation(oblong, an::ImageOrientation::Rotate90Clockwise);
    ASSERT_TRUE(turned.hasValue()) << turned.status().message();
    const an::GeometryPlan unturned{
        an::GeometryOperation::Orient, oblong, oblong, turned->destinationToSource(), {}, 0u, 0u, {}};
    const an::ImageLayout oblongLayout = planarLayout(an::SampleType::Float32, 4u, 2u);
    std::vector<float> oblongDestination(8u, -7.0f);
    an::Result<an::ValidityMask> oblongMask = an::ValidityMask::create(engine, oblongLayout.extent());
    ASSERT_TRUE(oblongMask.hasValue());
    const std::vector<float> oblongSource(8u, 1.0f);
    const an::Result<an::GeometryResult> mismatched =
        an::transformImageGeometry(execution, an::SampleType::Float32, oblongSource.data(), oblongLayout,
                                   oblongDestination.data(), oblongLayout, unturned, {}, viewOf(*oblongMask));
    EXPECT_EQ(mismatched.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(oblongDestination, std::vector<float>(8u, -7.0f));
}

} // namespace
