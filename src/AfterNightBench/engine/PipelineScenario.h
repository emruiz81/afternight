// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineBench - the integration pipeline, measured end to end.
//
// The row lives in the shared contract; this is how this engine runs it, over the reference frame set,
// stage by stage, writing its intermediates to the scratch protocol the contract states.
//
// The engine offers per-image entry points and a streaming stack executor, so the orchestration of
// the stages is here, in the runner, where the ImageIntegration stages have it. No stage calls
// anything but this engine's published surface.
//
// The frames arrive as paths and the engine opens them itself. Decoding, the calibrated and aligned
// scratch files and the streaming that keeps the stack bounded are all inside the measured region,
// for the reason the shared contract gives: they are a large part of what a pipeline costs and what
// its memory ceiling is stated against.

#pragma once

#include "Fixture.h"
#include "PipelineFixture.h"
#include "PipelineStageFixture.h"
#include "RunnerHarness.h"
#include "Scenario.h"

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstdint>
#include <string>

namespace an::bench::engine
{

/// Run the integration pipeline over `frames` and return the digest of the final stacked image.
///
/// `scratchRoot` receives the calibrated and aligned intermediates - roughly twice the light set -
/// and is removed by the caller. `timings` is filled per stage.
///
/// Returns false with `error` set when a stage fails or the set is not usable. A pipeline that
/// could not complete has measured nothing, and is reported as a skip rather than as a timing.
[[nodiscard]] bool runIntegrationPipeline(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                          const FixtureSet& frames, const ScenarioSpec& scenario,
                                          const std::string& scratchRoot, PipelineStageTimings& timings,
                                          std::string& digest, std::string& error);

/// Run the pipeline's first stage on its own: the master bias, stacked from the bias frames' files
/// exactly as `runIntegrationPipeline` stacks it.
///
/// Everything the pipeline does before that stage runs untimed first, then `hooks.beforeClock`;
/// `timing` covers the stack alone, as the pipeline's stage clock does, and `hooks.afterClock` is
/// handed the stacked plane in the frames' code units. `frameReads` receives how many bands the
/// stack read and `digest` the digest of the plane.
///
/// Returns false with `error` set when the stage could not run, which is a skip rather than a
/// timing.
[[nodiscard]] bool runMasterBiasStage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                      const FixtureSet& frames, const ScenarioSpec& scenario,
                                      const StageClockHooks& hooks, PipelineStageTiming& timing,
                                      std::uint64_t& frameReads, std::string& digest, std::string& error);

/// Run the pipeline's last stage on its own: the final stack, stacked from the aligned lights' files
/// exactly as `runIntegrationPipeline` stacks the lights it aligned.
///
/// `frames` is the aligned lights' frame set. Their description is probed from the first light,
/// untimed, then `hooks.beforeClock` runs; `timing` covers the stack alone, as the pipeline's stage
/// clock does, and `hooks.afterClock` is handed the stacked plane in the lights' own units.
/// `frameReads` receives how many bands the stack read and `digest` the digest of the plane.
///
/// Returns false with `error` set when the stage could not run, which is a skip rather than a
/// timing.
[[nodiscard]] bool runFinalStackStage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                      const FixtureSet& frames, const ScenarioSpec& scenario,
                                      const StageClockHooks& hooks, PipelineStageTiming& timing,
                                      std::uint64_t& frameReads, std::string& digest, std::string& error);

/// What a final stack over XISF copies states beside its clock.
struct XisfStackStreamFigures
{
    /// How long one whole decode of every copy took, outside the clock.
    double wholeReadMs = 0.0;

    /// Whether the row wrote its master into a reserved image as it was combined, which the figures below describe.
    bool reservedMaster = false;

    /// The most the stack held, and the most its plan allowed, with the master taken tile by tile.
    std::uint64_t peakHeldBytes = 0u;
    std::uint64_t plannedPeakBytes = 0u;

    /// The published master's size.
    std::uint64_t masterBytes = 0u;

    /// Outside the clock: how long the same stack took handing its master over whole and then writing it, and what it
    /// held and planned to.
    double wholeThenWriteMs = 0.0;
    std::uint64_t wholePeakHeldBytes = 0u;
    std::uint64_t wholePlannedPeakBytes = 0u;
};

/// Run the final stack over the aligned lights rewritten as XISF: the stack `runFinalStackStage` runs, over copies.
///
/// `frames` is the aligned lights' frame set. Untimed, each light is decoded from its FITS file and written as XISF
/// under `scratchRoot` in the compression the row names, and every copy is decoded whole once, which
/// `figures.wholeReadMs` receives; then `hooks.beforeClock` runs over the copies, `timing` covers the stack of the
/// copies alone, and `hooks.afterClock` is handed the stacked plane with the FITS files' paths, whose exact average it
/// is checked against. `frameReads` receives how many bands the stack read and `digest` the digest of the plane. A row
/// that writes a reserved master (`writesReservedMaster`) first runs the stack with its master handed over whole and
/// written, untimed, then times the reservation, the stack handing each tile to it and the commit, and checks the plane
/// read back from the published unit; `figures` receives both runs' figures.
///
/// Returns false with `error` set when the stage could not run, which is a skip rather than a timing.
[[nodiscard]] bool runXisfStackStreamStage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                           const FixtureSet& frames, const ScenarioSpec& scenario,
                                           const std::string& scratchRoot, const StageClockHooks& hooks,
                                           PipelineStageTiming& timing, XisfStackStreamFigures& figures,
                                           std::uint64_t& frameReads, std::string& digest, std::string& error);

} // namespace an::bench::engine
