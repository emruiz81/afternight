// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the instruction loops themselves, with their arguments already checked.
//
// Private to the pixel-math domain, and it exists for one reason: an image walk evaluates the same
// program at every sample of every channel, and every check the published entry point performs -
// that the image count matches, that no pointer is null, that the scratch is big enough - is a check
// about the *invocation* rather than about the pixel. Performing it seventy-two million times is
// paying for the same answer seventy-two million times.
//
// So the checks live in the published entry point and the arithmetic lives here, and the walk checks
// once and then calls this. There are two loops over the instructions: one evaluates one sample, and
// is what `evaluatePixelMathPixel` and the reference walk run; the other evaluates a run of adjacent
// samples an instruction at a time, which is what a walk runs for a program with no branch, whose
// samples all take the same instructions. Both apply each operator, function, sample read and random
// draw through the same functions, one sample at a time, so the run computes what the single sample
// computes, bit for bit; only how often an instruction is decoded differs.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathEvaluator.h>
#include <AfterNightEngine/Core/Result.h>

#include "core/PositionalRandom.h"

#include <cstdint>
#include <span>
#include <vector>

namespace an
{

/// One colour component the program reads, with its conversion already planned.
struct PlannedColorRead
{
    /// The validated conversion from the image's stated encoding to the component's space.
    ColorConversionPlan plan;

    /// Which of the program's images it reads.
    std::uint32_t image = 0u;

    /// Which channel of the converted pixel the component is.
    std::uint32_t channel = 0u;
};

/// Every colour conversion one program needs, in the order its colour reads are numbered.
using PlannedColorReads = std::vector<PlannedColorRead>;

/// Builds and reads the handle the published surface hands around.
///
/// Declared here and defined beside the evaluator, so that the walk can reach the planned
/// conversions without the handle exposing a colour type on the published surface.
class PixelMathColorPlanner
{
public:
    /// Plans every conversion a program asks for.
    ///
    /// @param program the compiled expression.
    /// @return the handle, empty when the program reads no colour component; the colour transform's
    ///         own failure; or `ErrorCode::Internal` for a colour read this build cannot plan.
    [[nodiscard]] static Result<PixelMathColorPlans> plan(const PixelMathProgram& program);

    /// The conversions a handle holds.
    ///
    /// @param handle the handle to read.
    /// @return the plans, or null when the handle holds none.
    [[nodiscard]] static const PlannedColorReads* read(const PixelMathColorPlans& handle) noexcept;
};

/// Produces one deterministic draw for one expression site.
///
/// Shared by the general interpreter and instruction-shape specializations so an optimized path
/// cannot acquire a different random stream. Thread-safe and allocation-free.
///
/// @param seed the execution seed.
/// @param x the sample column.
/// @param y the sample row.
/// @param channel the sample channel.
/// @param index how many draws this site has already made, which counts only the draws an
///        evaluation actually executed rather than every random instruction the program holds.
/// @return one value in the half-open interval [0, 1).
[[nodiscard]] inline double pixelMathRandomDraw(std::uint64_t seed, std::uint32_t x, std::uint32_t y,
                                                std::uint32_t channel, std::uint64_t index) noexcept
{
    return positionalUniformDraw(seed, x, y, channel, index);
}

/// Runs one compiled expression at one site, with every argument already checked.
///
/// The caller has established that `images` is as long as the program's image list and holds no
/// null, that `statistics` is as long as the program's request list, that the scratch is at least as
/// large as the program asks for, and that `colorPlans` matches the program's colour reads. Nothing
/// here re-establishes any of that.
///
/// Thread-safe and reentrant with respect to everything it is given, all of which it only reads
/// except the scratch. Allocation-free.
///
/// @param program the compiled expression.
/// @param images the bound images, in the program's own order.
/// @param statistics one value per statistic request, for the channel being written.
/// @param colorPlans the planned conversions, or null when the program reads no colour component.
/// @param site where the evaluation is happening and what its random stream is.
/// @param scratch the variables and the stack.
/// @return the value; the colour transform's failure for a pixel it cannot convert; or
///         `ErrorCode::Internal` for an instruction this build cannot execute or a program whose
///         stack use is inconsistent.
[[nodiscard]] Result<double>
runPixelMathProgram(const PixelMathProgram& program, std::span<const PixelMathImageView> images,
                    std::span<const double> statistics, const PlannedColorReads* colorPlans,
                    const PixelMathEvaluationSite& site, const PixelMathEvaluationScratch& scratch);

/// The most samples one run evaluates together.
inline constexpr std::uint32_t kPixelMathRunLength = 128u;

/// Adjacent samples of one row of one channel, evaluated together.
struct PixelMathEvaluationRun
{
    /// The first column.
    std::uint32_t x = 0u;

    /// How many columns, at least one and at most the scratch's lane count.
    std::uint32_t count = 0u;

    /// The row being written.
    std::uint32_t y = 0u;

    /// The channel being written.
    std::uint32_t channel = 0u;

    /// The execution's random seed.
    std::uint64_t randomSeed = 0u;
};

/// The working memory one run needs, owned by the caller.
struct PixelMathRunScratch
{
    /// How many samples each stack slot and each variable holds; a run is no longer than this.
    std::uint32_t lanes = 0u;

    /// Stack slots of `lanes` values each, one after another. How many slots there are is the
    /// stack's capacity, which the single-sample loop takes from its own stack's length.
    std::span<double> stack{};

    /// One block of `lanes` values per variable the program declares.
    std::span<double> variables{};

    /// As many values as there are stack slots: one sample's call arguments, gathered from their
    /// slots, and the room above them.
    std::span<double> arguments{};
};

/// Whether every sample takes the same instructions, which is what a run requires.
///
/// @param program the compiled expression.
/// @return true when the program has no jump, and so no conditional.
[[nodiscard]] bool pixelMathProgramIsStraightLine(const PixelMathProgram& program) noexcept;

/// Runs one compiled expression over a run of adjacent samples, one instruction at a time for all of
/// them.
///
/// Each sample's value is the one `runPixelMathProgram` produces at that sample, bit for bit: every
/// operator, function, read and draw is applied to each sample through the same function, in the same
/// order, and the stack and draw counts are the same at every sample because the program does not
/// branch. The caller has established what `runPixelMathProgram` requires, that the program is
/// straight-line, that the scratch has at least `stackDepth()` slots of `lanes` values, one block
/// per variable and a slot's worth of arguments, and that the run is no longer than `lanes`.
///
/// Thread-safe and reentrant with respect to everything it is given, all of which it only reads
/// except the scratch and `values`. Allocation-free.
///
/// @param program the compiled expression.
/// @param images the bound images, in the program's own order.
/// @param statistics one value per statistic request, for the channel being written.
/// @param colorPlans the planned conversions, or null when the program reads no colour component.
/// @param run which samples, and what their random stream is.
/// @param scratch the stack, the variables and the call arguments.
/// @param values where each sample's value goes, at least `run.count` long.
/// @return success; the colour transform's failure for the first sample it cannot convert; or
///         `ErrorCode::Internal` where `runPixelMathProgram` reports it, and for a jump.
[[nodiscard]] Status runPixelMathProgramOverRun(const PixelMathProgram& program,
                                                std::span<const PixelMathImageView> images,
                                                std::span<const double> statistics, const PlannedColorReads* colorPlans,
                                                const PixelMathEvaluationRun& run, const PixelMathRunScratch& scratch,
                                                std::span<double> values);

} // namespace an
