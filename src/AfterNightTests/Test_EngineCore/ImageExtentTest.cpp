// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/ImageExtent.h.
//
// An extent is three numbers, and every interesting case here is one of the three being a number
// no image has: zero, larger than the configured maximum, or large enough that the sample count
// they multiply out to cannot be counted at all. The last one is the reason the type exists rather
// than a struct of three integers.

#include <AfterNightEngine/Core/ImageExtent.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>

namespace
{

constexpr std::uint32_t kMaxDimension = (std::numeric_limits<std::uint32_t>::max)();

an::EngineLimits unboundedLimits()
{
    // Everything a configured limit could refuse, raised out of the way, so that a case about
    // representability is not answered by policy before it gets there.
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.maxImageWidth = kMaxDimension;
    limits.maxImageHeight = kMaxDimension;
    limits.maxChannelCount = kMaxDimension;
    limits.maxImageBytes = (std::numeric_limits<std::uint64_t>::max)();
    return limits;
}

TEST(ImageExtentTest, AcceptsAnOrdinaryFrameAndReportsItBack)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(8192u, 6144u, 3u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    EXPECT_EQ(extent->width(), 8192u);
    EXPECT_EQ(extent->height(), 6144u);
    EXPECT_EQ(extent->channelCount(), 3u);
    EXPECT_EQ(extent->pixelCount(), 8192ull * 6144ull);
    EXPECT_EQ(extent->sampleCount(), 8192ull * 6144ull * 3ull);
}

TEST(ImageExtentTest, RefusesAZeroDimension)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    EXPECT_EQ(an::ImageExtent::check(0u, 6144u, 3u, limits), an::GeometryError::ZeroDimension);
    EXPECT_EQ(an::ImageExtent::check(8192u, 0u, 3u, limits), an::GeometryError::ZeroDimension);
    EXPECT_EQ(an::ImageExtent::check(8192u, 6144u, 0u, limits), an::GeometryError::ZeroDimension);
}

TEST(ImageExtentTest, RefusesADimensionOverTheConfiguredMaximum)
{
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.maxImageWidth = 100u;
    limits.maxImageHeight = 200u;
    limits.maxChannelCount = 4u;
    EXPECT_EQ(an::ImageExtent::check(100u, 200u, 4u, limits), an::GeometryError::None);
    EXPECT_EQ(an::ImageExtent::check(101u, 200u, 4u, limits), an::GeometryError::DimensionTooLarge);
    EXPECT_EQ(an::ImageExtent::check(100u, 201u, 4u, limits), an::GeometryError::DimensionTooLarge);
    EXPECT_EQ(an::ImageExtent::check(100u, 200u, 5u, limits), an::GeometryError::ChannelCountTooLarge);
}

TEST(ImageExtentTest, RefusesASampleCountThatCannotBeCounted)
{
    // Reachable only with the configured maxima raised, which is the point: the overflow check is
    // about representability and is never configurable, so raising every policy limit does not
    // buy a geometry the machine cannot describe.
    EXPECT_EQ(an::ImageExtent::check(kMaxDimension, kMaxDimension, 2u, unboundedLimits()),
              an::GeometryError::ArithmeticOverflow);
    EXPECT_EQ(an::ImageExtent::check(kMaxDimension, kMaxDimension, 1u, unboundedLimits()), an::GeometryError::None);
}

TEST(ImageExtentTest, AGeometryThatCannotBeCountedIsRefusedBeforeAConfiguredMaximumIs)
{
    // Both rules apply to these arguments: the channel count is over the configured maximum, and
    // the sample count they multiply out to does not exist. Reporting the limit would invite a
    // caller to raise it and meet a second refusal, so the fact is reported first.
    an::EngineLimits limits = unboundedLimits();
    limits.maxChannelCount = 4u;
    EXPECT_EQ(an::ImageExtent::check(kMaxDimension, kMaxDimension, 5u, limits), an::GeometryError::ArithmeticOverflow);
    EXPECT_EQ(an::ImageExtent::check(1024u, 1024u, 5u, limits), an::GeometryError::ChannelCountTooLarge);
}

TEST(ImageExtentTest, CreatingProducesAValueExactlyWhenCheckingPassed)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    EXPECT_TRUE(an::ImageExtent::create(4u, 4u, 1u, limits).has_value());
    EXPECT_FALSE(an::ImageExtent::create(0u, 4u, 1u, limits).has_value());
    EXPECT_FALSE(an::ImageExtent::create(kMaxDimension, 4u, 1u, limits).has_value());
    EXPECT_FALSE(an::ImageExtent::create(kMaxDimension, kMaxDimension, 2u, unboundedLimits()).has_value());
}

TEST(ImageExtentTest, LimitsOfZeroRefuseEveryImage)
{
    // A degenerate configuration, and the reason it is worth a case: it must refuse rather than
    // wrap into accepting everything.
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.maxImageWidth = 0u;
    limits.maxImageHeight = 0u;
    limits.maxChannelCount = 0u;
    EXPECT_EQ(an::ImageExtent::check(1u, 1u, 1u, limits), an::GeometryError::DimensionTooLarge);
    EXPECT_FALSE(an::ImageExtent::create(1u, 1u, 1u, limits).has_value());
}

TEST(ImageExtentTest, EqualExtentsCompareEqual)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> first = an::ImageExtent::create(64u, 32u, 3u, limits);
    const std::optional<an::ImageExtent> same = an::ImageExtent::create(64u, 32u, 3u, limits);
    const std::optional<an::ImageExtent> other = an::ImageExtent::create(64u, 32u, 1u, limits);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(same.has_value());
    ASSERT_TRUE(other.has_value());
    EXPECT_EQ(*first, *same);
    EXPECT_NE(*first, *other);
}

TEST(ImageExtentTest, IsBuiltAtCompileTime)
{
    constexpr std::optional<an::ImageExtent> kExtent =
        an::ImageExtent::create(1024u, 768u, 3u, an::defaultEngineLimits());
    static_assert(kExtent.has_value());
    static_assert(kExtent->sampleCount() == 1024ull * 768ull * 3ull);
    EXPECT_EQ(kExtent->pixelCount(), 1024ull * 768ull);
}

} // namespace
