// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the stack's frame weighting reads of a light, what a group asks of it, and the weights it derives.

#include "stdafx.hpp"
#include "StackWeightingInputs.h"

#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace
{

/// Whether a figure is one the engine takes in a measurement: finite and strictly positive.
[[nodiscard]] bool takenByTheEngine(const double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// The stack a set of frames' weights are derived for: the smallest image that states one, since a stack's frame
/// weights depend on its frames' measurements, its weighting and its reference alone.
[[nodiscard]] std::optional<an::ImageDescriptor> weightingStackDescriptor()
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                  .sampleType = an::SampleType::Float32,
                                                                  .channelSchema = an::ChannelSchema::Mono,
                                                                  .colorSpace = an::ColorSpace::Unspecified,
                                                                  .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                  .cfa = {},
                                                                  .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                  .pixelAspectRatio = 1.0});
}

} // namespace

namespace ImageIntegration
{

an::StackFrameMeasurement stackFrameMeasurementFor(const FrameQualityMetrics& metrics,
                                                   const FrameNoiseStatistics& noise)
{
    an::StackFrameMeasurement measurement;
    if (metrics.stars > 0)
    {
        measurement.starCount = static_cast<std::uint32_t>(metrics.stars);
        // The registration records an eccentricity of one where it measured no roundness; the engine refuses a
        // roundness of zero in any light's record, which would refuse the group's weights whatever they read.
        const double roundness = 1.0 - metrics.eccentricity;
        if (takenByTheEngine(roundness))
        {
            measurement.medianRoundness = (std::min)(roundness, 1.0);
        }
    }
    if (takenByTheEngine(metrics.fwhm))
    {
        measurement.fullWidth = metrics.fwhm;
    }
    if (takenByTheEngine(metrics.snr))
    {
        measurement.signalToNoise = metrics.snr;
    }
    if (takenByTheEngine(noise.background_noise))
    {
        measurement.backgroundNoise = noise.background_noise;
    }
    if (takenByTheEngine(noise.global_scale))
    {
        measurement.dispersion = noise.global_scale;
    }
    else if (takenByTheEngine(noise.global_mad))
    {
        measurement.dispersion = noise.global_mad;
    }
    measurement.photometric = toPhotometricFrameMeasurement(metrics);
    return measurement;
}

bool carriesWeightingFigure(const an::StackWeighting weighting, const an::StackFrameMeasurement& measurement)
{
    switch (weighting)
    {
    case an::StackWeighting::Noise:
        return measurement.backgroundNoise.has_value();
    case an::StackWeighting::StarCount:
        return measurement.starCount.has_value();
    case an::StackWeighting::Fwhm:
        return measurement.fullWidth.has_value();
    case an::StackWeighting::Photometric:
        return measurement.photometric.zeroPointMagnitudes.has_value();
    default:
        break;
    }
    return true;
}

QString weightingInputDescription(const an::StackWeighting weighting)
{
    switch (weighting)
    {
    case an::StackWeighting::Noise:
        return QStringLiteral("per-frame background noise and normalization scale");
    case an::StackWeighting::StarCount:
        return QStringLiteral("per-frame star counts");
    case an::StackWeighting::Fwhm:
        return QStringLiteral("per-frame FWHM");
    case an::StackWeighting::Photometric:
        return QStringLiteral("per-frame photometric zero points (requires plate solving)");
    default:
        break;
    }
    return QStringLiteral("per-frame registration metrics");
}

an::StackWeightingRequest stackWeightingRequestFor(const StackingSettings& settings, const an::StackWeighting weighting,
                                                   const an::StarCountScaling scaling)
{
    an::StackWeightingRequest request;
    request.method = weighting;
    request.minimumWeight = std::clamp(settings.minWeightFloor, 0.0, 1.0);
    // The group's floor is the photometric model's too: it raises a frame its confidence and alignment terms take
    // below it on the model's raw scale.
    request.photometric.minimumRawWeight = request.minimumWeight;
    request.starCount.scaling = scaling;
    if (std::isfinite(settings.exponentialAlpha) && settings.exponentialAlpha > 0.0)
    {
        request.starCount.exponentialAlpha = settings.exponentialAlpha;
    }
    request.perChannelFwhm = weighting == an::StackWeighting::Fwhm && settings.perChannelFwhmWeighting;
    return request;
}

std::uint32_t stackWeightingReferenceIndex(const std::span<const FrameId> frames,
                                           const std::optional<FrameId>& reference)
{
    if (frames.empty())
    {
        return 0u;
    }
    const auto found = reference.has_value() ? std::find(frames.begin(), frames.end(), *reference) : frames.end();
    const auto chosen = found != frames.end() ? found : std::min_element(frames.begin(), frames.end());
    return static_cast<std::uint32_t>(std::distance(frames.begin(), chosen));
}

an::Result<DerivedStackWeights> deriveStackWeights(const an::ExecutionContext& execution,
                                                   const an::StackWeightingRequest& weighting,
                                                   const std::span<const an::StackFrameMeasurement> measurements,
                                                   const std::uint32_t referenceIndex)
{
    const std::optional<an::ImageDescriptor> descriptor = weightingStackDescriptor();
    if (!descriptor.has_value() || measurements.empty())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "No frame weights are derived for no frames.");
    }
    an::StackRequest request{
        .input = an::StackInputDescription{.frameCount = static_cast<std::uint32_t>(measurements.size()),
                                           .descriptor = *descriptor}};
    request.weighting = weighting;
    request.normalization.referenceFrame = referenceIndex;
    const an::StackModelError refused = an::StackModel::check(request);
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    if (refused != an::StackModelError::None || !model.has_value())
    {
        return an::Status::failure(an::stackModelErrorCode(refused),
                                   std::string("No stack takes these frame weights: ") +
                                       an::stackModelErrorName(refused));
    }
    DerivedStackWeights derived;
    derived.weights.assign(measurements.size(), 1.0);
    an::Result<an::StackFrameWeightingResult> computed =
        an::computeStackFrameWeights(execution, *model, measurements, derived.weights);
    if (!computed.hasValue())
    {
        return computed.status();
    }
    derived.result = std::move(*computed);
    return derived;
}

} // namespace ImageIntegration
