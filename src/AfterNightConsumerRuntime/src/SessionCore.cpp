// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The session's shared state: construction and replacement of its context, holds, admission and shutdown.

#include "SessionCore.h"

#include "OperationScopeState.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/SystemMemory.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace an::consumer::detail
{

namespace
{

[[nodiscard]] an::Status failure(an::ErrorCode code, std::string message, const char* operation,
                                 std::vector<an::StatusContextField> context = {})
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message),
                                                      .operation = operation,
                                                      .domain = an::ErrorDomain::Runtime,
                                                      .context = std::move(context)});
}

/// One count of what is outstanding, spelled for a message: "2 image assets".
void appendCount(std::string& text, std::uint64_t count, const char* singular, const char* plural)
{
    if (count == 0u)
    {
        return;
    }
    if (!text.empty())
    {
        text += ", ";
    }
    text += std::to_string(count) + " " + (count == 1u ? singular : plural);
}

[[nodiscard]] bool knownPurpose(OperationPurpose purpose) noexcept
{
    return static_cast<std::uint8_t>(purpose) <= static_cast<std::uint8_t>(OperationPurpose::Background);
}

} // namespace

SessionCore::SessionCore(const EngineSessionSettings& settings)
    : m_active(settings), m_queueCapacity(settings.admissionQueueCapacity), m_memory(settings.memory)
{
    // Without it every context takes the platform's own path, which is correct and only slower to fault in.
    if (an::Result<RecyclingMemoryResource*> made = RecyclingMemoryResource::create(); made.hasValue())
    {
        m_recycler = *made;
        // The machine's memory never changes while the process runs, so it is asked once, here, rather than under the
        // lock at every preview reservation and release.
        try
        {
            m_physicalMemoryBytes = an::platform::querySystemMemory().totalPhysicalBytes;
        }
        catch (const std::exception&)
        {
            // A machine that does not say how much memory it has keeps nothing while no budget is set.
            m_physicalMemoryBytes = 0u;
        }
    }
}

SessionCore::~SessionCore()
{
    // The context's handle goes first. Storage taken from it may outlive this core, and the resource it was taken from
    // stays until that storage has come back.
    m_engine = an::EngineContext{};
    if (m_recycler != nullptr)
    {
        m_recycler->detach();
    }
}

an::Status SessionCore::construct()
{
    EngineSessionSettings settings;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_state = SessionState::Replacing;
        settings = m_active;
    }
    an::Result<an::EngineContext> built = build(settings);
    const std::lock_guard<std::mutex> lock(m_mutex);
    return finishLocked(std::move(built), settings, false);
}

an::Result<an::EngineContext> SessionCore::build(const EngineSessionSettings& settings)
{
    an::EngineContextOptions options;
    options.maxConcurrency = settings.maxConcurrency;
    options.limits = settings.limits;
    options.memoryResource = settings.memoryResource;
    if (options.memoryResource == nullptr && m_recycler != nullptr)
    {
        // A large block goes back to the platform on the resource's own thread exactly when the engine's own resource
        // would have sent it there: in a context with no memory budget.
        m_recycler->deferReleases(settings.limits.memoryBudgetBytes == an::kDefaultMemoryBudgetBytes);
        options.memoryResource = m_recycler;
    }
    if (settings.codecRegistry)
    {
        try
        {
            an::Result<std::shared_ptr<const an::CodecRegistry>> registry = settings.codecRegistry();
            if (!registry.hasValue())
            {
                return an::Status::failure(registry.code(),
                                           an::StatusDetail{.message = "the session's codec registry was not built",
                                                            .operation = "EngineSession",
                                                            .domain = an::ErrorDomain::Runtime},
                                           registry.status());
            }
            options.codecRegistry = std::move(*registry);
        }
        catch (const std::bad_alloc&)
        {
            return an::Status::failure(an::ErrorCode::AllocationFailed);
        }
        catch (const std::exception& error)
        {
            return failure(an::ErrorCode::ExternalFailure,
                           std::string("the session's codec registry factory threw: ") + error.what(), "EngineSession");
        }
    }
    return an::EngineContext::create(options);
}

an::Status SessionCore::finishLocked(an::Result<an::EngineContext>&& built, const EngineSessionSettings& settings,
                                     bool fromPending)
{
    if (!built.hasValue())
    {
        m_engine = an::EngineContext{};
        m_state = SessionState::Unavailable;
        m_contextId = 0u;
        m_width = 0u;
        m_limits = an::defaultEngineLimits();
        m_lastFailure = built.status();
        m_changed.notify_all();
        LOG_WARNING("The engine session is unavailable: its context could not be built: {}", m_lastFailure.message());
        return m_lastFailure;
    }
    m_engine = std::move(*built);
    m_active = settings;
    if (fromPending)
    {
        m_pending.reset();
    }
    m_queueCapacity = settings.admissionQueueCapacity;
    m_memory.setAllowances(settings.memory);
    m_contextId = m_engine.id();
    m_width = m_engine.maxConcurrency();
    m_limits = m_engine.limits();
    ++m_generation;
    m_lastFailure = an::Status::success();
    m_state = SessionState::Available;
    m_changed.notify_all();
    LOG_INFO("The engine session built context {} with {} threads (construction {})", m_contextId, m_width,
             m_generation);
    return an::Status::success();
}

// ---- State

SessionState SessionCore::state() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_closed ? SessionState::Closed : m_state;
}

std::uint64_t SessionCore::generation() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_generation;
}

std::uint64_t SessionCore::contextId() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextId;
}

std::uint32_t SessionCore::width() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_width;
}

an::EngineLimits SessionCore::limits() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_limits;
}

an::Status SessionCore::lastFailure() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastFailure;
}

EngineSessionSettings SessionCore::activeSettings() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_active;
}

std::optional<EngineSessionSettings> SessionCore::pendingSettings() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending;
}

bool SessionCore::settingsPending() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.has_value();
}

SessionOutstanding SessionCore::outstanding() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return outstandingLocked();
}

SessionOutstanding SessionCore::outstandingLocked() const
{
    SessionOutstanding outstanding;
    outstanding.holds = m_holds;
    outstanding.operations = m_operations;
    outstanding.queued = m_waiters.size();
    outstanding.assets = m_assets;
    outstanding.previewReservations = m_previewClaims;
    // The context is read only while no construction can be touching it.
    outstanding.engineLeases = m_state == SessionState::Available ? m_engine.outstandingLeases() : 0u;
    return outstanding;
}

AdmissionSnapshot SessionCore::admission() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    AdmissionSnapshot snapshot;
    snapshot.width = m_width;
    snapshot.previewReservation = activeReservationLocked();
    snapshot.previewThreads = m_previewThreads;
    snapshot.otherThreads = m_otherThreads;
    snapshot.queued = m_waiters.size();
    snapshot.admitted = m_admitted;
    snapshot.refusedBusy = m_refusedBusy;
    snapshot.cancelledWhileQueued = m_cancelledWhileQueued;
    snapshot.expiredWhileQueued = m_expiredWhileQueued;
    return snapshot;
}

an::Status SessionCore::refusalForStateLocked(const char* operation) const
{
    if (m_closed)
    {
        return failure(an::ErrorCode::InvalidState, "the session has closed admission", operation);
    }
    switch (m_state)
    {
    case SessionState::Available:
        return an::Status::success();
    case SessionState::Replacing:
        return failure(an::ErrorCode::Busy, "the session is building its engine context", operation);
    case SessionState::Unavailable:
        return an::Status::failure(
            an::ErrorCode::NotInitialized,
            an::StatusDetail{.message = "the session holds no engine context: its last construction failed",
                             .operation = operation,
                             .domain = an::ErrorDomain::Runtime},
            m_lastFailure);
    case SessionState::Closed:
        break;
    }
    return failure(an::ErrorCode::InvalidState, "the session has closed admission", operation);
}

an::Status SessionCore::busyWith(const SessionOutstanding& outstanding, const char* operation) const
{
    std::string held;
    appendCount(held, outstanding.assets, "image asset", "image assets");
    appendCount(held, outstanding.holds, "hold", "holds");
    appendCount(held, outstanding.operations, "operation", "operations");
    appendCount(held, outstanding.queued, "queued request", "queued requests");
    appendCount(held, outstanding.previewReservations, "preview reservation", "preview reservations");
    const std::uint64_t total = outstanding.assets + outstanding.holds + outstanding.operations + outstanding.queued +
                                outstanding.previewReservations;
    return failure(an::ErrorCode::Busy, "the session still has " + held + " outstanding", operation,
                   {an::StatusContextField::ofCount(an::ContextKey::Total, total)});
}

// ---- Settings

an::Status SessionCore::requestSettings(const EngineSessionSettings& settings)
{
    const an::Status checked = checkSessionSettings(settings);
    if (checked.failed())
    {
        return checked;
    }
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed)
    {
        return failure(an::ErrorCode::InvalidState, "the session has closed admission",
                       "EngineSession::requestSettings");
    }
    if (m_state == SessionState::Replacing)
    {
        return failure(an::ErrorCode::Busy, "the session is building its engine context",
                       "EngineSession::requestSettings");
    }
    m_pending = settings;
    LOG_INFO("Engine session settings recorded for the next construction: {} threads", settings.maxConcurrency);
    return an::Status::success();
}

an::Status SessionCore::applyPendingSettings()
{
    static constexpr const char* kOperation = "EngineSession::applyPendingSettings";
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_closed)
    {
        return failure(an::ErrorCode::InvalidState, "the session has closed admission", kOperation);
    }
    if (m_state == SessionState::Replacing)
    {
        return failure(an::ErrorCode::Busy, "the session is building its engine context", kOperation);
    }
    if (!m_pending.has_value())
    {
        return an::Status::success();
    }
    if (m_state == SessionState::Available)
    {
        const SessionOutstanding outstanding = outstandingLocked();
        if (!outstanding.none())
        {
            return busyWith(outstanding, kOperation);
        }
        // Shut down before building, so two contexts are never held at once. The engine refuses while anything taken
        // from the context is outstanding that the session does not count - storage created through a hold and kept
        // after it - and then nothing has changed.
        an::Status closed = m_engine.shutdown();
        if (closed.failed())
        {
            return an::Status::failure(
                an::ErrorCode::Busy,
                an::StatusDetail{.message = "the engine context still has storage or leases outstanding",
                                 .operation = kOperation,
                                 .domain = an::ErrorDomain::Runtime},
                std::move(closed));
        }
        m_contextId = 0u;
        m_width = 0u;
    }
    m_state = SessionState::Replacing;
    const EngineSessionSettings next = *m_pending;
    lock.unlock();
    an::Result<an::EngineContext> built = build(next);
    lock.lock();
    return finishLocked(std::move(built), next, true);
}

an::Status SessionCore::reopen()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_closed)
    {
        return failure(an::ErrorCode::InvalidState, "the session has closed admission", "EngineSession::reopen");
    }
    if (m_state != SessionState::Unavailable)
    {
        return failure(an::ErrorCode::InvalidState, "only an unavailable session is reopened", "EngineSession::reopen");
    }
    const bool fromPending = m_pending.has_value();
    const EngineSessionSettings next = fromPending ? *m_pending : m_active;
    m_state = SessionState::Replacing;
    lock.unlock();
    an::Result<an::EngineContext> built = build(next);
    lock.lock();
    return finishLocked(std::move(built), next, fromPending);
}

void SessionCore::closeAdmission() noexcept
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    m_changed.notify_all();
}

an::Status SessionCore::shutdown()
{
    static constexpr const char* kOperation = "EngineSession::shutdown";
    std::unique_lock<std::mutex> lock(m_mutex);
    m_closed = true;
    m_changed.notify_all();
    // Waiting requests leave as soon as they see admission closed; nothing else holds them.
    m_changed.wait(lock, [this] { return m_waiters.empty(); });
    if (m_state == SessionState::Replacing)
    {
        return failure(an::ErrorCode::Busy, "the session is building its engine context", kOperation);
    }
    if (!m_engine.valid())
    {
        return an::Status::success();
    }
    const SessionOutstanding outstanding = outstandingLocked();
    if (!outstanding.none())
    {
        return busyWith(outstanding, kOperation);
    }
    an::Status closed = m_engine.shutdown();
    if (closed.failed())
    {
        return an::Status::failure(
            an::ErrorCode::Busy,
            an::StatusDetail{.message = "the engine context still has storage or leases outstanding",
                             .operation = kOperation,
                             .domain = an::ErrorDomain::Runtime},
            std::move(closed));
    }
    m_contextId = 0u;
    m_width = 0u;
    LOG_INFO("The engine session shut its context down");
    return an::Status::success();
}

// ---- Holds and preview reservations

an::Result<SessionHold> SessionCore::hold()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        const an::Status refused = refusalForStateLocked("EngineSession::hold");
        if (refused.failed())
        {
            return refused;
        }
        ++m_holds;
    }
    // Read outside the lock: the hold just counted keeps the context from being replaced.
    an::Result<an::EngineContextLease> lease = m_engine.acquireLease(0u);
    if (!lease.hasValue())
    {
        releaseHold();
        return lease.status();
    }
    return SessionHold{shared_from_this(), std::move(*lease)};
}

void SessionCore::releaseHold() noexcept
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_holds -= m_holds > 0u ? 1u : 0u;
    }
    m_changed.notify_all();
}

an::Result<PreviewReservation> SessionCore::reservePreview()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        const an::Status refused = refusalForStateLocked("EngineSession::reservePreview");
        if (refused.failed())
        {
            return refused;
        }
        ++m_previewClaims;
    }
    // While a preview dialog is open, the large blocks its frames give back are kept for the next frame.
    keepBlocksForPreviews();
    return PreviewReservation{shared_from_this()};
}

void SessionCore::releasePreview() noexcept
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_previewClaims -= m_previewClaims > 0u ? 1u : 0u;
    }
    // With the last dialog gone, what its frames kept goes back to the platform.
    keepBlocksForPreviews();
    // The reservation may have ended, which frees threads for everything else.
    m_changed.notify_all();
}

void SessionCore::keepBlocksForPreviews() noexcept
{
    if (m_recycler == nullptr)
    {
        return;
    }
    // The claims are read under this second lock, so a reservation and a release on two threads leave the resource
    // told what the claims are once both have finished, whichever reaches it last.
    const std::lock_guard<std::mutex> keeping(m_keeping);
    std::uint64_t bound = 0u;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        bound = m_previewClaims > 0u ? recycledBoundLocked() : 0u;
    }
    m_recycler->keepBlocks(bound);
}

std::uint64_t SessionCore::recycledBoundLocked() const noexcept
{
    const std::uint64_t basis = m_limits.memoryBudgetBytes == an::kDefaultMemoryBudgetBytes
                                    ? m_physicalMemoryBytes
                                    : m_limits.memoryBudgetBytes;
    return basis / 8u;
}

RecycledMemory SessionCore::recycledMemory() const noexcept
{
    if (m_recycler == nullptr)
    {
        return RecycledMemory{};
    }
    const RecycledBlocks blocks = m_recycler->recycled();
    return RecycledMemory{.keptBlocks = blocks.keptBlocks,
                          .keptBytes = blocks.keptBytes,
                          .reusedBlocks = blocks.reusedBlocks,
                          .releasedLater = blocks.releasedLater};
}

// ---- Assets

an::Status SessionCore::registerAsset(std::uint64_t contextId, const char* operation)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    const an::Status refused = refusalForStateLocked(operation);
    if (refused.failed())
    {
        return refused;
    }
    if (contextId != m_contextId)
    {
        return failure(an::ErrorCode::IncompatibleArguments,
                       "the image's storage was taken from another engine context than the session's", operation,
                       {an::StatusContextField::ofText(an::ContextKey::Parameter, "image")});
    }
    ++m_assets;
    return an::Status::success();
}

void SessionCore::releaseAsset() noexcept
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_assets -= m_assets > 0u ? 1u : 0u;
    }
    m_changed.notify_all();
}

// ---- Admission

AdmissionPool SessionCore::poolOf(OperationPurpose purpose) const noexcept
{
    // At width one there is one share and one queue, so every operation is serialized in arrival order.
    return m_width > 1u && purpose == OperationPurpose::Preview ? AdmissionPool::Preview : AdmissionPool::Other;
}

std::uint32_t SessionCore::activeReservationLocked() const noexcept
{
    if (m_width <= 1u)
    {
        return 0u;
    }
    const bool previewing =
        m_previewClaims > 0u || m_previewOperations > 0u || poolWaitingLocked(AdmissionPool::Preview);
    return previewing ? previewReservationWidth(m_width) : 0u;
}

std::uint32_t SessionCore::availableLocked(AdmissionPool pool) const noexcept
{
    const std::uint32_t used = m_previewThreads + m_otherThreads;
    const std::uint32_t free = m_width > used ? m_width - used : 0u;
    if (m_width <= 1u)
    {
        return free;
    }
    if (pool == AdmissionPool::Preview)
    {
        // The reservation is what a preview is sure of. Once other work waits, previews are held to it, so the waiting
        // work is granted its share as soon as the threads previews borrowed come back. While none waits, a preview
        // borrows every free thread but one, which stays for the work that never waits.
        const std::uint32_t reservation = previewReservationWidth(m_width);
        const std::uint32_t room = reservation > m_previewThreads ? reservation - m_previewThreads : 0u;
        const std::uint32_t sure = (std::min)(room, free);
        if (poolWaitingLocked(AdmissionPool::Other))
        {
            return sure;
        }
        return (std::max)(sure, free > 0u ? free - 1u : 0u);
    }
    const std::uint32_t reserved = activeReservationLocked();
    const std::uint32_t share = m_width > reserved ? m_width - reserved : 0u;
    const std::uint32_t room = share > m_otherThreads ? share - m_otherThreads : 0u;
    return (std::min)(room, free);
}

bool SessionCore::firstOfPoolLocked(WaiterList::const_iterator waiter) const noexcept
{
    for (auto ahead = m_waiters.cbegin(); ahead != waiter; ++ahead)
    {
        if (ahead->pool == waiter->pool)
        {
            return false;
        }
    }
    return true;
}

bool SessionCore::poolWaitingLocked(AdmissionPool pool) const noexcept
{
    return std::any_of(m_waiters.cbegin(), m_waiters.cend(),
                       [pool](const Waiter& waiter) { return waiter.pool == pool; });
}

std::uint32_t SessionCore::grantLocked(const OperationRequest& request, AdmissionPool pool) const noexcept
{
    // What a pool may take is the pool's availability; the width is only the most anything asks for.
    std::uint32_t cap = m_width;
    if (request.purpose == OperationPurpose::Background)
    {
        cap = 1u;
    }
    const std::uint32_t wanted = request.maxConcurrency == 0u ? cap : (std::min)(request.maxConcurrency, cap);
    return (std::max)(1u, (std::min)(wanted, availableLocked(pool)));
}

bool SessionCore::narrowedByBorrowedLocked(const OperationRequest& request, AdmissionPool pool) const noexcept
{
    const std::uint32_t reservation = previewReservationWidth(m_width);
    if (pool != AdmissionPool::Other || m_width <= 1u || m_previewThreads <= reservation)
    {
        return false;
    }
    // What the request is granted now, against what it is granted once the borrowed threads are back.
    const std::uint32_t borrowed = m_previewThreads - reservation;
    const std::uint32_t used = m_previewThreads + m_otherThreads;
    const std::uint32_t free = m_width > used ? m_width - used : 0u;
    const std::uint32_t reserved = activeReservationLocked();
    const std::uint32_t share = m_width > reserved ? m_width - reserved : 0u;
    const std::uint32_t room = share > m_otherThreads ? share - m_otherThreads : 0u;
    const std::uint32_t wanted = request.maxConcurrency == 0u ? m_width : (std::min)(request.maxConcurrency, m_width);
    return (std::min)({wanted, room, free}) < (std::min)({wanted, room, free + borrowed});
}

an::Result<OperationScope> SessionCore::admit(const OperationRequest& request, bool wait)
{
    const char* operation = wait ? "EngineSession::admit" : "EngineSession::tryAdmit";
    if (!knownPurpose(request.purpose))
    {
        return failure(an::ErrorCode::InvalidArgument, "the purpose is not one of the operation purposes", operation,
                       {an::StatusContextField::ofText(an::ContextKey::Parameter, "purpose")});
    }
    if (request.stopToken.stop_requested())
    {
        return failure(an::ErrorCode::Cancelled, "the request was stopped before it was admitted", operation);
    }
    // Background work is admitted only when a thread is free, so it never waits.
    const bool waits = wait && request.purpose != OperationPurpose::Background;

    std::unique_lock<std::mutex> lock(m_mutex);
    an::Status refused = refusalForStateLocked(operation);
    if (refused.failed())
    {
        return refused;
    }
    const AdmissionPool pool = poolOf(request.purpose);
    // A grant is never widened, so a request that can wait waits for the threads a preview borrowed - they come back
    // when it ends, and previews are held to the reservation meanwhile - rather than start narrower than its share.
    const auto grantable = [this, &request, pool, waits] {
        return availableLocked(pool) > 0u && !(waits && narrowedByBorrowedLocked(request, pool));
    };
    if (poolWaitingLocked(pool) || !grantable())
    {
        if (!waits)
        {
            ++m_refusedBusy;
            return failure(
                an::ErrorCode::Busy, "no thread is free for the request's purpose now", operation,
                {an::StatusContextField::ofText(an::ContextKey::Parameter, operationPurposeName(request.purpose)),
                 an::StatusContextField::ofCount(an::ContextKey::Available, availableLocked(pool))});
        }
        if (m_waiters.size() >= m_queueCapacity)
        {
            ++m_refusedBusy;
            return failure(an::ErrorCode::Busy, "the admission queue is full", operation,
                           {an::StatusContextField::ofCount(an::ContextKey::Limit, m_queueCapacity)});
        }
        const WaiterList::iterator waiter = m_waiters.insert(m_waiters.end(), Waiter{pool});
        // A preview that starts waiting activates the reservation, which can change what everything else may take.
        m_changed.notify_all();
        const auto admissible = [this, waiter, &grantable] {
            return m_closed || m_state != SessionState::Available || (firstOfPoolLocked(waiter) && grantable());
        };
        const bool ready = request.deadline.has_value()
                               ? m_changed.wait_until(lock, request.stopToken, *request.deadline, admissible)
                               : m_changed.wait(lock, request.stopToken, admissible);
        m_waiters.erase(waiter);
        m_changed.notify_all();
        if (!ready)
        {
            if (request.stopToken.stop_requested())
            {
                ++m_cancelledWhileQueued;
                return failure(an::ErrorCode::Cancelled, "the request was stopped while it waited for admission",
                               operation);
            }
            ++m_expiredWhileQueued;
            return failure(an::ErrorCode::DeadlineExceeded, "the request's deadline passed while it waited", operation);
        }
        refused = refusalForStateLocked(operation);
        if (refused.failed())
        {
            return refused;
        }
    }
    const std::uint32_t granted = grantLocked(request, pool);
    if (pool == AdmissionPool::Preview)
    {
        m_previewThreads += granted;
        ++m_previewOperations;
    }
    else
    {
        m_otherThreads += granted;
    }
    ++m_operations;
    ++m_admitted;
    lock.unlock();

    // From here the admitted slot is counted, so the context is not replaced; a failure releases the slot through the
    // state's own release order.
    std::unique_ptr<OperationScope::State> state;
    try
    {
        state = std::make_unique<OperationScope::State>(shared_from_this(), request.purpose, pool, granted);
        if (request.stopToken.stop_possible())
        {
            state->link.emplace(request.stopToken, ForwardStop{&state->stopSource});
        }
    }
    catch (const std::bad_alloc&)
    {
        if (state == nullptr)
        {
            releaseOperation(pool, granted);
        }
        return an::Status::failure(an::ErrorCode::AllocationFailed);
    }
    if (request.scratchBlockBytes != 0u)
    {
        an::Result<an::ScratchArena> scratch = an::ScratchArena::create(m_engine, request.scratchBlockBytes);
        if (!scratch.hasValue())
        {
            return scratch.status();
        }
        state->scratch.emplace(std::move(*scratch));
    }
    an::ExecutionOptions options;
    options.stopToken = state->stopSource.get_token();
    options.deadline = request.deadline;
    options.progressSink = request.progressSink;
    options.diagnosticSink = request.diagnosticSink;
    options.traceSink = request.traceSink;
    options.scratch = state->scratch.has_value() ? &*state->scratch : nullptr;
    options.maxConcurrency = granted;
    options.backend = request.backend;
    options.instructionSet = request.instructionSet;
    options.backendFallback = request.backendFallback;
    options.reductionDeterminism = request.reductionDeterminism;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    state->execution = std::move(*execution);
    return OperationScope{std::move(state)};
}

void SessionCore::releaseOperation(AdmissionPool pool, std::uint32_t granted) noexcept
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (pool == AdmissionPool::Preview)
        {
            m_previewThreads -= (std::min)(m_previewThreads, granted);
            m_previewOperations -= m_previewOperations > 0u ? 1u : 0u;
        }
        else
        {
            m_otherThreads -= (std::min)(m_otherThreads, granted);
        }
        m_operations -= m_operations > 0u ? 1u : 0u;
    }
    m_changed.notify_all();
}

} // namespace an::consumer::detail
