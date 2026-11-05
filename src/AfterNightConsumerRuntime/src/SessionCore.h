// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The state a session shares with everything it hands out: its engine context, its settings, its admission queue,
/// its outstanding counts, its memory accountant and the resource its context takes its bytes from. Private to the
/// library.
///
/// One mutex guards every field but the context itself. The context is touched outside the lock only while the state
/// is `Replacing`, which nothing else may observe as available, and is read under the lock or under something that
/// keeps the session from replacing it - a hold, a scope or an asset. A second one only orders what the recycling
/// resource is told to keep.

#pragma once

#include "RecyclingMemoryResource.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <optional>

namespace an::consumer::detail
{

/// Which share of the width an operation is admitted from. At width one there is a single share.
enum class AdmissionPool : std::uint8_t
{
    Preview,
    Other
};

/// The session's shared state.
class SessionCore : public std::enable_shared_from_this<SessionCore>
{
public:
    /// A core that holds no context yet; `construct` builds the first.
    ///
    /// @param settings the checked settings the first context is built from.
    explicit SessionCore(const EngineSessionSettings& settings);

    SessionCore(const SessionCore&) = delete;
    SessionCore& operator=(const SessionCore&) = delete;
    SessionCore(SessionCore&&) = delete;
    SessionCore& operator=(SessionCore&&) = delete;

    /// Lets the context go, then the recycling resource, which outlives every block the context still holds.
    ~SessionCore();

    /// Builds the first context.
    ///
    /// @return the construction's failure, or success.
    [[nodiscard]] an::Status construct();

    [[nodiscard]] SessionState state() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::uint64_t contextId() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] an::EngineLimits limits() const noexcept;
    [[nodiscard]] an::Status lastFailure() const;
    [[nodiscard]] EngineSessionSettings activeSettings() const;
    [[nodiscard]] std::optional<EngineSessionSettings> pendingSettings() const;
    [[nodiscard]] bool settingsPending() const noexcept;
    [[nodiscard]] SessionOutstanding outstanding() const;
    [[nodiscard]] AdmissionSnapshot admission() const;

    [[nodiscard]] an::Status requestSettings(const EngineSessionSettings& settings);
    [[nodiscard]] an::Status applyPendingSettings();
    [[nodiscard]] an::Status reopen();
    void closeAdmission() noexcept;
    [[nodiscard]] an::Status shutdown();

    [[nodiscard]] an::Result<SessionHold> hold();
    void releaseHold() noexcept;

    [[nodiscard]] an::Result<OperationScope> admit(const OperationRequest& request, bool wait);
    void releaseOperation(AdmissionPool pool, std::uint32_t granted) noexcept;

    [[nodiscard]] an::Result<PreviewReservation> reservePreview();
    void releasePreview() noexcept;

    /// What the recycling resource keeps and has reused; nothing when the context takes its bytes from a supplied
    /// resource.
    [[nodiscard]] RecycledMemory recycledMemory() const noexcept;

    /// Counts a new asset, if its storage came from the context held now.
    ///
    /// @param contextId the engine identity of the context the asset's storage came from.
    /// @param operation the entry point asking, for a refusal's record.
    [[nodiscard]] an::Status registerAsset(std::uint64_t contextId, const char* operation);
    void releaseAsset() noexcept;

    /// The context, for a holder of something that keeps it from being replaced.
    [[nodiscard]] const an::EngineContext& engine() const noexcept
    {
        return m_engine;
    }

    /// The memory accountant.
    [[nodiscard]] ConsumerMemory& memory() noexcept
    {
        return m_memory;
    }

private:
    /// One request waiting for admission.
    struct Waiter
    {
        AdmissionPool pool = AdmissionPool::Other;
    };

    using WaiterList = std::list<Waiter>;

    /// Why the session cannot hand anything out now, or success. Called with the lock held.
    [[nodiscard]] an::Status refusalForStateLocked(const char* operation) const;
    [[nodiscard]] SessionOutstanding outstandingLocked() const;
    [[nodiscard]] an::Status busyWith(const SessionOutstanding& outstanding, const char* operation) const;

    /// The pool an operation is admitted from at the current width.
    [[nodiscard]] AdmissionPool poolOf(OperationPurpose purpose) const noexcept;

    /// The threads reserved for previews now. Called with the lock held.
    [[nodiscard]] std::uint32_t activeReservationLocked() const noexcept;

    /// The threads a pool may be granted now. Called with the lock held.
    [[nodiscard]] std::uint32_t availableLocked(AdmissionPool pool) const noexcept;

    /// Whether a waiter is the first of its pool. Called with the lock held.
    [[nodiscard]] bool firstOfPoolLocked(WaiterList::const_iterator waiter) const noexcept;

    /// Whether a pool has a waiter. Called with the lock held.
    [[nodiscard]] bool poolWaitingLocked(AdmissionPool pool) const noexcept;

    /// The threads a request would be granted from what its pool has now. Called with the lock held.
    [[nodiscard]] std::uint32_t grantLocked(const OperationRequest& request, AdmissionPool pool) const noexcept;

    /// Whether a request would be granted fewer threads now than once the threads previews borrowed are back. Called
    /// with the lock held.
    [[nodiscard]] bool narrowedByBorrowedLocked(const OperationRequest& request, AdmissionPool pool) const noexcept;

    /// Builds a context from settings, the codec registry first, over the recycling resource unless the settings supply
    /// one of their own. Called with the state `Replacing`, without the lock.
    [[nodiscard]] an::Result<an::EngineContext> build(const EngineSessionSettings& settings);

    /// The most bytes the recycling resource keeps while a preview is open: an eighth of the memory budget, or of the
    /// machine's memory, as it was read when the core was made, when no budget is set. Called with the lock held.
    [[nodiscard]] std::uint64_t recycledBoundLocked() const noexcept;

    /// Tells the recycling resource to keep large blocks while any preview is reserved, up to `recycledBoundLocked`,
    /// and to return them all when none is. Called without the lock.
    void keepBlocksForPreviews() noexcept;

    /// Records a construction's outcome. Called with the lock held.
    [[nodiscard]] an::Status finishLocked(an::Result<an::EngineContext>&& built, const EngineSessionSettings& settings,
                                          bool fromPending);

    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;

    an::EngineContext m_engine;
    SessionState m_state = SessionState::Unavailable;
    bool m_closed = false;
    EngineSessionSettings m_active;
    std::optional<EngineSessionSettings> m_pending;
    an::Status m_lastFailure;

    std::uint64_t m_generation = 0u;
    std::uint64_t m_contextId = 0u;
    std::uint32_t m_width = 0u;
    an::EngineLimits m_limits = an::defaultEngineLimits();
    std::uint32_t m_queueCapacity = kDefaultAdmissionQueueCapacity;

    std::uint64_t m_holds = 0u;
    std::uint64_t m_operations = 0u;
    std::uint64_t m_assets = 0u;
    std::uint64_t m_previewClaims = 0u;
    std::uint64_t m_previewOperations = 0u;
    std::uint32_t m_previewThreads = 0u;
    std::uint32_t m_otherThreads = 0u;
    WaiterList m_waiters;

    std::uint64_t m_admitted = 0u;
    std::uint64_t m_refusedBusy = 0u;
    std::uint64_t m_cancelledWhileQueued = 0u;
    std::uint64_t m_expiredWhileQueued = 0u;

    ConsumerMemory m_memory;

    /// Where every context the session builds takes its bytes from, unless the settings supply a resource; null when
    /// it could not be made, and the platform's own path is taken. Detached, never destroyed, by this core.
    RecyclingMemoryResource* m_recycler = nullptr;

    /// The machine's physical memory, read once when the core is made and the resource with it, outside the lock: the
    /// basis of what the resource keeps when no budget is set. Zero when the machine does not say, or with no resource.
    std::uint64_t m_physicalMemoryBytes = 0u;

    /// Taken before the main lock, never under it, while the recycling resource is told what to keep.
    std::mutex m_keeping;
};

} // namespace an::consumer::detail
