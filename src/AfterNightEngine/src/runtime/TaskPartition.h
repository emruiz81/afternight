// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a parallel loop that states how finely its items are divided.
//
// `parallelFor` offers each admitted worker `kChunksPerWorker` chunks, which balances a loop whose
// chunks take the time their items say they will. A loop of costly, uniform items does not balance
// that way on cores the host is also giving to other work: whichever chunk starts last decides when
// the loop ends, and with four chunks a worker, one late chunk is a quarter of a worker's share. A
// loop that has measured this states more chunks a worker here. The chunks are claimed in order by
// whichever worker is free, exactly as `parallelFor`'s are, and none is made smaller than the least
// work a task is worth scheduling for.

#pragma once

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <cstdint>

namespace an
{
class ExecutionContext;

/// How many items one task of a loop offered `chunksPerWorker` chunks a worker should take.
///
/// `chooseGrainSize` with `chunksPerWorker` in place of `kChunksPerWorker`: the larger of the items
/// that make a task worth scheduling and the items that give every worker that many chunks, and never
/// more than `itemCount`.
///
/// @param itemCount how many items the loop covers.
/// @param cost what one item costs.
/// @param concurrency how many workers will walk the partition. Zero is read as one.
/// @param chunksPerWorker how many chunks each worker is offered. Zero is read as one.
/// @return the grain, which is at least one.
[[nodiscard]] std::uint64_t chooseGrainSizeInChunks(std::uint64_t itemCount, const TaskCost& cost,
                                                    std::uint32_t concurrency, std::uint32_t chunksPerWorker) noexcept;

/// Runs a loop as `parallelFor` does, with `chunksPerWorker` chunks offered to each admitted worker.
///
/// Admission, the arena, the cancellation checkpoints, allocation behaviour and failure ordering are
/// `parallelFor`'s, and the grain is `chooseGrainSizeInChunks`'s.
///
/// @param execution the operation this loop belongs to. Must hold something.
/// @param itemCount how many items to cover. Zero succeeds without calling the body.
/// @param cost what one item costs, for the smallest task worth scheduling.
/// @param chunksPerWorker how many chunks each admitted worker is offered. Zero is read as one.
/// @param body what to run for each range. Calls may overlap, on disjoint ranges.
/// @return what `parallelFor` returns for the same loop.
[[nodiscard]] Status parallelForInChunks(const ExecutionContext& execution, std::uint64_t itemCount,
                                         const TaskCost& cost, std::uint32_t chunksPerWorker, TaskBody body);

} // namespace an
