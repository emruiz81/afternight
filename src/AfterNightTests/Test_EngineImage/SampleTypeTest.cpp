// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/SampleType.h.
//
// The eight representations are the whole of what this engine stores, so the facts about them -
// how many bytes one costs, whether it can hold a negative number, whether it is floating - are
// read from one place by storage, by the conversion matrix and by every codec. A wrong answer here
// is a wrong allocation size or a wrong saturation bound, and neither announces itself.
//
// The out-of-enumeration cases are not defensive noise. A sample type arrives from a file header
// and from a caller compiled against a different build of this library, so a value outside the
// enumeration is a thing that happens rather than a thing that cannot.

#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace
{

constexpr an::SampleType kAllSampleTypes[] = {an::SampleType::UInt8,   an::SampleType::Int8,   an::SampleType::UInt16,
                                              an::SampleType::Int16,   an::SampleType::UInt32, an::SampleType::Int32,
                                              an::SampleType::Float32, an::SampleType::Float64};

TEST(SampleTypeTest, TheCountCoversEveryEnumerator)
{
    EXPECT_EQ(an::kSampleTypeCount, sizeof(kAllSampleTypes) / sizeof(kAllSampleTypes[0]));
    for (const an::SampleType type : kAllSampleTypes)
    {
        EXPECT_TRUE(an::isSampleType(type)) << an::sampleTypeName(type);
    }
}

TEST(SampleTypeTest, EachTypeCostsTheBytesItsNameClaims)
{
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::UInt8), 1u);
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::Int8), 1u);
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::UInt16), 2u);
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::Int16), 2u);
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::UInt32), 4u);
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::Int32), 4u);
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::Float32), 4u);
    EXPECT_EQ(an::sampleTypeSizeBytes(an::SampleType::Float64), 8u);
}

TEST(SampleTypeTest, EverySizeIsAPowerOfTwoBecauseTheLayoutRefusesAnythingElse)
{
    // ImageLayout refuses an element size that is not a power of two, so a sample type whose size
    // were not one could never be laid out at all. Asserting it here is what keeps the two rules
    // from drifting apart when a type is added.
    for (const an::SampleType type : kAllSampleTypes)
    {
        const std::uint32_t size = an::sampleTypeSizeBytes(type);
        EXPECT_NE(size, 0u) << an::sampleTypeName(type);
        EXPECT_EQ(size & (size - 1u), 0u) << an::sampleTypeName(type);
    }
}

TEST(SampleTypeTest, FloatingAndIntegerPartitionTheSet)
{
    for (const an::SampleType type : kAllSampleTypes)
    {
        EXPECT_NE(an::isFloatingSampleType(type), an::isIntegerSampleType(type)) << an::sampleTypeName(type);
    }
    EXPECT_TRUE(an::isFloatingSampleType(an::SampleType::Float32));
    EXPECT_TRUE(an::isFloatingSampleType(an::SampleType::Float64));
    EXPECT_TRUE(an::isIntegerSampleType(an::SampleType::UInt16));
}

TEST(SampleTypeTest, SignednessCoversTheFloatingTypesToo)
{
    EXPECT_FALSE(an::isSignedSampleType(an::SampleType::UInt8));
    EXPECT_FALSE(an::isSignedSampleType(an::SampleType::UInt16));
    EXPECT_FALSE(an::isSignedSampleType(an::SampleType::UInt32));
    EXPECT_TRUE(an::isSignedSampleType(an::SampleType::Int8));
    EXPECT_TRUE(an::isSignedSampleType(an::SampleType::Int16));
    EXPECT_TRUE(an::isSignedSampleType(an::SampleType::Int32));
    EXPECT_TRUE(an::isSignedSampleType(an::SampleType::Float32));
    EXPECT_TRUE(an::isSignedSampleType(an::SampleType::Float64));
}

TEST(SampleTypeTest, AValueOutsideTheEnumerationIsRefusedRatherThanSized)
{
    const an::SampleType stranger = static_cast<an::SampleType>(200);
    EXPECT_FALSE(an::isSampleType(stranger));
    EXPECT_EQ(an::sampleTypeSizeBytes(stranger), 0u);
    EXPECT_FALSE(an::isFloatingSampleType(stranger));
    EXPECT_FALSE(an::isIntegerSampleType(stranger));
    EXPECT_FALSE(an::isSignedSampleType(stranger));
    EXPECT_STREQ(an::sampleTypeName(stranger), "UnknownSampleType");
}

TEST(SampleTypeTest, EveryTypeHasItsOwnName)
{
    for (const an::SampleType left : kAllSampleTypes)
    {
        for (const an::SampleType right : kAllSampleTypes)
        {
            const bool sameName = std::strcmp(an::sampleTypeName(left), an::sampleTypeName(right)) == 0;
            EXPECT_EQ(sameName, left == right) << an::sampleTypeName(left) << " / " << an::sampleTypeName(right);
        }
    }
    EXPECT_STREQ(an::sampleTypeName(an::SampleType::Float32), "Float32");
}

TEST(SampleTypeTest, ByteOrderIsMeaninglessForASingleByteSample)
{
    EXPECT_FALSE(an::isByteOrderMeaningful(an::SampleType::UInt8));
    EXPECT_FALSE(an::isByteOrderMeaningful(an::SampleType::Int8));
    EXPECT_TRUE(an::isByteOrderMeaningful(an::SampleType::UInt16));
    EXPECT_TRUE(an::isByteOrderMeaningful(an::SampleType::Float64));
    EXPECT_FALSE(an::isByteOrderMeaningful(static_cast<an::SampleType>(200)));
}

TEST(SampleTypeTest, TheHostResolvesToOneOfTheTwoStatedOrders)
{
    const an::ByteOrder native = an::nativeByteOrder();
    EXPECT_TRUE(native == an::ByteOrder::Little || native == an::ByteOrder::Big);
    EXPECT_TRUE(an::isByteOrder(an::ByteOrder::Native));
    EXPECT_FALSE(an::isByteOrder(static_cast<an::ByteOrder>(9)));
    EXPECT_STREQ(an::byteOrderName(an::ByteOrder::Native), "Native");
    EXPECT_STREQ(an::byteOrderName(static_cast<an::ByteOrder>(9)), "UnknownByteOrder");
}

TEST(SampleTypeTest, EveryFactIsAvailableAtCompileTime)
{
    static_assert(an::sampleTypeSizeBytes(an::SampleType::Float64) == 8u);
    static_assert(an::isFloatingSampleType(an::SampleType::Float32));
    static_assert(!an::isSignedSampleType(an::SampleType::UInt32));
    static_assert(an::isByteOrderMeaningful(an::SampleType::Int16));
    static_assert(an::kSampleTypeCount == 8u);
    SUCCEED();
}

} // namespace
