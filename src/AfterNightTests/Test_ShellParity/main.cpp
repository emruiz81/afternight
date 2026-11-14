// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The parity suite's entry point: the storage root, the temporary directory and the headless Qt application every
// scenario runs under. No application context is installed for the whole run: each shell's scenarios start the
// application as `main` starts it, which installs its context, and end it as `main` ends it.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>

#include "app/ApplicationStorage.h"
#include "fixtures/HeadlessQtFixture.h"
#include "fixtures/ScopedTestTempDir.h"

#include <AfterNightPlatform/Logging.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    QTemporaryDir test_storage(QDir::temp().filePath(QStringLiteral("AfterNight-Test_ShellParity-XXXXXX")));
    if (!test_storage.isValid())
    {
        return 1;
    }
    qputenv(an::ui::kTestStorageRootEnvironmentVariable, test_storage.path().toUtf8());

    // Redirect TEMP/TMP/TMPDIR into the storage root before any Qt application object
    // exists - Qt caches QDir::tempPath() lazily, so this must precede QApplication.
    const an::test::scoped_test_temp_dir scoped_temp_dir("Test_ShellParity");

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, test_storage.path());
    // Qt's own file dialog keeps its history in the native store, a file under the user's configuration directory on
    // Linux, which the scenarios that drive the application's file dialogs would otherwise write.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, test_storage.path());

    // No scenario reaches the extension repository over the network.
    qputenv("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "1");

    an::tests::fixtures::prepareHeadlessQtEnvironment(argc, argv);
    an::tests::fixtures::installQtTestMessageFilters();

    int test_result = 0;
    {
        QApplication app(argc, argv);
        app.setApplicationName("Test_ShellParity");
        app.setOrganizationName("AfterNight");
        an::tests::fixtures::applyFusionStyle(app);

        an::platform::initializeLogging(
            QDir(test_storage.path()).filePath(QStringLiteral("Test_ShellParity.log")).toStdString());

        test_result = RUN_ALL_TESTS();

        // Work a scenario left on the thread pool finishes while the application object it may use still exists, as
        // the application drains the pool before it lets its session and its QApplication go.
        QThreadPool::globalInstance()->waitForDone();

        an::platform::shutdownLogging();
    }

    return test_storage.remove() ? test_result : 1;
}
