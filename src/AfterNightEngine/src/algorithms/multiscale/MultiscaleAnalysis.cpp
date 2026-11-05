// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - multiscale layer noise analysis and coefficient shrinkage.

#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/SpatialPlane.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{
namespace
{

constexpr const char* kNoiseOperation = "an::estimateMultiscaleLayerNoise";
constexpr const char* kShrinkOperation = "an::shrinkMultiscaleLayer";

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

/// One failure from this file.
///
/// The neighbourhood family already spells a failure of this domain one way, and a transform that
/// spelled it a second way would give a caller two shapes to branch on for the same kind of refusal.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status multiscaleFailure(ErrorCode code, const char* operation, const char* message)
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
[[nodiscard]] Status multiscaleLimitFailure(ErrorCode code, const char* operation, const char* message,
                                            std::uint64_t requested, std::uint64_t limit)
{
    return spatial_detail::spatialLimitFailure(code, operation, message, requested, limit);
}

} // namespace

Result<MultiscaleLayerNoise> estimateMultiscaleLayerNoise(const ExecutionContext& execution,
                                                          const MultiscaleDecomposition& decomposition,
                                                          std::uint32_t channel, std::uint32_t layer)
{
    if (!execution.valid())
    {
        return multiscaleFailure(ErrorCode::NotInitialized, kNoiseOperation,
                                 "a noise estimate needs an execution holding an engine-context lease");
    }
    if (!decomposition.valid())
    {
        return multiscaleFailure(ErrorCode::NotInitialized, kNoiseOperation,
                                 "a noise estimate needs a decomposition holding layers");
    }
    if (channel >= decomposition.channelCount() || layer >= decomposition.layerCount())
    {
        return multiscaleLimitFailure(ErrorCode::OutOfRange, kNoiseOperation,
                                      "the decomposition has no such channel and layer",
                                      static_cast<std::uint64_t>(layer), decomposition.layerCount());
    }
    const std::optional<ImageLayout> layout = decomposition.layout();
    if (!layout.has_value())
    {
        return multiscaleFailure(ErrorCode::Internal, kNoiseOperation, "the layer memory describes no image");
    }

    const KernelPlaneSource source{.sampleType = SampleType::Float32,
                                   .samples = decomposition.samples(),
                                   .layout = &*layout,
                                   .plane = channel * decomposition.layerCount() + layer,
                                   .sampleInPixel = 0u};
    const std::array<double, 1u> quantiles{0.5};
    std::array<double, 1u> values{};
    const PlaneOrderRequest request{.interpolation = QuantileInterpolation::Linear, .medianAbsoluteDeviation = true};
    const Result<PlaneOrderStatistics> statistics =
        selectPlaneOrderStatistics(execution, source, quantiles, values, request, KernelValidityView{});
    if (!statistics.hasValue())
    {
        return statistics.status();
    }

    MultiscaleLayerNoise noise{};
    noise.median = statistics->median;
    noise.medianAbsoluteDeviation = statistics->medianAbsoluteDeviation;
    noise.sigma = statistics->medianAbsoluteDeviation * kMedianAbsoluteDeviationToSigma;
    noise.sampleCount = statistics->run.selection.valid;
    noise.planeScans = statistics->planeScans;
    return noise;
}

Result<MultiscaleShrinkResult> shrinkMultiscaleLayer(const ExecutionContext& execution,
                                                     MultiscaleDecomposition& decomposition, std::uint32_t channel,
                                                     std::uint32_t layer, const MultiscaleShrinkRequest& request)
{
    if (!execution.valid())
    {
        return multiscaleFailure(ErrorCode::NotInitialized, kShrinkOperation,
                                 "a shrinkage needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!decomposition.valid())
    {
        return multiscaleFailure(ErrorCode::NotInitialized, kShrinkOperation,
                                 "a shrinkage needs a decomposition holding layers");
    }
    if (!isMultiscaleShrinkage(request.shrinkage))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kShrinkOperation,
                                 "the shrinkage rule is not one this engine implements");
    }
    if (!std::isfinite(request.threshold) || request.threshold < 0.0)
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kShrinkOperation,
                                 "a coefficient threshold must be finite and not negative");
    }
    if (!std::isfinite(request.amount) || request.amount < 0.0 || request.amount > 1.0)
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kShrinkOperation,
                                 "the amount of a shrinkage lies within the unit interval");
    }
    const std::optional<PlaneView<float>> plane = decomposition.layer(channel, layer);
    if (!plane.has_value())
    {
        return multiscaleLimitFailure(ErrorCode::OutOfRange, kShrinkOperation,
                                      "the decomposition has no such channel and layer",
                                      static_cast<std::uint64_t>(layer), decomposition.layerCount());
    }

    const std::uint32_t width = plane->width();
    const double threshold = request.threshold;
    const double amount = request.amount;
    const bool soft = request.shrinkage == MultiscaleShrinkage::Soft;
    std::atomic<std::uint64_t> changed{0u};
    const Status walked = parallelFor(execution, plane->height(), rowCost(width, 1u), [&](TaskRange range) {
        std::uint64_t local = 0u;
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::span<float> row = *plane->row(static_cast<std::uint32_t>(y));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double value = static_cast<double>(row[x]);
                double target = value;
                if (std::abs(value) < threshold)
                {
                    target = 0.0;
                }
                else if (soft)
                {
                    target = value > 0.0 ? value - threshold : value + threshold;
                }
                const double blended = value + amount * (target - value);
                const float stored = static_cast<float>(blended);
                if (stored != row[x])
                {
                    row[x] = stored;
                    ++local;
                }
            }
        }
        changed.fetch_add(local, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }

    MultiscaleShrinkResult result{};
    result.samplesConsidered = static_cast<std::uint64_t>(width) * plane->height();
    result.samplesChanged = changed.load(std::memory_order_relaxed);
    result.plannedWorkers = execution.admittedConcurrency();
    return result;
}
} // namespace an
