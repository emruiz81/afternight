// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The execution scope: admitted by the provider's session and run in its context, with sinks that own every payload
/// before the engine's call returns, coalesce progress, bound diagnostics and count what they lose, deliver on the GUI
/// thread only while the receiver lives, and end with one terminal report after everything else - once, and also when
/// the worker never stated a result.

#include "ControlsSessionTestSupport.h"

#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

using an::consumer::OperationPurpose;
using an::tests::controls::answers;
using an::tests::controls::openProvider;
using an::tests::controls::pumpQuietly;
using an::tests::controls::pumpUntil;
using an::tests::controls::stillWaiting;
using an::ui::ExecutionDiagnostic;
using an::ui::ExecutionObserver;
using an::ui::ExecutionOutcome;
using an::ui::ExecutionProgress;
using an::ui::ExecutionRequest;
using an::ui::ExecutionScope;
using an::ui::ExecutionTelemetry;

/// Everything an observer received, in order, and whether each callback ran on the GUI thread.
struct Received
{
    std::vector<std::string> order;
    std::vector<ExecutionProgress> progress;
    std::vector<ExecutionDiagnostic> diagnostics;
    std::vector<ExecutionOutcome> outcomes;
    bool offGuiThread = false;
};

/// An observer recording into `received`, guarded by `receiver`.
[[nodiscard]] ExecutionObserver recordingObserver(QObject* receiver, const std::shared_ptr<Received>& received)
{
    const auto onGuiThread = [received] {
        if (QThread::currentThread() != QCoreApplication::instance()->thread())
        {
            received->offGuiThread = true;
        }
    };
    ExecutionObserver observer;
    observer.receiver = receiver;
    observer.progress = [received, onGuiThread](const ExecutionProgress& report) {
        onGuiThread();
        received->order.push_back("progress");
        received->progress.push_back(report);
    };
    observer.diagnostic = [received, onGuiThread](const ExecutionDiagnostic& event) {
        onGuiThread();
        received->order.push_back("diagnostic");
        received->diagnostics.push_back(event);
    };
    observer.finished = [received, onGuiThread](const ExecutionOutcome& outcome) {
        onGuiThread();
        received->order.push_back("finished");
        received->outcomes.push_back(outcome);
    };
    return observer;
}

/// A request of a purpose, logging nothing so a case's diagnostics do not fill the suite's log.
[[nodiscard]] ExecutionRequest quietRequest(OperationPurpose purpose = OperationPurpose::Apply)
{
    ExecutionRequest request;
    request.purpose = purpose;
    request.logFrom = std::nullopt;
    return request;
}

} // namespace

TEST(ExecutionScopeTest, AScopeRunsInTheProvidersSession)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(4u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    QObject receiver;
    const auto received = std::make_shared<Received>();
    an::Result<ExecutionScope> scope = ExecutionScope::tryAdmit(session, quietRequest(OperationPurpose::Preview),
                                                                recordingObserver(&receiver, received));
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    EXPECT_TRUE(scope->valid());
    EXPECT_EQ(scope->purpose(), OperationPurpose::Preview);
    EXPECT_EQ(scope->engine().id(), session.contextId());
    EXPECT_GE(scope->grantedConcurrency(), 1u);
    EXPECT_NE(scope->operationId(), 0u);
    EXPECT_EQ(scope->execution().operationId(), scope->operationId());
    EXPECT_EQ(session.outstanding().operations, 1u);
    scope->finish(an::Status::success());
    EXPECT_FALSE(scope->valid());
    EXPECT_EQ(session.outstanding().operations, 0u);
    ASSERT_TRUE(pumpUntil([&received] { return !received->outcomes.empty(); }));
    EXPECT_TRUE(received->outcomes.front().status.ok());
    EXPECT_EQ(received->outcomes.front().purpose, OperationPurpose::Preview);
}

TEST(ExecutionScopeTest, ABorrowedStringIsOwnedBeforeTheSinkReturns)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    QObject receiver;
    const auto received = std::make_shared<Received>();
    an::Result<ExecutionScope> scope =
        ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), recordingObserver(&receiver, received));
    ASSERT_TRUE(scope.hasValue());

    std::string stage = "reading the frames";
    std::string operation = "decodeImage";
    std::string message = "the exposure time was assumed";
    scope->execution().reportProgress(1u, 4u, stage.c_str());
    scope->execution().reportDiagnostic(an::DiagnosticSeverity::Warning, an::ErrorDomain::Io, operation.c_str(),
                                        message.c_str());
    // The engine is free to move on: what it lent is overwritten before anything is delivered.
    stage.assign(stage.size(), '#');
    operation.assign(operation.size(), '#');
    message.assign(message.size(), '#');
    scope->finish(an::Status::success());

    ASSERT_TRUE(pumpUntil([&received] { return !received->outcomes.empty(); }));
    ASSERT_EQ(received->progress.size(), 1u);
    EXPECT_EQ(received->progress.front().stage, "reading the frames");
    EXPECT_EQ(received->progress.front().completed, 1u);
    EXPECT_EQ(received->progress.front().total, 4u);
    ASSERT_EQ(received->diagnostics.size(), 1u);
    const ExecutionDiagnostic& diagnostic = received->diagnostics.front();
    EXPECT_EQ(diagnostic.operation, "decodeImage");
    EXPECT_EQ(diagnostic.message, "the exposure time was assumed");
    EXPECT_EQ(diagnostic.severity, an::DiagnosticSeverity::Warning);
    EXPECT_EQ(diagnostic.domain, an::ErrorDomain::Io);
    EXPECT_EQ(diagnostic.operationId, received->outcomes.front().operationId);
    EXPECT_EQ(received->progress.front().operationId, received->outcomes.front().operationId);
    EXPECT_LT(received->progress.front().sequence, diagnostic.sequence) << "the engine's sequence was not kept";
}

TEST(ExecutionScopeTest, ProgressIsCoalescedToTheNewestReport)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    QObject receiver;
    const auto received = std::make_shared<Received>();
    an::Result<ExecutionScope> scope =
        ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), recordingObserver(&receiver, received));
    ASSERT_TRUE(scope.hasValue());
    constexpr std::uint64_t kReports = 1000u;
    for (std::uint64_t completed = 1u; completed <= kReports; ++completed)
    {
        scope->execution().reportProgress(completed, kReports, "integrating");
    }
    const ExecutionTelemetry beforeDelivery = scope->telemetry();
    EXPECT_EQ(beforeDelivery.progressReceived, kReports);
    EXPECT_EQ(beforeDelivery.progressCoalesced, kReports - 1u);
    EXPECT_EQ(beforeDelivery.progressDelivered, 0u) << "something was delivered without the event loop turning";
    EXPECT_EQ(beforeDelivery.deliveriesQueued, 1u) << "a burst of reports queued more than one delivery";
    scope->finish(an::Status::success());
    ASSERT_TRUE(pumpUntil([&received] { return !received->outcomes.empty(); }));
    ASSERT_EQ(received->progress.size(), 1u) << "a burst of reports was delivered one by one";
    EXPECT_EQ(received->progress.front().completed, kReports);
    const ExecutionTelemetry& telemetry = received->outcomes.front().telemetry;
    EXPECT_EQ(telemetry.progressReceived, kReports);
    EXPECT_EQ(telemetry.progressDropped, 0u);
    EXPECT_EQ(telemetry.deliveriesQueued, 1u) << "the terminal report did not join the delivery already queued";
}

TEST(ExecutionScopeTest, DiagnosticsBeyondTheCapacityAreDroppedAndCounted)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    QObject receiver;
    const auto received = std::make_shared<Received>();
    ExecutionRequest request = quietRequest();
    request.diagnosticCapacity = 4u;
    an::Result<ExecutionScope> scope =
        ExecutionScope::tryAdmit(provider->engineSession(), request, recordingObserver(&receiver, received));
    ASSERT_TRUE(scope.hasValue());
    for (int index = 0; index < 10; ++index)
    {
        const std::string message = "diagnostic " + std::to_string(index);
        scope->execution().reportDiagnostic(an::DiagnosticSeverity::Info, an::ErrorDomain::Unspecified, "operation",
                                            message.c_str());
    }
    scope->finish(an::Status::success());
    ASSERT_TRUE(pumpUntil([&received] { return !received->outcomes.empty(); }));
    ASSERT_EQ(received->diagnostics.size(), 4u);
    for (std::size_t index = 0u; index < received->diagnostics.size(); ++index)
    {
        EXPECT_EQ(received->diagnostics[index].message, "diagnostic " + std::to_string(index))
            << "the diagnostics kept are not the first ones, in order";
    }
    const ExecutionTelemetry& telemetry = received->outcomes.front().telemetry;
    EXPECT_EQ(telemetry.diagnosticsReceived, 10u);
    EXPECT_EQ(telemetry.diagnosticsDropped, 6u);
}

TEST(ExecutionScopeTest, TheTerminalReportComesOnceAndLast)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    QObject receiver;
    const auto received = std::make_shared<Received>();
    an::Result<ExecutionScope> scope =
        ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), recordingObserver(&receiver, received));
    ASSERT_TRUE(scope.hasValue());
    scope->execution().reportProgress(1u, 2u, "first");
    scope->execution().reportDiagnostic(an::DiagnosticSeverity::Info, an::ErrorDomain::Unspecified, "operation",
                                        "between");
    scope->execution().reportProgress(2u, 2u, "second");
    scope->finish(an::Status::failure(an::ErrorCode::InvalidArgument, "the worker's own failure"));
    scope->finish(an::Status::success());
    ASSERT_TRUE(pumpUntil([&received] { return !received->outcomes.empty(); }));
    pumpQuietly();
    ASSERT_EQ(received->outcomes.size(), 1u);
    EXPECT_EQ(received->outcomes.front().status.code(), an::ErrorCode::InvalidArgument);
    ASSERT_FALSE(received->order.empty());
    EXPECT_EQ(received->order.back(), "finished");
    EXPECT_EQ(received->progress.back().completed, 2u);
    EXPECT_EQ(received->diagnostics.size(), 1u);
    EXPECT_FALSE(received->offGuiThread);
}

TEST(ExecutionScopeTest, AScopeEndedWithoutAResultSaysSo)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    QObject receiver;
    const auto received = std::make_shared<Received>();
    {
        an::Result<ExecutionScope> scope =
            ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), recordingObserver(&receiver, received));
        ASSERT_TRUE(scope.hasValue());
        ExecutionScope moved = std::move(*scope);
        EXPECT_FALSE(scope->valid());
        moved.execution().reportProgress(1u, 3u, "abandoned");
    }
    EXPECT_EQ(provider->engineSession().outstanding().operations, 0u);
    ASSERT_TRUE(pumpUntil([&received] { return !received->outcomes.empty(); }));
    pumpQuietly();
    ASSERT_EQ(received->outcomes.size(), 1u) << "a moved-from scope reported too";
    EXPECT_EQ(received->outcomes.front().status.code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(received->progress.size(), 1u);
}

TEST(ExecutionScopeTest, ADestroyedReceiverReceivesNothingAndTheLossIsCounted)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    auto receiver = std::make_unique<QObject>();
    const auto received = std::make_shared<Received>();
    an::Result<ExecutionScope> scope = ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(),
                                                                recordingObserver(receiver.get(), received));
    ASSERT_TRUE(scope.hasValue());
    scope->execution().reportProgress(1u, 2u, "before the dialog closed");
    scope->execution().reportDiagnostic(an::DiagnosticSeverity::Info, an::ErrorDomain::Unspecified, "operation",
                                        "before the dialog closed");
    receiver.reset();
    scope->finish(an::Status::success());
    pumpQuietly();
    EXPECT_TRUE(received->order.empty()) << "a destroyed dialog received a report";
    const ExecutionTelemetry telemetry = scope->telemetry();
    EXPECT_EQ(telemetry.discarded, 3u) << "the progress, the diagnostic and the terminal report were not counted";
    EXPECT_EQ(telemetry.progressDelivered, 0u);
    EXPECT_EQ(telemetry.diagnosticsDelivered, 0u);
}

// A dialog closed from inside its own progress callback - a person pressing Close while a report is shown - takes
// nothing more: the reports still queued for it and the terminal one are discarded and counted, and none of them
// reaches a receiver that is gone.
TEST(ExecutionScopeTest, AReceiverDestroyedDuringADeliveryReceivesNothingAfterIt)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    auto receiver = std::make_unique<QObject>();
    int progressCalls = 0;
    int finishedCalls = 0;
    ExecutionObserver observer;
    observer.receiver = receiver.get();
    observer.progress = [&](const ExecutionProgress&) {
        ++progressCalls;
        // The first delivery closes the dialog it is delivered to.
        receiver.reset();
    };
    observer.diagnostic = [&](const ExecutionDiagnostic&) { ADD_FAILURE() << "a diagnostic reached a gone receiver"; };
    observer.finished = [&](const ExecutionOutcome&) { ++finishedCalls; };
    an::Result<ExecutionScope> scope =
        ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), std::move(observer));
    ASSERT_TRUE(scope.hasValue());
    scope->execution().reportProgress(1u, 4u, "first");
    ASSERT_TRUE(pumpUntil([&] { return progressCalls == 1; }));
    // Reported after the dialog went.
    scope->execution().reportProgress(2u, 4u, "second");
    scope->execution().reportDiagnostic(an::DiagnosticSeverity::Info, an::ErrorDomain::Unspecified, "operation",
                                        "after the dialog closed");
    scope->finish(an::Status::success());
    pumpQuietly();
    EXPECT_EQ(progressCalls, 1) << "a report reached the receiver after it was destroyed";
    EXPECT_EQ(finishedCalls, 0) << "the terminal report reached a receiver that was gone";
    const ExecutionTelemetry telemetry = scope->telemetry();
    EXPECT_EQ(telemetry.progressDelivered, 1u);
    EXPECT_EQ(telemetry.discarded, 3u) << "the second report, the diagnostic and the terminal report were not counted";
}

TEST(ExecutionScopeTest, FinishingReturnsTheThreadsBeforeAnyoneIsTold)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    QObject receiver;
    const auto received = std::make_shared<Received>();
    an::Result<ExecutionScope> first =
        ExecutionScope::tryAdmit(session, quietRequest(), recordingObserver(&receiver, received));
    ASSERT_TRUE(first.hasValue());
    EXPECT_EQ(ExecutionScope::tryAdmit(session, quietRequest(), {}).status().code(), an::ErrorCode::Busy)
        << "a second operation ran beside the first at width one";
    first->finish(an::Status::success());
    an::Result<ExecutionScope> second = ExecutionScope::tryAdmit(session, quietRequest(), {});
    EXPECT_TRUE(second.hasValue()) << "finishing kept the admission slot: " << second.status().message();
    EXPECT_TRUE(received->outcomes.empty()) << "the terminal report ran on the worker's thread";
}

TEST(ExecutionScopeTest, AQueuedAdmissionWaitsAndAStopEndsTheWait)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    an::Result<ExecutionScope> running = ExecutionScope::tryAdmit(session, quietRequest(), {});
    ASSERT_TRUE(running.hasValue());

    std::stop_source stop;
    ExecutionRequest request = quietRequest();
    request.stopToken = stop.get_token();
    QObject receiver;
    const auto received = std::make_shared<Received>();
    std::future<an::ErrorCode> waiting = std::async(std::launch::async, [&session, request, &receiver, received] {
        return ExecutionScope::admit(session, request, recordingObserver(&receiver, received)).status().code();
    });
    EXPECT_TRUE(stillWaiting(waiting));
    stop.request_stop();
    ASSERT_TRUE(answers(waiting));
    EXPECT_EQ(waiting.get(), an::ErrorCode::Cancelled);
    pumpQuietly();
    EXPECT_TRUE(received->order.empty()) << "a refused admission delivered a report";

    std::future<bool> admitted = std::async(std::launch::async, [&session] {
        an::Result<ExecutionScope> scope = ExecutionScope::admit(session, quietRequest(), {});
        return scope.hasValue();
    });
    EXPECT_TRUE(stillWaiting(admitted));
    running->finish(an::Status::success());
    ASSERT_TRUE(answers(admitted));
    EXPECT_TRUE(admitted.get());
}

TEST(ExecutionScopeTest, ReportsFromTheOperationsThreadsArriveOnTheGuiThread)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(4u);
    ASSERT_TRUE(provider.has_value());
    QObject receiver;
    const auto received = std::make_shared<Received>();
    ExecutionRequest request = quietRequest(OperationPurpose::Batch);
    request.diagnosticCapacity = 1000u;
    an::Result<ExecutionScope> scope =
        ExecutionScope::tryAdmit(provider->engineSession(), request, recordingObserver(&receiver, received));
    ASSERT_TRUE(scope.hasValue());
    const an::ExecutionContext& execution = scope->execution();
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker)
    {
        workers.emplace_back([&execution, worker] {
            for (std::uint64_t index = 1u; index <= 100u; ++index)
            {
                execution.reportProgress(index, 100u, "frames");
                const std::string message = "worker " + std::to_string(worker);
                execution.reportDiagnostic(an::DiagnosticSeverity::Debug, an::ErrorDomain::Unspecified, "operation",
                                           message.c_str());
            }
        });
    }
    // The GUI thread keeps delivering while the workers report.
    for (int turn = 0; turn < 20; ++turn)
    {
        QCoreApplication::processEvents();
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    scope->finish(an::Status::success());
    ASSERT_TRUE(pumpUntil([&received] { return !received->outcomes.empty(); }));
    EXPECT_FALSE(received->offGuiThread);
    EXPECT_EQ(received->diagnostics.size(), 400u);
    const ExecutionTelemetry& telemetry = received->outcomes.front().telemetry;
    EXPECT_EQ(telemetry.diagnosticsReceived, 400u);
    EXPECT_EQ(telemetry.progressReceived, telemetry.progressCoalesced + received->progress.size())
        << "a progress report was neither delivered nor replaced by a newer one";
    for (std::size_t index = 1u; index < received->diagnostics.size(); ++index)
    {
        EXPECT_LT(received->diagnostics[index - 1u].sequence, received->diagnostics[index].sequence)
            << "the diagnostics arrived out of the order the engine reported them";
    }
}

TEST(ExecutionScopeTest, AnObserverWithNoCallbacksOrNoReceiverIsAllowed)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    an::Result<ExecutionScope> scope = ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), {});
    ASSERT_TRUE(scope.hasValue());
    scope->execution().reportProgress(1u, 1u, "alone");
    scope->execution().reportDiagnostic(an::DiagnosticSeverity::Error, an::ErrorDomain::Unspecified, "operation",
                                        "nobody listens");
    scope->finish(an::Status::success());
    pumpQuietly();
    const ExecutionTelemetry telemetry = scope->telemetry();
    EXPECT_EQ(telemetry.progressDelivered, 0u);
    EXPECT_EQ(telemetry.discarded, 3u);

    ExecutionScope empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.operationId(), 0u);
    EXPECT_EQ(empty.grantedConcurrency(), 0u);
    empty.finish(an::Status::success());
    empty.requestStop();
    EXPECT_FALSE(empty.stopRequested());
}

TEST(ExecutionScopeTest, StoppingTheScopeIsSeenAtTheNextCheckpoint)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::stop_source ticket;
    ExecutionRequest request = quietRequest();
    request.stopToken = ticket.get_token();
    an::Result<ExecutionScope> scope = ExecutionScope::tryAdmit(provider->engineSession(), request, {});
    ASSERT_TRUE(scope.hasValue());
    EXPECT_FALSE(scope->stopRequested());
    EXPECT_TRUE(scope->execution().checkCancellation().ok());
    ticket.request_stop();
    EXPECT_TRUE(scope->stopRequested());
    EXPECT_EQ(scope->execution().checkCancellation().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), {}).status().code(),
              an::ErrorCode::Busy)
        << "a stopped operation gave its thread back before it finished";
    scope->finish(an::Status::failure(an::ErrorCode::Cancelled));

    an::Result<ExecutionScope> own = ExecutionScope::tryAdmit(provider->engineSession(), quietRequest(), {});
    ASSERT_TRUE(own.hasValue()) << own.status().message();
    EXPECT_FALSE(own->stopRequested());
    own->requestStop();
    EXPECT_TRUE(own->stopRequested());
    EXPECT_EQ(own->execution().checkCancellation().code(), an::ErrorCode::Cancelled);
}
