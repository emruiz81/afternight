// AfterNightEngine - the arithmetic both background paths are built out of.
//
// The thin-plate radial function, half the squared distance times its logarithm, is adapted from Siril's
// background extraction (https://siril.org, `siril/src/algos/background_extraction.c`), whose file states
// `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)` and
// `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. The provenance note is
// `docs/dev/BACKGROUND_SAMPLING_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "algorithms/background/BackgroundInternal.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace an::background_detail
{
namespace
{

/// How much work one row of a plane pass is, so the scheduler chunks rows rather than guessing.
///
/// @param width how many samples one row holds.
/// @param planes how many planes the row is read from or written to.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(std::uint32_t width, std::uint64_t planes) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width) * planes;
    return TaskCost{.bytesPerItem = samples * sizeof(float), .operationsPerItem = samples * 2u};
}

/// One coordinate clamped inside an extent.
///
/// @param position where the read wants to be, possibly outside.
/// @param extent how many samples there are. Must be at least one.
/// @return the sample index inside the extent.
[[nodiscard]] std::int64_t clampedIndex(std::int64_t position, std::int64_t extent) noexcept
{
    if (position < 0)
    {
        return 0;
    }
    if (position >= extent)
    {
        return extent - 1;
    }
    return position;
}

/// Averages a moving horizontal window over a plane, as a running sum.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the plane to read.
/// @param destination the plane to write, of the same extent and disjoint from the source.
/// @param radius the half-window, in pixels.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status horizontalPass(const ExecutionContext& execution, PlaneView<const float> source,
                                    PlaneView<float> destination, std::uint32_t radius)
{
    const auto width = static_cast<std::int64_t>(source.width());
    const auto reach = static_cast<std::int64_t>(radius);
    const double inverseWindow = 1.0 / static_cast<double>(2 * reach + 1);
    return parallelFor(execution, source.height(), rowCost(source.width(), 2u), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<const float> from = *source.row(y);
            const std::span<float> to = *destination.row(y);
            double accumulator = 0.0;
            for (std::int64_t offset = -reach; offset <= reach; ++offset)
            {
                accumulator += static_cast<double>(from[static_cast<std::size_t>(clampedIndex(offset, width))]);
            }
            for (std::int64_t x = 0; x < width; ++x)
            {
                to[static_cast<std::size_t>(x)] = static_cast<float>(accumulator * inverseWindow);
                const std::int64_t entering = clampedIndex(x + reach + 1, width);
                const std::int64_t leaving = clampedIndex(x - reach, width);
                accumulator += static_cast<double>(from[static_cast<std::size_t>(entering)]) -
                               static_cast<double>(from[static_cast<std::size_t>(leaving)]);
            }
        }
        return Status::success();
    });
}

/// Averages a moving vertical window over a plane, as a running sum over one column at a time.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the plane to read.
/// @param destination the plane to write, of the same extent and disjoint from the source.
/// @param radius the half-window, in pixels.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status verticalPass(const ExecutionContext& execution, PlaneView<const float> source,
                                  PlaneView<float> destination, std::uint32_t radius)
{
    const auto height = static_cast<std::int64_t>(source.height());
    const auto reach = static_cast<std::int64_t>(radius);
    const double inverseWindow = 1.0 / static_cast<double>(2 * reach + 1);
    return parallelFor(execution, source.width(), rowCost(source.height(), 2u), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto x = static_cast<std::uint32_t>(index);
            double accumulator = 0.0;
            for (std::int64_t offset = -reach; offset <= reach; ++offset)
            {
                accumulator +=
                    static_cast<double>((*source.row(static_cast<std::uint32_t>(clampedIndex(offset, height))))[x]);
            }
            for (std::int64_t y = 0; y < height; ++y)
            {
                (*destination.row(static_cast<std::uint32_t>(y)))[x] = static_cast<float>(accumulator * inverseWindow);
                const std::int64_t entering = clampedIndex(y + reach + 1, height);
                const std::int64_t leaving = clampedIndex(y - reach, height);
                accumulator += static_cast<double>((*source.row(static_cast<std::uint32_t>(entering)))[x]) -
                               static_cast<double>((*source.row(static_cast<std::uint32_t>(leaving)))[x]);
            }
        }
        return Status::success();
    });
}

} // namespace

double normalisedCoordinate(double value, std::uint32_t extent) noexcept
{
    if (extent <= 1u)
    {
        return 0.0;
    }
    return value / static_cast<double>(extent - 1u) * 2.0 - 1.0;
}

std::uint32_t polynomialTermCount(std::uint32_t degree) noexcept
{
    return (degree + 1u) * (degree + 2u) / 2u;
}

std::uint32_t writePolynomialTerms(std::uint32_t degree, std::span<PolynomialTerm> terms) noexcept
{
    std::uint32_t written = 0u;
    for (std::uint32_t total = 0u; total <= degree; ++total)
    {
        for (std::uint32_t xPower = total + 1u; xPower > 0u; --xPower)
        {
            if (written >= terms.size())
            {
                return written;
            }
            terms[written] = PolynomialTerm{.xPower = xPower - 1u, .yPower = total - (xPower - 1u)};
            ++written;
        }
    }
    return written;
}

void evaluatePolynomialBasis(std::span<const PolynomialTerm> terms, double x, double y,
                             std::span<double> values) noexcept
{
    for (std::size_t index = 0u; index < terms.size() && index < values.size(); ++index)
    {
        double value = 1.0;
        for (std::uint32_t power = 0u; power < terms[index].xPower; ++power)
        {
            value *= x;
        }
        for (std::uint32_t power = 0u; power < terms[index].yPower; ++power)
        {
            value *= y;
        }
        values[index] = value;
    }
}

double evaluatePolynomial(std::span<const PolynomialTerm> terms, std::span<const double> coefficients, double x,
                          double y) noexcept
{
    double sum = 0.0;
    for (std::size_t index = 0u; index < terms.size() && index < coefficients.size(); ++index)
    {
        double value = coefficients[index];
        for (std::uint32_t power = 0u; power < terms[index].xPower; ++power)
        {
            value *= x;
        }
        for (std::uint32_t power = 0u; power < terms[index].yPower; ++power)
        {
            value *= y;
        }
        sum += value;
    }
    return sum;
}

double thinPlateRadial(double squaredDistance) noexcept
{
    if (!(squaredDistance > kThinPlateOriginEpsilon))
    {
        return 0.0;
    }
    return squaredDistance * std::log(squaredDistance) * 0.5;
}

SymmetricSolveResult solveSymmetric(std::span<double> matrix, std::span<double> rhs, std::uint32_t order) noexcept
{
    SymmetricSolveResult result{};
    if (order == 0u || matrix.size() < static_cast<std::size_t>(order) * order || rhs.size() < order)
    {
        return result;
    }

    const auto at = [&](std::uint32_t row, std::uint32_t column) -> double& {
        return matrix[static_cast<std::size_t>(row) * order + column];
    };

    double largestDiagonal = 0.0;
    for (std::uint32_t index = 0u; index < order; ++index)
    {
        largestDiagonal = (std::max)(largestDiagonal, at(index, index));
    }
    if (!(largestDiagonal > 0.0))
    {
        return result;
    }
    // Eight times the machine epsilon of the largest diagonal, scaled by the order: a pivot at or
    // below this is a direction the data does not constrain, and dividing by it would put the
    // rounding of the accumulation into the answer.
    const double tolerance =
        largestDiagonal * std::numeric_limits<double>::epsilon() * static_cast<double>(order) * 8.0;

    double smallestPivot = std::numeric_limits<double>::max();
    double largestPivot = 0.0;
    for (std::uint32_t column = 0u; column < order; ++column)
    {
        double pivot = at(column, column);
        for (std::uint32_t k = 0u; k < column; ++k)
        {
            pivot -= at(column, k) * at(column, k);
        }
        if (!(pivot > tolerance))
        {
            for (std::uint32_t k = 0u; k <= column; ++k)
            {
                at(column, k) = 0.0;
            }
            at(column, column) = 1.0;
            rhs[column] = 0.0;
            for (std::uint32_t row = column + 1u; row < order; ++row)
            {
                at(row, column) = 0.0;
            }
            ++result.droppedTerms;
            continue;
        }
        smallestPivot = (std::min)(smallestPivot, pivot);
        largestPivot = (std::max)(largestPivot, pivot);
        const double diagonal = std::sqrt(pivot);
        at(column, column) = diagonal;
        for (std::uint32_t row = column + 1u; row < order; ++row)
        {
            double sum = at(row, column);
            for (std::uint32_t k = 0u; k < column; ++k)
            {
                sum -= at(row, k) * at(column, k);
            }
            at(row, column) = sum / diagonal;
        }
    }

    if (result.droppedTerms >= order)
    {
        return result;
    }

    for (std::uint32_t row = 0u; row < order; ++row)
    {
        double sum = rhs[row];
        for (std::uint32_t k = 0u; k < row; ++k)
        {
            sum -= at(row, k) * rhs[k];
        }
        rhs[row] = sum / at(row, row);
    }
    for (std::uint32_t index = order; index > 0u; --index)
    {
        const std::uint32_t row = index - 1u;
        double sum = rhs[row];
        for (std::uint32_t k = row + 1u; k < order; ++k)
        {
            sum -= at(k, row) * rhs[k];
        }
        rhs[row] = sum / at(row, row);
    }

    result.solved = true;
    result.conditionEstimate = smallestPivot > 0.0 ? std::sqrt(largestPivot / smallestPivot) : 0.0;
    return result;
}

Result<bool> solveGeneral(const ExecutionContext& execution, std::span<double> matrix, std::span<double> rhs,
                          std::uint32_t order)
{
    if (order == 0u || matrix.size() < static_cast<std::size_t>(order) * order || rhs.size() < order)
    {
        return false;
    }

    const auto at = [&](std::uint32_t row, std::uint32_t column) -> double& {
        return matrix[static_cast<std::size_t>(row) * order + column];
    };

    double largest = 0.0;
    for (std::uint32_t row = 0u; row < order; ++row)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        for (std::uint32_t column = 0u; column < order; ++column)
        {
            largest = (std::max)(largest, std::abs(at(row, column)));
        }
    }
    if (!(largest > 0.0))
    {
        return false;
    }
    // The symmetric solve's tolerance, scaled by the largest entry rather than the largest diagonal,
    // because the thin-plate system this serves has a block of zeros on its diagonal.
    const double tolerance = largest * std::numeric_limits<double>::epsilon() * static_cast<double>(order) * 8.0;

    for (std::uint32_t column = 0u; column < order; ++column)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        std::uint32_t pivotRow = column;
        double pivotValue = std::abs(at(column, column));
        for (std::uint32_t row = column + 1u; row < order; ++row)
        {
            const double candidate = std::abs(at(row, column));
            if (candidate > pivotValue)
            {
                pivotValue = candidate;
                pivotRow = row;
            }
        }
        if (!(pivotValue > tolerance))
        {
            return false;
        }
        if (pivotRow != column)
        {
            for (std::uint32_t k = 0u; k < order; ++k)
            {
                std::swap(at(column, k), at(pivotRow, k));
            }
            std::swap(rhs[column], rhs[pivotRow]);
        }
        const double diagonal = at(column, column);
        for (std::uint32_t row = column + 1u; row < order; ++row)
        {
            const double factor = at(row, column) / diagonal;
            if (factor == 0.0)
            {
                continue;
            }
            at(row, column) = 0.0;
            for (std::uint32_t k = column + 1u; k < order; ++k)
            {
                at(row, k) -= factor * at(column, k);
            }
            rhs[row] -= factor * rhs[column];
        }
    }

    for (std::uint32_t index = order; index > 0u; --index)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        const std::uint32_t row = index - 1u;
        double sum = rhs[row];
        for (std::uint32_t k = row + 1u; k < order; ++k)
        {
            sum -= at(row, k) * rhs[k];
        }
        rhs[row] = sum / at(row, row);
    }
    return true;
}

double ditherFraction(std::uint64_t index) noexcept
{
    std::uint64_t mixed = index + 0x9e3779b97f4a7c15ull;
    mixed = (mixed ^ (mixed >> 30u)) * 0xbf58476d1ce4e5b9ull;
    mixed = (mixed ^ (mixed >> 27u)) * 0x94d049bb133111ebull;
    mixed ^= mixed >> 31u;
    // The top fifty-three bits are the ones a double represents exactly.
    return static_cast<double>(mixed >> 11u) * (1.0 / 9007199254740992.0) - 0.5;
}

double medianOf(std::span<double> values) noexcept
{
    if (values.empty())
    {
        return 0.0;
    }
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if ((values.size() % 2u) != 0u)
    {
        return upper;
    }
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle - 1u), values.end());
    return (values[middle - 1u] + upper) * 0.5;
}

double percentileOf(std::span<double> values, double percentile) noexcept
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double bounded = std::clamp(percentile, 0.0, 100.0);
    const double position = bounded / 100.0 * static_cast<double>(values.size() - 1u);
    const auto low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = low + 1u < values.size() ? low + 1u : low;
    const double weight = position - static_cast<double>(low);
    return values[low] * (1.0 - weight) + values[high] * weight;
}

Status lowpassPass(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                   PlaneView<float> scratch, std::uint32_t radius)
{
    if (radius == 0u)
    {
        return copyPlaneSamples(execution, source, destination);
    }
    const Status horizontal = horizontalPass(execution, source, scratch, radius);
    if (horizontal.failed())
    {
        return horizontal;
    }
    return verticalPass(execution, scratch.constView(), destination, radius);
}

Status lowpassPlane(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                    PlaneView<float> first, PlaneView<float> second, std::uint32_t radius, std::uint32_t passes)
{
    if (radius == 0u || passes == 0u)
    {
        return copyPlaneSamples(execution, source, destination);
    }
    for (std::uint32_t pass = 0u; pass < passes; ++pass)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        // The last pass has to land in the destination, so the parity of what is left decides which
        // of the two planes this one writes into.
        const bool intoDestination = ((passes - 1u - pass) % 2u) == 0u;
        const PlaneView<float> into = intoDestination ? destination : first;
        const PlaneView<const float> from =
            pass == 0u ? source : (intoDestination ? first.constView() : PlaneView<const float>{destination});
        const Status ran = lowpassPass(execution, from, into, second, radius);
        if (ran.failed())
        {
            return ran;
        }
    }
    return Status::success();
}

Status copyPlaneSamples(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination)
{
    const std::uint32_t width = source.width();
    return parallelFor(execution, source.height(), rowCost(width, 2u), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<const float> from = *source.row(y);
            const std::span<float> to = *destination.row(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                to[x] = from[x];
            }
        }
        return Status::success();
    });
}

double interpolateReduced(PlaneView<const float> plane, std::uint32_t factor, double x, double y) noexcept
{
    const std::uint32_t width = plane.width();
    const std::uint32_t height = plane.height();
    if (width == 0u || height == 0u)
    {
        return 0.0;
    }
    const double offset = static_cast<double>(factor - 1u) * 0.5;
    const double scale = 1.0 / static_cast<double>(factor);
    const double column = std::clamp((x - offset) * scale, 0.0, static_cast<double>(width - 1u));
    const double row = std::clamp((y - offset) * scale, 0.0, static_cast<double>(height - 1u));
    const auto lowColumn = static_cast<std::uint32_t>(column);
    const auto lowRow = static_cast<std::uint32_t>(row);
    const std::uint32_t highColumn = (std::min)(lowColumn + 1u, width - 1u);
    const std::uint32_t highRow = (std::min)(lowRow + 1u, height - 1u);
    const double columnWeight = column - static_cast<double>(lowColumn);
    const double rowWeight = row - static_cast<double>(lowRow);
    const std::span<const float> upper = *plane.row(lowRow);
    const std::span<const float> lower = *plane.row(highRow);
    const double top = static_cast<double>(upper[lowColumn]) * (1.0 - columnWeight) +
                       static_cast<double>(upper[highColumn]) * columnWeight;
    const double bottom = static_cast<double>(lower[lowColumn]) * (1.0 - columnWeight) +
                          static_cast<double>(lower[highColumn]) * columnWeight;
    return top * (1.0 - rowWeight) + bottom * rowWeight;
}

} // namespace an::background_detail
