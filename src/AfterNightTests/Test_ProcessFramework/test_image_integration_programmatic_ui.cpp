// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Integration window as its builder makes it: its tabs, its menu bar and its buttons, and tab bars sized for
/// their titles.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

#include "processes/ImageIntegration/ImageIntegrationProcessUiBuilder.h"

namespace
{

// The window opens on the first of its two root tabs, its four source tabs and four workflow tabs, a menu bar of its
// own, Close and Reset, and tab bars that neither stretch, elide nor scroll their titles.
TEST(ImageIntegrationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildImageIntegrationProcessUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.mainMenuBar, nullptr);
    ASSERT_NE(ui.rootTabWidget, nullptr);
    ASSERT_NE(ui.sourceTabWidget, nullptr);
    ASSERT_NE(ui.workflowTabWidget, nullptr);
    ASSERT_NE(ui.biasTab, nullptr);
    ASSERT_NE(ui.darkTab, nullptr);
    ASSERT_NE(ui.flatTab, nullptr);
    ASSERT_NE(ui.lightTab, nullptr);
    ASSERT_NE(ui.calibrationTab, nullptr);
    ASSERT_NE(ui.registrationTab, nullptr);
    ASSERT_NE(ui.stackingTab, nullptr);
    ASSERT_NE(ui.summaryTab, nullptr);
    ASSERT_NE(ui.outputDirectoryEdit, nullptr);
    ASSERT_NE(ui.outputDirectoryBrowseButton, nullptr);
    ASSERT_NE(ui.dialogButtonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image Integration"));
    EXPECT_EQ(host.size(), QSize(1024, 768));
    EXPECT_EQ(ui.verticalLayout->spacing(), 0);
    EXPECT_FALSE(ui.mainMenuBar->isNativeMenuBar());
    EXPECT_EQ(ui.rootTabWidget->count(), 2);
    EXPECT_EQ(ui.sourceTabWidget->count(), 4);
    EXPECT_EQ(ui.workflowTabWidget->count(), 4);
    EXPECT_EQ(ui.rootTabWidget->currentIndex(), 0);
    EXPECT_EQ(ui.sourceTabWidget->currentIndex(), 0);
    EXPECT_EQ(ui.workflowTabWidget->currentIndex(), 0);
    EXPECT_EQ(ui.outputDirectoryBrowseButton->text(), QStringLiteral("..."));
    EXPECT_EQ(ui.outputDirectoryBrowseButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));

    EXPECT_NE(ui.dialogButtonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.dialogButtonBox->button(QDialogButtonBox::Reset), nullptr);

    auto assertTabBarSized = [](QTabWidget* tabWidget) {
        ASSERT_NE(tabWidget, nullptr);
        auto* tabBar = tabWidget->tabBar();
        ASSERT_NE(tabBar, nullptr);
        EXPECT_FALSE(tabBar->expanding());
        EXPECT_EQ(tabBar->elideMode(), Qt::ElideNone);
        EXPECT_FALSE(tabBar->usesScrollButtons());
        EXPECT_EQ(tabBar->property("anTabMinWidth").toInt(), 170);
        EXPECT_EQ(tabBar->property("anTabMinHeight").toInt(), 18);
    };

    assertTabBarSized(ui.rootTabWidget);
    assertTabBarSized(ui.sourceTabWidget);
    assertTabBarSized(ui.workflowTabWidget);
}

} // anonymous namespace
