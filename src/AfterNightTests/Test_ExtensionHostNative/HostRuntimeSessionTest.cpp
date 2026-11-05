// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The host's borrow of the runtime backend's session and its teardown order, in this process, and the startup and
/// teardown scenarios that load the real backend and start the interpreter, each in a process of its own.

#include "RuntimeBackendTestSupport.h"

#include "ExtensionAppContext.h"
#include "HostNativeTestSupport.h"
#include "HostRuntimeSession.h"
#include "RuntimeSessionCapsule.h"
#include "fixtures/harness/TestHarness.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <QMetaObject>
#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{

using an::ui::extensions::HostEngineSession;
using an::ui::extensions::RuntimeBackendModule;

/// A session of its own beside the run's, held as the backend's seat holds its own, and the record a capsule carries.
struct SeatSession
{
    SeatSession()
    {
        an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open({});
        EXPECT_TRUE(opened.hasValue()) << opened.status().message();
        if (opened.hasValue())
        {
            record.session = std::make_shared<an::consumer::EngineSession>(std::move(*opened));
        }
    }

    ~SeatSession()
    {
        if (record.session)
        {
            EXPECT_TRUE(record.session->shutdown().ok());
        }
    }

    an::sdk::RuntimeSessionCapsule record;
};

TEST(HostEngineSessionTest, BorrowsTheSessionACheckedCapsuleHolds)
{
    SeatSession seat;
    ASSERT_TRUE(seat.record.session);
    HostEngineSession session;
    EXPECT_FALSE(session.borrowed());
    EXPECT_EQ(session.owners(), 0);

    ASSERT_TRUE(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &seat.record).ok());
    EXPECT_TRUE(session.borrowed());
    EXPECT_EQ(&session.engineSession(), seat.record.session.get()) << "the borrow is the seat's session, not a copy";
    EXPECT_EQ(session.owners(), 2) << "the seat's record and the host's borrow";

    session.release();
    EXPECT_FALSE(session.borrowed());
    EXPECT_EQ(seat.record.session.use_count(), 1);
    session.release();
    EXPECT_FALSE(session.borrowed()) << "letting go twice lets go once";
}

TEST(HostEngineSessionTest, RefusesACapsuleThatIsNotTheBackendsSession)
{
    SeatSession seat;
    ASSERT_TRUE(seat.record.session);
    HostEngineSession session;

    EXPECT_EQ(session.borrow("afternight.another.session", &seat.record).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(session.borrow(nullptr, &seat.record).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(session.borrow(an::sdk::kRuntimeSessionCapsuleName, nullptr).code(), an::ErrorCode::NotInitialized);

    an::sdk::RuntimeSessionCapsule smaller = seat.record;
    smaller.recordBytes = static_cast<std::uint32_t>(sizeof(an::sdk::RuntimeSessionCapsule) - 8u);
    EXPECT_EQ(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &smaller).code(),
              an::ErrorCode::IncompatibleArguments);

    an::sdk::RuntimeSessionCapsule otherRuntime = seat.record;
    otherRuntime.consumerRuntimeInterfaceVersion += 1u;
    const an::Status refused = session.borrow(an::sdk::kRuntimeSessionCapsuleName, &otherRuntime);
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(refused.message()).find("consumer runtime interface"), std::string::npos)
        << refused.message();

    an::sdk::RuntimeSessionCapsule otherEngine = seat.record;
    otherEngine.engineAbiGeneration += 1;
    EXPECT_EQ(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &otherEngine).code(),
              an::ErrorCode::IncompatibleArguments);

    an::sdk::RuntimeSessionCapsule empty;
    EXPECT_EQ(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &empty).code(), an::ErrorCode::NotInitialized);

    EXPECT_FALSE(session.borrowed()) << "a refused capsule borrows nothing";
    EXPECT_EQ(seat.record.session.use_count(), 4) << "only the records the case copied hold the session";
}

TEST(HostEngineSessionTest, RefusesASessionThatNoLongerAdmitsWorkAndASecondBorrow)
{
    SeatSession seat;
    ASSERT_TRUE(seat.record.session);
    HostEngineSession session;
    ASSERT_TRUE(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &seat.record).ok());
    EXPECT_EQ(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &seat.record).code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(session.owners(), 2) << "a refused second borrow changes nothing";
    session.release();

    SeatSession shut;
    ASSERT_TRUE(shut.record.session);
    ASSERT_TRUE(shut.record.session->shutdown().ok());
    HostEngineSession late;
    EXPECT_NE(late.borrow(an::sdk::kRuntimeSessionCapsuleName, &shut.record).code(), an::ErrorCode::Ok);
    EXPECT_FALSE(late.borrowed());
}

TEST(HostEngineSessionTest, NoInterpreterNoBorrow)
{
    ASSERT_EQ(Py_IsInitialized(), 0) << "the suite's own process starts no interpreter";
    HostEngineSession session;
    EXPECT_EQ(session.borrowFromInterpreter().code(), an::ErrorCode::NotInitialized);
    EXPECT_FALSE(session.borrowed());
}

TEST(HostEngineSessionTest, ClosingAdmissionRefusesNewWork)
{
    SeatSession seat;
    ASSERT_TRUE(seat.record.session);
    HostEngineSession session;
    session.closeAdmission();
    EXPECT_FALSE(session.borrowed()) << "closing nothing borrowed does nothing";
    ASSERT_TRUE(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &seat.record).ok());
    const an::consumer::OperationRequest request{.purpose = an::consumer::OperationPurpose::Preview};
    EXPECT_TRUE(session.engineSession().tryAdmit(request).hasValue());
    session.closeAdmission();
    EXPECT_FALSE(session.engineSession().tryAdmit(request).hasValue());
}

TEST(RuntimeBackendModuleTest, NothingLoadedAnswersAsNothingLoaded)
{
    RuntimeBackendModule backend;
    EXPECT_FALSE(backend.loaded());
    EXPECT_FALSE(backend.initialized());
    EXPECT_EQ(backend.abiVersion(), 0u);
    EXPECT_EQ(backend.probe().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(backend.initialize().code(), an::ErrorCode::NotInitialized);
    EXPECT_TRUE(backend.shutdown().ok());
    EXPECT_TRUE(backend.unload().ok());
    EXPECT_TRUE(backend.lastErrorText().empty());
}

TEST(RuntimeBackendModuleTest, AMissingModuleIsRefusedByName)
{
    RuntimeBackendModule backend;
    const std::filesystem::path missing =
        std::filesystem::path(an::tests::harness::executablePath().parent_path()) / "no_such_runtime_backend.dll";
    const an::Status refused = backend.load(missing);
    EXPECT_EQ(refused.code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(refused.message()).find("no_such_runtime_backend"), std::string::npos) << refused.message();
    EXPECT_FALSE(backend.loaded());
}

TEST(RuntimeBackendModuleTest, AModuleWithoutTheExportsIsRefusedAndUnloaded)
{
    // The suite's own platform library is a shared library beside it that exports none of the backend's names.
#ifdef _WIN32
#ifdef NDEBUG
    const char* library = "AfterNightPlatform.dll";
#else
    const char* library = "AfterNightPlatform_d.dll";
#endif
#else
#ifdef NDEBUG
    const char* library = "libAfterNightPlatform.so";
#else
    const char* library = "libAfterNightPlatform_d.so";
#endif
#endif
    RuntimeBackendModule backend;
    const an::Status refused = backend.load(an::tests::harness::executablePath().parent_path() / library);
    EXPECT_EQ(refused.code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(refused.message()).find("required symbols"), std::string::npos) << refused.message();
    EXPECT_FALSE(backend.loaded());
}

TEST(RuntimeBackendModuleTest, OnlyInterfaceVersionTwoIsSpoken)
{
    EXPECT_EQ(an::ui::extensions::kRuntimeBackendAbiVersion, 2u);
    EXPECT_TRUE(an::ui::extensions::checkRuntimeBackendAbiVersion(2u).ok());
    for (const std::uint32_t other : {0u, 1u, 3u, 0xffffffffu})
    {
        const an::Status refused = an::ui::extensions::checkRuntimeBackendAbiVersion(other);
        EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments) << other;
        EXPECT_NE(std::string(refused.message()).find(std::to_string(other)), std::string::npos) << refused.message();
    }
}

TEST(HostRuntimeTeardownTest, TheWaitForThePoolDeliversWhatAWorkerQueuedToTheGuiThread)
{
    SeatSession seat;
    ASSERT_TRUE(seat.record.session);
    HostEngineSession session;
    ASSERT_TRUE(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &seat.record).ok());
    RuntimeBackendModule backend;

    // A worker that waits for the GUI thread to run something it queued, as the host's bridge does when Python asks for
    // the window: the teardown's wait for the pool has to deliver it, or neither would ever finish.
    QObject receiver;
    std::atomic<bool> delivered{false};
    QThreadPool::globalInstance()->start(
        [&]() { QMetaObject::invokeMethod(&receiver, [&]() { delivered = true; }, Qt::BlockingQueuedConnection); });
    EXPECT_TRUE(an::ui::extensions::shutDownHostRuntime(session, backend, {}).ok());
    EXPECT_TRUE(delivered.load());
}

TEST(RuntimeBackendModuleTest, TheModuleIsFoundBesideTheHost)
{
    const std::filesystem::path directory = an::tests::harness::executablePath().parent_path();
    const std::filesystem::path found = an::ui::extensions::runtimeBackendModulePath(directory);
    ASSERT_FALSE(found.empty()) << "the runtime backend is deployed beside the suite";
    EXPECT_EQ(found.parent_path(), directory.lexically_normal());
    EXPECT_TRUE(an::ui::extensions::runtimeBackendModulePath(directory / "no-such-directory").empty());
}

TEST(HostRuntimeTeardownTest, ClosesAdmissionDestroysWindowsReleasesTheBorrowThenFinalizes)
{
    SeatSession seat;
    ASSERT_TRUE(seat.record.session);
    HostEngineSession session;
    ASSERT_TRUE(session.borrow(an::sdk::kRuntimeSessionCapsuleName, &seat.record).ok());
    RuntimeBackendModule backend;

    std::vector<std::string> steps;
    an::ui::extensions::HostTeardownSteps teardown;
    teardown.destroyNativeWindows = [&]() {
        const an::consumer::OperationRequest request{.purpose = an::consumer::OperationPurpose::Preview};
        EXPECT_FALSE(seat.record.session->tryAdmit(request).hasValue()) << "admission is closed first";
        EXPECT_TRUE(session.borrowed()) << "the borrow outlives the native windows";
        steps.emplace_back("windows");
    };
    teardown.finalizeInterpreter = [&]() {
        EXPECT_FALSE(session.borrowed()) << "the borrow is let go before the interpreter is finalized";
        EXPECT_EQ(seat.record.session.use_count(), 1);
        steps.emplace_back("interpreter");
    };
    EXPECT_TRUE(an::ui::extensions::shutDownHostRuntime(session, backend, teardown).ok());
    EXPECT_EQ(steps, (std::vector<std::string>{"windows", "interpreter"}));
}

TEST(ExtensionAppContextSessionTest, LendsTheSessionTheHostBorrowedAndNoRawPolicy)
{
    an::ui::extensions::ExtensionAppContext& context = an::tests::host_native::suiteContext();
    ASSERT_NE(context.getEngineSessionProvider(), nullptr);
    EXPECT_EQ(&context.getEngineSessionProvider()->engineSession(), &an::tests::host_native::suiteSession());
    EXPECT_EQ(context.getRawOpenPolicyService(), nullptr) << "the host reads no camera file";

    an::ui::extensions::ExtensionAppContext unlent;
    EXPECT_EQ(unlent.getEngineSessionProvider(), nullptr) << "nothing is lent before the host borrows a session";
}

/// The scenarios, each run in a new process of this executable.
class HostRuntimeLifecycleTest : public ::testing::TestWithParam<const char*>
{
};

TEST_P(HostRuntimeLifecycleTest, RunsInAProcessOfItsOwn)
{
    const an::tests::runtime_backend::ScenarioRun run = an::tests::runtime_backend::runScenario(GetParam());
    EXPECT_EQ(run.exitCode, 0) << run.output;
}

INSTANTIATE_TEST_SUITE_P(Scenarios, HostRuntimeLifecycleTest,
                         ::testing::Values("the-host-borrows-the-backend-session-and-shuts-down-in-order",
                                           "a-native-owner-left-keeps-the-backend-loaded",
                                           "a-refused-capsule-stops-the-start-and-leaves-nothing-behind",
                                           "the-backend-can-be-initialized-and-shut-down-again"),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name;
                             bool upper = true;
                             for (const char* c = info.param; *c != '\0'; ++c)
                             {
                                 if (*c == '-')
                                 {
                                     upper = true;
                                     continue;
                                 }
                                 name += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(*c))) : *c;
                                 upper = false;
                             }
                             return name;
                         });

} // namespace
