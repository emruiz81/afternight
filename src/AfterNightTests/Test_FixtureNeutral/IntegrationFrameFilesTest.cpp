// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "fixtures/harness/TestHarness.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace
{

using an::tests::fixtures::AcquisitionFacts;
using an::tests::fixtures::FixtureFitsFile;
using an::tests::fixtures::FixtureImage;
using an::tests::fixtures::parseDouble;

class IntegrationFrameFilesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::optional<std::string> root = an::tests::harness::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
        ASSERT_TRUE(root.has_value());
        m_directory = std::filesystem::path(*root) / "integration-frames";
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    std::filesystem::path m_directory;
};

TEST_F(IntegrationFrameFilesTest, AFrameStatesExactlyTheFactsItWasGiven)
{
    AcquisitionFacts facts;
    facts.kind = "Light Frame";
    facts.exposureSeconds = 300.0;
    facts.sensorTemperatureCelsius = -10.5;
    facts.gain = 139;
    facts.offset = 21;
    facts.filter = "Ha";
    facts.object = "M 42";
    facts.instrument = "ZWO ASI2600MM Pro";
    facts.exposureStart = "2026-01-12T23:59:30";
    facts.fileDate = "2026-01-13T00:04:31";

    const FixtureImage frame = an::tests::fixtures::acquisitionFrame("light", facts, 8u, 6u);
    const std::filesystem::path path = m_directory / "lights" / "light_001.fit";
    std::string error;
    ASSERT_TRUE(an::tests::fixtures::writeAcquisitionFrame(frame, path, error)) << error;

    const std::optional<FixtureFitsFile> read = an::tests::fixtures::readFixtureFits(path, error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_EQ(read->card("IMAGETYP"), "Light Frame");
    EXPECT_EQ(parseDouble(*read->card("EXPTIME")), 300.0);
    EXPECT_EQ(parseDouble(*read->card("CCD-TEMP")), -10.5);
    EXPECT_EQ(read->card("GAIN"), "139");
    EXPECT_EQ(read->card("OFFSET"), "21");
    EXPECT_EQ(read->card("FILTER"), "Ha");
    EXPECT_EQ(read->card("OBJECT"), "M 42");
    EXPECT_EQ(read->card("INSTRUME"), "ZWO ASI2600MM Pro");
    EXPECT_EQ(read->card("DATE-OBS"), "2026-01-12T23:59:30");
    EXPECT_EQ(read->card("DATE"), "2026-01-13T00:04:31");
    EXPECT_FALSE(read->card("ISOSPEED").has_value());
    EXPECT_FALSE(read->card("FRAME").has_value());
    EXPECT_EQ(read->samples.width(), 8u);
    EXPECT_EQ(read->samples.height(), 6u);
    EXPECT_EQ(read->samples.value(3u, 2u, 0u), 1000.0);
}

TEST_F(IntegrationFrameFilesTest, AFactLeftOutWritesNoCard)
{
    AcquisitionFacts facts;
    facts.kindKeyword = "FRAME";
    facts.kind = "Dark";
    facts.exposureKeyword = "EXPOSURE";
    facts.exposureSeconds = 60.0;

    const FixtureImage frame = an::tests::fixtures::acquisitionFrame("dark", facts);
    const std::filesystem::path path = m_directory / "dark.fit";
    std::string error;
    ASSERT_TRUE(an::tests::fixtures::writeAcquisitionFrame(frame, path, error)) << error;

    const std::optional<FixtureFitsFile> read = an::tests::fixtures::readFixtureFits(path, error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_EQ(read->card("FRAME"), "Dark");
    EXPECT_EQ(parseDouble(*read->card("EXPOSURE")), 60.0);
    for (const char* keyword : {"IMAGETYP", "EXPTIME", "CCD-TEMP", "GAIN", "ISOSPEED", "OFFSET", "FILTER", "OBJECT",
                                "INSTRUME", "DATE-OBS", "DATE"})
    {
        EXPECT_FALSE(read->card(keyword).has_value()) << keyword;
    }
}

} // namespace
