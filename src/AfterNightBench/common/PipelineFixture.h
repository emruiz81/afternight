// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the reference integration pipeline, declared once.
//
// This is the row the engine's end-to-end wall time and peak memory are read from. The runner and the
// suite of this layer take its declaration from this header rather than writing one each, because
// two hand-maintained copies of a declaration that must not differ is the arrangement that makes them
// differ.
//
// What the row measures is the *engine* pipeline: the entry points the ImageIntegration stages
// call, in the order they call them. The product workflow lives in AfterNightProcesses and no
// runner links it - manifest building, execution planning, artifact reuse, progress and archiving
// are all outside this. The engine runs those stages by orchestrating its published per-image entry
// points.
//
// The frames arrive as file paths and the engine opens them itself. The decode, the calibrated and
// aligned scratch files and the streaming that keeps the stack bounded are all inside the measured
// region, because they are a large part of what a pipeline costs and what its memory ceiling is
// stated against.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an::bench
{

/// The one pipeline row.
inline constexpr const char* kPipelineScenarioId = "pipelines.integration.carina_red";

/// The nominated reference dataset, 232 frames over the original FITS paths.
inline constexpr const char* kPipelineFrameSetId = "carina-asi294mm-red";

/// The largest per-light star-residual statistic a completed pipeline may report, in pixels.
///
/// The row is scored on the geometry the pipeline fitted, not on the samples it produced. A star
/// detector, a matcher and an interpolation may change and the stacked image with them; what the
/// pipeline is required to do is land every light on the reference frame. The residual the
/// registration reports for each light says whether it did, in pixels, which is a unit that means
/// the same thing on every host.
///
/// One and a half pixels is far above what the engine achieves on this dithered set and far
/// below what a pipeline that mismatched its fields would produce: this set dithers by roughly
/// thirty pixels peak to peak, so a registration that failed reports tens of pixels rather than a
/// fraction of one. The value is a ceiling on a correct run, not a target to approach.
inline constexpr double kPipelineResidualTolerancePixels = 1.5;

/// Per-frame counts of the reference set, as the manifest records them.
inline constexpr std::uint32_t kPipelineLightCount = 72u;
inline constexpr std::uint32_t kPipelineFlatCount = 40u;
inline constexpr std::uint32_t kPipelineDarkCount = 60u;
inline constexpr std::uint32_t kPipelineBiasCount = 60u;

/// The reference frame's geometry, which every frame of the set shares.
inline constexpr std::uint32_t kPipelineFrameWidth = 8288u;
inline constexpr std::uint32_t kPipelineFrameHeight = 5644u;

/// The complete declaration of the pipeline row.
///
/// Frame counts are parameters rather than constants because the run protocol says to trim the
/// light count and re-measure if one run is ever too slow for a variance sweep on the slower host.
/// That has to be a scenario edit rather than a code change - and it is why they are written here
/// as text: the parameters are part of the scenario digest, so a runner that read them from a
/// different place would be describing a different row.
[[nodiscard]] inline ScenarioSpec pipelineScenario()
{
    ScenarioSpec pipeline;
    pipeline.id = kPipelineScenarioId;
    pipeline.group = "pipelines";
    pipeline.inputSource = InputSource::CorpusFrameSet;
    pipeline.corpusId = kPipelineFrameSetId;
    pipeline.input.width = kPipelineFrameWidth;
    pipeline.input.height = kPipelineFrameHeight;
    pipeline.input.channels = 1u;
    pipeline.input.sampleType = SampleType::UInt16;
    pipeline.input.layout = Layout::Planar;
    pipeline.input.byteOrder = ByteOrder::Little;
    // A geometric residual in pixels rather than a sample difference: see the tolerance above.
    pipeline.correctnessClass = CorrectnessClass::GeometricResidual;
    pipeline.tolerance = kPipelineResidualTolerancePixels;
    pipeline.workUnit = "frames";
    pipeline.parameters["lights"] = std::to_string(kPipelineLightCount);
    pipeline.parameters["flats"] = std::to_string(kPipelineFlatCount);
    pipeline.parameters["darks"] = std::to_string(kPipelineDarkCount);
    pipeline.parameters["bias"] = std::to_string(kPipelineBiasCount);
    return pipeline;
}

/// How many frames of a role a scenario's limit parameter actually selects.
///
/// Zero, an absent parameter and anything unparseable all mean "every frame the set holds", which
/// is what the unmodified row asks for. A limit above the role's size selects the role.
[[nodiscard]] inline std::size_t pipelineRoleLimit(const ScenarioSpec& scenario, const char* name,
                                                   std::size_t available)
{
    const auto at = scenario.parameters.find(name);
    if (at == scenario.parameters.end())
    {
        return available;
    }
    std::size_t requested = 0u;
    try
    {
        requested = static_cast<std::size_t>(std::stoul(at->second));
    }
    catch (const std::exception&)
    {
        return available;
    }
    return requested == 0u ? available : (std::min)(requested, available);
}

/// How many bytes one tile of a pipeline stack holds across every frame at once.
///
/// A stack whose source decodes each frame's band into the tile folds the tile once every frame's
/// band has landed, so the tile is read back after it was written; a tile wider than the
/// last-level cache comes back from memory, one stream per frame, which no prefetcher follows at
/// seventy-two frames. Half of one core complex's 32 MiB cache keeps a tile resident between the
/// two with room for the terms. Measured on the reference set's 72 aligned lights, hot: 7 rows
/// (16.7 MB) stack in 2.5 s on one worker where 84 rows (192 MB) took 5.3 s, and 10 rows (24 MB)
/// already take 3.4 s.
inline constexpr std::uint64_t kPipelineStackTileBytes = 16u * 1024u * 1024u;

/// The most rows one tile of a pipeline stack holds, however narrow its frames.
inline constexpr std::uint32_t kPipelineStackMostTileRows = 1024u;

/// How many rows of every frame one tile of a pipeline stack holds.
///
/// For a stack whose source decodes each band straight into the tile - the master bias and the final
/// stack. Chosen rather than left to the planner, which does not know that the source decodes each
/// band into the tile: `kPipelineStackTileBytes` of every frame's band, counting each sample at least
/// as wide as the Float32 a tile holds, and never fewer rows than the operation's workers, because
/// the fold spreads a tile's rows over them. At sixteen workers across two core complexes that is
/// sixteen rows of the aligned lights, 38 MB, which measured fastest there. Reading a band of a few
/// rows costs nothing extra, because each frame's file stays open for the whole stack.
///
/// @param frameCount how many frames the stack reads.
/// @param width the frames' width.
/// @param elementBytes one stored sample's size.
/// @param workers the operation's admitted width; zero is read as one.
/// @return at least one row, and at most `kPipelineStackMostTileRows`.
[[nodiscard]] inline std::uint32_t pipelineStackTileRows(std::size_t frameCount, std::uint32_t width,
                                                         std::uint32_t elementBytes, std::uint32_t workers) noexcept
{
    const std::uint64_t sampleBytes = (std::max)(elementBytes, 4u);
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(frameCount) * width * sampleBytes;
    const std::uint64_t cached = rowBytes == 0u ? kPipelineStackMostTileRows : kPipelineStackTileBytes / rowBytes;
    const std::uint64_t rows = (std::max)(cached, static_cast<std::uint64_t>((std::max)(workers, 1u)));
    return static_cast<std::uint32_t>((std::min)(rows, std::uint64_t{kPipelineStackMostTileRows}));
}

/// How many bytes one band of a stack that calibrates may reach across every frame at once.
inline constexpr std::uint64_t kPipelineStackBandBytes = 192u * 1024u * 1024u;

/// How many rows of every frame one tile of a pipeline stack that calibrates holds.
///
/// A stack that calibrates on the way in reads one frame at a time through its read slot and
/// calibrates each band on the operation's workers before it lands in the tile, so the tile is what
/// that per-band work is spread over, and a tile sized for the cache multiplies it: at sixteen
/// workers the master dark took 2.37 s and the master flat 1.56 s with cache-sized tiles, against
/// 2.03 s and 1.28 s with this band budget. The band budget counts each stored sample at its own
/// size, with at least sixteen rows.
///
/// @param frameCount how many frames the stack reads.
/// @param width the frames' width.
/// @param elementBytes one stored sample's size.
/// @return at least sixteen rows, and at most `kPipelineStackMostTileRows`.
[[nodiscard]] inline std::uint32_t pipelineCalibratingStackTileRows(std::size_t frameCount, std::uint32_t width,
                                                                    std::uint32_t elementBytes) noexcept
{
    constexpr std::uint64_t kFewestRows = 16u;
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(frameCount) * width * elementBytes;
    if (rowBytes == 0u)
    {
        return static_cast<std::uint32_t>(kFewestRows);
    }
    const std::uint64_t rows = (std::max)(kPipelineStackBandBytes / rowBytes, kFewestRows);
    return static_cast<std::uint32_t>((std::min)(rows, std::uint64_t{kPipelineStackMostTileRows}));
}

/// One stage's wall time and the process CPU time spent inside it, both in milliseconds.
///
/// The pair is what makes a stage interpretable, and neither half is useful alone. A stage whose
/// CPU time sits far below its wall time is waiting - for storage, most often - and no faster
/// kernel will improve it; a stage whose CPU time is a large multiple of its wall time is using the
/// machine and its cost is arithmetic. Those two call for opposite work, and a wall time on its own
/// cannot tell them apart.
struct PipelineStageTiming
{
    double wallMs = 0.0;
    double cpuMs = 0.0;
};

/// The shape of a per-light residual set, in pixels.
struct PipelineResidualSummary
{
    /// Lights that reported a residual at all.
    std::size_t count = 0u;

    /// The statistic the row is scored on: the median per-light residual.
    double medianPixels = 0.0;

    /// The tails, reported beside the median so a single bad frame is visible rather than hidden.
    double minimumPixels = 0.0;
    double maximumPixels = 0.0;
};

/// Summarizes the per-light registration residuals the engine reports.
///
/// Shared so every run computes one statistic rather than two that resemble each other. The
/// median is the scored figure because one light of a 72-frame set is allowed to be the worst one
/// without the pipeline being wrong; the extremes travel beside it as counters so a run whose worst
/// frame moved is not summarised into looking unchanged.
///
/// Non-finite entries are not measurements and are left out of all four figures. A set with no
/// finite entry summarises to nothing, which a caller must treat as a pipeline that measured no
/// geometry rather than as a residual of zero.
[[nodiscard]] inline PipelineResidualSummary summarizePipelineResiduals(std::span<const double> residuals)
{
    std::vector<double> finite;
    finite.reserve(residuals.size());
    for (const double value : residuals)
    {
        if (value >= 0.0 && value < std::numeric_limits<double>::infinity())
        {
            finite.push_back(value);
        }
    }
    if (finite.empty())
    {
        return PipelineResidualSummary{};
    }
    std::sort(finite.begin(), finite.end());
    const std::size_t middle = finite.size() / 2u;
    const double median = finite.size() % 2u == 1u ? finite[middle] : 0.5 * (finite[middle - 1u] + finite[middle]);
    return PipelineResidualSummary{finite.size(), median, finite.front(), finite.back()};
}

/// Per-stage clocks and frame counts, recorded as result counters.
///
/// One aggregate time cannot be acted on: a pipeline that regressed says nothing about which of
/// eight stages did. These travel beside the measurement rather than replacing it, under stable
/// names so a stage can be compared across runs.
struct PipelineStageTimings
{
    PipelineStageTiming masterBias;
    PipelineStageTiming masterDark;
    PipelineStageTiming masterFlat;
    PipelineStageTiming calibrateLights;
    PipelineStageTiming detectStars;
    PipelineStageTiming registerStars;
    PipelineStageTiming align;
    PipelineStageTiming stack;

    /// Frames actually read, which is not the set's size when the counts are trimmed.
    std::size_t framesProcessed = 0;

    /// How many lights survived each stage that can drop one.
    ///
    /// A pipeline that calibrated 72 frames, registered 60 and stacked 22 has not run a faster
    /// version of the same work - it has run different work, and its wall time is not comparable
    /// with a run that kept them all. These counters are what make that visible instead of hiding
    /// it inside an aggregate that looks fine.
    std::size_t lightsCalibrated = 0;
    std::size_t lightsRegistered = 0;
    std::size_t lightsAligned = 0;
    std::size_t lightsStacked = 0;

    /// The geometry the registration fitted, which is what the row is scored on.
    PipelineResidualSummary residuals;

    /// The most working memory one frame's detection and one frame's resampling asked their engine
    /// for, where the engine reports it, and zero where it does not.
    ///
    /// The pipeline's peak resident figure is one number for eight stages, and it says nothing
    /// about which of them set it. These two say whether a per-frame operation's own working set
    /// is what a run's peak is made of, which is the difference between a pipeline that holds too
    /// much and an operation that asks for too much.
    std::uint64_t peakDetectionWorkingBytes = 0;
    std::uint64_t peakAlignmentWorkingBytes = 0;

    /// What the reference frame's detection found, which is what every transform is fitted against.
    ///
    /// A pipeline whose registration refuses its reference field has usually calibrated badly
    /// rather than matched badly, and these three say which: a field of no stars over a background
    /// of nothing is a black frame, not a hard match.
    std::size_t referenceStars = 0;
    double referenceBackground = 0.0;
    double referenceBackgroundNoise = 0.0;

    /// Fold into a result's counters under stable names.
    void writeCounters(ResultRecord& out) const
    {
        // The wall names are the ones recorded runs carry. Renaming one would not fail anything here;
        // it would quietly make a comparison with a recorded run compare nothing.
        const std::pair<const char*, const PipelineStageTiming*> stages[] = {{"masterBias", &masterBias},
                                                                             {"masterDark", &masterDark},
                                                                             {"masterFlat", &masterFlat},
                                                                             {"calibrateLights", &calibrateLights},
                                                                             {"detectStars", &detectStars},
                                                                             {"registerStars", &registerStars},
                                                                             {"align", &align},
                                                                             {"stack", &stack}};
        for (const auto& stage : stages)
        {
            out.counters[std::string("stage.") + stage.first + "Ms"] = stage.second->wallMs;
            out.counters[std::string("stage.") + stage.first + "CpuMs"] = stage.second->cpuMs;
        }
        out.counters["framesProcessed"] = static_cast<double>(framesProcessed);
        out.counters["lightsCalibrated"] = static_cast<double>(lightsCalibrated);
        out.counters["lightsRegistered"] = static_cast<double>(lightsRegistered);
        out.counters["lightsAligned"] = static_cast<double>(lightsAligned);
        out.counters["lightsStacked"] = static_cast<double>(lightsStacked);
        out.counters["alignmentResidualFrames"] = static_cast<double>(residuals.count);
        out.counters["alignmentResidualMedianPx"] = residuals.medianPixels;
        out.counters["alignmentResidualMinPx"] = residuals.minimumPixels;
        out.counters["alignmentResidualMaxPx"] = residuals.maximumPixels;
        out.counters["peakDetectionWorkingBytes"] = static_cast<double>(peakDetectionWorkingBytes);
        out.counters["peakAlignmentWorkingBytes"] = static_cast<double>(peakAlignmentWorkingBytes);
        out.counters["referenceStars"] = static_cast<double>(referenceStars);
        out.counters["referenceBackground"] = referenceBackground;
        out.counters["referenceBackgroundNoise"] = referenceBackgroundNoise;
    }
};

} // namespace an::bench
