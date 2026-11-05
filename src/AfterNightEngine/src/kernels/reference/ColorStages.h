// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour conversion's stage arithmetic for walks that convert every pixel themselves.
///
/// `Algorithms/Color/ColorTransform.h` converts an image or a pixel by walking a plan's stages and reading the plan's
/// encodings, matrices and white points at every stage of every pixel. A walk that converts each pixel of a chunk as
/// one step of a larger composition - a blend mode, an LRGB combination - reads them once instead: a plan is resolved
/// into its stages and constants, and each stage is computed here with the colour conversion's own expressions in its
/// own order, which is what makes a pixel converted here the pixel `convertColorPixel` converts. A plan holding a
/// stage this file does not restate is not resolved. The suites of the walks that use it hold them to the colour
/// conversion's answer, bit for bit.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <optional>
#include <span>

/// The colour conversion's stages, restated for the compositions that convert each pixel themselves.
namespace an::color_stage_detail
{

/// Whether a value is finite, read from its exponent; the answer `std::isfinite` gives, without the library call the
/// compiler makes for it under precise floating point.
///
/// @param value the value.
/// @return true unless the value is an infinity or not a number.
[[nodiscard]] inline bool isFiniteValue(double value) noexcept
{
    return (std::bit_cast<std::uint64_t>(value) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

/// `std::copysign` written out on the bits, the answer it gives for every input.
///
/// @param magnitude the value whose magnitude is kept.
/// @param sign the value whose sign is taken.
/// @return the magnitude with the sign.
[[nodiscard]] inline double copySign(double magnitude, double sign) noexcept
{
    constexpr std::uint64_t kSignBit = 0x8000000000000000ull;
    return std::bit_cast<double>((std::bit_cast<std::uint64_t>(magnitude) & ~kSignBit) |
                                 (std::bit_cast<std::uint64_t>(sign) & kSignBit));
}

/// The CIE lightness helper's linear-segment bound.
inline constexpr double kLabDelta = 6.0 / 29.0;

/// The linear value a stated transfer function gives an encoded one; the magnitude goes through the curve and the
/// sign is put back.
///
/// @param transfer which curve.
/// @param gamma the pure power law's exponent, read only by `PureGamma`.
/// @param value the encoded value.
/// @return the linear value.
[[nodiscard]] inline double decodeTransfer(ColorTransferFunction transfer, double gamma, double value) noexcept
{
    if (transfer == ColorTransferFunction::Linear)
    {
        return value;
    }
    const double magnitude = std::abs(value);
    double linear = magnitude;
    switch (transfer)
    {
    case ColorTransferFunction::Srgb:
        linear = magnitude <= 0.04045 ? magnitude / 12.92 : std::pow((magnitude + 0.055) / 1.055, 2.4);
        break;
    case ColorTransferFunction::Rec709:
        linear = magnitude < 0.081 ? magnitude / 4.5 : std::pow((magnitude + 0.099) / 1.099, 1.0 / 0.45);
        break;
    case ColorTransferFunction::PureGamma:
        linear = std::pow(magnitude, gamma);
        break;
    case ColorTransferFunction::Linear:
        break;
    }
    return copySign(linear, value);
}

/// The encoded value a stated transfer function gives a linear one.
///
/// @param transfer which curve.
/// @param gamma the pure power law's exponent, read only by `PureGamma`.
/// @param value the linear value.
/// @return the encoded value.
[[nodiscard]] inline double encodeTransfer(ColorTransferFunction transfer, double gamma, double value) noexcept
{
    if (transfer == ColorTransferFunction::Linear)
    {
        return value;
    }
    const double magnitude = std::abs(value);
    double encoded = magnitude;
    switch (transfer)
    {
    case ColorTransferFunction::Srgb:
        encoded = magnitude <= 0.0031308 ? 12.92 * magnitude : 1.055 * std::pow(magnitude, 1.0 / 2.4) - 0.055;
        break;
    case ColorTransferFunction::Rec709:
        encoded = magnitude < 0.018 ? 4.5 * magnitude : 1.099 * std::pow(magnitude, 0.45) - 0.099;
        break;
    case ColorTransferFunction::PureGamma:
        encoded = std::pow(magnitude, 1.0 / gamma);
        break;
    case ColorTransferFunction::Linear:
        break;
    }
    return copySign(encoded, value);
}

/// What a clamp to the unit interval makes of a linear value's encoded form, when that is certain without the curve.
///
/// Every curve keeps a negative value negative, so its clamp is zero; the margin below zero keeps the encoded value
/// clear of a Float32 underflow to minus zero, which a clamp keeps. From 1.01 up the sRGB, Rec. 709 and linear curves
/// are above one by more than any rounding, so the clamp is one. A walk that clamps a recomposed colour skips the
/// curve's power for an out-of-gamut channel this way and writes what the curve and the clamp would have.
///
/// @param transfer the curve the value would be encoded by.
/// @param linear the linear value.
/// @return zero or one when the clamp decides the answer by itself; nothing when the curve is needed.
[[nodiscard]] inline std::optional<double> clampedEncodingWithoutCurve(ColorTransferFunction transfer,
                                                                       double linear) noexcept
{
    if (linear < -1.0e-30)
    {
        return 0.0;
    }
    if (linear >= 1.01 && transfer != ColorTransferFunction::PureGamma)
    {
        return 1.0;
    }
    return std::nullopt;
}

/// The perceptual compression the Lab transform applies to a normalized tristimulus ratio.
///
/// @param ratio the tristimulus value divided by the white's.
/// @return the compressed value.
[[nodiscard]] inline double labForward(double ratio) noexcept
{
    return ratio > kLabDelta * kLabDelta * kLabDelta ? std::pow(ratio, 1.0 / 3.0)
                                                     : ratio / (3.0 * kLabDelta * kLabDelta) + 4.0 / 29.0;
}

/// The inverse of `labForward`.
///
/// @param compressed the compressed value.
/// @return the tristimulus ratio.
[[nodiscard]] inline double labInverse(double compressed) noexcept
{
    return compressed > kLabDelta ? compressed * compressed * compressed
                                  : 3.0 * kLabDelta * kLabDelta * (compressed - 4.0 / 29.0);
}

/// A hue brought into one turn; a hue that is not a number is returned unchanged.
///
/// @param hue a hue in turns.
/// @return the same hue in `[0, 1)`.
[[nodiscard]] inline double wrapTurn(double hue) noexcept
{
    if (!isFiniteValue(hue))
    {
        return hue;
    }
    const double wrapped = hue - std::floor(hue);
    return wrapped >= 1.0 ? 0.0 : wrapped;
}

/// A matrix applied to three channels, each row summed left to right.
///
/// @param matrix the coefficients, row by row.
/// @param channels the three input channels.
/// @return the three output channels.
[[nodiscard]] inline std::array<double, 3u> applyMatrix(const ColorMatrix3& matrix,
                                                        const std::array<double, 3u>& channels) noexcept
{
    const std::array<double, 9u>& m = matrix.coefficients;
    return std::array<double, 3u>{m[0] * channels[0] + m[1] * channels[1] + m[2] * channels[2],
                                  m[3] * channels[0] + m[4] * channels[1] + m[5] * channels[2],
                                  m[6] * channels[0] + m[7] * channels[1] + m[8] * channels[2]};
}

/// The fixed stage sequences the compositions' conversions take for the common encodings, which a walk runs without
/// choosing each stage per pixel; `General` runs whatever the plan holds.
enum class ResolvedPath : std::uint8_t
{
    /// Any other sequence of the restated stages, run stage by stage.
    General,

    /// RGB, with or without its transfer, to Lab under one white.
    ToLab,

    /// Lab under one white to RGB, with or without its transfer.
    FromLab,

    /// RGB to unit-packed L*C*h* under one white.
    ToPackedLch,

    /// Unit-packed L*C*h* under one white to RGB.
    FromPackedLch,

    /// RGB to HSI.
    ToHsi,

    /// HSI to RGB.
    FromHsi
};

/// One planned colour conversion with what its stages read taken out of the plan.
struct ResolvedColorConversion
{
    /// The plan's stages, in order.
    std::array<ColorStage, kMaxColorStages> stages{};

    /// How many of `stages` the plan holds.
    std::uint32_t stageCount = 0u;

    /// The source encoding's transfer, which a decode removes.
    ColorTransferFunction decode = ColorTransferFunction::Linear;

    /// The source encoding's pure power law exponent, one when it states none.
    double decodeGamma = 1.0;

    /// The destination encoding's transfer, which an encode applies.
    ColorTransferFunction encode = ColorTransferFunction::Linear;

    /// The destination encoding's pure power law exponent, one when it states none.
    double encodeGamma = 1.0;

    /// The plan's matrix from linear RGB to XYZ.
    ColorMatrix3 toXyz{};

    /// The plan's white-point adaptation.
    ColorMatrix3 adaptation{};

    /// The plan's matrix from XYZ to linear RGB.
    ColorMatrix3 fromXyz{};

    /// The source encoding's white, which a stage from Lab reads.
    std::array<double, 3u> sourceWhite{};

    /// The destination encoding's white, which a stage to Lab reads.
    std::array<double, 3u> destinationWhite{};

    /// Whether the source's cylindrical hue is in degrees rather than turns.
    bool sourceHueInDegrees = false;

    /// Whether the destination's cylindrical hue is in degrees rather than turns.
    bool destinationHueInDegrees = false;

    /// Which fixed sequence the stages are, if any. A sequence without its transfer stage has a linear transfer,
    /// whose curve answers every value unchanged, so the fixed sequence runs the curve either way.
    ResolvedPath path = ResolvedPath::General;
};

/// Removes the source transfer from three channels.
///
/// @param resolved the conversion.
/// @param channels the channels, in place.
inline void decodeChannels(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    for (double& channel : channels)
    {
        channel = decodeTransfer(resolved.decode, resolved.decodeGamma, channel);
    }
}

/// Applies the destination transfer to three channels.
///
/// @param resolved the conversion.
/// @param channels the channels, in place.
inline void encodeChannels(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    for (double& channel : channels)
    {
        channel = encodeTransfer(resolved.encode, resolved.encodeGamma, channel);
    }
}

/// XYZ to Lab in CIE units under the destination white.
///
/// @param resolved the conversion.
/// @param channels the channels, in place.
inline void xyzToLab(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    const std::array<double, 3u>& white = resolved.destinationWhite;
    const double fx = labForward(channels[0] / white[0]);
    const double fy = labForward(channels[1] / white[1]);
    const double fz = labForward(channels[2] / white[2]);
    channels = {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

/// Lab in CIE units under the source white to XYZ.
///
/// @param resolved the conversion.
/// @param channels the channels, in place.
inline void labToXyz(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    const std::array<double, 3u>& white = resolved.sourceWhite;
    const double fy = (channels[0] + 16.0) / 116.0;
    const double fx = fy + channels[1] / 500.0;
    const double fz = fy - channels[2] / 200.0;
    channels = {white[0] * labInverse(fx), white[1] * labInverse(fy), white[2] * labInverse(fz)};
}

/// Lab to L*C*h* with the hue in degrees.
///
/// @param channels the channels, in place.
inline void labToLch(std::array<double, 3u>& channels) noexcept
{
    const double chroma = std::hypot(channels[1], channels[2]);
    double hue = std::atan2(channels[2], channels[1]) * 180.0 / std::numbers::pi;
    if (hue < 0.0)
    {
        hue += 360.0;
    }
    channels = {channels[0], chroma, hue};
}

/// L*C*h* with the hue in degrees to Lab.
///
/// @param channels the channels, in place.
inline void lchToLab(std::array<double, 3u>& channels) noexcept
{
    const double radians = channels[2] * std::numbers::pi / 180.0;
    channels = {channels[0], channels[1] * std::cos(radians), channels[1] * std::sin(radians)};
}

/// L*C*h* in CIE units to the unit packing: lightness over a hundred, chroma over two hundred, the signed hue in turns.
///
/// @param channels the channels, in place.
inline void packLch(std::array<double, 3u>& channels) noexcept
{
    const double signedHue = channels[2] >= 180.0 ? channels[2] - 360.0 : channels[2];
    channels = {channels[0] / 100.0, channels[1] / 200.0, (signedHue + 180.0) / 360.0};
}

/// Unit-packed L*C*h* to CIE units.
///
/// @param channels the channels, in place.
inline void unpackLch(std::array<double, 3u>& channels) noexcept
{
    const double signedHue = channels[2] * 360.0 - 180.0;
    channels = {channels[0] * 100.0, channels[1] * 200.0, signedHue < 0.0 ? signedHue + 360.0 : signedHue};
}

/// The intensity of RGB to HSI.
///
/// @param channels the RGB channels.
/// @return the intensity.
[[nodiscard]] inline double hsiIntensity(const std::array<double, 3u>& channels) noexcept
{
    return (channels[0] + channels[1] + channels[2]) / 3.0;
}

/// The saturation of RGB to HSI.
///
/// @param channels the RGB channels.
/// @param intensity their intensity.
/// @return the saturation.
[[nodiscard]] inline double hsiSaturation(const std::array<double, 3u>& channels, double intensity) noexcept
{
    const double lowerOfLast = channels[2] < channels[1] ? channels[2] : channels[1];
    const double minimum = lowerOfLast < channels[0] ? lowerOfLast : channels[0];
    return intensity == 0.0 ? 0.0 : 1.0 - minimum / intensity;
}

/// The hue of RGB to HSI, in the destination's convention.
///
/// @param resolved the conversion.
/// @param channels the RGB channels.
/// @return the hue.
[[nodiscard]] inline double hsiHue(const ResolvedColorConversion& resolved,
                                   const std::array<double, 3u>& channels) noexcept
{
    const double numerator = 0.5 * ((channels[0] - channels[1]) + (channels[0] - channels[2]));
    const double denominator = std::sqrt((channels[0] - channels[1]) * (channels[0] - channels[1]) +
                                         (channels[0] - channels[2]) * (channels[1] - channels[2]));
    double hue = 0.0;
    if (denominator != 0.0)
    {
        const double ratio = numerator / denominator;
        const double cosine = ratio < -1.0 ? -1.0 : (1.0 < ratio ? 1.0 : ratio);
        const double angle = std::acos(cosine);
        const double turns =
            (channels[2] <= channels[1] ? angle : 2.0 * std::numbers::pi - angle) / (2.0 * std::numbers::pi);
        hue = wrapTurn(turns);
    }
    if (resolved.destinationHueInDegrees)
    {
        hue *= 360.0;
    }
    return hue;
}

/// RGB to HSI.
///
/// @param resolved the conversion.
/// @param channels the channels, in place.
inline void rgbToHsi(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    const double intensity = hsiIntensity(channels);
    const double saturation = hsiSaturation(channels, intensity);
    const double hue = hsiHue(resolved, channels);
    channels = {hue, saturation, intensity};
}

/// HSI to RGB.
///
/// @param resolved the conversion.
/// @param channels the channels, in place.
inline void hsiToRgb(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    const double saturation = channels[1];
    const double intensity = channels[2];
    if (saturation == 0.0)
    {
        channels = {intensity, intensity, intensity};
        return;
    }
    const double degrees = wrapTurn(resolved.sourceHueInDegrees ? channels[0] / 360.0 : channels[0]) * 360.0;
    const double radians = std::numbers::pi / 180.0;
    const double sector = std::floor(degrees / 120.0);
    const double within = (degrees - sector * 120.0) * radians;
    const double low = intensity * (1.0 - saturation);
    const double high = intensity * (1.0 + (saturation * std::cos(within)) / std::cos(60.0 * radians - within));
    const double rest = 3.0 * intensity - (low + high);
    if (sector <= 0.0)
    {
        channels = {high, rest, low};
    }
    else if (sector <= 1.0)
    {
        channels = {low, high, rest};
    }
    else
    {
        channels = {rest, low, high};
    }
}

/// Runs a resolved conversion's stages, chosen one by one, over one pixel's three colour channels.
///
/// @param resolved the conversion.
/// @param channels the channels, in place.
inline void runResolved(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    for (std::uint32_t index = 0u; index < resolved.stageCount; ++index)
    {
        switch (resolved.stages[index])
        {
        case ColorStage::DecodeTransfer:
            decodeChannels(resolved, channels);
            break;
        case ColorStage::EncodeTransfer:
            encodeChannels(resolved, channels);
            break;
        case ColorStage::LinearRgbToXyz:
            channels = applyMatrix(resolved.toXyz, channels);
            break;
        case ColorStage::XyzToLinearRgb:
            channels = applyMatrix(resolved.fromXyz, channels);
            break;
        case ColorStage::AdaptWhitePoint:
            channels = applyMatrix(resolved.adaptation, channels);
            break;
        case ColorStage::XyzToLab:
            xyzToLab(resolved, channels);
            break;
        case ColorStage::LabToXyz:
            labToXyz(resolved, channels);
            break;
        case ColorStage::LabToLch:
            labToLch(channels);
            break;
        case ColorStage::LchToLab:
            lchToLab(channels);
            break;
        case ColorStage::PackPerceptual:
            packLch(channels);
            break;
        case ColorStage::UnpackPerceptual:
            unpackLch(channels);
            break;
        case ColorStage::RgbToHsi:
            rgbToHsi(resolved, channels);
            break;
        case ColorStage::HsiToRgb:
            hsiToRgb(resolved, channels);
            break;
        default:
            break;
        }
    }
}

/// Whether a stage list is exactly the expected stages, an optional stage before them and one after them allowed.
///
/// @param stages the plan's stages.
/// @param expected the stages that must be there, in order.
/// @param optionalFirst a stage that may come before them.
/// @param optionalLast a stage that may come after them.
/// @return true when the list is exactly that.
[[nodiscard]] inline bool stagesAre(std::span<const ColorStage> stages, std::initializer_list<ColorStage> expected,
                                    std::optional<ColorStage> optionalFirst,
                                    std::optional<ColorStage> optionalLast) noexcept
{
    const std::size_t begin =
        optionalFirst.has_value() && !stages.empty() && stages.front() == *optionalFirst ? 1u : 0u;
    const std::size_t end = optionalLast.has_value() && stages.size() > begin && stages.back() == *optionalLast
                                ? stages.size() - 1u
                                : stages.size();
    return std::equal(stages.begin() + static_cast<std::ptrdiff_t>(begin),
                      stages.begin() + static_cast<std::ptrdiff_t>(end), expected.begin(), expected.end());
}

/// Reads a plan's stages and constants, or nothing when it holds a stage this file does not restate - a pack or an
/// unpack is restated only for unit-packed L*C*h*.
///
/// @param plan the checked conversion.
/// @return the resolved conversion, or nothing.
[[nodiscard]] inline std::optional<ResolvedColorConversion> resolveColorConversion(const ColorConversionPlan& plan)
{
    const ColorEncoding& source = plan.source();
    const ColorEncoding& destination = plan.destination();
    ResolvedColorConversion resolved;
    for (const ColorStage stage : plan.stages())
    {
        switch (stage)
        {
        case ColorStage::PackPerceptual:
            if (destination.schema != ChannelSchema::Lch)
            {
                return std::nullopt;
            }
            break;
        case ColorStage::UnpackPerceptual:
            if (source.schema != ChannelSchema::Lch)
            {
                return std::nullopt;
            }
            break;
        case ColorStage::DecodeTransfer:
        case ColorStage::EncodeTransfer:
        case ColorStage::LinearRgbToXyz:
        case ColorStage::XyzToLinearRgb:
        case ColorStage::AdaptWhitePoint:
        case ColorStage::XyzToLab:
        case ColorStage::LabToXyz:
        case ColorStage::LabToLch:
        case ColorStage::LchToLab:
        case ColorStage::RgbToHsi:
        case ColorStage::HsiToRgb:
            break;
        default:
            return std::nullopt;
        }
        resolved.stages[resolved.stageCount] = stage;
        ++resolved.stageCount;
    }
    resolved.decode = source.transfer;
    resolved.decodeGamma = source.transferGamma.value_or(1.0);
    resolved.encode = destination.transfer;
    resolved.encodeGamma = destination.transferGamma.value_or(1.0);
    resolved.toXyz = plan.rgbToXyz();
    resolved.adaptation = plan.adaptation();
    resolved.fromXyz = plan.xyzToRgb();
    // The white points of the encodings that are perceptual, the only ones a stage reads.
    resolved.sourceWhite = whitePointTristimulus(source.whitePoint);
    resolved.destinationWhite = whitePointTristimulus(destination.whitePoint);
    resolved.sourceHueInDegrees = source.hue == HueConvention::Degrees;
    resolved.destinationHueInDegrees = destination.hue == HueConvention::Degrees;
    const std::span<const ColorStage> kept(resolved.stages.data(), resolved.stageCount);
    if (stagesAre(kept, {ColorStage::LinearRgbToXyz, ColorStage::XyzToLab}, ColorStage::DecodeTransfer, std::nullopt))
    {
        resolved.path = ResolvedPath::ToLab;
    }
    else if (stagesAre(kept, {ColorStage::LabToXyz, ColorStage::XyzToLinearRgb}, std::nullopt,
                       ColorStage::EncodeTransfer))
    {
        resolved.path = ResolvedPath::FromLab;
    }
    else if (stagesAre(
                 kept,
                 {ColorStage::LinearRgbToXyz, ColorStage::XyzToLab, ColorStage::LabToLch, ColorStage::PackPerceptual},
                 ColorStage::DecodeTransfer, std::nullopt))
    {
        resolved.path = ResolvedPath::ToPackedLch;
    }
    else if (stagesAre(
                 kept,
                 {ColorStage::UnpackPerceptual, ColorStage::LchToLab, ColorStage::LabToXyz, ColorStage::XyzToLinearRgb},
                 std::nullopt, ColorStage::EncodeTransfer))
    {
        resolved.path = ResolvedPath::FromPackedLch;
    }
    else if (stagesAre(kept, {ColorStage::RgbToHsi}, std::nullopt, std::nullopt))
    {
        resolved.path = ResolvedPath::ToHsi;
    }
    else if (stagesAre(kept, {ColorStage::HsiToRgb}, std::nullopt, std::nullopt))
    {
        resolved.path = ResolvedPath::FromHsi;
    }
    return resolved;
}

/// Converts one pixel through a resolved conversion whose stage sequence is fixed when the walk is compiled.
///
/// @tparam Path the sequence, `General` for whatever the plan holds.
/// @param resolved the conversion.
/// @param channels the channels, in place.
template <ResolvedPath Path>
inline void convertResolved(const ResolvedColorConversion& resolved, std::array<double, 3u>& channels) noexcept
{
    if constexpr (Path == ResolvedPath::ToLab || Path == ResolvedPath::ToPackedLch)
    {
        decodeChannels(resolved, channels);
        channels = applyMatrix(resolved.toXyz, channels);
        xyzToLab(resolved, channels);
        if constexpr (Path == ResolvedPath::ToPackedLch)
        {
            labToLch(channels);
            packLch(channels);
        }
    }
    else if constexpr (Path == ResolvedPath::FromLab || Path == ResolvedPath::FromPackedLch)
    {
        if constexpr (Path == ResolvedPath::FromPackedLch)
        {
            unpackLch(channels);
            lchToLab(channels);
        }
        labToXyz(resolved, channels);
        channels = applyMatrix(resolved.fromXyz, channels);
        encodeChannels(resolved, channels);
    }
    else if constexpr (Path == ResolvedPath::ToHsi)
    {
        rgbToHsi(resolved, channels);
    }
    else if constexpr (Path == ResolvedPath::FromHsi)
    {
        hsiToRgb(resolved, channels);
    }
    else
    {
        runResolved(resolved, channels);
    }
}

/// A pixel's first converted channel - its Lab lightness on the way to Lab - computing only what that needs when the
/// sequence is the fixed one to Lab: the decode, the matrix's second row and one compression.
///
/// @tparam Path the sequence.
/// @param resolved the conversion.
/// @param channels the pixel's three channels.
/// @return the first converted channel.
template <ResolvedPath Path>
[[nodiscard]] inline double firstResolvedChannel(const ResolvedColorConversion& resolved,
                                                 std::array<double, 3u> channels) noexcept
{
    if constexpr (Path == ResolvedPath::ToLab)
    {
        decodeChannels(resolved, channels);
        const std::array<double, 9u>& m = resolved.toXyz.coefficients;
        const double tristimulusY = m[3] * channels[0] + m[4] * channels[1] + m[5] * channels[2];
        return 116.0 * labForward(tristimulusY / resolved.destinationWhite[1]) - 16.0;
    }
    else
    {
        convertResolved<Path>(resolved, channels);
        return channels[0];
    }
}

/// How many pixels a chunked colour walk carries through each stage before the next: enough independent conversions
/// for the processor to overlap their long chains of transcendental calls, few enough to keep a chunk on the stack.
inline constexpr std::uint32_t kColorChunk = 256u;

/// The samples of one chunk of pixels, one array per channel.
template <std::size_t Channels> using ChunkPlanes = std::array<std::array<double, kColorChunk>, Channels>;

/// Runs one per-pixel stage over every pixel of a chunk's three colour channels, in place.
///
/// @tparam Stage a callable taking one pixel's three channels by reference.
/// @param chunk the chunk's channels.
/// @param count how many pixels of the chunk are filled.
/// @param stage the stage.
template <typename Stage>
inline void forEachChunkPixel(ChunkPlanes<3u>& chunk, std::uint32_t count, Stage&& stage) noexcept
{
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        std::array<double, 3u> pixel{chunk[0][index], chunk[1][index], chunk[2][index]};
        stage(pixel);
        chunk[0][index] = pixel[0];
        chunk[1][index] = pixel[1];
        chunk[2][index] = pixel[2];
    }
}

} // namespace an::color_stage_detail
