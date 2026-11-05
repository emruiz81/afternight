// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The drawing tools, held to their recorded expectations.
//
// `drawing.expectations` records what the drawing tools draw from neutral inputs: single dabs, five-point strokes in
// each blend mode, a stroke with a repeated point, and a shape's coverage feathered and screened over a paint plane.
// Here the product's own document draws every stroke - the input imported as its mask, the brush begun, each point
// added at its pressure and the stroke ended - and the product's own shape engine feathers and screens every shape, and
// each answer is held to its record by the record's classification.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"

#include "controls/drawing/ShapeEngine.h"
#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/MaskRenderingCases.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;

using an::tests::drawing::admitted;
using an::tests::drawing::greyAsset;
using an::tests::drawing::importInto;
using an::tests::drawing::makeDrawing;
using an::tests::drawing::openDrawingProvider;
using an::tests::drawing::TestSessionProvider;
using an::ui::drawing::BrushParams;
using an::ui::drawing::DrawingDocument;
using an::ui::drawing::ShapeEngine;
using an::ui::drawing::ShapeLayer;

[[nodiscard]] BrushParams::BlendMode blendModeOf(fixtures::DrawingBlendMode mode)
{
    switch (mode)
    {
    case fixtures::DrawingBlendMode::Multiply:
        return BrushParams::BlendMode::Multiply;
    case fixtures::DrawingBlendMode::Screen:
        return BrushParams::BlendMode::Screen;
    case fixtures::DrawingBlendMode::Overlay:
        return BrushParams::BlendMode::Overlay;
    case fixtures::DrawingBlendMode::Erase:
        return BrushParams::BlendMode::Erase;
    case fixtures::DrawingBlendMode::Normal:
    default:
        return BrushParams::BlendMode::Normal;
    }
}

[[nodiscard]] std::optional<fixtures::FixtureSamples> materialized(const std::string& name)
{
    const fixtures::FixtureImage* fixture = fixtures::fixtureNamed(name);
    EXPECT_NE(fixture, nullptr) << name;
    if (fixture == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*fixture, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

[[nodiscard]] fixtures::FixtureSamples samplesOf(const std::vector<float>& plane, std::uint32_t width,
                                                 std::uint32_t height)
{
    fixtures::FixtureSamples result(width, height, 1u, fixtures::FixtureSampleType::Float32);
    std::memcpy(result.mutableBytes().data(), plane.data(), plane.size() * sizeof(float));
    return result;
}

[[nodiscard]] double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    double largest = 0.0;
    for (std::uint32_t y = 0u; y < left.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < left.width(); ++x)
        {
            largest = (std::max)(largest, std::abs(left.value(x, y, 0u) - right.value(x, y, 0u)));
        }
    }
    return largest;
}

class DrawingRecordTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_provider = openDrawingProvider(4u);
        ASSERT_TRUE(m_provider.has_value());
        std::string error;
        const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
        ASSERT_TRUE(directory.has_value()) << error;
        const std::optional<std::vector<fixtures::Expectation>> records =
            fixtures::readExpectationFile(*directory / fixtures::kDrawingExpectationFile, error);
        ASSERT_TRUE(records.has_value()) << error;
        m_records = *records;
    }

    void TearDown() override
    {
        m_provider.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_provider->engineSession();
    }

    /// The mask the product's document draws for a stroke case.
    [[nodiscard]] std::optional<fixtures::FixtureSamples> strokeByDocument(const fixtures::DrawingCase& item)
    {
        const std::optional<fixtures::FixtureSamples> input = materialized(item.input);
        if (!input.has_value())
        {
            return std::nullopt;
        }
        const int width = static_cast<int>(input->width());
        const int height = static_cast<int>(input->height());
        const std::vector<float> paint = fixtures::importedPlane(*input);
        const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), width, height);
        std::optional<an::consumer::ImageAsset> asset =
            greyAsset(session(), width, height, [&paint, width](std::uint32_t x, std::uint32_t y) {
                return paint[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x];
            });
        std::optional<an::consumer::OperationScope> scope = admitted(session());
        if (!document || !asset.has_value() || !scope.has_value())
        {
            return std::nullopt;
        }
        importInto(*document, scope->execution(), *asset);

        BrushParams brush;
        brush.size = item.diameter;
        brush.hardness = item.hardness;
        brush.tipShape = item.square ? BrushParams::TipShape::Square : BrushParams::TipShape::Round;
        brush.spacing = item.spacing;
        brush.flow = item.flow;
        brush.opacity = item.opacity;
        brush.blendMode = blendModeOf(item.mode);
        EXPECT_FALSE(document->beginStroke(scope->execution(), brush).failed());
        for (const fixtures::DrawingStrokePoint& point : item.points)
        {
            const an::Status added =
                document->addStrokePoint(scope->execution(), QPointF(point.x, point.y), point.pressure);
            EXPECT_FALSE(added.failed()) << added.message();
        }
        EXPECT_FALSE(document->endStroke(scope->execution()).failed());
        return maskSamples(*document);
    }

    /// The composite the product's shape engine makes for a feather case: the coverage feathered and screened over the
    /// paint at the shape's opacity.
    [[nodiscard]] std::optional<fixtures::FixtureSamples> featherByShapeEngine(const fixtures::DrawingCase& item)
    {
        const std::optional<fixtures::FixtureSamples> input = materialized(item.input);
        const std::optional<fixtures::FixtureSamples> base = materialized(item.base);
        if (!input.has_value() || !base.has_value())
        {
            return std::nullopt;
        }
        const auto width = input->width();
        const auto height = input->height();
        ShapeLayer layer;
        layer.bounds = QRect(0, 0, static_cast<int>(width), static_cast<int>(height));
        layer.coverage = fixtures::shapeCoverage(*input);
        std::vector<float> composite = fixtures::importedPlane(*base);
        std::optional<an::consumer::OperationScope> scope = admitted(session());
        const std::optional<an::PlaneView<float>> plane = an::ui::drawing::packedPlane(composite, width, height);
        if (!scope.has_value() || !plane.has_value())
        {
            return std::nullopt;
        }
        EXPECT_FALSE(ShapeEngine::featherShapeLayer(scope->execution(), layer, item.feather).failed());
        EXPECT_FALSE(
            ShapeEngine::screenShapeLayer(scope->execution(), layer, item.opacity, *plane, layer.bounds).failed());
        return samplesOf(composite, width, height);
    }

    [[nodiscard]] static std::optional<fixtures::FixtureSamples> maskSamples(const DrawingDocument& document)
    {
        an::Result<an::consumer::AssetReadLease> lease = document.mask()->tryRead();
        EXPECT_TRUE(lease.hasValue());
        if (!lease.hasValue())
        {
            return std::nullopt;
        }
        const auto width = static_cast<std::uint32_t>(document.width());
        const auto height = static_cast<std::uint32_t>(document.height());
        std::vector<float> plane(static_cast<std::size_t>(width) * height);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                plane[static_cast<std::size_t>(y) * width + x] = an::tests::controls::sampleAt(lease->image(), x, y);
            }
        }
        return samplesOf(plane, width, height);
    }

    std::optional<TestSessionProvider> m_provider;
    std::vector<fixtures::Expectation> m_records;
};

} // namespace

// Every stroke and dab the records hold, drawn by the document from the same input: bit for bit, as the records'
// exact classification requires.
TEST_F(DrawingRecordTest, EveryStrokeTheDocumentDrawsMatchesItsRecord)
{
    std::size_t compared = 0u;
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::DrawingCase* item = fixtures::drawingCase(record.id);
        ASSERT_NE(item, nullptr) << record.id;
        if (item->kind != fixtures::DrawingCaseKind::Stroke)
        {
            continue;
        }
        const std::optional<fixtures::FixtureSamples> drawn = strokeByDocument(*item);
        ASSERT_TRUE(drawn.has_value()) << record.id;
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *drawn);
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason << "; the largest difference is "
                                        << largestDifference(record.result, *drawn);
        EXPECT_EQ(item->classification, fixtures::ExpectationClass::Exact) << record.id;
        ++compared;
    }
    EXPECT_EQ(compared, 9u);
}

// Every feathered shape the records hold, feathered and screened by the shape engine: the unfeathered one within its
// rounding, and each feather within the bound its accepted difference states - the thin feather whose record is not
// blurred at all among them.
TEST_F(DrawingRecordTest, EveryFeatherTheShapeEngineDrawsIsWithinItsRecordsBound)
{
    std::size_t compared = 0u;
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::DrawingCase* item = fixtures::drawingCase(record.id);
        ASSERT_NE(item, nullptr) << record.id;
        if (item->kind != fixtures::DrawingCaseKind::Feather)
        {
            continue;
        }
        const std::optional<fixtures::FixtureSamples> drawn = featherByShapeEngine(*item);
        ASSERT_TRUE(drawn.has_value()) << record.id;
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *drawn);
        const double largest = largestDifference(record.result, *drawn);
        RecordProperty(record.id, std::to_string(largest));
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason << "; the largest difference is "
                                        << largest;
        if (item->classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_LE(largest, item->acceptedBound) << record.id;
        }
        ++compared;
    }
    EXPECT_EQ(compared, 4u);
}
