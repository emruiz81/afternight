// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - border-, halo- and tile-aware separable and dense convolution primitives.
//
// Four decisions shape the separable path, and each of them is what makes the optimized path
// checkable against the scalar one rather than merely faster than it. The dense path has no tiling
// and no scratch band: each valid output sums its whole coefficient window directly, in row-major
// coefficient order, and rows are divided into a deterministic number of chunks.
//
// **The border is resolved into a padded row before the taps are applied.** A convolution written
// the obvious way tests each tap for being outside the image, which puts the border rule inside the
// inner loop and makes the interior unvectorizable along with the edges. Here each source row is
// first materialized into a buffer that already holds the border samples on either side and the
// substitute for every invalid sample, after which the horizontal pass is a contiguous
// multiply-accumulate with no cases at all. The buffer is one row with `radius` samples of padding
// on each side, not a copy of the image.
//
// **The intermediate is a band, and the bands are reused.** A separable convolution that keeps a
// whole horizontally filtered image needs an image-sized Float64 buffer, which for a production
// frame is larger than the frame. Each worker holds one band and takes the next tile that is free,
// or, where the ceiling holds fewer bands than there are workers, the tiles run in waves of as many
// as there are bands. Either way the working set is the band times the concurrent tiles rather than
// the image, and which tile used which buffer never reaches the result.
//
// **A tile is a run of rows of one residue class, in both directions.** Spreading the taps `d`
// samples apart makes a run of adjacent rows read a footprint `d` times taller, and a band holding
// that whole footprint would grow with the spacing until it left the scratch ceiling - which is what
// would put a multiscale transform back on an image-sized intermediate. Every row the vertical taps
// of one output row read shares its index modulo `d`, so a tile takes consecutive rows of one such
// class: its band holds the rows its taps actually read, the memory is what the undilated pass costs
// whatever the spacing, and because the classes share no row, a deep layer still has a tile for
// every worker. The same split along a row turns the horizontal pass back into the contiguous
// multiply-accumulate the compiled kernels are written for, at the cost of one strided store per
// output.
//
// **Both passes accumulate in Float64 tap by tap, in the array's own order.** The vector bodies do
// the same thing across lanes that the scalar body does across pixels, so the two produce the same
// arithmetic rather than a reassociated version of it, and a differential test between them is a
// tight comparison instead of a tolerance argument.

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/ReferenceKernelInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{
namespace
{

constexpr const char* kSeparableOperation = "an::convolveSeparablePlane";
constexpr const char* kDenseOperation = "an::convolveDensePlane";

/// The band size one tile aims at before the scratch ceiling has a say.
///
/// A band of a couple of hundred kibibytes is written by the horizontal pass and read straight back
/// by the vertical one, so keeping it near the size of a private cache is what the tiling is for.
/// It is a target rather than a rule: a plane wider than this gets a one-row band and still works.
constexpr std::uint64_t kTargetBandBytes = 256u * 1024u;

/// Larger rows need a taller band to amortize their scheduling wave without penalizing small planes.
constexpr std::uint64_t kWideTargetBandBytes = 512u * 1024u;

/// One failure from this file.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status stencilFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a tap array names a center and an equal number of samples either side of it.
///
/// @param taps the array.
/// @param operation the entry point the caller is inside.
/// @return success, or why the array is not usable.
[[nodiscard]] Status checkTaps(std::span<const double> taps, const char* operation)
{
    if (taps.empty() || taps.size() % 2u == 0u)
    {
        return stencilFailure(ErrorCode::InvalidArgument, operation,
                              "a separable pass needs an odd, non-empty number of taps");
    }
    if (taps.size() > kMaxStencilTaps)
    {
        return stencilFailure(ErrorCode::OutOfRange, operation,
                              "a separable pass exceeds the stated maximum stencil radius");
    }
    for (const double tap : taps)
    {
        if (!std::isfinite(tap))
        {
            return stencilFailure(ErrorCode::InvalidArgument, operation, "a stencil tap is not finite");
        }
    }
    return Status::success();
}

/// Whether a border policy names a rule this family implements and finite substitutes.
///
/// @param borders the policy.
/// @param operation the entry point the caller is inside.
/// @return success, or why the policy is not usable.
[[nodiscard]] Status checkBorders(const StencilBorderPolicy& borders, const char* operation)
{
    if (borders.border == KernelBorderMode::NotApplicable || !isKernelBorderMode(borders.border))
    {
        return stencilFailure(ErrorCode::InvalidArgument, operation,
                              "the stencil border must be one of the modes that supply a neighbour");
    }
    if (!std::isfinite(borders.constantBorder) || !std::isfinite(borders.invalidNeighbor))
    {
        return stencilFailure(ErrorCode::InvalidArgument, operation,
                              "the stencil border and invalid-neighbour substitutes must be finite");
    }
    return Status::success();
}

/// The tile band plan one separable convolution runs with.
///
/// A tile is `tileRows` consecutive rows of one residue class of the row index modulo the tap
/// spacing, so its output rows sit `dilation` apart and every row its vertical taps read belongs to
/// the same class. An undilated pass has one class, and its tiles are runs of adjacent rows.
struct BandPlan
{
    /// Output rows one tile produces.
    std::uint32_t tileRows = 1u;

    /// How many tiles one residue class is divided into, counted for the longest class.
    std::uint64_t tilesPerClass = 1u;

    /// How many tiles the destination is divided into, every class together.
    std::uint64_t tileCount = 1u;

    /// Rows one band holds, the halo on both sides included.
    std::uint64_t bandRows = 1u;

    /// How many bands fit under the scratch ceiling at once, at most one per worker.
    std::uint64_t slots = 1u;
};

/// The plan one tile height produces, when its whole working set fits the ceiling.
///
/// The two costs pull against each other: a taller band holds more samples per slot, and a shorter
/// one divides the plane into more tiles and so needs more per-tile counters. Both are charged
/// here, so a caller choosing a height sees one answer rather than two budgets.
///
/// @param width how many samples one row holds.
/// @param height how many rows the plane holds.
/// @param horizontalRadius the padded-row halo on each side.
/// @param verticalRadius the band halo depth on each side.
/// @param concurrency how many workers the operation was admitted.
/// @param dilation how many samples apart two consecutive taps sit.
/// @param tileRows the tile height to cost, at least one.
/// @return the plan, or no value when this height needs more than the ceiling allows.
[[nodiscard]] std::optional<BandPlan> costBandPlan(std::uint32_t width, std::uint32_t height,
                                                   std::uint32_t horizontalRadius, std::uint32_t verticalRadius,
                                                   std::uint32_t concurrency, std::uint32_t dilation,
                                                   std::uint32_t tileRows) noexcept
{
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * sizeof(double);
    // Row `y` belongs to class `y % dilation`. The first class is the longest, and a spacing beyond
    // the height leaves every class past the last row empty, so those hold no tile.
    const std::uint64_t classCount =
        (std::min)(static_cast<std::uint64_t>(dilation), static_cast<std::uint64_t>(height));
    const std::uint64_t classRows = (static_cast<std::uint64_t>(height) + dilation - 1u) / dilation;
    BandPlan plan;
    plan.tileRows = tileRows;
    plan.tilesPerClass = (classRows + plan.tileRows - 1u) / plan.tileRows;
    plan.tileCount = classCount * plan.tilesPerClass;
    // The band holds the rows of the tile's own class that its taps read, not every row of the
    // footprint they span.
    plan.bandRows = static_cast<std::uint64_t>(plan.tileRows) + 2u * verticalRadius;
    const std::uint64_t bandBytesPerSlot = plan.bandRows * rowBytes;
    const std::uint64_t phaseBytesPerSlot = dilation > 1u ? rowBytes : 0u;
    const std::uint64_t paddedBytesPerSlot =
        (static_cast<std::uint64_t>(width) + 2u * horizontalRadius) * sizeof(float);
    const std::uint64_t fixedBytes = plan.tileCount * sizeof(std::uint64_t);
    const std::uint64_t bytesPerSlot = bandBytesPerSlot + phaseBytesPerSlot + paddedBytesPerSlot;
    if (fixedBytes >= kMaxStencilScratchBytes || bytesPerSlot > kMaxStencilScratchBytes - fixedBytes)
    {
        return std::nullopt;
    }
    const std::uint64_t affordable = (kMaxStencilScratchBytes - fixedBytes) / bytesPerSlot;
    const std::uint64_t workers = (std::max)(std::uint64_t{1}, static_cast<std::uint64_t>(concurrency));
    plan.slots = (std::min)({plan.tileCount, affordable, workers});
    return plan;
}

/// What one plan asks of its busiest worker, in tap applications per sample of a row.
///
/// Every wave filters one band horizontally and one tile vertically, and the busiest worker runs
/// as many waves as the tiles divided among the bands round up to. A shorter tile repeats its halo
/// rows more often and can give more workers a band under the ceiling; this is the one number both
/// effects move.
///
/// @param plan the plan to cost.
/// @param horizontalRadius the padded-row halo on each side.
/// @param verticalRadius the band halo depth on each side.
/// @return the estimate, which is only ever compared with another plan's for the same pass.
[[nodiscard]] std::uint64_t busiestWorkerCost(const BandPlan& plan, std::uint32_t horizontalRadius,
                                              std::uint32_t verticalRadius) noexcept
{
    const std::uint64_t waves = (plan.tileCount + plan.slots - 1u) / plan.slots;
    const std::uint64_t perTile =
        plan.bandRows * (2u * static_cast<std::uint64_t>(horizontalRadius) + 1u) +
        static_cast<std::uint64_t>(plan.tileRows) * (2u * static_cast<std::uint64_t>(verticalRadius) + 1u);
    return waves * perTile;
}

/// Chooses the tile height, how many bands exist at once, and therefore the working set.
///
/// @param width how many samples one row holds.
/// @param height how many rows the plane holds.
/// @param horizontalRadius the padded-row halo on each side.
/// @param verticalRadius the band halo depth on each side.
/// @param concurrency how many workers the operation was admitted.
/// @param dilation how many samples apart two consecutive taps sit.
/// @return the plan, or no value when no tile height fits the scratch ceiling.
[[nodiscard]] std::optional<BandPlan> planBands(std::uint32_t width, std::uint32_t height,
                                                std::uint32_t horizontalRadius, std::uint32_t verticalRadius,
                                                std::uint32_t concurrency, std::uint32_t dilation) noexcept
{
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * sizeof(double);
    const std::uint64_t targetBandBytes = width >= 1024u ? kWideTargetBandBytes : kTargetBandBytes;
    const std::uint64_t targetRows = rowBytes == 0u ? height : (std::max)(std::uint64_t{1}, targetBandBytes / rowBytes);
    const std::uint64_t classRows = (static_cast<std::uint64_t>(height) + dilation - 1u) / dilation;
    const std::uint32_t target =
        static_cast<std::uint32_t>((std::max)(std::uint64_t{1}, (std::min)(classRows, targetRows)));

    // Downward from the height the band size targets, which is the search that matters: it is the
    // bands of a wide plane that overflow the ceiling, and halving the band is what answers that.
    // Of the heights that fit, the one whose busiest worker does least is taken, the taller on a
    // tie, so one worker keeps the tallest affordable band and many workers take a shorter one only
    // where the bands it lets them hold outweigh the halo rows it repeats.
    std::optional<BandPlan> chosen;
    std::uint64_t chosenCost = 0u;
    for (std::uint32_t tileRows = target;; tileRows = (std::max)(std::uint32_t{1}, tileRows / 2u))
    {
        const std::optional<BandPlan> plan =
            costBandPlan(width, height, horizontalRadius, verticalRadius, concurrency, dilation, tileRows);
        if (plan.has_value())
        {
            const std::uint64_t cost = busiestWorkerCost(*plan, horizontalRadius, verticalRadius);
            if (!chosen.has_value() || cost < chosenCost)
            {
                chosen = plan;
                chosenCost = cost;
            }
        }
        if (tileRows == 1u)
        {
            break;
        }
    }
    if (chosen.has_value())
    {
        return chosen;
    }

    // Upward second, and only because the other cost exists. A plan refused for its per-tile
    // counters is refused harder as the band shrinks, so the heights above the target are the ones
    // that could still fit and the search above never reached them. It takes a plane of some
    // hundreds of gigapixels to arrive here and the engine's configured maxima refuse one long
    // before; without this the refusal would mean one thing and say another.
    for (std::uint64_t tileRows = static_cast<std::uint64_t>(target) * 2u; tileRows < classRows; tileRows *= 2u)
    {
        const std::optional<BandPlan> plan = costBandPlan(width, height, horizontalRadius, verticalRadius, concurrency,
                                                          dilation, static_cast<std::uint32_t>(tileRows));
        if (plan.has_value())
        {
            return plan;
        }
    }
    return costBandPlan(width, height, horizontalRadius, verticalRadius, concurrency, dilation,
                        static_cast<std::uint32_t>(classRows));
}

/// Fills one padded row with border-resolved, validity-resolved source samples.
///
/// After this, applying the taps is a contiguous multiply-accumulate: index `i` of the buffer is the
/// sample the output at column `i - radius` would read for its first tap. Where the rule accepts every
/// sample, the samples inside the row are copied as they stand and only the halo either side is
/// resolved one sample at a time, which is the same buffer at a fraction of the cost.
///
/// @param source the input plane.
/// @param accepts the validity rule.
/// @param borders the border and substitute policy.
/// @param sourceRow the already border-resolved row to read.
/// @param radius half the horizontal extent.
/// @param acceptsEverySample whether `accepts` accepts every sample, as it does under an all-valid
///        rule, beside which no mask is admitted.
/// @param padded the buffer, `width + 2 * radius` samples long.
void fillPaddedRow(PlaneView<const float> source, const reference_detail::ValidityAccessor& accepts,
                   const StencilBorderPolicy& borders, std::uint32_t sourceRow, std::uint32_t radius,
                   bool acceptsEverySample, float* padded)
{
    const std::int64_t width = static_cast<std::int64_t>(source.width());
    const std::span<const float> row = *source.row(sourceRow);
    const std::uint64_t length = static_cast<std::uint64_t>(width) + 2u * radius;
    const auto resolve = [&](std::uint64_t index) {
        const std::int64_t x = static_cast<std::int64_t>(index) - static_cast<std::int64_t>(radius);
        const std::int64_t resolved = kernelBorderIndex(x, width, borders.border);
        if (resolved == width)
        {
            padded[index] = borders.constantBorder;
            return;
        }
        const std::uint32_t column = static_cast<std::uint32_t>(resolved);
        const float sample = row[column];
        padded[index] = accepts.accepts(sample, column, sourceRow) ? sample : borders.invalidNeighbor;
    };
    if (!acceptsEverySample)
    {
        for (std::uint64_t index = 0u; index < length; ++index)
        {
            resolve(index);
        }
        return;
    }
    for (std::uint64_t index = 0u; index < radius; ++index)
    {
        resolve(index);
    }
    std::copy(row.begin(), row.end(), padded + radius);
    for (std::uint64_t index = radius + static_cast<std::uint64_t>(width); index < length; ++index)
    {
        resolve(index);
    }
}

/// Applies the horizontal taps to a padded row, scalar.
///
/// @param padded the resolved samples.
/// @param count how many outputs to produce.
/// @param taps the taps, applied in their own order.
/// @param destination where the Float64 intermediates go.
void scalarRowRun(const float* padded, std::uint64_t count, std::span<const double> taps, double* destination) noexcept
{
    for (std::uint64_t x = 0u; x < count; ++x)
    {
        double sum = 0.0;
        for (std::size_t tap = 0u; tap < taps.size(); ++tap)
        {
            sum += taps[tap] * static_cast<double>(padded[x + tap]);
        }
        destination[x] = sum;
    }
}

/// Fills one padded row of a dilated pass with border-resolved, validity-resolved source samples.
///
/// The dilated counterpart of `fillPaddedRow`: the buffer holds every sample the outputs of one
/// residue class read, in the order they read them, so the taps still apply as a contiguous
/// multiply-accumulate. Index `i` is the sample the output at column `phase + (i - radius) * dilation`
/// would read for its first tap, which lies inside the row exactly for `i` in
/// `[radius, radius + count)`; there, where the rule accepts every sample, it is copied as it stands.
///
/// @param source the input plane.
/// @param accepts the validity rule.
/// @param borders the border and substitute policy.
/// @param sourceRow the already border-resolved row to read.
/// @param radius half the horizontal extent, counted in taps.
/// @param dilation how many samples apart two consecutive taps sit.
/// @param phase which residue class of columns this buffer serves.
/// @param count how many outputs the class produces.
/// @param acceptsEverySample whether `accepts` accepts every sample, as it does under an all-valid
///        rule, beside which no mask is admitted.
/// @param padded the buffer, `count + 2 * radius` samples long.
void fillPaddedPhase(PlaneView<const float> source, const reference_detail::ValidityAccessor& accepts,
                     const StencilBorderPolicy& borders, std::uint32_t sourceRow, std::uint32_t radius,
                     std::uint32_t dilation, std::uint32_t phase, std::uint64_t count, bool acceptsEverySample,
                     float* padded)
{
    const std::int64_t width = static_cast<std::int64_t>(source.width());
    const std::span<const float> row = *source.row(sourceRow);
    const std::uint64_t length = count + 2u * radius;
    const auto resolve = [&](std::uint64_t index) {
        const std::int64_t x =
            static_cast<std::int64_t>(phase) + (static_cast<std::int64_t>(index) - static_cast<std::int64_t>(radius)) *
                                                   static_cast<std::int64_t>(dilation);
        const std::int64_t resolved = kernelBorderIndex(x, width, borders.border);
        if (resolved == width)
        {
            padded[index] = borders.constantBorder;
            return;
        }
        const std::uint32_t column = static_cast<std::uint32_t>(resolved);
        const float sample = row[column];
        padded[index] = accepts.accepts(sample, column, sourceRow) ? sample : borders.invalidNeighbor;
    };
    if (!acceptsEverySample)
    {
        for (std::uint64_t index = 0u; index < length; ++index)
        {
            resolve(index);
        }
        return;
    }
    for (std::uint64_t index = 0u; index < radius; ++index)
    {
        resolve(index);
    }
    const float* const first = row.data() + phase;
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        padded[radius + index] = first[index * dilation];
    }
    for (std::uint64_t index = radius + count; index < length; ++index)
    {
        resolve(index);
    }
}

/// Applies the vertical taps down a band, scalar.
///
/// @param band the first contributing row of the band, at column zero.
/// @param rowStride how many samples separate one band row from the next.
/// @param count how many outputs to produce.
/// @param taps the taps, applied in their own order.
/// @param destination where the narrowed outputs go.
void scalarColumnRun(const double* band, std::uint64_t rowStride, std::uint64_t count, std::span<const double> taps,
                     float* destination) noexcept
{
    for (std::uint64_t x = 0u; x < count; ++x)
    {
        double sum = 0.0;
        for (std::size_t tap = 0u; tap < taps.size(); ++tap)
        {
            sum += taps[tap] * band[tap * rowStride + x];
        }
        destination[x] = static_cast<float>(sum);
    }
}

/// The sample one dense coefficient reads, with the border and validity rules applied.
///
/// @param source the input plane.
/// @param accepts the validity rule.
/// @param borders the border and substitute policy.
/// @param x the column, which may be outside the plane.
/// @param y the row, which may be outside the plane.
/// @return the sample the coefficient multiplies.
[[nodiscard]] double denseSampleAt(PlaneView<const float> source, const reference_detail::ValidityAccessor& accepts,
                                   const StencilBorderPolicy& borders, std::int64_t x, std::int64_t y) noexcept
{
    const std::int64_t width = static_cast<std::int64_t>(source.width());
    const std::int64_t height = static_cast<std::int64_t>(source.height());
    const std::int64_t sourceX = kernelBorderIndex(x, width, borders.border);
    const std::int64_t sourceY = kernelBorderIndex(y, height, borders.border);
    if (sourceX == width || sourceY == height)
    {
        return static_cast<double>(borders.constantBorder);
    }
    const std::uint32_t column = static_cast<std::uint32_t>(sourceX);
    const std::uint32_t row = static_cast<std::uint32_t>(sourceY);
    const float sample = (*source.row(row))[column];
    return static_cast<double>(accepts.accepts(sample, column, row) ? sample : borders.invalidNeighbor);
}

} // namespace

Result<StencilExecutionResult> convolveSeparablePlane(const ExecutionContext& execution, PlaneView<const float> source,
                                                      PlaneView<float> destination,
                                                      const SeparableStencilRequest& request,
                                                      const KernelValidityView& validity)
{
    Status status = reference_detail::checkExecution(execution, kSeparableOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkPair(source, destination, kSeparableOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkValidity(validity, source.width(), source.height(), kSeparableOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkOverlap(source, destination, false, kSeparableOperation);
    if (status.failed())
    {
        return status;
    }
    status = checkTaps(request.horizontalTaps, kSeparableOperation);
    if (status.failed())
    {
        return status;
    }
    status = checkTaps(request.verticalTaps, kSeparableOperation);
    if (status.failed())
    {
        return status;
    }
    status = checkBorders(request.borders, kSeparableOperation);
    if (status.failed())
    {
        return status;
    }
    if (request.dilation == 0u)
    {
        return stencilFailure(ErrorCode::InvalidArgument, kSeparableOperation,
                              "a separable pass needs a tap spacing of at least one");
    }
    if (request.dilation > kMaxStencilDilation)
    {
        return stencilFailure(ErrorCode::OutOfRange, kSeparableOperation,
                              "a separable pass exceeds the stated maximum tap spacing");
    }

    const std::uint32_t width = source.width();
    const std::uint32_t height = source.height();
    const std::uint32_t radiusX = static_cast<std::uint32_t>(request.horizontalTaps.size() / 2u);
    const std::uint32_t radiusY = static_cast<std::uint32_t>(request.verticalTaps.size() / 2u);
    const std::uint32_t dilation = request.dilation;
    double horizontalSum = 0.0;
    for (const double tap : request.horizontalTaps)
    {
        horizontalSum += tap;
    }

    const std::optional<BandPlan> planned =
        planBands(width, height, radiusX, radiusY, execution.admittedConcurrency(), dilation);
    if (!planned.has_value())
    {
        return stencilFailure(ErrorCode::LimitExceeded, kSeparableOperation,
                              "the minimum separable tile plan exceeds the stencil scratch ceiling");
    }
    const BandPlan plan = *planned;
    const std::uint64_t bandSamples = plan.bandRows * width;
    // A dilated pass produces one residue class of columns at a time, and the widest class holds
    // `ceil(width / dilation)` outputs, so the padded row an undilated pass needs is an upper bound
    // for every class of a dilated one.
    const std::uint64_t paddedSamples = static_cast<std::uint64_t>(width) + 2u * radiusX;
    const std::uint64_t phaseSamples = dilation > 1u ? static_cast<std::uint64_t>(width) : 0u;
    const std::span<const double> horizontal = request.horizontalTaps;
    const std::span<const double> vertical = request.verticalTaps;
    const std::uint32_t tapsX = static_cast<std::uint32_t>(horizontal.size());
    const std::uint32_t tapsY = static_cast<std::uint32_t>(vertical.size());
    const TaskCost tileCost{.bytesPerItem = bandSamples * sizeof(double),
                            .operationsPerItem = bandSamples * (tapsX + tapsY)};

    // Every worker the tiles can occupy holds a band of its own and takes the next tile that is
    // free. Where the ceiling holds fewer bands than that, the tiles run in waves of as many as
    // there are bands.
    const std::uint64_t workers = (std::max)(std::uint32_t{1}, plannedWorkerCount(execution, plan.tileCount, tileCost));
    const bool bandPerWorker = workers <= plan.slots;
    const std::uint64_t bandCount = bandPerWorker ? workers : plan.slots;
    const std::uint64_t bandBytes = bandCount * bandSamples * sizeof(double);
    const std::uint64_t paddedBytes = bandCount * paddedSamples * sizeof(float);
    const std::uint64_t phaseBytes = bandCount * phaseSamples * sizeof(double);
    Result<plane_detail::ScopedContextMemory> bands =
        plane_detail::ScopedContextMemory::take(execution, bandBytes, alignof(double), kSeparableOperation);
    if (!bands.hasValue())
    {
        return bands.status();
    }
    Result<plane_detail::ScopedContextMemory> paddedRows =
        plane_detail::ScopedContextMemory::take(execution, paddedBytes, alignof(float), kSeparableOperation);
    if (!paddedRows.hasValue())
    {
        return paddedRows.status();
    }
    Result<plane_detail::ScopedContextMemory> phaseRows =
        plane_detail::ScopedContextMemory::take(execution, phaseBytes, alignof(double), kSeparableOperation);
    if (!phaseRows.hasValue())
    {
        return phaseRows.status();
    }
    Result<plane_detail::ScopedContextMemory> counters = plane_detail::ScopedContextMemory::take(
        execution, plan.tileCount * sizeof(std::uint64_t), alignof(std::uint64_t), kSeparableOperation);
    if (!counters.hasValue())
    {
        return counters.status();
    }
    double* const bandBase = static_cast<double*>(bands->data());
    float* const paddedBase = static_cast<float*>(paddedRows->data());
    double* const phaseBase = static_cast<double*>(phaseRows->data());
    std::uint64_t* const writtenPerTile = static_cast<std::uint64_t*>(counters->data());
    std::fill_n(writtenPerTile, static_cast<std::size_t>(plan.tileCount), std::uint64_t{0});

    const reference_detail::ValidityAccessor accepts(validity);
    const StencilRowRunFn rowRun = stencilRowRunFor(execution);
    const StencilColumnRunFn columnRun = stencilColumnRunFor(execution);

    // An all-valid rule accepts every sample and admits no mask, so a padded row copies the inside of
    // its source row rather than asking the rule one sample at a time.
    const bool everySampleValid = validity.descriptor.isAllValid();
    const auto runTile = [&](std::uint64_t tile, std::uint64_t slot) {
        double* const band = bandBase + slot * bandSamples;
        float* const padded = paddedBase + slot * paddedSamples;
        double* const phaseRow = phaseBase == nullptr ? nullptr : phaseBase + slot * phaseSamples;
        // The residue class the tile belongs to, and where among that class's rows its own start. The
        // longest class sets how many tiles each class is given, so a shorter class may leave its last
        // one empty.
        const std::uint32_t rowPhase = static_cast<std::uint32_t>(tile / plan.tilesPerClass);
        const std::uint64_t firstIndex = (tile % plan.tilesPerClass) * plan.tileRows;
        const std::uint64_t classRows = (static_cast<std::uint64_t>(height) - rowPhase + dilation - 1u) / dilation;
        std::uint64_t produced = 0u;
        if (firstIndex < classRows)
        {
            const std::uint64_t outputRows =
                (std::min)(static_cast<std::uint64_t>(plan.tileRows), classRows - firstIndex);
            const std::uint64_t usedRows = outputRows + 2u * radiusY;
            const std::int64_t firstRow =
                static_cast<std::int64_t>(rowPhase) + static_cast<std::int64_t>(firstIndex * dilation);

            for (std::uint64_t bandRow = 0u; bandRow < usedRows; ++bandRow)
            {
                double* const target = band + bandRow * width;
                const std::int64_t wanted =
                    firstRow + (static_cast<std::int64_t>(bandRow) - static_cast<std::int64_t>(radiusY)) *
                                   static_cast<std::int64_t>(dilation);
                const std::int64_t resolved =
                    kernelBorderIndex(wanted, static_cast<std::int64_t>(height), request.borders.border);
                if (resolved == static_cast<std::int64_t>(height))
                {
                    // A whole row outside the footprint contributes the constant, which after the
                    // horizontal pass is the constant scaled by the sum of its taps.
                    std::fill_n(target, static_cast<std::size_t>(width),
                                static_cast<double>(request.borders.constantBorder) * horizontalSum);
                    continue;
                }
                const std::uint32_t sourceRow = static_cast<std::uint32_t>(resolved);
                if (dilation == 1u)
                {
                    fillPaddedRow(source, accepts, request.borders, sourceRow, radiusX, everySampleValid, padded);
                    if (rowRun != nullptr)
                    {
                        rowRun(padded, width, horizontal.data(), tapsX, target);
                    }
                    else
                    {
                        scalarRowRun(padded, width, horizontal, target);
                    }
                    continue;
                }
                for (std::uint32_t columnPhase = 0u; columnPhase < dilation && columnPhase < width; ++columnPhase)
                {
                    const std::uint64_t count =
                        (static_cast<std::uint64_t>(width) - columnPhase + dilation - 1u) / dilation;
                    fillPaddedPhase(source, accepts, request.borders, sourceRow, radiusX, dilation, columnPhase, count,
                                    everySampleValid, padded);
                    if (rowRun != nullptr)
                    {
                        rowRun(padded, count, horizontal.data(), tapsX, phaseRow);
                    }
                    else
                    {
                        scalarRowRun(padded, count, horizontal, phaseRow);
                    }
                    for (std::uint64_t index = 0u; index < count; ++index)
                    {
                        target[columnPhase + index * dilation] = phaseRow[index];
                    }
                }
            }

            for (std::uint64_t index = 0u; index < outputRows; ++index)
            {
                const std::uint32_t y =
                    static_cast<std::uint32_t>(firstRow + static_cast<std::int64_t>(index * dilation));
                const double* const first = band + index * width;
                const std::span<float> output = *destination.row(y);
                if (everySampleValid)
                {
                    if (columnRun != nullptr)
                    {
                        columnRun(first, width, width, vertical.data(), tapsY, output.data());
                    }
                    else
                    {
                        scalarColumnRun(first, width, width, vertical, output.data());
                    }
                    produced += width;
                    continue;
                }
                // An invalid center produces nothing at all, so the run kernel - which writes every
                // column it is given - is not the right shape for a masked row.
                const std::span<const float> input = *source.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (!accepts.accepts(input[x], x, y))
                    {
                        continue;
                    }
                    double sum = 0.0;
                    for (std::uint32_t tap = 0u; tap < tapsY; ++tap)
                    {
                        sum += vertical[tap] * first[static_cast<std::uint64_t>(tap) * width + x];
                    }
                    output[x] = static_cast<float>(sum);
                    ++produced;
                }
            }
        }
        writtenPerTile[tile] = produced;
    };

    if (bandPerWorker)
    {
        status = parallelForWorkers(execution, plan.tileCount, tileCost, [&](WorkerTask task) -> Status {
            for (std::uint64_t tile = task.range.begin; tile < task.range.end; ++tile)
            {
                runTile(tile, task.workerSlot);
            }
            return Status::success();
        });
    }
    else
    {
        for (std::uint64_t wave = 0u; wave < plan.tileCount && !status.failed(); wave += bandCount)
        {
            const std::uint64_t waveTiles = (std::min)(bandCount, plan.tileCount - wave);
            status = parallelFor(execution, waveTiles, tileCost, [&](TaskRange slots) -> Status {
                for (std::uint64_t slot = slots.begin; slot < slots.end; ++slot)
                {
                    runTile(wave + slot, slot);
                }
                return Status::success();
            });
        }
    }
    if (status.failed())
    {
        return status;
    }

    std::uint64_t written = 0u;
    for (std::uint64_t tile = 0u; tile < plan.tileCount; ++tile)
    {
        written += writtenPerTile[tile];
    }

    StencilExecutionResult result;
    result.run.selection = ValidityCount{.valid = written, .total = static_cast<std::uint64_t>(width) * height};
    result.run.outputsWritten = written;
    result.horizontalRadius = radiusX;
    result.verticalRadius = radiusY;
    result.dilation = dilation;
    result.tileRows = plan.tileRows;
    result.tileCount = plan.tileCount;
    result.concurrentTiles = bandCount;
    result.haloRows = static_cast<std::uint64_t>(radiusY) * dilation;
    result.scratchBytes = bandBytes + paddedBytes + phaseBytes + plan.tileCount * sizeof(std::uint64_t);
    result.plannedWorkers = static_cast<std::uint32_t>(bandCount);
    return result;
}

Result<StencilExecutionResult> convolveDensePlane(const ExecutionContext& execution, PlaneView<const float> source,
                                                  PlaneView<float> destination, const DenseStencilRequest& request,
                                                  const KernelValidityView& validity)
{
    Status status = reference_detail::checkExecution(execution, kDenseOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkPair(source, destination, kDenseOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkValidity(validity, source.width(), source.height(), kDenseOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkOverlap(source, destination, false, kDenseOperation);
    if (status.failed())
    {
        return status;
    }
    status = checkBorders(request.borders, kDenseOperation);
    if (status.failed())
    {
        return status;
    }
    if (request.width == 0u || request.height == 0u || request.width % 2u == 0u || request.height % 2u == 0u)
    {
        return stencilFailure(ErrorCode::InvalidArgument, kDenseOperation,
                              "a dense stencil needs odd, non-zero extents");
    }
    const std::uint64_t coefficientCount = static_cast<std::uint64_t>(request.width) * request.height;
    if (coefficientCount > kMaxStencilKernelSamples || request.width > kMaxStencilTaps ||
        request.height > kMaxStencilTaps)
    {
        return stencilFailure(ErrorCode::OutOfRange, kDenseOperation,
                              "a dense stencil exceeds the stated coefficient or radius ceiling");
    }
    if (static_cast<std::uint64_t>(request.coefficients.size()) != coefficientCount)
    {
        return stencilFailure(ErrorCode::IncompatibleArguments, kDenseOperation,
                              "the dense stencil coefficient count is not the product of its extents");
    }
    for (const double coefficient : request.coefficients)
    {
        if (!std::isfinite(coefficient))
        {
            return stencilFailure(ErrorCode::InvalidArgument, kDenseOperation,
                                  "a dense stencil coefficient is not finite");
        }
    }

    const std::uint32_t width = source.width();
    const std::uint32_t height = source.height();
    const std::int64_t radiusX = static_cast<std::int64_t>(request.width / 2u);
    const std::int64_t radiusY = static_cast<std::int64_t>(request.height / 2u);
    const reference_detail::ValidityAccessor accepts(validity);
    const TaskCost rowCost{.bytesPerItem = static_cast<std::uint64_t>(width) * coefficientCount * sizeof(float),
                           .operationsPerItem = static_cast<std::uint64_t>(width) * coefficientCount * 2u};
    const std::uint64_t chunkCount = (std::max)(std::uint64_t{1}, deterministicChunkCount(height, rowCost));

    Result<plane_detail::ScopedContextMemory> counters = plane_detail::ScopedContextMemory::take(
        execution, chunkCount * sizeof(std::uint64_t), alignof(std::uint64_t), kDenseOperation);
    if (!counters.hasValue())
    {
        return counters.status();
    }
    std::uint64_t* const writtenPerChunk = static_cast<std::uint64_t*>(counters->data());
    std::fill_n(writtenPerChunk, static_cast<std::size_t>(chunkCount), std::uint64_t{0});

    status = parallelFor(execution, chunkCount, rowCost, [&](TaskRange chunkIndices) -> Status {
        for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
        {
            const TaskRange rows = plane_detail::deterministicRange(chunk, height, chunkCount);
            std::uint64_t produced = 0u;
            for (std::uint64_t row = rows.begin; row < rows.end; ++row)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(row);
                const std::span<const float> input = *source.row(y);
                const std::span<float> output = *destination.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (!accepts.accepts(input[x], x, y))
                    {
                        continue;
                    }
                    double sum = 0.0;
                    std::size_t coefficient = 0u;
                    for (std::int64_t offsetY = -radiusY; offsetY <= radiusY; ++offsetY)
                    {
                        for (std::int64_t offsetX = -radiusX; offsetX <= radiusX; ++offsetX, ++coefficient)
                        {
                            sum +=
                                request.coefficients[coefficient] *
                                denseSampleAt(source, accepts, request.borders, static_cast<std::int64_t>(x) + offsetX,
                                              static_cast<std::int64_t>(y) + offsetY);
                        }
                    }
                    output[x] = static_cast<float>(sum);
                    ++produced;
                }
            }
            writtenPerChunk[chunk] = produced;
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    std::uint64_t written = 0u;
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        written += writtenPerChunk[chunk];
    }

    StencilExecutionResult result;
    result.run.selection = ValidityCount{.valid = written, .total = static_cast<std::uint64_t>(width) * height};
    result.run.outputsWritten = written;
    result.horizontalRadius = static_cast<std::uint32_t>(radiusX);
    result.verticalRadius = static_cast<std::uint32_t>(radiusY);
    result.tileRows = static_cast<std::uint32_t>(
        (std::max)(std::uint64_t{1}, (static_cast<std::uint64_t>(height) + chunkCount - 1u) / chunkCount));
    result.tileCount = chunkCount;
    result.concurrentTiles = chunkCount;
    result.haloRows = static_cast<std::uint64_t>(radiusY);
    result.scratchBytes = 0u;
    result.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunkCount));
    return result;
}

} // namespace an
