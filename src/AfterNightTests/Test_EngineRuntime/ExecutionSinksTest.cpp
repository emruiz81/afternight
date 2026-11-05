// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Progress and diagnostic tests for AfterNightEngine/Runtime/ExecutionContext.h and the two
// sink interfaces it forwards to.
//
// The sinks are the engine's only way to say anything while an operation runs, and their shape
// rules out ad hoc progress and task reporting on a type that also owns cancellation, reached from
// anywhere. Here a sink is an interface a caller implements, the execution context holds a pointer
// to it and never owns it, and the engine says nothing at all when there is none.
//
// Two properties are worth more than the forwarding. Reporting through an execution that has no
// sink is a no-op rather than a branch every algorithm writes, and every report carries the
// operation identity, so one sink can serve several executions at once and tell them apart. The
// second is what makes a sink shared by a workflow useful rather than confusing.

#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

/// A progress sink that records what it was told, from any number of threads at once.
///
/// It allocates nothing while reporting, because `onProgress` is `noexcept` and an allocation that
/// throws inside one ends the process rather than failing the test.
class RecordingProgressSink final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_count;
        m_lastSequence = report.sequence;
        m_lastCompleted = report.completed;
        m_lastTotal = report.total;
        m_lastOperationId = report.operationId;
        m_lastStage = report.stage;
        m_operationIds.insert(report.operationId);
    }

    [[nodiscard]] std::uint64_t count() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_count;
    }

    [[nodiscard]] an::ProgressReport last() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return an::ProgressReport{.operationId = m_lastOperationId,
                                  .sequence = m_lastSequence,
                                  .completed = m_lastCompleted,
                                  .total = m_lastTotal,
                                  .stage = m_lastStage};
    }

    [[nodiscard]] std::set<std::uint64_t> operationIds() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_operationIds;
    }

private:
    mutable std::mutex m_guard;
    std::uint64_t m_count = 0u;
    std::uint64_t m_lastSequence = 0u;
    std::uint64_t m_lastCompleted = 0u;
    std::uint64_t m_lastTotal = 0u;
    std::uint64_t m_lastOperationId = 0u;
    const char* m_lastStage = "";
    std::set<std::uint64_t> m_operationIds;
};

/// A diagnostic sink that keeps the last event and counts them by severity.
class RecordingDiagnosticSink final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_count;
        m_last = event;
    }

    [[nodiscard]] std::uint64_t count() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_count;
    }

    [[nodiscard]] an::DiagnosticEvent last() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_last;
    }

private:
    mutable std::mutex m_guard;
    std::uint64_t m_count = 0u;
    an::DiagnosticEvent m_last{};
};

TEST(ExecutionSinksTest, AnExecutionWithoutSinksReportsNothingAndSaysSo)
{
    an::EngineContext engine = makeEngineContext();

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    EXPECT_EQ(execution->progressSink(), nullptr);
    EXPECT_EQ(execution->diagnosticSink(), nullptr);
    EXPECT_EQ(execution->traceSink(), nullptr);

    // The reason the helpers exist: an algorithm reports unconditionally and the engine drops it,
    // rather than every algorithm carrying the same null check.
    execution->reportProgress(1u, 10u, "reading");
    execution->reportDiagnostic(an::DiagnosticSeverity::Warning, an::ErrorDomain::Runtime, "test", "nobody hears this");
    execution->reportTrace(an::TraceEventKind::Instant, "test", "nobody sees this");

    const an::OperationCounters counters = execution->operationCounters();
    EXPECT_EQ(counters.operationId, execution->operationId());
    EXPECT_EQ(counters.lastSequence, 0u);
}

TEST(ExecutionSinksTest, ProgressReachesTheSinkWithTheOperationIdentity)
{
    an::EngineContext engine = makeEngineContext();

    RecordingProgressSink sink;
    an::ExecutionOptions options;
    options.progressSink = &sink;
    options.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    EXPECT_EQ(execution->progressSink(), &sink);

    execution->reportProgress(3u, 12u, "calibrating");

    EXPECT_EQ(sink.count(), 1u);
    const an::ProgressReport report = sink.last();
    EXPECT_EQ(report.completed, 3u);
    EXPECT_EQ(report.total, 12u);
    EXPECT_EQ(report.operationId, execution->operationId());
    EXPECT_EQ(report.sequence, 1u);
    EXPECT_STREQ(report.stage, "calibrating");
}

TEST(ExecutionSinksTest, ProgressWithNoStageNamedStillReportsTheNumbers)
{
    an::EngineContext engine = makeEngineContext();

    RecordingProgressSink sink;
    an::ExecutionOptions options;
    options.progressSink = &sink;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    execution->reportProgress(7u, 7u);

    const an::ProgressReport report = sink.last();
    EXPECT_EQ(report.completed, 7u);
    EXPECT_EQ(report.total, 7u);

    // Never null, so a sink writing it into a log needs no fallback of its own.
    ASSERT_NE(report.stage, nullptr);
    EXPECT_STREQ(report.stage, "");
}

TEST(ExecutionSinksTest, DiagnosticsReachTheSinkWithSeverityDomainAndOperation)
{
    an::EngineContext engine = makeEngineContext();

    RecordingDiagnosticSink sink;
    an::ExecutionOptions options;
    options.diagnosticSink = &sink;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    EXPECT_EQ(execution->diagnosticSink(), &sink);

    execution->reportDiagnostic(an::DiagnosticSeverity::Warning, an::ErrorDomain::Image, "Stacking::normalize",
                                "a frame carried no exposure time");

    EXPECT_EQ(sink.count(), 1u);
    const an::DiagnosticEvent event = sink.last();
    EXPECT_EQ(event.severity, an::DiagnosticSeverity::Warning);
    EXPECT_EQ(event.domain, an::ErrorDomain::Image);
    EXPECT_EQ(event.operationId, execution->operationId());
    EXPECT_EQ(event.sequence, 1u);
    EXPECT_STREQ(event.operation, "Stacking::normalize");
    EXPECT_STREQ(event.message, "a frame carried no exposure time");
}

TEST(ExecutionSinksTest, EverySeverityHasItsOwnName)
{
    const an::DiagnosticSeverity severities[] = {an::DiagnosticSeverity::Trace, an::DiagnosticSeverity::Debug,
                                                 an::DiagnosticSeverity::Info, an::DiagnosticSeverity::Warning,
                                                 an::DiagnosticSeverity::Error};

    std::set<std::string> names;
    for (const an::DiagnosticSeverity severity : severities)
    {
        const char* name = an::diagnosticSeverityName(severity);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0u);
        names.insert(name);
    }
    EXPECT_EQ(names.size(), sizeof(severities) / sizeof(severities[0]));
    EXPECT_STREQ(an::diagnosticSeverityName(static_cast<an::DiagnosticSeverity>(200)), "UnknownDiagnosticSeverity");
}

TEST(ExecutionSinksTest, OneSinkServesSeveralExecutionsAndTellsThemApart)
{
    an::EngineContext engine = makeEngineContext();

    RecordingProgressSink sink;
    an::ExecutionOptions options;
    options.progressSink = &sink;
    options.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> first = an::ExecutionContext::create(engine, options);
    const an::Result<an::ExecutionContext> second = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    first->reportProgress(1u, 2u, "first");
    second->reportProgress(1u, 2u, "second");

    // The shape a workflow really has: one progress display, several operations under it. Without
    // the identity on the report the display cannot tell which of them moved.
    const std::set<std::uint64_t> ids = sink.operationIds();
    EXPECT_EQ(sink.count(), 2u);
    EXPECT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids.count(first->operationId()), 1u);
    EXPECT_EQ(ids.count(second->operationId()), 1u);
}

TEST(ExecutionSinksTest, EqualProgressReportedFromSeveralThreadsLosesNothing)
{
    an::EngineContext engine = makeEngineContext();

    RecordingProgressSink sink;
    an::ExecutionOptions options;
    options.progressSink = &sink;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    constexpr int kThreadCount = 8;
    constexpr int kReportsPerThread = 500;

    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (int worker = 0; worker < kThreadCount; ++worker)
    {
        workers.emplace_back([&execution] {
            for (int report = 1; report <= kReportsPerThread; ++report)
            {
                execution->reportProgress(1u, static_cast<std::uint64_t>(kReportsPerThread), "tiling");
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    // Equal completed counts do not move progress backwards and may carry a new stage or total, so
    // every one is delivered. The operation-local gate makes the sink's own lock unnecessary for
    // one execution, but the sink still needs it when several executions share it.
    EXPECT_EQ(sink.count(), static_cast<std::uint64_t>(kThreadCount) * kReportsPerThread);
    EXPECT_EQ(sink.last().operationId, execution->operationId());
}

TEST(ExecutionSinksTest, ASinkOutlivesTheExecutionThatReportedToIt)
{
    an::EngineContext engine = makeEngineContext();

    RecordingProgressSink sink;
    {
        an::ExecutionOptions options;
        options.progressSink = &sink;

        const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue()) << execution.status().message();
        execution->reportProgress(1u, 1u, "done");
    }

    // The execution context borrows its sinks and never owns them, so the caller's sink is intact
    // and readable afterwards - which is what lets one sink span a whole workflow.
    EXPECT_EQ(sink.count(), 1u);
    EXPECT_TRUE(engine.quiescent());
}

} // namespace
