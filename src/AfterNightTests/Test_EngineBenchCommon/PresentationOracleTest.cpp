// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The display presentation rows and their engine-independent definition.
#include "PresentationOracle.h"
#include "PresentationScenarios.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace an::bench;

TEST(PresentationOracleTest, BothRowsAreWellFormedAndHeldToTwoCodes)
{
    const std::vector<ScenarioSpec> rows = presentationScenarios();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].id, kPresentationAutostretchRgba);
    EXPECT_EQ(rows[1].id, kPresentationSubsampled8bit);
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_TRUE(requiresPresentationOracle(row)) << row.id;
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::AbsoluteRelative) << row.id;
        EXPECT_EQ(row.tolerance, 2.0) << row.id;
    }
    EXPECT_EQ(rows[0].input.channels, 3u);
    EXPECT_EQ(rows[0].input.sampleType, SampleType::Float32);
    EXPECT_EQ(rows[1].input.sampleType, SampleType::UInt16);
    EXPECT_EQ(rows[1].parameters.at("subsample"), "4");
}

TEST(PresentationOracleTest, ALocationIsExactOverTheClampedFiniteSamples)
{
    const std::vector<float> odd{0.5f, 0.1f, 0.3f, std::numeric_limits<float>::quiet_NaN(), 0.9f, 0.2f};
    const PresentationLocation location = presentationLocation(odd);
    EXPECT_DOUBLE_EQ(location.median, static_cast<double>(0.3f));
    // Deviations from 0.3: 0.2, 0.2, 0.0, 0.6, 0.1 -> 0.2.
    EXPECT_NEAR(location.deviation, 0.2, 1e-7);
    const std::vector<float> even{-1.0f, 0.2f, 0.4f, 2.0f};
    const PresentationLocation clamped = presentationLocation(even);
    EXPECT_NEAR(clamped.median, 0.3, 1e-7);
}

TEST(PresentationOracleTest, TheTransferAndItsBalanceFollowTheirDefinitions)
{
    const PresentationTransfer identity{};
    EXPECT_EQ(presentationTransfer(0.37L, identity), 0.37L);
    const PresentationTransfer clipped{.midtones = 0.2L, .shadows = 0.1L, .highlights = 0.9L};
    EXPECT_EQ(presentationTransfer(0.05L, clipped), 0.0L);
    EXPECT_EQ(presentationTransfer(0.95L, clipped), 1.0L);
    // The balance maps the median's height above the shadows to the target.
    const std::vector<PresentationLocation> sky{{.median = 0.05, .deviation = 0.004}};
    const std::vector<PresentationTransfer> transfers = unlinkedPresentationTransfers(sky, -2.8, 0.25);
    ASSERT_EQ(transfers.size(), 1u);
    const long double shadows = 0.05L - 2.8L * 0.004L * 1.4826022185056018L;
    EXPECT_NEAR(static_cast<double>(transfers[0].shadows), static_cast<double>(shadows), 1e-15);
    // The balance is the one that takes the median's height above the shadow point to the target.
    const PresentationTransfer balance{.midtones = transfers[0].midtones};
    EXPECT_NEAR(static_cast<double>(presentationTransfer(0.05L - transfers[0].shadows, balance)), 0.25, 1e-12);
    // Every median above one half mirrors the rule about the highlights.
    const std::vector<PresentationLocation> bright{{.median = 0.8, .deviation = 0.01},
                                                   {.median = 0.7, .deviation = 0.0}};
    const std::vector<PresentationTransfer> inverted = unlinkedPresentationTransfers(bright, -2.8, 0.25);
    EXPECT_EQ(inverted[0].shadows, 0.0L);
    EXPECT_NEAR(static_cast<double>(inverted[1].highlights), 0.7 + 2.8 * 0.001, 1e-15);
}

TEST(PresentationOracleTest, AByteRoundsToNearestWithTiesToEven)
{
    EXPECT_EQ(presentationByte(-0.1L), 0u);
    EXPECT_EQ(presentationByte(std::numeric_limits<long double>::quiet_NaN()), 0u);
    EXPECT_EQ(presentationByte(1.5L), 255u);
    EXPECT_EQ(presentationByte(0.5L / 255.0L), 0u);
    EXPECT_EQ(presentationByte(1.5L / 255.0L), 2u);
    EXPECT_EQ(presentationByte(128.4L / 255.0L), 128u);
}

TEST(PresentationOracleTest, TheChecksMeasureEveryDifferenceAndRefuseATranslucentPixel)
{
    // Two pixels of grey RGB: the oracle's bytes, then one code off, then a translucent alpha.
    const std::vector<float> input{0.02f, 0.4f, 0.02f, 0.4f, 0.02f, 0.4f};
    ResultRecord exact;
    std::vector<std::uint8_t> rgba(8u, 255u);
    {
        const std::vector<PresentationLocation> locations(3u, presentationLocation(std::vector<float>{0.02f, 0.4f}));
        const std::vector<PresentationTransfer> transfers = unlinkedPresentationTransfers(locations, -2.8, 0.25);
        for (std::size_t pixel = 0u; pixel < 2u; ++pixel)
        {
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                rgba[pixel * 4u + channel] =
                    presentationByte(presentationTransfer(input[channel * 2u + pixel], transfers[channel]));
            }
        }
    }
    checkPresentationAutostretchOutput(input, 2u, 1u, rgba, exact);
    ASSERT_TRUE(exact.correctnessMetric.has_value());
    EXPECT_EQ(*exact.correctnessMetric, 0.0);
    rgba[1] = static_cast<std::uint8_t>(rgba[1] + 1u);
    ResultRecord off;
    checkPresentationAutostretchOutput(input, 2u, 1u, rgba, off);
    EXPECT_EQ(*off.correctnessMetric, 1.0);
    EXPECT_EQ(off.counters.at("presentationOracleDifferingSamples"), 1.0);
    rgba[7] = 254u;
    ResultRecord translucent;
    checkPresentationAutostretchOutput(input, 2u, 1u, rgba, translucent);
    EXPECT_EQ(*translucent.correctnessMetric, 255.0);

    const std::vector<std::uint16_t> codes{0u, 65535u, 1000u, 2000u, 32768u, 4u, 5u, 6u, 7u};
    // The 3x3 frame at a factor of two presents (0,0), (2,0), (0,2), (2,2): codes 0, 1000, 5 and 7, which are the
    // bytes 0, 4, 0 and 0.
    std::vector<std::uint8_t> subsampled{0u, 4u, 0u, 0u};
    ResultRecord linear;
    checkPresentationSubsampledOutput(codes, 3u, 3u, 2u, subsampled, linear);
    EXPECT_EQ(linear.counters.at("presentationOracleSamples"), 4.0);
    EXPECT_EQ(*linear.correctnessMetric, 0.0);
    subsampled[1] = 3u;
    ResultRecord truncated;
    checkPresentationSubsampledOutput(codes, 3u, 3u, 2u, subsampled, truncated);
    EXPECT_EQ(*truncated.correctnessMetric, 1.0);
}
