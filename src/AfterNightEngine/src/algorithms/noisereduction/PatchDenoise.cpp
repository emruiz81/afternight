// AfterNightEngine - the public surface of the patch denoisers: the plans, the noise estimator,
// and the orchestration that stands between a caller's storage and the two estimators.
//
// The orchestration is where the contract lives. A caller's image is widened once into channel-major
// Float32 planes and multiplied onto the estimator's own tuning scale, because that method's
// parameter tables are written in the code values of an eight-bit image and are part of the method
// rather than something this engine may rescale; everything after that - the noise measurement, the
// variance-stabilising transform, both estimator passes, the strengthening loop and the guided
// finishing stage - happens on that one scale; and the answer is divided back and written through
// the stated conversion. What makes it honest rather than a guess is that the scale comes from a
// number the caller stated instead of from the largest sample in the frame. The
// finishing stage inside `denoiseImagePatches` runs with `GuidedShrinkageRequest`'s default radius,
// weights, coverage threshold and independent channel coupling; only its noise level and output
// policy come from the request.
//
// Provenance: the parameter tables in `PatchDenoisePlan::passParameters` are the Non-Local Bayes
// tables of Marc Lebrun's IPOL implementation (https://www.ipol.im/pub/art/2013/16/,
// `nl-bayes_20130617`), whose files state `Copyright (c) 2013, Marc Lebrun <marc.lebrun.ik@gmail.com>`
// and are distributed under the GNU General Public License, version 3 or any later version; this file
// is part of that derived work and is distributed under the same licence. The Anscombe inverse is
// Makitalo and Foi's closed form. The noise estimator's
// clipping rule, the strengthen-operate-subtract arithmetic and the variance-stabilising scale are
// this engine's, and so are a tile grid that comes from the frame rather than the core count, a
// working maximum the caller states, and the refusals that stand where a silent override would.
// The provenance note is `docs/dev/PATCH_DENOISE_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/noisereduction/PatchDenoiseInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/SpatialPlane.h"
#include "kernels/reference/ReferenceKernelInternal.h"
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

using patch_detail::EstimatorCounters;
using patch_detail::EstimatorTuning;
using patch_detail::GuidedShrinkageCounters;
using patch_detail::GuidedShrinkageGeometry;
using patch_detail::WorkingFrame;
using spatial_detail::SpatialGeometry;

constexpr const char* kPlanOperation = "an::planPatchDenoise";
constexpr const char* kDenoiseOperation = "an::denoiseImagePatches";
constexpr const char* kEstimateOperation = "an::estimateImageNoise";
constexpr const char* kGuidedPlanOperation = "an::planGuidedShrinkage";
constexpr const char* kGuidedOperation = "an::shrinkImageWithGuide";

/// How many sigma-clipping passes the noise estimator makes over a row of first differences.
constexpr std::uint32_t kNoiseClipPasses = 3u;

/// How many standard deviations a first difference may stand from the mean and be kept.
constexpr double kNoiseClipSigma = 5.0;

/// What the standard deviation of a first difference has to be divided by to describe one sample.
///
/// A difference of two independent samples of equal variance has twice that variance, so the sample
/// standard deviation is the difference's over the square root of two.
constexpr double kFirstDifferenceScale = 0.70710678118654752440;

/// How much a strengthening iteration's noise estimate may rise before the loop is abandoned.
///
/// One per cent: below it the estimate is wandering and above it the loop is diverging.
constexpr double kConvergenceTolerance = 1.01;

/// How many tiles a frame is divided into, where its tiles' smallest interior allows.
///
/// A pass hands its tiles to its workers one at a time, so a frame of four tiles keeps four workers
/// busy however many it was granted, and the slowest of the four decides the pass. Sixteen is a number
/// this engine chose rather than one a machine reported, so a grid that reaches for it is still a
/// function of the frame.
constexpr std::uint64_t kMinimumPatchDenoiseTiles = 16u;

/// Rounds a byte count up to the alignment every working frame starts at.
///
/// @param bytes what a frame needs.
/// @return what it occupies.
[[nodiscard]] constexpr std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) / kDefaultBufferAlignmentBytes * kDefaultBufferAlignmentBytes;
}

/// Whether a number is finite and above zero.
///
/// @param value the number.
/// @return true when it can be a sigma, a scale or a threshold.
[[nodiscard]] bool isPositiveFinite(double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// Whether a finite value survives narrowing into the estimator's representation.
///
/// @param value the value.
/// @return true when Float32 carries it without an infinity.
[[nodiscard]] bool fitsFiniteFloat(double value) noexcept
{
    constexpr double kMaximum = static_cast<double>((std::numeric_limits<float>::max)());
    return std::isfinite(value) && value >= -kMaximum && value <= kMaximum;
}

/// Whether a positive finite value survives narrowing into the estimator's representation.
///
/// @param value the value.
/// @return true when Float32 carries it as a value above zero.
[[nodiscard]] bool fitsPositiveFloat(double value) noexcept
{
    constexpr double kMinimum = static_cast<double>((std::numeric_limits<float>::denorm_min)());
    return fitsFiniteFloat(value) && value >= kMinimum;
}

/// Whether a positive derived value fits in the estimator's representation.
///
/// @param value the value, computed in the widest arithmetic this host supplies.
/// @return true when Float32 carries it as a value above zero.
[[nodiscard]] bool fitsPositiveFloat(long double value) noexcept
{
    constexpr long double kMinimum = static_cast<long double>((std::numeric_limits<float>::denorm_min)());
    constexpr long double kMaximum = static_cast<long double>((std::numeric_limits<float>::max)());
    return std::isfinite(value) && value >= kMinimum && value <= kMaximum;
}

/// Checks a measured noise level before the parameter tables or estimator narrow it.
///
/// @param tunedSigma the measured level on the estimator's tuning scale.
/// @return success, or a data or range failure that precedes any Float32 conversion.
[[nodiscard]] Status checkEstimatorSigma(double tunedSigma)
{
    if (!std::isfinite(tunedSigma) || tunedSigma < 0.0)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidData, kDenoiseOperation,
                                          "the measured noise level is not finite and non-negative");
    }
    if (tunedSigma > kMaximumPatchDenoiseTunedSigma)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kDenoiseOperation,
                                               "the measured noise level is past what the estimator is tuned for",
                                               static_cast<std::uint64_t>((std::min)(tunedSigma, 1.0e18)),
                                               static_cast<std::uint64_t>(kMaximumPatchDenoiseTunedSigma));
    }
    return Status::success();
}

/// Whether a policy states an output promise and a write-back this family can honour.
///
/// @param output the policy.
/// @param operation the entry point the caller is inside.
/// @return success, or the first reason it cannot be honoured.
[[nodiscard]] Status checkOutputPolicy(const SpatialOutputPolicy& output, const char* operation)
{
    if (!isOutputPrecisionContract(output.contract))
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, operation,
                                          "the output contract is not one this engine knows");
    }
    const Status policy = checkConversionPolicy(output.writeBack, operation);
    if (policy.failed())
    {
        return policy;
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, operation,
                                          "a patch denoise carries its samples across as the numbers they are; a "
                                          "full-scale domain mapping would make it a denoise and a domain change at "
                                          "once");
    }
    return Status::success();
}

/// A Float32 plane over one channel of a working frame.
///
/// @param frame the frame.
/// @param channel which channel.
/// @return the view, which always exists for a frame that describes anything.
[[nodiscard]] std::optional<PlaneView<float>> framePlane(const WorkingFrame& frame, std::uint32_t channel) noexcept
{
    const std::uint64_t rowStride = static_cast<std::uint64_t>(frame.width) * sizeof(float);
    return PlaneView<float>::create(
        std::span<float>{frame.plane(channel), static_cast<std::size_t>(frame.planeSamples())}, frame.width,
        frame.height, 1u, rowStride);
}

/// The sigma-clipped standard deviation of one row's first differences.
///
/// Three passes at five standard deviations. A pass whose accepted
/// set is empty, or whose deviation falls below `1e-10`, stops the clipping; a row narrower than
/// three samples or whose accepted set ends empty is not measured at all.
///
/// @param row the samples.
/// @param accepts whether an adjacent pair is admitted.
/// @param measured where the standard deviation goes, when there is one.
/// @return true when the row produced a measurement.
template <typename Accepts>
[[nodiscard]] bool clippedRowDeviation(std::span<const float> row, const Accepts& accepts, double& measured) noexcept
{
    if (row.size() < 3u)
    {
        return false;
    }
    // The accepted set is described by the means and deviations of the passes that produced it,
    // which is what lets the row be re-scanned instead of copied into a shrinking buffer.
    std::array<double, kNoiseClipPasses> means{};
    std::array<double, kNoiseClipPasses> deviations{};
    std::uint32_t applied = 0u;
    double mean = 0.0;
    double deviation = 0.0;
    std::uint64_t population = 0u;

    for (std::uint32_t pass = 0u; pass <= kNoiseClipPasses; ++pass)
    {
        double sum = 0.0;
        std::uint64_t count = 0u;
        for (std::size_t i = 1u; i < row.size(); ++i)
        {
            if (!accepts(i) || !std::isfinite(row[i]) || !std::isfinite(row[i - 1u]))
                continue;
            const double difference = static_cast<double>(row[i]) - static_cast<double>(row[i - 1u]);
            bool accepted = true;
            for (std::uint32_t previous = 0u; previous < applied; ++previous)
            {
                if (std::abs(difference - means[previous]) > kNoiseClipSigma * deviations[previous])
                {
                    accepted = false;
                    break;
                }
            }
            if (accepted)
            {
                sum += difference;
                ++count;
            }
        }
        if (count == 0u)
        {
            break;
        }
        mean = sum / static_cast<double>(count);
        double variance = 0.0;
        for (std::size_t i = 1u; i < row.size(); ++i)
        {
            if (!accepts(i) || !std::isfinite(row[i]) || !std::isfinite(row[i - 1u]))
                continue;
            const double difference = static_cast<double>(row[i]) - static_cast<double>(row[i - 1u]);
            bool accepted = true;
            for (std::uint32_t previous = 0u; previous < applied; ++previous)
            {
                if (std::abs(difference - means[previous]) > kNoiseClipSigma * deviations[previous])
                {
                    accepted = false;
                    break;
                }
            }
            if (accepted)
            {
                const double centred = difference - mean;
                variance += centred * centred;
            }
        }
        deviation = std::sqrt(variance / static_cast<double>(count));
        population = count;
        if (pass == kNoiseClipPasses || deviation < 1.0e-10)
        {
            break;
        }
        means[pass] = mean;
        deviations[pass] = deviation;
        applied = pass + 1u;
    }

    if (population == 0u)
    {
        return false;
    }
    measured = deviation;
    return true;
}

/// The median of a channel's row deviations.
///
/// @param deviations the measurements, which this reorders.
/// @return the upper median.
[[nodiscard]] double medianDeviation(std::span<double> deviations) noexcept
{
    if (deviations.empty())
    {
        return 0.0;
    }
    std::sort(deviations.begin(), deviations.end());
    return deviations[deviations.size() / 2u];
}

/// The background noise level of one Float32 frame, in the frame's own units.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the frame to measure.
/// @param deviations scratch of at least the frame height, one double per row.
/// @param measured where the estimate goes.
/// @param admission original samples whose validity is tested, or null to use the measured frame.
/// @param selection the original validity rule and mask, or null for all finite pairs.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status estimateFrameNoise(const ExecutionContext& execution, const WorkingFrame& frame,
                                        std::span<double> deviations, double& measured,
                                        const WorkingFrame* admission = nullptr,
                                        const SpatialSelection* selection = nullptr)
{
    double total = 0.0;
    for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
    {
        const float* const plane = frame.plane(channel);
        const KernelValidityView validity =
            selection != nullptr ? spatial_detail::channelValidity(*selection, channel) : KernelValidityView{};
        const reference_detail::ValidityAccessor access(validity);
        const float* const admittedPlane = admission != nullptr ? admission->plane(channel) : plane;
        std::fill(deviations.begin(), deviations.begin() + frame.height, -1.0);
        const Status walked = parallelFor(
            execution, frame.height,
            TaskCost{.bytesPerItem = static_cast<std::uint64_t>(frame.width) * sizeof(float),
                     .operationsPerItem = static_cast<std::uint64_t>(frame.width) * 8u},
            [&](TaskRange range) -> Status {
                for (std::uint64_t row = range.begin; row < range.end; ++row)
                {
                    const std::span<const float> line{plane + row * frame.width, static_cast<std::size_t>(frame.width)};
                    double deviation = 0.0;
                    const auto accepts = [&](std::size_t x) noexcept {
                        return access.accepts(admittedPlane[row * frame.width + x], static_cast<std::uint32_t>(x),
                                              static_cast<std::uint32_t>(row)) &&
                               access.accepts(admittedPlane[row * frame.width + x - 1u],
                                              static_cast<std::uint32_t>(x - 1u), static_cast<std::uint32_t>(row));
                    };
                    if (clippedRowDeviation(line, accepts, deviation))
                    {
                        deviations[static_cast<std::size_t>(row)] = deviation;
                    }
                }
                return Status::success();
            });
        if (walked.failed())
        {
            return walked;
        }
        std::size_t kept = 0u;
        for (std::uint32_t row = 0u; row < frame.height; ++row)
        {
            if (deviations[row] >= 0.0)
            {
                deviations[kept] = deviations[row];
                ++kept;
            }
        }
        total += medianDeviation(deviations.first(kept)) * kFirstDifferenceScale;
    }
    measured = total / static_cast<double>((std::max)(frame.channels, 1u));
    return Status::success();
}

/// Multiplies every sample of a frame by a constant.
///
/// @param frame the frame.
/// @param factor what to multiply by.
void scaleFrame(const WorkingFrame& frame, float factor) noexcept
{
    const std::uint64_t count = frame.sampleCount();
    for (std::uint64_t i = 0u; i < count; ++i)
    {
        frame.base[i] *= factor;
    }
}

/// Applies the Anscombe transform, or an approximation of its exact unbiased inverse, to a whole
/// frame.
///
/// The transform turns noise whose variance follows the signal into noise whose variance does not,
/// which is what lets one stated sigma describe a Poisson-dominated frame. It is the generalised
/// transform with unit gain, no Gaussian term and no offset, so it reduces to the classical
/// `2 * sqrt(x + 3/8)`. The inverse is Makitalo and Foi's closed-form
/// approximation of the exact unbiased inverse rather than the algebraic inverse, which is biased;
/// a restored value that is not a number is stored as zero.
///
/// The intermediate scale lifts the samples onto a sixteen-bit range before the square root and
/// divides by the transform's own normalisation afterwards, so the stabilised values land back on
/// the estimator's tuning scale.
///
/// @param frame the frame, changed in place.
/// @param forward true to stabilise, false to undo it.
void applyVarianceStabilisation(const WorkingFrame& frame, bool forward) noexcept
{
    const float lift = 65536.0f / static_cast<float>(kPatchDenoiseTuningScale);
    const float normalisation = lift * 2.0f * std::sqrt(11.0f / 8.0f);
    const std::uint64_t count = frame.sampleCount();
    if (forward)
    {
        for (std::uint64_t i = 0u; i < count; ++i)
        {
            const float lifted = frame.base[i] * lift + 0.375f;
            frame.base[i] = 2.0f * std::sqrt((std::max)(lifted, 0.0f)) / normalisation;
        }
        return;
    }
    const float root = std::sqrt(1.5f);
    for (std::uint64_t i = 0u; i < count; ++i)
    {
        const float value = (std::max)(frame.base[i] * normalisation, 1.0f);
        const float reciprocal = 1.0f / value;
        const float squared = reciprocal * reciprocal;
        const float cubed = squared * reciprocal;
        const float exact =
            0.25f * value * value + 0.25f * root * reciprocal - 1.375f * squared + 0.625f * root * cubed - 0.125f;
        const float restored = (std::max)(0.0f, exact) / lift;
        frame.base[i] = std::isnan(restored) ? 0.0f : restored;
    }
}

/// Holds every sample of a frame inside the closed interval the tuning scale defines.
///
/// @param frame the frame, changed in place.
void clampFrame(const WorkingFrame& frame) noexcept
{
    const auto ceiling = static_cast<float>(kPatchDenoiseTuningScale);
    const std::uint64_t count = frame.sampleCount();
    for (std::uint64_t i = 0u; i < count; ++i)
    {
        frame.base[i] = std::clamp(frame.base[i], 0.0f, ceiling);
    }
}

/// Widens one image into channel-major Float32 planes.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are.
/// @param samples where they start.
/// @param layout where they sit.
/// @param frame where the widened samples go.
/// @param operation the name carried by any failure.
/// @return success, or the first failure the gather reported.
[[nodiscard]] Status gatherFrame(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                 const ImageLayout& layout, const WorkingFrame& frame, const char* operation)
{
    for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
    {
        const std::optional<PlaneView<float>> plane = framePlane(frame, channel);
        if (!plane.has_value())
        {
            return patch_detail::patchFailure(ErrorCode::Internal, operation,
                                              "a working frame does not describe a plane");
        }
        const Status gathered =
            spatial_detail::gatherChannelPlane(execution, sampleType, samples, layout, channel, *plane, operation);
        if (gathered.failed())
        {
            return gathered;
        }
    }
    return Status::success();
}

/// Replaces every sample the validity rule rejects with a stated substitute.
///
/// Applied to the frame the estimator reads and never to the one the write-back compares against,
/// which is what lets a rejected sample keep its own destination while its neighbours are still
/// estimated from something finite.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the frame to repair.
/// @param selection which samples mean something.
/// @param substitute what stands in for a rejected sample.
/// @param substituted how many samples were replaced, added to whatever it holds.
/// @param operation the name carried by any failure.
/// @return success, or the first failure the repair reported.
[[nodiscard]] Status substituteFrame(const ExecutionContext& execution, const WorkingFrame& frame,
                                     const SpatialSelection& selection, float substitute, std::uint64_t& substituted,
                                     const char* operation)
{
    for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
    {
        const std::optional<PlaneView<float>> plane = framePlane(frame, channel);
        if (!plane.has_value())
        {
            return patch_detail::patchFailure(ErrorCode::Internal, operation,
                                              "a working frame does not describe a plane");
        }
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status repaired =
            spatial_detail::substituteRejectedSamples(execution, *plane, validity, substitute, substituted, operation);
        if (repaired.failed())
        {
            return repaired;
        }
    }
    return Status::success();
}

/// Writes channel-major Float32 planes back into one image under the stated conversion.
///
/// @param execution the operation policy and engine-context lease.
/// @param answer the frame holding the result.
/// @param input the frame the estimator read, whose samples the validity rule describes.
/// @param selection which samples mean something.
/// @param sampleType the destination representation.
/// @param destination where the destination's samples start.
/// @param destinationLayout where they sit.
/// @param source the caller's source storage, compared after conversion.
/// @param sourceLayout where it sits.
/// @param conversion how a computed value becomes a stored sample.
/// @param changed how many stored samples moved, added to whatever it holds.
/// @param operation the name carried by any failure.
/// @return success, or the first failure the scatter reported.
[[nodiscard]] Status scatterFrame(const ExecutionContext& execution, const WorkingFrame& answer,
                                  const WorkingFrame& input, const SpatialSelection& selection, SampleType sampleType,
                                  void* destination, const ImageLayout& destinationLayout, const void* source,
                                  const ImageLayout& sourceLayout, const ConversionPolicy& conversion,
                                  std::uint64_t& changed, const char* operation)
{
    std::array<std::optional<PlaneView<float>>, kMaximumPatchDenoiseChannels> answerPlanes{};
    std::array<std::optional<PlaneView<float>>, kMaximumPatchDenoiseChannels> inputPlanes{};
    for (std::uint32_t channel = 0u; channel < answer.channels; ++channel)
    {
        answerPlanes[channel] = framePlane(answer, channel);
        inputPlanes[channel] = framePlane(input, channel);
        if (!answerPlanes[channel].has_value() || !inputPlanes[channel].has_value())
        {
            return patch_detail::patchFailure(ErrorCode::Internal, operation,
                                              "a working frame does not describe a plane");
        }
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    for (std::uint32_t channel = 0u; channel < answer.channels; ++channel)
    {
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status scattered = spatial_detail::commitChannelPlane(
            *answerPlanes[channel], *inputPlanes[channel], validity, sampleType, destination, destinationLayout,
            channel, conversion, operation, source, &sourceLayout, &changed);
        if (scattered.failed())
        {
            return scattered;
        }
    }
    return Status::success();
}

/// The smallest frame extent a pass's search window can run inside.
///
/// @param tuning both passes' parameters.
/// @return the wider of the two search windows.
[[nodiscard]] std::uint32_t requiredExtent(const EstimatorTuning& tuning) noexcept
{
    return (std::max)(tuning.first.searchWindow, tuning.second.searchWindow);
}

} // namespace

namespace patch_detail
{

Status patchFailure(ErrorCode code, const char* operation, const char* message)
{
    return spatial_detail::spatialFailure(code, operation, message);
}

Status patchLimitFailure(ErrorCode code, const char* operation, const char* message, std::uint64_t requested,
                         std::uint64_t limit)
{
    return spatial_detail::spatialLimitFailure(code, operation, message, requested, limit);
}

std::uint32_t symmetricCoordinate(std::int64_t position, std::uint32_t extent) noexcept
{
    if (extent <= 1u)
    {
        return 0u;
    }
    const auto last = static_cast<std::int64_t>(extent) - 1;
    while (position < 0 || position > last)
    {
        position = position < 0 ? -position - 1 : 2 * last - position + 1;
    }
    return static_cast<std::uint32_t>(position);
}

FrameTile tileAt(std::uint32_t width, std::uint32_t height, std::uint32_t tilesX, std::uint32_t tilesY,
                 std::uint32_t index) noexcept
{
    const std::uint32_t column = index % tilesX;
    const std::uint32_t row = index / tilesX;
    const std::uint32_t firstX = static_cast<std::uint32_t>(static_cast<std::uint64_t>(width) * column / tilesX);
    const std::uint32_t lastX = static_cast<std::uint32_t>(static_cast<std::uint64_t>(width) * (column + 1u) / tilesX);
    const std::uint32_t firstY = static_cast<std::uint32_t>(static_cast<std::uint64_t>(height) * row / tilesY);
    const std::uint32_t lastY = static_cast<std::uint32_t>(static_cast<std::uint64_t>(height) * (row + 1u) / tilesY);
    return FrameTile{
        .originX = firstX, .originY = firstY, .interiorWidth = lastX - firstX, .interiorHeight = lastY - firstY};
}

void readTile(const WorkingFrame& frame, const FrameTile& tile, std::uint32_t boundary,
              const WorkingFrame& destination) noexcept
{
    for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
    {
        const float* const source = frame.plane(channel);
        float* const target = destination.plane(channel);
        for (std::uint32_t row = 0u; row < destination.height; ++row)
        {
            const std::uint32_t sourceRow = symmetricCoordinate(
                static_cast<std::int64_t>(tile.originY) + static_cast<std::int64_t>(row) - boundary, frame.height);
            const float* const sourceLine = source + static_cast<std::uint64_t>(sourceRow) * frame.width;
            float* const targetLine = target + static_cast<std::uint64_t>(row) * destination.width;
            for (std::uint32_t column = 0u; column < destination.width; ++column)
            {
                const std::uint32_t sourceColumn = symmetricCoordinate(static_cast<std::int64_t>(tile.originX) +
                                                                           static_cast<std::int64_t>(column) - boundary,
                                                                       frame.width);
                targetLine[column] = sourceLine[sourceColumn];
            }
        }
    }
}

void writeTileInterior(const WorkingFrame& tileFrame, const FrameTile& tile, std::uint32_t boundary,
                       const WorkingFrame& frame) noexcept
{
    for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
    {
        const float* const source = tileFrame.plane(channel);
        float* const target = frame.plane(channel);
        for (std::uint32_t row = 0u; row < tile.interiorHeight; ++row)
        {
            const float* const sourceLine =
                source + static_cast<std::uint64_t>(row + boundary) * tileFrame.width + boundary;
            float* const targetLine =
                target + static_cast<std::uint64_t>(tile.originY + row) * frame.width + tile.originX;
            std::copy_n(sourceLine, tile.interiorWidth, targetLine);
        }
    }
}

} // namespace patch_detail

PatchDenoisePlan::PatchDenoisePlan(const PatchDenoiseRequest& request, std::uint32_t channelCount) noexcept
    : m_request(request), m_channelCount(channelCount), m_tuningScale(kPatchDenoiseTuningScale / request.whiteLevel)
{
}

const PatchDenoiseRequest& PatchDenoisePlan::request() const noexcept
{
    return m_request;
}

std::uint32_t PatchDenoisePlan::channelCount() const noexcept
{
    return m_channelCount;
}

double PatchDenoisePlan::tuningScale() const noexcept
{
    return m_tuningScale;
}

PatchDenoisePassParameters PatchDenoisePlan::passParameters(double tunedSigma, bool secondPass) const noexcept
{
    PatchDenoisePassParameters parameters{};
    if (m_channelCount == 0u || !std::isfinite(tunedSigma) || tunedSigma < 0.0 ||
        tunedSigma > kMaximumPatchDenoiseTunedSigma)
    {
        return parameters;
    }
    const double sigma = tunedSigma;
    const bool monochrome = m_channelCount == 1u;

    // Every number below is the published method's own table, as Marc Lebrun's NL-Bayes
    // implementation sets it for a noise level on an eight-bit scale, carried unchanged; the
    // strict comparisons decide which row a level exactly on a threshold falls in.
    if (monochrome)
    {
        parameters.patchSize = secondPass ? 5u : (sigma < 30.0f ? 5u : 7u);
        parameters.similarPatches = secondPass
                                        ? (sigma < 20.0f ? 15u : (sigma < 40.0f ? 25u : (sigma < 80.0f ? 30u : 45u)))
                                        : (sigma < 10.0f ? 35u : (sigma < 30.0f ? 45u : (sigma < 80.0f ? 90u : 100u)));
    }
    else
    {
        parameters.patchSize = secondPass ? (sigma < 50.0f ? 3u : (sigma < 70.0f ? 5u : 7u))
                                          : (sigma < 20.0f ? 3u : (sigma < 50.0f ? 5u : 7u));
        parameters.similarPatches = parameters.patchSize * parameters.patchSize * 3u;
    }

    parameters.referenceStep = parameters.patchSize / 2u;
    parameters.searchWindow = parameters.similarPatches / 2u;
    if (parameters.searchWindow % 2u == 0u)
    {
        ++parameters.searchWindow;
    }
    parameters.boundary = static_cast<std::uint32_t>(1.5f * static_cast<float>(parameters.searchWindow));
    parameters.homogeneityGamma = 1.05f;
    if (monochrome)
    {
        parameters.covarianceWeight = secondPass ? (sigma < 15.0f ? 1.1f : (sigma < 35.0f ? 1.0f : 0.9f))
                                                 : (sigma < 15.0f ? 1.1f : (sigma < 70.0f ? 1.0f : 0.9f));
    }
    else
    {
        parameters.covarianceWeight = secondPass ? (sigma < 50.0f ? 1.2f : 1.0f) : 1.0f;
    }
    parameters.similarityDistance =
        secondPass ? 16.0f * static_cast<float>(parameters.patchSize * parameters.patchSize * m_channelCount) : 0.0f;
    parameters.homogeneousArea = secondPass ? m_request.homogeneousAreaSecondPass : m_request.homogeneousAreaFirstPass;
    return parameters;
}

void PatchDenoisePlan::tileGrid(std::uint32_t width, std::uint32_t height, std::uint32_t boundary,
                                std::uint32_t& tilesX, std::uint32_t& tilesY) noexcept
{
    // A tile's boundary is denoised and then discarded, so an interior that is small against its
    // boundary spends most of its work on samples nobody keeps. An interior of at least four
    // boundaries keeps the discarded border to at most a third of a tile's side, and one of 256
    // samples, where the frame holds enough of them, keeps it smaller still - whatever the machine
    // happens to have, which is the point: the answer is a function of the frame and not of the core
    // count.
    const std::uint32_t smallest = 4u * (std::max)(boundary, 1u);
    patch_detail::tileGridBetween(width, height, (std::max)(smallest, 256u), smallest, tilesX, tilesY);
}

void patch_detail::tileGridBetween(std::uint32_t width, std::uint32_t height, std::uint32_t preferredInterior,
                                   std::uint32_t smallestInterior, std::uint32_t& tilesX,
                                   std::uint32_t& tilesY) noexcept
{
    const auto tilesAt = [&](std::uint32_t interior) noexcept {
        tilesX = (std::max)(width / interior, 1u);
        tilesY = (std::max)(height / interior, 1u);
        return static_cast<std::uint64_t>(tilesX) * tilesY;
    };
    if (tilesAt(preferredInterior) < kMinimumPatchDenoiseTiles)
    {
        // The largest interior that reaches the count, or the smallest interior when none does.
        for (std::uint32_t interior = preferredInterior - 1u; interior >= smallestInterior; --interior)
        {
            if (tilesAt(interior) >= kMinimumPatchDenoiseTiles || interior == smallestInterior)
            {
                break;
            }
        }
    }
    while (static_cast<std::uint64_t>(tilesX) * tilesY > kMaximumPatchDenoiseTiles)
    {
        if (tilesX >= tilesY)
        {
            --tilesX;
        }
        else
        {
            --tilesY;
        }
    }
}

std::uint64_t PatchDenoisePlan::imagePasses() const noexcept
{
    if (m_channelCount == 0u)
    {
        return 0u;
    }
    const std::uint64_t estimator = 2u * static_cast<std::uint64_t>(m_request.iterations);
    return estimator + (m_request.finishing == PatchDenoiseFinishing::GuidedDualDomain ? 1u : 0u);
}

Result<PatchDenoisePlan> planPatchDenoise(const PatchDenoiseRequest& request, std::uint32_t channelCount)
{
    if (!isPositiveFinite(request.whiteLevel) || request.whiteLevel < kMinimumPatchDenoiseWhiteLevel)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                          "a patch denoise needs a finite full scale above the minimum");
    }
    if (!request.measureNoise && !isPositiveFinite(request.sigma))
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                          "a stated noise level must be finite and above zero");
    }
    if (!std::isfinite(request.finishingSigma) || request.finishingSigma < 0.0)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                          "a finishing noise level must be finite and not negative");
    }
    if (!std::isfinite(request.invalidSample))
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                          "the substitute for a rejected sample must be finite");
    }
    if (request.finishing != PatchDenoiseFinishing::None &&
        request.finishing != PatchDenoiseFinishing::GuidedDualDomain)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                          "the finishing stage is outside its enumeration");
    }
    const Status output = checkOutputPolicy(request.output, kPlanOperation);
    if (output.failed())
    {
        return output;
    }
    if (request.iterations == 0u || request.iterations > kMaximumPatchDenoiseIterations)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                               "the strengthening loop runs between one and its maximum",
                                               request.iterations, kMaximumPatchDenoiseIterations);
    }
    if (request.iterations > 1u &&
        (!std::isfinite(request.reinjection) || request.reinjection <= 0.0 || request.reinjection >= 1.0))
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                          "the strengthening mix must lie strictly inside the unit interval");
    }
    if (!fitsPositiveFloat(request.whiteLevel) || !fitsFiniteFloat(request.invalidSample) ||
        (request.finishingSigma > 0.0 && !fitsPositiveFloat(request.finishingSigma)) ||
        (request.iterations > 1u && !fitsPositiveFloat(request.reinjection)))
    {
        return patch_detail::patchFailure(ErrorCode::OutOfRange, kPlanOperation,
                                          "a patch-denoise numeric field cannot be represented by Float32");
    }
    if (channelCount == 0u || channelCount > kMaximumPatchDenoiseChannels)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                               "the patch estimator is defined for up to four channels", channelCount,
                                               kMaximumPatchDenoiseChannels);
    }
    if (channelCount == 2u)
    {
        return patch_detail::patchFailure(ErrorCode::Unsupported, kPlanOperation,
                                          "the patch estimator has no colour decorrelation for two channels");
    }
    if (request.varianceStabilisation && request.iterations > 1u)
    {
        return patch_detail::patchFailure(ErrorCode::IncompatibleArguments, kPlanOperation,
                                          "a stabilised frame is not the frame the strengthening loop subtracts on");
    }
    if (request.varianceStabilisation && request.finishing != PatchDenoiseFinishing::None)
    {
        return patch_detail::patchFailure(ErrorCode::IncompatibleArguments, kPlanOperation,
                                          "a stabilised frame is not the frame the finishing stage is guided on");
    }

    const double tuningScale = kPatchDenoiseTuningScale / request.whiteLevel;
    const long double substituted = static_cast<long double>(std::abs(request.invalidSample)) * tuningScale;
    if (!fitsPositiveFloat(tuningScale) || (request.invalidSample != 0.0 && !fitsPositiveFloat(substituted)))
    {
        return patch_detail::patchFailure(ErrorCode::OutOfRange, kPlanOperation,
                                          "the tuning scale or substituted sample cannot be represented by Float32");
    }
    if (!request.measureNoise)
    {
        const double tuned = request.sigma * tuningScale;
        if (!std::isfinite(tuned) || tuned > kMaximumPatchDenoiseTunedSigma)
        {
            return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                                   "the stated noise level is past what the estimator is tuned for",
                                                   static_cast<std::uint64_t>((std::min)(tuned, 1.0e18)),
                                                   static_cast<std::uint64_t>(kMaximumPatchDenoiseTunedSigma));
        }
    }
    return PatchDenoisePlan{request, channelCount};
}

Result<double> estimateImageNoise(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                  const ImageLayout& layout, const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const SpatialOutputPolicy output{};
    const Status checked = spatial_detail::checkSpatialImage(execution, sampleType, source, layout, output, selection,
                                                             kEstimateOperation, geometry);
    if (checked.failed())
    {
        return checked;
    }
    if (geometry.width < 3u)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kEstimateOperation,
                                               "a noise estimate needs at least three samples in a row", geometry.width,
                                               3u);
    }

    const std::uint64_t frameBytes =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount * sizeof(float);
    Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
        execution,
        alignedBytes(frameBytes) + alignedBytes(static_cast<std::uint64_t>(geometry.height) * sizeof(double)),
        kDefaultBufferAlignmentBytes, kEstimateOperation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    const plane_detail::ScopedContextMemory memory = std::move(taken).value();
    const WorkingFrame frame{.base = static_cast<float*>(memory.data()),
                             .width = geometry.width,
                             .height = geometry.height,
                             .channels = geometry.channelCount};
    const std::span<double> deviations{
        reinterpret_cast<double*>(static_cast<std::uint8_t*>(memory.data()) + alignedBytes(frameBytes)),
        static_cast<std::size_t>(geometry.height)};

    const Status gathered = gatherFrame(execution, sampleType, source, layout, frame, kEstimateOperation);
    if (gathered.failed())
    {
        return gathered;
    }
    double measured = 0.0;
    const Status estimated = estimateFrameNoise(execution, frame, deviations, measured, &frame, &selection);
    if (estimated.failed())
    {
        return estimated;
    }
    return measured;
}

Result<PatchDenoiseResult> denoiseImagePatches(const ExecutionContext& execution, SampleType sampleType,
                                               const void* source, const ImageLayout& sourceLayout, void* destination,
                                               const ImageLayout& destinationLayout, const PatchDenoisePlan& plan,
                                               const SpatialSelection& selection)
{
    if (plan.channelCount() == 0u)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kDenoiseOperation,
                                          "this plan has not been validated");
    }
    SpatialGeometry geometry{};
    const Status checked =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kDenoiseOperation, geometry);
    if (checked.failed())
    {
        return checked;
    }
    if (geometry.channelCount != plan.channelCount())
    {
        return patch_detail::patchLimitFailure(ErrorCode::InvalidArgument, kDenoiseOperation,
                                               "this plan was built for a different channel count",
                                               geometry.channelCount, plan.channelCount());
    }
    if (geometry.width < 3u)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kDenoiseOperation,
                                               "a patch denoise needs at least three samples in a row", geometry.width,
                                               3u);
    }

    const PatchDenoiseRequest& request = plan.request();
    const double tuningScale = plan.tuningScale();
    const std::uint64_t frameBytes =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount * sizeof(float);
    const bool loops = request.iterations > 1u;
    // A rule that can reject a sample needs the frame as it was gathered, because the write-back
    // decides what to leave untouched from the samples the caller supplied rather than from the
    // substitutes the estimator read. A rule that rejects nothing needs no such copy.
    const bool keepsPristine = selection.validity.rule() != ValidityRule::AllValid;
    const std::uint64_t frameCount = (loops ? 5u : 3u) + (keepsPristine ? 1u : 0u);
    const std::uint64_t deviationBytes = alignedBytes(static_cast<std::uint64_t>(geometry.height) * sizeof(double));
    Result<plane_detail::ScopedContextMemory> taken =
        plane_detail::ScopedContextMemory::take(execution, alignedBytes(frameBytes) * frameCount + deviationBytes,
                                                kDefaultBufferAlignmentBytes, kDenoiseOperation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    const plane_detail::ScopedContextMemory memory = std::move(taken).value();
    auto* const block = static_cast<std::uint8_t*>(memory.data());
    const auto frameAt = [&](std::uint64_t slot) noexcept {
        return WorkingFrame{.base = reinterpret_cast<float*>(block + alignedBytes(frameBytes) * slot),
                            .width = geometry.width,
                            .height = geometry.height,
                            .channels = geometry.channelCount};
    };
    const WorkingFrame original = frameAt(0u);
    const WorkingFrame estimate = frameAt(1u);
    const WorkingFrame basic = frameAt(2u);
    const WorkingFrame input = loops ? frameAt(3u) : original;
    const WorkingFrame lastGood = loops ? frameAt(4u) : original;
    const WorkingFrame pristine = keepsPristine ? frameAt(loops ? 5u : 3u) : original;
    const std::span<double> deviations{reinterpret_cast<double*>(block + alignedBytes(frameBytes) * frameCount),
                                       static_cast<std::size_t>(geometry.height)};

    PatchDenoiseResult result{};
    result.filter.channelCount = geometry.channelCount;
    result.filter.imagePasses = plan.imagePasses();
    result.filter.workingBytes = memory.bytes();
    result.filter.precision = ComputePrecision::Float32;
    result.filter.run.selection.total =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;

    const Status gathered = gatherFrame(execution, sampleType, source, sourceLayout, pristine, kDenoiseOperation);
    if (gathered.failed())
    {
        return gathered;
    }
    if (keepsPristine)
    {
        std::copy_n(pristine.base, pristine.sampleCount(), original.base);
    }
    const Status repaired = substituteFrame(execution, original, selection, static_cast<float>(request.invalidSample),
                                            result.substitutedSamples, kDenoiseOperation);
    if (repaired.failed())
    {
        return repaired;
    }
    scaleFrame(original, static_cast<float>(tuningScale));

    double inputNoise = 0.0;
    const Status measured = estimateFrameNoise(execution, original, deviations, inputNoise, &pristine, &selection);
    if (measured.failed())
    {
        return measured;
    }
    result.inputNoise = inputNoise / tuningScale;

    const double statedSigma = request.measureNoise ? inputNoise : request.sigma * tuningScale;
    result.effectiveSigma = statedSigma / tuningScale;
    const Status sigmaChecked = checkEstimatorSigma(statedSigma);
    if (sigmaChecked.failed())
    {
        return sigmaChecked;
    }

    // The tuning is derived once from the noise level the first iteration will run at, so the frame
    // geometry can be refused before any work rather than in the middle of the second pass.
    const EstimatorTuning tuning{.first = plan.passParameters(statedSigma, false),
                                 .second = plan.passParameters(statedSigma, true)};
    const std::uint32_t needed = requiredExtent(tuning);
    if (geometry.width < needed || geometry.height < needed)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kDenoiseOperation,
                                               "this noise level needs a frame at least one search window across",
                                               (std::min)(geometry.width, geometry.height), needed);
    }

    EstimatorCounters counters{};
    std::copy_n(original.base, original.sampleCount(), estimate.base);
    double lastMeasured = inputNoise;

    for (std::uint32_t iteration = 0u; iteration < request.iterations; ++iteration)
    {
        const Status cancelled = execution.checkCancellation();
        if (cancelled.failed())
        {
            return cancelled;
        }
        double iterationSigma = statedSigma;
        if (loops)
        {
            const auto mix = static_cast<float>(request.reinjection);
            const std::uint64_t count = original.sampleCount();
            for (std::uint64_t i = 0u; i < count; ++i)
            {
                input.base[i] = mix * estimate.base[i] + (1.0f - mix) * original.base[i];
            }
            double iterationNoise = 0.0;
            const Status remeasured =
                estimateFrameNoise(execution, input, deviations, iterationNoise, &pristine, &selection);
            if (remeasured.failed())
            {
                return remeasured;
            }
            if (iteration > 0u && iterationNoise > lastMeasured * kConvergenceTolerance)
            {
                std::copy_n(lastGood.base, lastGood.sampleCount(), estimate.base);
                result.convergenceStopped = true;
                break;
            }
            lastMeasured = iterationNoise;
            if (request.measureNoise)
            {
                iterationSigma = iterationNoise;
            }
        }

        if (request.varianceStabilisation)
        {
            applyVarianceStabilisation(input, true);
            double stabilised = 0.0;
            const Status remeasured =
                estimateFrameNoise(execution, input, deviations, stabilised, &pristine, &selection);
            if (remeasured.failed())
            {
                return remeasured;
            }
            iterationSigma = stabilised;
        }

        const Status iterationSigmaChecked = checkEstimatorSigma(iterationSigma);
        if (iterationSigmaChecked.failed())
        {
            return iterationSigmaChecked;
        }

        const EstimatorTuning iterationTuning{.first = plan.passParameters(iterationSigma, false),
                                              .second = plan.passParameters(iterationSigma, true)};
        const std::uint32_t iterationNeeded = requiredExtent(iterationTuning);
        if (geometry.width < iterationNeeded || geometry.height < iterationNeeded)
        {
            return patch_detail::patchLimitFailure(
                ErrorCode::OutOfRange, kDenoiseOperation,
                "the measured noise level needs a frame at least one search window across",
                (std::min)(geometry.width, geometry.height), iterationNeeded);
        }
        Status denoised = Status::success();
        {
            // Every iteration is two of the operation's image passes, and the estimator's two passes
            // fill exactly that share.
            const ProgressRange share(execution, 2u * static_cast<std::uint64_t>(iteration),
                                      2u * static_cast<std::uint64_t>(iteration) + 2u, plan.imagePasses());
            denoised = patch_detail::runPatchEstimator(execution, input, basic, estimate, iterationTuning,
                                                       static_cast<float>(iterationSigma), counters, kDenoiseOperation);
        }
        if (denoised.failed())
        {
            return denoised;
        }
        ++result.iterationsRun;

        if (request.varianceStabilisation)
        {
            applyVarianceStabilisation(estimate, false);
        }

        if (loops && iteration + 1u < request.iterations)
        {
            // The subtract step: the answer less the reinjected share of its input, rescaled by
            // `1 / (1 - reinjection)`. The answer is kept first, so an iteration whose noise estimate
            // rises can fall back to it.
            std::copy_n(estimate.base, estimate.sampleCount(), lastGood.base);
            const auto inverse = 1.0f / (1.0f - static_cast<float>(request.reinjection));
            const auto mix = static_cast<float>(request.reinjection) * inverse;
            const std::uint64_t count = estimate.sampleCount();
            for (std::uint64_t i = 0u; i < count; ++i)
            {
                estimate.base[i] = estimate.base[i] * inverse - input.base[i] * mix;
            }
        }
    }
    result.patchGroups = counters.groups;
    result.homogeneousGroups = counters.homogeneousGroups;
    result.inversionFailures = counters.inversionFailures;
    result.tileCount = counters.tileCount;
    result.filter.workingBytes = memory.bytes() + counters.workingBytes;

    clampFrame(estimate);

    WorkingFrame answer = estimate;
    if (request.finishing == PatchDenoiseFinishing::GuidedDualDomain)
    {
        const double finishingSigma = request.finishingSigma > 0.0 ? request.finishingSigma * tuningScale : inputNoise;
        const GuidedShrinkageRequest guided{
            .sigma = finishingSigma, .whiteLevel = kPatchDenoiseTuningScale, .output = request.output};
        Result<GuidedShrinkagePlan> guidedPlan = planGuidedShrinkage(guided);
        if (!guidedPlan.hasValue())
        {
            return guidedPlan.status();
        }
        const std::uint32_t side = guidedPlan->transformSize();
        if (geometry.width < side || geometry.height < side)
        {
            return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kDenoiseOperation,
                                                   "the finishing stage needs a frame at least one transform across",
                                                   (std::min)(geometry.width, geometry.height), side);
        }
        const GuidedShrinkageGeometry shrinkage{.radius = guided.radius,
                                                .transformSize = side,
                                                .sigma = static_cast<float>(finishingSigma),
                                                .spatialSigma = static_cast<float>(guided.spatialSigma),
                                                .rangeFactor = static_cast<float>(guided.rangeFactor),
                                                .coverageThreshold = static_cast<float>(guided.coverageThreshold)};
        GuidedShrinkageCounters guidedCounters{};
        Status shrunk = Status::success();
        {
            // The finishing stage is the last image pass.
            const ProgressRange share(execution, plan.imagePasses() - 1u, plan.imagePasses(), plan.imagePasses());
            shrunk = patch_detail::runGuidedShrinkage(execution, original, estimate, basic, shrinkage, guidedCounters,
                                                      kDenoiseOperation);
        }
        if (shrunk.failed())
        {
            return shrunk;
        }
        result.finishingRan = true;
        result.finishingPatches = guidedCounters.patchesPlaced;
        result.filter.workingBytes = memory.bytes() + (std::max)(counters.workingBytes, guidedCounters.workingBytes);
        answer = basic;
        clampFrame(answer);
    }

    scaleFrame(answer, static_cast<float>(1.0 / tuningScale));
    scaleFrame(original, static_cast<float>(1.0 / tuningScale));
    double outputNoise = 0.0;
    const Status remeasured = estimateFrameNoise(execution, answer, deviations, outputNoise, &pristine, &selection);
    if (remeasured.failed())
    {
        return remeasured;
    }
    result.outputNoise = outputNoise;

    const Status scattered =
        scatterFrame(execution, answer, pristine, selection, sampleType, destination, destinationLayout, source,
                     sourceLayout, request.output.writeBack, result.samplesChanged, kDenoiseOperation);
    if (scattered.failed())
    {
        return scattered;
    }
    // An iteration loop that stopped early skipped passes it had counted on, and the work is still done.
    execution.reportProgress(plan.imagePasses(), plan.imagePasses(), "patch denoise: done");
    result.filter.run.selection.valid = result.filter.run.selection.total - result.substitutedSamples;
    result.filter.run.outputsWritten = result.filter.run.selection.valid;
    result.filter.plannedWorkers = plannedWorkerCount(
        execution, geometry.height, TaskCost{.bytesPerItem = geometry.planeBytes, .operationsPerItem = 1u});
    return result;
}

GuidedShrinkagePlan::GuidedShrinkagePlan(const GuidedShrinkageRequest& request) noexcept : m_request(request)
{
    std::uint32_t side = 1u;
    while (side < 2u * request.radius + 1u)
    {
        side <<= 1u;
    }
    m_transformSize = side;
}

const GuidedShrinkageRequest& GuidedShrinkagePlan::request() const noexcept
{
    return m_request;
}

std::uint32_t GuidedShrinkagePlan::transformSize() const noexcept
{
    return m_transformSize;
}

std::uint64_t GuidedShrinkagePlan::imagePasses() const noexcept
{
    return m_transformSize == 0u ? 0u : 1u;
}

Result<GuidedShrinkagePlan> planGuidedShrinkage(const GuidedShrinkageRequest& request)
{
    if (!isPositiveFinite(request.sigma) || !isPositiveFinite(request.spatialSigma) ||
        !isPositiveFinite(request.rangeFactor) || !isPositiveFinite(request.coverageThreshold))
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kGuidedPlanOperation,
                                          "a guided shrinkage needs finite positive weights and a noise level");
    }
    if (!isPositiveFinite(request.whiteLevel) || request.whiteLevel < kMinimumPatchDenoiseWhiteLevel)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kGuidedPlanOperation,
                                          "a guided shrinkage needs a finite full scale above the minimum");
    }
    if (!std::isfinite(request.invalidSample))
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kGuidedPlanOperation,
                                          "the substitute for a rejected sample must be finite");
    }
    const long double noiseVariance = static_cast<long double>(request.sigma) * request.sigma;
    const long double relaxedRange = noiseVariance * request.rangeFactor * 10.0L;
    const long double relaxedSpatial = static_cast<long double>(request.spatialSigma) * request.spatialSigma * 2.0L;
    if (!fitsPositiveFloat(request.sigma) || !fitsPositiveFloat(request.spatialSigma) ||
        !fitsPositiveFloat(request.rangeFactor) || !fitsPositiveFloat(request.coverageThreshold) ||
        !fitsPositiveFloat(request.whiteLevel) || !fitsFiniteFloat(request.invalidSample) ||
        !fitsPositiveFloat(noiseVariance) || !fitsPositiveFloat(relaxedRange) || !fitsPositiveFloat(relaxedSpatial))
    {
        return patch_detail::patchFailure(
            ErrorCode::OutOfRange, kGuidedPlanOperation,
            "a guided-shrinkage numeric field or derived weight cannot be represented by Float32");
    }
    if (request.channelCoupling != GuidedShrinkageChannelCoupling::Independent &&
        request.channelCoupling != GuidedShrinkageChannelCoupling::Shared)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kGuidedPlanOperation,
                                          "the channel coupling is outside its enumeration");
    }
    const Status output = checkOutputPolicy(request.output, kGuidedPlanOperation);
    if (output.failed())
    {
        return output;
    }
    if (request.radius == 0u || request.radius > kMaximumGuidedShrinkageRadius)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kGuidedPlanOperation,
                                               "the patch radius lies between one and its maximum", request.radius,
                                               kMaximumGuidedShrinkageRadius);
    }
    return GuidedShrinkagePlan{request};
}

Result<GuidedShrinkageResult> shrinkImageWithGuide(const ExecutionContext& execution, SampleType sampleType,
                                                   const void* noisy, const ImageLayout& noisyLayout, const void* guide,
                                                   const ImageLayout& guideLayout, void* destination,
                                                   const ImageLayout& destinationLayout,
                                                   const GuidedShrinkagePlan& plan, const SpatialSelection& selection)
{
    if (plan.transformSize() == 0u)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kGuidedOperation,
                                          "this plan has not been validated");
    }
    SpatialGeometry geometry{};
    const Status checked =
        spatial_detail::checkSpatialImages(execution, sampleType, noisy, noisyLayout, destination, destinationLayout,
                                           plan.request().output, selection, kGuidedOperation, geometry);
    if (checked.failed())
    {
        return checked;
    }
    SpatialGeometry guideGeometry{};
    const Status guideChecked =
        spatial_detail::checkSpatialImages(execution, sampleType, guide, guideLayout, destination, destinationLayout,
                                           plan.request().output, selection, kGuidedOperation, guideGeometry);
    if (guideChecked.failed())
    {
        return guideChecked;
    }
    if (guideGeometry.width != geometry.width || guideGeometry.height != geometry.height ||
        guideGeometry.channelCount != geometry.channelCount)
    {
        return patch_detail::patchFailure(ErrorCode::InvalidArgument, kGuidedOperation,
                                          "the guide must describe the same picture as the noisy image");
    }
    if (geometry.channelCount > kMaximumPatchDenoiseChannels)
    {
        return patch_detail::patchLimitFailure(ErrorCode::Unsupported, kGuidedOperation,
                                               "the guided stage is defined for up to four channels",
                                               geometry.channelCount, kMaximumPatchDenoiseChannels);
    }
    const std::uint32_t side = plan.transformSize();
    if (geometry.width < side || geometry.height < side)
    {
        return patch_detail::patchLimitFailure(ErrorCode::OutOfRange, kGuidedOperation,
                                               "a guided shrinkage needs a frame at least one transform across",
                                               (std::min)(geometry.width, geometry.height), side);
    }

    const GuidedShrinkageRequest& request = plan.request();
    const bool shared = request.channelCoupling == GuidedShrinkageChannelCoupling::Shared && geometry.channelCount > 1u;
    const std::uint32_t workingChannels = shared ? 1u : geometry.channelCount;
    const bool keepsPristine = selection.validity.rule() != ValidityRule::AllValid;
    const std::uint64_t frameBytes =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount * sizeof(float);
    // Shared coupling needs a fourth frame: the averaged noisy and guide planes have to live
    // somewhere that is not the gathered frames, because those are what the shrinkage reads. A rule
    // that can reject a sample needs a fifth, holding the frame as it was gathered, because that is
    // what the write-back decides to leave untouched from.
    Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
        execution, alignedBytes(frameBytes) * (3u + (shared ? 1u : 0u) + (keepsPristine ? 1u : 0u)),
        kDefaultBufferAlignmentBytes, kGuidedOperation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    const plane_detail::ScopedContextMemory memory = std::move(taken).value();
    auto* const block = static_cast<std::uint8_t*>(memory.data());
    const auto frameAt = [&](std::uint64_t slot, std::uint32_t channels) noexcept {
        return WorkingFrame{.base = reinterpret_cast<float*>(block + alignedBytes(frameBytes) * slot),
                            .width = geometry.width,
                            .height = geometry.height,
                            .channels = channels};
    };
    const WorkingFrame noisyFrame = frameAt(0u, geometry.channelCount);
    const WorkingFrame guideFrame = frameAt(1u, geometry.channelCount);
    const WorkingFrame answer = frameAt(2u, geometry.channelCount);

    GuidedShrinkageResult result{};
    result.filter.channelCount = geometry.channelCount;
    result.filter.imagePasses = plan.imagePasses();
    result.filter.precision = ComputePrecision::Float32;
    result.filter.run.selection.total =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    result.transformSize = side;

    const WorkingFrame pristine = keepsPristine ? frameAt(3u + (shared ? 1u : 0u), geometry.channelCount) : noisyFrame;
    std::uint64_t substituted = 0u;
    const auto substitute = static_cast<float>(request.invalidSample);
    const Status gatheredNoisy = gatherFrame(execution, sampleType, noisy, noisyLayout, pristine, kGuidedOperation);
    if (gatheredNoisy.failed())
    {
        return gatheredNoisy;
    }
    if (keepsPristine)
    {
        std::copy_n(pristine.base, pristine.sampleCount(), noisyFrame.base);
    }
    const Status repairedNoisy =
        substituteFrame(execution, noisyFrame, selection, substitute, substituted, kGuidedOperation);
    if (repairedNoisy.failed())
    {
        return repairedNoisy;
    }
    const Status gatheredGuide = gatherFrame(execution, sampleType, guide, guideLayout, guideFrame, kGuidedOperation);
    if (gatheredGuide.failed())
    {
        return gatheredGuide;
    }
    std::uint64_t guideSubstituted = 0u;
    const Status repairedGuide =
        substituteFrame(execution, guideFrame, selection, substitute, guideSubstituted, kGuidedOperation);
    if (repairedGuide.failed())
    {
        return repairedGuide;
    }
    result.substitutedSamples = substituted;

    // Shared coupling averages the channels into the first plane, shrinks that one frame, and writes
    // its answer to every channel: the monochromatic-noise path, asked for by name rather than
    // decided by comparing the channels of the image.
    const WorkingFrame averaged = shared ? frameAt(3u, geometry.channelCount) : WorkingFrame{};
    if (shared)
    {
        const std::uint64_t samples = noisyFrame.planeSamples();
        for (std::uint64_t i = 0u; i < samples; ++i)
        {
            double noisySum = 0.0;
            double guideSum = 0.0;
            for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
            {
                noisySum += static_cast<double>(noisyFrame.plane(channel)[i]);
                guideSum += static_cast<double>(guideFrame.plane(channel)[i]);
            }
            averaged.base[i] = static_cast<float>(noisySum / geometry.channelCount);
            averaged.plane(1u)[i] = static_cast<float>(guideSum / geometry.channelCount);
        }
    }

    const WorkingFrame workingNoisy{.base = shared ? averaged.base : noisyFrame.base,
                                    .width = geometry.width,
                                    .height = geometry.height,
                                    .channels = workingChannels};
    const WorkingFrame workingGuide{.base = shared ? averaged.plane(1u) : guideFrame.base,
                                    .width = geometry.width,
                                    .height = geometry.height,
                                    .channels = workingChannels};
    const WorkingFrame workingAnswer{
        .base = answer.base, .width = geometry.width, .height = geometry.height, .channels = workingChannels};
    const GuidedShrinkageGeometry shrinkage{.radius = request.radius,
                                            .transformSize = side,
                                            .sigma = static_cast<float>(request.sigma),
                                            .spatialSigma = static_cast<float>(request.spatialSigma),
                                            .rangeFactor = static_cast<float>(request.rangeFactor),
                                            .coverageThreshold = static_cast<float>(request.coverageThreshold)};
    GuidedShrinkageCounters counters{};
    const Status shrunk = patch_detail::runGuidedShrinkage(execution, workingNoisy, workingGuide, workingAnswer,
                                                           shrinkage, counters, kGuidedOperation);
    if (shrunk.failed())
    {
        return shrunk;
    }
    result.patchesPlaced = counters.patchesPlaced;
    result.guideAcceptedPatches = counters.guideAcceptedPatches;
    result.tileCount = counters.tileCount;
    result.filter.workingBytes = memory.bytes() + counters.workingBytes;

    if (shared)
    {
        for (std::uint32_t channel = 1u; channel < geometry.channelCount; ++channel)
        {
            std::copy_n(answer.base, answer.planeSamples(), answer.plane(channel));
        }
    }

    const auto ceiling = static_cast<float>(request.whiteLevel);
    const std::uint64_t count = answer.sampleCount();
    for (std::uint64_t i = 0u; i < count; ++i)
    {
        answer.base[i] = std::clamp(answer.base[i], 0.0f, ceiling);
    }

    const Status scattered =
        scatterFrame(execution, answer, pristine, selection, sampleType, destination, destinationLayout, noisy,
                     noisyLayout, request.output.writeBack, result.samplesChanged, kGuidedOperation);
    if (scattered.failed())
    {
        return scattered;
    }
    result.filter.run.selection.valid = result.filter.run.selection.total - result.substitutedSamples;
    result.filter.run.outputsWritten = result.filter.run.selection.valid;
    result.filter.plannedWorkers = plannedWorkerCount(
        execution, geometry.height, TaskCost{.bytesPerItem = geometry.planeBytes, .operationsPerItem = 1u});
    return result;
}

} // namespace an
