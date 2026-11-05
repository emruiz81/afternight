// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - color adjustments and colorization.
//
// Every image operation is a per-pixel function over three output channels. One validation checks the
// execution, representation, geometry, overlap and validity view for all four; each then walks rows
// the same way, reading each channel through its layout, skipping the pixels the validity view
// rejects, refusing a selected sample that is not finite, and counting what it wrote - the three
// RGB operations through one shared walker and colorization, whose source may be mono, through its
// own. The component adjustments run the colour transform's scalar path per pixel, into their
// component space and back, so their colour arithmetic is exactly `convertColorPixel`'s; the
// saturation and colorization operations use their own cylindrical formulas below.
// The LCh adjustment of a Float32 image under no mask runs a row compiled per instruction set
// (`backends/cpu/ColorAdjustmentCpu.cpp`) that evaluates the same steps in Float32 lanes, within a
// stated distance of the scalar path, which the reference backend still takes.

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>

#include "kernels/reference/ColorAdjustmentKernels.h"
#include "kernels/reference/ColorStages.h"

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>

namespace an
{
namespace
{

constexpr const char* kBackgroundOperation = "an::deriveSaturationEnhancementBackground";
constexpr const char* kHslOperation = "an::adjustImageHsl";
constexpr const char* kLchOperation = "an::adjustImageLch";
constexpr const char* kSaturationOperation = "an::enhanceImageSaturation";
constexpr const char* kColorizeOperation = "an::colorizeImage";

[[nodiscard]] Status adjustmentFailure(const char* operation, ErrorCode code, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool isSampleAligned(const void* samples, std::uint32_t elementBytes) noexcept
{
    return reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

[[nodiscard]] bool exactlyAliases(const void* sourceSamples, const ImageLayout& sourceLayout,
                                  const void* destinationSamples, const ImageLayout& destinationLayout) noexcept
{
    return sourceSamples == destinationSamples && sourceLayout == destinationLayout;
}

[[nodiscard]] bool maskAccepts(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

[[nodiscard]] Status validateValidity(const KernelValidityView& validity, const ImageLayout& sourceLayout,
                                      const void* destinationSamples, const ImageLayout& destinationLayout,
                                      const char* operation)
{
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return adjustmentFailure(operation, ErrorCode::InvalidArgument,
                                     "a color-adjustment validity view supplies an unused mask");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return adjustmentFailure(operation, ErrorCode::InvalidArgument,
                                 "an explicit-mask color adjustment supplies no mask");
    }
    if (validity.mask->width() != sourceLayout.extent().width() ||
        validity.mask->height() != sourceLayout.extent().height() || validity.maskPlane >= validity.mask->planeCount())
    {
        return adjustmentFailure(operation, ErrorCode::IncompatibleArguments,
                                 "a color-adjustment validity mask does not cover the source");
    }
    if (regionsOverlap(validity.mask->data(), validity.mask->spannedByteCount(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()))
    {
        return adjustmentFailure(operation, ErrorCode::InvalidArgument,
                                 "a color-adjustment validity mask overlaps the writable destination");
    }
    return Status::success();
}

[[nodiscard]] Status validateImageOperation(const ExecutionContext& execution, SampleType sampleType,
                                            const void* sourceSamples, const ImageLayout& sourceLayout,
                                            void* destinationSamples, const ImageLayout& destinationLayout,
                                            std::uint32_t minimumSourceChannels, std::uint32_t maximumSourceChannels,
                                            const KernelValidityView& validity, const char* operation)
{
    if (!execution.valid())
    {
        return adjustmentFailure(operation, ErrorCode::NotInitialized,
                                 "a color adjustment was given an empty execution context");
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
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return adjustmentFailure(operation, ErrorCode::IncompatibleArguments,
                                 "a color adjustment runs on a Float32 or Float64 working image");
    }
    if (sourceSamples == nullptr || destinationSamples == nullptr)
    {
        return adjustmentFailure(operation, ErrorCode::InvalidArgument,
                                 "a color-adjustment image has a null sample pointer");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (sourceLayout.elementSizeBytes() != elementBytes || destinationLayout.elementSizeBytes() != elementBytes ||
        !isSampleAligned(sourceSamples, elementBytes) || !isSampleAligned(destinationSamples, elementBytes))
    {
        return adjustmentFailure(operation, ErrorCode::InvalidArgument,
                                 "a color-adjustment layout or pointer disagrees with its sample type");
    }
    if (sourceLayout.extent().width() != destinationLayout.extent().width() ||
        sourceLayout.extent().height() != destinationLayout.extent().height())
    {
        return adjustmentFailure(operation, ErrorCode::IncompatibleArguments,
                                 "a color adjustment needs equal source and destination geometry");
    }
    const std::uint32_t sourceChannels = sourceLayout.extent().channelCount();
    if (sourceChannels < minimumSourceChannels || sourceChannels > maximumSourceChannels ||
        destinationLayout.extent().channelCount() != 3u)
    {
        return adjustmentFailure(operation, ErrorCode::IncompatibleArguments,
                                 "a color adjustment was given an unsupported channel count");
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(sourceSamples), sourceLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()) &&
        !exactlyAliases(sourceSamples, sourceLayout, destinationSamples, destinationLayout))
    {
        return adjustmentFailure(operation, ErrorCode::IncompatibleArguments,
                                 "a color-adjustment source partially overlaps its destination");
    }
    return validateValidity(validity, sourceLayout, destinationSamples, destinationLayout, operation);
}

template <typename SampleT>
[[nodiscard]] const SampleT* channelRow(const void* samples, const ImageLayout& layout, std::uint32_t channel,
                                        std::uint32_t y) noexcept
{
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(samples);
    return reinterpret_cast<const SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

template <typename SampleT>
[[nodiscard]] SampleT* channelRow(void* samples, const ImageLayout& layout, std::uint32_t channel,
                                  std::uint32_t y) noexcept
{
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(samples);
    return reinterpret_cast<SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

template <ResidentSample SampleT, typename TransformT>
[[nodiscard]] Result<ColorAdjustmentResult>
runThreeChannelTransform(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
                         void* destinationSamples, const ImageLayout& destinationLayout,
                         const KernelValidityView& validity, const char* operation, const TransformT& transform)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 6u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 80u};
    std::atomic<std::uint64_t> validPixels{0u};

    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::array<const SampleT*, 3u> sourceRows{};
        std::array<SampleT*, 3u> destinationRows{};
        std::uint64_t localValid = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                sourceRows[channel] = channelRow<SampleT>(sourceSamples, sourceLayout, channel, y);
                destinationRows[channel] = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y);
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                bool accepted = maskAccepts(validity, x, y);
                std::array<double, 3u> input{};
                std::array<double, 3u> output{};
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const std::size_t index = static_cast<std::size_t>(x) * sourceLayout.samplesPerPixel() +
                                              channel % sourceLayout.samplesPerPixel();
                    const SampleT value = sourceRows[channel][index];
                    input[channel] = static_cast<double>(value);
                    accepted = accepted && sampleIsValidByValue(validity.descriptor, value);
                }
                if (!accepted)
                {
                    continue;
                }
                if (!std::isfinite(input[0]) || !std::isfinite(input[1]) || !std::isfinite(input[2]))
                {
                    return adjustmentFailure(operation, ErrorCode::InvalidData,
                                             "a selected color-adjustment sample is not finite");
                }
                const Status transformed = transform(input, output);
                if (transformed.failed())
                {
                    return transformed;
                }
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const std::size_t index = static_cast<std::size_t>(x) * destinationLayout.samplesPerPixel() +
                                              channel % destinationLayout.samplesPerPixel();
                    destinationRows[channel][index] = static_cast<SampleT>(output[channel]);
                }
                ++localValid;
            }
        }
        validPixels.fetch_add(localValid, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    ColorAdjustmentResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = validPixels.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid * 3u;
    return result;
}

struct ComponentPlans
{
    std::optional<ColorConversionPlan> toComponent{};
    std::optional<ColorConversionPlan> toRgb{};
};

// The component space is built from the destination encoding: HSL keeps its primaries and transfer
// function and measures hue in turns, and LCh is natural CIE units under its white point. Both
// directions are planned through the ordinary colour transform, so a source in other primaries, in
// another transfer or in a precomputed component encoding is converted rather than assumed.
[[nodiscard]] Result<ComponentPlans> makeComponentPlans(const ColorEncoding& sourceEncoding,
                                                        const ColorEncoding& destinationEncoding,
                                                        ChannelSchema componentSchema, const char* operation)
{
    if (destinationEncoding.schema != ChannelSchema::Rgb ||
        (sourceEncoding.schema != ChannelSchema::Rgb && sourceEncoding.schema != componentSchema))
    {
        return adjustmentFailure(operation, ErrorCode::IncompatibleArguments,
                                 "the source or destination color schema is not accepted by this adjustment");
    }
    if (checkColorEncoding(sourceEncoding) != ColorEncodingError::None ||
        checkColorEncoding(destinationEncoding) != ColorEncodingError::None)
    {
        return adjustmentFailure(operation, ErrorCode::InvalidArgument,
                                 "a color adjustment was given an invalid color encoding");
    }

    ColorEncoding component = destinationEncoding;
    component.schema = componentSchema;
    component.transferGamma = destinationEncoding.transferGamma;
    if (componentSchema == ChannelSchema::Lch)
    {
        component.primaries = RgbPrimaries::Srgb;
        component.transfer = ColorTransferFunction::Linear;
        component.transferGamma.reset();
        component.whitePoint = destinationEncoding.whitePoint;
        component.perceptual = PerceptualEncoding::NaturalUnits;
    }
    else
    {
        component.hue = HueConvention::TurnFraction;
    }

    Result<ColorConversionPlan> forward = planColorConversion(sourceEncoding, component);
    if (!forward.hasValue())
    {
        return forward.status();
    }
    Result<ColorConversionPlan> reverse = planColorConversion(component, destinationEncoding);
    if (!reverse.hasValue())
    {
        return reverse.status();
    }
    ComponentPlans plans;
    plans.toComponent = *forward;
    plans.toRgb = *reverse;
    return plans;
}

[[nodiscard]] double wrap(double value, double period) noexcept
{
    double result = std::fmod(value, period);
    if (result < 0.0)
    {
        result += period;
    }
    return result;
}

[[nodiscard]] Status transformHslPixel(const ComponentPlans& plans, const HslAdjustment& adjustment,
                                       const std::array<double, 3u>& input, std::array<double, 3u>& output)
{
    std::array<double, 3u> hsl{};
    Status converted = convertColorPixel(*plans.toComponent, input, hsl);
    if (converted.failed())
    {
        return converted;
    }
    hsl[0] = wrap(hsl[0] + adjustment.hueOffsetTurns, 1.0);
    hsl[1] = std::clamp(hsl[1] * adjustment.saturationScale, 0.0, 1.0);
    hsl[2] = std::clamp(hsl[2] + adjustment.lightnessOffset, 0.0, 1.0);
    converted = convertColorPixel(*plans.toRgb, hsl, output);
    if (converted.failed())
    {
        return converted;
    }
    for (double& channel : output)
    {
        channel = std::clamp(channel, 0.0, 1.0);
    }
    return Status::success();
}

[[nodiscard]] Status transformLchPixel(const ComponentPlans& plans, const LchAdjustment& adjustment,
                                       const std::array<double, 3u>& input, std::array<double, 3u>& output)
{
    std::array<double, 3u> lch{};
    Status converted = convertColorPixel(*plans.toComponent, input, lch);
    if (converted.failed())
    {
        return converted;
    }
    lch[0] = std::clamp(lch[0] + adjustment.lightnessOffset, 0.0, 100.0);
    lch[1] = std::clamp(lch[1] * adjustment.chromaScale, 0.0, 200.0);
    lch[2] = wrap(lch[2] + adjustment.hueOffsetDegrees, 360.0);
    converted = convertColorPixel(*plans.toRgb, lch, output);
    if (converted.failed())
    {
        return converted;
    }
    for (double& channel : output)
    {
        channel = std::clamp(channel, 0.0, 1.0);
    }
    return Status::success();
}

// A thresholded RGB-to-HSL of its own, because the hue interval the saturation operation selects is
// stated on this wheel: hue is in sextants over `[0, 6)`, and a pixel whose lightness falls below
// `low` is reported as all zero, which the caller's background test then copies unchanged.
void rgbToSelectiveHsl(const std::array<double, 3u>& rgb, double low, double& hue, double& saturation,
                       double& lightness) noexcept
{
    const double maximum = (std::max)(rgb[0], (std::max)(rgb[1], rgb[2]));
    const double minimum = (std::min)(rgb[0], (std::min)(rgb[1], rgb[2]));
    if (minimum + maximum < low + low)
    {
        hue = 0.0;
        saturation = 0.0;
        lightness = 0.0;
        return;
    }
    lightness = (minimum + maximum) * 0.5;
    hue = 0.0;
    const double delta = maximum - minimum;
    saturation = delta;
    if (delta <= 0.0)
    {
        return;
    }
    saturation /= lightness <= 0.5 ? maximum + minimum : 2.0 - maximum - minimum;
    if (rgb[0] == maximum)
    {
        const double green = (maximum - rgb[1]) / delta;
        const double blue = (maximum - rgb[2]) / delta;
        hue = rgb[1] == minimum ? 5.0 + blue : 1.0 - green;
    }
    else if (rgb[1] == maximum)
    {
        const double red = (maximum - rgb[0]) / delta;
        const double blue = (maximum - rgb[2]) / delta;
        hue = rgb[2] == minimum ? 1.0 + red : 3.0 - blue;
    }
    else
    {
        const double red = (maximum - rgb[0]) / delta;
        const double green = (maximum - rgb[1]) / delta;
        hue = rgb[0] == minimum ? 3.0 + green : 5.0 - red;
    }
}

// The inverse of `rgbToSelectiveHsl` on the same sextant wheel.
[[nodiscard]] std::array<double, 3u> selectiveHslToRgb(double hue, double saturation, double lightness) noexcept
{
    hue = wrap(hue, 6.0);
    const double high =
        lightness <= 0.5 ? lightness * (1.0 + saturation) : lightness + saturation - lightness * saturation;
    if (high <= 0.0)
    {
        return {};
    }
    const double low = lightness + lightness - high;
    const double spread = (high - low) / high;
    const int sextant = static_cast<int>(hue);
    const double fraction = hue - static_cast<double>(sextant);
    const double rising = low + high * spread * fraction;
    const double falling = high - high * spread * fraction;
    switch (sextant)
    {
    case 0:
        return {high, rising, low};
    case 1:
        return {falling, high, low};
    case 2:
        return {low, high, rising};
    case 3:
        return {low, falling, high};
    case 4:
        return {rising, low, high};
    default:
        return {high, low, falling};
    }
}

[[nodiscard]] std::array<double, 3u> hsvToRgb(double hue, double saturation, double value) noexcept
{
    const double sextants = wrap(hue, 1.0) * 6.0;
    const int sextant = static_cast<int>(std::floor(sextants));
    const double fraction = sextants - std::floor(sextants);
    const double low = value * (1.0 - saturation);
    const double falling = value * (1.0 - saturation * fraction);
    const double rising = value * (1.0 - saturation * (1.0 - fraction));
    switch (sextant)
    {
    case 0:
        return {value, rising, low};
    case 1:
        return {falling, value, low};
    case 2:
        return {low, value, rising};
    case 3:
        return {low, falling, value};
    case 4:
        return {rising, low, value};
    default:
        return {value, low, falling};
    }
}

[[nodiscard]] std::array<double, 3u> hsiToRgb(double hue, double saturation, double intensity) noexcept
{
    if (saturation <= 0.0)
    {
        return {intensity, intensity, intensity};
    }
    const double degrees = wrap(hue, 1.0) * 360.0;
    const double radians = std::numbers::pi / 180.0;
    const double sector = std::floor(degrees / 120.0);
    const double within = (degrees - sector * 120.0) * radians;
    // Colorization adds a small constant to the divisor; the divisor never falls below one half, which
    // is why the colour transform's HSI omits it.
    const double low = intensity * (1.0 - saturation);
    const double high =
        intensity * (1.0 + saturation * std::cos(within) / (std::cos(60.0 * radians - within) + 1.0e-12));
    const double rest = 3.0 * intensity - low - high;
    std::array<double, 3u> rgb{};
    if (sector <= 0.0)
    {
        rgb = {high, rest, low};
    }
    else if (sector <= 1.0)
    {
        rgb = {low, high, rest};
    }
    else
    {
        rgb = {rest, low, high};
    }
    for (double& channel : rgb)
    {
        channel = std::clamp(channel, 0.0, 1.0);
    }
    return rgb;
}

template <ResidentSample SampleT>
[[nodiscard]] Result<ColorAdjustmentResult>
colorizeTyped(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
              void* destinationSamples, const ImageLayout& destinationLayout, const ColorizationParameters& parameters,
              const KernelValidityView& validity)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t sourceChannels = sourceLayout.extent().channelCount();
    const double hue = wrap(parameters.hueDegrees, 360.0) / 360.0;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * (sourceChannels + 3u),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 42u};
    std::atomic<std::uint64_t> validPixels{0u};

    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::array<const SampleT*, 3u> sourceRows{};
        std::array<SampleT*, 3u> destinationRows{};
        std::uint64_t localValid = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < sourceChannels; ++channel)
            {
                sourceRows[channel] = channelRow<SampleT>(sourceSamples, sourceLayout, channel, y);
            }
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                destinationRows[channel] = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y);
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                bool accepted = maskAccepts(validity, x, y);
                std::array<double, 3u> intensity{};
                for (std::uint32_t channel = 0u; channel < sourceChannels; ++channel)
                {
                    const std::size_t index = static_cast<std::size_t>(x) * sourceLayout.samplesPerPixel() +
                                              channel % sourceLayout.samplesPerPixel();
                    const SampleT value = sourceRows[channel][index];
                    intensity[channel] = static_cast<double>(value);
                    accepted = accepted && sampleIsValidByValue(validity.descriptor, value);
                }
                if (!accepted)
                {
                    continue;
                }
                if (sourceChannels == 1u)
                {
                    intensity[1] = intensity[0];
                    intensity[2] = intensity[0];
                }
                if (!std::isfinite(intensity[0]) || !std::isfinite(intensity[1]) || !std::isfinite(intensity[2]))
                {
                    return adjustmentFailure(kColorizeOperation, ErrorCode::InvalidData,
                                             "a selected colorization sample is not finite");
                }

                std::array<double, 3u> output{};
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const std::array<double, 3u> rgb = parameters.model == ColorizationModel::Hsv
                                                           ? hsvToRgb(hue, parameters.saturation, intensity[channel])
                                                           : hsiToRgb(hue, parameters.saturation, intensity[channel]);
                    output[channel] = rgb[channel];
                }
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const std::size_t index = static_cast<std::size_t>(x) * destinationLayout.samplesPerPixel() +
                                              channel % destinationLayout.samplesPerPixel();
                    destinationRows[channel][index] = static_cast<SampleT>(output[channel]);
                }
                ++localValid;
            }
        }
        validPixels.fetch_add(localValid, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    ColorAdjustmentResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = validPixels.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid * 3u;
    return result;
}

/// The constants a compiled LCh row reads, from the two component plans, when the plans are the shape the row
/// compiles - a transfer the row knows, the matrix to XYZ, Lab and LCh under one white, and the same way back - and
/// the validity view is one the row answers: every pixel, or every finite pixel, with no mask, over planar rows.
/// Nothing otherwise, and the adjustment runs its scalar authority.
[[nodiscard]] std::optional<LchAdjustCoefficients> compiledLchCoefficients(const ComponentPlans& plans,
                                                                           const LchAdjustment& adjustment,
                                                                           const KernelValidityView& validity,
                                                                           const ImageLayout& sourceLayout,
                                                                           const ImageLayout& destinationLayout)
{
    const ValidityRule rule = validity.descriptor.rule();
    if (validity.mask != nullptr || validity.maskPlane != 0u ||
        (rule != ValidityRule::AllValid && rule != ValidityRule::FiniteOnly) || sourceLayout.samplesPerPixel() != 1u ||
        destinationLayout.samplesPerPixel() != 1u)
    {
        return std::nullopt;
    }
    const std::optional<color_stage_detail::ResolvedColorConversion> forward =
        color_stage_detail::resolveColorConversion(*plans.toComponent);
    const std::optional<color_stage_detail::ResolvedColorConversion> reverse =
        color_stage_detail::resolveColorConversion(*plans.toRgb);
    if (!forward.has_value() || !reverse.has_value())
    {
        return std::nullopt;
    }
    const std::span<const ColorStage> forwardStages(forward->stages.data(), forward->stageCount);
    const std::span<const ColorStage> reverseStages(reverse->stages.data(), reverse->stageCount);
    if (!color_stage_detail::stagesAre(forwardStages,
                                       {ColorStage::LinearRgbToXyz, ColorStage::XyzToLab, ColorStage::LabToLch},
                                       ColorStage::DecodeTransfer, std::nullopt) ||
        !color_stage_detail::stagesAre(reverseStages,
                                       {ColorStage::LchToLab, ColorStage::LabToXyz, ColorStage::XyzToLinearRgb},
                                       std::nullopt, ColorStage::EncodeTransfer) ||
        forward->decode != reverse->encode || forward->destinationWhite != reverse->sourceWhite)
    {
        return std::nullopt;
    }
    LchAdjustCoefficients coefficients;
    switch (forward->decode)
    {
    case ColorTransferFunction::Linear:
        coefficients.transfer = LchAdjustTransfer::Linear;
        break;
    case ColorTransferFunction::Srgb:
        coefficients.transfer = LchAdjustTransfer::Srgb;
        break;
    case ColorTransferFunction::Rec709:
    case ColorTransferFunction::PureGamma:
        return std::nullopt;
    }
    coefficients.skipNonFinite = rule == ValidityRule::FiniteOnly;
    coefficients.toXyz = forward->toXyz.coefficients;
    coefficients.white = forward->destinationWhite;
    coefficients.fromXyz = reverse->fromXyz.coefficients;
    coefficients.lightnessOffset = adjustment.lightnessOffset;
    coefficients.chromaScale = adjustment.chromaScale;
    coefficients.hueOffsetDegrees = adjustment.hueOffsetDegrees;
    return coefficients;
}

/// Runs the compiled LCh row over every row of a planar Float32 image, counting what it adjusted; a pixel that is
/// not finite under the all-valid rule refuses the run as the scalar authority refuses it.
[[nodiscard]] Result<ColorAdjustmentResult>
runCompiledLchAdjustment(const ExecutionContext& execution, LchAdjustFloat32RunFn row,
                         const LchAdjustCoefficients& coefficients, const void* sourceSamples,
                         const ImageLayout& sourceLayout, void* destinationSamples,
                         const ImageLayout& destinationLayout)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(float) * 6u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 80u};
    std::atomic<std::uint64_t> validPixels{0u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t localValid = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            localValid += row(channelRow<float>(sourceSamples, sourceLayout, 0u, y),
                              channelRow<float>(sourceSamples, sourceLayout, 1u, y),
                              channelRow<float>(sourceSamples, sourceLayout, 2u, y),
                              channelRow<float>(destinationSamples, destinationLayout, 0u, y),
                              channelRow<float>(destinationSamples, destinationLayout, 1u, y),
                              channelRow<float>(destinationSamples, destinationLayout, 2u, y), width, coefficients);
        }
        validPixels.fetch_add(localValid, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    ColorAdjustmentResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = validPixels.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid * 3u;
    if (!coefficients.skipNonFinite && result.run.selection.valid != result.run.selection.total)
    {
        return adjustmentFailure(kLchOperation, ErrorCode::InvalidData,
                                 "a selected color-adjustment sample is not finite");
    }
    return result;
}
} // namespace

Result<double> deriveSaturationEnhancementBackground(const ExecutionContext& execution, SampleType sampleType,
                                                     const void* samples, const ImageLayout& layout, double factor,
                                                     SaturationBackgroundLocation location,
                                                     const StatisticsSelection& selection)
{
    if (!execution.valid())
    {
        return adjustmentFailure(kBackgroundOperation, ErrorCode::NotInitialized,
                                 "a saturation background was given an empty execution context");
    }
    if (!std::isfinite(factor) || !isSaturationBackgroundLocation(location))
    {
        return adjustmentFailure(kBackgroundOperation, ErrorCode::InvalidArgument,
                                 "a saturation background factor or estimator is invalid");
    }
    if (layout.extent().channelCount() != 3u)
    {
        return adjustmentFailure(kBackgroundOperation, ErrorCode::IncompatibleArguments,
                                 "a saturation background is derived from an RGB image");
    }
    if (factor <= 0.0)
    {
        return 0.0;
    }
    const Result<BasicStatistics> basic =
        computeImageBasicStatistics(execution, sampleType, samples, layout, 1u, selection);
    if (!basic.hasValue())
    {
        return basic.status();
    }
    double center = basic->mean;
    if (location == SaturationBackgroundLocation::Median)
    {
        const Result<RobustStatistics> robust =
            computeImageRobustStatistics(execution, sampleType, samples, layout, 1u, selection, {});
        if (!robust.hasValue())
        {
            return robust.status();
        }
        center = robust->median;
    }
    return (center + basic->populationStandardDeviation) * factor;
}

Result<ColorAdjustmentResult> adjustImageHsl(const ExecutionContext& execution, SampleType sampleType,
                                             const void* sourceSamples, const ImageLayout& sourceLayout,
                                             void* destinationSamples, const ImageLayout& destinationLayout,
                                             const ColorEncoding& sourceEncoding,
                                             const ColorEncoding& destinationEncoding, const HslAdjustment& adjustment,
                                             const KernelValidityView& validity)
{
    const Status valid = validateImageOperation(execution, sampleType, sourceSamples, sourceLayout, destinationSamples,
                                                destinationLayout, 3u, 3u, validity, kHslOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(adjustment.hueOffsetTurns) || !std::isfinite(adjustment.saturationScale) ||
        !std::isfinite(adjustment.lightnessOffset))
    {
        return adjustmentFailure(kHslOperation, ErrorCode::InvalidArgument,
                                 "an HSL adjustment parameter is not finite");
    }
    const Result<ComponentPlans> plans =
        makeComponentPlans(sourceEncoding, destinationEncoding, ChannelSchema::Hsl, kHslOperation);
    if (!plans.hasValue())
    {
        return plans.status();
    }
    const auto transform = [&](const std::array<double, 3u>& input, std::array<double, 3u>& output) {
        return transformHslPixel(*plans, adjustment, input, output);
    };
    if (sampleType == SampleType::Float32)
    {
        return runThreeChannelTransform<float>(execution, sourceSamples, sourceLayout, destinationSamples,
                                               destinationLayout, validity, kHslOperation, transform);
    }
    return runThreeChannelTransform<double>(execution, sourceSamples, sourceLayout, destinationSamples,
                                            destinationLayout, validity, kHslOperation, transform);
}

Result<ColorAdjustmentResult> adjustImageLch(const ExecutionContext& execution, SampleType sampleType,
                                             const void* sourceSamples, const ImageLayout& sourceLayout,
                                             void* destinationSamples, const ImageLayout& destinationLayout,
                                             const ColorEncoding& sourceEncoding,
                                             const ColorEncoding& destinationEncoding, const LchAdjustment& adjustment,
                                             const KernelValidityView& validity)
{
    const Status valid = validateImageOperation(execution, sampleType, sourceSamples, sourceLayout, destinationSamples,
                                                destinationLayout, 3u, 3u, validity, kLchOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(adjustment.lightnessOffset) || !std::isfinite(adjustment.chromaScale) ||
        !std::isfinite(adjustment.hueOffsetDegrees))
    {
        return adjustmentFailure(kLchOperation, ErrorCode::InvalidArgument,
                                 "an LCh adjustment parameter is not finite");
    }
    const Result<ComponentPlans> plans =
        makeComponentPlans(sourceEncoding, destinationEncoding, ChannelSchema::Lch, kLchOperation);
    if (!plans.hasValue())
    {
        return plans.status();
    }
    const auto transform = [&](const std::array<double, 3u>& input, std::array<double, 3u>& output) {
        return transformLchPixel(*plans, adjustment, input, output);
    };
    if (sampleType == SampleType::Float32)
    {
        // The row compiled for the execution's instruction set, where the plans and the rule are its shape; the
        // reference backend has none and takes the scalar path.
        if (const LchAdjustFloat32RunFn row = lchAdjustFloat32RunFor(execution); row != nullptr)
        {
            const std::optional<LchAdjustCoefficients> coefficients =
                compiledLchCoefficients(*plans, adjustment, validity, sourceLayout, destinationLayout);
            if (coefficients.has_value())
            {
                return runCompiledLchAdjustment(execution, row, *coefficients, sourceSamples, sourceLayout,
                                                destinationSamples, destinationLayout);
            }
        }
        return runThreeChannelTransform<float>(execution, sourceSamples, sourceLayout, destinationSamples,
                                               destinationLayout, validity, kLchOperation, transform);
    }
    return runThreeChannelTransform<double>(execution, sourceSamples, sourceLayout, destinationSamples,
                                            destinationLayout, validity, kLchOperation, transform);
}

Result<ColorAdjustmentResult> enhanceImageSaturation(const ExecutionContext& execution, SampleType sampleType,
                                                     const void* sourceSamples, const ImageLayout& sourceLayout,
                                                     void* destinationSamples, const ImageLayout& destinationLayout,
                                                     const SaturationEnhancementParameters& parameters,
                                                     const KernelValidityView& validity)
{
    const Status valid = validateImageOperation(execution, sampleType, sourceSamples, sourceLayout, destinationSamples,
                                                destinationLayout, 3u, 3u, validity, kSaturationOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(parameters.coefficient) || !std::isfinite(parameters.background) ||
        !std::isfinite(parameters.hueMinimumDegrees) || !std::isfinite(parameters.hueMaximumDegrees) ||
        parameters.background < 0.0 || parameters.background > 1.0 || parameters.hueMinimumDegrees < 0.0 ||
        parameters.hueMinimumDegrees > 360.0 || parameters.hueMaximumDegrees < 0.0 ||
        parameters.hueMaximumDegrees > 360.0)
    {
        return adjustmentFailure(kSaturationOperation, ErrorCode::InvalidArgument,
                                 "a selective-saturation parameter lies outside its stated range");
    }
    const double hueMinimum = parameters.hueMinimumDegrees / 60.0;
    const double hueMaximum = parameters.hueMaximumDegrees / 60.0;
    const bool wrappedRange = hueMinimum > hueMaximum;
    const auto transform = [&](const std::array<double, 3u>& input, std::array<double, 3u>& output) {
        double hue = 0.0;
        double saturation = 0.0;
        double lightness = 0.0;
        rgbToSelectiveHsl(input, parameters.background, hue, saturation, lightness);
        if (lightness <= parameters.background)
        {
            output = input;
            return Status::success();
        }
        const bool selected =
            wrappedRange ? hue >= hueMinimum || hue <= hueMaximum : hue >= hueMinimum && hue <= hueMaximum;
        if (selected)
        {
            saturation = std::clamp(saturation * (1.0 + parameters.coefficient), 0.0, 1.0);
        }
        output = selectiveHslToRgb(hue, saturation, lightness);
        for (double& channel : output)
        {
            channel = std::clamp(channel, 0.0, 1.0);
        }
        return Status::success();
    };
    if (sampleType == SampleType::Float32)
    {
        return runThreeChannelTransform<float>(execution, sourceSamples, sourceLayout, destinationSamples,
                                               destinationLayout, validity, kSaturationOperation, transform);
    }
    return runThreeChannelTransform<double>(execution, sourceSamples, sourceLayout, destinationSamples,
                                            destinationLayout, validity, kSaturationOperation, transform);
}

Result<ColorAdjustmentResult> colorizeImage(const ExecutionContext& execution, SampleType sampleType,
                                            const void* sourceSamples, const ImageLayout& sourceLayout,
                                            void* destinationSamples, const ImageLayout& destinationLayout,
                                            const ColorizationParameters& parameters,
                                            const KernelValidityView& validity)
{
    const Status valid = validateImageOperation(execution, sampleType, sourceSamples, sourceLayout, destinationSamples,
                                                destinationLayout, 1u, 3u, validity, kColorizeOperation);
    if (valid.failed())
    {
        return valid;
    }
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    if ((channels != 1u && channels != 3u) || !isColorizationModel(parameters.model) ||
        !std::isfinite(parameters.hueDegrees) || !std::isfinite(parameters.saturation) || parameters.saturation < 0.0 ||
        parameters.saturation > 1.0)
    {
        return adjustmentFailure(kColorizeOperation, ErrorCode::InvalidArgument,
                                 "a colorization channel count, model, hue or saturation is invalid");
    }
    if (sampleType == SampleType::Float32)
    {
        return colorizeTyped<float>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout,
                                    parameters, validity);
    }
    return colorizeTyped<double>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout,
                                 parameters, validity);
}

} // namespace an
