// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - one integration pipeline stage measured on its own.
//
// The pipeline row's per-stage clocks say which stage of eight regressed, but a stage measured inside
// a 50-second run cannot be repeated often enough to decide a small loss, and its inputs arrive in
// whatever state the previous stages left the page cache in. A stage row runs exactly the stage the
// pipeline runs - the same frames, the same entry points, the same parameters and the same output -
// with nothing before it, and states the storage condition it reads its frames under instead of
// inheriting one.
//
// The master bias is the first stage: nothing calibrates it and nothing precedes it, so it is the
// average of the bias frames with exact zero read as no data. The engine stacks the frames from
// their files as the pipeline does. The row is scored against an exact average read from the same
// files by an independent reader.
//
// The final stack is the last stage: the average of the aligned lights, again with exact zero as no
// data, which is what an aligned light holds wherever no source pixel lands. Its input exists only
// inside a pipeline run, so it is produced once, outside any measurement, by one run that keeps its
// intermediates, and pinned frame by frame as a frame set of its own. Every run stacks those same
// files.
#pragma once

#include "PipelineFixture.h"
#include "Scenario.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// The master bias with its frames evicted from the operating system's cache before the clock
/// starts, so every read comes from storage.
inline constexpr const char* kMasterBiasColdScenarioId = "stages.master_bias.carina_red.cold";

/// The master bias with its frames read through the cache before the clock starts, so every read is
/// served from memory.
inline constexpr const char* kMasterBiasHotScenarioId = "stages.master_bias.carina_red.hot";

/// The largest relative residual of a master bias against the exact average of its frames.
///
/// The engine sums the exact integer codes exactly and narrows the quotient to Float32 once, which is
/// half a unit in the last place. One part in a million is the tier the stretch rows hold.
inline constexpr double kMasterBiasRelativeTolerance = 1e-6;

/// The frame set of the pipeline's aligned lights: one role, `lights`, holding the 72 lights the
/// pipeline aligned - Float32, on the reference light's grid - in the order it stacks them, divided by
/// the 16-bit full scale, which is the unit the engine reads a floating image in.
inline constexpr const char* kAlignedFrameSetId = "carina-asi294mm-red-aligned";

/// The final stack with its aligned lights evicted from the operating system's cache before the clock
/// starts, so every read comes from storage.
inline constexpr const char* kFinalStackColdScenarioId = "stages.final_stack.carina_red.cold";

/// The final stack with its aligned lights read through the cache before the clock starts, which is
/// the state the pipeline leaves them in: it has just written them.
inline constexpr const char* kFinalStackHotScenarioId = "stages.final_stack.carina_red.hot";

/// The largest residual of a final stack against the exact average of its aligned lights, relative
/// to the average magnitude of the samples averaged.
///
/// This engine sums the Float32 samples in double in frame order, which is the check's own
/// arithmetic, and narrows once. One part in a million is the master bias's tier.
inline constexpr double kFinalStackRelativeTolerance = 1e-6;

/// The storage state a stage row reads its frames in.
enum class StageCacheCondition
{
    /// Evicted from the operating system's cache: the drive's reading is part of the measurement.
    Cold,
    /// Read through the cache beforehand: the measurement is the engine's work over memory.
    Hot
};

/// The master bias stage row in one storage state.
[[nodiscard]] inline ScenarioSpec masterBiasStageScenario(StageCacheCondition condition)
{
    ScenarioSpec stage;
    stage.id = condition == StageCacheCondition::Cold ? kMasterBiasColdScenarioId : kMasterBiasHotScenarioId;
    stage.group = "stages";
    stage.inputSource = InputSource::CorpusFrameSet;
    stage.corpusId = kPipelineFrameSetId;
    stage.input.width = kPipelineFrameWidth;
    stage.input.height = kPipelineFrameHeight;
    stage.input.channels = 1u;
    stage.input.sampleType = SampleType::UInt16;
    stage.input.layout = Layout::Planar;
    stage.input.byteOrder = ByteOrder::Little;
    stage.correctnessClass = CorrectnessClass::AbsoluteRelative;
    stage.tolerance = kMasterBiasRelativeTolerance;
    stage.workUnit = "frames";
    stage.parameters["stage"] = "masterBias";
    stage.parameters["bias"] = std::to_string(kPipelineBiasCount);
    stage.parameters["cache"] = condition == StageCacheCondition::Cold ? "cold" : "hot";
    return stage;
}

/// The final stack stage row in one storage state.
[[nodiscard]] inline ScenarioSpec finalStackStageScenario(StageCacheCondition condition)
{
    ScenarioSpec stage;
    stage.id = condition == StageCacheCondition::Cold ? kFinalStackColdScenarioId : kFinalStackHotScenarioId;
    stage.group = "stages";
    stage.inputSource = InputSource::CorpusFrameSet;
    stage.corpusId = kAlignedFrameSetId;
    stage.input.width = kPipelineFrameWidth;
    stage.input.height = kPipelineFrameHeight;
    stage.input.channels = 1u;
    stage.input.sampleType = SampleType::Float32;
    stage.input.layout = Layout::Planar;
    stage.input.byteOrder = ByteOrder::Little;
    stage.correctnessClass = CorrectnessClass::AbsoluteRelative;
    stage.tolerance = kFinalStackRelativeTolerance;
    stage.workUnit = "frames";
    stage.parameters["stage"] = "finalStack";
    stage.parameters["lights"] = std::to_string(kPipelineLightCount);
    stage.parameters["cache"] = condition == StageCacheCondition::Cold ? "cold" : "hot";
    return stage;
}

/// The frames a stage reads, as UTF-8 paths in stack order.
using StageFramePaths = std::span<const std::string>;

/// What a runner does on either side of a stage's clock.
///
/// The engine's side of a stage row is only the stage; what the row declares about storage and how
/// it is scored are stated here, so the runner hands its stage these two calls rather than writing
/// them again.
struct StageClockHooks
{
    /// Runs after everything the stage does not time and immediately before its clock starts: puts
    /// the frames into the row's storage state. A false return stops the stage with the reason.
    std::function<bool(StageFramePaths, std::string&)> beforeClock;

    /// Runs immediately after the clock stops, while the stacked plane is still alive, with that
    /// plane as tight rows of Float32 values.
    std::function<void(StageFramePaths, std::span<const float>)> afterClock;
};

/// Every stage row, in catalogue order.
[[nodiscard]] inline std::vector<ScenarioSpec> pipelineStageScenarios()
{
    return {masterBiasStageScenario(StageCacheCondition::Cold), masterBiasStageScenario(StageCacheCondition::Hot),
            finalStackStageScenario(StageCacheCondition::Cold), finalStackStageScenario(StageCacheCondition::Hot)};
}

/// The final stack over the aligned lights rewritten as uncompressed XISF, read through the cache.
inline constexpr const char* kXisfStackStreamScenarioId = "pipeline.stack_stream_xisf.carina_red.hot";

/// The final stack over the aligned lights rewritten as XISF in Zstandard, in the form the XISF writer gives it when
/// nothing more is stated - the bytes of every sample shuffled, in subblocks of 4 MiB - read through the cache.
inline constexpr const char* kXisfZstdStackStreamScenarioId = "pipeline.stack_stream_xisf_zstd.carina_red.hot";

/// The final stack over the aligned lights rewritten as uncompressed XISF, its master written tile by tile into an XISF
/// image an output transaction reserved, read through the cache.
inline constexpr const char* kXisfReservedMasterScenarioId = "pipeline.stack_stream_xisf_reserved.carina_red.hot";

/// A final stack over the aligned lights rewritten as XISF, in the compression stated.
///
/// The frames are the final stack's own: decoded from their FITS files and written as XISF in the run's scratch
/// directory before anything is timed, then read through the cache, and stacked band by band through a row reader per
/// frame, as the final stack's rows read the FITS files - so the clock compares the two containers' bands. The plane
/// is checked against the exact average of the FITS files the copies were made from, and the row states beside its
/// clock how long one whole decode of every copy took, untimed.
///
/// @param id the row's identifier.
/// @param compression how the copies' pixel data are compressed, as the row names it.
[[nodiscard]] inline ScenarioSpec xisfStackStreamScenario(const char* id, const char* compression)
{
    ScenarioSpec stage = finalStackStageScenario(StageCacheCondition::Hot);
    stage.id = id;
    stage.group = "pipeline";
    stage.parameters["container"] = "XISF";
    stage.parameters["compression"] = compression;
    return stage;
}

/// The final stack over the uncompressed XISF copies with its master written as it is combined.
///
/// The stack is the uncompressed row's, and the stack's sink hands each finished tile of the master to an output
/// transaction that reserved the master as an XISF image before the first frame was read, so neither the stack nor
/// the row holds the master whole. The clock covers the reservation, the stack and the commit; the plane is read back
/// from the published unit, untimed, and checked against the same oracle. Beside the clock the row states the stack's
/// peak and the master's size, and, untimed, what the conventional path costs: the same stack handing its master over
/// whole and then writing it.
[[nodiscard]] inline ScenarioSpec xisfReservedMasterScenario()
{
    ScenarioSpec stage = xisfStackStreamScenario(kXisfReservedMasterScenarioId, "None");
    stage.parameters["output"] = "reserved";
    return stage;
}

/// Every final stack over the aligned lights rewritten as XISF, in catalogue order.
[[nodiscard]] inline std::vector<ScenarioSpec> xisfStackStreamScenarios()
{
    return {xisfStackStreamScenario(kXisfStackStreamScenarioId, "None"),
            xisfStackStreamScenario(kXisfZstdStackStreamScenarioId, "Zstd"), xisfReservedMasterScenario()};
}

/// Whether a final stack over XISF copies writes its master as it is combined, into a reserved image.
[[nodiscard]] inline bool writesReservedMaster(const ScenarioSpec& scenario)
{
    const auto output = scenario.parameters.find("output");
    return output != scenario.parameters.end() && output->second == "reserved";
}

/// Whether a scenario is a final stack over the aligned lights rewritten as XISF.
[[nodiscard]] inline bool isXisfStackStreamScenario(const ScenarioSpec& scenario)
{
    const auto container = scenario.parameters.find("container");
    const auto stage = scenario.parameters.find("stage");
    return scenario.group == "pipeline" && container != scenario.parameters.end() && container->second == "XISF" &&
           stage != scenario.parameters.end() && stage->second == "finalStack";
}

/// Whether a scenario is the master bias stage row, in either storage state.
[[nodiscard]] inline bool isMasterBiasStageScenario(const ScenarioSpec& scenario)
{
    const auto stage = scenario.parameters.find("stage");
    return scenario.group == "stages" && stage != scenario.parameters.end() && stage->second == "masterBias";
}

/// Whether a scenario is the final stack stage row, in either storage state.
[[nodiscard]] inline bool isFinalStackStageScenario(const ScenarioSpec& scenario)
{
    const auto stage = scenario.parameters.find("stage");
    return scenario.group == "stages" && stage != scenario.parameters.end() && stage->second == "finalStack";
}

/// Whether a scenario is any pipeline stage row.
[[nodiscard]] inline bool isPipelineStageScenario(const ScenarioSpec& scenario)
{
    return isMasterBiasStageScenario(scenario) || isFinalStackStageScenario(scenario);
}

/// The frame set role a stage row stacks: the bias frames, or the aligned lights.
[[nodiscard]] inline const char* stageFrameRole(const ScenarioSpec& scenario)
{
    return isFinalStackStageScenario(scenario) ? "lights" : "bias";
}

/// How many frames a stage row stacks.
[[nodiscard]] inline std::uint64_t stageFrameCount(const ScenarioSpec& scenario)
{
    return isFinalStackStageScenario(scenario) ? pipelineRoleLimit(scenario, "lights", kPipelineLightCount)
                                               : pipelineRoleLimit(scenario, "bias", kPipelineBiasCount);
}

/// The storage state a stage row declares, or nothing for a row that declares none it knows.
[[nodiscard]] inline std::optional<StageCacheCondition> stageCacheCondition(const ScenarioSpec& scenario)
{
    const auto cache = scenario.parameters.find("cache");
    if (cache == scenario.parameters.end())
    {
        return std::nullopt;
    }
    if (cache->second == "cold")
    {
        return StageCacheCondition::Cold;
    }
    if (cache->second == "hot")
    {
        return StageCacheCondition::Hot;
    }
    return std::nullopt;
}

} // namespace an::bench
