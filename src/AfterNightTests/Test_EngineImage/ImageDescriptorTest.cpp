// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ImageDescriptor.h.
//
// The descriptor is where the separate vocabularies meet, so it is where a combination that is
// impossible - rather than merely unusual - has to be refused. Every refusal below exists because
// the two fields it names cannot both be true of one image, and the test says which two.
//
// The order the rules are applied in is itself a contract, and it matches the geometry types: a
// value outside an enumeration is refused before any rule that would have to interpret it, so
// raising a policy never turns one refusal into a differently named one.

#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace
{

an::ImageExtent extentOf(std::uint32_t channelCount)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(64u, 48u, channelCount, limits);
    EXPECT_TRUE(extent.has_value());
    return extent.value_or(an::ImageExtent::create(1u, 1u, 1u, limits).value());
}

/// A linear RGB frame in normalized values: the combination nothing in the engine argues with, and
/// the base every case below changes exactly one field of.
an::ImageDescriptorRequest linearRgbRequest()
{
    return an::ImageDescriptorRequest{.extent = extentOf(3u),
                                      .sampleType = an::SampleType::Float32,
                                      .channelSchema = an::ChannelSchema::Rgb,
                                      .colorSpace = an::ColorSpace::LinearRgb,
                                      .numericDomain = an::NumericDomain::NormalizedLinear,
                                      .cfa = an::CfaDescriptor{},
                                      .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                      .pixelAspectRatio = 1.0};
}

/// A mosaiced sensor frame: one channel of raw codes with a filter pattern over it.
an::ImageDescriptorRequest mosaicRequest()
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.extent = extentOf(1u);
    request.sampleType = an::SampleType::UInt16;
    request.channelSchema = an::ChannelSchema::Mono;
    request.colorSpace = an::ColorSpace::Unspecified;
    request.numericDomain = an::NumericDomain::RawSensorCode;
    request.cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u).value();
    return request;
}

TEST(ImageDescriptorTest, AnOrdinaryFrameIsAcceptedAndReadsBackWhatItWasGiven)
{
    const an::ImageDescriptorRequest request = linearRgbRequest();
    ASSERT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::None);
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->extent(), request.extent);
    EXPECT_EQ(descriptor->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(descriptor->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(descriptor->colorSpace(), an::ColorSpace::LinearRgb);
    EXPECT_EQ(descriptor->numericDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(descriptor->cfa(), an::CfaDescriptor{});
    EXPECT_EQ(descriptor->coordinateOrigin(), an::CoordinateOrigin::TopLeft);
    EXPECT_EQ(descriptor->pixelAspectRatio(), 1.0);
    EXPECT_FALSE(descriptor->isMosaiced());
}

TEST(ImageDescriptorTest, AMosaicedSensorFrameIsAccepted)
{
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(mosaicRequest());
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_TRUE(descriptor->isMosaiced());
    EXPECT_EQ(descriptor->cfa().pattern(), an::CfaPattern::Rggb);
}

TEST(ImageDescriptorTest, ARequestReadBackFromADescriptorRebuildsIt)
{
    // The explicit transition: an algorithm that changes what an image means takes the request
    // apart, states the change, and builds a new descriptor. There is no setter, so a change that
    // breaks a rule is refused at the point it is made rather than carried.
    const std::optional<an::ImageDescriptor> original = an::ImageDescriptor::create(mosaicRequest());
    ASSERT_TRUE(original.has_value());
    an::ImageDescriptorRequest demosaiced = original->toRequest();
    demosaiced.extent = extentOf(3u);
    demosaiced.channelSchema = an::ChannelSchema::Rgb;
    demosaiced.cfa = an::CfaDescriptor{};
    const std::optional<an::ImageDescriptor> result = an::ImageDescriptor::create(demosaiced);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->isMosaiced());
    EXPECT_EQ(result->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(result->numericDomain(), an::NumericDomain::RawSensorCode);

    const std::optional<an::ImageDescriptor> rebuilt = an::ImageDescriptor::create(original->toRequest());
    ASSERT_TRUE(rebuilt.has_value());
    EXPECT_EQ(*rebuilt, *original);
}

TEST(ImageDescriptorTest, AValueOutsideAnEnumerationIsRefusedByName)
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.sampleType = static_cast<an::SampleType>(200);
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::UnknownSampleType);

    request = linearRgbRequest();
    request.channelSchema = static_cast<an::ChannelSchema>(200);
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::UnknownChannelSchema);

    request = linearRgbRequest();
    request.colorSpace = static_cast<an::ColorSpace>(200);
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::UnknownColorSpace);

    request = linearRgbRequest();
    request.numericDomain = static_cast<an::NumericDomain>(200);
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::UnknownNumericDomain);

    request = linearRgbRequest();
    request.coordinateOrigin = static_cast<an::CoordinateOrigin>(200);
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::UnknownCoordinateOrigin);

    EXPECT_FALSE(an::ImageDescriptor::create(request).has_value());
}

TEST(ImageDescriptorTest, AnUnknownEnumeratorIsRefusedBeforeAnyRuleThatWouldInterpretIt)
{
    // Both fields are wrong here: the sample type is outside its enumeration and a Lab image cannot
    // be in a display RGB basis. The unknown value is reported, because a rule cannot be applied to
    // a value it does not understand.
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.sampleType = static_cast<an::SampleType>(200);
    request.channelSchema = an::ChannelSchema::Lab;
    request.colorSpace = an::ColorSpace::DisplayRgb;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::UnknownSampleType);
}

TEST(ImageDescriptorTest, ASchemaWithAFixedChannelCountRefusesAnExtentThatDisagrees)
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.extent = extentOf(4u);
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::ChannelCountMismatch);

    request.channelSchema = an::ChannelSchema::Rgba;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::None);
}

TEST(ImageDescriptorTest, NamedChannelsAcceptWhateverCountTheImageHas)
{
    for (const std::uint32_t channelCount : {1u, 3u, 7u})
    {
        an::ImageDescriptorRequest request = linearRgbRequest();
        request.extent = extentOf(channelCount);
        request.channelSchema = an::ChannelSchema::Named;
        request.colorSpace = an::ColorSpace::NarrowbandMapping;
        EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::None) << channelCount;
    }
}

TEST(ImageDescriptorTest, ASchemaAndABasisThatCannotDescribeTheSameValuesAreRefused)
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.channelSchema = an::ChannelSchema::Lab;
    request.colorSpace = an::ColorSpace::DisplayRgb;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::ColorSpaceMismatch);

    request.colorSpace = an::ColorSpace::CieLab;
    request.numericDomain = an::NumericDomain::EncodedNonlinear;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::None);
}

TEST(ImageDescriptorTest, ABasisAndADomainThatContradictEachOtherAreRefused)
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.numericDomain = an::NumericDomain::RawSensorCode;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::NumericDomainMismatch);

    request.colorSpace = an::ColorSpace::Unspecified;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::None);
}

TEST(ImageDescriptorTest, APhysicalDomainInAnIntegerSampleIsRefused)
{
    an::ImageDescriptorRequest request = linearRgbRequest();
    request.sampleType = an::SampleType::UInt16;
    request.numericDomain = an::NumericDomain::PhysicalScaled;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::NumericDomainNotRepresentable);

    request.sampleType = an::SampleType::Float64;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::None);
}

TEST(ImageDescriptorTest, AMosaicOverMoreThanOneChannelIsRefused)
{
    // A colour filter array sits over one sensor plane. Three channels means the mosaic has already
    // been resolved, so a pattern claiming otherwise describes an image that cannot exist.
    an::ImageDescriptorRequest request = mosaicRequest();
    request.extent = extentOf(3u);
    request.channelSchema = an::ChannelSchema::Rgb;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::CfaRequiresSingleChannel);
}

TEST(ImageDescriptorTest, AMosaicUnderASchemaThatIsNotMonoIsRefused)
{
    an::ImageDescriptorRequest request = mosaicRequest();
    request.channelSchema = an::ChannelSchema::Named;
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::CfaRequiresMonoSchema);
}

TEST(ImageDescriptorTest, ASingleChannelFrameNeedNotCarryAMosaic)
{
    // The mosaic rules constrain a descriptor that has a pattern; they do not require one. A
    // luminance frame from a monochrome sensor is one channel with no filter array at all.
    an::ImageDescriptorRequest request = mosaicRequest();
    request.cfa = an::CfaDescriptor{};
    EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::None);
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_FALSE(descriptor->isMosaiced());
}

TEST(ImageDescriptorTest, APixelAspectThatIsNotAPositiveFiniteNumberIsRefused)
{
    for (const double aspect : {0.0, -1.0, -0.0, std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        an::ImageDescriptorRequest request = linearRgbRequest();
        request.pixelAspectRatio = aspect;
        EXPECT_EQ(an::ImageDescriptor::check(request), an::DescriptorError::PixelAspectNotPositive) << aspect;
    }

    an::ImageDescriptorRequest anamorphic = linearRgbRequest();
    anamorphic.pixelAspectRatio = 2.0;
    EXPECT_EQ(an::ImageDescriptor::check(anamorphic), an::DescriptorError::None);
}

TEST(ImageDescriptorTest, BothCoordinateOriginsAreAcceptedAndNamed)
{
    // FITS counts rows from the bottom and every other codec this engine reads counts from the top.
    // Carrying which one an image uses is what stops a flip being applied twice or not at all.
    for (const an::CoordinateOrigin origin : {an::CoordinateOrigin::TopLeft, an::CoordinateOrigin::BottomLeft})
    {
        an::ImageDescriptorRequest request = linearRgbRequest();
        request.coordinateOrigin = origin;
        const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
        ASSERT_TRUE(descriptor.has_value());
        EXPECT_EQ(descriptor->coordinateOrigin(), origin);
        EXPECT_TRUE(an::isCoordinateOrigin(origin));
    }
    EXPECT_STREQ(an::coordinateOriginName(an::CoordinateOrigin::BottomLeft), "BottomLeft");
    EXPECT_FALSE(an::isCoordinateOrigin(static_cast<an::CoordinateOrigin>(9)));
    EXPECT_STREQ(an::coordinateOriginName(static_cast<an::CoordinateOrigin>(9)), "UnknownCoordinateOrigin");
}

TEST(ImageDescriptorTest, TwoDescriptorsAreEqualWhenEveryFieldAgrees)
{
    const std::optional<an::ImageDescriptor> first = an::ImageDescriptor::create(linearRgbRequest());
    const std::optional<an::ImageDescriptor> same = an::ImageDescriptor::create(linearRgbRequest());
    ASSERT_TRUE(first.has_value() && same.has_value());
    EXPECT_EQ(*first, *same);

    an::ImageDescriptorRequest displayRequest = linearRgbRequest();
    displayRequest.colorSpace = an::ColorSpace::DisplayRgb;
    const std::optional<an::ImageDescriptor> display = an::ImageDescriptor::create(displayRequest);
    ASSERT_TRUE(display.has_value());
    EXPECT_NE(*first, *display);
}

TEST(ImageDescriptorTest, ADescriptorIsUsableInAConstantExpression)
{
    constexpr an::EngineLimits kLimits = an::defaultEngineLimits();
    constexpr an::ImageDescriptorRequest kRequest{.extent = an::ImageExtent::create(16u, 16u, 1u, kLimits).value(),
                                                  .sampleType = an::SampleType::UInt16,
                                                  .channelSchema = an::ChannelSchema::Mono,
                                                  .colorSpace = an::ColorSpace::Unspecified,
                                                  .numericDomain = an::NumericDomain::RawSensorCode,
                                                  .cfa =
                                                      an::CfaDescriptor::create(an::CfaPattern::Bggr, 1u, 0u).value(),
                                                  .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                                  .pixelAspectRatio = 1.0};
    static_assert(an::ImageDescriptor::check(kRequest) == an::DescriptorError::None);
    static_assert(an::ImageDescriptor::create(kRequest).value().isMosaiced());
    SUCCEED();
}

} // namespace
