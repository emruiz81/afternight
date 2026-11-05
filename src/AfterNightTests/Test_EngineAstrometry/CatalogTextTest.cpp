// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Bounded parsing of a catalog text: what it accepts, what it repairs and what it refuses.

#include <AfterNightEngine/Algorithms/Catalog/CatalogText.h>

#include "CatalogTestSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>

namespace
{

using catalog_test::makeEngine;
using catalog_test::makeExecution;

constexpr std::string_view kObjectText =
    "# a deep-sky table\r\n"
    "id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,magnitude\r\n"
    "M 1,Crab Nebula,83.6250,22.0167,7.0,5.0,30,Bright Nebula,8.4\r\n"
    "\r\n"
    "NGC 7000,\"North America Nebula, the\",314.75,44.3167,120.0,,0,Bright Nebula,4.0\r\n"
    "NGC 4,,1.85,8.3667,0.0,0.0,0,Galaxy,99.0\r\n";

constexpr std::string_view kStarText = "id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                                       "HIP 32349,Sirius,alpha CMa,101.287215,-16.716116,-1.44,A0m...\n"
                                       "HIP 30438,Canopus,alpha Car,95.987925,-52.69566,-0.62,F0Ib\n";

[[nodiscard]] an::CatalogTextRequest starRequest()
{
    an::CatalogTextRequest request;
    request.schema = an::CatalogTextSchema::Star;
    return request;
}

TEST(CatalogTextTest, ReadsEveryExtendedObjectColumn)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, kObjectText);
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    ASSERT_EQ(parsed->entries.size(), 3u);

    const an::CatalogEntry& crab = parsed->entries[0];
    EXPECT_EQ(crab.identifier, "M 1");
    EXPECT_EQ(crab.commonName, "Crab Nebula");
    EXPECT_EQ(crab.objectType, "Bright Nebula");
    EXPECT_DOUBLE_EQ(crab.coordinate.rightAscensionDegrees, 83.6250);
    EXPECT_DOUBLE_EQ(crab.coordinate.declinationDegrees, 22.0167);
    ASSERT_TRUE(crab.magnitude.has_value());
    EXPECT_DOUBLE_EQ(*crab.magnitude, 8.4);
    ASSERT_TRUE(crab.extent.has_value());
    EXPECT_DOUBLE_EQ(crab.extent->majorAxisArcminutes, 7.0);
    EXPECT_DOUBLE_EQ(crab.extent->minorAxisArcminutes, 5.0);
    EXPECT_DOUBLE_EQ(crab.extent->positionAngleDegrees, 30.0);

    // A quoted field carries its comma, and a blank minor axis takes the major axis.
    const an::CatalogEntry& northAmerica = parsed->entries[1];
    EXPECT_EQ(northAmerica.commonName, "North America Nebula, the");
    ASSERT_TRUE(northAmerica.extent.has_value());
    EXPECT_DOUBLE_EQ(northAmerica.extent->minorAxisArcminutes, 120.0);

    EXPECT_EQ(parsed->report.entryCount, 3u);
    EXPECT_EQ(parsed->report.lineCount, 6u);
    EXPECT_EQ(parsed->report.commentLineCount, 1u);
    EXPECT_EQ(parsed->report.headerLineCount, 1u);
    EXPECT_EQ(parsed->report.blankLineCount, 1u);
    EXPECT_EQ(parsed->report.byteCount, kObjectText.size());
}

TEST(CatalogTextTest, ReadsEveryStarColumn)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, kStarText, starRequest());
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    ASSERT_EQ(parsed->entries.size(), 2u);

    const an::CatalogEntry& sirius = parsed->entries[0];
    EXPECT_EQ(sirius.identifier, "HIP 32349");
    EXPECT_EQ(sirius.commonName, "Sirius");
    EXPECT_EQ(sirius.designation, "alpha CMa");
    EXPECT_EQ(sirius.spectralType, "A0m...");
    ASSERT_TRUE(sirius.magnitude.has_value());
    EXPECT_DOUBLE_EQ(*sirius.magnitude, -1.44);
    EXPECT_FALSE(sirius.extent.has_value());
    EXPECT_TRUE(sirius.objectType.empty());
}

TEST(CatalogTextTest, TheUnknownMagnitudeSentinelIsNoMeasurement)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, kObjectText);
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    ASSERT_EQ(parsed->entries.size(), 3u);
    EXPECT_FALSE(parsed->entries[2].magnitude.has_value());
    EXPECT_EQ(parsed->report.unknownMagnitudeCount, 1u);

    // A provider that writes no sentinel says so, and then ninety-nine is a magnitude.
    an::CatalogTextRequest request;
    request.unknownMagnitudeAtOrAbove.reset();
    const an::Result<an::ParsedCatalogText> literal = an::parseCatalogText(execution, kObjectText, request);
    ASSERT_TRUE(literal.hasValue()) << literal.status().message();
    ASSERT_TRUE(literal->entries[2].magnitude.has_value());
    EXPECT_DOUBLE_EQ(*literal->entries[2].magnitude, 99.0);
    EXPECT_EQ(literal->report.unknownMagnitudeCount, 0u);
}

TEST(CatalogTextTest, ABlankMagnitudeIsNoMeasurement)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::string_view text = "NGC 1,,10.0,10.0,1.0,1.0,0,Galaxy,\n";

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text);
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    ASSERT_EQ(parsed->entries.size(), 1u);
    EXPECT_FALSE(parsed->entries[0].magnitude.has_value());
}

TEST(CatalogTextTest, DoubledQuotesAreOneQuote)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::string_view text = "NGC 1,\"the \"\"blue\"\" one\",10.0,10.0,1.0,1.0,0,Galaxy,7.5\n";

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text);
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    ASSERT_EQ(parsed->entries.size(), 1u);
    EXPECT_EQ(parsed->entries[0].commonName, "the \"blue\" one");
}

TEST(CatalogTextTest, AnglesAreFoldedAndClampedAndCounted)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::string_view text = "NGC 1,,370.5,95.0,-2.0,1.0,0,Galaxy,7.5\n"
                                      "NGC 2,,-10.0,-95.0,1.0,1.0,0,Galaxy,7.6\n"
                                      "NGC 3,,10.0,10.0,1.0,1.0,0,Galaxy,7.7\n";

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text);
    ASSERT_TRUE(parsed.hasValue()) << parsed.status().message();
    ASSERT_EQ(parsed->entries.size(), 3u);
    EXPECT_DOUBLE_EQ(parsed->entries[0].coordinate.rightAscensionDegrees, 10.5);
    EXPECT_DOUBLE_EQ(parsed->entries[0].coordinate.declinationDegrees, 90.0);
    EXPECT_DOUBLE_EQ(parsed->entries[1].coordinate.rightAscensionDegrees, 350.0);
    EXPECT_DOUBLE_EQ(parsed->entries[1].coordinate.declinationDegrees, -90.0);
    ASSERT_TRUE(parsed->entries[0].extent.has_value());
    EXPECT_DOUBLE_EQ(parsed->entries[0].extent->majorAxisArcminutes, 0.0);
    EXPECT_EQ(parsed->report.normalizedRightAscensionCount, 2u);
    EXPECT_EQ(parsed->report.clampedDeclinationCount, 2u);
}

TEST(CatalogTextTest, AMalformedRowStopsTheParse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::string_view shortRow = "NGC 1,,10.0,10.0,1.0,1.0,0,Galaxy\n";
    constexpr std::string_view emptyIdentifier = "NGC 1,,10.0,10.0,1.0,1.0,0,Galaxy,7.5\n"
                                                 ",,10.0,10.0,1.0,1.0,0,Galaxy,7.5\n";
    constexpr std::string_view badNumber = "NGC 1,,ten,10.0,1.0,1.0,0,Galaxy,7.5\n";
    constexpr std::string_view trailingText = "NGC 1,,10.0deg,10.0,1.0,1.0,0,Galaxy,7.5\n";

    for (const std::string_view text : {shortRow, emptyIdentifier, badNumber, trailingText})
    {
        const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text);
        ASSERT_FALSE(parsed.hasValue()) << "text was accepted: " << text;
        EXPECT_EQ(parsed.status().code(), an::ErrorCode::InvalidData);
    }
}

TEST(CatalogTextTest, AMalformedRowSaysWhereItStopped)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::string_view text = "# comment\n"
                                      "NGC 1,,10.0,10.0,1.0,1.0,0,Galaxy,7.5\n"
                                      "NGC 2,,10.0,not-a-number,1.0,1.0,0,Galaxy,7.6\n";

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text);
    ASSERT_FALSE(parsed.hasValue());
    const an::StatusDetail* const detail = parsed.status().detail();
    ASSERT_NE(detail, nullptr);
    bool sawField = false;
    bool sawLines = false;
    for (const an::StatusContextField& field : detail->context)
    {
        if (field.key == an::ContextKey::Parameter && field.kind == an::ContextValueKind::Text)
        {
            EXPECT_EQ(field.text, "dec_deg");
            sawField = true;
        }
        if (field.key == an::ContextKey::Completed && field.kind == an::ContextValueKind::Count)
        {
            EXPECT_EQ(field.count, 2u);
            sawLines = true;
        }
    }
    EXPECT_TRUE(sawField);
    EXPECT_TRUE(sawLines);
}

TEST(CatalogTextTest, TextWithNoEntryIsAnEmptySelection)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    for (const std::string_view text : {std::string_view{}, std::string_view{"# nothing here\n\n"}})
    {
        const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text);
        ASSERT_FALSE(parsed.hasValue());
        EXPECT_EQ(parsed.status().code(), an::ErrorCode::EmptySelection);
    }
}

TEST(CatalogTextTest, EveryLimitRefusesRatherThanTruncates)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::string text = "NGC 1,,10.0,10.0,1.0,1.0,0,Galaxy,7.5\n"
                             "NGC 2,,11.0,10.0,1.0,1.0,0,Galaxy,7.6\n";

    {
        an::CatalogTextRequest request;
        request.limits.maximumBytes = text.size() - 1u;
        const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text, request);
        ASSERT_FALSE(parsed.hasValue());
        EXPECT_EQ(parsed.status().code(), an::ErrorCode::LimitExceeded);
    }
    {
        an::CatalogTextRequest request;
        request.limits.maximumEntries = 1u;
        const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text, request);
        ASSERT_FALSE(parsed.hasValue());
        EXPECT_EQ(parsed.status().code(), an::ErrorCode::LimitExceeded);
    }
    {
        an::CatalogTextRequest request;
        request.limits.maximumLineBytes = 10u;
        const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text, request);
        ASSERT_FALSE(parsed.hasValue());
        EXPECT_EQ(parsed.status().code(), an::ErrorCode::LimitExceeded);
    }
    {
        an::CatalogTextRequest request;
        request.limits.maximumFieldBytes = 3u;
        const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text, request);
        ASSERT_FALSE(parsed.hasValue());
        EXPECT_EQ(parsed.status().code(), an::ErrorCode::LimitExceeded);
    }
}

TEST(CatalogTextTest, AnUnknownSchemaOrZeroLimitIsAnArgumentMistake)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::string_view text = "NGC 1,,10.0,10.0,1.0,1.0,0,Galaxy,7.5\n";

    an::CatalogTextRequest unknownSchema;
    unknownSchema.schema = static_cast<an::CatalogTextSchema>(9);
    const an::Result<an::ParsedCatalogText> bySchema = an::parseCatalogText(execution, text, unknownSchema);
    ASSERT_FALSE(bySchema.hasValue());
    EXPECT_EQ(bySchema.status().code(), an::ErrorCode::InvalidArgument);

    an::CatalogTextRequest zeroLimit;
    zeroLimit.limits.maximumEntries = 0u;
    const an::Result<an::ParsedCatalogText> byLimit = an::parseCatalogText(execution, text, zeroLimit);
    ASSERT_FALSE(byLimit.hasValue());
    EXPECT_EQ(byLimit.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CatalogTextTest, WithoutAnExecutionThereIsNothingToParseWith)
{
    const an::ExecutionContext nothing;
    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(nothing, kObjectText);
    ASSERT_FALSE(parsed.hasValue());
    EXPECT_EQ(parsed.status().code(), an::ErrorCode::NotInitialized);
}

TEST(CatalogTextTest, AStoppedParseReturnsCancellation)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 4u, stop.get_token());
    stop.request_stop();

    const an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, kObjectText);
    ASSERT_FALSE(parsed.hasValue());
    EXPECT_TRUE(an::isCancellation(parsed.status().code()));
}

TEST(CatalogTextTest, SchemaNamesAreStable)
{
    EXPECT_STREQ(an::catalogTextSchemaName(an::CatalogTextSchema::ExtendedObject), "ExtendedObject");
    EXPECT_STREQ(an::catalogTextSchemaName(an::CatalogTextSchema::Star), "Star");
    EXPECT_STREQ(an::catalogTextSchemaName(static_cast<an::CatalogTextSchema>(9)), "UnknownCatalogTextSchema");
    EXPECT_TRUE(an::isCatalogTextSchema(an::CatalogTextSchema::Star));
    EXPECT_FALSE(an::isCatalogTextSchema(static_cast<an::CatalogTextSchema>(9)));
}

} // namespace
