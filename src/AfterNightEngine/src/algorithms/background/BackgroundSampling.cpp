// AfterNightEngine - measuring where a frame's background can be measured.
//
// A site is a square box around a whole pixel; its measurement is the exact median of each
// channel's admitted samples and one luminance formed from those medians. A grid sampling measures
// every site first, in parallel and in grid order, because the acceptance threshold is a statistic
// of the measurements themselves: the median of the luminances and their median absolute deviation.
// The accepted sites are then compacted to the front of the caller's array.
//
// The site's box of twenty-five pixels by default, its median, and the acceptance of a site whose level
// is at or below a median plus the tolerance times the median absolute deviation are adapted from
// Siril's background extraction (https://siril.org, `siril/src/algos/background_extraction.c`), whose file
// states `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)` and
// `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. The engine's are the statistics taken over the sites' own
// luminances, the validity rule, the parallel measurement in grid order and the typed failures. The
// provenance note is `docs/dev/BACKGROUND_SAMPLING_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Background/BackgroundModel.h>

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
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
#include <span>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kGridOperation = "an::planBackgroundSampleGrid";
constexpr const char* kSampleOperation = "an::sampleImageBackground";
constexpr const char* kMeasureOperation = "an::measureImageBackgroundSample";
constexpr const char* kNearestOperation = "an::nearestBackgroundSample";

/// One failure from this domain.
///
/// The neighbourhood family already spells a failure of an image argument one way, and a background
/// model that spelled it a second way would give a caller two shapes to branch on for one refusal.
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

/// Whether a sampling request describes a grid at all.
///
/// @param request the box size, the density and the acceptance rule.
/// @param operation the entry point the caller is inside.
/// @param needsDensity whether the sites-per-line field is read, which a single hand-placed site
///        does not read.
/// @return success, or the first reason the request means nothing.
[[nodiscard]] Status checkSampleRequest(const BackgroundSampleRequest& request, const char* operation,
                                        bool needsDensity)
{
    if (!isBackgroundLuminance(request.luminance))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, operation,
                                 "the luminance rule is not one this engine implements");
    }
    if ((request.extent % 2u) == 0u)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, operation,
                                 "a sample box needs an odd side, so that it has a centre to sit on");
    }
    if (request.extent < kMinBackgroundSampleExtent || request.extent > kMaxBackgroundSampleExtent)
    {
        return backgroundLimitFailure(
            ErrorCode::OutOfRange, operation, "the sample box is outside its size limits", request.extent,
            request.extent < kMinBackgroundSampleExtent ? kMinBackgroundSampleExtent : kMaxBackgroundSampleExtent);
    }
    if (!std::isfinite(request.tolerance) || request.tolerance < 0.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, operation,
                                 "the acceptance tolerance has to be finite and not negative");
    }
    if (needsDensity && request.samplesPerLine < 2u)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, operation,
                                 "a grid needs at least two sites along a row to have a spacing");
    }
    return Status::success();
}

/// The single number one site is accepted on, from its per-channel medians.
///
/// The rule is applied to the medians rather than to a per-pixel luminance, which is what lets one
/// measurement per channel serve both the selection and the fit. For a background box - which is
/// what a site is meant to be - the two agree to well within the noise, and the mean of the channel
/// medians is the more robust of the two. `Rec709` over fewer than three channels falls back to the
/// mean.
///
/// @param medians the per-channel medians.
/// @param channelCount how many of them hold a measurement.
/// @param rule which luminance to form.
/// @return the luminance.
[[nodiscard]] double siteLuminance(const std::array<double, kMaxBackgroundChannels>& medians,
                                   std::uint32_t channelCount, BackgroundLuminance rule) noexcept
{
    if (channelCount == 0u)
    {
        return 0.0;
    }
    if (rule == BackgroundLuminance::FirstChannel)
    {
        return medians[0];
    }
    if (rule == BackgroundLuminance::Rec709 && channelCount >= 3u)
    {
        return kBackgroundLuminanceRed * medians[0] + kBackgroundLuminanceGreen * medians[1] +
               kBackgroundLuminanceBlue * medians[2];
    }
    double sum = 0.0;
    for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
    {
        sum += medians[channel];
    }
    return sum / static_cast<double>(channelCount);
}

/// Collects the samples of one box of one channel that the validity rule accepts.
///
/// @tparam SampleT the resident C++ type the channel holds.
/// @param samples where the image's samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to read.
/// @param validity the rule and optional packed mask for that channel.
/// @param centreX the column the box is centred on.
/// @param centreY the row it is centred on.
/// @param radius half the box's side.
/// @param values where the accepted samples go, of at least the box's area.
/// @return how many were accepted.
template <typename SampleT>
[[nodiscard]] std::uint32_t collectBox(const void* samples, const ImageLayout& layout, std::uint32_t channelIndex,
                                       const KernelValidityView& validity, std::uint32_t centreX, std::uint32_t centreY,
                                       std::uint32_t radius, std::span<double> values) noexcept
{
    const std::uint32_t columnStride = layout.samplesPerPixel();
    const std::uint32_t firstRow = centreY - radius;
    const std::uint32_t firstColumn = centreX - radius;
    const std::uint32_t side = 2u * radius + 1u;
    std::uint32_t accepted = 0u;
    for (std::uint32_t row = 0u; row < side; ++row)
    {
        const std::uint32_t y = firstRow + row;
        const auto* const sourceRow = reinterpret_cast<const SampleT*>(
            static_cast<const std::uint8_t*>(samples) + plane_detail::channelRowByteOffset(layout, channelIndex, y));
        for (std::uint32_t column = 0u; column < side; ++column)
        {
            const std::uint32_t x = firstColumn + column;
            const SampleT stored = sourceRow[static_cast<std::size_t>(x) * columnStride];
            if (!sampleIsValidByValue(validity.descriptor, stored) || !plane_detail::maskAllows(validity, x, y))
            {
                continue;
            }
            if (accepted >= values.size())
            {
                return accepted;
            }
            values[accepted] = static_cast<double>(stored);
            ++accepted;
        }
    }
    return accepted;
}

/// Measures one box over every channel.
///
/// @param sampleType what the samples are.
/// @param samples where they start.
/// @param layout where they sit.
/// @param selection which samples mean something.
/// @param request the box size and the luminance rule.
/// @param centreX the column the box is centred on.
/// @param centreY the row it is centred on.
/// @param channelCount how many channels to measure.
/// @param scratch working room for one channel's box, of at least the box's area.
/// @param rejected how many samples the validity rule rejected, added to for every channel measured
///        before the first that had none accepted.
/// @param site where the measurement goes.
/// @return true when the representation is known and at least one sample of every channel was
///         accepted.
[[nodiscard]] bool measureSite(SampleType sampleType, const void* samples, const ImageLayout& layout,
                               const SpatialSelection& selection, const BackgroundSampleRequest& request,
                               std::uint32_t centreX, std::uint32_t centreY, std::uint32_t channelCount,
                               std::span<double> scratch, std::uint64_t& rejected, BackgroundSample& site) noexcept
{
    const std::uint32_t radius = request.extent / 2u;
    const std::uint64_t area = static_cast<std::uint64_t>(request.extent) * request.extent;
    site = BackgroundSample{};
    site.x = static_cast<double>(centreX);
    site.y = static_cast<double>(centreY);
    site.extent = request.extent;
    site.channelCount = channelCount;
    std::uint32_t smallestCount = 0u;
    for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
    {
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        std::uint32_t accepted = 0u;
        const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            accepted = collectBox<SampleT>(samples, layout, channel, validity, centreX, centreY, radius, scratch);
        });
        if (!known || accepted == 0u)
        {
            return false;
        }
        rejected += area - accepted;
        site.median[channel] = background_detail::medianOf(scratch.subspan(0u, accepted));
        smallestCount = channel == 0u ? accepted : (std::min)(smallestCount, accepted);
    }
    site.sampleCount = smallestCount;
    site.luminance = siteLuminance(site.median, channelCount, request.luminance);
    return true;
}

} // namespace

const char* backgroundLuminanceName(BackgroundLuminance luminance) noexcept
{
    switch (luminance)
    {
    case BackgroundLuminance::ChannelMean:
        return "channel-mean";
    case BackgroundLuminance::Rec709:
        return "rec709";
    case BackgroundLuminance::FirstChannel:
        return "first-channel";
    }
    return "unknown";
}

Result<BackgroundSampleGrid> planBackgroundSampleGrid(const BackgroundSampleRequest& request, std::uint32_t width,
                                                      std::uint32_t height)
{
    const Status checked = checkSampleRequest(request, kGridOperation, true);
    if (checked.failed())
    {
        return checked;
    }
    // One pixel of margin on each side, which is what keeps the outermost box inside the picture
    // whatever the rounding of the spacing does.
    if (width < request.extent + 2u || height < request.extent + 2u)
    {
        return backgroundFailure(ErrorCode::EmptySelection, kGridOperation,
                                 "the picture cannot hold one complete sample box with a margin");
    }

    const auto columns = static_cast<double>(request.samplesPerLine);
    const auto boxSide = static_cast<double>(request.extent);
    const double available = static_cast<double>(width - 2u);
    const double occupied = columns * boxSide;
    if (occupied > available)
    {
        return backgroundLimitFailure(ErrorCode::OutOfRange, kGridOperation,
                                      "this many boxes of this size do not fit across the picture",
                                      request.samplesPerLine, static_cast<std::uint64_t>(available / boxSide));
    }
    const double gap = (available - occupied) / (columns - 1.0);
    const double pitch = boxSide + gap;

    const double verticalAvailable = static_cast<double>(height - 2u);
    auto rows = static_cast<std::uint32_t>(std::floor((verticalAvailable - boxSide) / pitch)) + 1u;
    rows = (std::max)(rows, 1u);
    while (rows > 1u && (static_cast<double>(rows - 1u) * pitch + boxSide) > verticalAvailable)
    {
        --rows;
    }

    const std::uint64_t siteCount = static_cast<std::uint64_t>(request.samplesPerLine) * rows;
    if (siteCount > kMaxBackgroundSites)
    {
        return backgroundLimitFailure(ErrorCode::OutOfRange, kGridOperation,
                                      "this grid holds more sites than a background fit accepts", siteCount,
                                      kMaxBackgroundSites);
    }

    const double occupiedWidth = static_cast<double>(request.samplesPerLine - 1u) * pitch + boxSide;
    const double occupiedHeight = static_cast<double>(rows - 1u) * pitch + boxSide;
    const auto originX = static_cast<std::uint32_t>(std::floor((static_cast<double>(width) - occupiedWidth) * 0.5)) +
                         request.extent / 2u;
    const auto originY = static_cast<std::uint32_t>(std::floor((static_cast<double>(height) - occupiedHeight) * 0.5)) +
                         request.extent / 2u;

    return BackgroundSampleGrid{.columns = request.samplesPerLine,
                                .rows = rows,
                                .extent = request.extent,
                                .columnSpacing = pitch,
                                .rowSpacing = pitch,
                                .originX = originX,
                                .originY = originY};
}

Result<BackgroundSampleResult> sampleImageBackground(const ExecutionContext& execution, SampleType sampleType,
                                                     const void* source, const ImageLayout& sourceLayout,
                                                     const BackgroundSampleRequest& request,
                                                     std::span<BackgroundSample> sites,
                                                     const SpatialSelection& selection)
{
    spatial_detail::SpatialGeometry geometry{};
    const SpatialOutputPolicy reading{};
    const Status ready = spatial_detail::checkSpatialImage(execution, sampleType, source, sourceLayout, reading,
                                                           selection, kSampleOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (geometry.channelCount > kMaxBackgroundChannels)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kSampleOperation,
                                      "a background sample carries a median for at most this many channels",
                                      geometry.channelCount, kMaxBackgroundChannels);
    }
    const Result<BackgroundSampleGrid> grid = planBackgroundSampleGrid(request, geometry.width, geometry.height);
    if (!grid.hasValue())
    {
        return grid.status();
    }
    if (sites.size() < grid->siteCount())
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kSampleOperation,
                                      "the site array is shorter than the grid it would hold", sites.size(),
                                      grid->siteCount());
    }

    const std::uint64_t area = static_cast<std::uint64_t>(request.extent) * request.extent;
    const TaskCost siteCost{.bytesPerItem = area * geometry.channelCount * sizeof(double),
                            .operationsPerItem = area * geometry.channelCount * 4u};
    const std::uint32_t workers = plannedWorkerCount(execution, grid->siteCount(), siteCost);
    const std::uint64_t scratchBytes = static_cast<std::uint64_t>(workers) * area * sizeof(double);
    Result<plane_detail::ScopedContextMemory> scratch =
        plane_detail::ScopedContextMemory::take(execution, scratchBytes, alignof(double), kSampleOperation);
    if (!scratch.hasValue())
    {
        return scratch.status();
    }
    const std::span<double> scratchValues{static_cast<double*>(scratch->data()),
                                          static_cast<std::size_t>(workers) * static_cast<std::size_t>(area)};

    // Every site is measured before any is accepted, because the threshold is a statistic of the
    // measurements themselves. The caller's array is the working room for that: the grid is measured
    // into it in grid order and then compacted in place, which is why it has to be as long as the
    // grid rather than as long as the answer.
    const std::span<BackgroundSample> siteBuffer = sites.subspan(0u, grid->siteCount());
    Result<plane_detail::ScopedContextMemory> flags =
        plane_detail::ScopedContextMemory::take(execution, grid->siteCount(), alignof(std::uint8_t), kSampleOperation);
    if (!flags.hasValue())
    {
        return flags.status();
    }
    const std::span<std::uint8_t> siteMeasured{static_cast<std::uint8_t*>(flags->data()),
                                               static_cast<std::size_t>(grid->siteCount())};

    std::atomic<std::uint64_t> rejectedSamples{0u};
    const std::uint32_t columns = grid->columns;
    const Status walked =
        parallelForWorkers(execution, grid->siteCount(), siteCost, [&](const WorkerTask& task) -> Status {
            const std::span<double> workerScratch =
                scratchValues.subspan(static_cast<std::size_t>(task.workerSlot) * static_cast<std::size_t>(area),
                                      static_cast<std::size_t>(area));
            std::uint64_t rejectedHere = 0u;
            for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
            {
                const auto column = static_cast<std::uint32_t>(index % columns);
                const auto row = static_cast<std::uint32_t>(index / columns);
                const auto centreX = static_cast<std::uint32_t>(
                    grid->originX +
                    static_cast<std::uint32_t>(std::llround(static_cast<double>(column) * grid->columnSpacing)));
                const auto centreY = static_cast<std::uint32_t>(
                    grid->originY +
                    static_cast<std::uint32_t>(std::llround(static_cast<double>(row) * grid->rowSpacing)));
                BackgroundSample site{};
                const bool ok = measureSite(sampleType, source, sourceLayout, selection, request, centreX, centreY,
                                            geometry.channelCount, workerScratch, rejectedHere, site);
                siteBuffer[static_cast<std::size_t>(index)] = site;
                siteMeasured[static_cast<std::size_t>(index)] = ok ? std::uint8_t{1} : std::uint8_t{0};
            }
            rejectedSamples.fetch_add(rejectedHere, std::memory_order_relaxed);
            return Status::success();
        });
    if (walked.failed())
    {
        return walked;
    }

    // The frame's own background level and scale, taken over the sites that could be measured.
    Result<plane_detail::ScopedContextMemory> luminances = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(grid->siteCount()) * sizeof(double), alignof(double), kSampleOperation);
    if (!luminances.hasValue())
    {
        return luminances.status();
    }
    const std::span<double> values{static_cast<double*>(luminances->data()),
                                   static_cast<std::size_t>(grid->siteCount())};
    std::uint32_t measuredCount = 0u;
    for (std::uint32_t index = 0u; index < grid->siteCount(); ++index)
    {
        if (siteMeasured[index] == 0u)
        {
            continue;
        }
        values[measuredCount] = siteBuffer[index].luminance;
        ++measuredCount;
    }
    if (measuredCount == 0u)
    {
        return backgroundFailure(ErrorCode::EmptySelection, kSampleOperation,
                                 "no sample box held a value the validity rule accepts");
    }

    const double level = background_detail::medianOf(values.subspan(0u, measuredCount));
    for (std::uint32_t index = 0u; index < measuredCount; ++index)
    {
        values[index] = std::abs(values[index] - level);
    }
    const double deviation = background_detail::medianOf(values.subspan(0u, measuredCount));
    const double threshold = level + request.tolerance * deviation;

    BackgroundSampleResult result{};
    result.grid = *grid;
    result.channelCount = geometry.channelCount;
    result.frameLevel = level;
    result.frameDeviation = deviation;
    result.threshold = threshold;
    result.rejectedSamples = rejectedSamples.load(std::memory_order_relaxed);
    result.workingBytes = scratch->bytes() + flags->bytes() + luminances->bytes();
    result.plannedWorkers = workers;

    for (std::uint32_t index = 0u; index < grid->siteCount(); ++index)
    {
        if (siteMeasured[index] == 0u)
        {
            ++result.rejectedCount;
            continue;
        }
        const BackgroundSample& site = siteBuffer[index];
        if (site.luminance > threshold || (request.rejectNonPositive && !(site.luminance > 0.0)))
        {
            ++result.rejectedCount;
            continue;
        }
        sites[result.acceptedCount] = site;
        ++result.acceptedCount;
    }
    return result;
}

Result<BackgroundSample> measureImageBackgroundSample(const ExecutionContext& execution, SampleType sampleType,
                                                      const void* source, const ImageLayout& sourceLayout,
                                                      const BackgroundSampleRequest& request, std::uint32_t x,
                                                      std::uint32_t y, const SpatialSelection& selection)
{
    spatial_detail::SpatialGeometry geometry{};
    const SpatialOutputPolicy reading{};
    const Status ready = spatial_detail::checkSpatialImage(execution, sampleType, source, sourceLayout, reading,
                                                           selection, kMeasureOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    const Status checked = checkSampleRequest(request, kMeasureOperation, false);
    if (checked.failed())
    {
        return checked;
    }
    if (geometry.channelCount > kMaxBackgroundChannels)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kMeasureOperation,
                                      "a background sample carries a median for at most this many channels",
                                      geometry.channelCount, kMaxBackgroundChannels);
    }
    const std::uint32_t radius = request.extent / 2u;
    if (x >= geometry.width || y >= geometry.height || x < radius || y < radius || radius >= geometry.width - x ||
        radius >= geometry.height - y)
    {
        return backgroundFailure(ErrorCode::OutOfRange, kMeasureOperation,
                                 "the sample box would reach outside the picture");
    }

    const std::uint64_t area = static_cast<std::uint64_t>(request.extent) * request.extent;
    Result<plane_detail::ScopedContextMemory> scratch =
        plane_detail::ScopedContextMemory::take(execution, area * sizeof(double), alignof(double), kMeasureOperation);
    if (!scratch.hasValue())
    {
        return scratch.status();
    }
    const std::span<double> values{static_cast<double*>(scratch->data()), static_cast<std::size_t>(area)};

    std::uint64_t rejected = 0u;
    BackgroundSample site{};
    if (!measureSite(sampleType, source, sourceLayout, selection, request, x, y, geometry.channelCount, values,
                     rejected, site))
    {
        return backgroundFailure(ErrorCode::EmptySelection, kMeasureOperation,
                                 "the sample box held no value the validity rule accepts");
    }
    return site;
}

Result<std::uint32_t> nearestBackgroundSample(std::span<const BackgroundSample> sites, double x, double y)
{
    if (sites.empty())
    {
        return backgroundFailure(ErrorCode::EmptySelection, kNearestOperation,
                                 "there is no site for a position to be nearest to");
    }
    if (!std::isfinite(x) || !std::isfinite(y))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kNearestOperation, "the position is not finite");
    }
    std::uint32_t nearest = 0u;
    double smallest = std::numeric_limits<double>::max();
    for (std::uint32_t index = 0u; index < sites.size(); ++index)
    {
        const double dx = sites[index].x - x;
        const double dy = sites[index].y - y;
        const double distance = dx * dx + dy * dy;
        if (distance < smallest)
        {
            smallest = distance;
            nearest = index;
        }
    }
    return nearest;
}

} // namespace an
