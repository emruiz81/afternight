// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the published eight-way sample dispatch in Image/SampleTraits.h.
//
// The dispatch is the one switch from a run-time representation to its C++ type. These cases hold it to
// the tables beside it - every value reaches the type whose traits name that value, exactly once - and to
// the three promises its declaration makes: a value outside the enumeration runs nothing and says so, the
// call is a constant expression when the body is, and it is noexcept exactly when every instantiation of
// the body is.

#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace
{

constexpr std::array<an::SampleType, 8u> kEveryType{
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

// A body that may throw, so the dispatch over it must not claim noexcept.
struct ThrowingBody
{
    template <typename SampleT> void operator()() const {}
};

// A body every instantiation of which is noexcept.
struct QuietBody
{
    template <typename SampleT> void operator()() const noexcept {}
};

// A body that is noexcept for every type but one, which is enough to make the dispatch potentially throwing.
struct MostlyQuietBody
{
    template <typename SampleT> void operator()() const noexcept(!std::is_same_v<SampleT, double>) {}
};

// What the dispatch reports about one value, computed in a constant expression.
[[nodiscard]] constexpr std::size_t sizeDispatchedFor(an::SampleType type) noexcept
{
    std::size_t size = 0u;
    const bool ran = an::dispatchSampleType(type, [&]<typename SampleT>() noexcept { size = sizeof(SampleT); });
    return ran ? size : 0u;
}

TEST(SampleDispatchTest, EveryRepresentationReachesTheTypeItsTraitsName)
{
    for (const an::SampleType type : kEveryType)
    {
        std::size_t calls = 0u;
        an::SampleType reached = an::SampleType::UInt8;
        bool floating = false;
        bool isSigned = false;
        std::size_t size = 0u;
        const bool ran = an::dispatchSampleType(type, [&]<typename SampleT>() {
            ++calls;
            reached = an::kSampleTypeOf<SampleT>;
            floating = std::is_floating_point_v<SampleT>;
            isSigned = std::is_signed_v<SampleT>;
            size = sizeof(SampleT);
        });
        EXPECT_TRUE(ran) << an::sampleTypeName(type);
        EXPECT_EQ(calls, 1u) << an::sampleTypeName(type);
        EXPECT_EQ(reached, type);
        EXPECT_EQ(floating, an::isFloatingSampleType(type));
        EXPECT_EQ(isSigned, an::isSignedSampleType(type));
        EXPECT_EQ(size, an::sampleTypeSizeBytes(type));
    }
}

TEST(SampleDispatchTest, TheBodyReceivesTheUnqualifiedResidentType)
{
    for (const an::SampleType type : kEveryType)
    {
        bool resident = false;
        bool unqualified = false;
        ASSERT_TRUE(an::dispatchSampleType(type, [&]<typename SampleT>() {
            resident = an::ResidentSample<SampleT>;
            unqualified = std::is_same_v<SampleT, std::remove_cv_t<SampleT>>;
        }));
        EXPECT_TRUE(resident);
        EXPECT_TRUE(unqualified);
    }
}

TEST(SampleDispatchTest, AValueOutsideTheEnumerationRunsNothingAndSaysSo)
{
    for (std::uint32_t raw = static_cast<std::uint32_t>(an::kSampleTypeCount); raw <= 255u; ++raw)
    {
        std::size_t calls = 0u;
        const bool ran = an::dispatchSampleType(static_cast<an::SampleType>(raw), [&]<typename SampleT>() { ++calls; });
        EXPECT_FALSE(ran) << raw;
        EXPECT_EQ(calls, 0u) << raw;
    }
}

TEST(SampleDispatchTest, ItIsAConstantExpressionWhenTheBodyIsOne)
{
    static_assert(sizeDispatchedFor(an::SampleType::UInt8) == 1u);
    static_assert(sizeDispatchedFor(an::SampleType::Int16) == 2u);
    static_assert(sizeDispatchedFor(an::SampleType::Float32) == 4u);
    static_assert(sizeDispatchedFor(an::SampleType::Float64) == 8u);
    static_assert(sizeDispatchedFor(static_cast<an::SampleType>(an::kSampleTypeCount)) == 0u);
    static_assert(an::sampleDispatchAgrees());
    // The run-time half of the same question, so a failure here names the representation.
    for (const an::SampleType type : kEveryType)
    {
        EXPECT_EQ(sizeDispatchedFor(type), an::sampleTypeSizeBytes(type)) << an::sampleTypeName(type);
    }
}

TEST(SampleDispatchTest, ItIsNoexceptExactlyWhenEveryInstantiationOfTheBodyIs)
{
    QuietBody quiet;
    ThrowingBody throwing;
    MostlyQuietBody mostly;
    static_assert(noexcept(an::dispatchSampleType(an::SampleType::UInt8, quiet)));
    static_assert(!noexcept(an::dispatchSampleType(an::SampleType::UInt8, throwing)));
    static_assert(!noexcept(an::dispatchSampleType(an::SampleType::UInt8, mostly)));
    EXPECT_TRUE(an::dispatchSampleType(an::SampleType::Float64, mostly));
}

// A body with state of its own, to show the dispatch calls the object it was given rather than a copy.
struct Counting
{
    int calls = 0;
    template <typename SampleT> void operator()() noexcept
    {
        ++calls;
    }
};

TEST(SampleDispatchTest, ABodyHeldByValueOrByReferenceIsCalledOnTheObjectPassed)
{
    Counting counting;
    ASSERT_TRUE(an::dispatchSampleType(an::SampleType::UInt16, counting));
    ASSERT_TRUE(an::dispatchSampleType(an::SampleType::Float32, counting));
    EXPECT_EQ(counting.calls, 2);
}

} // namespace
