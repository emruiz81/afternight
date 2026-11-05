// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - one image blended over another, with its transfers, pre-filters, kept lightness and opacity.
//
// A composition over Float32 working planes - Float64 when the result is Float64 - in this order: both images
// transferred, the blend image filtered, a grey image given three channels where the other is colour, the target's
// lightness measured, the mode, the lightness restored, the opacity. The transfers are the published stretch, the
// blur and the high pass the exact Gaussian in passes whose variances add, the colourizing filters the published
// colourize, the lightness the colour transform run in place, and twenty of the modes `blendImages`. The seven
// arithmetic modes, the high pass's difference, the opacity and the pass-through of an invalid pixel are walks of
// this file, each sample computed in double. The shared writer narrows the result once.
//
// A blend that needs none of that - both images already in the result's floating-point representation with its
// channel count, nothing transferred, filtered, kept or laid over, and every sample selected - runs straight from
// the two images into the destination, because its working copies would hold exactly what the images hold and the
// writer would copy them out unchanged.

#include <AfterNightEngine/Algorithms/Pointwise/BlendComposition.h>

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/Algorithms/Pointwise/BlendMode.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/CompositionPlanes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace an
{
namespace
{

using composition_detail::CompositionSource;
using composition_detail::WorkingBlock;
using composition_detail::WorkingPlanes;

constexpr const char* kPlanOperation = "an::planBlendComposition";
constexpr const char* kLayoutOperation = "an::blendCompositionLayout";
constexpr const char* kComposeOperation = "an::composeBlend";

/// What the high pass adds to the image less its blur: the product's mid-grey.
constexpr double kHighPassOffset = 0.5;

/// The channels a colour image has.
constexpr std::uint32_t kColorChannels = 3u;

[[nodiscard]] Status blendFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] constexpr bool normalizedDomain(NumericDomain domain) noexcept
{
    return domain == NumericDomain::NormalizedLinear || domain == NumericDomain::EncodedNonlinear;
}

[[nodiscard]] constexpr bool blendChannels(std::uint32_t channels) noexcept
{
    return channels == 1u || channels == kColorChannels;
}

[[nodiscard]] bool unitParameter(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

[[nodiscard]] bool validTransfer(const MidtonesTransferParameters& transfer) noexcept
{
    return unitParameter(transfer.midtones) && unitParameter(transfer.shadows) && unitParameter(transfer.highlights) &&
           transfer.highlights >= transfer.shadows;
}

[[nodiscard]] bool identityTransfer(const MidtonesTransferParameters& transfer) noexcept
{
    return transfer.shadows == 0.0 && transfer.highlights == 1.0 && transfer.midtones == 0.5;
}

[[nodiscard]] constexpr bool modeMakesColor(BlendCompositionMode mode) noexcept
{
    return mode == BlendCompositionMode::Hue || mode == BlendCompositionMode::Saturation ||
           mode == BlendCompositionMode::Color || mode == BlendCompositionMode::Lightness;
}

[[nodiscard]] constexpr bool filterMakesColor(BlendPreFilter filter) noexcept
{
    return filter == BlendPreFilter::ColorizeLightness || filter == BlendPreFilter::ColorizeChannels;
}

[[nodiscard]] constexpr bool filterBlurs(BlendPreFilter filter) noexcept
{
    return filter == BlendPreFilter::GaussianBlur || filter == BlendPreFilter::HighPass;
}

/// The named formula a mode runs, when `blendImages` holds it.
[[nodiscard]] constexpr std::optional<BlendMode> namedMode(BlendCompositionMode mode) noexcept
{
    switch (mode)
    {
    case BlendCompositionMode::ColorBurn:
        return BlendMode::ColorBurn;
    case BlendCompositionMode::LinearBurn:
        return BlendMode::LinearBurn;
    case BlendCompositionMode::DarkerColor:
        return BlendMode::DarkerColor;
    case BlendCompositionMode::Screen:
        return BlendMode::Screen;
    case BlendCompositionMode::ColorDodge:
        return BlendMode::ColorDodge;
    case BlendCompositionMode::LighterColor:
        return BlendMode::LighterColor;
    case BlendCompositionMode::Overlay:
        return BlendMode::Overlay;
    case BlendCompositionMode::SoftLight:
        return BlendMode::SoftLight;
    case BlendCompositionMode::HardLight:
        return BlendMode::HardLight;
    case BlendCompositionMode::VividLight:
        return BlendMode::VividLight;
    case BlendCompositionMode::LinearLight:
        return BlendMode::LinearLight;
    case BlendCompositionMode::PinLight:
        return BlendMode::PinLight;
    case BlendCompositionMode::Difference:
        return BlendMode::Difference;
    case BlendCompositionMode::Exclusion:
        return BlendMode::Exclusion;
    case BlendCompositionMode::Power:
        return BlendMode::Power;
    case BlendCompositionMode::ArcTangent:
        return BlendMode::ArcTangent;
    case BlendCompositionMode::Hue:
        return BlendMode::Hue;
    case BlendCompositionMode::Saturation:
        return BlendMode::Saturation;
    case BlendCompositionMode::Color:
        return BlendMode::Color;
    case BlendCompositionMode::Lightness:
        return BlendMode::Lightness;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::uint32_t resultChannels(const BlendCompositionRequest& request, std::uint32_t target,
                                           std::uint32_t blend) noexcept
{
    if (modeMakesColor(request.mode) || filterMakesColor(request.filter))
    {
        return kColorChannels;
    }
    return (std::max)(target, blend);
}

[[nodiscard]] CompositionSource compositionOf(const BlendCompositionSource& source) noexcept
{
    return CompositionSource{.descriptor = source.descriptor,
                             .samples = source.samples,
                             .layout = source.layout,
                             .validity = source.validity};
}

[[nodiscard]] Status checkRequest(const BlendCompositionRequest& request, const char* operation)
{
    if (!isBlendCompositionMode(request.mode) || !isBlendPreFilter(request.filter))
    {
        return blendFailure(ErrorCode::InvalidArgument, "a blend enumerator is outside its enumeration", operation,
                            "request");
    }
    if (!validTransfer(request.targetTransfer) || !validTransfer(request.blendTransfer))
    {
        return blendFailure(ErrorCode::InvalidArgument,
                            "a transfer's parameters must be finite, within the unit interval and ordered", operation,
                            "request");
    }
    if (!std::isfinite(request.filterSigma) || request.filterSigma <= kMinimumGaussianSigma ||
        !std::isfinite(request.filterHueDegrees) || !unitParameter(request.filterSaturation))
    {
        return blendFailure(ErrorCode::InvalidArgument,
                            "the filter needs a finite standard deviation above the smallest, a finite hue and a "
                            "saturation within the unit interval",
                            operation, "request");
    }
    if (!unitParameter(request.opacity))
    {
        return blendFailure(ErrorCode::InvalidArgument, "the opacity must be within the unit interval", operation,
                            "request");
    }
    if (request.colorEncoding.schema != ChannelSchema::Rgb ||
        checkColorEncoding(request.colorEncoding) != ColorEncodingError::None)
    {
        return blendFailure(ErrorCode::InvalidArgument, "the colour encoding is invalid or not RGB", operation,
                            "request");
    }
    if (!isSampleType(request.outputSampleType))
    {
        return blendFailure(ErrorCode::InvalidArgument, "the result's representation is outside its enumeration",
                            operation, "request");
    }
    return Status::success();
}

/// The widest standard deviation one exact pass is expected to hold: a quarter of the largest stencil radius at
/// the default truncation. The search below starts from it and lets the planner decide.
constexpr double kLargestPassSigma = 32.0;

/// The fewest exact passes whose standard deviations add up, in variance, to the filter's.
[[nodiscard]] Result<std::uint32_t> passesFor(double sigma)
{
    std::uint32_t passes =
        (std::max)(1u,
                   static_cast<std::uint32_t>(std::ceil((sigma * sigma) / (kLargestPassSigma * kLargestPassSigma))));
    for (;;)
    {
        const double passSigma = sigma / std::sqrt(static_cast<double>(passes));
        const Result<GaussianPlan> gaussian =
            planGaussianBlur(GaussianRequest{.sigmaX = passSigma, .sigmaY = passSigma});
        if (gaussian.hasValue())
        {
            return passes;
        }
        if (gaussian.status().code() != ErrorCode::OutOfRange)
        {
            return gaussian.status();
        }
        ++passes;
    }
}

[[nodiscard]] Status checkImages(const ImageDescriptor& target, const ImageDescriptor& blend, const char* operation)
{
    if (!blendChannels(target.extent().channelCount()))
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "a blend reads one channel or three", operation,
                            "target");
    }
    if (!blendChannels(blend.extent().channelCount()))
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "a blend reads one channel or three", operation, "blend");
    }
    if (blend.extent().width() != target.extent().width() || blend.extent().height() != target.extent().height())
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "the blend image is not the target's width and height",
                            operation, "blend");
    }
    return Status::success();
}

/// Runs a body over every row of the working images on the operation's threads.
template <typename Body>
[[nodiscard]] Status forEachRow(const ExecutionContext& execution, const WorkingPlanes& shape, std::uint32_t channels,
                                std::uint32_t operations, Body&& body)
{
    const std::uint32_t width = shape.layout->extent().width();
    const TaskCost cost{.bytesPerItem =
                            static_cast<std::uint64_t>(width) * channels * 3u * sampleTypeSizeBytes(shape.sampleType),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * operations};
    return parallelFor(execution, shape.layout->extent().height(), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            body(static_cast<std::uint32_t>(y));
        }
        return Status::success();
    });
}

/// The first `planes` planes of a working image, as a layout.
[[nodiscard]] Result<ImageLayout> planesOf(const WorkingPlanes& working, std::uint32_t planes)
{
    const std::optional<ImageLayout> layout = composition_detail::workingPlaneLayout(working, planes);
    if (!layout.has_value())
    {
        return blendFailure(ErrorCode::Internal, "a working image's planes cannot be described", kComposeOperation,
                            "working");
    }
    return *layout;
}

/// Stretches the first `planes` planes of a working image by one transfer, in place.
[[nodiscard]] Status transferPlanes(const ExecutionContext& execution, const WorkingPlanes& working,
                                    std::uint32_t planes, const MidtonesTransferParameters& transfer)
{
    const Result<ImageLayout> layout = planesOf(working, planes);
    if (!layout.hasValue())
    {
        return layout.status();
    }
    StretchImageRequest stretch;
    stretch.stretch.function = StretchFunction::ScreenTransfer;
    stretch.stretch.screenTransfer = {transfer, transfer, transfer};
    stretch.colorMode = StretchColorMode::Independent;
    stretch.channelEnabled = {true, true, true, false};
    const Result<StretchImageResult> stretched = stretchImage(execution, working.sampleType, working.samples, *layout,
                                                              working.samples, *layout, stretch, KernelValidityView{});
    return stretched.hasValue() ? Status::success() : stretched.status();
}

/// Blurs the first `planes` planes of `source` into `destination` by the exact Gaussian in `passes` passes, using
/// `scratch` between them; the three are distinct and `source` is left as it is.
[[nodiscard]] Status gaussianInto(const ExecutionContext& execution, const WorkingPlanes& source,
                                  const WorkingPlanes& destination, const WorkingPlanes* scratch, std::uint32_t planes,
                                  double sigma, std::uint32_t passes)
{
    const double passSigma = sigma / std::sqrt(static_cast<double>(passes));
    const Result<GaussianPlan> plan = planGaussianBlur(GaussianRequest{.sigmaX = passSigma, .sigmaY = passSigma});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const Result<ImageLayout> layout = planesOf(source, planes);
    if (!layout.hasValue())
    {
        return layout.status();
    }
    const WorkingPlanes* from = &source;
    // The passes alternate between the destination and the scratch, so the last lands in the destination.
    const WorkingPlanes* to = passes % 2u == 1u ? &destination : scratch;
    for (std::uint32_t pass = 0u; pass < passes; ++pass)
    {
        const Result<SpatialFilterResult> blurred =
            blurImageGaussian(execution, source.sampleType, from->samples, *layout, to->samples, *layout, *plan);
        if (!blurred.hasValue())
        {
            return blurred.status();
        }
        from = to;
        to = to == &destination ? scratch : &destination;
    }
    return Status::success();
}

/// Converts the first three planes of a working image between RGB and packed Lab, in place.
[[nodiscard]] Status convertInPlace(const ExecutionContext& execution, const ColorConversionPlan& plan,
                                    const WorkingPlanes& working)
{
    const Result<ImageLayout> layout = planesOf(working, kColorChannels);
    if (!layout.hasValue())
    {
        return layout.status();
    }
    return convertImageColor(execution, plan, working.sampleType, working.samples, *layout, working.samples, *layout);
}

/// Colourizes one plane of `source` into the three planes of `destination`.
[[nodiscard]] Status colorizePlane(const ExecutionContext& execution, const WorkingPlanes& source,
                                   const WorkingPlanes& destination, const BlendCompositionRequest& request)
{
    const Result<ImageLayout> plane = planesOf(source, 1u);
    const Result<ImageLayout> color = planesOf(destination, kColorChannels);
    if (!plane.hasValue() || !color.hasValue())
    {
        return plane.hasValue() ? color.status() : plane.status();
    }
    const Result<ColorAdjustmentResult> colorized =
        colorizeImage(execution, source.sampleType, source.samples, *plane, destination.samples, *color,
                      ColorizationParameters{.hueDegrees = request.filterHueDegrees,
                                             .saturation = request.filterSaturation,
                                             .model = ColorizationModel::Hsv});
    return colorized.hasValue() ? Status::success() : colorized.status();
}

/// Holds the first three planes of a working image to the unit interval.
template <typename WorkingT>
[[nodiscard]] Status holdColor(const ExecutionContext& execution, const WorkingPlanes& working)
{
    const std::uint32_t width = working.layout->extent().width();
    return forEachRow(execution, working, kColorChannels, 2u, [&](std::uint32_t y) {
        for (std::uint32_t channel = 0u; channel < kColorChannels; ++channel)
        {
            WorkingT* const row = working.row<WorkingT>(channel, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                row[x] = static_cast<WorkingT>(std::clamp(static_cast<double>(row[x]), 0.0, 1.0));
            }
        }
    });
}

/// Replaces the lightness of a colour working image with plane `lightnessPlane` of `lightness`: the colour taken to
/// packed Lab, its first plane overwritten, taken back and held to the unit interval.
template <typename WorkingT>
[[nodiscard]] Status replaceLightness(const ExecutionContext& execution, const ColorConversionPlan& toLab,
                                      const ColorConversionPlan& fromLab, const WorkingPlanes& color,
                                      const WorkingPlanes& lightness, std::uint32_t lightnessPlane)
{
    Status step = convertInPlace(execution, toLab, color);
    if (step.failed())
    {
        return step;
    }
    composition_detail::copyWorkingPlane(lightness, lightnessPlane, color, 0u);
    step = convertInPlace(execution, fromLab, color);
    if (step.failed())
    {
        return step;
    }
    return holdColor<WorkingT>(execution, color);
}

/// One of the seven arithmetic modes, into the result.
template <typename WorkingT>
[[nodiscard]] Status arithmeticMode(const ExecutionContext& execution, BlendCompositionMode mode,
                                    const WorkingPlanes& target, const WorkingPlanes& blend,
                                    const WorkingPlanes& result, std::uint32_t channels)
{
    const std::uint32_t width = target.layout->extent().width();
    return forEachRow(execution, target, channels, 2u, [&](std::uint32_t y) {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            const WorkingT* const base = target.row<WorkingT>(channel, y);
            const WorkingT* const over = blend.row<WorkingT>(channel, y);
            WorkingT* const out = result.row<WorkingT>(channel, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double a = static_cast<double>(base[x]);
                const double b = static_cast<double>(over[x]);
                double value = b;
                switch (mode)
                {
                case BlendCompositionMode::Darken:
                    value = b < a ? b : a;
                    break;
                case BlendCompositionMode::Lighten:
                    value = a < b ? b : a;
                    break;
                case BlendCompositionMode::Multiply:
                    value = a * b;
                    break;
                case BlendCompositionMode::LinearDodge:
                    value = (std::min)(a + b, 1.0);
                    break;
                case BlendCompositionMode::Subtract:
                    value = (std::max)(a - b, 0.0);
                    break;
                case BlendCompositionMode::Divide:
                    value = b == 0.0 ? 0.0 : std::clamp(a / b, 0.0, 1.0);
                    break;
                default:
                    break;
                }
                out[x] = static_cast<WorkingT>(value);
            }
        }
    });
}

/// Lays the composite over the original target at the opacity, and gives an invalid pixel the original target.
template <typename WorkingT>
[[nodiscard]] Status layOver(const ExecutionContext& execution, const WorkingPlanes& original,
                             const WorkingPlanes& result, std::uint32_t channels, double opacity,
                             const std::uint8_t* valid)
{
    const std::uint32_t width = result.layout->extent().width();
    return forEachRow(execution, result, channels, 3u, [&](std::uint32_t y) {
        const std::uint8_t* const rowValid = valid != nullptr ? valid + static_cast<std::uint64_t>(y) * width : nullptr;
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            const WorkingT* const base = original.row<WorkingT>(channel, y);
            WorkingT* const out = result.row<WorkingT>(channel, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (rowValid != nullptr && rowValid[x] == 0u)
                {
                    out[x] = base[x];
                }
                else if (opacity != 1.0)
                {
                    out[x] = static_cast<WorkingT>(static_cast<double>(base[x]) * (1.0 - opacity) +
                                                   static_cast<double>(out[x]) * opacity);
                }
            }
        }
    });
}

/// Whether a blend can run straight from its two images into the destination.
[[nodiscard]] bool runsDirect(const BlendCompositionRequest& request, const CompositionSource& target,
                              const CompositionSource& blend, std::uint32_t channels) noexcept
{
    const SampleType type = request.outputSampleType;
    return (type == SampleType::Float32 || type == SampleType::Float64) && target.descriptor->sampleType() == type &&
           blend.descriptor->sampleType() == type && target.descriptor->extent().channelCount() == channels &&
           blend.descriptor->extent().channelCount() == channels && identityTransfer(request.targetTransfer) &&
           identityTransfer(request.blendTransfer) && request.filter == BlendPreFilter::None &&
           !request.preserveLightness && request.opacity == 1.0 &&
           !composition_detail::compositionSourceRuled(target) && !composition_detail::compositionSourceRuled(blend);
}

/// Replace's or one arithmetic mode's value for one pair of samples, its mode fixed when the walk is compiled. The
/// clamps are the arithmetic walk's `std::min`, `std::max` and `std::clamp`, chosen as they choose but written on
/// values, which is what lets a loop of them be vectorized.
template <BlendCompositionMode Mode> [[nodiscard]] double arithmeticValue(double a, double b) noexcept
{
    if constexpr (Mode == BlendCompositionMode::Darken)
    {
        return b < a ? b : a;
    }
    else if constexpr (Mode == BlendCompositionMode::Lighten)
    {
        return a < b ? b : a;
    }
    else if constexpr (Mode == BlendCompositionMode::Multiply)
    {
        return a * b;
    }
    else if constexpr (Mode == BlendCompositionMode::LinearDodge)
    {
        const double sum = a + b;
        return 1.0 < sum ? 1.0 : sum;
    }
    else if constexpr (Mode == BlendCompositionMode::Subtract)
    {
        const double difference = a - b;
        return difference < 0.0 ? 0.0 : difference;
    }
    else if constexpr (Mode == BlendCompositionMode::Divide)
    {
        const double ratio = a / b;
        const double raised = ratio < 0.0 ? 0.0 : ratio;
        const double held = 1.0 < raised ? 1.0 : raised;
        return b == 0.0 ? 0.0 : held;
    }
    else
    {
        return b;
    }
}

/// Whether every sample of one contiguous run is finite, in a loop the compiler can vectorize.
template <typename SampleT> [[nodiscard]] bool runIsFinite(const SampleT* samples, std::uint32_t width) noexcept
{
    std::uint32_t nonFinite = 0u;
    if constexpr (sizeof(SampleT) == sizeof(std::uint32_t))
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            nonFinite |= (std::bit_cast<std::uint32_t>(samples[x]) & 0x7F800000u) == 0x7F800000u;
        }
    }
    else
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            nonFinite |= (std::bit_cast<std::uint64_t>(samples[x]) & 0x7FF0000000000000ull) == 0x7FF0000000000000ull;
        }
    }
    return nonFinite == 0u;
}

/// One channel's run of Replace or an arithmetic mode, the mode fixed when the walk is compiled.
///
/// @return false when a sample of either image is not finite.
template <BlendCompositionMode Mode, typename SampleT>
[[nodiscard]] bool arithmeticRun(const SampleT* base, std::size_t baseStep, const SampleT* over, std::size_t overStep,
                                 SampleT* out, std::size_t outStep, std::uint32_t width) noexcept
{
    if (baseStep == 1u && overStep == 1u && outStep == 1u)
    {
        if (!runIsFinite(base, width) || !runIsFinite(over, width))
        {
            return false;
        }
        if constexpr (Mode == BlendCompositionMode::Replace)
        {
            std::memcpy(out, over, static_cast<std::size_t>(width) * sizeof(SampleT));
        }
        else
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                out[x] = static_cast<SampleT>(
                    arithmeticValue<Mode>(static_cast<double>(base[x]), static_cast<double>(over[x])));
            }
        }
        return true;
    }
    std::uint32_t nonFinite = 0u;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const SampleT baseValue = base[x * baseStep];
        const SampleT overValue = over[x * overStep];
        nonFinite |= composition_detail::nonFiniteBit(baseValue) | composition_detail::nonFiniteBit(overValue);
        out[x * outStep] =
            static_cast<SampleT>(arithmeticValue<Mode>(static_cast<double>(baseValue), static_cast<double>(overValue)));
    }
    return nonFinite == 0u;
}

template <typename SampleT>
using ArithmeticRun = bool (*)(const SampleT*, std::size_t, const SampleT*, std::size_t, SampleT*, std::size_t,
                               std::uint32_t) noexcept;

/// The run of Replace or one arithmetic mode; null for a mode `blendImages` computes.
template <typename SampleT> [[nodiscard]] ArithmeticRun<SampleT> arithmeticRunFor(BlendCompositionMode mode) noexcept
{
    switch (mode)
    {
    case BlendCompositionMode::Replace:
        return &arithmeticRun<BlendCompositionMode::Replace, SampleT>;
    case BlendCompositionMode::Darken:
        return &arithmeticRun<BlendCompositionMode::Darken, SampleT>;
    case BlendCompositionMode::Multiply:
        return &arithmeticRun<BlendCompositionMode::Multiply, SampleT>;
    case BlendCompositionMode::LinearDodge:
        return &arithmeticRun<BlendCompositionMode::LinearDodge, SampleT>;
    case BlendCompositionMode::Lighten:
        return &arithmeticRun<BlendCompositionMode::Lighten, SampleT>;
    case BlendCompositionMode::Subtract:
        return &arithmeticRun<BlendCompositionMode::Subtract, SampleT>;
    case BlendCompositionMode::Divide:
        return &arithmeticRun<BlendCompositionMode::Divide, SampleT>;
    default:
        return nullptr;
    }
}

/// The blend run straight from the two images into the destination.
template <typename SampleT>
[[nodiscard]] Status runDirect(const ExecutionContext& execution, const BlendCompositionRequest& request,
                               const CompositionSource& target, const CompositionSource& blend, void* destination,
                               const ImageLayout& destinationLayout, std::uint32_t channels)
{
    const std::optional<BlendMode> named = namedMode(request.mode);
    if (named.has_value())
    {
        const Result<BlendModeResult> blended = blendImages(
            execution, target.descriptor->sampleType(), target.samples, *target.layout, blend.samples, *blend.layout,
            destination, destinationLayout, BlendModeRequest{.mode = *named, .colorEncoding = request.colorEncoding});
        return blended.hasValue() ? Status::success() : blended.status();
    }
    const ArithmeticRun<SampleT> run = arithmeticRunFor<SampleT>(request.mode);
    if (run == nullptr)
    {
        return blendFailure(ErrorCode::Internal, "a mode with no arithmetic run reached the direct blend",
                            kComposeOperation, "plan");
    }
    const std::uint32_t width = destinationLayout.extent().width();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * 3u * sizeof(SampleT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 2u};
    return parallelFor(execution, destinationLayout.extent().height(), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                if (!run(composition_detail::channelRowOf<SampleT>(target.samples, *target.layout, channel, y),
                         target.layout->samplesPerPixel(),
                         composition_detail::channelRowOf<SampleT>(blend.samples, *blend.layout, channel, y),
                         blend.layout->samplesPerPixel(),
                         composition_detail::channelRowOf<SampleT>(destination, destinationLayout, channel, y),
                         destinationLayout.samplesPerPixel(), width))
                {
                    return blendFailure(ErrorCode::InvalidData,
                                        "a source holds a sample that is not finite in a pixel its validity admits",
                                        kComposeOperation, "source");
                }
            }
        }
        return Status::success();
    });
}

/// Every working image one blend uses.
struct BlendWorkspace
{
    WorkingBlock targetBlock;
    WorkingBlock blendBlock;
    WorkingBlock resultBlock;
    WorkingBlock scratchBlock;
    WorkingBlock secondScratchBlock;
    WorkingBlock originalBlock;
    WorkingBlock validBlock;
    WorkingPlanes target;
    WorkingPlanes blend;
    WorkingPlanes result;
    std::optional<WorkingPlanes> scratch;
    std::optional<WorkingPlanes> secondScratch;
    std::optional<WorkingPlanes> original;
    std::uint8_t* valid = nullptr;
};

template <typename WorkingT>
[[nodiscard]] Status runBlend(const ExecutionContext& execution, const BlendCompositionPlan& plan,
                              BlendWorkspace& space, std::uint32_t targetChannels, std::uint32_t blendChannelCount,
                              std::uint32_t channels, const ColorConversionPlan& toLab,
                              const ColorConversionPlan& fromLab)
{
    const BlendCompositionRequest& request = plan.request();
    const bool color = channels == kColorChannels;

    // A grey target meets a colour result as three equal channels, before the original is kept.
    if (targetChannels == 1u && color)
    {
        composition_detail::copyWorkingPlane(space.target, 0u, 1u);
        composition_detail::copyWorkingPlane(space.target, 0u, 2u);
    }
    const WorkingPlanes& original = space.original.has_value() ? *space.original : space.target;
    if (space.original.has_value())
    {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            composition_detail::copyWorkingPlane(space.target, channel, *space.original, channel);
        }
    }

    Status step = Status::success();
    if (!identityTransfer(request.targetTransfer))
    {
        step = transferPlanes(execution, space.target, channels, request.targetTransfer);
    }
    if (step.ok() && !identityTransfer(request.blendTransfer))
    {
        step = transferPlanes(execution, space.blend, blendChannelCount, request.blendTransfer);
    }
    if (step.failed())
    {
        return step;
    }

    // The blend image's filter.
    std::uint32_t filtered = blendChannelCount;
    if (request.filter == BlendPreFilter::GaussianBlur || request.filter == BlendPreFilter::HighPass)
    {
        step = gaussianInto(execution, space.blend, *space.scratch,
                            space.secondScratch.has_value() ? &*space.secondScratch : nullptr, blendChannelCount,
                            request.filterSigma, plan.gaussianPasses());
        if (step.failed())
        {
            return step;
        }
        const std::uint32_t width = space.blend.layout->extent().width();
        const bool highPass = request.filter == BlendPreFilter::HighPass;
        step = forEachRow(execution, space.blend, blendChannelCount, 3u, [&](std::uint32_t y) {
            for (std::uint32_t channel = 0u; channel < blendChannelCount; ++channel)
            {
                WorkingT* const row = space.blend.row<WorkingT>(channel, y);
                const WorkingT* const blurred = space.scratch->row<WorkingT>(channel, y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    row[x] = highPass
                                 ? static_cast<WorkingT>(std::clamp(
                                       static_cast<double>(row[x]) - static_cast<double>(blurred[x]) + kHighPassOffset,
                                       0.0, 1.0))
                                 : blurred[x];
                }
            }
        });
    }
    else if (request.filter == BlendPreFilter::ColorizeLightness)
    {
        if (blendChannelCount == kColorChannels)
        {
            step = convertInPlace(execution, toLab, space.blend);
        }
        if (step.ok())
        {
            composition_detail::copyWorkingPlane(space.blend, 0u, *space.scratch, 0u);
            step = colorizePlane(execution, *space.scratch, space.blend, request);
        }
        filtered = kColorChannels;
    }
    else if (request.filter == BlendPreFilter::ColorizeChannels)
    {
        if (blendChannelCount == kColorChannels)
        {
            const Result<ImageLayout> layout = planesOf(space.blend, kColorChannels);
            if (!layout.hasValue())
            {
                return layout.status();
            }
            const Result<ColorAdjustmentResult> colorized = colorizeImage(
                execution, space.blend.sampleType, space.blend.samples, *layout, space.blend.samples, *layout,
                ColorizationParameters{.hueDegrees = request.filterHueDegrees,
                                       .saturation = request.filterSaturation,
                                       .model = ColorizationModel::Hsv});
            step = colorized.hasValue() ? Status::success() : colorized.status();
        }
        else
        {
            composition_detail::copyWorkingPlane(space.blend, 0u, *space.scratch, 0u);
            step = colorizePlane(execution, *space.scratch, space.blend, request);
        }
        filtered = kColorChannels;
    }
    if (step.failed())
    {
        return step;
    }

    // A grey blend image meets a colour target as three equal channels, except under Lightness, which reads it as
    // the lightness itself.
    const bool greyLightness = request.mode == BlendCompositionMode::Lightness && filtered == 1u;
    if (filtered == 1u && color && !greyLightness)
    {
        composition_detail::copyWorkingPlane(space.blend, 0u, 1u);
        composition_detail::copyWorkingPlane(space.blend, 0u, 2u);
    }

    // The transferred target's lightness, kept in the scratch's first plane.
    const bool keepLightness = request.preserveLightness && color;
    if (keepLightness)
    {
        for (std::uint32_t channel = 0u; channel < kColorChannels; ++channel)
        {
            composition_detail::copyWorkingPlane(space.target, channel, *space.scratch, channel);
        }
        step = convertInPlace(execution, toLab, *space.scratch);
        if (step.failed())
        {
            return step;
        }
    }

    // The mode.
    const std::optional<BlendMode> named = namedMode(request.mode);
    if (request.mode == BlendCompositionMode::Replace)
    {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            composition_detail::copyWorkingPlane(space.blend, channel, space.result, channel);
        }
    }
    else if (greyLightness)
    {
        for (std::uint32_t channel = 0u; channel < kColorChannels; ++channel)
        {
            composition_detail::copyWorkingPlane(space.target, channel, space.result, channel);
        }
        step = replaceLightness<WorkingT>(execution, toLab, fromLab, space.result, space.blend, 0u);
    }
    else if (named.has_value())
    {
        const Result<ImageLayout> layout = planesOf(space.target, channels);
        if (!layout.hasValue())
        {
            return layout.status();
        }
        const Result<BlendModeResult> blended = blendImages(
            execution, space.target.sampleType, space.target.samples, *layout, space.blend.samples, *layout,
            space.result.samples, *layout, BlendModeRequest{.mode = *named, .colorEncoding = request.colorEncoding});
        step = blended.hasValue() ? Status::success() : blended.status();
    }
    else
    {
        step = arithmeticMode<WorkingT>(execution, request.mode, space.target, space.blend, space.result, channels);
    }
    if (step.ok() && keepLightness)
    {
        step = replaceLightness<WorkingT>(execution, toLab, fromLab, space.result, *space.scratch, 0u);
    }
    if (step.ok() && (request.opacity != 1.0 || space.valid != nullptr))
    {
        step = layOver<WorkingT>(execution, original, space.result, channels, request.opacity, space.valid);
    }
    return step;
}

} // namespace

Result<BlendCompositionPlan> planBlendComposition(const BlendCompositionRequest& request)
{
    const Status checked = checkRequest(request, kPlanOperation);
    if (checked.failed())
    {
        return checked;
    }
    std::uint32_t passes = 0u;
    if (filterBlurs(request.filter))
    {
        const Result<std::uint32_t> counted = passesFor(request.filterSigma);
        if (!counted.hasValue())
        {
            return counted.status();
        }
        passes = *counted;
    }
    return BlendCompositionPlan(request, passes);
}

std::uint32_t blendCompositionChannels(const BlendCompositionPlan& plan, const ImageDescriptor& target,
                                       const ImageDescriptor& blend) noexcept
{
    return resultChannels(plan.request(), target.extent().channelCount(), blend.extent().channelCount());
}

Result<ImageLayoutRequest> blendCompositionLayout(const BlendCompositionPlan& plan, const ImageDescriptor& target,
                                                  const ImageDescriptor& blend)
{
    const Status images = checkImages(target, blend, kLayoutOperation);
    if (images.failed())
    {
        return images;
    }
    const std::uint32_t channels = blendCompositionChannels(plan, target, blend);
    const std::optional<ImageExtent> extent =
        ImageExtent::create(target.extent().width(), target.extent().height(), channels, defaultEngineLimits());
    if (!extent.has_value())
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "the result's extent cannot be described",
                            kLayoutOperation, "target");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(plan.request().outputSampleType);
    return ImageLayoutRequest{.extent = *extent,
                              .elementSizeBytes = elementBytes,
                              .samplesPerPixel = 1u,
                              .planeCount = channels,
                              .rowAlignmentBytes = elementBytes};
}

Result<BlendCompositionResult> composeBlend(const ExecutionContext& execution, const BlendCompositionPlan& plan,
                                            const BlendCompositionSource& target, const BlendCompositionSource& blend,
                                            void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return blendFailure(ErrorCode::NotInitialized, "a blend was given an empty execution context",
                            kComposeOperation, "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    if (!backend.hasValue())
    {
        return backend.status();
    }
    const BlendCompositionRequest& request = plan.request();
    const CompositionSource targetSource = compositionOf(target);
    const CompositionSource blendSource = compositionOf(blend);
    for (const auto& [source, parameter] : {std::pair{&targetSource, "target"}, std::pair{&blendSource, "blend"}})
    {
        const Status checked = composition_detail::checkCompositionSource(*source, kComposeOperation, parameter);
        if (checked.failed())
        {
            return checked;
        }
        if (!normalizedDomain(source->descriptor->numericDomain()))
        {
            return blendFailure(ErrorCode::IncompatibleArguments,
                                "a blend reads fractions of full scale, so its images' domain is a normalized one",
                                kComposeOperation, parameter);
        }
    }
    const Status images = checkImages(*target.descriptor, *blend.descriptor, kComposeOperation);
    if (images.failed())
    {
        return images;
    }
    const std::uint32_t targetChannels = target.descriptor->extent().channelCount();
    const std::uint32_t blendChannelCount = blend.descriptor->extent().channelCount();
    const std::uint32_t channels = resultChannels(request, targetChannels, blendChannelCount);
    const std::uint32_t width = target.descriptor->extent().width();
    const std::uint32_t height = target.descriptor->extent().height();
    const std::optional<ImageExtent> extent = ImageExtent::create(width, height, channels, execution.limits());
    if (!extent.has_value())
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "the result's extent cannot be described",
                            kComposeOperation, "target");
    }
    const Status destinationChecked = composition_detail::checkCompositionDestination(
        request.outputSampleType, destination, destinationLayout, *extent, kComposeOperation);
    if (destinationChecked.failed())
    {
        return destinationChecked;
    }
    for (const auto& [source, parameter] : {std::pair{&targetSource, "target"}, std::pair{&blendSource, "blend"}})
    {
        const Status separate = composition_detail::checkCompositionSeparate(
            *source, destination, destinationLayout.totalBytes(), kComposeOperation, parameter);
        if (separate.failed())
        {
            return separate;
        }
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (runsDirect(request, targetSource, blendSource, channels))
    {
        const Status direct = request.outputSampleType == SampleType::Float64
                                  ? runDirect<double>(execution, request, targetSource, blendSource, destination,
                                                      destinationLayout, channels)
                                  : runDirect<float>(execution, request, targetSource, blendSource, destination,
                                                     destinationLayout, channels);
        if (direct.failed())
        {
            return direct;
        }
        BlendCompositionResult result;
        result.run.selection.total = pixels;
        result.run.selection.valid = pixels;
        result.run.outputsWritten = pixels * channels;
        return result;
    }
    ColorEncoding lab;
    lab.schema = ChannelSchema::Lab;
    lab.whitePoint = request.colorEncoding.whitePoint;
    lab.perceptual = PerceptualEncoding::UnitPacked;
    const Result<ColorConversionPlan> toLab = planColorConversion(request.colorEncoding, lab);
    if (!toLab.hasValue())
    {
        return toLab.status();
    }
    const Result<ColorConversionPlan> fromLab = planColorConversion(lab, request.colorEncoding);
    if (!fromLab.hasValue())
    {
        return fromLab.status();
    }

    const SampleType workingType =
        request.outputSampleType == SampleType::Float64 ? SampleType::Float64 : SampleType::Float32;
    BlendWorkspace space;
    const auto take = [&](WorkingBlock& block, std::uint32_t planes) -> Result<WorkingPlanes> {
        return composition_detail::takeWorkingPlanes(execution, block, width, height, planes, workingType,
                                                     kComposeOperation);
    };
    Result<WorkingPlanes> targetPlanes = take(space.targetBlock, channels);
    if (!targetPlanes.hasValue())
    {
        return targetPlanes.status();
    }
    space.target = *targetPlanes;
    Result<WorkingPlanes> blendPlanes = take(space.blendBlock, channels);
    if (!blendPlanes.hasValue())
    {
        return blendPlanes.status();
    }
    space.blend = *blendPlanes;
    Result<WorkingPlanes> resultPlanes = take(space.resultBlock, channels);
    if (!resultPlanes.hasValue())
    {
        return resultPlanes.status();
    }
    space.result = *resultPlanes;
    const bool color = channels == kColorChannels;
    if (request.filter != BlendPreFilter::None || (request.preserveLightness && color))
    {
        Result<WorkingPlanes> scratch = take(space.scratchBlock, channels);
        if (!scratch.hasValue())
        {
            return scratch.status();
        }
        space.scratch = *scratch;
    }
    if (filterBlurs(request.filter) && plan.gaussianPasses() > 1u)
    {
        Result<WorkingPlanes> scratch = take(space.secondScratchBlock, channels);
        if (!scratch.hasValue())
        {
            return scratch.status();
        }
        space.secondScratch = *scratch;
    }
    if (composition_detail::compositionSourceRuled(targetSource) ||
        composition_detail::compositionSourceRuled(blendSource))
    {
        const Status taken = space.validBlock.take(execution, pixels, kComposeOperation);
        if (taken.failed())
        {
            return taken;
        }
        space.valid = space.validBlock.bytes();
        std::memset(space.valid, 1, static_cast<std::size_t>(pixels));
    }
    if ((request.opacity != 1.0 || space.valid != nullptr) && !identityTransfer(request.targetTransfer))
    {
        Result<WorkingPlanes> original = take(space.originalBlock, channels);
        if (!original.hasValue())
        {
            return original.status();
        }
        space.original = *original;
    }

    Status step =
        composition_detail::readWorking(execution, targetSource, space.target, 0u, space.valid, kComposeOperation);
    if (step.ok())
    {
        step = composition_detail::readWorking(execution, blendSource, space.blend, 0u, space.valid, kComposeOperation);
    }
    if (step.ok())
    {
        step = workingType == SampleType::Float64 ? runBlend<double>(execution, plan, space, targetChannels,
                                                                     blendChannelCount, channels, *toLab, *fromLab)
                                                  : runBlend<float>(execution, plan, space, targetChannels,
                                                                    blendChannelCount, channels, *toLab, *fromLab);
    }
    if (step.ok())
    {
        step = composition_detail::writeWorking(execution, space.result, request.outputSampleType, destination,
                                                destinationLayout, kComposeOperation);
    }
    if (step.failed())
    {
        return step;
    }
    BlendCompositionResult result;
    result.run.selection.total = pixels;
    result.run.selection.valid = pixels;
    if (space.valid != nullptr)
    {
        std::uint64_t accepted = 0u;
        for (std::uint64_t index = 0u; index < pixels; ++index)
        {
            accepted += space.valid[index];
        }
        result.run.selection.valid = accepted;
    }
    result.run.outputsWritten = pixels * channels;
    result.gaussianPasses = filterBlurs(request.filter) ? plan.gaussianPasses() : 0u;
    return result;
}

} // namespace an
