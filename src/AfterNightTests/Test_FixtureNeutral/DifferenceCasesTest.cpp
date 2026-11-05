// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The comparison difference's case table: what the engine fixture executable reads must be well formed before it
// compares.

#include "fixtures/common/DifferenceCases.h"
#include "fixtures/common/FixtureCatalogue.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;

} // namespace

TEST(DifferenceCasesTest, EveryCaseIsUniqueAndNamesTwoFixturesOfOneShape)
{
    std::set<std::string> ids;
    for (const fixtures::DifferenceCase& item : fixtures::differenceCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        EXPECT_EQ(item.operation.rfind("difference.", 0u), 0u) << item.id();
        const fixtures::FixtureImage* first = fixtures::fixtureNamed(item.input);
        const fixtures::FixtureImage* second = fixtures::fixtureNamed(item.second);
        ASSERT_NE(first, nullptr) << item.id();
        ASSERT_NE(second, nullptr) << item.id();
        // The product compares only images of one width, height and channel count.
        EXPECT_EQ(first->width, second->width) << item.id();
        EXPECT_EQ(first->height, second->height) << item.id();
        EXPECT_EQ(first->channels, second->channels) << item.id();
        EXPECT_EQ(first->domain, second->domain) << item.id() << ": the engine compares images of one domain";
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_NE(item.source.find(".cpp:"), std::string::npos) << item.id();
        EXPECT_EQ(fixtures::differenceCase(item.id()), &item) << item.id();
    }
    EXPECT_EQ(fixtures::differenceCase("difference.none/none"), nullptr);
}

TEST(DifferenceCasesTest, AClassificationCarriesWhatItReads)
{
    for (const fixtures::DifferenceCase& item : fixtures::differenceCases())
    {
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, !item.acceptedReason.empty())
            << item.id();
        if (item.classification == fixtures::ExpectationClass::Tolerance)
        {
            EXPECT_GT(item.absoluteTolerance, 0.0) << item.id();
            EXPECT_GE(item.relativeTolerance, 0.0) << item.id();
            EXPECT_FALSE(item.note.empty()) << item.id() << ": a tolerance says why";
        }
        else
        {
            EXPECT_EQ(item.absoluteTolerance, 0.0) << item.id();
            EXPECT_EQ(item.relativeTolerance, 0.0) << item.id();
        }
    }
}
