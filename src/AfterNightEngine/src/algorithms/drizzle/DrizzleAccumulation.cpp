// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - drizzle accumulation: the argument and aliasing checks, the validation
// pass, and the ordered scatter over destination row strips.
//
// A call runs in three steps, and no destination term is written until the first two succeed.
// `checkArguments` refuses every malformed span, geometry, table and backend. The validation pass
// then reads every source sample and auxiliary weight, and for the public entry every prior
// destination term, in parallel over source rows and counting only integers; a non-finite sample
// under the reject policy or an unrepresentable product fails there. Only then does the scatter run.
//
// The scatter partitions destination rows, not source rows. Each worker owns a strip of destination
// rows, walks every source row and column of the band in order, and adds only into its own strip,
// so every destination element sees one addition order whatever the worker count; there are no
// floating atomics and no merge. A source row whose mapped support cannot reach a strip is skipped
// by its recorded or scanned vertical bounds, and when the band has fewer rows than the destination
// only the destination rows the band can reach are partitioned. The serial reference runs the same
// scatter over the whole band with the clipped-polygon overlap and per-cell coefficients; the
// optimized path adds direct routes for mono floating samples under Point and Turbo and caches the
// horizontal coefficients of a separable drop.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>

#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "algorithms/drizzle/DrizzleAccumulationInternal.h"
#include "algorithms/drizzle/DrizzleGeometryInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace an
{
namespace
{

using namespace drizzle_detail;

bool simpleMono(const DrizzleModel& model, const DrizzleInputBand& input) noexcept
{
    return !model.usesCfa() && model.request().input.extent().channelCount() == 1u &&
           model.request().validity.rule() == ValidityRule::AllValid && input.pixelWeights == nullptr &&
           input.flatWeights == nullptr &&
           (input.pixels.sampleType == SampleType::Float32 || input.pixels.sampleType == SampleType::Float64);
}

double monoValue(const DrizzleSampleBand& band, const std::byte* row, std::uint32_t x) noexcept
{
    return band.sampleType == SampleType::Float32 ? static_cast<double>(reinterpret_cast<const float*>(row)[x])
                                                  : reinterpret_cast<const double*>(row)[x];
}

bool addContribution(const DrizzleAccumulatorBand& destination, std::size_t index, double sample, double weight,
                     std::uint32_t frameIndex, DrizzleAccumulationReport& report) noexcept
{
    const double sum = destination.sums[index] + sample * weight;
    const double total = destination.weightSums[index] + weight;
    if (!std::isfinite(weight) || !std::isfinite(sum) || !std::isfinite(total))
        return false;
    destination.sums[index] = sum;
    destination.weightSums[index] = total;
    ++report.contributions;
    if (!destination.coverage.empty() && destination.lastFrame[index] != frameIndex + 1u)
    {
        ++destination.coverage[index];
        destination.lastFrame[index] = frameIndex + 1u;
        ++report.covered;
    }
    return true;
}

bool rectangleRange(double centre, double half, std::uint32_t first, std::uint32_t end, std::uint32_t& begin,
                    std::uint32_t& finish) noexcept
{
    if (!std::isfinite(centre))
        return false;
    const double low = std::floor(centre - half + 0.5), high = std::ceil(centre + half - 0.5);
    if (high < first || low >= end)
        return false;
    begin = static_cast<std::uint32_t>((std::max)(static_cast<double>(first), low));
    finish = static_cast<std::uint32_t>((std::min)(static_cast<double>(end - 1u), high)) + 1u;
    return begin < finish;
}

struct ByteRange
{
    const void* data = nullptr;
    std::uint64_t bytes = 0u;
};

bool overlaps(ByteRange a, ByteRange b) noexcept
{
    const auto x = reinterpret_cast<std::uintptr_t>(a.data), y = reinterpret_cast<std::uintptr_t>(b.data);
    return a.bytes != 0u && b.bytes != 0u && (x <= y ? y - x < a.bytes : x - y < b.bytes);
}

Status checkSamples(const DrizzleSampleBand& band, const ImageDescriptor& descriptor, std::uint32_t rows,
                    const ExecutionContext& execution)
{
    const auto& extent = band.layout.extent();
    if (band.sampleType != descriptor.sampleType() ||
        band.layout.elementSizeBytes() != sampleTypeSizeBytes(band.sampleType) ||
        extent.width() != descriptor.extent().width() || extent.channelCount() != descriptor.extent().channelCount() ||
        extent.height() != rows)
        return accumulationFailure(ErrorCode::IncompatibleArguments,
                                   "sample representation or band geometry differs from the model");
    if (band.samples.data() == nullptr || band.samples.size() < band.layout.totalBytes() ||
        reinterpret_cast<std::uintptr_t>(band.samples.data()) % sampleTypeSizeBytes(band.sampleType) != 0u)
        return accumulationFailure(ErrorCode::InvalidArgument, "sample buffer is null, short or misaligned");
    if (band.layout.totalBytes() > execution.limits().maxImageBytes)
        return accumulationFailure(ErrorCode::LimitExceeded, "source buffer exceeds the execution's image limit");
    return Status::success();
}

Status checkArguments(const ExecutionContext& execution, const DrizzleModel& model, const DrizzleInputBand& input,
                      const DrizzlePixmapView& map, const DrizzleAccumulatorBand& destination,
                      const DrizzleKernelTable* table)
{
    if (!execution.valid())
        return accumulationFailure(ErrorCode::NotInitialized, "execution is uninitialized");
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (execution.backendFallback() == BackendFallback::Forbidden &&
        (execution.backend() == ComputeBackend::Gpu || (execution.instructionSet() != InstructionSet::Automatic &&
                                                        execution.instructionSet() != InstructionSet::Scalar)))
        return accumulationFailure(ErrorCode::UnsupportedBackend,
                                   "drizzle accumulation has reference and scalar CPU implementations");
    const auto& request = model.request();
    const auto& source = request.input.extent();
    const auto& output = model.outputDescriptor().extent();
    for (const auto& extent : {source, output})
        if (!ImageExtent::create(extent.width(), extent.height(), extent.channelCount(), execution.limits()))
            return accumulationFailure(ErrorCode::LimitExceeded, "drizzle geometry exceeds execution limits");
    if (map.sourceExtent() != source || map.outputExtent() != output)
        return accumulationFailure(ErrorCode::IncompatibleArguments,
                                   "coordinate map has different source or output geometry");
    const auto rows = input.pixels.layout.extent().height();
    if (input.firstRow != map.coreRowBegin() || rows != map.coreRowEnd() - map.coreRowBegin() ||
        input.frameIndex >= request.frameCount)
        return accumulationFailure(ErrorCode::OutOfRange, "source band or frame index is outside the requested core");
    if (destination.rowCount == 0u || destination.firstRow >= output.height() ||
        destination.rowCount > output.height() - destination.firstRow)
        return accumulationFailure(ErrorCode::OutOfRange, "destination band lies outside the output");
    if (const auto status = checkSamples(input.pixels, request.input, rows, execution); status.failed())
        return status;
    for (const auto& pair :
         {std::pair{input.pixelWeights, &request.pixelWeights}, std::pair{input.flatWeights, &request.flatWeights}})
    {
        if ((pair.first != nullptr) != pair.second->has_value())
            return accumulationFailure(ErrorCode::IncompatibleArguments,
                                       "auxiliary weight presence differs from the model");
        if (pair.first != nullptr)
            if (const auto status = checkSamples(*pair.first, **pair.second, rows, execution); status.failed())
                return status;
    }
    if ((input.mask != nullptr) != request.validity.needsMask() ||
        (input.mask != nullptr && input.mask->extent() != input.pixels.layout.extent()))
        return accumulationFailure(ErrorCode::IncompatibleArguments,
                                   "mask presence or band extent differs from validity");
    for (std::uint32_t channel = 0u; channel < 4u; ++channel)
        if (!std::isfinite(input.frameWeights[channel]) || input.frameWeights[channel] <= 0.0 ||
            (channel >= output.channelCount() && input.frameWeights[channel] != 1.0))
            return accumulationFailure(ErrorCode::InvalidArgument,
                                       "frame weights must be positive finite with unused channels equal to one");
    const bool lanczos =
        request.parameters.kernel == DrizzleKernel::Lanczos2 || request.parameters.kernel == DrizzleKernel::Lanczos3;
    if (lanczos != (table != nullptr) || (table != nullptr && table->kernel() != request.parameters.kernel))
        return accumulationFailure(ErrorCode::IncompatibleArguments,
                                   "kernel table presence or order differs from the kernel");
    const double width = request.parameters.pixelFraction * request.parameters.scale;
    if (request.parameters.kernel != DrizzleKernel::Point &&
        (!std::isfinite(1.0 / (width * width)) || width < std::numeric_limits<float>::min()))
        return accumulationFailure(ErrorCode::InvalidData,
                                   "drop width is too small to represent its geometry or normalization");
    if (request.parameters.kernel == DrizzleKernel::Square &&
        (source.width() < 2u || source.height() < 2u ||
         !map.containsRow(input.firstRow == 0u ? 0u : input.firstRow - 1u) ||
         !map.containsRow((std::min)(map.coreRowEnd(), source.height() - 1u))))
        return accumulationFailure(ErrorCode::OutOfRange,
                                   "square drops require a two-dimensional map and their clipped row halo");
    const auto elements = static_cast<std::uint64_t>(output.width()) * destination.rowCount * output.channelCount();
    if (destination.sums.size() != elements || destination.weightSums.size() != elements ||
        destination.coverage.size() != (request.output.coverageMap ? elements : 0u) ||
        destination.lastFrame.size() != destination.coverage.size())
        return accumulationFailure(ErrorCode::IncompatibleArguments,
                                   "accumulator spans do not exactly cover the destination band");
    const std::array<ByteRange, 4u> writable{{{destination.sums.data(), destination.sums.size_bytes()},
                                              {destination.weightSums.data(), destination.weightSums.size_bytes()},
                                              {destination.coverage.data(), destination.coverage.size_bytes()},
                                              {destination.lastFrame.data(), destination.lastFrame.size_bytes()}}};
    const auto maskBytes = input.mask == nullptr
                               ? 0u
                               : (input.mask->extent().channelCount() - 1u) * input.mask->planeStrideBytes() +
                                     (rows - 1u) * input.mask->rowStrideBytes() + packedMaskRowBytes(source.width());
    const std::array<ByteRange, 5u> readable{
        {{input.pixels.samples.data(), input.pixels.layout.totalBytes()},
         {input.pixelWeights == nullptr ? nullptr : input.pixelWeights->samples.data(),
          input.pixelWeights == nullptr ? 0u : input.pixelWeights->layout.totalBytes()},
         {input.flatWeights == nullptr ? nullptr : input.flatWeights->samples.data(),
          input.flatWeights == nullptr ? 0u : input.flatWeights->layout.totalBytes()},
         {input.mask == nullptr ? nullptr : input.mask->data(), maskBytes},
         {table, table == nullptr ? 0u : sizeof(*table)}}};
    for (std::size_t i = 0u; i < writable.size(); ++i)
    {
        const auto range = writable[i];
        if (range.bytes == 0u)
            continue;
        const auto address = reinterpret_cast<std::uintptr_t>(range.data);
        if (range.data == nullptr || address % (i < 2u ? alignof(double) : alignof(std::uint32_t)) != 0u ||
            range.bytes > static_cast<std::uint64_t>((std::numeric_limits<std::ptrdiff_t>::max)()) ||
            range.bytes > (std::numeric_limits<std::uintptr_t>::max)() - address)
            return accumulationFailure(ErrorCode::InvalidArgument,
                                       "accumulator address is null, misaligned or unrepresentable");
        if (DrizzleGeometryAccess::overlaps(map, range.data, static_cast<std::size_t>(range.bytes)))
            return accumulationFailure(ErrorCode::Aliasing, "accumulator overlaps the coordinate map");
        for (const auto other : readable)
            if (overlaps(range, other))
                return accumulationFailure(ErrorCode::Aliasing, "accumulator overlaps an input");
        for (std::size_t j = 0u; j < i; ++j)
            if (overlaps(range, writable[j]))
                return accumulationFailure(ErrorCode::Aliasing, "accumulator terms overlap");
    }
    return Status::success();
}

// Bound all bilinear drops in a source row before visiting individual footprints. Interior
// interpolation is a convex combination; edge extrapolation has total negative weight at most
// 1.25 for a half-pixel corner, so expanding the complete tap range twice is conservative.
std::pair<double, double> rowSupport(const ExecutionContext& execution, const DrizzleModel& model,
                                     const DrizzlePixmapView& map, std::uint32_t y)
{
    const bool square = model.request().parameters.kernel == DrizzleKernel::Square;
    const auto begin = square && y > 0u ? y - 1u : y;
    const auto finish = square && y + 1u < map.sourceExtent().height() ? y + 2u : y + 1u;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (auto rowIndex = begin; rowIndex < finish; ++rowIndex)
    {
        // A generated map recorded each row's span; a bare view is scanned, once per strip.
        if (map.hasRowBounds())
        {
            const DrizzleRowBounds recorded = DrizzleGeometryAccess::rowBoundsUnchecked(map, rowIndex);
            low = (std::min)(low, static_cast<double>(recorded.low));
            high = (std::max)(high, static_cast<double>(recorded.high));
            continue;
        }
        const auto row = *map.row(rowIndex);
        std::size_t checked = 0u;
        for (float value : row.y)
        {
            if ((checked++ % 4096u) == 0u && execution.checkCancellation().failed())
                return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            if (std::isfinite(value))
            {
                low = (std::min)(low, static_cast<double>(value));
                high = (std::max)(high, static_cast<double>(value));
            }
        }
    }
    if (low > high)
        return {low, high};
    double radius = 3.0 * model.request().parameters.pixelFraction * model.request().parameters.scale + 1.0;
    if (square)
        radius = 2.0 * (high - low) + 1.0;
    return {low - radius, high + radius};
}

bool rowCanReach(const ExecutionContext& execution, const DrizzleModel& model, const DrizzlePixmapView& map,
                 std::uint32_t y, std::uint32_t first, std::uint32_t end)
{
    const auto [low, high] = rowSupport(execution, model, map, y);
    return low <= high && high >= first && low <= static_cast<double>(end - 1u);
}

template <bool Optimized>
Status scatter(const ExecutionContext& execution, const DrizzleModel& model, const DrizzleInputBand& input,
               const DrizzleReaders& readers, const DrizzlePixmapView& map, const DrizzleAccumulatorBand& destination,
               const DrizzleKernelTable* table, std::uint32_t first, std::uint32_t end,
               DrizzleAccumulationReport& report)
{
    report.implementation =
        Optimized ? DrizzleAccumulationImplementation::OwnedRows : DrizzleAccumulationImplementation::Reference;
    const auto width = model.outputDescriptor().extent().width();
    const bool simple = Optimized && simpleMono(model, input);
    const double scaleSquared = model.request().parameters.scale * model.request().parameters.scale;
    const double monoWeight =
        model.request().parameters.weightScale * model.request().parameters.channelWeights[0] * input.frameWeights[0];
    const std::uint64_t planeSize = static_cast<std::uint64_t>(width) * destination.rowCount;
    const auto kernel = model.request().parameters.kernel;
    const double dropWidth = model.request().parameters.pixelFraction * model.request().parameters.scale;
    const double halfDrop = dropWidth * 0.5, dropArea = dropWidth * dropWidth;
    const auto axisCoefficient = [&](const DrizzleDrop& drop, double distance) {
        if (kernel == DrizzleKernel::Turbo)
            return (std::max)(0.0, (std::min)(drop.radius, distance + 0.5) - (std::max)(-drop.radius, distance - 0.5));
        return drizzleAxisCoefficient(model, drop, table, distance);
    };
    std::uint64_t checkpoints = 0u;
    const auto checkpoint = [&]() -> Status {
        if ((checkpoints++ % 4096u) == 0u)
            return execution.checkCancellation();
        return Status::success();
    };
    for (auto y = map.coreRowBegin(); y < map.coreRowEnd(); ++y)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if constexpr (Optimized)
            if (!rowCanReach(execution, model, map, y, first, end))
                continue;
        const auto row = *map.row(y);
        const auto* sampleRow =
            input.pixels.samples.data() + (y - input.firstRow) * input.pixels.layout.rowStrideBytes();
        for (std::uint32_t x = 0u; x < map.sourceExtent().width(); ++x)
        {
            if (const auto stopped = checkpoint(); stopped.failed())
                return stopped;
            // Mono floating samples with no auxiliary weights skip the drop and the gather: Point adds
            // to the nearest cell, rounding half away from zero, and Turbo walks its clipped rectangle.
            if (simple && model.request().parameters.kernel == DrizzleKernel::Point)
            {
                const double ox = row.x[x], oy = row.y[x];
                if (!(ox > -0.5 && ox < static_cast<double>(width) - 0.5 && oy > -0.5 &&
                      oy < static_cast<double>(map.outputExtent().height()) - 0.5))
                    continue;
                const auto xx = static_cast<std::uint32_t>(ox + 0.5), yy = static_cast<std::uint32_t>(oy + 0.5);
                if (yy < first || yy >= end)
                    continue;
                const double sample = monoValue(input.pixels, sampleRow, x);
                if (!std::isfinite(sample))
                    continue;
                if (!addContribution(destination, static_cast<std::size_t>(yy - destination.firstRow) * width + xx,
                                     sample / scaleSquared, monoWeight, input.frameIndex, report))
                    return accumulationFailure(ErrorCode::InvalidData, "drizzle accumulation overflowed");
                continue;
            }
            if (simple && kernel == DrizzleKernel::Turbo)
            {
                const double ox = row.x[x], oy = row.y[x];
                std::uint32_t firstX = 0u, endX = 0u, firstY = 0u, endY = 0u;
                if (!rectangleRange(ox, halfDrop, 0u, width, firstX, endX) ||
                    !rectangleRange(oy, halfDrop, first, end, firstY, endY))
                    continue;
                const double sample = monoValue(input.pixels, sampleRow, x) / scaleSquared;
                if (!std::isfinite(sample))
                    continue;
                for (auto yy = firstY; yy < endY; ++yy)
                {
                    const double dy = yy - oy;
                    const double vertical =
                        (std::max)(0.0, (std::min)(halfDrop, dy + 0.5) - (std::max)(-halfDrop, dy - 0.5)) / dropArea;
                    for (auto xx = firstX; xx < endX; ++xx)
                    {
                        if (const auto stopped = checkpoint(); stopped.failed())
                            return stopped;
                        const double dx = xx - ox;
                        const double horizontal =
                            (std::max)(0.0, (std::min)(halfDrop, dx + 0.5) - (std::max)(-halfDrop, dx - 0.5));
                        const double weight = (horizontal * vertical) * monoWeight;
                        if (weight != 0.0 &&
                            !addContribution(destination,
                                             static_cast<std::size_t>(yy - destination.firstRow) * width + xx, sample,
                                             weight, input.frameIndex, report))
                            return accumulationFailure(ErrorCode::InvalidData, "drizzle accumulation overflowed");
                    }
                }
                continue;
            }
            DrizzleDrop drop;
            if (!prepareDrizzleDrop(model, map, row, x, y, drop))
                continue;
            const auto beginY = (std::max)(first, drop.yBegin), endY = (std::min)(end, drop.yEnd);
            if (beginY >= endY)
                continue;
            DrizzleValues values;
            if (simple)
            {
                const double sample = monoValue(input.pixels, sampleRow, x);
                if (!std::isfinite(sample))
                    continue;
                values.channelCount = 1u;
                values.samples[0] = sample / scaleSquared;
                values.weights[0] = monoWeight;
            }
            else if (const auto status =
                         gatherDrizzleValues(model, input, readers, x, y - input.firstRow, values, nullptr);
                     status.failed())
                return status;
            // Every non-Square drop fits this cache: the widest, Lanczos3 at the largest scale and
            // drop fraction, spans at most 24 columns. A wider rectangular Square drop takes the
            // general per-cell path instead.
            std::array<double, 28u> horizontal{};
            const bool rectangle = Optimized && model.request().parameters.kernel == DrizzleKernel::Square &&
                                   drop.rectangle && drop.xEnd - drop.xBegin <= horizontal.size();
            const bool separable =
                Optimized && (model.request().parameters.kernel != DrizzleKernel::Square || rectangle);
            if (separable)
                for (auto xx = drop.xBegin; xx < drop.xEnd; ++xx)
                {
                    if (rectangle)
                        horizontal[xx - drop.xBegin] =
                            (std::max)(0.0,
                                       (std::min)(static_cast<double>((std::max)(drop.corners[0].x, drop.corners[1].x)),
                                                  xx + 0.5) -
                                           (std::max)(static_cast<double>(
                                                          (std::min)(drop.corners[0].x, drop.corners[1].x)),
                                                      xx - 0.5));
                    else
                        horizontal[xx - drop.xBegin] = axisCoefficient(drop, xx - drop.x);
                }
            for (auto yy = beginY; yy < endY; ++yy)
            {
                double vertical = separable && !rectangle ? axisCoefficient(drop, yy - drop.y) : 0.0;
                if (rectangle)
                    vertical =
                        (std::max)(0.0,
                                   (std::min)(static_cast<double>((std::max)(drop.corners[0].y, drop.corners[2].y)),
                                              yy + 0.5) -
                                       (std::max)(static_cast<double>((std::min)(drop.corners[0].y, drop.corners[2].y)),
                                                  yy - 0.5)) /
                        drop.area;
                if (separable && model.request().parameters.kernel == DrizzleKernel::Turbo)
                    vertical /= drop.area;
                if (separable && model.request().parameters.kernel == DrizzleKernel::Gaussian)
                    vertical *= drop.gaussianNormalization;
                for (auto xx = drop.xBegin; xx < drop.xEnd; ++xx)
                {
                    if (const auto stopped = checkpoint(); stopped.failed())
                        return stopped;
                    const double coefficient = separable ? horizontal[xx - drop.xBegin] * vertical
                                                         : drizzleCoefficient(model, drop, table, xx, yy, !Optimized);
                    if (coefficient == 0.0 ||
                        ((table != nullptr || model.request().parameters.kernel == DrizzleKernel::Gaussian) &&
                         std::abs(coefficient) < 1e-10))
                        continue;
                    const auto offset = static_cast<std::uint64_t>(yy - destination.firstRow) * width + xx;
                    if (simple)
                    {
                        const double weight = coefficient * monoWeight;
                        if (weight != 0.0 &&
                            !addContribution(destination, offset, values.samples[0], weight, input.frameIndex, report))
                            return accumulationFailure(ErrorCode::InvalidData, "drizzle accumulation overflowed");
                        continue;
                    }
                    for (std::uint32_t channel = values.firstChannel;
                         channel < values.firstChannel + values.channelCount; ++channel)
                    {
                        if (values.weights[channel] == 0.0)
                            continue;
                        const auto index = channel * planeSize + offset;
                        const double weight = coefficient * values.weights[channel];
                        if (weight == 0.0)
                            continue;
                        if (!addContribution(destination, index, values.samples[channel], weight, input.frameIndex,
                                             report))
                            return accumulationFailure(ErrorCode::InvalidData, "drizzle accumulation overflowed");
                    }
                }
            }
        }
        execution.reportTrace(TraceEventKind::Counter, "drizzle", "accumulatedSourceRows", 1.0, "rows");
    }
    return execution.checkCancellation();
}

Result<DrizzleAccumulationReport> runAccumulation(const ExecutionContext& execution, const DrizzleModel& model,
                                                  const DrizzleInputBand& input, const DrizzlePixmapView& map,
                                                  const DrizzleAccumulatorBand& destination,
                                                  const DrizzleKernelTable* table, bool validateDestination)
{
    if (const auto status = checkArguments(execution, model, input, map, destination, table); status.failed())
        return status;
    const DrizzleReaders readers{readerFor(&input.pixels), readerFor(input.pixelWeights), readerFor(input.flatWeights)};
    DrizzleAccumulationReport report;
    const bool reference = execution.backend() == ComputeBackend::Reference;
    const bool simple = !reference && simpleMono(model, input);
    const double monoWeight =
        model.request().parameters.weightScale * model.request().parameters.channelWeights[0] * input.frameWeights[0];
    const double scaleSquared = model.request().parameters.scale * model.request().parameters.scale;
    if (simple && (!std::isfinite(monoWeight) || monoWeight <= 0.0))
        return accumulationFailure(ErrorCode::InvalidData, "effective weight is not representable");
    const auto rows = input.pixels.layout.extent().height();
    // The prior destination terms are validated by the same tasks as the source rows: a task over
    // source rows [begin, end) also takes the proportional share [outputOffset(begin),
    // outputOffset(end)) of the terms, so the shares partition them exactly.
    const auto outputOffset = [&](std::uint64_t part) {
        // part <= rows, and rows fits UInt32: neither product can overflow UInt64.
        return (destination.sums.size() / rows) * part + ((destination.sums.size() % rows) * part) / rows;
    };
    std::atomic<std::uint64_t> selected{0u}, excluded{0u}, nonFinite{0u};
    const auto validateRows = [&](TaskRange range) -> Status {
        DrizzleAccumulationReport local;
        for (auto y = range.begin; y < range.end; ++y)
        {
            const auto* row = input.pixels.samples.data() + y * input.pixels.layout.rowStrideBytes();
            for (std::uint32_t x = 0u; x < map.sourceExtent().width(); ++x)
            {
                if ((x % 4096u) == 0u)
                    if (const auto stopped = execution.checkCancellation(); stopped.failed())
                        return stopped;
                if (simple)
                {
                    const double sample = monoValue(input.pixels, row, x);
                    if (!std::isfinite(sample) &&
                        model.request().parameters.nonFinite == DrizzleNonFinitePolicy::Exclude)
                    {
                        ++local.excluded;
                        ++local.nonFinite;
                        continue;
                    }
                    if (!std::isfinite(sample / scaleSquared * monoWeight))
                        return accumulationFailure(ErrorCode::InvalidData,
                                                   "source sample or weighted flux is not finite");
                    ++local.selected;
                }
                else
                {
                    DrizzleValues values;
                    if (const auto status = gatherDrizzleValues(model, input, readers, x, static_cast<std::uint32_t>(y),
                                                                values, &local);
                        status.failed())
                        return status;
                }
            }
        }
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        for (auto i = outputOffset(range.begin), end = validateDestination ? outputOffset(range.end) : i; i < end; ++i)
        {
            if ((i % 4096u) == 0u)
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
            if (!std::isfinite(destination.sums[i]) || !std::isfinite(destination.weightSums[i]) ||
                (!destination.coverage.empty() &&
                 (destination.coverage[i] > input.frameIndex + 1u || destination.lastFrame[i] > input.frameIndex + 1u ||
                  (destination.coverage[i] == 0u) != (destination.lastFrame[i] == 0u))))
                return accumulationFailure(ErrorCode::InvalidData,
                                           "accumulator terms are non-finite or frame coverage is out of order");
        }
        selected.fetch_add(local.selected, std::memory_order_relaxed);
        excluded.fetch_add(local.excluded, std::memory_order_relaxed);
        nonFinite.fetch_add(local.nonFinite, std::memory_order_relaxed);
        return Status::success();
    };
    const auto validation = reference ? validateRows({0u, rows})
                                      : parallelFor(execution, rows,
                                                    {static_cast<std::uint64_t>(map.sourceExtent().width()) * 16u,
                                                     static_cast<std::uint64_t>(map.sourceExtent().width()) * 32u},
                                                    validateRows);
    if (validation.failed())
        return validation;
    report.selected = selected.load(std::memory_order_relaxed);
    report.excluded = excluded.load(std::memory_order_relaxed);
    report.nonFinite = nonFinite.load(std::memory_order_relaxed);
    if (execution.backend() == ComputeBackend::Reference)
    {
        report.workers = 1u;
        if (const auto status =
                scatter<false>(execution, model, input, readers, map, destination, table, destination.firstRow,
                               destination.firstRow + destination.rowCount, report);
            status.failed())
            return status;
        return report;
    }
    // Partition the rows this source band can actually reach. Partitioning the entire canvas
    // leaves nearly every worker idle when a narrow input band touches just one old strip.
    double low = destination.firstRow, high = destination.firstRow + destination.rowCount - 1u;
    if (rows < destination.rowCount)
    {
        low = std::numeric_limits<double>::infinity();
        high = -low;
        for (auto row = map.coreRowBegin(); row < map.coreRowEnd(); ++row)
        {
            const auto bounds = rowSupport(execution, model, map, row);
            low = (std::min)(low, bounds.first);
            high = (std::max)(high, bounds.second);
        }
    }
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    report.implementation = DrizzleAccumulationImplementation::OwnedRows;
    const double bandBegin = destination.firstRow, bandEnd = destination.firstRow + destination.rowCount;
    if (low > high || high < bandBegin || low >= bandEnd)
        return report;
    const auto activeBegin = static_cast<std::uint32_t>(std::clamp(std::floor(low), bandBegin, bandEnd));
    const auto activeEnd = static_cast<std::uint32_t>(std::clamp(std::ceil(high) + 1.0, bandBegin, bandEnd));
    const auto activeRows = activeEnd - activeBegin;
    const auto strips = (std::min)(execution.admittedConcurrency(), activeRows);
    const TaskCost cost{65536u, 65536u};
    report.workers = plannedWorkerCount(execution, strips, cost);
    std::atomic<std::uint64_t> contributions{0u}, covered{0u};
    std::atomic<DrizzleAccumulationImplementation> implementation{DrizzleAccumulationImplementation::Reference};
    const auto body = [&](TaskRange range) -> Status {
        for (auto strip = range.begin; strip < range.end; ++strip)
        {
            const auto first = activeBegin + static_cast<std::uint32_t>(strip * activeRows / strips);
            const auto end = activeBegin + static_cast<std::uint32_t>((strip + 1u) * activeRows / strips);
            DrizzleAccumulationReport local;
            if (const auto status =
                    scatter<true>(execution, model, input, readers, map, destination, table, first, end, local);
                status.failed())
                return status;
            contributions.fetch_add(local.contributions, std::memory_order_relaxed);
            covered.fetch_add(local.covered, std::memory_order_relaxed);
            implementation.store(local.implementation, std::memory_order_relaxed);
        }
        return Status::success();
    };
    if (const auto status = parallelFor(execution, strips, cost, body); status.failed())
        return status;
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    report.contributions = contributions.load(std::memory_order_relaxed);
    report.covered = covered.load(std::memory_order_relaxed);
    report.implementation = implementation.load(std::memory_order_relaxed);
    return report;
}

} // namespace

Result<DrizzleAccumulationReport> accumulateDrizzleBand(const ExecutionContext& execution, const DrizzleModel& model,
                                                        const DrizzleInputBand& input, const DrizzlePixmapView& map,
                                                        const DrizzleAccumulatorBand& destination,
                                                        const DrizzleKernelTable* table)
{
    return runAccumulation(execution, model, input, map, destination, table, true);
}

Result<DrizzleAccumulationReport>
drizzle_detail::accumulateOwnedDrizzleBand(const ExecutionContext& execution, const DrizzleModel& model,
                                           const DrizzleInputBand& input, const DrizzlePixmapView& map,
                                           const DrizzleAccumulatorBand& destination, const DrizzleKernelTable* table)
{
    return runAccumulation(execution, model, input, map, destination, table, false);
}

} // namespace an
