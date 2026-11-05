// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the out-of-line half of the reference kernels' shared checks and traversal.
//
// The argument checks, the validity accessor, coordinate rounding and the scatter conflict search
// live here; the interpolated read stays inline in the header because its callers run it once per
// output sample. The two loops here - over a coordinate list and over a scatter's earlier samples -
// check for a stop once every 256 items, so a long list can be abandoned part way.

#include "kernels/reference/ReferenceKernelInternal.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

namespace an::reference_detail
{

Status kernelFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

Status checkExecution(const ExecutionContext& execution, const char* operation)
{
    if (!execution.valid())
    {
        return kernelFailure(ErrorCode::NotInitialized, operation, "the kernel was given an empty execution context");
    }
    return execution.checkCancellation();
}

Status checkSingleChannel(PlaneView<const float> plane, const char* operation)
{
    if (plane.samplesPerPixel() != 1u)
    {
        return kernelFailure(ErrorCode::IncompatibleArguments, operation,
                             "the scalar reference kernel accepts one sample per pixel");
    }
    return Status::success();
}

Status checkSingleChannel(PlaneView<float> plane, const char* operation)
{
    if (plane.samplesPerPixel() != 1u)
    {
        return kernelFailure(ErrorCode::IncompatibleArguments, operation,
                             "the scalar reference kernel accepts one sample per pixel");
    }
    return Status::success();
}

Status checkPair(PlaneView<const float> source, PlaneView<float> destination, const char* operation)
{
    Status status = checkSingleChannel(source, operation);
    if (status.failed())
    {
        return status;
    }
    status = checkSingleChannel(destination, operation);
    if (status.failed())
    {
        return status;
    }
    if (source.width() != destination.width() || source.height() != destination.height())
    {
        return kernelFailure(ErrorCode::IncompatibleArguments, operation,
                             "the source and destination plane dimensions do not agree");
    }
    return Status::success();
}

bool exactlyInPlace(PlaneView<const float> source, PlaneView<float> destination) noexcept
{
    return source.data() == destination.data() && source.layout() == destination.layout();
}

Status checkOverlap(PlaneView<const float> source, PlaneView<float> destination, bool exactInPlaceAllowed,
                    const char* operation)
{
    if (!viewsOverlap(source, destination))
    {
        return Status::success();
    }
    if (exactInPlaceAllowed && exactlyInPlace(source, destination))
    {
        return Status::success();
    }
    return kernelFailure(ErrorCode::InvalidArgument, operation,
                         "the source and destination overlap in a way this kernel does not support");
}

Status checkValidity(const KernelValidityView& validity, std::uint32_t width, std::uint32_t height,
                     const char* operation)
{
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return kernelFailure(ErrorCode::InvalidArgument, operation,
                                 "a packed mask was supplied for a validity rule that does not use one");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return kernelFailure(ErrorCode::InvalidArgument, operation,
                             "the explicit-mask validity rule was given no packed mask");
    }
    if (validity.mask->width() != width || validity.mask->height() != height ||
        validity.maskPlane >= validity.mask->planeCount())
    {
        return kernelFailure(ErrorCode::IncompatibleArguments, operation,
                             "the packed mask does not cover the input plane and selected bit-plane");
    }
    return Status::success();
}

ValidityAccessor::ValidityAccessor(const KernelValidityView& validity) noexcept : m_validity(validity) {}

bool ValidityAccessor::accepts(float value, std::uint32_t x, std::uint32_t y) const noexcept
{
    if (!sampleIsValidByValue(m_validity.descriptor, value))
    {
        return false;
    }
    if (m_validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *m_validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(m_validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << (x % 8u));
    return (mask.data()[offset] & bit) != 0u;
}

Status checkInput(const ExecutionContext& execution, PlaneView<const float> source, const KernelValidityView& validity,
                  const char* operation)
{
    Status status = checkExecution(execution, operation);
    if (status.failed())
    {
        return status;
    }
    status = checkSingleChannel(source, operation);
    if (status.failed())
    {
        return status;
    }
    return checkValidity(validity, source.width(), source.height(), operation);
}

Status checkpoint(const ExecutionContext& execution) noexcept
{
    return execution.checkCancellation();
}

bool coordinateIsFinite(const KernelCoordinate& coordinate) noexcept
{
    // Staying inside the exact-integer range of `double` makes floor, the `+ 1` linear tap, and
    // every closed-form border transform representable in signed 64-bit arithmetic.
    constexpr double kCoordinateLimit = 9007199254740990.0;
    return std::isfinite(coordinate.x) && std::isfinite(coordinate.y) && coordinate.x >= -kCoordinateLimit &&
           coordinate.x <= kCoordinateLimit && coordinate.y >= -kCoordinateLimit && coordinate.y <= kCoordinateLimit;
}

ResolvedCoordinate resolveCoordinate(const KernelCoordinate& coordinate, std::uint32_t width, std::uint32_t height,
                                     bool clamp) noexcept
{
    double x = coordinate.x;
    double y = coordinate.y;
    if (!clamp)
    {
        if (x < -0.5 || y < -0.5 || x >= static_cast<double>(width) - 0.5 || y >= static_cast<double>(height) - 0.5)
        {
            return ResolvedCoordinate{.outside = true};
        }
    }
    else
    {
        if (x < 0.0)
        {
            x = 0.0;
        }
        else if (x > static_cast<double>(width - 1u))
        {
            x = static_cast<double>(width - 1u);
        }
        if (y < 0.0)
        {
            y = 0.0;
        }
        else if (y > static_cast<double>(height - 1u))
        {
            y = static_cast<double>(height - 1u);
        }
    }
    return ResolvedCoordinate{.x = static_cast<std::uint32_t>(std::floor(x + 0.5)),
                              .y = static_cast<std::uint32_t>(std::floor(y + 0.5)),
                              .outside = false};
}

Status checkCoordinates(const ExecutionContext& execution, std::span<const KernelCoordinate> coordinates,
                        const char* operation)
{
    for (std::size_t index = 0u; index < coordinates.size(); ++index)
    {
        if ((index & 0xFFu) == 0u)
        {
            const Status status = checkpoint(execution);
            if (status.failed())
            {
                return status;
            }
        }
        if (!coordinateIsFinite(coordinates[index]))
        {
            return kernelFailure(ErrorCode::InvalidArgument, operation,
                                 "a kernel coordinate is not finite or exactly representable");
        }
    }
    return Status::success();
}

bool isSamplingBorder(KernelBorderMode border) noexcept
{
    return border >= KernelBorderMode::Clamp && border <= KernelBorderMode::Wrap;
}

Status checkSamplingParameters(const SamplingKernelParameters& parameters, const char* operation)
{
    if (!isKernelInterpolation(parameters.interpolation) || !isSamplingInvalidPolicy(parameters.invalid) ||
        !isSamplingBorder(parameters.border) || !std::isfinite(parameters.constantBorder) ||
        !std::isfinite(parameters.invalidSample))
    {
        return kernelFailure(ErrorCode::InvalidArgument, operation,
                             "coordinate-sampling policies must be known and their constant values finite");
    }
    return Status::success();
}

SamplingPlan planSampling(PlaneView<const float> source, const KernelValidityView& validity,
                          const SamplingKernelParameters& parameters) noexcept
{
    return SamplingPlan{.samples = source.data(),
                        .rowStride = source.rowStrideBytes() / sizeof(float),
                        .width = static_cast<std::int64_t>(source.width()),
                        .height = static_cast<std::int64_t>(source.height()),
                        .interpolation = parameters.interpolation,
                        .border = parameters.border,
                        .invalid = parameters.invalid,
                        .constantBorder = parameters.constantBorder,
                        .invalidSample = parameters.invalidSample,
                        .validity = &validity,
                        // Only the rule that admits every value, and only without a mask beside it. Any other rule
                        // can reject a sample, so it keeps asking; being conservative here costs a branch on a path
                        // that was already asking, and being wrong here would silently accept invalid samples.
                        .acceptsEverySample =
                            validity.mask == nullptr && validity.descriptor.rule() == ValidityRule::AllValid};
}

float sampleAt(PlaneView<const float> source, const ResolvedCoordinate& coordinate) noexcept
{
    return (*source.row(coordinate.y))[coordinate.x];
}

bool totalFloatLess(float left, float right) noexcept
{
    const bool leftNan = std::isnan(left);
    const bool rightNan = std::isnan(right);
    if (leftNan != rightNan)
    {
        return !leftNan;
    }
    if (!leftNan)
    {
        if (left < right)
        {
            return true;
        }
        if (right < left)
        {
            return false;
        }
    }
    return std::bit_cast<std::uint32_t>(left) < std::bit_cast<std::uint32_t>(right);
}

float sourceAt(PlaneView<const float> source, std::uint64_t index, std::uint32_t& x, std::uint32_t& y) noexcept
{
    y = static_cast<std::uint32_t>(index / source.width());
    x = static_cast<std::uint32_t>(index % source.width());
    return (*source.row(y))[x];
}

Status findPriorScatterTarget(const ExecutionContext& execution, PlaneView<const float> source,
                              std::span<const KernelCoordinate> coordinates, const ValidityAccessor& validity,
                              std::uint64_t before, std::uint32_t destinationWidth, std::uint32_t destinationHeight,
                              const ResolvedCoordinate& target, bool& found)
{
    found = false;
    for (std::uint64_t earlier = 0u; earlier < before; ++earlier)
    {
        if ((earlier & 0xFFu) == 0u)
        {
            const Status status = checkpoint(execution);
            if (status.failed())
            {
                return status;
            }
        }
        std::uint32_t sourceX = 0u;
        std::uint32_t sourceY = 0u;
        const float value = sourceAt(source, earlier, sourceX, sourceY);
        if (!validity.accepts(value, sourceX, sourceY))
        {
            continue;
        }
        const ResolvedCoordinate candidate = resolveCoordinate(coordinates[static_cast<std::size_t>(earlier)],
                                                               destinationWidth, destinationHeight, false);
        if (!candidate.outside && candidate.x == target.x && candidate.y == target.y)
        {
            found = true;
            return Status::success();
        }
    }
    return Status::success();
}

} // namespace an::reference_detail
