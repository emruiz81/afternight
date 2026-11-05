// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the transcendental providers, applied to a plane and to a table.
//
// Two decisions shape this file.
//
// **A rejected sample interrupts a run rather than disabling one.** The obvious way to honor a
// validity rule in an element-wise kernel is to test every sample inside the loop, which puts a
// branch between each sample and its neighbour and costs the vectorization of the whole row for the
// benefit of the few samples that are excluded. Here a row is walked once to find its maximal runs
// of participating samples, and each run is handed to the compiled kernel whole. A row with no
// invalid sample is one run; a row that is half no-data is a handful of them; and the samples that
// were excluded keep the destination they already had, which is this family's contract for every
// operation.
//
// **Which provider ran is reported rather than assumed.** The dispatch table answers with the
// compiled run for the operation's instruction set, or with nothing - on a build with no vector
// kernels, on a host that cannot run them, and whenever a caller forces the reference backend. The
// executor evaluates through the platform math library in that case and says so, because a caller
// comparing two runs needs to know which of them was the approximation.

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>

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
#include <limits>
#include <span>

namespace an
{
namespace
{

constexpr const char* kPlaneOperation = "an::applyMathFunctionPlane";
constexpr const char* kTableOperation = "an::buildMathFunctionTable";

/// Roughly how much arithmetic one transcendental evaluation costs.
///
/// A polynomial of a handful of terms, a range reduction and the blended exceptional answers. The
/// grain policy needs an order of magnitude rather than a measurement, and this is the order of
/// magnitude that keeps a row of a small image from being handed to another thread.
constexpr std::uint64_t kOperationsPerSample = 32u;

/// One failure from this file.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status mathFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether the parameters name a function this engine has and an exponent it can use.
///
/// @param parameters the function and its exponent.
/// @param operation the entry point the caller is inside.
/// @return success, or why the parameters are not usable.
[[nodiscard]] Status checkParameters(const MathKernelParameters& parameters, const char* operation)
{
    if (!isMathFunction(parameters.function))
    {
        return mathFailure(ErrorCode::InvalidArgument, operation,
                           "the requested function is not one this engine evaluates");
    }
    if (!std::isfinite(parameters.exponent))
    {
        return mathFailure(ErrorCode::InvalidArgument, operation, "the exponent must be finite");
    }
    return Status::success();
}

/// Evaluates one contiguous run through whichever provider the operation resolved.
///
/// @param run the compiled run, or null for the scalar library.
/// @param parameters the function and its exponent.
/// @param source where the run starts.
/// @param destination where its results go, which may be the source.
/// @param count how many samples the run holds.
void evaluateRun(MathRunFn run, const MathKernelParameters& parameters, const float* source, float* destination,
                 std::uint64_t count) noexcept
{
    if (run != nullptr)
    {
        run(source, destination, count, static_cast<float>(parameters.exponent));
        return;
    }
    referenceMathRun(parameters.function, source, destination, count, parameters.exponent);
}

/// How many of one written run are not finite.
///
/// Counted after the run rather than inside it, so the compiled kernel stays a kernel and the count
/// is derived from what was actually written by whichever provider wrote it.
///
/// @param destination where the results start.
/// @param count how many results.
/// @return how many of them are infinite or not a number.
[[nodiscard]] std::uint64_t countNonFinite(const float* destination, std::uint64_t count) noexcept
{
    std::uint64_t found = 0u;
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        if (!std::isfinite(destination[index]))
        {
            ++found;
        }
    }
    return found;
}

/// What one row of the plane produced.
struct RowOutcome
{
    /// How many samples were written.
    std::uint64_t written = 0u;

    /// How many of them are not finite.
    std::uint64_t nonFinite = 0u;
};

/// Evaluates one row, one maximal run of participating samples at a time.
///
/// @param run the compiled run, or null for the scalar library.
/// @param parameters the function and its exponent.
/// @param accepts the validity rule.
/// @param input the source row.
/// @param output the destination row, which may be the source row.
/// @param y which row, for the mask lookup.
/// @param everySampleValid whether the rule accepts everything, which makes the row one run.
/// @return what the row produced.
[[nodiscard]] RowOutcome evaluateRow(MathRunFn run, const MathKernelParameters& parameters,
                                     const reference_detail::ValidityAccessor& accepts, std::span<const float> input,
                                     std::span<float> output, std::uint32_t y, bool everySampleValid) noexcept
{
    const std::uint32_t width = static_cast<std::uint32_t>(input.size());
    RowOutcome outcome;
    if (everySampleValid)
    {
        evaluateRun(run, parameters, input.data(), output.data(), width);
        outcome.written = width;
        outcome.nonFinite = countNonFinite(output.data(), width);
        return outcome;
    }

    std::uint32_t x = 0u;
    while (x < width)
    {
        if (!accepts.accepts(input[x], x, y))
        {
            ++x;
            continue;
        }
        std::uint32_t end = x + 1u;
        while (end < width && accepts.accepts(input[end], end, y))
        {
            ++end;
        }
        const std::uint64_t length = end - x;
        evaluateRun(run, parameters, input.data() + x, output.data() + x, length);
        outcome.written += length;
        outcome.nonFinite += countNonFinite(output.data() + x, length);
        x = end;
    }
    return outcome;
}

} // namespace

Result<MathExecutionResult> buildMathFunctionTable(const ExecutionContext& execution, const MathTableRequest& request,
                                                   std::span<float> entries)
{
    Status status = reference_detail::checkExecution(execution, kTableOperation);
    if (status.failed())
    {
        return status;
    }
    status = checkParameters(request.parameters, kTableOperation);
    if (status.failed())
    {
        return status;
    }
    if (!std::isfinite(request.first) || !std::isfinite(request.last))
    {
        return mathFailure(ErrorCode::InvalidArgument, kTableOperation, "a table domain end must be finite");
    }
    if (entries.empty() || entries.size() > kMaxMathTableEntries)
    {
        return mathFailure(ErrorCode::OutOfRange, kTableOperation,
                           "a table holds between one entry and one per sixteen-bit code value");
    }

    const std::uint64_t count = static_cast<std::uint64_t>(entries.size());
    const double step = count == 1u ? 0.0 : (request.last - request.first) / static_cast<double>(count - 1u);
    const MathKernelParameters parameters = request.parameters;
    float* const table = entries.data();
    std::uint64_t nonFinite = 0u;
    const TaskCost cost{.bytesPerItem = sizeof(float), .operationsPerItem = kOperationsPerSample};

    status = parallelFor(execution, count, cost, [&](TaskRange range) -> Status {
        const Status checkpoint = reference_detail::checkpoint(execution);
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            // The last entry of a table with more than one is written from the domain end itself
            // rather than from the accumulated step, so a table covers exactly the domain it was
            // asked for. A table of one entry has no span to cover and sits on the start of it.
            const double point =
                index + 1u == count && count > 1u ? request.last : request.first + static_cast<double>(index) * step;
            table[index] = referenceMathValue(parameters.function, static_cast<float>(point), parameters.exponent);
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    nonFinite = countNonFinite(table, count);

    MathExecutionResult result;
    result.run.selection = ValidityCount{.valid = count, .total = count};
    result.run.outputsWritten = count;
    result.provider = MathProvider::ExactTable;
    result.nonFiniteOutputs = nonFinite;
    result.plannedWorkers = plannedWorkerCount(execution, count, cost);
    return result;
}

Result<MathExecutionResult> applyMathFunctionPlane(const ExecutionContext& execution, PlaneView<const float> source,
                                                   PlaneView<float> destination, const MathKernelParameters& parameters,
                                                   const KernelValidityView& validity)
{
    Status status = reference_detail::checkExecution(execution, kPlaneOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkPair(source, destination, kPlaneOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkValidity(validity, source.width(), source.height(), kPlaneOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkOverlap(source, destination, true, kPlaneOperation);
    if (status.failed())
    {
        return status;
    }
    status = checkParameters(parameters, kPlaneOperation);
    if (status.failed())
    {
        return status;
    }

    const std::uint32_t width = source.width();
    const std::uint32_t height = source.height();
    MathRunFn run = mathRunFor(execution, parameters.function);
    if (run != nullptr && parameters.function == MathFunction::Pow)
    {
        // The compiled exponent is Float32. If narrowing changes its exceptional-value
        // classification, keep the original exponent in the scalar provider instead.
        const double exponent = parameters.exponent;
        if (std::abs(exponent) > static_cast<double>((std::numeric_limits<float>::max)()))
        {
            run = nullptr;
        }
        else
        {
            const float narrowed = static_cast<float>(exponent);
            const bool odd = std::fmod(std::abs(exponent), 2.0) == 1.0;
            const bool narrowedOdd = std::fmod(std::abs(static_cast<double>(narrowed)), 2.0) == 1.0;
            if (odd != narrowedOdd || (exponent != 0.0 && narrowed == 0.0f))
            {
                run = nullptr;
            }
        }
    }
    const MathProvider provider = run != nullptr ? MathProvider::VectorApproximation : MathProvider::ScalarLibrary;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * 2u * sizeof(float),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * kOperationsPerSample};

    MathExecutionResult result;
    result.run.selection = ValidityCount{.valid = 0u, .total = static_cast<std::uint64_t>(width) * height};
    result.provider = provider;
    result.plannedWorkers = plannedWorkerCount(execution, height, cost);
    if (width == 0u || height == 0u)
    {
        return result;
    }

    // One pair of counters per row, summed in row order afterwards, so the reported counts do not
    // depend on which thread finished first.
    Result<plane_detail::ScopedContextMemory> counters = plane_detail::ScopedContextMemory::take(
        execution, 2u * static_cast<std::uint64_t>(height) * sizeof(std::uint64_t), alignof(std::uint64_t),
        kPlaneOperation);
    if (!counters.hasValue())
    {
        return counters.status();
    }
    std::uint64_t* const writtenPerRow = static_cast<std::uint64_t*>(counters->data());
    std::uint64_t* const nonFinitePerRow = writtenPerRow + height;
    std::fill_n(writtenPerRow, static_cast<std::size_t>(2u) * height, std::uint64_t{0});

    const reference_detail::ValidityAccessor accepts(validity);
    const bool everySampleValid = validity.descriptor.isAllValid();
    status = parallelFor(execution, height, cost, [&](TaskRange rows) -> Status {
        for (std::uint64_t index = rows.begin; index < rows.end; ++index)
        {
            // Once per row rather than once per range: a row of a large image is enough work that a
            // caller who stopped the operation should not wait for the whole range to finish.
            const Status checkpoint = reference_detail::checkpoint(execution);
            if (checkpoint.failed())
            {
                return checkpoint;
            }
            const std::uint32_t y = static_cast<std::uint32_t>(index);
            const RowOutcome outcome =
                evaluateRow(run, parameters, accepts, *source.row(y), *destination.row(y), y, everySampleValid);
            writtenPerRow[y] = outcome.written;
            nonFinitePerRow[y] = outcome.nonFinite;
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    for (std::uint32_t y = 0u; y < height; ++y)
    {
        result.run.selection.valid += writtenPerRow[y];
        result.nonFiniteOutputs += nonFinitePerRow[y];
    }
    result.run.outputsWritten = result.run.selection.valid;
    return result;
}

} // namespace an
