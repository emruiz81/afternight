// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - finding where a frame is darker than its own smooth version, and darkening
// it further.
//
// The operation composes published calls and has no kernel of its own. Each channel is widened into
// a Float32 working plane with its rejected samples substituted, smoothed twice by
// `smoothImageMultiscaleResidual` into two further planes, and the difference between the second
// smoothing and the frame is summed into the one mask plane; the sum is averaged over the channels,
// rescaled by its own extremes, denoised through a three-layer starlet decomposition and rescaled
// again. The enhancement then reuses two of the three working planes, one for the frame in units of
// the white level and one for `stretchImage`'s answer, which is blended back through the mask.
//
// So the memory held is three planes of one channel, a fourth when the caller keeps no mask plane,
// and the mask decomposition, whatever the channel count and the number of layers removed.

#include <AfterNightEngine/Algorithms/Detail/DarkStructureEnhance.h>

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/SpatialPlane.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace an
{
namespace
{

using spatial_detail::SpatialGeometry;
using spatial_detail::SpatialPlaneBuffer;

constexpr const char* kPlanOperation = "an::planDarkStructure";
constexpr const char* kMaskOperation = "an::buildImageDarkStructureMask";
constexpr const char* kEnhanceOperation = "an::enhanceImageDarkStructures";

/// What building the mask says it is doing.
constexpr const char* kMaskStage = "dark structure: mask";

/// What the enhancement says it is doing.
constexpr const char* kEnhanceStage = "dark structure: enhancement";

/// One operation's progress, counted in whole passes: each channel's mask measurement, the mask's own
/// denoise when the request asks for one, and each enhancement iteration of each channel.
struct PassProgress
{
    /// How many passes are finished.
    std::uint64_t completed = 0u;

    /// How many the operation will make.
    std::uint64_t total = 0u;
};

/// One failure from this domain.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status darkStructureFailure(ErrorCode code, const char* operation, const char* message)
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
[[nodiscard]] Status darkStructureLimitFailure(ErrorCode code, const char* operation, const char* message,
                                               std::uint64_t requested, std::uint64_t limit)
{
    return spatial_detail::spatialLimitFailure(code, operation, message, requested, limit);
}

/// How much work one row of a pass is, so the scheduler chunks rows rather than guessing.
///
/// @param width how many samples one row holds.
/// @param planes how many planes the row is read from or written to.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(std::uint32_t width, std::uint64_t planes) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width) * planes;
    return TaskCost{.bytesPerItem = samples * sizeof(float), .operationsPerItem = samples * 2u};
}

/// The extremes of one plane, ignoring anything that is not finite.
struct PlaneRange
{
    /// The smallest finite sample, or the largest representable number when there is none.
    double lowest = std::numeric_limits<double>::max();

    /// The largest finite sample, or the most negative representable number when there is none.
    double highest = std::numeric_limits<double>::lowest();

    /// Whether the plane held a finite sample at all.
    ///
    /// @return true when the extremes describe something.
    [[nodiscard]] bool valid() const noexcept
    {
        return highest >= lowest;
    }
};

/// Measures a plane's finite extremes.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the plane to read.
/// @param range where the extremes go.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status measureRange(const ExecutionContext& execution, PlaneView<const float> plane, PlaneRange& range)
{
    const std::uint32_t width = plane.width();
    const auto minimum = [](double left, double right) noexcept { return left < right ? left : right; };
    const auto maximum = [](double left, double right) noexcept { return left > right ? left : right; };
    const Result<double> lowest = parallelReduce(
        execution, plane.height(), rowCost(width, 1u), std::numeric_limits<double>::max(),
        [&](TaskRange rows, double& partial) {
            for (std::uint64_t index = rows.begin; index < rows.end; ++index)
            {
                const std::span<const float> row = *plane.row(static_cast<std::uint32_t>(index));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (std::isfinite(row[x]))
                    {
                        partial = (std::min)(partial, static_cast<double>(row[x]));
                    }
                }
            }
            return Status::success();
        },
        minimum);
    if (!lowest.hasValue())
    {
        return lowest.status();
    }
    const Result<double> highest = parallelReduce(
        execution, plane.height(), rowCost(width, 1u), std::numeric_limits<double>::lowest(),
        [&](TaskRange rows, double& partial) {
            for (std::uint64_t index = rows.begin; index < rows.end; ++index)
            {
                const std::span<const float> row = *plane.row(static_cast<std::uint32_t>(index));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (std::isfinite(row[x]))
                    {
                        partial = (std::max)(partial, static_cast<double>(row[x]));
                    }
                }
            }
            return Status::success();
        },
        maximum);
    if (!highest.hasValue())
    {
        return highest.status();
    }
    range.lowest = lowest.value();
    range.highest = highest.value();
    return Status::success();
}

/// Maps a plane onto the unit interval by its own extremes.
///
/// A plane whose extremes are no further apart than `kMinDarkStructureMaskRange` describes no
/// structure and becomes zeros, which is what the caller is told about rather than a mask amplified
/// out of rounding noise.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the plane to rescale, in place.
/// @param range the extremes it was measured to have.
/// @return true when the plane was rescaled, false when it was flattened to zeros.
[[nodiscard]] Result<bool> rescaleToUnit(const ExecutionContext& execution, PlaneView<float> plane,
                                         const PlaneRange& range)
{
    const std::uint32_t width = plane.width();
    const bool degenerate = !range.valid() || (range.highest - range.lowest) <= kMinDarkStructureMaskRange;
    const double lowest = range.lowest;
    const double scale = degenerate ? 0.0 : 1.0 / (range.highest - range.lowest);
    const Status walked = parallelFor(execution, plane.height(), rowCost(width, 2u), [&](TaskRange rows) {
        for (std::uint64_t index = rows.begin; index < rows.end; ++index)
        {
            const std::span<float> row = *plane.row(static_cast<std::uint32_t>(index));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (degenerate || !std::isfinite(row[x]))
                {
                    row[x] = 0.0f;
                    continue;
                }
                row[x] = static_cast<float>(std::clamp((static_cast<double>(row[x]) - lowest) * scale, 0.0, 1.0));
            }
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    return !degenerate;
}

/// Holds the mask plane, whether it is the caller's or the operation's own.
struct MaskPlane
{
    /// The working memory, or an owner of nothing when the caller supplied the plane.
    SpatialPlaneBuffer owned{};

    /// The plane itself, which is present once either half has been decided.
    std::optional<PlaneView<float>> view{};

    /// How many bytes of context memory this holds.
    ///
    /// @return the byte count, which is zero for a caller's plane.
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return owned.bytes();
    }
};

/// Builds the dark-structure mask of one image into a plane.
///
/// @param engine the context whose memory owns the mask decomposition.
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the source samples are.
/// @param source where they start.
/// @param sourceLayout where they sit.
/// @param plan the validated request.
/// @param geometry the picture both sides agree about.
/// @param work the working plane that receives each channel, widened and with rejected samples
///        substituted.
/// @param first the working plane that receives the first smoothing, with the fine scales removed.
/// @param second the working plane that receives the second smoothing the frame is compared with.
/// @param mask where the mask goes; it is cleared first and left as a fraction or as zeros.
/// @param selection which samples mean something.
/// @param operation the name carried by any failure.
/// @param result the counters, added to.
/// @param nestedWorkingBytes the largest temporary allocation held by a nested operation.
/// @param progress the operation's pass count, advanced by one per channel and one for the denoise.
/// @return success, or the first failure a transform, a shrinkage or a reduction reported.
[[nodiscard]] Status buildMask(const EngineContext& engine, const ExecutionContext& execution, SampleType sampleType,
                               const void* source, const ImageLayout& sourceLayout, const DarkStructurePlan& plan,
                               const SpatialGeometry& geometry, PlaneView<float> work, PlaneView<float> first,
                               PlaneView<float> second, PlaneView<float> mask, const SpatialSelection& selection,
                               const char* operation, DarkStructureResult& result, std::uint64_t& nestedWorkingBytes,
                               PassProgress& progress)
{
    const std::uint32_t width = geometry.width;
    const Status cleared = parallelFor(execution, geometry.height, rowCost(width, 1u), [&](TaskRange rows) {
        for (std::uint64_t index = rows.begin; index < rows.end; ++index)
        {
            const std::span<float> row = *mask.row(static_cast<std::uint32_t>(index));
            std::fill(row.begin(), row.end(), 0.0f);
        }
        return Status::success();
    });
    if (cleared.failed())
    {
        return cleared;
    }

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        const Status gathered =
            spatial_detail::gatherChannelPlane(execution, sampleType, source, sourceLayout, channel, work, operation);
        if (gathered.failed())
        {
            return gathered;
        }
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status replaced = spatial_detail::substituteRejectedSamples(
            execution, work, validity, plan.finePlan().request().borders.invalidNeighbor, result.substitutedSamples,
            operation);
        if (replaced.failed())
        {
            return replaced;
        }

        // Everything smaller than the frame's sixth scale is removed, and then everything smaller
        // than the requested scale is removed from that. The difference between the second smoothing
        // and the frame is where the frame sits below its own illumination.
        const Result<SpatialFilterResult> fine = smoothImageMultiscaleResidual(
            execution, SampleType::Float32, work.data(), work.layout(), first.data(), first.layout(), plan.finePlan());
        if (!fine.hasValue())
        {
            return fine.status();
        }
        nestedWorkingBytes = (std::max)(nestedWorkingBytes, fine->workingBytes);
        const Result<SpatialFilterResult> coarse =
            smoothImageMultiscaleResidual(execution, SampleType::Float32, first.data(), first.layout(), second.data(),
                                          second.layout(), plan.coarsePlan());
        if (!coarse.hasValue())
        {
            return coarse.status();
        }
        nestedWorkingBytes = (std::max)(nestedWorkingBytes, coarse->workingBytes);

        const Status accumulated = parallelFor(execution, geometry.height, rowCost(width, 3u), [&](TaskRange rows) {
            for (std::uint64_t index = rows.begin; index < rows.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<const float> smooth = *second.constView().row(y);
                const std::span<const float> frame = *work.constView().row(y);
                const std::span<float> into = *mask.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    into[x] += smooth[x] - frame[x];
                }
            }
            return Status::success();
        });
        if (accumulated.failed())
        {
            return accumulated;
        }
        execution.reportProgress(++progress.completed, progress.total, kMaskStage);
    }

    const auto inverseChannels = static_cast<float>(1.0 / static_cast<double>(geometry.channelCount));
    const Status averaged = parallelFor(execution, geometry.height, rowCost(width, 2u), [&](TaskRange rows) {
        for (std::uint64_t index = rows.begin; index < rows.end; ++index)
        {
            const std::span<float> row = *mask.row(static_cast<std::uint32_t>(index));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                row[x] *= inverseChannels;
            }
        }
        return Status::success();
    });
    if (averaged.failed())
    {
        return averaged;
    }

    PlaneRange range{};
    const Status measured = measureRange(execution, mask.constView(), range);
    if (measured.failed())
    {
        return measured;
    }
    result.measurementLowest = range.valid() ? range.lowest : 0.0;
    result.measurementHighest = range.valid() ? range.highest : 0.0;
    const Result<bool> rescaled = rescaleToUnit(execution, mask, range);
    if (!rescaled.hasValue())
    {
        return rescaled.status();
    }
    if (!rescaled.value())
    {
        result.maskDegenerate = true;
        return Status::success();
    }

    if (!plan.request().denoiseMask)
    {
        return Status::success();
    }

    // The mask's own denoise is one pass of this operation's progress, and the decomposition inside it
    // reports its layers into that share rather than into a total of its own.
    std::optional<ProgressRange> denoiseShare;
    denoiseShare.emplace(execution, progress.completed, progress.completed + 1u, progress.total);
    Result<MultiscaleDecomposition> decomposed =
        decomposeImageMultiscale(engine, execution, SampleType::Float32, mask.data(), mask.layout(), plan.maskPlan());
    if (!decomposed.hasValue())
    {
        return decomposed.status();
    }
    const std::uint64_t layerBytes = decomposed->layerBytes();
    nestedWorkingBytes = (std::max)(nestedWorkingBytes, layerBytes + decomposed->result().workingBytes);
    const std::optional<ImageLayout> decompositionLayout = decomposed->layout();
    if (!decompositionLayout.has_value())
    {
        return darkStructureFailure(ErrorCode::Internal, operation, "the mask decomposition describes no image");
    }
    const KernelPlaneSource noiseSource{.sampleType = SampleType::Float32,
                                        .samples = decomposed->samples(),
                                        .layout = &*decompositionLayout,
                                        .plane = 1u,
                                        .sampleInPixel = 0u};
    const std::array<double, 1u> quantiles{0.5};
    std::array<double, 1u> quantileValues{};
    const PlaneOrderRequest noiseRequest{.interpolation = QuantileInterpolation::Linear,
                                         .medianAbsoluteDeviation = true};
    const Result<PlaneOrderStatistics> noise = selectPlaneOrderStatistics(
        execution, noiseSource, quantiles, quantileValues, noiseRequest, KernelValidityView{});
    if (!noise.hasValue())
    {
        return noise.status();
    }
    nestedWorkingBytes = (std::max)(nestedWorkingBytes, layerBytes + noise->peakScratchBytes);
    MultiscaleShrinkRequest shrink{};
    shrink.shrinkage = MultiscaleShrinkage::Soft;
    shrink.threshold =
        kDarkStructureMaskThresholdSigmas * noise->medianAbsoluteDeviation * kMedianAbsoluteDeviationToSigma;
    shrink.amount = 1.0;
    for (std::uint32_t pass = 0u; pass < kDarkStructureMaskShrinkPasses; ++pass)
    {
        const Result<MultiscaleShrinkResult> shrunk =
            shrinkMultiscaleLayer(execution, decomposed.value(), 0u, 1u, shrink);
        if (!shrunk.hasValue())
        {
            return shrunk.status();
        }
        result.maskCoefficientsShrunk += shrunk->samplesChanged;
    }

    // The finest layer is the mask's own noise and contributes nothing; the shrunk layer and the
    // residual are what is left of the structure.
    const std::array<MultiscaleLayerWeight, kDarkStructureMaskLayers> weights{
        MultiscaleLayerWeight{.enabled = false, .gain = 0.0}, MultiscaleLayerWeight{.enabled = true, .gain = 1.0},
        MultiscaleLayerWeight{.enabled = true, .gain = 1.0}};
    MultiscaleReconstructionRequest reconstruction{};
    reconstruction.layers = std::span<const MultiscaleLayerWeight>{weights.data(), weights.size()};
    const Result<SpatialFilterResult> rebuilt = reconstructImageMultiscale(
        execution, decomposed.value(), SampleType::Float32, mask.data(), mask.layout(), reconstruction);
    if (!rebuilt.hasValue())
    {
        return rebuilt.status();
    }
    nestedWorkingBytes = (std::max)(nestedWorkingBytes, layerBytes + rebuilt->workingBytes);
    decomposed->release();
    denoiseShare.reset();
    execution.reportProgress(++progress.completed, progress.total, kMaskStage);

    PlaneRange smoothedRange{};
    const Status remeasured = measureRange(execution, mask.constView(), smoothedRange);
    if (remeasured.failed())
    {
        return remeasured;
    }
    const Result<bool> renormalised = rescaleToUnit(execution, mask, smoothedRange);
    if (!renormalised.hasValue())
    {
        return renormalised.status();
    }
    if (!renormalised.value())
    {
        result.maskDegenerate = true;
    }
    return Status::success();
}

} // namespace

DarkStructurePlan::DarkStructurePlan(const DarkStructureRequest& request, const MultiscaleTransformPlan& fine,
                                     const MultiscaleTransformPlan& coarse,
                                     const MultiscaleTransformPlan& mask) noexcept
    : m_request(request), m_fine(fine), m_coarse(coarse), m_mask(mask), m_valid(true)
{
}

const DarkStructureRequest& DarkStructurePlan::request() const noexcept
{
    return m_request;
}

const MultiscaleTransformPlan& DarkStructurePlan::finePlan() const noexcept
{
    return m_fine;
}

const MultiscaleTransformPlan& DarkStructurePlan::coarsePlan() const noexcept
{
    return m_coarse;
}

const MultiscaleTransformPlan& DarkStructurePlan::maskPlan() const noexcept
{
    return m_mask;
}

std::uint64_t DarkStructurePlan::imagePasses() const noexcept
{
    if (!m_valid)
    {
        return 0u;
    }
    // Two passes for each smoothing of each cascade, one difference, two rescales, the mask
    // decomposition and its reconstruction, and one stretch and one blend per iteration.
    const std::uint64_t cascades =
        2u * static_cast<std::uint64_t>(m_fine.detailLayerCount() + m_coarse.detailLayerCount());
    const std::uint64_t masking = m_request.denoiseMask ? m_mask.imagePasses() + 1u : 0u;
    return cascades + 3u + masking + 2u * static_cast<std::uint64_t>(m_request.iterations);
}

std::uint64_t DarkStructurePlan::maskLayerBytes(std::uint32_t width, std::uint32_t height) const noexcept
{
    return m_valid && m_request.denoiseMask ? m_mask.layerBytes(width, height, 1u) : 0u;
}

Result<DarkStructurePlan> planDarkStructure(const DarkStructureRequest& request)
{
    if (request.removedLayers < kMinDarkStructureRemovedLayers ||
        request.removedLayers > kMaxDarkStructureRemovedLayers)
    {
        return darkStructureLimitFailure(
            ErrorCode::OutOfRange, kPlanOperation,
            "the removed-layer count is outside the interval this operation accepts", request.removedLayers,
            request.removedLayers < kMinDarkStructureRemovedLayers ? kMinDarkStructureRemovedLayers
                                                                   : kMaxDarkStructureRemovedLayers);
    }
    if (request.iterations == 0u || request.iterations > kMaxDarkStructureIterations)
    {
        return darkStructureLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                         "the iteration count is outside its limits", request.iterations,
                                         kMaxDarkStructureIterations);
    }
    if (!std::isfinite(request.whiteLevel) || request.whiteLevel <= 0.0)
    {
        return darkStructureFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                    "the white level has to be finite and above zero");
    }
    // The transfer function validates its own parameters, and asking it once here is what keeps this
    // domain from having a second opinion about what a midtones balance may be.
    const Result<double> probe = evaluateMidtonesTransfer(0.5, request.transfer);
    if (!probe.hasValue())
    {
        return probe.status();
    }

    MultiscaleTransformRequest fine{};
    fine.algorithm = MultiscaleAlgorithm::Starlet;
    fine.scaling = request.scaling;
    fine.spacing = MultiscaleLayerSpacing::Dyadic;
    fine.layerCount = kDarkStructureFineLayers + 1u;
    fine.output = request.output;
    const Result<MultiscaleTransformPlan> finePlan = planMultiscaleTransform(fine);
    if (!finePlan.hasValue())
    {
        return finePlan.status();
    }

    MultiscaleTransformRequest coarse = fine;
    coarse.layerCount = request.removedLayers + 1u;
    const Result<MultiscaleTransformPlan> coarsePlan = planMultiscaleTransform(coarse);
    if (!coarsePlan.hasValue())
    {
        return coarsePlan.status();
    }

    MultiscaleTransformRequest mask{};
    mask.algorithm = MultiscaleAlgorithm::Starlet;
    mask.scaling = MultiscaleScalingFunction::LinearInterpolation3;
    mask.spacing = MultiscaleLayerSpacing::Dyadic;
    mask.layerCount = kDarkStructureMaskLayers;
    mask.output = request.output;
    const Result<MultiscaleTransformPlan> maskPlan = planMultiscaleTransform(mask);
    if (!maskPlan.hasValue())
    {
        return maskPlan.status();
    }

    return DarkStructurePlan{request, finePlan.value(), coarsePlan.value(), maskPlan.value()};
}

Result<DarkStructureResult> buildImageDarkStructureMask(const EngineContext& engine, const ExecutionContext& execution,
                                                        SampleType sampleType, const void* source,
                                                        const ImageLayout& sourceLayout, const DarkStructurePlan& plan,
                                                        PlaneView<float> mask, const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready = spatial_detail::checkSpatialImage(execution, sampleType, source, sourceLayout,
                                                           plan.request().output, selection, kMaskOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (plan.finePlan().layerCount() == 0u)
    {
        return darkStructureFailure(ErrorCode::NotInitialized, kMaskOperation,
                                    "a dark-structure mask needs a plan produced by planDarkStructure");
    }
    if (!engine.valid())
    {
        return darkStructureFailure(ErrorCode::NotInitialized, kMaskOperation,
                                    "the mask decomposition needs an engine context holding a memory resource");
    }
    if (engine.id() != execution.engineContextId())
    {
        return darkStructureFailure(ErrorCode::IncompatibleArguments, kMaskOperation,
                                    "the mask layers would be owned by a context the operation was not leased from");
    }
    if (mask.width() != geometry.width || mask.height() != geometry.height)
    {
        return darkStructureFailure(ErrorCode::IncompatibleArguments, kMaskOperation,
                                    "the mask plane is not the picture being measured");
    }

    std::array<SpatialPlaneBuffer, 3u> planes{};
    std::uint64_t workingBytes = 0u;
    for (SpatialPlaneBuffer& plane : planes)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kMaskOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        plane = std::move(taken).value();
        workingBytes += plane.bytes();
    }

    DarkStructureResult result{};
    result.channelCount = geometry.channelCount;
    result.imagePasses = plan.imagePasses();
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float32;
    std::uint64_t nestedWorkingBytes = 0u;
    PassProgress progress{.total = geometry.channelCount + (plan.request().denoiseMask ? 1u : 0u)};
    const Status built = buildMask(engine, execution, sampleType, source, sourceLayout, plan, geometry,
                                   planes[0].view(), planes[1].view(), planes[2].view(), mask, selection,
                                   kMaskOperation, result, nestedWorkingBytes, progress);
    if (built.failed())
    {
        return built;
    }
    // A mask that degenerated skipped its denoise, and is still finished.
    execution.reportProgress(progress.total, progress.total, kMaskStage);

    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height;
    result.workingBytes = workingBytes + nestedWorkingBytes;
    result.run.selection = ValidityCount{.valid = samples * geometry.channelCount - result.substitutedSamples,
                                         .total = samples * geometry.channelCount};
    result.run.outputsWritten = samples;
    return result;
}

Result<DarkStructureResult> enhanceImageDarkStructures(const EngineContext& engine, const ExecutionContext& execution,
                                                       SampleType sampleType, const void* source,
                                                       const ImageLayout& sourceLayout, void* destination,
                                                       const ImageLayout& destinationLayout,
                                                       const DarkStructurePlan& plan, const PlaneView<float>* mask,
                                                       const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kEnhanceOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (plan.finePlan().layerCount() == 0u)
    {
        return darkStructureFailure(ErrorCode::NotInitialized, kEnhanceOperation,
                                    "a dark-structure enhancement needs a plan produced by planDarkStructure");
    }
    if (!engine.valid())
    {
        return darkStructureFailure(ErrorCode::NotInitialized, kEnhanceOperation,
                                    "the mask decomposition needs an engine context holding a memory resource");
    }
    if (engine.id() != execution.engineContextId())
    {
        return darkStructureFailure(ErrorCode::IncompatibleArguments, kEnhanceOperation,
                                    "the mask layers would be owned by a context the operation was not leased from");
    }
    if (mask != nullptr && (mask->width() != geometry.width || mask->height() != geometry.height))
    {
        return darkStructureFailure(ErrorCode::IncompatibleArguments, kEnhanceOperation,
                                    "the mask plane is not the picture being enhanced");
    }

    std::array<SpatialPlaneBuffer, 3u> planes{};
    std::uint64_t workingBytes = 0u;
    for (SpatialPlaneBuffer& plane : planes)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kEnhanceOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        plane = std::move(taken).value();
        workingBytes += plane.bytes();
    }
    MaskPlane maskPlane{};
    if (mask != nullptr)
    {
        maskPlane.view = *mask;
    }
    else
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kEnhanceOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        maskPlane.owned = std::move(taken).value();
        maskPlane.view = maskPlane.owned.view();
        workingBytes += maskPlane.bytes();
    }
    const PlaneView<float> maskTarget = *maskPlane.view;

    DarkStructureResult result{};
    result.channelCount = geometry.channelCount;
    result.imagePasses = plan.imagePasses();
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float32;
    std::uint64_t nestedWorkingBytes = 0u;
    PassProgress progress{.total = geometry.channelCount + (plan.request().denoiseMask ? 1u : 0u) +
                                   static_cast<std::uint64_t>(geometry.channelCount) * plan.request().iterations};
    const Status built = buildMask(engine, execution, sampleType, source, sourceLayout, plan, geometry,
                                   planes[0].view(), planes[1].view(), planes[2].view(), maskTarget, selection,
                                   kEnhanceOperation, result, nestedWorkingBytes, progress);
    if (built.failed())
    {
        return built;
    }

    StretchImageRequest stretch{};
    stretch.stretch.function = StretchFunction::ScreenTransfer;
    stretch.stretch.screenTransfer[0] = plan.request().transfer;
    stretch.stretch.screenTransfer[1] = plan.request().transfer;
    stretch.stretch.screenTransfer[2] = plan.request().transfer;
    stretch.colorMode = StretchColorMode::Independent;
    stretch.channelEnabled = {true, true, true, true};

    const PlaneView<float> work = planes[0].view();
    const PlaneView<float> stretched = planes[1].view();
    const PlaneView<const float> maskView = maskTarget.constView();
    const std::uint32_t width = geometry.width;
    const double whiteLevel = plan.request().whiteLevel;
    const double inverseWhite = 1.0 / whiteLevel;
    const std::uint32_t iterations = plan.request().iterations;

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const Status gathered = spatial_detail::gatherChannelPlane(execution, sampleType, source, sourceLayout, channel,
                                                                   work, kEnhanceOperation);
        if (gathered.failed())
        {
            return gathered;
        }
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status normalised = parallelFor(execution, geometry.height, rowCost(width, 2u), [&](TaskRange rows) {
            for (std::uint64_t index = rows.begin; index < rows.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<float> row = *work.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    row[x] =
                        sampleIsValidByValue(validity.descriptor, row[x]) && plane_detail::maskAllows(validity, x, y)
                            ? static_cast<float>(static_cast<double>(row[x]) * inverseWhite)
                            : 0.0f;
                }
            }
            return Status::success();
        });
        if (normalised.failed())
        {
            return normalised;
        }

        for (std::uint32_t iteration = 0u; iteration < iterations; ++iteration)
        {
            const Status running = execution.checkCancellation();
            if (running.failed())
            {
                return running;
            }
            const Result<StretchImageResult> transferred =
                stretchImage(execution, SampleType::Float32, work.data(), work.layout(), stretched.data(),
                             stretched.layout(), stretch);
            if (!transferred.hasValue())
            {
                return transferred.status();
            }
            const Status blended = parallelFor(execution, geometry.height, rowCost(width, 4u), [&](TaskRange rows) {
                for (std::uint64_t index = rows.begin; index < rows.end; ++index)
                {
                    const auto y = static_cast<std::uint32_t>(index);
                    const std::span<float> row = *work.row(y);
                    const std::span<const float> curve = *stretched.constView().row(y);
                    const std::span<const float> weight = *maskView.row(y);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const double alpha = static_cast<double>(weight[x]);
                        const double mixed =
                            static_cast<double>(row[x]) * (1.0 - alpha) + static_cast<double>(curve[x]) * alpha;
                        row[x] = static_cast<float>(std::clamp(mixed, 0.0, 1.0));
                    }
                }
                return Status::success();
            });
            if (blended.failed())
            {
                return blended;
            }
            // Counted from where the mask's share of the bar ends, so a mask that degenerated before
            // its denoise does not leave the enhancement one pass short of its total.
            const std::uint64_t enhancementPasses = static_cast<std::uint64_t>(geometry.channelCount) * iterations;
            progress.completed =
                progress.total - enhancementPasses + static_cast<std::uint64_t>(channel) * iterations + iteration + 1u;
            execution.reportProgress(progress.completed, progress.total, kEnhanceStage);
        }
        result.iterationsRun = iterations;

        const Status restored = parallelFor(execution, geometry.height, rowCost(width, 2u), [&](TaskRange rows) {
            for (std::uint64_t index = rows.begin; index < rows.end; ++index)
            {
                const std::span<float> row = *work.row(static_cast<std::uint32_t>(index));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    row[x] = static_cast<float>(static_cast<double>(row[x]) * whiteLevel);
                }
            }
            return Status::success();
        });
        if (restored.failed())
        {
            return restored;
        }

        // The stretched plane is no longer needed, so it carries the original samples for the
        // validity decision. The answer stays in `work`, and a rejected destination remains exactly
        // as the caller left it.
        const Status gatheredOriginal = spatial_detail::gatherChannelPlane(execution, sampleType, source, sourceLayout,
                                                                           channel, stretched, kEnhanceOperation);
        if (gatheredOriginal.failed())
        {
            return gatheredOriginal;
        }
        const Status scattered = spatial_detail::scatterChannelPlane(
            execution, work.constView(), stretched.constView(), validity, sampleType, destination, destinationLayout,
            channel, plan.request().output.writeBack, kEnhanceOperation, source, &sourceLayout, &result.samplesChanged);
        if (scattered.failed())
        {
            return scattered;
        }
    }

    execution.reportProgress(progress.total, progress.total, kEnhanceStage);
    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height;
    result.workingBytes = workingBytes + nestedWorkingBytes;
    result.run.selection = ValidityCount{.valid = samples * geometry.channelCount - result.substitutedSamples,
                                         .total = samples * geometry.channelCount};
    result.run.outputsWritten = result.run.selection.valid;
    return result;
}

} // namespace an
