// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Two-dimensional tiling of an image-shaped loop, with scratch private to each worker.
///
/// Many kernels walk an image in rectangular tiles and need a working buffer per thread - a row
/// halo, a small histogram, a transposed block. Written against `Runtime/TaskScheduler.h` directly,
/// each of them would repeat the same arithmetic: the tile grid, the per-tile cost the grain policy
/// needs, and the split of one scratch buffer into disjoint per-worker partitions. This header does
/// that once, and checks it.
///
/// It is two calls. `planParallelTiles` turns a request into a `ParallelTilePlan` with every count
/// and byte size checked for overflow, and says how many workers will run and how much scratch they
/// need together, so the caller can allocate it once. `executeParallelTiles` revalidates the tile
/// plan against the operation, gives each worker slot one fixed aligned partition of the caller's
/// scratch, and calls the body once per tile - the tiles numbered in row-major order - on the
/// operation's arena through `parallelForWorkers`. A tile arrives with the partition of the worker
/// slot running it, which no concurrent call shares.
///
/// The executor allocates nothing of its own: the scratch is the caller's, borrowed for the call.
/// Cancellation is checked before every tile, and a failure is reported by tile order rather than
/// by arrival, as the scheduler reports it.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace an
{

/// Requested geometry, work estimate, and scratch needs of one tiled operation.
struct ParallelTileRequest
{
    /// Logical image width in pixels. Zero describes no work.
    std::uint32_t width = 0u;

    /// Logical image height in pixels. Zero describes no work.
    std::uint32_t height = 0u;

    /// Maximum logical width of one tile. Must not be zero.
    std::uint32_t tileWidth = 256u;

    /// Maximum logical height of one tile. Must not be zero.
    std::uint32_t tileHeight = 32u;

    /// Cost of one pixel. The planner converts it to the cost of one full tile without overflow.
    TaskCost pixelCost{};

    /// Bytes reserved privately for every planned worker. Zero asks for none.
    std::uint64_t scratchBytesPerWorker = 0u;

    /// Required alignment of each worker's scratch start. A nonzero power of two.
    std::uint64_t scratchAlignment = alignof(std::max_align_t);
};

/// A checked tiling and scratch plan, made by `planParallelTiles` and run by `executeParallelTiles`.
///
/// A visible value so that callers can store and inspect one. Nothing stops a caller changing a
/// member, so `executeParallelTiles` validates the tile plan again before running, and a modified
/// one cannot create unchecked arithmetic or overlapping scratch partitions; it is refused instead.
struct ParallelTilePlan
{
    /// Logical image width in pixels.
    std::uint32_t width = 0u;

    /// Logical image height in pixels.
    std::uint32_t height = 0u;

    /// Maximum logical width of one tile.
    std::uint32_t tileWidth = 0u;

    /// Maximum logical height of one tile.
    std::uint32_t tileHeight = 0u;

    /// Tile columns covering the width.
    std::uint32_t columns = 0u;

    /// Tile rows covering the height.
    std::uint32_t rows = 0u;

    /// Total tiles in row-major order.
    std::uint64_t tileCount = 0u;

    /// Workers the operation and grain policy will actually use.
    std::uint32_t plannedWorkers = 0u;

    /// Bytes visible to each worker body.
    std::uint64_t scratchBytesPerWorker = 0u;

    /// Byte distance between worker scratch partitions, including alignment padding.
    std::uint64_t scratchStrideBytes = 0u;

    /// Minimum caller-owned scratch span required by this plan.
    std::uint64_t requiredScratchBytes = 0u;

    /// Cost of one tile used to reproduce the scheduler partition during execution.
    TaskCost tileCost{};

    /// Required alignment of the caller's scratch base and every worker partition.
    std::uint64_t scratchAlignment = 1u;
};

/// One row-major tile and the scratch private to the worker executing it.
struct ParallelTile
{
    /// Row-major tile index in `[0, ParallelTilePlan::tileCount)`.
    std::uint64_t index = 0u;

    /// Tile row in `[0, ParallelTilePlan::rows)`.
    std::uint32_t row = 0u;

    /// Tile column in `[0, ParallelTilePlan::columns)`.
    std::uint32_t column = 0u;

    /// First logical x coordinate covered.
    std::uint32_t xBegin = 0u;

    /// One past the last logical x coordinate covered.
    std::uint32_t xEnd = 0u;

    /// First logical y coordinate covered.
    std::uint32_t yBegin = 0u;

    /// One past the last logical y coordinate covered.
    std::uint32_t yEnd = 0u;

    /// Dense worker slot owning this tile for the current execution call.
    std::uint32_t workerSlot = 0u;

    /// Worker-private bytes, borrowed from the caller's scratch span for the duration of the call.
    std::span<std::byte> scratch{};
};

/// What the tile executor calls once for each tile.
///
/// The body is borrowed and may be called concurrently for distinct worker slots. A body may start
/// another engine parallel region: it inherits this operation's arena and cannot widen it.
class ParallelTileBody
{
public:
    /// Borrows a callable that takes a `const ParallelTile&` and returns a `Status`.
    ///
    /// @param callable what each tile should call. Borrowed, and must outlive this body.
    template <typename CallableT>
        requires(!std::is_same_v<std::remove_cvref_t<CallableT>, ParallelTileBody>)
    ParallelTileBody(CallableT&& callable) noexcept
        : m_callable(const_cast<void*>(static_cast<const void*>(std::addressof(callable)))),
          m_invoke(&invokeAs<std::remove_reference_t<CallableT>>)
    {
    }

    /// Runs the borrowed callable for one tile.
    ///
    /// @param tile the geometry and worker-private scratch for this call.
    /// @return whatever the callable reported.
    [[nodiscard]] Status operator()(const ParallelTile& tile) const
    {
        return m_invoke(m_callable, tile);
    }

private:
    using Invoke = Status (*)(void*, const ParallelTile&);

    /// Calls one particular callable type through the erased pointer.
    ///
    /// @param callable the address the constructor recorded.
    /// @param tile the tile to pass.
    /// @return whatever the callable reported.
    template <typename CallableT> [[nodiscard]] static Status invokeAs(void* callable, const ParallelTile& tile)
    {
        return (*static_cast<CallableT*>(callable))(tile);
    }

    /// The callable this body borrows. Never null.
    void* m_callable;

    /// How to call it. Never null.
    Invoke m_invoke;
};

/// Builds a checked row-major tile and scratch plan for one operation.
///
/// Planning does not observe cancellation. The tile count, the whole-tile cost, the scratch stride
/// and the total scratch are checked for overflow. An empty width or height produces a zero-tile
/// plan with no workers or scratch.
///
/// Thread-safe with respect to `execution`, which is only read. Allocation-free apart from a failure
/// it has to describe.
///
/// @param execution the operation whose admitted concurrency shapes the worker count.
/// @param request the geometry, cost, and per-worker scratch requirements.
/// @return the tile plan; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for zero tile dimensions or a non-power-of-two alignment;
///         or `ErrorCode::LimitExceeded` when derived counts or byte sizes do not fit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ParallelTilePlan> planParallelTiles(const ExecutionContext& execution,
                                                                              const ParallelTileRequest& request);

/// Executes a checked tile plan on the operation's arena.
///
/// The caller owns `scratch` through the call. Before any body runs, its size and base alignment are
/// checked and the tile plan is revalidated. Each worker sees one fixed disjoint partition, and a
/// cancellation checkpoint occurs before every tile, in addition to the scheduler's per-range
/// checkpoint, so after a stop each worker finishes at most the tile it is running. Body failures
/// are returned in tile order; tiles that completed before it keep what they wrote.
///
/// Thread-safe with respect to `execution`, which is only read. Allocation-free unless a body or a
/// failure description allocates.
///
/// @param execution the operation to run. Must hold something and be admitted the concurrency the
///        plan was made for.
/// @param plan a plan returned by `planParallelTiles`, unmodified.
/// @param scratch caller-owned bytes held through the call; may be empty when the tile plan
///        requires none.
/// @param body the tile operation. Calls for different worker slots may overlap.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a malformed plan or misaligned scratch;
///         `ErrorCode::IncompatibleArguments` for short scratch or a plan whose worker count no
///         longer matches the operation; or the scheduler, cancellation and body failures
///         `parallelForWorkers` reports.
[[nodiscard]] AFTERNIGHTENGINE_API Status executeParallelTiles(const ExecutionContext& execution,
                                                               const ParallelTilePlan& plan,
                                                               std::span<std::byte> scratch, ParallelTileBody body);

} // namespace an
