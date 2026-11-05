// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the deconvolution domain's public surface: the point spread function
// builders, the two plans, and the orchestration that stands between a caller's storage and the two
// solvers.
//
// The orchestration is the same for both methods and is written once. A caller's image is widened
// into channel-major Float32 planes with the validity rule applied at the door, one convolution
// engine is built for the whole operation, the frame is optionally tapered, the method runs over
// either every channel or one luminance channel, and the answer is written back through the stated
// conversion with a rejected sample's own destination left untouched. A tightly packed planar
// Float32 destination under a rule that rejects nothing is itself the answer frame: the solver
// writes there, and the write-back only counts what changed.
//
// The working planes are one block of context memory and the convolution engine holds one more, both
// taken before the first sample is read. The planes are sized from the method and the request, so a
// plane nothing will address is never taken and never first touched.
//
// What the caller gets back: how many iterations actually ran, whether a stopping rule ended them,
// how far the last one moved, and what the flux correction had to multiply by. The last of those is
// the honest early warning that the point spread function does not fit the frame.

#include <AfterNightEngine/Algorithms/Deconvolution/Deconvolution.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/deconvolution/DeconvolutionInternal.h"
#include "kernels/reference/SpatialPlane.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <utility>

namespace an
{
namespace
{

using deconvolution_detail::ConvolutionEngine;
using deconvolution_detail::WorkingPlane;
using spatial_detail::SpatialGeometry;

constexpr const char* kPsfExtentOperation = "an::psfExtent";
constexpr const char* kBuildPsfOperation = "an::buildPsf";
constexpr const char* kCheckPsfOperation = "an::checkPsf";
constexpr const char* kRichardsonLucyPlanOperation = "an::planRichardsonLucy";
constexpr const char* kWienerPlanOperation = "an::planWiener";
constexpr const char* kRichardsonLucyOperation = "an::deconvolveImageRichardsonLucy";

/// What a Richardson-Lucy deconvolution says it is doing, once per iteration.
constexpr const char* kRichardsonLucyStage = "Richardson-Lucy deconvolution: iterations";
constexpr const char* kWienerOperation = "an::deconvolveImageWiener";

/// The loose working planes the orchestration can hold beyond the two frames.
///
/// Which of them exist depends on the method and the request, because a plane that is allocated is
/// also a plane that is first touched: a Wiener filter over a monochrome frame with no taper writes
/// straight into its answer frame and needs none of these six, while taking all six anyway costs a
/// one-megapixel frame twenty-four megabytes of page faults it never reads.
enum class WorkingPlaneSlot : std::uint32_t
{
    /// The tapered observation, when a taper runs.
    Tapered = 0u,

    /// The estimate, which every method writes its answer into.
    Estimate = 1u,

    /// The estimate under construction, which only an iterative method needs.
    Next = 2u,

    /// The blur, which an iterative method and a taper both need.
    Blur = 3u,

    /// The correction, on the same footing as the blur.
    Correction = 4u,

    /// The luminance a chroma-preserving mode deconvolves.
    ///
    /// Deliberately the last enumerator, so a count needs no table.
    Luminance = 5u
};

/// How many loose working planes exist at most.
constexpr std::uint32_t kWorkingPlaneCount = static_cast<std::uint32_t>(WorkingPlaneSlot::Luminance) + 1u;

/// At or above this kernel area the transform repays its own overhead.
///
/// Thirty-one squared: below it the direct sum is a few hundred multiplies per sample and the
/// transform's padding and two passes are not worth paying for.
constexpr std::uint64_t kTransformKernelArea = 31u * 31u;

/// The smallest positive flux a Richardson-Lucy rescaling divides by, and the smallest luminance the
/// chroma-preserving mode forms a ratio against.
constexpr double kFluxFloor = 1.0e-12;

/// Rounds a byte count up to the alignment every plane starts at.
///
/// @param bytes what a plane needs.
/// @return what it occupies.
[[nodiscard]] constexpr std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) / kDefaultBufferAlignmentBytes * kDefaultBufferAlignmentBytes;
}

/// Whether a number is finite and above zero.
///
/// @param value the number.
/// @return true when it can be a width, a guard or a step.
[[nodiscard]] bool isPositiveFinite(double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// Whether a finite value survives narrowing into the solver's representation.
///
/// @param value the value.
/// @return true when Float32 carries it without an infinity.
[[nodiscard]] bool fitsFiniteFloat(double value) noexcept
{
    constexpr double kMaximum = static_cast<double>((std::numeric_limits<float>::max)());
    return std::isfinite(value) && value >= -kMaximum && value <= kMaximum;
}

/// Whether a positive finite value survives narrowing into the solver's representation.
///
/// @param value the value.
/// @return true when Float32 carries it as a value above zero.
[[nodiscard]] bool fitsPositiveFloat(double value) noexcept
{
    constexpr double kMinimum = static_cast<double>((std::numeric_limits<float>::denorm_min)());
    return fitsFiniteFloat(value) && value >= kMinimum;
}

/// Whether a number is finite and not negative.
///
/// @param value the number.
/// @return true when it can be a strength, a radius or a threshold.
[[nodiscard]] bool isNonNegativeFinite(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0;
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

/// The odd side a request implies, checked before any floating-to-integer conversion.
///
/// @param request the profile and the widths.
/// @return the side, which is always odd, or an out-of-range failure.
[[nodiscard]] Result<std::uint32_t> derivedPsfExtent(const PsfRequest& request)
{
    if (request.profile == PsfProfile::Gaussian)
    {
        const double sigma = (std::max)(request.fwhmX, request.fwhmY) / kFwhmPerSigma;
        constexpr double kMaximumRadius = static_cast<double>((kMaximumPsfExtent - 1u) / 2u);
        if (!std::isfinite(sigma) || sigma > kMaximumRadius / 4.0)
        {
            return deconvolution_detail::deconvolutionLimitFailure(
                ErrorCode::OutOfRange, kPsfExtentOperation, "the derived Gaussian kernel is wider than the maximum",
                (std::numeric_limits<std::uint64_t>::max)(), kMaximumPsfExtent);
        }
        const auto radius = static_cast<std::uint32_t>((std::max)(2.0, std::ceil(sigma * 4.0)));
        return radius * 2u + 1u;
    }
    const double widest = (std::max)(request.fwhmX, request.fwhmY);
    if (widest > static_cast<double>(kMaximumPsfExtent) / 4.0)
    {
        return deconvolution_detail::deconvolutionLimitFailure(
            ErrorCode::OutOfRange, kPsfExtentOperation, "the derived Moffat kernel is wider than the maximum",
            (std::numeric_limits<std::uint64_t>::max)(), kMaximumPsfExtent);
    }
    auto size = static_cast<std::uint32_t>((std::max)(3.0, std::ceil(widest * 4.0)));
    if (size % 2u == 0u)
    {
        ++size;
    }
    return size;
}

/// Whether a policy states an output promise and a write-back this domain can honour.
///
/// @param output the policy.
/// @param operation the entry point the caller is inside.
/// @return success, or the first reason it cannot be honoured.
[[nodiscard]] Status checkOutputPolicy(const SpatialOutputPolicy& output, const char* operation)
{
    if (!isOutputPrecisionContract(output.contract))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "the output contract is not one this engine knows");
    }
    const Status policy = checkConversionPolicy(output.writeBack, operation);
    if (policy.failed())
    {
        return policy;
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, operation,
            "a deconvolution carries its samples across as the numbers they are; a full-scale domain mapping would "
            "make it a deconvolution and a domain change at once");
    }
    return Status::success();
}

/// Whether the fields both methods share are usable.
///
/// @param common the shared fields.
/// @param operation the entry point the caller is inside.
/// @return success, or the first reason the request cannot be honoured.
[[nodiscard]] Status checkCommon(const DeconvolutionCommon& common, const char* operation)
{
    if (!isPositiveFinite(common.whiteLevel))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "a deconvolution needs a finite full scale above zero");
    }
    if (!isPositiveFinite(common.guard) || common.guard < kMinimumDeconvolutionGuard)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "the division guard must be finite and above its minimum");
    }
    if (!isNonNegativeFinite(common.edgeTaperRadius))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "the taper radius must be finite and not negative");
    }
    if (!std::isfinite(common.invalidSample))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "the substitute for a rejected sample must be finite");
    }
    if (!fitsPositiveFloat(common.whiteLevel) || !fitsPositiveFloat(common.guard) ||
        !fitsFiniteFloat(common.invalidSample))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::OutOfRange, operation,
            "a deconvolution full scale, division guard or substitute cannot be represented by Float32");
    }
    if (!kernelBorderResolvesCoordinate(common.border))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, operation,
            "a deconvolution needs a border that resolves a coordinate; a constant outside the frame is the "
            "discontinuity the taper exists to remove");
    }
    if (common.convolution != DeconvolutionConvolution::Automatic &&
        common.convolution != DeconvolutionConvolution::Direct &&
        common.convolution != DeconvolutionConvolution::Transform)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "the convolution path is outside its enumeration");
    }
    if (common.color != DeconvolutionColor::Automatic && common.color != DeconvolutionColor::PerChannel &&
        common.color != DeconvolutionColor::LuminancePreservingChroma)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "the colour mode is outside its enumeration");
    }
    if (common.edgeTaper != DeconvolutionEdgeTaper::Automatic && common.edgeTaper != DeconvolutionEdgeTaper::Off &&
        common.edgeTaper != DeconvolutionEdgeTaper::Manual)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, operation,
                                                          "the taper mode is outside its enumeration");
    }
    if (common.edgeTaperIterations > kMaximumEdgeTaperIterations)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::OutOfRange, operation,
                                                               "the taper runs at most its maximum passes",
                                                               common.edgeTaperIterations, kMaximumEdgeTaperIterations);
    }
    return checkOutputPolicy(common.output, operation);
}

/// How far the taper actually reaches for one kernel.
///
/// @param common the shared fields.
/// @param psfWidth the kernel's width.
/// @param psfHeight the kernel's height.
/// @return the radius, which is zero when no taper runs.
[[nodiscard]] double resolveTaperRadius(const DeconvolutionCommon& common, std::uint32_t psfWidth,
                                        std::uint32_t psfHeight) noexcept
{
    switch (common.edgeTaper)
    {
    case DeconvolutionEdgeTaper::Off:
        return 0.0;
    case DeconvolutionEdgeTaper::Manual:
        return common.edgeTaperRadius;
    case DeconvolutionEdgeTaper::Automatic:
    default:
        return common.edgeTaperRadius > 0.0 ? common.edgeTaperRadius
                                            : static_cast<double>((std::max)(psfWidth, psfHeight));
    }
}

/// Whether the convolutions go through the transform.
///
/// @param common the shared fields.
/// @param psfWidth the kernel's width.
/// @param psfHeight the kernel's height.
/// @return true for the transform path.
[[nodiscard]] bool resolveTransformPath(const DeconvolutionCommon& common, std::uint32_t psfWidth,
                                        std::uint32_t psfHeight) noexcept
{
    switch (common.convolution)
    {
    case DeconvolutionConvolution::Direct:
        return false;
    case DeconvolutionConvolution::Transform:
        return true;
    case DeconvolutionConvolution::Automatic:
    default:
        return static_cast<std::uint64_t>(psfWidth) * psfHeight >= kTransformKernelArea;
    }
}

/// Rechecks the point spread function supplied for execution and binds it to its plan.
///
/// @param psf the kernel supplied now.
/// @param plannedWidth the width checked when the plan was built.
/// @param plannedHeight the height checked when the plan was built.
/// @param operation the execution entry point.
/// @return success, whatever `checkPsf` refuses, or a geometry mismatch.
[[nodiscard]] Status checkPlannedPsf(PlaneView<const float> psf, std::uint32_t plannedWidth,
                                     std::uint32_t plannedHeight, const char* operation)
{
    const Status checked = checkPsf(psf);
    if (checked.failed())
    {
        return checked;
    }
    if (psf.width() != plannedWidth)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::InvalidArgument, operation,
                                                               "the point spread function width is not the plan's",
                                                               psf.width(), plannedWidth);
    }
    if (psf.height() != plannedHeight)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::InvalidArgument, operation,
                                                               "the point spread function height is not the plan's",
                                                               psf.height(), plannedHeight);
    }
    return Status::success();
}

/// The whole working set of one deconvolution.
///
/// One block, carved into source, optional answer and pristine frames plus up to six loose planes by
/// index rather than by a fixed array, so the channel count is whatever the image has rather than
/// whatever a constant here allows. A tightly packed planar Float32 destination under a rule that
/// rejects nothing is the answer frame itself.
struct DeconvolutionWorkspace
{
    /// The block every plane below points into.
    plane_detail::ScopedContextMemory memory{};

    /// How many bytes one plane occupies, including its alignment padding.
    std::uint64_t planeBytes = 0u;

    /// The picture every plane covers.
    std::uint32_t width = 0u;

    /// The picture every plane covers.
    std::uint32_t height = 0u;

    /// How many channels each frame holds.
    std::uint32_t channelCount = 0u;

    /// The caller's first destination byte when its Float32 planes can be addressed directly.
    void* borrowedAnswers = nullptr;

    /// Distance in bytes between borrowed answer planes.
    std::uint64_t borrowedAnswerPlaneStrideBytes = 0u;

    /// Whether a third frame holds the samples as they were gathered.
    bool keepsPristine = false;

    /// How many loose working planes were taken.
    std::uint32_t workingPlanes = 0u;

    /// One plane of the block.
    ///
    /// @param slot which owned plane, below the count taken from the context.
    /// @return the plane.
    [[nodiscard]] WorkingPlane planeAt(std::uint64_t slot) const noexcept
    {
        return WorkingPlane{.base =
                                reinterpret_cast<float*>(static_cast<std::uint8_t*>(memory.data()) + planeBytes * slot),
                            .width = width,
                            .height = height};
    }

    /// The widened source of one channel.
    ///
    /// @param channel which channel.
    /// @return the plane.
    [[nodiscard]] WorkingPlane source(std::uint32_t channel) const noexcept
    {
        return planeAt(channel);
    }

    /// The answer of one channel.
    ///
    /// @param channel which channel.
    /// @return the plane.
    [[nodiscard]] WorkingPlane answer(std::uint32_t channel) const noexcept
    {
        if (borrowedAnswers != nullptr)
        {
            return WorkingPlane{
                .base = reinterpret_cast<float*>(static_cast<std::uint8_t*>(borrowedAnswers) +
                                                 static_cast<std::uint64_t>(channel) * borrowedAnswerPlaneStrideBytes),
                .width = width,
                .height = height};
        }
        return planeAt(static_cast<std::uint64_t>(channelCount) + channel);
    }

    /// The samples of one channel as they were gathered, before any substitution.
    ///
    /// The same plane as the source when no rule can reject anything, because there is then nothing
    /// for a substitution to have changed.
    ///
    /// @param channel which channel.
    /// @return the plane.
    [[nodiscard]] WorkingPlane pristine(std::uint32_t channel) const noexcept
    {
        const std::uint64_t answerPlanes = borrowedAnswers == nullptr ? channelCount : 0u;
        return keepsPristine
                   ? planeAt(static_cast<std::uint64_t>(channelCount) + answerPlanes + workingPlanes + channel)
                   : source(channel);
    }

    /// One of the loose working planes: the tapered observation, the estimate, the next estimate,
    /// the blur, the correction, and the luminance a chroma-preserving mode deconvolves.
    ///
    /// @param slot which one. A slot this workspace did not take yields the first answer plane,
    ///        which is always present and is never read through a slot the method did not ask for.
    /// @return the plane.
    [[nodiscard]] WorkingPlane work(WorkingPlaneSlot slot) const noexcept
    {
        const auto index = static_cast<std::uint32_t>(slot);
        const std::uint64_t answerPlanes = borrowedAnswers == nullptr ? channelCount : 0u;
        return index < workingPlanes ? planeAt(static_cast<std::uint64_t>(channelCount) + answerPlanes + index)
                                     : answer(0u);
    }

    /// Whether the operation writes answers straight into the caller's Float32 planes.
    ///
    /// @return true when no owned answer frame exists.
    [[nodiscard]] bool answersAreBorrowed() const noexcept
    {
        return borrowedAnswers != nullptr;
    }
};

/// Whether a stopping rule ends a run.
///
/// @param request the iterative request.
/// @param change the mean absolute change the iteration made.
/// @param relative the same, against the estimate's mean positive signal.
/// @return true when the run should stop.
[[nodiscard]] bool shouldStop(const RichardsonLucyRequest& request, double change, double relative) noexcept
{
    return (request.stoppingDelta > 0.0 && change <= request.stoppingDelta) ||
           (request.relativeStoppingDelta > 0.0 && relative <= request.relativeStoppingDelta);
}

/// One sample of a plane, with a coordinate resolved against its border rule.
///
/// @param plane the plane.
/// @param x the column, possibly outside.
/// @param y the row, possibly outside.
/// @param border how a coordinate outside is answered.
/// @return the sample.
[[nodiscard]] float resolvedSample(const WorkingPlane& plane, std::int64_t x, std::int64_t y,
                                   KernelBorderMode border) noexcept
{
    const std::uint32_t column = deconvolution_detail::resolveCoordinate(x, plane.width, border);
    const std::uint32_t row = deconvolution_detail::resolveCoordinate(y, plane.height, border);
    return plane.base[static_cast<std::uint64_t>(row) * plane.width + column];
}

/// What divides an iteration's correction, under the stated regularisation.
///
/// Total variation is the backward-difference divergence of the forward-difference gradient divided
/// by its own magnitude, with `guard` keeping that magnitude above zero on a flat patch. The Hessian's
/// Frobenius norm comes from central second differences and is divided by the sample's own magnitude,
/// held at or above `guard`. Both read neighbours through the request's border rule.
///
/// @param estimate the current estimate.
/// @param x the column.
/// @param y the row.
/// @param request the iterative request.
/// @return the divisor, held inside a quarter and four so one iteration cannot travel far.
[[nodiscard]] double regularizationDivisor(const WorkingPlane& estimate, std::uint32_t x, std::uint32_t y,
                                           const RichardsonLucyRequest& request) noexcept
{
    if (request.regularizationStrength <= 0.0 || request.regularization == DeconvolutionRegularization::None)
    {
        return 1.0;
    }
    const KernelBorderMode border = request.common.border;
    const auto column = static_cast<std::int64_t>(x);
    const auto row = static_cast<std::int64_t>(y);
    const double guard = request.common.guard;

    if (request.regularization == DeconvolutionRegularization::TotalVariation)
    {
        const auto gradientTerm = [&](std::int64_t px, std::int64_t py, bool horizontal) noexcept {
            const double centre = resolvedSample(estimate, px, py, border);
            const double gx = resolvedSample(estimate, px + 1, py, border) - centre;
            const double gy = resolvedSample(estimate, px, py + 1, border) - centre;
            const double magnitude = std::sqrt(gx * gx + gy * gy + guard * guard);
            return (horizontal ? gx : gy) / magnitude;
        };
        const double divergence = gradientTerm(column, row, true) - gradientTerm(column - 1, row, true) +
                                  gradientTerm(column, row, false) - gradientTerm(column, row - 1, false);
        return std::clamp(1.0 - request.regularizationStrength * divergence, 0.25, 4.0);
    }

    const double centre = resolvedSample(estimate, column, row, border);
    const double gxx = resolvedSample(estimate, column + 1, row, border) - 2.0 * centre +
                       resolvedSample(estimate, column - 1, row, border);
    const double gyy = resolvedSample(estimate, column, row + 1, border) - 2.0 * centre +
                       resolvedSample(estimate, column, row - 1, border);
    const double gxy =
        (resolvedSample(estimate, column + 1, row + 1, border) - resolvedSample(estimate, column + 1, row - 1, border) -
         resolvedSample(estimate, column - 1, row + 1, border) +
         resolvedSample(estimate, column - 1, row - 1, border)) *
        0.25;
    const double curvature = std::sqrt(gxx * gxx + 2.0 * gxy * gxy + gyy * gyy);
    const double signal = (std::max)(std::abs(centre), guard);
    return std::clamp(1.0 + request.regularizationStrength * curvature / signal, 0.25, 4.0);
}

/// Compresses a correction towards one.
///
/// @param correction the ratio the iteration produced.
/// @param damping how far a correction may stand from one before it is compressed, or zero.
/// @return the compressed correction.
[[nodiscard]] double dampCorrection(double correction, double damping) noexcept
{
    if (damping <= 0.0)
    {
        return correction;
    }
    const double offset = correction - 1.0;
    const double limit = (std::max)(damping, 1.0e-6);
    return 1.0 + offset / (1.0 + std::abs(offset) / limit);
}

/// Moves one estimate along one correction.
///
/// @param estimate the current value.
/// @param correction the regularised correction.
/// @param request the iterative request.
/// @return the new value.
[[nodiscard]] double applyUpdate(double estimate, double correction, const RichardsonLucyRequest& request) noexcept
{
    if (request.update == DeconvolutionUpdate::GradientDescent)
    {
        return estimate + request.gradientDescentStep * (correction - 1.0);
    }
    return estimate * correction;
}

/// Rescales a plane so its positive flux matches a target.
///
/// A plane or a target at or below `guard` is left alone, and a product that is not finite in
/// Float32 is stored as zero.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the plane to rescale.
/// @param target the flux to restore.
/// @param guard the smallest flux worth dividing by.
/// @param applied what the plane was multiplied by, written when a rescaling happened.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status restoreFlux(const ExecutionContext& execution, const WorkingPlane& plane, double target,
                                 double guard, double& applied)
{
    Result<double> current = deconvolution_detail::positiveSum(execution, plane);
    if (!current.hasValue())
    {
        return current.status();
    }
    if (*current <= guard || target <= guard)
    {
        return Status::success();
    }
    const double scale = target / *current;
    applied = scale;
    const auto factor = static_cast<float>(scale);
    const std::uint32_t width = plane.width;
    float* const base = plane.base;
    return parallelFor(execution, plane.height, rowCost(width, 1u), [&](TaskRange range) -> Status {
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            float* const line = base + row * width;
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const float value = line[x] * factor;
                line[x] = std::isfinite(value) ? value : 0.0f;
            }
        }
        return Status::success();
    });
}

/// Runs the iterative solver over one plane.
///
/// The estimate starts as the observation with every non-finite or non-positive sample, and every
/// sample below `guard`, raised to `guard`. Each iteration blurs it, divides the observation by the
/// blur held at or above `guard`, blurs that ratio by the mirrored kernel, damps and regularises the
/// correction per sample, and applies it in Float64 before storing the new estimate in Float32; the
/// mean absolute change is summed in Float64 across the rows. An observation whose positive flux is
/// at or below `guard` has nothing to recover: the answer is zeros and the progress is completed.
///
/// @param execution the operation policy and engine-context lease.
/// @param target the observed plane, already tapered.
/// @param workspace the working planes: the estimate, the next estimate, the blur and the correction.
/// @param engine the convolution.
/// @param request the iterative request.
/// @param result where the iteration counts and the stopping evidence go.
/// @param restored where the answer ends up, written on return.
/// @return success, or an execution, cancellation or transform-library failure.
[[nodiscard]] Status runRichardsonLucy(const ExecutionContext& execution, const WorkingPlane& target,
                                       std::span<WorkingPlane> workspace, const ConvolutionEngine& engine,
                                       const RichardsonLucyRequest& request, DeconvolutionResult& result,
                                       WorkingPlane& restored)
{
    WorkingPlane estimate = workspace[0];
    WorkingPlane next = workspace[1];
    const WorkingPlane blurred = workspace[2];
    const WorkingPlane correction = workspace[3];
    const std::uint64_t count = target.sampleCount();
    const double guard = request.common.guard;

    Result<double> observedFlux = deconvolution_detail::positiveSum(execution, target);
    if (!observedFlux.hasValue())
    {
        return observedFlux.status();
    }
    if (*observedFlux <= guard)
    {
        std::fill_n(estimate.base, count, 0.0f);
        restored = estimate;
        // Nothing to iterate on is every iteration done, and a display that was told how many there
        // would be ends full rather than where it started.
        execution.reportProgress(request.iterations, request.iterations, kRichardsonLucyStage);
        return Status::success();
    }

    const auto floor = static_cast<float>(guard);
    const std::uint32_t width = target.width;
    const std::uint32_t height = target.height;
    std::uint32_t iterationsRun = 0u;
    bool stoppedEarly = false;
    const Status seeded = parallelFor(execution, height, rowCost(width, 2u), [&](TaskRange range) -> Status {
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const float* const source = target.base + row * width;
            float* const line = estimate.base + row * width;
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const float value = source[x];
                line[x] = (std::max)(floor, std::isfinite(value) && value > 0.0f ? value : 0.0f);
            }
        }
        return Status::success();
    });
    if (seeded.failed())
    {
        return seeded;
    }
    for (std::uint32_t iteration = 0u; iteration < request.iterations; ++iteration)
    {
        const Status cancelled = execution.checkCancellation();
        if (cancelled.failed())
        {
            return cancelled;
        }
        const Status blur =
            engine.convolve(execution, estimate, blurred, false, engine.usesTransform(), kRichardsonLucyOperation);
        if (blur.failed())
        {
            return blur;
        }
        const Status divided = parallelFor(execution, height, rowCost(width, 4u), [&](TaskRange range) -> Status {
            for (std::uint64_t row = range.begin; row < range.end; ++row)
            {
                const float* const observed = target.base + row * width;
                float* const line = blurred.base + row * width;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    line[x] = observed[x] / (std::max)(line[x], floor);
                }
            }
            return Status::success();
        });
        if (divided.failed())
        {
            return divided;
        }
        const Status back =
            engine.convolve(execution, blurred, correction, true, engine.usesTransform(), kRichardsonLucyOperation);
        if (back.failed())
        {
            return back;
        }

        const auto body = [&](TaskRange range, double& partial) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const float* const corrections = correction.base + y * width;
                const float* const current = estimate.base + y * width;
                float* const target32 = next.base + y * width;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double damped = dampCorrection(static_cast<double>(corrections[x]), request.damping);
                    const double divisor = regularizationDivisor(estimate, x, static_cast<std::uint32_t>(y), request);
                    double value = applyUpdate(static_cast<double>(current[x]), damped / divisor, request);
                    if (request.common.nonNegative)
                    {
                        value = (std::max)(0.0, value);
                    }
                    const auto stored = static_cast<float>(value);
                    target32[x] = stored;
                    partial += std::abs(static_cast<double>(stored) - static_cast<double>(current[x]));
                }
            }
            return Status::success();
        };
        Result<double> moved = parallelReduce(execution, height, rowCost(width, 40u), 0.0, body,
                                              [](double left, double right) noexcept { return left + right; });
        if (!moved.hasValue())
        {
            return moved.status();
        }
        Result<double> previousSignal = deconvolution_detail::positiveSum(execution, estimate);
        if (!previousSignal.hasValue())
        {
            return previousSignal.status();
        }
        const double change = *moved / static_cast<double>(count);
        const double meanSignal = *previousSignal / static_cast<double>(count);
        const double relative = change / (std::max)(meanSignal, guard);
        std::swap(estimate, next);
        ++iterationsRun;
        result.lastChange = change;
        result.lastRelativeChange = relative;

        if (request.common.preserveFlux)
        {
            const Status rescaled = restoreFlux(execution, estimate, *observedFlux, kFluxFloor, result.fluxScale);
            if (rescaled.failed())
            {
                return rescaled;
            }
        }
        execution.reportProgress(iterationsRun, request.iterations, kRichardsonLucyStage);
        if (shouldStop(request, change, relative))
        {
            stoppedEarly = true;
            break;
        }
    }
    // A loop that converged before its last iteration has still finished the work it was asked for.
    execution.reportProgress(request.iterations, request.iterations, kRichardsonLucyStage);
    result.iterationsRun = (std::max)(result.iterationsRun, iterationsRun);
    result.stoppedEarly = result.stoppedEarly || stoppedEarly;
    restored = estimate;
    return Status::success();
}

/// Takes the whole working set of one deconvolution from the engine context.
///
/// @param execution the operation policy and engine-context lease.
/// @param geometry the picture both sides agree about.
/// @param keepsPristine whether a third frame holds the samples as they were gathered.
/// @param workingPlanes how many of the loose planes this method will address.
/// @param borrowedAnswers the caller's directly addressable Float32 destination, or null.
/// @param borrowedAnswerPlaneStrideBytes distance in bytes between its planes.
/// @param operation the name carried by any failure.
/// @return the workspace, or the memory resource's refusal.
[[nodiscard]] Result<DeconvolutionWorkspace> takeWorkspace(const ExecutionContext& execution,
                                                           const SpatialGeometry& geometry, bool keepsPristine,
                                                           std::uint32_t workingPlanes, void* borrowedAnswers,
                                                           std::uint64_t borrowedAnswerPlaneStrideBytes,
                                                           const char* operation)
{
    const std::uint64_t planeBytes =
        alignedBytes(static_cast<std::uint64_t>(geometry.width) * geometry.height * sizeof(float));
    const std::uint64_t frameCount = 1u + (borrowedAnswers == nullptr ? 1u : 0u) + (keepsPristine ? 1u : 0u);
    const std::uint64_t planes = frameCount * static_cast<std::uint64_t>(geometry.channelCount) + workingPlanes;
    Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
        execution, planeBytes * planes, kDefaultBufferAlignmentBytes, operation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    DeconvolutionWorkspace workspace;
    workspace.memory = std::move(taken).value();
    workspace.planeBytes = planeBytes;
    workspace.width = geometry.width;
    workspace.height = geometry.height;
    workspace.channelCount = geometry.channelCount;
    workspace.borrowedAnswers = borrowedAnswers;
    workspace.borrowedAnswerPlaneStrideBytes = borrowedAnswerPlaneStrideBytes;
    workspace.keepsPristine = keepsPristine;
    workspace.workingPlanes = workingPlanes;
    return workspace;
}

/// A Float32 plane view over one working plane.
///
/// @param plane the plane.
/// @return the view, which always exists for a plane that describes anything.
[[nodiscard]] std::optional<PlaneView<float>> viewOf(const WorkingPlane& plane) noexcept
{
    return PlaneView<float>::create(std::span<float>{plane.base, static_cast<std::size_t>(plane.sampleCount())},
                                    plane.width, plane.height, 1u,
                                    static_cast<std::uint64_t>(plane.width) * sizeof(float));
}

/// Holds every sample of a plane inside what the request promises.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the plane, changed in place.
/// @param common the shared fields.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status holdInRange(const ExecutionContext& execution, const WorkingPlane& plane,
                                 const DeconvolutionCommon& common)
{
    const auto ceiling = static_cast<float>(common.whiteLevel);
    const std::uint32_t width = plane.width;
    float* const base = plane.base;
    const bool nonNegative = common.nonNegative;
    const bool clamp = common.clampToWhite;
    return parallelFor(execution, plane.height, rowCost(width, 3u), [&](TaskRange range) -> Status {
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            float* const line = base + row * width;
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                float value = line[x];
                if (!std::isfinite(value))
                {
                    value = 0.0f;
                }
                if (nonNegative)
                {
                    value = (std::max)(0.0f, value);
                }
                if (clamp)
                {
                    value = (std::min)(ceiling, value);
                }
                line[x] = value;
            }
        }
        return Status::success();
    });
}

} // namespace

Result<std::uint32_t> psfExtent(const PsfRequest& request)
{
    if (request.profile != PsfProfile::Gaussian && request.profile != PsfProfile::Moffat)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kPsfExtentOperation,
                                                          "the profile is outside its enumeration");
    }
    if (!isPositiveFinite(request.fwhmX) || !isPositiveFinite(request.fwhmY))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, kPsfExtentOperation,
            "a point spread function needs finite widths above zero in both axes");
    }
    if (!std::isfinite(request.angleDegrees))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kPsfExtentOperation,
                                                          "the profile's rotation must be finite");
    }
    if (request.profile == PsfProfile::Moffat && (!std::isfinite(request.beta) || request.beta <= 1.0))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, kPsfExtentOperation,
            "a Moffat exponent at or below one describes a profile with no finite integral");
    }
    if (request.size != 0u && request.size % 2u == 0u)
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, kPsfExtentOperation,
            "a stated kernel side must be odd so the source has a centre");
    }
    std::uint64_t size = request.size;
    if (size == 0u)
    {
        Result<std::uint32_t> derived = derivedPsfExtent(request);
        if (!derived.hasValue())
        {
            return derived.status();
        }
        size = *derived;
    }
    if (size < kMinimumPsfExtent)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::OutOfRange, kPsfExtentOperation,
                                                               "a kernel narrower than the minimum has no shape", size,
                                                               kMinimumPsfExtent);
    }
    if (size > kMaximumPsfExtent)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::OutOfRange, kPsfExtentOperation,
                                                               "a kernel wider than the maximum is refused", size,
                                                               kMaximumPsfExtent);
    }
    return static_cast<std::uint32_t>(size);
}

Status buildPsf(const PsfRequest& request, PlaneView<float> destination)
{
    Result<std::uint32_t> extent = psfExtent(request);
    if (!extent.hasValue())
    {
        return extent.status();
    }
    const std::uint32_t size = *extent;
    if (destination.width() != size || destination.height() != size)
    {
        return deconvolution_detail::deconvolutionLimitFailure(
            ErrorCode::InvalidArgument, kBuildPsfOperation,
            "the destination must be the square plane this request implies", destination.width(), size);
    }

    const double angle = std::remainder(request.angleDegrees, 360.0) * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double centre = static_cast<double>(size / 2u);
    double inverseX = 0.0;
    double inverseY = 0.0;
    if (request.profile == PsfProfile::Gaussian)
    {
        const double sigmaX = request.fwhmX / kFwhmPerSigma;
        const double sigmaY = request.fwhmY / kFwhmPerSigma;
        inverseX = 1.0 / (2.0 * sigmaX * sigmaX);
        inverseY = 1.0 / (2.0 * sigmaY * sigmaY);
    }
    else
    {
        const double widthScale = 2.0 * std::sqrt(std::pow(2.0, 1.0 / request.beta) - 1.0);
        const double alphaX = request.fwhmX / widthScale;
        const double alphaY = request.fwhmY / widthScale;
        inverseX = 1.0 / (alphaX * alphaX);
        inverseY = 1.0 / (alphaY * alphaY);
    }

    double total = 0.0;
    for (std::uint32_t y = 0u; y < size; ++y)
    {
        const std::optional<std::span<float>> row = destination.row(y);
        if (!row.has_value())
        {
            return deconvolution_detail::deconvolutionFailure(ErrorCode::Internal, kBuildPsfOperation,
                                                              "the destination plane has an unreachable row");
        }
        const double dy = static_cast<double>(y) - centre;
        for (std::uint32_t x = 0u; x < size; ++x)
        {
            const double dx = static_cast<double>(x) - centre;
            const double rotatedX = dx * cosine + dy * sine;
            const double rotatedY = -dx * sine + dy * cosine;
            const double radial = rotatedX * rotatedX * inverseX + rotatedY * rotatedY * inverseY;
            const double value =
                request.profile == PsfProfile::Gaussian ? std::exp(-radial) : std::pow(1.0 + radial, -request.beta);
            (*row)[x] = static_cast<float>(value);
            total += value;
        }
    }
    if (!(total > 0.0) || !std::isfinite(total))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidData, kBuildPsfOperation,
                                                          "this profile produced a kernel with no positive energy");
    }
    const auto normalization = static_cast<float>(1.0 / total);
    for (std::uint32_t y = 0u; y < size; ++y)
    {
        const std::span<float> row = *destination.row(y);
        for (std::uint32_t x = 0u; x < size; ++x)
        {
            row[x] *= normalization;
        }
    }
    return Status::success();
}

Status checkPsf(PlaneView<const float> psf)
{
    const std::uint32_t width = psf.width();
    const std::uint32_t height = psf.height();
    if (width % 2u == 0u || height % 2u == 0u)
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, kCheckPsfOperation,
            "a point spread function needs odd sides so the source it describes has a centre");
    }
    if (width < kMinimumPsfExtent || height < kMinimumPsfExtent)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::OutOfRange, kCheckPsfOperation,
                                                               "a kernel narrower than the minimum has no shape",
                                                               (std::min)(width, height), kMinimumPsfExtent);
    }
    if (width > kMaximumPsfExtent || height > kMaximumPsfExtent)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::OutOfRange, kCheckPsfOperation,
                                                               "a kernel wider than the maximum is refused",
                                                               (std::max)(width, height), kMaximumPsfExtent);
    }
    double total = 0.0;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const std::optional<std::span<const float>> row = psf.row(y);
        if (!row.has_value())
        {
            return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kCheckPsfOperation,
                                                              "this kernel has an unreadable row");
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const float value = (*row)[x];
            if (!std::isfinite(value) || value < 0.0f)
            {
                return deconvolution_detail::deconvolutionFailure(
                    ErrorCode::InvalidArgument, kCheckPsfOperation,
                    "a point spread function's samples are finite and not negative");
            }
            total += static_cast<double>(value);
        }
    }
    if (!(total > 0.0) || !std::isfinite(total))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidData, kCheckPsfOperation,
                                                          "a point spread function with no positive energy cannot be "
                                                          "normalised");
    }
    return Status::success();
}

RichardsonLucyPlan::RichardsonLucyPlan(const RichardsonLucyRequest& request, std::uint32_t psfWidth,
                                       std::uint32_t psfHeight) noexcept
    : m_request(request), m_taperRadius(resolveTaperRadius(request.common, psfWidth, psfHeight)),
      m_usesTransform(resolveTransformPath(request.common, psfWidth, psfHeight)), m_psfWidth(psfWidth),
      m_psfHeight(psfHeight), m_valid(true)
{
}

const RichardsonLucyRequest& RichardsonLucyPlan::request() const noexcept
{
    return m_request;
}

bool RichardsonLucyPlan::usesTransform() const noexcept
{
    return m_usesTransform;
}

std::uint32_t RichardsonLucyPlan::psfWidth() const noexcept
{
    return m_psfWidth;
}

std::uint32_t RichardsonLucyPlan::psfHeight() const noexcept
{
    return m_psfHeight;
}

double RichardsonLucyPlan::taperRadius() const noexcept
{
    return m_taperRadius;
}

std::uint64_t RichardsonLucyPlan::imagePasses() const noexcept
{
    if (!m_valid)
    {
        return 0u;
    }
    const std::uint64_t iterations = 2u * static_cast<std::uint64_t>(m_request.iterations);
    return iterations + (m_taperRadius > 0.0 ? m_request.common.edgeTaperIterations : 0u);
}

WienerPlan::WienerPlan(const WienerRequest& request, std::uint32_t psfWidth, std::uint32_t psfHeight) noexcept
    : m_request(request), m_taperRadius(resolveTaperRadius(request.common, psfWidth, psfHeight)),
      m_taperUsesTransform(m_taperRadius > 0.0 && request.common.edgeTaperIterations > 0u &&
                           resolveTransformPath(request.common, psfWidth, psfHeight)),
      m_psfWidth(psfWidth), m_psfHeight(psfHeight), m_valid(true)
{
}

const WienerRequest& WienerPlan::request() const noexcept
{
    return m_request;
}

std::uint32_t WienerPlan::psfWidth() const noexcept
{
    return m_psfWidth;
}

std::uint32_t WienerPlan::psfHeight() const noexcept
{
    return m_psfHeight;
}

bool WienerPlan::taperUsesTransform() const noexcept
{
    return m_taperUsesTransform;
}

double WienerPlan::taperRadius() const noexcept
{
    return m_taperRadius;
}

std::uint64_t WienerPlan::imagePasses() const noexcept
{
    if (!m_valid)
    {
        return 0u;
    }
    return 1u + (m_taperRadius > 0.0 ? m_request.common.edgeTaperIterations : 0u);
}

Result<RichardsonLucyPlan> planRichardsonLucy(const RichardsonLucyRequest& request, PlaneView<const float> psf)
{
    const Status kernel = checkPsf(psf);
    if (kernel.failed())
    {
        return kernel;
    }
    const Status common = checkCommon(request.common, kRichardsonLucyPlanOperation);
    if (common.failed())
    {
        return common;
    }
    if (request.iterations == 0u || request.iterations > kMaximumDeconvolutionIterations)
    {
        return deconvolution_detail::deconvolutionLimitFailure(ErrorCode::OutOfRange, kRichardsonLucyPlanOperation,
                                                               "the run makes between one and its maximum iterations",
                                                               request.iterations, kMaximumDeconvolutionIterations);
    }
    if (request.regularization != DeconvolutionRegularization::None &&
        request.regularization != DeconvolutionRegularization::TotalVariation &&
        request.regularization != DeconvolutionRegularization::HessianFrobenius)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kRichardsonLucyPlanOperation,
                                                          "the regularisation is outside its enumeration");
    }
    if (request.update != DeconvolutionUpdate::Multiplicative && request.update != DeconvolutionUpdate::GradientDescent)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kRichardsonLucyPlanOperation,
                                                          "the update method is outside its enumeration");
    }
    if (!isNonNegativeFinite(request.regularizationStrength) || !isNonNegativeFinite(request.damping) ||
        !isNonNegativeFinite(request.stoppingDelta) || !isNonNegativeFinite(request.relativeStoppingDelta))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, kRichardsonLucyPlanOperation,
            "the regularisation strength, the damping and the stopping thresholds must be finite and not negative");
    }
    if (request.update == DeconvolutionUpdate::GradientDescent && !isPositiveFinite(request.gradientDescentStep))
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kRichardsonLucyPlanOperation,
                                                          "a gradient-descent step must be finite and above zero");
    }
    if (!fitsFiniteFloat(request.regularizationStrength) || !fitsFiniteFloat(request.damping) ||
        !fitsFiniteFloat(request.stoppingDelta) || !fitsFiniteFloat(request.relativeStoppingDelta) ||
        (request.update == DeconvolutionUpdate::GradientDescent && !fitsPositiveFloat(request.gradientDescentStep)))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::OutOfRange, kRichardsonLucyPlanOperation,
            "a Richardson-Lucy numeric field cannot be represented by the Float32 solver");
    }
    return RichardsonLucyPlan{request, psf.width(), psf.height()};
}

Result<WienerPlan> planWiener(const WienerRequest& request, PlaneView<const float> psf)
{
    const Status kernel = checkPsf(psf);
    if (kernel.failed())
    {
        return kernel;
    }
    const Status common = checkCommon(request.common, kWienerPlanOperation);
    if (common.failed())
    {
        return common;
    }
    if (!isPositiveFinite(request.regularization))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, kWienerPlanOperation,
            "a Wiener filter needs a finite noise-to-signal ratio above zero, which is what keeps its divisor away "
            "from zero");
    }
    if (!fitsPositiveFloat(request.regularization))
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::OutOfRange, kWienerPlanOperation,
            "the Wiener regularisation cannot be represented by the Float32 solver");
    }
    return WienerPlan{request, psf.width(), psf.height()};
}

namespace
{

/// Runs one deconvolution, whichever method it is.
///
/// The shape both methods share: widen, taper, solve, hold in range, write back. The method itself
/// is a callable so that the two solvers differ only in what they do between a tapered plane and a
/// restored one.
///
/// @tparam SolverT a callable invoked as `solver(const WorkingPlane& target, std::span<WorkingPlane>
///         scratch, const ConvolutionEngine&, WorkingPlane& restored)` returning a `Status`.
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the observed image's samples start.
/// @param sourceLayout where they sit.
/// @param destination where the recovered samples go.
/// @param destinationLayout where they sit.
/// @param psf the point spread function.
/// @param common the shared fields.
/// @param useTransform whether the convolutions go through the transform.
/// @param taperUsesTransform whether the taper convolution goes through the transform.
/// @param taperRadius how far the taper reaches.
/// @param needs which of the three kernel spectra this method will use.
/// @param iterative whether the solver needs the estimate-under-construction and correction planes.
/// @param solverHoldsRange whether the solver already replaces non-finite values and applies the
///        non-negative rule while producing its answer.
/// @param selection which samples mean something.
/// @param operation the name carried by any failure.
/// @param result what the operation did.
/// @param progressUnits the solver's own progress total for one channel, which becomes one channel's
///        share of the operation's total when channels are solved separately.
/// @param solver what to run between a tapered plane and a restored one.
/// @return success, or the first failure any stage reported.
template <typename SolverT>
[[nodiscard]] Status
runDeconvolution(const ExecutionContext& execution, SampleType sampleType, const void* source,
                 const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                 PlaneView<const float> psf, const DeconvolutionCommon& common, bool useTransform,
                 bool taperUsesTransform, double taperRadius, const deconvolution_detail::ConvolutionEngineNeeds& needs,
                 bool iterative, bool solverHoldsRange, const SpatialSelection& selection, const char* operation,
                 DeconvolutionResult& result, std::uint64_t progressUnits, SolverT&& solver)
{
    SpatialGeometry geometry{};
    const Status checked =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           common.output, selection, operation, geometry);
    if (checked.failed())
    {
        return checked;
    }
    if (geometry.width < psf.width() || geometry.height < psf.height())
    {
        return deconvolution_detail::deconvolutionLimitFailure(
            ErrorCode::OutOfRange, operation, "a deconvolution needs a frame at least as large as its kernel",
            (std::min)(geometry.width, geometry.height), (std::max)(psf.width(), psf.height()));
    }
    const bool luminance = common.color == DeconvolutionColor::LuminancePreservingChroma ||
                           (common.color == DeconvolutionColor::Automatic && geometry.channelCount == 3u);
    if (luminance && geometry.channelCount != 3u)
    {
        return deconvolution_detail::deconvolutionFailure(
            ErrorCode::InvalidArgument, operation,
            "a chroma-preserving deconvolution needs exactly three channels to preserve the chroma of");
    }

    // A rule that can reject a sample needs the frame as it was gathered: the write-back decides what
    // to leave untouched from the samples the caller supplied, not from the substitutes the solver
    // read. A rule that rejects nothing needs no such copy.
    const bool keepsPristine = selection.validity.rule() != ValidityRule::AllValid;
    const bool tapers = taperRadius > 0.0 && common.edgeTaperIterations > 0u;
    // A plane that is taken is a plane that is first touched, so only the ones this method will
    // address are taken: an iterative solver needs the first five, while a single-pass filter over
    // a monochrome frame with no taper writes straight into its answer frame and needs none.
    std::uint32_t workingPlanes = 0u;
    if (iterative || tapers)
    {
        workingPlanes = static_cast<std::uint32_t>(WorkingPlaneSlot::Correction) + 1u;
    }
    if (luminance)
    {
        workingPlanes = kWorkingPlaneCount;
    }
    void* borrowedAnswers = nullptr;
    if (!keepsPristine && sampleType == SampleType::Float32 && destinationLayout.samplesPerPixel() == 1u &&
        destinationLayout.rowStrideBytes() == static_cast<std::uint64_t>(geometry.width) * sizeof(float))
    {
        bool everyPlaneIsAddressable = true;
        for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
        {
            if (!spatial_detail::borrowDestinationPlane(destination, destinationLayout, channel, sampleType, geometry)
                     .has_value())
            {
                everyPlaneIsAddressable = false;
                break;
            }
        }
        if (everyPlaneIsAddressable)
        {
            borrowedAnswers = destination;
        }
    }
    Result<DeconvolutionWorkspace> taken =
        takeWorkspace(execution, geometry, keepsPristine, workingPlanes, borrowedAnswers,
                      borrowedAnswers != nullptr ? destinationLayout.planeStrideBytes() : 0u, operation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    DeconvolutionWorkspace workspace = std::move(taken).value();

    Result<ConvolutionEngine> built = ConvolutionEngine::create(execution, psf, useTransform, geometry.width,
                                                                geometry.height, common.border, needs, operation);
    if (!built.hasValue())
    {
        return built.status();
    }
    const ConvolutionEngine engine = std::move(built).value();

    result.filter.channelCount = geometry.channelCount;
    result.filter.precision = ComputePrecision::Float32;
    result.filter.workingBytes = workspace.memory.bytes() + engine.workingBytes();
    result.filter.run.selection.total =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    result.usedTransform = engine.usesTransform();
    result.taperUsedTransform = tapers && taperUsesTransform;
    result.transformWidth = engine.transformWidth();
    result.transformHeight = engine.transformHeight();
    result.preservedChroma = luminance;

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const std::optional<PlaneView<float>> gatheredPlane = viewOf(workspace.pristine(channel));
        const std::optional<PlaneView<float>> plane = viewOf(workspace.source(channel));
        if (!gatheredPlane.has_value() || !plane.has_value())
        {
            return deconvolution_detail::deconvolutionFailure(ErrorCode::Internal, operation,
                                                              "a working plane does not describe a plane");
        }
        const Status gathered = spatial_detail::gatherChannelPlane(execution, sampleType, source, sourceLayout, channel,
                                                                   *gatheredPlane, operation);
        if (gathered.failed())
        {
            return gathered;
        }
        if (!keepsPristine)
        {
            // Nothing can be rejected, so nothing is substituted and the gathered plane is already
            // the plane the solver reads.
            continue;
        }
        std::copy_n(workspace.pristine(channel).base, workspace.pristine(channel).sampleCount(),
                    workspace.source(channel).base);
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status repaired = spatial_detail::substituteRejectedSamples(execution, *plane, validity,
                                                                          static_cast<float>(common.invalidSample),
                                                                          result.substitutedSamples, operation);
        if (repaired.failed())
        {
            return repaired;
        }
    }

    const WorkingPlane tapered = workspace.work(WorkingPlaneSlot::Tapered);
    std::array<WorkingPlane, 4u> scratchPlanes{
        workspace.work(WorkingPlaneSlot::Estimate), workspace.work(WorkingPlaneSlot::Next),
        workspace.work(WorkingPlaneSlot::Blur), workspace.work(WorkingPlaneSlot::Correction)};
    const std::span<WorkingPlane> scratch{scratchPlanes};
    const WorkingPlane luminancePlane = workspace.work(WorkingPlaneSlot::Luminance);
    const std::uint64_t count = tapered.sampleCount();

    const auto solveOne = [&](const WorkingPlane& observed, WorkingPlane& restored) -> Status {
        const Status prepared = deconvolution_detail::applyEdgeTaper(
            execution, observed, tapered, scratch[2], scratch[3], engine, taperUsesTransform, taperRadius,
            common.edgeTaperIterations, operation);
        if (prepared.failed())
        {
            return prepared;
        }
        return solver(tapers ? tapered : observed, scratch, engine, restored);
    };

    if (!luminance)
    {
        for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
        {
            WorkingPlane restored = workspace.answer(channel);
            // Each channel is one share of the operation's progress, measured in the solver's own units,
            // so its count of iterations fills one channel's share and still moves the bar one iteration
            // at a time rather than jumping at the end of the channel.
            const ProgressRange share(execution, channel * progressUnits, (channel + 1u) * progressUnits,
                                      geometry.channelCount * progressUnits);
            const Status solved = solveOne(workspace.source(channel), restored);
            if (solved.failed())
            {
                return solved;
            }
            if (restored.base != workspace.answer(channel).base)
            {
                std::copy_n(restored.base, count, workspace.answer(channel).base);
            }
            if (!solverHoldsRange || common.clampToWhite)
            {
                const Status held = holdInRange(execution, workspace.answer(channel), common);
                if (held.failed())
                {
                    return held;
                }
            }
        }
    }
    else
    {
        for (std::uint64_t i = 0u; i < count; ++i)
        {
            luminancePlane.base[i] =
                static_cast<float>(kLuminanceRed * static_cast<double>(workspace.source(0u).base[i]) +
                                   kLuminanceGreen * static_cast<double>(workspace.source(1u).base[i]) +
                                   kLuminanceBlue * static_cast<double>(workspace.source(2u).base[i]));
        }
        WorkingPlane restored = workspace.answer(0u);
        const Status solved = solveOne(luminancePlane, restored);
        if (solved.failed())
        {
            return solved;
        }
        // Every channel is scaled by the ratio the luminance moved, so no hue and no saturation
        // changes; a pixel whose luminance was zero has no ratio and takes the restored luminance
        // as a neutral grey.
        for (std::uint64_t i = 0u; i < count; ++i)
        {
            const double target = std::isfinite(restored.base[i]) && restored.base[i] > 0.0f
                                      ? static_cast<double>(restored.base[i])
                                      : 0.0;
            const double observed = static_cast<double>(luminancePlane.base[i]);
            if (observed > kFluxFloor)
            {
                const double scale = target / observed;
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    workspace.answer(channel).base[i] =
                        static_cast<float>(static_cast<double>(workspace.source(channel).base[i]) * scale);
                }
            }
            else
            {
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    workspace.answer(channel).base[i] = static_cast<float>(target);
                }
            }
        }
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const Status held = holdInRange(execution, workspace.answer(channel), common);
            if (held.failed())
            {
                return held;
            }
        }
    }

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const std::optional<PlaneView<float>> answerPlane = viewOf(workspace.answer(channel));
        const std::optional<PlaneView<float>> sourcePlane = viewOf(workspace.pristine(channel));
        if (!answerPlane.has_value() || !sourcePlane.has_value())
        {
            return deconvolution_detail::deconvolutionFailure(ErrorCode::Internal, operation,
                                                              "a working plane does not describe a plane");
        }
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        if (workspace.answersAreBorrowed())
        {
            const Status counted = spatial_detail::countChangedPlane(execution, *answerPlane, *sourcePlane, validity,
                                                                     result.samplesChanged);
            if (counted.failed())
            {
                return counted;
            }
            continue;
        }
        const Status scattered = spatial_detail::scatterChannelPlane(
            execution, *answerPlane, *sourcePlane, validity, sampleType, destination, destinationLayout, channel,
            common.output.writeBack, operation, source, &sourceLayout, &result.samplesChanged);
        if (scattered.failed())
        {
            return scattered;
        }
    }
    result.filter.run.selection.valid = result.filter.run.selection.total - result.substitutedSamples;
    result.filter.run.outputsWritten = result.filter.run.selection.valid;
    result.filter.plannedWorkers =
        plannedWorkerCount(execution, geometry.height, rowCost(static_cast<std::uint64_t>(geometry.width), 1u));
    return Status::success();
}

} // namespace

Result<DeconvolutionResult> deconvolveImageRichardsonLucy(const ExecutionContext& execution, SampleType sampleType,
                                                          const void* source, const ImageLayout& sourceLayout,
                                                          void* destination, const ImageLayout& destinationLayout,
                                                          PlaneView<const float> psf, const RichardsonLucyPlan& plan,
                                                          const SpatialSelection& selection)
{
    if (plan.imagePasses() == 0u)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kRichardsonLucyOperation,
                                                          "this plan has not been validated");
    }
    const Status psfChecked = checkPlannedPsf(psf, plan.psfWidth(), plan.psfHeight(), kRichardsonLucyOperation);
    if (psfChecked.failed())
    {
        return psfChecked;
    }
    const RichardsonLucyRequest& request = plan.request();
    DeconvolutionResult result{};
    result.filter.imagePasses = plan.imagePasses();
    const Status ran = runDeconvolution(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, psf, request.common,
        plan.usesTransform(), plan.usesTransform(), plan.taperRadius(),
        deconvolution_detail::ConvolutionEngineNeeds{.convolution = true, .adjoint = true, .inverseFilter = false},
        true, false, selection, kRichardsonLucyOperation, result, request.iterations,
        [&](const WorkingPlane& target, std::span<WorkingPlane> scratch, const ConvolutionEngine& engine,
            WorkingPlane& restored) -> Status {
            return runRichardsonLucy(execution, target, scratch, engine, request, result, restored);
        });
    if (ran.failed())
    {
        return ran;
    }
    return result;
}

Result<DeconvolutionResult> deconvolveImageWiener(const ExecutionContext& execution, SampleType sampleType,
                                                  const void* source, const ImageLayout& sourceLayout,
                                                  void* destination, const ImageLayout& destinationLayout,
                                                  PlaneView<const float> psf, const WienerPlan& plan,
                                                  const SpatialSelection& selection)
{
    if (plan.imagePasses() == 0u)
    {
        return deconvolution_detail::deconvolutionFailure(ErrorCode::InvalidArgument, kWienerOperation,
                                                          "this plan has not been validated");
    }
    const Status psfChecked = checkPlannedPsf(psf, plan.psfWidth(), plan.psfHeight(), kWienerOperation);
    if (psfChecked.failed())
    {
        return psfChecked;
    }
    const WienerRequest& request = plan.request();
    DeconvolutionResult result{};
    result.filter.imagePasses = plan.imagePasses();
    // A Wiener filter exists only in the frequency domain, so the engine takes the transform path
    // whatever the convolution field says; that field still decides how the taper blurs.
    // The filter itself divides by the kernel and never convolves by it, so the two convolution
    // spectra are built only when a taper is going to blur with one.
    const bool tapers = plan.taperRadius() > 0.0 && request.common.edgeTaperIterations > 0u;
    const Status ran = runDeconvolution(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, psf, request.common, true,
        plan.taperUsesTransform(), plan.taperRadius(),
        deconvolution_detail::ConvolutionEngineNeeds{.convolution = tapers && plan.taperUsesTransform(),
                                                     .adjoint = false,
                                                     .inverseFilter = true,
                                                     .directConvolution = tapers && !plan.taperUsesTransform()},
        false, true, selection, kWienerOperation, result, 1u,
        [&](const WorkingPlane& target, std::span<WorkingPlane>, const ConvolutionEngine& engine,
            WorkingPlane& restored) -> Status {
            const Status filtered = engine.wiener(execution, target, restored, request.regularization,
                                                  request.common.nonNegative, kWienerOperation);
            if (filtered.failed())
            {
                return filtered;
            }
            if (request.common.preserveFlux)
            {
                Result<double> observedFlux = deconvolution_detail::positiveSum(execution, target);
                if (!observedFlux.hasValue())
                {
                    return observedFlux.status();
                }
                const Status rescaled =
                    restoreFlux(execution, restored, *observedFlux, request.common.guard, result.fluxScale);
                if (rescaled.failed())
                {
                    return rescaled;
                }
            }
            return Status::success();
        });
    if (ran.failed())
    {
        return ran;
    }
    return result;
}

} // namespace an
