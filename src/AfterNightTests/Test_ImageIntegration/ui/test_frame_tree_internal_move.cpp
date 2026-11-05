// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QDropEvent>
#include <QMimeData>
#include <QtTest/QSignalSpy>
#include <QHeaderView>
#include <QEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QApplication>
#include <QtTest/QTest>
#include <QUndoStack>

#include <memory>

#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

namespace
{
/// Answers the move confirmation the view opens without a nested event loop - the drop itself is declined and the
/// frames move once the person answers - and lets the move land.
///
/// @return whether the view asked.
bool answerMoveConfirmation(QWidget* view, const QMessageBox::StandardButton answer = QMessageBox::Yes)
{
    QCoreApplication::processEvents();
    QMessageBox* asked = nullptr;
    for (QMessageBox* box : view->findChildren<QMessageBox*>())
    {
        if (!box->isHidden())
        {
            asked = box;
        }
    }
    if (asked == nullptr)
    {
        return false;
    }
    if (QAbstractButton* button = asked->button(answer))
    {
        button->click();
    }
    else
    {
        asked->done(answer);
    }
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return true;
}
} // namespace

// Helper to expose protected methods and find children
class TestFrameTreeView : public FrameTreeView
{
public:
    using FrameTreeView::dragEnterEvent;
    using FrameTreeView::dragMoveEvent;
    using FrameTreeView::dropEvent;
    using FrameTreeView::indexAt;
};

class FrameTreeInternalMoveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        undoStack = new QUndoStack();
        treeModel = new GroupTreeModel(FrameType::Light, model, nullptr, undoStack);
        treeView = new TestFrameTreeView();
        treeView->setGroupTreeModel(treeModel);

        // Setup grouping options
        GroupingOptions options;
        options.byObject = true;
        options.bySession = true;
        options.byResolution = true;
        options.byFilter = true;
        model->setGroupingOptions(FrameType::Light, options);
    }

    void TearDown() override
    {
        delete treeView;
        delete treeModel;
        delete undoStack;
        delete model;
    }

    FrameInfo createFrame(const QString& path, const QString& obj, const QString& session,
                          const QString& filter = "Red")
    {
        FrameMetadata meta(FrameType::Light, QSize(3008, 3004), 30.0, -10.0, 100, 50, filter, obj,
                           QDateTime::currentDateTimeUtc(), "Normal");

        FrameInfo info(FrameId(path), path, meta);
        info.userObjectName = obj;
        info.userSession = session;
        return info;
    }

    IntegrationDataModel* model = nullptr;
    QUndoStack* undoStack = nullptr;
    GroupTreeModel* treeModel = nullptr;
    TestFrameTreeView* treeView = nullptr;
};

TEST_F(FrameTreeInternalMoveTest, MoveFrameToCompatibleGroupUpdatesMetadata)
{
    // 1. Setup two groups
    FrameInfo f1 = createFrame("C:/test/m83_1.fit", "M83", "Session1");
    FrameInfo f2 = createFrame("C:/test/m101_1.fit", "M101", "Session2");

    model->addFrames(FrameType::Light, {f1, f2});

    // Ensure they are grouped
    ASSERT_EQ(treeModel->rowCount(QModelIndex()), 2);

    // Find target group (M101)
    QModelIndex m101Idx;
    for (int i = 0; i < treeModel->rowCount(QModelIndex()); ++i)
    {
        QModelIndex idx = treeModel->index(i, 0, QModelIndex());
        if (treeModel->data(idx).toString().contains("M101"))
        {
            m101Idx = idx;
            break;
        }
    }
    ASSERT_TRUE(m101Idx.isValid());

    // 2. Prepare drag of f1 (M83) to M101 group
    const auto mimeData = std::make_unique<QMimeData>();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << f1.id.canonicalPath();
    mimeData->setData("application/x-afternight-frame-id", encodedData);

    // Get position of M101 group in the view
    treeView->show();
    treeView->expandAll();
    QApplication::processEvents();

    // Find visual position of the target group
    QRect rect = treeView->visualRect(m101Idx);
    ASSERT_FALSE(rect.isEmpty());
    QPoint pos = rect.center();

    QDropEvent event(pos, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    // 3. Perform the drop; the view asks before it changes the frames' metadata, and the move lands on Yes.
    treeView->dropEvent(&event);
    ASSERT_TRUE(answerMoveConfirmation(treeView));

    ASSERT_TRUE(undoStack);
    EXPECT_TRUE(undoStack->canUndo());

    // 5. Verify metadata was updated
    auto updatedF1 = model->frames(FrameType::Light).findById(f1.id);
    ASSERT_TRUE(updatedF1.has_value());
    EXPECT_EQ(updatedF1->userObjectName, "M101");
    EXPECT_EQ(updatedF1->userSession, "Session2");

    // 6. Undo restores original metadata
    undoStack->undo();
    updatedF1 = model->frames(FrameType::Light).findById(f1.id);
    ASSERT_TRUE(updatedF1.has_value());
    EXPECT_EQ(updatedF1->userObjectName, "M83");
    EXPECT_EQ(updatedF1->userSession, "Session1");
}

TEST_F(FrameTreeInternalMoveTest, MoveToIncompatibleGroupShowsWarningAndDoesNothing)
{
    // 1. Setup two groups with different hardware (Red vs Blue filter)
    FrameInfo f1 = createFrame("C:/test/m83_red.fit", "M83", "Session1", "Red");
    FrameInfo f2 = createFrame("C:/test/m83_blue.fit", "M83", "Session1", "Blue");

    model->addFrames(FrameType::Light, {f1, f2});

    ASSERT_EQ(treeModel->rowCount(QModelIndex()), 2);

    // Find target group (Blue)
    QModelIndex blueIdx;
    for (int i = 0; i < treeModel->rowCount(QModelIndex()); ++i)
    {
        QModelIndex idx = treeModel->index(i, 0, QModelIndex());
        if (treeModel->data(idx).toString().contains("Blue"))
        {
            blueIdx = idx;
            break;
        }
    }
    ASSERT_TRUE(blueIdx.isValid());

    // 2. Prepare drag of Red frame to Blue group
    const auto mimeData = std::make_unique<QMimeData>();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << f1.id.canonicalPath();
    mimeData->setData("application/x-afternight-frame-id", encodedData);

    treeView->show();
    treeView->expandAll();
    QApplication::processEvents();
    QRect rect = treeView->visualRect(blueIdx);
    ASSERT_FALSE(rect.isEmpty());
    QPoint pos = rect.center();

    QDropEvent event(pos, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    // 3. Verify no metadata change
    // We expect a fallback to framesMoved if compatibility check fails and it's not handled internally
    // BUT since we handle it and show a warning, it should NOT update metadata.

    // An incompatible frame is not moved inside the tree, so nothing is asked.
    treeView->dropEvent(&event);
    EXPECT_FALSE(answerMoveConfirmation(treeView));

    auto updatedF1 = model->frames(FrameType::Light).findById(f1.id);
    ASSERT_TRUE(updatedF1.has_value());
    EXPECT_EQ(updatedF1->metadata.filter(), "Red"); // Hardware filter remains same
    EXPECT_EQ(updatedF1->userObjectName, "M83"); // Metadata remains same
}

TEST_F(FrameTreeInternalMoveTest, MoveEntireGroupToAnotherCompatibleGroup)
{
    // 1. Setup groups
    FrameInfo f1 = createFrame("C:/test/src_1.fit", "SRC", "S1");
    FrameInfo f2 = createFrame("C:/test/src_2.fit", "SRC", "S1");
    FrameInfo target = createFrame("C:/test/dst_1.fit", "DST", "S2");

    model->addFrames(FrameType::Light, {f1, f2, target});

    ASSERT_EQ(treeModel->rowCount(QModelIndex()), 2);

    QModelIndex dstIdx;
    for (int i = 0; i < treeModel->rowCount(QModelIndex()); ++i)
    {
        QModelIndex idx = treeModel->index(i, 0, QModelIndex());
        if (treeModel->data(idx).toString().contains("DST"))
        {
            dstIdx = idx;
            break;
        }
    }
    ASSERT_TRUE(dstIdx.isValid());

    // 2. Prepare drag of BOTH frames from SRC group
    const auto mimeData = std::make_unique<QMimeData>();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << f1.id.canonicalPath();
    stream << f2.id.canonicalPath();
    mimeData->setData("application/x-afternight-frame-id", encodedData);

    treeView->show();
    treeView->expandAll();
    QApplication::processEvents();
    QModelIndex firstFrameIdx = treeModel->index(0, 0, dstIdx);
    QRect rect = treeView->visualRect(firstFrameIdx);
    ASSERT_FALSE(rect.isEmpty());
    QPoint pos = rect.center();

    QDropEvent event(pos, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    // 3. Perform the drop and answer Yes to the confirmation.
    treeView->dropEvent(&event);
    ASSERT_TRUE(answerMoveConfirmation(treeView));

    // 4. Verify both updated
    EXPECT_EQ(model->frames(FrameType::Light).findById(f1.id)->userObjectName, "DST");
    EXPECT_EQ(model->frames(FrameType::Light).findById(f2.id)->userObjectName, "DST");
}

TEST_F(FrameTreeInternalMoveTest, MoveAcrossGroupsWithSomeIncompatibleFrames)
{
    // 1. Setup groups: DST (M101, Red, 3008x3004)
    FrameInfo target = createFrame("C:/test/dst.fit", "M101", "S1", "Red");

    // Source frames:
    // f1: Compatible hardware (Red), will be updated to M101
    FrameInfo f1 = createFrame("C:/test/compatible.fit", "M83", "S2", "Red");
    // f2: Incompatible hardware (Blue), will be ignored by internal move logic
    FrameInfo f2 = createFrame("C:/test/incompatible.fit", "M83", "S2", "Blue");

    model->addFrames(FrameType::Light, {target, f1, f2});

    QModelIndex dstIdx;
    for (int i = 0; i < treeModel->rowCount(QModelIndex()); ++i)
    {
        QModelIndex idx = treeModel->index(i, 0, QModelIndex());
        if (treeModel->data(idx).toString().contains("M101"))
        {
            dstIdx = idx;
            break;
        }
    }

    // 2. Drag both f1 and f2 to DST
    const auto mimeData = std::make_unique<QMimeData>();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << f1.id.canonicalPath();
    stream << f2.id.canonicalPath();
    mimeData->setData("application/x-afternight-frame-id", encodedData);

    treeView->show();
    treeView->expandAll();
    QApplication::processEvents();
    QModelIndex firstFrameIdx = treeModel->index(0, 0, dstIdx);
    QRect rect = treeView->visualRect(firstFrameIdx);
    ASSERT_FALSE(rect.isEmpty());
    QPoint pos = rect.center();

    QDropEvent event(pos, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    treeView->dropEvent(&event);
    ASSERT_TRUE(answerMoveConfirmation(treeView));

    // 3. Verify only f1 was updated
    EXPECT_EQ(model->frames(FrameType::Light).findById(f1.id)->userObjectName, "M101");
    EXPECT_EQ(model->frames(FrameType::Light).findById(f2.id)->userObjectName, "M83"); // No change
}

TEST_F(FrameTreeInternalMoveTest, DragMoveEventProvidesFeedback)
{
    // Setup compatible (M83, Red) and incompatible (M83, Blue) groups
    FrameInfo targetComp = createFrame("C:/test/target_red.fit", "M101", "S1", "Red");
    FrameInfo targetIncomp = createFrame("C:/test/target_blue.fit", "M101", "S1", "Blue");
    FrameInfo source = createFrame("C:/test/source_red.fit", "M83", "S2", "Red");

    model->addFrames(FrameType::Light, {targetComp, targetIncomp, source});

    QModelIndex compIdx, incompIdx;
    for (int i = 0; i < treeModel->rowCount(QModelIndex()); ++i)
    {
        QModelIndex idx = treeModel->index(i, 0, QModelIndex());
        if (treeModel->data(idx).toString().contains("Red"))
            compIdx = idx;
        if (treeModel->data(idx).toString().contains("Blue"))
            incompIdx = idx;
    }

    // 1. Enter drag
    const auto mimeData = std::make_unique<QMimeData>();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << source.id.canonicalPath();
    mimeData->setData("application/x-afternight-frame-id", encodedData);

    QDragEnterEvent enterEvent({0, 0}, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
    treeView->dragEnterEvent(&enterEvent);

    // 2. Test Move over compatible group
    treeView->show();
    treeView->expandAll();
    QApplication::processEvents();

    QModelIndex compFrameIdx = treeModel->index(0, 0, compIdx);
    QPoint posComp = treeView->visualRect(compFrameIdx).center();
    QDragMoveEvent moveComp(posComp, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    treeView->dragMoveEvent(&moveComp);
    EXPECT_TRUE(moveComp.isAccepted());

    // 3. Test Move over incompatible group
    QModelIndex incompFrameIdx = treeModel->index(0, 0, incompIdx);
    QPoint posIncomp = treeView->visualRect(incompFrameIdx).center();
    QDragMoveEvent moveIncomp(posIncomp, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    treeView->dragMoveEvent(&moveIncomp);
    EXPECT_FALSE(moveIncomp.isAccepted());

    // 4. Test Move over different tree type (should be accepted for type change fallback)
    const auto extMime = std::make_unique<QMimeData>();
    QByteArray extData;
    QDataStream extStream(&extData, QIODevice::WriteOnly);
    extStream << QString("C:/test/dark.fit"); // Not in light collection
    extMime->setData("application/x-afternight-frame-id", extData);

    QDragEnterEvent enterExt({0, 0}, Qt::MoveAction, extMime.get(), Qt::LeftButton, Qt::NoModifier);
    treeView->dragEnterEvent(&enterExt);

    QDragMoveEvent moveExt(posIncomp, Qt::MoveAction, extMime.get(), Qt::LeftButton, Qt::NoModifier);
    treeView->dragMoveEvent(&moveExt);
    EXPECT_TRUE(moveExt.isAccepted()); // External moves are always allowed
}
