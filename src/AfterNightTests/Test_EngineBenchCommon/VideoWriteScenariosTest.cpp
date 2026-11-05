// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FfmpegSequenceScenarios.h"
#include "SequenceScenarios.h"
#include "SerSequenceScenarios.h"
#include "VideoWriteScenarios.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

TEST(VideoWriteScenariosTest, EveryRowIsDispatchedOnItsOwn)
{
    const auto all = an::bench::videoWriteScenarios();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].id, "sequence.avi_write.8x_u8");
    EXPECT_EQ(all[1].id, "sequence.video_write.vp9_8x_u8");
    EXPECT_EQ(all[2].id, "sequence.video_write.h264_8x_u8");
#if defined(__linux__)
    EXPECT_EQ(all[1].parameters.at("encoderThreads"), "3");
#else
    EXPECT_EQ(all[1].parameters.at("encoderThreads"), "4");
#endif
    EXPECT_EQ(all[2].parameters.at("format"), "H264");
    EXPECT_EQ(all[2].parameters.at("container"), "mp4");
    EXPECT_EQ(all[2].parameters.at("quality"), "standard");
    EXPECT_EQ(all[2].parameters.at("encoderThreads"), "1");
    for (auto scenario : all)
    {
        SCOPED_TRACE(scenario.id);
        EXPECT_EQ(scenario.group, "sequence");
        EXPECT_TRUE(scenario.validate().empty());
        EXPECT_TRUE(an::bench::isVideoWriteScenario(scenario.id));
        EXPECT_TRUE(an::bench::matchesVideoWriteScenario(scenario));
        EXPECT_FALSE(an::bench::isSerSequenceScenario(scenario.id));
        EXPECT_FALSE(an::bench::isFfmpegSequenceScenario(scenario.id));
        EXPECT_FALSE(an::bench::matchesSequenceScenario(scenario));
        EXPECT_EQ(scenario.bytesProcessed, scenario.elementsProcessed);
        scenario.parameters["timedBatch"] = "3";
        EXPECT_TRUE(an::bench::matchesVideoWriteScenario(scenario));
    }
    for (const auto& other : an::bench::serSequenceScenarios())
        EXPECT_FALSE(an::bench::isVideoWriteScenario(other.id));
    for (const auto& other : an::bench::ffmpegSequenceScenarios())
        EXPECT_FALSE(an::bench::isVideoWriteScenario(other.id));
    for (const auto& other : an::bench::sequenceScenarios())
        EXPECT_FALSE(an::bench::isVideoWriteScenario(other.id));
}

TEST(VideoWriteScenariosTest, RefusesChangedGeometryEncodingAndWorkCounts)
{
    const auto scenario = an::bench::videoWriteScenarios()[1];
    auto changed = scenario;
    changed.input.width = 1022u;
    EXPECT_FALSE(an::bench::matchesVideoWriteScenario(changed));
    changed = scenario;
    changed.parameters["encoderThreads"] = "1";
    EXPECT_FALSE(an::bench::matchesVideoWriteScenario(changed));
    changed = scenario;
    changed.parameters["rows"] = "as-generated";
    EXPECT_FALSE(an::bench::matchesVideoWriteScenario(changed));
    changed = scenario;
    changed.elementsProcessed = 1u;
    EXPECT_FALSE(an::bench::matchesVideoWriteScenario(changed));
}

TEST(VideoWriteScenariosTest, MirroringMakesThePictureItsOwnVerticalReflection)
{
    for (const std::size_t rows : {1u, 4u, 5u})
    {
        constexpr std::size_t kRowBytes = 3u;
        std::vector<std::byte> samples(rows * kRowBytes);
        for (std::size_t i = 0u; i < samples.size(); ++i)
            samples[i] = static_cast<std::byte>(i + 1u);
        const auto original = samples;
        an::bench::mirrorRows(samples, kRowBytes, rows);
        for (std::size_t row = 0u; row < rows; ++row)
            for (std::size_t x = 0u; x < kRowBytes; ++x)
            {
                EXPECT_EQ(samples[row * kRowBytes + x], samples[(rows - 1u - row) * kRowBytes + x]);
                if (row < (rows + 1u) / 2u)
                    EXPECT_EQ(samples[row * kRowBytes + x], original[row * kRowBytes + x]);
            }
    }
}
