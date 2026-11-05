// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FixtureCatalogue.h"

#include <limits>

namespace an::tests::fixtures
{

namespace
{

FixtureImage describe(std::string name, std::uint32_t width, std::uint32_t height, FixtureChannels channels,
                      FixtureSampleType sampleType, FixtureDomain domain, FixturePattern pattern,
                      std::vector<FixtureChannelLevels> levels)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = width;
    image.height = height;
    image.channels = channels;
    image.sampleType = sampleType;
    image.domain = domain;
    image.pattern = pattern;
    image.levels = std::move(levels);
    return image;
}

std::vector<FixtureImage> build()
{
    std::vector<FixtureImage> fixtures;

    // The smallest camera output: one constant level.
    fixtures.push_back(describe("constant-mono-uint8", 5u, 4u, FixtureChannels::Mono, FixtureSampleType::UInt8,
                                FixtureDomain::SensorCode, FixturePattern::Constant, {{.base = 200.0}}));

    // What most cameras write: sixteen-bit colour, one channel running into the top of its range, with the cards a
    // capture program writes.
    FixtureImage colour = describe("gradient-rgb-uint16", 8u, 6u, FixtureChannels::Rgb, FixtureSampleType::UInt16,
                                   FixtureDomain::SensorCode, FixturePattern::Gradient,
                                   {{.base = 1000.0, .perColumn = 500.0, .perRow = 1000.0},
                                    {.base = 2000.0, .perColumn = -100.0, .perRow = 250.0},
                                    {.base = 60000.0, .perColumn = 1000.0, .perRow = 500.0}});
    colour.cards = {{"OBJECT", "Fixture Field", "target name", FixtureCardKind::Text},
                    {"EXPTIME", "120", "exposure in seconds", FixtureCardKind::Real},
                    {"GAIN", "100", "sensor gain", FixtureCardKind::Integer},
                    {"FILTER", "L", "filter name", FixtureCardKind::Text}};
    fixtures.push_back(std::move(colour));

    // Signed samples on both sides of zero.
    FixtureImage signedChecker =
        describe("checker-mono-int16", 7u, 5u, FixtureChannels::Mono, FixtureSampleType::Int16,
                 FixtureDomain::SensorCode, FixturePattern::Checker, {{.base = -12000.0, .alternate = 23456.0}});
    signedChecker.checkerCell = 2u;
    fixtures.push_back(std::move(signedChecker));

    // Normalized colour noise, seeded, with a different level per channel.
    FixtureImage noise = describe(
        "noise-rgb-float32", 6u, 4u, FixtureChannels::Rgb, FixtureSampleType::Float32, FixtureDomain::NormalizedLinear,
        FixturePattern::Noise,
        {{.base = 0.5, .amplitude = 0.25}, {.base = 0.25, .amplitude = 0.2}, {.base = 0.75, .amplitude = 0.2}});
    noise.seed = 7u;
    fixtures.push_back(std::move(noise));

    // A physical quantity: floats far outside zero to one, and negative.
    FixtureImage physical =
        describe("gradient-mono-float64-physical", 6u, 5u, FixtureChannels::Mono, FixtureSampleType::Float64,
                 FixtureDomain::PhysicalScaled, FixturePattern::Gradient,
                 {{.base = -1500.25, .perColumn = 1234.5, .perRow = -87.125}});
    physical.cards = {{"EXPTIME", "300", "exposure in seconds", FixtureCardKind::Real}};
    fixtures.push_back(std::move(physical));

    // Signed eight-bit samples, saturated at both ends of their range.
    FixtureImage signedNoise =
        describe("noise-mono-int8", 6u, 3u, FixtureChannels::Mono, FixtureSampleType::Int8, FixtureDomain::SensorCode,
                 FixturePattern::Noise, {{.base = 0.0, .amplitude = 160.0}});
    signedNoise.seed = 3u;
    fixtures.push_back(std::move(signedNoise));

    // Thirty-two-bit unsigned samples, saturated at the top and far below it, exactly representable in a double.
    fixtures.push_back(describe("gradient-mono-uint32", 5u, 3u, FixtureChannels::Mono, FixtureSampleType::UInt32,
                                FixtureDomain::SensorCode, FixturePattern::Gradient,
                                {{.base = 4294967000.0, .perColumn = 100.0, .perRow = -2000000000.0}}));

    // Thirty-two-bit signed samples at the extremes of their range.
    fixtures.push_back(describe("checker-rgb-int32", 4u, 4u, FixtureChannels::Rgb, FixtureSampleType::Int32,
                                FixtureDomain::SensorCode, FixturePattern::Checker,
                                {{.base = -2000000000.0, .alternate = 2000000000.0},
                                 {.base = 0.0, .alternate = -1.0},
                                 {.base = 2147483647.0, .alternate = -2147483648.0}}));

    // An undemosaiced sensor frame under a Bayer filter array.
    FixtureImage mosaic =
        describe("cfa-rggb-uint16", 8u, 6u, FixtureChannels::Mono, FixtureSampleType::UInt16, FixtureDomain::SensorCode,
                 FixturePattern::Gradient, {{.base = 1200.0, .perColumn = 37.0, .perRow = 91.0}});
    mosaic.cfa = FixtureCfa::Rggb;
    mosaic.cards = {{"EXPTIME", "30", "exposure in seconds", FixtureCardKind::Real}};
    fixtures.push_back(std::move(mosaic));

    // Invalid samples a float image can hold, and a negative zero.
    FixtureImage nonFinite =
        describe("nonfinite-mono-float32", 4u, 3u, FixtureChannels::Mono, FixtureSampleType::Float32,
                 FixtureDomain::PhysicalScaled, FixturePattern::Constant, {{.base = 0.25}});
    nonFinite.overrides = {{.x = 1u, .y = 1u, .channel = 0u, .value = std::numeric_limits<double>::quiet_NaN()},
                           {.x = 2u, .y = 0u, .channel = 0u, .value = std::numeric_limits<double>::infinity()},
                           {.x = 3u, .y = 2u, .channel = 0u, .value = -std::numeric_limits<double>::infinity()},
                           {.x = 0u, .y = 2u, .channel = 0u, .value = -0.0}};
    fixtures.push_back(std::move(nonFinite));

    // Double-precision colour inside zero to one.
    fixtures.push_back(describe("gradient-rgb-float64", 5u, 4u, FixtureChannels::Rgb, FixtureSampleType::Float64,
                                FixtureDomain::NormalizedLinear, FixturePattern::Gradient,
                                {{.base = 0.0, .perColumn = 0.25, .perRow = 0.0},
                                 {.base = 0.0, .perColumn = 0.0, .perRow = 1.0 / 3.0},
                                 {.base = 0.1, .perColumn = 0.05, .perRow = 0.1}}));

    return fixtures;
}

FixtureImage scene(std::string name, FixtureChannels channels, FixtureSampleType sampleType, FixtureDomain domain,
                   std::uint64_t seed, std::vector<FixtureChannelLevels> levels)
{
    FixtureImage image = describe(std::move(name), 12u, 8u, channels, sampleType, domain, FixturePattern::GradientNoise,
                                  std::move(levels));
    image.seed = seed;
    return image;
}

// Eight stars of different widths and peaks on a faint sloping background, none within two pixels of an edge.
std::vector<FixtureStar> starsOfTheField()
{
    return {{.x = 6.3, .y = 5.1, .width = 1.6, .peak = 0.7},    {.x = 20.5, .y = 4.2, .width = 1.3, .peak = 0.45},
            {.x = 27.1, .y = 14.8, .width = 2.0, .peak = 0.8},  {.x = 9.7, .y = 17.4, .width = 1.4, .peak = 0.55},
            {.x = 16.2, .y = 11.6, .width = 1.8, .peak = 0.75}, {.x = 24.4, .y = 20.6, .width = 1.2, .peak = 0.35},
            {.x = 4.8, .y = 21.3, .width = 1.5, .peak = 0.6},   {.x = 13.5, .y = 2.5, .width = 1.1, .peak = 0.3}};
}

std::vector<FixtureImage> buildBehavior()
{
    std::vector<FixtureImage> fixtures;

    // A normalized colour scene: three differently sloped channels with a little seeded noise, inside zero to one.
    fixtures.push_back(scene("scene-rgb-float32", FixtureChannels::Rgb, FixtureSampleType::Float32,
                             FixtureDomain::NormalizedLinear, 21u,
                             {{.base = 0.05, .perColumn = 0.06, .perRow = 0.02, .amplitude = 0.03},
                              {.base = 0.10, .perColumn = 0.03, .perRow = 0.05, .amplitude = 0.03},
                              {.base = 0.62, .perColumn = -0.04, .perRow = 0.02, .amplitude = 0.03}}));

    // The same kind of scene in one channel.
    fixtures.push_back(scene("scene-mono-float32", FixtureChannels::Mono, FixtureSampleType::Float32,
                             FixtureDomain::NormalizedLinear, 22u,
                             {{.base = 0.08, .perColumn = 0.05, .perRow = 0.04, .amplitude = 0.04}}));

    // Sixteen-bit colour camera codes, the integer path most actions meet.
    fixtures.push_back(scene("scene-rgb-uint16", FixtureChannels::Rgb, FixtureSampleType::UInt16,
                             FixtureDomain::SensorCode, 23u,
                             {{.base = 4000.0, .perColumn = 2500.0, .perRow = 1500.0, .amplitude = 800.0},
                              {.base = 9000.0, .perColumn = 1200.0, .perRow = 3000.0, .amplitude = 800.0},
                              {.base = 52000.0, .perColumn = -3000.0, .perRow = 1000.0, .amplitude = 800.0}}));

    // Eight-bit mono codes that saturate in the far corner.
    fixtures.push_back(scene("scene-mono-uint8", FixtureChannels::Mono, FixtureSampleType::UInt8,
                             FixtureDomain::SensorCode, 24u,
                             {{.base = 20.0, .perColumn = 18.0, .perRow = 6.0, .amplitude = 6.0}}));

    // Floats a physical quantity gives: negative in places and above one in others.
    fixtures.push_back(scene("scene-rgb-float32-unbounded", FixtureChannels::Rgb, FixtureSampleType::Float32,
                             FixtureDomain::PhysicalScaled, 25u,
                             {{.base = -0.3, .perColumn = 0.15, .perRow = 0.05, .amplitude = 0.05},
                              {.base = 1.2, .perColumn = -0.08, .perRow = 0.03, .amplitude = 0.05},
                              {.base = 0.4, .perColumn = 0.02, .perRow = -0.1, .amplitude = 0.05}}));

    // Signed sixteen-bit codes on both sides of zero.
    fixtures.push_back(scene("scene-mono-int16", FixtureChannels::Mono, FixtureSampleType::Int16,
                             FixtureDomain::SensorCode, 26u,
                             {{.base = -9000.0, .perColumn = 1500.0, .perRow = 800.0, .amplitude = 300.0}}));

    // A Bayer mosaic large enough for the filter-array actions' two-by-two blocks and their borders.
    FixtureImage mosaic = describe("mosaic-rggb-uint16", 24u, 16u, FixtureChannels::Mono, FixtureSampleType::UInt16,
                                   FixtureDomain::SensorCode, FixturePattern::GradientNoise,
                                   {{.base = 3000.0, .perColumn = 450.0, .perRow = 700.0, .amplitude = 200.0}});
    mosaic.seed = 27u;
    mosaic.cfa = FixtureCfa::Rggb;
    mosaic.cards = {{"EXPTIME", "60", "exposure in seconds", FixtureCardKind::Real}};
    fixtures.push_back(std::move(mosaic));

    // A larger mosaic for the demosaic method whose tiles need more border than the small one has.
    FixtureImage largeMosaic =
        describe("mosaic-rggb-uint16-large", 64u, 48u, FixtureChannels::Mono, FixtureSampleType::UInt16,
                 FixtureDomain::SensorCode, FixturePattern::GradientNoise,
                 {{.base = 3000.0, .perColumn = 170.0, .perRow = 230.0, .amplitude = 200.0}});
    largeMosaic.seed = 32u;
    largeMosaic.cfa = FixtureCfa::Rggb;
    largeMosaic.cards = {{"EXPTIME", "60", "exposure in seconds", FixtureCardKind::Real}};
    fixtures.push_back(std::move(largeMosaic));

    // Star fields for the actions that detect, profile, mask or reduce stars.
    FixtureImage monoStars =
        describe("stars-mono-float32", 32u, 24u, FixtureChannels::Mono, FixtureSampleType::Float32,
                 FixtureDomain::NormalizedLinear, FixturePattern::StarField,
                 {{.base = 0.06, .perColumn = 0.0005, .perRow = 0.0004, .alternate = 1.0, .amplitude = 0.004}});
    monoStars.seed = 28u;
    monoStars.stars = starsOfTheField();
    fixtures.push_back(std::move(monoStars));

    FixtureImage colourStars =
        describe("stars-rgb-float32", 32u, 24u, FixtureChannels::Rgb, FixtureSampleType::Float32,
                 FixtureDomain::NormalizedLinear, FixturePattern::StarField,
                 {{.base = 0.06, .perColumn = 0.0005, .perRow = 0.0004, .alternate = 1.0, .amplitude = 0.004},
                  {.base = 0.07, .perColumn = 0.0003, .perRow = 0.0005, .alternate = 0.9, .amplitude = 0.004},
                  {.base = 0.08, .perColumn = 0.0004, .perRow = 0.0002, .alternate = 0.75, .amplitude = 0.004}});
    colourStars.seed = 29u;
    colourStars.stars = starsOfTheField();
    fixtures.push_back(std::move(colourStars));

    // A wider field for the detectors that refuse a frame under about a hundred pixels a side: the same stars spread
    // over it, three times as far apart and one and a half times as wide.
    FixtureImage wideStars =
        describe("stars-mono-float32-wide", 112u, 104u, FixtureChannels::Mono, FixtureSampleType::Float32,
                 FixtureDomain::NormalizedLinear, FixturePattern::StarField,
                 {{.base = 0.06, .perColumn = 0.0002, .perRow = 0.00015, .alternate = 1.0, .amplitude = 0.004}});
    wideStars.seed = 33u;
    for (FixtureStar star : starsOfTheField())
    {
        star.x = 8.0 + 3.0 * star.x;
        star.y = 8.0 + 3.0 * star.y;
        star.width *= 1.5;
        wideStars.stars.push_back(star);
    }
    fixtures.push_back(std::move(wideStars));

    // The same fields' backgrounds without their stars, for the actions that take a starless image beside a starry one.
    FixtureImage monoStarless =
        describe("stars-mono-float32-starless", 32u, 24u, FixtureChannels::Mono, FixtureSampleType::Float32,
                 FixtureDomain::NormalizedLinear, FixturePattern::StarField,
                 {{.base = 0.06, .perColumn = 0.0005, .perRow = 0.0004, .alternate = 1.0, .amplitude = 0.004}});
    monoStarless.seed = 28u;
    fixtures.push_back(std::move(monoStarless));

    FixtureImage colourStarless =
        describe("stars-rgb-float32-starless", 32u, 24u, FixtureChannels::Rgb, FixtureSampleType::Float32,
                 FixtureDomain::NormalizedLinear, FixturePattern::StarField,
                 {{.base = 0.06, .perColumn = 0.0005, .perRow = 0.0004, .alternate = 1.0, .amplitude = 0.004},
                  {.base = 0.07, .perColumn = 0.0003, .perRow = 0.0005, .alternate = 0.9, .amplitude = 0.004},
                  {.base = 0.08, .perColumn = 0.0004, .perRow = 0.0002, .alternate = 0.75, .amplitude = 0.004}});
    colourStarless.seed = 29u;
    fixtures.push_back(std::move(colourStarless));

    // Second scenes of the first scenes' extent, for the actions that combine two images.
    fixtures.push_back(scene("scene-rgb-float32-second", FixtureChannels::Rgb, FixtureSampleType::Float32,
                             FixtureDomain::NormalizedLinear, 30u,
                             {{.base = 0.70, .perColumn = -0.05, .perRow = 0.01, .amplitude = 0.04},
                              {.base = 0.20, .perColumn = 0.02, .perRow = 0.06, .amplitude = 0.04},
                              {.base = 0.15, .perColumn = 0.05, .perRow = -0.01, .amplitude = 0.04}}));
    fixtures.push_back(scene("scene-mono-float32-second", FixtureChannels::Mono, FixtureSampleType::Float32,
                             FixtureDomain::NormalizedLinear, 31u,
                             {{.base = 0.85, .perColumn = -0.04, .perRow = -0.03, .amplitude = 0.04}}));

    return fixtures;
}

} // namespace

const std::vector<FixtureImage>& standardFixtures()
{
    static const std::vector<FixtureImage> fixtures = build();
    return fixtures;
}

const FixtureImage* standardFixture(std::string_view name)
{
    for (const FixtureImage& image : standardFixtures())
    {
        if (image.name == name)
        {
            return &image;
        }
    }
    return nullptr;
}

const std::vector<FixtureImage>& behaviorFixtures()
{
    static const std::vector<FixtureImage> fixtures = buildBehavior();
    return fixtures;
}

const FixtureImage* behaviorFixture(std::string_view name)
{
    for (const FixtureImage& image : behaviorFixtures())
    {
        if (image.name == name)
        {
            return &image;
        }
    }
    return nullptr;
}

const FixtureImage* fixtureNamed(std::string_view name)
{
    if (const FixtureImage* image = standardFixture(name))
    {
        return image;
    }
    return behaviorFixture(name);
}

} // namespace an::tests::fixtures
