// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "SerSequenceScenarios.h"
#include "SequenceScenarios.h"

#include <gtest/gtest.h>

TEST(SerSequenceScenariosTest, ReadAndWriteAreSharedRowsWithTheirOwnDispatch)
{
    const auto scenarios = an::bench::serSequenceScenarios();
    ASSERT_EQ(scenarios.size(), 2u);
    EXPECT_EQ(scenarios[0].id, "sequence.ser_read.8x_u16");
    EXPECT_EQ(scenarios[1].id, "sequence.ser_write.8x_u16");
    for (auto scenario : scenarios)
    {
        EXPECT_EQ(scenario.group, "sequence");
        EXPECT_TRUE(an::bench::isSerSequenceScenario(scenario.id));
        EXPECT_TRUE(an::bench::matchesSerSequenceScenario(scenario));
        EXPECT_FALSE(an::bench::matchesSequenceScenario(scenario));
        EXPECT_EQ(scenario.bytesProcessed, scenario.elementsProcessed * 2u);
        scenario.parameters["timedBatch"] = "3";
        EXPECT_TRUE(an::bench::matchesSerSequenceScenario(scenario));
        scenario.parameters["frames"] = "9";
        EXPECT_FALSE(an::bench::matchesSerSequenceScenario(scenario));
    }
    for (const auto& scenario : an::bench::sequenceScenarios())
        EXPECT_FALSE(an::bench::isSerSequenceScenario(scenario.id));
}

TEST(SerSequenceScenariosTest, RefusesUnimplementedGeometryRepresentationsAndWorkCounts)
{
    for (const auto& scenario : an::bench::serSequenceScenarios())
    {
        auto changed = scenario;
        changed.input.width = 2047u;
        EXPECT_FALSE(an::bench::matchesSerSequenceScenario(changed));
        changed = scenario;
        changed.input.sampleType = an::bench::SampleType::UInt8;
        EXPECT_FALSE(an::bench::matchesSerSequenceScenario(changed));
        changed = scenario;
        changed.elementsProcessed = 1u;
        EXPECT_FALSE(an::bench::matchesSerSequenceScenario(changed));
        changed = scenario;
        changed.parameters["mode"] = "append";
        EXPECT_FALSE(an::bench::matchesSerSequenceScenario(changed));
    }
}
