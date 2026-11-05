// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The memory an operation needs while it runs and nobody needs afterwards.
///
/// Scratch is the other half of the memory system, and it is a different problem from persistent
/// storage rather than a smaller one. An image buffer is asked for once and lives as long as
/// somebody holds the image; a convolution's row halo, a sort's workspace and a stacking pass's
/// per-tile accumulators are asked for on every tile, are the same size every time, and are dead the
/// moment the tile is. Serving the second from a general allocator is what makes a run spend
/// measurable time doing nothing anybody asked for.
///
/// So an arena takes memory from a `MemoryResource` in blocks and hands it out by moving a pointer.
/// Three rules follow, and they are the contract rather than an implementation note.
///
/// **A block, once taken, is kept.** `reset()` and `rewind()` make the space reusable without giving
/// it back, so a loop that needs the same scratch on every tile reaches the allocator once. That is
/// what "pooled for the lifetime of one execution" means.
///
/// **Everything is released when the arena is destroyed, however the execution ended.** Not on a
/// timer, not on an idle heuristic, not above a high-water threshold: on the scope. An application
/// holding open images and running nothing therefore holds no scratch, and that is a property of
/// where the arena lives rather than a promise something has to remember to keep.
///
/// **What is held counts against the budget while it is held**, idle or not, because it is held
/// through the same resource everything else is and the resource is where the budget lives.
///
/// An arena is not thread-safe. One belongs to one execution, and the threads of that execution
/// either take their scratch before the parallel region or take one arena each - which is the same
/// rule a bump allocator has everywhere, stated here because the objects around it are thread-safe
/// and the difference matters.

#pragma once

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <cstdint>
#include <memory>

namespace an
{

/// The engine context whose memory budget owns an arena. Defined in `Runtime/EngineContext.h`.
class EngineContext;

/// The block size an arena takes when a caller states none.
///
/// A quarter of a mebibyte is large enough that ordinary per-tile scratch comes out of one block
/// and small enough that an operation which turns out to need almost none has not reserved a
/// meaningful part of the budget. An operation that knows its working set states its own.
inline constexpr std::uint64_t kDefaultScratchBlockBytes = 256u * 1024u;

/// A position in an arena, to come back to.
///
/// Taken by `ScratchArena::mark` and given to `ScratchArena::rewind`. Its members are the arena's
/// business: a caller copies one and holds it, and cannot construct one that means anything. It
/// carries the accounting as well as the position, because a rewind has to restore what the arena
/// reports as well as where it is pointing, and the alignment padding between two allocations makes
/// the first underivable from the second.
///
/// A marker belongs to the arena that produced it and to the state that arena was in. Rewinding to
/// a marker taken before a `reset()` or a `releaseBlocks()` is a defect; the arena treats it as a
/// rewind to its beginning rather than as a licence to hand out memory it no longer has.
struct ScratchMarker
{
    /// Which engine context owns the arena that issued this marker.
    std::uint64_t contextId = 0u;

    /// Which arena in that context issued this marker.
    std::uint64_t arenaId = 0u;

    /// Which reset/release epoch of the arena issued this marker.
    std::uint64_t epoch = 0u;

    /// Which block the arena was filling.
    std::uint64_t block = 0u;

    /// How far into that block it had got, in bytes.
    std::uint64_t offset = 0u;

    /// How many bytes it had handed out at that point.
    std::uint64_t liveBytes = 0u;

    /// How many requests it had served at that point.
    std::uint64_t liveAllocations = 0u;

    /// Integrity tag over every preceding member, for detecting a changed or fabricated marker.
    std::uint64_t validation = 0u;
};

/// One operation's scratch memory.
///
/// Built from an `EngineContext`, on which it keeps a zero-concurrency lease until destruction.
/// The context therefore refuses shutdown while the arena can still allocate, and the binding
/// prevents an execution from spending one context's memory budget through another.
/// Move-only, because two arenas over one set of blocks would be two bump pointers into the same
/// memory.
///
/// This is the type `ExecutionContext` names and borrows. An execution context never owns one: the
/// arena belongs to whatever set the execution up, which is what makes its release the caller's
/// scope rather than the engine's guesswork.
class ScratchArena
{
public:
    /// An arena that holds nothing.
    ///
    /// The state one is left in by a move, and a usable default for a member assigned later. Every
    /// allocation on it is refused with `ErrorCode::NotInitialized`; `valid()` is how to ask.
    AFTERNIGHTENGINE_API ScratchArena() noexcept;

    /// Gives every block back to the resource.
    ///
    /// The whole of the release rule: however the execution ended - returned, failed, cancelled or
    /// unwound - this runs, because it is a destructor.
    AFTERNIGHTENGINE_API ~ScratchArena();

    /// Takes over another arena's blocks, leaving it holding nothing.
    ///
    /// Allocation-free. Pointers already handed out stay valid: the blocks move, and nothing in
    /// them does.
    ///
    /// @param other the arena to take over. Left exactly as a default-constructed one.
    AFTERNIGHTENGINE_API ScratchArena(ScratchArena&& other) noexcept;

    /// Releases this arena's blocks and takes over another's.
    ///
    /// @param other the arena to take over. Left exactly as a default-constructed one.
    /// @return this arena.
    AFTERNIGHTENGINE_API ScratchArena& operator=(ScratchArena&& other) noexcept;

    /// Copying is deleted: two bump pointers into one set of blocks would hand out the same bytes
    /// twice.
    ScratchArena(const ScratchArena&) = delete;

    /// Copying is deleted. See the copy constructor.
    ScratchArena& operator=(const ScratchArena&) = delete;

    /// Builds an arena over an engine context's memory resource.
    ///
    /// Nothing is allocated here: the first block is taken by the first request, so an operation
    /// that turns out to need no scratch has cost nothing.
    ///
    /// @param engine the context whose memory resource and budget own every block. It cannot shut
    ///        down while the arena lives. A context that holds nothing is refused with
    ///        `ErrorCode::NotInitialized`.
    /// @param blockBytes how much to take at a time. Rounded up to the default buffer alignment;
    ///        zero is refused with `ErrorCode::InvalidArgument`.
    /// @return the arena, or the refusal: besides the two above, `ErrorCode::LimitExceeded` when
    ///         `blockBytes` is not representable once rounded, and `ErrorCode::AllocationFailed` when
    ///         the arena's own state cannot be allocated.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ScratchArena>
    create(const EngineContext& engine, std::uint64_t blockBytes = kDefaultScratchBlockBytes);

    /// Whether this arena has a resource to take memory from.
    ///
    /// Allocation-free.
    ///
    /// @return false for a default-constructed arena and for one that has been moved from.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// Which engine context owns this arena's memory budget.
    ///
    /// Immutable, thread-safe and allocation-free. The arena itself remains otherwise not
    /// thread-safe.
    ///
    /// @return the owning context's identity, or `0` when this arena holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

    /// Takes scratch, or reports why it cannot.
    ///
    /// Ordinarily this moves a pointer and returns. It reaches the resource only when the current
    /// block cannot serve the request, and a request larger than the block size gets a block of its
    /// own rather than being refused.
    ///
    /// Not thread-safe: one arena belongs to one execution. The contents of what comes back are
    /// unspecified, including after a `reset()` - scratch is not zeroed, because the caller that
    /// needs it zeroed knows and the caller that does not would pay for it.
    ///
    /// @param bytes how many bytes are needed. Zero is refused with `ErrorCode::InvalidArgument`.
    /// @param alignment the boundary the returned address must sit on. Must be a power of two.
    /// @return the address, valid until the next `reset()`, `rewind()` past it, `releaseBlocks()`
    ///         or the arena's destruction; or `ErrorCode::NotInitialized` when this arena holds
    ///         nothing, `ErrorCode::InvalidArgument` for a zero size or an alignment that is not a
    ///         power of two, `ErrorCode::LimitExceeded` when the request and its alignment padding
    ///         are not representable, and whatever the resource reported - such as
    ///         `ErrorCode::MemoryBudgetExceeded` - when a new block could not be taken.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<void*> allocate(std::uint64_t bytes,
                                                              std::uint64_t alignment = kDefaultBufferAlignmentBytes);

    /// Where the arena has got to, so that `rewind` can bring it back.
    ///
    /// The tool for a nested stage: take a marker, run the stage, rewind, and the stage's scratch
    /// is available again to the next one without anything having reached the allocator.
    ///
    /// Not thread-safe. Allocation-free.
    ///
    /// @return the current position.
    [[nodiscard]] AFTERNIGHTENGINE_API ScratchMarker mark() const noexcept;

    /// Makes everything taken since a marker available again.
    ///
    /// Returns nothing to the resource: the blocks stay, which is the point. Every pointer handed
    /// out since the marker was taken must be treated as dead.
    ///
    /// Not thread-safe. Allocation-free.
    ///
    /// @param marker a position this arena produced, from a state it has not been reset or released
    ///        out of since. A marker past where the arena currently is, from a discarded state, from
    ///        another arena, or altered since it was taken rewinds to the beginning rather than
    ///        corrupting the accounting.
    AFTERNIGHTENGINE_API void rewind(ScratchMarker marker) noexcept;

    /// Makes everything available again, keeping the blocks.
    ///
    /// What an operation calls between passes. The high-water counter deliberately survives it, so
    /// a report still says how much the operation needed at its worst.
    ///
    /// Not thread-safe. Allocation-free.
    AFTERNIGHTENGINE_API void reset() noexcept;

    /// Gives every block back to the resource, and leaves the arena usable.
    ///
    /// The explicit half of what the destructor does, for an operation that has finished with its
    /// scratch and is going on to something that needs the budget back. The next request takes a
    /// fresh block.
    ///
    /// Not thread-safe. Allocation-free apart from the release itself.
    AFTERNIGHTENGINE_API void releaseBlocks() noexcept;

    /// How many bytes this arena holds from its resource, idle or not.
    ///
    /// The figure that counts against the configured budget, which is not the same as what has been
    /// handed out: a block half used is a block wholly held.
    ///
    /// Not thread-safe. Allocation-free.
    ///
    /// @return the reserved bytes, or `0` when this arena holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t reservedBytes() const noexcept;

    /// What this arena has handed out.
    ///
    /// `liveBytes` is what is currently taken, `peakBytes` the most ever taken at once, and
    /// `totalAllocations` the number of requests - which is the figure that says whether an
    /// operation is reaching for scratch per pixel when it meant per tile.
    ///
    /// Not thread-safe. Allocation-free.
    ///
    /// @return the counters, all zero for an arena that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API MemoryCounters counters() const noexcept;

private:
    /// The blocks and the bump pointer, defined only inside the engine so that the published
    /// surface says nothing about how an arena is laid out.
    class State;

    /// Takes ownership of a state the factory has already built.
    ///
    /// @param state the arena's blocks and accounting. Never null on this path.
    explicit ScratchArena(std::unique_ptr<State> state) noexcept;

    /// Builds the implementation state after the public factory has validated the context and
    /// block size.
    ///
    /// Keeping this bridge opaque lets the implementation retain the context lifetime without
    /// making the scratch translation unit include the context's full public surface.
    ///
    /// @param contextHold type-erased ownership of the context lease.
    /// @param resource the owning context's memory resource.
    /// @param contextId the owning context's identity.
    /// @param arenaId this arena's identity within the context.
    /// @param blockBytes the validated, aligned block size.
    /// @return the arena, or `ErrorCode::AllocationFailed` when its state cannot be built.
    [[nodiscard]] static Result<ScratchArena> createBound(std::shared_ptr<void> contextHold, MemoryResource& resource,
                                                          std::uint64_t contextId, std::uint64_t arenaId,
                                                          std::uint64_t blockBytes);

    /// What this arena holds, or null when it holds nothing.
    std::unique_ptr<State> m_state;
};

} // namespace an
