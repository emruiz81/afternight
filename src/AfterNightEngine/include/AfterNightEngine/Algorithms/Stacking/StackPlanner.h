// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A stack planner takes a checked model and returns an inspectable plan.
///
/// `planStack` is where every decision about how a stack runs is made once - the strategy, the
/// widths, the tile, the passes, the memory, the reads and the backend - so a caller can read the
/// cost of a run before it starts and refuse it, and the working set and the executor follow the
/// plan rather than deciding again. It reads nothing and allocates nothing but a failure it has to
/// describe; the plan's parts are described in `Algorithms/Stacking/StackPlan.h`.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>

namespace an
{

class StackModel;
class ExecutionContext;

/// Decides how a stack will run, and says why.
///
/// The strategy and the widths follow from the model; the tile, the passes, the bytes and the
/// reads follow from the model, the request and what the execution context offers; the backend is
/// the dispatch table's answer. Every decision is reported to the execution context's diagnostic
/// sink at `DiagnosticSeverity::Info`, one sentence each, and a combine pass that re-reads whole
/// frames for every tile is reported at `DiagnosticSeverity::Warning` with the multiplied figures.
/// An execution given no sink is planned in silence.
///
/// The budget is the engine context's configured memory budget less what it already has
/// outstanding, narrowed by the caller's own limit when one is stated. It is a snapshot: what is
/// outstanding moves, and an executor allocating against the plan checks again.
///
/// Thread-safe with respect to `execution`, which is only read. Allocates only a failure it has to
/// describe.
///
/// @param execution the operation the stack will run as. Must hold something.
/// @param model the checked stack.
/// @param request the facts about the run the model does not carry.
/// @return the plan; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for an unknown access or no concurrent reads, naming the
///         field; `ErrorCode::UnsupportedBackend` when the operation is constrained to a backend
///         this build cannot serve; `ErrorCode::LimitExceeded` when the byte or read accounting is
///         not representable; and `ErrorCode::MemoryBudgetExceeded` when the persistent state, a
///         whole-frame measurement pass, one row of the combine pass or a whole-frame read under a
///         streaming strategy does not fit, carrying the requested, available and limit bytes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackPlan>
planStack(const ExecutionContext& execution, const StackModel& model, const StackPlanningRequest& request);

} // namespace an
