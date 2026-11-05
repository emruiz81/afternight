// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The execution scope: the consumer runtime's operation scope, and the delivery state its sinks fill on the
/// operation's threads and the GUI thread empties.
///
/// The delivery state is shared between the scope and at most one queued delivery, so a delivery that runs after the
/// scope is gone still finds what it delivers; it never refers to the session or the scope.

#include "session/ExecutionScope.h"

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <QCoreApplication>
#include <QMetaObject>

#include <deque>
#include <mutex>
#include <utility>

namespace an::ui
{

namespace
{

/// Writes one diagnostic to the application's log at its own severity.
void logDiagnostic(const an::DiagnosticEvent& event)
{
    const char* operation = event.operation != nullptr ? event.operation : "";
    const char* message = event.message != nullptr ? event.message : "";
    const char* domain = an::errorDomainName(event.domain);
    switch (event.severity)
    {
    case an::DiagnosticSeverity::Error:
        LOG_ERROR("{} ({}): {}", operation, domain, message);
        break;
    case an::DiagnosticSeverity::Warning:
        LOG_WARNING("{} ({}): {}", operation, domain, message);
        break;
    case an::DiagnosticSeverity::Info:
        LOG_INFO("{} ({}): {}", operation, domain, message);
        break;
    case an::DiagnosticSeverity::Debug:
        LOG_DEBUG("{} ({}): {}", operation, domain, message);
        break;
    case an::DiagnosticSeverity::Trace:
        LOG_TRACE("{} ({}): {}", operation, domain, message);
        break;
    }
}

/// What one scope has waiting for the GUI thread, and what became of every report it was handed.
class Delivery : public std::enable_shared_from_this<Delivery>
{
public:
    Delivery(ExecutionObserver observer, std::size_t capacity, std::optional<an::DiagnosticSeverity> logFrom)
        : m_observer(std::move(observer)), m_capacity(capacity), m_logFrom(logFrom)
    {
    }

    Delivery(const Delivery&) = delete;
    Delivery& operator=(const Delivery&) = delete;

    /// Keeps the newest progress report, owned. Called on the operation's threads.
    void progress(const an::ProgressReport& report) noexcept
    {
        bool post = false;
        try
        {
            ExecutionProgress owned{.operationId = report.operationId,
                                    .sequence = report.sequence,
                                    .completed = report.completed,
                                    .total = report.total,
                                    .stage = std::string(report.stage != nullptr ? report.stage : "")};
            std::lock_guard lock(m_mutex);
            ++m_telemetry.progressReceived;
            if (m_progress.has_value())
            {
                ++m_telemetry.progressCoalesced;
            }
            m_progress = std::move(owned);
            post = claimDelivery();
        }
        catch (...)
        {
            countLost([](ExecutionTelemetry& telemetry) {
                ++telemetry.progressReceived;
                ++telemetry.progressDropped;
            });
            return;
        }
        if (post)
        {
            queueDelivery();
        }
    }

    /// Queues a diagnostic, owned, while the queue has room; logs it when it is severe enough. Called on the
    /// operation's threads.
    void diagnostic(const an::DiagnosticEvent& event) noexcept
    {
        if (m_logFrom.has_value() && event.severity >= *m_logFrom)
        {
            try
            {
                logDiagnostic(event);
            }
            catch (...)
            {
                // The log is a convenience; losing a line of it never fails the work.
            }
        }
        bool post = false;
        try
        {
            std::lock_guard lock(m_mutex);
            ++m_telemetry.diagnosticsReceived;
            if (m_diagnostics.size() >= m_capacity)
            {
                ++m_telemetry.diagnosticsDropped;
                return;
            }
            m_diagnostics.push_back(
                ExecutionDiagnostic{.severity = event.severity,
                                    .domain = event.domain,
                                    .operationId = event.operationId,
                                    .sequence = event.sequence,
                                    .operation = std::string(event.operation != nullptr ? event.operation : ""),
                                    .message = std::string(event.message != nullptr ? event.message : "")});
            post = claimDelivery();
        }
        catch (...)
        {
            countLost([](ExecutionTelemetry& telemetry) { ++telemetry.diagnosticsDropped; });
            return;
        }
        if (post)
        {
            queueDelivery();
        }
    }

    /// States the terminal report, once. Called by the scope's owner after the operation has ended.
    void finish(std::uint64_t operationId, an::consumer::OperationPurpose purpose, an::Status status) noexcept
    {
        bool post = false;
        try
        {
            std::lock_guard lock(m_mutex);
            if (m_finished)
            {
                return;
            }
            m_finished = true;
            m_outcome = ExecutionOutcome{.operationId = operationId, .purpose = purpose, .status = std::move(status)};
            post = claimDelivery();
        }
        catch (...)
        {
            countLost([](ExecutionTelemetry& telemetry) { ++telemetry.discarded; });
            return;
        }
        if (post)
        {
            queueDelivery();
        }
    }

    /// What has become of the reports so far.
    [[nodiscard]] ExecutionTelemetry telemetry() const
    {
        std::lock_guard lock(m_mutex);
        return m_telemetry;
    }

private:
    /// Whether the caller is the one to queue a delivery: true when none is queued. Called with the mutex held.
    [[nodiscard]] bool claimDelivery() noexcept
    {
        if (m_scheduled)
        {
            return false;
        }
        m_scheduled = true;
        ++m_telemetry.deliveriesQueued;
        return true;
    }

    /// Counts a loss that happened where the mutex may not be held.
    template <typename CountT> void countLost(CountT&& count) noexcept
    {
        try
        {
            std::lock_guard lock(m_mutex);
            count(m_telemetry);
        }
        catch (...)
        {
            // Nothing is left to count with.
        }
    }

    /// Queues one delivery to the GUI thread. Everything waiting is discarded and counted when none can be queued.
    void queueDelivery() noexcept
    {
        bool posted = false;
        try
        {
            if (QCoreApplication* application = QCoreApplication::instance(); application != nullptr)
            {
                posted = QMetaObject::invokeMethod(
                    application, [self = shared_from_this()] { self->deliver(); }, Qt::QueuedConnection);
            }
        }
        catch (...)
        {
            posted = false;
        }
        if (!posted)
        {
            countLost([this](ExecutionTelemetry& telemetry) {
                telemetry.discarded +=
                    m_diagnostics.size() + (m_progress.has_value() ? 1u : 0u) + (m_outcome.has_value() ? 1u : 0u);
                m_diagnostics.clear();
                m_progress.reset();
                m_outcome.reset();
                m_scheduled = false;
                --telemetry.deliveriesQueued;
            });
        }
    }

    /// Calls one of the observer's callbacks. A callback that throws loses its own report and nothing else: the
    /// delivery goes on, because the event loop it runs in cannot carry an exception.
    template <typename CallbackT, typename ValueT>
    static void call(const CallbackT& callback, const ValueT& value) noexcept
    {
        try
        {
            callback(value);
        }
        catch (...)
        {
            try
            {
                LOG_ERROR("an execution observer's callback threw; its report is lost");
            }
            catch (...)
            {
                // Nothing is left to report with.
            }
        }
    }

    /// Whether the receiver still lives. Read on the GUI thread, where the receiver is destroyed.
    [[nodiscard]] bool receiverLives() const noexcept
    {
        return !m_observer.receiver.isNull();
    }

    /// Hands everything waiting to the observer, on the GUI thread: the diagnostics in order, the newest progress, and
    /// then the terminal report.
    void deliver() noexcept
    {
        std::deque<ExecutionDiagnostic> diagnostics;
        std::optional<ExecutionProgress> progress;
        std::optional<ExecutionOutcome> outcome;
        {
            std::lock_guard lock(m_mutex);
            diagnostics.swap(m_diagnostics);
            progress.swap(m_progress);
            outcome.swap(m_outcome);
            m_scheduled = false;
        }

        std::uint64_t diagnosticsDelivered = 0u;
        std::uint64_t progressDelivered = 0u;
        std::uint64_t discarded = 0u;
        for (const ExecutionDiagnostic& diagnostic : diagnostics)
        {
            if (receiverLives() && m_observer.diagnostic)
            {
                call(m_observer.diagnostic, diagnostic);
                ++diagnosticsDelivered;
            }
            else
            {
                ++discarded;
            }
        }
        if (progress.has_value())
        {
            if (receiverLives() && m_observer.progress)
            {
                call(m_observer.progress, *progress);
                ++progressDelivered;
            }
            else
            {
                ++discarded;
            }
        }
        const bool deliverOutcome = outcome.has_value() && receiverLives() && m_observer.finished;
        if (outcome.has_value() && !deliverOutcome)
        {
            ++discarded;
        }
        {
            std::lock_guard lock(m_mutex);
            m_telemetry.diagnosticsDelivered += diagnosticsDelivered;
            m_telemetry.progressDelivered += progressDelivered;
            m_telemetry.discarded += discarded;
            if (deliverOutcome)
            {
                outcome->telemetry = m_telemetry;
            }
        }
        if (deliverOutcome)
        {
            call(m_observer.finished, *outcome);
        }
    }

    ExecutionObserver m_observer;
    std::size_t m_capacity;
    std::optional<an::DiagnosticSeverity> m_logFrom;

    mutable std::mutex m_mutex;
    std::deque<ExecutionDiagnostic> m_diagnostics;
    std::optional<ExecutionProgress> m_progress;
    std::optional<ExecutionOutcome> m_outcome;
    ExecutionTelemetry m_telemetry{};
    bool m_scheduled = false;
    bool m_finished = false;
};

/// The progress sink the engine is given: every report goes to the scope's delivery state.
class ProgressAdapter final : public an::ProgressSink
{
public:
    explicit ProgressAdapter(Delivery& delivery) noexcept : m_delivery(delivery) {}

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        m_delivery.progress(report);
    }

private:
    Delivery& m_delivery;
};

/// The diagnostic sink the engine is given: every event goes to the scope's delivery state.
class DiagnosticAdapter final : public an::DiagnosticSink
{
public:
    explicit DiagnosticAdapter(Delivery& delivery) noexcept : m_delivery(delivery) {}

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        m_delivery.diagnostic(event);
    }

private:
    Delivery& m_delivery;
};

} // namespace

/// What a scope owns, in one allocation so that moving the scope moves nothing the engine borrows. The operation is
/// declared last, so it ends before the sinks it was given are destroyed.
class ExecutionScope::State
{
public:
    State(std::shared_ptr<Delivery> delivery, an::consumer::OperationPurpose purpose) noexcept
        : delivery(std::move(delivery)), progressSink(*this->delivery), diagnosticSink(*this->delivery),
          purpose(purpose)
    {
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    std::shared_ptr<Delivery> delivery;
    ProgressAdapter progressSink;
    DiagnosticAdapter diagnosticSink;
    an::consumer::OperationPurpose purpose;
    bool finished = false;
    const an::consumer::EngineSession* session = nullptr;
    an::consumer::OperationScope operation;
    const ImageReads* reads = nullptr;
};

ExecutionScope::ExecutionScope() noexcept = default;

ExecutionScope::~ExecutionScope()
{
    finish(an::Status::failure(an::ErrorCode::InvalidState, "the execution ended without stating its result"));
}

ExecutionScope::ExecutionScope(ExecutionScope&& other) noexcept = default;

ExecutionScope& ExecutionScope::operator=(ExecutionScope&& other) noexcept
{
    if (this != &other)
    {
        finish(an::Status::failure(an::ErrorCode::InvalidState, "the execution ended without stating its result"));
        m_state = std::move(other.m_state);
    }
    return *this;
}

ExecutionScope::ExecutionScope(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

an::Result<ExecutionScope> ExecutionScope::admitWith(const an::consumer::EngineSession& session,
                                                     const ExecutionRequest& request, ExecutionObserver observer,
                                                     bool wait)
{
    auto state = std::make_unique<State>(
        std::make_shared<Delivery>(std::move(observer), request.diagnosticCapacity, request.logFrom), request.purpose);
    an::consumer::OperationRequest operation;
    operation.purpose = request.purpose;
    operation.maxConcurrency = request.maxConcurrency;
    operation.stopToken = request.stopToken;
    operation.deadline = request.deadline;
    operation.progressSink = &state->progressSink;
    operation.diagnosticSink = &state->diagnosticSink;
    operation.scratchBlockBytes = request.scratchBlockBytes;
    an::Result<an::consumer::OperationScope> admitted = wait ? session.admit(operation) : session.tryAdmit(operation);
    if (!admitted.hasValue())
    {
        // Nothing was queued for the observer, and nothing will be: the delivery state goes with the state.
        return admitted.status();
    }
    state->session = &session;
    state->operation = std::move(*admitted);
    state->reads = request.reads;
    return ExecutionScope(std::move(state));
}

an::Result<ExecutionScope> ExecutionScope::admit(const an::consumer::EngineSession& session,
                                                 const ExecutionRequest& request, ExecutionObserver observer)
{
    return admitWith(session, request, std::move(observer), true);
}

an::Result<ExecutionScope> ExecutionScope::tryAdmit(const an::consumer::EngineSession& session,
                                                    const ExecutionRequest& request, ExecutionObserver observer)
{
    return admitWith(session, request, std::move(observer), false);
}

bool ExecutionScope::valid() const noexcept
{
    return m_state != nullptr && !m_state->finished && m_state->operation.valid();
}

an::consumer::OperationPurpose ExecutionScope::purpose() const noexcept
{
    return m_state != nullptr ? m_state->purpose : an::consumer::OperationPurpose::Apply;
}

std::uint32_t ExecutionScope::grantedConcurrency() const noexcept
{
    return valid() ? m_state->operation.grantedConcurrency() : 0u;
}

std::uint64_t ExecutionScope::operationId() const noexcept
{
    return valid() ? m_state->operation.operationId() : 0u;
}

const an::consumer::EngineSession& ExecutionScope::session() const noexcept
{
    return *m_state->session;
}

const an::EngineContext& ExecutionScope::engine() const noexcept
{
    return m_state->operation.engine();
}

const an::ExecutionContext& ExecutionScope::execution() const noexcept
{
    return m_state->operation.execution();
}

void ExecutionScope::requestStop() noexcept
{
    if (valid())
    {
        m_state->operation.requestStop();
    }
}

bool ExecutionScope::stopRequested() const noexcept
{
    return valid() && m_state->operation.stopRequested();
}

ExecutionTelemetry ExecutionScope::telemetry() const
{
    return m_state != nullptr ? m_state->delivery->telemetry() : ExecutionTelemetry{};
}

const ImageReads* ExecutionScope::reads() const noexcept
{
    return m_state != nullptr ? m_state->reads : nullptr;
}

void ExecutionScope::finish(an::Status status) noexcept
{
    if (m_state == nullptr || m_state->finished)
    {
        return;
    }
    m_state->finished = true;
    const std::uint64_t operationId = m_state->operation.operationId();
    // The operation ends first - its threads, scratch and admission slot go back to the session - so whoever the
    // terminal report wakes finds them free.
    m_state->operation.end();
    m_state->delivery->finish(operationId, m_state->purpose, std::move(status));
}

} // namespace an::ui
