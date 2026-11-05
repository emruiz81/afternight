// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QMenu>
#include <QtTest/QSignalSpy>
#include <QStandardPaths>
#include <QDir>

#include "processes/ImageIntegration/widgets/TreeContextMenuHandler.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/FrameId.h"

namespace
{
FrameInfo createFrame(const QString& path, FrameType type = FrameType::Light)
{
    FrameMetadata meta(type, QSize(1000, 1000),
                       1.0, // exposure
                       std::nullopt, // temp
                       std::nullopt, // gain
                       std::nullopt, // offset
                       "L", // filter
                       "Obj", // object
                       QDateTime::currentDateTimeUtc(),
                       "" // readout mode
    );

    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

class TreeContextMenuHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        // Use Light for default model
        treeModel = new GroupTreeModel(FrameType::Light, model);
        treeView = new FrameTreeView();
        treeView->setGroupTreeModel(treeModel);
        handler = new TreeContextMenuHandler(model, treeView);
    }

    void TearDown() override
    {
        delete handler;
        delete treeView; // This might delete treeModel if setParent? No, setGroupTreeModel doesn't take ownership
                         // usually.
        // But QTreeView::setModel doesn't take ownership.
        delete treeModel;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    GroupTreeModel* treeModel = nullptr;
    FrameTreeView* treeView = nullptr;
    TreeContextMenuHandler* handler = nullptr;
};

TEST_F(TreeContextMenuHandlerTest, LightFrameShowsSetAsReferenceFrameAction)
{
    // Given: single LIGHT frame
    QString path = QStringLiteral("C:/test/light.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    // Select it
    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    // When: building context menu
    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    // Then: menu contains "Set as Registration Reference" action
    bool found = false;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Set as Registration Reference"))
            found = true;
    }
    EXPECT_TRUE(found);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, LightFrameShowsShowInTableActions)
{
    // Given: single LIGHT frame
    QString path = QStringLiteral("C:/test/light.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    bool foundCal = false;
    bool foundReg = false;
    bool foundInt = false;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Show in Calibration Table"))
            foundCal = true;
        if (action->text() == QStringLiteral("Show in Registration Table"))
            foundReg = true;
        if (action->text() == QStringLiteral("Show in Integration Table"))
            foundInt = true;
    }

    EXPECT_TRUE(foundCal);
    EXPECT_TRUE(foundReg);
    EXPECT_TRUE(foundInt);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, ExcludedLightFrameDoesNotShowIntegrationTableAction)
{
    // Given: single LIGHT frame that is excluded/disabled
    QString path = QStringLiteral("C:/test/light_excluded.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});
    model->updateFrameAttribute(path, FrameAttribute::Excluded, true);

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    bool foundReg = false;
    bool foundInt = false;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Show in Registration Table"))
            foundReg = true;
        if (action->text() == QStringLiteral("Show in Integration Table"))
            foundInt = true;
    }

    EXPECT_TRUE(foundReg);
    EXPECT_FALSE(foundInt);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, LightFrameShowInRegistrationTableEmitsFramePath)
{
    QString path = QStringLiteral("C:/test/light_reg_jump.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    QAction* action = nullptr;
    for (QAction* a : menu->actions())
    {
        if (a->text() == QStringLiteral("Show in Registration Table"))
        {
            action = a;
            break;
        }
    }
    ASSERT_TRUE(action);

    QSignalSpy spy(handler, &TreeContextMenuHandler::showInRegistrationTableRequested);
    action->trigger();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toString(), FrameId(path).canonicalPath());

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, LightFrameShowInIntegrationTableEmitsFramePath)
{
    QString path = QStringLiteral("C:/test/light_int_jump.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    QAction* action = nullptr;
    for (QAction* a : menu->actions())
    {
        if (a->text() == QStringLiteral("Show in Integration Table"))
        {
            action = a;
            break;
        }
    }
    ASSERT_TRUE(action);

    QSignalSpy spy(handler, &TreeContextMenuHandler::showInIntegrationTableRequested);
    action->trigger();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toString(), FrameId(path).canonicalPath());

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, BiasFrameShowsSetAsMasterAction)
{
    // Switch model to Bias
    treeModel->setFrameType(FrameType::Bias);

    QString path = QStringLiteral("C:/test/bias.fit");
    model->addFrames(FrameType::Bias, {createFrame(path, FrameType::Bias)});

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Bias, selection);

    bool found = false;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Set as Master"))
            found = true;
    }
    EXPECT_TRUE(found);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, ChangeTypeSubmenuHasAllFrameTypes)
{
    QString path = QStringLiteral("C:/test/light.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    bool foundMenu = false;
    for (QAction* action : menu->actions())
    {
        if (action->menu() && action->text() == QStringLiteral("Change Type"))
        {
            foundMenu = true;
            // Check subactions
            QList<QAction*> subActions = action->menu()->actions();
            // Should contain Bias, Dark, Flat (Light is excluded as current type)
            EXPECT_EQ(subActions.size(), 3);
        }
    }
    EXPECT_TRUE(foundMenu);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, ActionExecutionEmitsCorrectSignal)
{
    QString path = QStringLiteral("C:/test/light.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx = treeModel->index(0, 0, groupIdx);
    QModelIndexList selection = {frameIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    // Find Set as Registration Reference action
    QAction* targetAction = nullptr;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Set as Registration Reference"))
            targetAction = action;
    }
    ASSERT_TRUE(targetAction);

    QSignalSpy spy(handler, &TreeContextMenuHandler::referenceFrameRequested);
    targetAction->trigger();

    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toString(), path);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, BlinkSelectedSignalCarriesFrameTypeAndPaths)
{
    treeModel->setFrameType(FrameType::Bias);

    const QString path1 = QStringLiteral("C:/test/bias_1.fit");
    const QString path2 = QStringLiteral("C:/test/bias_2.fit");
    model->addFrames(FrameType::Bias, {createFrame(path1, FrameType::Bias), createFrame(path2, FrameType::Bias)});

    const QModelIndex groupIdx = treeModel->index(0, 0);
    const QModelIndex frameIdx1 = treeModel->index(0, 0, groupIdx);
    const QModelIndex frameIdx2 = treeModel->index(1, 0, groupIdx);
    const QModelIndexList selection = {frameIdx1, frameIdx2};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Bias, selection);

    QAction* blinkAction = nullptr;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Blink Selected..."))
        {
            blinkAction = action;
            break;
        }
    }

    ASSERT_NE(blinkAction, nullptr);

    QSignalSpy spy(handler, &TreeContextMenuHandler::blinkRequested);
    blinkAction->trigger();

    ASSERT_EQ(spy.count(), 1);
    const QList<QVariant> arguments = spy.takeFirst();
    ASSERT_EQ(arguments.size(), 2);

    EXPECT_EQ(arguments.at(0).value<FrameType>(), FrameType::Bias);

    const QStringList blinkPaths = arguments.at(1).toStringList();
    EXPECT_EQ(blinkPaths.size(), 2);
    EXPECT_EQ(blinkPaths.at(0), FrameId(path1).canonicalPath());
    EXPECT_EQ(blinkPaths.at(1), FrameId(path2).canonicalPath());

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, MultiSelectDisablesSetAsMaster)
{
    // Add two bias frames
    QString path1 = QStringLiteral("C:/test/bias1.fit");
    QString path2 = QStringLiteral("C:/test/bias2.fit");
    model->addFrames(FrameType::Bias, {createFrame(path1, FrameType::Bias), createFrame(path2, FrameType::Bias)});

    treeModel->setFrameType(FrameType::Bias);

    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndex frameIdx1 = treeModel->index(0, 0, groupIdx);
    QModelIndex frameIdx2 = treeModel->index(1, 0, groupIdx);
    QModelIndexList selection = {frameIdx1, frameIdx2};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Bias, selection);

    // "Set as Master" should NOT be present for multi-select
    bool foundSetMaster = false;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Set as Master"))
            foundSetMaster = true;
    }
    EXPECT_FALSE(foundSetMaster);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, GroupContextMenuShowsExpandCollapseActions)
{
    QString path = QStringLiteral("C:/test/light.fit");
    model->addFrames(FrameType::Light, {createFrame(path)});

    // Select the group node, not the frame
    QModelIndex groupIdx = treeModel->index(0, 0);
    QModelIndexList selection = {groupIdx};

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, selection);

    bool foundExpand = false;
    bool foundCollapse = false;
    bool foundRemoveGroup = false;

    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Expand All"))
            foundExpand = true;
        if (action->text() == QStringLiteral("Collapse All"))
            foundCollapse = true;
        if (action->text() == QStringLiteral("Remove Group"))
            foundRemoveGroup = true;
    }

    EXPECT_TRUE(foundExpand);
    EXPECT_TRUE(foundCollapse);
    EXPECT_TRUE(foundRemoveGroup);

    bool foundShowCal = false;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Show in Calibration Table"))
            foundShowCal = true;
    }
    EXPECT_TRUE(foundShowCal);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, MultiSelectShowsSelectActionWhenAnyExcluded)
{
    const QString path1 = QStringLiteral("C:/test/light1.fit");
    const QString path2 = QStringLiteral("C:/test/light2.fit");
    model->addFrames(FrameType::Light, {createFrame(path1), createFrame(path2)});

    const auto frameIndexFor = [this](const QString& path) {
        const QModelIndex idx = treeModel->findIndexForPath(path);
        EXPECT_TRUE(idx.isValid());
        return idx;
    };

    // Exclude the first frame (this will trigger a model rebuild via frameUpdated)
    ASSERT_TRUE(treeModel->setData(frameIndexFor(path1), Qt::Unchecked, Qt::CheckStateRole));
    EXPECT_EQ(treeModel->data(frameIndexFor(path1), Qt::CheckStateRole).toInt(), Qt::Unchecked);
    EXPECT_EQ(treeModel->data(frameIndexFor(path2), Qt::CheckStateRole).toInt(), Qt::Checked);

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, {frameIndexFor(path1), frameIndexFor(path2)});
    ASSERT_NE(menu, nullptr);

    QAction* selectAction = nullptr;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Include in Processing"))
        {
            selectAction = action;
            break;
        }
    }
    ASSERT_NE(selectAction, nullptr);
    EXPECT_TRUE(selectAction->isEnabled());

    // Trigger should include all selected frames
    selectAction->trigger();
    EXPECT_EQ(treeModel->data(frameIndexFor(path1), Qt::CheckStateRole).toInt(), Qt::Checked);
    EXPECT_EQ(treeModel->data(frameIndexFor(path2), Qt::CheckStateRole).toInt(), Qt::Checked);

    delete menu;
}

TEST_F(TreeContextMenuHandlerTest, MultiSelectShowsDeselectActionWhenAllIncluded)
{
    const QString path1 = QStringLiteral("C:/test/light1.fit");
    const QString path2 = QStringLiteral("C:/test/light2.fit");
    model->addFrames(FrameType::Light, {createFrame(path1), createFrame(path2)});

    const auto frameIndexFor = [this](const QString& path) {
        const QModelIndex idx = treeModel->findIndexForPath(path);
        EXPECT_TRUE(idx.isValid());
        return idx;
    };

    EXPECT_EQ(treeModel->data(frameIndexFor(path1), Qt::CheckStateRole).toInt(), Qt::Checked);
    EXPECT_EQ(treeModel->data(frameIndexFor(path2), Qt::CheckStateRole).toInt(), Qt::Checked);

    QMenu* menu = handler->createContextMenu(treeView, FrameType::Light, {frameIndexFor(path1), frameIndexFor(path2)});
    ASSERT_NE(menu, nullptr);

    QAction* deselectAction = nullptr;
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Exclude from Processing"))
        {
            deselectAction = action;
            break;
        }
    }
    ASSERT_NE(deselectAction, nullptr);
    EXPECT_TRUE(deselectAction->isEnabled());

    deselectAction->trigger();
    EXPECT_EQ(treeModel->data(frameIndexFor(path1), Qt::CheckStateRole).toInt(), Qt::Unchecked);
    EXPECT_EQ(treeModel->data(frameIndexFor(path2), Qt::CheckStateRole).toInt(), Qt::Unchecked);

    delete menu;
}
