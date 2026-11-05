// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The engine session's public surface over its shared state, the settings check, and the hold and preview claim.

#include "SessionCore.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <new>
#include <string>
#include <utility>

namespace an::consumer
{

namespace
{

[[nodiscard]] an::Status refuseZero(const char* field)
{
    return an::Status::failure(
        an::ErrorCode::InvalidArgument,
        an::StatusDetail{.message = std::string(field) + " is zero, which admits nothing",
                         .operation = "checkSessionSettings",
                         .domain = an::ErrorDomain::Runtime,
                         .context = {an::StatusContextField::ofText(an::ContextKey::Parameter, field)}});
}

[[nodiscard]] an::Status nothingHeld(const char* operation)
{
    return an::Status::failure(an::ErrorCode::NotInitialized,
                               an::StatusDetail{.message = "this session object holds no session",
                                                .operation = operation,
                                                .domain = an::ErrorDomain::Runtime});
}

} // namespace

std::uint32_t consumerRuntimeInterfaceVersion() noexcept
{
    return kConsumerRuntimeInterfaceVersion;
}

an::Status checkSessionSettings(const EngineSessionSettings& settings)
{
    // The engine's own rules for a context's options, applied before the previous context is shut down so that a
    // mistake in them costs nothing.
    if (settings.maxConcurrency > an::kMaxConfigurableConcurrency)
    {
        return an::Status::failure(
            an::ErrorCode::LimitExceeded,
            an::StatusDetail{
                .message = "the requested concurrency is above what an engine context may be configured with",
                .operation = "checkSessionSettings",
                .domain = an::ErrorDomain::Runtime,
                .context = {an::StatusContextField::ofCount(an::ContextKey::Requested, settings.maxConcurrency),
                            an::StatusContextField::ofCount(an::ContextKey::Limit, an::kMaxConfigurableConcurrency)}});
    }
    if (settings.limits.maxImageWidth == 0u)
    {
        return refuseZero("limits.maxImageWidth");
    }
    if (settings.limits.maxImageHeight == 0u)
    {
        return refuseZero("limits.maxImageHeight");
    }
    if (settings.limits.maxChannelCount == 0u)
    {
        return refuseZero("limits.maxChannelCount");
    }
    if (settings.limits.maxImageBytes == 0u)
    {
        return refuseZero("limits.maxImageBytes");
    }
    if (settings.limits.memoryBudgetBytes == 0u)
    {
        return refuseZero("limits.memoryBudgetBytes");
    }
    if (settings.admissionQueueCapacity == 0u)
    {
        return refuseZero("admissionQueueCapacity");
    }
    return checkMemoryAllowances(settings.memory);
}

const char* sessionStateName(SessionState state) noexcept
{
    switch (state)
    {
    case SessionState::Available:
        return "available";
    case SessionState::Replacing:
        return "replacing";
    case SessionState::Unavailable:
        return "unavailable";
    case SessionState::Closed:
        return "closed";
    }
    return "unknown";
}

const char* operationPurposeName(OperationPurpose purpose) noexcept
{
    switch (purpose)
    {
    case OperationPurpose::Preview:
        return "preview";
    case OperationPurpose::Apply:
        return "apply";
    case OperationPurpose::Batch:
        return "batch";
    case OperationPurpose::WorkflowStage:
        return "workflow stage";
    case OperationPurpose::Background:
        return "background";
    }
    return "unknown";
}

// ---- Holds

SessionHold::SessionHold() noexcept = default;

SessionHold::~SessionHold()
{
    release();
}

SessionHold::SessionHold(SessionHold&& other) noexcept = default;

SessionHold& SessionHold::operator=(SessionHold&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_core = std::move(other.m_core);
        m_lease = std::move(other.m_lease);
    }
    return *this;
}

SessionHold::SessionHold(std::shared_ptr<detail::SessionCore> core, an::EngineContextLease lease) noexcept
    : m_core(std::move(core)), m_lease(std::move(lease))
{
}

bool SessionHold::valid() const noexcept
{
    return m_core != nullptr;
}

const an::EngineContext& SessionHold::engine() const noexcept
{
    return m_core->engine();
}

void SessionHold::release() noexcept
{
    if (m_core == nullptr)
    {
        return;
    }
    // The engine's lease first, so that when the session stops counting the hold the context has nothing of it.
    m_lease.release();
    m_core->releaseHold();
    m_core.reset();
}

// ---- Preview reservations

PreviewReservation::PreviewReservation() noexcept = default;

PreviewReservation::~PreviewReservation()
{
    release();
}

PreviewReservation::PreviewReservation(PreviewReservation&& other) noexcept = default;

PreviewReservation& PreviewReservation::operator=(PreviewReservation&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_core = std::move(other.m_core);
    }
    return *this;
}

PreviewReservation::PreviewReservation(std::shared_ptr<detail::SessionCore> core) noexcept : m_core(std::move(core)) {}

bool PreviewReservation::valid() const noexcept
{
    return m_core != nullptr;
}

void PreviewReservation::release() noexcept
{
    if (m_core != nullptr)
    {
        m_core->releasePreview();
        m_core.reset();
    }
}

// ---- Sessions

EngineSession::EngineSession() noexcept = default;

EngineSession::~EngineSession()
{
    if (m_core == nullptr)
    {
        return;
    }
    const an::Status closed = m_core->shutdown();
    if (closed.failed())
    {
        // Nothing is torn down under what is still held: the context is released with the last of it.
        LOG_WARNING("The engine session was destroyed with work outstanding: {}", closed.message());
    }
}

EngineSession::EngineSession(EngineSession&& other) noexcept = default;

EngineSession& EngineSession::operator=(EngineSession&& other) noexcept
{
    if (this != &other)
    {
        if (m_core != nullptr)
        {
            const an::Status closed = m_core->shutdown();
            if (closed.failed())
            {
                LOG_WARNING("The engine session was replaced with work outstanding: {}", closed.message());
            }
        }
        m_core = std::move(other.m_core);
    }
    return *this;
}

EngineSession::EngineSession(std::shared_ptr<detail::SessionCore> core) noexcept : m_core(std::move(core)) {}

an::Result<EngineSession> EngineSession::open(const EngineSessionSettings& settings)
{
    const an::Status checked = checkSessionSettings(settings);
    if (checked.failed())
    {
        return checked;
    }
    std::shared_ptr<detail::SessionCore> core;
    try
    {
        core = std::make_shared<detail::SessionCore>(settings);
    }
    catch (const std::bad_alloc&)
    {
        return an::Status::failure(an::ErrorCode::AllocationFailed);
    }
    const an::Status built = core->construct();
    if (built.failed())
    {
        return built;
    }
    return EngineSession{std::move(core)};
}

bool EngineSession::valid() const noexcept
{
    return m_core != nullptr;
}

SessionState EngineSession::state() const noexcept
{
    return m_core != nullptr ? m_core->state() : SessionState::Closed;
}

std::uint64_t EngineSession::generation() const noexcept
{
    return m_core != nullptr ? m_core->generation() : 0u;
}

std::uint64_t EngineSession::contextId() const noexcept
{
    return m_core != nullptr ? m_core->contextId() : 0u;
}

std::uint32_t EngineSession::width() const noexcept
{
    return m_core != nullptr ? m_core->width() : 0u;
}

an::EngineLimits EngineSession::limits() const noexcept
{
    return m_core != nullptr ? m_core->limits() : an::defaultEngineLimits();
}

an::Status EngineSession::lastFailure() const
{
    return m_core != nullptr ? m_core->lastFailure() : an::Status::success();
}

EngineSessionSettings EngineSession::activeSettings() const
{
    return m_core != nullptr ? m_core->activeSettings() : EngineSessionSettings{};
}

std::optional<EngineSessionSettings> EngineSession::pendingSettings() const
{
    return m_core != nullptr ? m_core->pendingSettings() : std::nullopt;
}

bool EngineSession::settingsPendingRestart() const noexcept
{
    return m_core != nullptr && m_core->settingsPending();
}

an::Status EngineSession::requestSettings(const EngineSessionSettings& settings)
{
    return m_core != nullptr ? m_core->requestSettings(settings) : nothingHeld("EngineSession::requestSettings");
}

an::Status EngineSession::applyPendingSettings()
{
    return m_core != nullptr ? m_core->applyPendingSettings() : nothingHeld("EngineSession::applyPendingSettings");
}

an::Status EngineSession::reopen()
{
    return m_core != nullptr ? m_core->reopen() : nothingHeld("EngineSession::reopen");
}

an::Result<SessionHold> EngineSession::hold() const
{
    if (m_core == nullptr)
    {
        return nothingHeld("EngineSession::hold");
    }
    return m_core->hold();
}

an::Result<OperationScope> EngineSession::admit(const OperationRequest& request) const
{
    if (m_core == nullptr)
    {
        return nothingHeld("EngineSession::admit");
    }
    return m_core->admit(request, true);
}

an::Result<OperationScope> EngineSession::tryAdmit(const OperationRequest& request) const
{
    if (m_core == nullptr)
    {
        return nothingHeld("EngineSession::tryAdmit");
    }
    return m_core->admit(request, false);
}

an::Result<PreviewReservation> EngineSession::reservePreview() const
{
    if (m_core == nullptr)
    {
        return nothingHeld("EngineSession::reservePreview");
    }
    return m_core->reservePreview();
}

SessionOutstanding EngineSession::outstanding() const
{
    return m_core != nullptr ? m_core->outstanding() : SessionOutstanding{};
}

AdmissionSnapshot EngineSession::admission() const
{
    return m_core != nullptr ? m_core->admission() : AdmissionSnapshot{};
}

RecycledMemory EngineSession::recycledMemory() const
{
    return m_core != nullptr ? m_core->recycledMemory() : RecycledMemory{};
}

ConsumerMemory& EngineSession::memory() const noexcept
{
    return m_core->memory();
}

void EngineSession::closeAdmission() noexcept
{
    if (m_core != nullptr)
    {
        m_core->closeAdmission();
    }
}

an::Status EngineSession::shutdown()
{
    return m_core != nullptr ? m_core->shutdown() : an::Status::success();
}

} // namespace an::consumer
