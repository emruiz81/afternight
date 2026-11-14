// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's start in its order, over a main window of its own: the window cases share one window for their
// suite, so this case makes, starts and destroys another, never while theirs exists.

#include "app/AfterNightApp.h"
#include "app/ApplicationSession.h"
#include "app/ApplicationStart.h"

#include "core/ApplicationCore.h"
#include "controls/IAppContext.h"

#include <QApplication>
#include <QWidget>

#include <gtest/gtest.h>

// The stores open before anything else, because a shell is chosen from one of them; the style is installed next, before
// the first widget exists; and the window is made last, over the core, and readied with the session.
TEST(ApplicationStart, TheStoresOpenBeforeTheStyleAndTheFirstWidget)
{
    ASSERT_EQ(AfterNightApp::instanceOrNull(), nullptr);
    ASSERT_EQ(an::ui::appContext(), nullptr);

    an::ui::ApplicationSession session;
    an::ui::shell::ApplicationCore core;
    ASSERT_FALSE(core.storesOpen());
    const qsizetype widgetsBefore = QApplication::allWidgets().size();

    int styled = 0;
    bool storesOpenWhenStyled = false;
    qsizetype widgetsWhenStyled = -1;
    bool windowWhenStyled = true;
    AfterNightApp* const window = an::ui::startApplication(core, session, [&]() {
        ++styled;
        storesOpenWhenStyled = core.storesOpen();
        widgetsWhenStyled = QApplication::allWidgets().size();
        windowWhenStyled = AfterNightApp::instanceOrNull() != nullptr;
    });

    ASSERT_NE(window, nullptr);
    EXPECT_EQ(styled, 1);
    EXPECT_TRUE(storesOpenWhenStyled) << "the style was installed before the stores were open";
    EXPECT_EQ(widgetsWhenStyled, widgetsBefore) << "a widget was made before the style was installed";
    EXPECT_FALSE(windowWhenStyled);
    EXPECT_EQ(AfterNightApp::instanceOrNull(), window);
    EXPECT_TRUE(session.isOpen());
    ASSERT_NE(an::ui::appContext(), nullptr);
    EXPECT_EQ(an::ui::appContext()->getMainWindow(), window);
    EXPECT_EQ(an::ui::appContext()->getEngineSessionProvider(), &session);

    ASSERT_TRUE(window->close());
    const an::Status shut = an::ui::shutDownApplication(session, []() { AfterNightApp::destroyInstance(); });
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(an::ui::appContext(), nullptr);
}
