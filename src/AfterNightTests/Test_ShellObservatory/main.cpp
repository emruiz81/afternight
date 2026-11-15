// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory shell's suite's entry point: the storage root, the temporary directory and the headless Qt
// application every case runs under, drawn in the Observatory style profile the shell names, put in force before the
// first widget as the application puts it. No application context is installed for the whole run: each case's fixture
// starts the application, which installs its context, and ends it.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>

#include "app/ApplicationStorage.h"
#include "app/ModernizationFeatureFlags.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "fixtures/HeadlessQtFixture.h"
#include "fixtures/ScopedTestTempDir.h"

#include <AfterNightPlatform/Logging.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    QTemporaryDir test_storage(QDir::temp().filePath(QStringLiteral("AfterNight-Test_ShellObservatory-XXXXXX")));
    if (!test_storage.isValid())
    {
        return 1;
    }
    qputenv(an::ui::kTestStorageRootEnvironmentVariable, test_storage.path().toUtf8());

    // Redirect TEMP/TMP/TMPDIR into the storage root before any Qt application object
    // exists - Qt caches QDir::tempPath() lazily, so this must precede QApplication.
    const an::test::scoped_test_temp_dir scoped_temp_dir("Test_ShellObservatory");

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, test_storage.path());
    // Qt's own file dialog keeps its history in the native store, a file under the user's configuration directory on
    // Linux, which the cases that drive the application's file dialogs would otherwise write.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, test_storage.path());

    // No case reaches the extension repository over the network: a page left to its default provider would fetch the
    // index on the thread pool, and the run would depend on the network and on how long the fetch took.
    qputenv("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "1");

    an::tests::fixtures::prepareHeadlessQtEnvironment(argc, argv);
    an::tests::fixtures::installQtTestMessageFilters();

    int test_result = 0;
    {
        QApplication app(argc, argv);
        app.setApplicationName("Test_ShellObservatory");
        app.setOrganizationName("AfterNight");
        an::tests::fixtures::applyFusionStyle(app);
        // The shell's profile and the application's style before the first widget, as the application's start puts
        // them.
        (void)an::ui::setStyleProfile(an::ui::StyleProfile::Observatory);
        if (an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled())
        {
            (void)an::ui::installAnProxyStyle();
        }

        an::platform::initializeLogging(
            QDir(test_storage.path()).filePath(QStringLiteral("Test_ShellObservatory.log")).toStdString());

        test_result = RUN_ALL_TESTS();

        // Work a case left on the thread pool finishes while the application object it may use still exists, as the
        // application drains the pool before it lets its session and its QApplication go.
        QThreadPool::globalInstance()->waitForDone();

        an::platform::shutdownLogging();
    }

    return test_storage.remove() ? test_result : 1;
}
