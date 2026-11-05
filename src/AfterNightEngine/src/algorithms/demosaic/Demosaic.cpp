// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ParallelExecutor.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "algorithms/demosaic/DemosaicInternal.h"
#include "kernels/reference/DemosaicKernels.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <utility>

// Planning a demosaic, and running one.
//
// The picture is split in two before any arithmetic: an outer band `reach` pixels wide that no
// method can read a full neighbourhood inside, and the interior that is left. The band is filled by
// the shared border rule and the interior by the method, and the split is what removes every bounds
// test from every kernel - a tile of the interior can read `reach` pixels past its own edge and
// still be inside the picture, by construction.
//
// The interior is walked by the central tile executor, so the thread budget, the worker-private
// scratch partition and the per-tile cancellation checkpoint are the engine's rather than this
// family's. Every output pixel belongs to exactly one tile, so the answer is the same at one worker
// and at sixty-four.
//
// Validity is the last pass rather than a test inside the kernels. An estimate made partly from
// meaningless samples is still computed - a tiled stencil cannot cheaply skip a pixel - and the
// produced mask is what says so afterwards. Combining the footprint is a separable minimum over a
// rectangle, which is two cheap passes rather than one expensive one.

namespace an
{
namespace
{

using demosaic_detail::FloatPlane;

constexpr const char* kPlanOperation = "an::planDemosaic";
constexpr const char* kRunOperation = "an::demosaicImage";
constexpr const char* kTraceCategory = "algorithms.demosaic";

/// Keeps a demosaic span balanced across every success and returned failure.
class DemosaicTraceSpan final
{
public:
    /// Begins the operation span.
    ///
    /// @param execution where the trace is reported when one was supplied.
    explicit DemosaicTraceSpan(const ExecutionContext& execution) noexcept : m_execution(execution)
    {
        m_execution.reportTrace(TraceEventKind::SpanBegin, kTraceCategory, "demosaicImage");
    }

    /// Ends the operation span.
    ~DemosaicTraceSpan()
    {
        m_execution.reportTrace(TraceEventKind::SpanEnd, kTraceCategory, "demosaicImage");
    }

    DemosaicTraceSpan(const DemosaicTraceSpan&) = delete;
    DemosaicTraceSpan& operator=(const DemosaicTraceSpan&) = delete;

private:
    const ExecutionContext& m_execution;
};

/// Reports the stable numeric shape of one completed demosaic.
///
/// @param execution where counters go when a sink was supplied.
/// @param result the completed outcome.
void reportDemosaicCounters(const ExecutionContext& execution, const DemosaicResult& result) noexcept
{
    execution.reportTrace(TraceEventKind::Counter, kTraceCategory, "tiles", static_cast<double>(result.tileCount),
                          "tiles");
    execution.reportTrace(TraceEventKind::Counter, kTraceCategory, "working-memory",
                          static_cast<double>(result.workingBytes), "bytes");
    execution.reportTrace(TraceEventKind::Counter, kTraceCategory, "samples-converted",
                          static_cast<double>(result.conversion.samplesConverted), "samples");
    execution.reportTrace(TraceEventKind::Counter, kTraceCategory, "valid-output-samples",
                          static_cast<double>(result.run.selection.valid), "samples");
}

/// The version of each method's numeric contract.
///
/// @param method the estimate.
/// @return its operation and version.
[[nodiscard]] KernelId kernelIdOf(DemosaicMethod method) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
        return KernelId{.operation = KernelOperation::DemosaicBilinear, .version = 1u};
    case DemosaicMethod::Fast:
        return KernelId{.operation = KernelOperation::DemosaicFast, .version = 1u};
    case DemosaicMethod::Rcd:
        return KernelId{.operation = KernelOperation::DemosaicRcd, .version = 1u};
    case DemosaicMethod::Amaze:
        return KernelId{.operation = KernelOperation::DemosaicAmaze, .version = 1u};
    case DemosaicMethod::XTransFast:
        return KernelId{.operation = KernelOperation::DemosaicXTransFast, .version = 1u};
    case DemosaicMethod::MarkesteijnOnePass:
    case DemosaicMethod::MarkesteijnThreePass:
        return KernelId{.operation = KernelOperation::DemosaicMarkesteijn, .version = 1u};
    case DemosaicMethod::Lmmse:
        return KernelId{.operation = KernelOperation::DemosaicLmmse, .version = 1u};
    case DemosaicMethod::Vng4:
        return KernelId{.operation = KernelOperation::DemosaicVng4, .version = 1u};
    case DemosaicMethod::LuminanceAhd:
        return KernelId{.operation = KernelOperation::DemosaicLuminanceAhd, .version = 1u};
    }
    return KernelId{};
}

/// The cost of one pixel of one method, for the grain policy.
///
/// @param method the estimate.
/// @return the bytes one pixel moves and roughly what it costs.
[[nodiscard]] TaskCost pixelCostOf(DemosaicMethod method) noexcept
{
    const std::uint64_t passes = demosaicMethodPasses(method);
    return TaskCost{.bytesPerItem = 16u, .operationsPerItem = 8u * passes};
}

/// Fills the outer band of the picture with the shared border rule.
///
/// One parallel loop over rows rather than four over strips: a row inside the interior does its two
/// short ends and nothing else, which keeps the partition uniform and the cancellation checkpoints
/// evenly spaced.
///
/// @param execution the operation policy and engine-context lease.
/// @param tile the picture, its mosaic and its destination planes.
/// @param reach how wide the band is.
/// @return success, or a cancellation failure.
[[nodiscard]] Status runBorder(const ExecutionContext& execution, const DemosaicTile& tile, std::uint32_t reach)
{
    const std::uint32_t width = tile.width;
    const std::uint32_t height = tile.height;
    const bool wholePictureIsBand = width <= 2u * reach || height <= 2u * reach;
    return parallelFor(execution, height, demosaic_detail::transferRowCost(width), [&](TaskRange range) {
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const auto y = static_cast<std::uint32_t>(row);
            DemosaicTile strip = tile;
            strip.yBegin = y;
            strip.yEnd = y + 1u;
            if (wholePictureIsBand || y < reach || y >= height - reach)
            {
                strip.xBegin = 0u;
                strip.xEnd = width;
                demosaicBorderRun(strip);
                continue;
            }
            strip.xBegin = 0u;
            strip.xEnd = reach;
            demosaicBorderRun(strip);
            strip.xBegin = width - reach;
            strip.xEnd = width;
            demosaicBorderRun(strip);
        }
        return Status::success();
    });
}

/// Writes the result's validity from the mosaic's, combined over each output pixel's footprint.
///
/// Tiled and separable. Each worker materialises only the horizontal wide and three-sample answers
/// for one tile and its vertical halo, then combines those rows into the caller's packed mask. The
/// frame dimensions therefore do not appear in temporary memory: only tile geometry, reach and the
/// operation's admitted workers do.
///
/// @param execution the operation policy and engine-context lease.
/// @param sourcePlane the widened mosaic the estimate read.
/// @param selection the caller's rule and optional mask.
/// @param produced where the answer goes.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param reach the method's footprint inside the picture.
/// @param validCount set to how many output pixels mean something.
/// @param scratchBytes set to the peak context memory held by this validity phase.
/// @return success, or a memory or cancellation failure.
[[nodiscard]] Status combineFootprintValidity(const ExecutionContext& execution, FloatPlane sourcePlane,
                                              const DemosaicSelection& selection,
                                              const ValidityMaskView<std::uint8_t>& produced, std::uint32_t width,
                                              std::uint32_t height, std::uint32_t reach, std::uint64_t& validCount,
                                              std::uint64_t& scratchBytes)
{
    constexpr std::uint32_t kTileWidth = 128u;
    constexpr std::uint32_t kTileHeight = 32u;
    const std::uint64_t scratchRows = static_cast<std::uint64_t>(kTileHeight) + 2u * reach;
    const std::uint64_t scratchSamples = 2u * static_cast<std::uint64_t>(kTileWidth) * scratchRows;
    const ParallelTileRequest request{.width = width,
                                      .height = height,
                                      .tileWidth = kTileWidth,
                                      .tileHeight = kTileHeight,
                                      .pixelCost = {.bytesPerItem = 4u, .operationsPerItem = 8u},
                                      .scratchBytesPerWorker = scratchSamples,
                                      .scratchAlignment = kCacheLineBytes};
    Result<ParallelTilePlan> planned = planParallelTiles(execution, request);
    if (!planned.hasValue())
    {
        return planned.status();
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, planned->requiredScratchBytes, planned->scratchAlignment, kRunOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    const plane_detail::ScopedContextMemory owned = std::move(memory).value();
    scratchBytes = owned.bytes();
    const KernelValidityView validity{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = 0u};
    std::atomic<std::uint64_t> meaningfulOutputs{0u};
    const std::span<std::byte> scratch{static_cast<std::byte*>(owned.data()), static_cast<std::size_t>(owned.bytes())};
    const Status written = executeParallelTiles(execution, *planned, scratch, [&](const ParallelTile& tile) {
        const std::uint32_t tileWidth = tile.xEnd - tile.xBegin;
        const std::uint32_t haloBeginY = tile.yBegin > reach ? tile.yBegin - reach : 0u;
        const std::uint32_t haloEndY = static_cast<std::uint32_t>(
            (std::min)(static_cast<std::uint64_t>(height), static_cast<std::uint64_t>(tile.yEnd) + reach));
        const std::uint32_t haloRows = haloEndY - haloBeginY;
        auto* const wide = reinterpret_cast<std::uint8_t*>(tile.scratch.data());
        std::uint8_t* const narrow = wide + static_cast<std::uint64_t>(kTileWidth) * scratchRows;

        auto sampleMeansSomething = [&](std::uint32_t x, std::uint32_t y) noexcept {
            const float value = sourcePlane.data[static_cast<std::uint64_t>(y) * sourcePlane.stride + x];
            return sampleIsValidByValue(selection.validity, value) && plane_detail::maskAllows(validity, x, y);
        };
        auto fillHorizontal = [&](std::uint8_t* answer, std::uint32_t y, std::uint32_t radius) noexcept {
            std::uint32_t meaningless = 0u;
            const std::uint32_t initialBegin = tile.xBegin > radius ? tile.xBegin - radius : 0u;
            const std::uint32_t initialEnd = static_cast<std::uint32_t>(
                (std::min)(static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(tile.xBegin) + radius + 1u));
            for (std::uint32_t x = initialBegin; x < initialEnd; ++x)
            {
                meaningless += sampleMeansSomething(x, y) ? 0u : 1u;
            }
            for (std::uint32_t localX = 0u; localX < tileWidth; ++localX)
            {
                const std::uint32_t x = tile.xBegin + localX;
                answer[localX] = meaningless == 0u ? 1u : 0u;
                if (x >= radius)
                {
                    const std::uint32_t leaving = x - radius;
                    meaningless -= sampleMeansSomething(leaving, y) ? 0u : 1u;
                }
                const std::uint64_t entering = static_cast<std::uint64_t>(x) + radius + 1u;
                if (entering < width)
                {
                    meaningless += sampleMeansSomething(static_cast<std::uint32_t>(entering), y) ? 0u : 1u;
                }
            }
        };

        for (std::uint32_t haloRow = 0u; haloRow < haloRows; ++haloRow)
        {
            const std::uint32_t y = haloBeginY + haloRow;
            fillHorizontal(wide + static_cast<std::uint64_t>(haloRow) * kTileWidth, y, reach);
            fillHorizontal(narrow + static_cast<std::uint64_t>(haloRow) * kTileWidth, y, 1u);
        }

        std::uint64_t tileValid = 0u;
        for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
        {
            const bool rowInBand = y < reach || static_cast<std::uint64_t>(y) + reach >= height;
            for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
            {
                const bool inBand = rowInBand || x < reach || static_cast<std::uint64_t>(x) + reach >= width;
                const std::uint32_t radius = inBand ? 1u : reach;
                const std::uint8_t* const horizontal = inBand ? narrow : wide;
                const std::uint32_t lowestY = y > radius ? y - radius : 0u;
                const std::uint32_t highestY = static_cast<std::uint32_t>(
                    (std::min)(static_cast<std::uint64_t>(height - 1u), static_cast<std::uint64_t>(y) + radius));
                bool meaningful = true;
                for (std::uint32_t neighbourY = lowestY; meaningful && neighbourY <= highestY; ++neighbourY)
                {
                    const std::uint64_t index =
                        static_cast<std::uint64_t>(neighbourY - haloBeginY) * kTileWidth + (x - tile.xBegin);
                    meaningful = horizontal[index] != 0u;
                }
                for (std::uint32_t plane = 0u; plane < produced.planeCount(); ++plane)
                {
                    static_cast<void>(produced.setValid(plane, x, y, meaningful));
                }
                tileValid += meaningful ? 1u : 0u;
            }
        }
        meaningfulOutputs.fetch_add(tileValid, std::memory_order_relaxed);
        return Status::success();
    });
    if (written.failed())
    {
        return written;
    }
    produced.clearPadBits();
    validCount = meaningfulOutputs.load(std::memory_order_relaxed);
    return Status::success();
}

} // namespace

const char* demosaicMethodName(DemosaicMethod method) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
        return "Bilinear";
    case DemosaicMethod::Fast:
        return "Fast";
    case DemosaicMethod::Rcd:
        return "Rcd";
    case DemosaicMethod::Amaze:
        return "Amaze";
    case DemosaicMethod::XTransFast:
        return "XTransFast";
    case DemosaicMethod::MarkesteijnOnePass:
        return "MarkesteijnOnePass";
    case DemosaicMethod::MarkesteijnThreePass:
        return "MarkesteijnThreePass";
    case DemosaicMethod::Lmmse:
        return "Lmmse";
    case DemosaicMethod::Vng4:
        return "Vng4";
    case DemosaicMethod::LuminanceAhd:
        return "LuminanceAhd";
    }
    return "UnknownDemosaicMethod";
}

DemosaicPlan::DemosaicPlan(const DemosaicRequest& request, const CfaDescriptor& cfa) noexcept
    : m_request(request), m_cfa(cfa)
{
}

const DemosaicRequest& DemosaicPlan::request() const noexcept
{
    return m_request;
}

const CfaDescriptor& DemosaicPlan::cfa() const noexcept
{
    return m_cfa;
}

DemosaicMethod DemosaicPlan::method() const noexcept
{
    return m_request.method;
}

std::uint32_t DemosaicPlan::reach() const noexcept
{
    return demosaicMethodReach(m_request.method);
}

std::uint64_t DemosaicPlan::tileScratchBytes(std::uint32_t tileWidth, std::uint32_t tileHeight) const noexcept
{
    const std::uint64_t floats = demosaicTileScratchFloats(m_request.method, tileWidth, tileHeight);
    if (floats > (std::numeric_limits<std::uint64_t>::max)() / sizeof(float))
    {
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    return floats * sizeof(float);
}

KernelId DemosaicPlan::kernelId() const noexcept
{
    return kernelIdOf(m_request.method);
}

Result<DemosaicPlan> planDemosaic(const DemosaicRequest& request, const CfaDescriptor& cfa)
{
    if (!isDemosaicMethod(request.method))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                                "the demosaic method is not one this build knows");
    }
    if (!std::isfinite(request.domain.whiteLevel) || request.domain.whiteLevel <= 0.0)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                                "the white level must be a finite value above zero");
    }
    if (!std::isfinite(request.domain.clipFraction) || request.domain.clipFraction <= 0.0 ||
        request.domain.clipFraction > 1.0)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                                "the clip fraction must be above zero and at most one");
    }
    const float whiteLevel = static_cast<float>(request.domain.whiteLevel);
    const float clipPoint = static_cast<float>(request.domain.clipPoint());
    if (!std::isfinite(whiteLevel) || whiteLevel <= 0.0f || !std::isfinite(clipPoint) || clipPoint <= 0.0f)
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::InvalidArgument, kPlanOperation,
            "the white level and clip point must remain finite and above zero in the Float32 arithmetic domain");
    }
    if (CfaDescriptor::check(cfa) != DescriptorError::None)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                                "the filter array does not describe a mosaic");
    }
    if (!cfa.isMosaiced())
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kPlanOperation,
                                                "an image with no filter array has nothing to demosaic");
    }
    if (!demosaicMethodAppliesTo(request.method, cfa.pattern()))
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::IncompatibleArguments, kPlanOperation,
            "this method estimates a mosaic of a different shape from the one it was given");
    }
    if (!isOutputPrecisionContract(request.output.contract))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                                "the output precision contract is not one this build knows");
    }
    const Status conversion = checkConversionPolicy(request.output.writeBack, kPlanOperation);
    if (conversion.failed())
    {
        return conversion;
    }
    if (request.output.writeBack.domain != DomainMapping::CodeValue)
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::InvalidArgument, kPlanOperation,
            "a demosaic writes the numbers it computed, so its write back maps code values");
    }
    return DemosaicPlan{request, cfa};
}

Result<DemosaicResult> demosaicImage(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                     const ImageLayout& sourceLayout, void* destination,
                                     const ImageLayout& destinationLayout, const DemosaicPlan& plan,
                                     const DemosaicSelection& selection)
{
    const Status checked = demosaic_detail::checkImagePair(execution, sampleType, source, sourceLayout, 1u, destination,
                                                           destinationLayout, 3u, kRunOperation);
    if (checked.failed())
    {
        return checked;
    }
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    if (destinationLayout.extent().width() != width || destinationLayout.extent().height() != height)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kRunOperation,
                                                "the two images describe pictures of different sizes");
    }
    const Status sourceMask =
        demosaic_detail::checkSelectionMask(selection.validity, selection.mask, width, height, 1u, 0u, kRunOperation);
    if (sourceMask.failed())
    {
        return sourceMask;
    }
    const PrecisionError precision = spatialPrecisionError(sampleType, plan.request().output.contract);
    if (precision != PrecisionError::None)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::Unsupported, kRunOperation,
                                                "this representation cannot keep the promise the output policy makes "
                                                "while the estimate is computed in Float32");
    }
    const bool everySampleMeansSomething = selection.validity.rule() == ValidityRule::AllValid;
    if (everySampleMeansSomething && selection.produced != nullptr)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kRunOperation,
                                                "an all-valid mosaic produces an all-valid result, so there is nothing "
                                                "for a produced mask to say");
    }
    if (!everySampleMeansSomething && selection.produced == nullptr)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kRunOperation,
                                                "a mosaic whose samples do not all mean something needs a mask for the "
                                                "result, because no rule can carry the answer");
    }
    const Status producedMask =
        demosaic_detail::checkProducedMask(selection.produced, width, height, 1u, 3u, kRunOperation);
    if (producedMask.failed())
    {
        return producedMask;
    }
    const Status maskAliasing = demosaic_detail::checkMaskAliasing(source, sourceLayout.totalBytes(), destination,
                                                                   destinationLayout.totalBytes(), selection.mask,
                                                                   selection.produced, kRunOperation);
    if (maskAliasing.failed())
    {
        return maskAliasing;
    }
    const DemosaicRunFn referenceRun = referenceDemosaicRun(plan.method());
    const DemosaicRunFn run = demosaicRunFor(execution, plan.method());
    if (run == nullptr)
    {
        return demosaic_detail::demosaicFailure(
            referenceRun == nullptr ? ErrorCode::InvalidArgument : ErrorCode::UnsupportedBackend, kRunOperation,
            referenceRun == nullptr ? "the demosaic method is not one this build knows"
                                    : "the requested CPU backend has no compiled kernel for this method and fallback "
                                      "is forbidden");
    }
    if (selection.produced != nullptr)
    {
        selection.produced->fill(false);
    }

    DemosaicResult result{};
    result.method = plan.method();
    result.pattern = plan.cfa().pattern();
    result.borderWidth = plan.reach();
    result.imagePasses = demosaicMethodPasses(plan.method());
    if (run == referenceRun)
    {
        result.backend =
            BackendSelection{.backend = ComputeBackend::Reference, .instructionSet = InstructionSet::Scalar};
        char message[112] = {};
        std::snprintf(message, sizeof(message),
                      "dispatching demosaic method %s to backend Reference with instruction "
                      "set Scalar",
                      demosaicMethodName(plan.method()));
        execution.reportDiagnostic(DiagnosticSeverity::Debug, ErrorDomain::Algorithms, kRunOperation, message);
    }
    else
    {
        const Result<BackendSelection> backend = selectBackend(execution);
        if (!backend.hasValue())
        {
            return backend.status();
        }
        result.backend = backend.value();
    }
    const DemosaicTraceSpan traceSpan(execution);
    execution.reportProgress(0u, 0u, "preparing demosaic");
    const Status starting = execution.checkCancellation();
    if (starting.failed())
    {
        return starting;
    }
    result.outputValidity = everySampleMeansSomething
                                ? ValidityDescriptor{}
                                : ValidityDescriptor::create({.rule = ValidityRule::ExplicitMask}).value();
    result.run.selection.total = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    result.run.outputsWritten = result.run.selection.total;
    std::uint64_t peakScratchBytes = 0u;
    std::uint64_t totalProgress = 0u;

    const bool sourceInPlace = demosaic_detail::channelIsAddressablePlane(sampleType, sourceLayout, source);
    const bool destinationInPlace =
        demosaic_detail::channelIsAddressablePlane(sampleType, destinationLayout, destination) &&
        destinationLayout.planeCount() == 3u;
    result.readInPlace = sourceInPlace;
    result.wroteInPlace = destinationInPlace;

    demosaic_detail::WorkingPlane sourceWorking;
    if (!sourceInPlace)
    {
        Result<demosaic_detail::WorkingPlane> taken =
            demosaic_detail::WorkingPlane::take(execution, width, height, kRunOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        sourceWorking = std::move(taken).value();
        const Status gathered = demosaic_detail::gatherPlane(execution, sampleType, source, sourceLayout, 0u,
                                                             sourceWorking.plane(), width, height, kRunOperation);
        if (gathered.failed())
        {
            return gathered;
        }
        result.workingBytes += sourceWorking.bytes();
    }
    const FloatPlane sourcePlane = sourceInPlace
                                       ? demosaic_detail::borrowFloatPlane(const_cast<void*>(source), sourceLayout, 0u)
                                       : sourceWorking.plane();

    std::array<demosaic_detail::WorkingPlane, 3> destinationWorking;
    std::array<FloatPlane, 3> destinationPlanes{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        if (destinationInPlace)
        {
            destinationPlanes[channel] = demosaic_detail::borrowFloatPlane(destination, destinationLayout, channel);
            continue;
        }
        Result<demosaic_detail::WorkingPlane> taken =
            demosaic_detail::WorkingPlane::take(execution, width, height, kRunOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        destinationWorking[channel] = std::move(taken).value();
        destinationPlanes[channel] = destinationWorking[channel].plane();
        result.workingBytes += destinationWorking[channel].bytes();
    }

    const CfaDescriptor cfa = plan.cfa();
    DemosaicTile picture{};
    picture.cfa = &cfa;
    picture.source = sourcePlane.data;
    picture.sourceStride = sourcePlane.stride;
    picture.red = destinationPlanes[0].data;
    picture.green = destinationPlanes[1].data;
    picture.blue = destinationPlanes[2].data;
    picture.destinationStride = destinationPlanes[0].stride;
    picture.width = width;
    picture.height = height;
    picture.reach = plan.reach();
    picture.whiteLevel = static_cast<float>(plan.request().domain.whiteLevel);
    picture.clipPoint = static_cast<float>(plan.request().domain.clipPoint());
    picture.useCieLab = plan.request().useCieLab;
    if (destinationPlanes[1].stride != picture.destinationStride ||
        destinationPlanes[2].stride != picture.destinationStride)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kRunOperation,
                                                "the three destination planes do not share one row stride");
    }

    const Status bordered = runBorder(execution, picture, plan.reach());
    if (bordered.failed())
    {
        return bordered;
    }
    execution.reportProgress(1u, 0u, "filling demosaic border");
    const Status afterBorder = execution.checkCancellation();
    if (afterBorder.failed())
    {
        return afterBorder;
    }

    const std::uint32_t reach = plan.reach();
    if (width > 2u * reach && height > 2u * reach)
    {
        std::uint32_t tileWidth = 0u;
        std::uint32_t tileHeight = 0u;
        demosaicTileShape(plan.method(), tileWidth, tileHeight);
        const std::uint32_t interiorWidth = width - 2u * reach;
        const std::uint32_t interiorHeight = height - 2u * reach;
        tileWidth = (std::min)(tileWidth, interiorWidth);
        tileHeight = (std::min)(tileHeight, interiorHeight);
        const ParallelTileRequest tileRequest{.width = interiorWidth,
                                              .height = interiorHeight,
                                              .tileWidth = tileWidth,
                                              .tileHeight = tileHeight,
                                              .pixelCost = pixelCostOf(plan.method()),
                                              .scratchBytesPerWorker = plan.tileScratchBytes(tileWidth, tileHeight),
                                              .scratchAlignment = kCacheLineBytes};
        Result<ParallelTilePlan> tilePlan = planParallelTiles(execution, tileRequest);
        if (!tilePlan.hasValue())
        {
            return tilePlan.status();
        }
        result.tileCount = tilePlan.value().tileCount;
        result.plannedWorkers = tilePlan.value().plannedWorkers;
        totalProgress = result.tileCount + 4u;

        Result<plane_detail::ScopedContextMemory> scratchMemory = plane_detail::ScopedContextMemory::take(
            execution, tilePlan.value().requiredScratchBytes, tilePlan.value().scratchAlignment, kRunOperation);
        if (!scratchMemory.hasValue())
        {
            return scratchMemory.status();
        }
        plane_detail::ScopedContextMemory scratch = std::move(scratchMemory).value();
        peakScratchBytes = (std::max)(peakScratchBytes, scratch.bytes());

        const std::span<std::byte> scratchSpan{static_cast<std::byte*>(scratch.data()),
                                               static_cast<std::size_t>(scratch.bytes())};
        std::atomic<std::uint64_t> completedTiles{0u};
        const Status ran =
            executeParallelTiles(execution, tilePlan.value(), scratchSpan, [&](const ParallelTile& tile) -> Status {
                DemosaicTile region = picture;
                region.xBegin = tile.xBegin + reach;
                region.xEnd = tile.xEnd + reach;
                region.yBegin = tile.yBegin + reach;
                region.yEnd = tile.yEnd + reach;
                run(region, reinterpret_cast<float*>(tile.scratch.data()),
                    static_cast<std::uint64_t>(tile.scratch.size()) / sizeof(float));
                const std::uint64_t completed = completedTiles.fetch_add(1u, std::memory_order_relaxed) + 1u;
                execution.reportProgress(completed + 1u, totalProgress, "estimating demosaic interior");
                return execution.checkCancellation();
            });
        if (ran.failed())
        {
            return ran;
        }
    }
    totalProgress = result.tileCount + 4u;

    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const Status scattered = demosaic_detail::scatterPlane(
            execution, destinationPlanes[channel], sampleType, destination, destinationLayout, channel, width, height,
            plan.request().output.writeBack, result.conversion, kRunOperation);
        if (scattered.failed())
        {
            return scattered;
        }
    }
    execution.reportProgress(result.tileCount + 2u, totalProgress, "writing demosaic result");
    const Status afterWriteBack = execution.checkCancellation();
    if (afterWriteBack.failed())
    {
        return afterWriteBack;
    }

    if (everySampleMeansSomething)
    {
        result.run.selection.valid = result.run.selection.total;
        result.workingBytes += peakScratchBytes;
        execution.reportProgress(result.tileCount + 3u, totalProgress, "finalising demosaic validity");
        const Status afterValidity = execution.checkCancellation();
        if (afterValidity.failed())
        {
            return afterValidity;
        }
        reportDemosaicCounters(execution, result);
        execution.reportProgress(totalProgress, totalProgress, "demosaic complete");
        return result;
    }

    std::uint64_t validityScratchBytes = 0u;
    const Status combined = combineFootprintValidity(execution, sourcePlane, selection, *selection.produced, width,
                                                     height, reach, result.run.selection.valid, validityScratchBytes);
    if (combined.failed())
    {
        return combined;
    }
    result.workingBytes += (std::max)(peakScratchBytes, validityScratchBytes);
    execution.reportProgress(result.tileCount + 3u, totalProgress, "finalising demosaic validity");
    const Status afterValidity = execution.checkCancellation();
    if (afterValidity.failed())
    {
        return afterValidity;
    }
    reportDemosaicCounters(execution, result);
    execution.reportProgress(totalProgress, totalProgress, "demosaic complete");
    return result;
}

} // namespace an
