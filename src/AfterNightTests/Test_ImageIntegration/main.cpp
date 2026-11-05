// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the integration workflow's suite: the model, the import, the workflow core, the stages, the stores,
/// the report and every window of the integration and its Lite front end, run against the processes library and
/// over frames the neutral fixture layer writes as capture programs write them. Runs under a headless
/// QApplication, because the model is a QObject with an undo stack, with an application context installed for the whole
/// run - as the application installs one before it opens a window - which a case may replace with its own for its
/// length.

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"
#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <QSettings>
#include <QString>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ImageIntegration", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    // A window's settings - a dialog's size among them - are written under the suite's storage root, never the user's.
    // Where the native format is a file, as on Linux, this also moves the settings a window names by organization.
    const QString settingsRoot = QString::fromStdWString(harness.storage().root().wstring());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsRoot);
    ::testing::InitGoogleTest(&argc, argv);
    int result = 0;
    {
        const an::tests::integration::ScopedProcessWindowContext context;
        result = RUN_ALL_TESTS();
    }
    return harness.finish(result);
}
