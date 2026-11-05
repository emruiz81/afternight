// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle planner's byte and read accounting.
//
// Every buffer the executor will hold is priced from the model alone, through `CheckedSize`, before
// anything is compared with the budget. The fixed part - output, mask, maps, frame records, table,
// finalization numerators, prepared state, worker scratch and reader workspace - does not depend on
// the band heights; the source buffer, the coordinate map and the accumulators do. The source band
// height is then the largest, up to the request's cap, whose fixed part, source buffer and map
// still leave room for one destination row, found by bisection because that total grows with the
// height; whatever remains decides the destination band's height. When the context has a budget,
// what is available is that budget minus the live bytes that are not the caller's already-counted
// prepared state; a caller's ceiling narrows it further.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePlanner.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>

#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "algorithms/drizzle/DrizzleGeometryInternal.h"

#include <algorithm>
#include <cstdio>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::planDrizzle";

Status refusal(ErrorCode code, const char* parameter, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = kOperation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

Status budgetRefusal(std::uint64_t requested, std::uint64_t available, std::uint64_t limit)
{
    return Status::failure(
        ErrorCode::MemoryBudgetExceeded,
        StatusDetail{.message = "drizzle cannot fit its persistent state, source workspace and one destination row",
                     .operation = kOperation,
                     .domain = ErrorDomain::Algorithms,
                     .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                 StatusContextField::ofCount(ContextKey::Available, available),
                                 StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

std::uint32_t divideUp(std::uint32_t count, std::uint32_t divisor) noexcept
{
    return count / divisor + (count % divisor != 0u ? 1u : 0u);
}

CheckedSize imageRowBytes(const ImageDescriptor& descriptor) noexcept
{
    return CheckedSize{descriptor.extent().width()} * descriptor.extent().channelCount() *
           sampleTypeSizeBytes(descriptor.sampleType());
}

std::uint32_t coordinateRows(const DrizzlePlanningRequest& request, std::uint32_t sourceHeight, std::uint32_t rows,
                             std::uint32_t halo) noexcept
{
    return request.mapStorage == DrizzleMapStorage::Dense ? sourceHeight : (std::min)(sourceHeight, rows + 2u * halo);
}

} // namespace

Result<DrizzlePlan> planDrizzle(const ExecutionContext& execution, const DrizzleModel& model,
                                const DrizzlePlanningRequest& request)
{
    if (!execution.valid())
        return refusal(ErrorCode::NotInitialized, "execution", "the execution context is empty");
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (request.access != DrizzleSourceAccess::WholeFrame && request.access != DrizzleSourceAccess::Region)
        return refusal(ErrorCode::InvalidArgument, "access", "unknown drizzle source access");
    if (request.mapStorage != DrizzleMapStorage::BoundedRows && request.mapStorage != DrizzleMapStorage::Dense)
        return refusal(ErrorCode::InvalidArgument, "mapStorage", "unknown drizzle coordinate storage");
    if (request.maximumSourceRows == 0u || request.maximumSourceRows > 256u)
        return refusal(ErrorCode::InvalidArgument, "maximumSourceRows", "source band cap must be in [1,256]");
    const DrizzleRequest& description = model.request();
    if (request.concurrentFrameReads == 0u || request.concurrentFrameReads > description.frameCount)
        return refusal(ErrorCode::InvalidArgument, "concurrentFrameReads",
                       "concurrent reads must be in [1,frameCount]");
    const EngineLimits limits = execution.limits();
    const auto checkedModel = DrizzleModel::create(description, limits);
    if (!checkedModel.hasValue())
        return checkedModel.status();
    const auto selected = selectBackend(execution);
    if (!selected.hasValue())
        return selected.status();
    const std::uint64_t live = execution.memoryResource()->counters().liveBytes;
    if (request.preparedContextBytes > request.preparedStateBytes || request.preparedContextBytes > live)
        return refusal(ErrorCode::IncompatibleArguments, "preparedContextBytes",
                       "context-counted prepared bytes must belong to both the prepared state and live allocations");

    DrizzlePlan plan;
    plan.request = request;
    plan.workers = execution.admittedConcurrency();
    plan.backend = *selected;
    plan.collectMaximumWeight = description.parameters.collectMaximumWeight;
    plan.mapHaloRows = description.parameters.kernel == DrizzleKernel::Square ? 1u : 0u;
    const ImageExtent& source = description.input.extent();
    const ImageExtent& output = model.outputDescriptor().extent();
    const bool lanczos = description.parameters.kernel == DrizzleKernel::Lanczos2 ||
                         description.parameters.kernel == DrizzleKernel::Lanczos3;

    // Account the input mask at its packed per-plane row width, not one byte per pixel and not a
    // single shared plane. Reader payloads include every auxiliary input the model names.
    CheckedSize sourceRow = imageRowBytes(description.input);
    if (description.validity.needsMask())
        sourceRow = sourceRow + CheckedSize{packedMaskRowBytes(source.width())} * source.channelCount();
    if (description.pixelWeights.has_value())
        sourceRow = sourceRow + imageRowBytes(*description.pixelWeights);
    if (description.flatWeights.has_value())
        sourceRow = sourceRow + imageRowBytes(*description.flatWeights);
    const CheckedSize elements = CheckedSize{output.width()} * output.height() * output.channelCount();
    const CheckedSize outputImage = imageRowBytes(model.outputDescriptor()) * output.height();
    const CheckedSize outputMask =
        CheckedSize{packedMaskRowBytes(output.width())} * output.height() * output.channelCount();
    const CheckedSize outputMaps = elements * sizeof(float) *
                                   (static_cast<std::uint64_t>(description.output.weightMap) +
                                    static_cast<std::uint64_t>(description.output.coverageMap));
    const CheckedSize frameState =
        CheckedSize{description.frameCount} * (sizeof(DrizzleMapDescriptor) + 4u * sizeof(double)) +
        sizeof(DrizzleGeometryState) + DrizzlePixmap::controlBytes();
    const CheckedSize kernelTable{lanczos ? sizeof(DrizzleKernelTable) : 0u};
    const CheckedSize finalization = description.output.combination == DrizzleCombination::PeakNormalizedSum &&
                                             description.output.sampleType == SampleType::Float32
                                         ? elements * sizeof(double)
                                         : CheckedSize{0u};
    const CheckedSize persistent =
        outputImage + outputMask + outputMaps + frameState + kernelTable + finalization + request.preparedStateBytes;
    // A converted source row and its effective weights, sixteen corner coordinates, and eight
    // counters. Round the complete slice up, so adjacent worker slots never share an alignment gap.
    const CheckedSize scratchUnaligned = CheckedSize{source.width()} * source.channelCount() * 2u * sizeof(double) +
                                         16u * sizeof(double) + 8u * sizeof(std::uint64_t);
    const CheckedSize roundedScratch = scratchUnaligned + 63u;
    const CheckedSize scratchPerWorker =
        roundedScratch.isValid() ? CheckedSize{roundedScratch.valueOr(0u) / 64u * 64u} : CheckedSize::overflowed();
    const CheckedSize scratch = scratchPerWorker * plan.workers;
    const CheckedSize readerWorkspace = CheckedSize{request.sourceWorkspaceBytes} * request.concurrentFrameReads;
    const CheckedSize accumulatorRow = CheckedSize{output.width()} * output.channelCount() * 2u * sizeof(double);
    const CheckedSize coverageRow =
        description.output.coverageMap
            ? CheckedSize{output.width()} * output.channelCount() * 2u * sizeof(std::uint32_t)
            : CheckedSize{0u};
    const CheckedSize destinationRow = accumulatorRow + coverageRow;
    const CheckedSize frameBytes = sourceRow * source.height();
    const CheckedSize distinctInput = frameBytes * description.frameCount;
    const CheckedSize fixed = persistent + scratch + readerWorkspace;
    if (!fixed.toOffset().has_value() || !destinationRow.toOffset().has_value() || !distinctInput.isValid())
        return refusal(ErrorCode::LimitExceeded, "memory", "drizzle byte accounting overflows its representable range");

    std::uint64_t budget = limits.memoryBudgetBytes;
    std::uint64_t available = kDefaultMemoryBudgetBytes;
    bool bounded = budget != kDefaultMemoryBudgetBytes;
    if (bounded)
    {
        // Prepared bytes counted here are already part of live, so subtract only unrelated memory.
        const std::uint64_t unrelated = live - request.preparedContextBytes;
        available = budget > unrelated ? budget - unrelated : 0u;
    }
    if (request.workingSetLimitBytes != 0u)
    {
        bounded = true;
        budget = (std::min)(budget, request.workingSetLimitBytes);
        available = (std::min)(available, request.workingSetLimitBytes);
    }
    const auto sourceBytesAt = [&](std::uint32_t rows) {
        return sourceRow * (request.access == DrizzleSourceAccess::WholeFrame ? source.height() : rows) *
               request.concurrentFrameReads;
    };
    // Two Float32 planes plus the two recorded destination bounds of every stored row.
    const auto mapBytesAt = [&](std::uint32_t rows) {
        return (CheckedSize{source.width()} + 1u) * coordinateRows(request, source.height(), rows, plan.mapHaloRows) *
               2u * sizeof(float);
    };
    const auto minimumAt = [&](std::uint32_t rows) {
        return fixed + sourceBytesAt(rows) + mapBytesAt(rows) + destinationRow;
    };
    const CheckedSize minimum = minimumAt(1u);
    if (!minimum.toOffset().has_value())
        return refusal(ErrorCode::LimitExceeded, "memory",
                       "minimum drizzle working set overflows its representable range");
    if (minimum.valueOr(0u) > available)
        return budgetRefusal(minimum.valueOr(0u), available, budget);

    std::uint32_t low = 1u;
    std::uint32_t high = (std::min)(source.height(), request.maximumSourceRows);
    while (low < high)
    {
        const std::uint32_t candidate = low + (high - low + 1u) / 2u;
        const CheckedSize bytes = minimumAt(candidate);
        if (bytes.toOffset().has_value() && bytes.valueOr(0u) <= available)
            low = candidate;
        else
            high = candidate - 1u;
    }
    plan.sourceRows = low;
    plan.sourceBlockCount = divideUp(source.height(), low);
    plan.mapRows = coordinateRows(request, source.height(), low, plan.mapHaloRows);
    const CheckedSize readBytes = sourceBytesAt(low);
    const CheckedSize mapBytes = mapBytesAt(low);
    const CheckedSize beforeAccumulator = fixed + readBytes + mapBytes;
    const std::uint32_t cap = request.maximumDestinationRows == 0u
                                  ? output.height()
                                  : (std::min)(output.height(), request.maximumDestinationRows);
    const std::uint64_t fittingRows = (available - beforeAccumulator.valueOr(0u)) / destinationRow.valueOr(0u);
    plan.destinationRows = static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(cap), fittingRows));
    plan.destinationTileCount = divideUp(output.height(), plan.destinationRows);
    plan.tileReason = plan.destinationRows < cap ? DrizzleTileReason::BudgetBound
                      : cap < output.height()    ? DrizzleTileReason::CallerRowCap
                                                 : DrizzleTileReason::WholeOutput;
    const CheckedSize accumulators = accumulatorRow * plan.destinationRows;
    const CheckedSize coverage = coverageRow * plan.destinationRows;
    const CheckedSize peak = beforeAccumulator + accumulators + coverage;
    const CheckedSize logicalInput = distinctInput * plan.destinationTileCount;
    const CheckedSize reads = CheckedSize{description.frameCount} * plan.destinationTileCount *
                              (request.access == DrizzleSourceAccess::Region ? plan.sourceBlockCount : 1u);
    if (!peak.toOffset().has_value() || !logicalInput.isValid() || !reads.isValid())
        return refusal(ErrorCode::LimitExceeded, "io", "drizzle peak or repeated input accounting overflows");

    plan.memory = {.outputImageBytes = outputImage.valueOr(0u),
                   .outputMaskBytes = outputMask.valueOr(0u),
                   .outputMapBytes = outputMaps.valueOr(0u),
                   .frameStateBytes = frameState.valueOr(0u),
                   .kernelTableBytes = kernelTable.valueOr(0u),
                   .finalizationBytes = finalization.valueOr(0u),
                   .preparedStateBytes = request.preparedStateBytes,
                   .persistentBytes = persistent.valueOr(0u),
                   .accumulatorBytes = accumulators.valueOr(0u),
                   .coverageStateBytes = coverage.valueOr(0u),
                   .sourceBufferBytes = readBytes.valueOr(0u),
                   .sourceWorkspaceBytes = readerWorkspace.valueOr(0u),
                   .coordinateMapBytes = mapBytes.valueOr(0u),
                   .scratchBytesPerWorker = scratchPerWorker.valueOr(0u),
                   .scratchBytes = scratch.valueOr(0u),
                   .peakBytes = peak.valueOr(0u),
                   .availableBytes = available,
                   .budgetBytes = budget,
                   .budgetBounded = bounded};
    plan.io = {.frameBytes = frameBytes.valueOr(0u),
               .distinctInputBytes = distinctInput.valueOr(0u),
               .logicalInputBytes = logicalInput.valueOr(0u),
               .readRequests = reads.valueOr(0u),
               .inputPasses = plan.destinationTileCount};
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    char message[320];
    std::snprintf(message, sizeof message,
                  "Drizzle %s: %u source rows, %u destination rows, %u input passes, %llu input bytes, %llu peak "
                  "bytes, %u workers with exclusive destination ownership.",
                  drizzleKernelName(description.parameters.kernel), plan.sourceRows, plan.destinationRows,
                  plan.io.inputPasses, static_cast<unsigned long long>(plan.io.logicalInputBytes),
                  static_cast<unsigned long long>(plan.memory.peakBytes), plan.workers);
    execution.reportDiagnostic(plan.io.inputPasses > 1u ? DiagnosticSeverity::Warning : DiagnosticSeverity::Info,
                               ErrorDomain::Algorithms, kOperation, message);
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    return plan;
}

} // namespace an
