// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for crop, resample and warp plans.

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>

#include <AfterNightEngine/Algorithms/Kernel/GatherScatterKernel.h>

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;

[[nodiscard]] an::ImageDescriptor descriptor(std::uint32_t width, std::uint32_t height,
                                             an::CfaPattern pattern = an::CfaPattern::None, std::uint32_t phaseX = 0u,
                                             std::uint32_t phaseY = 0u,
                                             an::SampleType sampleType = an::SampleType::Float32)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, phaseX, phaseY);
    EXPECT_TRUE(cfa.has_value());
    const std::optional<an::ImageDescriptor> made =
        an::ImageDescriptor::create({.extent = *extent,
                                     .sampleType = sampleType,
                                     .channelSchema = an::ChannelSchema::Mono,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::RawSensorCode,
                                     .cfa = *cfa,
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    EXPECT_TRUE(made.has_value());
    return *made;
}

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

/// A one-channel Float32 source whose mosaic is the camera table at this phase.
[[nodiscard]] an::ImageDescriptor tabulatedDescriptor(std::uint32_t width, std::uint32_t height, std::uint32_t phaseX,
                                                      std::uint32_t phaseY)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(kCameraTable, phaseX, phaseY);
    EXPECT_TRUE(cfa.has_value());
    const std::optional<an::ImageDescriptor> made =
        an::ImageDescriptor::create({.extent = *extent,
                                     .sampleType = an::SampleType::Float32,
                                     .channelSchema = an::ChannelSchema::Mono,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::RawSensorCode,
                                     .cfa = *cfa,
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    EXPECT_TRUE(made.has_value());
    return *made;
}

/// The instruction sets this build compiled vector kernels for and this host can run.
[[nodiscard]] std::vector<an::InstructionSet> compiledWarpTargets()
{
    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(index);
        if (candidate != an::InstructionSet::Automatic && candidate != an::InstructionSet::Scalar &&
            an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            targets.push_back(candidate);
        }
    }
    return targets;
}

[[nodiscard]] an::ValidityMaskView<std::uint8_t> outputMask(an::ValidityMask& mask)
{
    const std::optional<an::ValidityMaskView<std::uint8_t>> view = mask.view();
    EXPECT_TRUE(view.has_value());
    return *view;
}

TEST(GeometryOperationsTest, CropCopiesExactlyAndMovesTheCfaPhase)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor sourceDescriptor = descriptor(6u, 4u, an::CfaPattern::Rggb);
    const an::Result<an::GeometryPlan> planned =
        an::planCrop(sourceDescriptor, {.x = 1u, .y = 1u, .width = 3u, .height = 2u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->destinationDescriptor().cfa().originX(), 1u);
    EXPECT_EQ(planned->destinationDescriptor().cfa().originY(), 1u);

    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 6u, 4u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 3u, 2u);
    std::vector<float> source(24u, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(index);
    }
    std::vector<float> destination(6u, -1.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);

    const an::Result<an::GeometryResult> transformed =
        an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(),
                                   destinationLayout, *planned, {}, maskView);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_EQ(destination, (std::vector<float>{7.0f, 8.0f, 9.0f, 13.0f, 14.0f, 15.0f}));
    EXPECT_EQ(an::countValidity(maskView.constView()).valid, 6u);
}

TEST(GeometryOperationsTest, CropOfATabulatedMosaicKeepsItsTableAndMovesItsPhaseOverItsOwnBlock)
{
    // A tabulated mosaic repeats over six pixels rather than two, and its filters are a table rather
    // than a name, so the cropped descriptor has to be built from the table and its phase reduced over
    // six. Every destination pixel must see the filter its source pixel was measured under.
    const an::ImageDescriptor sourceDescriptor = tabulatedDescriptor(12u, 12u, 1u, 2u);
    const an::Result<an::GeometryPlan> planned =
        an::planCrop(sourceDescriptor, {.x = 7u, .y = 3u, .width = 5u, .height = 4u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::CfaDescriptor source = sourceDescriptor.cfa();
    const an::CfaDescriptor cropped = planned->destinationDescriptor().cfa();
    EXPECT_EQ(cropped.pattern(), an::CfaPattern::XTrans);
    EXPECT_TRUE(cropped.hasFilterTable());
    EXPECT_EQ(cropped.originX(), 2u);
    EXPECT_EQ(cropped.originY(), 5u);
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 5u; ++x)
        {
            EXPECT_EQ(cropped.colorAt(x, y), source.colorAt(x + 7u, y + 3u)) << "at " << x << ", " << y;
        }
    }
}

TEST(GeometryOperationsTest, CropCanClipAnOverhangingRequest)
{
    const an::Result<an::GeometryPlan> planned =
        an::planCrop(descriptor(6u, 4u),
                     {.x = 4u, .y = 2u, .width = 9u, .height = 9u, .bounds = an::CropBoundsPolicy::ClipToSource});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->destinationDescriptor().extent().width(), 2u);
    EXPECT_EQ(planned->destinationDescriptor().extent().height(), 2u);
}

TEST(GeometryOperationsTest, OddPaddedImageRoundTripsThroughExplicitCropAndSubviewTiles)
{
    // A split into a grid puts every remainder in the final row and column of tiles. It is deliberately
    // an explicit composition: crop plans produce caller-owned tiles, and identity crop plans write
    // those tiles into checked destination subviews.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 17u;
    constexpr std::uint32_t kHeight = 13u;
    constexpr std::uint32_t kGrid = 3u;
    const an::ImageDescriptor sourceDescriptor = descriptor(kWidth, kHeight, an::CfaPattern::Rggb, 1u, 0u);
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u, 5u);
    std::vector<float> source(static_cast<std::size_t>(sourceLayout.totalBytes() / sizeof(float)), -91.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        float* const row =
            source.data() + static_cast<std::uint64_t>(y) * sourceLayout.rowStrideBytes() / sizeof(float);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            row[x] = static_cast<float>(1000u * y + x);
        }
    }

    std::vector<an::ImageDescriptor> tileDescriptors;
    std::vector<an::ImageLayout> tileLayouts;
    std::vector<std::vector<float>> tiles;
    std::vector<std::array<std::uint32_t, 4u>> regions;
    for (std::uint32_t tileRow = 0u; tileRow < kGrid; ++tileRow)
    {
        for (std::uint32_t tileColumn = 0u; tileColumn < kGrid; ++tileColumn)
        {
            const std::uint32_t x = tileColumn * (kWidth / kGrid);
            const std::uint32_t y = tileRow * (kHeight / kGrid);
            const std::uint32_t width = tileColumn + 1u == kGrid ? kWidth - x : kWidth / kGrid;
            const std::uint32_t height = tileRow + 1u == kGrid ? kHeight - y : kHeight / kGrid;
            const an::Result<an::GeometryPlan> crop =
                an::planCrop(sourceDescriptor, {.x = x, .y = y, .width = width, .height = height});
            ASSERT_TRUE(crop.hasValue()) << crop.status().message();
            EXPECT_EQ(crop->destinationDescriptor().cfa().originX(), (1u + x) % 2u);
            EXPECT_EQ(crop->destinationDescriptor().cfa().originY(), y % 2u);

            const an::ImageLayout tileLayout =
                planarLayout(an::SampleType::Float32, width, height, 1u, 2u + tileColumn);
            std::vector<float> tile(static_cast<std::size_t>(tileLayout.totalBytes() / sizeof(float)), -37.0f);
            an::Result<an::ValidityMask> validity = an::ValidityMask::create(engine, tileLayout.extent());
            ASSERT_TRUE(validity.hasValue()) << validity.status().message();
            an::ValidityMaskView<std::uint8_t> validityView = outputMask(*validity);
            const an::Result<an::GeometryResult> split =
                an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout, tile.data(),
                                           tileLayout, *crop, {}, validityView);
            ASSERT_TRUE(split.hasValue()) << split.status().message();
            EXPECT_TRUE(split->exactStoredValues);
            EXPECT_EQ(an::countValidity(validityView.constView()).valid, static_cast<std::uint64_t>(width) * height);

            tileDescriptors.push_back(crop->destinationDescriptor());
            tileLayouts.push_back(tileLayout);
            tiles.push_back(std::move(tile));
            regions.push_back({x, y, width, height});
        }
    }
    ASSERT_EQ(tiles.size(), static_cast<std::size_t>(kGrid * kGrid));
    EXPECT_EQ(regions[2u], (std::array<std::uint32_t, 4u>{10u, 0u, 7u, 4u}));
    EXPECT_EQ(regions[8u], (std::array<std::uint32_t, 4u>{10u, 8u, 7u, 5u}));

    const an::ImageLayout joinedLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u, 7u);
    std::vector<float> joined(static_cast<std::size_t>(joinedLayout.rowStrideBytes() / sizeof(float)) * kHeight,
                              -123.0f);
    const std::optional<an::ImageView<float>> joinedView = an::ImageView<float>::create(joined, joinedLayout);
    ASSERT_TRUE(joinedView.has_value());
    for (std::size_t index = 0u; index < tiles.size(); ++index)
    {
        const auto [x, y, width, height] = regions[index];
        const std::optional<an::ImageView<float>> destination = joinedView->subview(x, y, width, height);
        ASSERT_TRUE(destination.has_value());
        const an::Result<an::GeometryPlan> identity =
            an::planCrop(tileDescriptors[index], {.x = 0u, .y = 0u, .width = width, .height = height});
        ASSERT_TRUE(identity.hasValue()) << identity.status().message();
        an::Result<an::ValidityMask> validity = an::ValidityMask::create(engine, destination->layout().extent());
        ASSERT_TRUE(validity.hasValue()) << validity.status().message();
        an::ValidityMaskView<std::uint8_t> validityView = outputMask(*validity);
        const an::Result<an::GeometryResult> joinedTile =
            an::transformImageGeometry(execution, an::SampleType::Float32, tiles[index].data(), tileLayouts[index],
                                       destination->data(), destination->layout(), *identity, {}, validityView);
        ASSERT_TRUE(joinedTile.hasValue()) << joinedTile.status().message();
        EXPECT_TRUE(joinedTile->exactStoredValues);
    }

    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const float* const sourceRow =
            source.data() + static_cast<std::uint64_t>(y) * sourceLayout.rowStrideBytes() / sizeof(float);
        const float* const joinedRow =
            joined.data() + static_cast<std::uint64_t>(y) * joinedLayout.rowStrideBytes() / sizeof(float);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            EXPECT_EQ(joinedRow[x], sourceRow[x]) << x << "," << y;
        }
        for (std::uint32_t x = kWidth; x < kWidth + 7u; ++x)
        {
            EXPECT_EQ(joinedRow[x], -123.0f) << "padding at " << x << "," << y;
        }
    }
}

TEST(GeometryOperationsTest, ResizeUsesHalfPixelCentersAndRegeneratesValidity)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::GeometryPlan> planned = an::planResize(
        descriptor(2u, 2u),
        {.width = 4u,
         .height = 4u,
         .sampling = {.interpolation = an::KernelInterpolation::Linear, .border = an::KernelBorderMode::Clamp}});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 2u, 2u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 4u, 4u);
    const std::vector<float> source{0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> destination(16u, -1.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);

    const an::Result<an::GeometryResult> transformed =
        an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(),
                                   destinationLayout, *planned, {}, maskView);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_FLOAT_EQ(destination.front(), 0.0f);
    EXPECT_FLOAT_EQ(destination.back(), 3.0f);
    EXPECT_NEAR(destination[5u], 0.75f, 1.0e-6f);
    EXPECT_EQ(an::countValidity(maskView.constView()).valid, 16u);
}

TEST(GeometryOperationsTest, ResizeOffersEveryInterpolationChoice)
{
    const std::array<an::KernelInterpolation, 7u> modes{
        an::KernelInterpolation::Nearest,     an::KernelInterpolation::Linear,   an::KernelInterpolation::Cubic,
        an::KernelInterpolation::Area,        an::KernelInterpolation::Lanczos4, an::KernelInterpolation::LinearExact,
        an::KernelInterpolation::NearestExact};
    for (const an::KernelInterpolation mode : modes)
    {
        const an::Result<an::GeometryPlan> planned =
            an::planResize(descriptor(8u, 6u), {.width = 5u, .height = 4u, .sampling = {.interpolation = mode}});
        ASSERT_TRUE(planned.hasValue()) << static_cast<unsigned>(mode) << ": " << planned.status().message();
        EXPECT_EQ(planned->sampling().interpolation, mode);
    }
}

TEST(GeometryOperationsTest, ExactRightAngleRotationsAndFlipsKeepTheDocumentedOrientation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor sourceDescriptor = descriptor(2u, 3u);
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 2u, 3u);
    const std::vector<float> source{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};

    const auto run = [&](std::uint32_t width, std::uint32_t height, std::array<double, 6u> matrix) {
        const an::Result<an::GeometryPlan> plan =
            an::planAffineWarp(sourceDescriptor, {.width = width,
                                                  .height = height,
                                                  .destinationToSource = matrix,
                                                  .sampling = {.interpolation = an::KernelInterpolation::NearestExact,
                                                               .border = an::KernelBorderMode::Constant}});
        EXPECT_TRUE(plan.hasValue()) << plan.status().message();
        const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, width, height);
        std::vector<float> destination(static_cast<std::size_t>(width) * height, -1.0f);
        an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
        EXPECT_TRUE(mask.hasValue());
        an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
        const an::Result<an::GeometryResult> transformed =
            an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout,
                                       destination.data(), destinationLayout, *plan, {}, maskView);
        EXPECT_TRUE(transformed.hasValue()) << transformed.status().message();
        return destination;
    };

    EXPECT_EQ(run(2u, 3u, {-1.0, 0.0, 1.0, 0.0, 1.0, 0.0}), (std::vector<float>{2.0f, 1.0f, 4.0f, 3.0f, 6.0f, 5.0f}));
    EXPECT_EQ(run(2u, 3u, {1.0, 0.0, 0.0, 0.0, -1.0, 2.0}), (std::vector<float>{5.0f, 6.0f, 3.0f, 4.0f, 1.0f, 2.0f}));
    EXPECT_EQ(run(2u, 3u, {-1.0, 0.0, 1.0, 0.0, -1.0, 2.0}), (std::vector<float>{6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f}));
    EXPECT_EQ(run(3u, 2u, {0.0, 1.0, 0.0, -1.0, 0.0, 2.0}), (std::vector<float>{5.0f, 3.0f, 1.0f, 6.0f, 4.0f, 2.0f}));
    EXPECT_EQ(run(3u, 2u, {0.0, -1.0, 1.0, 1.0, 0.0, 0.0}), (std::vector<float>{2.0f, 4.0f, 6.0f, 1.0f, 3.0f, 5.0f}));
}

TEST(GeometryOperationsTest, BitExactResizeModesKeepTheirCoordinateAndIntegerArithmeticRules)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const auto runNearest = [&](std::vector<float> source, std::uint32_t destinationWidth,
                                an::KernelInterpolation interpolation) {
        const std::uint32_t sourceWidth = static_cast<std::uint32_t>(source.size());
        const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, sourceWidth, 1u);
        const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, destinationWidth, 1u);
        const an::Result<an::GeometryPlan> plan =
            an::planResize(descriptor(sourceWidth, 1u),
                           {.width = destinationWidth,
                            .height = 1u,
                            .sampling = {.interpolation = interpolation, .border = an::KernelBorderMode::Clamp}});
        EXPECT_TRUE(plan.hasValue()) << plan.status().message();
        std::vector<float> destination(destinationWidth, -1.0f);
        an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
        EXPECT_TRUE(mask.hasValue());
        an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
        const an::Result<an::GeometryResult> transformed =
            an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout,
                                       destination.data(), destinationLayout, *plan, {}, maskView);
        EXPECT_TRUE(transformed.hasValue()) << transformed.status().message();
        return destination;
    };

    const std::vector<float> fiveSamples{0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(runNearest(fiveSamples, 3u, an::KernelInterpolation::Nearest), (std::vector<float>{0.0f, 1.0f, 3.0f}));
    EXPECT_EQ(runNearest(fiveSamples, 3u, an::KernelInterpolation::NearestExact),
              (std::vector<float>{0.0f, 2.0f, 4.0f}));
    EXPECT_EQ(runNearest({0.0f, 1.0f}, 5u, an::KernelInterpolation::NearestExact),
              (std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f, 1.0f}));

    const auto verifyLinearExact = [&]<typename SampleT>(
                                       an::SampleType sampleType, std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                       std::vector<SampleT> source, std::uint32_t destinationWidth,
                                       std::uint32_t destinationHeight, std::vector<SampleT> expectedExact) {
        const an::ImageLayout integerSourceLayout = planarLayout(sampleType, sourceWidth, sourceHeight);
        const an::ImageLayout integerDestinationLayout = planarLayout(sampleType, destinationWidth, destinationHeight);
        const auto runInteger = [&](an::KernelInterpolation interpolation) {
            const an::Result<an::GeometryPlan> plan =
                an::planResize(descriptor(sourceWidth, sourceHeight, an::CfaPattern::None, 0u, 0u, sampleType),
                               {.width = destinationWidth,
                                .height = destinationHeight,
                                .sampling = {.interpolation = interpolation, .border = an::KernelBorderMode::Clamp}});
            EXPECT_TRUE(plan.hasValue()) << plan.status().message();
            std::vector<SampleT> destination(static_cast<std::size_t>(destinationWidth) * destinationHeight, SampleT{});
            an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, integerDestinationLayout.extent());
            EXPECT_TRUE(mask.hasValue());
            an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
            const an::Result<an::GeometryResult> transformed =
                an::transformImageGeometry(execution, sampleType, source.data(), integerSourceLayout,
                                           destination.data(), integerDestinationLayout, *plan, {}, maskView);
            EXPECT_TRUE(transformed.hasValue()) << transformed.status().message();
            return destination;
        };

        EXPECT_NE(runInteger(an::KernelInterpolation::Linear), expectedExact);
        EXPECT_EQ(runInteger(an::KernelInterpolation::LinearExact), expectedExact);
    };

    verifyLinearExact(an::SampleType::UInt8, 2u, 1u, std::vector<std::uint8_t>{215u, 81u}, 5u, 1u,
                      std::vector<std::uint8_t>{215u, 201u, 148u, 95u, 81u});
    verifyLinearExact(an::SampleType::UInt16, 2u, 1u, std::vector<std::uint16_t>{36188u, 36685u}, 3u, 1u,
                      std::vector<std::uint16_t>{36188u, 36437u, 36685u});
    verifyLinearExact(an::SampleType::Int16, 2u, 1u, std::vector<std::int16_t>{6760, -4947}, 3u, 1u,
                      std::vector<std::int16_t>{6760, 907, -4947});
    verifyLinearExact(an::SampleType::UInt8, 2u, 2u, std::vector<std::uint8_t>{1u, 2u, 3u, 4u}, 3u, 3u,
                      std::vector<std::uint8_t>{1u, 2u, 2u, 2u, 3u, 3u, 3u, 4u, 4u});
}

TEST(GeometryOperationsTest, AreaResizeIntegratesTheWholeSourceFootprint)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::GeometryPlan> planned = an::planResize(
        descriptor(6u, 4u), {.width = 2u, .height = 2u, .sampling = {.interpolation = an::KernelInterpolation::Area}});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 6u, 4u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 2u, 2u);
    std::vector<float> source(24u, 0.0f);
    source[0u] = 1.0f;
    std::vector<float> destination(4u, -1.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);

    const an::Result<an::GeometryResult> transformed =
        an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(),
                                   destinationLayout, *planned, {}, maskView);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_NEAR(destination[0u], 1.0f / 6.0f, 1.0e-6f);
    EXPECT_FLOAT_EQ(destination[1u], 0.0f);
    EXPECT_FLOAT_EQ(destination[2u], 0.0f);
    EXPECT_FLOAT_EQ(destination[3u], 0.0f);
}

TEST(GeometryOperationsTest, LanczosResizeCanRetainOrSuppressItsDarkRinging)
{
    const an::Result<an::GeometryPlan> guardedPlan = an::planResize(
        descriptor(5u, 5u),
        {.width = 21u,
         .height = 21u,
         .sampling = {.interpolation = an::KernelInterpolation::Lanczos4, .border = an::KernelBorderMode::Clamp}});
    const an::Result<an::GeometryPlan> unguardedPlan = an::planResize(
        descriptor(5u, 5u),
        {.width = 21u,
         .height = 21u,
         .sampling = {.interpolation = an::KernelInterpolation::Lanczos4, .border = an::KernelBorderMode::Clamp},
         .ringing = an::ResizeRingingPolicy::Keep});
    ASSERT_TRUE(guardedPlan.hasValue()) << guardedPlan.status().message();
    ASSERT_TRUE(unguardedPlan.hasValue()) << unguardedPlan.status().message();

    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 5u, 5u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 21u, 21u);
    std::vector<float> source(25u, 0.0f);
    source[12u] = 1.0f;
    const auto run = [&](const an::GeometryPlan& plan) {
        std::vector<float> destination(441u, -1.0f);
        an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
        EXPECT_TRUE(mask.hasValue());
        an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
        const an::Result<an::GeometryResult> transformed =
            an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout,
                                       destination.data(), destinationLayout, plan, {}, maskView);
        EXPECT_TRUE(transformed.hasValue()) << transformed.status().message();
        return destination;
    };
    const std::vector<float> guarded = run(*guardedPlan);
    const std::vector<float> unguarded = run(*unguardedPlan);
    EXPECT_LT(*std::min_element(unguarded.begin(), unguarded.end()), -1.0e-4f);
    EXPECT_GE(*std::min_element(guarded.begin(), guarded.end()), -1.0e-6f);
    EXPECT_NE(guarded, unguarded);
}

TEST(GeometryOperationsTest, IntegerReductionCanDiscardPartialEdgeBlocks)
{
    const an::Result<an::GeometryPlan> planned =
        an::planIntegerResample(descriptor(5u, 3u), {.factor = 2u,
                                                     .direction = an::IntegerResampleDirection::Reduce,
                                                     .reduction = an::IntegerReduction::Average,
                                                     .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->destinationDescriptor().extent().width(), 2u);
    EXPECT_EQ(planned->destinationDescriptor().extent().height(), 1u);

    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 5u, 3u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 2u, 1u);
    const std::vector<float> source{0, 1, 2, 3, 100, 5, 6, 7, 8, 100, 100, 100, 100, 100, 100};
    std::vector<float> destination(2u, -1.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    ASSERT_TRUE(an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout,
                                           destination.data(), destinationLayout, *planned, {}, maskView)
                    .hasValue());
    EXPECT_EQ(destination, (std::vector<float>{3.0f, 5.0f}));
}

TEST(GeometryOperationsTest, IntegerUpsampleReplicatesStoredValuesExactly)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor sourceDescriptor =
        descriptor(2u, 2u, an::CfaPattern::None, 0u, 0u, an::SampleType::UInt32);
    const an::Result<an::GeometryPlan> planned =
        an::planIntegerResample(sourceDescriptor, {.factor = 2u, .direction = an::IntegerResampleDirection::Enlarge});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::UInt32, 2u, 2u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::UInt32, 4u, 4u);
    const std::vector<std::uint32_t> source{0u, 16'777'217u, 4'000'000'001u, 4'294'967'295u};
    std::vector<std::uint32_t> destination(16u, 7u);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    ASSERT_TRUE(an::transformImageGeometry(execution, an::SampleType::UInt32, source.data(), sourceLayout,
                                           destination.data(), destinationLayout, *planned, {}, maskView)
                    .hasValue());
    EXPECT_EQ(destination,
              (std::vector<std::uint32_t>{0u, 0u, 16'777'217u, 16'777'217u, 0u, 0u, 16'777'217u, 16'777'217u,
                                          4'000'000'001u, 4'000'000'001u, 4'294'967'295u, 4'294'967'295u,
                                          4'000'000'001u, 4'000'000'001u, 4'294'967'295u, 4'294'967'295u}));
}

TEST(GeometryOperationsTest, CfaReductionCombinesOnlySamplesUnderTheSameFilter)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::GeometryPlan> planned = an::planCfaIntegerResample(
        descriptor(8u, 8u, an::CfaPattern::Rggb),
        {.factor = 2u, .direction = an::IntegerResampleDirection::Reduce, .reduction = an::IntegerReduction::Average});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->destinationDescriptor().cfa(), descriptor(4u, 4u, an::CfaPattern::Rggb).cfa());

    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 8u, 8u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 4u, 4u);
    std::vector<float> source(64u, 0.0f);
    for (std::uint32_t y = 0u; y < 8u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            source[static_cast<std::size_t>(y) * 8u + x] = static_cast<float>((y % 2u) * 2u + x % 2u);
        }
    }
    std::vector<float> destination(16u, -1.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    ASSERT_TRUE(an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout,
                                           destination.data(), destinationLayout, *planned, {}, maskView)
                    .hasValue());
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_FLOAT_EQ(destination[static_cast<std::size_t>(y) * 4u + x],
                            static_cast<float>((y % 2u) * 2u + x % 2u));
        }
    }
}

TEST(GeometryOperationsTest, CfaAwareResampleRefusesAMosaicThatDoesNotRepeatOverTwoPixels)
{
    // The CFA-aware plan walks two-by-two phase blocks. A six-by-six table has even extents and
    // passes every other test, and resampling it by two-by-two phases would mix its filters.
    for (const an::IntegerResampleDirection direction :
         {an::IntegerResampleDirection::Reduce, an::IntegerResampleDirection::Enlarge})
    {
        EXPECT_EQ(an::planCfaIntegerResample(
                      tabulatedDescriptor(12u, 12u, 0u, 0u),
                      {.factor = 2u, .direction = direction, .reduction = an::IntegerReduction::Average})
                      .status()
                      .code(),
                  an::ErrorCode::IncompatibleArguments);
    }
}

TEST(GeometryOperationsTest, RotateAffineAndProjectivePlansUseDestinationToSourceMatrices)
{
    const an::ImageDescriptor source = descriptor(5u, 3u);
    const an::Result<an::GeometryPlan> rotated = an::planRotation(source, {.angleRadians = 0.0});
    ASSERT_TRUE(rotated.hasValue()) << rotated.status().message();
    EXPECT_EQ(rotated->destinationToSource(), (std::array<double, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1}));

    const an::Result<an::GeometryPlan> affine =
        an::planAffineWarp(source, {.width = 5u, .height = 3u, .destinationToSource = {1, 0, 0, 0, 1, 0}});
    ASSERT_TRUE(affine.hasValue()) << affine.status().message();
    const an::Result<an::GeometryPlan> projective =
        an::planProjectiveWarp(source, {.width = 5u, .height = 3u, .destinationToSource = {1, 0, 0, 0, 1, 0, 0, 0, 1}});
    ASSERT_TRUE(projective.hasValue()) << projective.status().message();
    EXPECT_EQ(affine->destinationToSource(), projective->destinationToSource());

    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 5u, 3u);
    const std::vector<float> sourceSamples{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    const std::array<an::GeometryPlan, 3> plans{*rotated, *affine, *projective};
    for (const an::GeometryPlan& plan : plans)
    {
        std::vector<float> destinationSamples(15u, -1.0f);
        const an::Result<an::GeometryResult> transformed =
            an::transformImageGeometry(execution, an::SampleType::Float32, sourceSamples.data(), layout,
                                       destinationSamples.data(), layout, plan, {}, maskView);
        ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
        EXPECT_EQ(destinationSamples, sourceSamples);
        EXPECT_EQ(transformed->run.outputsWritten, 15u);
    }
}

TEST(GeometryOperationsTest, RotationCanExpandItsCanvasToRetainTheWholeImage)
{
    const an::Result<an::GeometryPlan> planned =
        an::planRotation(descriptor(5u, 3u), {.angleRadians = std::numbers::pi / 2.0,
                                              .canvas = an::RotationCanvas::ExpandToFit,
                                              .sampling = {.interpolation = an::KernelInterpolation::Lanczos4}});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->destinationDescriptor().extent().width(), 3u);
    EXPECT_EQ(planned->destinationDescriptor().extent().height(), 5u);
}

TEST(GeometryOperationsTest, EveryCompiledWarpRowAgreesWithTheReferenceReader)
{
    // Wide enough that the interior the compiled row serves is most of the destination, and shaped
    // so the border it refuses is exercised on every edge.
    constexpr std::uint32_t kWidth = 131u;
    constexpr std::uint32_t kHeight = 79u;
    const std::vector<an::InstructionSet> targets = compiledWarpTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }

    // Padding must not enter a source window or be overwritten in the destination.
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u, 17u);
    std::vector<float> source(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(std::sin(static_cast<double>(index) * 0.017) * 1000.0 + 2048.0);
    }
    const an::ImageDescriptor sourceDescriptor = descriptor(kWidth, kHeight);
    const double cosine = 0.9986295347545738;
    const double sine = 0.05233595624294383;
    const std::array<std::array<double, 9>, 3> transforms{
        {{cosine, -sine, 3.25, sine, cosine, -1.75, 4.0e-5, -2.0e-5, 1.0},
         {0.1, 0.99, 10.25, -0.99, 0.1, 95.75, 1.0e-5, -2.0e-5, 1.0},
         {-0.1, -0.99, 120.25, 0.99, -0.1, -15.75, -1.0e-5, 2.0e-5, 1.0}}};
    for (const an::KernelInterpolation interpolation :
         {an::KernelInterpolation::Cubic, an::KernelInterpolation::Lanczos4})
        for (const auto& transform : transforms)
            for (const float border : {0.0f, 7.0f})
            {
                const an::Result<an::GeometryPlan> plan = an::planProjectiveWarp(
                    sourceDescriptor, {.width = kWidth,
                                       .height = kHeight,
                                       .destinationToSource = transform,
                                       .sampling = {.interpolation = interpolation,
                                                    .border = an::KernelBorderMode::Constant,
                                                    .constantBorder = border,
                                                    .invalid = an::SamplingInvalidPolicy::RequireAll}});
                ASSERT_TRUE(plan.hasValue()) << plan.status().message();

                // Wide enough that the reference execution and one forced target hold leases at the same time.
                const an::EngineContext engine = makeEngine();

                const auto warpWith = [&](an::ComputeBackend backend, an::InstructionSet instructionSet) {
                    an::ExecutionOptions options;
                    options.maxConcurrency = 2u;
                    options.backend = backend;
                    options.instructionSet = instructionSet;
                    options.backendFallback = an::BackendFallback::Forbidden;
                    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
                    EXPECT_TRUE(execution.hasValue()) << execution.status().message();
                    std::vector<float> destination(source.size(), -1.0f);
                    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
                    EXPECT_TRUE(mask.hasValue());
                    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
                    const an::Result<an::GeometryResult> ran =
                        an::transformImageGeometry(*execution, an::SampleType::Float32, source.data(), layout,
                                                   destination.data(), layout, *plan, {}, maskView);
                    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
                    EXPECT_EQ(an::countValidity(maskView.constView()).valid,
                              ran.hasValue() ? ran->run.selection.valid : 0u);
                    return destination;
                };

                const std::vector<float> reference =
                    warpWith(an::ComputeBackend::Reference, an::InstructionSet::Scalar);
                for (const an::InstructionSet target : targets)
                {
                    const std::vector<float> optimized = warpWith(an::ComputeBackend::Cpu, target);
                    ASSERT_EQ(optimized.size(), reference.size());
                    for (std::size_t index = 0u; index < optimized.size(); ++index)
                    {
                        const double expected = static_cast<double>(reference[index]);
                        EXPECT_NEAR(static_cast<double>(optimized[index]), expected,
                                    1e-12 * (std::max)(1.0, std::abs(expected)))
                            << an::instructionSetName(target) << " at " << index;
                    }
                }
            }
}

TEST(GeometryOperationsTest, TheCompiledLanczosRowKeepsTheReadersSampleNextToWholeSamples)
{
    // A Lanczos-4 weight next to a whole sample divides a vanishing sine by a vanishing squared distance;
    // these fractions put a tap within a billionth of a sample of either end of the interval, where a
    // weight formed from the difference of two nearly equal cosines would lose half its digits. A whole
    // sample on one axis is the reader's, and must come back exactly as the reader writes it.
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 64u;
    const std::vector<an::InstructionSet> targets = compiledWarpTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(0.25 + 0.2 * std::sin(static_cast<double>(index) * 0.37) +
                                           0.05 * std::cos(static_cast<double>(index) * 1.91));
    }
    const an::ImageDescriptor sourceDescriptor = descriptor(kWidth, kHeight);
    const an::EngineContext engine = makeEngine();
    for (const double shiftX : {1.0e-9, 0.5, 1.0 - 1.0e-9, 2.0})
        for (const double shiftY : {1.0e-9, 0.25, 1.0 - 1.0e-9, -3.0})
        {
            const an::Result<an::GeometryPlan> plan = an::planProjectiveWarp(
                sourceDescriptor, {.width = kWidth,
                                   .height = kHeight,
                                   .destinationToSource = {1, 0, shiftX, 0, 1, shiftY, 0, 0, 1},
                                   .sampling = {.interpolation = an::KernelInterpolation::Lanczos4,
                                                .border = an::KernelBorderMode::Constant,
                                                .constantBorder = 0.0f,
                                                .invalid = an::SamplingInvalidPolicy::RequireAll}});
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const auto warpWith = [&](an::ComputeBackend backend, an::InstructionSet instructionSet) {
                an::ExecutionOptions options;
                options.maxConcurrency = 2u;
                options.backend = backend;
                options.instructionSet = instructionSet;
                options.backendFallback = an::BackendFallback::Forbidden;
                an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
                EXPECT_TRUE(execution.hasValue()) << execution.status().message();
                std::vector<float> destination(source.size(), -1.0f);
                an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
                EXPECT_TRUE(mask.hasValue());
                an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
                const an::Result<an::GeometryResult> ran =
                    an::transformImageGeometry(*execution, an::SampleType::Float32, source.data(), layout,
                                               destination.data(), layout, *plan, {}, maskView);
                EXPECT_TRUE(ran.hasValue()) << ran.status().message();
                return destination;
            };
            const std::vector<float> reference = warpWith(an::ComputeBackend::Reference, an::InstructionSet::Scalar);
            const bool whole = shiftX == std::floor(shiftX) || shiftY == std::floor(shiftY);
            for (const an::InstructionSet target : targets)
            {
                const std::vector<float> optimized = warpWith(an::ComputeBackend::Cpu, target);
                ASSERT_EQ(optimized.size(), reference.size());
                for (std::size_t index = 0u; index < optimized.size(); ++index)
                {
                    if (whole)
                    {
                        ASSERT_EQ(optimized[index], reference[index])
                            << an::instructionSetName(target) << " at " << index << " shifted " << shiftX << ", "
                            << shiftY;
                        continue;
                    }
                    const double expected = static_cast<double>(reference[index]);
                    ASSERT_NEAR(static_cast<double>(optimized[index]), expected,
                                1e-12 * (std::max)(1.0, std::abs(expected)))
                        << an::instructionSetName(target) << " at " << index << " shifted " << shiftX << ", " << shiftY;
                }
            }
        }
}

TEST(GeometryOperationsTest, ConstantBorderIsAValidContributor)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor sourceDescriptor = descriptor(2u, 1u);
    const an::Result<an::GeometryPlan> planned =
        an::planAffineWarp(sourceDescriptor, {.width = 2u,
                                              .height = 1u,
                                              .destinationToSource = {1, 0, -1, 0, 1, 0},
                                              .sampling = {.interpolation = an::KernelInterpolation::Nearest,
                                                           .border = an::KernelBorderMode::Constant,
                                                           .constantBorder = 9.0f}});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 2u, 1u);
    const std::vector<float> source{1.0f, 2.0f};
    std::vector<float> destination(2u, -1.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);

    const an::Result<an::GeometryResult> transformed = an::transformImageGeometry(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned, {}, maskView);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_EQ(destination, (std::vector<float>{9.0f, 1.0f}));
    EXPECT_EQ(an::countValidity(maskView.constView()).valid, 2u);
}

TEST(GeometryOperationsTest, AReductionWithNoValidContributorLeavesItsOutputUnwritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::GeometryPlan> planned = an::planIntegerResample(
        descriptor(2u, 2u),
        {.factor = 2u, .direction = an::IntegerResampleDirection::Reduce, .reduction = an::IntegerReduction::Average});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 2u, 2u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 1u, 1u);
    const std::vector<float> source{1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> destination(1u, -7.0f);

    an::Result<an::ValidityMask> sourceMask = an::ValidityMask::create(engine, sourceLayout.extent());
    an::Result<an::ValidityMask> destinationMask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(sourceMask.hasValue());
    ASSERT_TRUE(destinationMask.hasValue());
    an::ValidityMaskView<std::uint8_t> sourceMaskView = outputMask(*sourceMask);
    an::ValidityMaskView<std::uint8_t> destinationMaskView = outputMask(*destinationMask);
    sourceMaskView.fill(false);
    const an::ValidityMaskView<const std::uint8_t> sourceMaskConst = sourceMaskView.constView();
    const an::ValidityDescriptor explicitMask =
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});

    const an::Result<an::GeometryResult> transformed = an::transformImageGeometry(
        execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(), destinationLayout,
        *planned, {.validity = explicitMask, .mask = &sourceMaskConst}, destinationMaskView);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_FLOAT_EQ(destination.front(), -7.0f);
    EXPECT_EQ(transformed->run.outputsWritten, 0u);
    EXPECT_EQ(an::countValidity(destinationMaskView.constView()).valid, 0u);
}

TEST(GeometryOperationsTest, ProjectiveDenominatorsAreJudgedAtTheCentresWhateverTheCornersSay)
{
    const an::ImageDescriptor source = descriptor(4u, 4u);
    const auto refusal = [&](const std::array<double, 9>& matrix) {
        return an::planProjectiveWarp(source, {.width = 4u, .height = 4u, .destinationToSource = matrix})
            .status()
            .code();
    };
    // Clear of zero at every corner, the decision needs no centre evaluated.
    EXPECT_EQ(refusal({1, 0, 0, 0, 1, 0, 1.0e-6, -2.0e-6, 1.0}), an::ErrorCode::Ok);
    EXPECT_EQ(refusal({1, 0, 0, 0, 1, 0, 0, 0, -3.0}), an::ErrorCode::Ok);
    // Zero at an interior centre and at a corner: the corners straddle or touch zero, so the centres decide.
    EXPECT_EQ(refusal({1, 0, 0, 0, 1, 0, 1.0, 0, -2.0}), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal({1, 0, 0, 0, 1, 0, 0, 1.0, -3.0}), an::ErrorCode::InvalidArgument);
    // The corners straddle zero but no centre lands on it, which every centre evaluated accepts.
    EXPECT_EQ(refusal({1, 0, 0, 0, 1, 0, 1.0, 0, -1.5}), an::ErrorCode::Ok);
    // On one side of zero, but too close to it for the corners' margin: the centres decide, and accept.
    EXPECT_EQ(refusal({1, 0, 0, 0, 1, 0, 1.0e16, 0, 1.0}), an::ErrorCode::Ok);
}

TEST(GeometryOperationsTest, GenericInterpolationRefusesMosaicsAndSingularProjectiveCoordinates)
{
    EXPECT_EQ(an::planResize(descriptor(8u, 8u, an::CfaPattern::Rggb), {.width = 4u, .height = 4u}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::planProjectiveWarp(descriptor(4u, 4u),
                                     {.width = 4u, .height = 4u, .destinationToSource = {1, 0, 0, 0, 1, 0, 0, 0, 0}})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(GeometryOperationsTest, ADirectlyConstructedMalformedPlanIsRefusedBeforeWriting)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor sourceDescriptor = descriptor(4u, 4u);
    const an::ImageDescriptor destinationDescriptor = descriptor(2u, 2u);
    const an::GeometryPlan malformed{an::GeometryOperation::Crop,
                                     sourceDescriptor,
                                     destinationDescriptor,
                                     {1, 0, 0, 0, 1, 0, 0, 0, 1},
                                     {},
                                     4u,
                                     0u,
                                     {}};
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, 4u, 4u);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, 2u, 2u);
    const std::vector<float> source(16u, 1.0f);
    std::vector<float> destination(4u, -7.0f);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);

    const an::Result<an::GeometryResult> transformed =
        an::transformImageGeometry(execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(),
                                   destinationLayout, malformed, {}, maskView);
    EXPECT_EQ(transformed.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(destination, (std::vector<float>{-7.0f, -7.0f, -7.0f, -7.0f}));
}

TEST(GeometryOperationsTest, CropOverPaddedStridesIsExactForEveryRepresentation)
{
    // A crop under the all-valid rule moves stored bytes and nothing else, so for every resident
    // representation, planar or interleaved, padded on either side, the destination holds the
    // source rectangle bit for bit and every destination bit is valid.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<an::SampleType, 8u> kTypes{
        an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
        an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};
    constexpr std::uint32_t kSourceWidth = 11u;
    constexpr std::uint32_t kSourceHeight = 7u;
    constexpr std::uint32_t kCropX = 2u;
    constexpr std::uint32_t kCropY = 1u;
    constexpr std::uint32_t kCropWidth = 6u;
    constexpr std::uint32_t kCropHeight = 4u;
    for (const an::SampleType type : kTypes)
    {
        const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
        const auto fillPattern = [](std::vector<std::uint8_t>& bytes) {
            for (std::size_t index = 0u; index < bytes.size(); ++index)
            {
                bytes[index] = static_cast<std::uint8_t>((index * 31u + 7u) & 0xFFu);
            }
        };
        // Three channels under one descriptor, whether they are stored as planes or interleaved.
        const an::ImageDescriptor colourDescriptor = [&]() {
            const std::optional<an::ImageExtent> extent =
                an::ImageExtent::create(kSourceWidth, kSourceHeight, 3u, an::defaultEngineLimits());
            EXPECT_TRUE(extent.has_value());
            const std::optional<an::ImageDescriptor> made =
                an::ImageDescriptor::create({.extent = *extent,
                                             .sampleType = type,
                                             .channelSchema = an::ChannelSchema::Rgb,
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = an::NumericDomain::RawSensorCode,
                                             .cfa = *an::CfaDescriptor::create(an::CfaPattern::None, 0u, 0u),
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0});
            EXPECT_TRUE(made.has_value());
            return *made;
        }();
        const an::Result<an::GeometryPlan> plan =
            an::planCrop(colourDescriptor, {.x = kCropX, .y = kCropY, .width = kCropWidth, .height = kCropHeight});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();

        // Three planar channels, padded differently on each side. The buffers span every stride
        // in full so the padding after the last row can be checked too.
        {
            const an::ImageLayout sourceLayout = planarLayout(type, kSourceWidth, kSourceHeight, 3u, 3u);
            const an::ImageLayout destinationLayout = planarLayout(type, kCropWidth, kCropHeight, 3u, 5u);
            std::vector<std::uint8_t> source(
                static_cast<std::size_t>(sourceLayout.planeStrideBytes() * sourceLayout.planeCount()), 0u);
            fillPattern(source);
            std::vector<std::uint8_t> destination(
                static_cast<std::size_t>(destinationLayout.planeStrideBytes() * destinationLayout.planeCount()), 0xEEu);
            const std::optional<an::ImageExtent> maskExtent =
                an::ImageExtent::create(kCropWidth, kCropHeight, 3u, an::defaultEngineLimits());
            ASSERT_TRUE(maskExtent.has_value());
            an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, *maskExtent);
            ASSERT_TRUE(mask.hasValue());
            an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
            const an::Result<an::GeometryResult> cropped =
                an::transformImageGeometry(execution, type, source.data(), sourceLayout, destination.data(),
                                           destinationLayout, *plan, {}, maskView);
            ASSERT_TRUE(cropped.hasValue()) << static_cast<unsigned>(type) << ": " << cropped.status().message();
            EXPECT_TRUE(cropped->exactStoredValues);
            EXPECT_EQ(cropped->run.outputsWritten, static_cast<std::uint64_t>(kCropWidth) * kCropHeight * 3u);
            EXPECT_EQ(an::countValidity(maskView.constView()).valid,
                      static_cast<std::uint64_t>(kCropWidth) * kCropHeight * 3u);
            for (std::uint32_t plane = 0u; plane < 3u; ++plane)
            {
                for (std::uint32_t y = 0u; y < kCropHeight; ++y)
                {
                    const std::uint64_t from = *sourceLayout.rowByteOffset(plane, y + kCropY) +
                                               static_cast<std::uint64_t>(kCropX) * elementBytes;
                    const std::uint64_t to = *destinationLayout.rowByteOffset(plane, y);
                    EXPECT_EQ(std::memcmp(destination.data() + to, source.data() + from,
                                          static_cast<std::size_t>(kCropWidth) * elementBytes),
                              0)
                        << static_cast<unsigned>(type) << " plane " << plane << " row " << y;
                    // The padding after the row is untouched.
                    const std::uint64_t padStart = to + static_cast<std::uint64_t>(kCropWidth) * elementBytes;
                    for (std::uint64_t byte = padStart; byte < to + destinationLayout.rowStrideBytes(); ++byte)
                    {
                        ASSERT_EQ(destination[static_cast<std::size_t>(byte)], 0xEEu) << "padding at " << byte;
                    }
                }
            }
        }

        // Three interleaved samples per pixel in one plane.
        {
            const an::ImageLayout sourceLayout = spatial_test::interleavedLayout(type, kSourceWidth, kSourceHeight, 3u);
            const an::ImageLayout destinationLayout =
                spatial_test::interleavedLayout(type, kCropWidth, kCropHeight, 3u);
            std::vector<std::uint8_t> source(
                static_cast<std::size_t>(sourceLayout.rowStrideBytes() * sourceLayout.extent().height()), 0u);
            fillPattern(source);
            std::vector<std::uint8_t> destination(
                static_cast<std::size_t>(destinationLayout.rowStrideBytes() * destinationLayout.extent().height()),
                0xEEu);
            an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
            ASSERT_TRUE(mask.hasValue());
            an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
            const an::Result<an::GeometryResult> cropped =
                an::transformImageGeometry(execution, type, source.data(), sourceLayout, destination.data(),
                                           destinationLayout, *plan, {}, maskView);
            ASSERT_TRUE(cropped.hasValue()) << static_cast<unsigned>(type) << ": " << cropped.status().message();
            EXPECT_TRUE(cropped->exactStoredValues);
            EXPECT_EQ(an::countValidity(maskView.constView()).valid,
                      static_cast<std::uint64_t>(kCropWidth) * kCropHeight * maskView.planeCount());
            for (std::uint32_t y = 0u; y < kCropHeight; ++y)
            {
                const std::uint64_t from = *sourceLayout.rowByteOffset(0u, y + kCropY) +
                                           static_cast<std::uint64_t>(kCropX) * 3u * elementBytes;
                const std::uint64_t to = *destinationLayout.rowByteOffset(0u, y);
                EXPECT_EQ(std::memcmp(destination.data() + to, source.data() + from,
                                      static_cast<std::size_t>(kCropWidth) * 3u * elementBytes),
                          0)
                    << static_cast<unsigned>(type) << " interleaved row " << y;
            }
        }
    }
}

namespace
{

/// A three-channel description of one representation, whose channels may be stored as planes or interleaved.
[[nodiscard]] an::ImageDescriptor colourDescriptor(an::SampleType type, std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 3u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> made =
        an::ImageDescriptor::create({.extent = *extent,
                                     .sampleType = type,
                                     .channelSchema = an::ChannelSchema::Rgb,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::RawSensorCode,
                                     .cfa = *an::CfaDescriptor::create(an::CfaPattern::None, 0u, 0u),
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    EXPECT_TRUE(made.has_value());
    return *made;
}

/// Fills a buffer of one representation: integers over their whole range, floating samples with full mantissas over
/// magnitudes sixty binary orders apart, so no Float64 sum of a block is exact and one taken in another order than the
/// reducer's rounds differently.
void fillForReduction(an::SampleType type, std::vector<std::uint8_t>& bytes, std::uint64_t seed)
{
    spatial_test::Sequence sequence{seed};
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    for (std::size_t offset = 0u; offset + elementBytes <= bytes.size(); offset += elementBytes)
    {
        const double magnitude = std::ldexp(1.0, static_cast<int>(sequence.next() * 60.0f));
        const double mantissa = static_cast<double>(sequence.next()) - 0.5 +
                                std::ldexp(static_cast<double>(sequence.next()), -24) +
                                std::ldexp(static_cast<double>(sequence.next()), -48);
        const double value = mantissa * magnitude;
        if (type == an::SampleType::Float32)
        {
            const auto sample = static_cast<float>(value);
            std::memcpy(bytes.data() + offset, &sample, sizeof(sample));
        }
        else if (type == an::SampleType::Float64)
        {
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
        }
        else
        {
            for (std::uint32_t byte = 0u; byte < elementBytes; ++byte)
            {
                bytes[offset + byte] = static_cast<std::uint8_t>(sequence.next() * 256.0f);
            }
        }
    }
}

} // namespace

TEST(GeometryOperationsTest, AnAverageOverASourceWhoseEverySampleCountsIsTheGenericReducersMean)
{
    // A source every sample of which counts takes a reduction of its own, which sums each block row by row over
    // contiguous memory. It must write what the generic reducer writes: the same means, to the bit, in every
    // representation, planar or interleaved, padded, at factors that leave partial edge blocks behind.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<an::SampleType, 8u> kTypes{
        an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
        an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};
    constexpr std::uint32_t kWidth = 23u;
    constexpr std::uint32_t kHeight = 15u;
    const an::ValidityDescriptor explicitMask =
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    for (const an::SampleType type : kTypes)
    {
        const an::ImageDescriptor source = colourDescriptor(type, kWidth, kHeight);
        for (const std::uint32_t factor : {2u, 3u, 7u})
        {
            const an::Result<an::GeometryPlan> plan =
                an::planIntegerResample(source, {.factor = factor,
                                                 .direction = an::IntegerResampleDirection::Reduce,
                                                 .reduction = an::IntegerReduction::Average,
                                                 .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges});
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const std::uint32_t width = kWidth / factor;
            const std::uint32_t height = kHeight / factor;
            for (const bool interleaved : {false, true})
            {
                const an::ImageLayout sourceLayout = interleaved
                                                         ? spatial_test::interleavedLayout(type, kWidth, kHeight, 3u)
                                                         : planarLayout(type, kWidth, kHeight, 3u, 3u);
                const an::ImageLayout destinationLayout = interleaved
                                                              ? spatial_test::interleavedLayout(type, width, height, 3u)
                                                              : planarLayout(type, width, height, 3u, 1u);
                std::vector<std::uint8_t> samples(static_cast<std::size_t>(sourceLayout.totalBytes()), 0u);
                fillForReduction(type, samples, 11u + factor);
                const std::string what = std::to_string(static_cast<unsigned>(type)) + " by " + std::to_string(factor) +
                                         (interleaved ? " interleaved" : " planar");

                // Every bit set under the explicit-mask rule: the generic reducer, counting every sample.
                an::Result<an::ValidityMask> everySample = an::ValidityMask::create(engine, sourceLayout.extent());
                ASSERT_TRUE(everySample.hasValue());
                an::ValidityMaskView<std::uint8_t> everySampleView = outputMask(*everySample);
                everySampleView.fill(true);
                const an::ValidityMaskView<const std::uint8_t> sourceMask = everySampleView.constView();

                std::vector<std::uint8_t> generic(static_cast<std::size_t>(destinationLayout.totalBytes()), 0xEEu);
                std::vector<std::uint8_t> direct(generic.size(), 0xEEu);
                an::Result<an::ValidityMask> genericMask = an::ValidityMask::create(engine, destinationLayout.extent());
                an::Result<an::ValidityMask> directMask = an::ValidityMask::create(engine, destinationLayout.extent());
                ASSERT_TRUE(genericMask.hasValue() && directMask.hasValue());
                an::ValidityMaskView<std::uint8_t> genericMaskView = outputMask(*genericMask);
                an::ValidityMaskView<std::uint8_t> directMaskView = outputMask(*directMask);

                const an::Result<an::GeometryResult> byGeneric = an::transformImageGeometry(
                    execution, type, samples.data(), sourceLayout, generic.data(), destinationLayout, *plan,
                    {.validity = explicitMask, .mask = &sourceMask}, genericMaskView);
                ASSERT_TRUE(byGeneric.hasValue()) << what << ": " << byGeneric.status().message();
                const an::Result<an::GeometryResult> byDirect =
                    an::transformImageGeometry(execution, type, samples.data(), sourceLayout, direct.data(),
                                               destinationLayout, *plan, {}, directMaskView);
                ASSERT_TRUE(byDirect.hasValue()) << what << ": " << byDirect.status().message();

                EXPECT_EQ(direct, generic) << what;
                EXPECT_EQ(byDirect->run.outputsWritten, byGeneric->run.outputsWritten) << what;
                EXPECT_EQ(byDirect->run.outputsWritten, static_cast<std::uint64_t>(width) * height * 3u) << what;
                EXPECT_FALSE(byDirect->exactStoredValues) << what;
                EXPECT_EQ(an::countValidity(directMaskView.constView()).valid,
                          an::countValidity(genericMaskView.constView()).valid)
                    << what;
            }
        }
    }
}

TEST(GeometryOperationsTest, AnAverageOverAFiniteOnlySourceLeavesOutWhatIsNotFiniteAsTheGenericReducerDoes)
{
    // Under the finite-only rule a floating block's mean is of its finite samples alone, summed in the generic
    // reducer's order; a block with nothing finite leaves its output unwritten and invalid. The generic reducer is
    // forced by a mask whose bits are the samples' finiteness, which selects the same samples.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 23u;
    constexpr std::uint32_t kHeight = 15u;
    const an::ValidityDescriptor finiteOnly = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    const an::ValidityDescriptor explicitMask =
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    for (const an::SampleType type : {an::SampleType::Float32, an::SampleType::Float64})
    {
        const an::ImageDescriptor source = colourDescriptor(type, kWidth, kHeight);
        const an::ImageLayout sourceLayout = planarLayout(type, kWidth, kHeight, 3u, 2u);
        const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
        for (const bool withEmptyBlock : {false, true})
        {
            std::vector<std::uint8_t> samples(static_cast<std::size_t>(sourceLayout.totalBytes()), 0u);
            fillForReduction(type, samples, 29u);
            const auto store = [&](std::uint32_t channel, std::uint32_t x, std::uint32_t y, double value) {
                const std::uint64_t offset =
                    *sourceLayout.rowByteOffset(channel, y) + static_cast<std::uint64_t>(x) * elementBytes;
                if (type == an::SampleType::Float32)
                {
                    const auto narrowed = static_cast<float>(value);
                    std::memcpy(samples.data() + offset, &narrowed, sizeof(narrowed));
                }
                else
                {
                    std::memcpy(samples.data() + offset, &value, sizeof(value));
                }
            };
            for (std::uint32_t index = 0u; index < kWidth * kHeight; index += 7u)
            {
                store(index % 3u, index % kWidth, index / kWidth,
                      index % 2u == 0u ? std::numeric_limits<double>::quiet_NaN()
                                       : std::numeric_limits<double>::infinity());
            }
            if (withEmptyBlock)
            {
                for (std::uint32_t y = 0u; y < 2u; ++y)
                {
                    for (std::uint32_t x = 0u; x < 2u; ++x)
                    {
                        store(1u, 4u + x, 2u + y, std::numeric_limits<double>::quiet_NaN());
                    }
                }
            }

            // The mask that selects exactly the finite samples, one plane per channel.
            an::Result<an::ValidityMask> finite = an::ValidityMask::create(engine, sourceLayout.extent());
            ASSERT_TRUE(finite.hasValue());
            an::ValidityMaskView<std::uint8_t> finiteView = outputMask(*finite);
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        const std::uint64_t offset =
                            *sourceLayout.rowByteOffset(channel, y) + static_cast<std::uint64_t>(x) * elementBytes;
                        double value = 0.0;
                        if (type == an::SampleType::Float32)
                        {
                            float narrowed = 0.0f;
                            std::memcpy(&narrowed, samples.data() + offset, sizeof(narrowed));
                            value = narrowed;
                        }
                        else
                        {
                            std::memcpy(&value, samples.data() + offset, sizeof(value));
                        }
                        ASSERT_TRUE(finiteView.setValid(channel, x, y, std::isfinite(value)));
                    }
                }
            }
            const an::ValidityMaskView<const std::uint8_t> finiteMask = finiteView.constView();

            const an::Result<an::GeometryPlan> plan =
                an::planIntegerResample(source, {.factor = 2u,
                                                 .direction = an::IntegerResampleDirection::Reduce,
                                                 .reduction = an::IntegerReduction::Average,
                                                 .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges});
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const an::ImageLayout destinationLayout = planarLayout(type, kWidth / 2u, kHeight / 2u, 3u);
            std::vector<std::uint8_t> generic(static_cast<std::size_t>(destinationLayout.totalBytes()), 0xEEu);
            std::vector<std::uint8_t> direct(generic.size(), 0xEEu);
            an::Result<an::ValidityMask> genericMask = an::ValidityMask::create(engine, destinationLayout.extent());
            an::Result<an::ValidityMask> directMask = an::ValidityMask::create(engine, destinationLayout.extent());
            ASSERT_TRUE(genericMask.hasValue() && directMask.hasValue());
            an::ValidityMaskView<std::uint8_t> genericMaskView = outputMask(*genericMask);
            an::ValidityMaskView<std::uint8_t> directMaskView = outputMask(*directMask);

            const an::Result<an::GeometryResult> byGeneric = an::transformImageGeometry(
                execution, type, samples.data(), sourceLayout, generic.data(), destinationLayout, *plan,
                {.validity = explicitMask, .mask = &finiteMask}, genericMaskView);
            ASSERT_TRUE(byGeneric.hasValue()) << byGeneric.status().message();
            const an::Result<an::GeometryResult> byDirect =
                an::transformImageGeometry(execution, type, samples.data(), sourceLayout, direct.data(),
                                           destinationLayout, *plan, {.validity = finiteOnly}, directMaskView);
            ASSERT_TRUE(byDirect.hasValue()) << byDirect.status().message();

            const std::string what =
                std::to_string(static_cast<unsigned>(type)) + (withEmptyBlock ? " with a block of nothing finite" : "");
            EXPECT_EQ(direct, generic) << what;
            EXPECT_EQ(byDirect->run.outputsWritten, byGeneric->run.outputsWritten) << what;
            const std::uint64_t outputs = static_cast<std::uint64_t>(kWidth / 2u) * (kHeight / 2u) * 3u;
            EXPECT_EQ(byDirect->run.outputsWritten, withEmptyBlock ? outputs - 1u : outputs) << what;
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                for (std::uint32_t y = 0u; y < kHeight / 2u; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth / 2u; ++x)
                    {
                        ASSERT_EQ(directMaskView.test(channel, x, y), genericMaskView.test(channel, x, y))
                            << what << " at " << channel << " " << x << " " << y;
                    }
                }
            }
        }
    }
}

TEST(GeometryOperationsTest, ResizeMatchesTheReferenceGatherForEveryInterpolationAndPolicy)
{
    // A resize is the reference gather evaluated at the plan's destination-to-source coordinates.
    // Whatever the engine does to run it faster, the bytes are those of the scalar authority,
    // under every interpolation, border and invalid-neighbour policy, and at every thread count.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext narrow = makeExecution(engine, 1u);
    const an::ExecutionContext wide = makeExecution(engine, 4u);
    constexpr std::uint32_t kSourceWidth = 13u;
    constexpr std::uint32_t kSourceHeight = 9u;
    std::vector<float> source = spatial_test::randomPlane(kSourceWidth, kSourceHeight, 0x5eed1u);
    source[static_cast<std::size_t>(3u) * kSourceWidth + 4u] = std::numeric_limits<float>::quiet_NaN();
    source[static_cast<std::size_t>(6u) * kSourceWidth + 10u] = std::numeric_limits<float>::infinity();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, kSourceWidth, kSourceHeight);
    const std::optional<an::PlaneView<const float>> sourcePlane =
        an::PlaneView<const float>::create(std::span<const float>{source}, kSourceWidth, kSourceHeight, 1u,
                                           static_cast<std::uint64_t>(kSourceWidth) * sizeof(float));
    ASSERT_TRUE(sourcePlane.has_value());
    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    // The all-valid rule takes the direct path that sums straight into the destination; the
    // finite-only rule takes the general path through working planes. Both must be the gather.
    const std::array<an::ValidityDescriptor, 2u> kRules{*finiteOnly, an::ValidityDescriptor::allValid()};

    constexpr std::array<an::KernelInterpolation, 6u> kModes{
        an::KernelInterpolation::Nearest, an::KernelInterpolation::Linear,   an::KernelInterpolation::Cubic,
        an::KernelInterpolation::Area,    an::KernelInterpolation::Lanczos4, an::KernelInterpolation::LinearExact};
    constexpr std::array<an::KernelBorderMode, 3u> kBorders{an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant,
                                                            an::KernelBorderMode::Reflect};
    constexpr std::array<an::SamplingInvalidPolicy, 3u> kPolicies{an::SamplingInvalidPolicy::RequireAll,
                                                                  an::SamplingInvalidPolicy::Renormalize,
                                                                  an::SamplingInvalidPolicy::Substitute};
    constexpr std::array<std::array<std::uint32_t, 2u>, 2u> kShapes{std::array<std::uint32_t, 2u>{21u, 16u},
                                                                    std::array<std::uint32_t, 2u>{7u, 5u}};
    const float sentinel = -9.0f;
    std::uint32_t compared = 0u;
    // The ordered position of a Float32 bit pattern, so a subtraction counts representable values.
    const auto ordered = [](float value) {
        const auto bits = std::bit_cast<std::int32_t>(value);
        return bits < 0 ? static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)()) -
                              static_cast<std::int64_t>(bits)
                        : static_cast<std::int64_t>(bits);
    };
    for (const an::ValidityDescriptor& rule : kRules)
    {
        for (const an::KernelInterpolation mode : kModes)
        {
            for (const an::KernelBorderMode border : kBorders)
            {
                for (const an::SamplingInvalidPolicy policy : kPolicies)
                {
                    for (const auto [destinationWidth, destinationHeight] : kShapes)
                    {
                        const an::SamplingKernelParameters sampling{.interpolation = mode,
                                                                    .border = border,
                                                                    .constantBorder = 0.25f,
                                                                    .invalid = policy,
                                                                    .invalidSample = 0.5f};
                        const an::Result<an::GeometryPlan> plan = an::planResize(
                            descriptor(kSourceWidth, kSourceHeight), {.width = destinationWidth,
                                                                      .height = destinationHeight,
                                                                      .sampling = sampling,
                                                                      .ringing = an::ResizeRingingPolicy::Keep});
                        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                        const an::ImageLayout destinationLayout =
                            planarLayout(an::SampleType::Float32, destinationWidth, destinationHeight, 1u, 3u);
                        const std::size_t destinationCount =
                            static_cast<std::size_t>(destinationLayout.totalBytes() / sizeof(float));
                        const an::GeometrySelection selection{.validity = rule, .mask = nullptr};

                        const auto run = [&](const an::ExecutionContext& execution, std::vector<float>& destination,
                                             an::ValidityMask& mask) {
                            destination.assign(destinationCount, sentinel);
                            an::ValidityMaskView<std::uint8_t> maskView = outputMask(mask);
                            const an::Result<an::GeometryResult> transformed = an::transformImageGeometry(
                                execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(),
                                destinationLayout, *plan, selection, maskView);
                            ASSERT_TRUE(transformed.hasValue())
                                << static_cast<unsigned>(mode) << "/" << static_cast<unsigned>(border) << "/"
                                << static_cast<unsigned>(policy) << ": " << transformed.status().message();
                        };
                        std::vector<float> narrowOut;
                        std::vector<float> wideOut;
                        an::Result<an::ValidityMask> narrowMask =
                            an::ValidityMask::create(engine, destinationLayout.extent());
                        an::Result<an::ValidityMask> wideMask =
                            an::ValidityMask::create(engine, destinationLayout.extent());
                        ASSERT_TRUE(narrowMask.hasValue() && wideMask.hasValue());
                        run(narrow, narrowOut, *narrowMask);
                        run(wide, wideOut, *wideMask);
                        EXPECT_EQ(std::memcmp(narrowOut.data(), wideOut.data(), narrowOut.size() * sizeof(float)), 0)
                            << "thread count changed the bytes for " << static_cast<unsigned>(mode) << "/"
                            << static_cast<unsigned>(border) << "/" << static_cast<unsigned>(policy);
                        EXPECT_EQ(an::countValidity(outputMask(*narrowMask).constView()).valid,
                                  an::countValidity(outputMask(*wideMask).constView()).valid);

                        const bool integratesFootprints = mode == an::KernelInterpolation::Area &&
                                                          destinationWidth <= kSourceWidth &&
                                                          destinationHeight <= kSourceHeight;
                        if (integratesFootprints)
                        {
                            // An area reduction integrates source cells rather than reading a point, so
                            // the point gather is not its oracle; its own footprint test covers it.
                            continue;
                        }
                        std::vector<an::KernelCoordinate> coordinates(static_cast<std::size_t>(destinationWidth) *
                                                                      destinationHeight);
                        const std::array<double, 9>& matrix = plan->destinationToSource();
                        for (std::uint32_t y = 0u; y < destinationHeight; ++y)
                        {
                            for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                            {
                                const double denominator = matrix[6] * x + matrix[7] * y + matrix[8];
                                coordinates[static_cast<std::size_t>(y) * destinationWidth + x] = {
                                    .x = (matrix[0] * x + matrix[1] * y + matrix[2]) / denominator,
                                    .y = (matrix[3] * x + matrix[4] * y + matrix[5]) / denominator};
                            }
                        }
                        std::vector<float> expected(coordinates.size(), sentinel);
                        const an::KernelValidityView validity{.descriptor = rule, .mask = nullptr, .maskPlane = 0u};
                        const an::Result<an::KernelRunResult> gathered = an::referenceGatherInterpolatedKernel(
                            narrow, *sourcePlane, coordinates, expected, sampling, validity);
                        ASSERT_TRUE(gathered.hasValue()) << gathered.status().message();
                        const an::ValidityMaskView<std::uint8_t> narrowView = outputMask(*narrowMask);
                        for (std::uint32_t y = 0u; y < destinationHeight; ++y)
                        {
                            const float* const row = narrowOut.data() + static_cast<std::uint64_t>(y) *
                                                                            destinationLayout.rowStrideBytes() /
                                                                            sizeof(float);
                            for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                            {
                                const float want = expected[static_cast<std::size_t>(y) * destinationWidth + x];
                                const bool wantValid =
                                    std::bit_cast<std::uint32_t>(want) != std::bit_cast<std::uint32_t>(sentinel);
                                EXPECT_EQ(narrowView.test(0u, x, y).value_or(false), wantValid)
                                    << static_cast<unsigned>(mode) << "/" << static_cast<unsigned>(border) << "/"
                                    << static_cast<unsigned>(policy) << " at " << x << "," << y;
                                // The all-valid cubic and Lanczos resizes sum separably, which
                                // is the gather's real-valued sum in another association, so they
                                // are held to one unit in the last place; every other case is the
                                // gather's bytes.
                                const bool separableEvaluation = rule.rule() == an::ValidityRule::AllValid &&
                                                                 (mode == an::KernelInterpolation::Cubic ||
                                                                  mode == an::KernelInterpolation::Lanczos4);
                                if (separableEvaluation && std::isfinite(want) && std::isfinite(row[x]))
                                {
                                    const std::int64_t distance = ordered(row[x]) - ordered(want);
                                    EXPECT_LE(distance < 0 ? -distance : distance, 1)
                                        << static_cast<unsigned>(mode) << "/" << static_cast<unsigned>(border) << "/"
                                        << static_cast<unsigned>(policy) << " at " << x << "," << y << ": " << row[x]
                                        << " against " << want;
                                }
                                else
                                {
                                    EXPECT_EQ(std::bit_cast<std::uint32_t>(row[x]), std::bit_cast<std::uint32_t>(want))
                                        << static_cast<unsigned>(mode) << "/" << static_cast<unsigned>(border) << "/"
                                        << static_cast<unsigned>(policy) << " at " << x << "," << y;
                                }
                            }
                        }
                        ++compared;
                    }
                }
            }
        }
    }
    EXPECT_EQ(compared, 2u * (6u * 3u * 3u * 2u - 3u * 3u));
}

TEST(GeometryOperationsTest, AZeroWeightTapNeverCarriesANonFiniteSampleIntoASeparableResize)
{
    // Keeping the width puts every destination column exactly on a source column, where a wide
    // window weighs some of its taps at exactly zero, and it makes each column tap one long run of
    // adjacent source samples - the arrangement the separable column pass walks without resolving
    // each tap.
    // The scalar authority skips a zero-weight tap rather than multiplying it, so a non-finite
    // sample under one changes nothing; the separable pass must reach the same bytes, which it
    // cannot if it multiplies a tap it should skip. The height is reduced so every row still mixes.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 4u);
    constexpr std::uint32_t kWidth = 15u;
    constexpr std::uint32_t kSourceHeight = 15u;
    constexpr std::uint32_t kDestinationHeight = 13u;
    std::vector<float> source = spatial_test::randomPlane(kWidth, kSourceHeight, 0x2e70u);
    source[static_cast<std::size_t>(3u) * kWidth + 5u] = std::numeric_limits<float>::quiet_NaN();
    source[static_cast<std::size_t>(9u) * kWidth + 6u] = std::numeric_limits<float>::infinity();
    const an::ImageLayout sourceLayout = planarLayout(an::SampleType::Float32, kWidth, kSourceHeight);
    const std::optional<an::PlaneView<const float>> sourcePlane = an::PlaneView<const float>::create(
        std::span<const float>{source}, kWidth, kSourceHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(sourcePlane.has_value());
    const an::ValidityDescriptor allValid = an::ValidityDescriptor::allValid();
    // The ordered position of a Float32 bit pattern, so a subtraction counts representable values.
    const auto ordered = [](float value) {
        const auto bits = std::bit_cast<std::int32_t>(value);
        return bits < 0 ? static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)()) -
                              static_cast<std::int64_t>(bits)
                        : static_cast<std::int64_t>(bits);
    };

    std::uint32_t protectedOutputs = 0u;
    for (const an::KernelInterpolation mode : {an::KernelInterpolation::Cubic, an::KernelInterpolation::Lanczos4})
    {
        for (const an::KernelBorderMode border : {an::KernelBorderMode::Clamp, an::KernelBorderMode::Reflect})
        {
            const an::SamplingKernelParameters sampling{.interpolation = mode, .border = border};
            const an::Result<an::GeometryPlan> plan =
                an::planResize(descriptor(kWidth, kSourceHeight), {.width = kWidth,
                                                                   .height = kDestinationHeight,
                                                                   .sampling = sampling,
                                                                   .ringing = an::ResizeRingingPolicy::Keep});
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const std::array<double, 9>& matrix = plan->destinationToSource();
            ASSERT_EQ(matrix[0], 1.0);
            ASSERT_EQ(matrix[2], 0.0);

            const an::ImageLayout destinationLayout = planarLayout(an::SampleType::Float32, kWidth, kDestinationHeight);
            std::vector<float> destination(static_cast<std::size_t>(kWidth) * kDestinationHeight, -9.0f);
            an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
            ASSERT_TRUE(mask.hasValue());
            an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
            const an::Result<an::GeometryResult> transformed = an::transformImageGeometry(
                execution, an::SampleType::Float32, source.data(), sourceLayout, destination.data(), destinationLayout,
                *plan, {.validity = allValid, .mask = nullptr}, maskView);
            ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();

            std::vector<an::KernelCoordinate> coordinates(destination.size());
            for (std::uint32_t y = 0u; y < kDestinationHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    coordinates[static_cast<std::size_t>(y) * kWidth + x] = {.x = matrix[0] * x + matrix[2],
                                                                             .y = matrix[4] * y + matrix[5]};
                }
            }
            std::vector<float> expected(coordinates.size(), -9.0f);
            const an::Result<an::KernelRunResult> gathered =
                an::referenceGatherInterpolatedKernel(execution, *sourcePlane, coordinates, expected, sampling,
                                                      {.descriptor = allValid, .mask = nullptr, .maskPlane = 0u});
            ASSERT_TRUE(gathered.hasValue()) << gathered.status().message();

            for (std::uint32_t y = 0u; y < kDestinationHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                    if (!std::isfinite(expected[index]))
                    {
                        continue;
                    }
                    // A cubic window weighs an integral coordinate's neighbours at exactly zero, so its
                    // columns three, four and seven read a sample under one. A Lanczos window's sine
                    // leaves its inner neighbours a rounding error rather than a zero - the reference
                    // carries those samples too - and only its far tap, four away, weighs exactly
                    // zero: column one's lies over the not-a-number.
                    const bool cubic = mode == an::KernelInterpolation::Cubic;
                    protectedOutputs += (cubic && (x == 3u || x == 4u || x == 7u)) || (!cubic && x == 1u) ? 1u : 0u;
                    ASSERT_TRUE(std::isfinite(destination[index]))
                        << static_cast<unsigned>(mode) << "/" << static_cast<unsigned>(border) << " at " << x << ","
                        << y << " carried a non-finite sample its window weighs at zero";
                    // The separable sum is the gather's in another association: one unit in the last place.
                    const std::int64_t distance = ordered(destination[index]) - ordered(expected[index]);
                    EXPECT_LE(distance < 0 ? -distance : distance, 1)
                        << static_cast<unsigned>(mode) << "/" << static_cast<unsigned>(border) << " at " << x << ","
                        << y << ": " << destination[index] << " against " << expected[index];
                }
            }
        }
    }
    // Every row of those columns is finite in the reference under both borders, which is what makes
    // this a test of the zero-weight rule rather than of its fixture.
    EXPECT_EQ(protectedOutputs, 2u * (3u + 1u) * kDestinationHeight);
}

TEST(GeometryOperationsTest, AnAverageOverASourceWiderThanOneSpanOfOutputsIsTheGenericReducersMeanInEverySpan)
{
    // The direct reduction takes a row's outputs a span at a time - the vertical pass two thousand and forty-eight
    // source columns, the general pass two hundred and fifty-six outputs - so a source of two thousand one hundred
    // columns reduced by two reaches a second span and a fifth chunk, where the offsets of a later span are read, and
    // reduced by seven a second chunk. The means must be the generic reducer's in every span, in every representation,
    // planar or interleaved.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<an::SampleType, 8u> kTypes{
        an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
        an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};
    constexpr std::uint32_t kWidth = 2100u;
    constexpr std::uint32_t kHeight = 9u;
    const an::ValidityDescriptor explicitMask =
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    for (const an::SampleType type : kTypes)
    {
        const an::ImageDescriptor source = colourDescriptor(type, kWidth, kHeight);
        for (const std::uint32_t factor : {2u, 7u})
        {
            const an::Result<an::GeometryPlan> plan =
                an::planIntegerResample(source, {.factor = factor,
                                                 .direction = an::IntegerResampleDirection::Reduce,
                                                 .reduction = an::IntegerReduction::Average,
                                                 .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges});
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const std::uint32_t width = kWidth / factor;
            const std::uint32_t height = kHeight / factor;
            ASSERT_GT(width, 256u) << "the source is wide enough for a second chunk of outputs";
            for (const bool interleaved : {false, true})
            {
                const an::ImageLayout sourceLayout = interleaved
                                                         ? spatial_test::interleavedLayout(type, kWidth, kHeight, 3u)
                                                         : planarLayout(type, kWidth, kHeight, 3u, 3u);
                const an::ImageLayout destinationLayout = interleaved
                                                              ? spatial_test::interleavedLayout(type, width, height, 3u)
                                                              : planarLayout(type, width, height, 3u, 1u);
                std::vector<std::uint8_t> samples(static_cast<std::size_t>(sourceLayout.totalBytes()), 0u);
                fillForReduction(type, samples, 101u + factor);
                const std::string what = std::to_string(static_cast<unsigned>(type)) + " by " + std::to_string(factor) +
                                         (interleaved ? " interleaved" : " planar");

                an::Result<an::ValidityMask> everySample = an::ValidityMask::create(engine, sourceLayout.extent());
                ASSERT_TRUE(everySample.hasValue());
                an::ValidityMaskView<std::uint8_t> everySampleView = outputMask(*everySample);
                everySampleView.fill(true);
                const an::ValidityMaskView<const std::uint8_t> sourceMask = everySampleView.constView();

                std::vector<std::uint8_t> generic(static_cast<std::size_t>(destinationLayout.totalBytes()), 0xEEu);
                std::vector<std::uint8_t> direct(generic.size(), 0xEEu);
                an::Result<an::ValidityMask> genericMask = an::ValidityMask::create(engine, destinationLayout.extent());
                an::Result<an::ValidityMask> directMask = an::ValidityMask::create(engine, destinationLayout.extent());
                ASSERT_TRUE(genericMask.hasValue() && directMask.hasValue());
                an::ValidityMaskView<std::uint8_t> genericMaskView = outputMask(*genericMask);
                an::ValidityMaskView<std::uint8_t> directMaskView = outputMask(*directMask);

                const an::Result<an::GeometryResult> byGeneric = an::transformImageGeometry(
                    execution, type, samples.data(), sourceLayout, generic.data(), destinationLayout, *plan,
                    {.validity = explicitMask, .mask = &sourceMask}, genericMaskView);
                ASSERT_TRUE(byGeneric.hasValue()) << what << ": " << byGeneric.status().message();
                const an::Result<an::GeometryResult> byDirect =
                    an::transformImageGeometry(execution, type, samples.data(), sourceLayout, direct.data(),
                                               destinationLayout, *plan, {}, directMaskView);
                ASSERT_TRUE(byDirect.hasValue()) << what << ": " << byDirect.status().message();

                EXPECT_EQ(direct, generic) << what;
                EXPECT_EQ(byDirect->run.outputsWritten, static_cast<std::uint64_t>(width) * height * 3u) << what;
            }
        }
    }
}

TEST(GeometryOperationsTest, AThirtyTwoBitAverageWhoseBlockSumMightPassTheExactRangeIsTheGenericReducersMean)
{
    // A block's sum of thirty-two-bit codes is exact in an integer up to the factor at which it could pass two to the
    // fifty-three: beyond it the direct reduction sums in Float64, in the generic reducer's order, and must still write
    // the generic reducer's mean.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ValidityDescriptor explicitMask =
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    struct Case
    {
        an::SampleType type;
        std::uint32_t factor;
    };
    for (const Case& reduction : {Case{an::SampleType::UInt32, 1449u}, Case{an::SampleType::Int32, 2048u}})
    {
        const std::uint32_t side = reduction.factor;
        const an::ImageDescriptor source = colourDescriptor(reduction.type, side, side);
        const an::Result<an::GeometryPlan> plan =
            an::planIntegerResample(source, {.factor = reduction.factor,
                                             .direction = an::IntegerResampleDirection::Reduce,
                                             .reduction = an::IntegerReduction::Average,
                                             .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const an::ImageLayout sourceLayout = planarLayout(reduction.type, side, side, 3u, 0u);
        const an::ImageLayout destinationLayout = planarLayout(reduction.type, 1u, 1u, 3u, 0u);
        std::vector<std::uint8_t> samples(static_cast<std::size_t>(sourceLayout.totalBytes()), 0u);
        fillForReduction(reduction.type, samples, 7u + reduction.factor);
        const std::string what =
            std::to_string(static_cast<unsigned>(reduction.type)) + " by " + std::to_string(reduction.factor);

        an::Result<an::ValidityMask> everySample = an::ValidityMask::create(engine, sourceLayout.extent());
        ASSERT_TRUE(everySample.hasValue());
        an::ValidityMaskView<std::uint8_t> everySampleView = outputMask(*everySample);
        everySampleView.fill(true);
        const an::ValidityMaskView<const std::uint8_t> sourceMask = everySampleView.constView();

        std::vector<std::uint8_t> generic(static_cast<std::size_t>(destinationLayout.totalBytes()), 0xEEu);
        std::vector<std::uint8_t> direct(generic.size(), 0xEEu);
        an::Result<an::ValidityMask> genericMask = an::ValidityMask::create(engine, destinationLayout.extent());
        an::Result<an::ValidityMask> directMask = an::ValidityMask::create(engine, destinationLayout.extent());
        ASSERT_TRUE(genericMask.hasValue() && directMask.hasValue());
        an::ValidityMaskView<std::uint8_t> genericMaskView = outputMask(*genericMask);
        an::ValidityMaskView<std::uint8_t> directMaskView = outputMask(*directMask);

        const an::Result<an::GeometryResult> byGeneric = an::transformImageGeometry(
            execution, reduction.type, samples.data(), sourceLayout, generic.data(), destinationLayout, *plan,
            {.validity = explicitMask, .mask = &sourceMask}, genericMaskView);
        ASSERT_TRUE(byGeneric.hasValue()) << what << ": " << byGeneric.status().message();
        const an::Result<an::GeometryResult> byDirect =
            an::transformImageGeometry(execution, reduction.type, samples.data(), sourceLayout, direct.data(),
                                       destinationLayout, *plan, {}, directMaskView);
        ASSERT_TRUE(byDirect.hasValue()) << what << ": " << byDirect.status().message();
        EXPECT_EQ(direct, generic) << what;
        EXPECT_EQ(byDirect->run.outputsWritten, 3u) << what;
    }
}

} // namespace
