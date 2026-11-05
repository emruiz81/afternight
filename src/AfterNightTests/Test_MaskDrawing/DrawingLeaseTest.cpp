// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The mask as an asset and the edits that land on it: an edit is refused while the mask is read and changes nothing,
// the output mask is a snapshot later edits do not reach, every change publishes a revision, the document holds its
// session's context while it lives, and the queue lands every edit in the order it was made - waiting, never dropping,
// while the session is busy or the mask is read.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"

#include "controls/drawing/DrawingDocument.h"
#include "controls/drawing/DrawingEditQueue.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

namespace
{

using an::tests::drawing::admitted;
using an::tests::drawing::makeBrush;
using an::tests::drawing::makeDrawing;
using an::tests::drawing::makeRectangle;
using an::tests::drawing::maskAt;
using an::tests::drawing::openDrawingProvider;
using an::tests::drawing::pumpQuietly;
using an::tests::drawing::pumpUntil;
using an::tests::drawing::TestSessionProvider;
using an::ui::drawing::BrushParams;
using an::ui::drawing::DrawingDocument;
using an::ui::drawing::DrawingEdit;
using an::ui::drawing::DrawingEditQueue;
using an::ui::drawing::DrawingEditReport;

/// The edits of one stroke through points, as the canvas queues them.
[[nodiscard]] std::vector<DrawingEdit> strokeEdits(const BrushParams& brush, std::vector<QPointF> points)
{
    std::vector<DrawingEdit> edits;
    edits.emplace_back([brush](DrawingDocument& document, const an::ExecutionContext& execution) {
        return document.beginStroke(execution, brush);
    });
    for (const QPointF& point : points)
    {
        edits.emplace_back([point](DrawingDocument& document, const an::ExecutionContext& execution) {
            return document.addStrokePoint(execution, point);
        });
    }
    edits.emplace_back(
        [](DrawingDocument& document, const an::ExecutionContext& execution) { return document.endStroke(execution); });
    return edits;
}

/// Whether two documents' masks hold the same samples.
[[nodiscard]] bool sameMask(const DrawingDocument& left, const DrawingDocument& right)
{
    for (int y = 0; y < left.height(); ++y)
    {
        for (int x = 0; x < left.width(); ++x)
        {
            if (maskAt(left, x, y) != maskAt(right, x, y))
            {
                return false;
            }
        }
    }
    return true;
}

class DrawingLeaseTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_provider = openDrawingProvider(1u);
        ASSERT_TRUE(m_provider.has_value());
    }

    void TearDown() override
    {
        pumpQuietly();
        m_provider.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_provider->engineSession();
    }

    std::optional<TestSessionProvider> m_provider;
};

// While anything reads the mask, every edit is refused as busy and changes nothing - no sample, no revision, no step,
// and not the active stroke, which a refused fill or shape does not end; once the reader lets go, the same edits land.
TEST_F(DrawingLeaseTest, AnEditWhileTheMaskIsReadIsRefusedAndChangesNothing)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 40, 30);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::OperationScope> scope = admitted(session());
    ASSERT_TRUE(scope.has_value());
    ASSERT_FALSE(document->beginStroke(scope->execution(), makeBrush(5.0f)).failed());

    {
        an::Result<an::consumer::AssetReadLease> reader = document->mask()->tryRead();
        ASSERT_TRUE(reader.hasValue());
        const std::uint64_t revision = document->revision();
        EXPECT_EQ(document->addStrokePoint(scope->execution(), QPointF(10.0, 10.0)).code(), an::ErrorCode::Busy);
        EXPECT_EQ(document->floodFill(scope->execution(), QPoint(30, 20), 1.0f).code(), an::ErrorCode::Busy);
        EXPECT_EQ(document->addShape(scope->execution(), makeRectangle(20.0, 15.0, 6.0, 6.0)).status().code(),
                  an::ErrorCode::Busy);
        EXPECT_EQ(document->undo(scope->execution()).code(), an::ErrorCode::Busy);
        EXPECT_EQ(document->endStroke(scope->execution()).code(), an::ErrorCode::Busy);
        EXPECT_EQ(document->revision(), revision);
        EXPECT_TRUE(document->hasActiveStroke());
        EXPECT_TRUE(document->shapes().empty());
        EXPECT_FALSE(document->canUndo());
        EXPECT_FLOAT_EQ(an::tests::controls::sampleAt(reader->image(), 10u, 10u), 0.0f);
    }

    ASSERT_FALSE(document->addStrokePoint(scope->execution(), QPointF(10.0, 10.0)).failed());
    ASSERT_FALSE(document->endStroke(scope->execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 10, 10), 1.0f);
    EXPECT_TRUE(document->canUndo());
}

// The output mask is a copy with an identity of its own: the document's later edits do not reach it.
TEST_F(DrawingLeaseTest, TheOutputMaskIsASnapshotTheDocumentsLaterEditsDoNotReach)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 24, 24);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::OperationScope> scope = admitted(session());
    ASSERT_TRUE(scope.has_value());
    ASSERT_FALSE(document->beginStroke(scope->execution(), makeBrush(3.0f)).failed());
    ASSERT_FALSE(document->addStrokePoint(scope->execution(), QPointF(5.0, 5.0)).failed());
    ASSERT_FALSE(document->endStroke(scope->execution()).failed());

    const an::Result<an::consumer::ImageAsset> output = document->createOutputMask();
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    EXPECT_NE(output->identity(), document->mask()->identity());

    ASSERT_FALSE(document->clear(scope->execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 5, 5), 0.0f);
    an::Result<an::consumer::AssetReadLease> lease = output->read();
    ASSERT_TRUE(lease.hasValue());
    EXPECT_FLOAT_EQ(an::tests::controls::sampleAt(lease->image(), 5u, 5u), 1.0f);
}

// Every change publishes a new revision of the mask asset; a refused one publishes none.
TEST_F(DrawingLeaseTest, EveryChangePublishesARevisionOfTheMaskAsset)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 16, 16);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::OperationScope> scope = admitted(session());
    ASSERT_TRUE(scope.has_value());
    const an::consumer::AssetRevision empty = document->mask()->revision();

    ASSERT_TRUE(document->addShape(scope->execution(), makeRectangle(8.0, 8.0, 4.0, 4.0)).hasValue());
    const an::consumer::AssetRevision shaped = document->mask()->revision();
    EXPECT_NE(shaped, empty);

    {
        an::Result<an::consumer::AssetReadLease> reader = document->mask()->tryRead();
        ASSERT_TRUE(reader.hasValue());
        EXPECT_TRUE(document->undo(scope->execution()).failed());
    }
    EXPECT_EQ(document->mask()->revision(), shaped);
    EXPECT_TRUE(document->canUndo());

    ASSERT_FALSE(document->undo(scope->execution()).failed());
    EXPECT_NE(document->mask()->revision(), shaped);
}

// A document's planes and mask come from its session's context, so the session counts them as outstanding while the
// document lives and is free of them once it is gone.
TEST_F(DrawingLeaseTest, TheDocumentHoldsItsSessionsContextWhileItLives)
{
    std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 16, 16);
    ASSERT_NE(document, nullptr);
    EXPECT_GE(session().outstanding().assets, 1u);
    EXPECT_FALSE(session().outstanding().none());
    document.reset();
    EXPECT_TRUE(session().outstanding().none());
}

// While another operation holds the only thread of a session, the queue keeps every edit in order and lands none;
// when the thread is free, the next retry lands them all, and the stroke is the one drawn with nothing in the way.
TEST_F(DrawingLeaseTest, EditsWaitInOrderWhileTheSessionIsBusyAndLandTogether)
{
    const std::shared_ptr<DrawingDocument> queued = makeDrawing(session(), 48, 32);
    const std::shared_ptr<DrawingDocument> direct = makeDrawing(session(), 48, 32);
    ASSERT_NE(queued, nullptr);
    ASSERT_NE(direct, nullptr);
    const std::vector<QPointF> points{QPointF(6.0, 6.0), QPointF(20.0, 14.0), QPointF(40.0, 26.0)};

    std::vector<DrawingEditReport> reports;
    DrawingEditQueue queue([&reports](const DrawingEditReport& report) { reports.push_back(report); });
    queue.setRetryInterval(std::chrono::milliseconds(1));
    queue.setDocument(queued);
    std::optional<an::consumer::OperationScope> busy = admitted(session());
    ASSERT_TRUE(busy.has_value());
    for (DrawingEdit& edit : strokeEdits(makeBrush(5.0f), points))
    {
        queue.submit(std::move(edit));
    }
    EXPECT_EQ(queue.pending(), 5u);
    pumpQuietly();
    EXPECT_EQ(queue.pending(), 5u);
    EXPECT_GT(queue.refusals(), 0u);
    EXPECT_TRUE(reports.empty());
    EXPECT_FLOAT_EQ(maskAt(*queued, 6, 6), 0.0f);

    // The direct drawing runs under the busy operation itself.
    for (DrawingEdit& edit : strokeEdits(makeBrush(5.0f), points))
    {
        ASSERT_FALSE(edit(*direct, busy->execution()).failed());
    }
    busy.reset();

    ASSERT_TRUE(pumpUntil([&queue]() { return queue.pending() == 0u; }));
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports.front().applied, 5u);
    EXPECT_EQ(reports.front().dropped, 0u);
    EXPECT_TRUE(queued->canUndo());
    EXPECT_FALSE(queued->hasActiveStroke());
    EXPECT_TRUE(sameMask(*queued, *direct));
}

// An edit the document refuses because the mask is being read stays at the head of the queue and lands once the reader
// lets go; nothing after it overtakes it.
TEST_F(DrawingLeaseTest, AnEditRefusedBecauseTheMaskIsReadIsRetried)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 32, 32);
    ASSERT_NE(document, nullptr);
    DrawingEditQueue queue;
    queue.setRetryInterval(std::chrono::milliseconds(1));
    queue.setDocument(document);

    std::optional<an::consumer::AssetReadLease> reader;
    {
        an::Result<an::consumer::AssetReadLease> taken = document->mask()->tryRead();
        ASSERT_TRUE(taken.hasValue());
        reader.emplace(std::move(*taken));
    }
    for (DrawingEdit& edit : strokeEdits(makeBrush(5.0f), {QPointF(16.0, 16.0)}))
    {
        queue.submit(std::move(edit));
    }
    pumpQuietly();
    // Every edit is made under the mask's write lease, so the stroke has not begun and all three wait in order.
    EXPECT_EQ(queue.pending(), 3u);
    EXPECT_GT(queue.refusals(), 0u);
    EXPECT_FALSE(document->hasActiveStroke());
    EXPECT_FLOAT_EQ(an::tests::controls::sampleAt(reader->image(), 16u, 16u), 0.0f);

    reader.reset();
    ASSERT_TRUE(pumpUntil([&queue]() { return queue.pending() == 0u; }));
    EXPECT_FLOAT_EQ(maskAt(*document, 16, 16), 1.0f);
    EXPECT_FALSE(document->hasActiveStroke());
    EXPECT_TRUE(document->canUndo());
}

// An edit the document refuses for any other reason is dropped and kept as the last failure, and the edits after it
// still land.
TEST_F(DrawingLeaseTest, AnEditTheDocumentRefusesIsDroppedAndTheNextLands)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 16, 16);
    ASSERT_NE(document, nullptr);
    std::vector<DrawingEditReport> reports;
    DrawingEditQueue queue([&reports](const DrawingEditReport& report) { reports.push_back(report); });
    queue.setDocument(document);

    queue.submit([](DrawingDocument& target, const an::ExecutionContext& execution) {
        return target.addStrokePoint(execution, QPointF(4.0, 4.0));
    });
    queue.submit([](DrawingDocument& target, const an::ExecutionContext& execution) {
        return target.floodFill(execution, QPoint(8, 8), 0.5f);
    });

    EXPECT_EQ(queue.pending(), 0u);
    EXPECT_EQ(queue.lastFailure().code(), an::ErrorCode::InvalidState);
    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].dropped, 1u);
    EXPECT_EQ(reports[1].applied, 1u);
    EXPECT_FLOAT_EQ(maskAt(*document, 8, 8), 0.5f);
}

// The edits waiting for one document are dropped when the queue is given another, and none reaches the new one.
TEST_F(DrawingLeaseTest, ChangingTheDocumentDropsTheEditsWaitingForTheOldOne)
{
    const std::shared_ptr<DrawingDocument> first = makeDrawing(session(), 16, 16);
    const std::shared_ptr<DrawingDocument> second = makeDrawing(session(), 16, 16);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    DrawingEditQueue queue;
    queue.setRetryInterval(std::chrono::milliseconds(1));
    queue.setDocument(first);
    {
        std::optional<an::consumer::OperationScope> busy = admitted(session());
        ASSERT_TRUE(busy.has_value());
        queue.submit([](DrawingDocument& target, const an::ExecutionContext& execution) {
            return target.floodFill(execution, QPoint(8, 8), 1.0f);
        });
        EXPECT_EQ(queue.pending(), 1u);
        queue.setDocument(second);
        EXPECT_EQ(queue.pending(), 0u);
    }
    pumpQuietly();
    EXPECT_TRUE(first->isEmpty());
    EXPECT_TRUE(second->isEmpty());
}

} // anonymous namespace
