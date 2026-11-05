// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the convolution both deconvolution methods run on, and the edge taper
// built out of it.
//
// Three things here are worth reading rather than skimming.
//
// **The kernel is normalised into this engine's memory, never into the caller's.** A point spread
// function is frequently reused across a hundred frames, and an operation that silently rescaled the
// caller's plane would make the second frame's answer depend on the first's having been run. The
// caller's plane is read only while the engine is built.
//
// **The transform path pre-transforms the kernel once.** A Richardson-Lucy run does two
// convolutions per iteration, all of the same geometry, so planning the transforms and allocating
// their buffers per convolution would repeat the same work a hundred times over twenty iterations.
// Whichever of the kernel's spectrum, its mirror's spectrum and its circularly wrapped spectrum the
// operation asked for is computed at construction, and the two plans are created once.
//
// **Both paths answer the border the same way.** The border rule is resolved once into a row and a
// column index table; the direct path reads through them at the frame's edge, and the transform path
// uses them to extend the frame by one kernel radius before zero-padding it to a fast transform
// length large enough that the circular convolution equals the linear one over the whole frame.

#include "algorithms/deconvolution/DeconvolutionInternal.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Interop/FftwAdapter.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <utility>

namespace an::deconvolution_detail
{
namespace
{

/// Rounds a byte count up to the alignment every buffer starts at.
///
/// @param bytes what a buffer needs.
/// @return what it occupies.
[[nodiscard]] constexpr std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) / kDefaultBufferAlignmentBytes * kDefaultBufferAlignmentBytes;
}

/// How much work one row of a plane pass is.
///
/// @param width how many samples one row holds.
/// @param perSample how many operations each sample costs.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(std::uint64_t width, std::uint64_t perSample) noexcept
{
    return TaskCost{.bytesPerItem = width * sizeof(float), .operationsPerItem = width * perSample};
}

/// The weight the taper gives a sample at a stated distance from the nearest edge.
///
/// One in the interior, and a squared sine falling to zero at the edge itself.
///
/// @param distance how far the sample is from the nearest edge, in pixels.
/// @param radius how far the taper reaches.
/// @return the weight, within `[0, 1]`.
[[nodiscard]] double taperWeight(double distance, double radius) noexcept
{
    if (radius <= 0.0 || distance >= radius)
    {
        return 1.0;
    }
    const double denominator = (std::max)(1.0, 2.0 * radius - 1.0);
    const double sine = std::sin(distance * std::numbers::pi / denominator);
    return sine * sine;
}

/// Adds two Float64 partials.
///
/// @param left one partial.
/// @param right the other.
/// @return their sum.
[[nodiscard]] double sumMerge(double left, double right) noexcept
{
    return left + right;
}

} // namespace

Status deconvolutionFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

Status deconvolutionLimitFailure(ErrorCode code, const char* operation, const char* message, std::uint64_t requested,
                                 std::uint64_t limit)
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = operation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                          StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

std::uint32_t resolveCoordinate(std::int64_t position, std::uint32_t extent, KernelBorderMode border) noexcept
{
    if (extent <= 1u)
    {
        return 0u;
    }
    const auto last = static_cast<std::int64_t>(extent) - 1;
    switch (border)
    {
    case KernelBorderMode::Clamp:
        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(position, 0, last));
    case KernelBorderMode::Wrap: {
        const auto span = static_cast<std::int64_t>(extent);
        return static_cast<std::uint32_t>(((position % span) + span) % span);
    }
    case KernelBorderMode::ReflectExcludingEdge:
        while (position < 0 || position > last)
        {
            position = position < 0 ? -position : 2 * last - position;
        }
        return static_cast<std::uint32_t>(position);
    case KernelBorderMode::Reflect:
    case KernelBorderMode::NotApplicable:
    case KernelBorderMode::Constant:
    default:
        while (position < 0 || position > last)
        {
            position = position < 0 ? -position - 1 : 2 * last - position + 1;
        }
        return static_cast<std::uint32_t>(position);
    }
}

bool isFastTransformLength(std::uint64_t value) noexcept
{
    if (value == 0u)
    {
        return false;
    }
    for (const std::uint64_t factor : {std::uint64_t{2}, std::uint64_t{3}, std::uint64_t{5}})
    {
        while (value % factor == 0u)
        {
            value /= factor;
        }
    }
    return value == 1u;
}

std::uint64_t nextFastTransformLength(std::uint64_t value) noexcept
{
    while (!isFastTransformLength(value))
    {
        ++value;
    }
    return value;
}

Result<ConvolutionEngine> ConvolutionEngine::create(const ExecutionContext& execution, PlaneView<const float> psf,
                                                    bool useTransform, std::uint32_t width, std::uint32_t height,
                                                    KernelBorderMode border, const ConvolutionEngineNeeds& needs,
                                                    const char* operation)
{
    const std::uint32_t kernelWidth = psf.width();
    const std::uint32_t kernelHeight = psf.height();
    const std::uint64_t kernelSamples = static_cast<std::uint64_t>(kernelWidth) * kernelHeight;
    const std::uint64_t rowIndexCount = static_cast<std::uint64_t>(height) + kernelHeight - 1u;
    const std::uint64_t columnIndexCount = static_cast<std::uint64_t>(width) + kernelWidth - 1u;
    const std::uint64_t indexBytes =
        alignedBytes(rowIndexCount * sizeof(std::uint32_t)) + alignedBytes(columnIndexCount * sizeof(std::uint32_t));

    std::uint64_t transformWidth = 0u;
    std::uint64_t transformHeight = 0u;
    std::uint64_t rowReals = 0u;
    std::uint64_t planeFloats = 0u;
    if (useTransform)
    {
        // The padded extent has to hold the image extended by one kernel radius on each side under
        // the border rule, plus the kernel's own support, so that the circular convolution the
        // transform computes agrees with the linear one over the whole frame rather than only in its
        // interior.
        const std::uint64_t radiusX = kernelWidth / 2u;
        const std::uint64_t radiusY = kernelHeight / 2u;
        transformWidth = nextFastTransformLength(width + 2u * radiusX + kernelWidth - 1u);
        transformHeight = nextFastTransformLength(height + 2u * radiusY + kernelHeight - 1u);
        // The transform library plans in int dimensions and this engine keeps them as 32-bit extents,
        // so a padded extent past either is refused here rather than narrowed into a plan that
        // disagrees with the buffers sized from the full value.
        constexpr std::uint64_t kMaxTransformExtent = 0x7FFFFFFFu;
        if (transformWidth > kMaxTransformExtent || transformHeight > kMaxTransformExtent)
        {
            return deconvolutionLimitFailure(ErrorCode::LimitExceeded, operation,
                                             "the padded transform is wider or taller than a transform can be planned",
                                             (std::max)(transformWidth, transformHeight), kMaxTransformExtent);
        }
        rowReals = 2u * fftwHermitianColumns(static_cast<std::uint32_t>(transformWidth));
        planeFloats = transformHeight * rowReals;
    }

    const std::uint64_t spectra =
        (needs.convolution ? 1u : 0u) + (needs.adjoint ? 1u : 0u) + (needs.inverseFilter ? 1u : 0u);
    const std::uint64_t spatialFloats = transformHeight * transformWidth;
    const bool needsDirect = !useTransform || needs.directConvolution;
    const std::uint64_t directBytes = needsDirect ? alignedBytes(kernelSamples * sizeof(float)) * 2u : 0u;
    const std::uint64_t bytes = indexBytes + directBytes +
                                (useTransform ? alignedBytes(planeFloats * sizeof(float)) * (spectra + 1u) +
                                                    alignedBytes(spatialFloats * sizeof(float))
                                              : 0u);
    Result<plane_detail::ScopedContextMemory> taken =
        plane_detail::ScopedContextMemory::take(execution, bytes, kDefaultBufferAlignmentBytes, operation);
    if (!taken.hasValue())
    {
        return taken.status();
    }

    ConvolutionEngine engine{std::move(taken).value()};
    engine.m_width = width;
    engine.m_height = height;
    engine.m_kernelWidth = kernelWidth;
    engine.m_kernelHeight = kernelHeight;
    engine.m_border = border;
    engine.m_usesTransform = useTransform;

    auto* cursor = static_cast<std::uint8_t*>(engine.m_memory.data());
    const auto take = [&cursor](std::uint64_t count) noexcept {
        auto* const block = cursor;
        cursor += alignedBytes(count * sizeof(float));
        return std::span<float>{reinterpret_cast<float*>(block), static_cast<std::size_t>(count)};
    };
    const auto takeIndices = [&cursor](std::uint64_t count) noexcept {
        auto* const block = cursor;
        cursor += alignedBytes(count * sizeof(std::uint32_t));
        return std::span<std::uint32_t>{reinterpret_cast<std::uint32_t*>(block), static_cast<std::size_t>(count)};
    };

    // The border rule is resolved once per row and once per column of the read window, so the
    // innermost loop of a convolution is two table lookups rather than a switch and a loop.
    const std::span<std::uint32_t> rowIndices = takeIndices(rowIndexCount);
    for (std::uint64_t offset = 0u; offset < rowIndexCount; ++offset)
    {
        rowIndices[static_cast<std::size_t>(offset)] = resolveCoordinate(
            static_cast<std::int64_t>(offset) - static_cast<std::int64_t>(kernelHeight / 2u), height, border);
    }
    const std::span<std::uint32_t> columnIndices = takeIndices(columnIndexCount);
    for (std::uint64_t offset = 0u; offset < columnIndexCount; ++offset)
    {
        columnIndices[static_cast<std::size_t>(offset)] = resolveCoordinate(
            static_cast<std::int64_t>(offset) - static_cast<std::int64_t>(kernelWidth / 2u), width, border);
    }
    engine.m_rowIndices = rowIndices;
    engine.m_columnIndices = columnIndices;

    // The kernel is normalised into this engine's memory. Its sum was proved positive and finite by
    // `checkPsf`, which the plan and the entry point both run, so the division is defined; doing it
    // here rather than in the caller's plane is what keeps a kernel reusable across frames.
    double total = 0.0;
    for (std::uint32_t y = 0u; y < kernelHeight; ++y)
    {
        const std::optional<std::span<const float>> row = psf.row(y);
        if (!row.has_value())
        {
            return deconvolutionFailure(ErrorCode::Internal, operation, "a checked kernel has an unreadable row");
        }
        for (std::uint32_t x = 0u; x < kernelWidth; ++x)
        {
            total += static_cast<double>((*row)[x]);
        }
    }
    const auto normalization = static_cast<float>(1.0 / total);

    if (needsDirect)
    {
        engine.m_kernel = take(kernelSamples);
        engine.m_mirrored = take(kernelSamples);
        for (std::uint32_t y = 0u; y < kernelHeight; ++y)
        {
            const std::span<const float> row = *psf.row(y);
            for (std::uint32_t x = 0u; x < kernelWidth; ++x)
            {
                const float value = row[x] * normalization;
                engine.m_kernel[static_cast<std::size_t>(y) * kernelWidth + x] = value;
                engine.m_mirrored[static_cast<std::size_t>(kernelHeight - 1u - y) * kernelWidth +
                                  (kernelWidth - 1u - x)] = value;
            }
        }
    }
    if (!useTransform)
    {
        return engine;
    }

    engine.m_transformWidth = static_cast<std::uint32_t>(transformWidth);
    engine.m_transformHeight = static_cast<std::uint32_t>(transformHeight);
    engine.m_rowReals = rowReals;
    engine.m_spatial = take(spatialFloats);
    if (needs.convolution)
    {
        engine.m_kernelSpectrum = take(planeFloats);
    }
    if (needs.adjoint)
    {
        engine.m_mirroredSpectrum = take(planeFloats);
    }
    if (needs.inverseFilter)
    {
        engine.m_wrappedSpectrum = take(planeFloats);
    }
    engine.m_work = take(planeFloats);

    const std::optional<PlaneView<float>> spatialPlane =
        PlaneView<float>::create(std::span<float>{engine.m_spatial.data(), static_cast<std::size_t>(spatialFloats)},
                                 engine.m_transformWidth, engine.m_transformHeight, 1u, transformWidth * sizeof(float));
    const std::optional<PlaneView<float>> spectrumPlane = PlaneView<float>::create(
        std::span<float>{engine.m_work.data(), static_cast<std::size_t>(planeFloats)},
        static_cast<std::uint32_t>(rowReals), engine.m_transformHeight, 1u, rowReals * sizeof(float));
    if (!spatialPlane.has_value() || !spectrumPlane.has_value())
    {
        return deconvolutionFailure(ErrorCode::Internal, operation, "the transform buffer does not describe a plane");
    }
    const ExternalBufferContract contract{.engineDomain = NumericDomain::PhysicalScaled,
                                          .libraryDomain = NumericDomain::PhysicalScaled,
                                          .ownership = BufferOwnership::EngineOwned,
                                          .scope = BorrowScope::Operation,
                                          .mutability = BufferMutability::ReadWrite};
    const std::optional<FftwArrayDescription> describedSpatial =
        describeAsFftwReal(*spatialPlane, contract, FftwPlacement::OutOfPlace, kFftwDefaultAlignmentBytes);
    const std::optional<FftwArrayDescription> describedSpectrum =
        describeAsFftwComplex(*spectrumPlane, contract, kFftwDefaultAlignmentBytes);
    if (!describedSpatial.has_value() || !describedSpectrum.has_value())
    {
        return deconvolutionFailure(ErrorCode::Internal, operation,
                                    "the transform buffer does not satisfy the adapter bridge");
    }
    Result<fftw_detail::FftwRealTransformPlan> forward = fftw_detail::FftwRealTransformPlan::createSplit(
        fftw_detail::FftwTransformDirection::Forward, *describedSpatial, *describedSpectrum, operation);
    if (!forward.hasValue())
    {
        return forward.status();
    }
    Result<fftw_detail::FftwRealTransformPlan> inverse = fftw_detail::FftwRealTransformPlan::createSplit(
        fftw_detail::FftwTransformDirection::Inverse, *describedSpatial, *describedSpectrum, operation);
    if (!inverse.hasValue())
    {
        return inverse.status();
    }
    engine.m_forward = std::move(forward).value();
    engine.m_inverse = std::move(inverse).value();

    // Three spectra: the kernel placed at the origin corner, its mirror, and the circular form a
    // Wiener filter divides by, which puts the kernel's centre at the transform's origin.
    const auto placeAndTransform = [&](std::span<float> target, bool mirrored, bool wrapped) -> Status {
        std::fill(engine.m_spatial.begin(), engine.m_spatial.end(), 0.0f);
        for (std::uint32_t y = 0u; y < kernelHeight; ++y)
        {
            const std::span<const float> row = *psf.row(y);
            for (std::uint32_t x = 0u; x < kernelWidth; ++x)
            {
                const float value = row[x] * normalization;
                std::uint64_t targetY = y;
                std::uint64_t targetX = x;
                if (mirrored)
                {
                    targetY = kernelHeight - 1u - y;
                    targetX = kernelWidth - 1u - x;
                }
                if (wrapped)
                {
                    const auto offsetY = static_cast<std::int64_t>(y) - static_cast<std::int64_t>(kernelHeight / 2u);
                    const auto offsetX = static_cast<std::int64_t>(x) - static_cast<std::int64_t>(kernelWidth / 2u);
                    targetY = offsetY >= 0 ? static_cast<std::uint64_t>(offsetY)
                                           : transformHeight - static_cast<std::uint64_t>(-offsetY);
                    targetX = offsetX >= 0 ? static_cast<std::uint64_t>(offsetX)
                                           : transformWidth - static_cast<std::uint64_t>(-offsetX);
                }
                engine.m_spatial[static_cast<std::size_t>(targetY * transformWidth + targetX)] = value;
            }
        }
        return engine.m_forward.executeSplit(engine.m_spatial.data(), target.data(), operation);
    };
    if (needs.convolution)
    {
        const Status transformed = placeAndTransform(engine.m_kernelSpectrum, false, false);
        if (transformed.failed())
        {
            return transformed;
        }
    }
    if (needs.adjoint)
    {
        const Status transformed = placeAndTransform(engine.m_mirroredSpectrum, true, false);
        if (transformed.failed())
        {
            return transformed;
        }
    }
    if (needs.inverseFilter)
    {
        const Status transformed = placeAndTransform(engine.m_wrappedSpectrum, false, true);
        if (transformed.failed())
        {
            return transformed;
        }
    }
    return engine;
}

Status ConvolutionEngine::fillTransformBuffer(const ExecutionContext& execution, const WorkingPlane& source,
                                              std::span<float> buffer) const
{
    const std::uint64_t radiusX = m_kernelWidth / 2u;
    const std::uint64_t radiusY = m_kernelHeight / 2u;
    const std::uint64_t extendedWidth = static_cast<std::uint64_t>(m_width) + 2u * radiusX;
    const std::uint64_t extendedHeight = static_cast<std::uint64_t>(m_height) + 2u * radiusY;
    // Only the padding is zeroed: the reflected image covers the rest of the buffer on every call,
    // so clearing all of it would write several megabytes nothing reads.
    const std::uint64_t stride = m_transformWidth;
    for (std::uint64_t y = 0u; y < extendedHeight; ++y)
    {
        std::fill_n(buffer.data() + y * stride + extendedWidth, stride - extendedWidth, 0.0f);
    }
    std::fill_n(buffer.data() + extendedHeight * stride, (m_transformHeight - extendedHeight) * stride, 0.0f);
    return parallelFor(execution, extendedHeight, rowCost(extendedWidth, 2u), [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::uint32_t sourceY = m_rowIndices[static_cast<std::size_t>(y)];
            const float* const sourceRow = source.base + static_cast<std::uint64_t>(sourceY) * m_width;
            float* const target = buffer.data() + y * stride;
            for (std::uint64_t x = 0u; x < extendedWidth; ++x)
            {
                target[x] = sourceRow[m_columnIndices[static_cast<std::size_t>(x)]];
            }
        }
        return Status::success();
    });
}

float ConvolutionEngine::borderedSample(const WorkingPlane& source, std::span<const float> walked, std::uint32_t x,
                                        std::uint64_t y) const noexcept
{
    float sum = 0.0f;
    for (std::uint32_t ky = 0u; ky < m_kernelHeight; ++ky)
    {
        const std::uint32_t sourceY = m_rowIndices[static_cast<std::size_t>(y) + ky];
        const float* const sourceRow = source.base + static_cast<std::uint64_t>(sourceY) * m_width;
        const float* const kernelRow = walked.data() + static_cast<std::size_t>(ky) * m_kernelWidth;
        for (std::uint32_t kx = 0u; kx < m_kernelWidth; ++kx)
        {
            sum += kernelRow[kx] * sourceRow[m_columnIndices[static_cast<std::size_t>(x) + kx]];
        }
    }
    return sum;
}

Status ConvolutionEngine::convolve(const ExecutionContext& execution, const WorkingPlane& source,
                                   const WorkingPlane& destination, bool mirrored, bool useTransform,
                                   const char* operation) const
{
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }

    if (!useTransform)
    {
        if (m_kernel.empty() || m_mirrored.empty())
        {
            return deconvolutionFailure(ErrorCode::InvalidState, operation,
                                        "this operation did not build a direct convolution kernel");
        }
        // A convolution reads its kernel backwards. Reading the *mirrored* kernel forwards computes
        // exactly the same sum, so the inner loop is two contiguous runs walking the same way -
        // which is what a vector unit can do and a reversed walk is not. It also means the mirrored
        // convolution, which the iterative method needs once per iteration, is the plain kernel read
        // forwards and costs nothing extra.
        const std::span<const float> walked = mirrored ? m_kernel : m_mirrored;
        const std::uint32_t radiusX = m_kernelWidth / 2u;
        const std::uint32_t radiusY = m_kernelHeight / 2u;
        const std::uint32_t kernelWidth = m_kernelWidth;
        const std::uint32_t kernelHeight = m_kernelHeight;
        const std::uint32_t width = m_width;
        const std::uint32_t height = m_height;
        // A pixel at least one radius from every edge reads a rectangle entirely inside the frame,
        // so its window needs no border resolution at all. That is almost every pixel of a real
        // frame, and separating the two cases is what keeps the branchy path off the hot one.
        const bool hasInterior = width > 2u * radiusX && height > 2u * radiusY;
        return parallelFor(
            execution, m_height, rowCost(m_width, static_cast<std::uint64_t>(kernelWidth) * kernelHeight * 2u),
            [&](TaskRange range) -> Status {
                for (std::uint64_t y = range.begin; y < range.end; ++y)
                {
                    float* const target = destination.base + y * width;
                    const bool interiorRow =
                        hasInterior && y >= radiusY && y + radiusY < static_cast<std::uint64_t>(height);
                    const std::uint32_t firstInterior = interiorRow ? radiusX : width;
                    const std::uint32_t lastInterior = interiorRow ? width - radiusX : width;
                    for (std::uint32_t x = 0u; x < firstInterior; ++x)
                    {
                        target[x] = borderedSample(source, walked, x, y);
                    }
                    for (std::uint32_t x = firstInterior; x < lastInterior; ++x)
                    {
                        const float* const window = source.base + (y - radiusY) * width + (x - radiusX);
                        float sum = 0.0f;
                        for (std::uint32_t ky = 0u; ky < kernelHeight; ++ky)
                        {
                            const float* const sourceRow = window + static_cast<std::uint64_t>(ky) * width;
                            const float* const kernelRow = walked.data() + static_cast<std::size_t>(ky) * kernelWidth;
                            for (std::uint32_t kx = 0u; kx < kernelWidth; ++kx)
                            {
                                sum += kernelRow[kx] * sourceRow[kx];
                            }
                        }
                        target[x] = sum;
                    }
                    for (std::uint32_t x = lastInterior; x < width; ++x)
                    {
                        target[x] = borderedSample(source, walked, x, y);
                    }
                }
                return Status::success();
            });
    }

    if (!m_usesTransform)
    {
        return deconvolutionFailure(ErrorCode::InvalidState, operation,
                                    "this operation did not build a transform convolution");
    }

    const Status filled = fillTransformBuffer(execution, source, m_spatial);
    if (filled.failed())
    {
        return filled;
    }
    const Status transformed = m_forward.executeSplit(m_spatial.data(), m_work.data(), operation);
    if (transformed.failed())
    {
        return transformed;
    }

    const std::span<const float> spectrum = mirrored ? m_mirroredSpectrum : m_kernelSpectrum;
    if (spectrum.empty())
    {
        return deconvolutionFailure(ErrorCode::InvalidState, operation,
                                    "this operation did not build the requested convolution spectrum");
    }
    const std::uint32_t hermitian = fftwHermitianColumns(m_transformWidth);
    const Status multiplied =
        parallelFor(execution, m_transformHeight, rowCost(hermitian, 6u), [&](TaskRange range) -> Status {
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                float* const work = m_work.data() + row * m_rowReals;
                const float* const kernel = spectrum.data() + row * m_rowReals;
                for (std::uint32_t column = 0u; column < hermitian; ++column)
                {
                    const float ar = work[2u * column];
                    const float ai = work[2u * column + 1u];
                    const float br = kernel[2u * column];
                    const float bi = kernel[2u * column + 1u];
                    work[2u * column] = ar * br - ai * bi;
                    work[2u * column + 1u] = ar * bi + ai * br;
                }
            }
            return Status::success();
        });
    if (multiplied.failed())
    {
        return multiplied;
    }

    const Status restored = m_inverse.executeSplit(m_spatial.data(), m_work.data(), operation);
    if (restored.failed())
    {
        return restored;
    }

    // The linear convolution of the padded source with the kernel sits offset by twice the kernel's
    // radius, because the source was itself placed at one radius of padding.
    const std::uint64_t offsetX = 2u * (m_kernelWidth / 2u);
    const std::uint64_t offsetY = 2u * (m_kernelHeight / 2u);
    const auto scale =
        static_cast<float>(1.0 / (static_cast<double>(m_transformWidth) * static_cast<double>(m_transformHeight)));
    return parallelFor(execution, m_height, rowCost(m_width, 2u), [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const float* const work = m_spatial.data() + (y + offsetY) * m_transformWidth + offsetX;
            float* const target = destination.base + y * m_width;
            for (std::uint32_t x = 0u; x < m_width; ++x)
            {
                target[x] = work[x] * scale;
            }
        }
        return Status::success();
    });
}

Status ConvolutionEngine::wiener(const ExecutionContext& execution, const WorkingPlane& source,
                                 const WorkingPlane& destination, double regularization, bool nonNegative,
                                 const char* operation) const
{
    if (!m_usesTransform || m_wrappedSpectrum.empty())
    {
        return deconvolutionFailure(ErrorCode::InvalidState, operation,
                                    "a Wiener filter requires a prepared inverse-filter spectrum");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Status filled = fillTransformBuffer(execution, source, m_spatial);
    if (filled.failed())
    {
        return filled;
    }
    const Status transformed = m_forward.executeSplit(m_spatial.data(), m_work.data(), operation);
    if (transformed.failed())
    {
        return transformed;
    }

    const std::uint32_t hermitian = fftwHermitianColumns(m_transformWidth);
    const auto guard = static_cast<float>(regularization);
    const Status divided =
        parallelFor(execution, m_transformHeight, rowCost(hermitian, 10u), [&](TaskRange range) -> Status {
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                float* const work = m_work.data() + row * m_rowReals;
                const float* const kernel = m_wrappedSpectrum.data() + row * m_rowReals;
                for (std::uint32_t column = 0u; column < hermitian; ++column)
                {
                    const float gr = work[2u * column];
                    const float gi = work[2u * column + 1u];
                    const float hr = kernel[2u * column];
                    const float hi = kernel[2u * column + 1u];
                    const float denominator = hr * hr + hi * hi + guard;
                    work[2u * column] = (gr * hr + gi * hi) / denominator;
                    work[2u * column + 1u] = (gi * hr - gr * hi) / denominator;
                }
            }
            return Status::success();
        });
    if (divided.failed())
    {
        return divided;
    }

    const Status restored = m_inverse.executeSplit(m_spatial.data(), m_work.data(), operation);
    if (restored.failed())
    {
        return restored;
    }

    // The circular kernel put the response at the origin, so the restored frame sits at exactly the
    // padding the source was placed at rather than at twice it.
    const std::uint64_t offsetX = m_kernelWidth / 2u;
    const std::uint64_t offsetY = m_kernelHeight / 2u;
    const auto scale =
        static_cast<float>(1.0 / (static_cast<double>(m_transformWidth) * static_cast<double>(m_transformHeight)));
    return parallelFor(execution, m_height, rowCost(m_width, 4u), [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const float* const work = m_spatial.data() + (y + offsetY) * m_transformWidth + offsetX;
            float* const target = destination.base + y * m_width;
            for (std::uint32_t x = 0u; x < m_width; ++x)
            {
                float value = work[x] * scale;
                if (!std::isfinite(value))
                {
                    value = 0.0f;
                }
                if (nonNegative)
                {
                    value = (std::max)(0.0f, value);
                }
                target[x] = value;
            }
        }
        return Status::success();
    });
}

Result<double> positiveSum(const ExecutionContext& execution, const WorkingPlane& plane)
{
    const std::uint32_t width = plane.width;
    const float* const base = plane.base;
    const auto body = [&](TaskRange range, double& partial) -> Status {
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const float* const line = base + row * width;
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const float value = line[x];
                if (std::isfinite(value) && value > 0.0f)
                {
                    partial += static_cast<double>(value);
                }
            }
        }
        return Status::success();
    };
    return parallelReduce(execution, plane.height, rowCost(width, 2u), 0.0, body, sumMerge);
}

Status applyEdgeTaper(const ExecutionContext& execution, const WorkingPlane& observed, const WorkingPlane& tapered,
                      const WorkingPlane& scratchA, const WorkingPlane& scratchB, const ConvolutionEngine& engine,
                      bool useTransform, double radius, std::uint32_t iterations, const char* operation)
{
    if (radius <= 0.0 || iterations == 0u)
    {
        // Nothing to do, and nothing copied: the caller reads the observation directly when no taper
        // runs, so a frame-sized copy per channel is not paid for a stage that was turned off.
        return Status::success();
    }
    const std::uint64_t count = observed.sampleCount();
    std::copy_n(observed.base, count, tapered.base);

    WorkingPlane current = tapered;
    WorkingPlane next = scratchB;
    for (std::uint32_t iteration = 0u; iteration < iterations; ++iteration)
    {
        const Status blurred = engine.convolve(execution, current, scratchA, false, useTransform, operation);
        if (blurred.failed())
        {
            return blurred;
        }
        const std::uint32_t width = observed.width;
        const std::uint32_t height = observed.height;
        const Status blended = parallelFor(execution, height, rowCost(width, 8u), [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const double verticalDistance =
                    static_cast<double>((std::min)(y, static_cast<std::uint64_t>(height) - 1u - y));
                const double vertical = taperWeight(verticalDistance, radius);
                const float* const source = current.base + y * width;
                const float* const blur = scratchA.base + y * width;
                float* const target = next.base + y * width;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double horizontal = taperWeight(static_cast<double>((std::min)(x, width - 1u - x)), radius);
                    const auto weight = static_cast<float>(horizontal * vertical);
                    target[x] = source[x] * weight + blur[x] * (1.0f - weight);
                }
            }
            return Status::success();
        });
        if (blended.failed())
        {
            return blended;
        }
        std::swap(current, next);
    }
    if (current.base != tapered.base)
    {
        std::copy_n(current.base, count, tapered.base);
    }

    // The taper removes energy at the edges, so the whole frame is rescaled back to the flux it came
    // in with. Without it a Richardson-Lucy run would spend its first iterations restoring a total
    // the taper took away.
    Result<double> observedFlux = positiveSum(execution, observed);
    if (!observedFlux.hasValue())
    {
        return observedFlux.status();
    }
    Result<double> taperedFlux = positiveSum(execution, tapered);
    if (!taperedFlux.hasValue())
    {
        return taperedFlux.status();
    }
    if (*observedFlux > 0.0 && *taperedFlux > 0.0)
    {
        const auto scale = static_cast<float>(*observedFlux / *taperedFlux);
        for (std::uint64_t i = 0u; i < count; ++i)
        {
            tapered.base[i] *= scale;
        }
    }
    return Status::success();
}

} // namespace an::deconvolution_detail
