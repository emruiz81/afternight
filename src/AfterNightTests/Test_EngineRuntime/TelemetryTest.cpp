// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for operation-local diagnostics, progress, traces, and counters.

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngineContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 8u;
    options.limits = an::defaultEngineLimits();
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

class RecordingTelemetrySink final : public an::ProgressSink, public an::DiagnosticSink, public an::TraceSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_sequences.push_back(report.sequence);
        m_completed.push_back(report.completed);
    }

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_sequences.push_back(event.sequence);
        m_lastDiagnostic = event;
    }

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_sequences.push_back(event.sequence);
        m_lastTrace = event;
    }

    [[nodiscard]] std::vector<std::uint64_t> sequences() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_sequences;
    }

    [[nodiscard]] std::vector<std::uint64_t> completed() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_completed;
    }

    [[nodiscard]] an::DiagnosticEvent lastDiagnostic() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_lastDiagnostic;
    }

    [[nodiscard]] an::TraceEvent lastTrace() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_lastTrace;
    }

private:
    mutable std::mutex m_guard;
    std::vector<std::uint64_t> m_sequences;
    std::vector<std::uint64_t> m_completed;
    an::DiagnosticEvent m_lastDiagnostic{};
    an::TraceEvent m_lastTrace{};
};

class ConcurrentCallbackProbe final : public an::ProgressSink, public an::DiagnosticSink, public an::TraceSink
{
public:
    void onProgress(const an::ProgressReport&) noexcept override
    {
        enter();
    }

    void onDiagnostic(const an::DiagnosticEvent&) noexcept override
    {
        enter();
    }

    void onTrace(const an::TraceEvent&) noexcept override
    {
        enter();
    }

    [[nodiscard]] int maxActive() const noexcept
    {
        return m_maxActive.load(std::memory_order_acquire);
    }

private:
    void enter() noexcept
    {
        const int active = m_active.fetch_add(1, std::memory_order_acq_rel) + 1;
        int previous = m_maxActive.load(std::memory_order_acquire);
        while (active > previous && !m_maxActive.compare_exchange_weak(previous, active, std::memory_order_acq_rel,
                                                                       std::memory_order_acquire))
        {
        }

        std::this_thread::sleep_for(std::chrono::microseconds(50));
        m_active.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::atomic<int> m_active{0};
    std::atomic<int> m_maxActive{0};
};

class BlockingProgressSink final : public an::ProgressSink
{
public:
    void setBlockedOperation(std::uint64_t operationId)
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_blockedOperation = operationId;
    }

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        std::unique_lock<std::mutex> held(m_guard);
        if (report.operationId == m_blockedOperation)
        {
            m_blockedEntered = true;
            m_changed.notify_all();
            m_changed.wait(held, [this] { return m_releaseBlocked; });
            return;
        }

        m_otherEntered = true;
        m_changed.notify_all();
    }

    [[nodiscard]] bool waitForBlocked()
    {
        std::unique_lock<std::mutex> held(m_guard);
        return m_changed.wait_for(held, std::chrono::seconds(2), [this] { return m_blockedEntered; });
    }

    [[nodiscard]] bool waitForOther()
    {
        std::unique_lock<std::mutex> held(m_guard);
        return m_changed.wait_for(held, std::chrono::seconds(2), [this] { return m_otherEntered; });
    }

    void releaseBlocked()
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_releaseBlocked = true;
        m_changed.notify_all();
    }

private:
    std::mutex m_guard;
    std::condition_variable m_changed;
    std::uint64_t m_blockedOperation = 0u;
    bool m_blockedEntered = false;
    bool m_otherEntered = false;
    bool m_releaseBlocked = false;
};

static_assert(noexcept(std::declval<const an::ExecutionContext&>().reportProgress(0u, 0u)));
static_assert(noexcept(std::declval<const an::ExecutionContext&>().reportDiagnostic(an::DiagnosticSeverity::Info,
                                                                                    an::ErrorDomain::Runtime,
                                                                                    "operation", "message")));
static_assert(noexcept(std::declval<const an::ExecutionContext&>().reportTrace(an::TraceEventKind::Instant, "category",
                                                                               "name")));

TEST(TelemetryTest, ProgressDiagnosticsAndTracesShareOneStableSequence)
{
    an::EngineContext engine = makeEngineContext();
    RecordingTelemetrySink sink;

    an::ExecutionOptions options;
    options.progressSink = &sink;
    options.maxConcurrency = 1u;
    options.diagnosticSink = &sink;
    options.traceSink = &sink;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    execution->reportProgress(1u, 3u, "load");
    execution->reportDiagnostic(an::DiagnosticSeverity::Info, an::ErrorDomain::Runtime, "load", "input ready");
    execution->reportTrace(an::TraceEventKind::Counter, "io", "bytes-read", 4096.0, "bytes");

    EXPECT_EQ(sink.sequences(), (std::vector<std::uint64_t>{1u, 2u, 3u}));

    const an::DiagnosticEvent diagnostic = sink.lastDiagnostic();
    EXPECT_EQ(diagnostic.operationId, execution->operationId());
    EXPECT_EQ(diagnostic.sequence, 2u);

    const an::TraceEvent trace = sink.lastTrace();
    EXPECT_EQ(trace.kind, an::TraceEventKind::Counter);
    EXPECT_EQ(trace.operationId, execution->operationId());
    EXPECT_EQ(trace.sequence, 3u);
    EXPECT_STREQ(trace.category, "io");
    EXPECT_STREQ(trace.name, "bytes-read");
    EXPECT_DOUBLE_EQ(trace.value, 4096.0);
    EXPECT_STREQ(trace.unit, "bytes");

    const an::OperationCounters counters = execution->operationCounters();
    EXPECT_EQ(counters.operationId, execution->operationId());
    EXPECT_EQ(counters.progressReports, 1u);
    EXPECT_EQ(counters.discardedProgressReports, 0u);
    EXPECT_EQ(counters.diagnosticEvents, 1u);
    EXPECT_EQ(counters.traceEvents, 1u);
    EXPECT_EQ(counters.lastSequence, 3u);
}

TEST(TelemetryTest, AProgressRegressionIsCountedAndNeverReachesTheSink)
{
    an::EngineContext engine = makeEngineContext();
    RecordingTelemetrySink sink;

    an::ExecutionOptions options;
    options.progressSink = &sink;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    execution->reportProgress(4u, 8u, "tiles");
    execution->reportProgress(3u, 8u, "tiles");
    execution->reportProgress(5u, 8u, "tiles");

    EXPECT_EQ(sink.completed(), (std::vector<std::uint64_t>{4u, 5u}));
    EXPECT_EQ(sink.sequences(), (std::vector<std::uint64_t>{1u, 2u}));

    const an::OperationCounters counters = execution->operationCounters();
    EXPECT_EQ(counters.progressReports, 2u);
    EXPECT_EQ(counters.discardedProgressReports, 1u);
    EXPECT_EQ(counters.lastSequence, 2u);
}

TEST(TelemetryTest, ConcurrentProgressIsObservedInMonotonicOrder)
{
    an::EngineContext engine = makeEngineContext();
    RecordingTelemetrySink sink;

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
            for (int report = 0; report < kReportsPerThread; ++report)
            {
                execution->reportProgress(static_cast<std::uint64_t>(report), kReportsPerThread, "tiles");
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    const std::vector<std::uint64_t> completed = sink.completed();
    ASSERT_FALSE(completed.empty());
    for (std::size_t index = 1u; index < completed.size(); ++index)
    {
        EXPECT_LE(completed[index - 1u], completed[index]);
    }
    EXPECT_EQ(completed.back(), static_cast<std::uint64_t>(kReportsPerThread - 1));

    const an::OperationCounters counters = execution->operationCounters();
    EXPECT_EQ(counters.progressReports, completed.size());
    EXPECT_EQ(counters.progressReports + counters.discardedProgressReports,
              static_cast<std::uint64_t>(kThreadCount) * kReportsPerThread);
    EXPECT_EQ(counters.lastSequence, counters.progressReports);
}

TEST(TelemetryTest, OneOperationsCallbacksNeverOverlapEachOther)
{
    an::EngineContext engine = makeEngineContext();
    ConcurrentCallbackProbe sink;

    an::ExecutionOptions options;
    options.progressSink = &sink;
    options.diagnosticSink = &sink;
    options.traceSink = &sink;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    constexpr int kThreadCount = 9;
    constexpr int kReportsPerThread = 20;
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (int worker = 0; worker < kThreadCount; ++worker)
    {
        workers.emplace_back([&execution, worker] {
            for (int report = 0; report < kReportsPerThread; ++report)
            {
                switch (worker % 3)
                {
                case 0:
                    execution->reportProgress(1u, 1u, "steady");
                    break;
                case 1:
                    execution->reportDiagnostic(an::DiagnosticSeverity::Debug, an::ErrorDomain::Runtime, "probe",
                                                "serialized");
                    break;
                case 2:
                    execution->reportTrace(an::TraceEventKind::Instant, "probe", "serialized");
                    break;
                }
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    EXPECT_EQ(sink.maxActive(), 1);
}

TEST(TelemetryTest, ABlockedCallbackDoesNotBlockAnotherOperation)
{
    an::EngineContext engine = makeEngineContext();
    BlockingProgressSink sink;

    an::ExecutionOptions options;
    options.progressSink = &sink;
    options.maxConcurrency = 1u;
    const an::Result<an::ExecutionContext> blocked = an::ExecutionContext::create(engine, options);
    const an::Result<an::ExecutionContext> other = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(blocked.hasValue()) << blocked.status().message();
    ASSERT_TRUE(other.hasValue()) << other.status().message();
    sink.setBlockedOperation(blocked->operationId());

    std::thread blockedReporter([&blocked] { blocked->reportProgress(1u, 1u, "blocked"); });
    const bool blockedEntered = sink.waitForBlocked();
    if (!blockedEntered)
    {
        sink.releaseBlocked();
        blockedReporter.join();
        FAIL() << "the blocking callback was not entered";
        return;
    }

    std::thread otherReporter([&other] { other->reportProgress(1u, 1u, "independent"); });
    const bool otherEntered = sink.waitForOther();
    sink.releaseBlocked();
    blockedReporter.join();
    otherReporter.join();

    EXPECT_TRUE(otherEntered);
}

TEST(TelemetryTest, EveryTraceKindHasItsOwnName)
{
    const an::TraceEventKind kinds[] = {an::TraceEventKind::Instant, an::TraceEventKind::SpanBegin,
                                        an::TraceEventKind::SpanEnd, an::TraceEventKind::Counter};
    for (const an::TraceEventKind kind : kinds)
    {
        const char* name = an::traceEventKindName(kind);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0u);
    }
    EXPECT_STREQ(an::traceEventKindName(static_cast<an::TraceEventKind>(200)), "UnknownTraceEventKind");
}

} // namespace
