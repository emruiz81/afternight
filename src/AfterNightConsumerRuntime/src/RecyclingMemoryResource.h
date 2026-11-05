// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where a session's engine context takes its bytes from: the platform's allocator, with the large blocks given back
/// while a preview dialog is open kept for the next request of their size. Private to the library.
///
/// A preview frame allocates its destination, its display proxy, its presentation's bytes and its working buffers anew
/// each time, and on a large frame every one of them is memory the platform returns to the operating system when it is
/// given back and faults in again, page by page, when the next frame asks for it: forty thousand page faults a frame on
/// a 72-megapixel image, most of a frame's copies spent zeroing pages. While the session holds a preview reservation,
/// a block of at least `kRecycledBlockBytes` given back is kept instead - of one size, `kRecycledBlocksPerSize` blocks
/// or `kRecycledBytesPerSize` bytes, whichever holds more; `kMaxKeptBlocks` in all; and no more bytes than the bound it
/// was told - and the next request of exactly its size and alignment is handed it, its pages already resident, cleared
/// first when the request is for zeroed bytes. Two frames that write images of one size therefore alternate between
/// the same two blocks, and the working buffers each worker takes are each worker's again. Every kept block is returned
/// when the last reservation goes, so memory the application has finished with leaves the process as it did before.
///
/// Everything else is the engine's own platform path, repeated here because a supplied resource replaces it whole: a
/// request is taken with `malloc` - `calloc` for zeroed bytes - in an envelope that keeps the platform's pointer just
/// before the aligned block, and a block of at least `kDeferredReleaseBlockBytes` given back to a context with no
/// memory budget is returned to the platform on this resource's own thread rather than the caller's.
///
/// The engine context borrows the resource and may outlive the session that owns it, because storage outlives the
/// scope that made it: the owner detaches the resource rather than destroying it, and it deletes itself once the
/// last block it handed out has come back.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace an::consumer::detail
{

/// What a recycling resource keeps now and has reused, for a diagnostic or a test.
struct RecycledBlocks
{
    /// Blocks kept for reuse now.
    std::uint64_t keptBlocks = 0u;

    /// Their bytes, as they were asked for.
    std::uint64_t keptBytes = 0u;

    /// Requests a kept block served, since the resource was made.
    std::uint64_t reusedBlocks = 0u;

    /// Large blocks given back to the platform on the resource's own thread rather than the caller's, since the
    /// resource was made.
    std::uint64_t releasedLater = 0u;
};

/// The platform's allocator, keeping the large blocks given back while it is told to.
///
/// Thread-safe: every member may be called from any thread at once. Made by `create` and let go by `detach`, never
/// destroyed by its owner.
class RecyclingMemoryResource final : public an::MemoryResource
{
public:
    /// The smallest block kept: below it the platforms' heaps keep a freed block's pages for their next request, and
    /// from about it up they may give them back to the operating system - Windows decommits large free ranges of its
    /// heap, glibc maps a block of 128 KiB or more on its own - so a block given back is faulted in again.
    static constexpr std::uint64_t kRecycledBlockBytes = std::uint64_t{64} << 10u;

    /// How many blocks of one size are kept at least: the frame shown, the one before it, and one a frame in flight
    /// gives back.
    static constexpr std::uint32_t kRecycledBlocksPerSize = 3u;

    /// How many bytes of one size may be kept beyond those blocks: room for a working buffer of every worker.
    static constexpr std::uint64_t kRecycledBytesPerSize = std::uint64_t{32} << 20u;

    /// How many blocks are kept in all, whatever their sizes; past it the one kept longest goes.
    static constexpr std::size_t kMaxKeptBlocks = 128u;

    /// The smallest block returned to the platform on this resource's own thread: the engine's own threshold.
    static constexpr std::uint64_t kDeferredReleaseBlockBytes = std::uint64_t{1} << 20u;

    /// The most that may wait to be returned to the platform at once, as the engine's own resource bounds it.
    static constexpr std::uint64_t kDeferredReleaseLimitBytes = std::uint64_t{16} << 30u;

    /// A resource that keeps nothing until it is told to, and returns large blocks on the caller's thread until it is
    /// told otherwise.
    ///
    /// @return the resource, owned by whoever calls `detach`; or `ErrorCode::AllocationFailed`.
    [[nodiscard]] static an::Result<RecyclingMemoryResource*> create() noexcept;

    RecyclingMemoryResource(const RecyclingMemoryResource&) = delete;
    RecyclingMemoryResource& operator=(const RecyclingMemoryResource&) = delete;
    RecyclingMemoryResource(RecyclingMemoryResource&&) = delete;
    RecyclingMemoryResource& operator=(RecyclingMemoryResource&&) = delete;

    /// See `an::MemoryResource::allocate`. A kept block of exactly this size and alignment is handed back first.
    [[nodiscard]] an::Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) override;

    /// See `an::MemoryResource::allocateZeroed`. A kept block of exactly this size and alignment is cleared and handed
    /// back first.
    [[nodiscard]] an::Result<void*> allocateZeroed(std::uint64_t bytes, std::uint64_t alignment) override;

    /// See `an::MemoryResource::deallocate`. A large block is kept while blocks are kept and it fits, and otherwise
    /// returned to the platform.
    void deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept override;

    /// What is outstanding: blocks handed out and not yet given back, kept ones not counted.
    [[nodiscard]] an::MemoryCounters counters() const noexcept override;

    /// Starts keeping large blocks given back, up to `boundBytes` in all; or, with zero, stops keeping them and returns
    /// every kept block to the platform.
    ///
    /// @param boundBytes the most kept at once; a block that would take the kept bytes past it pushes out the ones
    ///        kept longest, and one larger than it is never kept.
    void keepBlocks(std::uint64_t boundBytes) noexcept;

    /// Whether a large block returned to the platform is returned on this resource's own thread, as the engine's own
    /// resource returns one to a context with no memory budget.
    ///
    /// @param defer true to return large blocks on this resource's thread.
    void deferReleases(bool defer) noexcept;

    /// What is kept now and has been reused.
    [[nodiscard]] RecycledBlocks recycled() const noexcept;

    /// Lets the owner go: the releasing thread is stopped once it has returned what it holds, kept blocks are returned
    /// now - all on the calling thread, the owner's at its shutdown - and the resource deletes itself at once, or when
    /// the last block it handed out comes back, which is then returned where it is given back and waits for nothing.
    /// Called once; nothing may call the owner's pointer afterwards but whoever still holds a block, through
    /// `deallocate`.
    void detach() noexcept;

private:
    /// A block kept for reuse.
    struct KeptBlock
    {
        void* block = nullptr;
        std::uint64_t bytes = 0u;
        std::uint64_t alignment = 0u;
    };

    /// A block waiting on the release thread: the platform's pointer, and the bytes it was asked for.
    struct ReleasedBlock
    {
        void* original = nullptr;
        std::uint64_t bytes = 0u;
    };

    RecyclingMemoryResource() = default;
    ~RecyclingMemoryResource() override;

    /// Takes the kept block of exactly this size and alignment kept last, or null when none is kept.
    [[nodiscard]] void* takeKept(std::uint64_t bytes, std::uint64_t alignment) noexcept;

    /// Takes a new block from the platform, zeroed or not.
    [[nodiscard]] an::Result<void*> allocateFromPlatform(std::uint64_t bytes, std::uint64_t alignment, bool zero);

    /// Gives a block back to the platform: a large one on the resource's own thread, while its queue has room, when
    /// releases are deferred or when it is a kept block let go of - one no allocation counts any more, so a budget is
    /// told nothing by when it goes - and on the calling thread otherwise.
    void releaseToPlatform(const KeptBlock& block, bool keptBlockLetGo = false) noexcept;

    /// Ends a call that counted itself inside, and deletes the resource when it was the last thing that could reach it.
    void finishCall() noexcept;

    /// The release thread's loop.
    void releaseDeferred() noexcept;

    /// Guards everything below but the release thread's queue.
    mutable std::mutex m_guard;

    /// The kept blocks, the one kept longest first; its capacity is reserved once, so it never allocates again.
    std::vector<KeptBlock> m_kept;
    std::uint64_t m_keptBytes = 0u;
    std::uint64_t m_boundBytes = 0u;
    std::uint64_t m_reused = 0u;
    std::uint64_t m_releasedLater = 0u;
    bool m_deferReleases = false;
    bool m_detached = false;

    /// Calls that give memory back or change what is kept and have not finished, so a detached resource is not deleted
    /// under one of them.
    std::uint64_t m_callsInside = 0u;
    std::uint64_t m_liveBytes = 0u;
    std::uint64_t m_liveAllocations = 0u;
    std::uint64_t m_peakBytes = 0u;
    std::uint64_t m_totalBytes = 0u;
    std::uint64_t m_totalAllocations = 0u;
    std::uint64_t m_deniedAllocations = 0u;

    /// Guards the release thread's queue and its closing.
    std::mutex m_releaseGuard;
    std::condition_variable m_releaseWake;
    std::deque<ReleasedBlock> m_released;
    std::uint64_t m_releasedBytes = 0u;
    bool m_releaseClosing = false;
    std::thread m_releaser;
};

} // namespace an::consumer::detail
