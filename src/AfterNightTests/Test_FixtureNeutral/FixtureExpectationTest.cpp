// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/harness/TestHarness.h"

#include <gtest/gtest.h>

#include <bit>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{

using an::tests::fixtures::Expectation;
using an::tests::fixtures::ExpectationClass;
using an::tests::fixtures::ExpectationComparison;
using an::tests::fixtures::ExpectationProvenance;
using an::tests::fixtures::FixtureImage;
using an::tests::fixtures::FixtureSamples;
using an::tests::fixtures::FixtureSampleType;

ExpectationProvenance provenance()
{
    ExpectationProvenance stated;
    stated.producer = "SomeEngine";
    stated.producerVersion = "1.2.3";
    stated.producerAbi = "4";
    stated.configuration = "Release";
    stated.compiler = "compiler 1.0";
    stated.platform = "system-x64";
    stated.captured = "2026-09-29";
    stated.commit = "0123456789abcdef";
    return stated;
}

FixtureSamples materialized(const FixtureImage& image)
{
    std::string error;
    const std::optional<FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples.value_or(FixtureSamples{});
}

const FixtureImage& fixture(const char* name)
{
    const FixtureImage* image = an::tests::fixtures::standardFixture(name);
    EXPECT_NE(image, nullptr) << name;
    static const FixtureImage empty;
    return image != nullptr ? *image : empty;
}

Expectation expectationOf(const FixtureImage& image, ExpectationClass classification = ExpectationClass::Exact)
{
    const FixtureSamples samples = materialized(image);
    return an::tests::fixtures::makeExpectation("identity", "nothing, which is what the test needs", image, samples,
                                                samples, classification, provenance());
}

std::vector<Expectation> parsed(const std::string& text)
{
    std::string error;
    const std::optional<std::vector<Expectation>> records = an::tests::fixtures::parseExpectations(text, error);
    EXPECT_TRUE(records.has_value()) << error;
    return records.value_or(std::vector<Expectation>{});
}

std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    const std::size_t at = text.find(from);
    EXPECT_NE(at, std::string::npos) << from;
    if (at != std::string::npos)
    {
        text.replace(at, from.size(), to);
    }
    return text;
}

TEST(FixtureExpectationTest, ARecordOfEveryStandardFixtureReadsBackExactly)
{
    ASSERT_FALSE(an::tests::fixtures::standardFixtures().empty());
    for (const FixtureImage& image : an::tests::fixtures::standardFixtures())
    {
        const Expectation written = expectationOf(image);
        EXPECT_EQ(written.id, "identity/" + image.name);
        const std::vector<Expectation> records = parsed(an::tests::fixtures::formatExpectation(written));
        ASSERT_EQ(records.size(), 1u) << image.name;
        const Expectation& read = records[0];
        EXPECT_EQ(read.id, written.id);
        EXPECT_EQ(read.operation, written.operation);
        EXPECT_EQ(read.inputName, image.name);
        EXPECT_EQ(read.inputDescription, an::tests::fixtures::fixtureDigest(image));
        EXPECT_EQ(read.inputSamples, materialized(image).digest());
        EXPECT_EQ(read.classification, ExpectationClass::Exact);
        EXPECT_EQ(read.provenance.producer, "SomeEngine");
        EXPECT_EQ(read.provenance.producerVersion, "1.2.3");
        EXPECT_EQ(read.provenance.producerAbi, "4");
        EXPECT_EQ(read.provenance.configuration, "Release");
        EXPECT_EQ(read.provenance.compiler, "compiler 1.0");
        EXPECT_EQ(read.provenance.platform, "system-x64");
        EXPECT_EQ(read.provenance.captured, "2026-09-29");
        EXPECT_EQ(read.provenance.commit, "0123456789abcdef");
        EXPECT_TRUE(an::tests::fixtures::sameSamples(read.result, written.result)) << image.name;
        EXPECT_TRUE(an::tests::fixtures::expectationDescribes(read, image, materialized(image))) << image.name;
    }
}

TEST(FixtureExpectationTest, ARecordOfNegativeAndPayloadNansReadsBackToTheSameDigest)
{
    // An engine's 0/0 in single precision is the negative quiet NaN; a record of it must read back to the samples, and
    // so to the digest, it states.
    const FixtureImage& image = fixture("nonfinite-mono-float32");
    const FixtureSamples input = materialized(image);
    FixtureSamples result(2u, 1u, 1u, FixtureSampleType::Float32);
    result.setValue(0u, 0u, 0u, static_cast<double>(std::bit_cast<float>(0xFFC00000u)));
    result.setValue(1u, 0u, 0u, static_cast<double>(std::bit_cast<float>(0x7FC00123u)));
    FixtureSamples wide(1u, 1u, 1u, FixtureSampleType::Float64);
    wide.setValue(0u, 0u, 0u, std::bit_cast<double>(0xFFF8000000000000ull));
    for (const FixtureSamples& samples : {result, wide})
    {
        const Expectation written =
            an::tests::fixtures::makeExpectation("nan", "a result holding NaNs of either sign", image, input, samples,
                                                 ExpectationClass::Exact, provenance());
        const std::vector<Expectation> records = parsed(an::tests::fixtures::formatExpectation(written));
        ASSERT_EQ(records.size(), 1u);
        EXPECT_EQ(records[0].result.digest(), samples.digest());
    }
}

TEST(FixtureExpectationTest, ARecordWhoseSamplesDisagreeWithItsDigestIsRefused)
{
    const std::string text = an::tests::fixtures::formatExpectation(expectationOf(fixture("constant-mono-uint8")));
    const std::size_t row = text.find("\nrow ");
    ASSERT_NE(row, std::string::npos);
    const std::size_t end = text.find('\n', row + 1u);
    std::string line = text.substr(row + 1u, end - row - 1u);
    const std::size_t lastSpace = line.rfind(' ');
    const std::string tampered = replaced(text, line, line.substr(0u, lastSpace + 1u) + "17");
    std::string error;
    EXPECT_FALSE(an::tests::fixtures::parseExpectations(tampered, error).has_value());
    EXPECT_NE(error.find("digest"), std::string::npos) << error;
}

TEST(FixtureExpectationTest, AnIncompleteOrContradictoryRecordIsRefused)
{
    const std::string exact = an::tests::fixtures::formatExpectation(expectationOf(fixture("constant-mono-uint8")));
    const std::string tolerance = an::tests::fixtures::formatExpectation(
        expectationOf(fixture("constant-mono-uint8"), ExpectationClass::Tolerance));
    Expectation acceptedRecord = expectationOf(fixture("constant-mono-uint8"), ExpectationClass::Accepted);
    acceptedRecord.acceptedReason = "the engine rounds half to even";
    const std::string accepted = an::tests::fixtures::formatExpectation(acceptedRecord);
    ASSERT_EQ(parsed(tolerance).size(), 1u);
    ASSERT_EQ(parsed(accepted).size(), 1u);
    EXPECT_EQ(parsed(accepted)[0].acceptedReason, "the engine rounds half to even");

    const std::vector<std::pair<std::string, std::string>> broken = {
        {"an unknown class", replaced(exact, "class        exact", "class        close")},
        {"an unknown field", replaced(exact, "operation", "procedure")},
        {"no operation", replaced(exact, "operation", "#")},
        {"a missing row", exact.substr(0u, exact.rfind("\nrow ")) + "\nend\n"},
        {"a reason on an exact record", replaced(exact, "end\n", "accepted     because\nend\n")},
        {"an accepted record with no reason", replaced(accepted, "accepted     the engine rounds half to even\n", "")},
        {"a tolerance on an exact record", replaced(exact, "end\n", "tolerance    absolute=1 relative=0\nend\n")},
        {"a result with a wrong type", replaced(exact, " uint8 ", " uint9 ")},
        {"a record with no end", exact.substr(0u, exact.rfind("end"))},
    };
    for (const auto& [why, text] : broken)
    {
        std::string error;
        EXPECT_FALSE(an::tests::fixtures::parseExpectations(text, error).has_value()) << why;
        EXPECT_FALSE(error.empty()) << why;
    }
}

TEST(FixtureExpectationTest, AnExactRecordMatchesIdenticalSamplesAndAnyNan)
{
    const FixtureImage& image = fixture("nonfinite-mono-float32");
    Expectation expectation = expectationOf(image);
    FixtureSamples candidate = materialized(image);
    EXPECT_TRUE(an::tests::fixtures::compareWithExpectation(expectation, candidate).matched);
    for (std::uint32_t y = 0u; y < candidate.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < candidate.width(); ++x)
        {
            if (std::isnan(candidate.value(x, y, 0u)))
            {
                candidate.setValue(x, y, 0u, -std::numeric_limits<double>::quiet_NaN());
            }
        }
    }
    EXPECT_TRUE(an::tests::fixtures::compareWithExpectation(expectation, candidate).matched);
}

TEST(FixtureExpectationTest, AnExactRecordRefusesOneUnitOfDifferenceAndAnotherShape)
{
    const FixtureImage& image = fixture("gradient-rgb-uint16");
    const Expectation expectation = expectationOf(image);
    FixtureSamples candidate = materialized(image);
    candidate.setValue(3u, 2u, 1u, candidate.value(3u, 2u, 1u) + 1.0);
    const ExpectationComparison off = an::tests::fixtures::compareWithExpectation(expectation, candidate);
    EXPECT_FALSE(off.matched);
    EXPECT_EQ(off.differingSamples, 1u);
    EXPECT_EQ(off.largestDifference, 1.0);
    EXPECT_FALSE(off.reason.empty());

    const FixtureSamples other(image.width, image.height, 3u, FixtureSampleType::Int16);
    const ExpectationComparison shape = an::tests::fixtures::compareWithExpectation(expectation, other);
    EXPECT_FALSE(shape.matched);
    EXPECT_NE(shape.reason.find("int16"), std::string::npos) << shape.reason;
}

TEST(FixtureExpectationTest, AToleranceRecordHoldsEverySampleToItsAbsoluteAndRelativeBound)
{
    const FixtureImage& image = fixture("gradient-mono-float64-physical");
    Expectation expectation = expectationOf(image, ExpectationClass::Tolerance);
    expectation.absoluteTolerance = 0.5;
    expectation.relativeTolerance = 1e-3;
    FixtureSamples candidate = materialized(image);
    const double reference = candidate.value(1u, 1u, 0u);
    const double bound = 0.5 + 1e-3 * std::fabs(reference);
    candidate.setValue(1u, 1u, 0u, reference + bound * 0.999);
    EXPECT_TRUE(an::tests::fixtures::compareWithExpectation(expectation, candidate).matched);
    candidate.setValue(1u, 1u, 0u, reference + bound * 1.001);
    const ExpectationComparison outside = an::tests::fixtures::compareWithExpectation(expectation, candidate);
    EXPECT_FALSE(outside.matched);
    EXPECT_EQ(outside.differingSamples, 1u);
    candidate.setValue(1u, 1u, 0u, std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(an::tests::fixtures::compareWithExpectation(expectation, candidate).matched);
}

TEST(FixtureExpectationTest, AnAcceptedDifferenceIsReportedAndNeverFails)
{
    const FixtureImage& image = fixture("gradient-rgb-uint16");
    Expectation expectation = expectationOf(image, ExpectationClass::Accepted);
    expectation.acceptedReason = "a documented rounding change";
    FixtureSamples candidate = materialized(image);
    candidate.setValue(0u, 0u, 0u, 0.0);
    candidate.setValue(1u, 0u, 0u, 0.0);
    const ExpectationComparison accepted = an::tests::fixtures::compareWithExpectation(expectation, candidate);
    EXPECT_TRUE(accepted.matched);
    EXPECT_EQ(accepted.differingSamples, 2u);
    EXPECT_NE(accepted.reason.find("a documented rounding change"), std::string::npos) << accepted.reason;
    EXPECT_FALSE(
        an::tests::fixtures::compareWithExpectation(expectation, FixtureSamples(1u, 1u, 1u, FixtureSampleType::UInt16))
            .matched);
}

TEST(FixtureExpectationTest, ARecordDescribesOnlyTheInputItWasCapturedFrom)
{
    const FixtureImage& image = fixture("gradient-rgb-uint16");
    const Expectation expectation = expectationOf(image);
    FixtureSamples samples = materialized(image);
    EXPECT_TRUE(an::tests::fixtures::expectationDescribes(expectation, image, samples));
    FixtureImage renamed = image;
    renamed.name = "renamed";
    EXPECT_FALSE(an::tests::fixtures::expectationDescribes(expectation, renamed, samples));
    FixtureImage redescribed = image;
    redescribed.cards.clear();
    EXPECT_FALSE(an::tests::fixtures::expectationDescribes(expectation, redescribed, samples));
    samples.setValue(0u, 0u, 0u, samples.value(0u, 0u, 0u) + 1.0);
    EXPECT_FALSE(an::tests::fixtures::expectationDescribes(expectation, image, samples));
}

TEST(FixtureExpectationTest, AFileHoldsSeveralRecordsAndIsWrittenWithCrlf)
{
    const std::optional<std::string> root = an::tests::harness::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
    ASSERT_TRUE(root.has_value());
    const std::filesystem::path path = std::filesystem::path(*root) / "two.expectations";
    const std::vector<Expectation> written = {expectationOf(fixture("constant-mono-uint8")),
                                              expectationOf(fixture("gradient-rgb-uint16"))};
    std::string error;
    ASSERT_TRUE(an::tests::fixtures::writeExpectationFile(path, written, error)) << error;
    std::ifstream stream(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();
    EXPECT_NE(bytes.find("\r\n"), std::string::npos);
    for (std::size_t at = bytes.find('\n'); at != std::string::npos; at = bytes.find('\n', at + 1u))
    {
        ASSERT_GT(at, 0u);
        ASSERT_EQ(bytes[at - 1u], '\r') << "a bare line feed at byte " << at;
    }
    const std::optional<std::vector<Expectation>> read = an::tests::fixtures::readExpectationFile(path, error);
    ASSERT_TRUE(read.has_value()) << error;
    ASSERT_EQ(read->size(), 2u);
    EXPECT_EQ((*read)[0].id, written[0].id);
    EXPECT_EQ((*read)[1].id, written[1].id);
    std::filesystem::remove(path);
}

TEST(FixtureExpectationTest, TheCheckedInDecodeExpectationsNameEveryStandardFixtureOnce)
{
    std::string error;
    const std::optional<std::filesystem::path> directory = an::tests::fixtures::expectationDirectory(error);
    ASSERT_TRUE(directory.has_value()) << error;
    const std::optional<std::vector<Expectation>> records =
        an::tests::fixtures::readExpectationFile(*directory / an::tests::fixtures::kFitsDecodeExpectationFile, error);
    ASSERT_TRUE(records.has_value()) << error;
    std::set<std::string> named;
    for (const Expectation& record : *records)
    {
        EXPECT_TRUE(named.insert(record.inputName).second) << record.inputName << " is captured twice";
        EXPECT_EQ(record.id, std::string(an::tests::fixtures::kFitsDecodeOperation) + "/" + record.inputName);
        const FixtureImage* image = an::tests::fixtures::standardFixture(record.inputName);
        ASSERT_NE(image, nullptr) << record.inputName << " is no standard fixture";
        EXPECT_TRUE(an::tests::fixtures::expectationDescribes(record, *image, materialized(*image)))
            << record.inputName << " changed after it was captured; capture it again";
    }
    ASSERT_FALSE(an::tests::fixtures::standardFixtures().empty());
    for (const FixtureImage& image : an::tests::fixtures::standardFixtures())
    {
        EXPECT_EQ(named.count(image.name), 1u) << image.name << " has no captured decode";
    }
}

} // namespace
