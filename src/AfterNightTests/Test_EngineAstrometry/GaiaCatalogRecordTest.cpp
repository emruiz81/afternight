// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Gaia provider's own record: its three binary layouts, its absent codes, and the entries it
// becomes.

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>

#include "CatalogTestSupport.h"
#include "GaiaTestSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using catalog_test::makeEngine;
using catalog_test::makeExecution;
using gaia_test::contextCount;
using gaia_test::contextText;
using gaia_test::encodeStars;
using gaia_test::FixtureStar;

/// One star with every optional field carrying a measurement.
[[nodiscard]] FixtureStar measuredStar()
{
    FixtureStar star;
    star.rightAscensionDegrees = 123.25;
    star.declinationDegrees = -17.5;
    star.sourceIdentifier = 4295806720u;
    star.magnitudeG = 11.25f;
    star.magnitudeBp = 11.75f;
    star.magnitudeRp = 10.5f;
    star.properMotionRightAscension = 12.5f;
    star.properMotionDeclination = -7.25f;
    star.temperature = 5800u;
    star.surfaceGravityHundredths = 445;
    star.metallicityHundredths = -12;
    star.extinctionTenThousandths = 1200u;
    star.reddeningTenThousandths = 800;
    return star;
}

/// One star whose every optional field carries its absent code.
[[nodiscard]] FixtureStar unmeasuredStar()
{
    FixtureStar star = measuredStar();
    star.magnitudeG = std::numeric_limits<float>::quiet_NaN();
    star.magnitudeBp = std::numeric_limits<float>::quiet_NaN();
    star.magnitudeRp = std::numeric_limits<float>::infinity();
    star.properMotionRightAscension = std::numeric_limits<float>::quiet_NaN();
    star.properMotionDeclination = -std::numeric_limits<float>::infinity();
    star.temperature = 0u;
    star.surfaceGravityHundredths = std::numeric_limits<std::int16_t>::min();
    star.metallicityHundredths = std::numeric_limits<std::int16_t>::min();
    star.extinctionTenThousandths = std::numeric_limits<std::uint16_t>::max();
    star.reddeningTenThousandths = std::numeric_limits<std::int16_t>::min();
    return star;
}

/// The records a block of fixture stars decodes to, failing the test rather than the caller.
[[nodiscard]] std::vector<an::GaiaStarRecord>
decodeOrFail(const an::ExecutionContext& execution, const std::vector<FixtureStar>& stars, an::GaiaRecordLayout layout)
{
    const std::vector<std::byte> bytes = encodeStars(stars, layout);
    an::Result<std::vector<an::GaiaStarRecord>> decoded = an::decodeGaiaRecords(execution, bytes, layout);
    EXPECT_TRUE(decoded.hasValue()) << decoded.status().message();
    return decoded.hasValue() ? std::move(*decoded) : std::vector<an::GaiaStarRecord>{};
}

TEST(GaiaCatalogRecordTest, TheLayoutVocabularyIsAClosedTableReadBothWays)
{
    EXPECT_TRUE(an::isGaiaRecordLayout(an::GaiaRecordLayout::Version1));
    EXPECT_TRUE(an::isGaiaRecordLayout(an::GaiaRecordLayout::Version2));
    EXPECT_TRUE(an::isGaiaRecordLayout(an::GaiaRecordLayout::Version3));
    EXPECT_FALSE(an::isGaiaRecordLayout(static_cast<an::GaiaRecordLayout>(9)));

    EXPECT_EQ(an::gaiaRecordSizeBytes(an::GaiaRecordLayout::Version1), 36u);
    EXPECT_EQ(an::gaiaRecordSizeBytes(an::GaiaRecordLayout::Version2), 44u);
    EXPECT_EQ(an::gaiaRecordSizeBytes(an::GaiaRecordLayout::Version3), 64u);
    EXPECT_EQ(an::gaiaRecordSizeBytes(static_cast<an::GaiaRecordLayout>(9)), 0u);

    EXPECT_EQ(an::gaiaRecordLayoutForSize(36u), an::GaiaRecordLayout::Version1);
    EXPECT_EQ(an::gaiaRecordLayoutForSize(44u), an::GaiaRecordLayout::Version2);
    EXPECT_EQ(an::gaiaRecordLayoutForSize(64u), an::GaiaRecordLayout::Version3);
    EXPECT_FALSE(an::gaiaRecordLayoutForSize(0u).has_value());
    EXPECT_FALSE(an::gaiaRecordLayoutForSize(48u).has_value());

    EXPECT_STREQ(an::gaiaRecordLayoutName(an::GaiaRecordLayout::Version3), "Version3");
    EXPECT_STREQ(an::gaiaRecordLayoutName(static_cast<an::GaiaRecordLayout>(9)), "UnknownGaiaRecordLayout");
}

TEST(GaiaCatalogRecordTest, DecodingRefusesWhatItCannotRead)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<std::byte> bytes =
        encodeStars(std::vector<FixtureStar>{measuredStar()}, an::GaiaRecordLayout::Version3);

    const an::ExecutionContext none;
    EXPECT_EQ(an::decodeGaiaRecords(none, bytes, an::GaiaRecordLayout::Version3).status().code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::decodeGaiaRecords(execution, bytes, static_cast<an::GaiaRecordLayout>(9)).status().code(),
              an::ErrorCode::InvalidArgument);

    an::GaiaRecordLimits nothing;
    nothing.maximumRecords = 0u;
    EXPECT_EQ(an::decodeGaiaRecords(execution, bytes, an::GaiaRecordLayout::Version3, nothing).status().code(),
              an::ErrorCode::InvalidArgument);

    const std::span<const std::byte> partial{bytes.data(), bytes.size() - 1u};
    const an::Status truncated = an::decodeGaiaRecords(execution, partial, an::GaiaRecordLayout::Version3).status();
    EXPECT_EQ(truncated.code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(contextCount(truncated, an::ContextKey::Requested), 63u);
    EXPECT_EQ(contextText(truncated, an::ContextKey::Parameter), "Version3");
}

TEST(GaiaCatalogRecordTest, DecodingRefusesMoreRecordsThanTheCeilingAdmits)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureStar> stars(4u, measuredStar());
    const std::vector<std::byte> bytes = encodeStars(stars, an::GaiaRecordLayout::Version3);

    an::GaiaRecordLimits limits;
    limits.maximumRecords = 3u;
    const an::Status refused = an::decodeGaiaRecords(execution, bytes, an::GaiaRecordLayout::Version3, limits).status();
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(contextCount(refused, an::ContextKey::Requested), 4u);
    EXPECT_EQ(contextCount(refused, an::ContextKey::Limit), 3u);
}

TEST(GaiaCatalogRecordTest, DecodingNothingIsASuccessWithNoRecords)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::Result<std::vector<an::GaiaStarRecord>> decoded =
        an::decodeGaiaRecords(execution, std::span<const std::byte>{}, an::GaiaRecordLayout::Version1);
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_TRUE(decoded->empty());
}

TEST(GaiaCatalogRecordTest, TheWidestLayoutCarriesEveryFieldItStates)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::GaiaStarRecord> records =
        decodeOrFail(execution, {measuredStar()}, an::GaiaRecordLayout::Version3);
    ASSERT_EQ(records.size(), 1u);

    const an::GaiaStarRecord& record = records.front();
    EXPECT_DOUBLE_EQ(record.coordinate.rightAscensionDegrees, 123.25);
    EXPECT_DOUBLE_EQ(record.coordinate.declinationDegrees, -17.5);
    EXPECT_EQ(record.sourceIdentifier, 4295806720u);
    ASSERT_TRUE(record.magnitudeG.has_value());
    EXPECT_DOUBLE_EQ(*record.magnitudeG, 11.25);
    ASSERT_TRUE(record.magnitudeBp.has_value());
    EXPECT_DOUBLE_EQ(*record.magnitudeBp, 11.75);
    ASSERT_TRUE(record.magnitudeRp.has_value());
    EXPECT_DOUBLE_EQ(*record.magnitudeRp, 10.5);
    ASSERT_TRUE(record.properMotionRightAscensionMasPerYear.has_value());
    EXPECT_DOUBLE_EQ(*record.properMotionRightAscensionMasPerYear, 12.5);
    ASSERT_TRUE(record.properMotionDeclinationMasPerYear.has_value());
    EXPECT_DOUBLE_EQ(*record.properMotionDeclinationMasPerYear, -7.25);

    // The quantized parameters are scaled in double: the stored integer is exact and dividing it
    // once is closer to the value it stands for than a float division is.
    ASSERT_TRUE(record.effectiveTemperatureKelvin.has_value());
    EXPECT_DOUBLE_EQ(*record.effectiveTemperatureKelvin, 5800.0);
    ASSERT_TRUE(record.surfaceGravityLog.has_value());
    EXPECT_DOUBLE_EQ(*record.surfaceGravityLog, 4.45);
    ASSERT_TRUE(record.metallicity.has_value());
    EXPECT_DOUBLE_EQ(*record.metallicity, -0.12);
    ASSERT_TRUE(record.extinctionG.has_value());
    EXPECT_DOUBLE_EQ(*record.extinctionG, 0.12);
    ASSERT_TRUE(record.reddeningBpMinusRp.has_value());
    EXPECT_DOUBLE_EQ(*record.reddeningBpMinusRp, 0.08);
}

TEST(GaiaCatalogRecordTest, EveryAbsentCodeBecomesNoMeasurement)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::GaiaStarRecord> records =
        decodeOrFail(execution, {unmeasuredStar()}, an::GaiaRecordLayout::Version3);
    ASSERT_EQ(records.size(), 1u);

    const an::GaiaStarRecord& record = records.front();
    EXPECT_FALSE(record.magnitudeG.has_value());
    EXPECT_FALSE(record.magnitudeBp.has_value());
    EXPECT_FALSE(record.magnitudeRp.has_value());
    EXPECT_FALSE(record.properMotionRightAscensionMasPerYear.has_value());
    EXPECT_FALSE(record.properMotionDeclinationMasPerYear.has_value());
    EXPECT_FALSE(record.effectiveTemperatureKelvin.has_value());
    EXPECT_FALSE(record.surfaceGravityLog.has_value());
    EXPECT_FALSE(record.metallicity.has_value());
    EXPECT_FALSE(record.extinctionG.has_value());
    EXPECT_FALSE(record.reddeningBpMinusRp.has_value());
    EXPECT_EQ(record.sourceIdentifier, 4295806720u);
}

TEST(GaiaCatalogRecordTest, TheNarrowerLayoutsCarryOnlyWhatTheyHold)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const std::vector<an::GaiaStarRecord> oldest =
        decodeOrFail(execution, {measuredStar()}, an::GaiaRecordLayout::Version1);
    ASSERT_EQ(oldest.size(), 1u);
    EXPECT_EQ(oldest.front().sourceIdentifier, 0u);
    EXPECT_FALSE(oldest.front().effectiveTemperatureKelvin.has_value());
    ASSERT_TRUE(oldest.front().magnitudeG.has_value());
    EXPECT_DOUBLE_EQ(*oldest.front().magnitudeG, 11.25);

    const std::vector<an::GaiaStarRecord> identified =
        decodeOrFail(execution, {measuredStar()}, an::GaiaRecordLayout::Version2);
    ASSERT_EQ(identified.size(), 1u);
    EXPECT_EQ(identified.front().sourceIdentifier, 4295806720u);
    EXPECT_FALSE(identified.front().surfaceGravityLog.has_value());
    EXPECT_FALSE(identified.front().metallicity.has_value());
}

TEST(GaiaCatalogRecordTest, DecodingStopsWhenTheCallerAsksItTo)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 4u, stop.get_token());
    const std::vector<FixtureStar> stars(4u, measuredStar());
    const std::vector<std::byte> bytes = encodeStars(stars, an::GaiaRecordLayout::Version3);

    stop.request_stop();
    EXPECT_EQ(an::decodeGaiaRecords(execution, bytes, an::GaiaRecordLayout::Version3).status().code(),
              an::ErrorCode::Cancelled);
}

TEST(GaiaCatalogRecordTest, AnEpochCorrectionAtTheCatalogEpochMovesNothing)
{
    an::GaiaStarRecord record;
    record.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 45.0};
    record.properMotionRightAscensionMasPerYear = 3600000.0;
    record.properMotionDeclinationMasPerYear = 3600000.0;

    const an::SkyCoordinate same = an::applyGaiaEpochCorrection(record, 2016.0, 2016.0);
    EXPECT_DOUBLE_EQ(same.rightAscensionDegrees, 10.0);
    EXPECT_DOUBLE_EQ(same.declinationDegrees, 45.0);

    const an::SkyCoordinate unknown =
        an::applyGaiaEpochCorrection(record, std::numeric_limits<double>::quiet_NaN(), 2016.0);
    EXPECT_DOUBLE_EQ(unknown.rightAscensionDegrees, 10.0);
    EXPECT_DOUBLE_EQ(unknown.declinationDegrees, 45.0);
}

TEST(GaiaCatalogRecordTest, AnEpochCorrectionMovesBothAxesByTheProperMotion)
{
    an::GaiaStarRecord record;
    record.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 45.0};
    record.properMotionRightAscensionMasPerYear = 3600000.0;
    record.properMotionDeclinationMasPerYear = 3600000.0;

    const an::SkyCoordinate moved = an::applyGaiaEpochCorrection(record, 2016.0, 2017.0);
    const double cosineDeclination = std::cos(45.0 * std::numbers::pi / 180.0);
    EXPECT_DOUBLE_EQ(moved.rightAscensionDegrees, 10.0 + (1.0 / cosineDeclination));
    EXPECT_DOUBLE_EQ(moved.declinationDegrees, 46.0);

    // The right-ascension step uses the declination the catalog states, not the one the correction
    // is on its way to, which is the only order a single pass can use.
    const an::SkyCoordinate back = an::applyGaiaEpochCorrection(record, 2016.0, 2015.0);
    EXPECT_DOUBLE_EQ(back.rightAscensionDegrees, 10.0 - (1.0 / cosineDeclination));
    EXPECT_DOUBLE_EQ(back.declinationDegrees, 44.0);
}

TEST(GaiaCatalogRecordTest, AnEpochCorrectionFoldsAndClampsWhatItProduces)
{
    an::GaiaStarRecord wrapping;
    wrapping.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 359.9, .declinationDegrees = 0.0};
    wrapping.properMotionRightAscensionMasPerYear = 3600000.0;
    const an::SkyCoordinate wrapped = an::applyGaiaEpochCorrection(wrapping, 2016.0, 2017.0);
    EXPECT_NEAR(wrapped.rightAscensionDegrees, 0.9, 1.0e-12);

    an::GaiaStarRecord rising;
    rising.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 30.0, .declinationDegrees = 89.5};
    rising.properMotionDeclinationMasPerYear = 3600000.0;
    const an::SkyCoordinate clamped = an::applyGaiaEpochCorrection(rising, 2016.0, 2017.0);
    EXPECT_DOUBLE_EQ(clamped.declinationDegrees, 90.0);

    an::GaiaStarRecord outside;
    outside.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 720.5, .declinationDegrees = 91.0};
    const an::SkyCoordinate repaired = an::applyGaiaEpochCorrection(outside, 2016.0, 2016.0);
    EXPECT_NEAR(repaired.rightAscensionDegrees, 0.5, 1.0e-12);
    EXPECT_DOUBLE_EQ(repaired.declinationDegrees, 90.0);
}

TEST(GaiaCatalogRecordTest, AnEpochCorrectionLeavesRightAscensionAloneAtThePole)
{
    an::GaiaStarRecord record;
    record.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 200.0, .declinationDegrees = 90.0};
    record.properMotionRightAscensionMasPerYear = 3600000.0;
    record.properMotionDeclinationMasPerYear = -3600000.0;

    const an::SkyCoordinate moved = an::applyGaiaEpochCorrection(record, 2016.0, 2026.0);
    EXPECT_DOUBLE_EQ(moved.rightAscensionDegrees, 200.0);
    EXPECT_DOUBLE_EQ(moved.declinationDegrees, 80.0);
}

TEST(GaiaCatalogRecordTest, AnEpochCorrectionMovesOnlyTheComponentsTheRecordCarries)
{
    an::GaiaStarRecord declinationOnly;
    declinationOnly.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 0.0};
    declinationOnly.properMotionDeclinationMasPerYear = 3600000.0;
    const an::SkyCoordinate moved = an::applyGaiaEpochCorrection(declinationOnly, 2016.0, 2017.0);
    EXPECT_DOUBLE_EQ(moved.rightAscensionDegrees, 10.0);
    EXPECT_DOUBLE_EQ(moved.declinationDegrees, 1.0);

    an::GaiaStarRecord neither;
    neither.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 0.0};
    const an::SkyCoordinate still = an::applyGaiaEpochCorrection(neither, 2016.0, 2116.0);
    EXPECT_DOUBLE_EQ(still.rightAscensionDegrees, 10.0);
    EXPECT_DOUBLE_EQ(still.declinationDegrees, 0.0);
}

TEST(GaiaCatalogRecordTest, TheColorIndexNeedsBothBands)
{
    an::GaiaStarRecord both;
    both.magnitudeBp = 12.5;
    both.magnitudeRp = 11.25;
    ASSERT_TRUE(an::gaiaColorIndexBpMinusRp(both).has_value());
    EXPECT_DOUBLE_EQ(*an::gaiaColorIndexBpMinusRp(both), 1.25);

    an::GaiaStarRecord blueOnly;
    blueOnly.magnitudeBp = 12.5;
    EXPECT_FALSE(an::gaiaColorIndexBpMinusRp(blueOnly).has_value());

    an::GaiaStarRecord redOnly;
    redOnly.magnitudeRp = 11.25;
    EXPECT_FALSE(an::gaiaColorIndexBpMinusRp(redOnly).has_value());
    EXPECT_FALSE(an::gaiaColorIndexBpMinusRp(an::GaiaStarRecord{}).has_value());
}

TEST(GaiaCatalogRecordTest, ADesignationNamesTheSourceOrTheTilePosition)
{
    EXPECT_EQ(an::makeGaiaSourceDesignation(4295806720u), "Gaia DR3 4295806720");
    EXPECT_EQ(an::makeGaiaTileDesignation(1234, 7u), "Gaia DR3 tile 1234:7");
}

TEST(GaiaCatalogRecordTest, EntriesCarryTheGBandAndTheCorrectedPositionAndNothingElseOfGaia)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    an::GaiaStarRecord record;
    record.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 45.0};
    record.sourceIdentifier = 42u;
    record.magnitudeG = 11.25;
    record.magnitudeBp = 12.0;
    record.magnitudeRp = 10.0;
    record.properMotionDeclinationMasPerYear = 3600000.0;

    an::GaiaEntryRequest request;
    request.catalogEpochYear = 2016.0;
    request.observationEpochYear = 2017.0;
    const std::vector<an::GaiaStarRecord> records{record};
    an::Result<std::vector<an::CatalogEntry>> entries = an::buildGaiaCatalogEntries(execution, records, request);
    ASSERT_TRUE(entries.hasValue()) << entries.status().message();
    ASSERT_EQ(entries->size(), 1u);

    const an::CatalogEntry& entry = entries->front();
    EXPECT_EQ(entry.identifier, "Gaia DR3 42");
    EXPECT_DOUBLE_EQ(entry.coordinate.declinationDegrees, 46.0);
    ASSERT_TRUE(entry.magnitude.has_value());
    EXPECT_DOUBLE_EQ(*entry.magnitude, 11.25);
    EXPECT_TRUE(entry.commonName.empty());
    EXPECT_TRUE(entry.designation.empty());
    EXPECT_TRUE(entry.objectType.empty());
    EXPECT_TRUE(entry.spectralType.empty());
    EXPECT_FALSE(entry.extent.has_value());
}

TEST(GaiaCatalogRecordTest, ARecordWithNoSourceIdentifierIsNamedAfterItsTile)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    an::GaiaStarRecord first;
    first.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 1.0, .declinationDegrees = 2.0};
    first.magnitudeG = 9.0;
    an::GaiaStarRecord second = first;
    second.coordinate.rightAscensionDegrees = 3.0;
    const std::vector<an::GaiaStarRecord> records{first, second};

    an::GaiaEntryRequest named;
    named.tileIndex = 512;
    an::Result<std::vector<an::CatalogEntry>> entries = an::buildGaiaCatalogEntries(execution, records, named);
    ASSERT_TRUE(entries.hasValue()) << entries.status().message();
    ASSERT_EQ(entries->size(), 2u);
    EXPECT_EQ((*entries)[0].identifier, "Gaia DR3 tile 512:0");
    EXPECT_EQ((*entries)[1].identifier, "Gaia DR3 tile 512:1");

    const an::Status nameless = an::buildGaiaCatalogEntries(execution, records, an::GaiaEntryRequest{}).status();
    EXPECT_EQ(nameless.code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(contextCount(nameless, an::ContextKey::Completed), 0u);
}

TEST(GaiaCatalogRecordTest, EntriesRefuseWhatCannotBecomeOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    an::GaiaStarRecord record;
    record.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 45.0};
    record.sourceIdentifier = 7u;
    const std::vector<an::GaiaStarRecord> records{record};

    const an::ExecutionContext none;
    EXPECT_EQ(an::buildGaiaCatalogEntries(none, records).status().code(), an::ErrorCode::NotInitialized);

    an::GaiaEntryRequest unknownEpoch;
    unknownEpoch.observationEpochYear = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::buildGaiaCatalogEntries(execution, records, unknownEpoch).status().code(),
              an::ErrorCode::InvalidArgument);

    an::GaiaEntryRequest nothing;
    nothing.maximumEntries = 0u;
    EXPECT_EQ(an::buildGaiaCatalogEntries(execution, records, nothing).status().code(), an::ErrorCode::InvalidArgument);

    an::GaiaEntryRequest tooMany;
    tooMany.maximumEntries = 1u;
    const std::vector<an::GaiaStarRecord> pair{record, record};
    const an::Status refused = an::buildGaiaCatalogEntries(execution, pair, tooMany).status();
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(contextCount(refused, an::ContextKey::Requested), 2u);

    an::GaiaStarRecord nowhere = record;
    nowhere.coordinate.declinationDegrees = std::numeric_limits<double>::quiet_NaN();
    const std::vector<an::GaiaStarRecord> lost{record, nowhere};
    const an::Status invalid = an::buildGaiaCatalogEntries(execution, lost).status();
    EXPECT_EQ(invalid.code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(contextCount(invalid, an::ContextKey::Completed), 1u);
}

TEST(GaiaCatalogRecordTest, AnUnmeasuredMagnitudeStaysUnmeasuredRatherThanBecomingBright)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    an::GaiaStarRecord record;
    record.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 45.0};
    record.sourceIdentifier = 7u;
    const std::vector<an::GaiaStarRecord> records{record};

    an::Result<std::vector<an::CatalogEntry>> entries = an::buildGaiaCatalogEntries(execution, records);
    ASSERT_TRUE(entries.hasValue()) << entries.status().message();
    ASSERT_EQ(entries->size(), 1u);
    EXPECT_FALSE(entries->front().magnitude.has_value());
}

TEST(GaiaCatalogRecordTest, BuildingEntriesStopsWhenTheCallerAsksItTo)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 4u, stop.get_token());

    an::GaiaStarRecord record;
    record.coordinate = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 45.0};
    record.sourceIdentifier = 7u;
    const std::vector<an::GaiaStarRecord> records{record};

    stop.request_stop();
    EXPECT_EQ(an::buildGaiaCatalogEntries(execution, records).status().code(), an::ErrorCode::Cancelled);
}

} // namespace
