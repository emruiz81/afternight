// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - local normalization: the apply stage and its transition.
//
// The frame is written a row at a time. For each row the contributing node rows of every grid are
// mixed once into per-column values, and the row body evaluates each cell's polynomial per sample
// and transforms it. The body is the text in `kernels/reference/LocalNormalizationRow.inl`; it is
// compiled here as the scalar authority and again, per instruction set, in the CPU kernel tables,
// so the two cannot store different bits.

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/normalization/LocalNormalizationInternal.h"
#include "kernels/reference/AlgorithmKernels.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <sstream>
#include <string>

namespace an
{

namespace
{

// The shared row body: the scalar authority in this translation unit.
#include "kernels/reference/LocalNormalizationRow.inl"

using local_normalization_detail::failure;
using local_normalization_detail::parameterFailure;

constexpr const char* kApplyOperation = "an::applyLocalNormalization";
constexpr const char* kTransitionOperation = "an::localNormalizationTransition";

[[nodiscard]] bool pointerIsAligned(const void* pointer, SampleType sampleType) noexcept
{
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    return sampleType == SampleType::Float64 ? address % alignof(double) == 0u : address % alignof(float) == 0u;
}

[[nodiscard]] bool rangesOverlap(const void* left, std::uint64_t leftBytes, const void* right,
                                 std::uint64_t rightBytes) noexcept
{
    const std::uintptr_t leftBegin = reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightBegin = reinterpret_cast<std::uintptr_t>(right);
    return leftBegin < rightBegin + static_cast<std::uintptr_t>(rightBytes) &&
           rightBegin < leftBegin + static_cast<std::uintptr_t>(leftBytes);
}

[[nodiscard]] bool sameArrangement(const ImageLayout& left, const ImageLayout& right) noexcept
{
    return left.samplesPerPixel() == right.samplesPerPixel() && left.planeCount() == right.planeCount();
}

[[nodiscard]] bool sameStrides(const ImageLayout& left, const ImageLayout& right) noexcept
{
    return sameArrangement(left, right) && left.rowStrideBytes() == right.rowStrideBytes() &&
           left.planeStrideBytes() == right.planeStrideBytes();
}

/// Rounds a byte count up to the engine's buffer alignment.
[[nodiscard]] std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) & ~(kDefaultBufferAlignmentBytes - 1u);
}

/// Checks a request against the shape it names.
[[nodiscard]] Status checkRequest(const LocalNormalizationApplyRequest& request, const char* operation)
{
    const Status shapeReady = checkLocalNormalizationGridShape(request.shape);
    if (shapeReady.failed())
    {
        return shapeReady;
    }
    if (!isLocalGridInterpolation(request.interpolation))
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "the interpolation is not one this build implements", "interpolation");
    }
    const std::uint64_t valueCount = request.shape.valueCount();
    if (request.targetBackground.size() != valueCount || request.referenceBackground.size() != valueCount ||
        (!request.scaleGrid.empty() && request.scaleGrid.size() != valueCount))
    {
        return failure(operation, ErrorCode::IncompatibleArguments, "a grid does not hold exactly the shape's values");
    }
    if (request.scale.size() != request.shape.channelCount)
    {
        return failure(operation, ErrorCode::IncompatibleArguments, "the scale does not hold one value per channel");
    }
    if (!local_normalization_detail::allFinite(request.targetBackground) ||
        !local_normalization_detail::allFinite(request.referenceBackground))
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument, "a background grid node is not finite",
                                "targetBackground");
    }
    for (const double scale : request.scale)
    {
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return parameterFailure(operation, ErrorCode::InvalidArgument, "a scale is finite and positive", "scale");
        }
    }
    for (const double scale : request.scaleGrid)
    {
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return parameterFailure(operation, ErrorCode::InvalidArgument, "a scale grid node is finite and positive",
                                    "scaleGrid");
        }
    }
    return Status::success();
}

/// Mixes every grid of one channel for one row into the worker's scratch and names the result.
[[nodiscard]] LocalNormalizationRunParameters prepareRow(const LocalNormalizationApplyRequest& request,
                                                         const LocalNormalizationRowScratch& scratch,
                                                         const double* fractions, const std::uint32_t* cellStarts,
                                                         std::uint32_t channel, std::uint32_t y,
                                                         std::uint32_t sampleStride, ValidityRule rule) noexcept
{
    const LocalNormalizationGridShape& shape = request.shape;
    const std::uint64_t planeOffset = static_cast<std::uint64_t>(channel) * shape.nodeCount();
    return localNormalizationPrepareRow(
        request.referenceBackground.data() + planeOffset, request.targetBackground.data() + planeOffset,
        request.scaleGrid.empty() ? nullptr : request.scaleGrid.data() + planeOffset, shape.nodesPerAxis(),
        shape.gridSize, shape.height, y, request.interpolation == LocalGridInterpolation::Bicubic,
        request.scale[channel], fractions, cellStarts, sampleStride, rule, scratch);
}

/// The scalar authority for a packed mask: the row body's arithmetic with the mask read per sample.
template <typename SampleT>
[[nodiscard]] LocalNormalizationRunOutcome maskedRun(const NormalizationFrameView& source, SampleT* destination,
                                                     std::uint32_t y, std::uint32_t channel,
                                                     const LocalNormalizationRunParameters& parameters) noexcept
{
    LocalNormalizationRunOutcome outcome{};
    const bool zeroRule = parameters.rule == ValidityRule::ZeroIsNoData;
    const bool bicubic = parameters.bicubic;
    const bool scaled = parameters.scale.mixed != nullptr;
    const std::uint64_t stride = parameters.sampleStride;
    for (std::uint32_t cell = 0u; cell <= parameters.gridSize; ++cell)
    {
        const std::uint32_t first = parameters.cellStarts[cell];
        const std::uint32_t last = parameters.cellStarts[cell + 1u];
        if (first >= last)
        {
            continue;
        }
        const LocalNormalizationCell reference =
            localNormalizationCellOf(parameters.reference, cell, parameters.gridSize, bicubic);
        const LocalNormalizationCell target =
            localNormalizationCellOf(parameters.target, cell, parameters.gridSize, bicubic);
        const LocalNormalizationCell scaleCell =
            scaled ? localNormalizationCellOf(parameters.scale, cell, parameters.gridSize, bicubic)
                   : LocalNormalizationCell{.a3 = parameters.uniformScale};
        for (std::uint32_t x = first; x < last; ++x)
        {
            const SampleT value = *local_normalization_detail::sampleAddress<SampleT>(source, x, y, channel);
            SampleT* const out = destination + static_cast<std::uint64_t>(x) * stride;
            if (!local_normalization_detail::sampleParticipates<SampleT>(source, value, x, y, channel))
            {
                *out = value;
                ++outcome.samplesPreserved;
                continue;
            }
            const double t = parameters.fractions[x];
            const double referenceLevel = localNormalizationEvaluateCell(reference, t, bicubic);
            const double targetLevel = localNormalizationEvaluateCell(target, t, bicubic);
            const double scale = scaled ? localNormalizationEvaluateCell(scaleCell, t, bicubic) : scaleCell.a3;
            *out = localNormalizationTransform<SampleT>(value, targetLevel, referenceLevel, scale, zeroRule,
                                                        outcome.noDataCollisions);
            ++outcome.samplesTransformed;
        }
    }
    return outcome;
}

/// Runs every row through one row function, or the masked authority when `run` is null.
template <typename SampleT>
[[nodiscard]] Result<LocalNormalizationApplyResult>
runRows(const ExecutionContext& execution, const LocalNormalizationApplyRequest& request,
        const NormalizationFrameView& source, void* destination, const ImageLayout& destinationLayout,
        NormalizationExecutionPath path, const BackendSelection& backend,
        LocalNormalizationRunOutcome (*run)(const SampleT*, SampleT*, const LocalNormalizationRunParameters&) noexcept)
{
    const LocalNormalizationGridShape& shape = request.shape;
    const ImageExtent& extent = source.layout().extent();
    const bool planar = source.layout().samplesPerPixel() == 1u;
    const std::uint32_t channels = extent.channelCount();
    const std::uint64_t taskCount = static_cast<std::uint64_t>(extent.height()) * (planar ? channels : 1u);
    const TaskCost cost{
        .bytesPerItem = static_cast<std::uint64_t>(extent.width()) * (planar ? 1u : channels) * sizeof(SampleT) * 2u,
        .operationsPerItem = static_cast<std::uint64_t>(extent.width()) * (planar ? 1u : channels) * 24u};
    const std::uint32_t slots = (std::max)(plannedWorkerCount(execution, taskCount, cost), 1u);
    const std::uint32_t nodes = shape.nodesPerAxis();
    const std::uint64_t fractionBytes = alignedBytes(static_cast<std::uint64_t>(extent.width()) * sizeof(double));
    const std::uint64_t cellStartBytes =
        alignedBytes(static_cast<std::uint64_t>(shape.gridSize + 2u) * sizeof(std::uint32_t));
    const std::uint64_t perWorkerBytes = alignedBytes(localNormalizationRowScratchDoubles(nodes) * sizeof(double));
    const std::uint64_t totalBytes = fractionBytes + cellStartBytes + perWorkerBytes * slots;
    MemoryResource* const memory = execution.memoryResource();
    Result<void*> taken = memory->allocate(totalBytes, kDefaultBufferAlignmentBytes);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    auto* const block = static_cast<std::byte*>(*taken);
    auto* const fractions = reinterpret_cast<double*>(block);
    auto* const cellStarts = reinterpret_cast<std::uint32_t*>(block + fractionBytes);
    std::byte* const workers = block + fractionBytes + cellStartBytes;

    // Every column's cell and fraction once, and the first column of every cell.
    localNormalizationPrepareColumns(extent.width(), shape.gridSize, fractions, cellStarts);

    const ValidityRule rule = source.validity().descriptor.rule();
    const std::uint32_t sampleStride = source.layout().samplesPerPixel();
    std::atomic<std::uint64_t> transformed{0u};
    std::atomic<std::uint64_t> preserved{0u};
    std::atomic<std::uint64_t> collisions{0u};
    std::atomic<std::uint64_t> tasksDone{0u};
    const Status completed = parallelForWorkers(execution, taskCount, cost, [&](WorkerTask task) {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        const LocalNormalizationRowScratch scratch = localNormalizationRowScratchOver(
            reinterpret_cast<double*>(workers + perWorkerBytes * task.workerSlot), nodes);
        std::uint64_t localTransformed = 0u;
        std::uint64_t localPreserved = 0u;
        std::uint64_t localCollisions = 0u;
        for (std::uint64_t item = task.range.begin; item < task.range.end; ++item)
        {
            const std::uint32_t firstChannel = planar ? static_cast<std::uint32_t>(item / extent.height()) : 0u;
            const std::uint32_t lastChannel = planar ? firstChannel + 1u : channels;
            const std::uint32_t y =
                planar ? static_cast<std::uint32_t>(item % extent.height()) : static_cast<std::uint32_t>(item);
            for (std::uint32_t channel = firstChannel; channel < lastChannel; ++channel)
            {
                const LocalNormalizationRunParameters parameters =
                    prepareRow(request, scratch, fractions, cellStarts, channel, y, sampleStride, rule);
                SampleT* const destinationRow =
                    local_normalization_detail::sampleAddress<SampleT>(destination, destinationLayout, 0u, y, channel);
                const LocalNormalizationRunOutcome row =
                    run != nullptr ? run(local_normalization_detail::sampleAddress<SampleT>(source, 0u, y, channel),
                                         destinationRow, parameters)
                                   : maskedRun<SampleT>(source, destinationRow, y, channel, parameters);
                localTransformed += row.samplesTransformed;
                localPreserved += row.samplesPreserved;
                localCollisions += row.noDataCollisions;
            }
        }
        transformed.fetch_add(localTransformed, std::memory_order_relaxed);
        preserved.fetch_add(localPreserved, std::memory_order_relaxed);
        collisions.fetch_add(localCollisions, std::memory_order_relaxed);
        execution.reportProgress(tasksDone.fetch_add(task.range.size(), std::memory_order_relaxed) + task.range.size(),
                                 taskCount, "applying local normalization");
        return Status::success();
    });
    memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
    if (completed.failed())
    {
        return completed;
    }
    const std::uint64_t written =
        transformed.load(std::memory_order_relaxed) + preserved.load(std::memory_order_relaxed);
    return LocalNormalizationApplyResult{.backend = backend,
                                         .path = path,
                                         .samplesWritten = written,
                                         .samplesTransformed = transformed.load(std::memory_order_relaxed),
                                         .samplesPreserved = preserved.load(std::memory_order_relaxed),
                                         .noDataCollisions = collisions.load(std::memory_order_relaxed),
                                         .plannedWorkers = slots,
                                         .imagePasses = 1u,
                                         .workingBytes = totalBytes};
}

/// The reference selection every scalar run reports.
[[nodiscard]] BackendSelection referenceSelection() noexcept
{
    return BackendSelection{.backend = ComputeBackend::Reference, .instructionSet = InstructionSet::Scalar};
}

} // namespace

Result<LocalNormalizationApplyResult> applyLocalNormalization(const ExecutionContext& execution,
                                                              const LocalNormalizationApplyRequest& request,
                                                              const NormalizationFrameView& source, void* destination,
                                                              std::uint64_t destinationBytes,
                                                              const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return failure(kApplyOperation, ErrorCode::NotInitialized,
                       "local normalization needs an execution holding an engine-context lease");
    }
    if (!source.valid())
    {
        return parameterFailure(kApplyOperation, ErrorCode::InvalidArgument,
                                "local normalization needs a checked source frame", "source");
    }
    const Status requestReady = checkRequest(request, kApplyOperation);
    if (requestReady.failed())
    {
        return requestReady;
    }
    const ImageExtent& extent = source.layout().extent();
    if (extent.width() != request.shape.width || extent.height() != request.shape.height ||
        extent.channelCount() != request.shape.channelCount)
    {
        return failure(kApplyOperation, ErrorCode::IncompatibleArguments,
                       "the frame does not have the grids' extent and channel count");
    }
    if (source.validity().descriptor.rule() == ValidityRule::DomainRange)
    {
        return failure(kApplyOperation, ErrorCode::Unsupported,
                       "a stated value range describes the untransformed samples; restate it after normalizing");
    }
    const SampleType sampleType = source.descriptor().sampleType();
    if (destination == nullptr || !pointerIsAligned(destination, sampleType))
    {
        return parameterFailure(kApplyOperation, ErrorCode::InvalidArgument,
                                "local normalization needs aligned destination storage", "destination");
    }
    if (destinationLayout.extent() != extent ||
        destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(sampleType) ||
        storageArrangementOf(destinationLayout) == StorageArrangement::SemiPlanar ||
        !sameArrangement(source.layout(), destinationLayout))
    {
        return failure(kApplyOperation, ErrorCode::IncompatibleArguments,
                       "the destination layout does not describe the source frame in its arrangement");
    }
    if (destinationBytes < destinationLayout.totalBytes())
    {
        return parameterFailure(kApplyOperation, ErrorCode::InvalidArgument,
                                "the destination buffer does not cover every byte named by its layout",
                                "destinationBytes");
    }
    const bool inPlace = destination == source.samples();
    if (inPlace ? !sameStrides(source.layout(), destinationLayout)
                : rangesOverlap(destination, destinationLayout.totalBytes(), source.samples(),
                                source.layout().totalBytes()))
    {
        return failure(kApplyOperation, ErrorCode::Aliasing,
                       "local normalization writes either the source in place or storage disjoint from it");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }

    const bool eligible = !source.validity().descriptor.needsMask();
    if (sampleType == SampleType::Float32)
    {
        const LocalNormalizationFloat32RunFn compiled = eligible ? localNormalizationFloat32RunFor(execution) : nullptr;
        if (compiled != nullptr)
        {
            Result<BackendSelection> selected = selectBackend(execution);
            if (!selected.hasValue())
            {
                return selected.status();
            }
            return runRows<float>(execution, request, source, destination, destinationLayout,
                                  NormalizationExecutionPath::CompiledRow, *selected, compiled);
        }
        if (execution.requestedBackend() == ComputeBackend::Cpu &&
            execution.backendFallback() == BackendFallback::Forbidden)
        {
            return failure(kApplyOperation, ErrorCode::UnsupportedBackend,
                           "the forced CPU local normalization cannot serve this validity");
        }
        return runRows<float>(execution, request, source, destination, destinationLayout,
                              NormalizationExecutionPath::Reference, referenceSelection(),
                              eligible ? &localNormalizationRunOf<float> : nullptr);
    }
    const LocalNormalizationFloat64RunFn compiled = eligible ? localNormalizationFloat64RunFor(execution) : nullptr;
    if (compiled != nullptr)
    {
        Result<BackendSelection> selected = selectBackend(execution);
        if (!selected.hasValue())
        {
            return selected.status();
        }
        return runRows<double>(execution, request, source, destination, destinationLayout,
                               NormalizationExecutionPath::CompiledRow, *selected, compiled);
    }
    if (execution.requestedBackend() == ComputeBackend::Cpu &&
        execution.backendFallback() == BackendFallback::Forbidden)
    {
        return failure(kApplyOperation, ErrorCode::UnsupportedBackend,
                       "the forced CPU local normalization cannot serve this validity");
    }
    return runRows<double>(execution, request, source, destination, destinationLayout,
                           NormalizationExecutionPath::Reference, referenceSelection(),
                           eligible ? &localNormalizationRunOf<double> : nullptr);
}

Result<MetadataTransition> localNormalizationTransition(const LocalNormalizationApplyRequest& request,
                                                        const ImageSemantics& source)
{
    const Status shapeReady = checkLocalNormalizationGridShape(request.shape);
    if (shapeReady.failed())
    {
        return shapeReady;
    }
    if (!isLocalGridInterpolation(request.interpolation))
    {
        return parameterFailure(kTransitionOperation, ErrorCode::InvalidArgument,
                                "the interpolation is not one this build implements", "interpolation");
    }
    if (request.scale.size() != request.shape.channelCount)
    {
        return parameterFailure(kTransitionOperation, ErrorCode::InvalidArgument,
                                "the scale does not hold one value per channel", "scale");
    }
    ImageSemanticsRequest semanticsRequest = source.toRequest();
    semanticsRequest.processingState.normalized = true;
    Result<ImageSemantics> semantics = ImageSemantics::create(semanticsRequest);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    std::ostringstream detail;
    detail << "grid=" << request.shape.gridSize
           << ",interpolation=" << localGridInterpolationName(request.interpolation)
           << ",channels=" << request.shape.channelCount
           << ",scaleGrid=" << (request.scaleGrid.empty() ? "none" : "fitted");
    for (std::uint32_t channel = 0u; channel < request.shape.channelCount; ++channel)
    {
        detail << ",scale" << channel << '=' << request.scale[channel];
    }
    return operationTransition(
        kLocalNormalizationEffect,
        OperationOutcome{.operation = kApplyOperation, .detail = detail.str(), .semantics = std::move(*semantics)});
}

} // namespace an
