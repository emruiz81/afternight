// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FfmpegSequenceScenarios.h"
#include "SequenceScenarios.h"
#include "SerSequenceScenarios.h"

#include <gtest/gtest.h>

TEST(FfmpegSequenceScenariosTest, TheDecodeRowIsSharedAndDispatchedOnItsOwn)
{
    const auto scenarios = an::bench::ffmpegSequenceScenarios();
    ASSERT_EQ(scenarios.size(), 2u);
    EXPECT_EQ(scenarios[0].id, "sequence.ffmpeg_read.ffv1_8x_u16");
    EXPECT_EQ(scenarios[1].id, "sequence.ffmpeg_cache.ffv1_8x_u16");
    EXPECT_EQ(scenarios[1].parameters.at("access"), "0,1,0,1,0,1,2,3");
    for (auto scenario : scenarios)
    {
        EXPECT_EQ(scenario.group, "sequence");
        EXPECT_TRUE(scenario.validate().empty());
        EXPECT_TRUE(an::bench::isFfmpegSequenceScenario(scenario.id));
        EXPECT_TRUE(an::bench::matchesFfmpegSequenceScenario(scenario));
        EXPECT_FALSE(an::bench::isSerSequenceScenario(scenario.id));
        EXPECT_FALSE(an::bench::matchesSequenceScenario(scenario));
        EXPECT_EQ(scenario.bytesProcessed, scenario.elementsProcessed * 2u);
        scenario.parameters["timedBatch"] = "3";
        EXPECT_TRUE(an::bench::matchesFfmpegSequenceScenario(scenario));
    }
    for (const auto& other : an::bench::serSequenceScenarios())
        EXPECT_FALSE(an::bench::isFfmpegSequenceScenario(other.id));
    for (const auto& other : an::bench::sequenceScenarios())
        EXPECT_FALSE(an::bench::isFfmpegSequenceScenario(other.id));
}

TEST(FfmpegSequenceScenariosTest, RefusesChangedGeometryCodingAndWorkCounts)
{
    for (const auto& scenario : an::bench::ffmpegSequenceScenarios())
    {
        auto changed = scenario;
        changed.input.width = 2047u;
        EXPECT_FALSE(an::bench::matchesFfmpegSequenceScenario(changed));
        changed = scenario;
        changed.input.sampleType = an::bench::SampleType::UInt8;
        EXPECT_FALSE(an::bench::matchesFfmpegSequenceScenario(changed));
        changed = scenario;
        changed.parameters["slices"] = "4";
        EXPECT_FALSE(an::bench::matchesFfmpegSequenceScenario(changed));
        changed = scenario;
        changed.elementsProcessed = 1u;
        EXPECT_FALSE(an::bench::matchesFfmpegSequenceScenario(changed));
    }
}
