// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SDK calls over an image's stars: the field's full width, the fitted stars, and the three deconvolutions - with
/// a Gaussian or Moffat PSF the caller states, or one composed from the image's own stars.
///
/// The star-derived PSF is composed as the SDK states it: of the stars whose fit has a finite, positive width on both
/// axes - and that are not saturated, when the caller says so - the median width on each axis (or the median mean
/// width on both, when circularized), the axial mean of the fitted angles, the median fitted Moffat beta above one or
/// 4.5 when none is, and the Moffat profile when at least as many stars were fitted with one as with a Gaussian. A
/// PSF with no stated size is four widths wide, odd and at least three samples.

#include "RuntimeBindings.h"

#include <AfterNightEngine/Algorithms/Deconvolution/Deconvolution.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace an::sdk
{

namespace
{

/// Seconds of arc per radian, for the plate scale a focal length and a pixel size give.
constexpr double kArcsecondsPerRadian = 206264.80624709636;

/// The Moffat beta a star-derived PSF takes when no star was fitted with one.
constexpr double kDefaultMoffatBeta = 4.5;

/// The PSF profile a deconvolution is asked for.
enum class SdkPsfProfile
{
    Automatic,
    Gaussian,
    Moffat
};

[[nodiscard]] SdkPsfProfile psfProfileFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "auto" || lower == "auto_detect" || lower == "stars" || lower == "star")
    {
        return SdkPsfProfile::Automatic;
    }
    if (lower == "gaussian")
    {
        return SdkPsfProfile::Gaussian;
    }
    if (lower == "moffat" || lower == "moffat_bfree" || lower == "moffat_free" || lower == "moffat_bfixed" ||
        lower == "moffat_fixed")
    {
        return SdkPsfProfile::Moffat;
    }
    throw std::runtime_error("Unsupported deconvolution PSF profile: " + name);
}

[[nodiscard]] an::PsfModel starModelFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "gaussian")
    {
        return an::PsfModel::Gaussian;
    }
    if (lower == "moffat_bfree" || lower == "moffat_free" || lower == "moffat")
    {
        return an::PsfModel::MoffatFreeBeta;
    }
    if (lower == "moffat_bfixed" || lower == "moffat_fixed")
    {
        return an::PsfModel::MoffatFixedBeta;
    }
    throw std::runtime_error("Unsupported star profile: " + name);
}

[[nodiscard]] const char* starModelSdkName(const an::PsfModel model) noexcept
{
    switch (model)
    {
    case an::PsfModel::MoffatFreeBeta:
        return "moffat_bfree";
    case an::PsfModel::MoffatFixedBeta:
        return "moffat_bfixed";
    case an::PsfModel::Gaussian:
    default:
        return "gaussian";
    }
}

[[nodiscard]] an::DeconvolutionConvolution convolutionFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "auto" || lower == "auto_select")
    {
        return an::DeconvolutionConvolution::Automatic;
    }
    if (lower == "direct" || lower == "spatial")
    {
        return an::DeconvolutionConvolution::Direct;
    }
    if (lower == "fft" || lower == "frequency")
    {
        return an::DeconvolutionConvolution::Transform;
    }
    throw std::runtime_error("Unsupported deconvolution convolution backend: " + name);
}

[[nodiscard]] an::DeconvolutionColor colorModeFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "auto" || lower == "auto_select")
    {
        return an::DeconvolutionColor::Automatic;
    }
    if (lower == "per_channel" || lower == "channels" || lower == "rgb_channels")
    {
        return an::DeconvolutionColor::PerChannel;
    }
    if (lower == "luminance" || lower == "luma" || lower == "preserve_chroma" || lower == "luminance_preserving_chroma")
    {
        return an::DeconvolutionColor::LuminancePreservingChroma;
    }
    throw std::runtime_error("Unsupported deconvolution color mode: " + name);
}

[[nodiscard]] an::DeconvolutionEdgeTaper edgeTaperFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "auto" || lower == "auto_select" || lower == "on" || lower == "enabled")
    {
        return an::DeconvolutionEdgeTaper::Automatic;
    }
    if (lower == "off" || lower == "none" || lower == "disabled")
    {
        return an::DeconvolutionEdgeTaper::Off;
    }
    if (lower == "manual" || lower == "custom" || lower == "radius")
    {
        return an::DeconvolutionEdgeTaper::Manual;
    }
    throw std::runtime_error("Unsupported deconvolution edge taper mode: " + name);
}

[[nodiscard]] an::DeconvolutionRegularization regularizationFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "none" || lower == "off" || lower == "disabled")
    {
        return an::DeconvolutionRegularization::None;
    }
    if (lower == "tv" || lower == "total_variation" || lower == "total-variation")
    {
        return an::DeconvolutionRegularization::TotalVariation;
    }
    if (lower == "fh" || lower == "hessian" || lower == "frobenius_hessian" || lower == "frobenius-hessian")
    {
        return an::DeconvolutionRegularization::HessianFrobenius;
    }
    throw std::runtime_error("Unsupported deconvolution regularization mode: " + name);
}

[[nodiscard]] an::DeconvolutionUpdate updateFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "multiplicative" || lower == "multiply" || lower == "mul")
    {
        return an::DeconvolutionUpdate::Multiplicative;
    }
    if (lower == "gradient_descent" || lower == "gradient-descent" || lower == "gradient" || lower == "gd")
    {
        return an::DeconvolutionUpdate::GradientDescent;
    }
    throw std::runtime_error("Unsupported deconvolution update method: " + name);
}

/// The channel a call names, the engine's choice for a negative one, or a raised refusal for one the image lacks.
[[nodiscard]] std::optional<std::uint32_t> detectionChannelOf(const RuntimeImage& image, const int channel,
                                                              const std::string_view function)
{
    const std::optional<an::ImageDescriptor> descriptor = image.asset()->asset().descriptor();
    const std::uint32_t channels = descriptor.has_value() ? descriptor->extent().channelCount() : 0u;
    if (channel < 0)
    {
        return std::nullopt;
    }
    if (static_cast<std::uint32_t>(channel) >= channels)
    {
        throw std::runtime_error(std::string(function) + " channel is out of range");
    }
    return static_cast<std::uint32_t>(channel);
}

[[nodiscard]] py::dict starToPythonDict(const an::DetectedStar& star)
{
    py::dict result;
    result[py::str("name")] = py::str("");
    result[py::str("x")] = star.positionX;
    result[py::str("y")] = star.positionY;
    result[py::str("x0")] = star.psf.centreX;
    result[py::str("y0")] = star.psf.centreY;
    result[py::str("background")] = star.psf.background;
    result[py::str("amplitude")] = star.psf.amplitude;
    result[py::str("sx")] = star.psf.scaleX;
    result[py::str("sy")] = star.psf.scaleY;
    result[py::str("fwhm")] = star.metrics.meanFullWidth;
    result[py::str("fwhm_x")] = star.psf.fullWidthX;
    result[py::str("fwhm_y")] = star.psf.fullWidthY;
    result[py::str("fwhm_x_arcsec")] = star.psf.fullWidthXArcseconds.value_or(0.0);
    result[py::str("fwhm_y_arcsec")] = star.psf.fullWidthYArcseconds.value_or(0.0);
    result[py::str("angle_degrees")] = star.psf.angleDegrees;
    result[py::str("rmse")] = star.psf.residualRootMeanSquare;
    result[py::str("roundness")] = star.metrics.roundness;
    result[py::str("circularity")] = star.metrics.circularity;
    result[py::str("snr")] = star.signalToNoise.value_or(0.0);
    result[py::str("magnitude")] = star.instrumentalMagnitude.value_or(0.0);
    result[py::str("saturated")] = star.psf.saturated;
    result[py::str("saturation")] = star.psf.saturationLevel;
    result[py::str("box_radius")] = static_cast<int>(star.psf.boxRadius);
    result[py::str("beta")] = star.psf.beta.value_or(-1.0);
    result[py::str("profile")] = py::str(starModelSdkName(star.psf.model));
    result[py::str("channel")] = static_cast<int>(star.channelIndex);
    result[py::str("units")] = py::str("px");
    result[py::str("ra")] = 0.0;
    result[py::str("dec")] = 0.0;
    result[py::str("has_world_coordinates")] = false;
    return result;
}

/// The median of the finite positive values, or nothing.
[[nodiscard]] std::optional<double> positiveMedian(std::vector<double> values)
{
    values.erase(std::remove_if(values.begin(), values.end(),
                                [](const double value) { return !std::isfinite(value) || value <= 0.0; }),
                 values.end());
    if (values.empty())
    {
        return std::nullopt;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2u;
    return values.size() % 2u == 0u ? (values[middle - 1u] + values[middle]) * 0.5 : values[middle];
}

/// The axial mean of angles in degrees, which a half turn leaves unchanged.
[[nodiscard]] double axialMeanDegrees(const std::vector<double>& angles)
{
    constexpr double kTiny = 1.0e-12;
    const double degreesToRadians = std::acos(-1.0) / 180.0;
    double sinSum = 0.0;
    double cosSum = 0.0;
    for (const double angle : angles)
    {
        if (std::isfinite(angle))
        {
            sinSum += std::sin(angle * degreesToRadians * 2.0);
            cosSum += std::cos(angle * degreesToRadians * 2.0);
        }
    }
    if (std::abs(sinSum) <= kTiny && std::abs(cosSum) <= kTiny)
    {
        return 0.0;
    }
    return std::atan2(sinSum, cosSum) / degreesToRadians * 0.5;
}

/// The side of a PSF with no stated size: four widths, odd, at least three samples.
[[nodiscard]] std::uint32_t automaticPsfSide(const double fullWidth)
{
    std::uint32_t side = (std::max)(3u, static_cast<std::uint32_t>(std::ceil(fullWidth * 4.0)));
    if (side % 2u == 0u)
    {
        ++side;
    }
    return side;
}

/// A built PSF: its samples and the plane over them.
struct BuiltPsf
{
    std::vector<float> samples;
    std::uint32_t side = 0u;

    [[nodiscard]] an::PlaneView<const float> plane() const
    {
        const std::optional<an::PlaneView<const float>> view = an::PlaneView<const float>::create(
            std::span<const float>(samples), side, side, 1u, static_cast<std::uint64_t>(side) * sizeof(float));
        if (!view.has_value())
        {
            throw std::runtime_error("the deconvolution PSF does not describe a plane");
        }
        return *view;
    }
};

[[nodiscard]] BuiltPsf buildPsf(const an::PsfRequest& request, const std::string_view context)
{
    BuiltPsf psf;
    psf.side = require(an::psfExtent(request), context);
    psf.samples.assign(static_cast<std::size_t>(psf.side) * psf.side, 0.0f);
    const std::optional<an::PlaneView<float>> plane = an::PlaneView<float>::create(
        std::span<float>(psf.samples), psf.side, psf.side, 1u, static_cast<std::uint64_t>(psf.side) * sizeof(float));
    if (!plane.has_value())
    {
        throw std::runtime_error("the deconvolution PSF does not describe a plane");
    }
    require(an::buildPsf(request, *plane), context);
    return psf;
}

/// The PSF a caller states by its widths and profile.
[[nodiscard]] BuiltPsf statedPsf(const double fwhmX, const double fwhmY, const double angleDegrees, const int psfSize,
                                 const std::string& profile, const double beta, const char* automaticRefusal,
                                 const std::string_view context)
{
    const SdkPsfProfile resolved = psfProfileFromSdkName(profile);
    if (resolved == SdkPsfProfile::Automatic)
    {
        throw std::runtime_error(automaticRefusal);
    }
    const double widthY = fwhmY > 0.0 ? fwhmY : fwhmX;
    an::PsfRequest request;
    request.profile = resolved == SdkPsfProfile::Moffat ? an::PsfProfile::Moffat : an::PsfProfile::Gaussian;
    request.fwhmX = fwhmX;
    request.fwhmY = widthY;
    request.angleDegrees = angleDegrees;
    request.beta = beta;
    request.size = psfSize != 0 ? static_cast<std::uint32_t>(psfSize)
                                : (std::isfinite(fwhmX) && std::isfinite(widthY) && fwhmX > 0.0 && widthY > 0.0
                                       ? automaticPsfSide((std::max)(fwhmX, widthY))
                                       : 0u);
    return buildPsf(request, context);
}

/// The common part of a deconvolution's request, from the SDK's parameters, for an image of a representation.
[[nodiscard]] an::DeconvolutionCommon deconvolutionCommon(const an::SampleType outputType, const bool preserveFlux,
                                                          const bool clampToUnitRange, const double edgeTaperRadius,
                                                          const std::string& convolution, const std::string& colorMode,
                                                          const std::string& edgeTaperMode,
                                                          const int edgeTaperIterations, const char* iterationRefusal)
{
    an::DeconvolutionCommon common;
    common.preserveFlux = preserveFlux;
    common.clampToWhite = clampToUnitRange;
    // The samples are worked as stored, so the unit range of an integer image is its full scale.
    double whiteLevel = 1.0;
    (void)an::dispatchSampleType(outputType, [&]<typename SampleT>() {
        if constexpr (std::is_integral_v<SampleT>)
        {
            whiteLevel = static_cast<double>(std::numeric_limits<SampleT>::max());
        }
    });
    common.whiteLevel = whiteLevel;
    common.edgeTaperRadius = edgeTaperRadius;
    common.convolution = convolutionFromSdkName(convolution);
    common.color = colorModeFromSdkName(colorMode);
    common.edgeTaper = edgeTaperFromSdkName(edgeTaperMode);
    if (common.edgeTaper != an::DeconvolutionEdgeTaper::Off && edgeTaperIterations <= 0)
    {
        throw std::runtime_error(iterationRefusal);
    }
    common.edgeTaperIterations = static_cast<std::uint32_t>((std::max)(edgeTaperIterations, 0));
    return common;
}

/// The SDK's Richardson-Lucy request.
[[nodiscard]] an::RichardsonLucyRequest
richardsonLucyRequest(const an::SampleType outputType, const int iterations, const double tvRegularization,
                      const double damping, const double stoppingDelta, const bool preserveFlux,
                      const bool clampToUnitRange, const double relativeStoppingDelta, const double edgeTaperRadius,
                      const std::string& convolution, const std::string& colorMode, const std::string& edgeTaperMode,
                      const int edgeTaperIterations, const std::string& regularizationMode,
                      const std::string& updateMethod, const double gradientDescentStep)
{
    if (iterations <= 0)
    {
        throw std::runtime_error("Richardson-Lucy iterations must be greater than zero");
    }
    if (!std::isfinite(gradientDescentStep) || gradientDescentStep <= 0.0 || gradientDescentStep > 1.0)
    {
        throw std::runtime_error("Richardson-Lucy gradient_descent_step must be in the range (0, 1]");
    }
    an::RichardsonLucyRequest request;
    request.common = deconvolutionCommon(outputType, preserveFlux, clampToUnitRange, edgeTaperRadius, convolution,
                                         colorMode, edgeTaperMode, edgeTaperIterations,
                                         "Richardson-Lucy edge_taper_iterations must be greater than zero");
    request.iterations = static_cast<std::uint32_t>(iterations);
    request.regularization = regularizationFromSdkName(regularizationMode);
    request.regularizationStrength = tvRegularization;
    request.update = updateFromSdkName(updateMethod);
    request.gradientDescentStep = gradientDescentStep;
    request.damping = damping;
    request.stoppingDelta = stoppingDelta;
    request.relativeStoppingDelta = relativeStoppingDelta;
    return request;
}

/// Which deconvolution runs.
using DeconvolutionPlan = std::variant<an::RichardsonLucyPlan, an::WienerPlan>;

/// Deconvolves a runtime image into a new one of the output representation: the source converted to it first at full
/// scale, as the SDK always delivered its result.
[[nodiscard]] std::shared_ptr<RuntimeImage> deconvolve(const RuntimeImage& source, const an::SampleType outputType,
                                                       const BuiltPsf& psf, const DeconvolutionPlan& plan,
                                                       const std::string_view context)
{
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::AssetReadLease lease = source.asset()->readForEngine(context);
    an::consumer::OperationScope scope = admitOperation(*session, an::consumer::OperationPurpose::Apply, context);
    an::consumer::AnyImage input = require(convertedImageOf(scope, lease.image(), outputType), context);
    lease.release();
    const an::PlaneView<const float> psfPlane = psf.plane();
    an::consumer::AnyImage output = std::visit(
        [&](const auto& typed) -> an::consumer::AnyImage {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
            const auto view = typed.view();
            if (!descriptor.has_value() || !view.has_value())
            {
                throw std::runtime_error("Runtime image is not initialized");
            }
            an::Image<SampleT> restored = require(
                createCompactImage<SampleT>(scope.engine(), *descriptor, an::StorageInitialization::Uninitialized),
                context);
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = restored.write();
                if (!access.has_value())
                {
                    throw std::runtime_error("Runtime image channel buffer is null");
                }
                if (const auto* iterative = std::get_if<an::RichardsonLucyPlan>(&plan))
                {
                    (void)require(an::deconvolveImageRichardsonLucy(scope.execution(), an::Image<SampleT>::kSampleType,
                                                                    view->data(), view->layout(), access->view().data(),
                                                                    access->view().layout(), psfPlane, *iterative),
                                  context);
                }
                else
                {
                    (void)require(an::deconvolveImageWiener(scope.execution(), an::Image<SampleT>::kSampleType,
                                                            view->data(), view->layout(), access->view().data(),
                                                            access->view().layout(), psfPlane,
                                                            std::get<an::WienerPlan>(plan)),
                                  context);
                }
            }
            return an::consumer::AnyImage(std::move(restored));
        },
        input);
    return runtimeImageOf(*session, scope, std::move(output), source.sdkMetadata(), context);
}

[[nodiscard]] an::SampleType outputTypeOf(const std::string& name)
{
    const std::optional<an::SampleType> type = sampleTypeFromSdkName(name);
    if (!type.has_value())
    {
        throw std::runtime_error("Unsupported pixel type: " + name);
    }
    return *type;
}

/// The PSF composed from fitted stars, as the SDK always composed it.
[[nodiscard]] BuiltPsf starDerivedPsf(const std::vector<an::DetectedStar>& stars, const SdkPsfProfile profile,
                                      const int psfSize, const std::size_t minimumStarCount, const bool ignoreSaturated,
                                      const bool circularize, const std::string_view context)
{
    std::vector<const an::DetectedStar*> usable;
    for (const an::DetectedStar& star : stars)
    {
        if (ignoreSaturated && star.psf.saturated)
        {
            continue;
        }
        if (std::isfinite(star.psf.fullWidthX) && std::isfinite(star.psf.fullWidthY) && star.psf.fullWidthX > 0.0 &&
            star.psf.fullWidthY > 0.0)
        {
            usable.push_back(&star);
        }
    }
    if (usable.size() < minimumStarCount)
    {
        throw std::runtime_error("Star-derived deconvolution PSF does not have enough usable fitted stars.");
    }
    std::vector<double> widthsX;
    std::vector<double> widthsY;
    std::vector<double> widths;
    std::vector<double> betas;
    std::vector<double> angles;
    std::size_t moffatFits = 0u;
    for (const an::DetectedStar* star : usable)
    {
        widthsX.push_back(star->psf.fullWidthX);
        widthsY.push_back(star->psf.fullWidthY);
        widths.push_back(std::isfinite(star->metrics.meanFullWidth) && star->metrics.meanFullWidth > 0.0
                             ? star->metrics.meanFullWidth
                             : (star->psf.fullWidthX + star->psf.fullWidthY) * 0.5);
        if (star->psf.beta.has_value() && std::isfinite(*star->psf.beta) && *star->psf.beta > 1.0)
        {
            betas.push_back(*star->psf.beta);
        }
        angles.push_back(star->psf.angleDegrees);
        if (star->psf.model != an::PsfModel::Gaussian)
        {
            ++moffatFits;
        }
    }
    std::optional<double> widthX = positiveMedian(widthsX);
    std::optional<double> widthY = positiveMedian(widthsY);
    if (circularize)
    {
        widthX = positiveMedian(widths);
        widthY = widthX;
    }
    if (!widthX.has_value() || !widthY.has_value())
    {
        throw std::runtime_error("Cannot estimate deconvolution PSF FWHM from star profiles.");
    }
    const bool moffat = profile == SdkPsfProfile::Moffat ||
                        (profile == SdkPsfProfile::Automatic && moffatFits >= usable.size() - moffatFits);
    an::PsfRequest request;
    request.profile = moffat ? an::PsfProfile::Moffat : an::PsfProfile::Gaussian;
    request.fwhmX = *widthX;
    request.fwhmY = *widthY;
    request.angleDegrees = axialMeanDegrees(angles);
    request.beta = positiveMedian(betas).value_or(kDefaultMoffatBeta);
    request.size = psfSize != 0 ? static_cast<std::uint32_t>(psfSize) : automaticPsfSide((std::max)(*widthX, *widthY));
    return buildPsf(request, context);
}

} // namespace

/// The detector's request a finder dictionary states, over the engine's defaults - which are the finder's own.
an::StarDetectionRequest parseStarFinderParams(const py::object& value)
{
    an::StarDetectionRequest request;
    if (value.is_none())
    {
        return request;
    }
    const py::dict values = value.cast<py::dict>();
    int radius = static_cast<int>(request.searchRadius);
    int convergence = static_cast<int>(request.convergence);
    double focalLength = 0.0;
    double pixelSize = 0.0;
    assignIfPresent(values, "radius", radius);
    assignIfPresent(values, "sigma", request.noiseSigma);
    assignIfPresent(values, "roundness", request.minimumRoundness);
    assignIfPresent(values, "focal_length", focalLength);
    assignIfPresent(values, "pixel_size_x", pixelSize);
    assignIfPresent(values, "convergence", convergence);
    assignIfPresent(values, "relax_checks", request.relaxedChecks);
    assignIfPresent(values, "min_beta", request.minimumBeta);
    assignIfPresent(values, "min_amplitude", request.minimumAmplitude);
    assignIfPresent(values, "max_amplitude", request.maximumAmplitude);
    assignIfPresent(values, "max_radius", request.maximumRoundness);
    const py::object profile = dictValueOrNone(values, "profile");
    if (!profile.is_none())
    {
        request.model = starModelFromSdkName(profile.cast<std::string>());
    }
    if (radius <= 0)
    {
        throw std::runtime_error("Star finder radius must be greater than zero");
    }
    if (request.noiseSigma <= 0.0)
    {
        throw std::runtime_error("Star finder sigma must be greater than zero");
    }
    if (convergence <= 0)
    {
        throw std::runtime_error("Star finder convergence must be greater than zero");
    }
    request.searchRadius = static_cast<std::uint32_t>(radius);
    request.convergence = static_cast<std::uint32_t>(convergence);
    if (focalLength > 0.0 && pixelSize > 0.0)
    {
        // A pixel of so many micrometres behind a focal length of so many millimetres subtends this many seconds.
        request.arcsecondsPerPixel = kArcsecondsPerRadian * pixelSize * 1.0e-3 / focalLength;
    }
    return request;
}

double detectFwhmCall(const std::shared_ptr<RuntimeImage>& image, const int channel)
{
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    const std::optional<std::uint32_t> selected = detectionChannelOf(*source, channel, "detect_fwhm()");
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::AssetReadLease lease = source->asset()->readForEngine("detect_fwhm()");
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "detect_fwhm()");
    an::StarDetectionRequest request;
    request.maximumStars = an::kDefaultFullWidthStarLimit;
    an::Result<an::StarDetectionResult> found = detectImageStars(scope, lease.image(), request, selected);
    if (!found.hasValue() && found.status().code() == an::ErrorCode::EmptySelection)
    {
        // A field with nothing to measure has no width, which the SDK answers as zero.
        return 0.0;
    }
    const an::StarDetectionResult result = require(std::move(found), "detect_fwhm()");
    return result.summary.medianFullWidth.value_or(0.0);
}

py::list findStarsCall(const std::shared_ptr<RuntimeImage>& image, const int channel, const int maxStars,
                       const py::object& params)
{
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    if (maxStars <= 0)
    {
        throw std::runtime_error("find_stars() max_stars must be greater than zero");
    }
    const std::optional<std::uint32_t> selected = detectionChannelOf(*source, channel, "find_stars()");
    an::StarDetectionRequest request = parseStarFinderParams(params);
    request.maximumStars = static_cast<std::uint32_t>(maxStars);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    an::consumer::AssetReadLease lease = source->asset()->readForEngine("find_stars()");
    an::consumer::OperationScope scope =
        admitOperation(*session, an::consumer::OperationPurpose::Apply, "find_stars()");
    const an::StarDetectionResult result =
        require(detectImageStars(scope, lease.image(), request, selected), "find_stars()");
    py::list stars;
    for (const an::DetectedStar& star : result.stars)
    {
        stars.append(starToPythonDict(star));
    }
    return stars;
}

std::shared_ptr<RuntimeImage> richardsonLucyDeconvolutionCall(
    const std::shared_ptr<RuntimeImage>& image, const double fwhmX, const double fwhmY, const double angleDegrees,
    const int iterations, const double tvRegularization, const double damping, const double stoppingDelta,
    const int psfSize, const std::string& outputPixelType, const bool preserveFlux, const bool clampToUnitRange,
    const std::string& psfProfile, const double beta, const double relativeStoppingDelta, const double edgeTaperRadius,
    const std::string& convolutionBackend, const std::string& colorMode, const std::string& edgeTaperMode,
    const int edgeTaperIterations, const std::string& regularizationMode, const std::string& updateMethod,
    const double gradientDescentStep)
{
    constexpr std::string_view kContext = "richardson_lucy_deconvolution()";
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    if (psfSize < 0)
    {
        throw std::runtime_error("Richardson-Lucy PSF size must be zero or a positive odd value");
    }
    const an::SampleType outputType = outputTypeOf(outputPixelType);
    const an::RichardsonLucyRequest request = richardsonLucyRequest(
        outputType, iterations, tvRegularization, damping, stoppingDelta, preserveFlux, clampToUnitRange,
        relativeStoppingDelta, edgeTaperRadius, convolutionBackend, colorMode, edgeTaperMode, edgeTaperIterations,
        regularizationMode, updateMethod, gradientDescentStep);
    const BuiltPsf psf = statedPsf(fwhmX, fwhmY, angleDegrees, psfSize, psfProfile, beta,
                                   "Automatic deconvolution PSF profile requires fitted stars", kContext);
    const an::RichardsonLucyPlan plan = require(an::planRichardsonLucy(request, psf.plane()), kContext);
    return deconvolve(*source, outputType, psf, plan, kContext);
}

std::shared_ptr<RuntimeImage>
wienerDeconvolutionCall(const std::shared_ptr<RuntimeImage>& image, const double fwhmX, const double fwhmY,
                        const double angleDegrees, const double regularization, const int psfSize,
                        const std::string& outputPixelType, const bool preserveFlux, const bool clampToUnitRange,
                        const std::string& psfProfile, const double beta, const double edgeTaperRadius,
                        const std::string& colorMode, const std::string& edgeTaperMode, const int edgeTaperIterations)
{
    constexpr std::string_view kContext = "wiener_deconvolution()";
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    if (psfSize < 0)
    {
        throw std::runtime_error("Wiener deconvolution PSF size must be zero or a positive odd value");
    }
    if (!std::isfinite(regularization) || regularization <= 0.0)
    {
        throw std::runtime_error("Wiener deconvolution regularization must be greater than zero");
    }
    const an::SampleType outputType = outputTypeOf(outputPixelType);
    an::WienerRequest request;
    request.common = deconvolutionCommon(outputType, preserveFlux, clampToUnitRange, edgeTaperRadius, "auto", colorMode,
                                         edgeTaperMode, edgeTaperIterations,
                                         "Wiener deconvolution edge_taper_iterations must be greater than zero");
    request.regularization = regularization;
    const BuiltPsf psf = statedPsf(fwhmX, fwhmY, angleDegrees, psfSize, psfProfile, beta,
                                   "Automatic Wiener deconvolution PSF profile requires fitted stars", kContext);
    const an::WienerPlan plan = require(an::planWiener(request, psf.plane()), kContext);
    return deconvolve(*source, outputType, psf, plan, kContext);
}

std::shared_ptr<RuntimeImage> richardsonLucyStarDeconvolutionCall(
    const std::shared_ptr<RuntimeImage>& image, const int channel, const int maxStars, const py::object& starParams,
    const int iterations, const double tvRegularization, const double damping, const double stoppingDelta,
    const int psfSize, const std::string& outputPixelType, const bool preserveFlux, const bool clampToUnitRange,
    const std::string& psfProfile, const int minimumStarCount, const bool ignoreSaturated, const bool circularize,
    const double relativeStoppingDelta, const double edgeTaperRadius, const std::string& convolutionBackend,
    const std::string& colorMode, const std::string& edgeTaperMode, const int edgeTaperIterations,
    const std::string& regularizationMode, const std::string& updateMethod, const double gradientDescentStep)
{
    constexpr std::string_view kContext = "richardson_lucy_star_deconvolution()";
    const std::shared_ptr<RuntimeImage>& source = requireRuntimeImage(image, "image");
    if (maxStars <= 0)
    {
        throw std::runtime_error("richardson_lucy_star_deconvolution() max_stars must be greater than zero");
    }
    if (psfSize < 0)
    {
        throw std::runtime_error("Richardson-Lucy PSF size must be zero or a positive odd value");
    }
    if (minimumStarCount <= 0)
    {
        throw std::runtime_error("Star-derived Richardson-Lucy minimum_star_count must be greater than zero");
    }
    const std::optional<std::uint32_t> selected =
        detectionChannelOf(*source, channel, "richardson_lucy_star_deconvolution()");
    an::StarDetectionRequest finder = parseStarFinderParams(starParams);
    if (starParams.is_none())
    {
        finder.model = an::PsfModel::MoffatFreeBeta;
    }
    finder.maximumStars = static_cast<std::uint32_t>(maxStars);
    const SdkPsfProfile profile = psfProfileFromSdkName(psfProfile);
    const an::SampleType outputType = outputTypeOf(outputPixelType);
    const an::RichardsonLucyRequest request = richardsonLucyRequest(
        outputType, iterations, tvRegularization, damping, stoppingDelta, preserveFlux, clampToUnitRange,
        relativeStoppingDelta, edgeTaperRadius, convolutionBackend, colorMode, edgeTaperMode, edgeTaperIterations,
        regularizationMode, updateMethod, gradientDescentStep);

    std::vector<an::DetectedStar> stars;
    {
        const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
        an::consumer::AssetReadLease lease = source->asset()->readForEngine(kContext);
        an::consumer::OperationScope scope = admitOperation(*session, an::consumer::OperationPurpose::Apply, kContext);
        an::Result<an::StarDetectionResult> found = detectImageStars(scope, lease.image(), finder, selected);
        if (!found.hasValue() && found.status().code() != an::ErrorCode::EmptySelection)
        {
            raiseFailure("Star-derived Richardson-Lucy could not fit stars in the image", found.status());
        }
        if (found.hasValue())
        {
            stars = std::move(found->stars);
        }
    }
    const BuiltPsf psf = starDerivedPsf(stars, profile, psfSize, static_cast<std::size_t>(minimumStarCount),
                                        ignoreSaturated, circularize, kContext);
    const an::RichardsonLucyPlan plan = require(an::planRichardsonLucy(request, psf.plane()), kContext);
    return deconvolve(*source, outputType, psf, plan, kContext);
}

} // namespace an::sdk
