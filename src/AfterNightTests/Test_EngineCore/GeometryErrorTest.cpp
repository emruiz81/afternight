// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/GeometryError.h.
//
// The vocabulary itself is exercised wherever an extent, a layout or a view refuses something. What
// is tested here is the one piece of behaviour the header has of its own: the name beside each
// reason. It exists because a geometry refusal has to become a returned failure somewhere - storage
// is the first place that happens - and the code a caller branches on is deliberately coarser than
// the rule that refused, so the rule's own name is what a person reads to find out which one it
// was.
//
// A name is only worth having if it is distinct, which is why the case below compares every pair
// rather than checking a spelling. Two reasons sharing a name would be a log that says the right
// thing about the wrong rule, and nothing else in this repository would notice.

#include <AfterNightEngine/Core/GeometryError.h>

#include <gtest/gtest.h>

#include <cstring>

namespace
{

/// Every reason the enumeration holds, in declaration order.
constexpr an::GeometryError kAllGeometryErrors[] = {an::GeometryError::None,
                                                    an::GeometryError::ZeroDimension,
                                                    an::GeometryError::DimensionTooLarge,
                                                    an::GeometryError::ChannelCountTooLarge,
                                                    an::GeometryError::PlaneCountMismatch,
                                                    an::GeometryError::AlignmentNotPowerOfTwo,
                                                    an::GeometryError::ElementSizeNotPowerOfTwo,
                                                    an::GeometryError::StrideTooSmall,
                                                    an::GeometryError::StrideMisaligned,
                                                    an::GeometryError::ArithmeticOverflow,
                                                    an::GeometryError::SizeNotRepresentable,
                                                    an::GeometryError::ImageTooLarge,
                                                    an::GeometryError::ElementSizeMismatch,
                                                    an::GeometryError::BufferTooSmall,
                                                    an::GeometryError::BaseAddressMisaligned,
                                                    an::GeometryError::RegionOutOfBounds};

TEST(GeometryErrorTest, EveryReasonIsNamedDistinctly)
{
    for (const an::GeometryError left : kAllGeometryErrors)
    {
        EXPECT_NE(an::geometryErrorName(left), nullptr);
        for (const an::GeometryError right : kAllGeometryErrors)
        {
            const bool sameName = std::strcmp(an::geometryErrorName(left), an::geometryErrorName(right)) == 0;
            EXPECT_EQ(sameName, left == right)
                << an::geometryErrorName(left) << " and " << an::geometryErrorName(right);
        }
    }
}

TEST(GeometryErrorTest, TheNameIsTheSpellingOfTheEnumerator)
{
    EXPECT_STREQ(an::geometryErrorName(an::GeometryError::None), "None");
    EXPECT_STREQ(an::geometryErrorName(an::GeometryError::StrideTooSmall), "StrideTooSmall");
    EXPECT_STREQ(an::geometryErrorName(an::GeometryError::ArithmeticOverflow), "ArithmeticOverflow");
    EXPECT_STREQ(an::geometryErrorName(an::GeometryError::ElementSizeMismatch), "ElementSizeMismatch");
    EXPECT_STREQ(an::geometryErrorName(an::GeometryError::RegionOutOfBounds), "RegionOutOfBounds");
}

TEST(GeometryErrorTest, AValueFromOutsideTheEnumerationIsNamedAsOne)
{
    // A reason cast in from a caller compiled against a different build of this library is a thing
    // that can happen, and naming it as blank rather than as unknown is how a log stops saying
    // anything at the moment it matters most.
    EXPECT_STREQ(an::geometryErrorName(static_cast<an::GeometryError>(200)), "UnknownGeometryError");
}

TEST(GeometryErrorTest, TheReasonsAddedForTypedMemoryKeptTheEarlierValuesWhereTheyWere)
{
    // The four reasons that arrived with typed storage and views were appended rather than sorted
    // into the groups they belong with, because every enumerator above them was already crossing
    // the library boundary as a value. This is the case that says so, and that would fail if
    // somebody tidied the enumeration into a better reading order.
    EXPECT_EQ(static_cast<int>(an::GeometryError::None), 0);
    EXPECT_EQ(static_cast<int>(an::GeometryError::ImageTooLarge), 11);
    EXPECT_EQ(static_cast<int>(an::GeometryError::ElementSizeMismatch), 12);
    EXPECT_EQ(static_cast<int>(an::GeometryError::BufferTooSmall), 13);
    EXPECT_EQ(static_cast<int>(an::GeometryError::BaseAddressMisaligned), 14);
    EXPECT_EQ(static_cast<int>(an::GeometryError::RegionOutOfBounds), 15);
}

} // namespace
