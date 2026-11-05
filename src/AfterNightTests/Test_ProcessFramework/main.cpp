// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the process framework suite: the extension manager, host manager, SDK service and protocol cases, the
/// process windows' programmatic interfaces, the registry and the launcher, each case compiled here and linked against
/// the processes and Controls libraries. Runs under a headless QApplication with one application
/// context installed for the whole run - one engine session, the views it makes, every message - as the application
/// installs one before it opens a window; a case may install its own for its length. Window settings are written under
/// the suite's storage root, never the user's.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"
#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <QSettings>
#include <QString>

#include <gtest/gtest.h>

#include <memory>

namespace
{

an::tests::processes::ProcessTestContext* g_suiteContext = nullptr;

} // namespace

namespace an::tests::processes::framework
{

ProcessTestContext& suiteContext()
{
    return *g_suiteContext;
}

} // namespace an::tests::processes::framework

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ProcessFramework", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    // A window's settings - a dialog's size among them - are written under the suite's storage root, never the user's.
    const QString settingsRoot = QString::fromStdWString(harness.storage().root().wstring());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsRoot);
    ::testing::InitGoogleTest(&argc, argv);
    int result = 0;
    {
        std::unique_ptr<an::tests::processes::ProcessTestContext> context = an::tests::processes::openProcessContext();
        if (context == nullptr)
        {
            return harness.finish(1);
        }
        g_suiteContext = context.get();
        const an::tests::processes::framework::ScopedAppContext installed(*context);
        result = RUN_ALL_TESTS();
        an::tests::processes::shared::closeOpenedProcessWindows();
        g_suiteContext = nullptr;
    }
    return harness.finish(result);
}
