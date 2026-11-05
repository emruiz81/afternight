// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the checked sink handle every sequence writer is reached through.
//
// A sink is marked failed before its backend is called, and is restored only when the call succeeds or
// never reached the backend: a backend that failed after it started has left its output in an unknown
// state, so every later append is refused. The backend's report is held to what it must be - one more
// frame per append, the same frames at `finish`, and never fewer bytes - and `finish` refuses a sink
// that wrote fewer frames than its descriptor declared. Closing or destroying an unfinished sink closes
// its backend; every sink this engine creates then discards the output it was writing.

#include <AfterNightEngine/IO/FrameSink.h>

#include "io/sequences/SequenceInternal.h"

namespace an
{
struct FrameSink::Impl
{
    EngineContextLease lease;
    SequenceDescriptor descriptor;
    std::unique_ptr<FrameSinkBackend> backend;
    SequenceState state = SequenceState::Open;
    SequenceWriteResult progress;
    std::optional<std::int64_t> previousTicks;
};

FrameSink::FrameSink() noexcept = default;
FrameSink::~FrameSink()
{
    (void)close();
}
FrameSink::FrameSink(FrameSink&& other) noexcept = default;
FrameSink& FrameSink::operator=(FrameSink&& other) noexcept
{
    if (this != &other)
    {
        (void)close();
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

Result<FrameSink> FrameSink::create(const EngineContext& engine, const SequenceDescriptor& descriptor,
                                    std::unique_ptr<FrameSinkBackend> backend)
{
    return sequence_detail::guarded([&]() -> Result<FrameSink> {
        if (!backend)
            return Status::failure(ErrorCode::InvalidArgument);
        if (const auto checked = sequence_detail::checkDescription(engine, descriptor); checked.failed())
            return checked;
        if (descriptor.seekByIndex || descriptor.seekByTime)
            return sequenceFailure(ErrorCode::InvalidArgument, SequenceFailure::InvalidDescriptor);
        auto lease = engine.acquireLease();
        if (!lease.hasValue())
            return lease.status();
        FrameSink sink;
        sink.m_impl = std::make_unique<Impl>(std::move(*lease), descriptor, std::move(backend));
        return sink;
    });
}

Result<SequenceWriteResult> FrameSink::append(const EngineContext& engine, const ExecutionContext& execution,
                                              const SequenceFrame& frame)
{
    return sequence_detail::guarded([&]() -> Result<SequenceWriteResult> {
        if (!m_impl)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::Closed);
        auto& session = *m_impl;
        if (session.state != SequenceState::Open)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::TerminalState);
        if (const auto checked = sequence_detail::checkContext(session.lease.contextId(), execution); checked.failed())
            return checked;
        if (!engine.valid() || engine.id() != session.lease.contextId())
            return Status::failure(ErrorCode::IncompatibleArguments);
        if (const auto checked = sequence_detail::checkPosition(session.descriptor, frame.position,
                                                                session.progress.framesWritten, session.previousTicks);
            checked.failed())
            return checked;
        if (const auto checked = sequence_detail::checkFrame(engine, session.descriptor, frame, false);
            checked.failed())
            return checked;
        bool entered = false;
        session.state = SequenceState::Failed;
        auto result = sequence_detail::call(execution, entered, false,
                                            [&] { return session.backend->append(engine, execution, frame); });
        if (!entered)
            session.state = SequenceState::Open;
        if (!result.hasValue())
            return result.status();
        if (result->framesWritten != session.progress.framesWritten + 1u ||
            result->bytesWritten < session.progress.bytesWritten)
            return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::InvalidReport);
        session.progress = *result;
        if (frame.position.presentationTicks)
            session.previousTicks = frame.position.presentationTicks;
        session.state = SequenceState::Open;
        return *result;
    });
}

Result<SequenceWriteResult> FrameSink::finish(const ExecutionContext& execution)
{
    return sequence_detail::guarded([&]() -> Result<SequenceWriteResult> {
        if (!m_impl)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::Closed);
        auto& session = *m_impl;
        // Even an idempotent completion must come from the owning context.
        if (!execution.valid())
            return Status::failure(ErrorCode::NotInitialized,
                                   {.message = "finishing a sequence needs a valid execution",
                                    .operation = "an::FrameSink::finish",
                                    .domain = ErrorDomain::Io});
        if (session.lease.contextId() != execution.engineContextId())
            return Status::failure(ErrorCode::IncompatibleArguments);
        if (session.state == SequenceState::Finalized)
            return session.progress;
        if (session.state != SequenceState::Open)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::TerminalState);
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (session.descriptor.frameCount && *session.descriptor.frameCount != session.progress.framesWritten)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::UnexpectedEnd);
        bool entered = false;
        session.state = SequenceState::Failed;
        auto result =
            sequence_detail::call(execution, entered, true, [&] { return session.backend->finish(execution); });
        if (!entered)
            session.state = SequenceState::Open;
        if (!result.hasValue())
            return result.status();
        if (result->framesWritten != session.progress.framesWritten ||
            result->bytesWritten < session.progress.bytesWritten)
            return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::InvalidReport);
        session.progress = *result;
        session.state = SequenceState::Finalized;
        return *result;
    });
}

Status FrameSink::close() noexcept
{
    if (!m_impl)
        return Status::success();
    auto session = std::move(m_impl);
    return sequence_detail::guarded([&] { return session->backend->close(); });
}
SequenceState FrameSink::state() const noexcept
{
    return m_impl ? m_impl->state : SequenceState::Closed;
}
const SequenceDescriptor* FrameSink::descriptor() const noexcept
{
    return m_impl ? &m_impl->descriptor : nullptr;
}
SequenceWriteResult FrameSink::progress() const noexcept
{
    return m_impl ? m_impl->progress : SequenceWriteResult{};
}
std::uint64_t FrameSink::contextId() const noexcept
{
    return m_impl ? m_impl->lease.contextId() : 0u;
}
} // namespace an
