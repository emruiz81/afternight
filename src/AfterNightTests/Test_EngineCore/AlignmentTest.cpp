// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/Alignment.h.
//
// Alignment arithmetic is where a rounding helper quietly stops rounding: an alignment that is not
// a power of two makes the usual mask arithmetic produce a number that is neither aligned nor
// larger than its input, and rounding a value near the top of the range wraps to a smaller one.
// Both are refusals here, and both are what these cases pin down.

#include <AfterNightEngine/Core/Alignment.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace
{

constexpr std::uint64_t kMaxValue = (std::numeric_limits<std::uint64_t>::max)();

TEST(AlignmentTest, OnlyPowersOfTwoAreAlignments)
{
    static_assert(an::isPowerOfTwo(1u));
    static_assert(an::isPowerOfTwo(64u));
    static_assert(!an::isPowerOfTwo(0u));
    static_assert(!an::isPowerOfTwo(24u));
    EXPECT_TRUE(an::isPowerOfTwo(1ull << 63u));
    EXPECT_FALSE(an::isPowerOfTwo(3u));
    EXPECT_FALSE(an::isPowerOfTwo(kMaxValue));
}

TEST(AlignmentTest, AnAlreadyAlignedValueIsLeftAlone)
{
    EXPECT_EQ(an::alignUp(an::CheckedSize{256u}, 64u).valueOr(0u), 256u);
    EXPECT_EQ(an::alignDown(an::CheckedSize{256u}, 64u).valueOr(0u), 256u);
    EXPECT_EQ(an::alignUp(an::CheckedSize{0u}, 64u).valueOr(1u), 0u);
}

TEST(AlignmentTest, RoundingGoesToTheNextMultipleAndThePreviousOne)
{
    EXPECT_EQ(an::alignUp(an::CheckedSize{200u}, 64u).valueOr(0u), 256u);
    EXPECT_EQ(an::alignDown(an::CheckedSize{200u}, 64u).valueOr(0u), 192u);
    EXPECT_EQ(an::alignUp(an::CheckedSize{1u}, 64u).valueOr(0u), 64u);
    EXPECT_EQ(an::alignDown(an::CheckedSize{63u}, 64u).valueOr(1u), 0u);
}

TEST(AlignmentTest, RoundingRefusesAnAlignmentThatIsNotAPowerOfTwo)
{
    EXPECT_FALSE(an::alignUp(an::CheckedSize{200u}, 24u).isValid());
    EXPECT_FALSE(an::alignDown(an::CheckedSize{200u}, 24u).isValid());
    EXPECT_FALSE(an::alignUp(an::CheckedSize{200u}, 0u).isValid());
    EXPECT_FALSE(an::alignDown(an::CheckedSize{200u}, 0u).isValid());
}

TEST(AlignmentTest, RoundingUpRefusesToWrapPastTheTop)
{
    // Rounding a value that has no aligned successor is the case a mask-and-add helper gets wrong
    // by producing a smaller number than it was given.
    EXPECT_FALSE(an::alignUp(an::CheckedSize{kMaxValue}, 64u).isValid());
    EXPECT_FALSE(an::alignUp(an::CheckedSize{kMaxValue - 62u}, 64u).isValid());
    EXPECT_EQ(an::alignUp(an::CheckedSize{kMaxValue - 63u}, 64u).valueOr(0u), kMaxValue - 63u);
}

TEST(AlignmentTest, RoundingPropagatesAnOverflowedInput)
{
    EXPECT_FALSE(an::alignUp(an::CheckedSize::overflowed(), 64u).isValid());
    EXPECT_FALSE(an::alignDown(an::CheckedSize::overflowed(), 64u).isValid());
}

TEST(AlignmentTest, AValueIsAlignedExactlyWhenRoundingLeavesItAlone)
{
    EXPECT_TRUE(an::isAligned(256u, 64u));
    EXPECT_TRUE(an::isAligned(0u, 64u));
    EXPECT_FALSE(an::isAligned(200u, 64u));
    // An alignment that is not a power of two is not an alignment, so nothing satisfies it.
    EXPECT_FALSE(an::isAligned(48u, 24u));
    EXPECT_FALSE(an::isAligned(0u, 0u));
}

TEST(AlignmentTest, PointerAlignmentReadsTheAddressRatherThanTheType)
{
    alignas(an::kDefaultBufferAlignmentBytes) unsigned char buffer[an::kDefaultBufferAlignmentBytes * 2u]{};
    EXPECT_TRUE(an::isPointerAligned(buffer, an::kDefaultBufferAlignmentBytes));
    EXPECT_TRUE(an::isPointerAligned(buffer + an::kDefaultBufferAlignmentBytes, an::kDefaultBufferAlignmentBytes));
    EXPECT_FALSE(an::isPointerAligned(buffer + 1u, 2u));
    EXPECT_FALSE(an::isPointerAligned(buffer, 0u));
    EXPECT_FALSE(an::isPointerAligned(nullptr, 64u));
}

TEST(AlignmentTest, TheDefaultBufferAlignmentSatisfiesBothReasonsForIt)
{
    // It exists to serve two independent constraints, and it is only correct while it satisfies
    // the larger of them rather than whichever one was written last.
    static_assert(an::isPowerOfTwo(an::kDefaultBufferAlignmentBytes));
    static_assert(an::kDefaultBufferAlignmentBytes >= an::kCacheLineBytes);
    static_assert(an::kDefaultBufferAlignmentBytes >= an::kWidestVectorBytes);
    EXPECT_TRUE(an::isPowerOfTwo(an::kCacheLineBytes));
    EXPECT_TRUE(an::isPowerOfTwo(an::kWidestVectorBytes));
}

} // namespace
