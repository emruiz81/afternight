// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which route drizzled frames take into a stack: rendered frame by frame as the stack reads them,
/// or read back from a dump history the caller materialized as a reference to compare against.
///
/// `planDrizzleStackWorkflow` reads a checked `StackModel` and the caller's policy and returns a
/// `DrizzleStackWorkflowPlan`: the route, whether the stack needs sample history and why, and the
/// accumulation strategy the stack executor will use. It opens no file and allocates nothing. The
/// automatic route always renders through `DrizzledStackFrameSource` in `DrizzleStackSource.h`; the
/// dump-history route is only ever the caller's explicit choice, and the caller then supplies the
/// `StackFrameSource` that reads its history.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>

namespace an
{

class StackModel;

/// Which route a caller requests for drizzled frames entering a stack.
enum class DrizzleStackExecutionPolicy : std::uint8_t
{
    /// Select the bounded direct source and let the stack planner choose its accumulation strategy.
    Automatic,

    /// Materialize versioned dump history and expose it through a caller-owned `StackFrameSource`.
    DumpHistoryReference
};

/// The source route selected for a drizzled stack.
enum class DrizzleStackExecutionPath : std::uint8_t
{
    /// No plan has been made yet.
    NotEvaluated,

    /// Render each frame through `DrizzledStackFrameSource` as the stack asks for it.
    DirectPerFrameSource,

    /// Read caller-materialized `DumpHistory` through a `StackFrameSource` adapter.
    DumpHistoryReference
};

/// Why the workflow does or does not need sample history.
enum class DrizzleStackHistoryReason : std::uint8_t
{
    /// No plan has been made yet.
    NotEvaluated,

    /// Whole-output accumulation needs no sample history.
    None,

    /// The caller explicitly requested the dump-backed reference route.
    ReferencePolicy,

    /// An extremum or variance is reduced while successive per-frame renders are available.
    DeferredPerFrameReduction,

    /// The combination or rejection needs the stack executor's bounded tile sample history.
    SampleHistory
};

/// The route and the stack strategy it composes with.
struct DrizzleStackWorkflowPlan
{
    /// Selected source route.
    DrizzleStackExecutionPath path = DrizzleStackExecutionPath::NotEvaluated;

    /// Typed reason for retaining or avoiding sample history.
    DrizzleStackHistoryReason historyReason = DrizzleStackHistoryReason::NotEvaluated;

    /// Strategy the checked stack model requires after the source produces a frame.
    StackAccumulationStrategy accumulationStrategy = StackAccumulationStrategy::WholeOutputAccumulators;

    /// Whether the caller must materialize versioned dump history before `executeStack`.
    bool dumpBacked = false;
};

/// Selects the complete drizzled-stack route without opening files or allocating memory.
///
/// The automatic route always uses `DrizzledStackFrameSource`. Rejection, median and auto-adaptive
/// combination retain only the executor's bounded tile history; minimum, maximum and a requested
/// variance map reduce successive rendered frames without a disk spill. The reference policy is
/// deliberately explicit and selects a caller-composed `DumpHistory` source to compare against.
///
/// Thread-safe and allocation-free.
///
/// @param model checked combination, rejection and output request.
/// @param policy requested compatibility route.
/// @return the selected path and typed reason, or `ErrorCode::InvalidArgument` for a policy outside
///         the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleStackWorkflowPlan>
planDrizzleStackWorkflow(const StackModel& model, DrizzleStackExecutionPolicy policy);

} // namespace an
