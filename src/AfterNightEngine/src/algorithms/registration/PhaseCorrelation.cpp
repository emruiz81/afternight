// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the registration domain's phase-correlation translation measurement.
//
// The shape of the computation is fixed by the mathematics - transform, whiten, invert, find the
// peak - and everything this file adds is the bookkeeping that makes each stage checkable: the two
// frames land in in-place padded transform buffers taken from the engine context, the plans come
// from the FFTW adapter domain with the geometry validated before the library sees a pointer, the
// pointwise stages run on the operation's workers with cancellation at chunk boundaries, and the
// peak is refined the way OpenCV's `phaseCorrelate` refines it - the weighted centroid of the
// five-by-five neighbourhood in the centred surface, clamped at the frame edge.

#include <AfterNightEngine/Algorithms/Registration/PhaseCorrelation.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Interop/FftwAdapter.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "interop/fftw/FftwRealTransform.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::computeImagePhaseCorrelation";

/// One failure from this domain.
///
/// @param code which failure.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status correlationFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract requires.
/// @return the failure, carrying both counts.
[[nodiscard]] Status correlationLimitFailure(ErrorCode code, const char* message, std::uint64_t requested,
                                             std::uint64_t limit)
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = kOperation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                          StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

/// What one gathered or normalized row costs, for the grain policy.
///
/// @param width how many pixels one row holds.
/// @return the cost of one row.
[[nodiscard]] TaskCost rowCost(std::uint32_t width) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * 8u, .operationsPerItem = samples * 4u};
}

/// Whether one frame's pointer and layout describe a single-channel plane of this operation.
///
/// @param sampleType what the samples claim to be.
/// @param samples where they start.
/// @param layout where they sit.
/// @param sideName which frame a failure names.
/// @return success, or the first reason the frame cannot be measured.
[[nodiscard]] Status checkFrame(SampleType sampleType, const void* samples, const ImageLayout& layout,
                                const char* sideName)
{
    if (samples == nullptr)
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = "a phase correlation needs both frames",
                                            .operation = kOperation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, sideName)}});
    }
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return correlationFailure(ErrorCode::InvalidArgument,
                                  "a layout's element size is not the stated representation's");
    }
    if (layout.extent().channelCount() != 1u)
    {
        return correlationFailure(ErrorCode::InvalidArgument,
                                  "a phase correlation measures one channel against one channel; state the colour "
                                  "conversion you meant and hand it the result");
    }
    return Status::success();
}

/// The transform buffers, plans and geometry of one measurement.
struct CorrelationWorkspace
{
    /// How many pixels one row holds.
    std::uint32_t width = 0u;

    /// How many rows the frames hold.
    std::uint32_t height = 0u;

    /// How many reals one padded in-place row holds.
    std::uint64_t paddedRowFloats = 0u;

    /// How many complex bins one spectrum row holds.
    std::uint32_t spectrumColumns = 0u;

    /// The reference frame's transform buffer.
    plane_detail::ScopedContextMemory reference{};

    /// The target frame's transform buffer.
    plane_detail::ScopedContextMemory target{};

    /// The first real of the reference buffer.
    float* referenceBase = nullptr;

    /// The first real of the target buffer.
    float* targetBase = nullptr;
};

/// Widens one frame into a padded transform buffer, summing rows for its mean.
///
/// @tparam SampleT the resident representation.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked resident channel.
/// @param workspace the geometry and buffers.
/// @param destination the first real of the buffer this frame lands in.
/// @param rowSums one double per row, written with each row's exact sum.
/// @return success; `ErrorCode::InvalidData` for a floating-point sample that is not finite; or an
///         execution or cancellation failure.
template <ResidentSample SampleT>
[[nodiscard]] Status gatherFrame(const ExecutionContext& execution, const KernelPlaneSource& source,
                                 const CorrelationWorkspace& workspace, float* destination, std::span<double> rowSums)
{
    const plane_detail::PlaneSampleReader<SampleT> reader{source, KernelValidityView{}};
    const std::uint32_t width = workspace.width;
    const std::uint32_t columnStride = reader.columnStride();
    return parallelFor(execution, workspace.height, rowCost(width), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const SampleT* sample = reader.rowStart(y);
            float* out = destination + index * workspace.paddedRowFloats;
            double sum = 0.0;
            for (std::uint32_t x = 0u; x < width; ++x, sample += columnStride)
            {
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    if (!sampleIsFinite(*sample))
                    {
                        return correlationFailure(ErrorCode::InvalidData,
                                                  "a transform spreads a sample that is not a number over every "
                                                  "bin; repair or reject it before measuring");
                    }
                }
                const float value = static_cast<float>(*sample);
                out[x] = value;
                sum += static_cast<double>(value);
            }
            rowSums[static_cast<std::size_t>(index)] = sum;
        }
        return Status::success();
    });
}

/// Subtracts a mean and applies a window over one transform buffer, in place.
///
/// @param execution the operation policy and engine-context lease.
/// @param workspace the geometry and buffers.
/// @param base the first real of the buffer to normalize.
/// @param mean what to subtract, or zero when the mean stays in.
/// @param columnWindow one factor per column, or empty for no window.
/// @param rowWindow one factor per row, or empty for no window.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status normalizeFrame(const ExecutionContext& execution, const CorrelationWorkspace& workspace,
                                    float* base, double mean, std::span<const double> columnWindow,
                                    std::span<const double> rowWindow)
{
    const std::uint32_t width = workspace.width;
    return parallelFor(execution, workspace.height, rowCost(width), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            float* row = base + index * workspace.paddedRowFloats;
            const double rowFactor = rowWindow.empty() ? 1.0 : rowWindow[static_cast<std::size_t>(index)];
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double centred = static_cast<double>(row[x]) - mean;
                const double factor = columnWindow.empty() ? rowFactor : rowFactor * columnWindow[x];
                row[x] = static_cast<float>(centred * factor);
            }
        }
        return Status::success();
    });
}

/// Whitens the cross-spectrum of the two transformed buffers into the reference buffer.
///
/// Each bin becomes the unit-magnitude direction of `F(target) * conj(F(reference))`, computed in
/// Float64; a bin whose magnitude is exactly zero has no direction and becomes zero, which is what
/// makes a structureless pair report a response of zero instead of dividing by nothing.
///
/// @param execution the operation policy and engine-context lease.
/// @param workspace the geometry and buffers, whose reference buffer is overwritten.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status whitenCrossSpectrum(const ExecutionContext& execution, const CorrelationWorkspace& workspace)
{
    const std::uint32_t columns = workspace.spectrumColumns;
    return parallelFor(execution, workspace.height, rowCost(columns * 2u), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            float* reference = workspace.referenceBase + index * workspace.paddedRowFloats;
            const float* target = workspace.targetBase + index * workspace.paddedRowFloats;
            for (std::uint32_t bin = 0u; bin < columns; ++bin)
            {
                const double referenceRe = reference[2u * bin];
                const double referenceIm = reference[2u * bin + 1u];
                const double targetRe = target[2u * bin];
                const double targetIm = target[2u * bin + 1u];
                const double re = targetRe * referenceRe + targetIm * referenceIm;
                const double im = targetIm * referenceRe - targetRe * referenceIm;
                const double magnitude = std::sqrt(re * re + im * im);
                if (magnitude > 0.0)
                {
                    reference[2u * bin] = static_cast<float>(re / magnitude);
                    reference[2u * bin + 1u] = static_cast<float>(im / magnitude);
                }
                else
                {
                    reference[2u * bin] = 0.0f;
                    reference[2u * bin + 1u] = 0.0f;
                }
            }
        }
        return Status::success();
    });
}

/// One sample of the centred correlation surface.
///
/// The surface lives in the reference buffer in transform order; centring is a cyclic shift by half
/// the extent in each axis, applied here as index arithmetic rather than as a pass over the memory.
/// The convention is that a displacement of zero stands at index `extent / 2` in each axis - exact
/// for every extent, even or odd, which is what lets the translation subtract that index rather
/// than a half-integer that no bin occupies.
///
/// @param workspace the geometry and buffers.
/// @param shiftedX the column in the centred surface.
/// @param shiftedY the row in the centred surface.
/// @return the sample.
[[nodiscard]] float centredSurfaceAt(const CorrelationWorkspace& workspace, std::uint32_t shiftedX,
                                     std::uint32_t shiftedY) noexcept
{
    const std::uint32_t x = (shiftedX + workspace.width - workspace.width / 2u) % workspace.width;
    const std::uint32_t y = (shiftedY + workspace.height - workspace.height / 2u) % workspace.height;
    return workspace.referenceBase[static_cast<std::uint64_t>(y) * workspace.paddedRowFloats + x];
}

} // namespace

const char* phaseCorrelationWindowName(PhaseCorrelationWindow window) noexcept
{
    switch (window)
    {
    case PhaseCorrelationWindow::None:
        return "None";
    case PhaseCorrelationWindow::Hann:
        return "Hann";
    }
    return "UnknownPhaseCorrelationWindow";
}

Result<PhaseCorrelationResult> computeImagePhaseCorrelation(const ExecutionContext& execution, SampleType sampleType,
                                                            const void* reference, const ImageLayout& referenceLayout,
                                                            const void* target, const ImageLayout& targetLayout,
                                                            const PhaseCorrelationRequest& request)
{
    if (!execution.valid())
    {
        return correlationFailure(ErrorCode::NotInitialized,
                                  "a phase correlation needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!isSampleType(sampleType))
    {
        return correlationFailure(ErrorCode::InvalidArgument,
                                  "the representation is not one of the eight this engine stores");
    }
    if (!isPhaseCorrelationWindow(request.window))
    {
        return correlationFailure(ErrorCode::InvalidArgument, "the window is not one this build implements");
    }
    const Status referenceReady = checkFrame(sampleType, reference, referenceLayout, "reference");
    if (referenceReady.failed())
    {
        return referenceReady;
    }
    const Status targetReady = checkFrame(sampleType, target, targetLayout, "target");
    if (targetReady.failed())
    {
        return targetReady;
    }
    const std::uint32_t width = referenceLayout.extent().width();
    const std::uint32_t height = referenceLayout.extent().height();
    if (targetLayout.extent().width() != width || targetLayout.extent().height() != height)
    {
        return correlationFailure(ErrorCode::IncompatibleArguments,
                                  "the two frames are not the same extent; crop or resample the one you mean to "
                                  "change, rather than having a measurement decide");
    }
    if (width < kMinPhaseCorrelationExtent || height < kMinPhaseCorrelationExtent)
    {
        return correlationLimitFailure(ErrorCode::OutOfRange,
                                       "a frame this small is mostly its own refinement neighbourhood",
                                       (std::min)(width, height), kMinPhaseCorrelationExtent);
    }

    CorrelationWorkspace workspace{};
    workspace.width = width;
    workspace.height = height;
    workspace.spectrumColumns = fftwHermitianColumns(width);
    workspace.paddedRowFloats = fftwRealRowReals(width, FftwPlacement::InPlace);
    const std::uint64_t bufferBytes = static_cast<std::uint64_t>(height) * workspace.paddedRowFloats * sizeof(float);

    Result<plane_detail::ScopedContextMemory> referenceTaken =
        plane_detail::ScopedContextMemory::take(execution, bufferBytes, kDefaultBufferAlignmentBytes, kOperation);
    if (!referenceTaken.hasValue())
    {
        return referenceTaken.status();
    }
    workspace.reference = std::move(referenceTaken).value();
    Result<plane_detail::ScopedContextMemory> targetTaken =
        plane_detail::ScopedContextMemory::take(execution, bufferBytes, kDefaultBufferAlignmentBytes, kOperation);
    if (!targetTaken.hasValue())
    {
        return targetTaken.status();
    }
    workspace.target = std::move(targetTaken).value();
    // One double per row for the exact means, taken from the context so the operation's whole
    // working set is counted memory.
    Result<plane_detail::ScopedContextMemory> sumsTaken = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(height) * sizeof(double) * 2u, alignof(double), kOperation);
    if (!sumsTaken.hasValue())
    {
        return sumsTaken.status();
    }
    const plane_detail::ScopedContextMemory rowSums = std::move(sumsTaken).value();
    workspace.referenceBase = static_cast<float*>(workspace.reference.data());
    workspace.targetBase = static_cast<float*>(workspace.target.data());

    PhaseCorrelationResult answer{};
    answer.workingBytes = workspace.reference.bytes() + workspace.target.bytes() + rowSums.bytes();
    answer.plannedWorkers = plannedWorkerCount(execution, height, rowCost(width));
    answer.precision = ComputePrecision::Float32;

    // Gather both frames, each with its row sums, reading the resident samples where they lie.
    const std::span<double> referenceSums{static_cast<double*>(rowSums.data()), height};
    const std::span<double> targetSums{static_cast<double*>(rowSums.data()) + height, height};
    struct FrameJob
    {
        const void* samples;
        const ImageLayout* layout;
        float* base;
        std::span<double> sums;
    };
    const FrameJob jobs[2] = {{reference, &referenceLayout, workspace.referenceBase, referenceSums},
                              {target, &targetLayout, workspace.targetBase, targetSums}};
    for (const FrameJob& job : jobs)
    {
        const KernelPlaneSource source{
            .sampleType = sampleType, .samples = job.samples, .layout = job.layout, .plane = 0u, .sampleInPixel = 0u};
        const Status checked = plane_detail::checkPlaneSource(execution, source, KernelValidityView{}, kOperation);
        if (checked.failed())
        {
            return checked;
        }
        Status gathered = Status::success();
        const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            gathered = gatherFrame<SampleT>(execution, source, workspace, job.base, job.sums);
        });
        if (!known)
        {
            return correlationFailure(ErrorCode::InvalidArgument,
                                      "the representation is not one of the eight this engine stores");
        }
        if (gathered.failed())
        {
            return gathered;
        }
    }

    // Normalize: the mean first, then the window, the order in which they mean anything - a window
    // applied first would put the mean back into the edges.
    const bool windowed = request.window == PhaseCorrelationWindow::Hann;
    if (request.subtractMean || windowed)
    {
        Result<plane_detail::ScopedContextMemory> windowTaken = plane_detail::ScopedContextMemory::take(
            execution, windowed ? (static_cast<std::uint64_t>(width) + height) * sizeof(double) : 0u, alignof(double),
            kOperation);
        if (!windowTaken.hasValue())
        {
            return windowTaken.status();
        }
        const plane_detail::ScopedContextMemory windowMemory = std::move(windowTaken).value();
        std::span<const double> columnWindow{};
        std::span<const double> rowWindow{};
        if (windowed)
        {
            auto* const factors = static_cast<double*>(windowMemory.data());
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                factors[x] = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * x / (width - 1u)));
            }
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                factors[width + y] = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * y / (height - 1u)));
            }
            columnWindow = std::span<const double>{factors, width};
            rowWindow = std::span<const double>{factors + width, height};
        }
        const double pixelCount = static_cast<double>(width) * static_cast<double>(height);
        for (const FrameJob& job : jobs)
        {
            double mean = 0.0;
            if (request.subtractMean)
            {
                double total = 0.0;
                for (std::uint32_t y = 0u; y < height; ++y)
                {
                    total += job.sums[y];
                }
                mean = total / pixelCount;
            }
            const Status normalized = normalizeFrame(execution, workspace, job.base, mean, columnWindow, rowWindow);
            if (normalized.failed())
            {
                return normalized;
            }
        }
    }

    // The three transforms. One forward plan serves both frames through the new-array interface,
    // because the two buffers share their geometry and alignment by construction.
    const ExternalBufferContract contract{.engineDomain = NumericDomain::PhysicalScaled,
                                          .libraryDomain = NumericDomain::PhysicalScaled,
                                          .ownership = BufferOwnership::EngineOwned,
                                          .scope = BorrowScope::Operation,
                                          .mutability = BufferMutability::ReadWrite};
    const std::optional<PlaneView<float>> referencePlane = PlaneView<float>::create(
        std::span<float>{workspace.referenceBase, static_cast<std::size_t>(height) * workspace.paddedRowFloats}, width,
        height, 1u, workspace.paddedRowFloats * sizeof(float));
    if (!referencePlane.has_value())
    {
        return correlationFailure(ErrorCode::Internal, "the transform buffer does not describe a plane");
    }
    const std::optional<FftwArrayDescription> described =
        describeAsFftwReal(*referencePlane, contract, FftwPlacement::InPlace, kDefaultBufferAlignmentBytes);
    if (!described.has_value())
    {
        return correlationFailure(ErrorCode::Internal, "the transform buffer does not satisfy the adapter's bridge");
    }

    const Status beforeTransforms = execution.checkCancellation();
    if (beforeTransforms.failed())
    {
        return beforeTransforms;
    }
    Result<fftw_detail::FftwRealTransformPlan> forward = fftw_detail::FftwRealTransformPlan::create(
        fftw_detail::FftwTransformDirection::Forward, *described, kOperation);
    if (!forward.hasValue())
    {
        return forward.status();
    }
    const Status referenceTransformed = forward->execute(workspace.referenceBase, kOperation);
    if (referenceTransformed.failed())
    {
        return referenceTransformed;
    }
    const Status targetTransformed = forward->execute(workspace.targetBase, kOperation);
    if (targetTransformed.failed())
    {
        return targetTransformed;
    }

    const Status whitened = whitenCrossSpectrum(execution, workspace);
    if (whitened.failed())
    {
        return whitened;
    }

    const Status beforeInverse = execution.checkCancellation();
    if (beforeInverse.failed())
    {
        return beforeInverse;
    }
    Result<fftw_detail::FftwRealTransformPlan> inverse = fftw_detail::FftwRealTransformPlan::create(
        fftw_detail::FftwTransformDirection::Inverse, *described, kOperation);
    if (!inverse.hasValue())
    {
        return inverse.status();
    }
    const Status inverted = inverse->execute(workspace.referenceBase, kOperation);
    if (inverted.failed())
    {
        return inverted;
    }

    // The integer peak, searched in the centred surface's own row-major order so the first of two
    // equal samples wins deterministically.
    Result<plane_detail::ScopedContextMemory> peaksTaken = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(height) * (sizeof(double) + sizeof(std::uint32_t)), alignof(double),
        kOperation);
    if (!peaksTaken.hasValue())
    {
        return peaksTaken.status();
    }
    const plane_detail::ScopedContextMemory peaks = std::move(peaksTaken).value();
    auto* const rowPeak = static_cast<double*>(peaks.data());
    auto* const rowArg = reinterpret_cast<std::uint32_t*>(rowPeak + height);
    const Status searched = parallelFor(execution, height, rowCost(width), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto shiftedY = static_cast<std::uint32_t>(index);
            double best = -std::numeric_limits<double>::infinity();
            std::uint32_t bestX = 0u;
            for (std::uint32_t shiftedX = 0u; shiftedX < width; ++shiftedX)
            {
                const double value = static_cast<double>(centredSurfaceAt(workspace, shiftedX, shiftedY));
                if (value > best)
                {
                    best = value;
                    bestX = shiftedX;
                }
            }
            rowPeak[index] = best;
            rowArg[index] = bestX;
        }
        return Status::success();
    });
    if (searched.failed())
    {
        return searched;
    }
    double peak = -std::numeric_limits<double>::infinity();
    std::uint32_t peakY = 0u;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        if (rowPeak[y] > peak)
        {
            peak = rowPeak[y];
            peakY = y;
        }
    }
    const std::uint32_t peakX = rowArg[peakY];

    // The sub-pixel refinement: the weighted centroid of the five-by-five neighbourhood, clamped at
    // the surface edge, sums in Float64.
    const std::uint32_t firstY = peakY >= 2u ? peakY - 2u : 0u;
    const std::uint32_t lastY = (std::min)(peakY + 2u, height - 1u);
    const std::uint32_t firstX = peakX >= 2u ? peakX - 2u : 0u;
    const std::uint32_t lastX = (std::min)(peakX + 2u, width - 1u);
    double sumValue = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    for (std::uint32_t y = firstY; y <= lastY; ++y)
    {
        for (std::uint32_t x = firstX; x <= lastX; ++x)
        {
            const double value = static_cast<double>(centredSurfaceAt(workspace, x, y));
            sumValue += value;
            sumX += static_cast<double>(x) * value;
            sumY += static_cast<double>(y) * value;
        }
    }
    const double pixelCount = static_cast<double>(width) * static_cast<double>(height);
    const double guarded = sumValue + std::numeric_limits<double>::epsilon();
    const double centroidX = sumX / guarded;
    const double centroidY = sumY / guarded;

    answer.translationX = static_cast<double>(width / 2u) - centroidX;
    answer.translationY = static_cast<double>(height / 2u) - centroidY;
    answer.response = sumValue / pixelCount;
    answer.peakValue = peak / pixelCount;
    answer.peakX = peakX;
    answer.peakY = peakY;
    return answer;
}

} // namespace an
