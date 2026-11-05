// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The RAW load case table and its DNG writer: what the engine fixture executable reads must be well formed before it
// compares, and the file it opens must say what the description says, read back here by hand rather than by the
// engine's decoder.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/RawPolicyCases.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;

/// A file's bytes, or none.
std::vector<std::uint8_t> bytesOf(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint32_t little(const std::vector<std::uint8_t>& bytes, std::size_t offset, unsigned size)
{
    std::uint32_t value = 0u;
    for (unsigned i = 0u; i < size; ++i)
    {
        value |= static_cast<std::uint32_t>(bytes.at(offset + i)) << (8u * i);
    }
    return value;
}

/// The first value of each tag of a little-endian TIFF's first directory, by tag, and the offset of a value too long
/// to sit in its entry.
struct Directory
{
    std::map<std::uint16_t, std::uint32_t> values;
    std::map<std::uint16_t, std::uint32_t> counts;
};

Directory readDirectory(const std::vector<std::uint8_t>& bytes)
{
    Directory directory;
    const std::uint32_t start = little(bytes, 4u, 4u);
    const std::uint32_t entries = little(bytes, start, 2u);
    for (std::uint32_t entry = 0u; entry < entries; ++entry)
    {
        const std::size_t at = start + 2u + entry * 12u;
        const auto tag = static_cast<std::uint16_t>(little(bytes, at, 2u));
        const std::uint32_t type = little(bytes, at + 2u, 2u);
        directory.counts[tag] = little(bytes, at + 4u, 4u);
        directory.values[tag] = little(bytes, at + 8u, type == 3u ? 2u : 4u);
    }
    return directory;
}

class RawPolicyCasesTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_directory =
            std::filesystem::temp_directory_path() /
            ("raw-policy-cases-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_directory);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    std::filesystem::path m_directory;
};

} // namespace

TEST_F(RawPolicyCasesTest, EveryInputIsUniqueMaterializesAndIsACameraFrame)
{
    std::set<std::string> names;
    for (const fixtures::RawPolicyInput& input : fixtures::rawPolicyInputs())
    {
        const fixtures::FixtureImage& image = input.image;
        EXPECT_TRUE(names.insert(image.name).second) << image.name << " is stated twice";
        EXPECT_EQ(fixtures::fixtureNamed(image.name), nullptr) << image.name << " is also a catalogue fixture";
        EXPECT_EQ(fixtures::rawPolicyInput(image.name), &input) << image.name;
        EXPECT_EQ(image.sampleType, fixtures::FixtureSampleType::UInt16) << image.name;
        EXPECT_EQ(image.domain, fixtures::FixtureDomain::SensorCode) << image.name;
        EXPECT_EQ(image.channels == fixtures::FixtureChannels::Mono, image.cfa != fixtures::FixtureCfa::None)
            << image.name << ": a mosaic has a filter array and a full-colour frame has none";
        EXPECT_LT(input.blackLevel, input.whiteLevel) << image.name;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(image, error);
        ASSERT_TRUE(samples.has_value()) << image.name << ": " << error;
        for (std::uint32_t channel = 0u; channel < samples->channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < samples->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < samples->width(); ++x)
                {
                    const double code = samples->value(x, y, channel);
                    ASSERT_GE(code, input.blackLevel)
                        << image.name << " sits below its black level at " << x << "," << y;
                    ASSERT_LE(code, input.whiteLevel) << image.name << " passes its white level at " << x << "," << y;
                }
            }
        }
    }
    EXPECT_EQ(fixtures::rawPolicyInput("no-such-input"), nullptr);
}

TEST_F(RawPolicyCasesTest, EveryCaseIsUniqueAndStatesARecordTheProductPersisted)
{
    std::set<std::string> ids;
    std::set<std::string> modes;
    for (const fixtures::RawPolicyCase& item : fixtures::rawPolicyCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        EXPECT_NE(fixtures::rawPolicyInput(item.input), nullptr) << item.id();
        EXPECT_EQ(fixtures::rawPolicyCase(item.id()), &item) << item.id();
        EXPECT_NE(std::find(std::begin(fixtures::kRawWhiteBalanceModes), std::end(fixtures::kRawWhiteBalanceModes),
                            item.policy.whiteBalance),
                  std::end(fixtures::kRawWhiteBalanceModes))
            << item.id();
        modes.insert(item.policy.whiteBalance);
        EXPECT_EQ(item.operation,
                  "raw-open." + item.policy.whiteBalance + (item.policy.autoRotate ? ".rotated" : std::string()))
            << item.id();
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_NE(item.source.find(".cpp:"), std::string::npos) << item.id();
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, !item.acceptedReason.empty())
            << item.id();
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Tolerance,
                  item.absoluteTolerance > 0.0 || item.relativeTolerance > 0.0)
            << item.id();
    }
    EXPECT_EQ(modes.size(), std::size(fixtures::kRawWhiteBalanceModes)) << "every mode the product persists";
    EXPECT_EQ(fixtures::rawPolicyCase("no-such-case"), nullptr);
}

TEST_F(RawPolicyCasesTest, TheDngStatesTheDescriptionsCodesAndCameraFacts)
{
    for (const fixtures::RawPolicyInput& input : fixtures::rawPolicyInputs())
    {
        const fixtures::FixtureImage& image = input.image;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        const std::filesystem::path path = m_directory / (image.name + ".dng");
        ASSERT_TRUE(fixtures::writeRawPolicyDng(input, *samples, path, error)) << error;
        const std::vector<std::uint8_t> bytes = bytesOf(path);
        ASSERT_GE(bytes.size(), 8u);
        EXPECT_EQ(little(bytes, 0u, 4u), 0x002A4949u) << "a little-endian TIFF";
        const Directory directory = readDirectory(bytes);
        const std::uint32_t channels = samples->channelCount();
        EXPECT_EQ(directory.values.at(256), image.width);
        EXPECT_EQ(directory.values.at(257), image.height);
        EXPECT_EQ(directory.values.at(262), channels == 1u ? 32803u : 34892u);
        EXPECT_EQ(directory.values.at(274), input.orientation);
        EXPECT_EQ(directory.values.at(277), channels);
        EXPECT_EQ(directory.values.at(50717), input.whiteLevel);
        EXPECT_EQ(directory.counts.count(33422), channels == 1u ? 1u : 0u) << "a filter pattern only on a mosaic";
        // The codes follow the directory, interleaved, two little-endian bytes each.
        const std::uint32_t strip = directory.values.at(273);
        ASSERT_EQ(bytes.size(), strip + std::size_t{image.width} * image.height * channels * 2u);
        for (std::uint32_t y = 0u; y < image.height; ++y)
        {
            for (std::uint32_t x = 0u; x < image.width; ++x)
            {
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const std::size_t at = strip + ((std::size_t{y} * image.width + x) * channels + channel) * 2u;
                    ASSERT_EQ(little(bytes, at, 2u), samples->value(x, y, channel))
                        << image.name << " at " << x << "," << y << "," << channel;
                }
            }
        }
    }
}

TEST_F(RawPolicyCasesTest, TheWriterRefusesSamplesThatAreNotTheInputs)
{
    const fixtures::RawPolicyInput& input = fixtures::rawPolicyInputs().front();
    const fixtures::FixtureSamples wrong(input.image.width + 1u, input.image.height, 1u,
                                         fixtures::FixtureSampleType::UInt16);
    std::string error;
    EXPECT_FALSE(fixtures::writeRawPolicyDng(input, wrong, m_directory / "wrong.dng", error));
    EXPECT_FALSE(error.empty());
}
