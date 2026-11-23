// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Integration window as its builder makes it: its menu bar, the eight pages of its stages and the output
/// folder's label, field and button, which the process lays out under the list of its stages - no tabs and no button
/// box.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QStringList>
#include <QTabWidget>
#include <QWidget>

#include <array>
#include <cstddef>

#include "processes/ImageIntegration/ImageIntegrationProcessUiBuilder.h"

namespace
{

// The window lays out a menu bar of its own and makes the eight pages - the frame sources' each with a layout of its
// own - and the output folder in it, for the process to lay out: no tabs and no button box.
TEST(ImageIntegrationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildImageIntegrationProcessUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.mainMenuBar, nullptr);
    const std::array<QWidget*, 8> pages = {ui.biasTab,        ui.darkTab,         ui.flatTab,     ui.lightTab,
                                           ui.calibrationTab, ui.registrationTab, ui.stackingTab, ui.summaryTab};
    const QStringList names = {QStringLiteral("biasTab"),        QStringLiteral("darkTab"),
                               QStringLiteral("flatTab"),        QStringLiteral("lightTab"),
                               QStringLiteral("calibrationTab"), QStringLiteral("registrationTab"),
                               QStringLiteral("stackingTab"),    QStringLiteral("summaryTab")};
    for (std::size_t page = 0; page < pages.size(); ++page)
    {
        const QString& name = names[static_cast<qsizetype>(page)];
        ASSERT_NE(pages[page], nullptr) << name.toStdString();
        EXPECT_EQ(pages[page]->objectName(), name);
        EXPECT_EQ(pages[page]->parentWidget(), &host) << name.toStdString();
    }
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.verticalLayout_3, nullptr);
    ASSERT_NE(ui.verticalLayout_4, nullptr);
    ASSERT_NE(ui.verticalLayout_5, nullptr);
    EXPECT_EQ(ui.biasTab->layout(), ui.verticalLayout_2);
    EXPECT_EQ(ui.darkTab->layout(), ui.verticalLayout_3);
    EXPECT_EQ(ui.flatTab->layout(), ui.verticalLayout_4);
    EXPECT_EQ(ui.lightTab->layout(), ui.verticalLayout_5);
    ASSERT_NE(ui.label, nullptr);
    ASSERT_NE(ui.outputDirectoryEdit, nullptr);
    ASSERT_NE(ui.outputDirectoryBrowseButton, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image Integration"));
    EXPECT_EQ(host.size(), QSize(1024, 768));
    EXPECT_EQ(ui.verticalLayout->spacing(), 0);
    ASSERT_EQ(ui.verticalLayout->count(), 1) << "the menu bar alone: the process lays out the rest";
    EXPECT_EQ(ui.verticalLayout->itemAt(0)->widget(), ui.mainMenuBar);
    EXPECT_FALSE(ui.mainMenuBar->isNativeMenuBar());
    EXPECT_EQ(ui.label->text(), QStringLiteral("Output Directory:"));
    EXPECT_EQ(ui.outputDirectoryBrowseButton->text(), QStringLiteral("..."));
    EXPECT_EQ(ui.outputDirectoryBrowseButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));

    EXPECT_TRUE(host.findChildren<QTabWidget*>().isEmpty()) << "no tabs";
    EXPECT_TRUE(host.findChildren<QDialogButtonBox*>().isEmpty()) << "no Close and Reset of the window's own";
}

} // anonymous namespace
