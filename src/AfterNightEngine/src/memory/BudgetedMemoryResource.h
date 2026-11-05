// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the memory resource an engine context owns.
//
// The one implementation of `MemoryResource` the engine ships, and deliberately a thin one. It does
// three things the platform allocator does not: it honours an alignment, it refuses a request that
// would take the context past its configured memory budget, and it counts. Everything else it hands
// straight to the platform, because a pool that beats the system allocator has to be measured
// rather than assumed.
//
// One thing was measured, and it is the platform giving memory back. Releasing a frame-sized block
// is the operating system unmapping every page of it, and a stack's band of every frame is
// gigabytes: returning one took 160 ms on the thread that stopped the stack, which is what a person
// waited for after asking a workflow to stop, and a workflow spent seconds of its stages' own time
// returning the frames it had read. The cost is not the size's alone: an operation that took and
// returned a megapixel's planes on every call spent more of its time returning them than computing,
// and more the more workers had run, since pages the process's threads touched are given back on
// every processor those threads ran on. So a context with no memory budget hands a block of a
// mebibyte or more it is given back to a thread of its own, which returns it to the platform while
// the caller goes on. The counters move when the caller gives the block back - the bytes are no
// longer the context's - and at most a bounded amount waits to be returned; past it, or with a budget
// configured, the caller returns the block itself, as every smaller block is returned. An
// address-sanitized build returns every block at once, so that a use after release is found where it
// happens.
//
// One mutex guards reservations and counters, which is the whole difficulty. Reserving before the
// platform allocation stops two threads from each taking the last available block, and reading all
// counters under the same guard gives callers one coherent snapshot rather than values sampled from
// different instants.
//
// Nothing here is a standard-library allocator and nothing is templated on it. The type never
// reaches a public signature and never affects a public layout, so replacing it later - with a pool,
// with a NUMA-aware resource, with something that pre-touches - recompiles this file and nothing
// above it.
//
// A caller may supply the resource underneath this one. The budget, the alignment and the counters
// stay here - they are the context's - and only the source of the admitted bytes moves, so a caller
// that refuses a chosen allocation is indistinguishable from a machine that has run out. It is fixed
// at construction rather than settable, which is what makes it impossible for a block to be taken
// from the platform and handed back to a supplied resource.

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

namespace an
{

/// The persistent memory of one engine context, within that context's configured budget.
class BudgetedMemoryResource final : public MemoryResource
{
public:
    /// Builds a resource that works within one set of configured limits.
    ///
    /// @param limits the context's limits, whose `memoryBudgetBytes` is the bound. Copied, because
    ///        they are immutable for the life of the context.
    /// @param upstream where the admitted bytes come from, or null for the platform allocator.
    ///        Borrowed and immutable for the life of this resource, so no block can be taken from
    ///        one source and returned to the other.
    explicit BudgetedMemoryResource(const EngineLimits& limits, MemoryResource* upstream = nullptr) noexcept
        : m_limits(limits), m_upstream(upstream)
    {
    }

    /// Returns what still waits to go back to the platform, then releases nothing more.
    ///
    /// A resource does not own what it handed out - the callers do - so there is nothing else here
    /// to free. What it can do is notice: a context destroyed with memory still outstanding is a
    /// lifetime defect in a caller, and `counters().liveAllocations` is where that shows.
    ~BudgetedMemoryResource() override;

    /// Neither copied nor moved: one context owns one of these, and the counters are the context's.
    BudgetedMemoryResource(const BudgetedMemoryResource&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    BudgetedMemoryResource& operator=(const BudgetedMemoryResource&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    BudgetedMemoryResource(BudgetedMemoryResource&&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    BudgetedMemoryResource& operator=(BudgetedMemoryResource&&) = delete;

    /// Takes memory, or reports why it cannot. See `MemoryResource::allocate`.
    ///
    /// @param bytes how many bytes are needed, which must not be zero.
    /// @param alignment the boundary, which must be a power of two.
    /// @return the address, or the refusal.
    [[nodiscard]] Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) override;

    /// Takes zero-filled memory under the same budget and counters as `allocate`. See
    /// `MemoryResource::allocateZeroed`.
    ///
    /// The platform path asks for already-zeroed memory rather than clearing it; a supplied
    /// upstream resource is asked through its own `allocateZeroed`.
    ///
    /// @param bytes how many bytes are needed, which must not be zero.
    /// @param alignment the boundary, which must be a power of two.
    /// @return the zero-filled address, or the refusal.
    [[nodiscard]] Result<void*> allocateZeroed(std::uint64_t bytes, std::uint64_t alignment) override;

    /// Gives memory back. See `MemoryResource::deallocate`.
    ///
    /// A block of at least `kDeferredReleaseBytes` from the platform, given back to a context with
    /// no memory budget while less than `kDeferredReleaseLimitBytes` waits, is returned to the
    /// platform on this resource's own thread rather than the caller's; the counters move now
    /// either way.
    ///
    /// @param pointer what `allocate` or `allocateZeroed` returned, or null.
    /// @param bytes what was asked for.
    /// @param alignment what was asked for.
    void deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept override;

    /// What this resource has done. See `MemoryResource::counters`.
    ///
    /// @return the counters.
    [[nodiscard]] MemoryCounters counters() const noexcept override;

    /// The smallest block returned to the platform on this resource's own thread: a quarter of a
    /// megapixel's Float32 plane, so frames, bands, accumulators and an operation's working planes
    /// go, and the small blocks the platform heap keeps for reuse stay with the caller.
    static constexpr std::uint64_t kDeferredReleaseBytes = std::uint64_t{1} << 20u;

    /// The most that may wait to be returned to the platform at once; past it a caller returns its
    /// block itself, so a burst of releases cannot hold memory the process no longer uses for long.
    static constexpr std::uint64_t kDeferredReleaseLimitBytes = std::uint64_t{16} << 30u;

private:
    /// The one allocation path behind `allocate` and `allocateZeroed`: validates the request,
    /// reserves its bytes against the budget, takes the memory, and moves the reservation into the
    /// live counters - or unwinds the reservation when the memory could not be taken.
    ///
    /// @param bytes how many payload bytes are needed.
    /// @param alignment the boundary the payload must start on.
    /// @param zero whether every payload byte must be zero.
    /// @return the aligned payload, or `ErrorCode::InvalidArgument`, `ErrorCode::MemoryBudgetExceeded`,
    ///         `ErrorCode::AllocationFailed`, or the upstream resource's own refusal.
    [[nodiscard]] Result<void*> allocateInitialized(std::uint64_t bytes, std::uint64_t alignment, bool zero);

    /// Hands a platform block to the release thread, starting it the first time.
    ///
    /// @param original the pointer `malloc` or `calloc` returned.
    /// @param bytes the payload bytes it held, which count against the waiting bound.
    /// @return whether the thread took it; false leaves the block for the caller to free.
    [[nodiscard]] bool deferRelease(void* original, std::uint64_t bytes) noexcept;

    /// The release thread's loop: returns each handed-over block to the platform, oldest first,
    /// until this resource is destroyed and nothing waits.
    void releaseDeferred() noexcept;

    /// The configured maxima and budget this resource works within. Immutable.
    EngineLimits m_limits;

    /// Where the admitted bytes come from, or null for the platform allocator. Borrowed, immutable.
    MemoryResource* m_upstream = nullptr;

    /// Guards budget reservation and every counter, making a returned snapshot coherent.
    mutable std::mutex m_guard;

    /// Bytes admitted while the platform allocator is being asked but not yet handed out.
    std::uint64_t m_reservedBytes = 0u;

    /// Bytes outstanding right now. The number the budget is checked against with reservations.
    std::uint64_t m_liveBytes = 0u;

    /// Allocations outstanding right now.
    std::uint64_t m_liveAllocations = 0u;

    /// The largest `m_liveBytes` ever reached.
    std::uint64_t m_peakBytes = 0u;

    /// One platform block waiting to be returned.
    struct DeferredBlock
    {
        /// The pointer `malloc` or `calloc` returned.
        void* original = nullptr;
        /// The payload bytes it held.
        std::uint64_t bytes = 0u;
    };

    /// Guards the waiting blocks and the release thread's state; never held while one is freed.
    std::mutex m_releaseGuard;

    /// Wakes the release thread when a block is handed over or the resource is destroyed.
    std::condition_variable m_releaseWake;

    /// Blocks waiting to be returned to the platform, oldest first.
    std::deque<DeferredBlock> m_deferred;

    /// The payload bytes of the waiting blocks, and of the one being returned.
    std::uint64_t m_deferredBytes = 0u;

    /// Whether the resource is being destroyed: nothing more is handed over.
    bool m_releaseClosing = false;

    /// The release thread, started with the first block handed over; joined by the destructor.
    std::thread m_releaser;

    /// Bytes ever handed out.
    std::uint64_t m_totalBytes = 0u;

    /// Allocations ever handed out.
    std::uint64_t m_totalAllocations = 0u;

    /// Requests refused, by the budget or by the platform.
    std::uint64_t m_deniedAllocations = 0u;
};

} // namespace an
