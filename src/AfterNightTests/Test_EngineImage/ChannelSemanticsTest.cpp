// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ChannelSemantics.h.
//
// These three vocabularies answer three different questions about the same numbers - what the
// channels are, what basis they are expressed in, and what the magnitudes mean - and the value of
// keeping them apart is entirely in the combinations they refuse together. An engine that lets a
// caller say "Lab channels in a display RGB basis" has three enumerations and one bag.
//
// Each refusal below is a rule with a reason, so the test names state the reason rather than the
// pair, and a later change that finds a legitimate case for one of them has something to argue with.

#include <AfterNightEngine/Image/ChannelSemantics.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>

namespace
{

constexpr an::ChannelSchema kAllSchemas[] = {an::ChannelSchema::Mono,  an::ChannelSchema::Rgb,  an::ChannelSchema::Rgba,
                                             an::ChannelSchema::Xyz,   an::ChannelSchema::Lab,  an::ChannelSchema::Lch,
                                             an::ChannelSchema::Hsl,   an::ChannelSchema::Hsv,  an::ChannelSchema::Hsi,
                                             an::ChannelSchema::YCbCr, an::ChannelSchema::Named};

constexpr an::ColorSpace kAllColorSpaces[] = {an::ColorSpace::Unspecified, an::ColorSpace::LinearRgb,
                                              an::ColorSpace::DisplayRgb,  an::ColorSpace::CieXyz,
                                              an::ColorSpace::CieLab,      an::ColorSpace::NarrowbandMapping};

constexpr an::NumericDomain kAllDomains[] = {an::NumericDomain::NormalizedLinear, an::NumericDomain::EncodedNonlinear,
                                             an::NumericDomain::RawSensorCode, an::NumericDomain::PhysicalScaled};

TEST(ChannelSemanticsTest, EveryEnumeratorIsRecognisedAndNamedDistinctly)
{
    for (const an::ChannelSchema schema : kAllSchemas)
    {
        EXPECT_TRUE(an::isChannelSchema(schema));
    }
    for (const an::ColorSpace space : kAllColorSpaces)
    {
        EXPECT_TRUE(an::isColorSpace(space));
    }
    for (const an::NumericDomain domain : kAllDomains)
    {
        EXPECT_TRUE(an::isNumericDomain(domain));
    }
    for (const an::ChannelSchema left : kAllSchemas)
    {
        for (const an::ChannelSchema right : kAllSchemas)
        {
            const bool sameName = std::strcmp(an::channelSchemaName(left), an::channelSchemaName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_STREQ(an::channelSchemaName(an::ChannelSchema::YCbCr), "YCbCr");
    EXPECT_STREQ(an::colorSpaceName(an::ColorSpace::NarrowbandMapping), "NarrowbandMapping");
    EXPECT_STREQ(an::numericDomainName(an::NumericDomain::RawSensorCode), "RawSensorCode");
}

TEST(ChannelSemanticsTest, AValueOutsideAnEnumerationIsRefusedRatherThanClassified)
{
    EXPECT_FALSE(an::isChannelSchema(static_cast<an::ChannelSchema>(90)));
    EXPECT_FALSE(an::isColorSpace(static_cast<an::ColorSpace>(90)));
    EXPECT_FALSE(an::isNumericDomain(static_cast<an::NumericDomain>(90)));
    EXPECT_STREQ(an::channelSchemaName(static_cast<an::ChannelSchema>(90)), "UnknownChannelSchema");
    EXPECT_STREQ(an::colorSpaceName(static_cast<an::ColorSpace>(90)), "UnknownColorSpace");
    EXPECT_STREQ(an::numericDomainName(static_cast<an::NumericDomain>(90)), "UnknownNumericDomain");
    EXPECT_FALSE(an::channelCountOf(static_cast<an::ChannelSchema>(90)).has_value());
    EXPECT_FALSE(an::isColorSpaceCompatible(static_cast<an::ChannelSchema>(90), an::ColorSpace::Unspecified));
    EXPECT_FALSE(an::isNumericDomainCompatible(static_cast<an::ColorSpace>(90), an::NumericDomain::NormalizedLinear));
    EXPECT_FALSE(
        an::isNumericDomainRepresentable(an::NumericDomain::NormalizedLinear, static_cast<an::SampleType>(90)));
}

TEST(ChannelSemanticsTest, ASchemaFixesItsChannelCountUnlessTheChannelsAreNamed)
{
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Mono), std::optional<std::uint32_t>(1u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Rgb), std::optional<std::uint32_t>(3u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Rgba), std::optional<std::uint32_t>(4u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Xyz), std::optional<std::uint32_t>(3u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Lab), std::optional<std::uint32_t>(3u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Lch), std::optional<std::uint32_t>(3u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Hsl), std::optional<std::uint32_t>(3u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Hsv), std::optional<std::uint32_t>(3u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::Hsi), std::optional<std::uint32_t>(3u));
    EXPECT_EQ(an::channelCountOf(an::ChannelSchema::YCbCr), std::optional<std::uint32_t>(3u));

    // Named channels are the one schema whose count is a property of the image rather than of the
    // schema - an SHO stack has three, a filter survey may have seven.
    EXPECT_FALSE(an::channelCountOf(an::ChannelSchema::Named).has_value());
}

TEST(ChannelSemanticsTest, OnlyRgbaCarriesAnAlphaChannel)
{
    for (const an::ChannelSchema schema : kAllSchemas)
    {
        EXPECT_EQ(an::schemaHasAlpha(schema), schema == an::ChannelSchema::Rgba);
    }
}

TEST(ChannelSemanticsTest, AnUnstatedBasisIsAllowedExceptWhereTheSchemaNamesOne)
{
    // Refusing Unspecified everywhere would force a caller with an uncalibrated sensor frame to
    // invent a colour basis it does not have.
    EXPECT_TRUE(an::isColorSpaceCompatible(an::ChannelSchema::Mono, an::ColorSpace::Unspecified));
    EXPECT_TRUE(an::isColorSpaceCompatible(an::ChannelSchema::Rgb, an::ColorSpace::Unspecified));
    EXPECT_TRUE(an::isColorSpaceCompatible(an::ChannelSchema::Named, an::ColorSpace::Unspecified));

    // Xyz, Lab and Lch each name their basis in the schema itself, so leaving it unstated would be
    // a descriptor disagreeing with itself.
    EXPECT_FALSE(an::isColorSpaceCompatible(an::ChannelSchema::Xyz, an::ColorSpace::Unspecified));
    EXPECT_FALSE(an::isColorSpaceCompatible(an::ChannelSchema::Lab, an::ColorSpace::Unspecified));
    EXPECT_FALSE(an::isColorSpaceCompatible(an::ChannelSchema::Lch, an::ColorSpace::Unspecified));
}

TEST(ChannelSemanticsTest, OneChannelCarriesNoColourBasis)
{
    EXPECT_TRUE(an::isColorSpaceCompatible(an::ChannelSchema::Mono, an::ColorSpace::Unspecified));
    for (const an::ColorSpace space : kAllColorSpaces)
    {
        if (space == an::ColorSpace::Unspecified)
        {
            continue;
        }
        EXPECT_FALSE(an::isColorSpaceCompatible(an::ChannelSchema::Mono, space)) << an::colorSpaceName(space);
    }
}

TEST(ChannelSemanticsTest, ASchemaThatNamesItsSpaceAcceptsThatSpaceAndNoOther)
{
    for (const an::ColorSpace space : kAllColorSpaces)
    {
        EXPECT_EQ(an::isColorSpaceCompatible(an::ChannelSchema::Xyz, space), space == an::ColorSpace::CieXyz);
        EXPECT_EQ(an::isColorSpaceCompatible(an::ChannelSchema::Lab, space), space == an::ColorSpace::CieLab);
        EXPECT_EQ(an::isColorSpaceCompatible(an::ChannelSchema::Lch, space), space == an::ColorSpace::CieLab);
    }
}

TEST(ChannelSemanticsTest, ASchemaDerivedFromRgbRejectsANonRgbBasis)
{
    // Hue/saturation triples and YCbCr are computed from red, green and blue, so a tristimulus or
    // perceptual basis underneath them describes numbers that were never produced.
    for (const an::ChannelSchema schema :
         {an::ChannelSchema::Hsl, an::ChannelSchema::Hsv, an::ChannelSchema::Hsi, an::ChannelSchema::YCbCr})
    {
        EXPECT_TRUE(an::isColorSpaceCompatible(schema, an::ColorSpace::LinearRgb));
        EXPECT_TRUE(an::isColorSpaceCompatible(schema, an::ColorSpace::DisplayRgb));
        EXPECT_FALSE(an::isColorSpaceCompatible(schema, an::ColorSpace::CieXyz));
        EXPECT_FALSE(an::isColorSpaceCompatible(schema, an::ColorSpace::CieLab));
        EXPECT_FALSE(an::isColorSpaceCompatible(schema, an::ColorSpace::NarrowbandMapping));
    }
}

TEST(ChannelSemanticsTest, NarrowbandMappingBelongsToRgbAndNamedChannelsOnly)
{
    EXPECT_TRUE(an::isColorSpaceCompatible(an::ChannelSchema::Rgb, an::ColorSpace::NarrowbandMapping));
    EXPECT_TRUE(an::isColorSpaceCompatible(an::ChannelSchema::Rgba, an::ColorSpace::NarrowbandMapping));
    EXPECT_TRUE(an::isColorSpaceCompatible(an::ChannelSchema::Named, an::ColorSpace::NarrowbandMapping));
    EXPECT_FALSE(an::isColorSpaceCompatible(an::ChannelSchema::Mono, an::ColorSpace::NarrowbandMapping));
    EXPECT_FALSE(an::isColorSpaceCompatible(an::ChannelSchema::Xyz, an::ColorSpace::NarrowbandMapping));
}

TEST(ChannelSemanticsTest, ASensorCodeHasNoColourBasis)
{
    // A raw code is what the sensor recorded. Naming a colorimetric basis for it claims a
    // calibration that has not happened.
    for (const an::ColorSpace space : kAllColorSpaces)
    {
        EXPECT_EQ(an::isNumericDomainCompatible(space, an::NumericDomain::RawSensorCode),
                  space == an::ColorSpace::Unspecified)
            << an::colorSpaceName(space);
    }
}

TEST(ChannelSemanticsTest, TristimulusIsLinearAndPerceptualIsNot)
{
    // XYZ is linear in light by construction, so an encoded-nonlinear XYZ is a contradiction; Lab
    // is a nonlinear function of XYZ by construction, so a linear Lab is the same contradiction
    // from the other side.
    EXPECT_TRUE(an::isNumericDomainCompatible(an::ColorSpace::CieXyz, an::NumericDomain::NormalizedLinear));
    EXPECT_TRUE(an::isNumericDomainCompatible(an::ColorSpace::CieXyz, an::NumericDomain::PhysicalScaled));
    EXPECT_FALSE(an::isNumericDomainCompatible(an::ColorSpace::CieXyz, an::NumericDomain::EncodedNonlinear));

    EXPECT_TRUE(an::isNumericDomainCompatible(an::ColorSpace::CieLab, an::NumericDomain::EncodedNonlinear));
    EXPECT_FALSE(an::isNumericDomainCompatible(an::ColorSpace::CieLab, an::NumericDomain::NormalizedLinear));
    EXPECT_FALSE(an::isNumericDomainCompatible(an::ColorSpace::CieLab, an::NumericDomain::PhysicalScaled));
}

TEST(ChannelSemanticsTest, AnRgbBasisAdmitsEveryDomainExceptTheSensorCode)
{
    for (const an::ColorSpace space :
         {an::ColorSpace::LinearRgb, an::ColorSpace::DisplayRgb, an::ColorSpace::NarrowbandMapping})
    {
        EXPECT_TRUE(an::isNumericDomainCompatible(space, an::NumericDomain::NormalizedLinear));
        EXPECT_TRUE(an::isNumericDomainCompatible(space, an::NumericDomain::EncodedNonlinear));
        EXPECT_TRUE(an::isNumericDomainCompatible(space, an::NumericDomain::PhysicalScaled));
        EXPECT_FALSE(an::isNumericDomainCompatible(space, an::NumericDomain::RawSensorCode));
    }
}

TEST(ChannelSemanticsTest, APhysicalValueNeedsAFloatingSampleToCarryItsScale)
{
    // The descriptor carries no scale factor: source BSCALE and BZERO are codec facts, not resident
    // ones, so an integer sample in a physical domain has no way to say what physical value it is.
    for (const an::SampleType type : {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16,
                                      an::SampleType::Int16, an::SampleType::UInt32, an::SampleType::Int32})
    {
        EXPECT_FALSE(an::isNumericDomainRepresentable(an::NumericDomain::PhysicalScaled, type))
            << an::sampleTypeName(type);
        EXPECT_TRUE(an::isNumericDomainRepresentable(an::NumericDomain::NormalizedLinear, type));
        EXPECT_TRUE(an::isNumericDomainRepresentable(an::NumericDomain::RawSensorCode, type));
    }
    EXPECT_TRUE(an::isNumericDomainRepresentable(an::NumericDomain::PhysicalScaled, an::SampleType::Float32));
    EXPECT_TRUE(an::isNumericDomainRepresentable(an::NumericDomain::PhysicalScaled, an::SampleType::Float64));
}

TEST(ChannelSemanticsTest, EveryRuleIsAvailableAtCompileTime)
{
    static_assert(an::channelCountOf(an::ChannelSchema::Rgba) == std::optional<std::uint32_t>(4u));
    static_assert(!an::isColorSpaceCompatible(an::ChannelSchema::Lab, an::ColorSpace::DisplayRgb));
    static_assert(!an::isNumericDomainCompatible(an::ColorSpace::DisplayRgb, an::NumericDomain::RawSensorCode));
    static_assert(!an::isNumericDomainRepresentable(an::NumericDomain::PhysicalScaled, an::SampleType::UInt16));
    SUCCEED();
}

} // namespace
