// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - multiscale transform vocabulary, tap construction and planning.

#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planMultiscaleTransform";
constexpr const char* kTapsOperation = "an::writeMultiscaleScalingTaps";

/// The centre weight of one three-tap small-scale kernel.
///
/// @param scaling the function.
/// @return the weight, or zero for a function that is not one of the small-scale ladder.
[[nodiscard]] constexpr double smallScaleCenterWeight(MultiscaleScalingFunction scaling) noexcept
{
    switch (scaling)
    {
    case MultiscaleScalingFunction::SmallScale3:
        return 3.0;
    case MultiscaleScalingFunction::SmallScale4:
        return 4.0;
    case MultiscaleScalingFunction::SmallScale5:
        return 5.0;
    case MultiscaleScalingFunction::SmallScale6:
        return 6.0;
    case MultiscaleScalingFunction::SmallScale8:
        return 8.0;
    case MultiscaleScalingFunction::SmallScale12:
        return 12.0;
    case MultiscaleScalingFunction::SmallScale16:
        return 16.0;
    case MultiscaleScalingFunction::SmallScale24:
        return 24.0;
    case MultiscaleScalingFunction::SmallScale32:
        return 32.0;
    case MultiscaleScalingFunction::LinearInterpolation3:
    case MultiscaleScalingFunction::BSpline5:
    case MultiscaleScalingFunction::Gaussian5:
    case MultiscaleScalingFunction::Gaussian7:
    case MultiscaleScalingFunction::Gaussian9:
    case MultiscaleScalingFunction::Gaussian11:
        break;
    }
    return 0.0;
}

/// The standard deviation a fixed-width Gaussian scaling kernel is sampled at.
///
/// It is derived from the radius as `radius / 3`, so that the kernel is cut off at three standard
/// deviations; changing the width these four functions realize would silently change every image
/// processed with one.
///
/// @param tapCount the odd tap count.
/// @return the sigma.
[[nodiscard]] constexpr double gaussianScalingSigma(std::uint32_t tapCount) noexcept
{
    return static_cast<double>(tapCount / 2u) / 3.0;
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

} // namespace

const char* multiscaleScalingFunctionName(MultiscaleScalingFunction scaling) noexcept
{
    switch (scaling)
    {
    case MultiscaleScalingFunction::LinearInterpolation3:
        return "LinearInterpolation3";
    case MultiscaleScalingFunction::BSpline5:
        return "BSpline5";
    case MultiscaleScalingFunction::SmallScale3:
        return "SmallScale3";
    case MultiscaleScalingFunction::SmallScale4:
        return "SmallScale4";
    case MultiscaleScalingFunction::SmallScale5:
        return "SmallScale5";
    case MultiscaleScalingFunction::SmallScale6:
        return "SmallScale6";
    case MultiscaleScalingFunction::SmallScale8:
        return "SmallScale8";
    case MultiscaleScalingFunction::SmallScale12:
        return "SmallScale12";
    case MultiscaleScalingFunction::SmallScale16:
        return "SmallScale16";
    case MultiscaleScalingFunction::SmallScale24:
        return "SmallScale24";
    case MultiscaleScalingFunction::SmallScale32:
        return "SmallScale32";
    case MultiscaleScalingFunction::Gaussian5:
        return "Gaussian5";
    case MultiscaleScalingFunction::Gaussian7:
        return "Gaussian7";
    case MultiscaleScalingFunction::Gaussian9:
        return "Gaussian9";
    case MultiscaleScalingFunction::Gaussian11:
        return "Gaussian11";
    }
    return "Unknown";
}

const char* multiscaleAlgorithmName(MultiscaleAlgorithm algorithm) noexcept
{
    switch (algorithm)
    {
    case MultiscaleAlgorithm::Starlet:
        return "Starlet";
    case MultiscaleAlgorithm::MultiscaleLinear:
        return "MultiscaleLinear";
    }
    return "Unknown";
}

Result<std::uint32_t> writeMultiscaleScalingTaps(MultiscaleScalingFunction scaling, std::span<double> taps)
{
    if (!isMultiscaleScalingFunction(scaling))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kTapsOperation,
                                 "the scaling function is not one this engine implements");
    }
    const std::uint32_t tapCount = multiscaleScalingTapCount(scaling);
    if (taps.size() < tapCount)
    {
        return multiscaleLimitFailure(ErrorCode::OutOfRange, kTapsOperation,
                                      "the array is shorter than this scaling function has taps", taps.size(),
                                      tapCount);
    }

    if (scaling == MultiscaleScalingFunction::LinearInterpolation3)
    {
        taps[0] = 0.25;
        taps[1] = 0.5;
        taps[2] = 0.25;
        return tapCount;
    }
    if (scaling == MultiscaleScalingFunction::BSpline5)
    {
        taps[0] = 1.0 / 16.0;
        taps[1] = 4.0 / 16.0;
        taps[2] = 6.0 / 16.0;
        taps[3] = 4.0 / 16.0;
        taps[4] = 1.0 / 16.0;
        return tapCount;
    }
    const double center = smallScaleCenterWeight(scaling);
    if (center > 0.0)
    {
        const double sum = center + 2.0;
        taps[0] = 1.0 / sum;
        taps[1] = center / sum;
        taps[2] = 1.0 / sum;
        return tapCount;
    }

    // The four fixed-width Gaussians. They are produced by the same writer the Gaussian blur uses,
    // so a nine-tap scaling function and a nine-tap blur are the same coefficients.
    const Result<std::uint32_t> written =
        writeGaussianTaps(gaussianScalingSigma(tapCount), tapCount / 2u, taps.subspan(0u, tapCount));
    if (!written.hasValue())
    {
        return written.status();
    }
    return tapCount;
}

MultiscaleTransformPlan::MultiscaleTransformPlan(const MultiscaleTransformRequest& request) noexcept
    : m_request(request), m_layerCount(request.layerCount)
{
}

const MultiscaleTransformRequest& MultiscaleTransformPlan::request() const noexcept
{
    return m_request;
}

std::uint32_t MultiscaleTransformPlan::layerCount() const noexcept
{
    return m_layerCount;
}

std::uint32_t MultiscaleTransformPlan::detailLayerCount() const noexcept
{
    return m_layerCount > 0u ? m_layerCount - 1u : 0u;
}

std::uint32_t MultiscaleTransformPlan::residualLayer() const noexcept
{
    return detailLayerCount();
}

std::uint32_t MultiscaleTransformPlan::layerScale(std::uint32_t detailLayer) const noexcept
{
    if (detailLayer >= detailLayerCount())
    {
        return 0u;
    }
    if (m_request.spacing == MultiscaleLayerSpacing::Linear)
    {
        const std::uint32_t step = (std::max)(std::uint32_t{1}, m_request.linearScale);
        const std::uint64_t scale = 1u + static_cast<std::uint64_t>(detailLayer) * step;
        return scale <= (std::numeric_limits<std::uint32_t>::max)() ? static_cast<std::uint32_t>(scale) : 0u;
    }
    return std::uint32_t{1} << detailLayer;
}

std::uint32_t MultiscaleTransformPlan::layerDilation(std::uint32_t detailLayer) const noexcept
{
    if (detailLayer >= detailLayerCount())
    {
        return 0u;
    }
    return m_request.algorithm == MultiscaleAlgorithm::Starlet ? layerScale(detailLayer) : 1u;
}

double MultiscaleTransformPlan::layerSigma(std::uint32_t detailLayer) const noexcept
{
    if (detailLayer >= detailLayerCount() || m_request.algorithm != MultiscaleAlgorithm::MultiscaleLinear)
    {
        return 0.0;
    }
    return static_cast<double>(layerScale(detailLayer)) * kMultiscaleLinearScaleToSigma;
}

std::uint32_t MultiscaleTransformPlan::layerTapCount(std::uint32_t detailLayer) const noexcept
{
    if (detailLayer >= detailLayerCount())
    {
        return 0u;
    }
    if (m_request.algorithm == MultiscaleAlgorithm::Starlet)
    {
        return multiscaleScalingTapCount(m_request.scaling);
    }
    const double sigma = layerSigma(detailLayer);
    const double radius = std::ceil(kDefaultGaussianTruncation * sigma);
    const std::uint32_t whole =
        radius >= static_cast<double>(kMaxStencilRadius) ? kMaxStencilRadius : static_cast<std::uint32_t>(radius);
    return 2u * (std::max)(std::uint32_t{1}, whole) + 1u;
}

std::uint32_t MultiscaleTransformPlan::layerRadius(std::uint32_t detailLayer) const noexcept
{
    const std::uint32_t tapCount = layerTapCount(detailLayer);
    if (tapCount == 0u)
    {
        return 0u;
    }
    return (tapCount / 2u) * layerDilation(detailLayer);
}

std::uint64_t MultiscaleTransformPlan::imagePasses() const noexcept
{
    if (m_layerCount == 0u)
    {
        return 0u;
    }
    // Two convolution passes and one subtraction per detail layer, and one copy of the residual.
    // Gather and scatter are representation transfers rather than algorithm passes.
    return static_cast<std::uint64_t>(detailLayerCount()) * 3u + 1u;
}

std::uint64_t MultiscaleTransformPlan::layerBytes(std::uint32_t width, std::uint32_t height,
                                                  std::uint32_t channelCount) const noexcept
{
    return static_cast<std::uint64_t>(width) * height * channelCount * m_layerCount * sizeof(float);
}

Result<MultiscaleTransformPlan> planMultiscaleTransform(const MultiscaleTransformRequest& request)
{
    if (!isMultiscaleAlgorithm(request.algorithm))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the transform is not one this engine implements");
    }
    if (!isMultiscaleScalingFunction(request.scaling))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the scaling function is not one this engine implements");
    }
    if (!isMultiscaleLayerSpacing(request.spacing))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the layer spacing is not one this engine implements");
    }
    if (request.spacing == MultiscaleLayerSpacing::Linear && request.linearScale == 0u)
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "a linear layer spacing needs a step of at least one");
    }
    if (request.layerCount < kMinMultiscaleLayers || request.layerCount > kMaxMultiscaleLayers)
    {
        return multiscaleLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                      "a decomposition holds between two and the stated maximum number of layers",
                                      request.layerCount, kMaxMultiscaleLayers);
    }
    if (request.borders.border == KernelBorderMode::NotApplicable || !isKernelBorderMode(request.borders.border))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the border must be one of the modes that supply a neighbour");
    }
    if (!std::isfinite(request.borders.constantBorder) || !std::isfinite(request.borders.invalidNeighbor))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the border and invalid-neighbour substitutes must be finite");
    }
    if (!isOutputPrecisionContract(request.output.contract))
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the output contract is not one this engine knows");
    }
    const Status policy = checkConversionPolicy(request.output.writeBack, kPlanOperation);
    if (policy.failed())
    {
        return policy;
    }
    if (request.output.writeBack.domain != DomainMapping::CodeValue)
    {
        return multiscaleFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "a multiscale transform carries its samples across as the numbers they are; a "
                                 "full-scale domain mapping would make it a transform and a domain change at once");
    }

    const std::uint32_t deepest = request.layerCount - 2u;
    const std::uint64_t deepestScale = request.spacing == MultiscaleLayerSpacing::Linear
                                           ? 1u + static_cast<std::uint64_t>(deepest) * request.linearScale
                                           : std::uint64_t{1} << deepest;
    if (deepestScale > (std::numeric_limits<std::uint32_t>::max)())
    {
        return multiscaleLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                      "the deepest layer scale does not fit the plan's published scale type; ask "
                                      "for fewer layers or a smaller linear step",
                                      deepestScale, (std::numeric_limits<std::uint32_t>::max)());
    }
    const MultiscaleTransformPlan plan{request};
    if (request.algorithm == MultiscaleAlgorithm::Starlet)
    {
        const std::uint32_t dilation = plan.layerDilation(deepest);
        if (dilation > kMaxStencilDilation)
        {
            return multiscaleLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                          "the deepest starlet layer needs a tap spacing above the stated maximum; "
                                          "ask for fewer layers or a smaller linear step",
                                          dilation, kMaxStencilDilation);
        }
    }
    else
    {
        const double wanted = std::ceil(kDefaultGaussianTruncation * plan.layerSigma(deepest));
        if (wanted > static_cast<double>(kMaxStencilRadius))
        {
            return multiscaleLimitFailure(
                ErrorCode::OutOfRange, kPlanOperation,
                "the deepest multiscale-linear layer needs a Gaussian radius above the stated maximum; ask for "
                "fewer layers or a smaller linear step",
                static_cast<std::uint64_t>(wanted), kMaxStencilRadius);
        }
    }
    return plan;
}

} // namespace an
