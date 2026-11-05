// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_tree_display_mode_widget.cpp
 * @brief Unit tests for TreeDisplayModeWidget.
 *
 * Tests the display mode toggle widget that switches between
 * hierarchical (grouped) and flat views in the frame tree.
 */

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QApplication>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDateTime>
#include <QTimeZone>

#include "processes/ImageIntegration/widgets/TreeDisplayModeWidget.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"

namespace
{
void ensureQApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }
}

FrameInfo createTestFrame(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("M42"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

class TreeDisplayModeWidgetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureQApp();
        model = new IntegrationDataModel();
        treeModel = new GroupTreeModel(FrameType::Light, model);
        widget = new TreeDisplayModeWidget();
    }

    void TearDown() override
    {
        delete widget;
        delete treeModel;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    GroupTreeModel* treeModel = nullptr;
    TreeDisplayModeWidget* widget = nullptr;
};

TEST_F(TreeDisplayModeWidgetTest, ConstructsWithoutCrash)
{
    ASSERT_NE(widget, nullptr);
}

TEST_F(TreeDisplayModeWidgetTest, ContainsRadioButtons)
{
    QRadioButton* hierarchical = widget->findChild<QRadioButton*>();
    ASSERT_NE(hierarchical, nullptr);

    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    EXPECT_EQ(radios.size(), 2);
}

TEST_F(TreeDisplayModeWidgetTest, DefaultsToFlatMode)
{
    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    ASSERT_EQ(radios.size(), 2);

    // Find the "Flat" radio by text
    QRadioButton* flatRadio = nullptr;
    for (auto* r : radios)
    {
        if (r->text().contains(QStringLiteral("Flat"), Qt::CaseInsensitive))
        {
            flatRadio = r;
            break;
        }
    }
    ASSERT_NE(flatRadio, nullptr);
    EXPECT_TRUE(flatRadio->isChecked());
}

TEST_F(TreeDisplayModeWidgetTest, SetModelSyncsState)
{
    widget->setModel(treeModel);

    // Set model to hierarchical mode
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);

    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    QRadioButton* hierarchicalRadio = nullptr;
    for (auto* r : radios)
    {
        if (r->text().contains(QStringLiteral("Hierarchical"), Qt::CaseInsensitive))
        {
            hierarchicalRadio = r;
            break;
        }
    }
    ASSERT_NE(hierarchicalRadio, nullptr);
    EXPECT_TRUE(hierarchicalRadio->isChecked());
}

TEST_F(TreeDisplayModeWidgetTest, EmitsSignalOnUserInteraction)
{
    widget->setModel(treeModel);

    QSignalSpy spy(widget, &TreeDisplayModeWidget::displayModeChanged);

    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    QRadioButton* hierarchicalRadio = nullptr;
    for (auto* r : radios)
    {
        if (r->text().contains(QStringLiteral("Hierarchical"), Qt::CaseInsensitive))
        {
            hierarchicalRadio = r;
            break;
        }
    }
    ASSERT_NE(hierarchicalRadio, nullptr);

    // Click hierarchical radio
    hierarchicalRadio->setChecked(true);

    EXPECT_GE(spy.count(), 1);
    auto args = spy.takeFirst();
    auto emittedMode = args.at(0).value<GroupTreeModel::DisplayMode>();
    EXPECT_EQ(emittedMode, GroupTreeModel::DisplayMode::Hierarchical);
}

TEST_F(TreeDisplayModeWidgetTest, UpdatesModelOnChange)
{
    widget->setModel(treeModel);

    // Ensure initial state is flat
    EXPECT_EQ(treeModel->displayMode(), GroupTreeModel::DisplayMode::Flat);

    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    QRadioButton* hierarchicalRadio = nullptr;
    for (auto* r : radios)
    {
        if (r->text().contains(QStringLiteral("Hierarchical"), Qt::CaseInsensitive))
        {
            hierarchicalRadio = r;
            break;
        }
    }
    ASSERT_NE(hierarchicalRadio, nullptr);

    hierarchicalRadio->setChecked(true);
    QApplication::processEvents();

    EXPECT_EQ(treeModel->displayMode(), GroupTreeModel::DisplayMode::Hierarchical);
}

TEST_F(TreeDisplayModeWidgetTest, TracksModelDisplayModeChanges)
{
    widget->setModel(treeModel);

    // Change model externally
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);

    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    QRadioButton* hierarchicalRadio = nullptr;
    for (auto* r : radios)
    {
        if (r->text().contains(QStringLiteral("Hierarchical"), Qt::CaseInsensitive))
        {
            hierarchicalRadio = r;
            break;
        }
    }
    ASSERT_NE(hierarchicalRadio, nullptr);
    EXPECT_TRUE(hierarchicalRadio->isChecked());

    // Change back to flat
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Flat);

    QRadioButton* flatRadio = nullptr;
    for (auto* r : radios)
    {
        if (r->text().contains(QStringLiteral("Flat"), Qt::CaseInsensitive))
        {
            flatRadio = r;
            break;
        }
    }
    ASSERT_NE(flatRadio, nullptr);
    EXPECT_TRUE(flatRadio->isChecked());
}

TEST_F(TreeDisplayModeWidgetTest, HandlesNullModel)
{
    // Should not crash with null model
    widget->setModel(nullptr);

    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    EXPECT_EQ(radios.size(), 2);

    // Clicking should not crash
    for (auto* r : radios)
    {
        r->setChecked(true);
    }
    SUCCEED();
}

TEST_F(TreeDisplayModeWidgetTest, DisconnectsOldModelOnReassign)
{
    widget->setModel(treeModel);

    // Create a new model
    auto* newModel = new IntegrationDataModel();
    auto* newTreeModel = new GroupTreeModel(FrameType::Light, newModel);

    widget->setModel(newTreeModel);

    // Changing old model should not affect widget
    treeModel->setDisplayMode(GroupTreeModel::DisplayMode::Hierarchical);

    // Widget should still reflect new model's state (flat by default)
    QList<QRadioButton*> radios = widget->findChildren<QRadioButton*>();
    QRadioButton* flatRadio = nullptr;
    for (auto* r : radios)
    {
        if (r->text().contains(QStringLiteral("Flat"), Qt::CaseInsensitive))
        {
            flatRadio = r;
            break;
        }
    }
    ASSERT_NE(flatRadio, nullptr);
    EXPECT_TRUE(flatRadio->isChecked());

    delete newTreeModel;
    delete newModel;
}
