// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - deterministic histogram remapping through a curve.
//
// Two serial passes over the source bins. The first evaluates every occupied bin's centre through
// the curve and records where its count will go, in a placement table taken from the engine
// context, so that every refusal - a curve failure, a rejected value, a cancellation - happens
// before the caller's destination is touched. The second clears the destination and adds the
// counts; it checks nothing and cannot fail, which is what lets it run to completion once begun.

#include <AfterNightEngine/Algorithms/Statistics/HistogramTransform.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::transformImageHistogram";

[[nodiscard]] Status transformFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool spansOverlap(std::span<const std::uint64_t> source,
                                std::span<const std::uint64_t> destination) noexcept
{
    if (source.empty() || destination.empty())
    {
        return false;
    }
    const std::uintptr_t sourceBegin = reinterpret_cast<std::uintptr_t>(source.data());
    const std::uintptr_t destinationBegin = reinterpret_cast<std::uintptr_t>(destination.data());
    const std::uintptr_t sourceEnd = sourceBegin + source.size_bytes();
    const std::uintptr_t destinationEnd = destinationBegin + destination.size_bytes();
    return sourceBegin < destinationEnd && destinationBegin < sourceEnd;
}

/// Where one source bin's count goes in the destination.
struct Placement
{
    /// The destination bin that receives the count, or its lower share.
    std::size_t lower = 0u;

    /// The destination bin that receives the upper share; equal to `lower` when nothing is split.
    std::size_t upper = 0u;

    /// The share of the count that goes to `upper`, within `[0, 1)`.
    double upperFraction = 0.0;

    /// Whether the transformed centre lay below the destination interval and was clamped.
    bool clampedLow = false;

    /// Whether the transformed centre lay above the destination interval and was clamped.
    bool clampedHigh = false;
};

/// Returns the placement table to the engine context's memory resource however the call ends.
class ScopedPlacementAllocation
{
public:
    ScopedPlacementAllocation(MemoryResource& resource, Placement* placements, std::uint64_t bytes,
                              std::size_t count) noexcept
        : m_resource(&resource), m_placements(placements), m_bytes(bytes), m_count(count)
    {
    }

    ~ScopedPlacementAllocation()
    {
        std::destroy_n(m_placements, m_count);
        m_resource->deallocate(m_placements, m_bytes, alignof(Placement));
    }

    ScopedPlacementAllocation(const ScopedPlacementAllocation&) = delete;
    ScopedPlacementAllocation& operator=(const ScopedPlacementAllocation&) = delete;

private:
    MemoryResource* m_resource;
    Placement* m_placements;
    std::uint64_t m_bytes;
    std::size_t m_count;
};

/// Where one transformed value's count goes.
///
/// The value is first clamped to the destination interval or refused, as the range policy says.
/// Its position is then measured in destination-bin centres, so that position `k` is the centre of
/// bin `k`: the nearest-bin rule rounds it, ties going up, and the linear split divides the count
/// between the two centres that bracket it, sending everything to the end bin beyond either end.
///
/// @param value the curve's output for one source-bin centre.
/// @param binCount how many destination bins there are, at least one.
/// @param request the destination interval, approximation and range policy.
/// @return the placement; `ErrorCode::InvalidData` for a value that is not finite; or
///         `ErrorCode::OutOfRange` for one outside the interval under
///         `HistogramTransformRangePolicy::Reject`.
[[nodiscard]] Result<Placement> placeValue(double value, std::size_t binCount, const HistogramTransformRequest& request)
{
    if (!std::isfinite(value))
    {
        return transformFailure(ErrorCode::InvalidData, "the histogram curve produced a non-finite value");
    }
    Placement placement;
    if (value < request.outputLow || value > request.outputHigh)
    {
        if (request.rangePolicy == HistogramTransformRangePolicy::Reject)
        {
            return transformFailure(ErrorCode::OutOfRange,
                                    "the histogram curve produced a value outside the destination interval");
        }
        placement.clampedLow = value < request.outputLow;
        placement.clampedHigh = value > request.outputHigh;
        value = placement.clampedLow ? request.outputLow : request.outputHigh;
    }

    const double position =
        (value - request.outputLow) / (request.outputHigh - request.outputLow) * static_cast<double>(binCount) - 0.5;
    if (request.approximation == HistogramTransformApproximation::NearestBin)
    {
        const double nearest = std::floor(position + 0.5);
        placement.lower =
            static_cast<std::size_t>((std::min)((std::max)(nearest, 0.0), static_cast<double>(binCount - 1u)));
        placement.upper = placement.lower;
        return placement;
    }
    if (position <= 0.0)
    {
        return placement;
    }
    if (position >= static_cast<double>(binCount - 1u))
    {
        placement.lower = binCount - 1u;
        placement.upper = placement.lower;
        return placement;
    }
    placement.lower = static_cast<std::size_t>(std::floor(position));
    placement.upper = placement.lower + 1u;
    placement.upperFraction = position - static_cast<double>(placement.lower);
    return placement;
}

} // namespace

Result<HistogramTransformResult> transformImageHistogram(const ExecutionContext& execution,
                                                         const ImageHistogram& sourceHistogram,
                                                         std::span<const std::uint64_t> sourceBins,
                                                         const HistogramTransformRequest& request,
                                                         std::span<std::uint64_t> destinationBins)
{
    if (!execution.valid())
    {
        return transformFailure(ErrorCode::NotInitialized,
                                "the histogram transform was given an empty execution context");
    }
    Status cancellation = execution.checkCancellation();
    if (cancellation.failed())
    {
        return cancellation;
    }
    if (sourceBins.size() != sourceHistogram.binCount)
    {
        return transformFailure(ErrorCode::IncompatibleArguments,
                                "the source counters do not match the histogram's recorded bin count");
    }
    if (sourceBins.empty() || destinationBins.empty() || !std::isfinite(sourceHistogram.lowest) ||
        !std::isfinite(sourceHistogram.highest) || !(sourceHistogram.lowest < sourceHistogram.highest) ||
        !std::isfinite(request.outputLow) || !std::isfinite(request.outputHigh) ||
        !(request.outputLow < request.outputHigh) || !isHistogramTransformApproximation(request.approximation) ||
        !isHistogramTransformRangePolicy(request.rangePolicy) || spansOverlap(sourceBins, destinationBins))
    {
        return transformFailure(ErrorCode::InvalidArgument,
                                "histogram intervals, spans and transform policies must be valid and disjoint");
    }
    if (sourceBins.size() > kMaxHistogramBins || destinationBins.size() > kMaxHistogramBins)
    {
        return transformFailure(ErrorCode::OutOfRange, "a histogram has more bins than this engine permits");
    }

    std::uint64_t sourceTotal = 0u;
    for (std::size_t bin = 0u; bin < sourceBins.size(); ++bin)
    {
        if ((bin & 255u) == 0u)
        {
            cancellation = execution.checkCancellation();
            if (cancellation.failed())
            {
                return cancellation;
            }
        }
        const std::uint64_t count = sourceBins[bin];
        if (count > (std::numeric_limits<std::uint64_t>::max)() - sourceTotal)
        {
            return transformFailure(ErrorCode::InvalidData, "the source histogram count overflows");
        }
        sourceTotal += count;
    }
    if (sourceTotal != sourceHistogram.counted)
    {
        return transformFailure(ErrorCode::IncompatibleArguments,
                                "the source counters do not sum to the histogram's recorded count");
    }
    if (sourceTotal == 0u)
    {
        return transformFailure(ErrorCode::EmptySelection, "the source histogram contains no counted sample");
    }

    MemoryResource* const resource = execution.memoryResource();
    if (resource == nullptr)
    {
        return transformFailure(ErrorCode::NotInitialized,
                                "the histogram transform execution has no context memory resource");
    }
    const std::uint64_t placementBytes = static_cast<std::uint64_t>(sourceBins.size()) * sizeof(Placement);
    Result<void*> allocated = resource->allocate(placementBytes, alignof(Placement));
    if (!allocated.hasValue())
    {
        return allocated.status();
    }
    Placement* const placements = static_cast<Placement*>(*allocated);
    for (std::size_t bin = 0u; bin < sourceBins.size(); ++bin)
    {
        std::construct_at(placements + bin);
    }
    const ScopedPlacementAllocation release{*resource, placements, placementBytes, sourceBins.size()};
    const double sourceWidth =
        (sourceHistogram.highest - sourceHistogram.lowest) / static_cast<double>(sourceBins.size());
    for (std::size_t bin = 0u; bin < sourceBins.size(); ++bin)
    {
        if ((bin & 255u) == 0u)
        {
            cancellation = execution.checkCancellation();
            if (cancellation.failed())
            {
                return cancellation;
            }
        }
        if (sourceBins[bin] == 0u)
        {
            continue;
        }
        const double center = sourceHistogram.lowest + (static_cast<double>(bin) + 0.5) * sourceWidth;
        const Result<double> mapped =
            evaluateCurve(request.points, request.interpolation, request.curveDomainPolicy, center);
        if (!mapped.hasValue())
        {
            return mapped.status();
        }
        const Result<Placement> placement = placeValue(*mapped, destinationBins.size(), request);
        if (!placement.hasValue())
        {
            return placement.status();
        }
        placements[bin] = *placement;
    }
    cancellation = execution.checkCancellation();
    if (cancellation.failed())
    {
        return cancellation;
    }

    std::fill(destinationBins.begin(), destinationBins.end(), std::uint64_t{0});
    HistogramTransformResult result;
    result.count = sourceTotal;
    result.approximation = request.approximation;
    for (std::size_t bin = 0u; bin < sourceBins.size(); ++bin)
    {
        const std::uint64_t count = sourceBins[bin];
        if (count == 0u)
        {
            continue;
        }
        const Placement& placement = placements[bin];
        ++result.sourceBinsVisited;
        result.clampedLow += placement.clampedLow ? count : 0u;
        result.clampedHigh += placement.clampedHigh ? count : 0u;
        if (placement.lower == placement.upper)
        {
            destinationBins[placement.lower] += count;
            continue;
        }
        const std::uint64_t upper =
            static_cast<std::uint64_t>(std::floor(static_cast<long double>(count) * placement.upperFraction));
        destinationBins[placement.lower] += count - upper;
        destinationBins[placement.upper] += upper;
    }
    return result;
}

} // namespace an
