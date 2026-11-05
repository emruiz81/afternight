// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QDropEvent>
#include <QMimeData>
#include <QtTest/QSignalSpy>

#include <memory>

#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/FrameTreeDragDropHandler.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

// Helper to expose protected dropEvent
class TestFrameTreeView : public FrameTreeView
{
public:
    using FrameTreeView::dropEvent;
};

class FrameTreeDragDropTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        treeModel = new GroupTreeModel(FrameType::Light, model);
        treeView = new TestFrameTreeView();
        treeView->setGroupTreeModel(treeModel);
        handler = new FrameTreeDragDropHandler();
    }

    void TearDown() override
    {
        delete handler;
        delete treeView;
        delete treeModel;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    GroupTreeModel* treeModel = nullptr;
    TestFrameTreeView* treeView = nullptr;
    FrameTreeDragDropHandler* handler = nullptr;
};

TEST_F(FrameTreeDragDropTest, FrameTreeViewEmitsFramesMovedOnInternalDrop)
{
    // Create custom mime data
    const auto mimeData = std::make_unique<QMimeData>();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << QString("C:/test/file1.fit");
    mimeData->setData("application/x-afternight-frame-id", encodedData);

    QDropEvent event({0, 0}, Qt::CopyAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    QSignalSpy spy(treeView, &FrameTreeView::framesMoved);
    treeView->dropEvent(&event);

    ASSERT_EQ(spy.count(), 1);
    QStringList paths = spy.takeFirst().at(0).toStringList();
    ASSERT_EQ(paths.size(), 1);
    EXPECT_EQ(paths[0], "C:/test/file1.fit");
}

TEST_F(FrameTreeDragDropTest, HandlerEmitsTypeChangeRequest)
{
    // Register target
    handler->registerDropTarget(treeView, FrameType::Dark);

    QSignalSpy spy(handler, &FrameTreeDragDropHandler::frameTypeChangeRequested);

    // Simulate signal from tree view
    emit treeView->framesMoved({"C:/test/file1.fit"});

    ASSERT_EQ(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toStringList()[0], "C:/test/file1.fit");
    EXPECT_EQ(args.at(1).value<FrameType>(), FrameType::Dark);
}

TEST_F(FrameTreeDragDropTest, HandlerIgnoresEmptyPathList)
{
    handler->registerDropTarget(treeView, FrameType::Dark);

    QSignalSpy spy(handler, &FrameTreeDragDropHandler::frameTypeChangeRequested);

    // Emit empty list
    emit treeView->framesMoved({});

    // Should not emit signal for empty paths
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(FrameTreeDragDropTest, HandlerHandlesMultiplePaths)
{
    handler->registerDropTarget(treeView, FrameType::Flat);

    QSignalSpy spy(handler, &FrameTreeDragDropHandler::frameTypeChangeRequested);

    QStringList paths = {"C:/test/file1.fit", "C:/test/file2.fit", "C:/test/file3.fit"};
    emit treeView->framesMoved(paths);

    ASSERT_EQ(spy.count(), 1);
    QStringList emittedPaths = spy.takeFirst().at(0).toStringList();
    ASSERT_EQ(emittedPaths.size(), 3);
    EXPECT_EQ(emittedPaths[0], "C:/test/file1.fit");
    EXPECT_EQ(emittedPaths[1], "C:/test/file2.fit");
    EXPECT_EQ(emittedPaths[2], "C:/test/file3.fit");
}

TEST_F(FrameTreeDragDropTest, MultipleTargetRegistration)
{
    // Create additional tree views
    auto* lightTree = new TestFrameTreeView();
    auto* darkTree = new TestFrameTreeView();

    handler->registerDropTarget(lightTree, FrameType::Light);
    handler->registerDropTarget(darkTree, FrameType::Dark);

    QSignalSpy spy(handler, &FrameTreeDragDropHandler::frameTypeChangeRequested);

    // Drop on dark tree
    emit darkTree->framesMoved({"C:/file.fit"});
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(1).value<FrameType>(), FrameType::Dark);

    // Drop on light tree
    emit lightTree->framesMoved({"C:/file2.fit"});
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(1).value<FrameType>(), FrameType::Light);

    delete lightTree;
    delete darkTree;
}

TEST_F(FrameTreeDragDropTest, DropEventWithMultipleFiles)
{
    const auto mimeData = std::make_unique<QMimeData>();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << QString("C:/test/file1.fit");
    stream << QString("C:/test/file2.fit");
    stream << QString("C:/test/file3.fit");
    mimeData->setData("application/x-afternight-frame-id", encodedData);

    QDropEvent event({0, 0}, Qt::CopyAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);

    QSignalSpy spy(treeView, &FrameTreeView::framesMoved);
    treeView->dropEvent(&event);

    ASSERT_EQ(spy.count(), 1);
    QStringList paths = spy.takeFirst().at(0).toStringList();
    ASSERT_EQ(paths.size(), 3);
}
