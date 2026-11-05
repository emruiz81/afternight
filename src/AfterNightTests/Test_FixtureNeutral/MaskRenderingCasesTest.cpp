// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The mask rendering and drawing case tables: what the engine fixture executable reads must be well formed before it
// compares, and the two plane rules it applies must say what the cases say they do.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/MaskRenderingCases.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;

template <typename CaseT> void expectClassificationCarriesWhatItReads(const CaseT& item)
{
    EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, !item.acceptedReason.empty()) << item.id();
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
    if (item.classification == fixtures::ExpectationClass::Accepted)
    {
        EXPECT_GE(item.acceptedBound, 0.0) << item.id();
        EXPECT_FALSE(item.note.empty()) << item.id() << ": an accepted case says what is compared";
    }
    else
    {
        EXPECT_EQ(item.acceptedBound, 0.0) << item.id();
    }
}

} // namespace

TEST(MaskRenderingCasesTest, EveryOverlayIsUniqueAndNamesAMaskAndAnyImageOfOneSize)
{
    std::set<std::string> ids;
    for (const fixtures::MaskOverlayCase& item : fixtures::maskOverlayCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        EXPECT_EQ(item.operation.rfind("overlay.", 0u), 0u) << item.id();
        EXPECT_EQ(item.operation.find("-inverted") != std::string::npos, item.invert) << item.id();
        const fixtures::FixtureImage* mask = fixtures::fixtureNamed(item.mask);
        ASSERT_NE(mask, nullptr) << item.id();
        // The product offers only a mask of the image's size, and a colour mask only over a colour image.
        EXPECT_EQ(item.mode == fixtures::MaskOverlayCaseMode::Multiply, !item.image.empty()) << item.id();
        if (!item.image.empty())
        {
            const fixtures::FixtureImage* image = fixtures::fixtureNamed(item.image);
            ASSERT_NE(image, nullptr) << item.id();
            EXPECT_EQ(mask->width, image->width) << item.id();
            EXPECT_EQ(mask->height, image->height) << item.id();
            EXPECT_TRUE(mask->channels == fixtures::FixtureChannels::Mono || image->channels == mask->channels)
                << item.id();
        }
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_NE(item.source.find(".cpp:"), std::string::npos) << item.id();
        EXPECT_EQ(fixtures::maskOverlayCase(item.id()), &item) << item.id();
        expectClassificationCarriesWhatItReads(item);
    }
    EXPECT_EQ(fixtures::maskOverlayCase("overlay.none/none"), nullptr);
}

TEST(MaskRenderingCasesTest, EveryDrawingIsUniqueAndNamesPlanesOfOneSize)
{
    std::set<std::string> ids;
    for (const fixtures::DrawingCase& item : fixtures::drawingCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        EXPECT_EQ(item.operation.rfind("drawing.", 0u), 0u) << item.id();
        const fixtures::FixtureImage* input = fixtures::fixtureNamed(item.input);
        ASSERT_NE(input, nullptr) << item.id();
        EXPECT_EQ(input->channels, fixtures::FixtureChannels::Mono) << item.id() << ": a drawing plane is one channel";
        if (item.kind == fixtures::DrawingCaseKind::Stroke)
        {
            EXPECT_TRUE(item.base.empty()) << item.id();
            EXPECT_FALSE(item.points.empty()) << item.id();
            EXPECT_GE(item.diameter, 1.0f) << item.id();
            for (const fixtures::DrawingStrokePoint& point : item.points)
            {
                EXPECT_GE(point.pressure, 0.0f) << item.id();
                EXPECT_LE(point.pressure, 1.0f) << item.id();
            }
        }
        else
        {
            const fixtures::FixtureImage* base = fixtures::fixtureNamed(item.base);
            ASSERT_NE(base, nullptr) << item.id();
            EXPECT_EQ(base->width, input->width) << item.id();
            EXPECT_EQ(base->height, input->height) << item.id();
            EXPECT_TRUE(item.points.empty()) << item.id();
            EXPECT_GE(item.feather, 0.0f) << item.id();
        }
        EXPECT_GE(item.opacity, 0.0f) << item.id();
        EXPECT_LE(item.opacity, 1.0f) << item.id();
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_NE(item.source.find(".cpp:"), std::string::npos) << item.id();
        EXPECT_EQ(fixtures::drawingCase(item.id()), &item) << item.id();
        expectClassificationCarriesWhatItReads(item);
    }
    EXPECT_EQ(fixtures::drawingCase("drawing.none/none"), nullptr);
}

TEST(MaskRenderingCasesTest, AnImportedPlaneIsHeldToTheUnitIntervalAndACoverageIsQuantizedToBytes)
{
    fixtures::FixtureSamples samples(4u, 1u, 1u, fixtures::FixtureSampleType::Float32);
    samples.setValue(0u, 0u, 0u, -0.5);
    samples.setValue(1u, 0u, 0u, 0.3);
    samples.setValue(2u, 0u, 0u, 1.5);
    samples.setValue(3u, 0u, 0u, static_cast<double>(std::numeric_limits<float>::quiet_NaN()));
    const std::vector<float> imported = fixtures::importedPlane(samples);
    ASSERT_EQ(imported.size(), 4u);
    EXPECT_EQ(imported[0], 0.0f);
    EXPECT_EQ(imported[1], 0.3f);
    EXPECT_EQ(imported[2], 1.0f);
    // The product's clamp lets a value that is not a number through.
    EXPECT_TRUE(std::isnan(imported[3]));
    const std::vector<float> coverage = fixtures::shapeCoverage(samples);
    EXPECT_EQ(coverage[0], 0.0f);
    EXPECT_EQ(coverage[1], 77.0f / 255.0f);
    EXPECT_EQ(coverage[2], 1.0f);
    EXPECT_EQ(coverage[3], 0.0f);
}
