// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The channel and conversion actions' one table, and what an action, the grey and colour conversions and the negative
/// do to one image.

#include "stdafx.hpp"

#include "processes/ChannelActions.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace process_utils
{

namespace
{

using an::ChannelScope;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

constexpr ChannelActionOutput out(ChannelScope scope, const char* suffix, const char* label) noexcept
{
    return ChannelActionOutput{.scope = scope, .suffix = suffix, .label = label};
}

constexpr ChannelAction extract(const char* action, const char* member, std::array<ChannelActionOutput, 3u> outputs,
                                std::uint32_t count, const char* progress) noexcept
{
    return ChannelAction{.action = action,
                         .member = member,
                         .kind = ChannelActionKind::Extract,
                         .outputs = outputs,
                         .outputCount = count,
                         .progressBefore = progress,
                         .progressAfter = ""};
}

constexpr ChannelAction convert(const char* action, const char* member, an::SampleType target, const char* suffix,
                                const char* label, const char* progressAfter) noexcept
{
    return ChannelAction{.action = action,
                         .member = member,
                         .kind = ChannelActionKind::Convert,
                         .suffix = suffix,
                         .label = label,
                         .target = target,
                         .progressBefore = "Converting sequence ",
                         .progressAfter = progressAfter};
}

constexpr ChannelActionOutput kRed = out(ChannelScope::Red, " - RED", "Red channel");
constexpr ChannelActionOutput kGreen = out(ChannelScope::Green, " - GREEN", "Green channel");
constexpr ChannelActionOutput kBlue = out(ChannelScope::Blue, " - BLUE", "Blue channel");
constexpr ChannelActionOutput kCieL = out(ChannelScope::CieLightness, " - CIE L*", "CIE L*");
constexpr ChannelActionOutput kCieA = out(ChannelScope::CieA, " - CIE a*", "CIE a*");
constexpr ChannelActionOutput kCieB = out(ChannelScope::CieB, " - CIE b*", "CIE b*");
constexpr ChannelActionOutput kCieC = out(ChannelScope::CieChroma, " - CIE c*", "CIE c*");
constexpr ChannelActionOutput kCieH = out(ChannelScope::CieHue, " - CIE h*", "CIE h*");
constexpr ChannelActionOutput kCieX = out(ChannelScope::CieX, " - CIE X", "CIE X");
constexpr ChannelActionOutput kCieY = out(ChannelScope::CieY, " - CIE Y", "CIE Y");
constexpr ChannelActionOutput kCieZ = out(ChannelScope::CieZ, " - CIE Z", "CIE Z");
constexpr ChannelActionOutput kHslH = out(ChannelScope::HslHue, " - HSL Hue", "HSL Hue");
constexpr ChannelActionOutput kHslS = out(ChannelScope::HslSaturation, " - HSL Saturation", "HSL Saturation");
constexpr ChannelActionOutput kHslL = out(ChannelScope::HslLightness, " - HSL Lightness", "HSL Lightness");
constexpr ChannelActionOutput kHsvH = out(ChannelScope::HsvHue, " - HSV Hue", "HSV Hue");
constexpr ChannelActionOutput kHsvS = out(ChannelScope::HsvSaturation, " - HSV Saturation", "HSV Saturation");
constexpr ChannelActionOutput kHsvV = out(ChannelScope::HsvValue, " - HSV Value", "HSV Value");
constexpr ChannelActionOutput kHsiH = out(ChannelScope::HsiHue, " - HSI Hue", "HSI Hue");
constexpr ChannelActionOutput kHsiS = out(ChannelScope::HsiSaturation, " - HSI Saturation", "HSI Saturation");
constexpr ChannelActionOutput kHsiI = out(ChannelScope::HsiIntensity, " - HSI Intensity", "HSI Intensity");
constexpr ChannelActionOutput kLuma = out(ChannelScope::YCbCrLuma, " - YCbCr Luma", "YCbCr Luma");
constexpr ChannelActionOutput kBlueChroma =
    out(ChannelScope::YCbCrBlueChroma, " - YCbCr Blue Chroma", "YCbCr Blue Chroma");
constexpr ChannelActionOutput kRedChroma = out(ChannelScope::YCbCrRedChroma, " - YCbCr Red Chroma", "YCbCr Red Chroma");

/// The actions' names and texts: a view of an extraction is the source's name and the plane's suffix, and its history
/// says what was extracted from which image or sequence.
constexpr std::array<ChannelAction, 39u> kActions{
    extract("actionExtract_Red_Channel", "extractRedChannel", {kRed}, 1u, "Extracting red channel from sequence "),
    extract("actionExtract_Green_Channel", "extractGreenChannel", {kGreen}, 1u,
            "Extracting green channel from sequence "),
    extract("actionExtract_Blue_Channel", "extractBlueChannel", {kBlue}, 1u, "Extracting blue channel from sequence "),
    extract("actionExtract_RGB_Channels", "extractRGBChannels", {kRed, kGreen, kBlue}, 3u,
            "Extracting RGB channels from sequence "),
    extract("actionExtract_CIE_L", "extractCIEL", {kCieL}, 1u, "Extracting CIE L* channel from sequence "),
    extract("actionExtract_CIE_a", "extractCIEA", {kCieA}, 1u, "Extracting CIE a* channel from sequence "),
    extract("actionExtract_CIE_b", "extractCIEB", {kCieB}, 1u, "Extracting CIE b* channel from sequence "),
    extract("actionExtract_CIE_c", "extractCIEC", {kCieC}, 1u, "Extracting CIE c* channel from sequence "),
    extract("actionExtract_CIE_h", "extractCIEH", {kCieH}, 1u, "Extracting CIE h* channel from sequence "),
    extract("actionExtract_CIE_L_c_h", "extractCIELCH", {kCieL, kCieC, kCieH}, 3u,
            "Extracting CIE L*c*h from sequence "),
    ChannelAction{.action = "actionExtract_CIE_a_b",
                  .member = "extractCIEAB",
                  .kind = ChannelActionKind::ChromaView,
                  .outputs = {kCieA, kCieB},
                  .outputCount = 2u,
                  .suffix = " - CIE a*=R / b*=G",
                  .label = "CIE a*=R / b*=G",
                  .progressBefore = "Extracting CIE a*=R / b*=G from sequence ",
                  .progressAfter = ""},
    extract("actionExtract_CIE_L_a_b", "extractCIELAB", {kCieL, kCieA, kCieB}, 3u,
            "Extracting CIE L*a*b from sequence "),
    extract("actionExtract_CIE_X", "extractCIEX", {kCieX}, 1u, "Extracting CIE X channel from sequence "),
    extract("actionExtract_CIE_Y", "extractCIEY", {kCieY}, 1u, "Extracting CIE Y channel from sequence "),
    extract("actionExtract_CIE_Z", "extractCIEZ", {kCieZ}, 1u, "Extracting CIE Z channel from sequence "),
    extract("actionExtract_CIE_X_Y_Z", "extractCIEXYZ", {kCieX, kCieY, kCieZ}, 3u, "Extracting CIE XYZ from sequence "),
    extract("actionExtract_HSL_Hue", "extractHSL_H", {kHslH}, 1u, "Extracting HSL Hue channel from sequence "),
    extract("actionExtract_HSL_Saturation", "extractHSL_S", {kHslS}, 1u,
            "Extracting HSL Saturation channel from sequence "),
    extract("actionExtract_HSL_Lightness", "extractHSL_L", {kHslL}, 1u,
            "Extracting HSL Lightness channel from sequence "),
    extract("actionExtract_HSL", "extractHSL", {kHslH, kHslS, kHslL}, 3u, "Extracting HSL from sequence "),
    extract("actionExtract_HSV_Hue", "extractHSV_H", {kHsvH}, 1u, "Extracting HSV Hue channel from sequence "),
    extract("actionExtract_HSV_Saturation", "extractHSV_S", {kHsvS}, 1u,
            "Extracting HSV Saturation channel from sequence "),
    extract("actionExtract_HSV_Value", "extractHSV_V", {kHsvV}, 1u, "Extracting HSV Value channel from sequence "),
    extract("actionExtract_HSV", "extractHSV", {kHsvH, kHsvS, kHsvV}, 3u, "Extracting HSV from sequence "),
    extract("actionExtract_HSI_Hue", "extractHSI_H", {kHsiH}, 1u, "Extracting HSI Hue channel from sequence "),
    extract("actionExtract_HSI_Saturation", "extractHSI_S", {kHsiS}, 1u,
            "Extracting HSI Saturation channel from sequence "),
    extract("actionExtract_HSI_Intensity", "extractHSI_I", {kHsiI}, 1u,
            "Extracting HSI Intensity channel from sequence "),
    extract("actionExtract_HSI", "extractHSI", {kHsiH, kHsiS, kHsiI}, 3u, "Extracting HSI from sequence "),
    extract("actionExtract_YCbCr_Luminance", "extractYCbCr_Y", {kLuma}, 1u, "Extracting YCbCr Luma from sequence "),
    extract("actionExtract_YCbCr_Blue_Chrominance", "extractYCbCr_Cb", {kBlueChroma}, 1u,
            "Extracting YCbCr Blue Chroma from sequence "),
    extract("actionExtract_YCbCr_Red_Chrominance", "extractYCbCr_Cr", {kRedChroma}, 1u,
            "Extracting YCbCr Red Chroma from sequence "),
    extract("actionExtract_YCbCr", "extractYCbCr", {kLuma, kBlueChroma, kRedChroma}, 3u,
            "Extracting YCbCr from sequence "),
    convert("actionConvert_to_8_bit", "convertTo8Bit", an::SampleType::UInt8, " - CONV_8_UINT", "8 bits", " to 8 bits"),
    convert("actionConvert_to_signed_16_bit", "convertToSigned16Bit", an::SampleType::Int16, " - CONV_16_INT",
            "signed 16 bits", " to signed 16 bits"),
    convert("actionConvert_to_unsigned_16_bit", "convertToUnsigned16Bit", an::SampleType::UInt16, " - CONV_16_UINT",
            "unsigned 16 bits", " to unsigned 16 bits"),
    convert("actionConvert_to_signed_32_bit", "convertToSigned32Bit", an::SampleType::Int32, " - CONV_32_INT",
            "signed 32 bits", " to signed 32 bits"),
    convert("actionConvert_to_unsigned_32_bit", "convertToUnsigned32Bit", an::SampleType::UInt32, " - CONV_32_UINT",
            "unsigned 32 bits", " to unsigned 32 bits"),
    convert("actionConvert_to_32_bit_float", "convertToFloat32", an::SampleType::Float32, " - CONV_32_FLOAT",
            "32 bits floating point", " to 32 bits floating point"),
    convert("actionConvert_to_64_bit_float", "convertToFloat64", an::SampleType::Float64, " - CONV_64_FLOAT",
            "64 bits floating point", " to 64 bits floating point"),
};

// ---- What an image is.

[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

[[nodiscard]] bool isGreyImage(const an::ImageDescriptor& described) noexcept
{
    return described.extent().channelCount() == 1u;
}

/// The smallest and the largest value of an integer representation, as doubles; nothing for a floating one.
[[nodiscard]] std::optional<std::pair<double, double>> integerRange(an::SampleType type) noexcept
{
    std::optional<std::pair<double, double>> range;
    (void)an::dispatchSampleType(type, [&range]<typename SampleT>() {
        if constexpr (std::is_integral_v<SampleT>)
        {
            range.emplace(static_cast<double>((std::numeric_limits<SampleT>::min)()),
                          static_cast<double>((std::numeric_limits<SampleT>::max)()));
        }
    });
    return range;
}

[[nodiscard]] bool isSignedInteger(an::SampleType type) noexcept
{
    const std::optional<std::pair<double, double>> range = integerRange(type);
    return range.has_value() && range->first < 0.0;
}

/// The description an image is read under as fractions of full scale: its own, except that a sensor code or a
/// physical quantity - as every integer and every floating file is read - is read as a fraction, as the view shows it.
[[nodiscard]] an::Result<an::ImageDescriptor> fractionsDescriptorOf(const an::ImageDescriptor& described)
{
    const an::NumericDomain domain = described.numericDomain();
    if (domain != an::NumericDomain::PhysicalScaled && domain != an::NumericDomain::RawSensorCode)
    {
        return described;
    }
    an::ImageDescriptorRequest restated = described.toRequest();
    restated.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> fractions = an::ImageDescriptor::create(restated);
    if (!fractions.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image has no description as fractions of full scale");
    }
    return *fractions;
}

/// The description of an image with another representation or channel count, read as fractions of full scale when
/// `asFractions` says so and in its own domain otherwise.
[[nodiscard]] an::Result<an::ImageDescriptor> restatedDescriptor(const an::ImageDescriptor& described,
                                                                 an::SampleType sampleType,
                                                                 std::optional<std::uint32_t> channels,
                                                                 const an::EngineLimits& limits, bool asFractions)
{
    an::Result<an::ImageDescriptor> fractions =
        asFractions ? fractionsDescriptorOf(described) : an::Result<an::ImageDescriptor>(described);
    if (!fractions.hasValue())
    {
        return fractions.status();
    }
    an::ImageDescriptorRequest request = fractions->toRequest();
    request.sampleType = sampleType;
    if (channels.has_value())
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(described.extent().width(), described.extent().height(), *channels, limits);
        if (!extent.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the image cannot be described with that many channels");
        }
        request.extent = *extent;
        request.channelSchema = *channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb;
        if (*channels == 1u)
        {
            request.colorSpace = an::ColorSpace::Unspecified;
        }
        request.cfa = an::CfaDescriptor{};
    }
    const std::optional<an::ImageDescriptor> restated = an::ImageDescriptor::create(request);
    if (!restated.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image cannot be described that way");
    }
    return *restated;
}

// ---- Planes of a planar image.

/// The layout of one plane of a planar image, at the image's own strides.
[[nodiscard]] an::Result<an::ImageLayout> planeLayoutOf(const an::ImageLayout& layout, const an::EngineLimits& limits)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(layout.extent().width(), layout.extent().height(), 1u, limits);
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a plane of the image cannot be described");
    }
    const std::optional<an::ImageLayout> plane = an::ImageLayout::createWithStrides(
        an::ImageLayoutRequest{.extent = *extent,
                               .elementSizeBytes = layout.elementSizeBytes(),
                               .samplesPerPixel = 1u,
                               .planeCount = 1u,
                               .rowAlignmentBytes = 1u},
        layout.rowStrideBytes(), layout.rowStrideBytes() * static_cast<std::uint64_t>(layout.extent().height()),
        limits);
    if (!plane.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a plane of the image cannot be laid out");
    }
    return *plane;
}

/// The first byte of one plane of a planar image.
template <typename ByteT>
[[nodiscard]] ByteT* planeStart(ByteT* data, const an::ImageLayout& layout, std::uint32_t plane) noexcept
{
    return data + static_cast<std::uint64_t>(plane) * layout.planeStrideBytes();
}

// ---- Validity.

[[nodiscard]] std::optional<an::ValidityMaskView<const std::uint8_t>> maskViewOf(const an::ValidityMask* mask)
{
    if (mask == nullptr)
    {
        return std::nullopt;
    }
    return mask->view();
}

/// One bit-plane of a mask with one plane per channel, as a mask view of its own.
[[nodiscard]] std::optional<an::ValidityMaskView<const std::uint8_t>>
bitPlaneOf(const an::ValidityMaskView<const std::uint8_t>& mask, std::uint32_t plane, const an::EngineLimits& limits)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(mask.width(), mask.height(), 1u, limits);
    if (plane >= mask.planeCount() || !extent.has_value())
    {
        return std::nullopt;
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask.planeStrideBytes();
    const std::uint64_t spanned = mask.spannedByteCount();
    if (offset >= spanned)
    {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> bits(mask.data() + offset, static_cast<std::size_t>(spanned - offset));
    return an::ValidityMaskView<const std::uint8_t>::create(bits, *extent, mask.rowStrideBytes(),
                                                            mask.planeStrideBytes());
}

/// A new mask of one plane, or of as many as `planes`, over an image's width and height.
[[nodiscard]] an::Result<an::ValidityMask> newMask(const an::EngineContext& engine,
                                                   const an::ImageDescriptor& described, std::uint32_t planes)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(described.extent().width(), described.extent().height(), planes, engine.limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the mask cannot be described");
    }
    return an::ValidityMask::create(engine, *extent);
}

/// One plane of a mask, copied into a mask of its own: the plane of a channel of a mask with one plane per channel,
/// or the one plane of a shared mask.
[[nodiscard]] an::Result<an::ValidityMask> maskPlaneOf(const an::EngineContext& engine,
                                                       const an::ImageDescriptor& image, const an::ValidityMask& mask,
                                                       std::uint32_t channel)
{
    const std::optional<an::ValidityMaskView<const std::uint8_t>> source = mask.view();
    if (!source.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image's mask holds nothing");
    }
    if (source->planeCount() == 1u)
    {
        return mask.clone();
    }
    an::Result<an::ValidityMask> plane = newMask(engine, image, 1u);
    if (!plane.hasValue())
    {
        return plane.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> written = plane->view();
    const std::optional<an::ValidityMaskView<const std::uint8_t>> read = bitPlaneOf(*source, channel, engine.limits());
    if (!written.has_value() || !read.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the mask has no plane for that channel");
    }
    if (an::Status copied = an::copyValidity(*written, *read); copied.failed())
    {
        return copied;
    }
    return plane;
}

/// The semantics an image keeps with its validity restated.
[[nodiscard]] an::Result<an::ImageSemantics> withValidity(const an::ImageSemantics& semantics,
                                                          const an::ValidityDescriptor& validity)
{
    an::ImageSemanticsRequest request = semantics.toRequest();
    request.validity = validity;
    return an::ImageSemantics::create(request);
}

/// The validity rule an explicit mask states.
[[nodiscard]] an::ValidityDescriptor explicitMaskRule()
{
    return *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
}

/// The validity rule a colour component states: every sample, unless the image's validity rejects some, whose
/// component the scope hands over as not-a-number.
[[nodiscard]] an::ValidityDescriptor componentRule(const an::ValidityDescriptor& source)
{
    if (source.isAllValid())
    {
        return an::ValidityDescriptor::allValid();
    }
    return *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
}

/// The mask of a value rule over an image's samples: one plane that calls a pixel valid only where every channel is.
[[nodiscard]] an::Result<an::ValidityMask> sharedMaskOfRule(const an::EngineContext& engine,
                                                            const an::consumer::AnyImage& image,
                                                            const an::ValidityDescriptor& rule)
{
    const std::optional<an::ImageDescriptor> described = descriptorOf(image);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    an::Result<an::ValidityMask> mask = newMask(engine, *described, 1u);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> written = mask->view();
    if (!written.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the mask holds nothing");
    }
    const an::Status marked = std::visit(
        [&](const auto& typed) -> an::Status {
            const auto view = typed.view();
            if (!view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            return an::markValidity(*written, *view, rule);
        },
        image);
    if (marked.failed())
    {
        return marked;
    }
    return mask;
}

/// A shared mask that calls a pixel valid only where every plane of a mask does.
[[nodiscard]] an::Result<an::ValidityMask>
intersectionOfPlanes(const an::EngineContext& engine, const an::ImageDescriptor& image, const an::ValidityMask& mask)
{
    const std::optional<an::ValidityMaskView<const std::uint8_t>> source = mask.view();
    if (!source.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image's mask holds nothing");
    }
    an::Result<an::ValidityMask> shared = maskPlaneOf(engine, image, mask, 0u);
    if (!shared.hasValue() || source->planeCount() == 1u)
    {
        return shared;
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> written = shared->view();
    for (std::uint32_t plane = 1u; plane < source->planeCount() && written.has_value(); ++plane)
    {
        const std::optional<an::ValidityMaskView<const std::uint8_t>> read =
            bitPlaneOf(*source, plane, engine.limits());
        if (!read.has_value())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "the mask has no such plane");
        }
        if (an::Status combined =
                an::combineValidity(*written, written->constView(), *read, an::ValidityCombination::Intersection);
            combined.failed())
        {
            return combined;
        }
    }
    return shared;
}

// ---- Recording what was done.

/// What an operation here does to the image it makes: it writes every sample, restates the description and, where
/// it says so, the meaning; a solution still describes the pixels, which no operation here moves; an output decision
/// is withdrawn, because the image is no longer the one it was made for.
[[nodiscard]] constexpr an::OperationEffect effectOf(bool restatesMeaning, bool dropsCards) noexcept
{
    return an::OperationEffect{
        .samples = an::SampleEffect::Writes,
        .description = an::DescriptionEffect::Restates,
        .semantics = restatesMeaning ? an::SemanticEffect::Restates : an::SemanticEffect::Preserves,
        .solution = an::SolutionEffect::Preserves,
        .codecMetadata = dropsCards ? an::CodecMetadataEffect::Drops : an::CodecMetadataEffect::Preserves,
        .outputIntent = an::OutputIntentEffect::Withdraws,
        .validity = an::ValidityTransition{.policy = restatesMeaning ? an::ValidityPolicy::Regenerate
                                                                     : an::ValidityPolicy::Preserve}};
}

/// States what was done on a new image that carries its source's metadata.
template <typename SampleT>
[[nodiscard]] an::Status record(an::Image<SampleT>& image, const an::OperationEffect& effect, std::string operation,
                                std::string detail, std::optional<an::ImageSemantics> semantics = std::nullopt)
{
    an::OperationEffect stated = effect;
    if (semantics.has_value())
    {
        stated.validity.produced = semantics->validity();
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        stated, an::OperationOutcome{
                    .operation = std::move(operation), .detail = std::move(detail), .semantics = std::move(semantics)});
    if (!change.hasValue())
    {
        return change.status();
    }
    return image.transition(*change);
}

// ---- Extraction.

/// The image an extraction reads a colour component from: the image itself when it is floating, read as fractions of
/// full scale, and a floating copy in those fractions when it is integer.
struct ComponentSource
{
    std::optional<NormalizedCopy> normalized;
    an::ImageDescriptor described;
    an::ChannelScopeSource scoped{};
    an::ImageLayout layout;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
};

[[nodiscard]] an::Result<ComponentSource> componentSourceOf(const an::ui::ExecutionScope& scope,
                                                            const an::ui::ProcessSource& source,
                                                            const an::ImageDescriptor& described)
{
    if (an::isIntegerSampleType(described.sampleType()))
    {
        an::Result<NormalizedCopy> copy = normalizedCopyOf(scope, source);
        if (!copy.hasValue())
        {
            return copy.status();
        }
        an::Result<an::ImageDescriptor> working =
            restatedDescriptor(described, copy->samples.sampleType(), std::nullopt, scope.engine().limits(), true);
        if (!working.hasValue())
        {
            return working.status();
        }
        an::ImageDescriptorRequest fractions = working->toRequest();
        fractions.numericDomain = an::NumericDomain::NormalizedLinear;
        const std::optional<an::ImageDescriptor> read = an::ImageDescriptor::create(fractions);
        if (!read.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the image cannot be described as unit fractions");
        }
        const an::ImageLayout layout = copy->samples.layout();
        ComponentSource component{.normalized = std::move(*copy), .described = *read, .layout = layout};
        component.scoped.samples = component.normalized->samples.samples();
        component.scoped.validity = component.normalized->samples.validity();
        return component;
    }
    an::Result<an::ImageDescriptor> fractions = fractionsDescriptorOf(described);
    if (!fractions.hasValue())
    {
        return fractions.status();
    }
    return std::visit(
        [&](const auto& typed) -> an::Result<ComponentSource> {
            const auto view = typed.view();
            if (!view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            ComponentSource component{.described = *fractions, .layout = view->layout()};
            component.mask = maskViewOf(source.validity());
            component.scoped.samples = view->data();
            component.scoped.validity.descriptor = source.metadata().semantics().validity();
            return component;
        },
        source.image());
}

/// A new grey plane of a description, carrying the source's metadata until the extraction states what it is.
template <typename PlaneT, typename SourceT>
[[nodiscard]] an::Result<an::Image<PlaneT>>
newPlane(const an::EngineContext& engine, const an::ImageDescriptor& described, const an::Image<SourceT>& source)
{
    return an::Image<PlaneT>::createDestination(engine, described, an::StorageArrangement::Planar,
                                                an::StorageInitialization::Zeroed, source);
}

/// Writes one plane the scope extracts into a plane of a planar image.
[[nodiscard]] an::Status extractInto(const an::ExecutionContext& execution, const an::ChannelScopePlan& plan,
                                     const an::ChannelScopeSource& scoped, void* destination,
                                     const an::ImageLayout& layout)
{
    const an::Result<an::ChannelScopeResult> extracted =
        an::extractChannelScope(execution, plan, scoped, destination, layout);
    return extracted.hasValue() ? an::Status::success() : extracted.status();
}

/// A stored channel of a colour image, in the image's own representation and meaning: its rule, and its own plane of
/// an explicit mask.
[[nodiscard]] an::Result<an::consumer::ImageAsset> storedChannelOf(const an::ui::ExecutionScope& scope,
                                                                   const an::ui::ProcessSource& source,
                                                                   const an::ImageDescriptor& described,
                                                                   const ChannelActionOutput& output)
{
    const an::Result<an::ChannelScopePlan> plan =
        an::planChannelScope(an::ui::processChannelScopeRequest(output.scope, described));
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageDescriptor> planeDescribed = an::channelScopePlaneDescriptor(*plan, described);
    if (!planeDescribed.hasValue())
    {
        return planeDescribed.status();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> mask = maskViewOf(source.validity());
    an::Result<an::consumer::AnyImage> plane = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            if (!view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            an::Result<an::Image<SampleT>> made = newPlane<SampleT>(scope.engine(), *planeDescribed, typed);
            if (!made.hasValue())
            {
                return made.status();
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = made->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the plane holds nothing");
                }
                const an::ImageLayout sourceLayout = view->layout();
                an::ChannelScopeSource scoped;
                scoped.descriptor = &described;
                scoped.samples = view->data();
                scoped.layout = &sourceLayout;
                scoped.validity.descriptor = typed.metadata().semantics().validity();
                scoped.validity.mask = mask.has_value() ? &*mask : nullptr;
                const an::ImageLayout planeLayout = access->view().layout();
                if (an::Status extracted =
                        extractInto(scope.execution(), *plan, scoped, access->view().data(), planeLayout);
                    extracted.failed())
                {
                    return extracted;
                }
            }
            if (an::Status recorded = record(*made, effectOf(false, false), "an::extractChannelScope",
                                             an::channelScopeName(output.scope));
                recorded.failed())
            {
                return recorded;
            }
            return an::consumer::AnyImage(std::move(*made));
        },
        source.image());
    if (!plane.hasValue())
    {
        return plane.status();
    }
    std::optional<an::ValidityMask> planeMask;
    if (source.validity() != nullptr)
    {
        an::Result<an::ValidityMask> narrowed =
            maskPlaneOf(scope.engine(), described, *source.validity(), an::channelScopeComponent(output.scope));
        if (!narrowed.hasValue())
        {
            return narrowed.status();
        }
        planeMask.emplace(std::move(*narrowed));
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*plane), std::move(planeMask));
}

/// What a colour component keeps of its image: the acquisition record, the solution and the lineage, a meaning of its
/// own, and none of the cards.
[[nodiscard]] an::Result<an::ImageSemantics> componentSemanticsOf(const an::ui::ProcessSource& source)
{
    return an::ImageSemantics::create(
        an::ImageSemanticsRequest{.validity = componentRule(source.metadata().semantics().validity())});
}

/// Colour components of an image, each in a grey plane of its own.
[[nodiscard]] an::Result<std::vector<an::consumer::ImageAsset>>
componentsOf(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
             const an::ImageDescriptor& described, std::span<const ChannelActionOutput> outputs)
{
    an::Result<ComponentSource> read = componentSourceOf(scope, source, described);
    if (!read.hasValue())
    {
        return read.status();
    }
    read->scoped.descriptor = &read->described;
    read->scoped.layout = &read->layout;
    if (read->mask.has_value())
    {
        read->scoped.validity.mask = &*read->mask;
    }
    const an::Result<an::ImageSemantics> semantics = componentSemanticsOf(source);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    std::vector<an::consumer::ImageAsset> made;
    for (const ChannelActionOutput& output : outputs)
    {
        const an::Result<an::ChannelScopePlan> plan =
            an::planChannelScope(an::ui::processChannelScopeRequest(output.scope, read->described));
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const an::Result<an::ImageDescriptor> planeDescribed = an::channelScopePlaneDescriptor(*plan, read->described);
        if (!planeDescribed.hasValue())
        {
            return planeDescribed.status();
        }
        an::Result<an::consumer::AnyImage> plane = std::visit(
            [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
                an::Result<an::consumer::AnyImage> written = refusal(an::ErrorCode::Internal, "no plane was made");
                (void)an::dispatchSampleType(planeDescribed->sampleType(), [&]<typename PlaneT>() {
                    if constexpr (std::is_floating_point_v<PlaneT>)
                    {
                        an::Result<an::Image<PlaneT>> image = newPlane<PlaneT>(scope.engine(), *planeDescribed, typed);
                        if (!image.hasValue())
                        {
                            written = image.status();
                            return;
                        }
                        {
                            std::optional<an::ImageWriteAccess<PlaneT>> access = image->write();
                            if (!access.has_value())
                            {
                                written = refusal(an::ErrorCode::NotInitialized, "the plane holds nothing");
                                return;
                            }
                            const an::ImageLayout planeLayout = access->view().layout();
                            if (an::Status extracted = extractInto(scope.execution(), *plan, read->scoped,
                                                                   access->view().data(), planeLayout);
                                extracted.failed())
                            {
                                written = extracted;
                                return;
                            }
                        }
                        if (an::Status recorded = record(*image, effectOf(true, true), "an::extractChannelScope",
                                                         an::channelScopeName(output.scope), *semantics);
                            recorded.failed())
                        {
                            written = recorded;
                            return;
                        }
                        written = an::consumer::AnyImage(std::move(*image));
                    }
                });
                return written;
            },
            source.image());
        if (!plane.hasValue())
        {
            return plane.status();
        }
        an::Result<an::consumer::ImageAsset> adopted =
            an::consumer::ImageAsset::adopt(scope.session(), std::move(*plane));
        if (!adopted.hasValue())
        {
            return adopted.status();
        }
        made.push_back(std::move(*adopted));
    }
    return made;
}

/// The a*b* view: a* as red, b* as green and a cleared blue, in one colour image of the components' representation.
[[nodiscard]] an::Result<an::consumer::ImageAsset> chromaViewOf(const an::ui::ExecutionScope& scope,
                                                                const an::ui::ProcessSource& source,
                                                                const an::ImageDescriptor& described,
                                                                const ChannelAction& action)
{
    an::Result<ComponentSource> read = componentSourceOf(scope, source, described);
    if (!read.hasValue())
    {
        return read.status();
    }
    read->scoped.descriptor = &read->described;
    read->scoped.layout = &read->layout;
    if (read->mask.has_value())
    {
        read->scoped.validity.mask = &*read->mask;
    }
    const an::Result<an::ImageSemantics> semantics = componentSemanticsOf(source);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const an::Result<an::ChannelScopePlan> firstPlan =
        an::planChannelScope(an::ui::processChannelScopeRequest(action.outputs[0].scope, read->described));
    if (!firstPlan.hasValue())
    {
        return firstPlan.status();
    }
    const an::Result<an::ImageDescriptor> planeDescribed = an::channelScopePlaneDescriptor(*firstPlan, read->described);
    if (!planeDescribed.hasValue())
    {
        return planeDescribed.status();
    }
    an::ImageDescriptorRequest request = planeDescribed->toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(described.extent().width(), described.extent().height(), 3u, scope.engine().limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the a*b* view cannot be described");
    }
    request.extent = *extent;
    request.channelSchema = an::ChannelSchema::Rgb;
    const std::optional<an::ImageDescriptor> viewDescribed = an::ImageDescriptor::create(request);
    if (!viewDescribed.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the a*b* view cannot be described");
    }
    an::Result<an::consumer::AnyImage> made = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            an::Result<an::consumer::AnyImage> written = refusal(an::ErrorCode::Internal, "no view was made");
            (void)an::dispatchSampleType(viewDescribed->sampleType(), [&]<typename PlaneT>() {
                if constexpr (std::is_floating_point_v<PlaneT>)
                {
                    // Zeroed, so the blue plane nothing writes is the view's cleared L*.
                    an::Result<an::Image<PlaneT>> image = newPlane<PlaneT>(scope.engine(), *viewDescribed, typed);
                    if (!image.hasValue())
                    {
                        written = image.status();
                        return;
                    }
                    {
                        std::optional<an::ImageWriteAccess<PlaneT>> access = image->write();
                        if (!access.has_value())
                        {
                            written = refusal(an::ErrorCode::NotInitialized, "the view holds nothing");
                            return;
                        }
                        const an::Result<an::ImageLayout> planeLayout =
                            planeLayoutOf(access->view().layout(), scope.engine().limits());
                        if (!planeLayout.hasValue())
                        {
                            written = planeLayout.status();
                            return;
                        }
                        auto* bytes = reinterpret_cast<std::byte*>(access->view().data());
                        for (std::uint32_t channel = 0u; channel < 2u; ++channel)
                        {
                            const an::Result<an::ChannelScopePlan> plan = an::planChannelScope(
                                an::ui::processChannelScopeRequest(action.outputs[channel].scope, read->described));
                            if (!plan.hasValue())
                            {
                                written = plan.status();
                                return;
                            }
                            if (an::Status extracted =
                                    extractInto(scope.execution(), *plan, read->scoped,
                                                planeStart(bytes, access->view().layout(), channel), *planeLayout);
                                extracted.failed())
                            {
                                written = extracted;
                                return;
                            }
                        }
                    }
                    if (an::Status recorded =
                            record(*image, effectOf(true, true), "an::extractChannelScope", "CieA, CieB", *semantics);
                        recorded.failed())
                    {
                        written = recorded;
                        return;
                    }
                    written = an::consumer::AnyImage(std::move(*image));
                }
            });
            return written;
        },
        source.image());
    if (!made.hasValue())
    {
        return made.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*made));
}

// ---- Validity a new image states.

/// What a new image states about which of its samples mean something, beside its samples: the meaning it is recorded
/// with when that changes, and the mask it carries.
struct StatedValidity
{
    std::optional<an::ImageSemantics> semantics;
    std::optional<an::ValidityMask> mask;
};

/// The validity an image keeps when a value rule of its own decides it on its samples: the rule's mask, one plane that
/// calls a pixel valid only where every channel is, under an explicit-mask meaning.
[[nodiscard]] an::Result<StatedValidity> ruleAsMask(const an::EngineContext& engine,
                                                    const an::ui::ProcessSource& source)
{
    an::Result<an::ValidityMask> decided =
        sharedMaskOfRule(engine, source.image(), source.metadata().semantics().validity());
    if (!decided.hasValue())
    {
        return decided.status();
    }
    an::Result<an::ImageSemantics> semantics = withValidity(source.metadata().semantics(), explicitMaskRule());
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    return StatedValidity{.semantics = std::move(*semantics), .mask = std::move(*decided)};
}

/// A copy of an image's explicit mask, as it is.
[[nodiscard]] an::Result<StatedValidity> maskCopy(const an::ui::ProcessSource& source)
{
    StatedValidity stated;
    if (source.validity() != nullptr)
    {
        an::Result<an::ValidityMask> copied = source.validity()->clone();
        if (!copied.hasValue())
        {
            return copied.status();
        }
        stated.mask.emplace(std::move(*copied));
    }
    return stated;
}

// ---- Planar samples.

/// The samples of an image laid out a plane per channel: the image's own when it is, and a planar copy otherwise.
template <typename SampleT> struct PlanarSamples
{
    std::optional<an::Image<SampleT>> copy;
    const SampleT* data = nullptr;
    std::optional<an::ImageLayout> layout;
};

template <typename SampleT>
[[nodiscard]] an::Result<PlanarSamples<SampleT>> planarSamplesOf(const an::ui::ExecutionScope& scope,
                                                                 const an::Image<SampleT>& image,
                                                                 const an::ImageDescriptor& described)
{
    const auto view = image.view();
    if (!view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    PlanarSamples<SampleT> planar;
    if (view->layout().samplesPerPixel() == 1u)
    {
        planar.data = view->data();
        planar.layout = view->layout();
        return planar;
    }
    an::Result<an::Image<SampleT>> copy = an::Image<SampleT>::createDestination(
        scope.engine(), described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = copy->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the planar copy holds nothing");
        }
        if (an::Status repacked =
                an::repackImageSamples(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout());
            repacked.failed())
        {
            return repacked;
        }
    }
    planar.copy.emplace(std::move(*copy));
    const auto copied = planar.copy->view();
    if (!copied.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the planar copy holds nothing");
    }
    planar.data = copied->data();
    planar.layout = copied->layout();
    return planar;
}

// ---- Conversion.

/// What a conversion does to a sample, for the lineage.
[[nodiscard]] std::string conversionDetail(an::SampleType from, an::SampleType to)
{
    return std::string(an::sampleTypeName(from)) + " to " + an::sampleTypeName(to) +
           ", as fractions of each representation's range, to the nearest code";
}

/// Whether a value rule still says what it said once the samples are converted: exact zero stays zero unless a signed
/// representation, counted from its smallest value, is on either side; a stated range is in the source's values.
[[nodiscard]] bool ruleSurvivesConversion(const an::ValidityDescriptor& rule, an::SampleType from,
                                          an::SampleType to) noexcept
{
    switch (rule.rule())
    {
    case an::ValidityRule::ZeroIsNoData:
        return !isSignedInteger(from) && !isSignedInteger(to);
    case an::ValidityRule::DomainRange:
        return false;
    default:
        return true;
    }
}

/// The validity a converted image states: the image's rule and its explicit mask as they are, and a value rule the
/// conversion moves carried as the mask it decides on the image's own samples.
[[nodiscard]] an::Result<StatedValidity> convertedValidity(const an::EngineContext& engine,
                                                           const an::ui::ProcessSource& source, an::SampleType from,
                                                           an::SampleType to)
{
    if (source.validity() == nullptr && !ruleSurvivesConversion(source.metadata().semantics().validity(), from, to))
    {
        return ruleAsMask(engine, source);
    }
    return maskCopy(source);
}

/// The image in another representation, through Float64 fractions of each representation's range.
template <typename SourceT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
convertTyped(const an::ui::ExecutionScope& scope, const an::Image<SourceT>& image, const an::ImageDescriptor& described,
             an::SampleType target, const std::optional<an::ImageSemantics>& semantics)
{
    an::Result<PlanarSamples<SourceT>> planar = planarSamplesOf(scope, image, described);
    if (!planar.hasValue())
    {
        return planar.status();
    }
    const an::Result<an::ImageDescriptor> targetDescribed =
        restatedDescriptor(described, target, std::nullopt, scope.engine().limits(), true);
    const an::Result<an::ImageDescriptor> workingDescribed =
        restatedDescriptor(described, an::SampleType::Float64, std::nullopt, scope.engine().limits(), true);
    if (!targetDescribed.hasValue() || !workingDescribed.hasValue())
    {
        return targetDescribed.hasValue() ? workingDescribed.status() : targetDescribed.status();
    }
    an::Result<an::Image<double>> working =
        an::Image<double>::createDestination(scope.engine(), *workingDescribed, an::StorageArrangement::Planar,
                                             an::StorageInitialization::Uninitialized, image);
    if (!working.hasValue())
    {
        return working.status();
    }
    std::optional<an::ImageWriteAccess<double>> workingAccess = working->write();
    if (!workingAccess.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the working image holds nothing");
    }
    const an::ImageView<double>& fractions = workingAccess->view();
    an::ConversionCounters counters;
    const std::optional<std::pair<double, double>> sourceRange = integerRange(an::kSampleTypeOf<SourceT>);
    const bool signedSource = sourceRange.has_value() && sourceRange->first < 0.0;
    // An unsigned integer is a fraction of its largest code and a floating sample is its own; a signed one is counted
    // from its smallest code, so it is carried across as the number it is and moved into place.
    an::ConversionPolicy toFractions;
    toFractions.domain = signedSource ? an::DomainMapping::CodeValue : an::DomainMapping::FullScale;
    if (an::Status read = an::convertImageSamples(scope.execution(), an::kSampleTypeOf<SourceT>, planar->data,
                                                  *planar->layout, an::SampleType::Float64, fractions.data(),
                                                  fractions.layout(), toFractions, counters);
        read.failed())
    {
        return read;
    }
    const std::optional<std::pair<double, double>> targetRange = integerRange(target);
    const bool signedTarget = targetRange.has_value() && targetRange->first < 0.0;
    // One affine step takes the samples from where they are to the target's codes: a signed source's codes to
    // fractions, and fractions to a signed target's codes.
    double scale = 1.0;
    double bias = 0.0;
    if (signedSource)
    {
        const double span = sourceRange->second - sourceRange->first;
        scale = 1.0 / span;
        bias = -sourceRange->first / span;
    }
    if (signedTarget)
    {
        const double span = targetRange->second - targetRange->first;
        scale *= span;
        bias = bias * span + targetRange->first;
    }
    if (signedSource || signedTarget)
    {
        if (an::Status moved = scaleAndBiasInPlace(scope.execution(), an::SampleType::Float64, fractions.data(),
                                                   fractions.layout(), scale, bias);
            moved.failed())
        {
            return moved;
        }
    }
    an::Result<an::consumer::AnyImage> converted = refusal(an::ErrorCode::Internal, "no representation was converted");
    (void)an::dispatchSampleType(target, [&]<typename TargetT>() {
        an::Result<an::Image<TargetT>> made =
            an::Image<TargetT>::createDestination(scope.engine(), *targetDescribed, an::StorageArrangement::Planar,
                                                  an::StorageInitialization::Uninitialized, image);
        if (!made.hasValue())
        {
            converted = made.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<TargetT>> access = made->write();
            if (!access.has_value())
            {
                converted = refusal(an::ErrorCode::NotInitialized, "the converted image holds nothing");
                return;
            }
            an::ConversionPolicy toTarget;
            toTarget.domain = signedTarget ? an::DomainMapping::CodeValue : an::DomainMapping::FullScale;
            if (an::Status written = an::convertImageSamples(
                    scope.execution(), an::SampleType::Float64, fractions.data(), fractions.layout(),
                    an::kSampleTypeOf<TargetT>, access->view().data(), access->view().layout(), toTarget, counters);
                written.failed())
            {
                converted = written;
                return;
            }
        }
        if (an::Status recorded =
                record(*made, effectOf(semantics.has_value(), false), "an::convertImageSamples",
                       conversionDetail(an::kSampleTypeOf<SourceT>, an::kSampleTypeOf<TargetT>), semantics);
            recorded.failed())
        {
            converted = recorded;
            return;
        }
        converted = an::consumer::AnyImage(std::move(*made));
    });
    return converted;
}

/// The image in another representation, with its validity.
[[nodiscard]] an::Result<an::consumer::ImageAsset> convertedImageOf(const an::ui::ExecutionScope& scope,
                                                                    const an::ui::ProcessSource& source,
                                                                    const an::ImageDescriptor& described,
                                                                    an::SampleType target)
{
    an::Result<StatedValidity> validity = convertedValidity(scope.engine(), source, described.sampleType(), target);
    if (!validity.hasValue())
    {
        return validity.status();
    }
    an::Result<an::consumer::AnyImage> converted = std::visit(
        [&](const auto& typed) { return convertTyped(scope, typed, described, target, validity->semantics); },
        source.image());
    if (!converted.hasValue())
    {
        return converted.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*converted), std::move(validity->mask));
}

// ---- Grey and colour.

/// The precision and output contract a program over samples of a representation computes at: Float64 for the samples
/// Float32 cannot hold - doubles, and thirty-two-bit codes - and Float32 otherwise.
struct CombinePrecision
{
    an::ComputePrecision precision = an::ComputePrecision::Float32;
    std::optional<an::Float64Justification> justification;
    an::OutputPrecisionContract contract = an::OutputPrecisionContract::NormalizedOrLossy;
};

[[nodiscard]] CombinePrecision combinePrecisionFor(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Float64:
        return {.precision = an::ComputePrecision::Float64,
                .justification = an::Float64Justification::StoredInFloat64,
                .contract = an::OutputPrecisionContract::NormalizedOrLossy};
    case an::SampleType::UInt32:
    case an::SampleType::Int32:
        return {.precision = an::ComputePrecision::Float64,
                .justification = an::Float64Justification::ExactIntegerCodeValues,
                .contract = an::OutputPrecisionContract::ExactStoredValues};
    default:
        break;
    }
    return {};
}

/// The grey plane of a colour image in its own representation.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
greyTyped(const an::ui::ExecutionScope& scope, const an::Image<SampleT>& image, const an::ImageDescriptor& described,
          const an::ImageDescriptor& greyDescribed, const std::optional<an::ImageSemantics>& semantics)
{
    an::Result<PlanarSamples<SampleT>> colour = planarSamplesOf(scope, image, described);
    if (!colour.hasValue())
    {
        return colour.status();
    }
    const an::Result<an::ImageLayout> colourPlane = planeLayoutOf(*colour->layout, scope.engine().limits());
    if (!colourPlane.hasValue())
    {
        return colourPlane.status();
    }
    an::Result<an::Image<SampleT>> grey = an::Image<SampleT>::createDestination(
        scope.engine(), greyDescribed, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
    if (!grey.hasValue())
    {
        return grey.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = grey->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the grey image holds nothing");
        }
        const CombinePrecision precision = combinePrecisionFor(an::kSampleTypeOf<SampleT>);
        an::PointwiseProgramBuilder builder;
        builder.channels(1u)
            .sources(3u)
            .readingSamples(an::kSampleTypeOf<SampleT>)
            .readingDomain(described.numericDomain())
            .promising(precision.contract)
            .computingIn(precision.precision, precision.justification)
            .append(an::pointwiseScale(0.2989))
            .append(an::pointwiseAdd(1u, 0.5870))
            .append(an::pointwiseAdd(2u, 0.1140));
        const an::Result<an::PointwiseProgram> program = builder.build();
        if (!program.hasValue())
        {
            return program.status();
        }
        const auto* bytes = reinterpret_cast<const std::byte*>(colour->data);
        const std::array<an::PointwiseImageView, 3u> sources{
            an::PointwiseImageView{.samples = planeStart(bytes, *colour->layout, 0u), .layout = &*colourPlane},
            an::PointwiseImageView{.samples = planeStart(bytes, *colour->layout, 1u), .layout = &*colourPlane},
            an::PointwiseImageView{.samples = planeStart(bytes, *colour->layout, 2u), .layout = &*colourPlane}};
        const an::ImageLayout greyLayout = access->view().layout();
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            scope.execution(), *program, sources,
            an::PointwiseMutableImageView{.samples = access->view().data(), .layout = &greyLayout},
            an::KernelValidityView{});
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    if (an::Status recorded = record(*grey, effectOf(semantics.has_value(), false), "an::executePointwiseProgram",
                                     "0.2989 R + 0.5870 G + 0.1140 B", semantics);
        recorded.failed())
    {
        return recorded;
    }
    return an::consumer::AnyImage(std::move(*grey));
}

/// The colour image of a grey one in its own representation: its plane in each of three channels.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage> colourTyped(const an::ui::ExecutionScope& scope,
                                                             const an::Image<SampleT>& image,
                                                             const an::ImageDescriptor& colourDescribed)
{
    const auto view = image.view();
    if (!view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    an::Result<an::Image<SampleT>> colour =
        an::Image<SampleT>::createDestination(scope.engine(), colourDescribed, an::StorageArrangement::Planar,
                                              an::StorageInitialization::Uninitialized, image);
    if (!colour.hasValue())
    {
        return colour.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = colour->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the colour image holds nothing");
        }
        const an::Result<an::ImageLayout> plane = planeLayoutOf(access->view().layout(), scope.engine().limits());
        if (!plane.hasValue())
        {
            return plane.status();
        }
        auto* bytes = reinterpret_cast<std::byte*>(access->view().data());
        an::ConversionCounters counters;
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (an::Status copied = an::convertImageSamples(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(),
                                                            view->layout(), an::kSampleTypeOf<SampleT>,
                                                            planeStart(bytes, access->view().layout(), channel), *plane,
                                                            an::ConversionPolicy{}, counters);
                copied.failed())
            {
                return copied;
            }
        }
    }
    if (an::Status recorded = record(*colour, effectOf(false, false), "an::convertImageSamples",
                                     "the grey plane copied into red, green and blue");
        recorded.failed())
    {
        return recorded;
    }
    return an::consumer::AnyImage(std::move(*colour));
}

// ---- The negative.

/// The negative of a run's destination in place, in fractions of full scale and under the source's validity.
[[nodiscard]] an::Status negateInPlace(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                                       an::ui::ProcessDestination& destination)
{
    an::Result<NormalizedSamples> samples = openNormalizedDestination(scope, source, destination);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    const an::Result<an::PointwiseProgram> program = an::buildScaleBiasProgram(
        normalizedPlanShape(samples->sampleType(), samples->layout().extent().channelCount()), -1.0, 1.0);
    if (!program.hasValue())
    {
        return program.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = samples->samples(), .layout = &samples->layout()}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        scope.execution(), *program, sources,
        an::PointwiseMutableImageView{.samples = samples->samples(), .layout = &samples->layout()},
        samples->validity());
    if (!ran.hasValue())
    {
        return ran.status();
    }
    return samples->commit(scope.execution());
}

// ---- Turns and mirrors.

/// What the view's turn or mirror does to an image: the samples move, so a solution no longer describes them and the
/// cards that located them no longer do; an output decision is withdrawn; the validity rule stays.
constexpr an::OperationEffect kOrientationEffect{.description = an::DescriptionEffect::Restates,
                                                 .solution = an::SolutionEffect::Invalidates,
                                                 .codecMetadata = an::CodecMetadataEffect::Drops,
                                                 .outputIntent = an::OutputIntentEffect::Withdraws,
                                                 .validity = {.policy = an::ValidityPolicy::Preserve}};

/// The name a turn or mirror is recorded under in an image's lineage.
[[nodiscard]] const char* orientationLineage(an::ImageOrientation orientation) noexcept
{
    switch (orientation)
    {
    case an::ImageOrientation::MirrorHorizontal:
        return "horizontal mirror";
    case an::ImageOrientation::MirrorVertical:
        return "vertical mirror";
    case an::ImageOrientation::Rotate180:
        return "half turn";
    case an::ImageOrientation::Rotate90Clockwise:
        return "quarter turn right";
    case an::ImageOrientation::Rotate270Clockwise:
        return "quarter turn left";
    default:
        break;
    }
    return "orientation";
}

template <typename SampleT>
[[nodiscard]] an::Result<std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>>
orientTyped(const an::ui::ExecutionScope& scope, const an::Image<SampleT>& image, const an::ValidityMask* validity,
            an::ImageOrientation orientation)
{
    using Oriented = std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>;
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::Result<an::GeometryPlan> plan = an::planOrientation(*stored, orientation);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<SampleT>> result =
        an::Image<SampleT>::createDestination(scope.engine(), plan->destinationDescriptor(),
                                              an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, image);
    if (!result.hasValue())
    {
        return result.status();
    }
    an::Result<an::ValidityMask> written =
        an::ValidityMask::create(scope.engine(), plan->destinationDescriptor().extent());
    if (!written.hasValue())
    {
        return written.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
    if (!writtenView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the turned image's mask holds nothing");
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask = maskViewOf(validity);
    an::GeometrySelection selection;
    selection.validity = image.metadata().semantics().validity();
    selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = result->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the turned image holds nothing");
        }
        const an::Result<an::GeometryResult> ran =
            an::transformImageGeometry(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout(), *plan, selection, *writtenView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::operationTransition(kOrientationEffect, an::OperationOutcome{.operation = "an::transformImageGeometry",
                                                                         .detail = orientationLineage(orientation)});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = result->transition(*change); stated.failed())
    {
        return stated;
    }
    std::optional<an::ValidityMask> turnedMask;
    if (validity != nullptr)
    {
        turnedMask.emplace(std::move(*written));
    }
    return Oriented{an::consumer::AnyImage(std::move(*result)), std::move(turnedMask)};
}

} // namespace

std::span<const ChannelAction> channelActions() noexcept
{
    return kActions;
}

const ChannelAction* channelActionNamed(std::string_view action) noexcept
{
    const auto found = std::find_if(kActions.begin(), kActions.end(),
                                    [action](const ChannelAction& row) { return action == row.action; });
    return found == kActions.end() ? nullptr : &*found;
}

const ChannelAction* channelActionOfMember(std::string_view member) noexcept
{
    const auto found = std::find_if(kActions.begin(), kActions.end(),
                                    [member](const ChannelAction& row) { return member == row.member; });
    return found == kActions.end() ? nullptr : &*found;
}

an::Result<std::vector<an::consumer::ImageAsset>>
runChannelAction(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, const ChannelAction& action)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    std::vector<an::consumer::ImageAsset> made;
    if (action.kind == ChannelActionKind::Convert)
    {
        an::Result<an::consumer::ImageAsset> converted = convertedImageOf(scope, source, *described, action.target);
        if (!converted.hasValue())
        {
            return converted.status();
        }
        made.push_back(std::move(*converted));
        return made;
    }
    if (!describesColourImage(*described))
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the image is not a colour image: it has no red, green and blue to extract from");
    }
    if (action.kind == ChannelActionKind::ChromaView)
    {
        an::Result<an::consumer::ImageAsset> view = chromaViewOf(scope, source, *described, action);
        if (!view.hasValue())
        {
            return view.status();
        }
        made.push_back(std::move(*view));
        return made;
    }
    const std::span<const ChannelActionOutput> outputs(action.outputs.data(), action.outputCount);
    const bool stored = std::all_of(outputs.begin(), outputs.end(), [](const ChannelActionOutput& output) {
        return an::channelScopeEncoding(output.scope) == an::ChannelSchema::Rgb;
    });
    if (!stored)
    {
        return componentsOf(scope, source, *described, outputs);
    }
    for (const ChannelActionOutput& output : outputs)
    {
        an::Result<an::consumer::ImageAsset> plane = storedChannelOf(scope, source, *described, output);
        if (!plane.hasValue())
        {
            return plane.status();
        }
        made.push_back(std::move(*plane));
    }
    return made;
}

an::Result<an::consumer::ImageAsset> greyImageOf(const an::ui::ExecutionScope& scope,
                                                 const an::ui::ProcessSource& source)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (!describesColourImage(*described))
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the image is not a colour image: it has no red, green and blue to make grey");
    }
    const an::Result<an::ImageDescriptor> greyDescribed =
        restatedDescriptor(*described, described->sampleType(), 1u, scope.engine().limits(), false);
    if (!greyDescribed.hasValue())
    {
        return greyDescribed.status();
    }
    // One plane of validity for one channel: an explicit mask's planes meet, and a value rule of each channel's values
    // is decided on the colour image's samples into the mask of the pixels where every channel is valid. The grey keeps
    // a rule that says the same of it: every pixel valid, or every finite one, since a non-finite channel makes its
    // grey non-finite. A mask of every pixel would send each process run over the grey down the pixel-by-pixel path.
    StatedValidity validity;
    const an::ValidityDescriptor& rule = source.metadata().semantics().validity();
    if (source.validity() != nullptr)
    {
        an::Result<an::ValidityMask> shared = intersectionOfPlanes(scope.engine(), *described, *source.validity());
        if (!shared.hasValue())
        {
            return shared.status();
        }
        validity.mask.emplace(std::move(*shared));
    }
    else if (an::validityRuleIsDecidedByValue(rule.rule()) && rule.rule() != an::ValidityRule::FiniteOnly &&
             rule.rule() != an::ValidityRule::AllValid)
    {
        an::Result<StatedValidity> decided = ruleAsMask(scope.engine(), source);
        if (!decided.hasValue())
        {
            return decided.status();
        }
        validity = std::move(*decided);
    }
    an::Result<an::consumer::AnyImage> grey = std::visit(
        [&](const auto& typed) { return greyTyped(scope, typed, *described, *greyDescribed, validity.semantics); },
        source.image());
    if (!grey.hasValue())
    {
        return grey.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*grey), std::move(validity.mask));
}

an::Result<an::consumer::ImageAsset> colourImageOf(const an::ui::ExecutionScope& scope,
                                                   const an::ui::ProcessSource& source)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (!isGreyImage(*described))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image is not one grey channel to make colour of");
    }
    const an::Result<an::ImageDescriptor> colourDescribed =
        restatedDescriptor(*described, described->sampleType(), 3u, scope.engine().limits(), false);
    if (!colourDescribed.hasValue())
    {
        return colourDescribed.status();
    }
    an::Result<StatedValidity> validity = maskCopy(source);
    if (!validity.hasValue())
    {
        return validity.status();
    }
    an::Result<an::consumer::AnyImage> colour =
        std::visit([&](const auto& typed) { return colourTyped(scope, typed, *colourDescribed); }, source.image());
    if (!colour.hasValue())
    {
        return colour.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*colour), std::move(validity->mask));
}

an::Result<an::consumer::ImageAsset> negativeImageOf(const an::ui::ExecutionScope& scope,
                                                     const an::ui::ProcessSource& source,
                                                     const an::ui::ProcessBlendRequest& blend)
{
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(scope.session(), scope.engine(), scope.execution(), source);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    if (an::Status negated = negateInPlace(scope, source, *destination); negated.failed())
    {
        return negated;
    }
    if (an::Status blended = an::ui::blendProcessResult(scope.engine(), scope.execution(), source, *destination, blend);
        blended.failed())
    {
        return blended;
    }
    if (an::Status recorded = recordNonlinearResult(
            *destination,
            NonlinearResultRecord{.stateOperation = "negative-transform",
                                  .engineOperation = "an::executePointwiseProgram",
                                  .detail = "one minus every sample, as a fraction of its representation's range"});
        recorded.failed())
    {
        return recorded;
    }
    return destination->release();
}

an::Result<an::consumer::ImageAsset> orientedImageOf(const an::ui::ExecutionScope& scope,
                                                     const an::ui::ProcessSource& source,
                                                     an::ImageOrientation orientation)
{
    if (!an::isImageOrientation(orientation))
    {
        return refusal(an::ErrorCode::InvalidArgument, "no orientation is named");
    }
    an::Result<std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>> oriented = std::visit(
        [&](const auto& typed) { return orientTyped(scope, typed, source.validity(), orientation); }, source.image());
    if (!oriented.hasValue())
    {
        return oriented.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(oriented->first), std::move(oriented->second));
}

} // namespace process_utils
