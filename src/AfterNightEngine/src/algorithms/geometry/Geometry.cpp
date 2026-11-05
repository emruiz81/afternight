// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - crop, resize, integer resample, rotation, warp and orientation of whole images.
//
// Every operation is a plan and an execution. A `plan*` builder validates a request against a source
// descriptor and records the destination descriptor and a destination-to-source matrix under the
// integer pixel-centre convention. `transformImageGeometry` rebuilds the plan it is handed through
// the builder its operation names, from the plan's own recorded fields, and refuses one that differs,
// so a hand-constructed `GeometryPlan` cannot reach the samples with invariants no builder checked.
// Every validation - the layouts, the masks, the sampling policy and every projective coordinate -
// completes before the first sample is written.
//
// Execution then takes the narrowest path that serves the request. The exact operations
// - crop, integer resample and orientation - copy or reduce stored values in their own
// representation. The interpolating ones have one general path: gather a channel into a Float32
// working plane, sample it with the reference sampler, and scatter it back through the default
// code-value conversion. Beside it are direct paths for the common cases in which every sample is
// valid: a single-sample Float32 plane resized or warped where it lies, a nearest resize through
// index tables, a crop or orientation by row copies. Each direct path says in its own comment how its
// bytes relate to the general path's. Two resize choices follow OpenCV's fixed-point rules:
// `NearestExact` its `INTER_NEAREST_EXACT` source index, and `LinearExact` its `INTER_LINEAR_EXACT`
// coefficients and rounding for UInt8, UInt16 and Int16.
//
// The output mask is cleared first and a bit is set only for a sample the operation wrote; a mask
// with one plane shared by every channel ends as the intersection of what each channel wrote.

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/ReferenceKernelInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kCropOperation = "an::planCrop";
constexpr const char* kResizeOperation = "an::planResize";
constexpr const char* kIntegerOperation = "an::planIntegerResample";
constexpr const char* kCfaIntegerOperation = "an::planCfaIntegerResample";
constexpr const char* kRotateOperation = "an::planRotation";
constexpr const char* kAffineOperation = "an::planAffineWarp";
constexpr const char* kProjectiveOperation = "an::planProjectiveWarp";
constexpr const char* kOrientOperation = "an::planOrientation";
constexpr const char* kTransformOperation = "an::transformImageGeometry";

/// One failure from this domain, naming the entry point the caller is inside.
[[nodiscard]] Status geometryFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a sampling policy names known interpolation, border and invalid-neighbour rules - any
/// border but `KernelBorderMode::NotApplicable` - with finite substitute values.
///
/// @return success, or `ErrorCode::InvalidArgument`.
[[nodiscard]] Status checkSampling(const SamplingKernelParameters& sampling, const char* operation)
{
    if (!isKernelInterpolation(sampling.interpolation) || !isKernelBorderMode(sampling.border) ||
        sampling.border == KernelBorderMode::NotApplicable || !isSamplingInvalidPolicy(sampling.invalid) ||
        !std::isfinite(sampling.constantBorder) || !std::isfinite(sampling.invalidSample))
    {
        return geometryFailure(ErrorCode::InvalidArgument, operation,
                               "a geometry sampler needs known interpolation, border and invalid-neighbour rules "
                               "and finite substitute values");
    }
    return Status::success();
}

/// The source's descriptor with a new extent, filter array and pixel aspect, and everything else kept.
///
/// @return the descriptor; `ErrorCode::OutOfRange` for an extent that is empty or beyond
///         `defaultEngineLimits()`; or `ErrorCode::InvalidArgument` when the three do not form a
///         descriptor.
[[nodiscard]] Result<ImageDescriptor> changedDescriptor(const ImageDescriptor& source, std::uint32_t width,
                                                        std::uint32_t height, const CfaDescriptor& cfa,
                                                        double pixelAspectRatio, const char* operation)
{
    const std::optional<ImageExtent> extent =
        ImageExtent::create(width, height, source.extent().channelCount(), defaultEngineLimits());
    if (!extent.has_value())
    {
        return geometryFailure(ErrorCode::OutOfRange, operation,
                               "the destination extent is empty or beyond the default engine limits");
    }
    ImageDescriptorRequest request = source.toRequest();
    request.extent = *extent;
    request.cfa = cfa;
    request.pixelAspectRatio = pixelAspectRatio;
    const std::optional<ImageDescriptor> descriptor = ImageDescriptor::create(request);
    if (!descriptor.has_value())
    {
        return geometryFailure(ErrorCode::InvalidArgument, operation,
                               "the transformed extent, CFA phase or pixel aspect does not form an image descriptor");
    }
    return *descriptor;
}

/// Multiplies an extent by a factor, answering false rather than wrapping when the product does not
/// fit in 32 bits.
[[nodiscard]] bool checkedProduct(std::uint32_t value, std::uint32_t factor, std::uint32_t& answer) noexcept
{
    const std::uint64_t product = static_cast<std::uint64_t>(value) * factor;
    if (product > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    answer = static_cast<std::uint32_t>(product);
    return true;
}

/// Whether an integer-resample request names known rules and a factor of at least one.
///
/// @return success; `ErrorCode::InvalidArgument` for a rule outside its enumeration; or
///         `ErrorCode::OutOfRange` for a zero factor.
[[nodiscard]] Status checkIntegerRequest(const IntegerResampleRequest& request, const char* operation)
{
    if (!isIntegerResampleDirection(request.direction) || !isIntegerReduction(request.reduction) ||
        !isIntegerRemainderPolicy(request.remainder))
    {
        return geometryFailure(ErrorCode::InvalidArgument, operation,
                               "the integer direction, reducer or remainder policy is outside its enumeration");
    }
    if (request.factor == 0u)
    {
        return geometryFailure(ErrorCode::OutOfRange, operation, "an integer resample factor is at least one");
    }
    return Status::success();
}

/// An affine request's two-by-three matrix completed to the homogeneous three-by-three one.
[[nodiscard]] std::array<double, 9> affineMatrix(const std::array<double, 6>& affine) noexcept
{
    return {affine[0], affine[1], affine[2], affine[3], affine[4], affine[5], 0.0, 0.0, 1.0};
}

/// Whether every coefficient of a matrix is finite.
[[nodiscard]] bool matrixIsFinite(const std::array<double, 9>& matrix) noexcept
{
    return std::ranges::all_of(matrix, [](double value) { return std::isfinite(value); });
}

/// Whether two matrices agree coefficient by coefficient to within rounding.
///
/// Rebuilding a rotation plan passes through its angle, and the cosine and sine of an angle
/// recovered by `atan2` are not always the coefficients it came from to the bit, so agreement is to
/// thirty-two machine epsilons relative to the larger magnitude, or absolute below a magnitude of one.
[[nodiscard]] bool matricesAgree(const std::array<double, 9>& left, const std::array<double, 9>& right) noexcept
{
    constexpr double kTolerance = 32.0 * std::numeric_limits<double>::epsilon();
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        const double scale = (std::max)({1.0, std::abs(left[index]), std::abs(right[index])});
        if (std::abs(left[index] - right[index]) > kTolerance * scale)
        {
            return false;
        }
    }
    return true;
}

/// The destination-to-source matrix of an exact orientation over a source of the stated extent.
///
/// Every coefficient is an integer, so each destination centre lands exactly on a source centre.
[[nodiscard]] std::array<double, 9> orientationMatrix(ImageOrientation orientation, std::uint32_t sourceWidth,
                                                      std::uint32_t sourceHeight) noexcept
{
    const double lastX = static_cast<double>(sourceWidth) - 1.0;
    const double lastY = static_cast<double>(sourceHeight) - 1.0;
    switch (orientation)
    {
    case ImageOrientation::Identity:
        break;
    case ImageOrientation::MirrorHorizontal:
        return {-1.0, 0.0, lastX, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    case ImageOrientation::Rotate180:
        return {-1.0, 0.0, lastX, 0.0, -1.0, lastY, 0.0, 0.0, 1.0};
    case ImageOrientation::MirrorVertical:
        return {1.0, 0.0, 0.0, 0.0, -1.0, lastY, 0.0, 0.0, 1.0};
    case ImageOrientation::Transpose:
        return {0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    case ImageOrientation::Rotate90Clockwise:
        return {0.0, 1.0, 0.0, -1.0, 0.0, lastY, 0.0, 0.0, 1.0};
    case ImageOrientation::Transverse:
        return {0.0, -1.0, lastX, -1.0, 0.0, lastY, 0.0, 0.0, 1.0};
    case ImageOrientation::Rotate270Clockwise:
        return {0.0, -1.0, lastX, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    }
    return {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
}

/// Which of the eight orientations a matrix is over a source of the stated extent, if any.
[[nodiscard]] std::optional<ImageOrientation>
orientationOfMatrix(const std::array<double, 9>& matrix, std::uint32_t sourceWidth, std::uint32_t sourceHeight) noexcept
{
    for (std::uint8_t index = 0u; index <= static_cast<std::uint8_t>(ImageOrientation::Rotate270Clockwise); ++index)
    {
        const auto orientation = static_cast<ImageOrientation>(index);
        if (matricesAgree(orientationMatrix(orientation, sourceWidth, sourceHeight), matrix))
        {
            return orientation;
        }
    }
    return std::nullopt;
}

/// The filter array a mosaic has once it is oriented, with its phase folded in and stated as zero.
///
/// Read off the source array at the source positions the matrix names, so the answer follows from
/// the transform and the picture's dimensions rather than from a table of pattern renamings.
[[nodiscard]] std::optional<CfaDescriptor> orientedCfa(const CfaDescriptor& source,
                                                       const std::array<double, 9>& matrix) noexcept
{
    if (!source.isMosaiced())
    {
        return source;
    }
    const std::int64_t period = source.hasFilterTable() ? static_cast<std::int64_t>(kCfaTableSide) : 2;
    const auto reduced = [period](std::int64_t value) noexcept {
        return static_cast<std::uint32_t>(((value % period) + period) % period);
    };
    const auto colourAt = [&](std::uint32_t x, std::uint32_t y) noexcept {
        const std::int64_t sourceX = static_cast<std::int64_t>(matrix[0]) * x +
                                     static_cast<std::int64_t>(matrix[1]) * y + static_cast<std::int64_t>(matrix[2]);
        const std::int64_t sourceY = static_cast<std::int64_t>(matrix[3]) * x +
                                     static_cast<std::int64_t>(matrix[4]) * y + static_cast<std::int64_t>(matrix[5]);
        return source.colorAt(reduced(sourceX), reduced(sourceY));
    };
    if (source.hasFilterTable())
    {
        CfaColorEntries entries{};
        for (std::uint32_t row = 0u; row < kCfaTableSide; ++row)
        {
            for (std::uint32_t column = 0u; column < kCfaTableSide; ++column)
            {
                entries[static_cast<std::size_t>(row) * kCfaTableSide + column] = colourAt(column, row);
            }
        }
        return CfaDescriptor::create(entries, 0u, 0u);
    }
    for (const CfaPattern pattern : {CfaPattern::Rggb, CfaPattern::Bggr, CfaPattern::Grbg, CfaPattern::Gbrg})
    {
        const std::optional<CfaDescriptor> candidate = CfaDescriptor::create(pattern, 0u, 0u);
        if (candidate.has_value() && candidate->colorAt(0u, 0u) == colourAt(0u, 0u) &&
            candidate->colorAt(1u, 0u) == colourAt(1u, 0u) && candidate->colorAt(0u, 1u) == colourAt(0u, 1u) &&
            candidate->colorAt(1u, 1u) == colourAt(1u, 1u))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

/// Whether two sampling policies agree field by field, substitute values exactly.
[[nodiscard]] bool samplingAgrees(const SamplingKernelParameters& left, const SamplingKernelParameters& right) noexcept
{
    return left.interpolation == right.interpolation && left.border == right.border &&
           left.constantBorder == right.constantBorder && left.invalid == right.invalid &&
           left.invalidSample == right.invalidSample;
}

/// Whether two integer-resample requests agree field by field.
[[nodiscard]] bool integerRequestAgrees(const IntegerResampleRequest& left,
                                        const IntegerResampleRequest& right) noexcept
{
    return left.factor == right.factor && left.direction == right.direction && left.reduction == right.reduction &&
           left.remainder == right.remainder;
}

/// Whether a supplied plan is the one its builder produces.
///
/// @param rebuilt the builder's answer for the supplied plan's own recorded fields.
/// @param supplied the plan the caller handed to the execution.
/// @return success; the builder's own refusal; or `ErrorCode::IncompatibleArguments` when any
///         recorded field differs, the matrix beyond what `matricesAgree` allows.
[[nodiscard]] Status checkRebuiltPlan(const Result<GeometryPlan>& rebuilt, const GeometryPlan& supplied)
{
    if (!rebuilt.hasValue())
    {
        return rebuilt.status();
    }
    const GeometryPlan& expected = *rebuilt;
    if (expected.operation() != supplied.operation() || expected.sourceDescriptor() != supplied.sourceDescriptor() ||
        expected.destinationDescriptor() != supplied.destinationDescriptor() ||
        !matricesAgree(expected.destinationToSource(), supplied.destinationToSource()) ||
        !samplingAgrees(expected.sampling(), supplied.sampling()) || expected.cropX() != supplied.cropX() ||
        expected.cropY() != supplied.cropY() ||
        !integerRequestAgrees(expected.integerRequest(), supplied.integerRequest()) ||
        expected.resizeRinging() != supplied.resizeRinging())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kTransformOperation,
                               "the supplied geometry plan is not the plan produced by its declared operation");
    }
    return Status::success();
}

/// Rebuilds a plan through the builder its operation names, and holds the plan to the answer.
///
/// `GeometryPlan` has a public constructor so that it stays an ordinary exported value, which means
/// a caller can build one no builder checked. Rather than restate every builder's rules for the
/// execution, this asks the builder again from the plan's own recorded request - its crop origin,
/// destination extent, sampling, integer request or matrix - and compares. A rotation's angle and
/// canvas and an orientation's enumerator are recovered from the matrix and the two extents.
///
/// @param plan the plan the caller handed to the execution.
/// @return success; any refusal of the rebuilding builder; `ErrorCode::IncompatibleArguments` for a
///         plan that differs from the rebuilt one or an orientation matrix that is none of the eight;
///         or `ErrorCode::InvalidArgument` for an operation outside the enumeration.
[[nodiscard]] Status checkPlanInvariants(const GeometryPlan& plan)
{
    const ImageExtent destinationExtent = plan.destinationDescriptor().extent();
    switch (plan.operation())
    {
    case GeometryOperation::Crop:
        return checkRebuiltPlan(planCrop(plan.sourceDescriptor(), {.x = plan.cropX(),
                                                                   .y = plan.cropY(),
                                                                   .width = destinationExtent.width(),
                                                                   .height = destinationExtent.height()}),
                                plan);
    case GeometryOperation::Resize:
        return checkRebuiltPlan(planResize(plan.sourceDescriptor(), {.width = destinationExtent.width(),
                                                                     .height = destinationExtent.height(),
                                                                     .sampling = plan.sampling(),
                                                                     .ringing = plan.resizeRinging()}),
                                plan);
    case GeometryOperation::IntegerResample:
        return checkRebuiltPlan(planIntegerResample(plan.sourceDescriptor(), plan.integerRequest()), plan);
    case GeometryOperation::CfaIntegerResample:
        return checkRebuiltPlan(planCfaIntegerResample(plan.sourceDescriptor(), plan.integerRequest()), plan);
    case GeometryOperation::Rotate:
        return checkRebuiltPlan(
            planRotation(plan.sourceDescriptor(),
                         {.angleRadians = std::atan2(plan.destinationToSource()[1], plan.destinationToSource()[0]),
                          .canvas = destinationExtent == plan.sourceDescriptor().extent() ? RotationCanvas::Preserve
                                                                                          : RotationCanvas::ExpandToFit,
                          .sampling = plan.sampling()}),
            plan);
    case GeometryOperation::AffineWarp:
        return checkRebuiltPlan(
            planAffineWarp(plan.sourceDescriptor(),
                           {.width = destinationExtent.width(),
                            .height = destinationExtent.height(),
                            .destinationToSource = {plan.destinationToSource()[0], plan.destinationToSource()[1],
                                                    plan.destinationToSource()[2], plan.destinationToSource()[3],
                                                    plan.destinationToSource()[4], plan.destinationToSource()[5]},
                            .destinationPixelAspectRatio = plan.destinationDescriptor().pixelAspectRatio(),
                            .sampling = plan.sampling()}),
            plan);
    case GeometryOperation::ProjectiveWarp:
        return checkRebuiltPlan(
            planProjectiveWarp(plan.sourceDescriptor(),
                               {.width = destinationExtent.width(),
                                .height = destinationExtent.height(),
                                .destinationToSource = plan.destinationToSource(),
                                .destinationPixelAspectRatio = plan.destinationDescriptor().pixelAspectRatio(),
                                .sampling = plan.sampling()}),
            plan);
    case GeometryOperation::Orient: {
        const std::optional<ImageOrientation> orientation =
            orientationOfMatrix(plan.destinationToSource(), plan.sourceDescriptor().extent().width(),
                                plan.sourceDescriptor().extent().height());
        if (!orientation.has_value())
        {
            return geometryFailure(ErrorCode::IncompatibleArguments, kTransformOperation,
                                   "an orientation plan's matrix is none of the eight right-angle turns and mirrors");
        }
        return checkRebuiltPlan(planOrientation(plan.sourceDescriptor(), *orientation), plan);
    }
    }
    return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                           "the geometry operation is outside its enumeration");
}

/// Whether the homogeneous denominator is finite and non-zero at every destination pixel centre, as
/// evaluating it at each one would answer.
///
/// The denominator is affine in the centre, so over the destination it takes its extremes at the
/// corners, and a centre's computed denominator is within three rounding errors of the sum of its
/// terms' magnitudes of the exact one - whether or not the multiplications and additions are fused.
/// When every corner is computed clear of zero, on one side of it, by twice a margin larger than that
/// error at the farthest centre, no centre's computed denominator can be zero or non-finite, and the
/// answer is the one evaluating every centre gives. Otherwise every centre is evaluated.
///
/// @return success, or `ErrorCode::InvalidArgument` at the first centre where it is not.
[[nodiscard]] Status checkProjectiveDenominators(const std::array<double, 9>& matrix, std::uint32_t width,
                                                 std::uint32_t height, const char* operation)
{
    if (width != 0u && height != 0u)
    {
        const double lastX = static_cast<double>(width - 1u);
        const double lastY = static_cast<double>(height - 1u);
        const double magnitude = std::abs(matrix[6]) * lastX + std::abs(matrix[7]) * lastY + std::abs(matrix[8]);
        if (std::isfinite(magnitude) && magnitude < std::numeric_limits<double>::max() / 16.0)
        {
            const double margin = 8.0 * std::numeric_limits<double>::epsilon() * magnitude;
            const auto at = [&matrix](double x, double y) { return matrix[6] * x + matrix[7] * y + matrix[8]; };
            const std::array<double, 4u> corners{at(0.0, 0.0), at(lastX, 0.0), at(0.0, lastY), at(lastX, lastY)};
            const double smallest = (std::min)((std::min)(corners[0], corners[1]), (std::min)(corners[2], corners[3]));
            const double largest = (std::max)((std::max)(corners[0], corners[1]), (std::max)(corners[2], corners[3]));
            if (smallest > 2.0 * margin || largest < -2.0 * margin)
            {
                return Status::success();
            }
        }
    }
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double denominator =
                matrix[6] * static_cast<double>(x) + matrix[7] * static_cast<double>(y) + matrix[8];
            if (!std::isfinite(denominator) || denominator == 0.0)
            {
                return geometryFailure(ErrorCode::InvalidArgument, operation,
                                       "the projective denominator is zero or non-finite at a destination center");
            }
        }
    }
    return Status::success();
}

/// The scheduler's cost of one destination row, so it chunks rows rather than guessing.
[[nodiscard]] TaskCost geometryRowCost(std::uint32_t width, std::uint64_t operationsPerPixel = 12u) noexcept
{
    return TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * 12u,
                    .operationsPerItem = static_cast<std::uint64_t>(width) * operationsPerPixel};
}

/// Which plane of a layout holds a channel, and which sample of a pixel within that plane.
///
/// @return the plane index, then the index within the pixel.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> channelAddress(const ImageLayout& layout,
                                                                     std::uint32_t channel) noexcept
{
    return {channel / layout.samplesPerPixel(), channel % layout.samplesPerPixel()};
}

/// One stored sample of one channel at one pixel, read through the layout's arrangement.
///
/// The position must lie inside the layout; nothing here checks it.
template <typename SampleT>
[[nodiscard]] const SampleT& sampleAt(const void* samples, const ImageLayout& layout, std::uint32_t channel,
                                      std::uint32_t x, std::uint32_t y) noexcept
{
    const auto [plane, within] = channelAddress(layout, channel);
    const std::uint64_t offset = *layout.sampleByteOffset(plane, y, x, within);
    return *reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(samples) + offset);
}

/// The writable counterpart of the reader above, under the same unchecked precondition.
template <typename SampleT>
[[nodiscard]] SampleT& sampleAt(void* samples, const ImageLayout& layout, std::uint32_t channel, std::uint32_t x,
                                std::uint32_t y) noexcept
{
    const auto [plane, within] = channelAddress(layout, channel);
    const std::uint64_t offset = *layout.sampleByteOffset(plane, y, x, within);
    return *reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(samples) + offset);
}

/// One destination column or row of a `LinearExact` resize: two source taps and their fixed-point
/// weights, which sum to the coefficient scale.
struct LinearExactAxis
{
    /// The first tap's source index.
    std::uint32_t lower = 0u;

    /// The second tap's source index, equal to the first at an edge.
    std::uint32_t upper = 0u;

    /// The first tap's weight, in units of one over the coefficient scale.
    std::int64_t lowerWeight = 0;

    /// The second tap's weight, in the same units.
    std::int64_t upperWeight = 0;
};

/// One destination column or row of the direct Float32 bilinear resize: the two taps the reference
/// sampler places for its coordinate, clamped into the source, and their weights.
struct DirectLinearAxis
{
    /// The first tap's source index.
    std::uint32_t lower = 0u;

    /// The second tap's source index.
    std::uint32_t upper = 0u;

    /// The first tap's weight.
    double lowerWeight = 1.0;

    /// The second tap's weight.
    double upperWeight = 0.0;
};

/// The linear taps of one source coordinate, with the clamp border applied to both indices.
[[nodiscard]] DirectLinearAxis directLinearAxis(double coordinate, std::uint32_t sourceSize) noexcept
{
    const reference_detail::SamplingAxis sampled =
        reference_detail::samplingAxis(coordinate, KernelInterpolation::Linear);
    const std::int64_t last = static_cast<std::int64_t>(sourceSize - 1u);
    return {.lower = static_cast<std::uint32_t>(std::clamp(sampled.indices[0], std::int64_t{0}, last)),
            .upper = static_cast<std::uint32_t>(std::clamp(sampled.indices[1], std::int64_t{0}, last)),
            .lowerWeight = sampled.weights[0],
            .upperWeight = sampled.weights[1]};
}

/// Whether a representation has a `LinearExact` fixed-point rule: the three that OpenCV's
/// `INTER_LINEAR_EXACT` resizes in fixed point.
template <typename SampleT>
inline constexpr bool kHasLinearExact = std::is_same_v<SampleT, std::uint8_t> ||
                                        std::is_same_v<SampleT, std::uint16_t> || std::is_same_v<SampleT, std::int16_t>;

/// How many fractional bits that rule's coefficients carry: eight for UInt8 and sixteen for the two
/// 16-bit representations.
template <typename SampleT>
inline constexpr std::uint32_t kLinearExactCoefficientBits = std::is_same_v<SampleT, std::uint8_t> ? 8u : 16u;

/// The fixed-point taps `INTER_LINEAR_EXACT` resolves for one destination index.
///
/// A coordinate before the first source centre or past the last puts the whole weight on the edge
/// sample; between them the fraction becomes the second tap's coefficient.
///
/// @tparam SampleT one of the three representations with a fixed-point rule.
/// @param destinationIndex the destination column or row.
/// @param sourceSize the source extent along the axis.
/// @param sourceScale source samples per destination sample, formed as the reciprocal of the
///        destination-over-source ratio.
/// @return the two taps and their weights.
template <typename SampleT>
[[nodiscard]] LinearExactAxis linearExactAxis(std::uint32_t destinationIndex, std::uint32_t sourceSize,
                                              double sourceScale) noexcept
{
    static_assert(kHasLinearExact<SampleT>);
    constexpr std::int64_t kCoefficientScale = std::int64_t{1} << kLinearExactCoefficientBits<SampleT>;
    if (sourceSize == 1u)
    {
        return {.lower = 0u, .upper = 0u, .lowerWeight = kCoefficientScale, .upperWeight = 0};
    }

    // The operation order is `INTER_LINEAR_EXACT`'s: OpenCV receives destination/source as its
    // inverse scale and its exact path takes the reciprocal before evaluating the half-pixel
    // coordinate. The coefficient conversion is round-to-nearest, ties-to-even.
    const double coordinate = sourceScale * (static_cast<double>(destinationIndex) + 0.5) - 0.5;
    const std::int64_t lower = static_cast<std::int64_t>(std::floor(coordinate));
    if (lower < 0)
    {
        return {.lower = 0u, .upper = 0u, .lowerWeight = kCoefficientScale, .upperWeight = 0};
    }
    if (lower >= static_cast<std::int64_t>(sourceSize - 1u))
    {
        const std::uint32_t last = sourceSize - 1u;
        return {.lower = last, .upper = last, .lowerWeight = kCoefficientScale, .upperWeight = 0};
    }

    const double fraction = coordinate - static_cast<double>(lower);
    const std::int64_t upperWeight =
        std::clamp(static_cast<std::int64_t>(
                       roundReal(fraction * static_cast<double>(kCoefficientScale), RoundingMode::NearestEven)),
                   std::int64_t{0}, kCoefficientScale);
    return {.lower = static_cast<std::uint32_t>(lower),
            .upper = static_cast<std::uint32_t>(lower + 1),
            .lowerWeight = kCoefficientScale - upperWeight,
            .upperWeight = upperWeight};
}

/// Integer division rounding towards negative infinity, for a positive denominator.
[[nodiscard]] std::int64_t floorDivide(std::int64_t numerator, std::int64_t denominator) noexcept
{
    return numerator >= 0 ? numerator / denominator : -((-numerator + denominator - 1) / denominator);
}

/// The fixed-point bilinear value of four stored samples, rounded to the representation.
///
/// The horizontal sums carry the coefficient bits once and the vertical sum twice, all exactly in
/// 64-bit integers; adding half of the final scale and dividing downwards is the rule's output
/// rounding - half towards positive infinity, for a negative Int16 sum as for any other. The weights
/// are non-negative and sum to the scale, so the answer lies between the four samples and the
/// narrowing cast cannot overflow.
template <typename SampleT>
[[nodiscard]] SampleT linearExactValue(SampleT upperLeft, SampleT upperRight, SampleT lowerLeft, SampleT lowerRight,
                                       const LinearExactAxis& xAxis, const LinearExactAxis& yAxis) noexcept
{
    static_assert(kHasLinearExact<SampleT>);
    constexpr std::uint32_t kCoefficientBits = kLinearExactCoefficientBits<SampleT>;
    constexpr std::int64_t kResultScale = std::int64_t{1} << (2u * kCoefficientBits);
    const std::int64_t upper = static_cast<std::int64_t>(upperLeft) * xAxis.lowerWeight +
                               static_cast<std::int64_t>(upperRight) * xAxis.upperWeight;
    const std::int64_t lower = static_cast<std::int64_t>(lowerLeft) * xAxis.lowerWeight +
                               static_cast<std::int64_t>(lowerRight) * xAxis.upperWeight;
    const std::int64_t weighted = upper * yAxis.lowerWeight + lower * yAxis.upperWeight;
    const std::int64_t rounded = floorDivide(weighted + kResultScale / 2, kResultScale);
    return static_cast<SampleT>(rounded);
}

void stateDestinationValidity(const ValidityMaskView<std::uint8_t>& mask, std::uint32_t channel, std::uint32_t x,
                              std::uint32_t y, bool valid) noexcept;
[[nodiscard]] bool selected(const GeometrySelection& selection, std::uint32_t channel, std::uint32_t x, std::uint32_t y,
                            double value) noexcept;

/// Resizes a UInt8, UInt16 or Int16 image under `LinearExact` with the clamp border and every tap
/// required, in any layout and under any validity rule.
///
/// Each output is the fixed-point bilinear value of its four taps, computed in the
/// representation's own integers. An output with a non-zero-weighted tap the validity rule refuses
/// is left unwritten and invalid. The values are computed rather than copied, so
/// `exactStoredValues` is false.
template <typename SampleT>
[[nodiscard]] Status
runLinearExactResize(const ExecutionContext& execution, const void* source, const ImageLayout& sourceLayout,
                     void* destination, const ImageLayout& destinationLayout, const GeometrySelection& selection,
                     const ValidityMaskView<std::uint8_t>& destinationValidity, GeometryResult& result)
{
    static_assert(kHasLinearExact<SampleT>);
    const std::uint32_t sourceWidth = sourceLayout.extent().width();
    const std::uint32_t sourceHeight = sourceLayout.extent().height();
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const double sourceScaleX = 1.0 / (static_cast<double>(destinationWidth) / static_cast<double>(sourceWidth));
    const double sourceScaleY = 1.0 / (static_cast<double>(destinationHeight) / static_cast<double>(sourceHeight));
    std::atomic<std::uint64_t> validCount{0u};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const Status ran =
            parallelFor(execution, destinationHeight, geometryRowCost(destinationWidth, 24u), [&](TaskRange range) {
                std::uint64_t localValid = 0u;
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    const LinearExactAxis yAxis = linearExactAxis<SampleT>(y, sourceHeight, sourceScaleY);
                    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                    {
                        const LinearExactAxis xAxis = linearExactAxis<SampleT>(x, sourceWidth, sourceScaleX);
                        const SampleT upperLeft =
                            sampleAt<SampleT>(source, sourceLayout, channel, xAxis.lower, yAxis.lower);
                        const SampleT upperRight =
                            sampleAt<SampleT>(source, sourceLayout, channel, xAxis.upper, yAxis.lower);
                        const SampleT lowerLeft =
                            sampleAt<SampleT>(source, sourceLayout, channel, xAxis.lower, yAxis.upper);
                        const SampleT lowerRight =
                            sampleAt<SampleT>(source, sourceLayout, channel, xAxis.upper, yAxis.upper);
                        const auto accepted = [&](SampleT value, std::uint32_t sourceX, std::uint32_t sourceY,
                                                  std::int64_t weight) {
                            return weight == 0 ||
                                   selected(selection, channel, sourceX, sourceY, static_cast<double>(value));
                        };
                        const bool valid =
                            accepted(upperLeft, xAxis.lower, yAxis.lower, xAxis.lowerWeight * yAxis.lowerWeight) &&
                            accepted(upperRight, xAxis.upper, yAxis.lower, xAxis.upperWeight * yAxis.lowerWeight) &&
                            accepted(lowerLeft, xAxis.lower, yAxis.upper, xAxis.lowerWeight * yAxis.upperWeight) &&
                            accepted(lowerRight, xAxis.upper, yAxis.upper, xAxis.upperWeight * yAxis.upperWeight);
                        stateDestinationValidity(destinationValidity, channel, x, y, valid);
                        if (valid)
                        {
                            sampleAt<SampleT>(destination, destinationLayout, channel, x, y) =
                                linearExactValue(upperLeft, upperRight, lowerLeft, lowerRight, xAxis, yAxis);
                            ++localValid;
                        }
                    }
                }
                validCount.fetch_add(localValid, std::memory_order_relaxed);
                return Status::success();
            });
        if (ran.failed())
        {
            return ran;
        }
    }

    const std::uint64_t outputCount = static_cast<std::uint64_t>(destinationWidth) * destinationHeight * channels;
    result.run.selection.total = outputCount;
    result.run.selection.valid = validCount.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, destinationHeight, geometryRowCost(destinationWidth, 24u));
    result.workingBytes = 0u;
    result.exactStoredValues = false;
    return Status::success();
}

/// Resizes single-sample Float32 planes whose every sample is valid, under `Linear` or `LinearExact`
/// with the clamp border and every tap required, straight into the destination.
///
/// The column taps are resolved once for the operation into context memory and the row taps once
/// per row. Each output adds its non-zero products in Float64 in the reference sampler's order and
/// arithmetic - a zero-weight tap is skipped rather than added, as the sampler skips it - so the
/// bytes are the general path's, and since every output is written the mask is stated once.
[[nodiscard]] Status runDirectLinearFloatResize(const ExecutionContext& execution, const float* source,
                                                const ImageLayout& sourceLayout, float* destination,
                                                const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                                const ValidityMaskView<std::uint8_t>& destinationValidity,
                                                GeometryResult& result)
{
    const std::uint32_t sourceWidth = sourceLayout.extent().width();
    const std::uint32_t sourceHeight = sourceLayout.extent().height();
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const std::array<double, 9>& matrix = plan.destinationToSource();
    const std::optional<std::uint64_t> axisBytes = (CheckedSize{destinationWidth} * sizeof(DirectLinearAxis)).value();
    if (!axisBytes.has_value())
    {
        return geometryFailure(ErrorCode::LimitExceeded, kTransformOperation,
                               "the direct linear-resize axis table overflows");
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, *axisBytes, kDefaultBufferAlignmentBytes, kTransformOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const xAxes = static_cast<DirectLinearAxis*>(memory->data());
    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
    {
        if ((x & 0xFFFu) == 0u)
        {
            const Status running = execution.checkCancellation();
            if (running.failed())
            {
                return running;
            }
        }
        xAxes[x] = directLinearAxis(matrix[0] * static_cast<double>(x) + matrix[2], sourceWidth);
    }

    const std::uint64_t sourceRowStride = sourceLayout.rowStrideBytes() / sizeof(float);
    const std::uint64_t sourcePlaneStride = sourceLayout.planeStrideBytes() / sizeof(float);
    const std::uint64_t destinationRowStride = destinationLayout.rowStrideBytes() / sizeof(float);
    const std::uint64_t destinationPlaneStride = destinationLayout.planeStrideBytes() / sizeof(float);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const float* const sourcePlane = source + static_cast<std::uint64_t>(channel) * sourcePlaneStride;
        float* const destinationPlane = destination + static_cast<std::uint64_t>(channel) * destinationPlaneStride;
        const Status ran =
            parallelFor(execution, destinationHeight, geometryRowCost(destinationWidth, 16u), [&](TaskRange range) {
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    const DirectLinearAxis yAxis =
                        directLinearAxis(matrix[4] * static_cast<double>(y) + matrix[5], sourceHeight);
                    const float* const upperRow =
                        sourcePlane + static_cast<std::uint64_t>(yAxis.lower) * sourceRowStride;
                    const float* const lowerRow =
                        sourcePlane + static_cast<std::uint64_t>(yAxis.upper) * sourceRowStride;
                    float* const outputRow = destinationPlane + static_cast<std::uint64_t>(y) * destinationRowStride;
                    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                    {
                        const DirectLinearAxis& xAxis = xAxes[x];
                        double weighted = 0.0;
                        double weight = xAxis.lowerWeight * yAxis.lowerWeight;
                        if (weight != 0.0)
                        {
                            weighted += weight * static_cast<double>(upperRow[xAxis.lower]);
                        }
                        weight = xAxis.upperWeight * yAxis.lowerWeight;
                        if (weight != 0.0)
                        {
                            weighted += weight * static_cast<double>(upperRow[xAxis.upper]);
                        }
                        weight = xAxis.lowerWeight * yAxis.upperWeight;
                        if (weight != 0.0)
                        {
                            weighted += weight * static_cast<double>(lowerRow[xAxis.lower]);
                        }
                        weight = xAxis.upperWeight * yAxis.upperWeight;
                        if (weight != 0.0)
                        {
                            weighted += weight * static_cast<double>(lowerRow[xAxis.upper]);
                        }
                        outputRow[x] = static_cast<float>(weighted);
                    }
                }
                return Status::success();
            });
        if (ran.failed())
        {
            return ran;
        }
    }

    destinationValidity.fill(true);
    const std::uint64_t outputCount = static_cast<std::uint64_t>(destinationWidth) * destinationHeight * channels;
    result.run.selection.total = outputCount;
    result.run.selection.valid = outputCount;
    result.run.outputsWritten = outputCount;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, destinationHeight, geometryRowCost(destinationWidth, 16u));
    result.workingBytes = memory->bytes();
    result.exactStoredValues = false;
    return Status::success();
}

/// Whether the source validity rule admits one sample: the value rule first, then, when a mask is
/// given, its bit - on the channel's own plane, or on the one plane every channel shares.
[[nodiscard]] bool selected(const GeometrySelection& selection, std::uint32_t channel, std::uint32_t x, std::uint32_t y,
                            double value) noexcept
{
    bool valueValid = true;
    switch (selection.validity.rule())
    {
    case ValidityRule::AllValid:
    case ValidityRule::ExplicitMask:
        break;
    case ValidityRule::FiniteOnly:
        valueValid = std::isfinite(value);
        break;
    case ValidityRule::ZeroIsNoData:
        valueValid = value != 0.0;
        break;
    case ValidityRule::DomainRange:
        valueValid = selection.validity.lowestValid().has_value() && selection.validity.highestValid().has_value() &&
                     value >= *selection.validity.lowestValid() && value <= *selection.validity.highestValid();
        break;
    }
    if (!valueValid || selection.mask == nullptr)
    {
        return valueValid;
    }
    const std::uint32_t plane = selection.mask->planeCount() == 1u ? 0u : channel;
    return selection.mask->test(plane, x, y).value_or(false);
}

/// States one output bit.
///
/// On a mask with a plane per channel the bit is the sample's own. On a mask with one shared plane
/// the first channel writes it and every later channel can only clear it, which leaves the
/// intersection of what the channels wrote.
void stateDestinationValidity(const ValidityMaskView<std::uint8_t>& mask, std::uint32_t channel, std::uint32_t x,
                              std::uint32_t y, bool valid) noexcept
{
    const std::uint32_t plane = mask.planeCount() == 1u ? 0u : channel;
    if (mask.planeCount() == 1u && channel != 0u)
    {
        if (!valid)
        {
            static_cast<void>(mask.setValid(plane, x, y, false));
        }
        return;
    }
    static_cast<void>(mask.setValid(plane, x, y, valid));
}

/// The source index `INTER_NEAREST_EXACT` picks for one destination index.
///
/// The step is source over destination in 16.16 fixed point, rounded to nearest; the origin is half
/// a step, less one unit of the last place for an odd source extent; their accumulation is floored
/// and clamped into the source. Evaluating the half-pixel nearest rule in this accumulator differs
/// from evaluating it in real arithmetic at some extent ratios, which is why `NearestExact` and
/// `Nearest` stay distinct choices.
[[nodiscard]] std::uint32_t nearestExactIndex(std::uint32_t destinationIndex, std::uint32_t sourceSize,
                                              std::uint32_t destinationSize) noexcept
{
    constexpr std::int64_t kCoordinateScale = std::int64_t{1} << 16u;
    const std::int64_t fixedStep = static_cast<std::int64_t>(
        ((static_cast<std::uint64_t>(sourceSize) << 16u) + destinationSize / 2u) / destinationSize);
    const std::int64_t fixedOrigin = fixedStep / 2 - static_cast<std::int64_t>(sourceSize % 2u);
    const std::int64_t fixedCoordinate = fixedStep * destinationIndex + fixedOrigin;
    const std::int64_t sourceIndex = std::clamp(floorDivide(fixedCoordinate, kCoordinateScale), std::int64_t{0},
                                                static_cast<std::int64_t>(sourceSize - 1u));
    return static_cast<std::uint32_t>(sourceIndex);
}

/// Resizes under `NearestExact` in any layout and under any validity rule.
///
/// Each output copies its one source sample. A sample the validity rule refuses leaves its output
/// unwritten and invalid, unless the policy substitutes, when the substitute is converted to the
/// representation and written. The border mode is never consulted, because the index is clamped into
/// the source. `exactStoredValues` is true unless something was substituted.
template <typename SampleT>
[[nodiscard]] Status
runNearestExactResize(const ExecutionContext& execution, const void* source, const ImageLayout& sourceLayout,
                      void* destination, const ImageLayout& destinationLayout, const GeometryPlan& plan,
                      const GeometrySelection& selection, const ValidityMaskView<std::uint8_t>& destinationValidity,
                      GeometryResult& result)
{
    const std::uint32_t sourceWidth = sourceLayout.extent().width();
    const std::uint32_t sourceHeight = sourceLayout.extent().height();
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    std::atomic<std::uint64_t> validCount{0u};
    std::atomic<std::uint64_t> substitutedCount{0u};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const Status ran =
            parallelFor(execution, destinationHeight, geometryRowCost(destinationWidth, 6u), [&](TaskRange range) {
                std::uint64_t localValid = 0u;
                std::uint64_t localSubstituted = 0u;
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    const std::uint32_t sourceY = nearestExactIndex(y, sourceHeight, destinationHeight);
                    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                    {
                        const std::uint32_t sourceX = nearestExactIndex(x, sourceWidth, destinationWidth);
                        const SampleT value = sampleAt<SampleT>(source, sourceLayout, channel, sourceX, sourceY);
                        bool valid = selected(selection, channel, sourceX, sourceY, static_cast<double>(value));
                        if (valid)
                        {
                            sampleAt<SampleT>(destination, destinationLayout, channel, x, y) = value;
                        }
                        else if (plan.sampling().invalid == SamplingInvalidPolicy::Substitute)
                        {
                            sampleAt<SampleT>(destination, destinationLayout, channel, x, y) =
                                convertSample<float, SampleT>(plan.sampling().invalidSample,
                                                              ConversionPolicy{.domain = DomainMapping::CodeValue})
                                    .value;
                            valid = true;
                            ++localSubstituted;
                        }
                        stateDestinationValidity(destinationValidity, channel, x, y, valid);
                        localValid += valid ? 1u : 0u;
                    }
                }
                validCount.fetch_add(localValid, std::memory_order_relaxed);
                substitutedCount.fetch_add(localSubstituted, std::memory_order_relaxed);
                return Status::success();
            });
        if (ran.failed())
        {
            return ran;
        }
    }

    const std::uint64_t outputCount = static_cast<std::uint64_t>(destinationWidth) * destinationHeight * channels;
    result.run.selection.total = outputCount;
    result.run.selection.valid = validCount.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, destinationHeight, geometryRowCost(destinationWidth, 6u));
    result.workingBytes = 0u;
    result.exactStoredValues = substitutedCount.load(std::memory_order_relaxed) == 0u;
    return Status::success();
}

/// Resizes all-valid, single-sample planes with either of the two nearest coordinate conventions.
///
/// A nearest resize needs one source index per destination column and one per destination row. The
/// general sampling path represents each as a weighted tap and revisits validity for every output;
/// this path resolves the column indices once, copies the selected stored value, and states the
/// all-valid destination mask once. `NearestExact` evaluates its fixed-point coordinate rule, while
/// `Nearest` evaluates the plan's own destination-to-source matrix before rounding.
///
/// @tparam SampleT the resident sample representation.
/// @param execution the operation policy and engine-context lease.
/// @param source the image to read.
/// @param sourceLayout where its single-sample planes are.
/// @param destination the resize to write.
/// @param destinationLayout where its single-sample planes go.
/// @param plan the nearest resize, using a clamp border over all-valid samples.
/// @param destinationValidity the mask to state as entirely valid.
/// @param result where the counts go.
/// @return success, or an allocation, execution or cancellation failure.
template <typename SampleT>
[[nodiscard]] Status
runDirectNearestResize(const ExecutionContext& execution, const void* source, const ImageLayout& sourceLayout,
                       void* destination, const ImageLayout& destinationLayout, const GeometryPlan& plan,
                       const ValidityMaskView<std::uint8_t>& destinationValidity, GeometryResult& result)
{
    const std::uint32_t sourceWidth = sourceLayout.extent().width();
    const std::uint32_t sourceHeight = sourceLayout.extent().height();
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const std::uint32_t planes = destinationLayout.planeCount();
    const bool exact = plan.sampling().interpolation == KernelInterpolation::NearestExact;
    const std::optional<std::uint64_t> indexBytes = (CheckedSize{destinationWidth} * sizeof(std::uint32_t)).value();
    if (!indexBytes.has_value())
    {
        return geometryFailure(ErrorCode::LimitExceeded, kTransformOperation,
                               "the direct nearest-resize index table overflows");
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, *indexBytes, kDefaultBufferAlignmentBytes, kTransformOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const sourceColumns = static_cast<std::uint32_t*>(memory->data());
    const std::array<double, 9>& matrix = plan.destinationToSource();
    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
    {
        if (exact)
        {
            sourceColumns[x] = nearestExactIndex(x, sourceWidth, destinationWidth);
        }
        else
        {
            const double coordinate = matrix[0] * static_cast<double>(x) + matrix[2];
            const std::int64_t index = static_cast<std::int64_t>(std::floor(coordinate + 0.5));
            sourceColumns[x] = static_cast<std::uint32_t>(
                std::clamp(index, std::int64_t{0}, static_cast<std::int64_t>(sourceWidth - 1u)));
        }
    }

    const std::uint64_t sourceRowStride = sourceLayout.rowStrideBytes() / sizeof(SampleT);
    const std::uint64_t sourcePlaneStride = sourceLayout.planeStrideBytes() / sizeof(SampleT);
    const std::uint64_t destinationRowStride = destinationLayout.rowStrideBytes() / sizeof(SampleT);
    const std::uint64_t destinationPlaneStride = destinationLayout.planeStrideBytes() / sizeof(SampleT);
    const std::uint64_t rowCount = static_cast<std::uint64_t>(planes) * destinationHeight;
    const TaskCost cost = geometryRowCost(destinationWidth, 1u);
    const Status ran = parallelFor(execution, rowCount, cost, [&](TaskRange range) {
        const auto* const typedSource = static_cast<const SampleT*>(source);
        auto* const typedDestination = static_cast<SampleT*>(destination);
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const auto plane = static_cast<std::uint32_t>(item / destinationHeight);
            const auto y = static_cast<std::uint32_t>(item % destinationHeight);
            const std::uint32_t sourceY =
                exact ? nearestExactIndex(y, sourceHeight, destinationHeight)
                      : static_cast<std::uint32_t>(std::clamp(
                            static_cast<std::int64_t>(std::floor(matrix[4] * static_cast<double>(y) + matrix[5] + 0.5)),
                            std::int64_t{0}, static_cast<std::int64_t>(sourceHeight - 1u)));
            const SampleT* const sourceRow = typedSource + static_cast<std::uint64_t>(plane) * sourcePlaneStride +
                                             static_cast<std::uint64_t>(sourceY) * sourceRowStride;
            SampleT* const destinationRow = typedDestination +
                                            static_cast<std::uint64_t>(plane) * destinationPlaneStride +
                                            static_cast<std::uint64_t>(y) * destinationRowStride;
            for (std::uint32_t x = 0u; x < destinationWidth; ++x)
            {
                destinationRow[x] = sourceRow[sourceColumns[x]];
            }
        }
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }

    destinationValidity.fill(true);
    const std::uint64_t outputCount = static_cast<std::uint64_t>(destinationWidth) * destinationHeight * planes;
    result.run.selection.total = outputCount;
    result.run.selection.valid = outputCount;
    result.run.outputsWritten = outputCount;
    result.channelCount = planes;
    result.plannedWorkers = plannedWorkerCount(execution, rowCount, cost);
    result.workingBytes = memory->bytes();
    result.exactStoredValues = true;
    return Status::success();
}

/// Every check `transformImageGeometry` makes before it writes anything, in the order it makes them.
///
/// @return success, or the first refusal: `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one;
///         `ErrorCode::InvalidArgument` for an unknown representation, a null buffer, or a plan whose
///         descriptors state another representation; `ErrorCode::IncompatibleArguments` for a layout
///         whose element size or extent disagrees with the plan; `ErrorCode::InvalidArgument` for a
///         buffer not aligned to its sample; `ErrorCode::IncompatibleArguments` for buffers that
///         overlap; `ErrorCode::InvalidArgument` for an explicit-mask rule without its mask or a mask
///         given to a rule that reads none; `ErrorCode::IncompatibleArguments` for a source or output
///         mask that does not cover its image; `ErrorCode::InvalidArgument` for an operation outside
///         the enumeration, a matrix that is not finite, or a sampling policy `checkSampling` refuses;
///         then whatever `checkPlanInvariants` reports.
[[nodiscard]] Status checkExecution(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                    const ImageLayout& sourceLayout, void* destination,
                                    const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                    const GeometrySelection& selection,
                                    const ValidityMaskView<std::uint8_t>& destinationValidity)
{
    if (!execution.valid())
    {
        return geometryFailure(ErrorCode::NotInitialized, kTransformOperation,
                               "a geometry transform needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!isSampleType(sampleType) || source == nullptr || destination == nullptr ||
        plan.sourceDescriptor().sampleType() != sampleType || plan.destinationDescriptor().sampleType() != sampleType)
    {
        return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                               "the buffers, plan and resident representation do not describe one transform");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (sourceLayout.elementSizeBytes() != elementBytes || destinationLayout.elementSizeBytes() != elementBytes ||
        sourceLayout.extent() != plan.sourceDescriptor().extent() ||
        destinationLayout.extent() != plan.destinationDescriptor().extent())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kTransformOperation,
                               "a source or destination layout disagrees with the plan's descriptor");
    }
    if (reinterpret_cast<std::uintptr_t>(source) % elementBytes != 0u ||
        reinterpret_cast<std::uintptr_t>(destination) % elementBytes != 0u)
    {
        return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                               "a sample buffer is not aligned for its resident representation");
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source), sourceLayout.totalBytes(),
                       static_cast<std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kTransformOperation,
                               "a geometry destination must not share storage with its source");
    }
    if (selection.validity.rule() == ValidityRule::ExplicitMask)
    {
        if (selection.mask == nullptr)
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "an explicit source-validity rule needs its packed bits");
        }
        const Status covers = checkMaskCovers(*selection.mask, sourceLayout.extent(), kTransformOperation);
        if (covers.failed())
        {
            return covers;
        }
    }
    else if (selection.mask != nullptr)
    {
        return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                               "source mask bits were supplied for a validity rule that does not read them");
    }
    const Status outputMask =
        checkMaskCovers(destinationValidity.constView(), destinationLayout.extent(), kTransformOperation);
    if (outputMask.failed())
    {
        return outputMask;
    }
    if (!isGeometryOperation(plan.operation()) || !matrixIsFinite(plan.destinationToSource()))
    {
        return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                               "the geometry plan's operation or matrix is not usable");
    }
    const Status sampling = checkSampling(plan.sampling(), kTransformOperation);
    if (sampling.failed())
    {
        return sampling;
    }
    return checkPlanInvariants(plan);
}

/// The integer coefficients of an orientation plan: source x and y as functions of destination x and y.
struct OrientationSteps
{
    /// How far the source column moves per destination column.
    std::int64_t xFromX = 0;

    /// How far the source column moves per destination row.
    std::int64_t xFromY = 0;

    /// The source column of destination `(0, 0)`.
    std::int64_t xOrigin = 0;

    /// How far the source row moves per destination column.
    std::int64_t yFromX = 0;

    /// How far the source row moves per destination row.
    std::int64_t yFromY = 0;

    /// The source row of destination `(0, 0)`.
    std::int64_t yOrigin = 0;
};

/// Reads an orientation plan's matrix as integer steps, which is exact because every coefficient
/// `orientationMatrix` writes is a whole number.
[[nodiscard]] OrientationSteps orientationStepsOf(const GeometryPlan& plan) noexcept
{
    const std::array<double, 9>& matrix = plan.destinationToSource();
    return {.xFromX = static_cast<std::int64_t>(matrix[0]),
            .xFromY = static_cast<std::int64_t>(matrix[1]),
            .xOrigin = static_cast<std::int64_t>(matrix[2]),
            .yFromX = static_cast<std::int64_t>(matrix[3]),
            .yFromY = static_cast<std::int64_t>(matrix[4]),
            .yOrigin = static_cast<std::int64_t>(matrix[5])};
}

/// Orients an image whose every sample is valid and whose two layouts share an arrangement.
///
/// The bytes of each pixel are copied as stored, so a representation's every bit arrives, and the
/// padding of either layout is neither read nor written.
[[nodiscard]] Status runDirectOrientation(const ExecutionContext& execution, std::uint32_t elementBytes,
                                          const void* source, const ImageLayout& sourceLayout, void* destination,
                                          const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                          const ValidityMaskView<std::uint8_t>& destinationValidity,
                                          GeometryResult& result)
{
    const OrientationSteps steps = orientationStepsOf(plan);
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const std::uint32_t planes = sourceLayout.planeCount();
    const std::size_t pixelBytes = static_cast<std::size_t>(sourceLayout.samplesPerPixel()) * elementBytes;
    const std::uint64_t sourceRowStride = sourceLayout.rowStrideBytes();
    const auto* const sourceBytes = static_cast<const std::uint8_t*>(source);
    auto* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * pixelBytes * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width)};
    const std::uint64_t rowCount = static_cast<std::uint64_t>(planes) * height;
    const Status ran = parallelFor(execution, rowCount, cost, [&](TaskRange range) {
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const auto plane = static_cast<std::uint32_t>(item / height);
            const auto y = static_cast<std::uint32_t>(item % height);
            std::uint8_t* const to = destinationBytes + *destinationLayout.rowByteOffset(plane, y);
            const std::uint8_t* const sourcePlane = sourceBytes + *sourceLayout.rowByteOffset(plane, 0u);
            if (steps.xFromY == 0)
            {
                // The source row is fixed along the destination row and the column steps by one.
                const std::uint64_t sourceRow = static_cast<std::uint64_t>(steps.yFromY * y + steps.yOrigin);
                const std::uint8_t* const from = sourcePlane + sourceRow * sourceRowStride;
                if (steps.xFromX == 1)
                {
                    std::memcpy(to, from, static_cast<std::size_t>(width) * pixelBytes);
                    continue;
                }
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const auto column = static_cast<std::size_t>(steps.xOrigin - static_cast<std::int64_t>(x));
                    std::memcpy(to + static_cast<std::size_t>(x) * pixelBytes, from + column * pixelBytes, pixelBytes);
                }
                continue;
            }
            // The source column is fixed along the destination row and the row steps by one.
            const auto column = static_cast<std::size_t>(steps.xFromY * y + steps.xOrigin);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::uint64_t sourceRow = static_cast<std::uint64_t>(steps.yFromX * x + steps.yOrigin);
                std::memcpy(to + static_cast<std::size_t>(x) * pixelBytes,
                            sourcePlane + sourceRow * sourceRowStride + column * pixelBytes, pixelBytes);
            }
        }
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    destinationValidity.fill(true);
    const std::uint64_t outputCount = static_cast<std::uint64_t>(width) * height * channels;
    result.run.selection.total = outputCount;
    result.run.selection.valid = outputCount;
    result.run.outputsWritten = outputCount;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, rowCount, cost);
    result.workingBytes = 0u;
    result.exactStoredValues = true;
    return Status::success();
}

/// Orients an image under any validity rule, or between two arrangements.
///
/// A sample the rule refuses leaves its destination unwritten and its destination bit clear; every
/// other sample is copied as stored.
template <typename SampleT>
[[nodiscard]] Status runOrientation(const ExecutionContext& execution, const void* source,
                                    const ImageLayout& sourceLayout, void* destination,
                                    const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                    const GeometrySelection& selection,
                                    const ValidityMaskView<std::uint8_t>& destinationValidity, GeometryResult& result)
{
    const OrientationSteps steps = orientationStepsOf(plan);
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    std::atomic<std::uint64_t> validCount{0u};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const Status ran = parallelFor(execution, height, geometryRowCost(width, 4u), [&](TaskRange range) {
            std::uint64_t localValid = 0u;
            for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
            {
                const auto y = static_cast<std::int64_t>(rowIndex);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const auto sourceX =
                        static_cast<std::uint32_t>(steps.xFromX * x + steps.xFromY * y + steps.xOrigin);
                    const auto sourceY =
                        static_cast<std::uint32_t>(steps.yFromX * x + steps.yFromY * y + steps.yOrigin);
                    const SampleT& stored = sampleAt<SampleT>(source, sourceLayout, channel, sourceX, sourceY);
                    const bool valid = selected(selection, channel, sourceX, sourceY, static_cast<double>(stored));
                    stateDestinationValidity(destinationValidity, channel, x, static_cast<std::uint32_t>(y), valid);
                    if (valid)
                    {
                        std::memcpy(&sampleAt<SampleT>(destination, destinationLayout, channel, x,
                                                       static_cast<std::uint32_t>(y)),
                                    &stored, sizeof(SampleT));
                        ++localValid;
                    }
                }
            }
            validCount.fetch_add(localValid, std::memory_order_relaxed);
            return Status::success();
        });
        if (ran.failed())
        {
            return ran;
        }
    }
    result.run.selection.total = static_cast<std::uint64_t>(width) * height * channels;
    result.run.selection.valid = validCount.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, height, geometryRowCost(width, 4u));
    result.workingBytes = 0u;
    result.exactStoredValues = true;
    return Status::success();
}

/// Copies a crop whose every sample is valid, one row of one plane at a time.
///
/// Under the all-valid rule with no mask a crop moves stored bytes and decides nothing per sample,
/// so it is a contiguous copy per row of each plane - the same bytes whatever the representation -
/// and the destination validity is stated once for the whole mask rather than one bit at a time.
/// Both sides must share an arrangement, because a row of one plane is contiguous only within it.
///
/// @param execution the operation policy and engine-context lease.
/// @param elementBytes the width of one sample.
/// @param source the image to read.
/// @param sourceLayout where its samples are.
/// @param destination the crop to write.
/// @param destinationLayout where its samples go, with the same samples per pixel and plane count.
/// @param plan the crop.
/// @param destinationValidity the mask to state as entirely valid.
/// @param result where the counts go.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status runDirectCrop(const ExecutionContext& execution, std::uint32_t elementBytes, const void* source,
                                   const ImageLayout& sourceLayout, void* destination,
                                   const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                   const ValidityMaskView<std::uint8_t>& destinationValidity, GeometryResult& result)
{
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const std::uint32_t samplesPerPixel = sourceLayout.samplesPerPixel();
    const std::uint32_t planes = sourceLayout.planeCount();
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * samplesPerPixel * elementBytes;
    const std::uint64_t leadBytes = static_cast<std::uint64_t>(plan.cropX()) * samplesPerPixel * elementBytes;
    const auto* const sourceBytes = static_cast<const std::uint8_t*>(source);
    auto* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const TaskCost cost{.bytesPerItem = rowBytes * 2u, .operationsPerItem = static_cast<std::uint64_t>(width)};
    const std::uint64_t rowCount = static_cast<std::uint64_t>(planes) * height;
    const Status ran = parallelFor(execution, rowCount, cost, [&](TaskRange range) {
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const auto plane = static_cast<std::uint32_t>(item / height);
            const auto y = static_cast<std::uint32_t>(item % height);
            const std::uint64_t from = *sourceLayout.rowByteOffset(plane, y + plan.cropY()) + leadBytes;
            const std::uint64_t to = *destinationLayout.rowByteOffset(plane, y);
            std::memcpy(destinationBytes + to, sourceBytes + from, static_cast<std::size_t>(rowBytes));
        }
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    destinationValidity.fill(true);
    const std::uint64_t outputCount = static_cast<std::uint64_t>(width) * height * channels;
    result.run.selection.total = outputCount;
    result.run.selection.valid = outputCount;
    result.run.outputsWritten = outputCount;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, rowCount, cost);
    result.workingBytes = 0u;
    result.exactStoredValues = true;
    return Status::success();
}

/// Whether a source's validity is decided by no mask and at most the finiteness of a sample: the all-valid rule, or
/// the finite-only rule, which admits every integer and every finite floating sample.
[[nodiscard]] bool decidedByFiniteness(const GeometrySelection& selection) noexcept
{
    return selection.mask == nullptr && (selection.validity.rule() == ValidityRule::AllValid ||
                                         selection.validity.rule() == ValidityRule::FiniteOnly);
}

/// Destination pixels of one row a direct reduction sums at a time.
constexpr std::uint32_t kDirectReductionChunk = 256u;

/// Source samples of one row an integer reduction of a planar source adds down its block's rows at a time.
constexpr std::uint32_t kVerticalReductionSpan = 2048u;

/// Averages each factor-by-factor block of a source whose validity no mask decides, one destination row of one channel
/// at a time.
///
/// The generic reducer reads every sample through the layout and the validity rule. Here a block's sum is the same
/// Float64 additions of its counted samples in the same order, the block's rows top to bottom and each row left to
/// right, read from contiguous source rows; its mean is the same division by how many it counted, and it returns to
/// the stored representation through the same conversion, so every output is the generic reducer's. The additions run
/// across a chunk of outputs at a time, so the sums of neighbouring blocks proceed together rather than one after
/// another, and the destination validity is stated once for the whole mask. Under the finite-only rule a floating
/// sample that is not finite is left out of its block's sum and count; a block with nothing finite in it has no
/// output to write, which this does not state, so it says so and the generic reducer runs instead.
///
/// An integer block's Float64 sum is exact while the block cannot reach 2^53 - every partial sum is an integer the
/// double holds - so it is the integer sum, in whatever order it is taken: an integer source sums each block row as a
/// run of its own in integer arithmetic, and converts the block's sum once. Over a planar source the block's rows are
/// first added down, sample by sample along a span of the row, which is one contiguous pass the compiler vectorizes,
/// and each block then sums its columns of that span.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the image to read.
/// @param sourceLayout where its samples are.
/// @param destination the reduction to write.
/// @param destinationLayout where its samples go.
/// @param plan the reduction, whose factor divides the destination's extent out of the source's.
/// @param selection the source's validity: the all-valid or the finite-only rule, with no mask.
/// @param destinationValidity the mask to state as entirely valid.
/// @param result where the counts go.
/// @param emptyBlock set when a block held nothing finite, in which case nothing else is stated.
/// @return success, or an execution or cancellation failure.
template <typename SampleT>
[[nodiscard]] Status
runDirectAverageReduction(const ExecutionContext& execution, const void* source, const ImageLayout& sourceLayout,
                          void* destination, const ImageLayout& destinationLayout, const GeometryPlan& plan,
                          const GeometrySelection& selection, const ValidityMaskView<std::uint8_t>& destinationValidity,
                          GeometryResult& result, bool& emptyBlock)
{
    // An integer is always finite, so only a floating sample under the finite-only rule is read to know it counts.
    const bool checkFinite = std::is_floating_point_v<SampleT> && selection.validity.rule() == ValidityRule::FiniteOnly;
    using WideT = std::conditional_t<std::is_signed_v<SampleT>, std::int64_t, std::uint64_t>;
    bool integerSums = false;
    if constexpr (std::is_integral_v<SampleT>)
    {
        const double largest = (std::max)(-static_cast<double>((std::numeric_limits<SampleT>::min)()),
                                          static_cast<double>((std::numeric_limits<SampleT>::max)()));
        integerSums = static_cast<double>(plan.integerRequest().factor) * plan.integerRequest().factor * largest <
                      9007199254740992.0;
    }
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const std::uint32_t factor = plan.integerRequest().factor;
    const auto blockSamples = static_cast<double>(static_cast<std::uint64_t>(factor) * factor);
    const std::uint64_t sourceStep = sourceLayout.samplesPerPixel();
    const std::uint64_t blockStep = static_cast<std::uint64_t>(factor) * sourceStep;
    const std::uint64_t destinationStep = destinationLayout.samplesPerPixel();
    const auto* const sourceBytes = static_cast<const std::uint8_t*>(source);
    auto* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * factor * factor * sizeof(SampleT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * factor * factor};
    const std::uint64_t rowCount = static_cast<std::uint64_t>(channels) * height;
    std::atomic<bool> empty{false};
    const Status ran = parallelFor(execution, rowCount, cost, [&](TaskRange range) {
        std::array<double, kDirectReductionChunk> sums{};
        std::array<std::uint64_t, kDirectReductionChunk> counted{};
        std::array<WideT, kDirectReductionChunk> wide{};
        std::array<WideT, kVerticalReductionSpan> columns{};
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const auto channel = static_cast<std::uint32_t>(item / height);
            const auto y = static_cast<std::uint32_t>(item % height);
            const auto [sourcePlane, sourceWithin] = channelAddress(sourceLayout, channel);
            const auto [destinationPlane, destinationWithin] = channelAddress(destinationLayout, channel);
            SampleT* const written =
                reinterpret_cast<SampleT*>(destinationBytes + *destinationLayout.rowByteOffset(destinationPlane, y)) +
                destinationWithin;
            if (integerSums && sourceStep == 1u && factor <= kVerticalReductionSpan)
            {
                const std::uint32_t spanOutputs = kVerticalReductionSpan / factor;
                for (std::uint32_t first = 0u; first < width; first += spanOutputs)
                {
                    const std::uint32_t outputs = (std::min)(spanOutputs, width - first);
                    const std::uint32_t span = outputs * factor;
                    std::fill_n(columns.begin(), span, WideT{0});
                    for (std::uint32_t by = 0u; by < factor; ++by)
                    {
                        const SampleT* const row =
                            reinterpret_cast<const SampleT*>(
                                sourceBytes + *sourceLayout.rowByteOffset(sourcePlane, y * factor + by)) +
                            sourceWithin + static_cast<std::uint64_t>(first) * factor;
                        for (std::uint32_t sample = 0u; sample < span; ++sample)
                        {
                            columns[sample] += static_cast<WideT>(row[sample]);
                        }
                    }
                    for (std::uint32_t x = 0u; x < outputs; ++x)
                    {
                        WideT block = 0;
                        for (std::uint32_t bx = 0u; bx < factor; ++bx)
                        {
                            block += columns[x * factor + bx];
                        }
                        written[static_cast<std::uint64_t>(first + x) * destinationStep] =
                            convertSample<double, SampleT>(static_cast<double>(block) / blockSamples,
                                                           ConversionPolicy{.domain = DomainMapping::CodeValue})
                                .value;
                    }
                }
                continue;
            }
            for (std::uint32_t first = 0u; first < width; first += kDirectReductionChunk)
            {
                const std::uint32_t chunk = (std::min)(kDirectReductionChunk, width - first);
                if (integerSums)
                {
                    std::fill_n(wide.begin(), chunk, WideT{0});
                    for (std::uint32_t by = 0u; by < factor; ++by)
                    {
                        const SampleT* const row =
                            reinterpret_cast<const SampleT*>(
                                sourceBytes + *sourceLayout.rowByteOffset(sourcePlane, y * factor + by)) +
                            sourceWithin + static_cast<std::uint64_t>(first) * blockStep;
                        for (std::uint32_t x = 0u; x < chunk; ++x)
                        {
                            const SampleT* const block = row + static_cast<std::uint64_t>(x) * blockStep;
                            WideT run = 0;
                            for (std::uint32_t bx = 0u; bx < factor; ++bx)
                            {
                                run += static_cast<WideT>(block[static_cast<std::uint64_t>(bx) * sourceStep]);
                            }
                            wide[x] += run;
                        }
                    }
                    for (std::uint32_t x = 0u; x < chunk; ++x)
                    {
                        written[static_cast<std::uint64_t>(first + x) * destinationStep] =
                            convertSample<double, SampleT>(static_cast<double>(wide[x]) / blockSamples,
                                                           ConversionPolicy{.domain = DomainMapping::CodeValue})
                                .value;
                    }
                    continue;
                }
                std::fill_n(sums.begin(), chunk, 0.0);
                std::fill_n(counted.begin(), chunk, std::uint64_t{0u});
                for (std::uint32_t by = 0u; by < factor; ++by)
                {
                    const SampleT* const row =
                        reinterpret_cast<const SampleT*>(sourceBytes +
                                                         *sourceLayout.rowByteOffset(sourcePlane, y * factor + by)) +
                        sourceWithin + static_cast<std::uint64_t>(first) * blockStep;
                    for (std::uint32_t bx = 0u; bx < factor; ++bx)
                    {
                        const SampleT* const column = row + static_cast<std::uint64_t>(bx) * sourceStep;
                        if (checkFinite)
                        {
                            for (std::uint32_t x = 0u; x < chunk; ++x)
                            {
                                const auto value =
                                    static_cast<double>(column[static_cast<std::uint64_t>(x) * blockStep]);
                                if (std::isfinite(value))
                                {
                                    sums[x] += value;
                                    ++counted[x];
                                }
                            }
                            continue;
                        }
                        for (std::uint32_t x = 0u; x < chunk; ++x)
                        {
                            sums[x] += static_cast<double>(column[static_cast<std::uint64_t>(x) * blockStep]);
                        }
                    }
                }
                for (std::uint32_t x = 0u; x < chunk; ++x)
                {
                    if (checkFinite && counted[x] == 0u)
                    {
                        empty.store(true, std::memory_order_relaxed);
                        continue;
                    }
                    const double samplesCounted = checkFinite ? static_cast<double>(counted[x]) : blockSamples;
                    written[static_cast<std::uint64_t>(first + x) * destinationStep] =
                        convertSample<double, SampleT>(sums[x] / samplesCounted,
                                                       ConversionPolicy{.domain = DomainMapping::CodeValue})
                            .value;
                }
            }
        }
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    emptyBlock = empty.load(std::memory_order_relaxed);
    if (emptyBlock)
    {
        return Status::success();
    }
    destinationValidity.fill(true);
    const std::uint64_t outputCount = static_cast<std::uint64_t>(width) * height * channels;
    result.run.selection.total = outputCount;
    result.run.selection.valid = outputCount;
    result.run.outputsWritten = outputCount;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, rowCount, cost);
    result.workingBytes = 0u;
    result.exactStoredValues = false;
    return Status::success();
}

/// One destination column or row of an axis-aligned resize, resolved once for the operation.
///
/// A resize maps a destination column to one source coordinate whatever the row, and a row to one
/// whatever the column, so the taps and weights `samplingAxis` derives from a coordinate - and the
/// border rule applied to each tap - belong to the column or row rather than to the pixel. The
/// per-pixel sampler reads these entries in exactly the order and arithmetic of
/// `reference_detail::sampleInterpolated`, which is what keeps its bytes those of the scalar
/// authority while the transcendental weights are evaluated once per axis rather than per pixel.
struct ResizeAxisEntry
{
    /// Border-resolved source indices, holding the extent for a tap outside a constant border.
    std::array<std::int64_t, 8u> indices{};

    /// The normalized tap weights, in tap order.
    std::array<double, 8u> weights{};

    /// How many taps the interpolation reads along this axis.
    std::uint32_t count = 0u;

    /// Whether the taps read consecutive samples inside the source, so a pass may walk them as one
    /// run rather than resolve each one; false near a border, where a tap folds or reads a constant.
    bool contiguous = false;
};

/// One destination column or row of an area reduction: the source cells its footprint covers.
struct AreaAxisEntry
{
    /// Inclusive footprint boundary, in source cells.
    double low = 0.0;

    /// Exclusive footprint boundary, in source cells.
    double high = 0.0;

    /// First source cell the footprint touches.
    std::int64_t begin = 0;

    /// One past the last source cell the footprint touches.
    std::int64_t end = 0;
};

/// The taps of one axis at one coordinate, with the border rule already applied.
///
/// @param coordinate where along the axis, under the pixel-center convention.
/// @param size the source extent along the axis.
/// @param interpolation what the operation interpolates with.
/// @param border what it does with a tap outside the source.
/// @return the entry.
[[nodiscard]] ResizeAxisEntry resizeAxisEntry(double coordinate, std::int64_t size, KernelInterpolation interpolation,
                                              KernelBorderMode border) noexcept
{
    const reference_detail::SamplingAxis axis = reference_detail::samplingAxis(coordinate, interpolation);
    ResizeAxisEntry entry{};
    entry.count = axis.count;
    bool contiguous = axis.count != 0u;
    for (std::uint32_t tap = 0u; tap < axis.count; ++tap)
    {
        entry.indices[tap] = kernelBorderIndex(axis.indices[tap], size, border);
        entry.weights[tap] = axis.weights[tap];
        contiguous = contiguous && entry.indices[tap] == entry.indices[0] + static_cast<std::int64_t>(tap) &&
                     entry.indices[tap] >= 0 && entry.indices[tap] < size;
    }
    entry.contiguous = contiguous;
    return entry;
}

/// The footprint of one destination cell of an area reduction along one axis.
///
/// @param destinationIndex the destination column or row.
/// @param scale source cells per destination cell.
/// @param size the source extent along the axis.
/// @return the entry, whose cell range is clipped to the source.
[[nodiscard]] AreaAxisEntry areaAxisEntry(std::uint32_t destinationIndex, double scale, std::int64_t size) noexcept
{
    const double low = static_cast<double>(destinationIndex) * scale;
    const double high = static_cast<double>(destinationIndex + 1u) * scale;
    return {.low = low,
            .high = high,
            .begin = (std::max)(std::int64_t{0}, static_cast<std::int64_t>(std::floor(low))),
            .end = (std::min)(size, static_cast<std::int64_t>(std::ceil(high)))};
}

/// `reference_detail::sampleInterpolated` over resolved axis entries.
///
/// The loop nest, the zero-weight skip, the border and validity decisions, the Float64 sums and
/// the final division are the sampler's, term for term; only where the taps come from differs.
///
/// @param plan what the operation resolved once for the plane.
/// @param xAxis the destination column's taps.
/// @param yAxis the destination row's taps.
/// @param output the sample, written only when the call returns true.
/// @return true when the taps the policy requires were accepted and `output` was written.
[[nodiscard]] inline bool sampleTabulated(const reference_detail::SamplingPlan& plan, const ResizeAxisEntry& xAxis,
                                          const ResizeAxisEntry& yAxis, float& output) noexcept
{
    double weighted = 0.0;
    double acceptedWeight = 0.0;
    const reference_detail::ValidityAccessor validity{*plan.validity};
    for (std::uint32_t yTap = 0u; yTap < yAxis.count; ++yTap)
    {
        const std::int64_t resolvedY = yAxis.indices[yTap];
        const bool outsideY = resolvedY == plan.height;
        const std::uint64_t rowBase = outsideY ? 0u : static_cast<std::uint64_t>(resolvedY) * plan.rowStride;
        for (std::uint32_t xTap = 0u; xTap < xAxis.count; ++xTap)
        {
            const double weight = xAxis.weights[xTap] * yAxis.weights[yTap];
            if (weight == 0.0)
            {
                continue;
            }
            const bool constant = xAxis.indices[xTap] == plan.width || outsideY;
            float sample = plan.constantBorder;
            bool accepted = constant;
            if (!constant)
            {
                const auto x = static_cast<std::uint32_t>(xAxis.indices[xTap]);
                sample = plan.samples[rowBase + static_cast<std::uint64_t>(x)];
                accepted =
                    plan.acceptsEverySample || validity.accepts(sample, x, static_cast<std::uint32_t>(resolvedY));
            }
            if (!accepted)
            {
                if (plan.invalid == SamplingInvalidPolicy::RequireAll)
                {
                    return false;
                }
                if (plan.invalid == SamplingInvalidPolicy::Renormalize)
                {
                    continue;
                }
                sample = plan.invalidSample;
            }
            weighted += weight * static_cast<double>(sample);
            acceptedWeight += weight;
        }
    }
    if (std::abs(acceptedWeight) <= std::numeric_limits<double>::epsilon())
    {
        return false;
    }
    output =
        static_cast<float>(plan.invalid == SamplingInvalidPolicy::Renormalize ? weighted / acceptedWeight : weighted);
    return true;
}

/// `reference_detail::sampleArea` over resolved footprint entries.
///
/// @param plan what the operation resolved once for the plane.
/// @param xAxis the destination column's footprint.
/// @param yAxis the destination row's footprint.
/// @param output the integrated sample, written only on success.
/// @return true when at least one accepted contributor exists and the policy permits the write.
[[nodiscard]] inline bool sampleAreaTabulated(const reference_detail::SamplingPlan& plan, const AreaAxisEntry& xAxis,
                                              const AreaAxisEntry& yAxis, float& output) noexcept
{
    const reference_detail::ValidityAccessor validity{*plan.validity};
    double weighted = 0.0;
    double acceptedWeight = 0.0;
    for (std::int64_t y = yAxis.begin; y < yAxis.end; ++y)
    {
        const double yWeight =
            (std::min)(yAxis.high, static_cast<double>(y + 1)) - (std::max)(yAxis.low, static_cast<double>(y));
        if (yWeight <= 0.0)
        {
            continue;
        }
        const std::uint64_t rowBase = static_cast<std::uint64_t>(y) * plan.rowStride;
        for (std::int64_t x = xAxis.begin; x < xAxis.end; ++x)
        {
            const double xWeight =
                (std::min)(xAxis.high, static_cast<double>(x + 1)) - (std::max)(xAxis.low, static_cast<double>(x));
            const double weight = xWeight * yWeight;
            if (weight <= 0.0)
            {
                continue;
            }
            const auto sampleX = static_cast<std::uint32_t>(x);
            const auto sampleY = static_cast<std::uint32_t>(y);
            float sample = plan.samples[rowBase + sampleX];
            const bool accepted = plan.acceptsEverySample || validity.accepts(sample, sampleX, sampleY);
            if (!accepted)
            {
                if (plan.invalid == SamplingInvalidPolicy::RequireAll)
                {
                    return false;
                }
                if (plan.invalid == SamplingInvalidPolicy::Renormalize)
                {
                    continue;
                }
                sample = plan.invalidSample;
            }
            weighted += weight * static_cast<double>(sample);
            acceptedWeight += weight;
        }
    }
    if (acceptedWeight <= 0.0)
    {
        return false;
    }
    output = static_cast<float>(weighted / acceptedWeight);
    return true;
}

/// Whether a plan's coordinates separate into one per column and one per row.
///
/// True of every resize plan, whose matrix scales and translates each axis on its own; false of a
/// rotation or warp, whose coordinates are a property of the pixel.
///
/// @param plan the plan.
/// @return whether the destination-to-source matrix is axis-aligned with a unit denominator.
[[nodiscard]] bool coordinatesSeparate(const GeometryPlan& plan) noexcept
{
    const std::array<double, 9>& matrix = plan.destinationToSource();
    return plan.operation() == GeometryOperation::Resize && matrix[1] == 0.0 && matrix[3] == 0.0 && matrix[6] == 0.0 &&
           matrix[7] == 0.0 && matrix[8] == 1.0;
}

/// The six axis tables of one axis-aligned resize, carved from one block.
struct ResizeTables
{
    /// The destination columns' taps under the operation's own interpolation.
    ResizeAxisEntry* xAxes = nullptr;

    /// The destination rows' taps under the operation's own interpolation.
    ResizeAxisEntry* yAxes = nullptr;

    /// The columns' taps under the linear rule the dark-ringing guide reads with when enlarging.
    ResizeAxisEntry* guideXAxes = nullptr;

    /// The rows' taps under that guide rule.
    ResizeAxisEntry* guideYAxes = nullptr;

    /// The columns' source footprints for an area reduction.
    AreaAxisEntry* areaXAxes = nullptr;

    /// The rows' source footprints for an area reduction.
    AreaAxisEntry* areaYAxes = nullptr;
};

/// One run of adjacent output columns whose source indices are adjacent too and whose weights are
/// all non-zero.
struct PackedResizeRun
{
    /// First destination column in the run.
    std::uint32_t begin = 0u;

    /// One past the last destination column in the run.
    std::uint32_t end = 0u;

    /// Source index paired with `begin`; later columns advance by one.
    std::int64_t sourceBegin = 0;
};

/// Column taps of a separable resize, grouped by tap rather than by destination column.
///
/// The scalar authority stores all taps of one column together. A column pass instead visits the
/// same tap across every output column, so this transposed view gives the compiler contiguous
/// weights and indices while each output still accumulates its taps in their original order.
struct PackedResizeColumns
{
    /// Border-resolved source indices, one contiguous destination-width run per tap.
    std::int64_t* indices = nullptr;

    /// Normalized weights paired with `indices`.
    double* weights = nullptr;

    /// Adjacent source runs, grouped by tap, with a gather fallback when they are too fragmented.
    PackedResizeRun* runs = nullptr;

    /// Start of each tap's run range; `runOffsets[count]` is one past the final run.
    std::array<std::uint32_t, 9u> runOffsets{};

    /// Destination columns in each tap run.
    std::uint32_t width = 0u;

    /// Taps in each destination column.
    std::uint32_t count = 0u;

    /// Whether every index names a source sample rather than the constant-border sentinel.
    bool allInBounds = false;
};

/// Bytes a packed column table occupies.
///
/// @param width destination columns.
/// @param count taps per column.
/// @return the checked byte count.
[[nodiscard]] CheckedSize packedResizeColumnBytes(std::uint32_t width, std::uint32_t count) noexcept
{
    return CheckedSize{width} * count * (sizeof(std::int64_t) + sizeof(double) + sizeof(PackedResizeRun));
}

/// Transposes scalar axis entries into tap-major column runs over caller-owned memory.
///
/// @param axes the scalar axis entries.
/// @param width destination columns.
/// @param count taps per column.
/// @param sourceWidth the source width; this value is the constant-border sentinel.
/// @param block aligned storage of at least `packedResizeColumnBytes(width, count)` bytes.
/// @return the packed view.
[[nodiscard]] PackedResizeColumns packResizeColumns(const ResizeAxisEntry* axes, std::uint32_t width,
                                                    std::uint32_t count, std::uint32_t sourceWidth,
                                                    void* block) noexcept
{
    PackedResizeColumns packed{};
    packed.indices = static_cast<std::int64_t*>(block);
    packed.weights = reinterpret_cast<double*>(packed.indices + static_cast<std::uint64_t>(width) * count);
    packed.runs = reinterpret_cast<PackedResizeRun*>(packed.weights + static_cast<std::uint64_t>(width) * count);
    packed.width = width;
    packed.count = count;
    packed.allInBounds = true;
    std::uint32_t runCount = 0u;
    for (std::uint32_t tap = 0u; tap < count; ++tap)
    {
        packed.runOffsets[tap] = runCount;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::size_t packedIndex = static_cast<std::size_t>(tap) * width + x;
            packed.indices[packedIndex] = axes[x].indices[tap];
            packed.weights[packedIndex] = axes[x].weights[tap];
            packed.allInBounds = packed.allInBounds && axes[x].indices[tap] != sourceWidth;
        }
        // A run holds no zero weight. The scalar authority adds an exact zero for such a tap rather
        // than its product, and a column sum starts at +0.0 and so is never -0.0, which makes that
        // addition the identity; leaving the tap out of every run is therefore the same sum, and
        // it spares each run the select that would otherwise keep it scalar.
        std::uint32_t x = 0u;
        while (x < width)
        {
            if (axes[x].weights[tap] == 0.0)
            {
                ++x;
                continue;
            }
            const std::uint32_t begin = x;
            const std::int64_t sourceBegin = axes[x].indices[tap];
            ++x;
            while (x < width && axes[x].weights[tap] != 0.0 &&
                   axes[x].indices[tap] == sourceBegin + static_cast<std::int64_t>(x - begin))
            {
                ++x;
            }
            packed.runs[runCount++] = {.begin = begin, .end = x, .sourceBegin = sourceBegin};
        }
    }
    packed.runOffsets[count] = runCount;
    return packed;
}

/// Bytes the tables of one resize occupy. Every entry size is a multiple of eight bytes, so the
/// regions that follow the tables in one block start where their types need them.
///
/// @param destinationWidth destination columns.
/// @param destinationHeight destination rows.
/// @return the byte count, unchecked: the caller composes it with the rest of its working set.
[[nodiscard]] CheckedSize resizeTableBytes(std::uint32_t destinationWidth, std::uint32_t destinationHeight) noexcept
{
    const std::uint64_t axisEntries = static_cast<std::uint64_t>(destinationWidth) + destinationHeight;
    return CheckedSize{axisEntries} * (sizeof(ResizeAxisEntry) * 2u + sizeof(AreaAxisEntry));
}

/// The tables laid out over the start of one block.
///
/// @param block where the tables start; at least `resizeTableBytes` long and eight-byte aligned.
/// @param destinationWidth destination columns.
/// @param destinationHeight destination rows.
/// @return the six tables.
[[nodiscard]] ResizeTables resizeTablesOver(void* block, std::uint32_t destinationWidth,
                                            std::uint32_t destinationHeight) noexcept
{
    ResizeTables tables{};
    tables.xAxes = static_cast<ResizeAxisEntry*>(block);
    tables.yAxes = tables.xAxes + destinationWidth;
    tables.guideXAxes = tables.yAxes + destinationHeight;
    tables.guideYAxes = tables.guideXAxes + destinationWidth;
    tables.areaXAxes = reinterpret_cast<AreaAxisEntry*>(tables.guideYAxes + destinationHeight);
    tables.areaYAxes = tables.areaXAxes + destinationWidth;
    return tables;
}

/// Resolves every column and row of an axis-aligned resize once.
///
/// The coordinate of a column is what `buildCoordinates` would compute for it on any row, spelled
/// with the same terms so the two agree to the bit, and the row likewise.
///
/// @param execution the operation, for its cancellation checkpoint.
/// @param plan the resize.
/// @param sourceWidth source columns.
/// @param sourceHeight source rows.
/// @param destinationWidth destination columns.
/// @param destinationHeight destination rows.
/// @param areaScaleX source columns per destination column.
/// @param areaScaleY source rows per destination row.
/// @param buildGuideTaps whether the linear dark-ringing guide needs tap entries.
/// @param buildAreaFootprints whether an area operation or guide needs footprint entries.
/// @param tables where the entries go.
/// @return success, or a non-finite coordinate or cancellation failure.
[[nodiscard]] Status buildResizeTables(const ExecutionContext& execution, const GeometryPlan& plan,
                                       std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                       std::uint32_t destinationWidth, std::uint32_t destinationHeight,
                                       double areaScaleX, double areaScaleY, bool buildGuideTaps,
                                       bool buildAreaFootprints, const ResizeTables& tables)
{
    const std::array<double, 9>& matrix = plan.destinationToSource();
    const KernelInterpolation interpolation = plan.sampling().interpolation;
    const KernelBorderMode border = plan.sampling().border;
    const std::uint64_t itemCount = static_cast<std::uint64_t>(destinationWidth) + destinationHeight;
    const std::uint64_t operations = interpolation == KernelInterpolation::Lanczos4
                                         ? 128u
                                         : (interpolation == KernelInterpolation::Cubic ? 32u : 8u);
    return parallelFor(
        execution, itemCount, {.bytesPerItem = sizeof(ResizeAxisEntry), .operationsPerItem = operations},
        [&](TaskRange range) {
            for (std::uint64_t item = range.begin; item < range.end; ++item)
            {
                if (item < destinationWidth)
                {
                    const auto x = static_cast<std::uint32_t>(item);
                    const double denominator = matrix[6] * x + matrix[7] * 0u + matrix[8];
                    const double sx = (matrix[0] * x + matrix[1] * 0u + matrix[2]) / denominator;
                    if (!std::isfinite(sx))
                    {
                        return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                               "a transformed coordinate is non-finite");
                    }
                    tables.xAxes[x] =
                        resizeAxisEntry(sx, static_cast<std::int64_t>(sourceWidth), interpolation, border);
                    if (buildGuideTaps)
                    {
                        tables.guideXAxes[x] = resizeAxisEntry(sx, static_cast<std::int64_t>(sourceWidth),
                                                               KernelInterpolation::Area, border);
                    }
                    if (buildAreaFootprints)
                    {
                        tables.areaXAxes[x] = areaAxisEntry(x, areaScaleX, static_cast<std::int64_t>(sourceWidth));
                    }
                    continue;
                }
                const auto y = static_cast<std::uint32_t>(item - destinationWidth);
                const double denominator = matrix[6] * 0u + matrix[7] * y + matrix[8];
                const double sy = (matrix[3] * 0u + matrix[4] * y + matrix[5]) / denominator;
                if (!std::isfinite(sy))
                {
                    return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                           "a transformed coordinate is non-finite");
                }
                tables.yAxes[y] = resizeAxisEntry(sy, static_cast<std::int64_t>(sourceHeight), interpolation, border);
                if (buildGuideTaps)
                {
                    tables.guideYAxes[y] =
                        resizeAxisEntry(sy, static_cast<std::int64_t>(sourceHeight), KernelInterpolation::Area, border);
                }
                if (buildAreaFootprints)
                {
                    tables.areaYAxes[y] = areaAxisEntry(y, areaScaleY, static_cast<std::int64_t>(sourceHeight));
                }
            }
            return Status::success();
        });
}

/// How many adjacent output samples the all-valid resize sums at once.
constexpr std::uint32_t kResizeBlock = 8u;

/// Up to `kResizeBlock` adjacent output samples of one row, each summed in the sampler's own order.
///
/// One sample at a time serializes the processor on that sample's own sum; eight independent
/// sums give it eight chains to overlap. Each chain still adds its terms exactly as
/// `sampleTabulated` would - row taps outside, column taps inside - and a zero weight contributes
/// an exact zero to both sums rather than being skipped, which leaves every bit where the sampler
/// puts it. Only an all-accepting plan reaches here, so no tap is ever rejected.
///
/// @param plan what the operation resolved once for the plane, accepting every sample.
/// @param xAxes the taps of the first output column and those after it.
/// @param yAxis the taps of the output row.
/// @param count how many adjacent samples, at most `kResizeBlock`.
/// @param output where the samples go.
/// @param valid one byte per sample, set to one when it was written.
void sampleTabulatedBlock(const reference_detail::SamplingPlan& plan, const ResizeAxisEntry* xAxes,
                          const ResizeAxisEntry& yAxis, std::uint32_t count, float* output,
                          std::uint8_t* valid) noexcept
{
    std::array<double, kResizeBlock> weighted{};
    std::array<double, kResizeBlock> accepted{};
    const std::uint32_t xTaps = xAxes[0].count;
    for (std::uint32_t yTap = 0u; yTap < yAxis.count; ++yTap)
    {
        const std::int64_t resolvedY = yAxis.indices[yTap];
        const bool outsideY = resolvedY == plan.height;
        const std::uint64_t rowBase = outsideY ? 0u : static_cast<std::uint64_t>(resolvedY) * plan.rowStride;
        const double yWeight = yAxis.weights[yTap];
        for (std::uint32_t xTap = 0u; xTap < xTaps; ++xTap)
        {
            for (std::uint32_t sample = 0u; sample < count; ++sample)
            {
                const ResizeAxisEntry& xAxis = xAxes[sample];
                const double weight = xAxis.weights[xTap] * yWeight;
                const bool constant = xAxis.indices[xTap] == plan.width || outsideY;
                const float value = constant ? plan.constantBorder
                                             : plan.samples[rowBase + static_cast<std::uint64_t>(xAxis.indices[xTap])];
                weighted[sample] += weight == 0.0 ? 0.0 : weight * static_cast<double>(value);
                accepted[sample] += weight;
            }
        }
    }
    for (std::uint32_t sample = 0u; sample < count; ++sample)
    {
        if (std::abs(accepted[sample]) <= std::numeric_limits<double>::epsilon())
        {
            valid[sample] = 0u;
            continue;
        }
        output[sample] =
            static_cast<float>(plan.invalid == SamplingInvalidPolicy::Renormalize ? weighted[sample] / accepted[sample]
                                                                                  : weighted[sample]);
        valid[sample] = 1u;
    }
}

/// One destination row of a separable resize: the column taps of every source row it reads, summed.
///
/// A cubic or Lanczos window is a product of a column weight and a row weight, so the sum over
/// the window is the row-weighted sum of column-weighted source rows. The column pass is this
/// function; it adds its terms in tap order, a zero weight contributing an exact zero, and reads
/// the constant border where a tap lies outside the plane.
///
/// @param plan what the operation resolved once for the plane, accepting every sample.
/// @param xAxes the destination columns' taps.
/// @param destinationWidth destination columns.
/// @param sourceRow which source row, or `plan.height` for the row a constant border supplies.
/// @param band where the column sums go, one per destination column.
void resizeColumnPass(const reference_detail::SamplingPlan& plan, const ResizeAxisEntry* xAxes,
                      std::uint32_t destinationWidth, std::int64_t sourceRow, double* band) noexcept
{
    const bool outsideY = sourceRow == plan.height;
    const std::uint64_t rowBase = outsideY ? 0u : static_cast<std::uint64_t>(sourceRow) * plan.rowStride;
    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
    {
        const ResizeAxisEntry& xAxis = xAxes[x];
        double sum = 0.0;
        if (xAxis.contiguous && !outsideY)
        {
            // Every tap of an interior column is the next sample along, so the window is one run.
            const float* const run = plan.samples + rowBase + static_cast<std::uint64_t>(xAxis.indices[0]);
            for (std::uint32_t xTap = 0u; xTap < xAxis.count; ++xTap)
            {
                const double weight = xAxis.weights[xTap];
                sum += weight == 0.0 ? 0.0 : weight * static_cast<double>(run[xTap]);
            }
            band[x] = sum;
            continue;
        }
        for (std::uint32_t xTap = 0u; xTap < xAxis.count; ++xTap)
        {
            const double weight = xAxis.weights[xTap];
            const bool constant = xAxis.indices[xTap] == plan.width || outsideY;
            const float value = constant ? plan.constantBorder
                                         : plan.samples[rowBase + static_cast<std::uint64_t>(xAxis.indices[xTap])];
            sum += weight == 0.0 ? 0.0 : weight * static_cast<double>(value);
        }
        band[x] = sum;
    }
}

/// One source row's column sums through the tap-major view when no tap reads a constant border.
///
/// The tap loop remains outermost so each output observes exactly the scalar authority's addition
/// order. Within one tap, indices and weights are contiguous and independent across columns, which
/// permits ordinary compiler vectorization without putting ISA code in the algorithm layer.
///
/// @param plan what the operation resolved once for the plane, accepting every sample.
/// @param columns the packed destination-column taps.
/// @param sourceRow the in-bounds source row.
/// @param band where the column sums go, one per destination column.
void resizePackedColumnPass(const reference_detail::SamplingPlan& plan, const PackedResizeColumns& columns,
                            std::int64_t sourceRow, double* band) noexcept
{
    // Every bound below is copied into a local first. A compiler without type-based alias analysis
    // cannot tell that a store to `band` leaves a bound it reads through a reference unchanged, and
    // then refuses to vectorize a loop whose trip count might move under it.
    const std::uint32_t width = columns.width;
    const std::uint32_t count = columns.count;
    std::fill_n(band, static_cast<std::size_t>(width), 0.0);
    const float* const row = plan.samples + static_cast<std::uint64_t>(sourceRow) * plan.rowStride;
    const std::uint32_t runCount = columns.runOffsets[count];
    if (runCount <= static_cast<std::uint64_t>(width) * count / 4u)
    {
        for (std::uint32_t tap = 0u; tap < count; ++tap)
        {
            const double* const weights = columns.weights + static_cast<std::uint64_t>(tap) * width;
            const std::uint32_t firstRun = columns.runOffsets[tap];
            const std::uint32_t endRun = columns.runOffsets[tap + 1u];
            for (std::uint32_t runIndex = firstRun; runIndex < endRun; ++runIndex)
            {
                const std::uint32_t begin = columns.runs[runIndex].begin;
                const std::uint32_t length = columns.runs[runIndex].end - begin;
                const float* const samples = row + static_cast<std::uint64_t>(columns.runs[runIndex].sourceBegin);
                const double* const runWeights = weights + begin;
                double* const runBand = band + begin;
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety) interleave(enable)
#elif defined(_MSC_VER) && !defined(__clang__)
#pragma loop(ivdep)
#endif
                // No run holds a zero weight, so no product here is one the scalar authority skips.
                for (std::uint32_t offset = 0u; offset < length; ++offset)
                {
                    runBand[offset] += runWeights[offset] * static_cast<double>(samples[offset]);
                }
            }
        }
        return;
    }
    for (std::uint32_t tap = 0u; tap < count; ++tap)
    {
        const std::int64_t* const indices = columns.indices + static_cast<std::uint64_t>(tap) * width;
        const double* const weights = columns.weights + static_cast<std::uint64_t>(tap) * width;
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety) interleave(enable)
#elif defined(_MSC_VER) && !defined(__clang__)
#pragma loop(ivdep)
#endif
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double weight = weights[x];
            band[x] += weight == 0.0 ? 0.0 : weight * static_cast<double>(row[indices[x]]);
        }
    }
}

/// One source row's area column sums: each destination column's footprint integrated across it.
///
/// The column half of `sampleAreaTabulated`, with the same cell weights; the row half is applied
/// by the caller across a band of these.
///
/// @param plan what the operation resolved once for the plane, accepting every sample.
/// @param xAxes the destination columns' footprints.
/// @param destinationWidth destination columns.
/// @param sourceRow which source row.
/// @param band where the column sums go, one per destination column.
void areaColumnPass(const reference_detail::SamplingPlan& plan, const AreaAxisEntry* xAxes,
                    std::uint32_t destinationWidth, std::int64_t sourceRow, double* band) noexcept
{
    const std::uint64_t rowBase = static_cast<std::uint64_t>(sourceRow) * plan.rowStride;
    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
    {
        const AreaAxisEntry& xAxis = xAxes[x];
        double sum = 0.0;
        for (std::int64_t cell = xAxis.begin; cell < xAxis.end; ++cell)
        {
            const double xWeight = (std::min)(xAxis.high, static_cast<double>(cell + 1)) -
                                   (std::max)(xAxis.low, static_cast<double>(cell));
            if (xWeight <= 0.0)
            {
                continue;
            }
            sum += xWeight * static_cast<double>(plan.samples[rowBase + static_cast<std::uint64_t>(cell)]);
        }
        band[x] = sum;
    }
}

/// How many destination rows one separable strip covers, given how many source rows each one reads.
///
/// The strip's column pass covers every source row its rows read, which grows with the reduction
/// ratio, so a strong reduction takes fewer destination rows per strip and a mild one more; the
/// band a worker holds stays a few hundred rows either way.
///
/// @param areaScaleY source rows per destination row.
/// @return destination rows per strip, at least one.
[[nodiscard]] std::uint32_t resizeStripRows(double areaScaleY) noexcept
{
    const double perRow = (std::max)(1.0, std::ceil(areaScaleY));
    return static_cast<std::uint32_t>(std::clamp(std::floor(256.0 / perRow), 1.0, 32.0));
}

/// Resizes single-sample Float32 planes whose every sample is valid, straight into the destination.
///
/// With nothing to reject and nothing to convert, the working planes, the per-sample validity
/// bookkeeping and the gather and scatter passes of the general path are all overhead: this path
/// resolves the axis tables once, sums each output straight from the source plane into the
/// destination plane in blocks, and states the destination validity once for the whole mask. Its
/// bytes are those of the general path for every interpolation but three: an area reduction, a
/// cubic window and a Lanczos window are evaluated separably - the column pass over each source
/// row the strip reads, then the row pass - which is the same real-valued sum in a different
/// association and lands within one unit in the last place of the direct evaluation; the guard
/// test holds the two point windows to that and the footprint test covers the reduction.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the image to read.
/// @param sourceLayout where its samples are.
/// @param destination the resize to write.
/// @param destinationLayout where its samples go.
/// @param plan the resize.
/// @param destinationValidity the mask to state.
/// @param result where the counts go.
/// @return success, or an allocation, execution or cancellation failure.
[[nodiscard]] Status runDirectFloatResize(const ExecutionContext& execution, const float* source,
                                          const ImageLayout& sourceLayout, float* destination,
                                          const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                          const ValidityMaskView<std::uint8_t>& destinationValidity,
                                          GeometryResult& result)
{
    const std::uint32_t sourceWidth = sourceLayout.extent().width();
    const std::uint32_t sourceHeight = sourceLayout.extent().height();
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const bool downsamplingResize = destinationWidth <= sourceWidth && destinationHeight <= sourceHeight;
    const bool areaResize = downsamplingResize && plan.sampling().interpolation == KernelInterpolation::Area;
    const bool suppressDarkRinging = plan.resizeRinging() == ResizeRingingPolicy::SuppressDarkRinging &&
                                     (plan.sampling().interpolation == KernelInterpolation::Cubic ||
                                      plan.sampling().interpolation == KernelInterpolation::Lanczos4);
    const double areaScaleX = static_cast<double>(sourceWidth) / destinationWidth;
    const double areaScaleY = static_cast<double>(sourceHeight) / destinationHeight;
    const std::uint64_t destinationPixels = static_cast<std::uint64_t>(destinationWidth) * destinationHeight;
    // An area reduction and the two wide windows are products of a column weight and a row weight,
    // so each is the row-weighted sum of column-weighted source rows and runs as two passes.
    const bool separable = areaResize || plan.sampling().interpolation == KernelInterpolation::Cubic ||
                           plan.sampling().interpolation == KernelInterpolation::Lanczos4;
    const bool needsState = !separable || suppressDarkRinging;
    const std::uint32_t packedColumnTaps =
        !areaResize && plan.sampling().interpolation == KernelInterpolation::Cubic
            ? 4u
            : (!areaResize && plan.sampling().interpolation == KernelInterpolation::Lanczos4 ? 8u : 0u);
    const CheckedSize checkedTableBytes = resizeTableBytes(destinationWidth, destinationHeight);
    // The per-axis weight sums a separable pass renormalizes with follow the tables, followed by
    // the tap-major columns and, only when needed, per-output state bytes. The first three regions
    // are eight-byte aligned, so the byte state may take any final length.
    const CheckedSize checkedSumBytes =
        (CheckedSize{destinationWidth} + CheckedSize{destinationHeight}) * (2u * sizeof(double));
    const CheckedSize checkedPackedColumnBytes = packedResizeColumnBytes(destinationWidth, packedColumnTaps);
    const CheckedSize checkedStateBytes = needsState ? CheckedSize{destinationPixels} : CheckedSize{0u};
    const std::optional<std::uint64_t> checkedWorkingBytes =
        (checkedTableBytes + checkedSumBytes + checkedPackedColumnBytes + checkedStateBytes).value();
    if (!checkedWorkingBytes.has_value())
    {
        return geometryFailure(ErrorCode::LimitExceeded, kTransformOperation,
                               "the total geometry working set overflows");
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, *checkedWorkingBytes, kDefaultBufferAlignmentBytes, kTransformOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const block = static_cast<std::uint8_t*>(memory->data());
    const ResizeTables tables = resizeTablesOver(block, destinationWidth, destinationHeight);
    auto* const columnWeight = reinterpret_cast<double*>(block + checkedTableBytes.valueOr(0u));
    auto* const rowWeight = columnWeight + destinationWidth;
    auto* const guideColumnWeight = rowWeight + destinationHeight;
    auto* const guideRowWeight = guideColumnWeight + destinationWidth;
    auto* const packedColumnBlock = block + checkedTableBytes.valueOr(0u) + checkedSumBytes.valueOr(0u);
    auto* const state = needsState ? packedColumnBlock + checkedPackedColumnBytes.valueOr(0u) : nullptr;
    const bool buildGuideTaps = suppressDarkRinging && !downsamplingResize;
    const bool buildAreaFootprints = areaResize || (suppressDarkRinging && downsamplingResize);
    const Status built =
        buildResizeTables(execution, plan, sourceWidth, sourceHeight, destinationWidth, destinationHeight, areaScaleX,
                          areaScaleY, buildGuideTaps, buildAreaFootprints, tables);
    if (built.failed())
    {
        return built;
    }
    PackedResizeColumns packedColumns{};
    if (packedColumnTaps != 0u)
    {
        packedColumns =
            packResizeColumns(tables.xAxes, destinationWidth, packedColumnTaps, sourceWidth, packedColumnBlock);
    }
    const bool renormalize = plan.sampling().invalid == SamplingInvalidPolicy::Renormalize;
    if (!areaResize && renormalize)
    {
        for (std::uint32_t x = 0u; x < destinationWidth; ++x)
        {
            double sum = 0.0;
            for (std::uint32_t tap = 0u; tap < tables.xAxes[x].count; ++tap)
            {
                sum += tables.xAxes[x].weights[tap];
            }
            columnWeight[x] = sum;
        }
        for (std::uint32_t y = 0u; y < destinationHeight; ++y)
        {
            double sum = 0.0;
            for (std::uint32_t tap = 0u; tap < tables.yAxes[y].count; ++tap)
            {
                sum += tables.yAxes[y].weights[tap];
            }
            rowWeight[y] = sum;
        }
    }
    // An area reduction and a dark-ringing guide renormalize with these per-axis weight sums.
    // Ordinary cubic and Lanczos passes do not build or visit the guide tables at all.
    if (areaResize || suppressDarkRinging)
    {
        for (std::uint32_t x = 0u; x < destinationWidth; ++x)
        {
            double sum = 0.0;
            if (downsamplingResize)
            {
                const AreaAxisEntry& axis = tables.areaXAxes[x];
                for (std::int64_t cell = axis.begin; cell < axis.end; ++cell)
                {
                    const double weight = (std::min)(axis.high, static_cast<double>(cell + 1)) -
                                          (std::max)(axis.low, static_cast<double>(cell));
                    sum += weight > 0.0 ? weight : 0.0;
                }
            }
            else
            {
                for (std::uint32_t tap = 0u; tap < tables.guideXAxes[x].count; ++tap)
                {
                    sum += tables.guideXAxes[x].weights[tap];
                }
            }
            guideColumnWeight[x] = sum;
        }
        for (std::uint32_t y = 0u; y < destinationHeight; ++y)
        {
            double sum = 0.0;
            if (downsamplingResize)
            {
                const AreaAxisEntry& axis = tables.areaYAxes[y];
                for (std::int64_t cell = axis.begin; cell < axis.end; ++cell)
                {
                    const double weight = (std::min)(axis.high, static_cast<double>(cell + 1)) -
                                          (std::max)(axis.low, static_cast<double>(cell));
                    sum += weight > 0.0 ? weight : 0.0;
                }
            }
            else
            {
                for (std::uint32_t tap = 0u; tap < tables.guideYAxes[y].count; ++tap)
                {
                    sum += tables.guideYAxes[y].weights[tap];
                }
            }
            guideRowWeight[y] = sum;
        }
    }

    // A separable pass holds, per worker, the column sums of every source row one strip reads:
    // the rows its taps span, plus the one row a constant border supplies.
    const std::uint32_t stripRows = separable ? resizeStripRows(areaScaleY) : 1u;
    const std::uint64_t stripCount = (static_cast<std::uint64_t>(destinationHeight) + stripRows - 1u) / stripRows;
    const std::uint64_t bandRowsMax =
        separable ? static_cast<std::uint64_t>(std::ceil(static_cast<double>(stripRows) * areaScaleY)) +
                        2u * static_cast<std::uint64_t>(tables.yAxes[0].count) + 2u
                  : 0u;
    // The band, the constant-border row and one row of running sums for the row pass, then the
    // same three for the dark-ringing guide.
    const std::uint64_t bandSets = suppressDarkRinging ? 2u : 1u;
    const std::uint64_t bandBytesPerWorker =
        bandSets * (bandRowsMax + 2u) * static_cast<std::uint64_t>(destinationWidth) * sizeof(double);
    const TaskCost stripCost =
        geometryRowCost(destinationWidth, 4u * static_cast<std::uint64_t>(stripRows) *
                                              (static_cast<std::uint64_t>(tables.xAxes[0].count) *
                                                   static_cast<std::uint64_t>(std::ceil(areaScaleY) + 1.0) +
                                               tables.yAxes[0].count + (suppressDarkRinging ? 8u : 0u)));
    const std::uint32_t bandSlots = separable ? plannedWorkerCount(execution, stripCount, stripCost) : 0u;
    const std::optional<std::uint64_t> checkedBandBytes =
        (CheckedSize{bandBytesPerWorker} * static_cast<std::uint64_t>(bandSlots)).value();
    if (!checkedBandBytes.has_value())
    {
        return geometryFailure(ErrorCode::LimitExceeded, kTransformOperation, "the separable resize band overflows");
    }
    Result<plane_detail::ScopedContextMemory> bandMemory = plane_detail::ScopedContextMemory::take(
        execution, *checkedBandBytes, kDefaultBufferAlignmentBytes, kTransformOperation);
    if (!bandMemory.hasValue())
    {
        return bandMemory.status();
    }

    const KernelValidityView allValid{.descriptor = ValidityDescriptor::allValid(), .mask = nullptr, .maskPlane = 0u};
    const std::uint64_t sourceRowStride = sourceLayout.rowStrideBytes() / sizeof(float);
    const std::uint64_t sourcePlaneStride = sourceLayout.planeStrideBytes() / sizeof(float);
    const std::uint64_t destinationRowStride = destinationLayout.rowStrideBytes() / sizeof(float);
    const std::uint64_t destinationPlaneStride = destinationLayout.planeStrideBytes() / sizeof(float);
    const std::uint64_t tapsPerOutput =
        static_cast<std::uint64_t>(tables.xAxes[0].count) * tables.yAxes[0].count + (suppressDarkRinging ? 8u : 0u);
    const TaskCost rowCost = geometryRowCost(destinationWidth, 4u * (std::max)(tapsPerOutput, std::uint64_t{4u}));
    std::uint64_t validCount = 0u;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        reference_detail::SamplingPlan sampling{};
        sampling.samples = source + static_cast<std::uint64_t>(channel) * sourcePlaneStride;
        sampling.rowStride = sourceRowStride;
        sampling.width = static_cast<std::int64_t>(sourceWidth);
        sampling.height = static_cast<std::int64_t>(sourceHeight);
        sampling.interpolation = plan.sampling().interpolation;
        sampling.border = plan.sampling().border;
        sampling.invalid = plan.sampling().invalid;
        sampling.constantBorder = plan.sampling().constantBorder;
        sampling.invalidSample = plan.sampling().invalidSample;
        sampling.validity = &allValid;
        sampling.acceptsEverySample = true;
        reference_detail::SamplingPlan guideSampling = sampling;
        guideSampling.interpolation = KernelInterpolation::Area;
        float* const destinationPlane = destination + static_cast<std::uint64_t>(channel) * destinationPlaneStride;
        const auto sampleGuide = [&](std::uint32_t x, std::uint32_t y, float& value) {
            return downsamplingResize
                       ? sampleAreaTabulated(guideSampling, tables.areaXAxes[x], tables.areaYAxes[y], value)
                       : sampleTabulated(guideSampling, tables.guideXAxes[x], tables.guideYAxes[y], value);
        };
        if (needsState)
        {
            std::fill_n(state, static_cast<std::size_t>(destinationPixels), std::uint8_t{0u});
        }
        // Every output of a row is marked, and a valid one under the dark-ringing guard is compared
        // with its guide, once the row's samples exist; both passes below end with this.
        const auto finishRow = [&](std::uint32_t y, float* row, std::uint8_t* rowState, std::uint64_t& localValid,
                                   std::uint64_t& localGuarded) {
            for (std::uint32_t x = 0u; x < destinationWidth; ++x)
            {
                if (rowState[x] == 0u)
                {
                    continue;
                }
                ++localValid;
                if (suppressDarkRinging)
                {
                    float guide = 0.0f;
                    if (sampleGuide(x, y, guide) && row[x] < 0.98f * guide)
                    {
                        rowState[x] = 2u;
                        ++localGuarded;
                    }
                }
            }
        };
        std::atomic<std::uint64_t> channelValidCount{0u};
        std::atomic<std::uint64_t> guardedCount{0u};
        Status status = Status::success();
        if (separable)
        {
            status = parallelForWorkers(execution, bandSlots, stripCost, [&](WorkerTask task) -> Status {
                // Read once: a bound read through the capture could be moved by any store in the
                // loops below as far as a compiler without type-based alias analysis can tell,
                // and it then leaves every one of them scalar.
                const std::uint32_t width = destinationWidth;
                double* const band =
                    reinterpret_cast<double*>(static_cast<std::uint8_t*>(bandMemory->data()) +
                                              static_cast<std::uint64_t>(task.workerSlot) * bandBytesPerWorker);
                double* const constantBand = band + bandRowsMax * width;
                double* const sums = constantBand + width;
                double* const guideBand = suppressDarkRinging ? sums + width : nullptr;
                double* const guideConstantBand = suppressDarkRinging ? guideBand + bandRowsMax * width : nullptr;
                double* const guideSums = suppressDarkRinging ? guideConstantBand + width : nullptr;
                std::uint64_t localValid = 0u;
                std::uint64_t localGuarded = 0u;
                std::int64_t cachedMin = (std::numeric_limits<std::int64_t>::max)();
                std::int64_t cachedMax = -1;
                for (std::uint64_t partition = task.range.begin; partition < task.range.end; ++partition)
                {
                    const std::uint64_t firstStrip = partition * stripCount / bandSlots;
                    const std::uint64_t lastStrip = (partition + 1u) * stripCount / bandSlots;
                    for (std::uint64_t strip = firstStrip; strip < lastStrip; ++strip)
                    {
                        const auto firstRow = static_cast<std::uint32_t>(strip * stripRows);
                        const auto endRow = static_cast<std::uint32_t>(
                            (std::min)(static_cast<std::uint64_t>(destinationHeight), (strip + 1u) * stripRows));
                        // The source rows this strip reads, and whether any tap reads the constant border.
                        std::int64_t minIndex = (std::numeric_limits<std::int64_t>::max)();
                        std::int64_t maxIndex = -1;
                        bool readsConstant = false;
                        for (std::uint32_t y = firstRow; y < endRow; ++y)
                        {
                            if (areaResize)
                            {
                                minIndex = (std::min)(minIndex, tables.areaYAxes[y].begin);
                                maxIndex = (std::max)(maxIndex, tables.areaYAxes[y].end - 1);
                                continue;
                            }
                            const ResizeAxisEntry& yAxis = tables.yAxes[y];
                            for (std::uint32_t tap = 0u; tap < yAxis.count; ++tap)
                            {
                                const std::int64_t index = yAxis.indices[tap];
                                if (index == sampling.height)
                                {
                                    readsConstant = true;
                                    continue;
                                }
                                minIndex = (std::min)(minIndex, index);
                                maxIndex = (std::max)(maxIndex, index);
                            }
                        }
                        const std::uint64_t bandRows =
                            maxIndex >= minIndex ? static_cast<std::uint64_t>(maxIndex - minIndex + 1) : 0u;
                        if (bandRows > bandRowsMax)
                        {
                            return geometryFailure(
                                ErrorCode::Internal, kTransformOperation,
                                "a separable resize strip reads more source rows than its band holds");
                        }
                        for (std::uint64_t bandRow = 0u; bandRow < bandRows; ++bandRow)
                        {
                            const std::int64_t sourceRow = minIndex + static_cast<std::int64_t>(bandRow);
                            if (sourceRow >= cachedMin && sourceRow <= cachedMax)
                            {
                                continue;
                            }
                            double* const target = band + static_cast<std::uint64_t>(sourceRow) % bandRowsMax * width;
                            if (areaResize)
                            {
                                areaColumnPass(sampling, tables.areaXAxes, width, sourceRow, target);
                            }
                            else
                            {
                                if (packedColumns.allInBounds)
                                {
                                    resizePackedColumnPass(sampling, packedColumns, sourceRow, target);
                                }
                                else
                                {
                                    resizeColumnPass(sampling, tables.xAxes, width, sourceRow, target);
                                }
                            }
                        }
                        cachedMin = minIndex;
                        cachedMax = maxIndex;
                        if (readsConstant)
                        {
                            resizeColumnPass(sampling, tables.xAxes, width, sampling.height, constantBand);
                        }
                        // The guide's own band: the source rows the strip's footprints or linear taps
                        // read, under the rule the guide samples with.
                        std::int64_t guideMin = (std::numeric_limits<std::int64_t>::max)();
                        std::int64_t guideMax = -1;
                        bool guideReadsConstant = false;
                        if (suppressDarkRinging)
                        {
                            for (std::uint32_t y = firstRow; y < endRow; ++y)
                            {
                                if (downsamplingResize)
                                {
                                    guideMin = (std::min)(guideMin, tables.areaYAxes[y].begin);
                                    guideMax = (std::max)(guideMax, tables.areaYAxes[y].end - 1);
                                    continue;
                                }
                                const ResizeAxisEntry& guideAxis = tables.guideYAxes[y];
                                for (std::uint32_t tap = 0u; tap < guideAxis.count; ++tap)
                                {
                                    const std::int64_t index = guideAxis.indices[tap];
                                    if (index == sampling.height)
                                    {
                                        guideReadsConstant = true;
                                        continue;
                                    }
                                    guideMin = (std::min)(guideMin, index);
                                    guideMax = (std::max)(guideMax, index);
                                }
                            }
                            const std::uint64_t guideRows =
                                guideMax >= guideMin ? static_cast<std::uint64_t>(guideMax - guideMin + 1) : 0u;
                            if (guideRows > bandRowsMax)
                            {
                                return geometryFailure(ErrorCode::Internal, kTransformOperation,
                                                       "a separable resize strip's guide reads more source rows than "
                                                       "its band holds");
                            }
                            for (std::uint64_t bandRow = 0u; bandRow < guideRows; ++bandRow)
                            {
                                const std::int64_t sourceRow = guideMin + static_cast<std::int64_t>(bandRow);
                                double* const target = guideBand + bandRow * width;
                                if (downsamplingResize)
                                {
                                    areaColumnPass(sampling, tables.areaXAxes, width, sourceRow, target);
                                }
                                else
                                {
                                    resizeColumnPass(sampling, tables.guideXAxes, width, sourceRow, target);
                                }
                            }
                            if (guideReadsConstant)
                            {
                                resizeColumnPass(sampling, tables.guideXAxes, width, sampling.height,
                                                 guideConstantBand);
                            }
                        }
                        for (std::uint32_t y = firstRow; y < endRow; ++y)
                        {
                            float* const row = destinationPlane + static_cast<std::uint64_t>(y) * destinationRowStride;
                            std::uint8_t* const rowState =
                                needsState ? state + static_cast<std::uint64_t>(y) * width : nullptr;
                            // Tap by tap across the whole row, so each output still sums its taps in
                            // order while the row walks as one run per tap.
                            std::fill_n(sums, static_cast<std::size_t>(width), 0.0);
                            if (areaResize)
                            {
                                const AreaAxisEntry& areaAxis = tables.areaYAxes[y];
                                for (std::int64_t cell = areaAxis.begin; cell < areaAxis.end; ++cell)
                                {
                                    const double yWeight = (std::min)(areaAxis.high, static_cast<double>(cell + 1)) -
                                                           (std::max)(areaAxis.low, static_cast<double>(cell));
                                    if (yWeight <= 0.0)
                                    {
                                        continue;
                                    }
                                    const double* const bandRow =
                                        band + static_cast<std::uint64_t>(cell) % bandRowsMax * width;
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        sums[x] += yWeight * bandRow[x];
                                    }
                                }
                                // An area reduction always divides by the footprint it covered, which
                                // the guide sums hold for a reduction, and it never reads a constant.
                                for (std::uint32_t x = 0u; x < width; ++x)
                                {
                                    const double accepted = guideRowWeight[y] * guideColumnWeight[x];
                                    if (accepted <= 0.0)
                                    {
                                        if (rowState != nullptr)
                                        {
                                            rowState[x] = 0u;
                                        }
                                        continue;
                                    }
                                    row[x] = static_cast<float>(sums[x] / accepted);
                                    if (rowState != nullptr)
                                    {
                                        rowState[x] = 1u;
                                    }
                                }
                            }
                            else
                            {
                                const ResizeAxisEntry& yAxis = tables.yAxes[y];
                                for (std::uint32_t tap = 0u; tap < yAxis.count; ++tap)
                                {
                                    const double weight = yAxis.weights[tap];
                                    if (weight == 0.0)
                                    {
                                        continue;
                                    }
                                    const std::int64_t index = yAxis.indices[tap];
                                    const double* const bandRow =
                                        index == sampling.height
                                            ? constantBand
                                            : band + static_cast<std::uint64_t>(index) % bandRowsMax * width;
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        sums[x] += weight * bandRow[x];
                                    }
                                }
                                if (renormalize)
                                {
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        const double accepted = rowWeight[y] * columnWeight[x];
                                        if (std::abs(accepted) > std::numeric_limits<double>::epsilon())
                                        {
                                            row[x] = static_cast<float>(sums[x] / accepted);
                                            if (rowState != nullptr)
                                            {
                                                rowState[x] = 1u;
                                            }
                                            continue;
                                        }
                                        if (rowState != nullptr)
                                        {
                                            rowState[x] = 0u;
                                        }
                                    }
                                }
                                else
                                {
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        row[x] = static_cast<float>(sums[x]);
                                        if (rowState != nullptr)
                                        {
                                            rowState[x] = 1u;
                                        }
                                    }
                                }
                            }
                            if (!suppressDarkRinging)
                            {
                                if (rowState == nullptr)
                                {
                                    localValid += width;
                                }
                                else
                                {
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        localValid += rowState[x] != 0u ? 1u : 0u;
                                    }
                                }
                                continue;
                            }
                            // The guide for the row, separably, then the comparison every valid output
                            // makes against it.
                            std::fill_n(guideSums, static_cast<std::size_t>(width), 0.0);
                            if (downsamplingResize)
                            {
                                const AreaAxisEntry& guideAxis = tables.areaYAxes[y];
                                for (std::int64_t cell = guideAxis.begin; cell < guideAxis.end; ++cell)
                                {
                                    const double yWeight = (std::min)(guideAxis.high, static_cast<double>(cell + 1)) -
                                                           (std::max)(guideAxis.low, static_cast<double>(cell));
                                    if (yWeight <= 0.0)
                                    {
                                        continue;
                                    }
                                    const double* const bandRow =
                                        guideBand + static_cast<std::uint64_t>(cell - guideMin) * width;
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        guideSums[x] += yWeight * bandRow[x];
                                    }
                                }
                            }
                            else
                            {
                                const ResizeAxisEntry& guideAxis = tables.guideYAxes[y];
                                for (std::uint32_t tap = 0u; tap < guideAxis.count; ++tap)
                                {
                                    const double weight = guideAxis.weights[tap];
                                    if (weight == 0.0)
                                    {
                                        continue;
                                    }
                                    const std::int64_t index = guideAxis.indices[tap];
                                    const double* const bandRow =
                                        index == sampling.height
                                            ? guideConstantBand
                                            : guideBand + static_cast<std::uint64_t>(index - guideMin) * width;
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        guideSums[x] += weight * bandRow[x];
                                    }
                                }
                            }
                            for (std::uint32_t x = 0u; x < width; ++x)
                            {
                                if (rowState[x] == 0u)
                                {
                                    continue;
                                }
                                ++localValid;
                                const double accepted = guideRowWeight[y] * guideColumnWeight[x];
                                bool guideValid = false;
                                float guide = 0.0f;
                                if (downsamplingResize)
                                {
                                    guideValid = accepted > 0.0;
                                    guide = static_cast<float>(guideSums[x] / accepted);
                                }
                                else
                                {
                                    guideValid = std::abs(accepted) > std::numeric_limits<double>::epsilon();
                                    guide =
                                        static_cast<float>(plan.sampling().invalid == SamplingInvalidPolicy::Renormalize
                                                               ? guideSums[x] / accepted
                                                               : guideSums[x]);
                                }
                                if (guideValid && row[x] < 0.98f * guide)
                                {
                                    rowState[x] = 2u;
                                    ++localGuarded;
                                }
                            }
                        }
                    }
                }
                channelValidCount.fetch_add(localValid, std::memory_order_relaxed);
                guardedCount.fetch_add(localGuarded, std::memory_order_relaxed);
                return Status::success();
            });
        }
        else
        {
            status = parallelFor(execution, destinationHeight, rowCost, [&](TaskRange range) {
                std::uint64_t localValid = 0u;
                std::uint64_t localGuarded = 0u;
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const auto y = static_cast<std::uint32_t>(rowIndex);
                    float* const row = destinationPlane + static_cast<std::uint64_t>(y) * destinationRowStride;
                    std::uint8_t* const rowState = state + static_cast<std::uint64_t>(y) * destinationWidth;
                    if (areaResize)
                    {
                        for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                        {
                            float value = 0.0f;
                            const bool valid =
                                sampleAreaTabulated(sampling, tables.areaXAxes[x], tables.areaYAxes[y], value);
                            if (valid)
                            {
                                row[x] = value;
                            }
                            rowState[x] = valid ? 1u : 0u;
                        }
                    }
                    else
                    {
                        for (std::uint32_t x = 0u; x < destinationWidth; x += kResizeBlock)
                        {
                            const std::uint32_t count = (std::min)(kResizeBlock, destinationWidth - x);
                            sampleTabulatedBlock(sampling, tables.xAxes + x, tables.yAxes[y], count, row + x,
                                                 rowState + x);
                        }
                    }
                    finishRow(y, row, rowState, localValid, localGuarded);
                }
                channelValidCount.fetch_add(localValid, std::memory_order_relaxed);
                guardedCount.fetch_add(localGuarded, std::memory_order_relaxed);
                return Status::success();
            });
        }
        if (status.failed())
        {
            return status;
        }
        if (suppressDarkRinging && guardedCount.load(std::memory_order_relaxed) != 0u)
        {
            status =
                parallelFor(execution, destinationHeight, geometryRowCost(destinationWidth, 12u), [&](TaskRange range) {
                    for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                    {
                        const auto y = static_cast<std::uint32_t>(rowIndex);
                        float* const row = destinationPlane + static_cast<std::uint64_t>(y) * destinationRowStride;
                        for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                        {
                            const std::size_t index = static_cast<std::size_t>(y) * destinationWidth + x;
                            if (state[index] == 0u)
                            {
                                continue;
                            }
                            const bool guarded = state[index] == 2u || (x != 0u && state[index - 1u] == 2u) ||
                                                 (x + 1u != destinationWidth && state[index + 1u] == 2u) ||
                                                 (y != 0u && state[index - destinationWidth] == 2u) ||
                                                 (y + 1u != destinationHeight && state[index + destinationWidth] == 2u);
                            if (!guarded)
                            {
                                continue;
                            }
                            float guide = 0.0f;
                            if (sampleGuide(x, y, guide))
                            {
                                row[x] = guide;
                            }
                        }
                    }
                    return Status::success();
                });
            if (status.failed())
            {
                return status;
            }
        }
        // A path that can suppress or omit an output carries state and translates it to the mask.
        // The ordinary separable path cannot omit one and states its whole mask after all planes.
        if (needsState)
        {
            const std::uint64_t channelValid = channelValidCount.load(std::memory_order_relaxed);
            const std::uint32_t maskPlane = destinationValidity.planeCount() == 1u ? 0u : channel;
            if (channel == 0u || destinationValidity.planeCount() != 1u)
            {
                for (std::uint32_t y = 0u; y < destinationHeight; ++y)
                {
                    const std::optional<std::span<std::uint8_t>> bits = destinationValidity.row(maskPlane, y);
                    if (!bits.has_value())
                    {
                        continue;
                    }
                    std::fill(bits->begin(), bits->end(), std::uint8_t{0xFFu});
                    bits->back() = static_cast<std::uint8_t>(bits->back() & packedMaskLastByteMask(destinationWidth));
                }
            }
            if (channelValid != destinationPixels)
            {
                for (std::uint32_t y = 0u; y < destinationHeight; ++y)
                {
                    const std::uint8_t* const rowState = state + static_cast<std::uint64_t>(y) * destinationWidth;
                    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                    {
                        if (rowState[x] == 0u)
                        {
                            stateDestinationValidity(destinationValidity, channel, x, y, false);
                        }
                    }
                }
            }
        }
        validCount += channelValidCount.load(std::memory_order_relaxed);
    }
    if (!needsState)
    {
        destinationValidity.fill(true);
    }
    const std::uint64_t valid = validCount;
    result.run.selection.total = destinationPixels * channels;
    result.run.selection.valid = valid;
    result.run.outputsWritten = valid;
    result.channelCount = channels;
    result.plannedWorkers = separable ? bandSlots : plannedWorkerCount(execution, destinationHeight, rowCost);
    result.workingBytes = memory->bytes() + bandMemory->bytes();
    result.exactStoredValues = false;
    return Status::success();
}

/// Whether every destination center of this plan has a finite source coordinate.
///
/// The projective denominator is affine in the destination position, so over a rectangle it is
/// monotone along each axis and takes both its extremes at the corners: one strict sign at all four
/// is one strict sign everywhere, and the smallest magnitude there is the smallest magnitude
/// everywhere. The numerators are affine too, so their largest magnitudes are corner values as well,
/// and a finite ratio of those bounds makes every quotient in between finite.
///
/// That is what lets the warp below compute a coordinate where it uses it rather than storing every
/// one first: this answers, in four evaluations, the whole question the stored pass answers in one
/// evaluation per pixel, so validation of all projective coordinates still completes before the
/// first sample write.
[[nodiscard]] bool warpCoordinatesAreFinite(const std::array<double, 9>& matrix, std::uint32_t destinationWidth,
                                            std::uint32_t destinationHeight) noexcept
{
    const double lastX = static_cast<double>(destinationWidth - 1u);
    const double lastY = static_cast<double>(destinationHeight - 1u);
    const std::array<double, 4u> cornerX{0.0, lastX, 0.0, lastX};
    const std::array<double, 4u> cornerY{0.0, 0.0, lastY, lastY};
    double smallestDenominator = std::numeric_limits<double>::infinity();
    double largestNumerator = 0.0;
    bool positive = false;
    for (std::uint32_t corner = 0u; corner < 4u; ++corner)
    {
        const double denominator = matrix[6] * cornerX[corner] + matrix[7] * cornerY[corner] + matrix[8];
        if (!std::isfinite(denominator) || denominator == 0.0)
        {
            return false;
        }
        if (corner == 0u)
        {
            positive = denominator > 0.0;
        }
        else if ((denominator > 0.0) != positive)
        {
            return false;
        }
        const double numeratorX = matrix[0] * cornerX[corner] + matrix[1] * cornerY[corner] + matrix[2];
        const double numeratorY = matrix[3] * cornerX[corner] + matrix[4] * cornerY[corner] + matrix[5];
        if (!std::isfinite(numeratorX) || !std::isfinite(numeratorY))
        {
            return false;
        }
        smallestDenominator = (std::min)(smallestDenominator, std::abs(denominator));
        largestNumerator = (std::max)(largestNumerator, (std::max)(std::abs(numeratorX), std::abs(numeratorY)));
    }
    return std::isfinite(largestNumerator / smallestDenominator);
}

/// States a run of written samples in one row of an output mask.
///
/// The mask reaches a geometry operation cleared, so a written sample is a bit to set and an
/// unwritten one is already what it should be. That leaves one case to state separately: a shared
/// mask plane under a second or later channel keeps only what every channel wrote, so there the
/// answer is an intersection and only the unwritten samples have anything to say.
void setValidityRun(std::span<std::uint8_t> row, std::uint32_t first, std::uint32_t count) noexcept
{
    std::uint32_t x = first;
    const std::uint32_t last = first + count;
    while (x < last && (x % 8u) != 0u)
    {
        row[x / 8u] = static_cast<std::uint8_t>(row[x / 8u] | (1u << (x % 8u)));
        ++x;
    }
    while (x + 8u <= last)
    {
        row[x / 8u] = 0xFFu;
        x += 8u;
    }
    while (x < last)
    {
        row[x / 8u] = static_cast<std::uint8_t>(row[x / 8u] | (1u << (x % 8u)));
        ++x;
    }
}

/// Clears one sample's bit in one row of an output mask.
void clearValidity(std::span<std::uint8_t> row, std::uint32_t x) noexcept
{
    row[x / 8u] = static_cast<std::uint8_t>(row[x / 8u] & ~(1u << (x % 8u)));
}

/// Carries one interpolation into a loop body as a compile-time constant.
template <KernelInterpolation InterpolationT> struct SamplingRule
{
};

/// Executes a rotation or a warp over Float32 planes without storing a coordinate per pixel.
///
/// The generic path below carries three whole-image buffers - one coordinate per destination pixel,
/// one gathered copy of the source channel and one of the result - because it has to serve every
/// representation, every validity rule and an interleaved pixel. None of that is needed when the
/// samples are already Float32 planes, one per channel, and the source admits every value: the
/// coordinates are a ratio of two affine functions of the destination position - an affine one
/// outright for a rotation or an affine warp - that costs less to evaluate than to store and
/// re-read, and both planes can be read and written where they are. The caller has already asked
/// `warpCoordinatesAreFinite` whether every coordinate is finite.
///
/// What the samples themselves are is not changed by any of it. The coordinate expression is the
/// stored pass's expression with the row terms hoisted out of a sum that associates left to right,
/// the taps come from `samplingTaps`, which is what `samplingAxis` is written in terms of, and the
/// accumulation below is `sampleInterpolated`'s accumulation with the border and validity questions
/// answered once per window instead of once per tap. A destination sample is therefore the same
/// sample, bit for bit, as the general path would have written - except where a cubic or Lanczos-4
/// warp that does not renormalize runs through the compiled row the execution's CPU table offers,
/// which sums each window one source row at a time - and resolves a Lanczos-4 window's weights from
/// four quarter-angle sines - and is held to the kernel conformance bound instead.
[[nodiscard]] Status runDirectFloatWarp(const ExecutionContext& execution, const void* source,
                                        const ImageLayout& sourceLayout, void* destination,
                                        const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                        const ValidityMaskView<std::uint8_t>& destinationValidity,
                                        GeometryResult& result)
{
    const std::uint32_t sourceWidth = sourceLayout.extent().width();
    const std::uint32_t sourceHeight = sourceLayout.extent().height();
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const std::array<double, 9>& matrix = plan.destinationToSource();
    const SamplingKernelParameters& sampling = plan.sampling();
    const KernelInterpolation interpolation = sampling.interpolation;
    const std::uint32_t window = reference_detail::samplingTapCount(interpolation);
    const std::int64_t lastSourceColumn = static_cast<std::int64_t>(sourceWidth) - static_cast<std::int64_t>(window);
    const std::int64_t lastSourceRow = static_cast<std::int64_t>(sourceHeight) - static_cast<std::int64_t>(window);
    const bool renormalize = sampling.invalid == SamplingInvalidPolicy::Renormalize;
    const KernelValidityView allValid{};
    std::atomic<std::uint64_t> validCount{0u};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const auto [sourcePlaneIndex, sourceWithin] = channelAddress(sourceLayout, channel);
        const auto [destinationPlaneIndex, destinationWithin] = channelAddress(destinationLayout, channel);
        const std::optional<std::uint64_t> sourceOrigin = sourceLayout.sampleByteOffset(sourcePlaneIndex, 0u, 0u, 0u);
        const std::optional<std::uint64_t> destinationOrigin =
            destinationLayout.sampleByteOffset(destinationPlaneIndex, 0u, 0u, 0u);
        if (!sourceOrigin.has_value() || !destinationOrigin.has_value() || sourceWithin != 0u ||
            destinationWithin != 0u)
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "a warp plane does not begin where its layout says");
        }
        const auto* const sourcePlane =
            reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(source) + *sourceOrigin);
        auto* const destinationPlane =
            reinterpret_cast<float*>(static_cast<std::uint8_t*>(destination) + *destinationOrigin);
        const std::uint64_t sourceRowStride = sourceLayout.rowStrideBytes() / sizeof(float);
        const std::uint64_t destinationRowStride = destinationLayout.rowStrideBytes() / sizeof(float);
        const std::uint32_t maskPlane = destinationValidity.planeCount() == 1u ? 0u : channel;
        const bool combineValidity = destinationValidity.planeCount() == 1u && channel != 0u;
        if (destinationValidity.width() != destinationWidth || destinationValidity.height() != destinationHeight ||
            maskPlane >= destinationValidity.planeCount())
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "the output mask does not cover the destination");
        }
        // The compiled rows serve the cubic and Lanczos-4 interiors and wholly exterior constant-zero
        // windows; partially overlapping windows, a whole-sample Lanczos-4 coordinate and other border
        // rules use the reader below.
        const WarpRowParameters rowParameters{.source = sourcePlane,
                                              .destinationToSource = matrix.data(),
                                              .sourceRowStride = sourceLayout.rowStrideBytes() / sizeof(float),
                                              .sourceWidth = static_cast<std::int64_t>(sourceWidth),
                                              .sourceHeight = static_cast<std::int64_t>(sourceHeight),
                                              .constantZeroBorder = sampling.border == KernelBorderMode::Constant &&
                                                                    sampling.constantBorder == 0.0f};
        WarpCubicRowRunFn compiledRow = nullptr;
        if (sampling.invalid != SamplingInvalidPolicy::Renormalize)
        {
            if (sampling.interpolation == KernelInterpolation::Cubic)
            {
                compiledRow = warpCubicRowFor(execution);
            }
            else if (sampling.interpolation == KernelInterpolation::Lanczos4)
            {
                compiledRow = warpLanczos4RowFor(execution);
            }
        }
        const reference_detail::SamplingPlan borderPlan{.samples = sourcePlane,
                                                        .rowStride = sourceRowStride,
                                                        .width = static_cast<std::int64_t>(sourceWidth),
                                                        .height = static_cast<std::int64_t>(sourceHeight),
                                                        .interpolation = sampling.interpolation,
                                                        .border = sampling.border,
                                                        .invalid = sampling.invalid,
                                                        .constantBorder = sampling.constantBorder,
                                                        .invalidSample = sampling.invalidSample,
                                                        .validity = &allValid,
                                                        .acceptsEverySample = true,
                                                        .pixelStride = 1u};
        std::atomic<std::uint64_t> channelValid{0u};
        // The tap count decides how many multiplications a sample is, so it is a template argument
        // rather than a loop bound: the window loops below unroll into straight-line arithmetic over
        // weights the compiler can keep in registers, which is most of the difference between this
        // and the general reader it agrees with.
        const auto runRows = [&]<KernelInterpolation InterpolationT, bool CompiledT>(SamplingRule<InterpolationT>,
                                                                                     std::bool_constant<CompiledT>) {
            constexpr std::uint32_t kTaps = reference_detail::samplingTapCount(InterpolationT);
            return parallelFor(
                execution, destinationHeight, geometryRowCost(destinationWidth, 24u), [&](TaskRange range) {
                    // Copied out of the plan rather than read through it. The loop below stores through
                    // a byte pointer to state validity, and a byte store may alias anything, so a
                    // coefficient still living behind a reference would be re-read from memory at every
                    // pixel of the destination.
                    const double m0 = matrix[0];
                    const double m1 = matrix[1];
                    const double m2 = matrix[2];
                    const double m3 = matrix[3];
                    const double m4 = matrix[4];
                    const double m5 = matrix[5];
                    const double m6 = matrix[6];
                    const double m7 = matrix[7];
                    const double m8 = matrix[8];
                    const float* const sourceSamples = sourcePlane;
                    const std::uint64_t sourceStride = sourceRowStride;
                    const std::uint32_t width = destinationWidth;
                    const std::int64_t lastColumn = lastSourceColumn;
                    const std::int64_t lastRow = lastSourceRow;
                    const WarpCubicRowRunFn row32 = compiledRow;
                    const WarpRowParameters rowRun = rowParameters;
                    const bool combine = combineValidity;
                    const bool renormalizes = renormalize;
                    for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                    {
                        const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                        const double rowNumeratorX = m1 * static_cast<double>(y);
                        const double rowNumeratorY = m4 * static_cast<double>(y);
                        const double rowDenominator = m7 * static_cast<double>(y);
                        float* const row = destinationPlane + rowIndex * destinationRowStride;
                        // One row of the mask, taken once: this states one bit per pixel, and asking the
                        // view for each of them separately pays for two bounds tests and a division on
                        // every one. Rows do not share bytes, so the thread holding this row holds these
                        // bytes alone.
                        const std::optional<std::span<std::uint8_t>> maskRow = destinationValidity.row(maskPlane, y);
                        std::uint8_t* const maskBytes = maskRow->data();
                        std::uint64_t localValid = 0u;
                        std::array<double, kTaps> xWeights{};
                        std::array<double, kTaps> yWeights{};
                        // The written samples of one byte of the mask, held until the walk leaves
                        // that byte. A store through a byte pointer is a store that may alias
                        // anything, so doing one per eight samples rather than one per sample is
                        // what keeps the coefficients above in registers.
                        std::uint8_t pendingBits = 0u;
                        std::uint32_t pendingByte = 0u;
                        const auto flushValidity = [&]() {
                            if (pendingBits != 0u)
                            {
                                maskBytes[pendingByte] =
                                    static_cast<std::uint8_t>(maskBytes[pendingByte] | pendingBits);
                                pendingBits = 0u;
                            }
                        };
                        for (std::uint32_t x = 0u; x < width;)
                        {
                            if constexpr (CompiledT)
                            {
                                // The compiled row writes a prefix of samples whose window it can
                                // reach and stops at the first it cannot, which is the same
                                // question the reader below asks - so where it stops, the reader
                                // takes one sample and the row is offered the rest. Whether there
                                // is such a row at all is decided once, outside every loop, because
                                // a test here would be a reload and a branch on every sample of
                                // every interpolation - including the ones that never have one.
                                const std::uint32_t covered = row32(rowRun, y, x, width, row);
                                if (covered != 0u)
                                {
                                    if (!combine)
                                    {
                                        flushValidity();
                                        setValidityRun(*maskRow, x, covered);
                                    }
                                    localValid += covered;
                                    x += covered;
                                    continue;
                                }
                            }
                            const double column = static_cast<double>(x);
                            const double denominator = m6 * column + rowDenominator + m8;
                            const double sourceX = (m0 * column + rowNumeratorX + m2) / denominator;
                            const double sourceY = (m3 * column + rowNumeratorY + m5) / denominator;
                            const std::int64_t firstColumn =
                                reference_detail::samplingTaps<InterpolationT>(sourceX, xWeights.data());
                            const std::int64_t firstRow =
                                reference_detail::samplingTaps<InterpolationT>(sourceY, yWeights.data());
                            float value = 0.0f;
                            bool valid = false;
                            if (firstColumn >= 0 && firstRow >= 0 && firstColumn <= lastColumn && firstRow <= lastRow)
                            {
                                // Every tap of this window is inside the source and the source admits
                                // every value, so the two questions the general reader asks per tap -
                                // which index a border rule resolves it to, and whether the validity
                                // rule accepts it - have one answer for the whole window.
                                double weighted = 0.0;
                                double acceptedWeight = 0.0;
                                const float* line = sourceSamples +
                                                    static_cast<std::uint64_t>(firstRow) * sourceStride +
                                                    static_cast<std::uint64_t>(firstColumn);
                                for (std::uint32_t yTap = 0u; yTap < kTaps; ++yTap)
                                {
                                    for (std::uint32_t xTap = 0u; xTap < kTaps; ++xTap)
                                    {
                                        const double weight = xWeights[xTap] * yWeights[yTap];
                                        if (weight == 0.0)
                                        {
                                            // Skipped rather than added, for the reason the general
                                            // reader gives: a zero weight against a non-finite sample
                                            // is a not-a-number.
                                            continue;
                                        }
                                        weighted += weight * static_cast<double>(line[xTap]);
                                        acceptedWeight += weight;
                                    }
                                    line += sourceStride;
                                }
                                if (std::abs(acceptedWeight) > std::numeric_limits<double>::epsilon())
                                {
                                    value = static_cast<float>(renormalizes ? weighted / acceptedWeight : weighted);
                                    valid = true;
                                }
                            }
                            else
                            {
                                valid = reference_detail::sampleInterpolated(
                                    borderPlan, KernelCoordinate{.x = sourceX, .y = sourceY}, value);
                            }
                            if (valid)
                            {
                                row[x] = value;
                                if (!combine)
                                {
                                    const std::uint32_t byte = x / 8u;
                                    if (byte != pendingByte)
                                    {
                                        flushValidity();
                                        pendingByte = byte;
                                    }
                                    pendingBits = static_cast<std::uint8_t>(pendingBits | (1u << (x % 8u)));
                                }
                                ++localValid;
                            }
                            else if (combine)
                            {
                                clearValidity(*maskRow, x);
                            }
                            ++x;
                        }
                        flushValidity();
                        channelValid.fetch_add(localValid, std::memory_order_relaxed);
                    }
                    return Status::success();
                });
        };
        Status status = Status::success();
        switch (interpolation)
        {
        case KernelInterpolation::Nearest:
        case KernelInterpolation::NearestExact:
            status = runRows(SamplingRule<KernelInterpolation::Nearest>{}, std::false_type{});
            break;
        case KernelInterpolation::Cubic:
            status = compiledRow != nullptr ? runRows(SamplingRule<KernelInterpolation::Cubic>{}, std::true_type{})
                                            : runRows(SamplingRule<KernelInterpolation::Cubic>{}, std::false_type{});
            break;
        case KernelInterpolation::Lanczos4:
            status = compiledRow != nullptr ? runRows(SamplingRule<KernelInterpolation::Lanczos4>{}, std::true_type{})
                                            : runRows(SamplingRule<KernelInterpolation::Lanczos4>{}, std::false_type{});
            break;
        default:
            status = runRows(SamplingRule<KernelInterpolation::Linear>{}, std::false_type{});
            break;
        }
        if (status.failed())
        {
            return status;
        }
        validCount.fetch_add(channelValid.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    const std::uint64_t valid = validCount.load(std::memory_order_relaxed);
    result.run.selection.total = static_cast<std::uint64_t>(destinationWidth) * destinationHeight * channels;
    result.run.selection.valid = valid;
    result.run.outputsWritten = valid;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, destinationHeight, geometryRowCost(destinationWidth, 24u));
    result.workingBytes = 0u;
    result.exactStoredValues = false;
    return Status::success();
}

/// Crops, or resamples by an integer factor, one sample at a time in any layout and under any
/// validity rule.
///
/// A crop or an enlargement copies each output's one source sample, and leaves an output whose
/// source sample the rule refuses unwritten and invalid. A reduction combines the admitted samples of
/// each factor-by-factor block - of one filter's lattice, for a mosaic - in Float64 and converts the
/// answer back through the default code-value conversion; a block with nothing admitted leaves its
/// output unwritten and invalid. A median holds every block's samples at once, so its working memory
/// is the destination's sample count times the block's, as Float64.
template <typename SampleT>
[[nodiscard]] Status runExact(const ExecutionContext& execution, const void* source, const ImageLayout& sourceLayout,
                              void* destination, const ImageLayout& destinationLayout, const GeometryPlan& plan,
                              const GeometrySelection& selection,
                              const ValidityMaskView<std::uint8_t>& destinationValidity, GeometryResult& result)
{
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    const IntegerResampleRequest& integer = plan.integerRequest();
    const std::uint32_t factor = integer.factor;
    const std::uint64_t blockSampleCount = static_cast<std::uint64_t>(factor) * factor;
    const std::uint64_t destinationSampleCount = static_cast<std::uint64_t>(destinationWidth) * destinationHeight;
    const bool needsMedianStorage = plan.operation() != GeometryOperation::Crop &&
                                    integer.direction == IntegerResampleDirection::Reduce &&
                                    integer.reduction == IntegerReduction::Median;
    const std::uint64_t medianSampleCount = needsMedianStorage ? destinationSampleCount * blockSampleCount : 0u;
    if (medianSampleCount > (std::numeric_limits<std::uint64_t>::max)() / sizeof(double))
    {
        return geometryFailure(ErrorCode::LimitExceeded, kTransformOperation,
                               "the integer median working set is not representable");
    }
    Result<plane_detail::ScopedContextMemory> medianMemory = plane_detail::ScopedContextMemory::take(
        execution, medianSampleCount * sizeof(double), kDefaultBufferAlignmentBytes, kTransformOperation);
    if (!medianMemory.hasValue())
    {
        return medianMemory.status();
    }
    auto* const medianValues = static_cast<double*>(medianMemory->data());
    const std::uint64_t reductionCost = blockSampleCount > (std::numeric_limits<std::uint64_t>::max)() / 4u
                                            ? (std::numeric_limits<std::uint64_t>::max)()
                                            : blockSampleCount * 4u;
    std::atomic<std::uint64_t> validCount{0u};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const Status ran = parallelFor(
            execution, destinationHeight,
            geometryRowCost(destinationWidth, plan.operation() == GeometryOperation::Crop ? 4u : reductionCost),
            [&](TaskRange range) {
                std::uint64_t localValid = 0u;
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                    {
                        if (plan.operation() == GeometryOperation::Crop)
                        {
                            const std::uint32_t sx = x + plan.cropX();
                            const std::uint32_t sy = y + plan.cropY();
                            const SampleT value = sampleAt<SampleT>(source, sourceLayout, channel, sx, sy);
                            const bool valid = selected(selection, channel, sx, sy, static_cast<double>(value));
                            stateDestinationValidity(destinationValidity, channel, x, y, valid);
                            if (valid)
                            {
                                sampleAt<SampleT>(destination, destinationLayout, channel, x, y) = value;
                                ++localValid;
                            }
                            continue;
                        }

                        const bool cfa = plan.operation() == GeometryOperation::CfaIntegerResample;
                        if (integer.direction == IntegerResampleDirection::Enlarge)
                        {
                            const std::uint32_t sx = cfa ? 2u * ((x / 2u) / factor) + x % 2u : x / factor;
                            const std::uint32_t sy = cfa ? 2u * ((y / 2u) / factor) + y % 2u : y / factor;
                            const SampleT value = sampleAt<SampleT>(source, sourceLayout, channel, sx, sy);
                            const bool valid = selected(selection, channel, sx, sy, static_cast<double>(value));
                            stateDestinationValidity(destinationValidity, channel, x, y, valid);
                            if (valid)
                            {
                                sampleAt<SampleT>(destination, destinationLayout, channel, x, y) = value;
                                ++localValid;
                            }
                            continue;
                        }

                        const std::uint64_t outputIndex = static_cast<std::uint64_t>(y) * destinationWidth + x;
                        double* const values =
                            needsMedianStorage ? medianValues + outputIndex * blockSampleCount : nullptr;
                        std::uint64_t count = 0u;
                        double sum = 0.0;
                        double minimum = (std::numeric_limits<double>::max)();
                        double maximum = -(std::numeric_limits<double>::max)();
                        for (std::uint32_t by = 0u; by < factor; ++by)
                        {
                            for (std::uint32_t bx = 0u; bx < factor; ++bx)
                            {
                                const std::uint32_t sx = cfa ? 2u * ((x / 2u) * factor + bx) + x % 2u : x * factor + bx;
                                const std::uint32_t sy = cfa ? 2u * ((y / 2u) * factor + by) + y % 2u : y * factor + by;
                                const SampleT value = sampleAt<SampleT>(source, sourceLayout, channel, sx, sy);
                                if (selected(selection, channel, sx, sy, static_cast<double>(value)))
                                {
                                    const double widened = static_cast<double>(value);
                                    if (needsMedianStorage)
                                    {
                                        values[count] = widened;
                                    }
                                    sum += widened;
                                    minimum = (std::min)(minimum, widened);
                                    maximum = (std::max)(maximum, widened);
                                    ++count;
                                }
                            }
                        }
                        const bool valid = count != 0u;
                        stateDestinationValidity(destinationValidity, channel, x, y, valid);
                        if (!valid)
                        {
                            continue;
                        }
                        double answer = sum;
                        switch (integer.reduction)
                        {
                        case IntegerReduction::Average:
                            answer /= static_cast<double>(count);
                            break;
                        case IntegerReduction::Median:
                            std::sort(values, values + count);
                            answer = count % 2u != 0u ? values[count / 2u]
                                                      : 0.5 * (values[count / 2u - 1u] + values[count / 2u]);
                            break;
                        case IntegerReduction::Maximum:
                            answer = maximum;
                            break;
                        case IntegerReduction::Minimum:
                            answer = minimum;
                            break;
                        }
                        sampleAt<SampleT>(destination, destinationLayout, channel, x, y) =
                            convertSample<double, SampleT>(answer, ConversionPolicy{.domain = DomainMapping::CodeValue})
                                .value;
                        ++localValid;
                    }
                }
                validCount.fetch_add(localValid, std::memory_order_relaxed);
                return Status::success();
            });
        if (ran.failed())
        {
            return ran;
        }
    }
    result.run.selection.total = static_cast<std::uint64_t>(destinationWidth) * destinationHeight * channels;
    result.run.selection.valid = validCount.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid;
    result.channelCount = channels;
    result.plannedWorkers = plannedWorkerCount(execution, destinationHeight, geometryRowCost(destinationWidth));
    result.workingBytes = medianMemory->bytes();
    result.exactStoredValues =
        plan.operation() == GeometryOperation::Crop || integer.direction == IntegerResampleDirection::Enlarge ||
        integer.reduction == IntegerReduction::Minimum || integer.reduction == IntegerReduction::Maximum;
    return Status::success();
}

/// A Float32 working plane over a block of context memory, its rows packed with no padding.
///
/// @return the view, or empty when the extent cannot be described by one.
[[nodiscard]] std::optional<PlaneView<float>> planeOver(void* samples, std::uint32_t width,
                                                        std::uint32_t height) noexcept
{
    const std::uint64_t sampleCount = static_cast<std::uint64_t>(width) * height;
    if (sampleCount > (std::numeric_limits<std::size_t>::max)())
    {
        return std::nullopt;
    }
    return PlaneView<float>::create(
        std::span<float>{static_cast<float*>(samples), static_cast<std::size_t>(sampleCount)}, width, height, 1u,
        static_cast<std::uint64_t>(width) * sizeof(float));
}

/// Widens one channel of the source into a Float32 working plane, carrying each sample across as the
/// number it is.
///
/// @return success; `ErrorCode::InvalidArgument` for a representation outside the resident set; or
///         the scheduler's failure.
[[nodiscard]] Status gatherChannel(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                   const ImageLayout& layout, std::uint32_t channel, PlaneView<float> destination)
{
    Status result = Status::success();
    const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        result =
            parallelFor(execution, destination.height(), geometryRowCost(destination.width()), [&](TaskRange range) {
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    std::span<float> row = *destination.row(y);
                    for (std::uint32_t x = 0u; x < destination.width(); ++x)
                    {
                        row[x] = static_cast<float>(sampleAt<SampleT>(source, layout, channel, x, y));
                    }
                }
                return Status::success();
            });
    });
    return dispatched ? result
                      : geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                        "the source representation is outside the resident set");
}

/// Writes a Float32 working plane back into one channel of the destination through the default
/// code-value conversion, skipping every position whose validity byte is zero.
///
/// @return success; `ErrorCode::InvalidArgument` for a representation outside the resident set; or
///         the scheduler's failure, in which case some rows may already have been written.
[[nodiscard]] Status scatterChannel(const ExecutionContext& execution, SampleType sampleType,
                                    PlaneView<const float> source, std::span<const std::uint8_t> validity,
                                    void* destination, const ImageLayout& layout, std::uint32_t channel)
{
    Status result = Status::success();
    const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        result = parallelFor(execution, source.height(), geometryRowCost(source.width()), [&](TaskRange range) {
            for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                const std::span<const float> row = *source.row(y);
                for (std::uint32_t x = 0u; x < source.width(); ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * source.width() + x;
                    if (validity[index] == 0u)
                    {
                        continue;
                    }
                    sampleAt<SampleT>(destination, layout, channel, x, y) =
                        convertSample<float, SampleT>(row[x], ConversionPolicy{.domain = DomainMapping::CodeValue})
                            .value;
                }
            }
            return Status::success();
        });
    });
    return dispatched ? result
                      : geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                        "the destination representation is outside the resident set");
}

/// Computes the source coordinate of every destination pixel centre, row-major, before any sample is
/// written.
///
/// @return success; `ErrorCode::InvalidArgument` for a centre whose denominator or coordinate is not
///         finite, or whose denominator is zero; or the scheduler's failure.
[[nodiscard]] Status buildCoordinates(const ExecutionContext& execution, const GeometryPlan& plan,
                                      std::span<KernelCoordinate> coordinates)
{
    const std::uint32_t width = plan.destinationDescriptor().extent().width();
    const std::uint32_t height = plan.destinationDescriptor().extent().height();
    const std::array<double, 9>& matrix = plan.destinationToSource();
    return parallelFor(execution, height, geometryRowCost(width), [&](TaskRange range) {
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double denominator = matrix[6] * x + matrix[7] * y + matrix[8];
                if (!std::isfinite(denominator) || denominator == 0.0)
                {
                    return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                           "the projective denominator is zero or non-finite at a destination center");
                }
                const double sx = (matrix[0] * x + matrix[1] * y + matrix[2]) / denominator;
                const double sy = (matrix[3] * x + matrix[4] * y + matrix[5]) / denominator;
                if (!std::isfinite(sx) || !std::isfinite(sy))
                {
                    return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                           "a transformed coordinate is non-finite");
                }
                coordinates[static_cast<std::size_t>(y) * width + x] = {.x = sx, .y = sy};
            }
        }
        return Status::success();
    });
}

} // namespace

GeometryPlan::GeometryPlan(GeometryOperation operation, const ImageDescriptor& sourceDescriptor,
                           const ImageDescriptor& destinationDescriptor,
                           const std::array<double, 9>& destinationToSource, const SamplingKernelParameters& sampling,
                           std::uint32_t cropX, std::uint32_t cropY, const IntegerResampleRequest& integerRequest,
                           ResizeRingingPolicy resizeRinging) noexcept
    : m_operation(operation), m_sourceDescriptor(sourceDescriptor), m_destinationDescriptor(destinationDescriptor),
      m_destinationToSource(destinationToSource), m_sampling(sampling), m_cropX(cropX), m_cropY(cropY),
      m_integerRequest(integerRequest), m_resizeRinging(resizeRinging)
{
}

GeometryOperation GeometryPlan::operation() const noexcept
{
    return m_operation;
}

const ImageDescriptor& GeometryPlan::sourceDescriptor() const noexcept
{
    return m_sourceDescriptor;
}

const ImageDescriptor& GeometryPlan::destinationDescriptor() const noexcept
{
    return m_destinationDescriptor;
}

const std::array<double, 9>& GeometryPlan::destinationToSource() const noexcept
{
    return m_destinationToSource;
}

const SamplingKernelParameters& GeometryPlan::sampling() const noexcept
{
    return m_sampling;
}

std::uint32_t GeometryPlan::cropX() const noexcept
{
    return m_cropX;
}

std::uint32_t GeometryPlan::cropY() const noexcept
{
    return m_cropY;
}

const IntegerResampleRequest& GeometryPlan::integerRequest() const noexcept
{
    return m_integerRequest;
}

ResizeRingingPolicy GeometryPlan::resizeRinging() const noexcept
{
    return m_resizeRinging;
}

Result<GeometryPlan> planCrop(const ImageDescriptor& source, const CropRequest& request)
{
    if (!isCropBoundsPolicy(request.bounds) || request.width == 0u || request.height == 0u ||
        request.x >= source.extent().width() || request.y >= source.extent().height())
    {
        return geometryFailure(
            ErrorCode::InvalidArgument, kCropOperation,
            "a crop has a known bounds policy and begins with a non-empty rectangle inside its source");
    }
    const std::uint32_t availableWidth = source.extent().width() - request.x;
    const std::uint32_t availableHeight = source.extent().height() - request.y;
    const bool overhangs = request.width > availableWidth || request.height > availableHeight;
    if (overhangs && request.bounds == CropBoundsPolicy::Reject)
    {
        return geometryFailure(ErrorCode::InvalidArgument, kCropOperation,
                               "a strict crop rectangle is fully contained by its source");
    }
    const std::uint32_t width = (std::min)(request.width, availableWidth);
    const std::uint32_t height = (std::min)(request.height, availableHeight);
    // The phase moves by the crop's origin, reduced over the mosaic's own block: two pixels for a named
    // pattern and six for a table, whose descriptor only the table's own entry point can build.
    CfaDescriptor cfa = source.cfa();
    if (cfa.isMosaiced())
    {
        const std::uint32_t block = cfaPatternBlockSize(cfa.pattern());
        const std::uint32_t originX = (cfa.originX() + request.x % block) % block;
        const std::uint32_t originY = (cfa.originY() + request.y % block) % block;
        cfa = cfa.hasFilterTable() ? *CfaDescriptor::create(cfa.filterTable().entries(), originX, originY)
                                   : *CfaDescriptor::create(cfa.pattern(), originX, originY);
    }
    Result<ImageDescriptor> destination =
        changedDescriptor(source, width, height, cfa, source.pixelAspectRatio(), kCropOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    return GeometryPlan{
        GeometryOperation::Crop,
        source,
        *destination,
        {1.0, 0.0, static_cast<double>(request.x), 0.0, 1.0, static_cast<double>(request.y), 0.0, 0.0, 1.0},
        {},
        request.x,
        request.y,
        {}};
}

Result<GeometryPlan> planResize(const ImageDescriptor& source, const ResizeRequest& request)
{
    if (source.isMosaiced())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kResizeOperation,
                               "generic interpolation would combine samples under different CFA filters");
    }
    Status ready = checkSampling(request.sampling, kResizeOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (!isResizeRingingPolicy(request.ringing))
    {
        return geometryFailure(ErrorCode::InvalidArgument, kResizeOperation,
                               "the resize ringing policy is outside its enumeration");
    }
    if (request.width == 0u || request.height == 0u)
    {
        return geometryFailure(ErrorCode::InvalidArgument, kResizeOperation, "a resize destination is non-empty");
    }
    const double sx = static_cast<double>(source.extent().width()) / request.width;
    const double sy = static_cast<double>(source.extent().height()) / request.height;
    const bool asymmetricNearest = request.sampling.interpolation == KernelInterpolation::Nearest;
    const double translationX = asymmetricNearest ? -0.5 : 0.5 * sx - 0.5;
    const double translationY = asymmetricNearest ? -0.5 : 0.5 * sy - 0.5;
    const double aspect = source.pixelAspectRatio() * sx / sy;
    Result<ImageDescriptor> destination =
        changedDescriptor(source, request.width, request.height, source.cfa(), aspect, kResizeOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    return GeometryPlan{GeometryOperation::Resize,
                        source,
                        *destination,
                        {sx, 0.0, translationX, 0.0, sy, translationY, 0.0, 0.0, 1.0},
                        request.sampling,
                        0u,
                        0u,
                        {},
                        request.ringing};
}

Result<GeometryPlan> planIntegerResample(const ImageDescriptor& source, const IntegerResampleRequest& request)
{
    if (source.isMosaiced())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kIntegerOperation,
                               "a mosaiced image needs the CFA-aware integer plan");
    }
    const Status ready = checkIntegerRequest(request, kIntegerOperation);
    if (ready.failed())
    {
        return ready;
    }
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    if (request.direction == IntegerResampleDirection::Enlarge)
    {
        if (!checkedProduct(source.extent().width(), request.factor, width) ||
            !checkedProduct(source.extent().height(), request.factor, height))
        {
            return geometryFailure(ErrorCode::OutOfRange, kIntegerOperation,
                                   "the enlarged extent is not representable");
        }
    }
    else
    {
        if (request.factor > source.extent().width() || request.factor > source.extent().height())
        {
            return geometryFailure(ErrorCode::OutOfRange, kIntegerOperation,
                                   "an integer reduction factor leaves at least one complete source block");
        }
        if (request.remainder == IntegerRemainderPolicy::RequireCompleteBlocks &&
            (source.extent().width() % request.factor != 0u || source.extent().height() % request.factor != 0u))
        {
            return geometryFailure(ErrorCode::IncompatibleArguments, kIntegerOperation,
                                   "both source axes are divisible by a strict reduction factor");
        }
        width = source.extent().width() / request.factor;
        height = source.extent().height() / request.factor;
    }
    Result<ImageDescriptor> destination =
        changedDescriptor(source, width, height, source.cfa(), source.pixelAspectRatio(), kIntegerOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    return GeometryPlan{
        GeometryOperation::IntegerResample, source, *destination, {1, 0, 0, 0, 1, 0, 0, 0, 1}, {}, 0u, 0u, request};
}

Result<GeometryPlan> planCfaIntegerResample(const ImageDescriptor& source, const IntegerResampleRequest& request)
{
    if (!source.isMosaiced())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kCfaIntegerOperation,
                               "a CFA-aware plan needs an unresolved mosaic");
    }
    if (cfaPatternBlockSize(source.cfa().pattern()) != 2u)
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kCfaIntegerOperation,
                               "a CFA-aware plan walks two-by-two phase blocks, and this mosaic repeats over more");
    }
    const Status ready = checkIntegerRequest(request, kCfaIntegerOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (source.extent().width() % 2u != 0u || source.extent().height() % 2u != 0u)
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kCfaIntegerOperation,
                               "a two-by-two CFA resample needs a whole number of phase blocks on both axes");
    }
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    if (request.direction == IntegerResampleDirection::Enlarge)
    {
        if (!checkedProduct(source.extent().width(), request.factor, width) ||
            !checkedProduct(source.extent().height(), request.factor, height))
        {
            return geometryFailure(ErrorCode::OutOfRange, kCfaIntegerOperation,
                                   "the enlarged CFA extent is not representable");
        }
    }
    else
    {
        const std::uint64_t divisor = 2u * static_cast<std::uint64_t>(request.factor);
        if (divisor > source.extent().width() || divisor > source.extent().height())
        {
            return geometryFailure(ErrorCode::OutOfRange, kCfaIntegerOperation,
                                   "a CFA reduction factor leaves at least one complete block in every phase");
        }
        if (request.remainder == IntegerRemainderPolicy::RequireCompleteBlocks &&
            (source.extent().width() % divisor != 0u || source.extent().height() % divisor != 0u))
        {
            return geometryFailure(
                ErrorCode::IncompatibleArguments, kCfaIntegerOperation,
                "a strict CFA reduction axis is divisible by twice its factor, preserving every phase");
        }
        width = 2u * static_cast<std::uint32_t>(source.extent().width() / divisor);
        height = 2u * static_cast<std::uint32_t>(source.extent().height() / divisor);
    }
    Result<ImageDescriptor> destination =
        changedDescriptor(source, width, height, source.cfa(), source.pixelAspectRatio(), kCfaIntegerOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    return GeometryPlan{
        GeometryOperation::CfaIntegerResample, source, *destination, {1, 0, 0, 0, 1, 0, 0, 0, 1}, {}, 0u, 0u, request};
}

Result<GeometryPlan> planRotation(const ImageDescriptor& source, const RotationRequest& request)
{
    if (source.isMosaiced())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kRotateOperation,
                               "rotating an unresolved mosaic would combine different filters");
    }
    if (source.pixelAspectRatio() != 1.0)
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kRotateOperation,
                               "a same-grid arbitrary rotation requires square source pixels");
    }
    if (!std::isfinite(request.angleRadians) || !isRotationCanvas(request.canvas))
    {
        return geometryFailure(ErrorCode::InvalidArgument, kRotateOperation,
                               "a rotation angle is finite and its canvas rule is known");
    }
    const Status ready = checkSampling(request.sampling, kRotateOperation);
    if (ready.failed())
    {
        return ready;
    }
    const double cosine = std::cos(request.angleRadians);
    const double sine = std::sin(request.angleRadians);
    std::uint32_t destinationWidth = source.extent().width();
    std::uint32_t destinationHeight = source.extent().height();
    if (request.canvas == RotationCanvas::ExpandToFit)
    {
        const double exactWidth =
            std::abs(cosine) * source.extent().width() + std::abs(sine) * source.extent().height();
        const double exactHeight =
            std::abs(sine) * source.extent().width() + std::abs(cosine) * source.extent().height();
        const auto containingExtent = [](double exact) -> std::optional<std::uint32_t> {
            const double tolerance = 64.0 * std::numeric_limits<double>::epsilon() * (std::max)(1.0, exact);
            const double rounded = std::ceil(exact - tolerance);
            if (!std::isfinite(rounded) || rounded < 1.0 ||
                rounded > static_cast<double>((std::numeric_limits<std::uint32_t>::max)()))
            {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(rounded);
        };
        const std::optional<std::uint32_t> width = containingExtent(exactWidth);
        const std::optional<std::uint32_t> height = containingExtent(exactHeight);
        if (!width.has_value() || !height.has_value())
        {
            return geometryFailure(ErrorCode::OutOfRange, kRotateOperation,
                                   "the bound-preserving rotation extent is representable");
        }
        destinationWidth = *width;
        destinationHeight = *height;
    }
    Result<ImageDescriptor> destination = changedDescriptor(source, destinationWidth, destinationHeight, source.cfa(),
                                                            source.pixelAspectRatio(), kRotateOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    const double cx = 0.5 * static_cast<double>(source.extent().width() - 1u);
    const double cy = 0.5 * static_cast<double>(source.extent().height() - 1u);
    const double dcx = 0.5 * static_cast<double>(destinationWidth - 1u);
    const double dcy = 0.5 * static_cast<double>(destinationHeight - 1u);
    const std::array<double, 9> matrix{
        cosine, sine, cx - cosine * dcx - sine * dcy, -sine, cosine, cy + sine * dcx - cosine * dcy, 0.0, 0.0, 1.0};
    return GeometryPlan{GeometryOperation::Rotate, source, *destination, matrix, request.sampling, 0u, 0u, {}};
}

Result<GeometryPlan> planAffineWarp(const ImageDescriptor& source, const AffineWarpRequest& request)
{
    if (source.isMosaiced())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kAffineOperation,
                               "an affine interpolation cannot combine unresolved CFA samples");
    }
    const std::array<double, 9> matrix = affineMatrix(request.destinationToSource);
    if (!matrixIsFinite(matrix) || !std::isfinite(request.destinationPixelAspectRatio) ||
        request.destinationPixelAspectRatio <= 0.0)
    {
        return geometryFailure(ErrorCode::InvalidArgument, kAffineOperation,
                               "affine coefficients and a positive destination pixel aspect are finite");
    }
    const Status ready = checkSampling(request.sampling, kAffineOperation);
    if (ready.failed())
    {
        return ready;
    }
    Result<ImageDescriptor> destination = changedDescriptor(source, request.width, request.height, source.cfa(),
                                                            request.destinationPixelAspectRatio, kAffineOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    return GeometryPlan{GeometryOperation::AffineWarp, source, *destination, matrix, request.sampling, 0u, 0u, {}};
}

Result<GeometryPlan> planProjectiveWarp(const ImageDescriptor& source, const ProjectiveWarpRequest& request)
{
    if (source.isMosaiced())
    {
        return geometryFailure(ErrorCode::IncompatibleArguments, kProjectiveOperation,
                               "a projective interpolation cannot combine unresolved CFA samples");
    }
    if (!matrixIsFinite(request.destinationToSource) || !std::isfinite(request.destinationPixelAspectRatio) ||
        request.destinationPixelAspectRatio <= 0.0)
    {
        return geometryFailure(ErrorCode::InvalidArgument, kProjectiveOperation,
                               "projective coefficients and a positive destination pixel aspect are finite");
    }
    const Status sampling = checkSampling(request.sampling, kProjectiveOperation);
    if (sampling.failed())
    {
        return sampling;
    }
    Result<ImageDescriptor> destination = changedDescriptor(source, request.width, request.height, source.cfa(),
                                                            request.destinationPixelAspectRatio, kProjectiveOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    const Status denominator =
        checkProjectiveDenominators(request.destinationToSource, request.width, request.height, kProjectiveOperation);
    if (denominator.failed())
    {
        return denominator;
    }
    return GeometryPlan{GeometryOperation::ProjectiveWarp,
                        source,
                        *destination,
                        request.destinationToSource,
                        request.sampling,
                        0u,
                        0u,
                        {}};
}

Result<GeometryPlan> planOrientation(const ImageDescriptor& source, ImageOrientation orientation)
{
    if (!isImageOrientation(orientation))
    {
        return geometryFailure(ErrorCode::InvalidArgument, kOrientOperation,
                               "the orientation is outside its enumeration");
    }
    const std::uint32_t width = source.extent().width();
    const std::uint32_t height = source.extent().height();
    const bool swaps = imageOrientationSwapsAxes(orientation);
    const std::array<double, 9> matrix = orientationMatrix(orientation, width, height);
    const std::optional<CfaDescriptor> cfa = orientedCfa(source.cfa(), matrix);
    if (!cfa.has_value())
    {
        return geometryFailure(ErrorCode::InvalidArgument, kOrientOperation,
                               "the oriented filter array does not describe a mosaic");
    }
    Result<ImageDescriptor> destination =
        changedDescriptor(source, swaps ? height : width, swaps ? width : height, *cfa,
                          swaps ? 1.0 / source.pixelAspectRatio() : source.pixelAspectRatio(), kOrientOperation);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    return GeometryPlan{GeometryOperation::Orient, source, *destination, matrix, {}, 0u, 0u, {}};
}

Result<GeometryResult> transformImageGeometry(const ExecutionContext& execution, SampleType sampleType,
                                              const void* source, const ImageLayout& sourceLayout, void* destination,
                                              const ImageLayout& destinationLayout, const GeometryPlan& plan,
                                              const GeometrySelection& selection,
                                              const ValidityMaskView<std::uint8_t>& destinationValidity)
{
    const Status ready = checkExecution(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                        plan, selection, destinationValidity);
    if (ready.failed())
    {
        return ready;
    }
    // Every path below starts from a cleared mask. The direct paths are tried first, each under the
    // conditions its own comment relies on; whatever none of them takes falls through to the gather,
    // sample and scatter at the end.
    destinationValidity.fill(false);
    GeometryResult answer{};
    if (plan.operation() == GeometryOperation::Resize && sampleType == SampleType::Float32 &&
        (plan.sampling().interpolation == KernelInterpolation::Linear ||
         plan.sampling().interpolation == KernelInterpolation::LinearExact) &&
        plan.sampling().border == KernelBorderMode::Clamp &&
        plan.sampling().invalid == SamplingInvalidPolicy::RequireAll &&
        selection.validity.rule() == ValidityRule::AllValid && selection.mask == nullptr &&
        sourceLayout.samplesPerPixel() == 1u && destinationLayout.samplesPerPixel() == 1u)
    {
        const Status resized = runDirectLinearFloatResize(execution, static_cast<const float*>(source), sourceLayout,
                                                          static_cast<float*>(destination), destinationLayout, plan,
                                                          destinationValidity, answer);
        if (resized.failed())
        {
            return resized;
        }
        return answer;
    }
    if (plan.operation() == GeometryOperation::Resize &&
        (plan.sampling().interpolation == KernelInterpolation::Nearest ||
         plan.sampling().interpolation == KernelInterpolation::NearestExact) &&
        plan.sampling().border == KernelBorderMode::Clamp && selection.validity.rule() == ValidityRule::AllValid &&
        selection.mask == nullptr && sourceLayout.samplesPerPixel() == 1u && destinationLayout.samplesPerPixel() == 1u)
    {
        Status resized = Status::success();
        const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            resized = runDirectNearestResize<SampleT>(execution, source, sourceLayout, destination, destinationLayout,
                                                      plan, destinationValidity, answer);
        });
        if (!dispatched)
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "the resident representation is outside the closed set");
        }
        if (resized.failed())
        {
            return resized;
        }
        return answer;
    }
    if (plan.operation() == GeometryOperation::Orient)
    {
        if (selection.validity.rule() == ValidityRule::AllValid && selection.mask == nullptr &&
            sourceLayout.samplesPerPixel() == destinationLayout.samplesPerPixel() &&
            sourceLayout.planeCount() == destinationLayout.planeCount())
        {
            const Status oriented =
                runDirectOrientation(execution, sampleTypeSizeBytes(sampleType), source, sourceLayout, destination,
                                     destinationLayout, plan, destinationValidity, answer);
            if (oriented.failed())
            {
                return oriented;
            }
            return answer;
        }
        Status oriented = Status::success();
        const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            oriented = runOrientation<SampleT>(execution, source, sourceLayout, destination, destinationLayout, plan,
                                               selection, destinationValidity, answer);
        });
        if (!dispatched)
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "the resident representation is outside the closed set");
        }
        if (oriented.failed())
        {
            return oriented;
        }
        return answer;
    }
    if (plan.operation() == GeometryOperation::Crop && selection.validity.rule() == ValidityRule::AllValid &&
        selection.mask == nullptr && sourceLayout.samplesPerPixel() == destinationLayout.samplesPerPixel() &&
        sourceLayout.planeCount() == destinationLayout.planeCount())
    {
        const Status copied = runDirectCrop(execution, sampleTypeSizeBytes(sampleType), source, sourceLayout,
                                            destination, destinationLayout, plan, destinationValidity, answer);
        if (copied.failed())
        {
            return copied;
        }
        return answer;
    }
    if (plan.operation() == GeometryOperation::IntegerResample &&
        plan.integerRequest().direction == IntegerResampleDirection::Reduce &&
        plan.integerRequest().reduction == IntegerReduction::Average && decidedByFiniteness(selection))
    {
        Status reduced = Status::success();
        bool emptyBlock = false;
        const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            reduced =
                runDirectAverageReduction<SampleT>(execution, source, sourceLayout, destination, destinationLayout,
                                                   plan, selection, destinationValidity, answer, emptyBlock);
        });
        if (!dispatched)
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "the resident representation is outside the closed set");
        }
        if (reduced.failed())
        {
            return reduced;
        }
        if (!emptyBlock)
        {
            return answer;
        }
        // A block with nothing finite leaves an output unwritten and invalid, which the generic reducer states.
    }
    if (plan.operation() == GeometryOperation::Crop || plan.operation() == GeometryOperation::IntegerResample ||
        plan.operation() == GeometryOperation::CfaIntegerResample)
    {
        Status exact = Status::success();
        const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            exact = runExact<SampleT>(execution, source, sourceLayout, destination, destinationLayout, plan, selection,
                                      destinationValidity, answer);
        });
        if (!dispatched)
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "the resident representation is outside the closed set");
        }
        if (exact.failed())
        {
            return exact;
        }
        return answer;
    }

    if (plan.operation() == GeometryOperation::Resize &&
        plan.sampling().interpolation == KernelInterpolation::NearestExact)
    {
        Status exact = Status::success();
        const bool dispatched = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            exact = runNearestExactResize<SampleT>(execution, source, sourceLayout, destination, destinationLayout,
                                                   plan, selection, destinationValidity, answer);
        });
        if (!dispatched)
        {
            return geometryFailure(ErrorCode::InvalidArgument, kTransformOperation,
                                   "the resident representation is outside the closed set");
        }
        if (exact.failed())
        {
            return exact;
        }
        return answer;
    }

    if (plan.operation() == GeometryOperation::Resize &&
        plan.sampling().interpolation == KernelInterpolation::LinearExact &&
        plan.sampling().border == KernelBorderMode::Clamp &&
        plan.sampling().invalid == SamplingInvalidPolicy::RequireAll)
    {
        Status exact = Status::success();
        switch (sampleType)
        {
        case SampleType::UInt8:
            exact = runLinearExactResize<std::uint8_t>(execution, source, sourceLayout, destination, destinationLayout,
                                                       selection, destinationValidity, answer);
            break;
        case SampleType::UInt16:
            exact = runLinearExactResize<std::uint16_t>(execution, source, sourceLayout, destination, destinationLayout,
                                                        selection, destinationValidity, answer);
            break;
        case SampleType::Int16:
            exact = runLinearExactResize<std::int16_t>(execution, source, sourceLayout, destination, destinationLayout,
                                                       selection, destinationValidity, answer);
            break;
        default:
            break;
        }
        if (exact.failed())
        {
            return exact;
        }
        if (sampleType == SampleType::UInt8 || sampleType == SampleType::UInt16 || sampleType == SampleType::Int16)
        {
            return answer;
        }
    }

    const bool coordinateWarp = plan.operation() == GeometryOperation::Rotate ||
                                plan.operation() == GeometryOperation::AffineWarp ||
                                plan.operation() == GeometryOperation::ProjectiveWarp;
    if (coordinateWarp && sampleType == SampleType::Float32 && selection.validity.rule() == ValidityRule::AllValid &&
        selection.mask == nullptr && sourceLayout.samplesPerPixel() == 1u &&
        destinationLayout.samplesPerPixel() == 1u &&
        warpCoordinatesAreFinite(plan.destinationToSource(), destinationLayout.extent().width(),
                                 destinationLayout.extent().height()))
    {
        const Status warped = runDirectFloatWarp(execution, source, sourceLayout, destination, destinationLayout, plan,
                                                 destinationValidity, answer);
        if (warped.failed())
        {
            return warped;
        }
        return answer;
    }

    const std::uint32_t sourceWidth = sourceLayout.extent().width();
    const std::uint32_t sourceHeight = sourceLayout.extent().height();
    const std::uint32_t destinationWidth = destinationLayout.extent().width();
    const std::uint32_t destinationHeight = destinationLayout.extent().height();
    const bool downsamplingResize = plan.operation() == GeometryOperation::Resize && destinationWidth <= sourceWidth &&
                                    destinationHeight <= sourceHeight;
    const bool areaResize = downsamplingResize && plan.sampling().interpolation == KernelInterpolation::Area;
    const bool suppressDarkRinging = plan.operation() == GeometryOperation::Resize &&
                                     plan.resizeRinging() == ResizeRingingPolicy::SuppressDarkRinging &&
                                     (plan.sampling().interpolation == KernelInterpolation::Cubic ||
                                      plan.sampling().interpolation == KernelInterpolation::Lanczos4);
    const double areaScaleX = static_cast<double>(sourceWidth) / destinationWidth;
    const double areaScaleY = static_cast<double>(sourceHeight) / destinationHeight;
    const bool tabulated = coordinatesSeparate(plan);
    if (tabulated && sampleType == SampleType::Float32 && selection.validity.rule() == ValidityRule::AllValid &&
        selection.mask == nullptr && sourceLayout.samplesPerPixel() == 1u && destinationLayout.samplesPerPixel() == 1u)
    {
        const Status resized = runDirectFloatResize(execution, static_cast<const float*>(source), sourceLayout,
                                                    static_cast<float*>(destination), destinationLayout, plan,
                                                    destinationValidity, answer);
        if (resized.failed())
        {
            return resized;
        }
        return answer;
    }
    const std::uint64_t sourcePixels = static_cast<std::uint64_t>(sourceWidth) * sourceHeight;
    const std::uint64_t destinationPixels = static_cast<std::uint64_t>(destinationWidth) * destinationHeight;
    // A resize resolves its taps once per column and once per row; anything else carries one
    // coordinate per pixel. The two never coexist in one operation, so one of the two sizes is zero.
    const CheckedSize checkedCoordinateBytes =
        tabulated ? CheckedSize{0u} : CheckedSize{destinationPixels} * sizeof(KernelCoordinate);
    const CheckedSize checkedTableBytes =
        tabulated ? resizeTableBytes(destinationWidth, destinationHeight) : CheckedSize{0u};
    const CheckedSize checkedSourceBytes = CheckedSize{sourcePixels} * sizeof(float);
    const CheckedSize checkedDestinationBytes = CheckedSize{destinationPixels} * sizeof(float);
    const CheckedSize checkedValidityBytes = destinationPixels;
    const std::optional<std::uint64_t> checkedWorkingBytes =
        (checkedTableBytes + checkedCoordinateBytes + checkedSourceBytes + checkedDestinationBytes +
         checkedValidityBytes)
            .value();
    if (!checkedWorkingBytes.has_value())
    {
        return geometryFailure(ErrorCode::LimitExceeded, kTransformOperation,
                               "the total geometry working set overflows");
    }
    const std::uint64_t tableBytes = checkedTableBytes.valueOr(0u);
    const std::uint64_t coordinateBytes = checkedCoordinateBytes.valueOr(0u);
    const std::uint64_t sourceBytes = checkedSourceBytes.valueOr(0u);
    const std::uint64_t destinationBytes = checkedDestinationBytes.valueOr(0u);
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, *checkedWorkingBytes, kDefaultBufferAlignmentBytes, kTransformOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const block = static_cast<std::uint8_t*>(memory->data());
    // Tables first, because their entries carry the strictest alignment; every size before the
    // sample planes is a multiple of eight bytes, so each region starts where its type needs it.
    const ResizeTables tables = resizeTablesOver(block, destinationWidth, destinationHeight);
    auto* const coordinates = reinterpret_cast<KernelCoordinate*>(block + tableBytes);
    auto* const sourcePlaneSamples = reinterpret_cast<float*>(block + tableBytes + coordinateBytes);
    auto* const destinationPlaneSamples = reinterpret_cast<float*>(block + tableBytes + coordinateBytes + sourceBytes);
    auto* const outputValidity = block + tableBytes + coordinateBytes + sourceBytes + destinationBytes;
    const std::optional<PlaneView<float>> sourcePlane = planeOver(sourcePlaneSamples, sourceWidth, sourceHeight);
    const std::optional<PlaneView<float>> destinationPlane =
        planeOver(destinationPlaneSamples, destinationWidth, destinationHeight);
    if (!sourcePlane.has_value() || !destinationPlane.has_value())
    {
        return geometryFailure(ErrorCode::LimitExceeded, kTransformOperation,
                               "a geometry working plane cannot describe this extent");
    }
    if (tabulated)
    {
        const bool buildGuideTaps = suppressDarkRinging && !downsamplingResize;
        const bool buildAreaFootprints = areaResize || (suppressDarkRinging && downsamplingResize);
        const Status built =
            buildResizeTables(execution, plan, sourceWidth, sourceHeight, destinationWidth, destinationHeight,
                              areaScaleX, areaScaleY, buildGuideTaps, buildAreaFootprints, tables);
        if (built.failed())
        {
            return built;
        }
    }
    else
    {
        const Status coordinatesBuilt = buildCoordinates(
            execution, plan, std::span<KernelCoordinate>{coordinates, static_cast<std::size_t>(destinationPixels)});
        if (coordinatesBuilt.failed())
        {
            return coordinatesBuilt;
        }
    }

    answer.channelCount = sourceLayout.extent().channelCount();
    answer.workingBytes = memory->bytes();
    answer.plannedWorkers = plannedWorkerCount(execution, destinationHeight, geometryRowCost(destinationWidth));
    for (std::uint32_t channel = 0u; channel < answer.channelCount; ++channel)
    {
        Status status = gatherChannel(execution, sampleType, source, sourceLayout, channel, *sourcePlane);
        if (status.failed())
        {
            return status;
        }
        std::fill_n(outputValidity, static_cast<std::size_t>(destinationPixels), std::uint8_t{0u});
        KernelValidityView validity{.descriptor = selection.validity,
                                    .mask = selection.mask,
                                    .maskPlane =
                                        selection.mask != nullptr && selection.mask->planeCount() > 1u ? channel : 0u};
        const reference_detail::SamplingPlan sampling =
            reference_detail::planSampling(sourcePlane->constView(), validity, plan.sampling());
        reference_detail::SamplingPlan guideSampling = sampling;
        guideSampling.interpolation = KernelInterpolation::Area;
        const auto sampleGuide = [&](std::uint32_t x, std::uint32_t y, float& value) {
            if (tabulated)
            {
                return downsamplingResize
                           ? sampleAreaTabulated(guideSampling, tables.areaXAxes[x], tables.areaYAxes[y], value)
                           : sampleTabulated(guideSampling, tables.guideXAxes[x], tables.guideYAxes[y], value);
            }
            const std::size_t index = static_cast<std::size_t>(y) * destinationWidth + x;
            return downsamplingResize ? reference_detail::sampleArea(guideSampling, static_cast<double>(x) * areaScaleX,
                                                                     static_cast<double>(y) * areaScaleY,
                                                                     static_cast<double>(x + 1u) * areaScaleX,
                                                                     static_cast<double>(y + 1u) * areaScaleY, value)
                                      : reference_detail::sampleInterpolated(guideSampling, coordinates[index], value);
        };
        std::atomic<std::uint64_t> channelValid{0u};
        status =
            parallelFor(execution, destinationHeight, geometryRowCost(destinationWidth, 24u), [&](TaskRange range) {
                std::uint64_t localValid = 0u;
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    std::span<float> row = *destinationPlane->row(y);
                    for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                    {
                        const std::size_t index = static_cast<std::size_t>(y) * destinationWidth + x;
                        float value = 0.0f;
                        bool valid = false;
                        if (tabulated)
                        {
                            valid = areaResize
                                        ? sampleAreaTabulated(sampling, tables.areaXAxes[x], tables.areaYAxes[y], value)
                                        : sampleTabulated(sampling, tables.xAxes[x], tables.yAxes[y], value);
                        }
                        else
                        {
                            valid = areaResize
                                        ? reference_detail::sampleArea(sampling, static_cast<double>(x) * areaScaleX,
                                                                       static_cast<double>(y) * areaScaleY,
                                                                       static_cast<double>(x + 1u) * areaScaleX,
                                                                       static_cast<double>(y + 1u) * areaScaleY, value)
                                        : reference_detail::sampleInterpolated(sampling, coordinates[index], value);
                        }
                        std::uint8_t validityState = valid ? 1u : 0u;
                        if (suppressDarkRinging && valid)
                        {
                            float guide = 0.0f;
                            if (sampleGuide(x, y, guide) && value < 0.98f * guide)
                            {
                                validityState = 2u;
                            }
                        }
                        outputValidity[index] = validityState;
                        stateDestinationValidity(destinationValidity, channel, x, y, valid);
                        if (valid)
                        {
                            row[x] = value;
                            ++localValid;
                        }
                    }
                }
                channelValid.fetch_add(localValid, std::memory_order_relaxed);
                return Status::success();
            });
        if (status.failed())
        {
            return status;
        }
        if (suppressDarkRinging)
        {
            status =
                parallelFor(execution, destinationHeight, geometryRowCost(destinationWidth, 12u), [&](TaskRange range) {
                    for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                    {
                        const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                        std::span<float> row = *destinationPlane->row(y);
                        for (std::uint32_t x = 0u; x < destinationWidth; ++x)
                        {
                            const std::size_t index = static_cast<std::size_t>(y) * destinationWidth + x;
                            if (outputValidity[index] == 0u)
                            {
                                continue;
                            }
                            const bool guarded =
                                outputValidity[index] == 2u || (x != 0u && outputValidity[index - 1u] == 2u) ||
                                (x + 1u != destinationWidth && outputValidity[index + 1u] == 2u) ||
                                (y != 0u && outputValidity[index - destinationWidth] == 2u) ||
                                (y + 1u != destinationHeight && outputValidity[index + destinationWidth] == 2u);
                            if (!guarded)
                            {
                                continue;
                            }
                            float guide = 0.0f;
                            if (sampleGuide(x, y, guide))
                            {
                                row[x] = guide;
                            }
                        }
                    }
                    return Status::success();
                });
            if (status.failed())
            {
                return status;
            }
        }
        status =
            scatterChannel(execution, sampleType, destinationPlane->constView(),
                           std::span<const std::uint8_t>{outputValidity, static_cast<std::size_t>(destinationPixels)},
                           destination, destinationLayout, channel);
        if (status.failed())
        {
            return status;
        }
        const std::uint64_t valid = channelValid.load(std::memory_order_relaxed);
        answer.run.selection.total += destinationPixels;
        answer.run.selection.valid += valid;
        answer.run.outputsWritten += valid;
    }
    return answer;
}

} // namespace an
