// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the checked source handle every sequence reader is reached through.
//
// A source is marked failed before its backend is called, and is restored only when the call succeeds or
// never reached the backend, so a backend that failed part-way cannot be read again. A read at the
// declared frame count ends the stream without calling the backend, and a backend that ends before the
// declared count - or before the frame a seek promised - is refused rather than taken as the end. The
// first frame after a seek must arrive at the time the seek reported, when it reported one, and the
// ordering of timestamps starts again from it.

#include <AfterNightEngine/IO/FrameSource.h>

#include "io/sequences/SequenceInternal.h"

namespace an
{
struct FrameSource::Impl
{
    EngineContextLease lease;
    SequenceDescriptor descriptor;
    std::unique_ptr<FrameSourceBackend> backend;
    SequenceState state = SequenceState::Open;
    std::uint64_t nextIndex = 0u;
    std::optional<std::int64_t> previousTicks;
    std::optional<std::int64_t> seekTicks;
};

Result<SequencePosition> FrameSourceBackend::seek(const ExecutionContext&, const SequenceSeekRequest&)
{
    return sequenceFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedSeek);
}

FrameSource::FrameSource() noexcept = default;
FrameSource::~FrameSource()
{
    (void)close();
}
FrameSource::FrameSource(FrameSource&& other) noexcept = default;
FrameSource& FrameSource::operator=(FrameSource&& other) noexcept
{
    if (this != &other)
    {
        (void)close();
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

Result<FrameSource> FrameSource::create(const EngineContext& engine, const SequenceDescriptor& descriptor,
                                        std::unique_ptr<FrameSourceBackend> backend)
{
    return sequence_detail::guarded([&]() -> Result<FrameSource> {
        if (!backend)
            return Status::failure(ErrorCode::InvalidArgument);
        if (const auto checked = sequence_detail::checkDescription(engine, descriptor); checked.failed())
            return checked;
        auto lease = engine.acquireLease();
        if (!lease.hasValue())
            return lease.status();
        FrameSource source;
        source.m_impl = std::make_unique<Impl>(std::move(*lease), descriptor, std::move(backend));
        return source;
    });
}

Result<SequenceReadResult> FrameSource::read(const EngineContext& engine, const ExecutionContext& execution)
{
    return sequence_detail::guarded([&]() -> Result<SequenceReadResult> {
        if (!m_impl)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::Closed);
        auto& session = *m_impl;
        if (session.state == SequenceState::Failed)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::TerminalState);
        if (const auto checked = sequence_detail::checkContext(session.lease.contextId(), execution); checked.failed())
            return checked;
        if (!engine.valid() || engine.id() != session.lease.contextId())
            return Status::failure(ErrorCode::IncompatibleArguments);
        if (session.state == SequenceState::EndOfStream || session.descriptor.frameCount == session.nextIndex)
        {
            session.state = SequenceState::EndOfStream;
            return SequenceReadResult{};
        }
        if (session.nextIndex == (std::numeric_limits<std::uint64_t>::max)())
            return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
        bool entered = false;
        session.state = SequenceState::Failed;
        auto result = sequence_detail::call(
            execution, entered, false, [&] { return session.backend->read(engine, execution, session.nextIndex); });
        if (!entered)
            session.state = SequenceState::Open;
        if (!result.hasValue())
            return result.status();
        if (!result->frame)
        {
            if (session.descriptor.frameCount || session.seekTicks)
                return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::UnexpectedEnd);
            session.state = SequenceState::EndOfStream;
            return std::move(*result);
        }
        const auto& frame = *result->frame;
        if (const auto checked = sequence_detail::checkPosition(session.descriptor, frame.position, session.nextIndex,
                                                                session.previousTicks);
            checked.failed())
            return checked;
        if (session.seekTicks && session.seekTicks != frame.position.presentationTicks)
            return sequenceFailure(ErrorCode::InvalidData, SequenceFailure::InvalidPosition);
        if (const auto checked = sequence_detail::checkFrame(engine, session.descriptor, frame, true); checked.failed())
            return checked;
        if (frame.position.presentationTicks)
            session.previousTicks = frame.position.presentationTicks;
        session.seekTicks.reset();
        ++session.nextIndex;
        session.state = SequenceState::Open;
        return std::move(*result);
    });
}

Result<SequencePosition> FrameSource::seek(const ExecutionContext& execution, const SequenceSeekRequest& request)
{
    return sequence_detail::guarded([&]() -> Result<SequencePosition> {
        if (!m_impl)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::Closed);
        auto& session = *m_impl;
        if (session.state == SequenceState::Failed)
            return sequenceFailure(ErrorCode::InvalidState, SequenceFailure::TerminalState);
        if (const auto checked = sequence_detail::checkContext(session.lease.contextId(), execution); checked.failed())
            return checked;
        if (const auto checked = sequence_detail::checkSeek(session.descriptor, request); checked.failed())
            return checked;
        const auto previousState = session.state;
        bool entered = false;
        session.state = SequenceState::Failed;
        auto result =
            sequence_detail::call(execution, entered, false, [&] { return session.backend->seek(execution, request); });
        if (!entered)
            session.state = previousState;
        if (!result.hasValue())
            return result.status();
        if (const auto checked = sequence_detail::checkSeekResult(session.descriptor, request, *result);
            checked.failed())
            return checked;
        session.nextIndex = result->index;
        session.previousTicks.reset();
        session.seekTicks = result->presentationTicks;
        session.state =
            session.descriptor.frameCount == session.nextIndex ? SequenceState::EndOfStream : SequenceState::Open;
        return *result;
    });
}

Status FrameSource::close() noexcept
{
    if (!m_impl)
        return Status::success();
    auto session = std::move(m_impl);
    return sequence_detail::guarded([&] { return session->backend->close(); });
}

SequenceState FrameSource::state() const noexcept
{
    return m_impl ? m_impl->state : SequenceState::Closed;
}
const SequenceDescriptor* FrameSource::descriptor() const noexcept
{
    return m_impl ? &m_impl->descriptor : nullptr;
}
std::uint64_t FrameSource::nextIndex() const noexcept
{
    return m_impl ? m_impl->nextIndex : 0u;
}
std::uint64_t FrameSource::contextId() const noexcept
{
    return m_impl ? m_impl->lease.contextId() : 0u;
}
} // namespace an
