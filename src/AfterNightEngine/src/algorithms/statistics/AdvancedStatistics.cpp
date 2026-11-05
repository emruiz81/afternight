// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - deviation, square, biweight and background-noise image statistics.
//
// The centres come from the public basic and robust statistics, and the basic statistics are returned
// with the result. When the request asks for the iterative k-sigma estimators, their population is
// ranked again through the order-statistic substrate with a value window of six median absolute
// deviations around the median, clamped to the extremes. The remaining row work uses a fixed
// partition and ordered merge, so its parallel accumulations agree at every thread count.
//
// That pass accumulates, per selected sample, the absolute deviation from the mean, the square, and
// the two sums of Tukey's biweight midvariance,
//
//     n * sum(d^2 (1 - u^2)^4) / (sum((1 - u^2)(1 - 5 u^2)))^2
//
// with `d` the distance from the median and `u = d / (9 * MAD)`, over samples with `|u| < 1`. It
// also collects each row's first differences between consecutive selected samples, so an excluded
// sample joins its neighbours rather than breaking the row; a row's population deviation is clipped
// at five sigma, strictly, up to three times, and the background noise is the median of those row
// deviations divided by the square root of two. The row scratch comes from the engine context.

#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/statistics/StatisticsSelectionInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::computeImageAdvancedStatistics";
constexpr std::uint32_t kNoiseClippingIterations = 3u;
constexpr double kNoiseClippingSigma = 5.0;
constexpr double kIterativeKsigmaClipMad = 6.0;
constexpr double kIterativeKsigmaBiweightTuning = 9.0;
constexpr double kIterativeKsigmaScaleCorrection = 0.991;

[[nodiscard]] Status advancedFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

struct MeanAndDeviation
{
    double mean = 0.0;
    double deviation = 0.0;
};

[[nodiscard]] MeanAndDeviation populationDeviation(const double* values, std::size_t count) noexcept
{
    double sum = 0.0;
    double squares = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        sum += values[index];
        squares += values[index] * values[index];
    }
    const double mean = count == 0u ? 0.0 : sum / static_cast<double>(count);
    const double variance = count == 0u ? 0.0 : (std::max)(0.0, squares / static_cast<double>(count) - mean * mean);
    return MeanAndDeviation{.mean = mean, .deviation = std::sqrt(variance)};
}

/// Independent row partitions, fixed across execution widths; their row scratch stays bounded.
constexpr std::uint32_t kAdvancedChunks = 16u;

/// One partition's sums, published once after its rows have finished.
struct AdvancedPartial
{
    double absoluteDeviationSum = 0.0;
    double sumOfSquares = 0.0;
    double biweightNumerator = 0.0;
    double biweightDenominator = 0.0;
    double iterativeKsigmaNumerator = 0.0;
    double iterativeKsigmaDenominator = 0.0;
};

template <ResidentSample SampleT>
[[nodiscard]] Result<AdvancedStatistics>
computeTyped(const ExecutionContext& execution, const statistics_detail::ResolvedSelection& resolved,
             const BasicStatistics& basic, const RobustStatistics& robust, const PlaneOrderStatistics* iterativeKsigma,
             double iterativeKsigmaLow, double iterativeKsigmaHigh)
{
    const std::uint32_t width = resolved.region.width;
    const std::uint32_t height = resolved.region.height;
    const std::uint32_t chunkCount = (std::min)(height, kAdvancedChunks);
    const std::uint64_t differenceElements = static_cast<std::uint64_t>(width) * chunkCount;
    const std::uint64_t scratchElements = differenceElements + height;
    if (scratchElements > (std::numeric_limits<std::uint64_t>::max)() / sizeof(double))
    {
        return advancedFailure(ErrorCode::OutOfRange, "the advanced-statistics scratch size overflows");
    }
    const std::uint64_t scratchBytes = scratchElements * sizeof(double);
    Result<plane_detail::ScopedContextMemory> scratch =
        plane_detail::ScopedContextMemory::take(execution, scratchBytes, alignof(double), kOperation);
    if (!scratch.hasValue())
    {
        return scratch.status();
    }
    double* const differenceBuffers = static_cast<double*>(scratch->data());
    double* const rowDeviations = differenceBuffers + differenceElements;
    std::array<AdvancedPartial, kAdvancedChunks> partials{};

    const plane_detail::PlaneSampleReader<SampleT> reader{resolved.source, resolved.validity};
    std::uint32_t usableRows = 0u;
    const bool hasBiweightScale = robust.medianAbsoluteDeviation > 0.0;
    const double biweightFactor =
        hasBiweightScale ? 1.0 / (kIterativeKsigmaBiweightTuning * robust.medianAbsoluteDeviation) : 0.0;
    const bool hasIterativeKsigmaScale = iterativeKsigma != nullptr && iterativeKsigma->medianAbsoluteDeviation > 0.0;
    const double iterativeKsigmaFactor =
        hasIterativeKsigmaScale ? 1.0 / (kIterativeKsigmaBiweightTuning * iterativeKsigma->medianAbsoluteDeviation)
                                : 0.0;

    const TaskCost chunkCost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) *
                                             (height / chunkCount + (height % chunkCount != 0u ? 1u : 0u)),
                             .operationsPerItem = static_cast<std::uint64_t>(width) * 80u *
                                                  (height / chunkCount + (height % chunkCount != 0u ? 1u : 0u))};
    const Status computed = parallelFor(execution, chunkCount, chunkCost, [&](TaskRange chunks) -> Status {
        for (std::uint64_t chunk = chunks.begin; chunk < chunks.end; ++chunk)
        {
            double absoluteDeviationSum = 0.0;
            double sumOfSquares = 0.0;
            double biweightNumerator = 0.0;
            double biweightDenominator = 0.0;
            double iterativeKsigmaNumerator = 0.0;
            double iterativeKsigmaDenominator = 0.0;
            double* const differences = differenceBuffers + chunk * width;
            const auto firstRow = static_cast<std::uint32_t>(static_cast<std::uint64_t>(height) * chunk / chunkCount);
            const auto endRow =
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(height) * (chunk + 1u) / chunkCount);
            for (std::uint32_t relativeY = firstRow; relativeY < endRow; ++relativeY)
            {
                const Status cancelled = execution.checkCancellation();
                if (cancelled.failed())
                {
                    return cancelled;
                }
                const std::uint32_t y = resolved.region.originY + relativeY;
                const SampleT* const row = reader.rowStart(y);
                bool hasPrevious = false;
                double previous = 0.0;
                std::size_t differenceCount = 0u;
                for (std::uint32_t relativeX = 0u; relativeX < width; ++relativeX)
                {
                    const std::uint32_t x = resolved.region.originX + relativeX;
                    double value = 0.0;
                    if (!reader.selected(row[static_cast<std::size_t>(x) * reader.columnStride()], x, y, value))
                    {
                        continue;
                    }
                    absoluteDeviationSum += std::abs(value - basic.mean);
                    const double square = value * value;
                    if (!std::isfinite(square) || !std::isfinite(sumOfSquares + square))
                    {
                        return advancedFailure(ErrorCode::InvalidData,
                                               "the sum of squared selected samples cannot be represented in Float64");
                    }
                    sumOfSquares += square;

                    if (hasBiweightScale)
                    {
                        const double distance = value - robust.median;
                        const double normalized = distance * biweightFactor;
                        const double normalizedSquared = std::abs(normalized) < 1.0 ? normalized * normalized : 1.0;
                        const double retained = 1.0 - normalizedSquared;
                        const double weightedDistance = distance * retained * retained;
                        biweightNumerator += weightedDistance * weightedDistance;
                        biweightDenominator += retained * (1.0 - 5.0 * normalizedSquared);
                    }

                    if (hasIterativeKsigmaScale && value >= iterativeKsigmaLow && value <= iterativeKsigmaHigh)
                    {
                        const double distance = value - iterativeKsigma->median;
                        const double normalized = distance * iterativeKsigmaFactor;
                        const double normalizedSquared = std::abs(normalized) < 1.0 ? normalized * normalized : 1.0;
                        const double retained = 1.0 - normalizedSquared;
                        const double weightedDistance = distance * retained * retained;
                        iterativeKsigmaNumerator += weightedDistance * weightedDistance;
                        iterativeKsigmaDenominator += retained * (1.0 - 5.0 * normalizedSquared);
                    }

                    if (hasPrevious)
                    {
                        differences[differenceCount++] = previous - value;
                    }
                    previous = value;
                    hasPrevious = true;
                }

                if (differenceCount < 2u)
                {
                    rowDeviations[relativeY] = -1.0;
                    continue;
                }
                MeanAndDeviation rowStatistics = populationDeviation(differences, differenceCount);
                if (rowStatistics.deviation > 0.0)
                {
                    for (std::uint32_t iteration = 0u; iteration < kNoiseClippingIterations; ++iteration)
                    {
                        std::size_t retainedCount = 0u;
                        for (std::size_t index = 0u; index < differenceCount; ++index)
                        {
                            if (std::abs(differences[index] - rowStatistics.mean) <
                                kNoiseClippingSigma * rowStatistics.deviation)
                            {
                                differences[retainedCount++] = differences[index];
                            }
                        }
                        if (retainedCount == differenceCount || retainedCount == 0u)
                        {
                            break;
                        }
                        differenceCount = retainedCount;
                        rowStatistics = populationDeviation(differences, differenceCount);
                        if (rowStatistics.deviation == 0.0)
                        {
                            break;
                        }
                    }
                }
                rowDeviations[relativeY] = rowStatistics.deviation;
            }
            partials[chunk] = AdvancedPartial{absoluteDeviationSum,     sumOfSquares,
                                              biweightNumerator,        biweightDenominator,
                                              iterativeKsigmaNumerator, iterativeKsigmaDenominator};
        }
        return Status::success();
    });
    if (computed.failed())
        return computed;
    double absoluteDeviationSum = 0.0;
    double sumOfSquares = 0.0;
    double biweightNumerator = 0.0;
    double biweightDenominator = 0.0;
    double iterativeKsigmaNumerator = 0.0;
    double iterativeKsigmaDenominator = 0.0;
    for (std::uint32_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        absoluteDeviationSum += partials[chunk].absoluteDeviationSum;
        sumOfSquares += partials[chunk].sumOfSquares;
        biweightNumerator += partials[chunk].biweightNumerator;
        biweightDenominator += partials[chunk].biweightDenominator;
        iterativeKsigmaNumerator += partials[chunk].iterativeKsigmaNumerator;
        iterativeKsigmaDenominator += partials[chunk].iterativeKsigmaDenominator;
    }
    if (!std::isfinite(sumOfSquares))
        return advancedFailure(ErrorCode::InvalidData,
                               "the sum of squared selected samples cannot be represented in Float64");
    for (std::uint32_t row = 0u; row < height; ++row)
        if (rowDeviations[row] >= 0.0)
            rowDeviations[usableRows++] = rowDeviations[row];

    double backgroundNoise = 0.0;
    if (usableRows != 0u)
    {
        std::sort(rowDeviations, rowDeviations + usableRows);
        const std::uint32_t lower = (usableRows - 1u) / 2u;
        const std::uint32_t upper = usableRows / 2u;
        backgroundNoise = (rowDeviations[lower] + rowDeviations[upper]) * 0.5 * std::sqrt(0.5);
    }

    AdvancedStatistics result;
    result.basic = basic;
    result.selection = basic.selection;
    result.mean = basic.mean;
    result.median = robust.median;
    result.medianAbsoluteDeviation = robust.medianAbsoluteDeviation;
    result.averageAbsoluteDeviation = absoluteDeviationSum / static_cast<double>(basic.selection.valid);
    result.sumOfSquares = sumOfSquares;
    result.meanOfSquares = sumOfSquares / static_cast<double>(basic.selection.valid);
    result.biweightMidvariance = hasBiweightScale && biweightDenominator != 0.0
                                     ? static_cast<double>(basic.selection.valid) * biweightNumerator /
                                           (biweightDenominator * biweightDenominator)
                                     : 0.0;
    result.backgroundNoise = backgroundNoise;
    if (iterativeKsigma != nullptr)
    {
        result.iterativeKsigmaSelection =
            ValidityCount{.valid = iterativeKsigma->run.selection.valid, .total = basic.selection.valid};
        if (hasIterativeKsigmaScale && iterativeKsigmaDenominator != 0.0)
        {
            const double variance = static_cast<double>(iterativeKsigma->run.selection.valid) *
                                    iterativeKsigmaNumerator /
                                    (iterativeKsigmaDenominator * iterativeKsigmaDenominator);
            result.iterativeKsigmaLocation = iterativeKsigma->median;
            result.iterativeKsigmaScale = std::sqrt((std::max)(0.0, variance)) * kIterativeKsigmaScaleCorrection;
        }
        else
        {
            result.iterativeKsigmaLocation = robust.median;
        }
    }
    result.planeScans = 3u + robust.planeScans + (iterativeKsigma == nullptr ? 0u : iterativeKsigma->planeScans);
    result.peakScratchBytes = (std::max)({scratchBytes, robust.peakScratchBytes,
                                          iterativeKsigma == nullptr ? 0u : iterativeKsigma->peakScratchBytes});
    return result;
}

} // namespace

Result<AdvancedStatistics> computeImageAdvancedStatistics(const ExecutionContext& execution, SampleType sampleType,
                                                          const void* samples, const ImageLayout& layout,
                                                          std::uint32_t channelIndex,
                                                          const StatisticsSelection& selection,
                                                          const AdvancedStatisticsRequest& request)
{
    if (!execution.valid())
    {
        return advancedFailure(ErrorCode::NotInitialized, "advanced statistics were given an empty execution context");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<statistics_detail::ResolvedSelection> resolved =
        statistics_detail::resolveStatisticsSelection(sampleType, samples, layout, channelIndex, selection, kOperation);
    if (!resolved.hasValue())
    {
        return resolved.status();
    }
    if (resolved->region.width == 0u || resolved->region.height == 0u)
    {
        return advancedFailure(ErrorCode::EmptySelection, "advanced statistics were asked of an empty selection");
    }

    const Result<BasicStatistics> basic =
        computeImageBasicStatistics(execution, sampleType, samples, layout, channelIndex, selection);
    if (!basic.hasValue())
    {
        return basic.status();
    }
    RobustStatisticsRequest robustRequest;
    robustRequest.medianAbsoluteDeviation = true;
    const Result<RobustStatistics> robust =
        computeImageRobustStatistics(execution, sampleType, samples, layout, channelIndex, selection, robustRequest);
    if (!robust.hasValue())
    {
        return robust.status();
    }

    std::optional<PlaneOrderStatistics> iterativeKsigma;
    double iterativeKsigmaLow = 0.0;
    double iterativeKsigmaHigh = 0.0;
    if (request.iterativeKsigma && robust->medianAbsoluteDeviation > 0.0)
    {
        iterativeKsigmaLow =
            (std::max)(robust->minimum, robust->median - kIterativeKsigmaClipMad * robust->medianAbsoluteDeviation);
        iterativeKsigmaHigh =
            (std::min)(robust->maximum, robust->median + kIterativeKsigmaClipMad * robust->medianAbsoluteDeviation);
        KernelPlaneSelection clippedRegion = resolved->region;
        clippedRegion.valueWindow = KernelValueWindow{.lowest = iterativeKsigmaLow, .highest = iterativeKsigmaHigh};
        Result<PlaneOrderStatistics> clipped = selectPlaneOrderStatisticsOver(
            execution, resolved->source, std::span<const double>{}, std::span<double>{},
            PlaneOrderRequest{.interpolation = QuantileInterpolation::Linear, .medianAbsoluteDeviation = true},
            resolved->validity, clippedRegion);
        if (!clipped.hasValue())
        {
            return clipped.status();
        }
        iterativeKsigma.emplace(std::move(*clipped));
    }

    switch (sampleType)
    {
    case SampleType::UInt8:
        return computeTyped<std::uint8_t>(execution, *resolved, *basic, *robust,
                                          iterativeKsigma ? &*iterativeKsigma : nullptr, iterativeKsigmaLow,
                                          iterativeKsigmaHigh);
    case SampleType::Int8:
        return computeTyped<std::int8_t>(execution, *resolved, *basic, *robust,
                                         iterativeKsigma ? &*iterativeKsigma : nullptr, iterativeKsigmaLow,
                                         iterativeKsigmaHigh);
    case SampleType::UInt16:
        return computeTyped<std::uint16_t>(execution, *resolved, *basic, *robust,
                                           iterativeKsigma ? &*iterativeKsigma : nullptr, iterativeKsigmaLow,
                                           iterativeKsigmaHigh);
    case SampleType::Int16:
        return computeTyped<std::int16_t>(execution, *resolved, *basic, *robust,
                                          iterativeKsigma ? &*iterativeKsigma : nullptr, iterativeKsigmaLow,
                                          iterativeKsigmaHigh);
    case SampleType::UInt32:
        return computeTyped<std::uint32_t>(execution, *resolved, *basic, *robust,
                                           iterativeKsigma ? &*iterativeKsigma : nullptr, iterativeKsigmaLow,
                                           iterativeKsigmaHigh);
    case SampleType::Int32:
        return computeTyped<std::int32_t>(execution, *resolved, *basic, *robust,
                                          iterativeKsigma ? &*iterativeKsigma : nullptr, iterativeKsigmaLow,
                                          iterativeKsigmaHigh);
    case SampleType::Float32:
        return computeTyped<float>(execution, *resolved, *basic, *robust, iterativeKsigma ? &*iterativeKsigma : nullptr,
                                   iterativeKsigmaLow, iterativeKsigmaHigh);
    case SampleType::Float64:
        return computeTyped<double>(execution, *resolved, *basic, *robust,
                                    iterativeKsigma ? &*iterativeKsigma : nullptr, iterativeKsigmaLow,
                                    iterativeKsigmaHigh);
    }
    return advancedFailure(ErrorCode::InvalidArgument, "advanced statistics name no resident sample type");
}

} // namespace an
