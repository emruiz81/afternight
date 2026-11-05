// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle executor: argument checks, the recomputed plan, the working
// set, and the ordered loop over destination bands, frames and source bands.
//
// Everything the loop touches is allocated before it starts, at the plan's own byte counts: the
// output image and mask, the optional maps, the frame records, the accumulators and coverage
// terms, the Lanczos table, and the read slots a decoder is lent. Each lives in an owning object
// whose scope returns it, so an early return releases everything acquired so far and hands nothing
// over. For each destination band the terms are zeroed and every frame is visited in order, with
// one geometry plan per frame, a dense map once or a bounded map per source band, and one
// whole-frame read or one read per band; the band is finalized before the next begins. Once the
// output exists, `ReportOnExit` copies the report to the caller's diagnostics on every return.
//
// The progress total is fixed before the loop: one unit per source band of every frame in every
// destination band, one per finalized destination band, one for a peak-normalized sum's
// normalization and one for the handover. Map generation, reads and accumulation run inside an
// empty range at the current count, so their own reports hold the bar where it is.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>

#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/drizzle/DrizzleExecutionInternal.h"
#include "algorithms/drizzle/DrizzleAccumulationInternal.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <type_traits>

namespace an
{
namespace
{
using drizzle_detail::ExecutionBuffer;
using drizzle_detail::executionFailure;

class ReportOnExit
{
public:
    ReportOnExit(DrizzleExecutionReport& report, DrizzleExecutionReport* destination)
        : m_report(report), m_destination(destination)
    {
    }
    ~ReportOnExit()
    {
        if (m_destination != nullptr)
            *m_destination = m_report;
    }
    ReportOnExit(const ReportOnExit&) = delete;
    ReportOnExit& operator=(const ReportOnExit&) = delete;

private:
    DrizzleExecutionReport& m_report;
    DrizzleExecutionReport* m_destination;
};

std::uint64_t rowBytes(const ImageDescriptor& descriptor) noexcept
{
    return static_cast<std::uint64_t>(descriptor.extent().width()) * descriptor.extent().channelCount() *
           sampleTypeSizeBytes(descriptor.sampleType());
}

Result<ImageStorageBytes> outputStorage(const EngineContext& engine, const ImageExtent& extent, std::uint32_t bytes)
{
    return ImageStorageBytes::create(engine, ImageLayoutRequest{extent, bytes, 1u, extent.channelCount(), 1u},
                                     StorageInitialization::Zeroed);
}

Status checkFrames(const DrizzleModel& model, std::span<const DrizzleFrameParameters> frames)
{
    if (frames.size() != model.request().frameCount)
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "drizzle needs exactly one geometry record per frame");
    const auto& output = model.outputDescriptor().extent();
    const auto& source = model.request().input.extent();
    if (model.request().parameters.kernel == DrizzleKernel::Square && (source.width() < 2u || source.height() < 2u))
        return executionFailure(ErrorCode::InvalidData, "square drizzle requires at least two source rows and columns");
    for (const auto& frame : frames)
    {
        if (frame.geometry.sourceExtent() != source || frame.geometry.outputExtent() != output ||
            frame.geometry.scale() != model.request().parameters.scale)
            return executionFailure(ErrorCode::IncompatibleArguments, "frame geometry differs from the drizzle model");
        for (std::uint32_t channel = 0u; channel < 4u; ++channel)
            if (!std::isfinite(frame.weights[channel]) || frame.weights[channel] <= 0.0 ||
                (channel >= output.channelCount() && frame.weights[channel] != 1.0))
                return executionFailure(ErrorCode::InvalidArgument,
                                        "frame weights must be positive finite with unused factors equal to one");
    }
    return Status::success();
}

Result<DrizzleFrameData> readBand(const ExecutionContext& execution, const DrizzleModel& model,
                                  DrizzleFrameSource& source, std::uint32_t frame, std::uint32_t first,
                                  std::uint32_t rows, const DrizzleReadBuffers& buffers, DrizzleExecutionReport& report)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    // Catch the callback itself, including a third-party decoder's exception, before it can
    // unwind through an engine operation. The original typed failure remains the nested cause.
    constexpr ExternalBoundary kBoundary{"DrizzleFrameSource", "an::executeDrizzle", ErrorDomain::Algorithms};
    auto read = evaluateAcrossBoundary(kBoundary, [&] { return source.read(execution, frame, first, rows, buffers); });
    if (!read.hasValue())
        return read.status();
    auto data = std::move(*read);
    if (!data.hasValue())
        return Status::failure(data.status().code(),
                               StatusDetail{.message = "drizzle source read failed",
                                            .operation = "an::executeDrizzle",
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "frameIndex"),
                                                        StatusContextField::ofCount(ContextKey::Completed, frame),
                                                        StatusContextField::ofCount(ContextKey::Requested, first)}},
                               data.status());
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    const auto& extent = data->pixels.layout.extent();
    if (extent.height() != rows || extent.width() != model.request().input.extent().width() ||
        extent.channelCount() != model.request().input.extent().channelCount() ||
        data->pixels.sampleType != model.request().input.sampleType())
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "reader returned a different representation or band extent");
    ++report.readRequests;
    report.inputBytesRead += report.plan.io.frameBytes / model.request().input.extent().height() * rows;
    return data;
}

Result<DrizzlePixmapView> coreView(const DrizzlePixmap& map, std::uint32_t first, std::uint32_t rows)
{
    auto view = map.view();
    if (!view.hasValue())
        return view.status();
    const auto coordinates = view->row(view->storedRowBegin());
    if (!coordinates.hasValue())
        return coordinates.status();
    const auto count =
        static_cast<std::size_t>(view->sourceExtent().width()) * (view->storedRowEnd() - view->storedRowBegin());
    return DrizzlePixmapView::create(view->sourceExtent(), view->outputExtent(), view->storedRowBegin(),
                                     view->storedRowEnd(), first, first + rows, {coordinates->x.data(), count},
                                     {coordinates->y.data(), count}, view->rowBounds());
}

void addGeometry(DrizzleExecutionReport& report, const DrizzlePixmapReport& next) noexcept
{
    report.geometry.mapped += next.mapped;
    report.geometry.unmapped += next.unmapped;
    report.geometry.sipEvaluations += next.sipEvaluations;
    report.geometry.polynomialEvaluations += next.polynomialEvaluations;
    report.geometry.workers = next.workers;
    report.geometry.implementation = next.implementation;
}
} // namespace

Result<DrizzleResult> executeDrizzle(const EngineContext& engine, const ExecutionContext& execution,
                                     const DrizzleModel& model, DrizzleFrameSource& source,
                                     std::span<const DrizzleFrameParameters> frames,
                                     const DrizzleExecutionRequest& request)
{
    if (request.diagnostics != nullptr)
        *request.diagnostics = {};
    if (!execution.valid() || !engine.valid())
        return executionFailure(ErrorCode::NotInitialized,
                                "drizzle requires initialized engine and execution contexts");
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (engine.id() != execution.engineContextId())
        return executionFailure(ErrorCode::IncompatibleArguments,
                                "drizzle engine and execution must share one memory owner");
    if ((request.artifact.runId == 0u) != (request.artifact.artifactId == 0u))
        return executionFailure(ErrorCode::InvalidArgument,
                                "artifact and run identities must both be stated or both anonymous");
    if (const auto checked = checkFrames(model, frames); checked.failed())
        return checked;
    auto planning = request.planning;
    planning.access = source.access();
    auto planned = planDrizzle(execution, model, planning);
    if (!planned.hasValue())
        return planned.status();
    const auto& plan = *planned;
    const auto& input = model.request();
    const auto& output = model.outputDescriptor();
    const bool peakSum = input.output.combination == DrizzleCombination::PeakNormalizedSum;
    const bool dense = planning.mapStorage == DrizzleMapStorage::Dense;
    const auto total = CheckedSize{plan.sourceBlockCount} * input.frameCount * plan.destinationTileCount +
                       plan.destinationTileCount + 1u + static_cast<std::uint64_t>(peakSum);
    if (!total.isValid())
        return executionFailure(ErrorCode::LimitExceeded, "drizzle progress count overflows");
    const std::uint64_t progressTotal = total.valueOr(0u);
    std::uint64_t completed = 0u;
    execution.reportProgress(0u, progressTotal, "Preparing drizzle");

    auto image = outputStorage(engine, output.extent(), sampleTypeSizeBytes(output.sampleType()));
    if (!image.hasValue())
        return image.status();
    auto mask = ValidityMask::create(engine, output.extent());
    if (!mask.hasValue())
        return mask.status();
    DrizzleResult result{std::move(*image), output, std::move(*mask), {}, {}, request.artifact};
    const ReportOnExit reportOnExit(result.report, request.diagnostics);
    result.report.plan = plan;
    if (plan.collectMaximumWeight)
        result.report.maximumWeight = 0.0;
    for (const auto pair : {std::pair{input.output.weightMap, &result.weightMap},
                            std::pair{input.output.coverageMap, &result.coverageMap}})
    {
        if (!pair.first)
            continue;
        auto storage = outputStorage(engine, output.extent(), 4u);
        if (!storage.hasValue())
            return storage.status();
        *pair.second = std::move(*storage);
    }
    auto& memory = *execution.memoryResource();
    ExecutionBuffer frameStorage, sums, weights, counts, last, kernel, finalization;
    if (const auto status = finalization.allocate(memory, plan.memory.finalizationBytes); status.failed())
        return status;
    std::span<double> numerators;
    if (peakSum)
        numerators = output.sampleType() == SampleType::Float64
                         ? std::span{static_cast<double*>(result.image.data()),
                                     static_cast<std::size_t>(output.extent().sampleCount())}
                         : finalization.values<double>();
    static_assert(std::is_trivially_destructible_v<DrizzleFrameParameters>);
    static_assert(sizeof(DrizzleFrameParameters) == sizeof(DrizzleMapDescriptor) + 4u * sizeof(double));
    if (const auto status = frameStorage.allocate(memory, frames.size_bytes()); status.failed())
        return status;
    auto ownedFrames = frameStorage.values<DrizzleFrameParameters>();
    for (std::size_t i = 0u; i < frames.size(); ++i)
        std::construct_at(&ownedFrames[i], frames[i]);
    for (const auto pair :
         {std::pair{&sums, plan.memory.accumulatorBytes / 2u}, std::pair{&weights, plan.memory.accumulatorBytes / 2u},
          std::pair{&counts, plan.memory.coverageStateBytes / 2u},
          std::pair{&last, plan.memory.coverageStateBytes / 2u}, std::pair{&kernel, plan.memory.kernelTableBytes}})
        if (const auto status = pair.first->allocate(memory, pair.second); status.failed())
            return status;
    DrizzleKernelTable* table = nullptr;
    if (plan.memory.kernelTableBytes != 0u)
    {
        auto prepared = DrizzleKernelTable::create(input.parameters.kernel);
        if (!prepared.hasValue())
            return prepared.status();
        table = std::construct_at(kernel.values<DrizzleKernelTable>().data(), *prepared);
    }
    std::array<ExecutionBuffer, 5u> reads;
    const auto readRows =
        planning.access == DrizzleSourceAccess::WholeFrame ? input.input.extent().height() : plan.sourceRows;
    const std::uint64_t slots = planning.concurrentFrameReads;
    const std::array<std::uint64_t, 5u> sizes{
        rowBytes(input.input) * readRows,
        input.validity.needsMask()
            ? packedMaskRowBytes(input.input.extent().width()) * input.input.extent().channelCount() * readRows
            : 0u,
        input.pixelWeights ? rowBytes(*input.pixelWeights) * readRows : 0u,
        input.flatWeights ? rowBytes(*input.flatWeights) * readRows : 0u, planning.sourceWorkspaceBytes};
    for (std::size_t i = 0u; i < sizes.size(); ++i)
        if (const auto status = reads[i].allocate(memory, sizes[i] * slots); status.failed())
            return status;
    DrizzleReadBuffers buffers{reads[0].bytes().first(sizes[0]), reads[1].bytes().first(sizes[1]),
                               reads[2].bytes().first(sizes[2]), reads[3].bytes().first(sizes[3]),
                               reads[4].bytes().first(sizes[4])};

    // Advancing by the consumed tail avoids wraparound at caller-raised UInt32 geometry limits.
    for (std::uint32_t destinationRow = 0u; destinationRow < output.extent().height();)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const auto rows = (std::min)(plan.destinationRows, output.extent().height() - destinationRow);
        const auto elements = static_cast<std::size_t>(output.extent().width()) * rows * output.extent().channelCount();
        DrizzleAccumulatorBand destination{destinationRow, rows, sums.values<double>().first(elements),
                                           weights.values<double>().first(elements)};
        if (input.output.coverageMap)
        {
            destination.coverage = counts.values<std::uint32_t>().first(elements);
            destination.lastFrame = last.values<std::uint32_t>().first(elements);
            std::fill(destination.coverage.begin(), destination.coverage.end(), 0u);
            std::fill(destination.lastFrame.begin(), destination.lastFrame.end(), 0u);
        }
        std::fill(destination.sums.begin(), destination.sums.end(), 0.0);
        std::fill(destination.weightSums.begin(), destination.weightSums.end(), 0.0);
        for (std::uint32_t frame = 0u; frame < input.frameCount; ++frame)
        {
            auto geometry = DrizzleGeometryPlan::create(execution, ownedFrames[frame].geometry);
            if (!geometry.hasValue())
                return geometry.status();
            DrizzlePixmap denseMap;
            if (dense)
            {
                const ProgressRange range(execution, completed, completed, progressTotal);
                auto generated = generateDrizzlePixmap(execution, *geometry, {.storage = DrizzleMapStorage::Dense});
                if (!generated.hasValue())
                    return generated.status();
                addGeometry(result.report, generated->report());
                denseMap = std::move(*generated);
            }
            std::optional<DrizzleFrameData> whole;
            if (planning.access == DrizzleSourceAccess::WholeFrame)
            {
                const ProgressRange range(execution, completed, completed, progressTotal);
                auto read = readBand(execution, model, source, frame, 0u, input.input.extent().height(), buffers,
                                     result.report);
                if (!read.hasValue())
                    return read.status();
                whole = *read;
            }
            for (std::uint32_t sourceRow = 0u; sourceRow < input.input.extent().height();)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const auto sourceRows = (std::min)(plan.sourceRows, input.input.extent().height() - sourceRow);
                {
                    const ProgressRange range(execution, completed, completed, progressTotal);
                    auto data = whole ? drizzleSourceRows(*whole, sourceRow, sourceRows)
                                      : readBand(execution, model, source, frame, sourceRow, sourceRows, buffers,
                                                 result.report);
                    if (!data.hasValue())
                        return data.status();
                    DrizzlePixmap boundedMap;
                    if (!dense)
                    {
                        auto generated = generateDrizzlePixmap(execution, *geometry,
                                                               {.storage = DrizzleMapStorage::BoundedRows,
                                                                .firstRow = sourceRow,
                                                                .rowCount = sourceRows,
                                                                .haloRows = plan.mapHaloRows});
                        if (!generated.hasValue())
                            return generated.status();
                        addGeometry(result.report, generated->report());
                        boundedMap = std::move(*generated);
                    }
                    auto map = coreView(dense ? denseMap : boundedMap, sourceRow, sourceRows);
                    if (!map.hasValue())
                        return map.status();
                    const DrizzleInputBand band{data->pixels,
                                                sourceRow,
                                                frame,
                                                ownedFrames[frame].weights,
                                                data->mask ? &*data->mask : nullptr,
                                                data->pixelWeights ? &*data->pixelWeights : nullptr,
                                                data->flatWeights ? &*data->flatWeights : nullptr};
                    auto accumulated =
                        drizzle_detail::accumulateOwnedDrizzleBand(execution, model, band, *map, destination, table);
                    if (!accumulated.hasValue())
                        return accumulated.status();
                    result.report.implementation = accumulated->implementation;
                    result.report.contributions += accumulated->contributions;
                    if (destinationRow == 0u)
                    {
                        result.report.selected += accumulated->selected;
                        result.report.excluded += accumulated->excluded;
                        result.report.nonFinite += accumulated->nonFinite;
                    }
                }
                execution.reportProgress(++completed, progressTotal, "Accumulating drizzle");
                sourceRow += sourceRows;
            }
        }
        if (const auto finalized = drizzle_detail::finalizeBand(execution, model, destination, result, numerators);
            finalized.failed())
            return finalized;
        execution.reportProgress(++completed, progressTotal, "Finalizing drizzle");
        destinationRow += rows;
    }
    if (peakSum)
    {
        if (const auto normalized = drizzle_detail::normalizeSum(execution, numerators, result); normalized.failed())
            return normalized;
        execution.reportProgress(++completed, progressTotal, "Normalizing drizzle sum");
    }
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    execution.reportProgress(++completed, progressTotal, "Drizzle complete");
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    result.report.outputBytes = result.image.capacityBytes() + result.mask.capacityBytes() +
                                result.weightMap.capacityBytes() + result.coverageMap.capacityBytes();
    return result;
}
} // namespace an
