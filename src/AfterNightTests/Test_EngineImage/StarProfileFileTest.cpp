// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/StarProfileFile.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
class StarProfileFileTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-star-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        engine = std::move(*an::EngineContext::create({.maxConcurrency = 2u}));
        execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 1u}));
    }

    void TearDown() override
    {
        execution = {};
        EXPECT_TRUE(engine.shutdown().ok());
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(directory, error);
        ASSERT_FALSE(error);
        ASSERT_EQ(root.parent_path(), std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()));
        std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error) << error.message();
    }

    static std::string utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    static void writeText(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

TEST_F(StarProfileFileTest, DetectsVersionOneFromContentAndRetainsEveryVersionOneField)
{
    const auto path = directory / "misleading.v2";
    writeText(
        path,
        R"({"stars_profile":{"engine":"v1","version":1,"params":{"detection_threshold":0.2,"roundness_tolerance":1.7,"min_size":3.0,"max_size":19.0},"global":{"global_fwhm":4.1,"global_quality":5.2,"background":0.03,"background_noise":0.004},"manually_edited":true,"stars":[{"intensity":1.0,"percentage":2.0,"deltaRadius":3.0,"quality":4.0,"meanRadius":5.0,"radius":6.0,"x":7.0,"y":8.0,"largeMajorAxis":9.0,"smallMajorAxis":10.0,"largeMinorAxis":11.0,"smallMinorAxis":12.0,"majorAxisAngle":13.0}]}})");

    auto profile = an::readStarProfileFile(execution, utf8(path));
    ASSERT_TRUE(profile.hasValue()) << profile.status().message();
    EXPECT_EQ(profile->generation, an::StarProfileGeneration::Version1);
    const auto* v1 = std::get_if<an::StarProfileV1>(&profile->payload);
    ASSERT_NE(v1, nullptr);
    EXPECT_DOUBLE_EQ(v1->parameters.detectionThreshold, 0.2);
    EXPECT_DOUBLE_EQ(v1->parameters.roundnessTolerance, 1.7);
    EXPECT_DOUBLE_EQ(v1->parameters.minimumSize, 3.0);
    EXPECT_DOUBLE_EQ(v1->parameters.maximumSize, 19.0);
    EXPECT_DOUBLE_EQ(v1->global.fullWidth, 4.1);
    EXPECT_DOUBLE_EQ(v1->global.quality, 5.2);
    EXPECT_DOUBLE_EQ(v1->global.background, 0.03);
    EXPECT_DOUBLE_EQ(v1->global.backgroundNoise, 0.004);
    EXPECT_TRUE(v1->manuallyEdited);
    ASSERT_EQ(v1->stars.size(), 1u);
    const auto& star = v1->stars.front();
    EXPECT_DOUBLE_EQ(star.intensity, 1.0);
    EXPECT_DOUBLE_EQ(star.percentage, 2.0);
    EXPECT_DOUBLE_EQ(star.deltaRadius, 3.0);
    EXPECT_DOUBLE_EQ(star.quality, 4.0);
    EXPECT_DOUBLE_EQ(star.meanRadius, 5.0);
    EXPECT_DOUBLE_EQ(star.radius, 6.0);
    EXPECT_DOUBLE_EQ(star.x, 7.0);
    EXPECT_DOUBLE_EQ(star.y, 8.0);
    EXPECT_DOUBLE_EQ(star.largeMajorAxis, 9.0);
    EXPECT_DOUBLE_EQ(star.smallMajorAxis, 10.0);
    EXPECT_DOUBLE_EQ(star.largeMinorAxis, 11.0);
    EXPECT_DOUBLE_EQ(star.smallMinorAxis, 12.0);
    EXPECT_DOUBLE_EQ(star.majorAxisAngle, 13.0);

    const auto outputPath = utf8(directory / "round-trip.v1");
    auto written = an::writeStarProfileFile(engine, execution, {.destinationPath = outputPath}, *profile);
    ASSERT_TRUE(written.hasValue()) << written.status().message();
    auto reloaded = an::readStarProfileFile(execution, outputPath);
    ASSERT_TRUE(reloaded.hasValue()) << reloaded.status().message();
    const auto* reloadedV1 = std::get_if<an::StarProfileV1>(&reloaded->payload);
    ASSERT_NE(reloadedV1, nullptr);
    ASSERT_EQ(reloadedV1->stars.size(), 1u);
    EXPECT_DOUBLE_EQ(reloadedV1->parameters.detectionThreshold, v1->parameters.detectionThreshold);
    EXPECT_DOUBLE_EQ(reloadedV1->global.backgroundNoise, v1->global.backgroundNoise);
    EXPECT_TRUE(reloadedV1->manuallyEdited);
    EXPECT_DOUBLE_EQ(reloadedV1->stars.front().majorAxisAngle, star.majorAxisAngle);
}

TEST_F(StarProfileFileTest, AcceptsBothVersionOneDiscriminatorForms)
{
    const auto engineOnly = directory / "engine-only.profile";
    writeText(engineOnly, R"({"stars_profile":{"engine":"v1","params":{},"global":{},"stars":[]}})");
    auto byEngine = an::readStarProfileFile(execution, utf8(engineOnly));
    ASSERT_TRUE(byEngine.hasValue()) << byEngine.status().message();
    EXPECT_EQ(byEngine->generation, an::StarProfileGeneration::Version1);

    const auto versionOnly = directory / "version-only.profile";
    writeText(versionOnly, R"({"stars_profile":{"version":1,"params":{},"global":{},"stars":[]}})");
    auto byVersion = an::readStarProfileFile(execution, utf8(versionOnly));
    ASSERT_TRUE(byVersion.hasValue()) << byVersion.status().message();
    EXPECT_EQ(byVersion->generation, an::StarProfileGeneration::Version1);
}

TEST_F(StarProfileFileTest, VersionTwoRoundTripRetainsPsfPhotometryAstrometryAndChannelStatistics)
{
    an::StarProfileV2 data;
    data.parameters.radius = 7;
    data.parameters.sigma = 2.2;
    data.parameters.roundness = 0.63;
    data.parameters.focalLength = 550.0;
    data.parameters.pixelSizeX = 3.76;
    data.parameters.convergence = 4;
    data.parameters.relaxChecks = true;
    data.parameters.profile = an::PersistedPsfModel::MoffatFreeBeta;
    data.parameters.minimumBeta = 1.7;
    data.parameters.minimumAmplitude = 0.01;
    data.parameters.maximumAmplitude = 0.9;
    data.parameters.maximumRoundness = 0.8;
    data.global.fullWidth = 3.4;
    data.global.roundness = 0.91;
    data.global.quality = 8.0;
    data.global.signalToNoise = 22.0;
    data.global.background = 0.02;
    data.global.backgroundNoise = 0.003;
    data.global.medianAbsoluteDeviation = 0.002;
    data.global.robustScale = 0.0025;
    data.global.zeroPointG = 25.1;
    data.global.zeroPointError = 0.04;
    data.global.catalogMatchedCount = 17u;
    data.global.limitingMagnitude = 18.2;
    data.global.channelCount = 3u;
    data.global.channelBackgroundNoise = {0.1, 0.2, 0.3};
    data.global.channelMedianAbsoluteDeviation = {0.4, 0.5, 0.6};
    data.global.channelRobustScale = {0.7, 0.8, 0.9};
    an::StarProfileV2Star star;
    star.name = "Gaia DR3 123";
    star.psf = {.background = 1.0,
                .amplitude = 2.0,
                .centreX = 3.0,
                .centreY = 4.0,
                .scaleX = 5.0,
                .scaleY = 6.0,
                .fullWidthX = 7.0,
                .fullWidthY = 8.0,
                .fullWidthXArcseconds = 9.0,
                .fullWidthYArcseconds = 10.0,
                .angle = 11.0,
                .residualRootMeanSquare = 12.0,
                .saturation = 13.0,
                .boxRadius = 14,
                .saturated = true,
                .beta = 15.0,
                .profile = an::PersistedPsfModel::MoffatFixedBeta};
    star.position = {.x = 16.0, .y = 17.0};
    star.photometry = {.magnitude = 18.0,
                       .backgroundMagnitude = 19.0,
                       .magnitudeError = 20.0,
                       .backgroundMagnitudeError = 21.0,
                       .signalToNoise = 22.0,
                       .valid = true,
                       .colorIndexBv = 23.0};
    star.uncertainty = {.background = 24.0,
                        .amplitude = 25.0,
                        .centreX = 26.0,
                        .centreY = 27.0,
                        .scaleX = 28.0,
                        .scaleY = 29.0,
                        .angle = 30.0,
                        .beta = 31.0};
    star.astrometry = {.channel = 2,
                       .units = "deg",
                       .rightAscension = 32.0,
                       .declination = 33.0,
                       .hasWorldCoordinates = true,
                       .catalogId = "123",
                       .catalogMagnitudeG = 34.0,
                       .catalogMagnitudeBp = 35.0,
                       .catalogMagnitudeRp = 36.0,
                       .colorIndexBpRp = 37.0,
                       .matchSeparationArcseconds = 38.0,
                       .hasCatalogMatch = true};
    star.metrics = {.roundness = 39.0, .circularity = 40.0, .fullWidth = 41.0};
    data.stars.push_back(star);
    const an::StarProfileFile profile{.generation = an::StarProfileGeneration::Version2, .payload = data};
    const auto path = utf8(directory / u8"星-profile.anything");

    auto report = an::writeStarProfileFile(engine, execution, {.destinationPath = path}, profile);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_GT(report->bytesWritten, 0u);
    auto loaded = an::readStarProfileFile(execution, path);
    ASSERT_TRUE(loaded.hasValue()) << loaded.status().message();
    EXPECT_EQ(loaded->generation, an::StarProfileGeneration::Version2);
    const auto* v2 = std::get_if<an::StarProfileV2>(&loaded->payload);
    ASSERT_NE(v2, nullptr);
    ASSERT_EQ(v2->stars.size(), 1u);
    EXPECT_EQ(v2->parameters.radius, data.parameters.radius);
    EXPECT_DOUBLE_EQ(v2->parameters.sigma, data.parameters.sigma);
    EXPECT_DOUBLE_EQ(v2->parameters.roundness, data.parameters.roundness);
    EXPECT_DOUBLE_EQ(v2->parameters.focalLength, data.parameters.focalLength);
    EXPECT_DOUBLE_EQ(v2->parameters.pixelSizeX, data.parameters.pixelSizeX);
    EXPECT_EQ(v2->parameters.convergence, data.parameters.convergence);
    EXPECT_EQ(v2->parameters.relaxChecks, data.parameters.relaxChecks);
    EXPECT_EQ(v2->parameters.profile, an::PersistedPsfModel::MoffatFreeBeta);
    EXPECT_DOUBLE_EQ(v2->parameters.minimumBeta, data.parameters.minimumBeta);
    EXPECT_DOUBLE_EQ(v2->parameters.minimumAmplitude, data.parameters.minimumAmplitude);
    EXPECT_DOUBLE_EQ(v2->parameters.maximumAmplitude, data.parameters.maximumAmplitude);
    EXPECT_DOUBLE_EQ(v2->parameters.maximumRoundness, data.parameters.maximumRoundness);
    EXPECT_DOUBLE_EQ(v2->global.fullWidth, data.global.fullWidth);
    EXPECT_DOUBLE_EQ(v2->global.roundness, data.global.roundness);
    EXPECT_DOUBLE_EQ(v2->global.quality, data.global.quality);
    EXPECT_DOUBLE_EQ(v2->global.signalToNoise, data.global.signalToNoise);
    EXPECT_DOUBLE_EQ(v2->global.background, data.global.background);
    EXPECT_DOUBLE_EQ(v2->global.backgroundNoise, data.global.backgroundNoise);
    EXPECT_DOUBLE_EQ(v2->global.medianAbsoluteDeviation, data.global.medianAbsoluteDeviation);
    EXPECT_DOUBLE_EQ(v2->global.robustScale, data.global.robustScale);
    EXPECT_DOUBLE_EQ(v2->global.zeroPointG, 25.1);
    EXPECT_DOUBLE_EQ(v2->global.zeroPointError, data.global.zeroPointError);
    EXPECT_EQ(v2->global.catalogMatchedCount, 17u);
    EXPECT_DOUBLE_EQ(v2->global.limitingMagnitude, data.global.limitingMagnitude);
    EXPECT_EQ(v2->global.channelCount, data.global.channelCount);
    EXPECT_EQ(v2->global.channelBackgroundNoise, data.global.channelBackgroundNoise);
    EXPECT_EQ(v2->global.channelMedianAbsoluteDeviation, data.global.channelMedianAbsoluteDeviation);
    EXPECT_EQ(v2->global.channelRobustScale, data.global.channelRobustScale);
    const auto& actual = v2->stars.front();
    EXPECT_EQ(actual.name, star.name);
    EXPECT_DOUBLE_EQ(actual.psf.background, star.psf.background);
    EXPECT_DOUBLE_EQ(actual.psf.amplitude, star.psf.amplitude);
    EXPECT_DOUBLE_EQ(actual.psf.centreX, star.psf.centreX);
    EXPECT_DOUBLE_EQ(actual.psf.centreY, star.psf.centreY);
    EXPECT_DOUBLE_EQ(actual.psf.scaleX, star.psf.scaleX);
    EXPECT_DOUBLE_EQ(actual.psf.scaleY, star.psf.scaleY);
    EXPECT_DOUBLE_EQ(actual.psf.fullWidthX, star.psf.fullWidthX);
    EXPECT_DOUBLE_EQ(actual.psf.fullWidthY, star.psf.fullWidthY);
    EXPECT_DOUBLE_EQ(actual.psf.fullWidthXArcseconds, star.psf.fullWidthXArcseconds);
    EXPECT_DOUBLE_EQ(actual.psf.fullWidthYArcseconds, star.psf.fullWidthYArcseconds);
    EXPECT_DOUBLE_EQ(actual.psf.angle, star.psf.angle);
    EXPECT_DOUBLE_EQ(actual.psf.residualRootMeanSquare, star.psf.residualRootMeanSquare);
    EXPECT_DOUBLE_EQ(actual.psf.saturation, star.psf.saturation);
    EXPECT_EQ(actual.psf.boxRadius, star.psf.boxRadius);
    EXPECT_EQ(actual.psf.saturated, star.psf.saturated);
    EXPECT_DOUBLE_EQ(actual.psf.beta, star.psf.beta);
    EXPECT_EQ(actual.psf.profile, an::PersistedPsfModel::MoffatFixedBeta);
    EXPECT_DOUBLE_EQ(actual.position.x, star.position.x);
    EXPECT_DOUBLE_EQ(actual.position.y, star.position.y);
    EXPECT_DOUBLE_EQ(actual.photometry.magnitude, star.photometry.magnitude);
    EXPECT_DOUBLE_EQ(actual.photometry.backgroundMagnitude, star.photometry.backgroundMagnitude);
    EXPECT_DOUBLE_EQ(actual.photometry.magnitudeError, star.photometry.magnitudeError);
    EXPECT_DOUBLE_EQ(actual.photometry.backgroundMagnitudeError, star.photometry.backgroundMagnitudeError);
    EXPECT_DOUBLE_EQ(actual.photometry.signalToNoise, star.photometry.signalToNoise);
    EXPECT_EQ(actual.photometry.valid, star.photometry.valid);
    EXPECT_DOUBLE_EQ(actual.photometry.colorIndexBv, star.photometry.colorIndexBv);
    EXPECT_DOUBLE_EQ(actual.uncertainty.background, star.uncertainty.background);
    EXPECT_DOUBLE_EQ(actual.uncertainty.amplitude, star.uncertainty.amplitude);
    EXPECT_DOUBLE_EQ(actual.uncertainty.centreX, star.uncertainty.centreX);
    EXPECT_DOUBLE_EQ(actual.uncertainty.centreY, star.uncertainty.centreY);
    EXPECT_DOUBLE_EQ(actual.uncertainty.scaleX, star.uncertainty.scaleX);
    EXPECT_DOUBLE_EQ(actual.uncertainty.scaleY, star.uncertainty.scaleY);
    EXPECT_DOUBLE_EQ(actual.uncertainty.angle, star.uncertainty.angle);
    EXPECT_DOUBLE_EQ(actual.uncertainty.beta, star.uncertainty.beta);
    EXPECT_EQ(actual.astrometry.channel, star.astrometry.channel);
    EXPECT_EQ(actual.astrometry.units, star.astrometry.units);
    EXPECT_DOUBLE_EQ(actual.astrometry.rightAscension, star.astrometry.rightAscension);
    EXPECT_DOUBLE_EQ(actual.astrometry.declination, star.astrometry.declination);
    EXPECT_EQ(actual.astrometry.hasWorldCoordinates, star.astrometry.hasWorldCoordinates);
    EXPECT_EQ(actual.astrometry.catalogId, star.astrometry.catalogId);
    EXPECT_DOUBLE_EQ(actual.astrometry.catalogMagnitudeG, star.astrometry.catalogMagnitudeG);
    EXPECT_DOUBLE_EQ(actual.astrometry.catalogMagnitudeBp, star.astrometry.catalogMagnitudeBp);
    EXPECT_DOUBLE_EQ(actual.astrometry.catalogMagnitudeRp, star.astrometry.catalogMagnitudeRp);
    EXPECT_DOUBLE_EQ(actual.astrometry.colorIndexBpRp, star.astrometry.colorIndexBpRp);
    EXPECT_DOUBLE_EQ(actual.astrometry.matchSeparationArcseconds, star.astrometry.matchSeparationArcseconds);
    EXPECT_EQ(actual.astrometry.hasCatalogMatch, star.astrometry.hasCatalogMatch);
    EXPECT_DOUBLE_EQ(actual.metrics.roundness, star.metrics.roundness);
    EXPECT_DOUBLE_EQ(actual.metrics.circularity, star.metrics.circularity);
    EXPECT_DOUBLE_EQ(actual.metrics.fullWidth, star.metrics.fullWidth);
}

TEST_F(StarProfileFileTest, BoundsMalformedSchemasAndMismatchedPayloadsAreRefused)
{
    const auto tooMany = directory / "too-many.v1";
    writeText(tooMany, R"({"stars_profile":{"engine":"v1","version":1,"params":{},"global":{},"stars":[{},{}]}})");
    auto count = an::readStarProfileFile(execution, utf8(tooMany), {.maximumStars = 1u});
    EXPECT_EQ(count.status().code(), an::ErrorCode::LimitExceeded);

    const auto wrong = directory / "wrong.v2";
    writeText(wrong, R"({"stars_profile":{"engine":"v3","version":3,"params":{},"global":{}}})");
    EXPECT_EQ(an::readStarProfileFile(execution, utf8(wrong)).status().code(), an::ErrorCode::Unsupported);

    const auto malformed = directory / "malformed.v1";
    writeText(malformed, "{not json");
    EXPECT_EQ(an::readStarProfileFile(execution, utf8(malformed)).status().code(), an::ErrorCode::InvalidData);

    const an::StarProfileFile mismatch{.generation = an::StarProfileGeneration::Version2,
                                       .payload = an::StarProfileV1{}};
    EXPECT_EQ(
        an::writeStarProfileFile(engine, execution, {.destinationPath = utf8(directory / "mismatch.v2")}, mismatch)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);

    const an::StarProfileFile valid{};
    const auto invalidPath = utf8(directory / "invalid.v1");
    an::StarProfileWriteRequest invalidRequest{.destinationPath = invalidPath};
    invalidRequest.durability = static_cast<an::StarProfileDurability>(99);
    EXPECT_EQ(an::writeStarProfileFile(engine, execution, invalidRequest, valid).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(StarProfileFileSortTest, OrdersEveryVersionOneOnlyFieldStably)
{
    using Member = double an::StarProfileV1Star::*;
    struct Case
    {
        an::StarProfileV1SortKey key;
        Member member;
    };
    constexpr std::array cases{Case{an::StarProfileV1SortKey::Intensity, &an::StarProfileV1Star::intensity},
                               Case{an::StarProfileV1SortKey::Percentage, &an::StarProfileV1Star::percentage},
                               Case{an::StarProfileV1SortKey::DeltaRadius, &an::StarProfileV1Star::deltaRadius},
                               Case{an::StarProfileV1SortKey::Quality, &an::StarProfileV1Star::quality},
                               Case{an::StarProfileV1SortKey::MeanRadius, &an::StarProfileV1Star::meanRadius},
                               Case{an::StarProfileV1SortKey::Radius, &an::StarProfileV1Star::radius},
                               Case{an::StarProfileV1SortKey::PositionX, &an::StarProfileV1Star::x},
                               Case{an::StarProfileV1SortKey::PositionY, &an::StarProfileV1Star::y},
                               Case{an::StarProfileV1SortKey::LargeMajorAxis, &an::StarProfileV1Star::largeMajorAxis},
                               Case{an::StarProfileV1SortKey::SmallMajorAxis, &an::StarProfileV1Star::smallMajorAxis},
                               Case{an::StarProfileV1SortKey::LargeMinorAxis, &an::StarProfileV1Star::largeMinorAxis},
                               Case{an::StarProfileV1SortKey::SmallMinorAxis, &an::StarProfileV1Star::smallMinorAxis},
                               Case{an::StarProfileV1SortKey::MajorAxisAngle, &an::StarProfileV1Star::majorAxisAngle}};
    for (const Case& test : cases)
    {
        SCOPED_TRACE(static_cast<unsigned>(test.key));
        std::vector<an::StarProfileV1Star> stars(3u);
        stars[0].*test.member = stars[1].*test.member = 2.0;
        stars[2].*test.member = 1.0;
        Member identity = test.member == &an::StarProfileV1Star::quality ? &an::StarProfileV1Star::intensity
                                                                         : &an::StarProfileV1Star::quality;
        stars[0].*identity = 10.0;
        stars[1].*identity = 20.0;
        stars[2].*identity = 30.0;
        ASSERT_TRUE(an::sortStarProfileV1(stars, test.key, an::StarProfileSortDirection::Ascending).ok());
        EXPECT_DOUBLE_EQ(stars[0].*identity, 30.0);
        EXPECT_DOUBLE_EQ(stars[1].*identity, 10.0);
        EXPECT_DOUBLE_EQ(stars[2].*identity, 20.0);
        ASSERT_TRUE(an::sortStarProfileV1(stars, test.key, an::StarProfileSortDirection::Descending).ok());
        EXPECT_DOUBLE_EQ(stars[0].*identity, 10.0);
        EXPECT_DOUBLE_EQ(stars[1].*identity, 20.0);
        EXPECT_DOUBLE_EQ(stars[2].*identity, 30.0);
    }
    std::vector<an::StarProfileV1Star> stars;
    EXPECT_EQ(an::sortStarProfileV1(stars, static_cast<an::StarProfileV1SortKey>(99)).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::sortStarProfileV1(stars, an::StarProfileV1SortKey::Intensity, static_cast<an::StarProfileSortDirection>(99))
            .code(),
        an::ErrorCode::InvalidArgument);
}
} // namespace
