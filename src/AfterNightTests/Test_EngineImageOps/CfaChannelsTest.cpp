// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Algorithms/Demosaic/CfaChannels.h.
//
// None of these three operations estimates anything, which is what makes them testable exactly: a
// split is a permutation of the samples, a join is its inverse, and a super-pixel is an average of a
// known set. So every case here is an equality rather than a tolerance, and the two that could
// silently be wrong - which plane a position lands in, and which filter a block averaged - are
// checked against the mosaic itself rather than against a table written out beside the test.

#include <AfterNightEngine/Algorithms/Demosaic/CfaChannels.h>

#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;

/// One real camera table.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// A named Bayer mosaic with a phase.
///
/// @param pattern which arrangement.
/// @param originX the horizontal phase.
/// @param originY the vertical phase.
/// @return the descriptor.
[[nodiscard]] an::CfaDescriptor bayer(an::CfaPattern pattern, std::uint32_t originX = 0u, std::uint32_t originY = 0u)
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, originX, originY);
    EXPECT_TRUE(cfa.has_value());
    return cfa.value_or(an::CfaDescriptor{});
}

/// The tabulated mosaic with no phase.
///
/// @return the descriptor.
[[nodiscard]] an::CfaDescriptor tabulated()
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(kCameraTable, 0u, 0u);
    EXPECT_TRUE(cfa.has_value());
    return cfa.value_or(an::CfaDescriptor{});
}

/// A mosaic whose every sample is distinguishable from every other.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @return the samples.
[[nodiscard]] std::vector<float> countedMosaic(std::uint32_t width, std::uint32_t height)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>(index);
    }
    return samples;
}

TEST(CfaChannelsTest, ABlockCountIsFourForANamedMosaicAndNothingForTheOthers)
{
    for (const an::CfaPattern pattern :
         {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
    {
        EXPECT_EQ(an::cfaBlockPlaneCount(pattern), 4u);
        EXPECT_EQ(an::cfaSuperPixelBlockSize(pattern), 2u);
    }
    EXPECT_EQ(an::cfaBlockPlaneCount(an::CfaPattern::XTrans), 0u);
    EXPECT_EQ(an::cfaSuperPixelBlockSize(an::CfaPattern::XTrans), 3u);
    EXPECT_EQ(an::cfaBlockPlaneCount(an::CfaPattern::None), 0u);
    EXPECT_EQ(an::cfaSuperPixelBlockSize(an::CfaPattern::None), 0u);
    EXPECT_EQ(an::cfaBlockPlaneCount(static_cast<an::CfaPattern>(70)), 0u);
    EXPECT_EQ(an::cfaSuperPixelBlockSize(static_cast<an::CfaPattern>(70)), 0u);
}

TEST(CfaChannelsTest, EveryPositionOfTheBlockLandsInItsOwnPlane)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 12u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Grbg, 1u, 0u);
    const std::vector<float> mosaic = countedMosaic(kWidth, kHeight);
    std::vector<float> planes(static_cast<std::size_t>(kWidth / 2u) * (kHeight / 2u) * 4u, -1.0f);
    const an::Result<an::CfaChannelResult> split = an::splitCfaBlockPlanes(
        execution, an::SampleType::Float32, mosaic.data(), planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u),
        cfa, planes.data(), planarLayout(an::SampleType::Float32, kWidth / 2u, kHeight / 2u, 4u));
    ASSERT_TRUE(split.hasValue()) << split.status().message();
    EXPECT_EQ(split.value().blockSize, 2u);
    EXPECT_EQ(split.value().blockColumns, kWidth / 2u);
    EXPECT_EQ(split.value().blockRows, kHeight / 2u);
    EXPECT_EQ(split.value().trailingColumns, 0u);
    EXPECT_EQ(split.value().trailingRows, 0u);

    const std::size_t planeSamples = static_cast<std::size_t>(kWidth / 2u) * (kHeight / 2u);
    for (std::uint32_t blockRow = 0u; blockRow < kHeight / 2u; ++blockRow)
    {
        for (std::uint32_t blockColumn = 0u; blockColumn < kWidth / 2u; ++blockColumn)
        {
            for (std::uint32_t withinRow = 0u; withinRow < 2u; ++withinRow)
            {
                for (std::uint32_t withinColumn = 0u; withinColumn < 2u; ++withinColumn)
                {
                    const std::uint32_t plane = withinRow * 2u + withinColumn;
                    const std::uint32_t x = blockColumn * 2u + withinColumn;
                    const std::uint32_t y = blockRow * 2u + withinRow;
                    EXPECT_EQ(
                        planes[plane * planeSamples + static_cast<std::size_t>(blockRow) * (kWidth / 2u) + blockColumn],
                        mosaic[static_cast<std::size_t>(y) * kWidth + x])
                        << plane << " at " << x << "," << y;
                }
            }
        }
    }
    // The plane a filter lands in is the mosaic's question, and its answer moves with the phase.
    EXPECT_EQ(cfa.colorAt(1u, 0u), an::CfaColor::Green);
    EXPECT_EQ(cfa.colorAt(0u, 0u), an::CfaColor::Red);
}

TEST(CfaChannelsTest, JoiningTheSplitPlanesRebuildsTheMosaicSampleForSample)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 33u;
    constexpr std::uint32_t kHeight = 21u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    const std::vector<float> mosaic = countedMosaic(kWidth, kHeight);
    const std::uint32_t blockColumns = kWidth / 2u;
    const std::uint32_t blockRows = kHeight / 2u;
    std::vector<float> planes(static_cast<std::size_t>(blockColumns) * blockRows * 4u, -1.0f);
    ASSERT_TRUE(an::splitCfaBlockPlanes(execution, an::SampleType::Float32, mosaic.data(),
                                        planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u), cfa, planes.data(),
                                        planarLayout(an::SampleType::Float32, blockColumns, blockRows, 4u))
                    .hasValue());

    constexpr float kUntouched = -7.0f;
    std::vector<float> rebuilt(mosaic.size(), kUntouched);
    const an::Result<an::CfaChannelResult> joined =
        an::joinCfaBlockPlanes(execution, an::SampleType::Float32, planes.data(),
                               planarLayout(an::SampleType::Float32, blockColumns, blockRows, 4u), cfa, rebuilt.data(),
                               planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u));
    ASSERT_TRUE(joined.hasValue()) << joined.status().message();
    EXPECT_EQ(joined.value().trailingColumns, 1u);
    EXPECT_EQ(joined.value().trailingRows, 1u);

    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            const bool covered = x < blockColumns * 2u && y < blockRows * 2u;
            EXPECT_EQ(rebuilt[index], covered ? mosaic[index] : kUntouched) << x << "," << y;
        }
    }
}

TEST(CfaChannelsTest, ASplitIsRefusedForEveryReasonSeparately)
{
    const an::EngineContext engine = makeEngine(2u);
    const an::ExecutionContext execution = makeExecution(engine, 2u);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    const std::vector<float> mosaic = countedMosaic(kWidth, kHeight);
    std::vector<float> planes(mosaic.size(), 0.0f);
    const an::ImageLayout mosaicLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    const an::ImageLayout planeLayout = planarLayout(an::SampleType::Float32, kWidth / 2u, kHeight / 2u, 4u);

    EXPECT_EQ(an::splitCfaBlockPlanes(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout,
                                      an::CfaDescriptor{}, planes.data(), planeLayout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::splitCfaBlockPlanes(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout, tabulated(),
                                      planes.data(), planeLayout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::splitCfaBlockPlanes(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout,
                                      bayer(an::CfaPattern::Rggb), planes.data(), mosaicLayout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // The destination buffer has to be as large as the layout it is described by, even for a case
    // that is refused: the shared checks test whether the two ranges overlap before they test the
    // geometry, and a range that runs past its own vector makes the answer depend on where the
    // allocator happened to put the two of them.
    const an::ImageLayout tallLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight / 2u, 4u);
    std::vector<float> tallPlanes(static_cast<std::size_t>(kWidth) * (kHeight / 2u) * 4u, 0.0f);
    EXPECT_EQ(an::splitCfaBlockPlanes(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout,
                                      bayer(an::CfaPattern::Rggb), tallPlanes.data(), tallLayout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(CfaChannelsTest, ASuperPixelIsTheAverageOfTheSitesThatMeasuredEachFilter)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    for (const an::CfaDescriptor& cfa : {bayer(an::CfaPattern::Bggr, 1u, 1u), tabulated()})
    {
        const std::uint32_t blockSize = an::cfaSuperPixelBlockSize(cfa.pattern());
        ASSERT_TRUE(an::planCfaSuperPixel(cfa).ok()) << an::cfaPatternName(cfa.pattern());
        const std::uint32_t width = blockSize * 7u + 1u;
        const std::uint32_t height = blockSize * 5u + 2u;
        const std::vector<float> mosaic = countedMosaic(width, height);
        const std::uint32_t blockColumns = width / blockSize;
        const std::uint32_t blockRows = height / blockSize;
        std::vector<float> colour(static_cast<std::size_t>(blockColumns) * blockRows * 3u, -1.0f);
        const an::Result<an::CfaChannelResult> ran = an::superPixelCfa(
            execution, an::SampleType::Float32, mosaic.data(), planarLayout(an::SampleType::Float32, width, height, 1u),
            cfa, colour.data(), planarLayout(an::SampleType::Float32, blockColumns, blockRows, 3u));
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(ran.value().blockSize, blockSize);
        EXPECT_EQ(ran.value().trailingColumns, width % blockSize);
        EXPECT_EQ(ran.value().trailingRows, height % blockSize);

        const std::size_t planeSamples = static_cast<std::size_t>(blockColumns) * blockRows;
        for (std::uint32_t blockRow = 0u; blockRow < blockRows; ++blockRow)
        {
            for (std::uint32_t blockColumn = 0u; blockColumn < blockColumns; ++blockColumn)
            {
                std::array<double, 3> sum{};
                std::array<std::uint32_t, 3> count{};
                for (std::uint32_t withinRow = 0u; withinRow < blockSize; ++withinRow)
                {
                    for (std::uint32_t withinColumn = 0u; withinColumn < blockSize; ++withinColumn)
                    {
                        const std::uint32_t x = blockColumn * blockSize + withinColumn;
                        const std::uint32_t y = blockRow * blockSize + withinRow;
                        const std::size_t colorIndex = static_cast<std::size_t>(cfa.colorAt(x, y));
                        sum[colorIndex] += static_cast<double>(mosaic[static_cast<std::size_t>(y) * width + x]);
                        ++count[colorIndex];
                    }
                }
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    ASSERT_GT(count[channel], 0u);
                    const float expected = static_cast<float>(sum[channel] / static_cast<double>(count[channel]));
                    EXPECT_FLOAT_EQ(colour[channel * planeSamples + static_cast<std::size_t>(blockRow) * blockColumns +
                                           blockColumn],
                                    expected)
                        << an::cfaPatternName(cfa.pattern()) << " block " << blockColumn << "," << blockRow
                        << " channel " << channel;
                }
            }
        }
    }
}

TEST(CfaChannelsTest, ASuperPixelIsRefusedForAMosaicThatHasNoBlockToAverage)
{
    EXPECT_EQ(an::planCfaSuperPixel(an::CfaDescriptor{}).code(), an::ErrorCode::IncompatibleArguments);
    for (const an::CfaPattern pattern :
         {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
    {
        for (std::uint32_t originY = 0u; originY < 2u; ++originY)
        {
            for (std::uint32_t originX = 0u; originX < 2u; ++originX)
            {
                EXPECT_TRUE(an::planCfaSuperPixel(bayer(pattern, originX, originY)).ok())
                    << an::cfaPatternName(pattern) << " at " << originX << "," << originY;
            }
        }
    }
    EXPECT_TRUE(an::planCfaSuperPixel(tabulated()).ok());
}

TEST(CfaChannelsTest, ABlockHoldingAMeaninglessSampleIsNotCountedAsMeaningful)
{
    const an::EngineContext engine = makeEngine(2u);
    const an::ExecutionContext execution = makeExecution(engine, 2u);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    std::vector<float> mosaic = countedMosaic(kWidth, kHeight);
    mosaic[static_cast<std::size_t>(3u) * kWidth + 5u] = 0.0f;
    std::vector<float> colour(static_cast<std::size_t>(kWidth / 2u) * (kHeight / 2u) * 3u, -1.0f);

    std::vector<std::uint8_t> producedBits(an::packedMaskRowBytes(kWidth / 2u) * (kHeight / 2u), 0u);
    const std::optional<an::ImageExtent> producedExtent =
        an::ImageExtent::create(kWidth / 2u, kHeight / 2u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(producedExtent.has_value());
    std::optional<an::ValidityMaskView<std::uint8_t>> produced =
        an::ValidityMaskView<std::uint8_t>::createTight(producedBits, *producedExtent);
    ASSERT_TRUE(produced.has_value());

    an::CfaChannelSelection selection;
    selection.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData}).value();
    selection.produced = &produced.value();
    const an::Result<an::CfaChannelResult> ran = an::superPixelCfa(
        execution, an::SampleType::Float32, mosaic.data(), planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u),
        cfa, colour.data(), planarLayout(an::SampleType::Float32, kWidth / 2u, kHeight / 2u, 3u), {}, selection);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    // Sample zero of the counted mosaic is also zero, so two blocks hold a meaningless sample.
    EXPECT_EQ(ran.value().run.selection.total, 16u);
    EXPECT_EQ(ran.value().run.selection.valid, 14u);
    EXPECT_EQ(ran.value().outputValidity.rule(), an::ValidityRule::ExplicitMask);
    EXPECT_EQ(produced->test(0u, 0u, 0u), std::optional<bool>{false});
    EXPECT_EQ(produced->test(0u, 2u, 1u), std::optional<bool>{false});
    EXPECT_EQ(produced->test(0u, 3u, 3u), std::optional<bool>{true});
}

TEST(CfaChannelsTest, AnExplicitMaskFollowsSamplesThroughSplitAndJoin)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::uint32_t kBlockWidth = kWidth / 2u;
    constexpr std::uint32_t kBlockHeight = kHeight / 2u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    const std::vector<float> mosaic = countedMosaic(kWidth, kHeight);
    constexpr std::uint64_t kCoveredSamples = static_cast<std::uint64_t>(kBlockWidth) * kBlockHeight * 4u;
    std::vector<float> planes(kCoveredSamples, -1.0f);

    const std::optional<an::ImageExtent> mosaicMaskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    const std::optional<an::ImageExtent> planeMaskExtent =
        an::ImageExtent::create(kBlockWidth, kBlockHeight, 4u, an::defaultEngineLimits());
    ASSERT_TRUE(mosaicMaskExtent.has_value() && planeMaskExtent.has_value());
    std::vector<std::uint8_t> sourceBits(an::packedMaskRowBytes(kWidth) * kHeight, 0xFFu);
    std::vector<std::uint8_t> planeBits(an::packedMaskRowBytes(kBlockWidth) * kBlockHeight * 4u, 0u);
    std::optional<an::ValidityMaskView<std::uint8_t>> sourceWritable =
        an::ValidityMaskView<std::uint8_t>::createTight(sourceBits, *mosaicMaskExtent);
    std::optional<an::ValidityMaskView<std::uint8_t>> planeMask =
        an::ValidityMaskView<std::uint8_t>::createTight(planeBits, *planeMaskExtent);
    ASSERT_TRUE(sourceWritable.has_value() && planeMask.has_value());
    ASSERT_TRUE(sourceWritable->setValid(0u, 5u, 3u, false));
    const an::ValidityMaskView<const std::uint8_t> sourceMask = sourceWritable->constView();

    an::CfaChannelSelection splitSelection;
    splitSelection.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}).value();
    splitSelection.mask = &sourceMask;
    splitSelection.produced = &planeMask.value();
    const an::Result<an::CfaChannelResult> split = an::splitCfaBlockPlanes(
        execution, an::SampleType::Float32, mosaic.data(), planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u),
        cfa, planes.data(), planarLayout(an::SampleType::Float32, kBlockWidth, kBlockHeight, 4u), splitSelection);
    ASSERT_TRUE(split.hasValue()) << split.status().message();
    EXPECT_EQ(split.value().outputValidity.rule(), an::ValidityRule::ExplicitMask);
    EXPECT_EQ(split.value().run.selection.valid, kCoveredSamples - 1u);
    EXPECT_EQ(planeMask->test(3u, 2u, 1u), std::optional<bool>{false});
    EXPECT_EQ(planeMask->test(0u, 2u, 1u), std::optional<bool>{true});

    std::vector<std::uint8_t> joinedBits(an::packedMaskRowBytes(kWidth) * kHeight, 0xFFu);
    std::optional<an::ValidityMaskView<std::uint8_t>> joinedMask =
        an::ValidityMaskView<std::uint8_t>::createTight(joinedBits, *mosaicMaskExtent);
    ASSERT_TRUE(joinedMask.has_value());
    const an::ValidityMaskView<const std::uint8_t> splitMask = planeMask->constView();
    an::CfaChannelSelection joinSelection;
    joinSelection.validity = splitSelection.validity;
    joinSelection.mask = &splitMask;
    joinSelection.produced = &joinedMask.value();
    std::vector<float> rebuilt(mosaic.size(), -1.0f);
    const an::Result<an::CfaChannelResult> joined = an::joinCfaBlockPlanes(
        execution, an::SampleType::Float32, planes.data(),
        planarLayout(an::SampleType::Float32, kBlockWidth, kBlockHeight, 4u), cfa, rebuilt.data(),
        planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u), joinSelection);
    ASSERT_TRUE(joined.hasValue()) << joined.status().message();
    EXPECT_EQ(joined.value().run.selection.valid, kCoveredSamples - 1u);
    EXPECT_EQ(joinedMask->test(0u, 5u, 3u), std::optional<bool>{false});
    EXPECT_EQ(joinedMask->test(0u, 4u, 3u), std::optional<bool>{true});
    EXPECT_EQ(joinedMask->test(0u, 8u, 3u), std::optional<bool>{false});
    EXPECT_EQ(joinedMask->test(0u, 3u, 8u), std::optional<bool>{false});
}

} // namespace
