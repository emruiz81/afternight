// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/DescriptorText.h.
//
// The text form exists so that a test can state a whole descriptor in one readable line and so a
// failure can print one. That only holds if the form is lossless, which is what most of these cases
// prove: every descriptor formats to text that parses back to exactly the descriptor it came from,
// including the pixel aspect, which is the one field a careless decimal conversion would round.
//
// The parser is deliberately strict. It is not reading a file format - the codecs own that - so
// there is no reason for it to guess at a missing field or ignore one it does not recognise, and
// every way of getting it wrong reports which way it was.

#include <AfterNightEngine/Image/DescriptorText.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

namespace
{

const an::EngineLimits& limits()
{
    static const an::EngineLimits kLimits = an::defaultEngineLimits();
    return kLimits;
}

an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channelCount, limits());
    EXPECT_TRUE(extent.has_value());
    return extent.value_or(an::ImageExtent::create(1u, 1u, 1u, limits()).value());
}

an::ImageDescriptorRequest linearRgbRequest()
{
    return an::ImageDescriptorRequest{.extent = extentOf(640u, 480u, 3u),
                                      .sampleType = an::SampleType::Float32,
                                      .channelSchema = an::ChannelSchema::Rgb,
                                      .colorSpace = an::ColorSpace::LinearRgb,
                                      .numericDomain = an::NumericDomain::NormalizedLinear,
                                      .cfa = an::CfaDescriptor{},
                                      .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                      .pixelAspectRatio = 1.0};
}

an::ImageDescriptor descriptorOf(const an::ImageDescriptorRequest& request)
{
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value_or(an::ImageDescriptor::create(linearRgbRequest()).value());
}

TEST(DescriptorTextTest, TheCanonicalFormNamesEveryFieldOnce)
{
    EXPECT_EQ(an::formatImageDescriptor(descriptorOf(linearRgbRequest())),
              std::string("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                          "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1"));
}

/// One real camera table, and the letters the canonical form writes it as.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// A mosaic descriptor request carrying that table.
///
/// @param originX the horizontal phase.
/// @param originY the vertical phase.
/// @return the request, describing a single-channel mosaic frame.
an::ImageDescriptorRequest tabulatedMosaicRequest(std::uint32_t originX, std::uint32_t originY)
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(kCameraTable, originX, originY);
    EXPECT_TRUE(cfa.has_value());
    return an::ImageDescriptorRequest{.extent = extentOf(640u, 480u, 1u),
                                      .sampleType = an::SampleType::UInt16,
                                      .channelSchema = an::ChannelSchema::Mono,
                                      .colorSpace = an::ColorSpace::Unspecified,
                                      .numericDomain = an::NumericDomain::RawSensorCode,
                                      .cfa = cfa.value_or(an::CfaDescriptor{}),
                                      .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                      .pixelAspectRatio = 1.0};
}

TEST(DescriptorTextTest, ATabulatedMosaicWritesItsTableIntoTheFilterArrayField)
{
    EXPECT_EQ(an::formatImageDescriptor(descriptorOf(tabulatedMosaicRequest(1u, 2u))),
              std::string("extent=640x480x1 sample=UInt16 channels=Mono color=Unspecified "
                          "domain=RawSensorCode cfa=XTrans@1,2:GGRGGBGGBGGRBRGRBGGGBGGRGGRGGBRBGBRG "
                          "origin=TopLeft aspect=1"));
}

TEST(DescriptorTextTest, ATabulatedMosaicSurvivesTheRoundTripEntryForEntry)
{
    const an::ImageDescriptor descriptor = descriptorOf(tabulatedMosaicRequest(3u, 5u));
    const std::optional<an::ImageDescriptor> parsed =
        an::parseImageDescriptor(an::formatImageDescriptor(descriptor), limits());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, descriptor);
    EXPECT_EQ(parsed->cfa().filterTable().entries(), kCameraTable);
    EXPECT_EQ(parsed->cfa().originX(), 3u);
    EXPECT_EQ(parsed->cfa().originY(), 5u);
}

TEST(DescriptorTextTest, AFilterArrayFieldThatContradictsItselfIsRefusedForThatReason)
{
    const std::string prefix = "extent=640x480x1 sample=UInt16 channels=Mono color=Unspecified domain=RawSensorCode ";
    const std::string suffix = " origin=TopLeft aspect=1";
    const std::string table = "GGRGGBGGBGGRBRGRBGGGBGGRGGRGGBRBGBRG";
    EXPECT_EQ(an::checkImageDescriptorText(prefix + "cfa=XTrans@0,0" + suffix, limits()),
              an::DescriptorError::CfaTableRequired);
    EXPECT_EQ(an::checkImageDescriptorText(prefix + "cfa=Rggb@0,0:" + table + suffix, limits()),
              an::DescriptorError::CfaTableNotAllowed);
    EXPECT_EQ(an::checkImageDescriptorText(prefix + "cfa=XTrans@0,0:" + table.substr(1u) + suffix, limits()),
              an::DescriptorError::MalformedText);
    EXPECT_EQ(an::checkImageDescriptorText(prefix + "cfa=XTrans@0,0:Y" + table.substr(1u) + suffix, limits()),
              an::DescriptorError::UnknownCfaColor);
    std::string wrongCounts = table;
    wrongCounts[2] = 'G';
    EXPECT_EQ(an::checkImageDescriptorText(prefix + "cfa=XTrans@0,0:" + wrongCounts + suffix, limits()),
              an::DescriptorError::CfaTableColorCountsWrong);
    EXPECT_EQ(an::checkImageDescriptorText(prefix + "cfa=XTrans@6,0:" + table + suffix, limits()),
              an::DescriptorError::CfaOriginOutOfPattern);
}

TEST(DescriptorTextTest, AMosaicedFrameCarriesItsPatternAndPhase)
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.extent = extentOf(6248u, 4176u, 1u);
    request.sampleType = an::SampleType::UInt16;
    request.channelSchema = an::ChannelSchema::Mono;
    request.colorSpace = an::ColorSpace::Unspecified;
    request.numericDomain = an::NumericDomain::RawSensorCode;
    request.cfa = an::CfaDescriptor::create(an::CfaPattern::Grbg, 1u, 0u).value();
    request.coordinateOrigin = an::CoordinateOrigin::BottomLeft;
    EXPECT_EQ(an::formatImageDescriptor(descriptorOf(request)),
              std::string("extent=6248x4176x1 sample=UInt16 channels=Mono color=Unspecified "
                          "domain=RawSensorCode cfa=Grbg@1,0 origin=BottomLeft aspect=1"));
}

TEST(DescriptorTextTest, EveryDescriptorSurvivesARoundTrip)
{
    an::ImageDescriptorRequest anamorphic = linearRgbRequest();
    anamorphic.pixelAspectRatio = 1.3333333333333333;

    an::ImageDescriptorRequest perceptual = linearRgbRequest();
    perceptual.sampleType = an::SampleType::Float64;
    perceptual.channelSchema = an::ChannelSchema::Lch;
    perceptual.colorSpace = an::ColorSpace::CieLab;
    perceptual.numericDomain = an::NumericDomain::EncodedNonlinear;

    an::ImageDescriptorRequest narrowband = linearRgbRequest();
    narrowband.extent = extentOf(1u, 1u, 7u);
    narrowband.channelSchema = an::ChannelSchema::Named;
    narrowband.colorSpace = an::ColorSpace::NarrowbandMapping;
    narrowband.pixelAspectRatio = 0.5;

    for (const an::ImageDescriptorRequest& request : {linearRgbRequest(), anamorphic, perceptual, narrowband})
    {
        const an::ImageDescriptor descriptor = descriptorOf(request);
        const std::string text = an::formatImageDescriptor(descriptor);
        EXPECT_EQ(an::checkImageDescriptorText(text, limits()), an::DescriptorError::None) << text;
        const std::optional<an::ImageDescriptor> parsed = an::parseImageDescriptor(text, limits());
        ASSERT_TRUE(parsed.has_value()) << text;
        EXPECT_EQ(*parsed, descriptor) << text;
        EXPECT_EQ(an::formatImageDescriptor(*parsed), text);
    }
}

TEST(DescriptorTextTest, ThePixelAspectSurvivesExactlyRatherThanApproximately)
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.pixelAspectRatio = 1.0 / 3.0;
    const an::ImageDescriptor descriptor = descriptorOf(request);
    const std::optional<an::ImageDescriptor> parsed =
        an::parseImageDescriptor(an::formatImageDescriptor(descriptor), limits());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->pixelAspectRatio(), 1.0 / 3.0);
}

TEST(DescriptorTextTest, FieldsMayBeGivenInAnyOrder)
{
    const std::optional<an::ImageDescriptor> parsed =
        an::parseImageDescriptor("aspect=1 origin=TopLeft cfa=None@0,0 domain=NormalizedLinear "
                                 "color=LinearRgb channels=Rgb sample=Float32 extent=640x480x3",
                                 limits());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, descriptorOf(linearRgbRequest()));
}

TEST(DescriptorTextTest, AFieldThatIsMissingRepeatedOrUnknownIsRefusedByName)
{
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft",
                                           limits()),
              an::DescriptorError::MissingField);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 extent=640x480x3 sample=Float32 channels=Rgb "
                                           "color=LinearRgb domain=NormalizedLinear cfa=None@0,0 "
                                           "origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::DuplicateField);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1 "
                                           "validity=AllValid",
                                           limits()),
              an::DescriptorError::UnknownField);
}

TEST(DescriptorTextTest, TextThatIsNotKeyValuePairsIsRefused)
{
    for (const char* text : {"", "   ", "extent", "extent=", "=Float32",
                             "extent=640x480 sample=Float32 channels=Rgb color=LinearRgb "
                             "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1",
                             "extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                             "domain=NormalizedLinear cfa=Rggb origin=TopLeft aspect=1",
                             "extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                             "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=one"})
    {
        EXPECT_EQ(an::checkImageDescriptorText(text, limits()), an::DescriptorError::MalformedText) << text;
        EXPECT_FALSE(an::parseImageDescriptor(text, limits()).has_value()) << text;
    }
}

TEST(DescriptorTextTest, AValueThatNamesNoEnumeratorIsRefusedAsThatFieldsUnknownValue)
{
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float128 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::UnknownSampleType);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Cmyk color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::UnknownChannelSchema);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=ProPhoto "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::UnknownColorSpace);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=Logarithmic cfa=None@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::UnknownNumericDomain);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=Quad@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::UnknownCfaPattern);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=Centre aspect=1",
                                           limits()),
              an::DescriptorError::UnknownCoordinateOrigin);
}

TEST(DescriptorTextTest, TextThatParsesIntoAnImpossibleCombinationIsRefusedForThatReason)
{
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x4 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::ChannelCountMismatch);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=UInt16 channels=Rgb color=LinearRgb "
                                           "domain=PhysicalScaled cfa=None@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::NumericDomainNotRepresentable);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@1,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::CfaOriginNotZero);
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=0",
                                           limits()),
              an::DescriptorError::PixelAspectNotPositive);
}

TEST(DescriptorTextTest, AnExtentThePassedLimitsRefuseIsReportedAsSuch)
{
    an::EngineLimits small = an::defaultEngineLimits();
    small.maxImageWidth = 32u;
    EXPECT_EQ(an::checkImageDescriptorText("extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1",
                                           small),
              an::DescriptorError::ExtentRefused);
    EXPECT_EQ(an::checkImageDescriptorText("extent=0x480x3 sample=Float32 channels=Rgb color=LinearRgb "
                                           "domain=NormalizedLinear cfa=None@0,0 origin=TopLeft aspect=1",
                                           limits()),
              an::DescriptorError::ExtentRefused);
}

} // namespace
