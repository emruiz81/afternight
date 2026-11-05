// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "RawCodecFixture.h"

#include <gtest/gtest.h>

TEST(RawCodecScenariosTest, DecodeOrientationAndMosaicBalanceAreSeparatePairedRows)
{
    const auto scenarios = an::bench::rawCodecScenarios();
    ASSERT_EQ(scenarios.size(), 3u);
    EXPECT_EQ(scenarios[0].id, "codec.raw_decode.u16");
    EXPECT_EQ(scenarios[1].id, "codec.raw_orientation.u16");
    EXPECT_EQ(scenarios[2].id, "codec.raw_mosaic_balance.u16");
    EXPECT_EQ(scenarios[0].parameters.at("operation"), "decode");
    EXPECT_EQ(scenarios[1].parameters.at("orientation"), "rotate90-clockwise");
    EXPECT_EQ(scenarios[2].parameters.at("whiteBalance"), "custom-2,1,1.5");
    for (auto scenario : scenarios)
    {
        EXPECT_TRUE(an::bench::matchesRawCodecScenario(scenario));
        scenario.parameters["timedBatch"] = "4";
        EXPECT_TRUE(an::bench::matchesRawCodecScenario(scenario));
        scenario.input.width -= 1u;
        EXPECT_FALSE(an::bench::matchesRawCodecScenario(scenario));
    }
}
