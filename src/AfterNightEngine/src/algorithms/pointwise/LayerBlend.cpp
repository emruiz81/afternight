// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - five layer blend modes over Float32 coverage planes.
//
// One walk over the rows. Each sample of the base and the source is read as a coverage, the source's decides
// whether the pixel is blended or keeps its base, and the blend is the Float32 expression the header states,
// in the order it states. Nothing is allocated: each range of rows reads and writes in place.

#include <AfterNightEngine/Algorithms/Pointwise/LayerBlend.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planLayerBlend";
constexpr const char* kBlendOperation = "an::blendLayerPlane";

[[nodiscard]] Status blendFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// A sample read as a coverage: below zero or not a number is zero, above one is one.
[[nodiscard]] inline float coverageOf(float value) noexcept
{
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
}

/// The blend of a base and a source already read as coverages, at an opacity within the unit interval.
[[nodiscard]] inline float blendCoverages(LayerBlendMode mode, float base, float source, float opacity) noexcept
{
    if (mode == LayerBlendMode::Erase)
    {
        return coverageOf(base * (1.0f - (source * opacity)));
    }
    float target = source;
    switch (mode)
    {
    case LayerBlendMode::Multiply:
        target = base * source;
        break;
    case LayerBlendMode::Screen:
        target = 1.0f - ((1.0f - base) * (1.0f - source));
        break;
    case LayerBlendMode::Overlay:
        target = base < 0.5f ? (2.0f * base * source) : (1.0f - (2.0f * (1.0f - base) * (1.0f - source)));
        break;
    case LayerBlendMode::Normal:
    case LayerBlendMode::Erase:
        break;
    }
    return coverageOf(base + ((target - base) * opacity));
}

/// Whether two planes are the same samples: one first sample and one row stride.
[[nodiscard]] bool samePlane(const PlaneView<const float>& left, const PlaneView<float>& right) noexcept
{
    return left.data() == right.data() && left.rowStrideBytes() == right.rowStrideBytes();
}

/// The bytes a plane's samples span, from its first sample to the end of its last row.
[[nodiscard]] std::uint64_t spannedBytes(std::uint64_t rowStrideBytes, std::uint32_t width,
                                         std::uint32_t height) noexcept
{
    return rowStrideBytes * (static_cast<std::uint64_t>(height) - 1u) + static_cast<std::uint64_t>(width) * 4u;
}

} // namespace

Result<LayerBlendPlan> planLayerBlend(const LayerBlendRequest& request)
{
    if (!isLayerBlendMode(request.mode))
    {
        return blendFailure(ErrorCode::InvalidArgument, "the layer blend mode is outside its enumeration",
                            kPlanOperation, "mode");
    }
    if (!(request.opacity >= 0.0f && request.opacity <= 1.0f))
    {
        return blendFailure(ErrorCode::InvalidArgument, "a layer blend's opacity is a number from zero to one",
                            kPlanOperation, "opacity");
    }
    return LayerBlendPlan(request);
}

float evaluateLayerBlend(const LayerBlendPlan& plan, float base, float source) noexcept
{
    const LayerBlendRequest& request = plan.request();
    return blendCoverages(request.mode, coverageOf(base), coverageOf(source), request.opacity);
}

Result<LayerBlendResult> blendLayerPlane(const ExecutionContext& execution, const LayerBlendPlan& plan,
                                         PlaneView<const float> base, PlaneView<const float> source,
                                         PlaneView<float> destination)
{
    if (!execution.valid())
    {
        return blendFailure(ErrorCode::NotInitialized, "a layer blend was given an empty execution context",
                            kBlendOperation, "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    if (!backend.hasValue())
    {
        return backend.status();
    }
    if (base.samplesPerPixel() != 1u || source.samplesPerPixel() != 1u || destination.samplesPerPixel() != 1u)
    {
        return blendFailure(ErrorCode::InvalidArgument, "a coverage plane holds one sample per pixel", kBlendOperation,
                            "destination");
    }
    const std::uint32_t width = destination.width();
    const std::uint32_t height = destination.height();
    if (base.width() != width || base.height() != height || source.width() != width || source.height() != height)
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "a layer blend's three planes have one width and height",
                            kBlendOperation, "source");
    }
    const std::uint64_t destinationBytes = spannedBytes(destination.rowStrideBytes(), width, height);
    if (regionsOverlap(source.data(), spannedBytes(source.rowStrideBytes(), width, height), destination.data(),
                       destinationBytes))
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "the layer blend's destination overlaps its source",
                            kBlendOperation, "destination");
    }
    const bool inPlace = samePlane(base, destination);
    if (!inPlace && regionsOverlap(base.data(), spannedBytes(base.rowStrideBytes(), width, height), destination.data(),
                                   destinationBytes))
    {
        return blendFailure(ErrorCode::IncompatibleArguments,
                            "the layer blend's destination overlaps its base without being it", kBlendOperation,
                            "destination");
    }

    const LayerBlendRequest& request = plan.request();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * 12u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 8u};
    std::atomic<std::uint64_t> blended{0u};
    const Status ran = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t covered = 0u;
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            const std::span<const float> baseRow = *base.row(y);
            const std::span<const float> sourceRow = *source.row(y);
            const std::span<float> destinationRow = *destination.row(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const float coverage = coverageOf(sourceRow[x]);
                if (coverage > 0.0f)
                {
                    destinationRow[x] = blendCoverages(request.mode, coverageOf(baseRow[x]), coverage, request.opacity);
                    ++covered;
                }
                else if (!inPlace)
                {
                    destinationRow[x] = baseRow[x];
                }
            }
        }
        blended.fetch_add(covered, std::memory_order_relaxed);
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    LayerBlendResult result;
    result.blended = blended.load(std::memory_order_relaxed);
    result.kept = static_cast<std::uint64_t>(width) * height - result.blended;
    return result;
}

} // namespace an
