// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the standalone workflow tools' suite: Image Calibration, Image Stacking, Image Registration and the
/// Image List Analyzer, with the file list, the run units and the registration records they share, run against the
/// processes library, with the integration's own dialogs and comet registration the tools reach
/// through their companions. Runs under a headless QApplication whose event loop delivers what the tools' workers queue
/// to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <QSettings>
#include <QString>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ProcessesWorkflowTools", an::engineVersionString()});
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
    return harness.finish(RUN_ALL_TESTS());
}
