// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/RawSampleFormat.h.
//
// The whole value of this vocabulary is that "is this one of the eight" is *derived* rather than
// stated, so the two directions of the table can never disagree about the same bytes. These cases
// check both directions over all eight, and they check the near misses one at a time - eight bytes
// of integer, two bytes of floating point, anything opaque - because each of those is a real file
// format and each of them has to answer "not resident" rather than "close enough".

#include <AfterNightEngine/Image/RawSampleFormat.h>

#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace
{

constexpr an::SampleType kAllSampleTypes[] = {an::SampleType::UInt8,   an::SampleType::Int8,   an::SampleType::UInt16,
                                              an::SampleType::Int16,   an::SampleType::UInt32, an::SampleType::Int32,
                                              an::SampleType::Float32, an::SampleType::Float64};

constexpr an::RawSampleClass kAllClasses[] = {an::RawSampleClass::UnsignedInteger, an::RawSampleClass::SignedInteger,
                                              an::RawSampleClass::FloatingPoint, an::RawSampleClass::Opaque};

TEST(RawSampleFormatTest, EveryResidentRepresentationRoundTripsThroughTheBoundaryForm)
{
    for (const an::SampleType type : kAllSampleTypes)
    {
        const an::RawSampleFormat format = an::rawSampleFormatOf(type);
        EXPECT_TRUE(an::isRawSampleFormat(format)) << an::sampleTypeName(type);
        EXPECT_EQ(format.sizeBytes, an::sampleTypeSizeBytes(type)) << an::sampleTypeName(type);

        const std::optional<an::SampleType> back = an::residentSampleTypeOf(format);
        ASSERT_TRUE(back.has_value()) << an::sampleTypeName(type);
        EXPECT_EQ(*back, type) << an::sampleTypeName(type);
    }
}

TEST(RawSampleFormatTest, TheClassAgreesWithWhatTheRepresentationIs)
{
    for (const an::SampleType type : kAllSampleTypes)
    {
        const an::RawSampleFormat format = an::rawSampleFormatOf(type);
        if (an::isFloatingSampleType(type))
        {
            EXPECT_EQ(format.sampleClass, an::RawSampleClass::FloatingPoint) << an::sampleTypeName(type);
        }
        else if (an::isSignedSampleType(type))
        {
            EXPECT_EQ(format.sampleClass, an::RawSampleClass::SignedInteger) << an::sampleTypeName(type);
        }
        else
        {
            EXPECT_EQ(format.sampleClass, an::RawSampleClass::UnsignedInteger) << an::sampleTypeName(type);
        }
    }
}

TEST(RawSampleFormatTest, SixtyFourBitIntegersAreDescribableAndNotResident)
{
    // The case the sample-type set is held at eight for. A file holding these can be read,
    // described and written back unchanged; what it cannot do is become a processing image without
    // a conversion that says what it did.
    const an::RawSampleFormat signedInteger{.sizeBytes = 8u, .sampleClass = an::RawSampleClass::SignedInteger};
    const an::RawSampleFormat unsignedInteger{.sizeBytes = 8u, .sampleClass = an::RawSampleClass::UnsignedInteger};

    EXPECT_TRUE(an::isRawSampleFormat(signedInteger));
    EXPECT_TRUE(an::isRawSampleFormat(unsignedInteger));
    EXPECT_FALSE(an::residentSampleTypeOf(signedInteger).has_value());
    EXPECT_FALSE(an::residentSampleTypeOf(unsignedInteger).has_value());
}

TEST(RawSampleFormatTest, TheOtherNearMissesAreDescribableAndNotResidentEither)
{
    const an::RawSampleFormat halfFloat{.sizeBytes = 2u, .sampleClass = an::RawSampleClass::FloatingPoint};
    const an::RawSampleFormat wideFloat{.sizeBytes = 16u, .sampleClass = an::RawSampleClass::FloatingPoint};
    const an::RawSampleFormat packedTwelve{.sizeBytes = 2u, .sampleClass = an::RawSampleClass::Opaque};

    EXPECT_TRUE(an::isRawSampleFormat(halfFloat));
    EXPECT_TRUE(an::isRawSampleFormat(wideFloat));
    EXPECT_TRUE(an::isRawSampleFormat(packedTwelve));
    EXPECT_FALSE(an::residentSampleTypeOf(halfFloat).has_value());
    EXPECT_FALSE(an::residentSampleTypeOf(wideFloat).has_value());
    EXPECT_FALSE(an::residentSampleTypeOf(packedTwelve).has_value());
}

TEST(RawSampleFormatTest, OpaqueIsNeverResidentAtAnyWidth)
{
    for (std::uint32_t size = 1u; size <= an::kMaxRawSampleSizeBytes; size *= 2u)
    {
        const an::RawSampleFormat format{.sizeBytes = size, .sampleClass = an::RawSampleClass::Opaque};
        EXPECT_TRUE(an::isRawSampleFormat(format)) << size;
        EXPECT_FALSE(an::residentSampleTypeOf(format).has_value()) << size;
    }
}

TEST(RawSampleFormatTest, OnlyPowersOfTwoWithinTheBoundaryMaximumAreWidths)
{
    EXPECT_FALSE(an::isRawSampleSize(0u));
    EXPECT_TRUE(an::isRawSampleSize(1u));
    EXPECT_TRUE(an::isRawSampleSize(2u));
    EXPECT_FALSE(an::isRawSampleSize(3u));
    EXPECT_TRUE(an::isRawSampleSize(4u));
    EXPECT_FALSE(an::isRawSampleSize(6u));
    EXPECT_TRUE(an::isRawSampleSize(8u));
    EXPECT_FALSE(an::isRawSampleSize(12u));
    EXPECT_TRUE(an::isRawSampleSize(an::kMaxRawSampleSizeBytes));
    EXPECT_FALSE(an::isRawSampleSize(an::kMaxRawSampleSizeBytes * 2u));
}

TEST(RawSampleFormatTest, AWidthOrAClassOutsideTheSetIsNotAFormatAndIsNotResident)
{
    const an::RawSampleFormat threeBytes{.sizeBytes = 3u, .sampleClass = an::RawSampleClass::UnsignedInteger};
    const an::RawSampleFormat unfilled{.sizeBytes = 0u, .sampleClass = an::RawSampleClass::UnsignedInteger};
    const an::RawSampleFormat stranger{.sizeBytes = 4u, .sampleClass = static_cast<an::RawSampleClass>(200)};

    EXPECT_FALSE(an::isRawSampleFormat(threeBytes));
    EXPECT_FALSE(an::isRawSampleFormat(unfilled));
    EXPECT_FALSE(an::isRawSampleFormat(stranger));
    EXPECT_FALSE(an::residentSampleTypeOf(threeBytes).has_value());
    EXPECT_FALSE(an::residentSampleTypeOf(unfilled).has_value());
    EXPECT_FALSE(an::residentSampleTypeOf(stranger).has_value());
}

TEST(RawSampleFormatTest, ARepresentationOutsideTheEnumerationDescribesNoSample)
{
    const an::RawSampleFormat format = an::rawSampleFormatOf(static_cast<an::SampleType>(200));
    EXPECT_EQ(format.sizeBytes, 0u);
    EXPECT_FALSE(an::isRawSampleFormat(format));
}

TEST(RawSampleFormatTest, ByteOrderMeansNothingForAOneByteSample)
{
    for (const an::RawSampleClass sampleClass : kAllClasses)
    {
        EXPECT_FALSE(an::isRawByteOrderMeaningful(an::RawSampleFormat{.sizeBytes = 1u, .sampleClass = sampleClass}));
        EXPECT_TRUE(an::isRawByteOrderMeaningful(an::RawSampleFormat{.sizeBytes = 2u, .sampleClass = sampleClass}));
    }
}

TEST(RawSampleFormatTest, EveryClassHasItsOwnNameAndAStrangerSaysSo)
{
    for (const an::RawSampleClass left : kAllClasses)
    {
        EXPECT_TRUE(an::isRawSampleClass(left));
        EXPECT_GT(std::strlen(an::rawSampleClassName(left)), 0u);
        for (const an::RawSampleClass right : kAllClasses)
        {
            const bool sameName = std::strcmp(an::rawSampleClassName(left), an::rawSampleClassName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_FALSE(an::isRawSampleClass(static_cast<an::RawSampleClass>(200)));
    EXPECT_STREQ(an::rawSampleClassName(static_cast<an::RawSampleClass>(200)), "UnknownRawSampleClass");
}

TEST(RawSampleFormatTest, TwoFormatsAreEqualExactlyWhenBothFieldsAre)
{
    const an::RawSampleFormat left{.sizeBytes = 4u, .sampleClass = an::RawSampleClass::FloatingPoint};
    EXPECT_EQ(left, (an::RawSampleFormat{.sizeBytes = 4u, .sampleClass = an::RawSampleClass::FloatingPoint}));
    EXPECT_NE(left, (an::RawSampleFormat{.sizeBytes = 8u, .sampleClass = an::RawSampleClass::FloatingPoint}));
    EXPECT_NE(left, (an::RawSampleFormat{.sizeBytes = 4u, .sampleClass = an::RawSampleClass::SignedInteger}));
}

} // namespace
