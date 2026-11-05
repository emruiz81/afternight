// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "SequenceScenarios.h"

#include <gtest/gtest.h>

TEST(SequenceScenariosTest, EveryModeIsDeclaredAndRecognised)
{
    const auto scenarios = an::bench::sequenceScenarios();
    ASSERT_EQ(scenarios.size(), 4u);
    EXPECT_EQ(an::bench::sequenceScenarios(false).size(), 3u);
    for (auto scenario : scenarios)
    {
        EXPECT_TRUE(an::bench::matchesSequenceScenario(scenario));
        EXPECT_EQ(an::bench::matchesSequenceScenario(scenario, false), scenario.parameters.at("mode") != "cached");
        scenario.parameters["timedBatch"] = "20";
        EXPECT_TRUE(an::bench::matchesSequenceScenario(scenario));
        scenario.parameters["frames"] = "5";
        EXPECT_FALSE(an::bench::matchesSequenceScenario(scenario));
    }
}

TEST(SequenceScenariosTest, RefusesUnimplementedGeometryAndWorkCounts)
{
    for (const auto& scenario : an::bench::sequenceScenarios())
    {
        auto changed = scenario;
        changed.input.width = 2047u;
        EXPECT_FALSE(an::bench::matchesSequenceScenario(changed));
        changed = scenario;
        changed.bytesProcessed = 1u;
        EXPECT_FALSE(an::bench::matchesSequenceScenario(changed));
        changed = scenario;
        changed.parameters["unimplemented"] = "yes";
        EXPECT_FALSE(an::bench::matchesSequenceScenario(changed));
    }
}

TEST(SequenceScenariosTest, ImageListWriteIsAPairedAtomicPublicationRow)
{
    auto scenario = an::bench::imageListWriteScenario();
    EXPECT_EQ(scenario.id, "sequence.image_list_write.8x_f32");
    EXPECT_EQ(scenario.parameters.at("frames"), "8");
    EXPECT_EQ(scenario.parameters.at("format"), "FITS");
    EXPECT_TRUE(an::bench::matchesImageListWriteScenario(scenario));
    scenario.parameters["timedBatch"] = "3";
    EXPECT_TRUE(an::bench::matchesImageListWriteScenario(scenario));
    scenario.input.height -= 1u;
    EXPECT_FALSE(an::bench::matchesImageListWriteScenario(scenario));
}
