// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/EngineLimits.h.
//
// The limits are policy, so most of what is worth testing is that they behave like policy: a
// caller can raise or lower them, the defaults refuse a corrupt header without refusing a large
// image, and the budget arithmetic cannot be made to accept a request by overflowing it.

#include <AfterNightEngine/Core/EngineLimits.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace
{

constexpr std::uint64_t kMaxValue = (std::numeric_limits<std::uint64_t>::max)();

TEST(EngineLimitsTest, TheDefaultsAreTheDeclaredConstants)
{
    constexpr an::EngineLimits kLimits = an::defaultEngineLimits();
    static_assert(kLimits.maxImageWidth == an::kDefaultMaxImageWidth);
    static_assert(kLimits.maxImageHeight == an::kDefaultMaxImageHeight);
    static_assert(kLimits.maxChannelCount == an::kDefaultMaxChannelCount);
    static_assert(kLimits.maxImageBytes == an::kDefaultMaxImageBytes);
    static_assert(kLimits.memoryBudgetBytes == an::kDefaultMemoryBudgetBytes);
    EXPECT_EQ(kLimits.maxImageWidth, an::kDefaultMaxImageWidth);
}

TEST(EngineLimitsTest, TheDefaultsAdmitALargeMosaicAndRefuseACorruptHeader)
{
    // The line the default is meant to sit on: a plausible very large image is accepted, and the
    // kind of number a damaged header produces is not.
    constexpr an::EngineLimits kLimits = an::defaultEngineLimits();
    EXPECT_GE(kLimits.maxImageWidth, 100000u);
    EXPECT_GE(kLimits.maxImageHeight, 100000u);
    EXPECT_LT(kLimits.maxImageWidth, (std::numeric_limits<std::uint32_t>::max)());
    EXPECT_LT(kLimits.maxImageHeight, (std::numeric_limits<std::uint32_t>::max)());
    EXPECT_GE(kLimits.maxChannelCount, 4u);
    EXPECT_LT(kLimits.maxChannelCount, (std::numeric_limits<std::uint32_t>::max)());
    EXPECT_GE(kLimits.maxImageBytes, 1024ull * 1024ull * 1024ull);
}

TEST(EngineLimitsTest, TheDefaultBudgetRefusesNothing)
{
    // A default budget that refused anything would be an invisible cap on a machine nobody
    // measured. The physical allocator remains the backstop until a host sets a real number.
    constexpr an::EngineLimits kLimits = an::defaultEngineLimits();
    EXPECT_TRUE(an::fitsMemoryBudget(kLimits, 0u, kMaxValue));
    EXPECT_TRUE(an::fitsMemoryBudget(kLimits, kMaxValue - 1u, 1u));
}

TEST(EngineLimitsTest, TheBudgetIsAggregateRatherThanPerAllocation)
{
    // Each request fits on its own; together they do not. That is the difference between a budget
    // and a maximum allocation size, and it is the reason the outstanding total is a parameter.
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.memoryBudgetBytes = 1000u;
    EXPECT_TRUE(an::fitsMemoryBudget(limits, 0u, 600u));
    EXPECT_TRUE(an::fitsMemoryBudget(limits, 600u, 400u));
    EXPECT_FALSE(an::fitsMemoryBudget(limits, 600u, 401u));
}

TEST(EngineLimitsTest, TheBudgetCannotBeSatisfiedByOverflowingIt)
{
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.memoryBudgetBytes = 1000u;
    EXPECT_FALSE(an::fitsMemoryBudget(limits, kMaxValue, 1u));
    EXPECT_FALSE(an::fitsMemoryBudget(limits, 1u, kMaxValue));
}

TEST(EngineLimitsTest, ABudgetOfZeroAdmitsOnlyAnEmptyRequest)
{
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.memoryBudgetBytes = 0u;
    EXPECT_TRUE(an::fitsMemoryBudget(limits, 0u, 0u));
    EXPECT_FALSE(an::fitsMemoryBudget(limits, 0u, 1u));
}

} // namespace
