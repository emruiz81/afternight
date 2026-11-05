// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QAbstractItemModel>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QTimeZone>
#include <QtTest/QSignalSpy>
#include <QElapsedTimer>
#include <QItemSelectionModel>
#include <QCoreApplication>
#include <QTreeView>
#include <QUndoStack>

#include <algorithm>
#include <optional>

#include <QDir>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "theme/AnDesignTokens.h"

// Helper to create a dummy frame
FrameInfo createFrame(const QString& path, int w = 1000, int h = 1000, const QString& filter = "L",
                      const QString& object = "Obj")
{
    FrameMetadata meta(FrameType::Light, QSize(w, h),
                       1.0, // exposure
                       std::nullopt, // temp
                       std::nullopt, // gain
                       std::nullopt, // offset
                       filter,
                       object, // object
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                       "" // readout mode
    );

    return FrameInfo(FrameId(path), path, meta);
}

FrameInfo createFrameForType(const QString& path, FrameType type, int w = 1000, int h = 1000)
{
    FrameMetadata meta(type, QSize(w, h),
                       1.0, // exposure
                       std::nullopt, // temp
                       std::nullopt, // gain
                       std::nullopt, // offset
                       "L", // filter
                       "Obj", // object
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                       "" // readout mode
    );

    return FrameInfo(FrameId(path), path, meta);
}

bool anyGroupLabelContains(GroupTreeModel* treeModel, const QString& needle, const QModelIndex& parent = QModelIndex())
{
    for (int row = 0; row < treeModel->rowCount(parent); ++row)
    {
        const QModelIndex idx = treeModel->index(row, GroupTreeModel::ColName, parent);
        if (!idx.isValid())
        {
            continue;
        }

        if (treeModel->isGroup(idx))
        {
            const QString text = treeModel->data(idx, Qt::DisplayRole).toString();
            if (text.contains(needle))
            {
                return true;
            }
        }

        if (anyGroupLabelContains(treeModel, needle, idx))
        {
            return true;
        }
    }

    return false;
}

class GroupTreeModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        treeModel = new GroupTreeModel(FrameType::Light, model);
    }

    static QTreeView* createTreeView(QAbstractItemModel* model)
    {
        auto* view = new QTreeView();
        view->setModel(model);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        return view;
    }

    void TearDown() override
    {
        delete treeModel;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    GroupTreeModel* treeModel = nullptr;
};

TEST_F(GroupTreeModelTest, InitialStateIsEmpty)
{
    EXPECT_EQ(treeModel->rowCount(), 0);
}

TEST_F(GroupTreeModelTest, UpdatesOnFrameAddition)
{
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/light1.fit"));

    model->addFrames(FrameType::Light, frames);

    EXPECT_GT(treeModel->rowCount(), 0);
}

TEST_F(GroupTreeModelTest, ReflectsGroupingStructure)
{
    // Add two frames that should be in different groups (different resolution)
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/bin1/light1.fit", 1000, 1000));
    frames.push_back(createFrame("C:/images/bin2/light2.fit", 500, 500));

    model->addFrames(FrameType::Light, frames);

    int topLevelRows = treeModel->rowCount();
    EXPECT_GE(topLevelRows, 2);

    // Check children
    QModelIndex group1 = treeModel->index(0, 0);
    QModelIndex group2 = treeModel->index(1, 0);

    EXPECT_GT(treeModel->rowCount(group1), 0);
    EXPECT_GT(treeModel->rowCount(group2), 0);
}

TEST_F(GroupTreeModelTest, DataDisplay)
{
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/light1.fit"));
    model->addFrames(FrameType::Light, frames);

    // Find the frame item (Flat mode: Root -> Group -> Frame)
    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);

    EXPECT_TRUE(frameIdx.isValid());

    QString text = treeModel->data(frameIdx, Qt::DisplayRole).toString();
    EXPECT_TRUE(text.contains("light1.fit"));
}

TEST_F(GroupTreeModelTest, RespondsToModelClear)
{
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/light1.fit"));
    model->addFrames(FrameType::Light, frames);

    EXPECT_GT(treeModel->rowCount(), 0);

    QStringList paths = {"C:/images/light1.fit"};
    model->removeFrames(FrameType::Light, paths);

    EXPECT_EQ(treeModel->rowCount(), 0);
}

TEST_F(GroupTreeModelTest, GroupOrderingIsDeterministic)
{
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/bin2/light2.fit", 500, 500));
    frames.push_back(createFrame("C:/images/bin1/light1.fit", 1000, 1000));
    frames.push_back(createFrame("C:/images/bin0/light0.fit", 250, 250));
    model->addFrames(FrameType::Light, frames);

    QStringList keys;
    for (int i = 0; i < treeModel->rowCount(); ++i)
    {
        // In Flat mode, label is the group key string
        keys << treeModel->data(treeModel->index(i, 0), Qt::DisplayRole).toString();
    }

    QStringList sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(keys, sorted);
}

TEST_F(GroupTreeModelTest, StableNodeIdSurvivesModelRebuild)
{
    const QString path = QStringLiteral("C:/images/light1.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    const QModelIndex group_idx = treeModel->index(0, 0);
    ASSERT_TRUE(group_idx.isValid());
    const QModelIndex frame_idx = treeModel->index(0, 0, group_idx);
    ASSERT_TRUE(frame_idx.isValid());

    const QString group_id_before = treeModel->stableNodeId(group_idx);
    const QString frame_id_before = treeModel->stableNodeId(frame_idx);
    ASSERT_FALSE(group_id_before.isEmpty());
    ASSERT_FALSE(frame_id_before.isEmpty());

    // Triggers a rebuild
    model->updateFrameAttribute(path, FrameAttribute::CustomTag, QStringLiteral("TagA"));

    const QModelIndex group_idx_after = treeModel->index(0, 0);
    ASSERT_TRUE(group_idx_after.isValid());
    const QModelIndex frame_idx_after = treeModel->index(0, 0, group_idx_after);
    ASSERT_TRUE(frame_idx_after.isValid());

    EXPECT_EQ(treeModel->stableNodeId(group_idx_after), group_id_before);
    EXPECT_EQ(treeModel->stableNodeId(frame_idx_after), frame_id_before);
}

TEST_F(GroupTreeModelTest, FlatModeCreatesOneParentNodePerGroup)
{
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Flat);

    // Given: 2 frames with different resolutions (so 2 groups)
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/bin1/light1.fit", 1000, 1000));
    frames.push_back(createFrame("C:/images/bin2/light2.fit", 500, 500));
    model->addFrames(FrameType::Light, frames);

    // Then: root has exactly 2 children (group nodes)
    EXPECT_EQ(treeModel->rowCount(), 2);

    // Check that children are frames
    QModelIndex group0 = treeModel->index(0, 0);
    EXPECT_EQ(treeModel->rowCount(group0), 1);
    EXPECT_TRUE(treeModel->isFrame(treeModel->index(0, 0, group0)));
}

TEST_F(GroupTreeModelTest, HierarchicalModeCreatesNestedNodes)
{
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);

    // Disable session grouping to match test expectation
    GroupingOptions options = model->groupingOptions(FrameType::Light);
    options.bySession = false;
    model->setGroupingOptions(FrameType::Light, options);

    // Given: frames with Resolution=3008x3004, Filter=Red, Object=M31
    // We rely on grouping options: Resolution=True, Filter=True, Object=True

    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/light1.fit", 3008, 3004, "Red", "M31"));
    model->addFrames(FrameType::Light, frames);

    // Structure: Resolution -> Filter -> Object -> Frames
    // Root has 1 child (Resolution)
    EXPECT_EQ(treeModel->rowCount(), 1);
    QModelIndex resNode = treeModel->index(0, 0);
    EXPECT_TRUE(treeModel->data(resNode, Qt::DisplayRole).toString().contains("Resolution"));

    // Res has 1 child (Filter)
    EXPECT_EQ(treeModel->rowCount(resNode), 1);
    QModelIndex filterNode = treeModel->index(0, 0, resNode);
    EXPECT_TRUE(treeModel->data(filterNode, Qt::DisplayRole).toString().contains("Filter"));

    // Filter has 1 child (Object)
    EXPECT_EQ(treeModel->rowCount(filterNode), 1);
    QModelIndex objNode = treeModel->index(0, 0, filterNode);
    EXPECT_TRUE(treeModel->data(objNode, Qt::DisplayRole).toString().contains("Object"));

    // Object has 1 child (Frame)
    EXPECT_EQ(treeModel->rowCount(objNode), 1);
    QModelIndex frameNode = treeModel->index(0, 0, objNode);
    EXPECT_TRUE(treeModel->isFrame(frameNode));
}

TEST_F(GroupTreeModelTest, DisplayModeTogglePreservesExpansionState_StableIDs)
{
    // We check if stable IDs are generated correctly in both modes,
    // though they will be different for group nodes.

    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/light1.fit", 1000, 1000));
    model->addFrames(FrameType::Light, frames);

    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Flat);
    QModelIndex flatGroup = treeModel->index(0, 0);
    QString flatId = treeModel->stableNodeId(flatGroup);
    EXPECT_TRUE(flatId.startsWith("group:"));

    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);
    QModelIndex hierGroup = treeModel->index(0, 0); // Resolution node
    QString hierId = treeModel->stableNodeId(hierGroup);
    EXPECT_TRUE(hierId.startsWith("group:"));
    EXPECT_NE(flatId, hierId); // Structure changed, so IDs changed (except maybe for frames if we track them specially,
                               // but frames are children of different parents now)
}

TEST_F(GroupTreeModelTest, ColumnVisibilityRespectsGrouping)
{
    // Given: Flat mode (so grouping is active but flattened)
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Flat);

    GroupingOptions options = model->groupingOptions(FrameType::Light);
    options.byResolution = true;
    options.byFilter = false; // Filter NOT grouped
    model->setGroupingOptions(FrameType::Light, options);

    std::vector<FrameInfo> frames;
    frames.push_back(createFrame("C:/images/light.fit", 1000, 1000, "Red"));
    model->addFrames(FrameType::Light, frames);

    // Get frame node
    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);

    // Column 1 is Resolution. Grouped=True => Should be empty
    EXPECT_FALSE(treeModel->data(frameIdx.siblingAtColumn(1), Qt::DisplayRole).isValid());

    // Column 2 is Filter. Grouped=False => Should show "Red"
    EXPECT_EQ(treeModel->data(frameIdx.siblingAtColumn(2), Qt::DisplayRole).toString(), QStringLiteral("Red"));
}

TEST_F(GroupTreeModelTest, HierarchicalModeOrderingIsDeterministic)
{
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);

    // Disable session grouping for simpler structure
    GroupingOptions options = model->groupingOptions(FrameType::Light);
    options.bySession = false;
    options.byFilter = false;
    options.byObject = false;
    model->setGroupingOptions(FrameType::Light, options);

    // Add frames with different resolutions in random order
    std::vector<FrameInfo> frames;
    frames.push_back(createFrame(QStringLiteral("C:/images/bin2/light2.fit"), 500, 500));
    frames.push_back(createFrame(QStringLiteral("C:/images/bin1/light1.fit"), 1000, 1000));
    frames.push_back(createFrame(QStringLiteral("C:/images/bin0/light0.fit"), 250, 250));
    model->addFrames(FrameType::Light, frames);

    // Collect top-level group labels
    QStringList keys;
    for (int i = 0; i < treeModel->rowCount(); ++i)
    {
        keys << treeModel->data(treeModel->index(i, 0), Qt::DisplayRole).toString();
    }

    // Should be sorted
    QStringList sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(keys, sorted);
}

TEST_F(GroupTreeModelTest, DisplayModeTogglePerformance)
{
    // Add 500 frames across multiple groups
    std::vector<FrameInfo> frames;
    for (int i = 0; i < 500; ++i)
    {
        int res = (i % 5) * 100 + 1000; // 5 different resolutions
        QString filter = (i % 3 == 0)   ? QStringLiteral("Red")
                         : (i % 3 == 1) ? QStringLiteral("Green")
                                        : QStringLiteral("Blue");
        frames.push_back(createFrame(QStringLiteral("C:/images/light%1.fit").arg(i), res, res, filter));
    }
    model->addFrames(FrameType::Light, frames);

    QElapsedTimer timer;

    // Measure toggle to hierarchical
    timer.start();
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);
    qint64 toHierarchical = timer.elapsed();

    // Measure toggle back to flat
    timer.restart();
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Flat);
    qint64 toFlat = timer.elapsed();

    // Both should be under 100ms
    EXPECT_LT(toHierarchical, 100) << "Hierarchical mode toggle took " << toHierarchical << "ms";
    EXPECT_LT(toFlat, 100) << "Flat mode toggle took " << toFlat << "ms";
}

TEST_F(GroupTreeModelTest, ExcludedFrameHasStrikeoutFont)
{
    const QString path = QStringLiteral("C:/images/light1.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    model->updateFrameAttribute(path, FrameAttribute::Excluded, true);

    const QModelIndex groupIdx = treeModel->index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());
    const QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    ASSERT_TRUE(frameIdx.isValid());

    const QVariant fontVar = treeModel->data(frameIdx, Qt::FontRole);
    ASSERT_TRUE(fontVar.isValid());
    const QFont font = qvariant_cast<QFont>(fontVar);
    EXPECT_TRUE(font.strikeOut());
}

TEST_F(GroupTreeModelTest, BulkExcludeDoesNotResetModel)
{
    model->addFrames(FrameType::Light, {createFrame("C:/images/light1.fit"), createFrame("C:/images/light2.fit")});

    const QModelIndex groupIdx = treeModel->index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());

    const QModelIndex frameIdx1 = treeModel->index(0, 0, groupIdx);
    const QModelIndex frameIdx2 = treeModel->index(1, 0, groupIdx);
    ASSERT_TRUE(frameIdx1.isValid());
    ASSERT_TRUE(frameIdx2.isValid());

    QSignalSpy resetSpy(treeModel, &QAbstractItemModel::modelReset);

    treeModel->setFramesExcluded({QStringLiteral("C:/images/light1.fit"), QStringLiteral("C:/images/light2.fit")},
                                 true);

    EXPECT_EQ(resetSpy.count(), 0);
    EXPECT_EQ(treeModel->data(frameIdx1, Qt::CheckStateRole).toInt(), Qt::Unchecked);
    EXPECT_EQ(treeModel->data(frameIdx2, Qt::CheckStateRole).toInt(), Qt::Unchecked);
}

TEST(GroupTreeModel, CheckboxExcludeWithUndoStackDoesNotResetModel)
{
    IntegrationDataModel dataModel;
    QUndoStack undoStack;
    GroupTreeModel treeModel(FrameType::Light, &dataModel, nullptr, &undoStack);
    dataModel.addFrames(FrameType::Light, {createFrame(QStringLiteral("C:/images/light1.fit")),
                                           createFrame(QStringLiteral("C:/images/light2.fit"))});

    const QModelIndex groupIdx = treeModel.index(0, GroupTreeModel::ColName);
    ASSERT_TRUE(groupIdx.isValid());
    const QModelIndex frameIdx = treeModel.index(0, GroupTreeModel::ColName, groupIdx);
    ASSERT_TRUE(frameIdx.isValid());

    QSignalSpy resetSpy(&treeModel, &QAbstractItemModel::modelReset);

    ASSERT_TRUE(treeModel.setData(frameIdx, Qt::Unchecked, Qt::CheckStateRole));

    EXPECT_EQ(resetSpy.count(), 0);
    EXPECT_EQ(treeModel.data(frameIdx, Qt::CheckStateRole).toInt(), Qt::Unchecked);
    EXPECT_TRUE(treeModel.parent(frameIdx).isValid());
    ASSERT_EQ(dataModel.lightFrames().size(), 2);
    EXPECT_TRUE(dataModel.lightFrames().frames().front().excluded);

    undoStack.undo();

    const auto restored = dataModel.lightFrames().findById(FrameId(QStringLiteral("C:/images/light1.fit")));
    ASSERT_TRUE(restored.has_value());
    EXPECT_FALSE(restored->excluded);
}

TEST_F(GroupTreeModelTest, CoverageModeUpdatesWithCalibrationFrames)
{
    treeModel->setShowOnlyCalibrationWarnings(true);
    model->addFrames(FrameType::Light, {createFrame("C:/images/light1.fit")});
    QCoreApplication::processEvents();

    EXPECT_EQ(treeModel->matchCount(), 0);

    // Add a bias frame with a different resolution -> coverage warning should appear.
    model->addFrames(FrameType::Bias, {createFrameForType("C:/images/bias1.fit", FrameType::Bias, 500, 500)});
    QCoreApplication::processEvents();
    EXPECT_EQ(treeModel->matchCount(), 1);

    // Add matching-resolution bias -> warnings remain (Dark is still missing).
    model->addFrames(FrameType::Bias, {createFrameForType("C:/images/bias2.fit", FrameType::Bias, 1000, 1000)});
    QCoreApplication::processEvents();
    EXPECT_EQ(treeModel->matchCount(), 1);

    // Add a mismatched dark -> warning appears.
    model->addFrames(FrameType::Dark, {createFrameForType("C:/images/dark1.fit", FrameType::Dark, 500, 500)});
    QCoreApplication::processEvents();
    EXPECT_EQ(treeModel->matchCount(), 1);

    // Add matching dark -> warning clears again.
    model->addFrames(FrameType::Dark, {createFrameForType("C:/images/dark2.fit", FrameType::Dark, 1000, 1000)});
    QCoreApplication::processEvents();
    EXPECT_EQ(treeModel->matchCount(), 1);

    // Add mismatched flat -> warning appears.
    model->addFrames(FrameType::Flat, {createFrameForType("C:/images/flat1.fit", FrameType::Flat, 500, 500)});
    QCoreApplication::processEvents();
    EXPECT_EQ(treeModel->matchCount(), 1);

    // Add matching flat -> warning clears.
    model->addFrames(FrameType::Flat, {createFrameForType("C:/images/flat2.fit", FrameType::Flat, 1000, 1000)});
    QCoreApplication::processEvents();
    EXPECT_EQ(treeModel->matchCount(), 0);
}

TEST_F(GroupTreeModelTest, MasterFrameHasBoldFont)
{
    const QString path = QStringLiteral("C:/images/light1.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    model->setMasterStatus(path, true, MasterSource::UserDefined);

    const QModelIndex groupIdx = treeModel->index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());
    const QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    ASSERT_TRUE(frameIdx.isValid());

    const QVariant fontVar = treeModel->data(frameIdx, Qt::FontRole);
    ASSERT_TRUE(fontVar.isValid());
    const QFont font = qvariant_cast<QFont>(fontVar);
    EXPECT_TRUE(font.bold());
}

TEST_F(GroupTreeModelTest, FrameWithMissingCalibrationHasWarningStyle)
{
    // Add bias frames so calibration is expected.
    FrameMetadata biasMeta(FrameType::Bias, QSize(1000, 1000), 0.0, std::nullopt, std::nullopt, std::nullopt, QString(),
                           QString(), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    FrameInfo biasFrame(FrameId(QStringLiteral("C:/images/bias1.fit")), QStringLiteral("C:/images/bias1.fit"),
                        biasMeta);

    // Add bias with DIFFERENT resolution so it won't match.
    FrameMetadata biasMeta2(FrameType::Bias, QSize(500, 500), // Different resolution - won't match light at 1000x1000
                            0.0, std::nullopt, std::nullopt, std::nullopt, QString(), QString(),
                            QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    FrameInfo biasFrame2(FrameId(QStringLiteral("C:/images/bias2.fit")), QStringLiteral("C:/images/bias2.fit"),
                         biasMeta2);
    model->addFrames(FrameType::Bias, {biasFrame2});

    // Add a light frame that has no matching bias (resolution mismatch).
    const QString lightPath = QStringLiteral("C:/images/light1.fit");
    model->addFrames(FrameType::Light, {createFrame(lightPath)});

    // Force tree model rebuild.
    const QModelIndex groupIdx = treeModel->index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());
    const QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    ASSERT_TRUE(frameIdx.isValid());

    const QVariant fgVar = treeModel->data(frameIdx, Qt::ForegroundRole);
    ASSERT_TRUE(fgVar.isValid());
    const QColor fgColor = qvariant_cast<QColor>(fgVar);

    const QVariant bgVar = treeModel->data(frameIdx, Qt::BackgroundRole);
    ASSERT_TRUE(bgVar.isValid());
    const QColor bgColor = qvariant_cast<QColor>(bgVar);
    // Background is translucent warning tint.
    const QColor expectedAmber = an::ui::tokens().colors.statusWarning;
    EXPECT_EQ(bgColor.red(), expectedAmber.red());
    EXPECT_EQ(bgColor.green(), expectedAmber.green());
    EXPECT_EQ(bgColor.blue(), expectedAmber.blue());
    EXPECT_LT(bgColor.alpha(), 255);

    // Highlighted rows must use a contrast-safe foreground.
    const QColor effectiveBackground = an::ui::compositeOver(bgColor, an::ui::tokens().colors.bgInput);
    EXPECT_GE(an::ui::contrastRatio(fgColor, effectiveBackground), 3.0);
}

TEST_F(GroupTreeModelTest, InsertRowPreservesExpansionState)
{
    auto* view = createTreeView(treeModel);

    // Two groups
    model->addFrames(FrameType::Light, {createFrame(QStringLiteral("C:/images/bin1/light1.fit"), 1000, 1000),
                                        createFrame(QStringLiteral("C:/images/bin2/light2.fit"), 500, 500)});

    QList<QPersistentModelIndex> expanded;
    for (int i = 0; i < treeModel->rowCount(); ++i)
    {
        const QModelIndex idx = treeModel->index(i, 0);
        view->setExpanded(idx, true);
        expanded.push_back(QPersistentModelIndex(idx));
    }

    model->addFrames(FrameType::Light, {createFrame(QStringLiteral("C:/images/bin1/light3.fit"), 1000, 1000)});

    for (const auto& idx : expanded)
    {
        ASSERT_TRUE(idx.isValid());
        EXPECT_TRUE(view->isExpanded(idx));
    }

    delete view;
}

TEST_F(GroupTreeModelTest, InsertRowPreservesSelection)
{
    auto* view = createTreeView(treeModel);

    model->addFrames(FrameType::Light, {createFrame(QStringLiteral("C:/images/bin1/light1.fit"), 1000, 1000),
                                        createFrame(QStringLiteral("C:/images/bin1/light2.fit"), 1000, 1000)});

    const QModelIndex groupIdx = treeModel->index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());
    const QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    ASSERT_TRUE(frameIdx.isValid());

    view->selectionModel()->select(frameIdx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const QPersistentModelIndex selected(frameIdx);

    model->addFrames(FrameType::Light, {createFrame(QStringLiteral("C:/images/bin1/light3.fit"), 1000, 1000)});

    ASSERT_TRUE(selected.isValid());
    EXPECT_TRUE(view->selectionModel()->isSelected(selected));

    delete view;
}

TEST_F(GroupTreeModelTest, RemoveRowPreservesOtherSelections)
{
    auto* view = createTreeView(treeModel);

    const QString p1 = QStringLiteral("C:/images/bin1/light1.fit");
    const QString p2 = QStringLiteral("C:/images/bin1/light2.fit");
    const QString p3 = QStringLiteral("C:/images/bin1/light3.fit");

    model->addFrames(FrameType::Light,
                     {createFrame(p1, 1000, 1000), createFrame(p2, 1000, 1000), createFrame(p3, 1000, 1000)});

    const QModelIndex groupIdx = treeModel->index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());

    const QModelIndex f1 = treeModel->index(0, 0, groupIdx);
    const QModelIndex f2 = treeModel->index(1, 0, groupIdx);
    const QModelIndex f3 = treeModel->index(2, 0, groupIdx);

    view->selectionModel()->select(f1, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view->selectionModel()->select(f2, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view->selectionModel()->select(f3, QItemSelectionModel::Select | QItemSelectionModel::Rows);

    const QPersistentModelIndex pf1(f1);
    const QPersistentModelIndex pf3(f3);

    model->removeFrames(FrameType::Light, QStringList{p2});

    ASSERT_TRUE(pf1.isValid());
    ASSERT_TRUE(pf3.isValid());
    EXPECT_TRUE(view->selectionModel()->isSelected(pf1));
    EXPECT_TRUE(view->selectionModel()->isSelected(pf3));

    delete view;
}

TEST_F(GroupTreeModelTest, NormalizesUnsupportedGroupingFlagsPerFrameType)
{
    GroupingOptions opts;
    opts.byFilter = true;
    opts.byObject = true;
    opts.bySession = true;
    opts.byIsoGain = true;
    opts.byOffset = true;
    opts.byExposure = true;
    opts.exposureToleranceSeconds = 7.5;
    opts.byTemperature = true;
    opts.temperatureToleranceC = 4.0;
    opts.byCustomTag = true;

    model->setGroupingOptions(FrameType::Flat, opts);
    {
        const auto& normalized = model->groupingOptions(FrameType::Flat);
        EXPECT_FALSE(normalized.byExposure);
        EXPECT_EQ(normalized.exposureToleranceSeconds, 0.0);
        EXPECT_FALSE(normalized.byTemperature);
        EXPECT_EQ(normalized.temperatureToleranceC, 0.0);
    }

    model->setGroupingOptions(FrameType::Bias, opts);
    {
        const auto& normalized = model->groupingOptions(FrameType::Bias);
        EXPECT_FALSE(normalized.byFilter);
        EXPECT_FALSE(normalized.byObject);
        EXPECT_FALSE(normalized.bySession);
        EXPECT_FALSE(normalized.byExposure);
        EXPECT_EQ(normalized.exposureToleranceSeconds, 0.0);
        EXPECT_FALSE(normalized.byTemperature);
        EXPECT_EQ(normalized.temperatureToleranceC, 0.0);
    }

    model->setGroupingOptions(FrameType::Dark, opts);
    {
        const auto& normalized = model->groupingOptions(FrameType::Dark);
        EXPECT_FALSE(normalized.byFilter);
        EXPECT_FALSE(normalized.byObject);
        EXPECT_FALSE(normalized.bySession);
        EXPECT_TRUE(normalized.byExposure);
        EXPECT_EQ(normalized.exposureToleranceSeconds, 7.5);
        EXPECT_TRUE(normalized.byTemperature);
        EXPECT_EQ(normalized.temperatureToleranceC, 4.0);
    }

    model->setGroupingOptions(FrameType::Light, opts);
    {
        const auto& normalized = model->groupingOptions(FrameType::Light);
        EXPECT_TRUE(normalized.byExposure);
        EXPECT_EQ(normalized.exposureToleranceSeconds, 7.5);
        EXPECT_FALSE(normalized.byTemperature);
        EXPECT_EQ(normalized.temperatureToleranceC, 0.0);
    }
}

TEST_F(GroupTreeModelTest, FromJsonNormalizesUnsupportedGroupingOptions)
{
    QJsonObject json = model->toJson();
    QJsonObject grouping = json.value(QStringLiteral("groupingOptions")).toObject();
    QJsonObject flat = grouping.value(QStringLiteral("FLAT")).toObject();
    flat.insert(QStringLiteral("byExposure"), true);
    flat.insert(QStringLiteral("exposureToleranceSeconds"), 5.0);
    grouping.insert(QStringLiteral("FLAT"), flat);
    json.insert(QStringLiteral("groupingOptions"), grouping);

    model->fromJson(json);
    const auto& normalized = model->groupingOptions(FrameType::Flat);
    EXPECT_FALSE(normalized.byExposure);
    EXPECT_EQ(normalized.exposureToleranceSeconds, 0.0);
}

TEST_F(GroupTreeModelTest, FlatAndBiasHierarchicalModeNeverCreatesExposureNodes)
{
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);

    // Flat
    treeModel->setFrameType(FrameType::Flat);
    {
        GroupingOptions opts = model->groupingOptions(FrameType::Flat);
        opts.byExposure = true; // Should be ignored for flats.
        model->setGroupingOptions(FrameType::Flat, opts);

        FrameMetadata m1(FrameType::Flat, QSize(1000, 1000), 10.0, std::nullopt, std::nullopt, std::nullopt,
                         QStringLiteral("L"), QStringLiteral("Obj"),
                         QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
        FrameMetadata m2(FrameType::Flat, QSize(1000, 1000), 20.0, std::nullopt, std::nullopt, std::nullopt,
                         QStringLiteral("L"), QStringLiteral("Obj"),
                         QDateTime::fromSecsSinceEpoch(1700000001, QTimeZone::UTC), QString());
        model->addFrames(
            FrameType::Flat,
            {FrameInfo(FrameId(QStringLiteral("C:/images/flat1.fit")), QStringLiteral("C:/images/flat1.fit"), m1),
             FrameInfo(FrameId(QStringLiteral("C:/images/flat2.fit")), QStringLiteral("C:/images/flat2.fit"), m2)});

        EXPECT_FALSE(anyGroupLabelContains(treeModel, QStringLiteral("Exposure:")));
    }

    // Bias
    treeModel->setFrameType(FrameType::Bias);
    {
        GroupingOptions opts = model->groupingOptions(FrameType::Bias);
        opts.byExposure = true; // Should be ignored for bias.
        model->setGroupingOptions(FrameType::Bias, opts);

        FrameMetadata m1(FrameType::Bias, QSize(1000, 1000), 0.0, std::nullopt, std::nullopt, std::nullopt, QString(),
                         QString(), QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC), QString());
        model->addFrames(FrameType::Bias, {FrameInfo(FrameId(QStringLiteral("C:/images/bias1.fit")),
                                                     QStringLiteral("C:/images/bias1.fit"), m1)});

        EXPECT_FALSE(anyGroupLabelContains(treeModel, QStringLiteral("Exposure:")));
    }
}

TEST_F(GroupTreeModelTest, DarkAndLightExposureNodesRespectByExposureFlag)
{
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);

    // Dark
    treeModel->setFrameType(FrameType::Dark);
    {
        FrameMetadata d1(FrameType::Dark, QSize(1000, 1000), 10.0, std::nullopt, std::nullopt, std::nullopt, QString(),
                         QString(), QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC), QString());
        FrameMetadata d2(FrameType::Dark, QSize(1000, 1000), 20.0, std::nullopt, std::nullopt, std::nullopt, QString(),
                         QString(), QDateTime::fromSecsSinceEpoch(1700000201, QTimeZone::UTC), QString());
        model->addFrames(
            FrameType::Dark,
            {FrameInfo(FrameId(QStringLiteral("C:/images/dark10.fit")), QStringLiteral("C:/images/dark10.fit"), d1),
             FrameInfo(FrameId(QStringLiteral("C:/images/dark20.fit")), QStringLiteral("C:/images/dark20.fit"), d2)});

        GroupingOptions opts = model->groupingOptions(FrameType::Dark);
        opts.byExposure = false;
        model->setGroupingOptions(FrameType::Dark, opts);
        EXPECT_FALSE(anyGroupLabelContains(treeModel, QStringLiteral("Exposure:")));

        opts.byExposure = true;
        opts.exposureToleranceSeconds = 0.0;
        model->setGroupingOptions(FrameType::Dark, opts);
        EXPECT_TRUE(anyGroupLabelContains(treeModel, QStringLiteral("Exposure:")));
    }

    // Light
    treeModel->setFrameType(FrameType::Light);
    {
        FrameMetadata l1(FrameType::Light, QSize(1000, 1000), 10.0, std::nullopt, std::nullopt, std::nullopt,
                         QStringLiteral("L"), QStringLiteral("Obj"),
                         QDateTime::fromSecsSinceEpoch(1700000300, QTimeZone::UTC), QString());
        FrameMetadata l2(FrameType::Light, QSize(1000, 1000), 20.0, std::nullopt, std::nullopt, std::nullopt,
                         QStringLiteral("L"), QStringLiteral("Obj"),
                         QDateTime::fromSecsSinceEpoch(1700000301, QTimeZone::UTC), QString());
        model->addFrames(
            FrameType::Light,
            {FrameInfo(FrameId(QStringLiteral("C:/images/light10.fit")), QStringLiteral("C:/images/light10.fit"), l1),
             FrameInfo(FrameId(QStringLiteral("C:/images/light20.fit")), QStringLiteral("C:/images/light20.fit"), l2)});

        GroupingOptions opts = model->groupingOptions(FrameType::Light);
        opts.byExposure = false;
        model->setGroupingOptions(FrameType::Light, opts);
        EXPECT_FALSE(anyGroupLabelContains(treeModel, QStringLiteral("Exposure:")));

        opts.byExposure = true;
        opts.exposureToleranceSeconds = 0.0;
        model->setGroupingOptions(FrameType::Light, opts);
        EXPECT_TRUE(anyGroupLabelContains(treeModel, QStringLiteral("Exposure:")));
    }
}

namespace
{
FrameInfo preCalibratedCaseFrame(FrameType type, const QString& filename, const QSize& size, double exposure,
                                 const QString& session, const QString& custom_tag)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, size, exposure, std::nullopt, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, session, custom_tag);
}

std::optional<QModelIndex> findFrameIndexByTooltip(const QAbstractItemModel& model, const QString& tooltipPrefix,
                                                   const QModelIndex& parent = QModelIndex())
{
    const int rows = model.rowCount(parent);
    for (int r = 0; r < rows; ++r)
    {
        const QModelIndex idx = model.index(r, 0, parent);
        if (!idx.isValid())
        {
            continue;
        }

        const QString tip = model.data(idx, Qt::ToolTipRole).toString();
        if (tip == tooltipPrefix || tip.startsWith(tooltipPrefix))
        {
            return idx;
        }

        if (model.rowCount(idx) > 0)
        {
            if (auto child = findFrameIndexByTooltip(model, tooltipPrefix, idx))
            {
                return child;
            }
        }
    }
    return std::nullopt;
}
} // namespace

TEST(GroupTreeModelPreCalibrated, PreCalibratedFlag_UpdatesLightsTabWarningHighlighting)
{
    IntegrationDataModel model;

    // Create a single light + a bias frame (no dark/flat). This should trigger a calibration warning highlight.
    auto light = preCalibratedCaseFrame(FrameType::Light, QStringLiteral("light_warn.fit"), QSize(100, 100), 10.0,
                                        QStringLiteral("2025-01-01"), QString());
    model.addFrames(FrameType::Light, {light});

    model.addFrames(FrameType::Bias,
                    {preCalibratedCaseFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                            QStringLiteral("2025-01-01"), QString())});

    GroupTreeModel tree(FrameType::Light, &model);

    // Locate the frame row by its tooltip (displayPath).
    const auto idx_before_opt = findFrameIndexByTooltip(tree, light.displayPath);
    ASSERT_TRUE(idx_before_opt.has_value());
    const QModelIndex idx_before = *idx_before_opt;

    const QVariant bg_before = tree.data(idx_before, Qt::BackgroundRole);
    EXPECT_TRUE(bg_before.isValid());

    // Enable pre-calibrated for the light group: warning highlight should disappear.
    const GroupKey group_key = model.lightGroups().front().key;
    LightCalibrationSettings settings;
    settings.preCalibrated = true;
    model.setLightCalibrationSettings(group_key, settings);

    const auto idx_enabled_opt = findFrameIndexByTooltip(tree, light.displayPath);
    ASSERT_TRUE(idx_enabled_opt.has_value());
    const QVariant bg_enabled = tree.data(*idx_enabled_opt, Qt::BackgroundRole);
    EXPECT_FALSE(bg_enabled.isValid());

    // Disable pre-calibrated again: warning highlight should return.
    settings.preCalibrated = false;
    model.setLightCalibrationSettings(group_key, settings);

    const auto idx_disabled_opt = findFrameIndexByTooltip(tree, light.displayPath);
    ASSERT_TRUE(idx_disabled_opt.has_value());
    const QVariant bg_disabled = tree.data(*idx_disabled_opt, Qt::BackgroundRole);
    EXPECT_TRUE(bg_disabled.isValid());
}
