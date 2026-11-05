// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A consumer's engine session: the one engine context an application or tool owns, its settings, the admission of
/// every operation that runs in it, and the allowances for the memory it holds that the engine does not count.
///
/// The engine has no default context and no accessor for one: every operation runs in a context its caller owns, and
/// that context's thread and memory ceilings are fixed when it is built. A session is that caller. An application or
/// a command-line tool opens one after logging and before any image work, and hands it to whatever needs it - there
/// is no instance accessor here either.
///
/// **Settings take effect at the next construction, not now.** A change of a ceiling is recorded as pending and
/// applied when the context is next built: at the next start, or by `applyPendingSettings` once nothing taken from the
/// current context is outstanding. Outstanding means more than running work - an image asset, a hold, a queued
/// request, an open preview reservation, and anything else that took storage or a lease from the context, which the
/// engine itself refuses to shut down under. If the replacement cannot be built after the old context has been shut
/// down, the session reports itself unavailable, keeps the failure, and can try again; it never hands out a context
/// that has been shut down.
///
/// **Admission is the session's, and it never blocks the thread it is not asked to.** `admit` waits in a bounded
/// queue and is for a worker thread; `tryAdmit` answers at once and is for any thread, the GUI's included. At width
/// one every operation is serialized in arrival order. Above it, while a preview is admitted or a preview dialog holds
/// a reservation, `previewReservationWidth` threads are kept for previews and everything else shares the rest; with
/// no preview open, other work is granted the whole width. The reservation is what previews are sure of, not all they
/// may take: while no other work waits, a preview borrows every thread nothing holds but one, which stays for work
/// that never waits, and once other work waits, previews are held to the reservation until it has its share. A grant
/// is never taken back or widened: work admitted before a reservation keeps its threads until its operation ends,
/// work admitted during one regains the reserved threads at its next operation, a preview's borrowed threads come
/// back when its operation ends, and `admit` waits for them rather than grant fewer than they would make.
///
/// **While a preview dialog is open, large blocks are reused.** A preview frame asks for blocks of the same sizes
/// frame after frame, so while a preview reservation is held the session keeps the large blocks its context gives
/// back and hands each to the next request of its size, already resident (`RecycledMemory`). The images stay new
/// assets with identities of their own; only their memory is the earlier frame's.
#pragma once

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/ConsumerRuntimeExport.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace an
{
class CodecRegistry;
} // namespace an

namespace an::consumer
{

/// The version of the session, scope and asset interfaces a translation unit was compiled against. A module handed
/// a session by another module compares this with `consumerRuntimeInterfaceVersion()` before using it.
inline constexpr std::uint32_t kConsumerRuntimeInterfaceVersion = 1u;

/// The interface version the loaded library was built with.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint32_t consumerRuntimeInterfaceVersion() noexcept;

/// How many requests may wait for admission at once by default.
inline constexpr std::uint32_t kDefaultAdmissionQueueCapacity = 64u;

/// Builds the codec registry a context is configured with. Called each time the session builds a context, after the
/// previous one has been shut down, so the codecs of two configurations are never held at once.
using CodecRegistryFactory = std::function<an::Result<std::shared_ptr<const an::CodecRegistry>>()>;

/// What a session is built with. Copied; nothing in it is retained by reference.
struct EngineSessionSettings
{
    /// The most threads the session's work may use at once; zero asks for the host's concurrency.
    std::uint32_t maxConcurrency = an::kUseHostConcurrency;

    /// The engine's limits, its memory budget among them.
    an::EngineLimits limits = an::defaultEngineLimits();

    /// Builds the context's codec registry; empty builds a context with none.
    CodecRegistryFactory codecRegistry{};

    /// How many requests may wait for admission at once; a request beyond it is refused as busy.
    std::uint32_t admissionQueueCapacity = kDefaultAdmissionQueueCapacity;

    /// The consumer memory allowances.
    MemoryAllowances memory{};

    /// A resource each context the session builds takes its admitted bytes from, borrowed and outliving the session;
    /// null gives each context the platform's. Nothing an ordinary caller states: it is how a fault-injection run
    /// refuses an allocation at a chosen boundary of a consumer's work, through the engine's own seam
    /// (`an::EngineContextOptions::memoryResource`), so the session cannot tell the refusal from a machine that is out
    /// of memory.
    an::MemoryResource* memoryResource = nullptr;
};

/// Whether settings can be enforced, by the rules the engine applies to a context's options and the session's own.
///
/// @param settings what a caller states.
/// @return success; `ErrorCode::LimitExceeded` carrying the request and the limit when the concurrency is above what an
///         engine context may be configured with; `ErrorCode::InvalidArgument` naming the field when a limit, the
///         memory budget, the queue capacity or a memory allowance is zero.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status checkSessionSettings(const EngineSessionSettings& settings);

/// What a session can do now.
enum class SessionState : std::uint8_t
{
    /// It holds a context and admits work.
    Available,

    /// It is building a context; work is refused as busy until it finishes.
    Replacing,

    /// Its last construction failed after the previous context was shut down. It holds no context, refuses work with
    /// the failure as the cause, and can be reopened.
    Unavailable,

    /// Admission is closed: nothing new is admitted, and the session is shutting down or has shut down.
    Closed
};

/// A state's name, for messages and records.
///
/// @param state the state.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const char* sessionStateName(SessionState state) noexcept;

/// What a session has outstanding: everything that stops its context being replaced or shut down.
struct SessionOutstanding
{
    /// Holds on the context.
    std::uint64_t holds = 0u;

    /// Admitted operations.
    std::uint64_t operations = 0u;

    /// Requests waiting for admission.
    std::uint64_t queued = 0u;

    /// Image assets, counted while the asset or any of its leases lives.
    std::uint64_t assets = 0u;

    /// Preview reservations held by open preview dialogs.
    std::uint64_t previewReservations = 0u;

    /// Leases the engine context counts: the session's own and those of anything that took storage from it.
    std::uint64_t engineLeases = 0u;

    /// Whether nothing the session counts is outstanding. The engine's own count is decided by its shutdown.
    [[nodiscard]] bool none() const noexcept
    {
        return holds == 0u && operations == 0u && queued == 0u && assets == 0u && previewReservations == 0u;
    }
};

/// How a session's threads are divided now, and what admission has decided so far.
struct AdmissionSnapshot
{
    /// The session's width; zero while it holds no context.
    std::uint32_t width = 0u;

    /// The threads reserved for previews now; zero when no preview is admitted or open.
    std::uint32_t previewReservation = 0u;

    /// Threads granted to admitted previews.
    std::uint32_t previewThreads = 0u;

    /// Threads granted to everything else.
    std::uint32_t otherThreads = 0u;

    /// Requests waiting.
    std::uint64_t queued = 0u;

    /// Requests admitted since the session opened.
    std::uint64_t admitted = 0u;

    /// Requests refused as busy: a full queue, or work that does not wait finding no thread free.
    std::uint64_t refusedBusy = 0u;

    /// Requests stopped while they waited.
    std::uint64_t cancelledWhileQueued = 0u;

    /// Requests whose deadline passed while they waited.
    std::uint64_t expiredWhileQueued = 0u;
};

/// The large blocks a session keeps for reuse while a preview dialog is open, and how often they served a request.
///
/// A preview frame's destination, display proxy, presentation buffers and working bands are the same sizes frame after
/// frame. While a preview reservation is held, a block of 64 KiB or more that the context gives back - about where the
/// platforms' heaps start returning freed pages to the operating system - is kept: of one size, three blocks or 32 MiB,
/// whichever holds more; 128 blocks in all; and no more in all than an eighth of the memory budget, or of the machine's
/// memory when no budget is set. The next request of a kept block's size and alignment takes it, already resident and
/// cleared first when zeroed storage is asked for, rather than memory the platform returned to the operating system
/// and faults in again. A kept block is outside the engine's count of live bytes while it is kept - the memory budget
/// counts it again once a request takes it - so a session with a preview open can hold up to that bound beyond what its
/// budget counts. Every kept block is returned when the last reservation goes. A session whose settings supply a
/// memory resource keeps nothing.
struct RecycledMemory
{
    /// Blocks kept now.
    std::uint64_t keptBlocks = 0u;

    /// Their bytes, as they were asked for.
    std::uint64_t keptBytes = 0u;

    /// Requests a kept block served since the session opened.
    std::uint64_t reusedBlocks = 0u;

    /// Large blocks given back to the platform on the session's own releasing thread rather than the caller's since the
    /// session opened: every one a closing preview dialog's frames left kept, whatever the memory budget.
    std::uint64_t releasedLater = 0u;
};

/// A hold on a session's context for work that runs no operation: describing an image against the context's limits,
/// creating one, adopting it as an asset. The session refuses to replace or shut down its context while a hold is
/// outstanding, and the engine context counts the hold as a lease of no threads.
///
/// Move-only. Safe to release on any thread and after the session is gone.
class SessionHold
{
public:
    /// A hold on nothing.
    AFTERNIGHTCONSUMERRUNTIME_API SessionHold() noexcept;

    /// Releases the hold.
    AFTERNIGHTCONSUMERRUNTIME_API ~SessionHold();

    /// Takes over another hold.
    ///
    /// @param other the hold to take over.
    AFTERNIGHTCONSUMERRUNTIME_API SessionHold(SessionHold&& other) noexcept;

    /// Releases this hold and takes over another.
    ///
    /// @param other the hold to take over.
    /// @return this hold.
    AFTERNIGHTCONSUMERRUNTIME_API SessionHold& operator=(SessionHold&& other) noexcept;

    SessionHold(const SessionHold&) = delete;
    SessionHold& operator=(const SessionHold&) = delete;

    /// Whether this holds a context.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool valid() const noexcept;

    /// The held context, valid while this hold is. Calling it on a hold that holds nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const an::EngineContext& engine() const noexcept;

    /// Releases the hold now. Releasing twice releases once.
    AFTERNIGHTCONSUMERRUNTIME_API void release() noexcept;

private:
    friend class detail::SessionCore;

    SessionHold(std::shared_ptr<detail::SessionCore> core, an::EngineContextLease lease) noexcept;

    std::shared_ptr<detail::SessionCore> m_core;
    an::EngineContextLease m_lease;
};

/// A preview dialog's claim on the preview reservation while it is open, released when this is released or
/// destroyed. Every open dialog shares one reservation; it exists while any claim does.
class PreviewReservation
{
public:
    /// A claim on nothing.
    AFTERNIGHTCONSUMERRUNTIME_API PreviewReservation() noexcept;

    /// Releases the claim.
    AFTERNIGHTCONSUMERRUNTIME_API ~PreviewReservation();

    /// Takes over another claim.
    ///
    /// @param other the claim to take over.
    AFTERNIGHTCONSUMERRUNTIME_API PreviewReservation(PreviewReservation&& other) noexcept;

    /// Releases this claim and takes over another.
    ///
    /// @param other the claim to take over.
    /// @return this claim.
    AFTERNIGHTCONSUMERRUNTIME_API PreviewReservation& operator=(PreviewReservation&& other) noexcept;

    PreviewReservation(const PreviewReservation&) = delete;
    PreviewReservation& operator=(const PreviewReservation&) = delete;

    /// Whether this is a claim.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool valid() const noexcept;

    /// Releases the claim now. Releasing twice releases once.
    AFTERNIGHTCONSUMERRUNTIME_API void release() noexcept;

private:
    friend class detail::SessionCore;

    explicit PreviewReservation(std::shared_ptr<detail::SessionCore> core) noexcept;

    std::shared_ptr<detail::SessionCore> m_core;
};

/// A consumer's engine session.
///
/// Move-only: one owner. Thread-safe: every member may be called from any thread, and what it hands out - holds,
/// scopes, assets, reservations - keeps the session's state alive, so it may outlive the session object. Closing
/// and shutting down are the owner's to call.
class EngineSession
{
public:
    /// A session that holds nothing, as a moved-from one does.
    AFTERNIGHTCONSUMERRUNTIME_API EngineSession() noexcept;

    /// Closes admission and shuts the context down if nothing is outstanding. With something outstanding the context
    /// is released when the last of it goes; nothing is torn down under it.
    AFTERNIGHTCONSUMERRUNTIME_API ~EngineSession();

    /// Takes over another session.
    ///
    /// @param other the session to take over.
    AFTERNIGHTCONSUMERRUNTIME_API EngineSession(EngineSession&& other) noexcept;

    /// Closes this session as its destructor would and takes over another.
    ///
    /// @param other the session to take over.
    /// @return this session.
    AFTERNIGHTCONSUMERRUNTIME_API EngineSession& operator=(EngineSession&& other) noexcept;

    EngineSession(const EngineSession&) = delete;
    EngineSession& operator=(const EngineSession&) = delete;

    /// Opens a session: checks the settings, builds the codec registry and the context.
    ///
    /// @param settings what the session is built with.
    /// @return the session, or the failure of the check, of the registry factory or of the context's construction.
    [[nodiscard]] static AFTERNIGHTCONSUMERRUNTIME_API an::Result<EngineSession>
    open(const EngineSessionSettings& settings);

    /// Whether this is a session at all; false for a default-constructed or moved-from one.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool valid() const noexcept;

    /// What the session can do now.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API SessionState state() const noexcept;

    /// How many contexts the session has built; moves with every successful construction.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint64_t generation() const noexcept;

    /// The engine's identity of the context held now, or zero while none is held.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint64_t contextId() const noexcept;

    /// The session's width - the context's maximum concurrency - or zero while no context is held.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint32_t width() const noexcept;

    /// The limits of the context held now; the defaults while none is held.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::EngineLimits limits() const noexcept;

    /// Why the session is unavailable; success when it is not.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status lastFailure() const;

    /// The settings the context held now was built with, or the last one was when none is held.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API EngineSessionSettings activeSettings() const;

    /// The settings recorded to take effect at the next construction, if any.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::optional<EngineSessionSettings> pendingSettings() const;

    /// Whether settings are waiting for the next construction.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool settingsPendingRestart() const noexcept;

    /// Records settings to take effect at the next construction. Nothing running or held changes.
    ///
    /// @param settings the new settings, replacing any pending ones.
    /// @return success, or what `checkSessionSettings` refuses, in which case nothing is recorded; `InvalidState` once
    ///         the session is closed.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status requestSettings(const EngineSessionSettings& settings);

    /// Builds a replacement context from the pending settings, when nothing is outstanding.
    ///
    /// The previous context is shut down first, so two are never held at once; a failure after that leaves the
    /// session unavailable with the failure kept, and `reopen` tries again. Pending settings stay pending until a
    /// construction succeeds.
    ///
    /// @return success, also when nothing is pending; `ErrorCode::Busy` naming what is outstanding - the session's
    ///         own counts, or the engine's refusal to shut down as the cause - in which case the session is exactly as
    ///         it was; `ErrorCode::InvalidState` once the session is closed; otherwise the construction's failure.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status applyPendingSettings();

    /// Builds a context again after a failed construction, from the pending settings or else the active ones.
    ///
    /// @return success; `ErrorCode::InvalidState` when the session is not unavailable; otherwise the construction's
    ///         failure, leaving the session unavailable.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status reopen();

    /// Takes a hold on the context.
    ///
    /// @return the hold, or `ErrorCode::NotInitialized` carrying the last failure as its cause while unavailable,
    ///         `ErrorCode::Busy` while a context is being built, `ErrorCode::InvalidState` once closed.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<SessionHold> hold() const;

    /// Admits an operation, waiting in the queue until its purpose has a thread free. For a worker thread: it blocks.
    ///
    /// Background work never waits; it is answered as `tryAdmit` answers it.
    ///
    /// @param request what the operation asks for.
    /// @return the scope; `ErrorCode::Busy` when the queue is full; `ErrorCode::Cancelled` or
    ///         `ErrorCode::DeadlineExceeded` when the request was stopped or ran out of time while it waited; what
    ///         `hold` refuses for the session's state; or what the engine refuses when it builds the execution.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<OperationScope> admit(const OperationRequest& request) const;

    /// Admits an operation only if its purpose has a thread free now and nothing of its kind is waiting ahead of it.
    /// Never blocks, so any thread may call it.
    ///
    /// It takes what is free now, and a grant is never widened: beside a preview frame that borrowed every free thread
    /// but one, an Apply or a batch is granted that one thread and runs on it to its end, however many it asked for and
    /// however soon the frame gives the rest back. Work that wants its share should `admit` on a worker, which waits
    /// for the borrowed threads.
    ///
    /// @param request what the operation asks for.
    /// @return the scope, or `ErrorCode::Busy` when it would have to wait, or what `admit` refuses otherwise.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<OperationScope>
    tryAdmit(const OperationRequest& request) const;

    /// Claims the preview reservation for an open preview dialog.
    ///
    /// @return the claim, or what `hold` refuses for the session's state.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<PreviewReservation> reservePreview() const;

    /// What is outstanding now. A diagnostic: another thread may change it before the caller reads it.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API SessionOutstanding outstanding() const;

    /// How the threads are divided now, and what admission has decided so far.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API AdmissionSnapshot admission() const;

    /// What the session keeps for reuse now, and how often a kept block served a request. A diagnostic: another thread
    /// may change it before the caller reads it.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API RecycledMemory recycledMemory() const;

    /// The accountant of the session's consumer memory. Calling it on a session that holds nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API ConsumerMemory& memory() const noexcept;

    /// Closes admission: nothing new is admitted or held, and every waiting request is refused. The first step of
    /// shutting down; idempotent.
    AFTERNIGHTCONSUMERRUNTIME_API void closeAdmission() noexcept;

    /// Closes admission and shuts the context down, if nothing is outstanding.
    ///
    /// @return success, also for a session already shut down; or `ErrorCode::Busy` naming what is outstanding, with
    ///         the engine's refusal as the cause when only the engine counts it. Admission stays closed either way.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status shutdown();

private:
    friend class ImageAsset;

    explicit EngineSession(std::shared_ptr<detail::SessionCore> core) noexcept;

    std::shared_ptr<detail::SessionCore> m_core;
};

} // namespace an::consumer
