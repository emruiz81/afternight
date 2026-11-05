// AfterNightEngine - fitting a background surface to measurements, and taking it away.
//
// The fit forms a small dense system from the sites and solves it in Float64 on the calling thread:
// normal equations and a rank-revealing Cholesky factorisation for the polynomial, elimination with
// partial pivoting for the thin-plate spline. Evaluation and correction walk the picture on the
// operation's threads and read the surface either directly or through a lattice a stride asked for,
// filled once per model. The correction never materialises the surface at full resolution: each row
// evaluates it where it writes.
//
// The thin-plate spline's kernel and its smoothing, which puts `1e-4 * 10^(3 * (smoothing - 0.5))` times
// the mean row sum of the kernel matrix on its diagonal, are adapted from Siril's background extraction
// (https://siril.org, `siril/src/algos/background_extraction.c`), whose file states
// `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)` and
// `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. The spline's two linear terms beside its constant, the polynomial
// fit, the solvers, the lattice and the correction fused into the write are the engine's. The provenance
// note is `docs/dev/BACKGROUND_SAMPLING_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Background/BackgroundModel.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/background/BackgroundInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace an
{
namespace
{

using background_detail::PolynomialTerm;

constexpr const char* kTermCountOperation = "an::backgroundModelTermCount";
constexpr const char* kFitOperation = "an::fitBackgroundModel";
constexpr const char* kEvaluateOperation = "an::evaluateBackgroundModel";
constexpr const char* kApplyOperation = "an::applyImageBackground";

/// One failure from this domain.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status backgroundFailure(ErrorCode code, const char* operation, const char* message)
{
    return spatial_detail::spatialFailure(code, operation, message);
}

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract allows.
/// @return the failure, carrying both counts.
[[nodiscard]] Status backgroundLimitFailure(ErrorCode code, const char* operation, const char* message,
                                            std::uint64_t requested, std::uint64_t limit)
{
    return spatial_detail::spatialLimitFailure(code, operation, message, requested, limit);
}

/// How much work one row of a pass is, so the scheduler chunks rows rather than guessing.
///
/// @param width how many samples one row holds.
/// @param operationsPerSample roughly what one sample costs.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(std::uint32_t width, std::uint64_t operationsPerSample) noexcept
{
    const auto samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * sizeof(float) * 2u, .operationsPerItem = samples * operationsPerSample};
}

/// A fitted surface, ready to be asked for its value at a position.
///
/// It borrows the model's coefficients and the normalised site positions it was built beside, and
/// must not outlive either.
struct SurfaceEvaluator
{
    /// Whether the surface is a polynomial rather than a thin-plate spline.
    bool polynomial = true;

    /// The polynomial basis, empty for a spline.
    std::array<PolynomialTerm, background_detail::kMaxPolynomialTerms> terms{};

    /// How many of those terms the basis holds.
    std::uint32_t termCount = 0u;

    /// The fitted coefficients.
    std::span<const double> coefficients{};

    /// The sites' normalised horizontal coordinates, read by a spline alone.
    const double* siteX = nullptr;

    /// The sites' normalised vertical coordinates, read by a spline alone.
    const double* siteY = nullptr;

    /// How many sites the spline is centred on.
    std::uint32_t siteCount = 0u;

    /// How many pixels one row of the picture holds.
    std::uint32_t width = 0u;

    /// How many rows the picture holds.
    std::uint32_t height = 0u;

    /// The surface's value at one pixel position.
    ///
    /// @param x the column.
    /// @param y the row.
    /// @return the value, in the image's own units.
    [[nodiscard]] double at(double x, double y) const noexcept
    {
        const double nx = background_detail::normalisedCoordinate(x, width);
        const double ny = background_detail::normalisedCoordinate(y, height);
        if (polynomial)
        {
            return background_detail::evaluatePolynomial(std::span<const PolynomialTerm>{terms.data(), termCount},
                                                         coefficients, nx, ny);
        }
        double sum = 0.0;
        for (std::uint32_t index = 0u; index < siteCount; ++index)
        {
            const double dx = nx - siteX[index];
            const double dy = ny - siteY[index];
            sum += coefficients[index] * background_detail::thinPlateRadial(dx * dx + dy * dy);
        }
        const std::size_t affine = siteCount;
        return sum + coefficients[affine] + coefficients[affine + 1u] * nx + coefficients[affine + 2u] * ny;
    }
};

/// A surface read either directly or through a lattice it was evaluated on.
///
/// Which of the two a caller gets is a cost decision it stated; the arithmetic downstream is the
/// same either way, which is what keeps a stride from being a second implementation.
struct Surface
{
    /// The surface itself.
    const SurfaceEvaluator* evaluator = nullptr;

    /// The lattice, row-major, or null when the surface is evaluated directly.
    const double* lattice = nullptr;

    /// How many lattice points one row holds.
    std::uint32_t latticeWidth = 0u;

    /// How many rows of lattice points there are.
    std::uint32_t latticeHeight = 0u;

    /// The lattice spacing, in pixels.
    std::uint32_t stride = 1u;

    /// How many pixels one row of the picture holds.
    std::uint32_t width = 0u;

    /// How many rows the picture holds.
    std::uint32_t height = 0u;

    /// One axis resolved against the lattice.
    ///
    /// @param position the pixel coordinate.
    /// @param extent how many pixels the axis holds.
    /// @param points how many lattice points cover it.
    /// @param index where the lower lattice index goes.
    /// @return the interpolation weight of the upper point.
    [[nodiscard]] double resolve(std::uint32_t position, std::uint32_t extent, std::uint32_t points,
                                 std::uint32_t& index) const noexcept
    {
        if (points <= 1u)
        {
            index = 0u;
            return 0.0;
        }
        index = (std::min)(position / stride, points - 2u);
        const std::uint32_t low = index * stride;
        const std::uint32_t high = (std::min)((index + 1u) * stride, extent - 1u);
        if (high <= low)
        {
            return 0.0;
        }
        return static_cast<double>(position - low) / static_cast<double>(high - low);
    }

    /// The surface's value at one pixel.
    ///
    /// @param x the column.
    /// @param y the row.
    /// @return the value, in the image's own units.
    [[nodiscard]] double at(std::uint32_t x, std::uint32_t y) const noexcept
    {
        if (lattice == nullptr)
        {
            return evaluator->at(static_cast<double>(x), static_cast<double>(y));
        }
        std::uint32_t column = 0u;
        std::uint32_t row = 0u;
        const double columnWeight = resolve(x, width, latticeWidth, column);
        const double rowWeight = resolve(y, height, latticeHeight, row);
        const std::uint32_t nextColumn = (std::min)(column + 1u, latticeWidth - 1u);
        const std::uint32_t nextRow = (std::min)(row + 1u, latticeHeight - 1u);
        const double* const upper = lattice + static_cast<std::size_t>(row) * latticeWidth;
        const double* const lower = lattice + static_cast<std::size_t>(nextRow) * latticeWidth;
        const double top = upper[column] * (1.0 - columnWeight) + upper[nextColumn] * columnWeight;
        const double bottom = lower[column] * (1.0 - columnWeight) + lower[nextColumn] * columnWeight;
        return top * (1.0 - rowWeight) + bottom * rowWeight;
    }
};

/// How many lattice points one axis needs at a stated spacing.
///
/// @param extent how many pixels the axis holds.
/// @param stride the spacing, at least one.
/// @return the point count, whose last point always lands on the axis's last pixel.
[[nodiscard]] std::uint32_t latticePoints(std::uint32_t extent, std::uint32_t stride) noexcept
{
    if (extent <= 1u)
    {
        return 1u;
    }
    return (extent - 2u) / stride + 2u;
}

/// Whether a model view describes a surface this domain can evaluate.
///
/// @param model the fitted surface and the picture it describes.
/// @param operation the entry point the caller is inside.
/// @return success, or the first reason the model means nothing.
[[nodiscard]] Status checkModel(const BackgroundModelView& model, const char* operation)
{
    const Result<std::uint32_t> terms =
        backgroundModelTermCount(model.fit, static_cast<std::uint32_t>(model.sites.size()));
    if (!terms.hasValue())
    {
        return terms.status();
    }
    if (model.width == 0u || model.height == 0u)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, operation, "a model describes a picture of some size");
    }
    if (model.coefficients.size() != terms.value())
    {
        return backgroundLimitFailure(ErrorCode::InvalidArgument, operation,
                                      "the coefficient count is not the one this model implies",
                                      model.coefficients.size(), terms.value());
    }
    for (const double coefficient : model.coefficients)
    {
        if (!std::isfinite(coefficient))
        {
            return backgroundFailure(ErrorCode::InvalidArgument, operation, "a model coefficient is not finite");
        }
    }
    if (model.fit.kind == BackgroundModelKind::RadialBasis && model.sites.empty())
    {
        return backgroundFailure(ErrorCode::EmptySelection, operation,
                                 "a thin-plate spline is centred on its sites and needs at least one");
    }
    return Status::success();
}

/// Builds the evaluator of one model beside a scratch array of normalised site positions.
///
/// @param model the fitted surface.
/// @param positions room for two doubles per site, borrowed for the evaluator's life.
/// @return the evaluator.
[[nodiscard]] SurfaceEvaluator buildEvaluator(const BackgroundModelView& model, std::span<double> positions) noexcept
{
    SurfaceEvaluator evaluator{};
    evaluator.polynomial = model.fit.kind == BackgroundModelKind::Polynomial;
    evaluator.coefficients = model.coefficients;
    evaluator.width = model.width;
    evaluator.height = model.height;
    if (evaluator.polynomial)
    {
        evaluator.termCount = background_detail::writePolynomialTerms(model.fit.degree, evaluator.terms);
        return evaluator;
    }
    evaluator.siteCount = static_cast<std::uint32_t>(model.sites.size());
    evaluator.siteX = positions.data();
    evaluator.siteY = positions.data() + evaluator.siteCount;
    for (std::uint32_t index = 0u; index < evaluator.siteCount; ++index)
    {
        positions[index] = background_detail::normalisedCoordinate(model.sites[index].x, model.width);
        positions[static_cast<std::size_t>(evaluator.siteCount) + index] =
            background_detail::normalisedCoordinate(model.sites[index].y, model.height);
    }
    return evaluator;
}

/// How many operations one surface evaluation costs, for the scheduler's grain policy.
///
/// @param evaluator the surface.
/// @return the operation count.
[[nodiscard]] std::uint64_t evaluationCost(const SurfaceEvaluator& evaluator) noexcept
{
    return evaluator.polynomial ? static_cast<std::uint64_t>(evaluator.termCount) * 2u
                                : static_cast<std::uint64_t>(evaluator.siteCount) * 6u;
}

/// Fills a lattice with one surface's values.
///
/// @param execution the operation policy and engine-context lease.
/// @param evaluator the surface.
/// @param surface the lattice geometry.
/// @param lattice where the values go.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status fillLattice(const ExecutionContext& execution, const SurfaceEvaluator& evaluator,
                                 const Surface& surface, std::span<double> lattice)
{
    const std::uint32_t points = surface.latticeWidth;
    const std::uint32_t stride = surface.stride;
    const std::uint32_t width = surface.width;
    const std::uint32_t height = surface.height;
    return parallelFor(execution, surface.latticeHeight,
                       TaskCost{.bytesPerItem = static_cast<std::uint64_t>(points) * sizeof(double),
                                .operationsPerItem = static_cast<std::uint64_t>(points) * evaluationCost(evaluator)},
                       [&](TaskRange range) {
                           for (std::uint64_t index = range.begin; index < range.end; ++index)
                           {
                               const auto row = static_cast<std::uint32_t>(index);
                               const std::uint32_t y = (std::min)(row * stride, height - 1u);
                               double* const into = lattice.data() + static_cast<std::size_t>(row) * points;
                               for (std::uint32_t column = 0u; column < points; ++column)
                               {
                                   const std::uint32_t x = (std::min)(column * stride, width - 1u);
                                   into[column] = evaluator.at(static_cast<double>(x), static_cast<double>(y));
                               }
                           }
                           return Status::success();
                       });
}

/// The median of a surface over a bounded lattice of the picture.
///
/// @param execution the operation policy and engine-context lease.
/// @param evaluator the surface.
/// @param median where the median goes.
/// @param workingBytes where the temporary lattice's byte count goes.
/// @return success, or an execution, cancellation or memory failure.
[[nodiscard]] Status surfaceMedian(const ExecutionContext& execution, const SurfaceEvaluator& evaluator, double& median,
                                   std::uint64_t& workingBytes)
{
    const std::uint32_t columns = (std::min)(evaluator.width, kBackgroundLevelLatticeExtent);
    const std::uint32_t rows = (std::min)(evaluator.height, kBackgroundLevelLatticeExtent);
    const std::uint64_t count = static_cast<std::uint64_t>(columns) * rows;
    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, count * sizeof(double), alignof(double), kApplyOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    workingBytes = memory->bytes();
    const std::span<double> values{static_cast<double*>(memory->data()), static_cast<std::size_t>(count)};
    const double horizontal =
        columns > 1u ? static_cast<double>(evaluator.width - 1u) / static_cast<double>(columns - 1u) : 0.0;
    const double vertical =
        rows > 1u ? static_cast<double>(evaluator.height - 1u) / static_cast<double>(rows - 1u) : 0.0;
    const Status walked =
        parallelFor(execution, rows,
                    TaskCost{.bytesPerItem = static_cast<std::uint64_t>(columns) * sizeof(double),
                             .operationsPerItem = static_cast<std::uint64_t>(columns) * evaluationCost(evaluator)},
                    [&](TaskRange range) {
                        for (std::uint64_t index = range.begin; index < range.end; ++index)
                        {
                            const auto row = static_cast<std::uint32_t>(index);
                            const double y = static_cast<double>(row) * vertical;
                            double* const into = values.data() + static_cast<std::size_t>(row) * columns;
                            for (std::uint32_t column = 0u; column < columns; ++column)
                            {
                                into[column] = evaluator.at(static_cast<double>(column) * horizontal, y);
                            }
                        }
                        return Status::success();
                    });
    if (walked.failed())
    {
        return walked;
    }
    median = background_detail::medianOf(values);
    return Status::success();
}

/// The mean of every site's median over every channel it carries.
///
/// @param sites the measured sites.
/// @return the mean, or zero when there are no sites.
[[nodiscard]] double sampleMeanLevel(std::span<const BackgroundSample> sites) noexcept
{
    double sum = 0.0;
    std::uint64_t counted = 0u;
    for (const BackgroundSample& site : sites)
    {
        for (std::uint32_t channel = 0u; channel < site.channelCount && channel < kMaxBackgroundChannels; ++channel)
        {
            sum += site.median[channel];
            ++counted;
        }
    }
    return counted == 0u ? 0.0 : sum / static_cast<double>(counted);
}

/// The mean of the samples of one plane the validity rule accepts.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the plane to read.
/// @param validity the rule and optional packed mask.
/// @param mean where the mean goes.
/// @return success; `ErrorCode::EmptySelection` when nothing was accepted; or an execution failure.
[[nodiscard]] Status planeMean(const ExecutionContext& execution, PlaneView<const float> plane,
                               const KernelValidityView& validity, double& mean)
{
    const std::uint32_t width = plane.width();
    std::atomic<std::uint64_t> counted{0u};
    const auto add = [](double left, double right) noexcept { return left + right; };
    const Result<double> total = parallelReduce(
        execution, plane.height(), rowCost(width, 2u), 0.0,
        [&](TaskRange range, double& partial) {
            std::uint64_t here = 0u;
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<const float> row = *plane.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (!sampleIsValidByValue(validity.descriptor, row[x]) || !plane_detail::maskAllows(validity, x, y))
                    {
                        continue;
                    }
                    partial += static_cast<double>(row[x]);
                    ++here;
                }
            }
            counted.fetch_add(here, std::memory_order_relaxed);
            return Status::success();
        },
        add);
    if (!total.hasValue())
    {
        return total.status();
    }
    const std::uint64_t accepted = counted.load(std::memory_order_relaxed);
    if (accepted == 0u)
    {
        return backgroundFailure(ErrorCode::EmptySelection, kApplyOperation,
                                 "a level taken from the source needs a sample the validity rule accepts");
    }
    mean = total.value() / static_cast<double>(accepted);
    return Status::success();
}

} // namespace

const char* backgroundModelKindName(BackgroundModelKind kind) noexcept
{
    switch (kind)
    {
    case BackgroundModelKind::Polynomial:
        return "polynomial";
    case BackgroundModelKind::RadialBasis:
        return "radial-basis";
    }
    return "unknown";
}

const char* backgroundLevelName(BackgroundLevel level) noexcept
{
    switch (level)
    {
    case BackgroundLevel::ModelMedian:
        return "model-median";
    case BackgroundLevel::SampleMean:
        return "sample-mean";
    case BackgroundLevel::SourceMean:
        return "source-mean";
    case BackgroundLevel::Stated:
        return "stated";
    }
    return "unknown";
}

Result<std::uint32_t> backgroundModelTermCount(const BackgroundFitRequest& request, std::uint32_t siteCount)
{
    if (!isBackgroundModelKind(request.kind))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kTermCountOperation,
                                 "the model is not one this engine implements");
    }
    if (!std::isfinite(request.smoothing) || request.smoothing < 0.0 || request.smoothing > 1.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kTermCountOperation,
                                 "the smoothing has to be a finite number in the unit interval");
    }
    if (siteCount > kMaxBackgroundSites)
    {
        return backgroundLimitFailure(ErrorCode::OutOfRange, kTermCountOperation,
                                      "a background fit accepts fewer sites than this", siteCount, kMaxBackgroundSites);
    }
    if (request.kind == BackgroundModelKind::Polynomial)
    {
        if (request.degree == 0u)
        {
            return backgroundFailure(ErrorCode::InvalidArgument, kTermCountOperation,
                                     "a polynomial background of degree zero is a constant nothing needs to fit");
        }
        if (request.degree > kMaxBackgroundPolynomialDegree)
        {
            return backgroundLimitFailure(ErrorCode::OutOfRange, kTermCountOperation,
                                          "the polynomial degree is above what this domain accepts", request.degree,
                                          kMaxBackgroundPolynomialDegree);
        }
        return background_detail::polynomialTermCount(request.degree);
    }
    if (siteCount > kMaxRadialBasisSites)
    {
        return backgroundLimitFailure(ErrorCode::OutOfRange, kTermCountOperation,
                                      "a thin-plate spline solves a system of one row per site and accepts fewer",
                                      siteCount, kMaxRadialBasisSites);
    }
    return siteCount + background_detail::kThinPlateAffineTerms;
}

Result<BackgroundFitResult> fitBackgroundModel(const ExecutionContext& execution, const BackgroundFitRequest& request,
                                               std::span<const BackgroundSample> sites, std::uint32_t channel,
                                               std::uint32_t width, std::uint32_t height,
                                               std::span<double> coefficients)
{
    if (!execution.valid())
    {
        return backgroundFailure(ErrorCode::NotInitialized, kFitOperation,
                                 "a background fit needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    const Result<std::uint32_t> termCount = backgroundModelTermCount(request, static_cast<std::uint32_t>(sites.size()));
    if (!termCount.hasValue())
    {
        return termCount.status();
    }
    if (width == 0u || height == 0u)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kFitOperation,
                                 "the coordinate normalisation needs a picture of some size");
    }
    if (sites.empty())
    {
        return backgroundFailure(ErrorCode::EmptySelection, kFitOperation, "a background fit needs at least one site");
    }
    if (coefficients.size() != termCount.value())
    {
        return backgroundLimitFailure(ErrorCode::InvalidArgument, kFitOperation,
                                      "the coefficient array is not the length this model implies", coefficients.size(),
                                      termCount.value());
    }
    for (const BackgroundSample& site : sites)
    {
        if (channel >= site.channelCount || channel >= kMaxBackgroundChannels)
        {
            return backgroundLimitFailure(ErrorCode::OutOfRange, kFitOperation,
                                          "a site carries no median for that channel", channel, site.channelCount);
        }
        if (!std::isfinite(site.median[channel]) || !std::isfinite(site.x) || !std::isfinite(site.y))
        {
            return backgroundFailure(ErrorCode::InvalidArgument, kFitOperation,
                                     "a site carries a position or a median that is not finite");
        }
    }

    BackgroundFitResult result{};
    result.termCount = termCount.value();
    result.siteCount = static_cast<std::uint32_t>(sites.size());

    const std::uint32_t order = termCount.value();
    const std::uint64_t matrixBytes = static_cast<std::uint64_t>(order) * order * sizeof(double);
    Result<plane_detail::ScopedContextMemory> matrixMemory =
        plane_detail::ScopedContextMemory::take(execution, matrixBytes, alignof(double), kFitOperation);
    if (!matrixMemory.hasValue())
    {
        return matrixMemory.status();
    }
    const std::span<double> matrix{static_cast<double*>(matrixMemory->data()), static_cast<std::size_t>(order) * order};
    std::fill(matrix.begin(), matrix.end(), 0.0);
    Result<plane_detail::ScopedContextMemory> rhsMemory = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(order) * sizeof(double), alignof(double), kFitOperation);
    if (!rhsMemory.hasValue())
    {
        return rhsMemory.status();
    }
    const std::span<double> rhs{static_cast<double*>(rhsMemory->data()), order};
    std::fill(rhs.begin(), rhs.end(), 0.0);

    const auto at = [&](std::uint32_t row, std::uint32_t column) -> double& {
        return matrix[static_cast<std::size_t>(row) * order + column];
    };

    if (request.kind == BackgroundModelKind::Polynomial)
    {
        std::array<PolynomialTerm, background_detail::kMaxPolynomialTerms> terms{};
        const std::uint32_t written = background_detail::writePolynomialTerms(request.degree, terms);
        std::array<double, background_detail::kMaxPolynomialTerms> basis{};
        for (const BackgroundSample& site : sites)
        {
            const Status siteRunning = execution.checkCancellation();
            if (siteRunning.failed())
            {
                return siteRunning;
            }
            const double nx = background_detail::normalisedCoordinate(site.x, width);
            const double ny = background_detail::normalisedCoordinate(site.y, height);
            background_detail::evaluatePolynomialBasis(std::span<const PolynomialTerm>{terms.data(), written}, nx, ny,
                                                       basis);
            const double value = site.median[channel];
            for (std::uint32_t row = 0u; row < order; ++row)
            {
                rhs[row] += basis[row] * value;
                for (std::uint32_t column = 0u; column <= row; ++column)
                {
                    at(row, column) += basis[row] * basis[column];
                }
            }
        }
        // The factorisation reads the lower triangle alone, so the upper one is left as it was.
        const background_detail::SymmetricSolveResult solved = background_detail::solveSymmetric(matrix, rhs, order);
        if (!solved.solved)
        {
            return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                     "these sites do not determine a background surface at all");
        }
        result.droppedTerms = solved.droppedTerms;
        result.conditionEstimate = solved.conditionEstimate;
        for (const double coefficient : rhs)
        {
            if (!std::isfinite(coefficient))
            {
                return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                         "the polynomial solve produced a coefficient that is not finite");
            }
        }

        for (const BackgroundSample& site : sites)
        {
            const Status siteRunning = execution.checkCancellation();
            if (siteRunning.failed())
            {
                return siteRunning;
            }
            const double nx = background_detail::normalisedCoordinate(site.x, width);
            const double ny = background_detail::normalisedCoordinate(site.y, height);
            const double fitted = background_detail::evaluatePolynomial(
                std::span<const PolynomialTerm>{terms.data(), written}, rhs, nx, ny);
            const double residual = std::abs(site.median[channel] - fitted);
            if (!std::isfinite(residual))
            {
                return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                         "the polynomial fit produced a residual that is not finite");
            }
            result.largestResidual = (std::max)(result.largestResidual, residual);
            result.residualRootMeanSquare += residual * residual;
        }
        result.residualRootMeanSquare = std::sqrt(result.residualRootMeanSquare / static_cast<double>(sites.size()));
        if (!std::isfinite(result.residualRootMeanSquare))
        {
            return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                     "the polynomial fit produced an error measure that is not finite");
        }
        std::copy(rhs.begin(), rhs.end(), coefficients.begin());
        return result;
    }

    const auto siteCount = static_cast<std::uint32_t>(sites.size());
    Result<plane_detail::ScopedContextMemory> positionMemory = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(siteCount) * 2u * sizeof(double), alignof(double), kFitOperation);
    if (!positionMemory.hasValue())
    {
        return positionMemory.status();
    }
    const std::span<double> positions{static_cast<double*>(positionMemory->data()),
                                      static_cast<std::size_t>(siteCount) * 2u};
    for (std::uint32_t index = 0u; index < siteCount; ++index)
    {
        const Status siteRunning = execution.checkCancellation();
        if (siteRunning.failed())
        {
            return siteRunning;
        }
        positions[index] = background_detail::normalisedCoordinate(sites[index].x, width);
        positions[static_cast<std::size_t>(siteCount) + index] =
            background_detail::normalisedCoordinate(sites[index].y, height);
    }

    double kernelTotal = 0.0;
    for (std::uint32_t row = 0u; row < siteCount; ++row)
    {
        const Status rowRunning = execution.checkCancellation();
        if (rowRunning.failed())
        {
            return rowRunning;
        }
        const double x = positions[row];
        const double y = positions[static_cast<std::size_t>(siteCount) + row];
        for (std::uint32_t column = 0u; column < siteCount; ++column)
        {
            const double dx = x - positions[column];
            const double dy = y - positions[static_cast<std::size_t>(siteCount) + column];
            const double kernel = background_detail::thinPlateRadial(dx * dx + dy * dy);
            at(row, column) = kernel;
            kernelTotal += kernel;
        }
        at(row, siteCount) = 1.0;
        at(row, siteCount + 1u) = x;
        at(row, siteCount + 2u) = y;
        at(siteCount, row) = 1.0;
        at(siteCount + 1u, row) = x;
        at(siteCount + 2u, row) = y;
        rhs[row] = sites[row].median[channel];
    }
    // The mapping from a smoothing in the unit interval onto a ridge. The ridge is proportional to the
    // kernel's own scale rather than absolute, so it says the same thing whatever the frame's units are.
    const double ridge =
        1.0e-4 * std::pow(10.0, (request.smoothing - 0.5) * 3.0) * kernelTotal / static_cast<double>(siteCount);
    if (!std::isfinite(ridge))
    {
        return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                 "the thin-plate system exceeds the finite range of its arithmetic");
    }
    for (std::uint32_t index = 0u; index < siteCount; ++index)
    {
        at(index, index) += ridge;
    }

    const Result<bool> solved = background_detail::solveGeneral(execution, matrix, rhs, order);
    if (!solved.hasValue())
    {
        return solved.status();
    }
    if (!solved.value())
    {
        return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                 "the thin-plate system these sites describe is singular; sites that all lie on one "
                                 "line, or two sites at one position, describe no surface");
    }
    for (const double coefficient : rhs)
    {
        if (!std::isfinite(coefficient))
        {
            return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                     "the thin-plate solve produced a coefficient that is not finite");
        }
    }

    SurfaceEvaluator evaluator{};
    evaluator.polynomial = false;
    evaluator.coefficients = rhs;
    evaluator.siteX = positions.data();
    evaluator.siteY = positions.data() + siteCount;
    evaluator.siteCount = siteCount;
    evaluator.width = width;
    evaluator.height = height;
    for (const BackgroundSample& site : sites)
    {
        const Status siteRunning = execution.checkCancellation();
        if (siteRunning.failed())
        {
            return siteRunning;
        }
        const double residual = std::abs(site.median[channel] - evaluator.at(site.x, site.y));
        if (!std::isfinite(residual))
        {
            return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                     "the thin-plate fit produced a residual that is not finite");
        }
        result.largestResidual = (std::max)(result.largestResidual, residual);
        result.residualRootMeanSquare += residual * residual;
    }
    result.residualRootMeanSquare = std::sqrt(result.residualRootMeanSquare / static_cast<double>(siteCount));
    if (!std::isfinite(result.residualRootMeanSquare))
    {
        return backgroundFailure(ErrorCode::InvalidData, kFitOperation,
                                 "the thin-plate fit produced an error measure that is not finite");
    }
    std::copy(rhs.begin(), rhs.end(), coefficients.begin());
    return result;
}

Result<BackgroundEvaluationResult> evaluateBackgroundModel(const ExecutionContext& execution,
                                                           const BackgroundModelView& model,
                                                           PlaneView<float> destination, std::uint32_t evaluationStride)
{
    if (!execution.valid())
    {
        return backgroundFailure(ErrorCode::NotInitialized, kEvaluateOperation,
                                 "a model evaluation needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    const Status checked = checkModel(model, kEvaluateOperation);
    if (checked.failed())
    {
        return checked;
    }
    if (evaluationStride == 0u || evaluationStride > kMaxBackgroundEvaluationStride)
    {
        return backgroundLimitFailure(ErrorCode::InvalidArgument, kEvaluateOperation,
                                      "the evaluation lattice spacing is outside its limits", evaluationStride,
                                      kMaxBackgroundEvaluationStride);
    }
    if (destination.width() != model.width || destination.height() != model.height)
    {
        return backgroundFailure(ErrorCode::IncompatibleArguments, kEvaluateOperation,
                                 "the destination plane is not the picture this model describes");
    }

    Result<plane_detail::ScopedContextMemory> positionMemory = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(model.sites.size()) * 2u * sizeof(double), alignof(double),
        kEvaluateOperation);
    if (!positionMemory.hasValue())
    {
        return positionMemory.status();
    }
    const std::span<double> positions{static_cast<double*>(positionMemory->data()), model.sites.size() * 2u};
    const SurfaceEvaluator evaluator = buildEvaluator(model, positions);

    Surface surface{};
    surface.evaluator = &evaluator;
    surface.stride = evaluationStride;
    surface.width = model.width;
    surface.height = model.height;
    plane_detail::ScopedContextMemory latticeMemory;
    std::span<double> lattice{};
    if (evaluationStride > 1u)
    {
        surface.latticeWidth = latticePoints(model.width, evaluationStride);
        surface.latticeHeight = latticePoints(model.height, evaluationStride);
        const std::uint64_t points = static_cast<std::uint64_t>(surface.latticeWidth) * surface.latticeHeight;
        Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
            execution, points * sizeof(double), alignof(double), kEvaluateOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        latticeMemory = std::move(taken).value();
        lattice = std::span<double>{static_cast<double*>(latticeMemory.data()), static_cast<std::size_t>(points)};
        const Status filled = fillLattice(execution, evaluator, surface, lattice);
        if (filled.failed())
        {
            return filled;
        }
        surface.lattice = lattice.data();
    }

    const std::uint32_t width = model.width;
    const auto minimum = [](double left, double right) noexcept { return left < right ? left : right; };
    const auto maximum = [](double left, double right) noexcept { return left > right ? left : right; };

    const Status walked =
        parallelFor(execution, model.height,
                    TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(float),
                             .operationsPerItem = static_cast<std::uint64_t>(width) *
                                                  (surface.lattice != nullptr ? 8u : evaluationCost(evaluator))},
                    [&](TaskRange range) {
                        for (std::uint64_t index = range.begin; index < range.end; ++index)
                        {
                            const auto y = static_cast<std::uint32_t>(index);
                            const std::span<float> row = *destination.row(y);
                            for (std::uint32_t x = 0u; x < width; ++x)
                            {
                                row[x] = static_cast<float>(surface.at(x, y));
                            }
                        }
                        return Status::success();
                    });
    if (walked.failed())
    {
        return walked;
    }

    const Result<double> smallest = parallelReduce(
        execution, model.height, rowCost(width, 2u), std::numeric_limits<double>::max(),
        [&](TaskRange range, double& partial) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const std::span<const float> row = *destination.row(static_cast<std::uint32_t>(index));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    partial = (std::min)(partial, static_cast<double>(row[x]));
                }
            }
            return Status::success();
        },
        minimum);
    if (!smallest.hasValue())
    {
        return smallest.status();
    }
    const Result<double> largest = parallelReduce(
        execution, model.height, rowCost(width, 2u), std::numeric_limits<double>::lowest(),
        [&](TaskRange range, double& partial) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const std::span<const float> row = *destination.row(static_cast<std::uint32_t>(index));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    partial = (std::max)(partial, static_cast<double>(row[x]));
                }
            }
            return Status::success();
        },
        maximum);
    if (!largest.hasValue())
    {
        return largest.status();
    }
    BackgroundEvaluationResult result{};
    result.samplesWritten = static_cast<std::uint64_t>(model.width) * model.height;
    result.evaluationStride = evaluationStride;
    result.lowest = smallest.value();
    result.highest = largest.value();
    result.workingBytes = positionMemory->bytes() + latticeMemory.bytes();
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float64;
    return result;
}

Result<BackgroundApplyResult> applyImageBackground(const ExecutionContext& execution, SampleType sampleType,
                                                   const void* source, const ImageLayout& sourceLayout,
                                                   void* destination, const ImageLayout& destinationLayout,
                                                   const BackgroundApplyRequest& request,
                                                   std::span<const BackgroundModelView> models,
                                                   const SpatialSelection& selection)
{
    spatial_detail::SpatialGeometry geometry{};
    const Status ready = spatial_detail::checkSpatialImage(execution, sampleType, source, sourceLayout, request.output,
                                                           selection, kApplyOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (destination == nullptr)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                 "a correction needs both a source and a destination");
    }
    if (destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                 "a layout's element size is not the stated representation's");
    }
    if (destinationLayout.extent().width() != geometry.width ||
        destinationLayout.extent().height() != geometry.height ||
        destinationLayout.extent().channelCount() != geometry.channelCount)
    {
        return backgroundFailure(ErrorCode::IncompatibleArguments, kApplyOperation,
                                 "the source and the destination are not the same picture");
    }
    const bool exactlyAliased = source == destination && sourceLayout == destinationLayout;
    if (!exactlyAliased &&
        regionsOverlap(static_cast<const std::uint8_t*>(source), sourceLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return backgroundFailure(ErrorCode::Aliasing, kApplyOperation,
                                 "a correction source overlaps its destination without being the same image");
    }
    if (!isBackgroundCorrection(request.correction) || !isBackgroundLevel(request.level))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                 "the correction or the level rule is not one this engine implements");
    }
    if (!std::isfinite(request.guard) || request.guard <= 0.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                 "the division guard has to be finite and above zero");
    }
    if (!std::isfinite(request.statedLevel))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation, "the stated level is not finite");
    }
    if (!std::isfinite(request.dither) || request.dither < 0.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                 "the dither width has to be finite and not negative");
    }
    if (request.evaluationStride == 0u || request.evaluationStride > kMaxBackgroundEvaluationStride)
    {
        return backgroundLimitFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                      "the evaluation lattice spacing is outside its limits", request.evaluationStride,
                                      kMaxBackgroundEvaluationStride);
    }
    if (request.clamp.has_value() && (!std::isfinite(request.clamp->lowest) || !std::isfinite(request.clamp->highest) ||
                                      request.clamp->lowest > request.clamp->highest))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                 "a correction clamp needs two finite endpoints in order");
    }
    if (geometry.channelCount > kMaxBackgroundChannels)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kApplyOperation,
                                      "a correction reports the level it put back for at most this many channels",
                                      geometry.channelCount, kMaxBackgroundChannels);
    }
    if (models.size() != geometry.channelCount)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kApplyOperation,
                                      "a correction takes one model per channel", models.size(), geometry.channelCount);
    }
    std::uint64_t largestSiteCount = 0u;
    for (const BackgroundModelView& model : models)
    {
        const Status checked = checkModel(model, kApplyOperation);
        if (checked.failed())
        {
            return checked;
        }
        if (model.width != geometry.width || model.height != geometry.height)
        {
            return backgroundFailure(ErrorCode::IncompatibleArguments, kApplyOperation,
                                     "a model does not describe the picture being corrected");
        }
        largestSiteCount = (std::max)(largestSiteCount, static_cast<std::uint64_t>(model.sites.size()));
    }

    Result<plane_detail::ScopedContextMemory> positionMemory = plane_detail::ScopedContextMemory::take(
        execution, largestSiteCount * 2u * sizeof(double), alignof(double), kApplyOperation);
    if (!positionMemory.hasValue())
    {
        return positionMemory.status();
    }
    const std::span<double> positions{static_cast<double*>(positionMemory->data()),
                                      static_cast<std::size_t>(largestSiteCount) * 2u};

    const bool sourceInPlace = spatial_detail::channelIsAddressablePlane(sampleType, sourceLayout);
    const bool destinationInPlace = spatial_detail::channelIsAddressablePlane(sampleType, destinationLayout);
    spatial_detail::SpatialPlaneBuffer sourceWorking;
    if (!sourceInPlace)
    {
        Result<spatial_detail::SpatialPlaneBuffer> taken =
            spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kApplyOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        sourceWorking = std::move(taken).value();
    }
    spatial_detail::SpatialPlaneBuffer destinationWorking;
    if (!destinationInPlace)
    {
        Result<spatial_detail::SpatialPlaneBuffer> taken =
            spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kApplyOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        destinationWorking = std::move(taken).value();
    }

    BackgroundApplyResult result{};
    result.channelCount = geometry.channelCount;
    result.evaluationStride = request.evaluationStride;
    result.imagePasses = request.level == BackgroundLevel::SourceMean ? 2u : 1u;
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float32;
    result.workingBytes = positionMemory->bytes() + sourceWorking.bytes() + destinationWorking.bytes();

    const double sharedSampleMean = models.empty() ? 0.0 : sampleMeanLevel(models[0].sites);
    const std::uint32_t width = geometry.width;
    const bool dividing = request.correction == BackgroundCorrection::Divide;
    const double ditherWidth = request.dither;
    const bool dithering = ditherWidth > 0.0;
    const bool clamping = request.clamp.has_value();
    const double clampLow = clamping ? request.clamp->lowest : 0.0;
    const double clampHigh = clamping ? request.clamp->highest : 0.0;
    std::uint64_t guarded = 0u;
    std::uint64_t clamped = 0u;
    std::uint64_t written = 0u;
    std::uint64_t accepted = 0u;

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        if (sourceWorking.owned())
        {
            const Status gathered = spatial_detail::gatherChannelPlane(execution, sampleType, source, sourceLayout,
                                                                       channel, sourceWorking.view(), kApplyOperation);
            if (gathered.failed())
            {
                return gathered;
            }
        }
        const std::optional<PlaneView<const float>> borrowedInput =
            sourceWorking.owned()
                ? std::nullopt
                : spatial_detail::borrowSourcePlane(source, sourceLayout, channel, sampleType, geometry);
        const std::optional<PlaneView<float>> borrowedOutput =
            destinationWorking.owned()
                ? std::nullopt
                : spatial_detail::borrowDestinationPlane(destination, destinationLayout, channel, sampleType, geometry);
        if ((!sourceWorking.owned() && !borrowedInput.has_value()) ||
            (!destinationWorking.owned() && !borrowedOutput.has_value()))
        {
            return backgroundFailure(ErrorCode::InvalidArgument, kApplyOperation,
                                     "a channel of this image is not addressable as a plane");
        }
        const PlaneView<const float> input = sourceWorking.owned() ? sourceWorking.constView() : *borrowedInput;
        const PlaneView<float> output = destinationWorking.owned() ? destinationWorking.view() : *borrowedOutput;
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);

        const SurfaceEvaluator evaluator = buildEvaluator(models[channel], positions);
        Surface surface{};
        surface.evaluator = &evaluator;
        surface.stride = request.evaluationStride;
        surface.width = geometry.width;
        surface.height = geometry.height;
        plane_detail::ScopedContextMemory latticeMemory;
        if (request.evaluationStride > 1u)
        {
            surface.latticeWidth = latticePoints(geometry.width, request.evaluationStride);
            surface.latticeHeight = latticePoints(geometry.height, request.evaluationStride);
            const std::uint64_t points = static_cast<std::uint64_t>(surface.latticeWidth) * surface.latticeHeight;
            Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
                execution, points * sizeof(double), alignof(double), kApplyOperation);
            if (!taken.hasValue())
            {
                return taken.status();
            }
            latticeMemory = std::move(taken).value();
            const std::span<double> lattice{static_cast<double*>(latticeMemory.data()),
                                            static_cast<std::size_t>(points)};
            const Status filled = fillLattice(execution, evaluator, surface, lattice);
            if (filled.failed())
            {
                return filled;
            }
            surface.lattice = lattice.data();
        }

        double level = 0.0;
        switch (request.level)
        {
        case BackgroundLevel::Stated:
            level = request.statedLevel;
            break;
        case BackgroundLevel::SampleMean:
            level = sharedSampleMean;
            break;
        case BackgroundLevel::SourceMean: {
            const Status measured = planeMean(execution, input, validity, level);
            if (measured.failed())
            {
                return measured;
            }
            break;
        }
        case BackgroundLevel::ModelMedian:
        default: {
            std::uint64_t levelWorkingBytes = 0u;
            const Status measured = surfaceMedian(execution, evaluator, level, levelWorkingBytes);
            if (measured.failed())
            {
                return measured;
            }
            result.workingBytes = (std::max)(result.workingBytes, positionMemory->bytes() + sourceWorking.bytes() +
                                                                      destinationWorking.bytes() +
                                                                      latticeMemory.bytes() + levelWorkingBytes);
            break;
        }
        }
        result.appliedLevel[channel] = level;

        std::atomic<std::uint64_t> guardedHere{0u};
        std::atomic<std::uint64_t> clampedHere{0u};
        std::atomic<std::uint64_t> acceptedHere{0u};
        std::atomic<std::uint64_t> changedHere{0u};
        const bool countingInPlace = destinationInPlace;
        const double guard = request.guard;
        const Status corrected =
            parallelFor(execution, geometry.height,
                        TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(float) * 2u,
                                 .operationsPerItem = static_cast<std::uint64_t>(width) *
                                                      (surface.lattice != nullptr ? 8u : evaluationCost(evaluator))},
                        [&](TaskRange range) {
                            std::uint64_t guardedLocal = 0u;
                            std::uint64_t clampedLocal = 0u;
                            std::uint64_t acceptedLocal = 0u;
                            std::uint64_t changedLocal = 0u;
                            for (std::uint64_t index = range.begin; index < range.end; ++index)
                            {
                                const auto y = static_cast<std::uint32_t>(index);
                                const std::span<const float> from = *input.row(y);
                                const std::span<float> to = *output.row(y);
                                for (std::uint32_t x = 0u; x < width; ++x)
                                {
                                    const float stored = from[x];
                                    // A rejected sample is left where it was rather than copied forward: the
                                    // destination may be the source, and writing it back would be the one way
                                    // this operation could change a sample it was told meant nothing.
                                    if (!sampleIsValidByValue(validity.descriptor, stored) ||
                                        !plane_detail::maskAllows(validity, x, y))
                                    {
                                        continue;
                                    }
                                    ++acceptedLocal;
                                    const double model = surface.at(x, y);
                                    // The dither is a function of where the sample is rather than of when it was
                                    // reached, so the answer does not depend on how the rows were divided.
                                    const double sample =
                                        dithering ? static_cast<double>(stored) +
                                                        ditherWidth * background_detail::ditherFraction(
                                                                          static_cast<std::uint64_t>(y) * width + x)
                                                  : static_cast<double>(stored);
                                    double value = 0.0;
                                    if (dividing)
                                    {
                                        double divisor = model;
                                        if (!(divisor >= guard))
                                        {
                                            divisor = guard;
                                            ++guardedLocal;
                                        }
                                        value = sample / divisor * level;
                                    }
                                    else
                                    {
                                        value = sample - model + level;
                                    }
                                    if (clamping)
                                    {
                                        const double held = std::clamp(value, clampLow, clampHigh);
                                        if (held != value)
                                        {
                                            ++clampedLocal;
                                            value = held;
                                        }
                                    }
                                    const auto written = static_cast<float>(value);
                                    // Counted here rather than by comparing two planes afterwards, because an
                                    // in-place correction has overwritten the sample it would be compared with.
                                    changedLocal += (countingInPlace && written != stored) ? 1u : 0u;
                                    to[x] = written;
                                }
                            }
                            guardedHere.fetch_add(guardedLocal, std::memory_order_relaxed);
                            clampedHere.fetch_add(clampedLocal, std::memory_order_relaxed);
                            acceptedHere.fetch_add(acceptedLocal, std::memory_order_relaxed);
                            changedHere.fetch_add(changedLocal, std::memory_order_relaxed);
                            return Status::success();
                        });
        if (corrected.failed())
        {
            return corrected;
        }
        guarded += guardedHere.load(std::memory_order_relaxed);
        clamped += clampedHere.load(std::memory_order_relaxed);
        accepted += acceptedHere.load(std::memory_order_relaxed);
        written += acceptedHere.load(std::memory_order_relaxed);
        result.workingBytes = (std::max)(result.workingBytes, positionMemory->bytes() + sourceWorking.bytes() +
                                                                  destinationWorking.bytes() + latticeMemory.bytes());

        if (destinationInPlace)
        {
            result.samplesChanged += changedHere.load(std::memory_order_relaxed);
        }
        else
        {
            const Status scattered = spatial_detail::scatterChannelPlane(
                execution, destinationWorking.constView(), input, validity, sampleType, destination, destinationLayout,
                channel, request.output.writeBack, kApplyOperation, source, &sourceLayout, &result.samplesChanged);
            if (scattered.failed())
            {
                return scattered;
            }
        }
    }

    const std::uint64_t total = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    result.run.selection = ValidityCount{.valid = accepted, .total = total};
    result.run.outputsWritten = written;
    result.guardedSamples = guarded;
    result.clampedSamples = clamped;
    return result;
}

} // namespace an
