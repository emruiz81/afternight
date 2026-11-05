// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - multiscale decomposition and reconstruction execution.
//
// A decomposition works one channel at a time: the channel is gathered into a Float32 plane, every
// sample the validity rule rejects is replaced by the border policy's invalid-neighbour value, and
// the layers are produced by alternating smoothings between two working planes, each detail layer
// being the difference of two successive smoothings. The layers themselves are one `ImageStorage`
// taken from the engine context, which it holds a lease on, rather than working memory released when
// the call returns, because they outlive the call. The starlet is gathered straight into its first
// layer, which its first difference then overwrites; the multiscale-linear transform smooths the
// gathered plane itself for every layer, so it gathers into a plane of its own.
// Every smoothing goes through `convolveSeparablePlane`, so the border rule, the arithmetic and the
// tiling are the stencil family's and not a second copy of them.
//
// The smoothings go into the working planes rather than into the layers because the layers are fresh
// memory and a dilated pass writes one residue class of rows per worker, so neighbouring pages would
// be first written by different workers. On Windows that was measured at between a half and two
// thirds of the rate of each worker writing a run of rows, and every layer is first written by a pass
// over contiguous rows instead - the gather, a difference or the residual's copy.
//
// The residual path runs the same smoothings in the same order without holding a detail layer,
// alternating between the answer and one working plane, and the reconstruction is one weighted sum per
// sample; both write through the family's gather and scatter when a channel is not a Float32 plane of
// its own.

#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/SpatialPlane.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace an
{
namespace
{

using spatial_detail::SpatialGeometry;
using spatial_detail::SpatialPlaneBuffer;

constexpr const char* kDecomposeOperation = "an::decomposeImageMultiscale";
constexpr const char* kReconstructOperation = "an::reconstructImageMultiscale";

/// What a decomposition says it is doing, once per layer it finishes.
constexpr const char* kDecomposeStage = "multiscale decomposition: layers";
constexpr const char* kResidualOperation = "an::smoothImageMultiscaleResidual";

/// How many samples of a row a reconstruction sums at once: 4 KiB of Float64 on a worker's stack.
constexpr std::uint32_t kReconstructionChunk = 512u;

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

/// Writes `minuend - subtrahend` into a third plane.
///
/// @param execution the operation policy and engine-context lease.
/// @param minuend the plane subtracted from.
/// @param subtrahend the plane subtracted.
/// @param destination where the difference goes. May be neither input.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status subtractPlanes(const ExecutionContext& execution, PlaneView<const float> minuend,
                                    PlaneView<const float> subtrahend, PlaneView<float> destination)
{
    const std::uint32_t width = minuend.width();
    return parallelFor(execution, minuend.height(), rowCost(width, 3u), [&](TaskRange range) {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::span<const float> from = *minuend.row(static_cast<std::uint32_t>(y));
            const std::span<const float> take = *subtrahend.row(static_cast<std::uint32_t>(y));
            const std::span<float> to = *destination.row(static_cast<std::uint32_t>(y));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                to[x] = from[x] - take[x];
            }
        }
        return Status::success();
    });
}

/// Subtracts one plane from another where the minuend lies.
///
/// A loop of its own rather than `subtractPlanes` handed one plane as both minuend and destination,
/// which that function does not admit.
///
/// @param execution the operation policy and engine-context lease.
/// @param minuend the plane subtracted from, which receives the difference.
/// @param subtrahend the plane subtracted, which must not be the minuend.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status subtractInPlace(const ExecutionContext& execution, PlaneView<float> minuend,
                                     PlaneView<const float> subtrahend)
{
    const std::uint32_t width = minuend.width();
    return parallelFor(execution, minuend.height(), rowCost(width, 2u), [&](TaskRange range) {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::span<float> into = *minuend.row(static_cast<std::uint32_t>(y));
            const std::span<const float> take = *subtrahend.row(static_cast<std::uint32_t>(y));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                into[x] = into[x] - take[x];
            }
        }
        return Status::success();
    });
}

/// Smooths one plane into another with the transform's kernel for one layer.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the validated transform.
/// @param detailLayer which detail layer is being produced.
/// @param source the plane to smooth.
/// @param destination where the smoothing goes, disjoint from the source.
/// @param scratchBytes the largest kernel working set seen so far, raised if this pass needs more.
/// @return success, or whatever the separable convolution reported.
[[nodiscard]] Status smoothForLayer(const ExecutionContext& execution, const MultiscaleTransformPlan& plan,
                                    std::uint32_t detailLayer, PlaneView<const float> source,
                                    PlaneView<float> destination, std::uint64_t& scratchBytes)
{
    std::array<double, kMaxStencilTaps> taps{};
    const std::uint32_t tapCount = plan.layerTapCount(detailLayer);
    if (tapCount == 0u || tapCount > taps.size())
    {
        return multiscaleFailure(ErrorCode::Internal, kDecomposeOperation,
                                 "the plan named a kernel this transform cannot build");
    }
    if (plan.request().algorithm == MultiscaleAlgorithm::Starlet)
    {
        const Result<std::uint32_t> written =
            writeMultiscaleScalingTaps(plan.request().scaling, std::span<double>{taps.data(), tapCount});
        if (!written.hasValue())
        {
            return written.status();
        }
    }
    else
    {
        const Result<std::uint32_t> written =
            writeGaussianTaps(plan.layerSigma(detailLayer), tapCount / 2u, std::span<double>{taps.data(), tapCount});
        if (!written.hasValue())
        {
            return written.status();
        }
    }

    const std::span<const double> pass{taps.data(), tapCount};
    const SeparableStencilRequest request{.horizontalTaps = pass,
                                          .verticalTaps = pass,
                                          .borders = plan.request().borders,
                                          .dilation = plan.layerDilation(detailLayer)};
    // Every source sample already means something by the time a smoothing runs: the gather resolved
    // the validity rule into the plane, so the kernel is asked for the unconditional filter.
    const Result<StencilExecutionResult> ran =
        convolveSeparablePlane(execution, source, destination, request, KernelValidityView{});
    if (!ran.hasValue())
    {
        return ran.status();
    }
    scratchBytes = (std::max)(scratchBytes, ran.value().scratchBytes);
    return Status::success();
}

/// Decomposes one gathered channel into its layers.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the validated transform.
/// @param gathered the channel's samples: the first layer itself for the starlet, which its first
///        difference overwrites, and a plane of its own for the multiscale-linear transform, which
///        every smoothing reads and which is left as it was.
/// @param working two planes holding consecutive smoothings.
/// @param layers one plane per layer of this channel, the residual last.
/// @param scratchBytes the largest kernel working set seen so far, raised as passes run.
/// @return success, or the first failure a smoothing, a subtraction or a copy reported.
[[nodiscard]] Status decomposeChannel(const ExecutionContext& execution, const MultiscaleTransformPlan& plan,
                                      PlaneView<const float> gathered, std::span<const SpatialPlaneBuffer> working,
                                      std::span<const std::optional<PlaneView<float>>> layers,
                                      std::uint64_t& scratchBytes)
{
    const std::uint32_t detailCount = plan.detailLayerCount();
    const bool starlet = plan.request().algorithm == MultiscaleAlgorithm::Starlet;

    PlaneView<float> current = working[0].view();
    PlaneView<float> next = working[1].view();

    for (std::uint32_t detail = 0u; detail < detailCount; ++detail)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        const PlaneView<const float> smoothed = detail == 0u ? gathered : PlaneView<const float>{current};
        const Status smoothing =
            smoothForLayer(execution, plan, detail, starlet ? smoothed : gathered, next, scratchBytes);
        if (smoothing.failed())
        {
            return smoothing;
        }
        // The starlet's first layer is the gathered plane, so its first difference is taken where it
        // lies; every other difference goes into a layer nothing has written yet.
        const Status subtracted =
            detail == 0u && starlet
                ? subtractInPlace(execution, *layers[0], PlaneView<const float>{next})
                : subtractPlanes(execution, smoothed, PlaneView<const float>{next}, *layers[detail]);
        if (subtracted.failed())
        {
            return subtracted;
        }
        // `next` holds the smoothing this layer subtracted, which is what the layer after it starts
        // from; swapping rather than copying is what keeps the working set at two planes.
        std::swap(current, next);
        execution.reportProgress(detail + 1u, detailCount + 1u, kDecomposeStage);
    }

    const Status residual = spatial_detail::copyPlane(execution, PlaneView<const float>{current}, *layers[detailCount],
                                                      kDecomposeOperation);
    if (residual.failed())
    {
        return residual;
    }
    execution.reportProgress(detailCount + 1u, detailCount + 1u, kDecomposeStage);
    return Status::success();
}
} // namespace

Result<MultiscaleDecomposition> decomposeImageMultiscale(const EngineContext& engine, const ExecutionContext& execution,
                                                         SampleType sampleType, const void* source,
                                                         const ImageLayout& sourceLayout,
                                                         const MultiscaleTransformPlan& plan,
                                                         const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready = spatial_detail::checkSpatialImage(
        execution, sampleType, source, sourceLayout, plan.request().output, selection, kDecomposeOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    const Result<MultiscaleTransformPlan> revalidated = planMultiscaleTransform(plan.request());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }
    if (revalidated->layerCount() != plan.layerCount())
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kDecomposeOperation,
                                 "the plan was not produced by planMultiscaleTransform");
    }
    if (!engine.valid())
    {
        return multiscaleFailure(ErrorCode::NotInitialized, kDecomposeOperation,
                                 "the layers need an engine context holding a memory resource");
    }
    if (engine.id() != execution.engineContextId())
    {
        return multiscaleFailure(ErrorCode::IncompatibleArguments, kDecomposeOperation,
                                 "the layers would be owned by a context the operation was not leased from");
    }

    const std::uint32_t layerCount = plan.layerCount();
    const std::uint64_t planeCount = static_cast<std::uint64_t>(geometry.channelCount) * layerCount;
    const std::optional<ImageExtent> extent =
        ImageExtent::create(geometry.width, geometry.height, static_cast<std::uint32_t>(planeCount), engine.limits());
    if (!extent.has_value() || planeCount > engine.limits().maxChannelCount)
    {
        return multiscaleLimitFailure(ErrorCode::OutOfRange, kDecomposeOperation,
                                      "the layers of this picture need more planes than the engine admits", planeCount,
                                      engine.limits().maxChannelCount);
    }
    const ImageLayoutRequest layerRequest{.extent = *extent,
                                          .elementSizeBytes = static_cast<std::uint32_t>(sizeof(float)),
                                          .samplesPerPixel = 1u,
                                          .planeCount = static_cast<std::uint32_t>(planeCount),
                                          .rowAlignmentBytes = kDefaultBufferAlignmentBytes};
    // Uninitialized rather than zeroed: every coefficient of every layer is written before this
    // returns, and a decomposition that failed part way is destroyed rather than handed back, so
    // there is no path on which a caller reads a sample nothing wrote. Zeroing would cost a full
    // pass over layers that are about to be overwritten.
    Result<ImageStorage<float>> planes =
        ImageStorage<float>::create(engine, layerRequest, StorageInitialization::Uninitialized);
    if (!planes.hasValue())
    {
        return planes.status();
    }
    const std::optional<ImageView<float>> layerViews = planes->view();
    if (!layerViews.has_value())
    {
        return multiscaleFailure(ErrorCode::Internal, kDecomposeOperation, "the layer memory describes no image");
    }

    constexpr std::uint32_t kWorkingCount = 2u;
    std::array<SpatialPlaneBuffer, 2u> working{};
    std::uint64_t workingBytes = 0u;
    for (std::uint32_t index = 0u; index < kWorkingCount; ++index)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kDecomposeOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        working[index] = std::move(taken).value();
        workingBytes += working[index].bytes();
    }
    // The starlet reads the gathered plane once, as its first smoothing input and its first minuend,
    // so it is gathered into its first layer. The multiscale-linear transform reads the gathered
    // plane again for every layer, so it gathers into a plane of its own.
    const bool starlet = plan.request().algorithm == MultiscaleAlgorithm::Starlet;
    SpatialPlaneBuffer gatheredPlane;
    if (!starlet)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kDecomposeOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        gatheredPlane = std::move(taken).value();
        workingBytes += gatheredPlane.bytes();
    }

    MultiscaleTransformResult result{};
    result.channelCount = geometry.channelCount;
    result.layerCount = layerCount;
    result.imagePasses = plan.imagePasses();
    result.layerBytes = planes->capacityBytes();
    result.precision = ComputePrecision::Float32;
    result.plannedWorkers = execution.admittedConcurrency();
    const std::uint64_t samplesPerPlane = static_cast<std::uint64_t>(geometry.width) * geometry.height;

    std::uint64_t kernelScratchBytes = 0u;
    std::array<std::optional<PlaneView<float>>, kMaxMultiscaleLayers> channelLayers{};
    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        for (std::uint32_t layer = 0u; layer < layerCount; ++layer)
        {
            channelLayers[layer] = layerViews->plane(channel * layerCount + layer);
            if (!channelLayers[layer].has_value())
            {
                return multiscaleFailure(ErrorCode::Internal, kDecomposeOperation,
                                         "a layer of the decomposition is not addressable as a plane");
            }
        }
        const PlaneView<float> gathered = starlet ? *channelLayers[0] : gatheredPlane.view();
        const Status gatheredChannel = spatial_detail::gatherChannelPlane(execution, sampleType, source, sourceLayout,
                                                                          channel, gathered, kDecomposeOperation);
        if (gatheredChannel.failed())
        {
            return gatheredChannel;
        }
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status substituted = spatial_detail::substituteRejectedSamples(
            execution, gathered, validity, plan.request().borders.invalidNeighbor, result.substitutedSamples,
            kDecomposeOperation);
        if (substituted.failed())
        {
            return substituted;
        }
        // Each channel is one share of the operation's progress, measured in layers, so its layers move
        // the bar one at a time.
        const std::uint64_t layers = plan.detailLayerCount() + 1u;
        const ProgressRange share(execution, channel * layers, (channel + 1u) * layers, geometry.channelCount * layers);
        const Status decomposed = decomposeChannel(
            execution, plan, PlaneView<const float>{gathered},
            std::span<const SpatialPlaneBuffer>{working.data(), kWorkingCount},
            std::span<const std::optional<PlaneView<float>>>{channelLayers.data(), layerCount}, kernelScratchBytes);
        if (decomposed.failed())
        {
            return decomposed;
        }
    }

    result.run.selection = ValidityCount{.valid = samplesPerPlane * geometry.channelCount - result.substitutedSamples,
                                         .total = samplesPerPlane * geometry.channelCount};
    result.run.outputsWritten = samplesPerPlane * planeCount;
    result.workingBytes = workingBytes + kernelScratchBytes;
    return MultiscaleDecomposition{std::move(planes).value(), plan, result};
}

Result<SpatialFilterResult> reconstructImageMultiscale(const ExecutionContext& execution,
                                                       const MultiscaleDecomposition& decomposition,
                                                       SampleType sampleType, void* destination,
                                                       const ImageLayout& destinationLayout,
                                                       const MultiscaleReconstructionRequest& request)
{
    SpatialGeometry geometry{};
    const SpatialSelection everySample{};
    const Status ready =
        spatial_detail::checkSpatialImage(execution, sampleType, destination, destinationLayout, request.output,
                                          everySample, kReconstructOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (!decomposition.valid())
    {
        return multiscaleFailure(ErrorCode::NotInitialized, kReconstructOperation,
                                 "a reconstruction needs a decomposition holding layers");
    }
    if (decomposition.width() != geometry.width || decomposition.height() != geometry.height ||
        decomposition.channelCount() != geometry.channelCount)
    {
        return multiscaleFailure(ErrorCode::IncompatibleArguments, kReconstructOperation,
                                 "the destination is not the picture this decomposition holds the layers of");
    }
    const std::uint32_t layerCount = decomposition.layerCount();
    if (!request.layers.empty() && request.layers.size() != layerCount)
    {
        return multiscaleLimitFailure(ErrorCode::IncompatibleArguments, kReconstructOperation,
                                      "a reconstruction takes one weight per layer, or none at all",
                                      request.layers.size(), layerCount);
    }
    std::array<double, kMaxMultiscaleLayers> gains{};
    for (std::uint32_t layer = 0u; layer < layerCount; ++layer)
    {
        if (request.layers.empty())
        {
            gains[layer] = 1.0;
            continue;
        }
        const MultiscaleLayerWeight& weight = request.layers[layer];
        if (!std::isfinite(weight.gain))
        {
            return multiscaleFailure(ErrorCode::InvalidArgument, kReconstructOperation, "a layer gain is not finite");
        }
        gains[layer] = weight.enabled ? weight.gain : 0.0;
    }
    if (request.clamp.has_value() && (!std::isfinite(request.clamp->lowest) || !std::isfinite(request.clamp->highest) ||
                                      request.clamp->lowest > request.clamp->highest))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kReconstructOperation,
                                 "a reconstruction clamp needs two finite endpoints in order");
    }

    const bool writeInPlace = spatial_detail::channelIsAddressablePlane(sampleType, destinationLayout);
    SpatialPlaneBuffer workingPlane;
    if (!writeInPlace)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kReconstructOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        workingPlane = std::move(taken).value();
    }

    SpatialFilterResult result{};
    result.channelCount = geometry.channelCount;
    result.imagePasses = 1u;
    result.workingBytes = workingPlane.bytes();
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float32;
    result.readInPlace = false;
    result.wroteInPlace = writeInPlace;
    const double lowest = request.clamp.has_value() ? request.clamp->lowest : 0.0;
    const double highest = request.clamp.has_value() ? request.clamp->highest : 0.0;
    const bool clamping = request.clamp.has_value();
    const std::uint64_t samplesPerPlane = static_cast<std::uint64_t>(geometry.width) * geometry.height;

    std::array<std::optional<PlaneView<const float>>, kMaxMultiscaleLayers> channelLayers{};
    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        for (std::uint32_t layer = 0u; layer < layerCount; ++layer)
        {
            channelLayers[layer] = decomposition.layer(channel, layer);
            if (!channelLayers[layer].has_value())
            {
                return multiscaleFailure(ErrorCode::Internal, kReconstructOperation,
                                         "a layer of the decomposition is not addressable as a plane");
            }
        }
        const std::optional<PlaneView<float>> borrowed =
            writeInPlace
                ? spatial_detail::borrowDestinationPlane(destination, destinationLayout, channel, sampleType, geometry)
                : std::nullopt;
        if (writeInPlace && !borrowed.has_value())
        {
            return multiscaleFailure(ErrorCode::InvalidArgument, kReconstructOperation,
                                     "a channel of this image is not addressable as a plane");
        }
        const PlaneView<float> output = writeInPlace ? *borrowed : workingPlane.view();
        const std::uint32_t width = geometry.width;
        const Status summed =
            parallelFor(execution, geometry.height, rowCost(width, static_cast<std::uint64_t>(layerCount) + 1u),
                        [&](TaskRange range) {
                            // Each sample's sum starts from positive zero and takes the enabled layers in order,
                            // but a run of samples is summed a layer at a time into a Float64 buffer of the
                            // worker's own, which is a loop the compiler vectorizes; the order within one
                            // sample's sum is unchanged, and so is every rounding.
                            std::array<double, kReconstructionChunk> sums;
                            for (std::uint64_t y = range.begin; y < range.end; ++y)
                            {
                                const std::span<float> to = *output.row(static_cast<std::uint32_t>(y));
                                for (std::uint32_t start = 0u; start < width; start += kReconstructionChunk)
                                {
                                    const std::uint32_t count = (std::min)(kReconstructionChunk, width - start);
                                    std::fill_n(sums.data(), count, 0.0);
                                    for (std::uint32_t layer = 0u; layer < layerCount; ++layer)
                                    {
                                        const double gain = gains[layer];
                                        if (gain == 0.0)
                                        {
                                            continue;
                                        }
                                        const float* const from =
                                            channelLayers[layer]->row(static_cast<std::uint32_t>(y))->data();
                                        for (std::uint32_t x = 0u; x < count; ++x)
                                        {
                                            sums[x] += gain * static_cast<double>(from[start + x]);
                                        }
                                    }
                                    for (std::uint32_t x = 0u; x < count; ++x)
                                    {
                                        const double sum = clamping ? std::clamp(sums[x], lowest, highest) : sums[x];
                                        to[start + x] = static_cast<float>(sum);
                                    }
                                }
                            }
                            return Status::success();
                        });
        if (summed.failed())
        {
            return summed;
        }
        if (!writeInPlace)
        {
            const Status scattered = spatial_detail::scatterChannelPlane(
                execution, workingPlane.constView(), workingPlane.constView(), KernelValidityView{}, sampleType,
                destination, destinationLayout, channel, request.output.writeBack, kReconstructOperation);
            if (scattered.failed())
            {
                return scattered;
            }
        }
    }

    result.run.selection = ValidityCount{.valid = samplesPerPlane * geometry.channelCount,
                                         .total = samplesPerPlane * geometry.channelCount};
    result.run.outputsWritten = samplesPerPlane * geometry.channelCount;
    return result;
}

Result<SpatialFilterResult> smoothImageMultiscaleResidual(const ExecutionContext& execution, SampleType sampleType,
                                                          const void* source, const ImageLayout& sourceLayout,
                                                          void* destination, const ImageLayout& destinationLayout,
                                                          const MultiscaleTransformPlan& plan,
                                                          const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kResidualOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    const Result<MultiscaleTransformPlan> revalidated = planMultiscaleTransform(plan.request());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }
    if (revalidated->layerCount() != plan.layerCount())
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kResidualOperation,
                                 "the plan was not produced by planMultiscaleTransform");
    }

    const bool starlet = plan.request().algorithm == MultiscaleAlgorithm::Starlet;
    const std::uint32_t detailCount = plan.detailLayerCount();
    // The multiscale-linear transform smooths the source itself for every layer rather than folding
    // one smoothing into the next, so its residual is the deepest layer's smoothing alone and the
    // cascade collapses to a single pass. The starlet's does not.
    const std::uint32_t passCount = starlet ? detailCount : 1u;

    const bool writeInPlace = spatial_detail::channelIsAddressablePlane(sampleType, destinationLayout);
    // The cascade alternates between the answer and this plane, and the source is gathered into
    // whichever of the two lies an even number of passes before the answer.
    Result<SpatialPlaneBuffer> alternate = SpatialPlaneBuffer::own(execution, geometry, kResidualOperation);
    if (!alternate.hasValue())
    {
        return alternate.status();
    }
    SpatialPlaneBuffer outputWorking;
    if (!writeInPlace)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, kResidualOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        outputWorking = std::move(taken).value();
    }

    SpatialFilterResult result{};
    result.channelCount = geometry.channelCount;
    result.imagePasses = 2u * static_cast<std::uint64_t>(passCount);
    result.workingBytes = alternate->bytes() + outputWorking.bytes();
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float32;
    result.readInPlace = false;
    result.wroteInPlace = writeInPlace;
    // How far a sample can travel through the whole cascade, which for the starlet is the sum of
    // every layer's reach rather than the deepest one's.
    std::uint32_t reach = 0u;
    for (std::uint32_t pass = 0u; pass < passCount; ++pass)
    {
        reach += plan.layerRadius(starlet ? pass : detailCount - 1u);
    }
    result.horizontalRadius = reach;
    result.verticalRadius = reach;

    const std::uint64_t samplesPerPlane = static_cast<std::uint64_t>(geometry.width) * geometry.height;
    std::uint64_t substituted = 0u;
    std::uint64_t kernelScratchBytes = 0u;
    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const std::optional<PlaneView<float>> borrowed =
            writeInPlace
                ? spatial_detail::borrowDestinationPlane(destination, destinationLayout, channel, sampleType, geometry)
                : std::nullopt;
        if (writeInPlace && !borrowed.has_value())
        {
            return multiscaleFailure(ErrorCode::InvalidArgument, kResidualOperation,
                                     "a channel of this image is not addressable as a plane");
        }
        const PlaneView<float> answer = writeInPlace ? *borrowed : outputWorking.view();

        // The last smoothing has to land in the answer, so an even number of passes starts there and
        // an odd number starts in the other plane.
        PlaneView<float> current = passCount % 2u == 0u ? answer : alternate->view();
        PlaneView<float> next = passCount % 2u == 0u ? alternate->view() : answer;
        const Status gatheredChannel = spatial_detail::gatherChannelPlane(execution, sampleType, source, sourceLayout,
                                                                          channel, current, kResidualOperation);
        if (gatheredChannel.failed())
        {
            return gatheredChannel;
        }
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status replaced = spatial_detail::substituteRejectedSamples(
            execution, current, validity, plan.request().borders.invalidNeighbor, substituted, kResidualOperation);
        if (replaced.failed())
        {
            return replaced;
        }

        for (std::uint32_t pass = 0u; pass < passCount; ++pass)
        {
            const Status running = execution.checkCancellation();
            if (running.failed())
            {
                return running;
            }
            const std::uint32_t layer = starlet ? pass : detailCount - 1u;
            const Status smoothing =
                smoothForLayer(execution, plan, layer, PlaneView<const float>{current}, next, kernelScratchBytes);
            if (smoothing.failed())
            {
                return smoothing;
            }
            std::swap(current, next);
        }

        if (!writeInPlace)
        {
            const Status scattered = spatial_detail::scatterChannelPlane(
                execution, outputWorking.constView(), outputWorking.constView(), KernelValidityView{}, sampleType,
                destination, destinationLayout, channel, plan.request().output.writeBack, kResidualOperation);
            if (scattered.failed())
            {
                return scattered;
            }
        }
    }

    result.workingBytes += kernelScratchBytes;
    result.run.selection = ValidityCount{.valid = samplesPerPlane * geometry.channelCount - substituted,
                                         .total = samplesPerPlane * geometry.channelCount};
    result.run.outputsWritten = samplesPerPlane * geometry.channelCount;
    return result;
}

} // namespace an
