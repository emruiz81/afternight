// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QFile>
#include <QTreeView>
#include <QtTest/QSignalSpy>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

namespace
{

FrameMetadata makeMetadata(FrameType type = FrameType::Light)
{
    return FrameMetadata(type, QSize(100, 100), 1.0, -10.0, 100, 10, "L", "Obj", QDateTime(), "Cam");
}

} // namespace

class SelectionSyncFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(tempDir.isValid());
    }

    QString createTestFile(const QString& name)
    {
        QString path = tempDir.filePath(name);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
        {
            f.close();
        }
        return path;
    }

    QTemporaryDir tempDir;
};

TEST_F(SelectionSyncFixture, FindIndexForPath)
{
    QString path1 = createTestFile("test1.fit");
    QString path2 = createTestFile("test2.fit");

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    auto meta = makeMetadata();

    model.beginBatchUpdate();
    model.addFrames(FrameType::Light, {FrameInfo(FrameId(path1), path1, meta), FrameInfo(FrameId(path2), path2, meta)});
    model.endBatchUpdate();

    QCoreApplication::processEvents();

    FrameId id1(path1);
    FrameId id2(path2);

    QModelIndex idx1 = treeModel.findIndexForPath(id1.canonicalPath());
    ASSERT_TRUE(idx1.isValid()) << "Failed to find path: " << id1.canonicalPath().toStdString();
    EXPECT_EQ(treeModel.data(idx1, Qt::ToolTipRole).toString(), id1.canonicalPath());

    QModelIndex idx2 = treeModel.findIndexForPath(id2.canonicalPath());
    ASSERT_TRUE(idx2.isValid()) << "Failed to find path: " << id2.canonicalPath().toStdString();
    EXPECT_EQ(treeModel.data(idx2, Qt::ToolTipRole).toString(), id2.canonicalPath());

    QModelIndex idx3 = treeModel.findIndexForPath(tempDir.filePath("nonexistent.fit"));
    EXPECT_FALSE(idx3.isValid());
}

TEST_F(SelectionSyncFixture, TreeSelectionUpdatesModelSelection)
{
    QString path1 = createTestFile("light1.fit");
    QString path2 = createTestFile("light2.fit");

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    auto meta = makeMetadata();
    model.addFrames(FrameType::Light, {FrameInfo(FrameId(path1), path1, meta), FrameInfo(FrameId(path2), path2, meta)});

    QCoreApplication::processEvents();

    // Create a tree view and connect its selection model
    QTreeView treeView;
    treeView.setModel(&treeModel);
    treeView.setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* selModel = treeView.selectionModel();
    ASSERT_NE(selModel, nullptr);

    // Connect selection changes to model
    QObject::connect(selModel, &QItemSelectionModel::selectionChanged,
                     [&model, &treeModel](const QItemSelection& selected, const QItemSelection&) {
                         std::vector<FrameId> ids;
                         for (const auto& idx : selected.indexes())
                         {
                             if (idx.column() == 0)
                             {
                                 QString path = treeModel.data(idx, Qt::ToolTipRole).toString();
                                 if (!path.isEmpty())
                                 {
                                     ids.emplace_back(path);
                                 }
                             }
                         }
                         if (!ids.empty())
                         {
                             model.setSelectedFrameIds(ids);
                         }
                     });

    // Select first frame in tree
    FrameId id1(path1);
    QModelIndex idx1 = treeModel.findIndexForPath(id1.canonicalPath());
    ASSERT_TRUE(idx1.isValid());

    selModel->select(idx1, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    // Verify model was updated
    const auto& selectedIds = model.selectedFrameIds();
    ASSERT_EQ(selectedIds.size(), 1);
    EXPECT_EQ(selectedIds[0].canonicalPath(), id1.canonicalPath());
}

TEST_F(SelectionSyncFixture, ModelSelectionUpdatesTreeSelection)
{
    QString path1 = createTestFile("light1.fit");
    QString path2 = createTestFile("light2.fit");

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    auto meta = makeMetadata();
    model.addFrames(FrameType::Light, {FrameInfo(FrameId(path1), path1, meta), FrameInfo(FrameId(path2), path2, meta)});

    QCoreApplication::processEvents();

    QTreeView treeView;
    treeView.setModel(&treeModel);
    treeView.setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* selModel = treeView.selectionModel();

    // Connect model selection changes to tree
    QObject::connect(&model, &IntegrationDataModel::selectionChanged, [selModel, &treeModel, &model]() {
        const auto& selectedIds = model.selectedFrameIds();

        QItemSelection selection;
        for (const auto& id : selectedIds)
        {
            QModelIndex idx = treeModel.findIndexForPath(id.canonicalPath());
            if (idx.isValid())
            {
                selection.select(idx, idx);
            }
        }

        QSignalBlocker blocker(selModel);
        selModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    });

    // Select via model
    FrameId id2(path2);
    model.setSelectedFrameIds({id2});

    // Verify tree selection was updated
    auto selectedIndexes = selModel->selectedRows();
    ASSERT_EQ(selectedIndexes.size(), 1);
    EXPECT_EQ(treeModel.data(selectedIndexes[0], Qt::ToolTipRole).toString(), id2.canonicalPath());
}

TEST_F(SelectionSyncFixture, MultiSelectionSyncs)
{
    QString path1 = createTestFile("light1.fit");
    QString path2 = createTestFile("light2.fit");
    QString path3 = createTestFile("light3.fit");

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    auto meta = makeMetadata();
    model.addFrames(FrameType::Light, {FrameInfo(FrameId(path1), path1, meta), FrameInfo(FrameId(path2), path2, meta),
                                       FrameInfo(FrameId(path3), path3, meta)});

    QCoreApplication::processEvents();

    // Select multiple frames via model
    FrameId id1(path1);
    FrameId id3(path3);
    model.setSelectedFrameIds({id1, id3});

    const auto& selectedIds = model.selectedFrameIds();
    ASSERT_EQ(selectedIds.size(), 2);

    // Verify both can be found
    QModelIndex idx1 = treeModel.findIndexForPath(id1.canonicalPath());
    QModelIndex idx3 = treeModel.findIndexForPath(id3.canonicalPath());
    EXPECT_TRUE(idx1.isValid());
    EXPECT_TRUE(idx3.isValid());
}

TEST_F(SelectionSyncFixture, ClearingSelectionEmitsSignal)
{
    QString path1 = createTestFile("light1.fit");

    IntegrationDataModel model;
    auto meta = makeMetadata();
    model.addFrames(FrameType::Light, {FrameInfo(FrameId(path1), path1, meta)});

    // Set initial selection
    model.setSelectedFrameIds({FrameId(path1)});
    EXPECT_EQ(model.selectedFrameIds().size(), 1);

    QSignalSpy spy(&model, &IntegrationDataModel::selectionChanged);

    // Clear selection
    model.setSelectedFrameIds({});

    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(model.selectedFrameIds().empty());
}

TEST_F(SelectionSyncFixture, SelectionPreservedAcrossFrameTypeSwitch)
{
    QString lightPath = createTestFile("light.fit");
    QString darkPath = createTestFile("dark.fit");

    IntegrationDataModel model;
    GroupTreeModel lightModel(FrameType::Light, &model);
    GroupTreeModel darkModel(FrameType::Dark, &model);

    model.addFrames(FrameType::Light, {FrameInfo(FrameId(lightPath), lightPath, makeMetadata(FrameType::Light))});
    model.addFrames(FrameType::Dark, {FrameInfo(FrameId(darkPath), darkPath, makeMetadata(FrameType::Dark))});

    QCoreApplication::processEvents();

    // Select light frame
    FrameId lightId(lightPath);
    model.setSelectedFrameIds({lightId});

    // Verify light model can find it
    EXPECT_TRUE(lightModel.findIndexForPath(lightId.canonicalPath()).isValid());

    // Dark model should NOT find the light frame
    EXPECT_FALSE(darkModel.findIndexForPath(lightId.canonicalPath()).isValid());

    // Selection should still be in model
    EXPECT_EQ(model.selectedFrameIds().size(), 1);
}

TEST_F(SelectionSyncFixture, SignalBlockerPreventsRecursion)
{
    QString path1 = createTestFile("light1.fit");

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    auto meta = makeMetadata();
    model.addFrames(FrameType::Light, {FrameInfo(FrameId(path1), path1, meta)});

    QCoreApplication::processEvents();

    QTreeView treeView;
    treeView.setModel(&treeModel);

    auto* selModel = treeView.selectionModel();
    int modelToTreeCalls = 0;
    int treeToModelCalls = 0;

    // Bidirectional connection with signal blockers
    QObject::connect(&model, &IntegrationDataModel::selectionChanged, [&]() {
        modelToTreeCalls++;
        const auto& ids = model.selectedFrameIds();
        QItemSelection selection;
        for (const auto& id : ids)
        {
            QModelIndex idx = treeModel.findIndexForPath(id.canonicalPath());
            if (idx.isValid())
            {
                selection.select(idx, idx);
            }
        }
        QSignalBlocker blocker(selModel);
        selModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    });

    QObject::connect(selModel, &QItemSelectionModel::selectionChanged,
                     [&](const QItemSelection& selected, const QItemSelection&) {
                         treeToModelCalls++;
                         std::vector<FrameId> ids;
                         for (const auto& idx : selected.indexes())
                         {
                             if (idx.column() == 0)
                             {
                                 QString path = treeModel.data(idx, Qt::ToolTipRole).toString();
                                 if (!path.isEmpty())
                                 {
                                     ids.emplace_back(path);
                                 }
                             }
                         }
                         // Use signal blocker to prevent recursion
                         {
                             QSignalBlocker blocker(&model);
                             model.setSelectedFrameIds(ids);
                         }
                     });

    // Trigger selection from model
    model.setSelectedFrameIds({FrameId(path1)});

    // Should only have one call in each direction, not infinite recursion
    EXPECT_EQ(modelToTreeCalls, 1);
    // treeToModelCalls should be 0 because we used QSignalBlocker
}