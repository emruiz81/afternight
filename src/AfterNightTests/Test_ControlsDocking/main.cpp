// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QTemporaryDir>

#include "DockingTestSupport.h"
#include "fixtures/HeadlessQtFixture.h"
#include "theme/AnProxyStyle.h"

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // The dock library asks the platform where the cursor is while a window is dragged. The offscreen platform keeps
    // a cursor a case can place; the minimal platform, the other suites' choice on Windows, keeps none.
    if (!an::tests::fixtures::hasQtPlatformArg(argc, argv) && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    an::tests::fixtures::prepareHeadlessQtEnvironment(argc, argv);
    an::tests::fixtures::installQtTestMessageFilters();

    // Nothing here writes a setting, and nothing may reach the user's storage if a unit one day does.
    QTemporaryDir storageRoot;
    if (!qEnvironmentVariableIsSet("AFTERNIGHT_TEST_STORAGE_ROOT") && storageRoot.isValid())
    {
        qputenv("AFTERNIGHT_TEST_STORAGE_ROOT", storageRoot.path().toUtf8());
    }

    QApplication app(argc, argv);
    app.setApplicationName("Test_ControlsDocking");
    app.setOrganizationName("AfterNight");

    // The application's style, so the decorations are drawn over what they are drawn over in the application.
    an::ui::installAnProxyStyle();
    an::tests::docking::configureDockLibrary();

    return RUN_ALL_TESTS();
}
