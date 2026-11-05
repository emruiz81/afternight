// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Cancellation and deadline tests for AfterNightEngine/Runtime/ExecutionContext.h.
//
// A raw `bool*` carried across threads for cancellation, on a type whose virtual functions have no
// virtual destructor, is two defects with names: a data race, and a lifetime the caller cannot
// reason about. This engine takes a `std::stop_token` instead, which is a shared, race-free handle
// whose stop state outlives every observer - so the cases here are as much about what the type
// cannot do as about what it does.
//
// The deadline is the other half. A caller that set a time budget and a caller that pressed stop
// want different words back, and the vocabulary already tells them apart, so the checkpoint has to
// as well.

#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <stop_token>
#include <thread>
#include <utility>

namespace
{

/// An engine context every case in this file starts from.
[[nodiscard]] an::EngineContext makeEngineContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

TEST(ExecutionCancellationTest, AnExecutionWithNoStopSourceIsNeverCancelled)
{
    an::EngineContext engine = makeEngineContext();

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    // A default-constructed token has no stop state at all, so an operation that was given no way
    // to be stopped can never observe one - which is the state most calls into the engine are in.
    EXPECT_FALSE(execution->stopToken().stop_possible());
    EXPECT_FALSE(execution->stopRequested());
    EXPECT_TRUE(execution->checkCancellation().ok());
}

TEST(ExecutionCancellationTest, AStopRequestIsObservedAndReportedAsCancellation)
{
    an::EngineContext engine = makeEngineContext();

    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    EXPECT_TRUE(execution->stopToken().stop_possible());
    EXPECT_FALSE(execution->stopRequested());
    EXPECT_TRUE(execution->checkCancellation().ok());

    source.request_stop();

    EXPECT_TRUE(execution->stopRequested());
    const an::Status cancelled = execution->checkCancellation();
    EXPECT_TRUE(cancelled.failed());
    EXPECT_EQ(cancelled.code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(cancelled.isCancellation());

    // A checkpoint runs often enough that it must cost nothing to report with, so what it returns
    // carries only its code. The operation that turns it into a described failure is the one that
    // knows what it was doing.
    EXPECT_FALSE(cancelled.hasDetail());
}

TEST(ExecutionCancellationTest, AStopRequestFromAnotherThreadIsObservedWithoutARace)
{
    an::EngineContext engine = makeEngineContext();

    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    std::thread stopper([&source] { source.request_stop(); });

    // The shape an operation's checkpoint really has: poll from the thread doing the work while
    // another thread requests the stop. With a raw bool this is a data race that happens to work;
    // with a stop token it is a defined one, and the sanitizer job is what tells the two apart.
    const std::chrono::steady_clock::time_point giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool observed = false;
    while (!observed && std::chrono::steady_clock::now() < giveUp)
    {
        observed = execution->checkCancellation().code() == an::ErrorCode::Cancelled;
        std::this_thread::yield();
    }

    stopper.join();
    EXPECT_TRUE(observed);
    EXPECT_TRUE(execution->stopRequested());
}

TEST(ExecutionCancellationTest, OneStopSourceCancelsEveryExecutionSharingIt)
{
    an::EngineContext engine = makeEngineContext();

    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> first = an::ExecutionContext::create(engine, options);
    const an::Result<an::ExecutionContext> second = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    source.request_stop();

    // What a user pressing stop on a workflow means: every operation under it stops, without the
    // workflow holding a list of them.
    EXPECT_EQ(first->checkCancellation().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(second->checkCancellation().code(), an::ErrorCode::Cancelled);
}

TEST(ExecutionCancellationTest, TheTokenIsHandedOnSoNestedWorkSharesOneStopState)
{
    an::EngineContext engine = makeEngineContext();

    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> outer = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(outer.hasValue()) << outer.status().message();

    // An operation that starts a nested one passes the token on rather than inventing a second stop
    // state, and the nested execution stops with the outer one.
    an::ExecutionOptions nestedOptions;
    nestedOptions.stopToken = outer->stopToken();
    nestedOptions.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> nested = an::ExecutionContext::create(engine, nestedOptions);
    ASSERT_TRUE(nested.hasValue()) << nested.status().message();

    source.request_stop();
    EXPECT_EQ(outer->checkCancellation().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(nested->checkCancellation().code(), an::ErrorCode::Cancelled);
}

TEST(ExecutionCancellationTest, ADeadlineAlreadyPastIsExceededImmediately)
{
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    // A deadline in the past is a legitimate request, not a mistake: it is what a caller whose
    // budget has already gone passes, and the answer is the same one it would get a moment later.
    EXPECT_TRUE(execution->deadlineExceeded());
    EXPECT_FALSE(execution->stopRequested());

    const an::Status expired = execution->checkCancellation();
    EXPECT_EQ(expired.code(), an::ErrorCode::DeadlineExceeded);
    EXPECT_TRUE(expired.isCancellation());
    EXPECT_FALSE(expired.hasDetail());
}

TEST(ExecutionCancellationTest, ADeadlineInTheFutureHasNotBeenReached)
{
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    ASSERT_TRUE(execution->deadline().has_value());
    EXPECT_FALSE(execution->deadlineExceeded());
    EXPECT_TRUE(execution->checkCancellation().ok());
}

TEST(ExecutionCancellationTest, AnExecutionWithoutADeadlineNeverExceedsOne)
{
    an::EngineContext engine = makeEngineContext();

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    EXPECT_FALSE(execution->deadline().has_value());
    EXPECT_FALSE(execution->deadlineExceeded());
}

TEST(ExecutionCancellationTest, AStopRequestOutranksAnExceededDeadline)
{
    an::EngineContext engine = makeEngineContext();

    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    ASSERT_EQ(execution->checkCancellation().code(), an::ErrorCode::DeadlineExceeded);

    source.request_stop();

    // Both are true and only one is reported. A person deciding to stop is the more informative of
    // the two, and a report that named the elapsed budget instead would send somebody looking for a
    // performance problem that is not there.
    EXPECT_TRUE(execution->deadlineExceeded());
    EXPECT_EQ(execution->checkCancellation().code(), an::ErrorCode::Cancelled);
}

TEST(ExecutionCancellationTest, CancellationSaysNothingAboutTheEngineContextItRanIn)
{
    an::EngineContext engine = makeEngineContext();

    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();

    {
        const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue()) << execution.status().message();
        source.request_stop();
        ASSERT_EQ(execution->checkCancellation().code(), an::ErrorCode::Cancelled);

        // A cancelled operation still holds its lease until it actually gives up, because the
        // engine context is not free while a stopped operation is still unwinding.
        EXPECT_EQ(engine.outstandingLeases(), 1u);
        EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    }

    EXPECT_TRUE(engine.quiescent());
    EXPECT_TRUE(engine.shutdown().ok());
}

} // namespace
