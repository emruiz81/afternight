// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - validating a star detection, and measuring the channel it runs on.
//
// The half of the star surface that answers before any pixel is read: whether a frame can be
// detected on at all, which channel that means, what the search rectangle really is, how much
// working memory the detection will hold, and what the sky under it looks like. Keeping it here
// rather than inside the detector is what lets a caller running a sequence of like frames validate
// once, and what lets a caller who wants only the noise of a channel ask for that alone.

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>

#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include "algorithms/stars/StarsInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace an
{

namespace
{

using stars_detail::failure;
using stars_detail::isFinitePositive;
using stars_detail::parameterFailure;
using stars_detail::rangeFailure;

constexpr const char* kFrameOperation = "an::StarFrameView::create";
constexpr const char* kPlanOperation = "an::planStarDetection";
constexpr const char* kMeasureOperation = "an::measureStarChannel";

/// The largest search radius a candidate scan will dominate a box of.
///
/// Thirty-two, which is a sixty-five by sixty-five box. Past that the scan is comparing every
/// sample against four thousand neighbours and the answer is a blob-finder rather than a star
/// detector.
constexpr std::uint32_t kMaxSearchRadius = 32u;

/// The largest smoothing sigma the candidate scan accepts, in pixels.
constexpr double kMaxSmoothingSigma = 16.0;

/// Whether a pointer is aligned for a representation the star surface reads.
[[nodiscard]] bool pointerIsAligned(const void* pointer, SampleType sampleType) noexcept
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uint32_t width = sampleTypeSizeBytes(sampleType);
    return width == 0u || address % width == 0u;
}

/// Whether a validity claim and its mask agree with each other and with the frame.
[[nodiscard]] Status checkValidityBinding(const KernelValidityView& validity, const ImageExtent& extent,
                                          const char* operation)
{
    if (validity.maskPlane != 0u)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "a frame-wide star validity view starts at mask plane zero", "validity");
    }
    if (validity.descriptor.needsMask())
    {
        if (validity.mask == nullptr)
        {
            return parameterFailure(operation, ErrorCode::InvalidArgument,
                                    "an explicit validity rule needs the mask that decides it", "validity");
        }
        return checkMaskCovers(*validity.mask, extent, operation);
    }
    if (validity.mask != nullptr)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "a validity mask was supplied for a rule that does not read one", "validity");
    }
    return Status::success();
}

/// Rounds a byte count up to the engine's buffer alignment.
[[nodiscard]] std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) & ~(kDefaultBufferAlignmentBytes - 1u);
}

} // namespace

const char* starDetectionSmoothingName(StarDetectionSmoothing smoothing) noexcept
{
    switch (smoothing)
    {
    case StarDetectionSmoothing::Gaussian:
        return "Gaussian";
    case StarDetectionSmoothing::RepeatedBox:
        return "RepeatedBox";
    }
    return "UnknownStarDetectionSmoothing";
}

const char* starRejectionReasonName(StarRejectionReason reason) noexcept
{
    switch (reason)
    {
    case StarRejectionReason::NonFiniteWidth:
        return "NonFiniteWidth";
    case StarRejectionReason::NonFinitePosition:
        return "NonFinitePosition";
    case StarRejectionReason::NoMagnitude:
        return "NoMagnitude";
    case StarRejectionReason::WidthBelowColourFloor:
        return "WidthBelowColourFloor";
    case StarRejectionReason::NonPositiveWidth:
        return "NonPositiveWidth";
    case StarRejectionReason::RoundnessOutsideWindow:
        return "RoundnessOutsideWindow";
    case StarRejectionReason::WidthAboveEstimate:
        return "WidthAboveEstimate";
    case StarRejectionReason::ResidualTooLarge:
        return "ResidualTooLarge";
    case StarRejectionReason::AmplitudeOutsideWindow:
        return "AmplitudeOutsideWindow";
    case StarRejectionReason::BetaBelowMinimum:
        return "BetaBelowMinimum";
    }
    return "UnknownStarRejectionReason";
}

Result<StarFrameView> StarFrameView::create(const void* samples, std::uint64_t bufferBytes, const ImageLayout& layout,
                                            const ImageDescriptor& descriptor, const KernelValidityView& validity)
{
    if (samples == nullptr)
    {
        return parameterFailure(kFrameOperation, ErrorCode::InvalidArgument, "a star frame needs sample storage",
                                "samples");
    }
    if (descriptor.numericDomain() == NumericDomain::EncodedNonlinear)
    {
        return failure(kFrameOperation, ErrorCode::Unsupported,
                       "a point-spread function has no meaning over a nonlinear encoded domain");
    }
    if (descriptor.isMosaiced())
    {
        return failure(kFrameOperation, ErrorCode::Unsupported,
                       "stars are measured after the mosaic is resolved, not on the filter array");
    }
    if (!layoutDescribes(descriptor, layout) || storageArrangementOf(layout) == StorageArrangement::SemiPlanar)
    {
        return failure(kFrameOperation, ErrorCode::InvalidArgument,
                       "the layout does not describe this frame in a planar or interleaved arrangement");
    }
    if (bufferBytes < layout.totalBytes())
    {
        return parameterFailure(kFrameOperation, ErrorCode::InvalidArgument,
                                "the frame buffer does not cover every byte named by its layout", "bufferBytes");
    }
    if (!pointerIsAligned(samples, descriptor.sampleType()))
    {
        return parameterFailure(kFrameOperation, ErrorCode::InvalidArgument,
                                "the frame storage is not aligned for its sample type", "samples");
    }
    const Status validityReady = checkValidityBinding(validity, descriptor.extent(), kFrameOperation);
    if (validityReady.failed())
    {
        return validityReady;
    }
    return StarFrameView{samples, bufferBytes, layout, descriptor, validity};
}

Result<StarDetectionPlan> planStarDetection(const StarDetectionRequest& request, const ImageDescriptor& descriptor,
                                            const ImageLayout& layout, std::optional<std::uint32_t> channel)
{
    if (!isPsfModel(request.model))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the model is not one this build implements", "model");
    }
    if (!isStarDetectionSmoothing(request.smoothing))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the smoothing is not one this build produces", "smoothing");
    }
    if (request.searchRadius == 0u || request.searchRadius > kMaxSearchRadius)
    {
        return rangeFailure(kPlanOperation, "the search radius lies between one and the maximum", "searchRadius",
                            request.searchRadius, kMaxSearchRadius);
    }
    if (request.convergence == 0u || request.convergence > 16u)
    {
        return rangeFailure(kPlanOperation, "the convergence multiplier lies between one and sixteen", "convergence",
                            request.convergence, 16u);
    }
    if (!isFinitePositive(request.noiseSigma))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument, "the noise sigma is finite and positive",
                                "noiseSigma");
    }
    if (!isFinitePositive(request.minimumRoundness) || request.minimumRoundness > 1.0)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the smallest roundness lies within the unit interval", "minimumRoundness");
    }
    if (!isFinitePositive(request.maximumRoundness) || request.maximumRoundness > 1.0 ||
        request.maximumRoundness < request.minimumRoundness)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the largest roundness lies within the unit interval and at or above the smallest",
                                "maximumRoundness");
    }
    if (!isFinitePositive(request.minimumBeta) || request.minimumBeta > kMoffatBetaCeiling)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the smallest exponent is finite, positive and at or below the ceiling", "minimumBeta");
    }
    if (!std::isfinite(request.minimumAmplitude) || !std::isfinite(request.maximumAmplitude) ||
        request.minimumAmplitude < 0.0 || request.maximumAmplitude < 0.0)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "an amplitude window is finite and not negative", "minimumAmplitude");
    }
    if (request.maximumAmplitude > 0.0 && request.maximumAmplitude < request.minimumAmplitude)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the amplitude window's upper bound is at or above its lower bound",
                                "maximumAmplitude");
    }
    if (!isFinitePositive(request.smoothingSigma) || request.smoothingSigma > kMaxSmoothingSigma)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the smoothing sigma is finite, positive and at or below sixteen pixels",
                                "smoothingSigma");
    }
    if (!isFinitePositive(request.fullScale))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument, "full scale is finite and positive",
                                "fullScale");
    }
    if (request.arcsecondsPerPixel.has_value() && !isFinitePositive(*request.arcsecondsPerPixel))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "a stated plate scale is finite and positive", "arcsecondsPerPixel");
    }
    if (request.maximumStars.has_value() && (*request.maximumStars == 0u || *request.maximumStars > kMaxStarsPerFrame))
    {
        return rangeFailure(kPlanOperation, "a star limit lies between one and the frame ceiling", "maximumStars",
                            request.maximumStars.value_or(0u), kMaxStarsPerFrame);
    }
    if (descriptor.numericDomain() == NumericDomain::EncodedNonlinear || descriptor.isMosaiced())
    {
        return failure(kPlanOperation, ErrorCode::Unsupported, "stars are measured on a linear, demosaiced frame");
    }
    if (!layoutDescribes(descriptor, layout) || storageArrangementOf(layout) == StorageArrangement::SemiPlanar)
    {
        return failure(kPlanOperation, ErrorCode::IncompatibleArguments,
                       "the layout does not describe this frame in a planar or interleaved arrangement");
    }

    const ImageExtent& extent = descriptor.extent();
    const std::uint32_t channelCount = extent.channelCount();
    std::uint32_t resolved = 0u;
    if (channel.has_value())
    {
        if (*channel >= channelCount)
        {
            return rangeFailure(kPlanOperation, "the detection channel is one the frame has", "channel", *channel,
                                channelCount == 0u ? 0u : channelCount - 1u);
        }
        resolved = *channel;
    }
    else
    {
        // Green for a three-channel frame, because it is the channel a colour sensor samples
        // twice and therefore the sharpest one, and channel zero otherwise.
        resolved = channelCount >= 3u ? 1u : 0u;
    }

    StatisticsRegion region{.originX = 0u, .originY = 0u, .width = extent.width(), .height = extent.height()};
    if (request.region.has_value())
    {
        region = *request.region;
        const std::uint64_t right = static_cast<std::uint64_t>(region.originX) + region.width;
        const std::uint64_t bottom = static_cast<std::uint64_t>(region.originY) + region.height;
        if (region.width == 0u || region.height == 0u || right > extent.width() || bottom > extent.height())
        {
            return failure(kPlanOperation, ErrorCode::OutOfRange, "the search region does not lie inside the frame");
        }
    }
    // Two search radii of margin on each side: the scan needs one for the local-maximum box and one
    // more for the second-derivative walk that sizes the fitting box, plus the four samples the
    // walk's own edge tests reserve.
    const std::uint32_t margin = 4u * request.searchRadius + 8u;
    if (region.width <= margin || region.height <= margin)
    {
        return rangeFailure(kPlanOperation,
                            "the searched rectangle is too small for the search radius: each side must be longer "
                            "than four search radii and eight pixels",
                            "region", (std::min)(region.width, region.height), margin);
    }

    const bool alreadyFloat = descriptor.sampleType() == SampleType::Float32 && layout.samplesPerPixel() == 1u &&
                              layout.rowStrideBytes() == static_cast<std::uint64_t>(extent.width()) * sizeof(float);
    const std::uint64_t planeSamples = static_cast<std::uint64_t>(extent.width()) * extent.height();
    const std::uint64_t planeBytes = alignedBytes(planeSamples * sizeof(float));
    const std::uint32_t frameRadius = static_cast<std::uint32_t>((std::min)(extent.width(), extent.height()) / 2u);
    const std::uint32_t maximumBoxRadius =
        (std::max)(request.searchRadius, (std::min)(kMaxPsfBoxRadius, frameRadius == 0u ? 1u : frameRadius));
    // A plan has no execution. Bound both planes and a widest-box fit for every worker a
    // context can admit; the actual allocation uses only the admitted workers and observed boxes.
    const std::uint64_t scratchBytes =
        alignedBytes((psfFitScratchLength(maximumBoxRadius) +
                      static_cast<std::uint64_t>(2u * maximumBoxRadius + 1u) * (2u * maximumBoxRadius + 1u)) *
                     sizeof(double));
    const std::uint64_t workingBytes = planeBytes * 2u + scratchBytes * kMaxConfigurableConcurrency;

    StarDetectionPlan plan;
    plan.m_request = request;
    plan.m_region = region;
    plan.m_channelIndex = resolved;
    plan.m_copiesDetectionPlane = !alreadyFloat;
    plan.m_workingBytesUpperBound = workingBytes;
    plan.m_maximumBoxRadius = maximumBoxRadius;
    plan.m_valid = true;
    return plan;
}

Result<StarChannelStatistics> measureStarChannel(const ExecutionContext& execution, const StarFrameView& frame,
                                                 std::uint32_t channelIndex,
                                                 const std::optional<StatisticsRegion>& region)
{
    if (!execution.valid())
    {
        return failure(kMeasureOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!frame.valid())
    {
        return failure(kMeasureOperation, ErrorCode::InvalidArgument, "the frame view holds nothing");
    }
    const ImageExtent& extent = frame.descriptor().extent();
    if (channelIndex >= extent.channelCount())
    {
        return rangeFailure(kMeasureOperation, "the measured channel is one the frame has", "channelIndex",
                            channelIndex, extent.channelCount() == 0u ? 0u : extent.channelCount() - 1u);
    }

    StatisticsSelection selection{};
    selection.validity = frame.validity().descriptor;
    selection.mask = frame.validity().mask;
    selection.region = region;

    const Result<AdvancedStatistics> advanced = computeImageAdvancedStatistics(
        execution, frame.descriptor().sampleType(), frame.samples(), frame.layout(), channelIndex, selection);
    if (!advanced.hasValue())
    {
        return advanced.status();
    }
    const Result<BasicStatistics> basic = computeImageBasicStatistics(
        execution, frame.descriptor().sampleType(), frame.samples(), frame.layout(), channelIndex, selection);
    if (!basic.hasValue())
    {
        return basic.status();
    }

    StarChannelStatistics statistics{};
    statistics.background = advanced->median;
    statistics.backgroundNoise = advanced->backgroundNoise;
    statistics.medianAbsoluteDeviation = advanced->medianAbsoluteDeviation;
    statistics.robustScale = advanced->iterativeKsigmaScale;
    statistics.admittedSamples = advanced->selection.valid;
    statistics.maximum = basic->maximum;
    statistics.channelIndex = channelIndex;
    return statistics;
}

} // namespace an
