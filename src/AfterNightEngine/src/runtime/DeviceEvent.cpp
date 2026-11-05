// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/DeviceEvent.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "runtime/DeviceEventState.h"

#include <stop_token>

// The public face of a device event. Everything is answered by the record behind the handle; an event
// that observes nothing answers as though its work had finished with nothing to report, so a caller
// waiting on one is never left waiting.

namespace an
{

const char* deviceWorkPhaseName(DeviceWorkPhase phase) noexcept
{
    switch (phase)
    {
    case DeviceWorkPhase::Waiting:
        return "Waiting";
    case DeviceWorkPhase::Queued:
        return "Queued";
    case DeviceWorkPhase::Running:
        return "Running";
    case DeviceWorkPhase::Finished:
        return "Finished";
    }
    return "UnknownDeviceWorkPhase";
}

std::uint64_t DeviceEvent::id() const noexcept
{
    return m_state != nullptr ? m_state->id() : 0u;
}

std::uint64_t DeviceEvent::deviceId() const noexcept
{
    return m_state != nullptr ? m_state->deviceId() : 0u;
}

DeviceWorkPhase DeviceEvent::phase() const noexcept
{
    return m_state != nullptr ? m_state->phase() : DeviceWorkPhase::Finished;
}

std::optional<Status> DeviceEvent::outcome() const noexcept
{
    if (m_state == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized);
    }
    return m_state->outcome();
}

Status DeviceEvent::wait() const noexcept
{
    if (m_state == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized);
    }
    return m_state->wait(std::stop_token{}, std::nullopt);
}

Status DeviceEvent::wait(const ExecutionContext& execution) const noexcept
{
    if (m_state == nullptr || !execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized);
    }
    return m_state->wait(execution.stopToken(), execution.deadline());
}

DeviceWorkTiming DeviceEvent::timing() const noexcept
{
    return m_state != nullptr ? m_state->timing() : DeviceWorkTiming{};
}

} // namespace an
