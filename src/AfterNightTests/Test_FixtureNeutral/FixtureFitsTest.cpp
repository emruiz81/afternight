// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/harness/TestHarness.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureCardKind;
using an::tests::fixtures::FixtureCfa;
using an::tests::fixtures::FixtureFitsFile;
using an::tests::fixtures::FixtureImage;
using an::tests::fixtures::FixtureSamples;
using an::tests::fixtures::FixtureSampleType;

constexpr std::size_t kRecord = 2880u;
constexpr std::size_t kCard = 80u;

class FixtureFitsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::optional<std::string> root = an::tests::harness::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
        ASSERT_TRUE(root.has_value());
        m_directory = std::filesystem::path(*root) / "fits";
        std::filesystem::create_directories(m_directory);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    std::filesystem::path written(const FixtureImage& image)
    {
        std::string error;
        const std::optional<FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        const std::filesystem::path path = m_directory / (image.name + ".fits");
        EXPECT_TRUE(an::tests::fixtures::writeFixtureFits(image, samples.value_or(FixtureSamples{}), path, error))
            << error;
        return path;
    }

    static std::vector<char> bytesOf(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    static void rewrite(const std::filesystem::path& path, const std::vector<char>& bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    // The header's cards, one string of 80 characters each, up to and including END.
    static std::vector<std::string> headerCards(const std::vector<char>& bytes)
    {
        std::vector<std::string> cards;
        for (std::size_t offset = 0u; offset + kCard <= bytes.size(); offset += kCard)
        {
            cards.emplace_back(bytes.data() + offset, kCard);
            if (cards.back().rfind("END", 0) == 0 && cards.back().find_first_not_of(' ', 3) == std::string::npos)
            {
                break;
            }
        }
        return cards;
    }

    static std::size_t dataOffset(const std::vector<std::string>& cards)
    {
        return (cards.size() * kCard + kRecord - 1u) / kRecord * kRecord;
    }

    static void replaceCardValue(std::vector<char>& bytes, const std::string& keyword, const std::string& value)
    {
        for (std::size_t offset = 0u; offset + kCard <= bytes.size(); offset += kCard)
        {
            if (std::string(bytes.data() + offset, 8u) == (keyword + std::string(8u - keyword.size(), ' ')))
            {
                std::string card = keyword + std::string(8u - keyword.size(), ' ') + "= " + value;
                card.resize(kCard, ' ');
                std::memcpy(bytes.data() + offset, card.data(), kCard);
                return;
            }
        }
        FAIL() << keyword << " is not in the header";
    }

    std::filesystem::path m_directory;
};

std::string trimmed(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(' ') - first + 1u);
}

const FixtureImage& fixture(const char* name)
{
    const FixtureImage* image = an::tests::fixtures::standardFixture(name);
    EXPECT_NE(image, nullptr) << name;
    static const FixtureImage empty;
    return image != nullptr ? *image : empty;
}

TEST_F(FixtureFitsTest, AFileIsWholeRecordsWithTheMandatoryCardsFirst)
{
    const std::vector<char> bytes = bytesOf(written(fixture("gradient-rgb-uint16")));
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes.size() % kRecord, 0u);
    const std::vector<std::string> cards = headerCards(bytes);
    ASSERT_GE(cards.size(), 6u);
    EXPECT_EQ(cards[0].substr(0u, 30u), "SIMPLE  =                    T");
    EXPECT_EQ(cards[1].substr(0u, 30u), "BITPIX  =                   16");
    EXPECT_EQ(cards[2].substr(0u, 30u), "NAXIS   =                    3");
    EXPECT_EQ(cards[3].substr(0u, 30u), "NAXIS1  =                    8");
    EXPECT_EQ(cards[4].substr(0u, 30u), "NAXIS2  =                    6");
    EXPECT_EQ(cards[5].substr(0u, 30u), "NAXIS3  =                    3");
    EXPECT_EQ(cards.back().substr(0u, 3u), "END");
    const std::size_t data = dataOffset(cards);
    for (std::size_t offset = cards.size() * kCard; offset < data; ++offset)
    {
        ASSERT_EQ(bytes[offset], ' ') << "the header is padded with spaces";
    }
    EXPECT_EQ(bytes.size() - data, (8u * 6u * 3u * 2u + kRecord - 1u) / kRecord * kRecord);
}

TEST_F(FixtureFitsTest, TheOffsetRepresentationsAreStoredBigEndianWithTheirCanonicalZero)
{
    struct Case
    {
        FixtureSampleType sampleType;
        double value;
        const char* bitpix;
        const char* bzero;
        std::vector<unsigned char> stored;
    };
    const Case cases[] = {
        {FixtureSampleType::UInt16, 1000.0, "16", "32768", {0x83, 0xE8}},
        {FixtureSampleType::Int8, -5.0, "8", "-128", {0x7B}},
        {FixtureSampleType::UInt32, 4000000000.0, "32", "2147483648", {0x6E, 0x6B, 0x28, 0x00}},
        {FixtureSampleType::Int16, -2.0, "16", "", {0xFF, 0xFE}},
        {FixtureSampleType::Float32, 0.5, "-32", "", {0x3F, 0x00, 0x00, 0x00}},
        {FixtureSampleType::Float64, -2.0, "-64", "", {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    };
    for (const Case& current : cases)
    {
        FixtureImage image;
        image.name = std::string("offset-") + std::string(an::tests::fixtures::sampleTypeName(current.sampleType));
        image.width = 1u;
        image.height = 1u;
        image.sampleType = current.sampleType;
        image.domain = an::tests::fixtures::FixtureDomain::SensorCode;
        image.levels = {{.base = current.value}};
        const std::vector<char> bytes = bytesOf(written(image));
        const std::vector<std::string> cards = headerCards(bytes);
        std::string bitpix;
        std::string bzero;
        for (const std::string& card : cards)
        {
            if (card.rfind("BITPIX  =", 0) == 0)
            {
                bitpix = card.substr(10u, 20u);
            }
            if (card.rfind("BZERO   =", 0) == 0)
            {
                bzero = card.substr(10u, 20u);
            }
        }
        EXPECT_EQ(trimmed(bitpix), current.bitpix) << image.name;
        if (std::string(current.bzero).empty())
        {
            EXPECT_TRUE(bzero.empty()) << image.name << " writes BZERO " << bzero;
        }
        else
        {
            EXPECT_EQ(trimmed(bzero), current.bzero) << image.name;
        }
        const std::size_t data = dataOffset(cards);
        ASSERT_GE(bytes.size(), data + current.stored.size()) << image.name;
        for (std::size_t index = 0u; index < current.stored.size(); ++index)
        {
            EXPECT_EQ(static_cast<unsigned char>(bytes[data + index]), current.stored[index])
                << image.name << " byte " << index;
        }
    }
}

TEST_F(FixtureFitsTest, EveryStandardFixtureReadsBackExactlyWithItsCardsAndFilterArray)
{
    ASSERT_FALSE(an::tests::fixtures::standardFixtures().empty());
    for (const FixtureImage& image : an::tests::fixtures::standardFixtures())
    {
        std::string error;
        const std::optional<FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        const std::optional<FixtureFitsFile> file = an::tests::fixtures::readFixtureFits(written(image), error);
        ASSERT_TRUE(file.has_value()) << image.name << ": " << error;
        EXPECT_TRUE(an::tests::fixtures::sameSamples(file->samples, *samples)) << image.name;
        EXPECT_EQ(file->cfa, image.cfa) << image.name;
        EXPECT_EQ(file->card("ROWORDER").value_or(""), "TOP-DOWN") << image.name;
        for (const auto& card : image.cards)
        {
            EXPECT_EQ(file->card(card.keyword).value_or("<missing>"), card.value) << image.name << " " << card.keyword;
        }
    }
}

TEST_F(FixtureFitsTest, ABottomUpFileIsReadTopDown)
{
    const FixtureImage& image = fixture("gradient-rgb-uint16");
    const std::filesystem::path path = written(image);
    std::vector<char> bytes = bytesOf(path);
    replaceCardValue(bytes, "ROWORDER", "'BOTTOM-UP'");
    rewrite(path, bytes);
    std::string error;
    const std::optional<FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    ASSERT_TRUE(samples.has_value()) << error;
    const std::optional<FixtureFitsFile> file = an::tests::fixtures::readFixtureFits(path, error);
    ASSERT_TRUE(file.has_value()) << error;
    ASSERT_EQ(file->samples.height(), samples->height());
    ASSERT_EQ(file->samples.width(), samples->width());
    ASSERT_EQ(file->samples.channelCount(), samples->channelCount());
    for (std::uint32_t channel = 0u; channel < samples->channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < samples->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples->width(); ++x)
            {
                EXPECT_EQ(file->samples.value(x, y, channel), samples->value(x, samples->height() - 1u - y, channel));
            }
        }
    }
}

TEST_F(FixtureFitsTest, ABottomUpFileStoresTheLastRowFirstAndSaysSo)
{
    const FixtureImage& image = fixture("gradient-rgb-uint16");
    std::string error;
    const std::optional<FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    ASSERT_TRUE(samples.has_value()) << error;
    const std::filesystem::path path = m_directory / "bottom-up.fits";
    ASSERT_TRUE(an::tests::fixtures::writeFixtureFits(image, *samples, path, error,
                                                      an::tests::fixtures::FixtureRowOrder::BottomUp))
        << error;
    const std::vector<char> bytes = bytesOf(path);
    const std::vector<std::string> cards = headerCards(bytes);
    bool bottomUp = false;
    for (const std::string& card : cards)
    {
        bottomUp = bottomUp || card.rfind("ROWORDER= 'BOTTOM-UP'", 0) == 0;
    }
    EXPECT_TRUE(bottomUp);
    // The first stored sample is the description's last row, first column: red, 1000 + 1000 * 5, less the offset.
    const std::size_t data = dataOffset(cards);
    const int stored = static_cast<std::int16_t>((static_cast<unsigned char>(bytes[data]) << 8) |
                                                 static_cast<unsigned char>(bytes[data + 1u]));
    EXPECT_EQ(stored + 32768, static_cast<int>(samples->value(0u, image.height - 1u, 0u)));
    const std::optional<FixtureFitsFile> file = an::tests::fixtures::readFixtureFits(path, error);
    ASSERT_TRUE(file.has_value()) << error;
    EXPECT_TRUE(an::tests::fixtures::sameSamples(file->samples, *samples));
}

TEST_F(FixtureFitsTest, AScaledOrTruncatedFileIsRefusedWithItsReason)
{
    const FixtureImage& image = fixture("gradient-rgb-uint16");
    const std::filesystem::path path = written(image);
    const std::vector<char> original = bytesOf(path);

    std::vector<char> scaled = original;
    replaceCardValue(scaled, "BSCALE", "2");
    rewrite(path, scaled);
    std::string error;
    EXPECT_FALSE(an::tests::fixtures::readFixtureFits(path, error).has_value());
    EXPECT_NE(error.find("BSCALE"), std::string::npos) << error;

    std::vector<char> offset = original;
    replaceCardValue(offset, "BZERO", "1000");
    rewrite(path, offset);
    error.clear();
    EXPECT_FALSE(an::tests::fixtures::readFixtureFits(path, error).has_value());
    EXPECT_NE(error.find("BZERO"), std::string::npos) << error;

    std::vector<char> truncated(original.begin(), original.end() - static_cast<std::ptrdiff_t>(kRecord));
    rewrite(path, truncated);
    error.clear();
    EXPECT_FALSE(an::tests::fixtures::readFixtureFits(path, error).has_value());
    EXPECT_FALSE(error.empty());

    rewrite(path, std::vector<char>(kRecord, ' '));
    error.clear();
    EXPECT_FALSE(an::tests::fixtures::readFixtureFits(path, error).has_value());
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(an::tests::fixtures::readFixtureFits(m_directory / "absent.fits", error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST_F(FixtureFitsTest, TextCardsAreQuotedAndReadBackAsWritten)
{
    FixtureImage image = fixture("constant-mono-uint8");
    image.name = "quoted";
    image.cards = {{"OBJECT", "O'Brien's field", "a quote inside", FixtureCardKind::Text},
                   {"FILTER", "Ha", "", FixtureCardKind::Text},
                   {"FLIPPED", "F", "", FixtureCardKind::Logical},
                   {"GAIN", "-12", "", FixtureCardKind::Integer},
                   {"EXPTIME", "0.001", "seconds", FixtureCardKind::Real}};
    const std::filesystem::path path = written(image);
    const std::vector<std::string> cards = headerCards(bytesOf(path));
    bool quoted = false;
    for (const std::string& card : cards)
    {
        quoted = quoted || card.rfind("OBJECT  = 'O''Brien''s field'", 0) == 0;
    }
    EXPECT_TRUE(quoted);
    std::string error;
    const std::optional<FixtureFitsFile> file = an::tests::fixtures::readFixtureFits(path, error);
    ASSERT_TRUE(file.has_value()) << error;
    for (const auto& card : image.cards)
    {
        EXPECT_EQ(file->card(card.keyword).value_or("<missing>"), card.value) << card.keyword;
    }
    EXPECT_FALSE(file->card("NOSUCHKEY").has_value());
}

TEST_F(FixtureFitsTest, SamplesThatAreNotTheDescriptionsAreRefused)
{
    const FixtureImage& image = fixture("gradient-rgb-uint16");
    const FixtureSamples wrong(image.width, image.height, 1u, image.sampleType);
    std::string error;
    EXPECT_FALSE(an::tests::fixtures::writeFixtureFits(image, wrong, m_directory / "wrong.fits", error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(m_directory / "wrong.fits"));
}

} // namespace
