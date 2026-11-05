// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The display paths' case table: what the engine fixture executable reads must be well formed before it compares.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/PresentationCases.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;

} // namespace

TEST(PresentationCasesTest, EveryCaseIsUniqueAndNamesAFixture)
{
    std::set<std::string> ids;
    for (const fixtures::PresentationCase& item : fixtures::presentationCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        EXPECT_EQ(item.operation.rfind("presentation.", 0u), 0u) << item.id();
        EXPECT_NE(fixtures::fixtureNamed(item.input), nullptr) << item.id();
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_NE(item.source.find(".cpp:"), std::string::npos) << item.id();
        EXPECT_EQ(fixtures::presentationCase(item.id()), &item) << item.id();
    }
    EXPECT_EQ(fixtures::presentationCase("presentation.none/none"), nullptr);
}

TEST(PresentationCasesTest, AClassificationCarriesWhatItReads)
{
    for (const fixtures::PresentationCase& item : fixtures::presentationCases())
    {
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, !item.acceptedReason.empty())
            << item.id();
        if (item.classification == fixtures::ExpectationClass::Tolerance)
        {
            EXPECT_GT(item.absoluteTolerance, 0.0) << item.id();
            EXPECT_GE(item.relativeTolerance, 0.0) << item.id();
            // A bound wider than a byte's rounding says why.
            EXPECT_EQ(item.absoluteTolerance > 1.0 || item.product == fixtures::PresentationProduct::Native,
                      !item.note.empty())
                << item.id();
        }
        EXPECT_GE(item.subsample, 1u) << item.id();
        if (item.path == fixtures::PresentationPath::Lens)
        {
            const fixtures::FixtureImage* image = fixtures::fixtureNamed(item.input);
            ASSERT_NE(image, nullptr);
            EXPECT_LE(item.cropX + item.cropWidth, image->width) << item.id();
            EXPECT_LE(item.cropY + item.cropHeight, image->height) << item.id();
        }
    }
}

TEST(PresentationCasesTest, TheLevelsMapToAnOffsetAndAFactor)
{
    EXPECT_EQ(fixtures::presentationBrightnessOffset(0), 0.0);
    EXPECT_DOUBLE_EQ(fixtures::presentationBrightnessOffset(51), 0.2);
    EXPECT_DOUBLE_EQ(fixtures::presentationBrightnessOffset(-255), -1.0);
    EXPECT_DOUBLE_EQ(fixtures::presentationContrastFactor(0), 1.0);
    EXPECT_GT(fixtures::presentationContrastFactor(30), 1.0);
    EXPECT_LT(fixtures::presentationContrastFactor(-30), 1.0);
    EXPECT_DOUBLE_EQ(fixtures::presentationContrastFactor(-255), 0.0);
}
