// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - named two-image compositing modes.
//
// One image pass per call, over row ranges on the operation's threads. Each pixel is read whole
// from both inputs, as doubles, before anything is written, which is what makes exact aliasing with
// either input safe; the validity of both inputs decides whether the pixel is written at all. A
// separable mode then applies one scalar formula per channel, and a colour-aware mode converts the
// pixel's first three channels through the planned colour conversions - and, for the four
// component modes, back again.
//
// When both inputs select every sample the pass needs no per-pixel validity test, and it takes two
// walks of its own: a separable mode runs one channel's row at a time with its formula fixed when
// the walk is compiled, and a colour-aware mode whose conversions are to Lab and back, or to HSI
// and back, runs them a chunk of pixels and one stage at a time with the plans' constants read
// once, computing only the components the mode reads; darker and lighter colour, which read only the
// order of two lightnesses, take it from bounds on the two luminances wherever those are well apart.
// Both compute what the general walk computes; a conversion outside those sequences takes the general
// walk.

#include <AfterNightEngine/Algorithms/Pointwise/BlendMode.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/ColorStages.h"
#include "runtime/TaskPartition.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::blendImages";
constexpr const char* kScalarOperation = "an::evaluateBlendSample";

[[nodiscard]] Status blendFailure(ErrorCode code, const char* message, const char* operation = kOperation)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool isComponentMode(BlendMode mode) noexcept
{
    return mode == BlendMode::Hue || mode == BlendMode::Saturation || mode == BlendMode::Color ||
           mode == BlendMode::Lightness;
}

[[nodiscard]] bool isWholeColorSelection(BlendMode mode) noexcept
{
    return mode == BlendMode::DarkerColor || mode == BlendMode::LighterColor;
}

// The smaller and the larger of two values and a value held to the unit interval, each chosen exactly as
// `std::min`, `std::max` and `std::clamp` choose it but written on values rather than references, which is what
// lets the compiler vectorize a loop of them.
[[nodiscard]] constexpr double lesser(double first, double second) noexcept
{
    return second < first ? second : first;
}

[[nodiscard]] constexpr double greater(double first, double second) noexcept
{
    return first < second ? second : first;
}

[[nodiscard]] constexpr double clampUnit(double value) noexcept
{
    return value < 0.0 ? 0.0 : (1.0 < value ? 1.0 : value);
}

// The per-sample formulas, in double on normalized values, with their special-cased endpoint
// answers - a zero or unit blend answering zero - and every result clamped to the unit interval.
// One function per mode, fixed when it is compiled, so that a walk of one mode inlines its
// formula rather than choosing it for every sample. Where a formula chooses between two
// expressions, both are computed and then one is chosen - the values are the same, a quotient
// the choice discards included, and a loop of the choice can be vectorized. The four component
// modes are not separable and answer zero here; `blendColorPixel` computes them.
template <BlendMode Mode> [[nodiscard]] double evaluateFixed(double base, double blend) noexcept
{
    double value = 0.0;
    if constexpr (Mode == BlendMode::ColorBurn)
    {
        const double burned = 1.0 - lesser((1.0 - base) / blend, 1.0);
        value = blend == 0.0 ? 0.0 : burned;
    }
    else if constexpr (Mode == BlendMode::LinearBurn)
    {
        value = base + blend - 1.0;
    }
    else if constexpr (Mode == BlendMode::DarkerColor)
    {
        value = lesser(base, blend);
    }
    else if constexpr (Mode == BlendMode::LighterColor)
    {
        value = greater(base, blend);
    }
    else if constexpr (Mode == BlendMode::Screen)
    {
        value = 1.0 - (1.0 - base) * (1.0 - blend);
    }
    else if constexpr (Mode == BlendMode::UnscreenStars)
    {
        const double denominator = 1.0 - blend;
        const double unscreened = (base - blend) / denominator;
        value = denominator <= 0.0 ? 0.0 : unscreened;
    }
    else if constexpr (Mode == BlendMode::ColorDodge)
    {
        const double dodged = base / (1.0 - blend);
        value = blend == 1.0 ? 0.0 : dodged;
    }
    else if constexpr (Mode == BlendMode::Overlay)
    {
        const double screened = 1.0 - 2.0 * (1.0 - base) * (1.0 - blend);
        const double multiplied = 2.0 * base * blend;
        value = base > 0.5 ? screened : multiplied;
    }
    else if constexpr (Mode == BlendMode::SoftLight)
    {
        value = (1.0 - 2.0 * blend) * base * base + 2.0 * base * blend;
    }
    else if constexpr (Mode == BlendMode::HardLight)
    {
        const double screened = 1.0 - 2.0 * (1.0 - base) * (1.0 - blend);
        const double multiplied = 2.0 * base * blend;
        value = blend > 0.5 ? screened : multiplied;
    }
    else if constexpr (Mode == BlendMode::VividLight)
    {
        const double burned = greater(0.0, 1.0 - (1.0 - base) / (2.0 * blend));
        const double dodged = lesser(1.0, base / (2.0 * (1.0 - blend)));
        const double chosen = blend < 0.5 ? burned : dodged;
        value = blend == 0.0 ? 0.0 : chosen;
    }
    else if constexpr (Mode == BlendMode::LinearLight)
    {
        const double lit = 2.0 * blend + base - 1.0;
        value = blend == 0.0 ? 0.0 : lit;
    }
    else if constexpr (Mode == BlendMode::PinLight)
    {
        const double pinned = greater(2.0 * blend - 1.0, lesser(base, 2.0 * blend));
        value = blend == 0.0 ? 0.0 : pinned;
    }
    else if constexpr (Mode == BlendMode::Difference)
    {
        // `std::abs` on values: the larger of the difference and its negation, and adding zero makes a negative zero
        // positive.
        const double difference = base - blend;
        value = greater(difference, -difference) + 0.0;
    }
    else if constexpr (Mode == BlendMode::Exclusion)
    {
        value = 0.5 - 2.0 * (base - 0.5) * (blend - 0.5);
    }
    else if constexpr (Mode == BlendMode::Power)
    {
        value = std::pow(base, blend);
    }
    else if constexpr (Mode == BlendMode::ArcTangent)
    {
        value = std::atan2(base, blend);
    }
    return clampUnit(value);
}

/// The formula of a mode chosen at run time; zero for the four component modes.
[[nodiscard]] double evaluateUnchecked(BlendMode mode, double base, double blend) noexcept
{
    switch (mode)
    {
    case BlendMode::ColorBurn:
        return evaluateFixed<BlendMode::ColorBurn>(base, blend);
    case BlendMode::LinearBurn:
        return evaluateFixed<BlendMode::LinearBurn>(base, blend);
    case BlendMode::DarkerColor:
        return evaluateFixed<BlendMode::DarkerColor>(base, blend);
    case BlendMode::LighterColor:
        return evaluateFixed<BlendMode::LighterColor>(base, blend);
    case BlendMode::Screen:
        return evaluateFixed<BlendMode::Screen>(base, blend);
    case BlendMode::UnscreenStars:
        return evaluateFixed<BlendMode::UnscreenStars>(base, blend);
    case BlendMode::ColorDodge:
        return evaluateFixed<BlendMode::ColorDodge>(base, blend);
    case BlendMode::Overlay:
        return evaluateFixed<BlendMode::Overlay>(base, blend);
    case BlendMode::SoftLight:
        return evaluateFixed<BlendMode::SoftLight>(base, blend);
    case BlendMode::HardLight:
        return evaluateFixed<BlendMode::HardLight>(base, blend);
    case BlendMode::VividLight:
        return evaluateFixed<BlendMode::VividLight>(base, blend);
    case BlendMode::LinearLight:
        return evaluateFixed<BlendMode::LinearLight>(base, blend);
    case BlendMode::PinLight:
        return evaluateFixed<BlendMode::PinLight>(base, blend);
    case BlendMode::Difference:
        return evaluateFixed<BlendMode::Difference>(base, blend);
    case BlendMode::Exclusion:
        return evaluateFixed<BlendMode::Exclusion>(base, blend);
    case BlendMode::Power:
        return evaluateFixed<BlendMode::Power>(base, blend);
    case BlendMode::ArcTangent:
        return evaluateFixed<BlendMode::ArcTangent>(base, blend);
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Lightness:
        break;
    }
    return 0.0;
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

[[nodiscard]] Status validateValidity(const KernelValidityView& validity, const ImageLayout& layout,
                                      const void* destinationSamples, const ImageLayout& destinationLayout)
{
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return blendFailure(ErrorCode::InvalidArgument,
                                "a blend validity view supplies a mask for a rule that does not use one");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return blendFailure(ErrorCode::InvalidArgument, "an explicit-mask blend validity view supplies no mask");
    }
    if (validity.mask->width() != layout.extent().width() || validity.mask->height() != layout.extent().height() ||
        validity.maskPlane >= validity.mask->planeCount())
    {
        return blendFailure(ErrorCode::IncompatibleArguments,
                            "a blend validity mask does not cover the image and selected plane");
    }
    if (regionsOverlap(validity.mask->data(), validity.mask->spannedByteCount(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()))
    {
        return blendFailure(ErrorCode::InvalidArgument, "a blend validity mask overlaps the writable destination");
    }
    return Status::success();
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

struct BlendColorPlans
{
    std::optional<ColorConversionPlan> toComponent{};
    std::optional<ColorConversionPlan> fromComponent{};
};

// Plans the round trip a colour-aware mode needs: RGB to HSI for hue and saturation, RGB to Lab
// under the encoding's white point for the Lab modes and for darker or lighter colour, and the
// reverse only for the four component modes, which write a converted pixel back.
[[nodiscard]] Result<BlendColorPlans> makeColorPlans(const BlendModeRequest& request, std::uint32_t channels)
{
    if (channels != 3u && channels != 4u)
    {
        return blendFailure(ErrorCode::IncompatibleArguments,
                            "a whole-color or component blend needs three RGB channels and optional alpha");
    }
    const ChannelSchema expected = channels == 4u ? ChannelSchema::Rgba : ChannelSchema::Rgb;
    if (request.colorEncoding.schema != expected ||
        checkColorEncoding(request.colorEncoding) != ColorEncodingError::None)
    {
        return blendFailure(ErrorCode::InvalidArgument,
                            "the blend color encoding is invalid or disagrees with the image's channel count");
    }

    ColorEncoding rgb = request.colorEncoding;
    rgb.schema = ChannelSchema::Rgb;
    ColorEncoding component;
    if (request.mode == BlendMode::Hue || request.mode == BlendMode::Saturation)
    {
        component = rgb;
        component.schema = ChannelSchema::Hsi;
    }
    else
    {
        component.schema = ChannelSchema::Lab;
        component.whitePoint = rgb.whitePoint;
    }
    Result<ColorConversionPlan> forward = planColorConversion(rgb, component);
    if (!forward.hasValue())
    {
        return forward.status();
    }
    BlendColorPlans plans;
    plans.toComponent = *forward;
    if (isComponentMode(request.mode))
    {
        Result<ColorConversionPlan> reverse = planColorConversion(component, rgb);
        if (!reverse.hasValue())
        {
            return reverse.status();
        }
        plans.fromComponent = *reverse;
    }
    return plans;
}

[[nodiscard]] Status blendColorPixel(BlendMode mode, const BlendColorPlans& plans, const std::array<double, 4u>& base,
                                     const std::array<double, 4u>& blend, std::uint32_t channels,
                                     std::array<double, 4u>& output)
{
    std::array<double, 3u> baseComponent{};
    std::array<double, 3u> blendComponent{};
    Status converted = convertColorPixel(*plans.toComponent, std::span<const double>(base.data(), 3u), baseComponent);
    if (converted.failed())
    {
        return converted;
    }
    converted = convertColorPixel(*plans.toComponent, std::span<const double>(blend.data(), 3u), blendComponent);
    if (converted.failed())
    {
        return converted;
    }

    if (isWholeColorSelection(mode))
    {
        const bool chooseBase = mode == BlendMode::DarkerColor ? baseComponent[0] <= blendComponent[0]
                                                               : baseComponent[0] > blendComponent[0];
        output = chooseBase ? base : blend;
        return Status::success();
    }

    std::array<double, 3u> mixed{};
    switch (mode)
    {
    case BlendMode::Hue:
        mixed = {blendComponent[0], baseComponent[1], baseComponent[2]};
        break;
    case BlendMode::Saturation:
        mixed = {baseComponent[0], blendComponent[1], baseComponent[2]};
        break;
    case BlendMode::Color:
        mixed = {baseComponent[0], blendComponent[1], blendComponent[2]};
        break;
    case BlendMode::Lightness:
        mixed = {blendComponent[0], baseComponent[1], baseComponent[2]};
        break;
    default:
        return blendFailure(ErrorCode::Internal, "a non-color mode reached the color blend path");
    }
    std::array<double, 3u> rgb{};
    converted = convertColorPixel(*plans.fromComponent, mixed, rgb);
    if (converted.failed())
    {
        return converted;
    }
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        output[channel] = std::clamp(rgb[channel], 0.0, 1.0);
    }
    if (channels == 4u)
    {
        output[3] = base[3];
    }
    return Status::success();
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

/// Whether a validity view selects every sample whatever it holds, so a walk needs no test per pixel.
[[nodiscard]] bool selectsEverything(const KernelValidityView& validity) noexcept
{
    return validity.mask == nullptr && validity.descriptor.rule() == ValidityRule::AllValid;
}

/// Whether a sample is an infinity or not a number, read from its exponent so that a run tests it without a branch.
template <typename SampleT> [[nodiscard]] std::uint32_t nonFiniteBit(SampleT value) noexcept
{
    if constexpr (sizeof(SampleT) == sizeof(std::uint32_t))
    {
        return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) == 0x7F800000u ? 1u : 0u;
    }
    else
    {
        return (std::bit_cast<std::uint64_t>(value) & 0x7FF0000000000000ull) == 0x7FF0000000000000ull ? 1u : 0u;
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

/// One channel's run of a separable mode, its formula fixed when the walk is compiled rather than chosen per sample.
///
/// Every sample is read before the one at its own position is written, so an exactly aliased input is safe. A run
/// whose samples are adjacent is tested for finiteness before anything of it is written.
///
/// @return false when a sample of either input is not finite.
template <BlendMode Mode, typename SampleT>
[[nodiscard]] bool blendSeparableRun(const SampleT* base, std::size_t baseStep, const SampleT* blend,
                                     std::size_t blendStep, SampleT* destination, std::size_t destinationStep,
                                     std::uint32_t width) noexcept
{
    if (baseStep == 1u && blendStep == 1u && destinationStep == 1u)
    {
        if (!runIsFinite(base, width) || !runIsFinite(blend, width))
        {
            return false;
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            destination[x] =
                static_cast<SampleT>(evaluateFixed<Mode>(static_cast<double>(base[x]), static_cast<double>(blend[x])));
        }
        return true;
    }
    std::uint32_t nonFinite = 0u;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const SampleT baseValue = base[x * baseStep];
        const SampleT blendValue = blend[x * blendStep];
        nonFinite |= nonFiniteBit(baseValue) | nonFiniteBit(blendValue);
        destination[x * destinationStep] =
            static_cast<SampleT>(evaluateFixed<Mode>(static_cast<double>(baseValue), static_cast<double>(blendValue)));
    }
    return nonFinite == 0u;
}

template <typename SampleT>
using SeparableRun = bool (*)(const SampleT*, std::size_t, const SampleT*, std::size_t, SampleT*, std::size_t,
                              std::uint32_t) noexcept;

/// The run that computes one separable mode; null for the four component modes, which are not separable.
template <typename SampleT> [[nodiscard]] SeparableRun<SampleT> separableRunFor(BlendMode mode) noexcept
{
    switch (mode)
    {
    case BlendMode::ColorBurn:
        return &blendSeparableRun<BlendMode::ColorBurn, SampleT>;
    case BlendMode::LinearBurn:
        return &blendSeparableRun<BlendMode::LinearBurn, SampleT>;
    case BlendMode::DarkerColor:
        return &blendSeparableRun<BlendMode::DarkerColor, SampleT>;
    case BlendMode::LighterColor:
        return &blendSeparableRun<BlendMode::LighterColor, SampleT>;
    case BlendMode::Screen:
        return &blendSeparableRun<BlendMode::Screen, SampleT>;
    case BlendMode::UnscreenStars:
        return &blendSeparableRun<BlendMode::UnscreenStars, SampleT>;
    case BlendMode::ColorDodge:
        return &blendSeparableRun<BlendMode::ColorDodge, SampleT>;
    case BlendMode::Overlay:
        return &blendSeparableRun<BlendMode::Overlay, SampleT>;
    case BlendMode::SoftLight:
        return &blendSeparableRun<BlendMode::SoftLight, SampleT>;
    case BlendMode::HardLight:
        return &blendSeparableRun<BlendMode::HardLight, SampleT>;
    case BlendMode::VividLight:
        return &blendSeparableRun<BlendMode::VividLight, SampleT>;
    case BlendMode::LinearLight:
        return &blendSeparableRun<BlendMode::LinearLight, SampleT>;
    case BlendMode::PinLight:
        return &blendSeparableRun<BlendMode::PinLight, SampleT>;
    case BlendMode::Difference:
        return &blendSeparableRun<BlendMode::Difference, SampleT>;
    case BlendMode::Exclusion:
        return &blendSeparableRun<BlendMode::Exclusion, SampleT>;
    case BlendMode::Power:
        return &blendSeparableRun<BlendMode::Power, SampleT>;
    case BlendMode::ArcTangent:
        return &blendSeparableRun<BlendMode::ArcTangent, SampleT>;
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Lightness:
        break;
    }
    return nullptr;
}

/// How many row ranges each worker is offered for a walk whose every sample makes a transcendental call. Such rows
/// are long and alike, so the scheduler's usual four a worker leaves the last ranges of a busy host to a few workers;
/// the colour conversion's tight loop found the same and offers sixteen.
constexpr std::uint32_t kTranscendentalChunksPerWorker = 16u;

/// A separable mode over two images whose every sample is selected, one channel's run of a row at a time.
template <typename SampleT>
[[nodiscard]] Status blendSeparableSelected(const ExecutionContext& execution, const void* baseSamples,
                                            const ImageLayout& baseLayout, const void* blendSamples,
                                            const ImageLayout& blendLayout, void* destinationSamples,
                                            const ImageLayout& destinationLayout, BlendMode mode, const TaskCost& cost)
{
    const SeparableRun<SampleT> run = separableRunFor<SampleT>(mode);
    if (run == nullptr)
    {
        return blendFailure(ErrorCode::Internal, "a component mode reached the separable blend walk");
    }
    const std::uint32_t width = baseLayout.extent().width();
    const std::uint32_t channels = baseLayout.extent().channelCount();
    const auto walk = [&](TaskRange range) -> Status {
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const SampleT* const base =
                    channelRow<SampleT>(baseSamples, baseLayout, channel, y) + channel % baseLayout.samplesPerPixel();
                const SampleT* const blend = channelRow<SampleT>(blendSamples, blendLayout, channel, y) +
                                             channel % blendLayout.samplesPerPixel();
                SampleT* const destination = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y) +
                                             channel % destinationLayout.samplesPerPixel();
                if (!run(base, baseLayout.samplesPerPixel(), blend, blendLayout.samplesPerPixel(), destination,
                         destinationLayout.samplesPerPixel(), width))
                {
                    return blendFailure(ErrorCode::InvalidData,
                                        "a selected blend sample is not finite in the normalized domain");
                }
            }
        }
        return Status::success();
    };
    // A transcendental mode's rows are long and alike, as the colour walk's are.
    if (mode == BlendMode::Power || mode == BlendMode::ArcTangent)
    {
        return parallelForInChunks(execution, baseLayout.extent().height(), cost, kTranscendentalChunksPerWorker, walk);
    }
    return parallelFor(execution, baseLayout.extent().height(), cost, walk);
}

// The colour walks convert through `kernels/reference/ColorStages.h`: the colour conversion's own stage arithmetic,
// with a plan's constants read once. `BlendModeTest` holds them to the general walk's per-pixel conversion.
using color_stage_detail::applyMatrix;
using color_stage_detail::ChunkPlanes;
using color_stage_detail::decodeChannels;
using color_stage_detail::firstResolvedChannel;
using color_stage_detail::forEachChunkPixel;
using color_stage_detail::hsiHue;
using color_stage_detail::hsiIntensity;
using color_stage_detail::hsiSaturation;
using color_stage_detail::hsiToRgb;
using color_stage_detail::kColorChunk;
using color_stage_detail::labForward;
using color_stage_detail::labToXyz;
using color_stage_detail::resolveColorConversion;
using color_stage_detail::ResolvedColorConversion;
using color_stage_detail::ResolvedPath;
using color_stage_detail::runResolved;
using color_stage_detail::xyzToLab;

/// A chunk's Lab lightness through the fixed sequence to Lab: the decode, the matrix's second row and one
/// compression, the expressions the whole conversion uses for its first channel.
void chunkLightness(const ResolvedColorConversion& forward, ChunkPlanes<3u>& decoded,
                    std::array<double, kColorChunk>& lightness, std::uint32_t count) noexcept
{
    forEachChunkPixel(decoded, count, [&](std::array<double, 3u>& pixel) { decodeChannels(forward, pixel); });
    const std::array<double, 9u>& m = forward.toXyz.coefficients;
    const double white = forward.destinationWhite[1];
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const double tristimulusY = m[3] * decoded[0][index] + m[4] * decoded[1][index] + m[5] * decoded[2][index];
        lightness[index] = 116.0 * labForward(tristimulusY / white) - 16.0;
    }
}

/// A chunk converted completely through the fixed sequence to Lab.
void chunkToLab(const ResolvedColorConversion& forward, ChunkPlanes<3u>& chunk, std::uint32_t count) noexcept
{
    forEachChunkPixel(chunk, count, [&](std::array<double, 3u>& pixel) { decodeChannels(forward, pixel); });
    forEachChunkPixel(chunk, count, [&](std::array<double, 3u>& pixel) { pixel = applyMatrix(forward.toXyz, pixel); });
    forEachChunkPixel(chunk, count, [&](std::array<double, 3u>& pixel) { xyzToLab(forward, pixel); });
}

/// A chunk converted back through the fixed sequence from Lab as far as linear RGB; the curve is left to the hold.
void chunkFromLabToLinear(const ResolvedColorConversion& reverse, ChunkPlanes<3u>& chunk, std::uint32_t count) noexcept
{
    forEachChunkPixel(chunk, count, [&](std::array<double, 3u>& pixel) { labToXyz(reverse, pixel); });
    forEachChunkPixel(chunk, count,
                      [&](std::array<double, 3u>& pixel) { pixel = applyMatrix(reverse.fromXyz, pixel); });
}

/// A linear channel encoded by the reverse conversion's curve and held to the unit interval, without the curve where
/// the hold decides the answer by itself.
[[nodiscard]] double encodedAndHeld(const ResolvedColorConversion& reverse, double linear) noexcept
{
    const std::optional<double> held = color_stage_detail::clampedEncodingWithoutCurve(reverse.encode, linear);
    return held.has_value()
               ? *held
               : clampUnit(color_stage_detail::encodeTransfer(reverse.encode, reverse.encodeGamma, linear));
}

/// How many equal steps of the unit interval darker and lighter colour bound a sample's decode by.
constexpr std::uint32_t kDecodeSteps = 4096u;

/// The decode at the edges of each step of the unit interval; a monotone curve keeps every sample of a step between
/// its two edges' values.
using DecodeEdges = std::array<double, kDecodeSteps + 1u>;

/// Which of two pixels has the lower lightness, when bounds on their luminance alone decide it.
enum class LuminanceOrder : std::uint8_t
{
    Unknown,
    BaseDarker,
    BaseLighter
};

/// Bounds on a pixel's luminance, the second row of the matrix to XYZ over its decoded channels, from the steps its
/// samples fall in; false for a sample outside the unit interval.
[[nodiscard]] bool luminanceInterval(const DecodeEdges& edges, const std::array<double, 9u>& m,
                                     const std::array<double, 3u>& pixel, double& low, double& high) noexcept
{
    std::array<double, 3u> lows{};
    std::array<double, 3u> highs{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const double value = pixel[channel];
        if (!(value >= 0.0 && value <= 1.0))
        {
            return false;
        }
        const std::uint32_t step = (std::min)(static_cast<std::uint32_t>(value * kDecodeSteps), kDecodeSteps - 1u);
        lows[channel] = edges[step];
        highs[channel] = edges[step + 1u];
    }
    low = m[3] * lows[0] + m[4] * lows[1] + m[5] * lows[2];
    high = m[3] * highs[0] + m[4] * highs[1] + m[5] * highs[2];
    return true;
}

/// Which of two pixels is darker, when their luminance bounds are apart by more than every rounding between a
/// luminance and the lightness computed from it - the decode's, the matrix's and the compression's, each a few units
/// in the last place - could close: the lightness is a non-decreasing function of the luminance, so the order of two
/// luminances that far apart is the order of the two lightnesses the conversion computes.
[[nodiscard]] LuminanceOrder luminanceOrder(const DecodeEdges& edges, const std::array<double, 9u>& m,
                                            const std::array<double, 3u>& base,
                                            const std::array<double, 3u>& blend) noexcept
{
    double baseLow = 0.0;
    double baseHigh = 0.0;
    double blendLow = 0.0;
    double blendHigh = 0.0;
    if (!luminanceInterval(edges, m, base, baseLow, baseHigh) ||
        !luminanceInterval(edges, m, blend, blendLow, blendHigh))
    {
        return LuminanceOrder::Unknown;
    }
    const double margin = 1.0e-9 + 1.0e-6 * greater(baseHigh, blendHigh);
    if (baseHigh + margin < blendLow)
    {
        return LuminanceOrder::BaseDarker;
    }
    if (blendHigh + margin < baseLow)
    {
        return LuminanceOrder::BaseLighter;
    }
    return LuminanceOrder::Unknown;
}

/// A colour-aware mode over two images whose every sample is selected, through conversions with a fixed sequence -
/// to Lab and back, or to HSI and back - one chunk of a row and one stage at a time.
template <typename SampleT, bool Lab>
[[nodiscard]] Status
blendColorChunked(const ExecutionContext& execution, const void* baseSamples, const ImageLayout& baseLayout,
                  const void* blendSamples, const ImageLayout& blendLayout, void* destinationSamples,
                  const ImageLayout& destinationLayout, BlendMode mode, const ResolvedColorConversion& forward,
                  const ResolvedColorConversion* reverse, const TaskCost& cost)
{
    const std::uint32_t width = baseLayout.extent().width();
    const std::uint32_t channels = baseLayout.extent().channelCount();
    const std::size_t baseStep = baseLayout.samplesPerPixel();
    const std::size_t blendStep = blendLayout.samplesPerPixel();
    const std::size_t destinationStep = destinationLayout.samplesPerPixel();
    // Darker and lighter colour read only the order of two lightnesses, which is the order of the two luminances; the
    // sRGB decode at the edges of each step of the unit interval bounds each luminance without a power, and the
    // conversion runs for a pixel only where the bounds leave the order open.
    const std::array<double, 9u>& luminanceRow = forward.toXyz.coefficients;
    std::optional<DecodeEdges> edges{};
    if constexpr (Lab)
    {
        if (isWholeColorSelection(mode) && forward.decode == ColorTransferFunction::Srgb && luminanceRow[3] > 0.0 &&
            luminanceRow[4] > 0.0 && luminanceRow[5] > 0.0)
        {
            edges.emplace();
            for (std::uint32_t step = 0u; step <= kDecodeSteps; ++step)
            {
                (*edges)[step] = color_stage_detail::decodeTransfer(
                    forward.decode, forward.decodeGamma, static_cast<double>(step) / static_cast<double>(kDecodeSteps));
            }
        }
    }
    const DecodeEdges* const luminanceEdges = edges.has_value() ? &*edges : nullptr;
    return parallelForInChunks(
        execution, baseLayout.extent().height(), cost, kTranscendentalChunksPerWorker, [&](TaskRange range) -> Status {
            std::array<const SampleT*, 4u> baseRows{};
            std::array<const SampleT*, 4u> blendRows{};
            std::array<SampleT*, 4u> destinationRows{};
            ChunkPlanes<4u> base{};
            ChunkPlanes<4u> blend{};
            ChunkPlanes<3u> first{};
            ChunkPlanes<3u> second{};
            std::array<double, kColorChunk> lightness{};
            for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(y64);
                std::uint32_t nonFinite = 0u;
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    baseRows[channel] = channelRow<SampleT>(baseSamples, baseLayout, channel, y) + channel % baseStep;
                    blendRows[channel] =
                        channelRow<SampleT>(blendSamples, blendLayout, channel, y) + channel % blendStep;
                    destinationRows[channel] = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y) +
                                               channel % destinationStep;
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        nonFinite |= nonFiniteBit(baseRows[channel][x * baseStep]) |
                                     nonFiniteBit(blendRows[channel][x * blendStep]);
                    }
                }
                if (nonFinite != 0u)
                {
                    return blendFailure(ErrorCode::InvalidData,
                                        "a selected blend sample is not finite in the normalized domain");
                }
                for (std::uint32_t begin = 0u; begin < width; begin += kColorChunk)
                {
                    const std::uint32_t count = (std::min)(kColorChunk, width - begin);
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        for (std::uint32_t index = 0u; index < count; ++index)
                        {
                            const std::size_t x = static_cast<std::size_t>(begin) + index;
                            base[channel][index] = static_cast<double>(baseRows[channel][x * baseStep]);
                            blend[channel][index] = static_cast<double>(blendRows[channel][x * blendStep]);
                        }
                    }
                    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        first[channel] = base[channel];
                        second[channel] = blend[channel];
                    }

                    if (isWholeColorSelection(mode))
                    {
                        // The selection copies a whole pixel: the base's where the order of the two lightnesses asks.
                        for (std::uint32_t index = 0u; index < count; ++index)
                        {
                            const std::array<double, 3u> basePixel{first[0][index], first[1][index], first[2][index]};
                            const std::array<double, 3u> blendPixel{second[0][index], second[1][index],
                                                                    second[2][index]};
                            const LuminanceOrder order =
                                luminanceEdges != nullptr
                                    ? luminanceOrder(*luminanceEdges, luminanceRow, basePixel, blendPixel)
                                    : LuminanceOrder::Unknown;
                            bool chooseBase = false;
                            if (order == LuminanceOrder::Unknown)
                            {
                                std::array<double, 3u> baseComponent = basePixel;
                                std::array<double, 3u> blendComponent = blendPixel;
                                double baseLightness = 0.0;
                                double blendLightness = 0.0;
                                if constexpr (Lab)
                                {
                                    baseLightness = firstResolvedChannel<ResolvedPath::ToLab>(forward, baseComponent);
                                    blendLightness = firstResolvedChannel<ResolvedPath::ToLab>(forward, blendComponent);
                                }
                                else
                                {
                                    runResolved(forward, baseComponent);
                                    runResolved(forward, blendComponent);
                                    baseLightness = baseComponent[0];
                                    blendLightness = blendComponent[0];
                                }
                                chooseBase = mode == BlendMode::DarkerColor ? baseLightness <= blendLightness
                                                                            : baseLightness > blendLightness;
                            }
                            else
                            {
                                chooseBase = (order == LuminanceOrder::BaseDarker) == (mode == BlendMode::DarkerColor);
                            }
                            const std::size_t x = static_cast<std::size_t>(begin) + index;
                            for (std::uint32_t channel = 0u; channel < channels; ++channel)
                            {
                                destinationRows[channel][x * destinationStep] =
                                    static_cast<SampleT>(chooseBase ? base[channel][index] : blend[channel][index]);
                            }
                        }
                        continue;
                    }

                    // The mixed components are assembled in `first`, which is then converted back.
                    if constexpr (Lab)
                    {
                        if (mode == BlendMode::Color)
                        {
                            chunkLightness(forward, first, lightness, count);
                            chunkToLab(forward, second, count);
                            first[0] = lightness;
                            first[1] = second[1];
                            first[2] = second[2];
                        }
                        else
                        {
                            chunkToLab(forward, first, count);
                            chunkLightness(forward, second, lightness, count);
                            first[0] = lightness;
                        }
                        chunkFromLabToLinear(*reverse, first, count);
                    }
                    else
                    {
                        // Hue reads the blend's hue and the base's saturation and intensity; Saturation reads the
                        // base's hue and intensity and the blend's saturation.
                        const bool hue = mode == BlendMode::Hue;
                        for (std::uint32_t index = 0u; index < count; ++index)
                        {
                            const std::array<double, 3u> basePixel{first[0][index], first[1][index], first[2][index]};
                            const std::array<double, 3u> blendPixel{second[0][index], second[1][index],
                                                                    second[2][index]};
                            const double baseIntensity = hsiIntensity(basePixel);
                            if (hue)
                            {
                                first[0][index] = hsiHue(forward, blendPixel);
                                first[1][index] = hsiSaturation(basePixel, baseIntensity);
                            }
                            else
                            {
                                first[0][index] = hsiHue(forward, basePixel);
                                first[1][index] = hsiSaturation(blendPixel, hsiIntensity(blendPixel));
                            }
                            first[2][index] = baseIntensity;
                        }
                        forEachChunkPixel(first, count,
                                          [&](std::array<double, 3u>& pixel) { hsiToRgb(*reverse, pixel); });
                    }
                    for (std::uint32_t index = 0u; index < count; ++index)
                    {
                        const std::size_t x = static_cast<std::size_t>(begin) + index;
                        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            const double value = Lab ? encodedAndHeld(*reverse, first[channel][index])
                                                     : clampUnit(first[channel][index]);
                            destinationRows[channel][x * destinationStep] = static_cast<SampleT>(value);
                        }
                        if (channels == 4u)
                        {
                            destinationRows[3][x * destinationStep] = static_cast<SampleT>(base[3][index]);
                        }
                    }
                }
            }
            return Status::success();
        });
}

template <ResidentSample SampleT>
[[nodiscard]] Result<BlendModeResult> blendTyped(const ExecutionContext& execution, const void* baseSamples,
                                                 const ImageLayout& baseLayout, const void* blendSamples,
                                                 const ImageLayout& blendLayout, void* destinationSamples,
                                                 const ImageLayout& destinationLayout, const BlendModeRequest& request,
                                                 const KernelValidityView& baseValidity,
                                                 const KernelValidityView& blendValidity, const BlendColorPlans& plans)
{
    const std::uint32_t width = baseLayout.extent().width();
    const std::uint32_t height = baseLayout.extent().height();
    const std::uint32_t channels = baseLayout.extent().channelCount();
    const bool colorPath = channels >= 3u && (isComponentMode(request.mode) || isWholeColorSelection(request.mode));
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 3u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * (colorPath ? 48u : 8u)};

    if (selectsEverything(baseValidity) && selectsEverything(blendValidity))
    {
        // A colour-aware mode takes its own walk when its conversions are one of the fixed sequences - to Lab and
        // back, or to HSI and back - and the general walk otherwise.
        std::optional<ResolvedColorConversion> forward{};
        std::optional<ResolvedColorConversion> reverse{};
        bool fixedLab = false;
        bool fixedHsi = false;
        if (colorPath)
        {
            forward = resolveColorConversion(*plans.toComponent);
            if (plans.fromComponent.has_value())
            {
                reverse = resolveColorConversion(*plans.fromComponent);
            }
            const bool component = isComponentMode(request.mode);
            fixedLab = forward.has_value() && forward->path == ResolvedPath::ToLab &&
                       (!component || (reverse.has_value() && reverse->path == ResolvedPath::FromLab));
            fixedHsi = forward.has_value() && forward->path == ResolvedPath::ToHsi && reverse.has_value() &&
                       reverse->path == ResolvedPath::FromHsi;
        }
        if (!colorPath || fixedLab || fixedHsi)
        {
            const ResolvedColorConversion* const backward = reverse.has_value() ? &*reverse : nullptr;
            Status selected = Status::success();
            if (!colorPath)
            {
                selected =
                    blendSeparableSelected<SampleT>(execution, baseSamples, baseLayout, blendSamples, blendLayout,
                                                    destinationSamples, destinationLayout, request.mode, cost);
            }
            else if (fixedLab)
            {
                selected = blendColorChunked<SampleT, true>(execution, baseSamples, baseLayout, blendSamples,
                                                            blendLayout, destinationSamples, destinationLayout,
                                                            request.mode, *forward, backward, cost);
            }
            else
            {
                selected = blendColorChunked<SampleT, false>(execution, baseSamples, baseLayout, blendSamples,
                                                             blendLayout, destinationSamples, destinationLayout,
                                                             request.mode, *forward, backward, cost);
            }
            if (selected.failed())
            {
                return selected;
            }
            BlendModeResult result;
            result.run.selection.total = static_cast<std::uint64_t>(width) * height;
            result.run.selection.valid = result.run.selection.total;
            result.run.outputsWritten = result.run.selection.valid * channels;
            return result;
        }
    }

    std::atomic<std::uint64_t> validPixels{0u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::array<const SampleT*, 4u> baseRows{};
        std::array<const SampleT*, 4u> blendRows{};
        std::array<SampleT*, 4u> destinationRows{};
        std::uint64_t localValid = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                baseRows[channel] = channelRow<SampleT>(baseSamples, baseLayout, channel, y);
                blendRows[channel] = channelRow<SampleT>(blendSamples, blendLayout, channel, y);
                destinationRows[channel] = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y);
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                bool accepted = maskAccepts(baseValidity, x, y) && maskAccepts(blendValidity, x, y);
                std::array<double, 4u> base{};
                std::array<double, 4u> blend{};
                std::array<double, 4u> output{};
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const std::size_t baseIndex = static_cast<std::size_t>(x) * baseLayout.samplesPerPixel() +
                                                  channel % baseLayout.samplesPerPixel();
                    const std::size_t blendIndex = static_cast<std::size_t>(x) * blendLayout.samplesPerPixel() +
                                                   channel % blendLayout.samplesPerPixel();
                    const SampleT baseValue = baseRows[channel][baseIndex];
                    const SampleT blendValue = blendRows[channel][blendIndex];
                    base[channel] = static_cast<double>(baseValue);
                    blend[channel] = static_cast<double>(blendValue);
                    if (!sampleIsValidByValue(baseValidity.descriptor, baseValue) ||
                        !sampleIsValidByValue(blendValidity.descriptor, blendValue))
                    {
                        accepted = false;
                    }
                }
                if (!accepted)
                {
                    continue;
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    if (!std::isfinite(base[channel]) || !std::isfinite(blend[channel]))
                    {
                        return blendFailure(ErrorCode::InvalidData,
                                            "a selected blend sample is not finite in the normalized domain");
                    }
                }

                if (colorPath)
                {
                    const Status colored = blendColorPixel(request.mode, plans, base, blend, channels, output);
                    if (colored.failed())
                    {
                        return colored;
                    }
                }
                else
                {
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        output[channel] = evaluateUnchecked(request.mode, base[channel], blend[channel]);
                    }
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const std::size_t destinationIndex =
                        static_cast<std::size_t>(x) * destinationLayout.samplesPerPixel() +
                        channel % destinationLayout.samplesPerPixel();
                    destinationRows[channel][destinationIndex] = static_cast<SampleT>(output[channel]);
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
    BlendModeResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = validPixels.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid * channels;
    return result;
}

} // namespace

Result<double> evaluateBlendSample(BlendMode mode, double base, double blend)
{
    if (!isScalarBlendMode(mode))
    {
        return blendFailure(ErrorCode::InvalidArgument,
                            "the scalar blend evaluator was given an unknown or color-component mode",
                            kScalarOperation);
    }
    if (!std::isfinite(base) || !std::isfinite(blend))
    {
        return blendFailure(ErrorCode::InvalidData, "a scalar blend input is not finite", kScalarOperation);
    }
    return evaluateUnchecked(mode, base, blend);
}

Result<BlendModeResult> blendImages(const ExecutionContext& execution, SampleType sampleType, const void* baseSamples,
                                    const ImageLayout& baseLayout, const void* blendSamples,
                                    const ImageLayout& blendLayout, void* destinationSamples,
                                    const ImageLayout& destinationLayout, const BlendModeRequest& request,
                                    const KernelValidityView& baseValidity, const KernelValidityView& blendValidity)
{
    if (!execution.valid())
    {
        return blendFailure(ErrorCode::NotInitialized, "a blend was given an empty execution context");
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
    if (!isBlendMode(request.mode))
    {
        return blendFailure(ErrorCode::InvalidArgument, "the request names no supported blend mode");
    }
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return blendFailure(ErrorCode::IncompatibleArguments,
                            "a normalized blend runs on a Float32 or Float64 working image");
    }
    if (baseSamples == nullptr || blendSamples == nullptr || destinationSamples == nullptr)
    {
        return blendFailure(ErrorCode::InvalidArgument, "a blend image has a null sample pointer");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (baseLayout.elementSizeBytes() != elementBytes || blendLayout.elementSizeBytes() != elementBytes ||
        destinationLayout.elementSizeBytes() != elementBytes || !isSampleAligned(baseSamples, elementBytes) ||
        !isSampleAligned(blendSamples, elementBytes) || !isSampleAligned(destinationSamples, elementBytes))
    {
        return blendFailure(ErrorCode::InvalidArgument,
                            "a blend layout or pointer does not describe its resident sample type");
    }
    if (!(baseLayout.extent() == blendLayout.extent()) || !(baseLayout.extent() == destinationLayout.extent()))
    {
        return blendFailure(ErrorCode::IncompatibleArguments, "the three blend images do not have one extent");
    }
    const std::uint32_t channels = baseLayout.extent().channelCount();
    if (channels > 4u || ((isComponentMode(request.mode) || isWholeColorSelection(request.mode)) && channels != 1u &&
                          channels != 3u && channels != 4u))
    {
        return blendFailure(ErrorCode::IncompatibleArguments,
                            "this blend mode does not support the image's channel count");
    }
    if ((isComponentMode(request.mode) && channels == 1u) ||
        (isWholeColorSelection(request.mode) && channels != 1u && channels < 3u))
    {
        return blendFailure(ErrorCode::IncompatibleArguments,
                            "a color-component blend needs an RGB image; mono conversion is an explicit operation");
    }

    const auto overlapsDestination = [&](const void* source, const ImageLayout& layout) {
        return regionsOverlap(static_cast<const std::uint8_t*>(source), layout.totalBytes(),
                              static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()) &&
               !exactlyAliases(source, layout, destinationSamples, destinationLayout);
    };
    if (overlapsDestination(baseSamples, baseLayout) || overlapsDestination(blendSamples, blendLayout))
    {
        return blendFailure(ErrorCode::IncompatibleArguments,
                            "a blend input partially overlaps the destination without exactly aliasing it");
    }
    Status valid = validateValidity(baseValidity, baseLayout, destinationSamples, destinationLayout);
    if (valid.failed())
    {
        return valid;
    }
    valid = validateValidity(blendValidity, blendLayout, destinationSamples, destinationLayout);
    if (valid.failed())
    {
        return valid;
    }

    BlendColorPlans plans;
    if ((isComponentMode(request.mode) || isWholeColorSelection(request.mode)) && channels >= 3u)
    {
        Result<BlendColorPlans> made = makeColorPlans(request, channels);
        if (!made.hasValue())
        {
            return made.status();
        }
        plans = *made;
    }
    if (sampleType == SampleType::Float32)
    {
        return blendTyped<float>(execution, baseSamples, baseLayout, blendSamples, blendLayout, destinationSamples,
                                 destinationLayout, request, baseValidity, blendValidity, plans);
    }
    return blendTyped<double>(execution, baseSamples, baseLayout, blendSamples, blendLayout, destinationSamples,
                              destinationLayout, request, baseValidity, blendValidity, plans);
}

} // namespace an
