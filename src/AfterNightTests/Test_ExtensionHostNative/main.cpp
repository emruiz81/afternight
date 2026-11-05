// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the full host's native suite: the host's application context, its native process window and the
/// session it borrows from the runtime backend, compiled from the host's sources and linked against the processes and
/// Controls libraries, under the Qt test harness's storage root, temporary directory, logger and headless application.
///
/// The run stands where the runtime backend's module seat stands in the host: it opens one engine session before the
/// first case, puts it in a session capsule record, and the host's own `HostEngineSession` borrows it from there and is
/// lent by the host's own `ExtensionAppContext`, installed for the whole run. After the last case the context goes, the
/// borrow is let go and the session is shut down, checked: a case that left an image, a hold or an operation of the
/// session behind fails the run.
///
/// Started with AFTERNIGHT_RUNTIME_BACKEND_SCENARIO naming a scenario, the executable runs that scenario instead of the
/// suite and answers through its exit code: a case that starts the interpreter and loads the runtime backend, as the
/// host does, gets a process of its own, because the interpreter cannot be started again once it has been finalized.

// The interpreter's headers come first, before Qt's keywords are defined.
#include "RuntimeBackendTestSupport.h"

#include "ExtensionAppContext.h"
#include "HostRuntimeScenarios.h"
#include "HostRuntimeSession.h"
#include "RuntimeSessionCapsule.h"
#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightPlatform/Logging.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QThreadPool>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{

TEST(ApplicationStorageIsolation, UsesOwnedTemporaryDirectory)
{
    const std::string root = an::tests::runtime_backend::environmentText(an::tests::harness::kStorageRootVariable);
    ASSERT_FALSE(root.empty());
    EXPECT_TRUE(QDir::isAbsolutePath(QString::fromStdString(root)));
    EXPECT_TRUE(QDir(QDir::tempPath()).dirName().startsWith(QLatin1String("ANtmp-")))
        << "Temporary directory was not redirected: " << QDir::tempPath().toStdString();
}

/// Runs the cases under the host's context, lending a session borrowed through a capsule record, and shuts the session
/// down after them.
int runSuite(const an::tests::harness::QtTestHarness& harness)
{
    an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open({});
    if (!opened.hasValue())
    {
        std::fprintf(stderr, "Test_ExtensionHostNative: the run's session did not open: %s\n",
                     std::string(opened.status().message()).c_str());
        return 1;
    }
    an::sdk::RuntimeSessionCapsule record;
    record.session = std::make_shared<an::consumer::EngineSession>(std::move(*opened));

    int result = 0;
    {
        an::ui::extensions::HostEngineSession borrowed;
        if (const an::Status status = borrowed.borrow(an::sdk::kRuntimeSessionCapsuleName, &record); status.failed())
        {
            std::fprintf(stderr, "Test_ExtensionHostNative: the run's session was not borrowed: %s\n",
                         std::string(status.message()).c_str());
            return 1;
        }

        const QString root = QString::fromStdString(harness.storage().root().string());
        an::ui::extensions::ExtensionAppContext context;
        context.setSessionDirectories(QDir(root).filePath(QStringLiteral("temp")).toStdString(),
                                      QDir(root).filePath(QStringLiteral("artifacts")).toStdString());
        context.setEngineSessionProvider(&borrowed);
        an::ui::setAppContext(&context);

        result = RUN_ALL_TESTS();

        // The host's order: the windows the cases made are gone, the pool's work finishes and what it posted is
        // delivered, the context goes, then the borrow.
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        an::ui::clearAppContext();
        context.setEngineSessionProvider(nullptr);
        borrowed.release();
    }

    if (const an::Status stopped = record.session->shutdown(); stopped.failed())
    {
        std::fprintf(stderr, "Test_ExtensionHostNative: the run's session did not shut down cleanly: %s\n",
                     std::string(stopped.message()).c_str());
        return result == 0 ? 1 : result;
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ExtensionHostNative", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }

    const std::string scenario =
        an::tests::runtime_backend::environmentText(an::tests::runtime_backend::kScenarioVariable);
    if (!scenario.empty())
    {
        const int result = an::tests::host_native::runHostScenario(scenario);
        std::fflush(stdout);
        std::fflush(stderr);
        // A scenario ends its process here, without returning through the static destructors of the module it loaded
        // and the interpreter it finalized: those are what its checks were about, not what ends it.
        std::_Exit(result);
    }

    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(runSuite(harness));
}
