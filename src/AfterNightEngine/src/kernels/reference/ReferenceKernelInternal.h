// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - shared validation and scalar traversal helpers for reference kernels.
//
// Every scalar oracle in this directory checks its arguments through the same few functions, and so
// do the algorithm primitives that run the same operations on the engine's threads. That is what
// makes the two refuse the same inputs with the same `ErrorCode`: an empty execution is
// `ErrorCode::NotInitialized`, a stopped one is `ErrorCode::Cancelled` or
// `ErrorCode::DeadlineExceeded`, a plane of the wrong shape or a mask that does not fit it is
// `ErrorCode::IncompatibleArguments`, and a malformed argument or a forbidden overlap is
// `ErrorCode::InvalidArgument`. Every failure built here carries the caller's entry-point name and
// `ErrorDomain::Algorithms`; a stop is passed on as the execution reported it, carrying only its
// code.
//
// The interpolated read is defined here once as well: `samplingTaps` places and weights the taps
// of every interpolation, and `sampleInterpolated` and `sampleArea` combine them under a border
// rule and a validity policy. The reference resampler and gather, the geometry and registration
// primitives and the compiled warp all read through these, which is what makes them agree bit for
// bit rather than within a tolerance.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>

/// Validation, traversal and interpolated reading shared by the scalar reference kernels and the
/// algorithm primitives that must agree with them.
namespace an::reference_detail
{

/// One failure of a reference kernel.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside, carried in the failure.
/// @param message what a person reads; never parsed.
/// @return the failure, in `ErrorDomain::Algorithms`.
[[nodiscard]] Status kernelFailure(ErrorCode code, const char* operation, const char* message);

/// Whether an execution may run at all.
///
/// @param execution the operation.
/// @param operation the entry point, carried by a failure.
/// @return success; `ErrorCode::NotInitialized` when the execution holds no engine context; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when it has been stopped.
[[nodiscard]] Status checkExecution(const ExecutionContext& execution, const char* operation);

/// Whether a read-only plane holds one sample per pixel, which is all a scalar reference reads.
///
/// @param plane the plane.
/// @param operation the entry point, carried by a failure.
/// @return success, or `ErrorCode::IncompatibleArguments` for an interleaved plane.
[[nodiscard]] Status checkSingleChannel(PlaneView<const float> plane, const char* operation);

/// Whether a writable plane holds one sample per pixel, which is all a scalar reference writes.
///
/// @param plane the plane.
/// @param operation the entry point, carried by a failure.
/// @return success, or `ErrorCode::IncompatibleArguments` for an interleaved plane.
[[nodiscard]] Status checkSingleChannel(PlaneView<float> plane, const char* operation);

/// Whether a source and a destination are single-sample planes of the same width and height.
///
/// @param source the plane read.
/// @param destination the plane written.
/// @param operation the entry point, carried by a failure.
/// @return success, or `ErrorCode::IncompatibleArguments` for an interleaved plane or a difference
///         in extent.
[[nodiscard]] Status checkPair(PlaneView<const float> source, PlaneView<float> destination, const char* operation);

/// Whether a destination is exactly its source: the same first sample and the same layout.
///
/// @param source the plane read.
/// @param destination the plane written.
/// @return true for an exact in-place call; false for disjoint planes and for partial overlap.
[[nodiscard]] bool exactlyInPlace(PlaneView<const float> source, PlaneView<float> destination) noexcept;

/// Whether a source and a destination may share memory the way they do.
///
/// Disjoint planes always may; an exact in-place call may only when the kernel says so, because a
/// pointwise kernel reads each sample before it writes that sample, while a neighbourhood kernel
/// would read samples it had already overwritten.
///
/// @param source the plane read.
/// @param destination the plane written.
/// @param exactInPlaceAllowed whether this kernel supports writing over its own input.
/// @param operation the entry point, carried by a failure.
/// @return success, or `ErrorCode::InvalidArgument` for any other overlap, which `viewsOverlap`
///         reports conservatively.
[[nodiscard]] Status checkOverlap(PlaneView<const float> source, PlaneView<float> destination, bool exactInPlaceAllowed,
                                  const char* operation);

/// Whether a validity rule and its optional packed mask are coherent for a plane.
///
/// @param validity the rule, the mask and the selected bit-plane.
/// @param width the plane's width, which the mask must match exactly.
/// @param height the plane's height, which the mask must match exactly.
/// @param operation the entry point, carried by a failure.
/// @return success; `ErrorCode::InvalidArgument` for a mask or a nonzero bit-plane given to a rule
///         that reads none, or for the explicit-mask rule without a mask; or
///         `ErrorCode::IncompatibleArguments` for a mask of another extent or a bit-plane it does
///         not have.
[[nodiscard]] Status checkValidity(const KernelValidityView& validity, std::uint32_t width, std::uint32_t height,
                                   const char* operation);

/// The per-sample question a validity rule and its packed mask answer together.
///
/// It borrows the view it is built from, which must outlive it and must already have passed
/// `checkValidity`, because the mask is read without a bounds test.
class ValidityAccessor
{
public:
    /// Borrows a checked validity view.
    ///
    /// @param validity the rule and optional mask.
    explicit ValidityAccessor(const KernelValidityView& validity) noexcept;

    /// Whether one sample participates.
    ///
    /// @param value the sample, which the value rule judges.
    /// @param x its column, which selects the mask bit.
    /// @param y its row.
    /// @return true when the value rule accepts the value and either no mask is present or the
    ///         pixel's bit is set.
    [[nodiscard]] bool accepts(float value, std::uint32_t x, std::uint32_t y) const noexcept;

private:
    /// The rule and mask, borrowed for this accessor's life.
    const KernelValidityView& m_validity;
};

/// The common precondition of a kernel that reads one plane: the execution, a single-sample source
/// and a validity rule that fits it, in that order.
///
/// @param execution the operation.
/// @param source the plane read.
/// @param validity the rule and optional mask for that plane.
/// @param operation the entry point, carried by a failure.
/// @return success, or the first failure of `checkExecution`, `checkSingleChannel` and
///         `checkValidity`.
[[nodiscard]] Status checkInput(const ExecutionContext& execution, PlaneView<const float> source,
                                const KernelValidityView& validity, const char* operation);

/// The cancellation checkpoint a kernel calls between rows or batches of items.
///
/// @param execution the operation.
/// @return success, `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`. The failure carries
///         only its code.
[[nodiscard]] Status checkpoint(const ExecutionContext& execution) noexcept;

/// A continuous coordinate rounded to the pixel that contains it, or the answer that there is none.
struct ResolvedCoordinate
{
    /// The pixel's column, meaningful only when `outside` is false.
    std::uint32_t x = 0u;

    /// The pixel's row, meaningful only when `outside` is false.
    std::uint32_t y = 0u;

    /// Whether the coordinate fell outside the plane and was not clamped into it.
    bool outside = false;
};

/// Whether a coordinate can be interpolated: both components finite and within the range where
/// every integer is exact in `double`, so its taps and border transforms fit signed 64-bit
/// arithmetic.
///
/// @param coordinate the coordinate.
/// @return true when both components are finite and no further than `2^53 - 2` from zero.
[[nodiscard]] bool coordinateIsFinite(const KernelCoordinate& coordinate) noexcept;

/// Rounds a coordinate to the nearest pixel under the pixel-centre convention, halves rounding up.
///
/// Pixel `(x, y)` covers `[x - 0.5, x + 0.5)` on each axis.
///
/// @param coordinate the coordinate, which must have passed `coordinateIsFinite`.
/// @param width the plane's width. Must be positive when `clamp` is true.
/// @param height the plane's height. Must be positive when `clamp` is true.
/// @param clamp whether a coordinate outside the plane is moved to the nearest edge pixel rather
///        than reported outside.
/// @return the pixel, or a result whose `outside` is set.
[[nodiscard]] ResolvedCoordinate resolveCoordinate(const KernelCoordinate& coordinate, std::uint32_t width,
                                                   std::uint32_t height, bool clamp) noexcept;

/// Whether every coordinate of a list passes `coordinateIsFinite`.
///
/// Checks for a stop once every 256 coordinates, so a long list can be abandoned part way.
///
/// @param execution the operation.
/// @param coordinates the list.
/// @param operation the entry point, carried by a failure.
/// @return success; `ErrorCode::InvalidArgument` at the first coordinate that fails; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`.
[[nodiscard]] Status checkCoordinates(const ExecutionContext& execution, std::span<const KernelCoordinate> coordinates,
                                      const char* operation);

/// Whether a border mode is one an interpolated read can apply: `Clamp`, `Constant`, `Reflect`,
/// `ReflectExcludingEdge` or `Wrap`, and not `NotApplicable`.
///
/// @param border the mode.
/// @return true for the five sampling borders.
[[nodiscard]] bool isSamplingBorder(KernelBorderMode border) noexcept;

/// Whether a sampling request names known policies and finite substitute values.
///
/// @param parameters the interpolation, border, invalid-sample policy and the two values it uses.
/// @param operation the entry point, carried by a failure.
/// @return success, or `ErrorCode::InvalidArgument` for an unknown interpolation, invalid-sample
///         policy or border, or a non-finite constant border or invalid-sample value.
[[nodiscard]] Status checkSamplingParameters(const SamplingKernelParameters& parameters, const char* operation);

/// Everything about a sampling operation that does not change from one coordinate to the next.
///
/// Built once for an operation and read by every worker running it. Neither the source's address
/// nor whether the validity rule can reject a sample varies within one operation, so resolving
/// both once keeps a bounds-checked row accessor and a per-tap validity call out of the inner loop,
/// which is left as arithmetic over a pointer.
///
/// It owns nothing: the samples and the validity rule belong to the caller and must outlive it.
struct SamplingPlan
{
    /// The source plane's first sample.
    const float* samples = nullptr;

    /// Samples from one source row to the next, which is not the width when the plane is padded.
    std::uint64_t rowStride = 0u;

    /// The source extent, signed because the border transforms work in signed indices.
    std::int64_t width = 0;

    /// The source height, signed for the same reason.
    std::int64_t height = 0;

    /// What the operation interpolates with.
    KernelInterpolation interpolation = KernelInterpolation::Nearest;

    /// What it does with a tap outside the source.
    KernelBorderMode border = KernelBorderMode::Clamp;

    /// What it does with a tap the validity rule rejects.
    SamplingInvalidPolicy invalid = SamplingInvalidPolicy::RequireAll;

    /// The value a tap outside a constant border reads.
    float constantBorder = 0.0f;

    /// The value a rejected tap contributes under `SamplingInvalidPolicy::Substitute`.
    float invalidSample = 0.0f;

    /// The rule deciding which samples participate, borrowed for the sampling plan's life.
    const KernelValidityView* validity = nullptr;

    /// Whether that rule accepts every sample whatever its value, which lets the inner loop skip
    /// the per-tap question. Proven from the rule and the absence of a mask, never assumed.
    bool acceptsEverySample = false;

    /// Elements between samples of one component in an interleaved source.
    std::uint32_t pixelStride = 1u;
};

/// Resolves a checked source, validity rule and sampling request into a sampling plan.
///
/// `pixelStride` is left at one; a caller reading one component of an interleaved source sets it.
///
/// @param source the plane read, which must outlive the sampling plan.
/// @param validity the rule and optional mask, already checked, which must outlive the sampling
///        plan.
/// @param parameters the interpolation, border and invalid-sample policy, already checked.
/// @return the sampling plan.
[[nodiscard]] SamplingPlan planSampling(PlaneView<const float> source, const KernelValidityView& validity,
                                        const SamplingKernelParameters& parameters) noexcept;

/// One axis's taps with an explicit index for each, for a caller that walks them as a list.
///
/// The same taps and weights as `SamplingWindow`; the indices are consecutive.
struct SamplingAxis
{
    /// The source index of each tap, which may lie outside the source until a border resolves it.
    std::array<std::int64_t, 8u> indices{};

    /// Each tap's normalized weight.
    std::array<double, 8u> weights{};

    /// How many of the eight entries are taps.
    std::uint32_t count = 0u;
};

/// The cubic kernel's shape constant, which is OpenCV's.
inline constexpr double kCubicShape = -0.75;

/// The cubic kernel within one sample of the centre.
[[nodiscard]] inline constexpr double cubicInnerLobe(double x) noexcept
{
    return ((kCubicShape + 2.0) * x - (kCubicShape + 3.0)) * x * x + 1.0;
}

/// The cubic kernel between one and two samples of the centre.
///
/// Exactly zero at both ends of that interval, which is what lets a caller that knows which lobe a
/// tap falls in evaluate it without first asking. Split out of `cubicSamplingWeight` so that such a
/// caller and the general one share the polynomial rather than each carrying a copy of it.
[[nodiscard]] inline constexpr double cubicOuterLobe(double x) noexcept
{
    return ((kCubicShape * x - 5.0 * kCubicShape) * x + 8.0 * kCubicShape) * x - 4.0 * kCubicShape;
}

/// The cubic convolution kernel with the shape constant above, at any distance from the centre.
///
/// @param distance the signed distance from the centre, in samples.
/// @return the weight, which is zero from two samples out.
[[nodiscard]] inline double cubicSamplingWeight(double distance) noexcept
{
    const double x = std::abs(distance);
    if (x <= 1.0)
    {
        return cubicInnerLobe(x);
    }
    if (x < 2.0)
    {
        return cubicOuterLobe(x);
    }
    return 0.0;
}

/// The Lanczos kernel of order four: `sinc(x) * sinc(x / 4)` with the normalized sinc.
///
/// @param distance the signed distance from the centre, in samples.
/// @return the weight, which is one at the centre and zero from four samples out.
[[nodiscard]] inline double lanczos4SamplingWeight(double distance) noexcept
{
    const double x = std::abs(distance);
    if (x == 0.0)
    {
        return 1.0;
    }
    if (x >= 4.0)
    {
        return 0.0;
    }
    const double angle = std::numbers::pi_v<double> * x;
    return (std::sin(angle) / angle) * (std::sin(angle / 4.0) / (angle / 4.0));
}

/// One axis's taps as the contiguous window they always are: where it starts, how long it is, and
/// what each tap weighs.
///
/// The same rule as `SamplingAxis` and the same arithmetic, minus the per-tap index array. A window
/// is contiguous by construction - every rule below places its taps at consecutive integers - so its
/// first index says where all of them are, and a caller that resolves an axis per output sample
/// rather than once per destination row does not carry the sixty-four bytes of an axis's index
/// array. `samplingAxis` is written in terms of this, so the two cannot drift.
struct SamplingWindow
{
    /// The source index of the first tap, which may lie outside the source.
    std::int64_t first = 0;

    /// How many taps the window holds, as `samplingTapCount` reports for its interpolation.
    std::uint32_t count = 0u;

    /// Each tap's normalized weight; entries past `count` are zero.
    std::array<double, 8u> weights{};
};

/// How many taps one interpolation reads along one axis.
[[nodiscard]] inline constexpr std::uint32_t samplingTapCount(KernelInterpolation interpolation) noexcept
{
    switch (interpolation)
    {
    case KernelInterpolation::Nearest:
    case KernelInterpolation::NearestExact:
        return 1u;
    case KernelInterpolation::Cubic:
        return 4u;
    case KernelInterpolation::Lanczos4:
        return 8u;
    default:
        // Area is an integral-footprint rule only when a resize supplies that footprint. A generic
        // coordinate warp has a point and therefore falls back to linear interpolation.
        return 2u;
    }
}

/// Resolves one axis into a caller's tap array and answers where its window begins.
///
/// The one definition of this engine's tap placement and tap weighting; `samplingWindow` and
/// `samplingAxis` are both written in terms of it, so what a warp reading a coordinate per output
/// sample computes and what a resize reading a table computes cannot drift apart. `weights` must
/// have room for `samplingTapCount(interpolation)` entries, and exactly that many are written - a
/// caller in a per-sample loop passes a local array of the count it asked for rather than carrying
/// the widest window's worth of storage through every iteration.
///
/// The weights are normalized to sum to one unless they sum to exactly zero, which leaves them as
/// the kernel gave them.
///
/// @tparam InterpolationT which rule places and weights the taps. `NearestExact` places its tap as
///         `Nearest` does, and `Linear` is the rule for every interpolation with no case of its own.
/// @param coordinate where to read, under the pixel-center convention.
/// @param weights receives the normalized tap weights.
/// @return the source index of the first tap, which may be outside the source.
template <KernelInterpolation InterpolationT>
[[nodiscard]] inline std::int64_t samplingTaps(double coordinate, double* weights) noexcept
{
    constexpr std::uint32_t kCount = samplingTapCount(InterpolationT);
    if constexpr (InterpolationT == KernelInterpolation::Nearest || InterpolationT == KernelInterpolation::NearestExact)
    {
        weights[0] = 1.0;
        return static_cast<std::int64_t>(std::floor(coordinate + 0.5));
    }
    else
    {
        const double floored = std::floor(coordinate);
        const std::int64_t floorCoordinate = static_cast<std::int64_t>(floored);
        std::int64_t first = floorCoordinate;
        if constexpr (InterpolationT == KernelInterpolation::Cubic)
        {
            first = floorCoordinate - 1;
            // The window straddles the centre, so the first and last taps are always in the outer
            // lobe and the middle two always in the inner one - and the two polynomials agree, to
            // the bit, at the distance where the lobes meet. Naming the lobe a tap falls in is
            // therefore the same arithmetic as asking, and it is the same arithmetic on every tap,
            // which is what a warp resolving two of these per output sample needs it to be.
            const double fraction = coordinate - floored;
            weights[0] = cubicOuterLobe(fraction + 1.0);
            weights[1] = cubicInnerLobe(fraction);
            weights[2] = cubicInnerLobe(1.0 - fraction);
            weights[3] = cubicOuterLobe(2.0 - fraction);
        }
        else if constexpr (InterpolationT == KernelInterpolation::Lanczos4)
        {
            first = floorCoordinate - 3;
            for (std::uint32_t tap = 0u; tap < kCount; ++tap)
            {
                weights[tap] =
                    lanczos4SamplingWeight(coordinate - static_cast<double>(first + static_cast<std::int64_t>(tap)));
            }
        }
        else
        {
            const double fraction = coordinate - floored;
            weights[0] = 1.0 - fraction;
            weights[1] = fraction;
        }

        double sum = 0.0;
        for (std::uint32_t tap = 0u; tap < kCount; ++tap)
        {
            sum += weights[tap];
        }
        if (sum != 0.0)
        {
            for (std::uint32_t tap = 0u; tap < kCount; ++tap)
            {
                weights[tap] /= sum;
            }
        }
        return first;
    }
}

/// Resolves one axis for an interpolation named at run time.
///
/// The same taps as the template, selected by a switch: `Area` has no footprint at a point, so it
/// and any interpolation without a case of its own take the linear rule.
///
/// @param coordinate where to read, under the pixel-center convention.
/// @param interpolation which rule places and weights the taps.
/// @param weights receives `samplingTapCount(interpolation)` normalized tap weights.
/// @return the source index of the first tap, which may be outside the source.
[[nodiscard]] inline std::int64_t samplingTaps(double coordinate, KernelInterpolation interpolation,
                                               double* weights) noexcept
{
    switch (interpolation)
    {
    case KernelInterpolation::Nearest:
    case KernelInterpolation::NearestExact:
        return samplingTaps<KernelInterpolation::Nearest>(coordinate, weights);
    case KernelInterpolation::Cubic:
        return samplingTaps<KernelInterpolation::Cubic>(coordinate, weights);
    case KernelInterpolation::Lanczos4:
        return samplingTaps<KernelInterpolation::Lanczos4>(coordinate, weights);
    default:
        return samplingTaps<KernelInterpolation::Linear>(coordinate, weights);
    }
}

/// One axis's taps as a window.
///
/// @param coordinate where to read, under the pixel-center convention.
/// @param interpolation which rule places and weights the taps.
/// @return the window, whose weights past its count are zero.
[[nodiscard]] inline SamplingWindow samplingWindow(double coordinate, KernelInterpolation interpolation) noexcept
{
    SamplingWindow window{};
    window.count = samplingTapCount(interpolation);
    window.first = samplingTaps(coordinate, interpolation, window.weights.data());
    return window;
}

/// One axis's taps with an index per tap, expanded from `samplingWindow`.
///
/// @param coordinate where to read, under the pixel-center convention.
/// @param interpolation which rule places and weights the taps.
/// @return the axis.
[[nodiscard]] inline SamplingAxis samplingAxis(double coordinate, KernelInterpolation interpolation) noexcept
{
    const SamplingWindow window = samplingWindow(coordinate, interpolation);
    SamplingAxis axis{};
    axis.count = window.count;
    axis.weights = window.weights;
    for (std::uint32_t tap = 0u; tap < window.count; ++tap)
    {
        axis.indices[tap] = window.first + static_cast<std::int64_t>(tap);
    }
    return axis;
}

/// One sample of a plane at one coordinate, under the sampling plan's interpolation, border and
/// validity.
///
/// Defined here rather than beside the other traversal helpers so that the loop calling it for
/// every output sample can inline it. It is the only place this engine's interpolated read is
/// written, so the parallel resampler and the scalar oracle agree bitwise by construction rather
/// than by comparison.
///
/// A tap whose weight is exactly zero is skipped. When the accepted weights sum to within one
/// machine epsilon of zero - which is what every tap being rejected under
/// `SamplingInvalidPolicy::Renormalize` leaves - nothing is written.
///
/// @tparam StridedT whether the source is one component of an interleaved plane, read every
///         `plan.pixelStride` elements along a row.
/// @param plan what the operation resolved once.
/// @param coordinate where to read, under the pixel-center convention. It must already have passed
///        `coordinateIsFinite`, which is what makes the tap indices representable.
/// @param output the sample, written only when the call returns true.
/// @return true when the taps the policy requires were accepted and `output` was written.
template <bool StridedT = false>
[[nodiscard]] inline bool sampleInterpolated(const SamplingPlan& plan, const KernelCoordinate& coordinate,
                                             float& output) noexcept
{
    const SamplingAxis xAxis = samplingAxis(coordinate.x, plan.interpolation);
    const SamplingAxis yAxis = samplingAxis(coordinate.y, plan.interpolation);
    std::array<std::int64_t, 8u> resolvedX{};
    for (std::uint32_t xTap = 0u; xTap < xAxis.count; ++xTap)
    {
        resolvedX[xTap] = kernelBorderIndex(xAxis.indices[xTap], plan.width, plan.border);
    }

    double weighted = 0.0;
    double acceptedWeight = 0.0;
    const ValidityAccessor validity{*plan.validity};
    for (std::uint32_t yTap = 0u; yTap < yAxis.count; ++yTap)
    {
        const std::int64_t resolvedY = kernelBorderIndex(yAxis.indices[yTap], plan.height, plan.border);
        const bool outsideY = resolvedY == plan.height;
        const std::uint64_t rowBase = outsideY ? 0u : static_cast<std::uint64_t>(resolvedY) * plan.rowStride;
        for (std::uint32_t xTap = 0u; xTap < xAxis.count; ++xTap)
        {
            const double weight = xAxis.weights[xTap] * yAxis.weights[yTap];
            if (weight == 0.0)
            {
                // Skipped rather than added: a zero weight against a non-finite sample is a NaN,
                // and an exactly-integral coordinate is the ordinary way to produce one.
                continue;
            }
            const bool constant = resolvedX[xTap] == plan.width || outsideY;
            float sample = plan.constantBorder;
            bool accepted = constant;
            if (!constant)
            {
                const std::uint32_t x = static_cast<std::uint32_t>(resolvedX[xTap]);
                sample = plan.samples[rowBase + static_cast<std::uint64_t>(x) * (StridedT ? plan.pixelStride : 1u)];
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

/// Integrates one axis-aligned source-pixel footprint for an area resize.
///
/// Pixel `(x, y)` occupies `[x, x + 1) * [y, y + 1)`. The footprint must be finite, non-empty and
/// contained by the source; geometry planning establishes those facts before execution. Invalid
/// samples follow the same require, renormalize or substitute rule as point interpolation.
///
/// @param plan the source and validity policy resolved for this operation.
/// @param left inclusive source-cell boundary.
/// @param top inclusive source-cell boundary.
/// @param right exclusive source-cell boundary, greater than `left`.
/// @param bottom exclusive source-cell boundary, greater than `top`.
/// @param output the integrated sample, written only on success.
/// @return true when at least one accepted contributor exists and the policy permits the write.
[[nodiscard]] inline bool sampleArea(const SamplingPlan& plan, double left, double top, double right, double bottom,
                                     float& output) noexcept
{
    const std::int64_t xBegin = (std::max)(std::int64_t{0}, static_cast<std::int64_t>(std::floor(left)));
    const std::int64_t yBegin = (std::max)(std::int64_t{0}, static_cast<std::int64_t>(std::floor(top)));
    const std::int64_t xEnd = (std::min)(plan.width, static_cast<std::int64_t>(std::ceil(right)));
    const std::int64_t yEnd = (std::min)(plan.height, static_cast<std::int64_t>(std::ceil(bottom)));
    const ValidityAccessor validity{*plan.validity};
    double weighted = 0.0;
    double acceptedWeight = 0.0;
    for (std::int64_t y = yBegin; y < yEnd; ++y)
    {
        const double yWeight = (std::min)(bottom, static_cast<double>(y + 1)) - (std::max)(top, static_cast<double>(y));
        if (yWeight <= 0.0)
        {
            continue;
        }
        const std::uint64_t rowBase = static_cast<std::uint64_t>(y) * plan.rowStride;
        for (std::int64_t x = xBegin; x < xEnd; ++x)
        {
            const double xWeight =
                (std::min)(right, static_cast<double>(x + 1)) - (std::max)(left, static_cast<double>(x));
            const double weight = xWeight * yWeight;
            if (weight <= 0.0)
            {
                continue;
            }
            const std::uint32_t sampleX = static_cast<std::uint32_t>(x);
            const std::uint32_t sampleY = static_cast<std::uint32_t>(y);
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

/// The sample at a resolved pixel.
///
/// @param source the plane.
/// @param coordinate a pixel inside the plane; one reported outside must not be passed.
/// @return the stored sample.
[[nodiscard]] float sampleAt(PlaneView<const float> source, const ResolvedCoordinate& coordinate) noexcept;

/// A strict total order on Float32 values, for sorting samples that may include not-a-numbers.
///
/// Numbers order by value and come before every not-a-number. Two values that compare equal but
/// differ in their bits - positive and negative zero - and two not-a-numbers are ordered by their
/// bit patterns read as unsigned integers, which puts positive zero first.
///
/// @param left one value.
/// @param right the other.
/// @return true when `left` orders strictly before `right`.
[[nodiscard]] bool totalFloatLess(float left, float right) noexcept;

/// The sample at a row-major index of a plane, and where it is.
///
/// @param source the plane.
/// @param index the pixel's position counted row by row over the plane's width, not its stride;
///        below the plane's sample count.
/// @param x set to the pixel's column.
/// @param y set to the pixel's row.
/// @return the stored sample.
[[nodiscard]] float sourceAt(PlaneView<const float> source, std::uint64_t index, std::uint32_t& x,
                             std::uint32_t& y) noexcept;

/// Whether an earlier selected input of a scatter lands on the same destination pixel.
///
/// The scatter oracle's conflict test: it walks the source samples before `before` in row-major
/// order, skips those the validity rule rejects, rounds each one's coordinate without clamping, and
/// stops at the first that lands on `target`. It is linear in `before`, so the oracle that asks it
/// once per sample is quadratic. Checks for a stop once every 256 samples.
///
/// @param execution the operation.
/// @param source the plane scattered.
/// @param coordinates one destination coordinate per source sample, already checked finite.
/// @param validity the accessor over the source's rule and mask.
/// @param before how many leading samples, in row-major order, to search.
/// @param destinationWidth the destination's width.
/// @param destinationHeight the destination's height.
/// @param target the destination pixel the current sample lands on.
/// @param found set to whether an earlier sample lands there; false when the search was stopped.
/// @return success, or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`.
[[nodiscard]] Status findPriorScatterTarget(const ExecutionContext& execution, PlaneView<const float> source,
                                            std::span<const KernelCoordinate> coordinates,
                                            const ValidityAccessor& validity, std::uint64_t before,
                                            std::uint32_t destinationWidth, std::uint32_t destinationHeight,
                                            const ResolvedCoordinate& target, bool& found);

} // namespace an::reference_detail
