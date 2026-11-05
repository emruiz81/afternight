// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/CheckedSize.h.
//
// Every one of these is a case unchecked arithmetic gets wrong silently: a product
// that wraps produces a small, plausible-looking byte count, and the allocation that follows is
// smaller than the geometry that will be written through it. So the cases below are mostly about
// the boundary values where wrapping starts, and several of them are static_assert as well as
// EXPECT, because a primitive that cannot be evaluated at compile time cannot be used to size a
// buffer at compile time.

#include <AfterNightEngine/Core/CheckedSize.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{

constexpr std::uint64_t kMaxValue = (std::numeric_limits<std::uint64_t>::max)();

TEST(CheckedSizeTest, ADefaultConstructedValueIsZeroAndRepresentable)
{
    constexpr an::CheckedSize kZero;
    static_assert(kZero.isValid());
    EXPECT_TRUE(kZero.isValid());
    EXPECT_EQ(kZero.valueOr(kMaxValue), 0u);
}

TEST(CheckedSizeTest, CarriesTheValueItWasBuiltFrom)
{
    constexpr an::CheckedSize kValue{42u};
    EXPECT_TRUE(kValue.isValid());
    ASSERT_TRUE(kValue.value().has_value());
    EXPECT_EQ(*kValue.value(), 42u);
}

TEST(CheckedSizeTest, AnOverflowedValueCarriesNoNumber)
{
    constexpr an::CheckedSize kOverflowed = an::CheckedSize::overflowed();
    EXPECT_FALSE(kOverflowed.isValid());
    EXPECT_FALSE(kOverflowed.value().has_value());
    EXPECT_EQ(kOverflowed.valueOr(7u), 7u);
}

TEST(CheckedSizeTest, AdditionRefusesToWrapPastTheTop)
{
    constexpr an::CheckedSize kNearTop{kMaxValue - 1u};
    EXPECT_EQ((kNearTop + an::CheckedSize{1u}).valueOr(0u), kMaxValue);
    EXPECT_FALSE((kNearTop + an::CheckedSize{2u}).isValid());
    static_assert(!(an::CheckedSize{kMaxValue} + an::CheckedSize{1u}).isValid());
}

TEST(CheckedSizeTest, MultiplicationRefusesToWrapPastTheTop)
{
    // The product that matters in this engine: width times height times channels times sample
    // size. Unchecked, a wrapped product is a buffer overrun with a descriptor that looks fine.
    constexpr std::uint64_t kHalf = kMaxValue / 2u + 1u;
    EXPECT_FALSE((an::CheckedSize{kHalf} * an::CheckedSize{2u}).isValid());
    EXPECT_EQ((an::CheckedSize{kMaxValue} * an::CheckedSize{1u}).valueOr(0u), kMaxValue);
    EXPECT_EQ((an::CheckedSize{kMaxValue} * an::CheckedSize{0u}).valueOr(1u), 0u);
    static_assert(!(an::CheckedSize{1u << 20u} * an::CheckedSize{kMaxValue}).isValid());
}

TEST(CheckedSizeTest, SubtractionRefusesToGoBelowZero)
{
    EXPECT_EQ((an::CheckedSize{5u} - an::CheckedSize{5u}).valueOr(kMaxValue), 0u);
    EXPECT_FALSE((an::CheckedSize{5u} - an::CheckedSize{6u}).isValid());
}

TEST(CheckedSizeTest, AnOverflowedValueStaysOverflowedThroughFurtherArithmetic)
{
    // The property that makes a chain of operations checkable once at the end rather than after
    // every step, which is the only way the checks actually get written.
    constexpr an::CheckedSize kOverflowed = an::CheckedSize::overflowed();
    EXPECT_FALSE((kOverflowed + an::CheckedSize{1u}).isValid());
    EXPECT_FALSE((an::CheckedSize{1u} + kOverflowed).isValid());
    EXPECT_FALSE((kOverflowed * an::CheckedSize{0u}).isValid());
    EXPECT_FALSE((kOverflowed - an::CheckedSize{0u}).isValid());
    EXPECT_FALSE((an::CheckedSize{0u} - kOverflowed).isValid());
}

TEST(CheckedSizeTest, OverflowedValuesAreEqualWhateverProducedThem)
{
    EXPECT_EQ(an::CheckedSize{kMaxValue} + an::CheckedSize{1u}, an::CheckedSize{2u} - an::CheckedSize{3u});
    EXPECT_NE(an::CheckedSize::overflowed(), an::CheckedSize{0u});
    EXPECT_EQ(an::CheckedSize{9u}, an::CheckedSize{9u});
}

TEST(CheckedSizeTest, ConvertsToAnAllocationSizeOnlyWhenItFitsOne)
{
    constexpr std::uint64_t kLargestAllocation = static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
    EXPECT_EQ(an::CheckedSize{1024u}.toAllocationSize().value_or(0u), std::size_t{1024u});
    EXPECT_TRUE(an::CheckedSize{kLargestAllocation}.toAllocationSize().has_value());
    EXPECT_FALSE(an::CheckedSize::overflowed().toAllocationSize().has_value());
    if constexpr (kLargestAllocation < kMaxValue)
    {
        // Only reachable where std::size_t is narrower than the arithmetic type, which is the
        // whole reason the conversion is checked rather than a cast.
        EXPECT_FALSE(an::CheckedSize{kLargestAllocation + 1u}.toAllocationSize().has_value());
    }
}

TEST(CheckedSizeTest, ConvertsToAPointerDifferenceOnlyWhenItFitsOne)
{
    constexpr std::uint64_t kLargestOffset = static_cast<std::uint64_t>((std::numeric_limits<std::ptrdiff_t>::max)());
    EXPECT_EQ(an::CheckedSize{1024u}.toOffset().value_or(0), std::ptrdiff_t{1024});
    EXPECT_TRUE(an::CheckedSize{kLargestOffset}.toOffset().has_value());
    EXPECT_FALSE(an::CheckedSize{kLargestOffset + 1u}.toOffset().has_value());
    EXPECT_FALSE(an::CheckedSize::overflowed().toOffset().has_value());
}

TEST(CheckedSizeTest, SizesAFrameAtCompileTime)
{
    // What the type is for, written the way a caller writes it: one chain, one check at the end.
    constexpr an::CheckedSize kFrameBytes =
        an::CheckedSize{8192u} * an::CheckedSize{6144u} * an::CheckedSize{3u} * an::CheckedSize{4u};
    static_assert(kFrameBytes.isValid());
    static_assert(kFrameBytes.valueOr(0u) == 8192ull * 6144ull * 3ull * 4ull);
    EXPECT_EQ(kFrameBytes.valueOr(0u), 603979776ull);
}

} // namespace
