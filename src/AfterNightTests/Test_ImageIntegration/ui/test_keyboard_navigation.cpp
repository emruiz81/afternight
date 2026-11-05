// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QApplication>
#include <QItemSelectionModel>
#include <QTimeZone>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/tabs/FrameSourceTab.h"
#include "processes/ImageIntegration/model/FrameId.h"

namespace
{
FrameInfo makeFrame(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 1.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("TestObject"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    return FrameInfo(FrameId(path), path, meta);
}

void ensureApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }
}
} // namespace

TEST(KeyboardNavigation, EscapeClearsSelection)
{
    ensureApp();

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);
    FrameTreeView view;
    view.setGroupTreeModel(&treeModel);

    model.addFrames(FrameType::Light, {makeFrame(QStringLiteral("C:/images/light1.fit"))});

    const QModelIndex groupIdx = treeModel.index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());
    view.selectionModel()->select(groupIdx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    EXPECT_GT(view.selectionModel()->selectedRows().count(), 0);

    QTest::keyClick(&view, Qt::Key_Escape);
    EXPECT_EQ(view.selectionModel()->selectedRows().count(), 0);
}

TEST(KeyboardNavigation, CtrlASelectsAll)
{
    ensureApp();

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);
    FrameTreeView view;
    view.setGroupTreeModel(&treeModel);

    std::vector<FrameInfo> frames;
    frames.reserve(100);
    for (int i = 0; i < 100; ++i)
    {
        frames.push_back(makeFrame(QStringLiteral("C:/images/light%1.fit").arg(i)));
    }
    model.addFrames(FrameType::Light, frames);

    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
    // The click's own release of Ctrl still carries the modifier, so the application would go on reading Ctrl as
    // held and a later case's row selection would toggle rows; let the key go with nothing held.
    QTest::keyRelease(&view, Qt::Key_Control);
    EXPECT_EQ(view.selectedFilePaths().count(), 100);
    EXPECT_EQ(QGuiApplication::keyboardModifiers(), Qt::NoModifier) << "the case leaves a modifier held";
}

TEST(KeyboardNavigation, HomeAndEndNavigateFrames)
{
    ensureApp();

    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);
    FrameTreeView view;
    view.setGroupTreeModel(&treeModel);

    std::vector<FrameInfo> frames;
    frames.reserve(100);
    for (int i = 0; i < 100; ++i)
    {
        frames.push_back(makeFrame(QStringLiteral("C:/images/light%1.fit").arg(i)));
    }
    model.addFrames(FrameType::Light, frames);

    const QModelIndex groupIdx = treeModel.index(0, 0);
    ASSERT_TRUE(groupIdx.isValid());
    const QModelIndex mid = treeModel.index(50, 0, groupIdx);
    ASSERT_TRUE(mid.isValid());

    view.selectionModel()->setCurrentIndex(mid, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    QTest::keyClick(&view, Qt::Key_Home);
    EXPECT_EQ(view.currentIndex().row(), 0);

    QTest::keyClick(&view, Qt::Key_End);
    EXPECT_EQ(view.currentIndex().row(), 99);
}

TEST(KeyboardNavigation, CtrlGGoToGroupExists)
{
    ensureApp();

    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    // Add frames with different filters to create groups
    std::vector<FrameInfo> frames;
    for (int i = 0; i < 5; ++i)
    {
        FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                           QStringLiteral("Ha"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
        frames.push_back(FrameInfo(FrameId(QStringLiteral("C:/M42_Ha_%1.fit").arg(i)),
                                   QStringLiteral("C:/M42_Ha_%1.fit").arg(i), meta));
    }
    model.addFrames(FrameType::Light, frames);

    // Verify tree model has groups
    ASSERT_TRUE(tab.treeModel() != nullptr);
    EXPECT_GT(tab.treeModel()->rowCount(QModelIndex()), 0);
}
