// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the two published total-variation minimizations.
//
// Both minimise the Rudin-Osher-Fatemi functional one channel at a time, over Float32 planes with
// Float64 reductions, and neither carries a library's code: Chambolle's projection iteration on the
// dual variable is written to the discretisation scikit-image uses, and Goldstein and Osher's
// split-Bregman method from its publication. The library-parity suite holds Chambolle's iteration to
// that library bit for bit; the split-Bregman route relaxes by red-black Gauss-Seidel half-sweeps and
// applies the published soft threshold rather than the library's smooth approximation of it, so its
// intermediate states differ from the library's while it reaches the same minimum.
//
// Six working planes are taken from the engine context once and reused for every channel, and the
// answer is the same at any thread count. When validity rejects samples, one additional input
// plane substitutes zero before the solve. Rejected destination samples are left untouched in
// both direct and gathered layouts; a rejected non-finite input never reaches the iteration.

#include <AfterNightEngine/Algorithms/NoiseReduction/TotalVariationDenoise.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::denoiseImageTotalVariation";
constexpr const char* kStage = "total-variation denoise";

/// The merge a sum uses, as the plain function pointer the reduction takes.
///
/// @param left one partial.
/// @param right the other.
/// @return their sum.
[[nodiscard]] double add(const double left, const double right) noexcept
{
    return left + right;
}

/// How many Float32 planes either method keeps beside the channel it is working on.
constexpr std::uint32_t kWorkingPlanes = 6u;

/// How much work one row of a plane pass is.
///
/// @param width how many samples one row holds.
/// @param perSample how many operations each sample costs.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(const std::uint32_t width, const std::uint64_t perSample) noexcept
{
    const auto samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * sizeof(float), .operationsPerItem = samples * perSample};
}

/// Whether an output policy names a rule this operation implements.
///
/// @param output the policy.
/// @return success, or why the policy is not usable.
[[nodiscard]] Status checkOutputPolicy(const SpatialOutputPolicy& output)
{
    if (!isOutputPrecisionContract(output.contract))
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "the output contract is not one this engine knows");
    }
    const Status policy = checkConversionPolicy(output.writeBack, kOperation);
    if (policy.failed())
    {
        return policy;
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return spatial_detail::spatialFailure(
            ErrorCode::InvalidArgument, kOperation,
            "a total-variation denoise carries its samples across as the numbers they are; a full-scale domain "
            "mapping would make it a denoise and a domain change at once");
    }
    return Status::success();
}

/// One Float32 working plane carved out of a block the engine context owns.
struct WorkPlane
{
    /// The first sample.
    float* base = nullptr;

    /// Samples per row.
    std::uint32_t width = 0u;

    /// Rows.
    std::uint32_t height = 0u;

    /// One row of the plane.
    ///
    /// @param index which row, below `height`.
    /// @return the row's first sample.
    [[nodiscard]] float* rowAt(const std::uint32_t index) const noexcept
    {
        return base + static_cast<std::size_t>(index) * width;
    }

    /// How many samples the plane holds.
    ///
    /// @return width times height.
    [[nodiscard]] std::size_t sampleCount() const noexcept
    {
        return static_cast<std::size_t>(width) * height;
    }
};

/// What one channel's solve produced beyond its samples.
struct ChannelOutcome
{
    /// How many iterations ran.
    std::uint32_t iterationsRun = 0u;

    /// Whether the stopping rule, rather than the iteration limit, ended the run.
    bool converged = false;

    /// The last step the run took, in the method's own units.
    double finalChange = 0.0;
};

/// Chambolle's projection iteration over one plane.
///
/// The dual variable is stepped along the gradient of the current answer and projected back each
/// iteration; the answer is the input plus that variable's divergence. The gradient is a forward
/// difference with the last row and column left at zero, and the divergence is its exact adjoint,
/// which is what makes the iteration converge to the minimum of the functional rather than to
/// something near it. The step is one over twice the dimension count, a quarter, which is the
/// library's own and what bit-for-bit agreement with it needs; Chambolle's convergence proof covers
/// steps up to an eighth, and a quarter is the step his paper reports converging in practice.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the channel as it was gathered.
/// @param planes the working planes: answer, divergence, two dual components, two gradients.
/// @param request the weight, limits and tolerance.
/// @param reportBase how many progress units precede this channel.
/// @param reportTotal the operation's whole progress total.
/// @param outcome what the run spent, written on success.
/// @return success, or an execution, cancellation or allocation failure.
[[nodiscard]] Status runChambolle(const ExecutionContext& execution, PlaneView<const float> input,
                                  std::span<const WorkPlane> planes, const TotalVariationRequest& request,
                                  const std::uint64_t reportBase, const std::uint64_t reportTotal,
                                  ChannelOutcome& outcome)
{
    const WorkPlane answer = planes[0];
    const WorkPlane divergence = planes[1];
    const WorkPlane dualY = planes[2];
    const WorkPlane dualX = planes[3];
    const WorkPlane gradientY = planes[4];
    const WorkPlane gradientX = planes[5];
    const std::uint32_t width = answer.width;
    const std::uint32_t height = answer.height;
    const auto weight = static_cast<float>(request.weight);
    constexpr float kStep = 0.25f;

    std::fill_n(dualY.base, dualY.sampleCount(), 0.0f);
    std::fill_n(dualX.base, dualX.sampleCount(), 0.0f);
    std::fill_n(divergence.base, divergence.sampleCount(), 0.0f);

    double previousEnergy = 0.0;
    double initialEnergy = 0.0;
    for (std::uint32_t iteration = 0u; iteration < request.maximumIterations; ++iteration)
    {
        const Status stopped = execution.checkCancellation();
        if (stopped.failed())
        {
            return stopped;
        }

        // The answer is the input plus the divergence of the dual variable, which is zero on the
        // first pass, so the first answer is the input itself.
        const Status combined = parallelFor(execution, height, rowCost(width, 6u), [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const auto row = static_cast<std::uint32_t>(y);
                float* const answerRow = answer.rowAt(row);
                float* const divergenceRow = divergence.rowAt(row);
                if (iteration != 0u)
                {
                    const float* const dualYRow = dualY.rowAt(row);
                    const float* const dualXRow = dualX.rowAt(row);
                    const float* const dualYAbove = row == 0u ? nullptr : dualY.rowAt(row - 1u);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        float value = -(dualYRow[x] + dualXRow[x]);
                        if (dualYAbove != nullptr)
                        {
                            value += dualYAbove[x];
                        }
                        if (x != 0u)
                        {
                            value += dualXRow[x - 1u];
                        }
                        divergenceRow[x] = value;
                    }
                }
                const std::span<const float> source = *input.row(row);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    answerRow[x] = source[x] + divergenceRow[x];
                }
            }
            return Status::success();
        });
        if (combined.failed())
        {
            return combined;
        }

        // The forward gradient of the answer, with the last row and column left at zero so that the
        // divergence above is its exact adjoint.
        const Status gradients = parallelFor(execution, height, rowCost(width, 4u), [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const auto row = static_cast<std::uint32_t>(y);
                const float* const here = answer.rowAt(row);
                const float* const below = row + 1u == height ? nullptr : answer.rowAt(row + 1u);
                float* const gradientYRow = gradientY.rowAt(row);
                float* const gradientXRow = gradientX.rowAt(row);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    gradientYRow[x] = below == nullptr ? 0.0f : below[x] - here[x];
                    gradientXRow[x] = x + 1u == width ? 0.0f : here[x + 1u] - here[x];
                }
            }
            return Status::success();
        });
        if (gradients.failed())
        {
            return gradients;
        }

        // The functional: the squared divergence plus the weighted total variation of the answer.
        const Result<double> energy = parallelReduce(
            execution, height, rowCost(width, 6u), 0.0,
            [&](TaskRange range, double& partial) -> Status {
                double local = 0.0;
                for (std::uint64_t y = range.begin; y < range.end; ++y)
                {
                    const auto row = static_cast<std::uint32_t>(y);
                    const float* const divergenceRow = divergence.rowAt(row);
                    const float* const gradientYRow = gradientY.rowAt(row);
                    const float* const gradientXRow = gradientX.rowAt(row);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const double dy = gradientYRow[x];
                        const double dx = gradientXRow[x];
                        local += static_cast<double>(divergenceRow[x]) * divergenceRow[x] +
                                 request.weight * std::sqrt(dy * dy + dx * dx);
                    }
                }
                partial += local;
                return Status::success();
            },
            add);
        if (!energy.hasValue())
        {
            return energy.status();
        }
        const double scaled = *energy / static_cast<double>(answer.sampleCount());

        // The dual step, then the projection back onto the ball the weight defines.
        const Status projected = parallelFor(execution, height, rowCost(width, 8u), [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const auto row = static_cast<std::uint32_t>(y);
                float* const dualYRow = dualY.rowAt(row);
                float* const dualXRow = dualX.rowAt(row);
                const float* const gradientYRow = gradientY.rowAt(row);
                const float* const gradientXRow = gradientX.rowAt(row);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const float dy = gradientYRow[x];
                    const float dx = gradientXRow[x];
                    const float length = std::sqrt(dy * dy + dx * dx) * (kStep / weight) + 1.0f;
                    dualYRow[x] = (dualYRow[x] - kStep * dy) / length;
                    dualXRow[x] = (dualXRow[x] - kStep * dx) / length;
                }
            }
            return Status::success();
        });
        if (projected.failed())
        {
            return projected;
        }

        outcome.iterationsRun = iteration + 1u;
        execution.reportProgress(reportBase + outcome.iterationsRun, reportTotal, kStage);
        if (iteration == 0u)
        {
            initialEnergy = scaled;
            previousEnergy = scaled;
            continue;
        }
        outcome.finalChange = std::abs(previousEnergy - scaled);
        if (request.tolerance > 0.0 && outcome.finalChange < request.tolerance * initialEnergy)
        {
            outcome.converged = true;
            return Status::success();
        }
        previousEnergy = scaled;
    }
    return Status::success();
}

/// One half of a red-black Gauss-Seidel sweep over the answer.
///
/// Red-black rather than lexical so that the sweep runs on the operation's threads and still gives
/// the same answer at any thread count: a sample of one parity reads only samples of the other, or
/// itself where a replicated edge stands in for a missing neighbour, so no update reads a sample
/// another update of the same half-sweep writes.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the channel as it was gathered.
/// @param answer the answer being relaxed, read and written.
/// @param shrunk the two shrunk gradient planes.
/// @param bregman the two Bregman planes.
/// @param fidelity the data weight.
/// @param smoothness the split weight.
/// @param parity zero for the samples whose coordinates sum even, one for the others.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status relaxHalfSweep(const ExecutionContext& execution, PlaneView<const float> input,
                                    const WorkPlane& answer, std::span<const WorkPlane> shrunk,
                                    std::span<const WorkPlane> bregman, const float fidelity, const float smoothness,
                                    const std::uint32_t parity)
{
    const std::uint32_t width = answer.width;
    const std::uint32_t height = answer.height;
    const float denominator = fidelity + 4.0f * smoothness;
    return parallelFor(execution, height, rowCost(width, 14u), [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto row = static_cast<std::uint32_t>(y);
            const bool hasAbove = row != 0u;
            const std::uint32_t up = hasAbove ? row - 1u : row;
            float* const here = answer.rowAt(row);
            const float* const above = answer.rowAt(up);
            const float* const below = answer.rowAt(row + 1u == height ? row : row + 1u);
            const float* const shrunkYRow = shrunk[0].rowAt(row);
            const float* const shrunkXRow = shrunk[1].rowAt(row);
            const float* const shrunkYAbove = shrunk[0].rowAt(up);
            const float* const bregmanYRow = bregman[0].rowAt(row);
            const float* const bregmanXRow = bregman[1].rowAt(row);
            const float* const bregmanYAbove = bregman[0].rowAt(up);
            const std::span<const float> source = *input.row(row);
            for (std::uint32_t x = (row + parity) % 2u; x < width; x += 2u)
            {
                const bool hasLeft = x != 0u;
                const std::uint32_t left = hasLeft ? x - 1u : x;
                // The divergence of the shrunk gradient less the Bregman variable, which is the
                // right-hand side the split introduces beside the data term. It is the adjoint of
                // the forward difference the shrink reads, so the term before the first row or
                // column is zero rather than the edge's own - the answer's own Laplacian above is
                // the one that replicates, because a Neumann edge has three neighbours and not four.
                const float backShrunkX = hasLeft ? shrunkXRow[left] : 0.0f;
                const float backShrunkY = hasAbove ? shrunkYAbove[x] : 0.0f;
                const float backBregmanX = hasLeft ? bregmanXRow[left] : 0.0f;
                const float backBregmanY = hasAbove ? bregmanYAbove[x] : 0.0f;
                const float coupling = (backShrunkX - shrunkXRow[x]) + (backShrunkY - shrunkYRow[x]) -
                                       (backBregmanX - bregmanXRow[x]) - (backBregmanY - bregmanYRow[x]);
                here[x] = (fidelity * source[x] + smoothness * (here[left] + here[x + 1u == width ? x : x + 1u] +
                                                                above[x] + below[x] + coupling)) /
                          denominator;
            }
        }
        return Status::success();
    });
}

/// The split-Bregman iteration over one plane.
///
/// Each iteration relaxes the answer by one red and one black half-sweep towards the linear system
/// the split introduces, shrinks the forward gradient of the new answer plus the Bregman variable by
/// the published soft threshold - isotropically on its length or on each component - and keeps what
/// the shrink removed as the next Bregman variable. With `weight` the fidelity, the split weight is
/// twice it - the library documents its weight as half of that parameter - and the threshold is one
/// over the split weight. The run stops early when the root-mean-square change of the answer falls
/// below the tolerance.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the channel as it was gathered.
/// @param planes the working planes: answer, previous answer, two shrunk gradients, two Bregman
///        variables.
/// @param request the weight, limits, tolerance and isotropy.
/// @param reportBase how many progress units precede this channel.
/// @param reportTotal the operation's whole progress total.
/// @param outcome what the run spent, written on success.
/// @return success, or an execution, cancellation or allocation failure.
[[nodiscard]] Status runSplitBregman(const ExecutionContext& execution, PlaneView<const float> input,
                                     std::span<const WorkPlane> planes, const TotalVariationRequest& request,
                                     const std::uint64_t reportBase, const std::uint64_t reportTotal,
                                     ChannelOutcome& outcome)
{
    const WorkPlane answer = planes[0];
    const WorkPlane previous = planes[1];
    const std::array<WorkPlane, 2u> shrunk{planes[2], planes[3]};
    const std::array<WorkPlane, 2u> bregman{planes[4], planes[5]};
    const std::uint32_t width = answer.width;
    const std::uint32_t height = answer.height;
    const auto fidelity = static_cast<float>(request.weight);
    const float smoothness = 2.0f * fidelity;
    const float threshold = 1.0f / smoothness;

    for (const WorkPlane& plane : shrunk)
    {
        std::fill_n(plane.base, plane.sampleCount(), 0.0f);
    }
    for (const WorkPlane& plane : bregman)
    {
        std::fill_n(plane.base, plane.sampleCount(), 0.0f);
    }
    const Status seeded = parallelFor(execution, height, rowCost(width, 1u), [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto row = static_cast<std::uint32_t>(y);
            const std::span<const float> source = *input.row(row);
            std::copy_n(source.data(), width, answer.rowAt(row));
        }
        return Status::success();
    });
    if (seeded.failed())
    {
        return seeded;
    }

    const auto samples = static_cast<double>(answer.sampleCount());
    for (std::uint32_t iteration = 0u; iteration < request.maximumIterations; ++iteration)
    {
        const Status stopped = execution.checkCancellation();
        if (stopped.failed())
        {
            return stopped;
        }
        std::copy_n(answer.base, answer.sampleCount(), previous.base);

        for (std::uint32_t parity = 0u; parity < 2u; ++parity)
        {
            const Status relaxed =
                relaxHalfSweep(execution, input, answer, shrunk, bregman, fidelity, smoothness, parity);
            if (relaxed.failed())
            {
                return relaxed;
            }
        }

        const Status shrinkage = parallelFor(execution, height, rowCost(width, 12u), [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const auto row = static_cast<std::uint32_t>(y);
                const float* const here = answer.rowAt(row);
                const float* const below = answer.rowAt(row + 1u == height ? row : row + 1u);
                float* const shrunkYRow = shrunk[0].rowAt(row);
                float* const shrunkXRow = shrunk[1].rowAt(row);
                float* const bregmanYRow = bregman[0].rowAt(row);
                float* const bregmanXRow = bregman[1].rowAt(row);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const float gradientY = below[x] - here[x];
                    const float gradientX = (x + 1u == width ? here[x] : here[x + 1u]) - here[x];
                    const float candidateY = gradientY + bregmanYRow[x];
                    const float candidateX = gradientX + bregmanXRow[x];
                    float shrunkY = 0.0f;
                    float shrunkX = 0.0f;
                    if (request.isotropic)
                    {
                        const float length = std::sqrt(candidateY * candidateY + candidateX * candidateX);
                        const float scale = length > threshold ? (length - threshold) / length : 0.0f;
                        shrunkY = candidateY * scale;
                        shrunkX = candidateX * scale;
                    }
                    else
                    {
                        shrunkY = candidateY > threshold    ? candidateY - threshold
                                  : candidateY < -threshold ? candidateY + threshold
                                                            : 0.0f;
                        shrunkX = candidateX > threshold    ? candidateX - threshold
                                  : candidateX < -threshold ? candidateX + threshold
                                                            : 0.0f;
                    }
                    shrunkYRow[x] = shrunkY;
                    shrunkXRow[x] = shrunkX;
                    bregmanYRow[x] = candidateY - shrunkY;
                    bregmanXRow[x] = candidateX - shrunkX;
                }
            }
            return Status::success();
        });
        if (shrinkage.failed())
        {
            return shrinkage;
        }

        const Result<double> moved = parallelReduce(
            execution, height, rowCost(width, 3u), 0.0,
            [&](TaskRange range, double& partial) -> Status {
                double local = 0.0;
                for (std::uint64_t y = range.begin; y < range.end; ++y)
                {
                    const auto row = static_cast<std::uint32_t>(y);
                    const float* const now = answer.rowAt(row);
                    const float* const before = previous.rowAt(row);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const double step = static_cast<double>(now[x]) - before[x];
                        local += step * step;
                    }
                }
                partial += local;
                return Status::success();
            },
            add);
        if (!moved.hasValue())
        {
            return moved.status();
        }

        outcome.iterationsRun = iteration + 1u;
        execution.reportProgress(reportBase + outcome.iterationsRun, reportTotal, kStage);
        outcome.finalChange = std::sqrt(*moved / samples);
        if (request.tolerance > 0.0 && outcome.finalChange < request.tolerance)
        {
            outcome.converged = true;
            return Status::success();
        }
    }
    return Status::success();
}

} // namespace

const char* totalVariationMethodName(const TotalVariationMethod method) noexcept
{
    switch (method)
    {
    case TotalVariationMethod::Chambolle:
        return "chambolle";
    case TotalVariationMethod::SplitBregman:
        return "split-bregman";
    }
    return "unknown";
}

TotalVariationPlan::TotalVariationPlan(const TotalVariationRequest& request) noexcept : m_request(request)
{
    m_workingPlanes = kWorkingPlanes;
}

const TotalVariationRequest& TotalVariationPlan::request() const noexcept
{
    return m_request;
}

std::uint32_t TotalVariationPlan::workingPlanesPerChannel() const noexcept
{
    return m_workingPlanes;
}

std::uint64_t TotalVariationPlan::imagePasses() const noexcept
{
    return m_request.maximumIterations;
}

Result<TotalVariationPlan> planTotalVariationDenoise(const TotalVariationRequest& request)
{
    if (!isTotalVariationMethod(request.method))
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "the total-variation method is not one this build knows");
    }
    if (!std::isfinite(request.weight) || !(request.weight > kMinimumTotalVariationWeight))
    {
        return spatial_detail::spatialFailure(
            ErrorCode::InvalidArgument, kOperation,
            "a total-variation weight must be finite and above the smallest weight the methods divide by");
    }
    if (!std::isfinite(request.tolerance) || request.tolerance < 0.0)
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "a total-variation tolerance must be finite and not negative");
    }
    if (request.maximumIterations == 0u)
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "a total-variation denoise runs at least one iteration");
    }
    if (request.maximumIterations > kMaxTotalVariationIterations)
    {
        return spatial_detail::spatialLimitFailure(ErrorCode::OutOfRange, kOperation,
                                                   "a total-variation denoise runs at most the stated iterations",
                                                   request.maximumIterations, kMaxTotalVariationIterations);
    }
    const Status output = checkOutputPolicy(request.output);
    if (output.failed())
    {
        return output;
    }
    return TotalVariationPlan(request);
}

Result<TotalVariationResult> denoiseImageTotalVariation(const ExecutionContext& execution, const SampleType sampleType,
                                                        const void* const source, const ImageLayout& sourceLayout,
                                                        void* const destination, const ImageLayout& destinationLayout,
                                                        const TotalVariationPlan& plan,
                                                        const SpatialSelection& selection)
{
    spatial_detail::SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    const Result<TotalVariationPlan> revalidated = planTotalVariationDenoise(plan.request());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }
    if (revalidated->workingPlanesPerChannel() != plan.workingPlanesPerChannel())
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "the plan was not produced by planTotalVariationDenoise");
    }

    const std::uint32_t planeCount = plan.workingPlanesPerChannel();
    const auto planeSamples = static_cast<std::uint64_t>(geometry.width) * geometry.height;
    Result<plane_detail::ScopedContextMemory> block = plane_detail::ScopedContextMemory::take(
        execution, planeSamples * planeCount * sizeof(float), kDefaultBufferAlignmentBytes, kOperation);
    if (!block.hasValue())
    {
        return block.status();
    }
    std::vector<WorkPlane> planes(planeCount);
    for (std::uint32_t index = 0u; index < planeCount; ++index)
    {
        planes[index] =
            WorkPlane{static_cast<float*>(block->data()) + index * planeSamples, geometry.width, geometry.height};
    }

    TotalVariationResult result{};
    SpatialFilterResult& filter = result.noise.filter;
    filter.horizontalRadius = 0u;
    filter.verticalRadius = 0u;
    filter.imagePasses = plan.imagePasses();
    filter.precision = ComputePrecision::Float32;
    filter.plannedWorkers = execution.admittedConcurrency();
    const std::uint64_t reportTotal =
        static_cast<std::uint64_t>(geometry.channelCount) * plan.request().maximumIterations;
    const bool keepsRejected = selection.validity.rule() != ValidityRule::AllValid;
    spatial_detail::SpatialPlaneBuffer substitutedInput;
    if (keepsRejected)
    {
        auto taken = spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kOperation);
        if (!taken.hasValue())
            return taken.status();
        substitutedInput = std::move(*taken);
    }
    std::uint64_t written = 0u;
    std::uint32_t channelIndex = 0u;
    bool everyChannelConverged = true;

    const Status denoised = spatial_detail::filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        plan.request().output.writeBack, geometry, kOperation, filter,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            PlaneView<const float> solveInput = input;
            if (keepsRejected)
            {
                const Status copied = spatial_detail::copyPlane(execution, input, substitutedInput.view(), kOperation);
                if (copied.failed())
                    return copied;
                std::uint64_t substituted = 0u;
                const Status repaired = spatial_detail::substituteRejectedSamples(
                    execution, substitutedInput.view(), validity, 0.0f, substituted, kOperation);
                if (repaired.failed())
                    return repaired;
                solveInput = substitutedInput.constView();
            }
            ChannelOutcome outcome{};
            const std::uint64_t reportBase =
                static_cast<std::uint64_t>(channelIndex) * plan.request().maximumIterations;
            const Status ran =
                plan.request().method == TotalVariationMethod::Chambolle
                    ? runChambolle(execution, solveInput, planes, plan.request(), reportBase, reportTotal, outcome)
                    : runSplitBregman(execution, solveInput, planes, plan.request(), reportBase, reportTotal, outcome);
            if (ran.failed())
            {
                return ran;
            }
            // A display told how many iterations there would be ends where it was told it would,
            // whether the run met its tolerance or spent its budget.
            execution.reportProgress(reportBase + plan.request().maximumIterations, reportTotal, kStage);
            result.iterationsRun = (std::max)(result.iterationsRun, outcome.iterationsRun);
            result.finalChange = (std::max)(result.finalChange, outcome.finalChange);
            everyChannelConverged = everyChannelConverged && outcome.converged;
            ++channelIndex;

            const WorkPlane answer = planes[0];
            const std::uint32_t width = output.width();
            std::atomic<std::uint64_t> channelWritten{0u};
            const Status copied =
                parallelFor(execution, output.height(), rowCost(width, 2u), [&](TaskRange range) -> Status {
                    std::uint64_t count = 0u;
                    for (std::uint64_t y = range.begin; y < range.end; ++y)
                    {
                        const auto row = static_cast<std::uint32_t>(y);
                        const float* const solved = answer.rowAt(row);
                        const std::span<const float> original = *input.row(row);
                        const std::span<float> destinationRow = *output.row(row);
                        for (std::uint32_t x = 0u; x < width; ++x)
                        {
                            // Match the scatter path: only selected destination samples are written.
                            const bool means =
                                !keepsRejected || (sampleIsValidByValue(validity.descriptor, original[x]) &&
                                                   plane_detail::maskAllows(validity, x, row));
                            if (means)
                            {
                                destinationRow[x] = solved[x];
                                ++count;
                            }
                        }
                    }
                    channelWritten.fetch_add(count, std::memory_order_relaxed);
                    return Status::success();
                });
            if (copied.failed())
            {
                return copied;
            }
            written += channelWritten.load(std::memory_order_relaxed);
            return Status::success();
        },
        &result.noise.samplesChanged);
    if (denoised.failed())
    {
        return denoised;
    }

    result.converged = everyChannelConverged && plan.request().tolerance > 0.0;
    filter.workingBytes += block->bytes() + substitutedInput.bytes();
    const auto samples = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    filter.run.selection = ValidityCount{.valid = written, .total = samples};
    filter.run.outputsWritten = written;
    return result;
}

} // namespace an
