// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The drawing document: a zeroed Float32 mask asset, strokes landed by the engine's blend, the five
// modes against what the mask held, editable shapes, an imported mask, flood fills and gradients, each undone and
// redone.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"

#include "controls/drawing/DrawingDocument.h"
#include "controls/drawing/DrawingTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace
{

using an::tests::drawing::admitted;
using an::tests::drawing::Channels;
using an::tests::drawing::greyAsset;
using an::tests::drawing::importInto;
using an::tests::drawing::makeAsset;
using an::tests::drawing::makeBrush;
using an::tests::drawing::makeDrawing;
using an::tests::drawing::makeRectangle;
using an::tests::drawing::maskAt;
using an::tests::drawing::openDrawingProvider;
using an::tests::drawing::TestSessionProvider;
using an::ui::drawing::BrushParams;
using an::ui::drawing::DrawingDocument;

class DrawingDocumentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_provider = openDrawingProvider();
        ASSERT_TRUE(m_provider.has_value());
    }

    void TearDown() override
    {
        m_scope.reset();
        m_provider.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_provider->engineSession();
    }

    /// The execution every edit of a case runs under, admitted once.
    [[nodiscard]] const an::ExecutionContext& execution()
    {
        if (!m_scope.has_value())
        {
            m_scope = admitted(session());
        }
        return m_scope->execution();
    }

    /// A document whose paint is one value at one pixel and zero elsewhere.
    [[nodiscard]] std::shared_ptr<DrawingDocument> withPixel(int width, int height, int x, int y, float value)
    {
        std::shared_ptr<DrawingDocument> document = makeDrawing(session(), width, height);
        std::optional<an::consumer::ImageAsset> asset =
            greyAsset(session(), width, height, [x, y, value](std::uint32_t px, std::uint32_t py) {
                return static_cast<int>(px) == x && static_cast<int>(py) == y ? value : 0.0f;
            });
        EXPECT_TRUE(asset.has_value());
        if (document && asset.has_value())
        {
            importInto(*document, execution(), *asset);
        }
        return document;
    }

    void stroke(DrawingDocument& document, const BrushParams& brush, std::initializer_list<QPointF> points)
    {
        ASSERT_FALSE(document.beginStroke(execution(), brush).failed());
        for (const QPointF& point : points)
        {
            const an::Status added = document.addStrokePoint(execution(), point);
            ASSERT_FALSE(added.failed()) << added.message();
        }
        ASSERT_FALSE(document.endStroke(execution()).failed());
    }

    std::optional<TestSessionProvider> m_provider;
    std::optional<an::consumer::OperationScope> m_scope;
};

TEST_F(DrawingDocumentTest, InitializesZeroedFloatGrayscaleMaskBuffer)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 20, 14);
    ASSERT_NE(document, nullptr);
    const std::optional<an::ImageDescriptor> described = document->mask()->descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), 20u);
    EXPECT_EQ(described->extent().height(), 14u);
    EXPECT_EQ(described->extent().channelCount(), 1u);
    EXPECT_EQ(described->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(described->channelSchema(), an::ChannelSchema::Mono);
    EXPECT_FLOAT_EQ(maskAt(*document, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 19, 13), 0.0f);
    EXPECT_TRUE(document->isEmpty());
}

TEST_F(DrawingDocumentTest, StrokeUpdatesMaskAndSupportsUndoRedo)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 32, 32);
    ASSERT_NE(document, nullptr);

    stroke(*document, makeBrush(6.0f), {QPointF(8.0, 12.0), QPointF(20.0, 12.0)});

    ASSERT_TRUE(document->canUndo());
    EXPECT_GT(maskAt(*document, 14, 12), 0.0f);

    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_FALSE(document->canUndo());
    EXPECT_TRUE(document->canRedo());
    EXPECT_FLOAT_EQ(maskAt(*document, 14, 12), 0.0f);

    ASSERT_FALSE(document->redo(execution()).failed());
    EXPECT_TRUE(document->canUndo());
    EXPECT_FALSE(document->canRedo());
    EXPECT_GT(maskAt(*document, 14, 12), 0.0f);
}

TEST_F(DrawingDocumentTest, ClearResetsMaskAndCanBeUndone)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 24, 24);
    ASSERT_NE(document, nullptr);

    stroke(*document, makeBrush(5.0f), {QPointF(12.0, 12.0)});
    ASSERT_GT(maskAt(*document, 12, 12), 0.0f);

    ASSERT_FALSE(document->clear(execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 12, 12), 0.0f);
    EXPECT_TRUE(document->canUndo());

    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_GT(maskAt(*document, 12, 12), 0.0f);
}

TEST_F(DrawingDocumentTest, BlendModesCompositeAgainstExistingMaskData)
{
    constexpr int kX = 8;
    constexpr int kY = 8;

    const std::shared_ptr<DrawingDocument> multiplyDocument = withPixel(16, 16, kX, kY, 0.8f);
    ASSERT_NE(multiplyDocument, nullptr);
    BrushParams multiplyBrush = makeBrush(1.0f);
    multiplyBrush.flow = 0.5f;
    multiplyBrush.blendMode = BrushParams::BlendMode::Multiply;
    stroke(*multiplyDocument, multiplyBrush, {QPointF(kX, kY)});
    EXPECT_NEAR(maskAt(*multiplyDocument, kX, kY), 0.4f, 0.01f);

    const std::shared_ptr<DrawingDocument> screenDocument = withPixel(16, 16, kX, kY, 0.2f);
    ASSERT_NE(screenDocument, nullptr);
    BrushParams screenBrush = makeBrush(1.0f);
    screenBrush.flow = 0.5f;
    screenBrush.blendMode = BrushParams::BlendMode::Screen;
    stroke(*screenDocument, screenBrush, {QPointF(kX, kY)});
    EXPECT_NEAR(maskAt(*screenDocument, kX, kY), 0.6f, 0.01f);

    const std::shared_ptr<DrawingDocument> overlayDocument = withPixel(16, 16, kX, kY, 0.75f);
    ASSERT_NE(overlayDocument, nullptr);
    BrushParams overlayBrush = makeBrush(1.0f);
    overlayBrush.flow = 0.25f;
    overlayBrush.blendMode = BrushParams::BlendMode::Overlay;
    stroke(*overlayDocument, overlayBrush, {QPointF(kX, kY)});
    EXPECT_NEAR(maskAt(*overlayDocument, kX, kY), 0.625f, 0.01f);
}

TEST_F(DrawingDocumentTest, EditableShapesRasterizeAndSupportUndoRedo)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 32, 32);
    ASSERT_NE(document, nullptr);

    an::ui::drawing::ShapeObject rect = makeRectangle(10.0, 10.0, 8.0, 6.0);
    const an::Result<std::size_t> rectIndex = document->addShape(execution(), rect);
    ASSERT_TRUE(rectIndex.hasValue()) << rectIndex.status().message();
    EXPECT_EQ(*rectIndex, 0u);
    ASSERT_EQ(document->shapes().size(), 1u);
    EXPECT_GT(maskAt(*document, 10, 10), 0.9f);

    rect.center = QPointF(18.0, 18.0);
    ASSERT_FALSE(document->updateShape(execution(), *rectIndex, rect).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 10, 10), 0.0f);
    EXPECT_GT(maskAt(*document, 18, 18), 0.9f);

    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_GT(maskAt(*document, 10, 10), 0.9f);
    EXPECT_FLOAT_EQ(maskAt(*document, 18, 18), 0.0f);

    ASSERT_FALSE(document->redo(execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 10, 10), 0.0f);
    EXPECT_GT(maskAt(*document, 18, 18), 0.9f);

    ASSERT_FALSE(document->removeShape(execution(), *rectIndex).failed());
    EXPECT_TRUE(document->shapes().empty());
    EXPECT_FLOAT_EQ(maskAt(*document, 18, 18), 0.0f);
}

TEST_F(DrawingDocumentTest, ImportMaskConvertsToFloatGrayscaleAndSupportsUndoRedo)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 8, 6);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::ImageAsset> imported =
        makeAsset<std::uint16_t>(session(), 8u, 6u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return x == 5u && y == 4u ? std::uint16_t{65535u} : static_cast<std::uint16_t>(std::lround(0.25 * 65535.0));
        });
    ASSERT_TRUE(imported.has_value());

    importInto(*document, execution(), *imported);

    EXPECT_NEAR(maskAt(*document, 0, 0), 0.25f, 0.01f);
    EXPECT_NEAR(maskAt(*document, 5, 4), 1.0f, 0.01f);
    EXPECT_EQ(document->dirtyRect(), QRect(0, 0, 8, 6));
    EXPECT_TRUE(document->canUndo());

    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_TRUE(document->isEmpty());

    ASSERT_FALSE(document->redo(execution()).failed());
    EXPECT_NEAR(maskAt(*document, 5, 4), 1.0f, 0.01f);
}

TEST_F(DrawingDocumentTest, FloodFillUpdatesOnlyConnectedRegionAndTracksDirtyRect)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 8, 5);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::ImageAsset> imported =
        greyAsset(session(), 8, 5, [](std::uint32_t x, std::uint32_t) { return x >= 4u ? 1.0f : 0.0f; });
    ASSERT_TRUE(imported.has_value());
    importInto(*document, execution(), *imported);

    ASSERT_FALSE(document->floodFill(execution(), QPoint(1, 2), 0.65f, 0.01f).failed());

    EXPECT_NEAR(maskAt(*document, 0, 0), 0.65f, 0.01f);
    EXPECT_NEAR(maskAt(*document, 3, 4), 0.65f, 0.01f);
    EXPECT_NEAR(maskAt(*document, 4, 0), 1.0f, 0.01f);
    EXPECT_EQ(document->dirtyRect(), QRect(0, 0, 4, 5));

    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 4, 0), 1.0f);

    ASSERT_FALSE(document->redo(execution()).failed());
    EXPECT_NEAR(maskAt(*document, 2, 2), 0.65f, 0.01f);
}

TEST_F(DrawingDocumentTest, LinearGradientAppliesAcrossCanvasAndSupportsUndoRedo)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 5, 1);
    ASSERT_NE(document, nullptr);

    ASSERT_FALSE(document
                     ->applyLinearGradient(execution(), QPointF(0.0, 0.0), QPointF(4.0, 0.0), 0.0f, 1.0f,
                                           BrushParams::BlendMode::Normal)
                     .failed());

    EXPECT_NEAR(maskAt(*document, 0, 0), 0.0f, 0.01f);
    EXPECT_NEAR(maskAt(*document, 2, 0), 0.5f, 0.05f);
    EXPECT_NEAR(maskAt(*document, 4, 0), 1.0f, 0.01f);
    EXPECT_EQ(document->dirtyRect(), QRect(0, 0, 5, 1));

    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_TRUE(document->isEmpty());

    ASSERT_FALSE(document->redo(execution()).failed());
    EXPECT_NEAR(maskAt(*document, 4, 0), 1.0f, 0.01f);
}

// A colour mask is imported as its Rec. 601 luma; an image of another size or of two channels is refused and changes
// nothing.
TEST_F(DrawingDocumentTest, AColourMaskIsImportedAsItsLumaAndAnImageOfAnotherShapeIsRefused)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 4, 1);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::ImageAsset> colour =
        makeAsset<float>(session(), 4u, 1u, Channels::Rgb, [](std::uint32_t x, std::uint32_t, std::uint32_t channel) {
            return x == channel ? 1.0f : 0.0f;
        });
    ASSERT_TRUE(colour.has_value());
    importInto(*document, execution(), *colour);
    EXPECT_NEAR(maskAt(*document, 0, 0), 0.299f, 1.0e-5f);
    EXPECT_NEAR(maskAt(*document, 1, 0), 0.587f, 1.0e-5f);
    EXPECT_NEAR(maskAt(*document, 2, 0), 0.114f, 1.0e-5f);
    EXPECT_FLOAT_EQ(maskAt(*document, 3, 0), 0.0f);

    const std::uint64_t revision = document->revision();
    std::optional<an::consumer::ImageAsset> wider =
        greyAsset(session(), 5, 1, [](std::uint32_t, std::uint32_t) { return 1.0f; });
    ASSERT_TRUE(wider.has_value());
    an::Result<an::consumer::AssetReadLease> lease = wider->read();
    ASSERT_TRUE(lease.hasValue());
    const an::Status refused = document->importMask(execution(), *lease);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(document->revision(), revision);
    EXPECT_NEAR(maskAt(*document, 1, 0), 0.587f, 1.0e-5f);
}

// A not-a-number in an imported mask is read as no coverage, and so are values outside the unit interval held to it:
// nothing that is not a number reaches a stroke, a blend or the output.
TEST_F(DrawingDocumentTest, ANotANumberInAnImportedMaskIsReadAsNoCoverage)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 4, 1);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::ImageAsset> imported = greyAsset(session(), 4, 1, [](std::uint32_t x, std::uint32_t) {
        switch (x)
        {
        case 0u:
            return std::nanf("");
        case 1u:
            return -0.5f;
        case 2u:
            return 3.0f;
        default:
            return 0.5f;
        }
    });
    ASSERT_TRUE(imported.has_value());
    importInto(*document, execution(), *imported);
    EXPECT_FLOAT_EQ(maskAt(*document, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 1, 0), 0.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 2, 0), 1.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 3, 0), 0.5f);

    BrushParams screen = makeBrush(1.0f);
    screen.blendMode = BrushParams::BlendMode::Screen;
    screen.flow = 0.5f;
    stroke(*document, screen, {QPointF(0.0, 0.0)});
    EXPECT_FLOAT_EQ(maskAt(*document, 0, 0), 0.5f);

    const an::Result<an::consumer::ImageAsset> output = document->createOutputMask();
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    an::Result<an::consumer::AssetReadLease> lease = output->read();
    ASSERT_TRUE(lease.hasValue());
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        EXPECT_TRUE(std::isfinite(an::tests::controls::sampleAt(lease->image(), x, 0u))) << x;
    }
}

// A flood fill's seed outside the drawing is refused as out of range, with nothing changed and no step kept.
TEST_F(DrawingDocumentTest, AFloodFillSeededOutsideTheDrawingIsRefused)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 6, 6);
    ASSERT_NE(document, nullptr);
    const an::Status refused = document->floodFill(execution(), QPoint(6, 0), 1.0f);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::OutOfRange);
    EXPECT_FALSE(document->canUndo());
    EXPECT_TRUE(document->isEmpty());
}

} // anonymous namespace
