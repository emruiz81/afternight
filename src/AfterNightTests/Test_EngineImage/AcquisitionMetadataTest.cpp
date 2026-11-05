// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/AcquisitionMetadata.h.
//
// The distinction these cases exist to protect is between a fact that was not stated and a fact
// whose value happens to be zero. A card bag that holds both reads a missing `EXPTIME` and an
// `EXPTIME` of nothing identically - and the first thing that divides by one of them finds out which
// it was.
//
// So every numeric case checks the absent form as well as the stated one, and the validation cases
// check that a stated value an instrument could not have produced is refused rather than carried.

#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace
{

TEST(AcquisitionMetadataTest, ARecordThatStatesNothingIsTheDefaultAndSaysSo)
{
    const an::AcquisitionMetadata record;
    EXPECT_TRUE(record.instrumentName().empty());
    EXPECT_TRUE(record.telescopeName().empty());
    EXPECT_TRUE(record.observerName().empty());
    EXPECT_TRUE(record.objectName().empty());
    EXPECT_TRUE(record.filterName().empty());
    EXPECT_FALSE(record.exposureSeconds().has_value());
    EXPECT_FALSE(record.sensorTemperatureCelsius().has_value());
    EXPECT_FALSE(record.gainElectronsPerAdu().has_value());
    EXPECT_FALSE(record.binning().has_value());
    EXPECT_FALSE(record.exposureStart().has_value());
}

TEST(AcquisitionMetadataTest, EveryStatedFactIsCarriedAndReadsBackUnchanged)
{
    an::AcquisitionMetadataRequest request;
    request.instrumentName = "ASI2600MM";
    request.telescopeName = "TSA-120";
    request.observerName = "someone";
    request.objectName = "NGC 7000";
    request.filterName = "Ha";
    request.exposureSeconds = 300.0;
    request.sensorTemperatureCelsius = -10.0;
    request.gainElectronsPerAdu = 0.25;
    request.binning = an::PixelBinning{.horizontal = 2u, .vertical = 2u};
    request.exposureStart = an::UtcInstant{.nanosecondsSinceEpoch = 1'700'000'000'123'456'789};

    an::Result<an::AcquisitionMetadata> record = an::AcquisitionMetadata::create(request);
    ASSERT_TRUE(record.hasValue()) << record.status().message();

    EXPECT_EQ(record->instrumentName(), "ASI2600MM");
    EXPECT_EQ(record->telescopeName(), "TSA-120");
    EXPECT_EQ(record->observerName(), "someone");
    EXPECT_EQ(record->objectName(), "NGC 7000");
    EXPECT_EQ(record->filterName(), "Ha");
    ASSERT_TRUE(record->exposureSeconds().has_value());
    EXPECT_DOUBLE_EQ(*record->exposureSeconds(), 300.0);
    ASSERT_TRUE(record->sensorTemperatureCelsius().has_value());
    EXPECT_DOUBLE_EQ(*record->sensorTemperatureCelsius(), -10.0);
    ASSERT_TRUE(record->gainElectronsPerAdu().has_value());
    EXPECT_DOUBLE_EQ(*record->gainElectronsPerAdu(), 0.25);
    ASSERT_TRUE(record->binning().has_value());
    EXPECT_EQ(record->binning()->horizontal, 2u);
    EXPECT_EQ(record->binning()->vertical, 2u);
    ASSERT_TRUE(record->exposureStart().has_value());
    EXPECT_EQ(record->exposureStart()->nanosecondsSinceEpoch, 1'700'000'000'123'456'789);
}

TEST(AcquisitionMetadataTest, TheRecordRoundTripsThroughItsRequest)
{
    an::AcquisitionMetadataRequest request;
    request.instrumentName = "camera";
    request.exposureSeconds = 60.0;
    request.binning = an::PixelBinning{.horizontal = 1u, .vertical = 1u};

    an::Result<an::AcquisitionMetadata> record = an::AcquisitionMetadata::create(request);
    ASSERT_TRUE(record.hasValue()) << record.status().message();

    an::Result<an::AcquisitionMetadata> rebuilt = an::AcquisitionMetadata::create(record->toRequest());
    ASSERT_TRUE(rebuilt.hasValue()) << rebuilt.status().message();
    EXPECT_EQ(*rebuilt, *record);
}

TEST(AcquisitionMetadataTest, AnAbsentExposureIsNotAnExposureOfZero)
{
    an::AcquisitionMetadataRequest absent;
    an::Result<an::AcquisitionMetadata> unstated = an::AcquisitionMetadata::create(absent);
    ASSERT_TRUE(unstated.hasValue()) << unstated.status().message();
    EXPECT_FALSE(unstated->exposureSeconds().has_value());

    an::AcquisitionMetadataRequest zero;
    zero.exposureSeconds = 0.0;
    an::Result<an::AcquisitionMetadata> refused = an::AcquisitionMetadata::create(zero);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(AcquisitionMetadataTest, AStatedFactNoInstrumentProducedIsRefusedAndNamesItsField)
{
    struct Case
    {
        an::AcquisitionMetadataRequest request;
        const char* parameter;
    };

    an::AcquisitionMetadataRequest negativeExposure;
    negativeExposure.exposureSeconds = -1.0;
    an::AcquisitionMetadataRequest infiniteExposure;
    infiniteExposure.exposureSeconds = std::numeric_limits<double>::infinity();
    an::AcquisitionMetadataRequest belowAbsoluteZero;
    belowAbsoluteZero.sensorTemperatureCelsius = -400.0;
    an::AcquisitionMetadataRequest notATemperature;
    notATemperature.sensorTemperatureCelsius = std::nan("");
    an::AcquisitionMetadataRequest zeroGain;
    zeroGain.gainElectronsPerAdu = 0.0;
    an::AcquisitionMetadataRequest zeroBinning;
    zeroBinning.binning = an::PixelBinning{.horizontal = 0u, .vertical = 1u};

    const Case cases[] = {{negativeExposure, "exposureSeconds"},
                          {infiniteExposure, "exposureSeconds"},
                          {belowAbsoluteZero, "sensorTemperatureCelsius"},
                          {notATemperature, "sensorTemperatureCelsius"},
                          {zeroGain, "gainElectronsPerAdu"},
                          {zeroBinning, "binning"}};

    for (const Case& one : cases)
    {
        an::Result<an::AcquisitionMetadata> refused = an::AcquisitionMetadata::create(one.request);
        ASSERT_FALSE(refused.hasValue()) << one.parameter;
        EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument) << one.parameter;
        const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
        ASSERT_NE(field, nullptr) << one.parameter;
        EXPECT_EQ(field->text, one.parameter);
    }
}

TEST(AcquisitionMetadataTest, AnInstantBeforeTheEpochIsCarriedRatherThanWrapped)
{
    an::AcquisitionMetadataRequest request;
    request.exposureStart = an::UtcInstant{.nanosecondsSinceEpoch = -1'000'000'000};

    an::Result<an::AcquisitionMetadata> record = an::AcquisitionMetadata::create(request);
    ASSERT_TRUE(record.hasValue()) << record.status().message();
    ASSERT_TRUE(record->exposureStart().has_value());
    EXPECT_EQ(record->exposureStart()->nanosecondsSinceEpoch, -1'000'000'000);
}

TEST(AcquisitionMetadataTest, TwoRecordsAreEqualExactlyWhenEveryFactMatchesIncludingAbsence)
{
    an::AcquisitionMetadataRequest request;
    request.objectName = "M31";
    request.exposureSeconds = 120.0;

    an::Result<an::AcquisitionMetadata> left = an::AcquisitionMetadata::create(request);
    an::Result<an::AcquisitionMetadata> right = an::AcquisitionMetadata::create(request);
    ASSERT_TRUE(left.hasValue());
    ASSERT_TRUE(right.hasValue());
    EXPECT_EQ(*left, *right);

    an::AcquisitionMetadataRequest withoutExposure = request;
    withoutExposure.exposureSeconds.reset();
    an::Result<an::AcquisitionMetadata> different = an::AcquisitionMetadata::create(withoutExposure);
    ASSERT_TRUE(different.hasValue());
    EXPECT_NE(*different, *left);
}

TEST(AcquisitionMetadataTest, TheTwoSmallValueTypesCompareByTheirNumbers)
{
    EXPECT_EQ((an::UtcInstant{.nanosecondsSinceEpoch = 7}), (an::UtcInstant{.nanosecondsSinceEpoch = 7}));
    EXPECT_NE((an::UtcInstant{.nanosecondsSinceEpoch = 7}), (an::UtcInstant{.nanosecondsSinceEpoch = 8}));
    EXPECT_EQ((an::PixelBinning{.horizontal = 2u, .vertical = 3u}),
              (an::PixelBinning{.horizontal = 2u, .vertical = 3u}));
    EXPECT_NE((an::PixelBinning{.horizontal = 2u, .vertical = 3u}),
              (an::PixelBinning{.horizontal = 3u, .vertical = 2u}));
}

} // namespace

TEST(AcquisitionMetadataTest, RawCameraRefusalsNameTheRejectedField)
{
    an::RawCameraMetadata camera;
    camera.isoSpeed = -1.0;
    const auto invalid = an::validateRawCameraMetadata(camera);
    EXPECT_EQ(invalid.code(), an::ErrorCode::InvalidArgument);
    EXPECT_STREQ(invalid.operation(), "an::validateRawCameraMetadata");
    ASSERT_NE(invalid.findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(invalid.findContext(an::ContextKey::Parameter)->text, "isoSpeed");
    camera.isoSpeed = 0.0;
    camera.blackPatternRows = 1u;
    const auto pattern = an::validateRawCameraMetadata(camera);
    ASSERT_NE(pattern.findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(pattern.findContext(an::ContextKey::Parameter)->text, "blackPattern");
}
