// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The operation scope: it owns its execution, scratch arena and stop state in the order that outlives everything the
/// execution borrows, releases the execution's threads to the engine before its admission slot to the session, and
/// keeps all of that intact when it is moved.

#include "ConsumerRuntimeTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stop_token>

namespace
{

using an::consumer::EngineSession;
using an::consumer::OperationPurpose;
using an::consumer::OperationRequest;
using an::consumer::OperationScope;
using an::tests::consumer::answers;
using an::tests::consumer::eventually;
using an::tests::consumer::openSession;

class CountingProgress final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        ++reports;
        last = report.completed;
    }

    int reports = 0;
    std::uint64_t last = 0u;
};

} // namespace

TEST(OperationScopeTest, TheExecutionRunsInTheSessionsContextWithTheGrantedThreads)
{
    std::optional<EngineSession> session = openSession(3u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> scope = session->admit(OperationRequest{.purpose = OperationPurpose::Apply});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    EXPECT_TRUE(scope->valid());
    EXPECT_EQ(scope->purpose(), OperationPurpose::Apply);
    EXPECT_EQ(scope->grantedConcurrency(), 3u);
    EXPECT_TRUE(scope->execution().valid());
    EXPECT_EQ(scope->execution().engineContextId(), session->contextId());
    EXPECT_EQ(scope->engine().id(), session->contextId());
    EXPECT_EQ(scope->execution().maxConcurrency(), 3u);
    EXPECT_EQ(scope->execution().admittedConcurrency(), 3u);
    EXPECT_NE(scope->operationId(), 0u);
    EXPECT_EQ(scope->operationId(), scope->execution().operationId());
    EXPECT_EQ(session->outstanding().operations, 1u);

    scope->end();
    EXPECT_FALSE(scope->valid());
    EXPECT_EQ(scope->grantedConcurrency(), 0u);
    EXPECT_EQ(session->outstanding().operations, 0u);
    scope->end();
    EXPECT_EQ(session->outstanding().operations, 0u) << "ending twice ended twice";
}

// Ending a scope returns the execution's threads to the engine before the session's slot, so the operation the
// session admits next always finds the engine's threads free. Were the order reversed, the next execution would be
// refused by the engine as busy in the window between the two releases - a window shorter than a waiter takes to
// wake, so the hand-over is repeated, and the case fails at once only when anything slow sits between the releases.
TEST(OperationScopeTest, TheEngineHasTheThreadsBackBeforeTheNextOperationIsAdmitted)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::SessionHold> hold = session->hold();
    ASSERT_TRUE(hold.hasValue());

    for (int round = 0; round < 200; ++round)
    {
        an::Result<OperationScope> first = session->admit(OperationRequest{});
        ASSERT_TRUE(first.hasValue()) << "round " << round << ": " << first.status().message();
        std::stop_source cleanup;
        std::future<an::Result<OperationScope>> next = std::async(std::launch::async, [&session, &cleanup] {
            return session->admit(
                OperationRequest{.purpose = OperationPurpose::Batch, .stopToken = cleanup.get_token()});
        });
        ASSERT_TRUE(eventually([&session] { return session->admission().queued == 1u; })) << "round " << round;
        first->end();
        const bool answered = answers(next);
        if (!answered)
        {
            cleanup.request_stop();
        }
        ASSERT_TRUE(answered) << "round " << round;
        an::Result<OperationScope> admitted = next.get();
        ASSERT_TRUE(admitted.hasValue()) << "round " << round << ": " << admitted.status().message();
        EXPECT_EQ(admitted->execution().admittedConcurrency(), 1u);
        admitted->end();
        EXPECT_EQ(hold->engine().availableConcurrency(), 1u) << "an ended scope left threads claimed in the engine";
    }
}

TEST(OperationScopeTest, TheScratchArenaTheExecutionBorrowsLivesInTheScope)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    {
        an::Result<OperationScope> without = session->admit(OperationRequest{});
        ASSERT_TRUE(without.hasValue());
        EXPECT_EQ(without->scratch(), nullptr);
        EXPECT_EQ(without->execution().scratch(), nullptr);
    }
    an::Result<OperationScope> scope = session->admit(OperationRequest{.scratchBlockBytes = 4096u});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    ASSERT_NE(scope->scratch(), nullptr);
    EXPECT_EQ(scope->execution().scratch(), scope->scratch());
    EXPECT_EQ(scope->scratch()->contextId(), session->contextId());
    const an::Result<void*> block = scope->scratch()->allocate(256u);
    ASSERT_TRUE(block.hasValue()) << block.status().message();
    EXPECT_NE(*block, nullptr);
}

// The state lives in one allocation, so a moved scope's execution still borrows a live arena and a live stop source.
TEST(OperationScopeTest, MovingAScopeKeepsWhatTheExecutionBorrows)
{
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    std::stop_source caller;
    an::Result<OperationScope> admitted =
        session->admit(OperationRequest{.stopToken = caller.get_token(), .scratchBlockBytes = 4096u});
    ASSERT_TRUE(admitted.hasValue());
    an::ScratchArena* const arena = admitted->scratch();

    OperationScope moved = std::move(*admitted);
    EXPECT_FALSE(admitted->valid());
    ASSERT_TRUE(moved.valid());
    EXPECT_EQ(moved.scratch(), arena);
    EXPECT_EQ(moved.execution().scratch(), arena);
    EXPECT_TRUE(moved.scratch()->allocate(64u).hasValue());

    caller.request_stop();
    EXPECT_TRUE(moved.execution().stopRequested()) << "the caller's stop no longer reaches a moved scope";

    OperationScope assigned;
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(session->outstanding().operations, 1u);
}

TEST(OperationScopeTest, TheCallersStopTokenReachesTheExecution)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::stop_source caller;
    an::Result<OperationScope> scope = session->admit(OperationRequest{.stopToken = caller.get_token()});
    ASSERT_TRUE(scope.hasValue());
    EXPECT_FALSE(scope->execution().stopRequested());
    EXPECT_TRUE(scope->execution().checkCancellation().ok());

    caller.request_stop();
    EXPECT_TRUE(scope->stopRequested());
    EXPECT_TRUE(scope->execution().stopRequested());
    EXPECT_EQ(scope->execution().checkCancellation().code(), an::ErrorCode::Cancelled);
}

TEST(OperationScopeTest, StoppingTheScopeLeavesTheCallersSourceAlone)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    std::stop_source caller;
    an::Result<OperationScope> scope = session->admit(OperationRequest{.stopToken = caller.get_token()});
    ASSERT_TRUE(scope.hasValue());
    scope->requestStop();
    EXPECT_TRUE(scope->execution().stopRequested());
    EXPECT_FALSE(caller.stop_requested());
}

// The caller's source may go before the scope: the link the scope holds is to the source's shared state.
TEST(OperationScopeTest, ACallersSourceDestroyedFirstLeavesTheScopeSafe)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<OperationScope> scope = [&session] {
        std::stop_source shortLived;
        return session->admit(OperationRequest{.stopToken = shortLived.get_token()});
    }();
    ASSERT_TRUE(scope.hasValue());
    scope->requestStop();
    EXPECT_TRUE(scope->execution().stopRequested());
    scope->end();
    EXPECT_EQ(session->outstanding().operations, 0u);
}

TEST(OperationScopeTest, TheSinksAndTheDeadlineAreLentToTheExecution)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    CountingProgress progress;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    an::Result<OperationScope> scope =
        session->admit(OperationRequest{.deadline = deadline, .progressSink = &progress});
    ASSERT_TRUE(scope.hasValue());
    EXPECT_EQ(scope->execution().progressSink(), &progress);
    ASSERT_TRUE(scope->execution().deadline().has_value());
    EXPECT_EQ(*scope->execution().deadline(), deadline);
    scope->execution().reportProgress(3u, 10u, "stage");
    EXPECT_EQ(progress.reports, 1);
    EXPECT_EQ(progress.last, 3u);
}

// A scope keeps the session's state alive, so the session object may go first; the context is released with the
// scope rather than torn down under it.
TEST(OperationScopeTest, AScopeOutlivingItsSessionObjectEndsCleanly)
{
    an::Result<OperationScope> scope = [] {
        std::optional<EngineSession> session = openSession(2u);
        return session.has_value() ? session->admit(OperationRequest{})
                                   : an::Result<OperationScope>(an::Status::failure(an::ErrorCode::InvalidState));
    }();
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    EXPECT_TRUE(scope->execution().valid());
    EXPECT_TRUE(scope->execution().checkCancellation().ok());
    scope->end();
    EXPECT_FALSE(scope->valid());
}
