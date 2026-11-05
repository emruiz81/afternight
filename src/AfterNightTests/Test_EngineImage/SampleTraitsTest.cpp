// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/SampleTraits.h.
//
// This header is two closed tables and the proof that they agree with a third one written in
// `Image/SampleType.h`. Most of what it promises is therefore a compile-time fact, and most of what
// could go wrong would be a compile error rather than a failing case - which is exactly why the
// cases below are written as run-time assertions over `constexpr` expressions: a static assertion
// that stops the build reports one problem and then nothing, and a suite reports all of them and
// says which type each belonged to.
//
// The out-of-set cases matter more than they look. The reason this header keeps an allow-list
// instead of asking `<type_traits>` whether something is an integer is that `char`, `bool` and
// `long long` would all pass such a question and none of them is a representation this engine
// stores, has a conversion row for, or has ever tested. The cases that refuse them are the ones
// that would fail if somebody replaced the tables with a computed trait.

#include <AfterNightEngine/Image/SampleTraits.h>

#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace
{

/// Whether a type is one of the eight, asked the way a caller asks.
///
/// @tparam SampleT the type to classify.
/// @return true when the concept admits it.
template <typename SampleT> [[nodiscard]] constexpr bool isResident() noexcept
{
    return an::ResidentSample<SampleT>;
}

/// Whether a type is one a view may present, asked the way a caller asks.
///
/// @tparam SampleT the type to classify.
/// @return true when the concept admits it.
template <typename SampleT> [[nodiscard]] constexpr bool isViewable() noexcept
{
    return an::ViewableSample<SampleT>;
}

TEST(SampleTraitsTest, TheEightTypesAreTheOnesTheConceptAdmits)
{
    EXPECT_TRUE(isResident<std::uint8_t>());
    EXPECT_TRUE(isResident<std::int8_t>());
    EXPECT_TRUE(isResident<std::uint16_t>());
    EXPECT_TRUE(isResident<std::int16_t>());
    EXPECT_TRUE(isResident<std::uint32_t>());
    EXPECT_TRUE(isResident<std::int32_t>());
    EXPECT_TRUE(isResident<float>());
    EXPECT_TRUE(isResident<double>());
}

TEST(SampleTraitsTest, TypesThatMerelyLookLikeSamplesAreRefused)
{
    // Every one of these would be admitted by an is_integral or is_arithmetic test, and none of
    // them is a representation this engine stores. `char` is the sharpest of them: it is a distinct
    // type from both signed and unsigned char on every compiler, so admitting it would create a
    // ninth storage type that maps to no enumerator at all.
    EXPECT_FALSE(isResident<char>());
    EXPECT_FALSE(isResident<bool>());
    EXPECT_FALSE(isResident<long long>());
    EXPECT_FALSE(isResident<unsigned long long>());
    EXPECT_FALSE(isResident<long double>());
    EXPECT_FALSE(isResident<void*>());
    EXPECT_FALSE(isResident<an::SampleType>());
}

TEST(SampleTraitsTest, StorageRefusesAConstSampleTypeAndAViewAcceptsOne)
{
    // The asymmetry is the whole of how constness is carried. Storage owns memory something has to
    // be able to write; a view is the thing that decides whether this particular caller may.
    EXPECT_FALSE(isResident<const std::uint16_t>());
    EXPECT_TRUE(isViewable<const std::uint16_t>());
    EXPECT_TRUE(isViewable<std::uint16_t>());
    EXPECT_FALSE(isViewable<const char>());
    EXPECT_FALSE(isViewable<volatile std::uint16_t>());
}

TEST(SampleTraitsTest, EachRepresentationRoundTripsThroughItsCppType)
{
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::UInt8>, std::uint8_t>));
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::Int8>, std::int8_t>));
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::UInt16>, std::uint16_t>));
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::Int16>, std::int16_t>));
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::UInt32>, std::uint32_t>));
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::Int32>, std::int32_t>));
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::Float32>, float>));
    EXPECT_TRUE((std::is_same_v<an::SampleOf<an::SampleType::Float64>, double>));

    EXPECT_EQ(an::kSampleTypeOf<std::uint8_t>, an::SampleType::UInt8);
    EXPECT_EQ(an::kSampleTypeOf<std::int8_t>, an::SampleType::Int8);
    EXPECT_EQ(an::kSampleTypeOf<std::uint16_t>, an::SampleType::UInt16);
    EXPECT_EQ(an::kSampleTypeOf<std::int16_t>, an::SampleType::Int16);
    EXPECT_EQ(an::kSampleTypeOf<std::uint32_t>, an::SampleType::UInt32);
    EXPECT_EQ(an::kSampleTypeOf<std::int32_t>, an::SampleType::Int32);
    EXPECT_EQ(an::kSampleTypeOf<float>, an::SampleType::Float32);
    EXPECT_EQ(an::kSampleTypeOf<double>, an::SampleType::Float64);
}

TEST(SampleTraitsTest, AConstSampleReportsTheRepresentationItsWritableFormDoes)
{
    EXPECT_EQ(an::kSampleTypeOf<const std::uint16_t>, an::kSampleTypeOf<std::uint16_t>);
    EXPECT_EQ(an::kSampleTypeOf<const double>, an::SampleType::Float64);
}

TEST(SampleTraitsTest, TheCompileTimeTableAgreesWithTheRuntimeOne)
{
    // The four questions `sampleTraitsAgree` asks, restated as a case so that a disagreement is
    // reported as a named failure rather than only as a static assertion inside the header. Size is
    // what an allocation is computed from, signedness is what a saturation bound comes from, and
    // whether a type is floating decides whether a conversion rounds - three answers read out of
    // this table by code that never sees the other spelling.
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::UInt8>());
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::Int8>());
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::UInt16>());
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::Int16>());
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::UInt32>());
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::Int32>());
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::Float32>());
    EXPECT_TRUE(an::sampleTraitsAgree<an::SampleType::Float64>());
}

TEST(SampleTraitsTest, EverySizeTheRuntimeTableReportsIsTheSizeOfItsCppType)
{
    EXPECT_EQ(sizeof(an::SampleOf<an::SampleType::UInt8>), an::sampleTypeSizeBytes(an::SampleType::UInt8));
    EXPECT_EQ(sizeof(an::SampleOf<an::SampleType::Int16>), an::sampleTypeSizeBytes(an::SampleType::Int16));
    EXPECT_EQ(sizeof(an::SampleOf<an::SampleType::UInt32>), an::sampleTypeSizeBytes(an::SampleType::UInt32));
    EXPECT_EQ(sizeof(an::SampleOf<an::SampleType::Float32>), an::sampleTypeSizeBytes(an::SampleType::Float32));
    EXPECT_EQ(sizeof(an::SampleOf<an::SampleType::Float64>), an::sampleTypeSizeBytes(an::SampleType::Float64));
}

TEST(SampleTraitsTest, TheTablesCoverEveryEnumeratorAndNoMore)
{
    // Eight entries against a count derived from the last enumerator: a ninth representation added
    // to `SampleType` without a C++ type here is a compile error in `SampleOf`, and this is the
    // case that says the two sets are the same size in the first place.
    EXPECT_EQ(an::kSampleTypeCount, 8u);
}

} // namespace
