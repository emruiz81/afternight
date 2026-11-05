// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

class CapturedExpectationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::optional<std::string> root = an::tests::harness::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
        ASSERT_TRUE(root.has_value());
        m_directory = std::filesystem::path(*root) / "captured-expectation";
        std::filesystem::create_directories(m_directory);
        an::Result<an::consumer::EngineSession> session = adapter::createFixtureSession();
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(*session));

        std::string error;
        const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
        ASSERT_TRUE(directory.has_value()) << error;
        const std::optional<std::vector<fixtures::Expectation>> records =
            fixtures::readExpectationFile(*directory / fixtures::kFitsDecodeExpectationFile, error);
        ASSERT_TRUE(records.has_value()) << error;
        m_records = *records;
    }

    void TearDown() override
    {
        m_session.reset();
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    std::filesystem::path m_directory;
    std::optional<an::consumer::EngineSession> m_session;
    std::vector<fixtures::Expectation> m_records;
};

// The reference decode of every neutral input is a checked-in record; the engine's decode of the same input,
// regenerated here from the same description, is held to it by the record's classification.
TEST_F(CapturedExpectationTest, TheDecodeOfEveryInputMatchesItsRecordedDecode)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::FixtureImage* image = fixtures::standardFixture(record.inputName);
        ASSERT_NE(image, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *samples))
            << record.id << " was captured from another input than this one";
        const std::filesystem::path path = m_directory / (image->name + ".fits");
        ASSERT_TRUE(fixtures::writeFixtureFits(*image, *samples, path, error)) << error;
        const an::Result<adapter::DecodedFile> decoded = adapter::decodeEngineFile(*m_session, path);
        ASSERT_TRUE(decoded.hasValue()) << record.id << ": " << decoded.status().message();
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, decoded->samples);
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason;
    }
}

// The records name the engine as their producer, and none was produced by the engine under test: a comparison with
// the engine's own output would prove nothing.
TEST_F(CapturedExpectationTest, NoComparedRecordWasProducedByTheEngineUnderTest)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
    }
}

} // namespace
