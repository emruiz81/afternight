// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/EngineContext.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ScratchArena.h>

#include "runtime/EngineContextState.h"
#include "runtime/OperationArena.h"

#include <cstdint>
#include <memory>
#include <new>
#include <utility>

// The lifetime rules of an engine context, in one place.
//
// Two of them are worth stating here rather than only in the header, because they are decisions
// with alternatives rather than mechanics.
//
// **Validation happens before anything is acquired, and in a fixed order.** The concurrency
// setting is answered first and each configured limit afterwards, so the same options always meet
// the same refusal. `createReplacement` runs the same validation before it touches
// the context it is replacing, which is what makes a configuration mistake cost the caller nothing.
//
// **Shutdown is checked and destruction is safe.** A context with work outstanding refuses to shut
// down, and reports it as `Busy` rather than as a generic bad state, because the caller's answer is
// to wait for its operations and ask again. Destroying such a context is a defect the engine
// cannot report - a destructor has nobody to report to - so what it does instead is survive it: the
// state is shared with the outstanding leases and goes when the last of them does.

namespace an
{
namespace
{

/// Builds the refusal for a configured limit of zero.
///
/// @param parameter which option field was zero, spelled as the caller wrote it.
/// @param operation the entry point that is refusing, for the failure's own record.
/// @return an `ErrorCode::InvalidArgument` failure naming the field.
[[nodiscard]] Status refuseZeroLimit(const char* parameter, const char* operation)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = "a configured limit of zero admits nothing at all",
                                        .operation = operation,
                                        .domain = ErrorDomain::Runtime,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Validates a caller's options and resolves the concurrency they ask for.
///
/// The one place either question is answered, so `create` and `createReplacement` cannot drift
/// apart on what they accept.
///
/// @param options what the caller stated.
/// @param operation the entry point that is validating, for the failure's own record.
/// @return the resolved maximum concurrency, which is at least one and no more than
///         `kMaxConfigurableConcurrency`, or the failure that refused the options.
[[nodiscard]] Result<std::uint32_t> validateOptions(const EngineContextOptions& options, const char* operation)
{
    if (options.maxConcurrency > kMaxConfigurableConcurrency)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message =
                             "the requested maximum concurrency is above what this engine may be configured with",
                         .operation = operation,
                         .domain = ErrorDomain::Runtime,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, options.maxConcurrency),
                                     StatusContextField::ofCount(ContextKey::Limit, kMaxConfigurableConcurrency)}});
    }

    if (options.limits.maxImageWidth == 0u)
    {
        return refuseZeroLimit("limits.maxImageWidth", operation);
    }
    if (options.limits.maxImageHeight == 0u)
    {
        return refuseZeroLimit("limits.maxImageHeight", operation);
    }
    if (options.limits.maxChannelCount == 0u)
    {
        return refuseZeroLimit("limits.maxChannelCount", operation);
    }
    if (options.limits.maxImageBytes == 0u)
    {
        return refuseZeroLimit("limits.maxImageBytes", operation);
    }
    if (options.limits.memoryBudgetBytes == 0u)
    {
        return refuseZeroLimit("limits.memoryBudgetBytes", operation);
    }

    if (options.maxConcurrency != kUseHostConcurrency)
    {
        return options.maxConcurrency;
    }

    // A caller that asked for the host's concurrency asked a question, so a machine larger than
    // this engine may be configured for is clamped rather than refused. The reported host value is
    // never zero, so the resolved maximum is never zero either.
    const std::uint32_t host = engineCapabilities().hostConcurrency;
    return host > kMaxConfigurableConcurrency ? kMaxConfigurableConcurrency : host;
}

} // namespace

Result<EngineContext> EngineContext::create(const EngineContextOptions& options)
{
    const Result<std::uint32_t> resolved = validateOptions(options, "EngineContext::create");
    if (!resolved.hasValue())
    {
        return resolved.status();
    }

    // The one allocation this entry point makes, and the only failure left after validation. It is
    // reported with the code-only failure on purpose: a translator that allocates in order to say
    // the allocator refused has nothing to say.
    try
    {
        return EngineContext{std::make_shared<EngineContextState>(
            EngineContextState::nextId(), *resolved, options.limits, options.codecRegistry, options.memoryResource)};
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<EngineContext> EngineContext::createReplacement(EngineContext& previous, const EngineContextOptions& options)
{
    const Result<std::uint32_t> resolved = validateOptions(options, "EngineContext::createReplacement");
    if (!resolved.hasValue())
    {
        return resolved.status();
    }

    Status closed = previous.shutdown();
    if (closed.failed())
    {
        // Reported in this entry point's own vocabulary, keeping what the shutdown said as the
        // cause: a report can show either level, and neither has to be recovered from a message.
        return Status::failure(ErrorCode::Busy,
                               StatusDetail{.message = "the context being replaced still has work outstanding",
                                            .operation = "EngineContext::createReplacement",
                                            .domain = ErrorDomain::Runtime},
                               std::move(closed));
    }

    return create(options);
}

std::uint64_t EngineContext::id() const noexcept
{
    return m_state != nullptr ? m_state->id() : 0u;
}

std::uint32_t EngineContext::maxConcurrency() const noexcept
{
    return m_state != nullptr ? m_state->maxConcurrency() : 0u;
}

EngineLimits EngineContext::limits() const noexcept
{
    return m_state != nullptr ? m_state->limits() : EngineLimits{};
}

EngineCapabilities EngineContext::capabilities() const noexcept
{
    return engineCapabilities();
}

MemoryResource* EngineContext::memoryResource() const noexcept
{
    return m_state != nullptr ? &m_state->memory() : nullptr;
}

const CodecRegistry* EngineContext::codecRegistry() const noexcept
{
    return m_state != nullptr ? m_state->codecRegistry() : nullptr;
}

Result<ScratchArena> ScratchArena::create(const EngineContext& engine, std::uint64_t blockBytes)
{
    if (engine.m_state == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "a scratch arena needs an engine context that holds state",
                                            .operation = "ScratchArena::create",
                                            .domain = ErrorDomain::Memory});
    }

    if (blockBytes == 0u)
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "an arena that takes no memory at a time can never serve a request",
                         .operation = "ScratchArena::create",
                         .domain = ErrorDomain::Memory,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "blockBytes")}});
    }

    const CheckedSize rounded = alignUp(CheckedSize{blockBytes}, kDefaultBufferAlignmentBytes);
    if (!rounded.isValid())
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "a block that size is not representable once aligned",
                         .operation = "ScratchArena::create",
                         .domain = ErrorDomain::Memory,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, blockBytes)}});
    }

    Result<EngineContextLease> lease = engine.acquireLease();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    MemoryResource* const resource = engine.memoryResource();
    if (resource == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized);
    }

    try
    {
        auto contextHold = std::make_shared<EngineContextLease>(std::move(*lease));
        const std::uint64_t contextId = contextHold->contextId();
        const std::uint64_t arenaId = engine.m_state->nextScratchArenaId();
        return createBound(std::move(contextHold), *resource, contextId, arenaId, rounded.valueOr(0u));
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<EngineContextLease> EngineContext::acquireLease(std::uint32_t requestedConcurrency) const
{
    if (m_state == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "this context holds nothing, so there is nothing to lease",
                                            .operation = "EngineContext::acquireLease",
                                            .domain = ErrorDomain::Runtime});
    }

    // A lease that wants no threads gets no arena, which is what keeps the cheap case cheap: a
    // caller holding a context open pays nothing for a scheduler it will not use.
    if (requestedConcurrency == 0u)
    {
        m_state->acquireLease();
        return EngineContextLease{m_state, nullptr};
    }

    // The grant is made before the hold is counted, so the arena that owns the grant is either
    // built or the tokens go straight back - there is no window where the budget has lent threads
    // to an object that does not exist.
    const std::uint32_t granted = m_state->admitConcurrency(requestedConcurrency);
    if (granted == 0u)
    {
        return Status::failure(ErrorCode::Busy,
                               StatusDetail{.message = "the context's concurrency budget is fully admitted",
                                            .operation = "EngineContext::acquireLease",
                                            .domain = ErrorDomain::Runtime});
    }

    std::shared_ptr<OperationArena> arena;
    try
    {
        arena = std::make_shared<OperationArena>(m_state, granted);
    }
    catch (const std::bad_alloc&)
    {
        m_state->releaseConcurrency(granted);
        return Status::failure(ErrorCode::AllocationFailed);
    }

    m_state->acquireLease();
    return EngineContextLease{m_state, std::move(arena)};
}

std::uint64_t EngineContext::outstandingLeases() const noexcept
{
    return m_state != nullptr ? m_state->outstandingLeases() : 0u;
}

std::uint32_t EngineContext::admittedConcurrency() const noexcept
{
    return m_state != nullptr ? m_state->admittedConcurrency() : 0u;
}

std::uint32_t EngineContext::availableConcurrency() const noexcept
{
    return m_state != nullptr ? m_state->availableConcurrency() : 0u;
}

Status EngineContext::shutdown()
{
    if (m_state == nullptr)
    {
        return Status::success();
    }

    if (m_state->outstandingLeases() != 0u)
    {
        return Status::failure(ErrorCode::Busy, StatusDetail{.message = "the context still has work outstanding",
                                                             .operation = "EngineContext::shutdown",
                                                             .domain = ErrorDomain::Runtime});
    }

    // Quiescent, so this handle holds the last reference and releasing it is what runs the state's
    // destructor. There is deliberately no separate teardown call: one release path means the
    // services cannot be shut down twice, or once through the destructor and not at all here.
    m_state.reset();
    return Status::success();
}

EngineContextLease::~EngineContextLease()
{
    release();
}

EngineContextLease& EngineContextLease::operator=(EngineContextLease&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_state = std::move(other.m_state);
        m_arena = std::move(other.m_arena);
    }
    return *this;
}

std::uint64_t EngineContextLease::contextId() const noexcept
{
    return m_state != nullptr ? m_state->id() : 0u;
}

std::uint32_t EngineContextLease::admittedConcurrency() const noexcept
{
    return m_arena != nullptr ? m_arena->concurrency() : 0u;
}

void EngineContextLease::release() noexcept
{
    if (m_state != nullptr)
    {
        // The grant goes back first - releasing the arena is what returns its threads to the budget
        // - so a context observed between the two still reports the work as outstanding, which is
        // the order that makes a shutdown check conservative rather than early.
        m_arena.reset();
        m_state->releaseLease();
        m_state.reset();
    }
}

} // namespace an
