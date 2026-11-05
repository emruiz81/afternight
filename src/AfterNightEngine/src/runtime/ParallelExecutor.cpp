// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - planning and running a tiled loop with worker-private scratch.
//
// The tile plan is derived once, by `derivePlan`, and the executor does not trust it: a plan is a
// value a caller could have edited, so before any body runs the executor recomputes the grid, the
// worker count the operation would now be given, and the scratch stride and total, and refuses a
// tile plan that disagrees with any of them. It compares those fields rather than deriving a second
// one because a tile plan carries the cost of a whole tile and the planner takes the cost of one
// pixel.
//
// Every tile's geometry is computed from its row-major index, and each worker's scratch partition
// is the slice of the caller's span at `workerSlot * scratchStrideBytes`, so two concurrent bodies
// never see overlapping bytes. The tile loop itself is `parallelForWorkers` over the tile count,
// with the whole-tile cost as the cost of one item.

#include <AfterNightEngine/Runtime/ParallelExecutor.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace an
{
namespace
{

[[nodiscard]] constexpr std::uint64_t ceilDivide(std::uint64_t value, std::uint64_t divisor) noexcept
{
    return value / divisor + (value % divisor != 0u ? 1u : 0u);
}

[[nodiscard]] constexpr bool isPowerOfTwo(std::uint64_t value) noexcept
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

[[nodiscard]] constexpr bool checkedMultiply(std::uint64_t left, std::uint64_t right, std::uint64_t& product) noexcept
{
    constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();
    if (left != 0u && right > kMaximum / left)
    {
        return false;
    }
    product = left * right;
    return true;
}

[[nodiscard]] Status executorFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Runtime});
}

[[nodiscard]] Result<ParallelTilePlan> derivePlan(const ExecutionContext& execution, const ParallelTileRequest& request,
                                                  const char* operation)
{
    if (execution.memoryResource() == nullptr)
    {
        return executorFailure(ErrorCode::NotInitialized, operation,
                               "a tile plan needs an operation that holds an engine context");
    }
    if (request.tileWidth == 0u || request.tileHeight == 0u || !isPowerOfTwo(request.scratchAlignment))
    {
        return executorFailure(ErrorCode::InvalidArgument, operation,
                               "tile dimensions and scratch alignment must be nonzero, and alignment a power of two");
    }

    ParallelTilePlan plan{.width = request.width,
                          .height = request.height,
                          .tileWidth = request.tileWidth,
                          .tileHeight = request.tileHeight,
                          .scratchBytesPerWorker = request.scratchBytesPerWorker,
                          .scratchAlignment = request.scratchAlignment};
    if (request.width == 0u || request.height == 0u)
    {
        plan.scratchBytesPerWorker = 0u;
        return plan;
    }

    const std::uint64_t columns = ceilDivide(request.width, request.tileWidth);
    const std::uint64_t rows = ceilDivide(request.height, request.tileHeight);
    if (columns > (std::numeric_limits<std::uint32_t>::max)() || rows > (std::numeric_limits<std::uint32_t>::max)() ||
        rows > (std::numeric_limits<std::uint64_t>::max)() / columns)
    {
        return executorFailure(ErrorCode::LimitExceeded, operation, "the tile grid does not fit its public counts");
    }
    plan.columns = static_cast<std::uint32_t>(columns);
    plan.rows = static_cast<std::uint32_t>(rows);
    plan.tileCount = columns * rows;

    const std::uint64_t fullTilePixels = static_cast<std::uint64_t>(request.tileWidth) * request.tileHeight;
    if (!checkedMultiply(request.pixelCost.bytesPerItem, fullTilePixels, plan.tileCost.bytesPerItem) ||
        !checkedMultiply(request.pixelCost.operationsPerItem, fullTilePixels, plan.tileCost.operationsPerItem))
    {
        return executorFailure(ErrorCode::LimitExceeded, operation, "the full-tile work estimate overflows");
    }
    plan.plannedWorkers = plannedWorkerCount(execution, plan.tileCount, plan.tileCost);

    if (request.scratchBytesPerWorker != 0u)
    {
        constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();
        const std::uint64_t padding = request.scratchAlignment - 1u;
        if (request.scratchBytesPerWorker > kMaximum - padding)
        {
            return executorFailure(ErrorCode::LimitExceeded, operation, "a worker scratch stride overflows");
        }
        plan.scratchStrideBytes = (request.scratchBytesPerWorker + padding) & ~(request.scratchAlignment - 1u);
        if (plan.plannedWorkers != 0u && plan.scratchStrideBytes > kMaximum / plan.plannedWorkers)
        {
            return executorFailure(ErrorCode::LimitExceeded, operation,
                                   "the complete worker scratch partition overflows");
        }
        plan.requiredScratchBytes = plan.scratchStrideBytes * plan.plannedWorkers;
    }
    return plan;
}

} // namespace

Result<ParallelTilePlan> planParallelTiles(const ExecutionContext& execution, const ParallelTileRequest& request)
{
    return derivePlan(execution, request, "an::planParallelTiles");
}

Status executeParallelTiles(const ExecutionContext& execution, const ParallelTilePlan& plan,
                            std::span<std::byte> scratch, ParallelTileBody body)
{
    constexpr const char* kOperation = "an::executeParallelTiles";
    // `derivePlan` expects a per-pixel cost. Validate geometry and scratch directly, then compare
    // the scheduler-dependent field separately rather than multiplying an already multiplied cost.
    if (execution.memoryResource() == nullptr)
    {
        return executorFailure(ErrorCode::NotInitialized, kOperation,
                               "a tile execution needs an operation that holds an engine context");
    }
    if (plan.tileWidth == 0u || plan.tileHeight == 0u || !isPowerOfTwo(plan.scratchAlignment))
    {
        return executorFailure(ErrorCode::InvalidArgument, kOperation, "the tile plan is malformed");
    }
    const std::uint64_t expectedColumns = plan.width == 0u ? 0u : ceilDivide(plan.width, plan.tileWidth);
    const std::uint64_t expectedRows = plan.height == 0u ? 0u : ceilDivide(plan.height, plan.tileHeight);
    if ((plan.width == 0u || plan.height == 0u) &&
        (plan.columns != 0u || plan.rows != 0u || plan.tileCount != 0u || plan.plannedWorkers != 0u ||
         plan.scratchBytesPerWorker != 0u || plan.scratchStrideBytes != 0u || plan.requiredScratchBytes != 0u))
    {
        return executorFailure(ErrorCode::InvalidArgument, kOperation, "an empty tile plan carries work");
    }
    if (plan.width != 0u && plan.height != 0u &&
        (plan.columns != expectedColumns || plan.rows != expectedRows ||
         plan.tileCount != expectedColumns * expectedRows))
    {
        return executorFailure(ErrorCode::InvalidArgument, kOperation, "the tile grid does not match its geometry");
    }
    const std::uint32_t expectedWorkers = plannedWorkerCount(execution, plan.tileCount, plan.tileCost);
    if (expectedWorkers != plan.plannedWorkers)
    {
        return executorFailure(ErrorCode::IncompatibleArguments, kOperation,
                               "the tile plan was made for a different admitted worker count");
    }
    constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();
    const std::uint64_t padding = plan.scratchAlignment - 1u;
    if (plan.scratchBytesPerWorker > kMaximum - padding)
    {
        return executorFailure(ErrorCode::InvalidArgument, kOperation, "the scratch partition overflows");
    }
    const std::uint64_t expectedStride =
        plan.scratchBytesPerWorker == 0u ? 0u : (plan.scratchBytesPerWorker + padding) & ~padding;
    std::uint64_t expectedScratchBytes = 0u;
    if (!checkedMultiply(expectedStride, plan.plannedWorkers, expectedScratchBytes) ||
        expectedStride != plan.scratchStrideBytes || expectedScratchBytes != plan.requiredScratchBytes)
    {
        return executorFailure(ErrorCode::InvalidArgument, kOperation, "the scratch partition is malformed");
    }
    if (scratch.size() < plan.requiredScratchBytes)
    {
        return executorFailure(ErrorCode::IncompatibleArguments, kOperation,
                               "the caller-owned scratch span is shorter than the tile plan requires");
    }
    if (plan.requiredScratchBytes != 0u &&
        reinterpret_cast<std::uintptr_t>(scratch.data()) % plan.scratchAlignment != 0u)
    {
        return executorFailure(ErrorCode::InvalidArgument, kOperation,
                               "the caller-owned scratch span does not meet the tile plan alignment");
    }

    auto run = [&](WorkerTask task) {
        for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
        {
            Status stopped = execution.checkCancellation();
            if (stopped.failed())
            {
                return stopped;
            }
            const std::uint32_t row = static_cast<std::uint32_t>(index / plan.columns);
            const std::uint32_t column = static_cast<std::uint32_t>(index % plan.columns);
            const std::uint64_t xBegin64 = static_cast<std::uint64_t>(column) * plan.tileWidth;
            const std::uint64_t yBegin64 = static_cast<std::uint64_t>(row) * plan.tileHeight;
            std::span<std::byte> workerScratch;
            if (plan.scratchBytesPerWorker != 0u)
            {
                workerScratch = scratch.subspan(static_cast<std::size_t>(task.workerSlot * plan.scratchStrideBytes),
                                                static_cast<std::size_t>(plan.scratchBytesPerWorker));
            }
            const ParallelTile tile{.index = index,
                                    .row = row,
                                    .column = column,
                                    .xBegin = static_cast<std::uint32_t>(xBegin64),
                                    .xEnd = static_cast<std::uint32_t>(
                                        (std::min)(xBegin64 + plan.tileWidth, static_cast<std::uint64_t>(plan.width))),
                                    .yBegin = static_cast<std::uint32_t>(yBegin64),
                                    .yEnd = static_cast<std::uint32_t>((
                                        std::min)(yBegin64 + plan.tileHeight, static_cast<std::uint64_t>(plan.height))),
                                    .workerSlot = task.workerSlot,
                                    .scratch = workerScratch};
            Status result = body(tile);
            if (result.failed())
            {
                return result;
            }
        }
        return Status::success();
    };
    return parallelForWorkers(execution, plan.tileCount, plan.tileCost, run);
}

} // namespace an
