// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QtTest/QTest>

#include "processes/ImageIntegration/tabs/FrameSourceTab.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"

#include <QDateTime>
#include <QTimeZone>

namespace
{
FrameInfo makeLight(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("Obj"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

TEST(FrameSourceTab, SearchBarFiltersAndShowsMatchCount)
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }

    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    model.addFrames(FrameType::Light, {makeLight(QStringLiteral("C:/test/M42_Light_001.fit")),
                                       makeLight(QStringLiteral("C:/test/M31_Light_001.fit"))});

    auto* search = tab.searchBar();
    ASSERT_TRUE(search != nullptr);

    // Using setText() is reliable even when the widget isn't shown/focused.
    search->setText(QStringLiteral("M42"));

    // Debounce in FrameSourceTab is 200ms; wait for it to apply.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500)
    {
        QCoreApplication::processEvents();
        if (tab.treeModel() && tab.treeModel()->matchCount() == 1)
        {
            break;
        }
        QTest::qWait(25);
    }

    ASSERT_TRUE(tab.treeModel() != nullptr);
    EXPECT_EQ(tab.treeModel()->matchCount(), 1);
    EXPECT_EQ(tab.treeModel()->totalCount(), 2);

    auto* label = tab.findChild<QLabel*>("frameTreeMatchCount");
    ASSERT_TRUE(label != nullptr);
    EXPECT_TRUE(label->text().contains("Showing 1 of 2"));
}

TEST(FrameSourceTab, CtrlFFocusesSearch)
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }

    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);
    tab.show();

    QTest::keyClick(&tab, Qt::Key_F, Qt::ControlModifier);
    // The click's own release of Ctrl still carries the modifier, so the application would go on reading Ctrl as
    // held and a later case's row selection would toggle rows; let the key go with nothing held.
    QTest::keyRelease(&tab, Qt::Key_Control);
    QCoreApplication::processEvents();

    ASSERT_TRUE(tab.searchBar() != nullptr);
    EXPECT_TRUE(tab.searchBar()->hasFocus());
    EXPECT_EQ(QGuiApplication::keyboardModifiers(), Qt::NoModifier) << "the case leaves a modifier held";
}
