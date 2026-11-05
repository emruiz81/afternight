// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for defective-pixel detection and repair.
//
// The suite is written around sentinels a detector should not have: a disabled hot threshold spelled
// as the number 65536 compared against normalized samples, a disabled sigma of negative one, and
// scans that walk a virtual per-sample accessor twice. Here an absent option is absent - the first
// cases prove that an unstated polarity selects nothing and refuses nothing - and the detection reads
// the resident samples once, whatever the representation. The correction cases pin the replacement
// definition: the exact median of the surviving neighbours, an even count averaging its two middles,
// read from the source as it stands.

#include <AfterNightEngine/Algorithms/CosmeticCorrection/CosmeticCorrection.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// A packed deviant map's backing bytes, sized for one image.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param channels how many bit-planes.
/// @param fill what every byte starts as, so a test can prove the operation overwrote it.
/// @return the bytes.
[[nodiscard]] std::vector<std::uint8_t> mapBytes(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                                 std::uint8_t fill)
{
    return std::vector<std::uint8_t>(static_cast<std::size_t>(an::packedMaskRowBytes(width)) * height * channels, fill);
}

/// A tight writable map view over caller bytes.
///
/// @param bytes the backing bytes.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param channels how many bit-planes.
/// @return the view.
[[nodiscard]] an::ValidityMaskView<std::uint8_t> mapView(std::vector<std::uint8_t>& bytes, std::uint32_t width,
                                                         std::uint32_t height, std::uint32_t channels = 1u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bytes}, *extent);
    EXPECT_TRUE(view.has_value());
    return *view;
}

/// Whether one map bit is set.
///
/// @param view the map.
/// @param channel which bit-plane.
/// @param x the column.
/// @param y the row.
/// @return true when the bit is set.
[[nodiscard]] bool mapBit(const an::ValidityMaskView<std::uint8_t>& view, std::uint32_t channel, std::uint32_t x,
                          std::uint32_t y)
{
    const std::uint8_t byte = view.data()[static_cast<std::uint64_t>(channel) * view.planeStrideBytes() +
                                          static_cast<std::uint64_t>(y) * view.rowStrideBytes() + x / 8u];
    return (byte & (1u << (x % 8u))) != 0u;
}

/// The exact statistics a detection derives, computed independently.
struct PlaneOracle
{
    /// The exact median, linearly interpolated between the two middle ranks of an even count.
    double median = 0.0;

    /// The population standard deviation.
    double deviation = 0.0;
};

/// Computes the oracle statistics of one plane.
///
/// @param samples the plane's samples.
/// @return the median and deviation.
[[nodiscard]] PlaneOracle oracleStatistics(std::span<const float> samples)
{
    std::vector<double> sorted{samples.begin(), samples.end()};
    std::sort(sorted.begin(), sorted.end());
    const std::size_t count = sorted.size();
    PlaneOracle oracle{};
    oracle.median = count % 2u == 1u ? sorted[count / 2u] : (sorted[count / 2u - 1u] + sorted[count / 2u]) * 0.5;
    double sum = 0.0;
    for (const double value : sorted)
    {
        sum += value;
    }
    const double mean = sum / static_cast<double>(count);
    double squares = 0.0;
    for (const double value : sorted)
    {
        squares += (value - mean) * (value - mean);
    }
    oracle.deviation = std::sqrt(squares / static_cast<double>(count));
    return oracle;
}

/// The replacement: the exact median, an even count averaging its two middles.
///
/// @param values the window's surviving samples, in any order.
/// @return the median, evaluated in Float64 over the sorted order and narrowed once.
[[nodiscard]] float windowMedian(std::vector<float> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t upper = values.size() / 2u;
    if (values.size() % 2u == 1u)
    {
        return values[upper];
    }
    return static_cast<float>((static_cast<double>(values[upper - 1u]) + static_cast<double>(values[upper])) / 2.0);
}

TEST(CosmeticCorrectionTest, AScanDerivesEachThresholdFromTheChannelsOwnExactStatistics)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 31u;
    constexpr std::uint32_t kHeight = 17u; // An odd sample count, so the exact median is one sample.
    std::vector<float> source = randomPlane(kWidth, kHeight, 11u);
    source[57] = 9.0f; // One implanted hot sample, far above every threshold a unit plane derives.
    source[203] = -9.0f; // One implanted cold sample, far below.
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const PlaneOracle oracle = oracleStatistics(source);

    std::array<an::CosmeticChannelThresholds, 1> thresholds{};
    const an::Result<an::CosmeticScanResult> scanned = an::scanImageDeviantPixels(
        execution, an::SampleType::Float32, source.data(), layout,
        an::CosmeticDetectionOptions{.coldSigma = 4.0, .hotSigma = 5.0}, std::span{thresholds});
    ASSERT_TRUE(scanned.hasValue()) << scanned.status().message();

    EXPECT_NEAR(thresholds[0].median, oracle.median, 1.0e-12);
    EXPECT_NEAR(thresholds[0].deviation, oracle.deviation, 1.0e-9);
    ASSERT_TRUE(thresholds[0].coldThreshold.has_value());
    ASSERT_TRUE(thresholds[0].hotThreshold.has_value());
    EXPECT_FLOAT_EQ(*thresholds[0].coldThreshold, static_cast<float>(oracle.median - 4.0 * oracle.deviation));
    EXPECT_FLOAT_EQ(*thresholds[0].hotThreshold, static_cast<float>(oracle.median + 5.0 * oracle.deviation));
    EXPECT_EQ(scanned->coldCount, 1u);
    EXPECT_EQ(scanned->hotCount, 1u);
    EXPECT_EQ(thresholds[0].coldCount, 1u);
    EXPECT_EQ(thresholds[0].hotCount, 1u);
    EXPECT_EQ(scanned->channelCount, 1u);
    EXPECT_EQ(scanned->run.outputsWritten, 0u) << "the counting scan writes nothing";
    EXPECT_EQ(scanned->run.selection.total, static_cast<std::uint64_t>(kWidth) * kHeight);
    EXPECT_EQ(scanned->precision, an::ComputePrecision::Float32);
    EXPECT_GE(scanned->plannedWorkers, 1u);
}

TEST(CosmeticCorrectionTest, AnAbsentPolarityHasNoThresholdAndSelectsNothing)
{
    // "No hot detection" is not the threshold 65536 and "no cold detection" is not a sigma of
    // negative one: the option is absent, the reported threshold is absent, and the most extreme
    // sample in the frame is not selected by the polarity nobody asked for.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 15u;
    std::vector<float> source = randomPlane(kWidth, kHeight, 3u);
    source[100] = 1000.0f;
    source[101] = -1000.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::array<an::CosmeticChannelThresholds, 1> thresholds{};
    const an::Result<an::CosmeticScanResult> coldOnly =
        an::scanImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                   an::CosmeticDetectionOptions{.coldSigma = 6.0}, std::span{thresholds});
    ASSERT_TRUE(coldOnly.hasValue()) << coldOnly.status().message();
    EXPECT_EQ(coldOnly->hotCount, 0u);
    EXPECT_EQ(coldOnly->coldCount, 1u);
    EXPECT_FALSE(thresholds[0].hotThreshold.has_value());
    EXPECT_TRUE(thresholds[0].coldThreshold.has_value());

    const an::Result<an::CosmeticScanResult> hotOnly =
        an::scanImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                   an::CosmeticDetectionOptions{.hotSigma = 6.0}, std::span{thresholds});
    ASSERT_TRUE(hotOnly.hasValue()) << hotOnly.status().message();
    EXPECT_EQ(hotOnly->coldCount, 0u);
    EXPECT_EQ(hotOnly->hotCount, 1u);
    EXPECT_FALSE(thresholds[0].coldThreshold.has_value());
    EXPECT_TRUE(thresholds[0].hotThreshold.has_value());
}

TEST(CosmeticCorrectionTest, ADetectionStatesAtLeastOnePolarityAndFiniteNonNegativeSigmas)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(64u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 8u, 8u);

    EXPECT_EQ(an::scanImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                         an::CosmeticDetectionOptions{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::scanImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                         an::CosmeticDetectionOptions{.hotSigma = -1.0})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument)
        << "a disable sentinel is an argument mistake here, not a mode";
    EXPECT_EQ(
        an::scanImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                   an::CosmeticDetectionOptions{.coldSigma = std::numeric_limits<double>::quiet_NaN()})
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
}

TEST(CosmeticCorrectionTest, AMapIsFullyWrittenAndItsRowPaddingStaysZero)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 13u; // Not a multiple of eight, so every row has padding bits.
    constexpr std::uint32_t kHeight = 9u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);
    const auto at = [&](std::uint32_t x, std::uint32_t y) -> float& {
        return source[static_cast<std::size_t>(y) * kWidth + x];
    };
    at(4u, 2u) = 5.0f;
    at(12u, 8u) = 5.0f; // The last pixel of the last row, whose bit shares a byte with padding.
    at(0u, 5u) = -5.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    // Every byte starts set, so surviving stale bits would be visible.
    std::vector<std::uint8_t> coldBytes = mapBytes(kWidth, kHeight, 1u, 0xFFu);
    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0xFFu);
    const an::ValidityMaskView<std::uint8_t> cold = mapView(coldBytes, kWidth, kHeight);
    const an::ValidityMaskView<std::uint8_t> hot = mapView(hotBytes, kWidth, kHeight);

    const an::Result<an::CosmeticScanResult> mapped =
        an::mapImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                  an::CosmeticDetectionOptions{.coldSigma = 3.0, .hotSigma = 3.0}, &cold, &hot);
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
    EXPECT_EQ(mapped->hotCount, 2u);
    EXPECT_EQ(mapped->coldCount, 1u);
    EXPECT_EQ(mapped->run.outputsWritten, 3u);

    std::uint64_t hotBits = 0u;
    std::uint64_t coldBits = 0u;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            hotBits += mapBit(hot, 0u, x, y) ? 1u : 0u;
            coldBits += mapBit(cold, 0u, x, y) ? 1u : 0u;
        }
    }
    EXPECT_EQ(hotBits, 2u);
    EXPECT_EQ(coldBits, 1u);
    EXPECT_TRUE(mapBit(hot, 0u, 4u, 2u));
    EXPECT_TRUE(mapBit(hot, 0u, 12u, 8u));
    EXPECT_TRUE(mapBit(cold, 0u, 0u, 5u));

    // The bits past the last pixel of every row were forced to zero, stale as they were.
    const std::uint64_t rowBytes = an::packedMaskRowBytes(kWidth);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const std::uint8_t lastHot = hotBytes[static_cast<std::size_t>(y) * rowBytes + rowBytes - 1u];
        const std::uint8_t lastCold = coldBytes[static_cast<std::size_t>(y) * rowBytes + rowBytes - 1u];
        EXPECT_EQ(lastHot & ~an::packedMaskLastByteMask(kWidth), 0u) << "row " << y;
        EXPECT_EQ(lastCold & ~an::packedMaskLastByteMask(kWidth), 0u) << "row " << y;
    }
}

TEST(CosmeticCorrectionTest, AMapMustMatchItsPolarityAndItsImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(64u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 8u, 8u);
    std::vector<std::uint8_t> bytes = mapBytes(8u, 8u, 1u, 0u);
    const an::ValidityMaskView<std::uint8_t> map = mapView(bytes, 8u, 8u);

    // A map for a polarity nobody enabled, and an enabled polarity with no map.
    EXPECT_EQ(an::mapImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                        an::CosmeticDetectionOptions{.hotSigma = 3.0}, &map, &map)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::mapImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                        an::CosmeticDetectionOptions{.coldSigma = 3.0, .hotSigma = 3.0}, nullptr, &map)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // A detection that records nothing belongs to the counting entry point.
    EXPECT_EQ(an::mapImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                        an::CosmeticDetectionOptions{.hotSigma = 3.0}, nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // A map of another image's shape.
    std::vector<std::uint8_t> wrongBytes = mapBytes(9u, 8u, 1u, 0u);
    const an::ValidityMaskView<std::uint8_t> wrong = mapView(wrongBytes, 9u, 8u);
    EXPECT_EQ(an::mapImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                        an::CosmeticDetectionOptions{.hotSigma = 3.0}, nullptr, &wrong)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(CosmeticCorrectionTest, AHotReplacementIsTheMedianOfItsSurvivingNeighbours)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(index) * 0.01f;
    }
    const std::uint32_t hotX = 3u;
    const std::uint32_t hotY = 3u;
    source[hotY * kWidth + hotX] = 100.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotWritable = mapView(hotBytes, kWidth, kHeight);
    hotWritable.data()[hotY * an::packedMaskRowBytes(kWidth) + hotX / 8u] =
        static_cast<std::uint8_t>(1u << (hotX % 8u));
    const an::ValidityMaskView<const std::uint8_t> hot{hotWritable};

    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::CosmeticCorrectionResult> corrected =
        an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                      layout, nullptr, &hot, an::CosmeticCorrectionRequest{});
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_EQ(corrected->hotReplaced, 1u);
    EXPECT_EQ(corrected->coldReplaced, 0u);
    EXPECT_EQ(corrected->unresolved, 0u);
    EXPECT_EQ(corrected->hotWindowSamples, 8u);
    EXPECT_EQ(corrected->coldWindowSamples, 24u);

    // The eight neighbours, the centre excluded: an even count, so the median is the mean of the
    // fourth and fifth smallest.
    std::vector<float> window;
    for (std::uint32_t y = hotY - 1u; y <= hotY + 1u; ++y)
    {
        for (std::uint32_t x = hotX - 1u; x <= hotX + 1u; ++x)
        {
            if (x != hotX || y != hotY)
            {
                window.push_back(source[y * kWidth + x]);
            }
        }
    }
    EXPECT_FLOAT_EQ(destination[hotY * kWidth + hotX], windowMedian(window));

    // Every other sample was carried across as the number it is.
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        if (index != hotY * kWidth + hotX)
        {
            EXPECT_FLOAT_EQ(destination[index], source[index]) << "at " << index;
        }
    }
    EXPECT_EQ(corrected->filter.run.outputsWritten, source.size());
}

TEST(CosmeticCorrectionTest, AdjacentDefectsRepairFromTheSourceAndNotFromEachOthersRepairs)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 9u;
    std::vector<float> source = randomPlane(kWidth, kHeight, 21u);
    const std::uint32_t firstX = 4u;
    const std::uint32_t secondX = 5u;
    const std::uint32_t y = 4u;
    source[y * kWidth + firstX] = 50.0f;
    source[y * kWidth + secondX] = 60.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotWritable = mapView(hotBytes, kWidth, kHeight);
    hotWritable.data()[y * an::packedMaskRowBytes(kWidth) + 0u] =
        static_cast<std::uint8_t>((1u << firstX) | (1u << secondX));
    const an::ValidityMaskView<const std::uint8_t> hot{hotWritable};

    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::CosmeticCorrectionResult> corrected =
        an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                      layout, nullptr, &hot, an::CosmeticCorrectionRequest{});
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_EQ(corrected->hotReplaced, 2u);

    // Each defect's window holds the other's original sample - 60 in the first window, 50 in the
    // second - not the other's repair: every window reads the source as it stood before any repair.
    const auto windowOf = [&](std::uint32_t centreX) {
        std::vector<float> window;
        for (std::uint32_t wy = y - 1u; wy <= y + 1u; ++wy)
        {
            for (std::uint32_t wx = centreX - 1u; wx <= centreX + 1u; ++wx)
            {
                if (wx != centreX || wy != y)
                {
                    window.push_back(source[wy * kWidth + wx]);
                }
            }
        }
        return window;
    };
    EXPECT_FLOAT_EQ(destination[y * kWidth + firstX], windowMedian(windowOf(firstX)));
    EXPECT_FLOAT_EQ(destination[y * kWidth + secondX], windowMedian(windowOf(secondX)));
}

TEST(CosmeticCorrectionTest, ACornerWindowIsTruncatedRatherThanInvented)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    source[0] = 100.0f; // The corner.
    source[1] = 0.3f;
    source[kWidth] = 0.1f;
    source[kWidth + 1u] = 0.2f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotWritable = mapView(hotBytes, kWidth, kHeight);
    hotWritable.data()[0] = 0x01u;
    const an::ValidityMaskView<const std::uint8_t> hot{hotWritable};

    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::CosmeticCorrectionResult> corrected =
        an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                      layout, nullptr, &hot, an::CosmeticCorrectionRequest{});
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    // Three surviving neighbours - 0.3, 0.1, 0.2 - whose middle is 0.2.
    EXPECT_FLOAT_EQ(destination[0], 0.2f);
}

TEST(CosmeticCorrectionTest, SameCfaFilterSpacingRanksOnlyTheCentresOwnFilter)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 12u;
    // A mosaic-shaped plane: sites sharing the defect's parity hold 0.25, every other site 0.9, so
    // a window that mixed filters would answer a number between them.
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    const std::uint32_t hotX = 6u;
    const std::uint32_t hotY = 6u;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const bool sameFilter = (x % 2u) == (hotX % 2u) && (y % 2u) == (hotY % 2u);
            source[static_cast<std::size_t>(y) * kWidth + x] = sameFilter ? 0.25f : 0.9f;
        }
    }
    source[static_cast<std::size_t>(hotY) * kWidth + hotX] = 100.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotWritable = mapView(hotBytes, kWidth, kHeight);
    hotWritable.data()[hotY * an::packedMaskRowBytes(kWidth) + hotX / 8u] =
        static_cast<std::uint8_t>(1u << (hotX % 8u));
    const an::ValidityMaskView<const std::uint8_t> hot{hotWritable};

    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::CosmeticCorrectionResult> corrected = an::correctImageDeviantPixels(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, nullptr, &hot,
        an::CosmeticCorrectionRequest{.spacing = an::CosmeticSiteSpacing::SameCfaFilter});
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_FLOAT_EQ(destination[static_cast<std::size_t>(hotY) * kWidth + hotX], 0.25f)
        << "a repair that read another filter's sites would have answered above 0.25";
    EXPECT_EQ(corrected->filter.horizontalRadius, 2u) << "one site of reach, doubled by the spacing";
}

TEST(CosmeticCorrectionTest, AValidityRejectedCentreKeepsItsDestinationAndRejectedNeighboursLeaveTheWindow)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);
    const std::uint32_t hotX = 3u;
    const std::uint32_t hotY = 3u;
    source[hotY * kWidth + hotX] = 100.0f;
    source[hotY * kWidth + hotX + 1u] = 0.0f; // A neighbour the zero-is-no-data rule rejects.
    source[2u * kWidth + 2u] = 0.0f; // A marked centre the rule rejects.
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotWritable = mapView(hotBytes, kWidth, kHeight);
    hotWritable.data()[hotY * an::packedMaskRowBytes(kWidth) + hotX / 8u] =
        static_cast<std::uint8_t>(1u << (hotX % 8u));
    hotWritable.data()[2u * an::packedMaskRowBytes(kWidth) + 0u] |= static_cast<std::uint8_t>(1u << 2u);
    const an::ValidityMaskView<const std::uint8_t> hot{hotWritable};

    const std::optional<an::ValidityDescriptor> zeroNoData =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroNoData.has_value());
    an::SpatialSelection selection{};
    selection.validity = *zeroNoData;

    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::CosmeticCorrectionResult> corrected =
        an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                      layout, nullptr, &hot, an::CosmeticCorrectionRequest{}, selection);
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();

    // The rejected marked centre kept the destination's own sample, untouched.
    EXPECT_FLOAT_EQ(destination[2u * kWidth + 2u], -1.0f);
    // The accepted marked centre ranked seven survivors - all 0.5 - rather than eight with a zero.
    EXPECT_FLOAT_EQ(destination[hotY * kWidth + hotX], 0.5f);
    EXPECT_EQ(corrected->hotReplaced, 1u);
    // The rejected unmarked neighbour also kept its destination sample.
    EXPECT_FLOAT_EQ(destination[hotY * kWidth + hotX + 1u], -1.0f);
    EXPECT_EQ(corrected->filter.run.outputsWritten, source.size() - 2u)
        << "the two rejected centres were left untouched";
}

TEST(CosmeticCorrectionTest, AWindowThatKeepsNothingLeavesTheSourceValueAndIsCounted)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    source[0] = 7.0f; // The marked corner; every neighbour is an exact zero the rule rejects.
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotWritable = mapView(hotBytes, kWidth, kHeight);
    hotWritable.data()[0] = 0x01u;
    const an::ValidityMaskView<const std::uint8_t> hot{hotWritable};

    const std::optional<an::ValidityDescriptor> zeroNoData =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroNoData.has_value());
    an::SpatialSelection selection{};
    selection.validity = *zeroNoData;

    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::CosmeticCorrectionResult> corrected =
        an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                      layout, nullptr, &hot, an::CosmeticCorrectionRequest{}, selection);
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_EQ(corrected->unresolved, 1u);
    EXPECT_EQ(corrected->hotReplaced, 0u);
    EXPECT_FLOAT_EQ(destination[0], 7.0f) << "an unresolvable defect carries its source value across";
}

TEST(CosmeticCorrectionTest, DetectionAndRepairMeanTheSameThingInCodeValuesAsInFloat)
{
    // The domain's units regression, in the direction the family states: a UInt16 dark and its
    // exact Float32 rendering must find the same defects and write the same repairs.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 24u;
    constexpr std::uint32_t kHeight = 21u;
    const std::size_t count = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<std::uint16_t> narrow(count, 0u);
    std::vector<float> wide(count, 0.0f);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const auto code = static_cast<std::uint16_t>(800u + (index * 397u) % 300u);
        narrow[index] = code;
        wide[index] = static_cast<float>(code);
    }
    // Both defects sit far past four deviations yet close enough that they do not inflate the
    // deviation into swallowing themselves - the same balance a real threshold choice has to make.
    narrow[100] = 2000u;
    wide[100] = 2000.0f;
    narrow[200] = 0u;
    wide[200] = 0.0f;
    const an::ImageLayout narrowLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    const an::ImageLayout wideLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::CosmeticDetectionOptions options{.coldSigma = 4.0, .hotSigma = 4.0};
    std::vector<std::uint8_t> narrowCold = mapBytes(kWidth, kHeight, 1u, 0u);
    std::vector<std::uint8_t> narrowHot = mapBytes(kWidth, kHeight, 1u, 0u);
    std::vector<std::uint8_t> wideCold = mapBytes(kWidth, kHeight, 1u, 0u);
    std::vector<std::uint8_t> wideHot = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> narrowColdView = mapView(narrowCold, kWidth, kHeight);
    an::ValidityMaskView<std::uint8_t> narrowHotView = mapView(narrowHot, kWidth, kHeight);
    an::ValidityMaskView<std::uint8_t> wideColdView = mapView(wideCold, kWidth, kHeight);
    an::ValidityMaskView<std::uint8_t> wideHotView = mapView(wideHot, kWidth, kHeight);

    const an::Result<an::CosmeticScanResult> narrowMapped = an::mapImageDeviantPixels(
        execution, an::SampleType::UInt16, narrow.data(), narrowLayout, options, &narrowColdView, &narrowHotView);
    ASSERT_TRUE(narrowMapped.hasValue()) << narrowMapped.status().message();
    const an::Result<an::CosmeticScanResult> wideMapped = an::mapImageDeviantPixels(
        execution, an::SampleType::Float32, wide.data(), wideLayout, options, &wideColdView, &wideHotView);
    ASSERT_TRUE(wideMapped.hasValue()) << wideMapped.status().message();
    EXPECT_EQ(narrowMapped->hotCount, wideMapped->hotCount);
    EXPECT_EQ(narrowMapped->coldCount, wideMapped->coldCount);
    EXPECT_EQ(narrowMapped->hotCount, 1u);
    EXPECT_EQ(narrowMapped->coldCount, 1u);
    EXPECT_EQ(narrowCold, wideCold);
    EXPECT_EQ(narrowHot, wideHot);

    const an::ValidityMaskView<const std::uint8_t> coldRead{narrowColdView};
    const an::ValidityMaskView<const std::uint8_t> hotRead{narrowHotView};
    std::vector<std::uint16_t> narrowOut(count, 0u);
    std::vector<float> wideOut(count, 0.0f);
    ASSERT_TRUE(an::correctImageDeviantPixels(execution, an::SampleType::UInt16, narrow.data(), narrowLayout,
                                              narrowOut.data(), narrowLayout, &coldRead, &hotRead,
                                              an::CosmeticCorrectionRequest{})
                    .hasValue());
    ASSERT_TRUE(an::correctImageDeviantPixels(execution, an::SampleType::Float32, wide.data(), wideLayout,
                                              wideOut.data(), wideLayout, &coldRead, &hotRead,
                                              an::CosmeticCorrectionRequest{})
                    .hasValue());
    for (std::size_t index = 0u; index < count; ++index)
    {
        // The repaired value is the same number on both sides; the integer side then stores it
        // under the family's write-back policy, which rounds to nearest even - visible exactly at
        // an even window's half-code median.
        const auto stored = static_cast<std::uint16_t>(std::nearbyint(wideOut[index]));
        EXPECT_EQ(narrowOut[index], stored) << "at " << index;
    }
}

TEST(CosmeticCorrectionTest, ACorrectionStatesItsWindowsWithinTheFamilysLimits)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(64u, 0.5f);
    std::vector<float> destination(64u, 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 8u, 8u);
    std::vector<std::uint8_t> bytes = mapBytes(8u, 8u, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> writable = mapView(bytes, 8u, 8u);
    const an::ValidityMaskView<const std::uint8_t> map{writable};

    // No map at all.
    EXPECT_EQ(an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                            destination.data(), layout, nullptr, nullptr,
                                            an::CosmeticCorrectionRequest{})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // A radius of zero.
    EXPECT_EQ(an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                            destination.data(), layout, nullptr, &map,
                                            an::CosmeticCorrectionRequest{.hotRadius = 0u})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    // A window above the order family's sample ceiling: radius sixteen holds 1,088 neighbours.
    EXPECT_EQ(an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                            destination.data(), layout, nullptr, &map,
                                            an::CosmeticCorrectionRequest{.coldRadius = 16u})
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
    // A reach past the stencil ceiling once the mosaic spacing doubles it: sixty-five sites at a
    // two-site step reach 130 pixels, past the family's 128.
    EXPECT_EQ(an::correctImageDeviantPixels(
                  execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, nullptr, &map,
                  an::CosmeticCorrectionRequest{
                      .hotRadius = 65u, .coldRadius = 1u, .spacing = an::CosmeticSiteSpacing::SameCfaFilter})
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
    // An aliasing destination.
    std::vector<float> shared = source;
    EXPECT_EQ(an::correctImageDeviantPixels(execution, an::SampleType::Float32, shared.data(), layout, shared.data(),
                                            layout, nullptr, &map, an::CosmeticCorrectionRequest{})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // An unknown spacing.
    EXPECT_EQ(an::correctImageDeviantPixels(
                  execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, nullptr, &map,
                  an::CosmeticCorrectionRequest{.spacing = static_cast<an::CosmeticSiteSpacing>(9)})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(CosmeticCorrectionTest, TwoChannelsDeriveTwoThresholdSetsAndTheirOwnMapPlanes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 11u;
    const std::size_t plane = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<float> source(plane * 2u, 0.0f);
    for (std::size_t index = 0u; index < plane; ++index)
    {
        source[index] = 0.2f + static_cast<float>(index % 7u) * 0.001f;
        source[plane + index] = 0.8f + static_cast<float>(index % 5u) * 0.001f;
    }
    source[40] = 3.0f; // Hot in the first channel alone.
    source[plane + 90u] = 4.0f; // Hot in the second channel alone.
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 2u);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 2u, 0xFFu);
    an::ValidityMaskView<std::uint8_t> hotView = mapView(hotBytes, kWidth, kHeight, 2u);
    std::array<an::CosmeticChannelThresholds, 2> thresholds{};
    const an::Result<an::CosmeticScanResult> mapped = an::mapImageDeviantPixels(
        execution, an::SampleType::Float32, source.data(), layout, an::CosmeticDetectionOptions{.hotSigma = 5.0},
        nullptr, &hotView, std::span{thresholds});
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
    EXPECT_EQ(mapped->channelCount, 2u);
    EXPECT_EQ(mapped->hotCount, 2u);
    EXPECT_EQ(thresholds[0].hotCount, 1u);
    EXPECT_EQ(thresholds[1].hotCount, 1u);
    EXPECT_LT(*thresholds[0].hotThreshold, *thresholds[1].hotThreshold)
        << "each channel's threshold came from its own statistics";
    EXPECT_TRUE(mapBit(hotView, 0u, 40u % kWidth, 40u / kWidth));
    EXPECT_TRUE(mapBit(hotView, 1u, 90u % kWidth, 90u / kWidth));
    EXPECT_FALSE(mapBit(hotView, 1u, 40u % kWidth, 40u / kWidth));
}

TEST(CosmeticCorrectionTest, AnEmptySelectionAndANonFiniteSampleAreRefusedByName)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    source[10] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(an::scanImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                         an::CosmeticDetectionOptions{.hotSigma = 3.0})
                  .status()
                  .code(),
              an::ErrorCode::InvalidData)
        << "a sample that is not a number has no place in a median or a deviation";

    // Under a rule that rejects it, the same frame scans; and a frame whose every sample is
    // rejected is an empty selection rather than an answer about nothing.
    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    an::SpatialSelection selection{};
    selection.validity = *finiteOnly;
    EXPECT_TRUE(an::scanImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout,
                                           an::CosmeticDetectionOptions{.hotSigma = 3.0}, {}, selection)
                    .hasValue());

    std::vector<float> nothing(static_cast<std::size_t>(kWidth) * kHeight, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(an::scanImageDeviantPixels(execution, an::SampleType::Float32, nothing.data(), layout,
                                         an::CosmeticDetectionOptions{.hotSigma = 3.0}, {}, selection)
                  .status()
                  .code(),
              an::ErrorCode::EmptySelection);
}

TEST(CosmeticCorrectionTest, StrayPaddingBitsInACallersMapAreDroppedRatherThanRead)
{
    // A map's row padding is contracted to stay zero, but a correction handed a broken one must
    // drop those bits rather than turn them into reads past the row.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 13u; // Three padding bits share the row's last byte.
    constexpr std::uint32_t kHeight = 6u;
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotWritable = mapView(hotBytes, kWidth, kHeight);
    const std::uint64_t rowBytes = an::packedMaskRowBytes(kWidth);
    hotWritable.data()[2u * rowBytes + rowBytes - 1u] = 0xFFu; // Real bits 8..12 plus the padding.
    const an::ValidityMaskView<const std::uint8_t> hot{hotWritable};

    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::CosmeticCorrectionResult> corrected =
        an::correctImageDeviantPixels(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                      layout, nullptr, &hot, an::CosmeticCorrectionRequest{});
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_EQ(corrected->hotReplaced, 5u) << "the five real bits repaired; the three padding bits did not";
    for (std::size_t index = 0u; index < destination.size(); ++index)
    {
        EXPECT_FLOAT_EQ(destination[index], 0.5f) << "at " << index;
    }
}

TEST(CosmeticCorrectionTest, TheTypedSpellingsForwardToTheUntypedEntryPoints)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 10u;
    std::vector<float> source = randomPlane(kWidth, kHeight, 5u);
    source[30] = 40.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::optional<an::ImageView<const float>> sourceView =
        an::ImageView<const float>::create(std::span<const float>{source.data(), source.size()}, layout);
    ASSERT_TRUE(sourceView.has_value());

    const an::Result<an::CosmeticScanResult> scanned =
        an::scanDeviantPixels<float>(execution, *sourceView, an::CosmeticDetectionOptions{.hotSigma = 5.0});
    ASSERT_TRUE(scanned.hasValue()) << scanned.status().message();
    EXPECT_EQ(scanned->hotCount, 1u);

    std::vector<std::uint8_t> hotBytes = mapBytes(kWidth, kHeight, 1u, 0u);
    an::ValidityMaskView<std::uint8_t> hotView = mapView(hotBytes, kWidth, kHeight);
    ASSERT_TRUE(an::mapDeviantPixels<float>(execution, *sourceView, an::CosmeticDetectionOptions{.hotSigma = 5.0},
                                            nullptr, &hotView)
                    .hasValue());

    std::vector<float> destination(source.size(), 0.0f);
    const std::optional<an::ImageView<float>> destinationView =
        an::ImageView<float>::create(std::span<float>{destination.data(), destination.size()}, layout);
    ASSERT_TRUE(destinationView.has_value());
    const an::ValidityMaskView<const std::uint8_t> hotRead{hotView};
    const an::Result<an::CosmeticCorrectionResult> corrected = an::correctDeviantPixels<float>(
        execution, *sourceView, *destinationView, nullptr, &hotRead, an::CosmeticCorrectionRequest{});
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_EQ(corrected->hotReplaced, 1u);
    EXPECT_NE(destination[30], 40.0f);
}

} // namespace
