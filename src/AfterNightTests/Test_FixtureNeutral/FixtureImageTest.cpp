// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureDigest.h"
#include "fixtures/common/FixtureImage.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureCardKind;
using an::tests::fixtures::FixtureCfa;
using an::tests::fixtures::FixtureChannelLevels;
using an::tests::fixtures::FixtureChannels;
using an::tests::fixtures::FixtureDomain;
using an::tests::fixtures::FixtureImage;
using an::tests::fixtures::FixturePattern;
using an::tests::fixtures::FixtureSamples;
using an::tests::fixtures::FixtureSampleType;

FixtureImage gradient(FixtureSampleType sampleType, std::uint32_t width, std::uint32_t height, double base,
                      double perColumn, double perRow)
{
    FixtureImage image;
    image.name = "gradient";
    image.width = width;
    image.height = height;
    image.sampleType = sampleType;
    image.domain =
        an::tests::fixtures::isFloating(sampleType) ? FixtureDomain::NormalizedLinear : FixtureDomain::SensorCode;
    image.pattern = FixturePattern::Gradient;
    image.levels = {FixtureChannelLevels{.base = base, .perColumn = perColumn, .perRow = perRow}};
    return image;
}

FixtureSamples materialized(const FixtureImage& image)
{
    std::string error;
    const std::optional<FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples.value_or(FixtureSamples{});
}

TEST(FixtureImageTest, AGradientIsItsBasePlusItsSlopesAtEveryPixel)
{
    const FixtureSamples samples = materialized(gradient(FixtureSampleType::UInt16, 4u, 3u, 1000.0, 500.0, 1000.0));
    ASSERT_EQ(samples.width(), 4u);
    ASSERT_EQ(samples.height(), 3u);
    ASSERT_EQ(samples.channelCount(), 1u);
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_EQ(samples.value(x, y, 0u), 1000.0 + 500.0 * x + 1000.0 * y) << x << "," << y;
        }
    }
}

TEST(FixtureImageTest, EveryChannelTakesItsOwnLevels)
{
    FixtureImage image;
    image.name = "constant";
    image.width = 3u;
    image.height = 2u;
    image.channels = FixtureChannels::Rgb;
    image.sampleType = FixtureSampleType::Float64;
    image.pattern = FixturePattern::Constant;
    image.levels = {{.base = 0.25}, {.base = -1.5}, {.base = 1e6}};
    const FixtureSamples samples = materialized(image);
    ASSERT_EQ(samples.channelCount(), 3u);
    EXPECT_EQ(samples.value(2u, 1u, 0u), 0.25);
    EXPECT_EQ(samples.value(0u, 0u, 1u), -1.5);
    EXPECT_EQ(samples.value(1u, 1u, 2u), 1e6);
}

TEST(FixtureImageTest, AnIntegerSampleRoundsToNearestWithTiesToEvenAndSaturates)
{
    using an::tests::fixtures::quantizeSample;
    EXPECT_EQ(quantizeSample(2.5, FixtureSampleType::UInt8), 2.0);
    EXPECT_EQ(quantizeSample(3.5, FixtureSampleType::UInt8), 4.0);
    EXPECT_EQ(quantizeSample(-2.5, FixtureSampleType::Int8), -2.0);
    EXPECT_EQ(quantizeSample(2.4999, FixtureSampleType::Int16), 2.0);
    EXPECT_EQ(quantizeSample(255.6, FixtureSampleType::UInt8), 255.0);
    EXPECT_EQ(quantizeSample(-3.0, FixtureSampleType::UInt16), 0.0);
    EXPECT_EQ(quantizeSample(70000.0, FixtureSampleType::UInt16), 65535.0);
    EXPECT_EQ(quantizeSample(-200.0, FixtureSampleType::Int8), -128.0);
    EXPECT_EQ(quantizeSample(1e12, FixtureSampleType::Int32), 2147483647.0);
    EXPECT_EQ(quantizeSample(-1e12, FixtureSampleType::Int32), -2147483648.0);
    EXPECT_EQ(quantizeSample(1e12, FixtureSampleType::UInt32), 4294967295.0);
    EXPECT_EQ(quantizeSample(0.1, FixtureSampleType::Float32), static_cast<double>(0.1f));
    EXPECT_EQ(quantizeSample(0.1, FixtureSampleType::Float64), 0.1);
}

TEST(FixtureImageTest, AGradientIsQuantizedOncePerSample)
{
    const FixtureSamples samples = materialized(gradient(FixtureSampleType::Int16, 4u, 1u, -1.0, 0.5, 0.0));
    EXPECT_EQ(samples.value(0u, 0u, 0u), -1.0);
    EXPECT_EQ(samples.value(1u, 0u, 0u), 0.0); // -0.5 ties to even
    EXPECT_EQ(samples.value(2u, 0u, 0u), 0.0);
    EXPECT_EQ(samples.value(3u, 0u, 0u), 0.0); // 0.5 ties to even
}

TEST(FixtureImageTest, ACheckerAlternatesByCell)
{
    FixtureImage image = gradient(FixtureSampleType::UInt8, 4u, 4u, 10.0, 0.0, 0.0);
    image.pattern = FixturePattern::Checker;
    image.checkerCell = 2u;
    image.levels[0].alternate = 20.0;
    const FixtureSamples samples = materialized(image);
    EXPECT_EQ(samples.value(0u, 0u, 0u), 10.0);
    EXPECT_EQ(samples.value(1u, 1u, 0u), 10.0);
    EXPECT_EQ(samples.value(2u, 0u, 0u), 20.0);
    EXPECT_EQ(samples.value(0u, 2u, 0u), 20.0);
    EXPECT_EQ(samples.value(2u, 2u, 0u), 10.0);
    EXPECT_EQ(samples.value(3u, 3u, 0u), 10.0);
}

TEST(FixtureImageTest, NoiseDependsOnItsSeedAndStaysInsideItsAmplitude)
{
    FixtureImage image = gradient(FixtureSampleType::Float64, 16u, 8u, 0.5, 0.0, 0.0);
    image.pattern = FixturePattern::Noise;
    image.seed = 11u;
    image.levels[0].amplitude = 0.25;
    const FixtureSamples first = materialized(image);
    const FixtureSamples again = materialized(image);
    EXPECT_TRUE(an::tests::fixtures::sameSamples(first, again));
    std::set<double> distinct;
    for (std::uint32_t y = 0u; y < 8u; ++y)
    {
        for (std::uint32_t x = 0u; x < 16u; ++x)
        {
            const double value = first.value(x, y, 0u);
            EXPECT_GE(value, 0.25);
            EXPECT_LT(value, 0.75);
            distinct.insert(value);
        }
    }
    EXPECT_GT(distinct.size(), 100u);
    image.seed = 12u;
    EXPECT_FALSE(an::tests::fixtures::sameSamples(first, materialized(image)));
}

TEST(FixtureImageTest, AGradientWithNoiseIsTheGradientPlusTheSameSeededNoise)
{
    FixtureImage noisy = gradient(FixtureSampleType::Float64, 9u, 5u, 0.2, 0.03, -0.01);
    noisy.pattern = FixturePattern::GradientNoise;
    noisy.seed = 5u;
    noisy.levels[0].amplitude = 0.05;
    FixtureImage noiseOnly = noisy;
    noiseOnly.pattern = FixturePattern::Noise;
    const FixtureSamples samples = materialized(noisy);
    const FixtureSamples noise = materialized(noiseOnly);
    bool varied = false;
    for (std::uint32_t y = 0u; y < 5u; ++y)
    {
        for (std::uint32_t x = 0u; x < 9u; ++x)
        {
            const double slope = 0.03 * static_cast<double>(x) - 0.01 * static_cast<double>(y);
            EXPECT_DOUBLE_EQ(samples.value(x, y, 0u), noise.value(x, y, 0u) + slope) << x << "," << y;
            varied = varied || noise.value(x, y, 0u) != 0.2;
        }
    }
    EXPECT_TRUE(varied);
}

TEST(FixtureImageTest, AStarFieldAddsEachStarsProfileScaledByItsChannel)
{
    FixtureImage field;
    field.name = "field";
    field.width = 12u;
    field.height = 9u;
    field.channels = FixtureChannels::Rgb;
    field.sampleType = FixtureSampleType::Float64;
    field.pattern = FixturePattern::StarField;
    field.levels = {FixtureChannelLevels{.base = 0.1, .alternate = 1.0},
                    FixtureChannelLevels{.base = 0.1, .alternate = 0.5},
                    FixtureChannelLevels{.base = 0.2, .alternate = 0.0}};
    field.stars = {{.x = 4.0, .y = 3.0, .width = 2.0, .peak = 0.8}, {.x = 9.5, .y = 6.0, .width = 1.0, .peak = 0.4}};
    const FixtureSamples samples = materialized(field);
    const auto profile = [](double dx, double dy, double width, double peak) {
        const double spread = 1.0 + (dx * dx + dy * dy) / (width * width);
        return peak / (spread * spread);
    };
    for (std::uint32_t y = 0u; y < field.height; ++y)
    {
        for (std::uint32_t x = 0u; x < field.width; ++x)
        {
            const double stars = profile(static_cast<double>(x) - 4.0, static_cast<double>(y) - 3.0, 2.0, 0.8) +
                                 profile(static_cast<double>(x) - 9.5, static_cast<double>(y) - 6.0, 1.0, 0.4);
            EXPECT_DOUBLE_EQ(samples.value(x, y, 0u), 0.1 + stars) << x << "," << y;
            EXPECT_DOUBLE_EQ(samples.value(x, y, 1u), 0.1 + 0.5 * stars) << x << "," << y;
            EXPECT_DOUBLE_EQ(samples.value(x, y, 2u), 0.2) << x << "," << y;
        }
    }
    EXPECT_DOUBLE_EQ(samples.value(4u, 3u, 0u), 0.1 + 0.8 + profile(-5.5, -3.0, 1.0, 0.4));
}

TEST(FixtureImageTest, AStarNeedsAStarFieldAPositiveWidthAndFiniteParameters)
{
    FixtureImage field = gradient(FixtureSampleType::Float32, 8u, 8u, 0.1, 0.0, 0.0);
    field.pattern = FixturePattern::StarField;
    field.stars = {{.x = 2.0, .y = 2.0, .width = 1.5, .peak = 0.5}};
    EXPECT_FALSE(an::tests::fixtures::checkFixture(field).has_value());

    FixtureImage onGradient = field;
    onGradient.pattern = FixturePattern::Gradient;
    EXPECT_TRUE(an::tests::fixtures::checkFixture(onGradient).has_value());

    FixtureImage flat = field;
    flat.stars[0].width = 0.0;
    EXPECT_TRUE(an::tests::fixtures::checkFixture(flat).has_value());

    FixtureImage infinite = field;
    infinite.stars[0].peak = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(an::tests::fixtures::checkFixture(infinite).has_value());
}

TEST(FixtureImageTest, AnOverrideReplacesTheSampleAfterThePattern)
{
    FixtureImage image = gradient(FixtureSampleType::Float32, 4u, 3u, 0.25, 0.0, 0.0);
    image.pattern = FixturePattern::Constant;
    image.overrides = {{.x = 1u, .y = 1u, .channel = 0u, .value = std::numeric_limits<double>::quiet_NaN()},
                       {.x = 2u, .y = 0u, .channel = 0u, .value = std::numeric_limits<double>::infinity()},
                       {.x = 3u, .y = 2u, .channel = 0u, .value = -std::numeric_limits<double>::infinity()},
                       {.x = 0u, .y = 2u, .channel = 0u, .value = -0.0}};
    const FixtureSamples samples = materialized(image);
    EXPECT_TRUE(std::isnan(samples.value(1u, 1u, 0u)));
    EXPECT_EQ(samples.value(2u, 0u, 0u), std::numeric_limits<double>::infinity());
    EXPECT_EQ(samples.value(3u, 2u, 0u), -std::numeric_limits<double>::infinity());
    EXPECT_EQ(samples.value(0u, 2u, 0u), 0.0);
    EXPECT_TRUE(std::signbit(samples.value(0u, 2u, 0u)));
    EXPECT_EQ(samples.value(0u, 0u, 0u), 0.25);
}

TEST(FixtureImageTest, ADescriptionBothEnginesCannotHoldIsRefusedWithItsReason)
{
    const FixtureImage valid = gradient(FixtureSampleType::UInt16, 4u, 3u, 1.0, 1.0, 1.0);
    ASSERT_FALSE(an::tests::fixtures::checkFixture(valid).has_value());

    std::vector<std::pair<std::string, FixtureImage>> refused;
    auto variant = [&](const std::string& why, auto change) {
        FixtureImage image = valid;
        change(image);
        refused.emplace_back(why, image);
    };
    variant("no name", [](FixtureImage& image) { image.name.clear(); });
    variant("no width", [](FixtureImage& image) { image.width = 0u; });
    variant("no height", [](FixtureImage& image) { image.height = 0u; });
    variant("levels for two channels on a mono image", [](FixtureImage& image) { image.levels.push_back({}); });
    variant("a non-finite slope",
            [](FixtureImage& image) { image.levels[0].perRow = std::numeric_limits<double>::infinity(); });
    variant("a zero checker cell", [](FixtureImage& image) {
        image.pattern = FixturePattern::Checker;
        image.checkerCell = 0u;
    });
    variant("a physical domain on integers", [](FixtureImage& image) { image.domain = FixtureDomain::PhysicalScaled; });
    variant("a filter array on colour", [](FixtureImage& image) {
        image.channels = FixtureChannels::Rgb;
        image.levels = {{}, {}, {}};
        image.cfa = FixtureCfa::Rggb;
    });
    variant("an override outside the image", [](FixtureImage& image) { image.overrides = {{.x = 4u}}; });
    variant("a NaN integer",
            [](FixtureImage& image) { image.overrides = {{.value = std::numeric_limits<double>::quiet_NaN()}}; });
    variant("a reserved keyword",
            [](FixtureImage& image) { image.cards = {{"BITPIX", "8", "", FixtureCardKind::Integer}}; });
    variant("ROWORDER",
            [](FixtureImage& image) { image.cards = {{"ROWORDER", "BOTTOM-UP", "", FixtureCardKind::Text}}; });
    variant("a long keyword",
            [](FixtureImage& image) { image.cards = {{"EXPOSURETIME", "1", "", FixtureCardKind::Real}}; });
    variant("a lower-case keyword",
            [](FixtureImage& image) { image.cards = {{"exptime", "1", "", FixtureCardKind::Real}}; });
    variant("a text card that is not ASCII",
            [](FixtureImage& image) { image.cards = {{"OBJECT", "M\xC3\xA9", "", FixtureCardKind::Text}}; });
    variant("a real card that is not a number",
            [](FixtureImage& image) { image.cards = {{"EXPTIME", "long", "", FixtureCardKind::Real}}; });
    variant("a logical card that is not T or F",
            [](FixtureImage& image) { image.cards = {{"FLIPPED", "yes", "", FixtureCardKind::Logical}}; });

    for (const auto& [why, image] : refused)
    {
        const std::optional<std::string> reason = an::tests::fixtures::checkFixture(image);
        EXPECT_TRUE(reason.has_value() && !reason->empty()) << why;
        std::string error;
        EXPECT_FALSE(an::tests::fixtures::materializeFixture(image, error).has_value()) << why;
        EXPECT_FALSE(error.empty()) << why;
    }
}

TEST(FixtureImageTest, TheCanonicalTextIsStableAndChangesWithEveryField)
{
    FixtureImage base = gradient(FixtureSampleType::UInt16, 4u, 3u, 1.0, 2.0, 3.0);
    base.cards = {{"EXPTIME", "120", "seconds", FixtureCardKind::Real}};
    base.overrides = {{.x = 1u, .y = 1u, .channel = 0u, .value = 7.0}};
    const std::string text = an::tests::fixtures::describeFixture(base);
    EXPECT_EQ(text, an::tests::fixtures::describeFixture(base));
    EXPECT_EQ(an::tests::fixtures::fixtureDigest(base), an::tests::fixtures::sha256Hex(text));

    std::vector<FixtureImage> changed(17u, base);
    changed[15].pattern = FixturePattern::StarField;
    changed[16].pattern = FixturePattern::StarField;
    changed[16].stars = {{.x = 1.0, .y = 1.0, .width = 1.0, .peak = 2.0}};
    changed[0].name = "other";
    changed[1].width = 5u;
    changed[2].height = 4u;
    changed[3].sampleType = FixtureSampleType::Int16;
    changed[4].domain = FixtureDomain::NormalizedLinear;
    changed[5].cfa = FixtureCfa::Gbrg;
    changed[6].pattern = FixturePattern::Noise;
    changed[7].checkerCell = 3u;
    changed[8].seed = 9u;
    changed[9].levels[0].perRow = 3.0000000000000004;
    changed[10].levels[0].amplitude = 1.0;
    changed[11].overrides[0].value = 8.0;
    changed[12].cards[0].value = "121";
    changed[13].cards[0].comment = "s";
    changed[14].cards[0].kind = FixtureCardKind::Integer;
    std::set<std::string> texts{text};
    for (const FixtureImage& image : changed)
    {
        texts.insert(an::tests::fixtures::describeFixture(image));
    }
    EXPECT_EQ(texts.size(), changed.size() + 1u);
}

TEST(FixtureImageTest, TheSamplesDigestIsTheSha256OfTheirLittleEndianPlanarBytes)
{
    FixtureSamples samples(2u, 1u, 1u, FixtureSampleType::UInt16);
    samples.setValue(0u, 0u, 0u, 0x0102);
    samples.setValue(1u, 0u, 0u, 0x0304);
    const std::byte expected[] = {std::byte{0x02}, std::byte{0x01}, std::byte{0x04}, std::byte{0x03}};
    ASSERT_EQ(samples.bytes().size(), 4u);
    EXPECT_EQ(std::memcmp(samples.bytes().data(), expected, 4u), 0);
    EXPECT_EQ(samples.digest(), an::tests::fixtures::sha256Hex(std::span<const std::byte>(expected)));
}

TEST(FixtureImageTest, SameSamplesMatchesAnyNanAndTellsTheZerosApart)
{
    FixtureSamples left(2u, 1u, 1u, FixtureSampleType::Float32);
    FixtureSamples right(2u, 1u, 1u, FixtureSampleType::Float32);
    left.setValue(0u, 0u, 0u, std::numeric_limits<double>::quiet_NaN());
    right.setValue(0u, 0u, 0u, -std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(an::tests::fixtures::sameSamples(left, right));
    right.setValue(1u, 0u, 0u, -0.0);
    EXPECT_FALSE(an::tests::fixtures::sameSamples(left, right));
    EXPECT_FALSE(an::tests::fixtures::sameSamples(left, FixtureSamples(2u, 1u, 1u, FixtureSampleType::Float64)));
    EXPECT_FALSE(an::tests::fixtures::sameSamples(left, FixtureSamples(1u, 2u, 1u, FixtureSampleType::Float32)));
}

TEST(FixtureImageTest, ADoubleIsWrittenSoItReadsBackExactly)
{
    const double values[] = {
        0.1, -0.0, 3.0, 1e-310, std::numeric_limits<double>::max(), -123456.789e-30, static_cast<double>(0.1f)};
    for (const double value : values)
    {
        const std::optional<double> parsed = an::tests::fixtures::parseDouble(an::tests::fixtures::formatDouble(value));
        ASSERT_TRUE(parsed.has_value()) << an::tests::fixtures::formatDouble(value);
        EXPECT_EQ(std::bit_cast<std::uint64_t>(*parsed), std::bit_cast<std::uint64_t>(value));
    }
    EXPECT_EQ(an::tests::fixtures::formatDouble(std::numeric_limits<double>::quiet_NaN()), "nan");
    EXPECT_EQ(an::tests::fixtures::formatDouble(std::numeric_limits<double>::infinity()), "inf");
    EXPECT_EQ(an::tests::fixtures::formatDouble(-std::numeric_limits<double>::infinity()), "-inf");
    EXPECT_TRUE(std::isnan(an::tests::fixtures::parseDouble("nan").value_or(0.0)));
    EXPECT_FALSE(an::tests::fixtures::parseDouble("1.5x").has_value());
    EXPECT_FALSE(an::tests::fixtures::parseDouble("").has_value());
}

TEST(FixtureImageTest, ANanOtherThanTheQuietOneKeepsItsSignAndPayloadThroughText)
{
    // The quiet NaN an x86 processor returns for 0/0 has its sign bit set, and a record whose samples read back as
    // another NaN would no longer reproduce the digest it states.
    const std::uint64_t patterns[] = {0xFFF8000000000000ull, 0x7FF8000000000123ull, 0xFFF0000000000001ull};
    for (const std::uint64_t pattern : patterns)
    {
        const double value = std::bit_cast<double>(pattern);
        const std::string text = an::tests::fixtures::formatDouble(value);
        const std::optional<double> parsed = an::tests::fixtures::parseDouble(text);
        ASSERT_TRUE(parsed.has_value()) << text;
        EXPECT_EQ(std::bit_cast<std::uint64_t>(*parsed), pattern) << text;
    }
    FixtureSamples samples(1u, 1u, 1u, FixtureSampleType::Float32);
    samples.setValue(0u, 0u, 0u, static_cast<double>(std::bit_cast<float>(0xFFC00000u)));
    FixtureSamples reread(1u, 1u, 1u, FixtureSampleType::Float32);
    reread.setValue(
        0u, 0u, 0u,
        an::tests::fixtures::parseDouble(an::tests::fixtures::formatDouble(samples.value(0u, 0u, 0u))).value_or(0.0));
    EXPECT_EQ(reread.digest(), samples.digest());
    EXPECT_FALSE(an::tests::fixtures::parseDouble("nan(0x1)").has_value());
    EXPECT_FALSE(an::tests::fixtures::parseDouble("nan(0x3ff0000000000000)").has_value());
}

TEST(FixtureImageTest, TheStandardFixturesAreValidNamedOnceAndCoverWhatTheTestStrategyAsks)
{
    const std::vector<FixtureImage>& fixtures = an::tests::fixtures::standardFixtures();
    ASSERT_GE(fixtures.size(), 10u);
    std::set<std::string> names;
    std::set<FixtureSampleType> types;
    bool colour = false;
    bool mosaic = false;
    bool physical = false;
    bool nonFinite = false;
    bool negativeZero = false;
    bool negativeSigned = false;
    bool saturated = false;
    bool carded = false;
    for (const FixtureImage& image : fixtures)
    {
        EXPECT_TRUE(names.insert(image.name).second) << image.name;
        const std::optional<std::string> reason = an::tests::fixtures::checkFixture(image);
        EXPECT_FALSE(reason.has_value()) << image.name << ": " << reason.value_or("");
        EXPECT_EQ(an::tests::fixtures::standardFixture(image.name), &image);
        const FixtureSamples samples = materialized(image);
        types.insert(image.sampleType);
        colour = colour || image.channels == FixtureChannels::Rgb;
        mosaic = mosaic || image.cfa != FixtureCfa::None;
        physical = physical || image.domain == FixtureDomain::PhysicalScaled;
        carded = carded || !image.cards.empty();
        for (std::uint32_t channel = 0u; channel < samples.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < samples.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < samples.width(); ++x)
                {
                    const double value = samples.value(x, y, channel);
                    nonFinite = nonFinite || !std::isfinite(value);
                    negativeZero = negativeZero || (value == 0.0 && std::signbit(value));
                    negativeSigned =
                        negativeSigned || (!an::tests::fixtures::isFloating(image.sampleType) && value < 0.0);
                    saturated = saturated || (!an::tests::fixtures::isFloating(image.sampleType) &&
                                              (value == an::tests::fixtures::highestOf(image.sampleType) ||
                                               value == an::tests::fixtures::lowestOf(image.sampleType)));
                }
            }
        }
    }
    EXPECT_EQ(types.size(), 8u);
    EXPECT_TRUE(colour);
    EXPECT_TRUE(mosaic);
    EXPECT_TRUE(physical);
    EXPECT_TRUE(nonFinite);
    EXPECT_TRUE(negativeZero);
    EXPECT_TRUE(negativeSigned);
    EXPECT_TRUE(saturated);
    EXPECT_TRUE(carded);
    EXPECT_EQ(an::tests::fixtures::standardFixture("no such fixture"), nullptr);
}

TEST(FixtureImageTest, TheBehaviorFixturesAreValidNamedOnceAndApartFromTheStandardOnes)
{
    const std::vector<FixtureImage>& fixtures = an::tests::fixtures::behaviorFixtures();
    ASSERT_GE(fixtures.size(), 8u);
    std::set<std::string> names;
    bool stars = false;
    bool colour = false;
    bool mosaic = false;
    bool unbounded = false;
    bool signedIntegers = false;
    for (const FixtureImage& image : fixtures)
    {
        EXPECT_TRUE(names.insert(image.name).second) << image.name;
        EXPECT_EQ(an::tests::fixtures::standardFixture(image.name), nullptr) << image.name;
        const std::optional<std::string> reason = an::tests::fixtures::checkFixture(image);
        EXPECT_FALSE(reason.has_value()) << image.name << ": " << reason.value_or("");
        EXPECT_EQ(an::tests::fixtures::behaviorFixture(image.name), &image);
        EXPECT_EQ(an::tests::fixtures::fixtureNamed(image.name), &image);
        const FixtureSamples samples = materialized(image);
        stars = stars || !image.stars.empty();
        colour = colour || image.channels == FixtureChannels::Rgb;
        mosaic = mosaic || image.cfa != FixtureCfa::None;
        signedIntegers = signedIntegers || image.sampleType == FixtureSampleType::Int16;
        for (std::uint32_t channel = 0u; channel < samples.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < samples.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < samples.width(); ++x)
                {
                    const double value = samples.value(x, y, channel);
                    unbounded = unbounded ||
                                (an::tests::fixtures::isFloating(image.sampleType) && (value < 0.0 || value > 1.0));
                }
            }
        }
    }
    EXPECT_TRUE(stars);
    EXPECT_TRUE(colour);
    EXPECT_TRUE(mosaic);
    EXPECT_TRUE(unbounded);
    EXPECT_TRUE(signedIntegers);
    const FixtureImage* standard = an::tests::fixtures::fixtureNamed("gradient-rgb-uint16");
    ASSERT_NE(standard, nullptr);
    EXPECT_EQ(standard, an::tests::fixtures::standardFixture("gradient-rgb-uint16"));
    EXPECT_EQ(an::tests::fixtures::behaviorFixture("no such fixture"), nullptr);
    EXPECT_EQ(an::tests::fixtures::fixtureNamed("no such fixture"), nullptr);
}

} // namespace
