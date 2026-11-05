// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The operation scope's members over the state the session built when it admitted the operation.

#include "OperationScopeState.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <utility>

namespace an::consumer
{

OperationScope::OperationScope() noexcept = default;

OperationScope::~OperationScope() = default;

OperationScope::OperationScope(OperationScope&& other) noexcept = default;

OperationScope& OperationScope::operator=(OperationScope&& other) noexcept
{
    if (this != &other)
    {
        end();
        m_state = std::move(other.m_state);
    }
    return *this;
}

OperationScope::OperationScope(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

bool OperationScope::valid() const noexcept
{
    return m_state != nullptr && m_state->core != nullptr;
}

OperationPurpose OperationScope::purpose() const noexcept
{
    return m_state != nullptr ? m_state->purpose : OperationPurpose::Apply;
}

std::uint32_t OperationScope::grantedConcurrency() const noexcept
{
    return m_state != nullptr ? m_state->granted : 0u;
}

const an::EngineContext& OperationScope::engine() const noexcept
{
    return m_state->core->engine();
}

const an::ExecutionContext& OperationScope::execution() const noexcept
{
    return m_state->execution;
}

an::ScratchArena* OperationScope::scratch() const noexcept
{
    return m_state != nullptr && m_state->scratch.has_value() ? &*m_state->scratch : nullptr;
}

std::uint64_t OperationScope::operationId() const noexcept
{
    return m_state != nullptr ? m_state->execution.operationId() : 0u;
}

void OperationScope::requestStop() noexcept
{
    if (m_state != nullptr)
    {
        m_state->stopSource.request_stop();
    }
}

bool OperationScope::stopRequested() const noexcept
{
    return m_state != nullptr && m_state->stopSource.stop_requested();
}

void OperationScope::end() noexcept
{
    if (m_state != nullptr)
    {
        m_state->end();
        m_state.reset();
    }
}

} // namespace an::consumer
