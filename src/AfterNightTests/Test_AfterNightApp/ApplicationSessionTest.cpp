// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The session the application owns: opened once from the stored limits with the product's codecs, lent to Controls,
// and shut down in the application's order - admission closed before the main window goes, the thread pool drained,
// the session shut down, checked, last.

#include "core/ApplicationSession.h"

#include "Test_ShellClassic/AppTestSupport.h"
#include "app/ApplicationStorage.h"
#include "controls/settings/ProcessingLimits.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRegistry.h>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QThread>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace
{

constexpr std::uint64_t kMebibyte = 1024u * 1024u;

} // namespace

// Qt's own file dialog keeps its history in a native store of its own, which on Linux is a file under the user's
// configuration directory: the cases that drive the application's file dialogs must leave it under the run's storage.
TEST(ApplicationSession, QtsOwnFileDialogStoreIsUnderTheRunsStorage)
{
#ifdef _WIN32
    GTEST_SKIP() << "Qt's native store is the registry on Windows, which no storage root redirects";
#else
    const QString root = QDir(qEnvironmentVariable(an::ui::kTestStorageRootEnvironmentVariable)).absolutePath();
    ASSERT_FALSE(root.isEmpty());
    const QSettings dialogStore(QSettings::UserScope, QStringLiteral("QtProject"));
    const QString file = QFileInfo(dialogStore.fileName()).absoluteFilePath();
    EXPECT_TRUE(file.startsWith(root + QLatin1Char('/'))) << file.toStdString() << " is outside " << root.toStdString();
#endif
}

TEST(ApplicationSession, OpensWithTheThreadsAndTheMemoryBudgetTheStoredLimitsState)
{
    an::ui::ApplicationSession session;
    ASSERT_TRUE(session.open(an::ui::ProcessingLimits{2u, 512u}).ok());

    ASSERT_TRUE(session.isOpen());
    EXPECT_EQ(session.engineSession().width(), 2u);
    EXPECT_EQ(session.engineSession().limits().memoryBudgetBytes, 512u * kMebibyte);
    EXPECT_EQ(session.engineSession().state(), an::consumer::SessionState::Available);
    EXPECT_TRUE(session.shutdown().ok());
}

TEST(ApplicationSession, StoredLimitsOfZeroLeaveTheHostsThreadsAndNoBudget)
{
    const an::consumer::EngineSessionSettings settings = an::ui::applicationSessionSettings(an::ui::ProcessingLimits{});

    EXPECT_EQ(settings.maxConcurrency, an::kUseHostConcurrency);
    EXPECT_EQ(settings.limits.memoryBudgetBytes, an::kDefaultMemoryBudgetBytes);
}

TEST(ApplicationSession, IsBuiltWithTheProductsCodecs)
{
    const an::consumer::EngineSessionSettings settings =
        an::ui::applicationSessionSettings(an::ui::ProcessingLimits{1u, 0u});

    ASSERT_TRUE(static_cast<bool>(settings.codecRegistry));
    const an::Result<std::shared_ptr<const an::CodecRegistry>> registry = settings.codecRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();
    ASSERT_TRUE(*registry != nullptr);
    EXPECT_TRUE((*registry)->capabilities(an::CodecFormat::Fits).has_value());
    EXPECT_TRUE((*registry)->capabilities(an::CodecFormat::Xisf).has_value());
    EXPECT_TRUE((*registry)->capabilities(an::CodecFormat::Tiff).has_value());
}

TEST(ApplicationSession, OpensOnceAndKeepsTheSessionItOpenedFirst)
{
    an::ui::ApplicationSession session;
    ASSERT_TRUE(session.open(an::ui::ProcessingLimits{1u, 0u}).ok());
    const std::uint64_t context = session.engineSession().contextId();

    const an::Status again = session.open(an::ui::ProcessingLimits{3u, 0u});

    EXPECT_EQ(again.code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(session.engineSession().contextId(), context);
    EXPECT_EQ(session.engineSession().width(), 1u);
    EXPECT_TRUE(session.shutdown().ok());
}

TEST(ApplicationSession, BeforeItOpensThereIsNothingToShutDown)
{
    an::ui::ApplicationSession session;

    EXPECT_FALSE(session.isOpen());
    session.closeAdmission();
    EXPECT_TRUE(session.shutdown().ok());

    bool destroyed = false;
    EXPECT_TRUE(an::ui::shutDownApplication(session, [&destroyed]() { destroyed = true; }).ok());
    EXPECT_TRUE(destroyed);
}

TEST(ApplicationSession, AdmissionClosesBeforeTheMainWindowGoesAndTheSessionShutsDownAfterIt)
{
    an::ui::ApplicationSession session;
    ASSERT_TRUE(session.open(an::ui::ProcessingLimits{2u, 0u}).ok());

    std::optional<an::consumer::SessionState> stateWhileTheWindowWent;
    an::Status refusedWhileTheWindowWent = an::Status::success();
    const an::Status shut = an::ui::shutDownApplication(session, [&]() {
        stateWhileTheWindowWent = session.engineSession().state();
        an::Result<an::consumer::OperationScope> admitted = session.engineSession().tryAdmit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        refusedWhileTheWindowWent = admitted.hasValue() ? an::Status::success() : admitted.status();
    });

    ASSERT_TRUE(stateWhileTheWindowWent.has_value());
    EXPECT_EQ(*stateWhileTheWindowWent, an::consumer::SessionState::Closed);
    EXPECT_TRUE(refusedWhileTheWindowWent.failed());
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(session.engineSession().contextId(), 0u);
}

TEST(ApplicationSession, TheShutdownWaitsForTheWorkThePoolStillHolds)
{
    an::ui::ApplicationSession session;
    ASSERT_TRUE(session.open(an::ui::ProcessingLimits{2u, 0u}).ok());

    // Work the window left on the pool, holding the context until it finishes: the checked shutdown would refuse
    // while it runs.
    an::Result<an::consumer::SessionHold> hold = session.engineSession().hold();
    ASSERT_TRUE(hold.hasValue());
    auto held = std::make_shared<std::optional<an::consumer::SessionHold>>(std::move(*hold));
    std::atomic_bool finished = false;
    QThreadPool::globalInstance()->start([held, &finished]() {
        QThread::msleep(150);
        held->reset();
        finished = true;
    });
    held.reset();

    const an::Status shut = an::ui::shutDownApplication(session, {});

    EXPECT_TRUE(finished.load());
    EXPECT_TRUE(shut.ok()) << shut.message();
}

TEST(ApplicationSession, ItsOwnShutdownWaitsForTheWorkThePoolStillHolds)
{
    an::ui::ApplicationSession session;
    ASSERT_TRUE(session.open(an::ui::ProcessingLimits{2u, 0u}).ok());

    an::Result<an::consumer::SessionHold> hold = session.engineSession().hold();
    ASSERT_TRUE(hold.hasValue());
    auto held = std::make_shared<std::optional<an::consumer::SessionHold>>(std::move(*hold));
    std::atomic_bool finished = false;
    QThreadPool::globalInstance()->start([held, &finished]() {
        QThread::msleep(150);
        held->reset();
        finished = true;
    });
    held.reset();

    const an::Status shut = session.shutdown();

    EXPECT_TRUE(finished.load());
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(session.engineSession().state(), an::consumer::SessionState::Closed);
}

TEST(ApplicationSession, AnImageStillHeldAfterTheWindowWentIsNamedByTheShutdown)
{
    an::ui::ApplicationSession session;
    ASSERT_TRUE(session.open(an::ui::ProcessingLimits{1u, 0u}).ok());
    std::optional<an::consumer::ImageAsset> kept = an::tests::app::filledAsset(session.engineSession(), 0.5f);
    ASSERT_TRUE(kept.has_value());

    const an::Status shut = an::ui::shutDownApplication(session, {});

    EXPECT_EQ(shut.code(), an::ErrorCode::Busy);
    EXPECT_EQ(session.engineSession().state(), an::consumer::SessionState::Closed);
    kept.reset();
}
