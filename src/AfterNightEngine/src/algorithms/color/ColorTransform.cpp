// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - colour encodings, the plan between two of them, and its execution.

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/color/ColorTransformPlanes.h"
#include "runtime/TaskPartition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

// Everything one colour transform needs, in one translation unit, because the stages only mean
// anything together. Splitting the transfer functions from the matrices from the perceptual space
// would produce three files whose contracts are each half of one sentence.
//
// **The chain is data and the executor is a loop over it.** A plan is a short list of stages and
// three matrices; running it is a switch inside a loop over that list, over a fixed array of at most
// four channels. That is deliberately not a fused kernel: a colour transform is a handful of
// transcendentals per pixel, so the switch is not what it costs, and having one evaluator means the
// scalar meaning a test compares against and the code an image pass runs are the same code rather
// than two things that agree until one is edited. The one exception is RGB to unit-packed Lab, which
// dominates several production and preview controls: that four-stage shape has its own row loops
// below, with the same formulas, and its sRGB form folds the white-point division into the matrix.
//
// **The connection space is normalized.** Tristimulus values run to one at the reference white, not
// to a hundred, which makes `NumericDomain::NormalizedLinear` true of an XYZ image rather than
// approximately true.

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planColorConversion";
constexpr const char* kPixelOperation = "an::convertColorPixel";
constexpr const char* kImageOperation = "an::convertImageColor";
constexpr const char* kPreviewPixelOperation = "an::convertRealtimePreviewColorPixel";
constexpr const char* kPreviewImageOperation = "an::convertRealtimePreviewImageColor";
constexpr const char* kDescriptorOperation = "an::colorTransformedDescriptor";
constexpr const char* kMatrixOperation = "an::buildChannelMatrixProgram";

/// How many channels any colour transform carries at once.
///
/// Four, because `Rgba` is the widest schema this family accepts and its alpha travels beside the
/// three colour channels rather than through them.
constexpr std::size_t kMaxColorChannels = 4u;

/// The three shapes a schema takes in the conversion graph.
///
/// Which of them a schema is, is what decides whether two encodings can be joined without visiting
/// the connection space at all.
enum class ColorFamily : std::uint8_t
{
    /// Red, green and blue, or something computed from them channel for channel.
    Rgb,

    /// CIE tristimulus values.
    Xyz,

    /// The CIE perceptual space, in either its rectangular or its cylindrical form.
    Perceptual
};

/// One set of primaries and its white, as chromaticities.
struct PrimaryChromaticities
{
    /// The red primary's `x` and `y`.
    std::array<double, 2u> red{};

    /// The green primary's `x` and `y`.
    std::array<double, 2u> green{};

    /// The blue primary's `x` and `y`.
    std::array<double, 2u> blue{};
};

/// Which family a schema belongs to.
///
/// @param schema the channel schema, already known to be one this family accepts.
/// @return the family.
[[nodiscard]] ColorFamily familyOf(ChannelSchema schema) noexcept
{
    switch (schema)
    {
    case ChannelSchema::Xyz:
        return ColorFamily::Xyz;
    case ChannelSchema::Lab:
    case ChannelSchema::Lch:
        return ColorFamily::Perceptual;
    default:
        return ColorFamily::Rgb;
    }
}

/// Whether a schema is one a colour transform can be an end of.
///
/// @param schema the channel schema.
/// @return false for `Mono`, `Named` and anything outside the enumeration.
[[nodiscard]] bool isTransformableSchema(ChannelSchema schema) noexcept
{
    switch (schema)
    {
    case ChannelSchema::Rgb:
    case ChannelSchema::Rgba:
    case ChannelSchema::Xyz:
    case ChannelSchema::Lab:
    case ChannelSchema::Lch:
    case ChannelSchema::Hsl:
    case ChannelSchema::Hsv:
    case ChannelSchema::Hsi:
    case ChannelSchema::YCbCr:
        return true;
    case ChannelSchema::Mono:
    case ChannelSchema::Named:
        return false;
    }
    return false;
}

/// The published chromaticities of one set of primaries.
///
/// @param primaries which set, already known to be one of the enumerators.
/// @return its three primaries.
[[nodiscard]] PrimaryChromaticities chromaticitiesOf(RgbPrimaries primaries) noexcept
{
    switch (primaries)
    {
    case RgbPrimaries::AdobeRgb1998:
        return PrimaryChromaticities{.red = {0.6400, 0.3300}, .green = {0.2100, 0.7100}, .blue = {0.1500, 0.0600}};
    case RgbPrimaries::DisplayP3:
        return PrimaryChromaticities{.red = {0.6800, 0.3200}, .green = {0.2650, 0.6900}, .blue = {0.1500, 0.0600}};
    case RgbPrimaries::Rec2020:
        return PrimaryChromaticities{.red = {0.7080, 0.2920}, .green = {0.1700, 0.7970}, .blue = {0.1310, 0.0460}};
    case RgbPrimaries::ProPhoto:
        return PrimaryChromaticities{
            .red = {0.734699, 0.265301}, .green = {0.159597, 0.840403}, .blue = {0.036598, 0.000105}};
    case RgbPrimaries::Srgb:
        break;
    }
    return PrimaryChromaticities{.red = {0.6400, 0.3300}, .green = {0.3000, 0.6000}, .blue = {0.1500, 0.0600}};
}

/// The illuminant's tristimulus values, as the standard tabulates them.
///
/// Tabulated rather than derived from a chromaticity pair, unlike the primaries below, because these
/// two triples are what every published conversion is stated against - the sRGB matrix's own
/// coefficients among them - and deriving them from four rounded decimal places would move the last
/// three digits of every Lab value this engine produces away from the reference.
///
/// @param whitePoint which illuminant.
/// @return its `X`, `Y` and `Z`, with `Y` exactly one.
[[nodiscard]] std::array<double, 3u> standardWhite(ChromaticWhitePoint whitePoint) noexcept
{
    return whitePoint == ChromaticWhitePoint::D50 ? std::array<double, 3u>{0.96422, 1.0, 0.82521}
                                                  : std::array<double, 3u>{0.95047, 1.0, 1.08883};
}

/// The tristimulus values of a chromaticity at unit luminance.
///
/// @param chromaticity the `x` and `y`.
/// @return `X`, `Y` and `Z` with `Y` exactly one.
[[nodiscard]] std::array<double, 3u> tristimulusOf(const std::array<double, 2u>& chromaticity) noexcept
{
    const double x = chromaticity[0];
    const double y = chromaticity[1];
    return std::array<double, 3u>{x / y, 1.0, (1.0 - x - y) / y};
}

/// The determinant of a row-major three by three matrix.
///
/// @param m the coefficients.
/// @return the determinant.
[[nodiscard]] double determinantOf(const std::array<double, 9u>& m) noexcept
{
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

/// The white point an encoding's values are under.
///
/// @param encoding the encoding, already validated.
/// @return the white point the primaries name, or the one the encoding states for the tristimulus
///         and perceptual schemas.
[[nodiscard]] ChromaticWhitePoint encodingWhitePoint(const ColorEncoding& encoding) noexcept
{
    return familyOf(encoding.schema) == ColorFamily::Rgb ? whitePointOfPrimaries(encoding.primaries)
                                                         : encoding.whitePoint;
}

/// The failure a refused encoding produces.
///
/// @param reason why the encoding describes no image.
/// @param parameter which argument carried it.
/// @return `ErrorCode::InvalidArgument`, naming the reason and the argument.
[[nodiscard]] Status encodingFailure(ColorEncodingError reason, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = colorEncodingErrorName(reason),
                                        .operation = kPlanOperation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// The failure an argument this operation cannot work with produces.
///
/// @param operation the engine operation to name.
/// @param code which failure.
/// @param message the sentence explaining what was wrong.
/// @param parameter which argument was wrong.
/// @return the failure, naming the argument.
[[nodiscard]] Status colorFailure(const char* operation, ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// The linear value a stated transfer function encodes as this one.
///
/// The magnitude goes through the curve and the sign is put back, so an out-of-gamut negative
/// survives instead of becoming a not-a-number.
///
/// @param transfer which curve.
/// @param gamma the pure power law's exponent, read only by `PureGamma`.
/// @param value the encoded value.
/// @param preview the preview power tables for the sRGB curve, or null for production arithmetic.
/// @return the linear value.
[[nodiscard]] double decodeTransfer(ColorTransferFunction transfer, double gamma, double value,
                                    const RealtimePreviewColorConversionPlan* preview = nullptr) noexcept
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
        linear = magnitude <= 0.04045 ? magnitude / 12.92
                                      : (preview != nullptr ? preview->powerTwoPointFour((magnitude + 0.055) / 1.055)
                                                            : std::pow((magnitude + 0.055) / 1.055, 2.4));
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
    return std::copysign(linear, value);
}

/// The encoded value a stated transfer function produces from this linear one.
///
/// @param transfer which curve.
/// @param gamma the pure power law's exponent, read only by `PureGamma`.
/// @param value the linear value.
/// @param preview the preview power tables for the sRGB curve, or null for production arithmetic.
/// @return the encoded value.
[[nodiscard]] double encodeTransfer(ColorTransferFunction transfer, double gamma, double value,
                                    const RealtimePreviewColorConversionPlan* preview = nullptr) noexcept
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
        encoded = magnitude <= 0.0031308 ? 12.92 * magnitude
                                         : 1.055 * (preview != nullptr ? preview->powerOneOverTwoPointFour(magnitude)
                                                                       : std::pow(magnitude, 1.0 / 2.4)) -
                                               0.055;
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
    return std::copysign(encoded, value);
}

/// The CIE lightness helper, whose linear segment keeps the function finite near zero.
constexpr double kLabDelta = 6.0 / 29.0;

/// The perceptual compression the Lab transform applies to a normalized tristimulus ratio.
///
/// @param ratio the tristimulus value divided by the white's.
/// @param preview the preview power tables for the cube root, or null for production arithmetic.
/// @return the compressed value.
[[nodiscard]] double labForward(double ratio, const RealtimePreviewColorConversionPlan* preview = nullptr) noexcept
{
    return ratio > kLabDelta * kLabDelta * kLabDelta
               ? (preview != nullptr ? preview->powerOneThird(ratio) : std::pow(ratio, 1.0 / 3.0))
               : ratio / (3.0 * kLabDelta * kLabDelta) + 4.0 / 29.0;
}

/// The inverse of `labForward`.
///
/// @param compressed the compressed value.
/// @return the tristimulus ratio.
[[nodiscard]] double labInverse(double compressed) noexcept
{
    return compressed > kLabDelta ? compressed * compressed * compressed
                                  : 3.0 * kLabDelta * kLabDelta * (compressed - 4.0 / 29.0);
}

/// Builds one lower-neighbour power table and measures it between entries.
///
/// @param entries all 1,024 destination entries.
/// @param exponent analytic exponent represented by the table.
/// @return largest absolute error found at eighth-interval probes.
[[nodiscard]] double fillPreviewPowerTable(std::span<double> entries, double exponent) noexcept
{
    const double last = static_cast<double>(entries.size() - 1u);
    for (std::size_t index = 0u; index < entries.size(); ++index)
    {
        entries[index] = std::pow(static_cast<double>(index) / last, exponent);
    }
    double maximumError = 0.0;
    constexpr std::uint32_t kProbesPerInterval = 8u;
    for (std::size_t interval = 0u; interval + 1u < entries.size(); ++interval)
    {
        for (std::uint32_t probe = 0u; probe < kProbesPerInterval; ++probe)
        {
            const double input =
                (static_cast<double>(interval) + static_cast<double>(probe) / static_cast<double>(kProbesPerInterval)) /
                last;
            maximumError = (std::max)(maximumError, std::abs(entries[interval] - std::pow(input, exponent)));
        }
    }
    return maximumError;
}

/// Evaluates one real-time-preview power table.
///
/// @param entries uniformly spaced lower-neighbour values over `[0, 1]`.
/// @param exponent scalar fallback exponent.
/// @param base value to raise.
/// @return the table lookup inside its interval, otherwise the scalar power.
[[nodiscard]] double previewPower(std::span<const double> entries, double exponent, double base) noexcept
{
    if (base >= 1.0 && base < 1.00001)
    {
        base = 1.0;
    }
    if (base >= 0.0 && base <= 1.0)
    {
        const std::size_t index = static_cast<std::size_t>(base * static_cast<double>(entries.size() - 1u));
        return entries[index];
    }
    return std::pow(base, exponent);
}

/// Where a colour's hue sits on the wheel, as a fraction of a turn.
///
/// The shared hue of the three cylindrical RGB spaces, so that a hue read out of one and put back
/// into another means the same thing.
///
/// @param red the red channel.
/// @param green the green channel.
/// @param blue the blue channel.
/// @param maximum the greatest of the three.
/// @param delta the greatest minus the least.
/// @return the hue in `[0, 1)`, and zero for a colour with no chroma at all.
[[nodiscard]] double hueTurnFraction(double red, double green, double blue, double maximum, double delta) noexcept
{
    if (delta == 0.0)
    {
        return 0.0;
    }
    double hue = 0.0;
    if (maximum == red)
    {
        hue = ((green - blue) / delta) / 6.0;
    }
    else if (maximum == green)
    {
        hue = (((blue - red) / delta) + 2.0) / 6.0;
    }
    else
    {
        hue = (((red - green) / delta) + 4.0) / 6.0;
    }
    return hue < 0.0 ? hue + 1.0 : hue;
}

/// A hue brought into one turn.
///
/// @param hue a hue in turns, of any magnitude or sign.
/// @return the same hue in `[0, 1)`; a hue that is not a number is returned unchanged.
[[nodiscard]] double wrapTurn(double hue) noexcept
{
    if (!std::isfinite(hue))
    {
        return hue;
    }
    const double wrapped = hue - std::floor(hue);
    return wrapped >= 1.0 ? 0.0 : wrapped;
}

/// One segment of the standard lightness-based hue reconstruction.
///
/// @param low the value the segment starts from.
/// @param high the value the segment reaches.
/// @param hue the hue in turns, already brought into one turn.
/// @return the channel value.
[[nodiscard]] double hueSegment(double low, double high, double hue) noexcept
{
    if (hue < 1.0 / 6.0)
    {
        return low + (high - low) * 6.0 * hue;
    }
    if (hue < 0.5)
    {
        return high;
    }
    if (hue < 2.0 / 3.0)
    {
        return low + (high - low) * (2.0 / 3.0 - hue) * 6.0;
    }
    return low;
}

/// The luma weights of one luma-chroma variant.
///
/// @param variant which weights.
/// @return the red, green and blue weights, which sum to one.
[[nodiscard]] std::array<double, 3u> lumaWeights(YCbCrVariant variant) noexcept
{
    if (variant == YCbCrVariant::Rec709FullRange)
    {
        return std::array<double, 3u>{0.2126, 0.7152, 0.0722};
    }
    return std::array<double, 3u>{0.299, 0.587, 0.114};
}

/// The Bradford cone response matrix, and its inverse below it.
constexpr ColorMatrix3 kBradford{{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296}};

/// Runs one stage over a pixel's three colour channels.
///
/// @param stage which step.
/// @param plan the conversion the step belongs to, for its matrices and its two encodings.
/// @param channels the three colour channels, read and written in place.
/// @param preview the preview power tables, or null for production arithmetic.
void runStage(ColorStage stage, const ColorConversionPlan& plan, std::array<double, 3u>& channels,
              const RealtimePreviewColorConversionPlan* preview = nullptr) noexcept
{
    const ColorEncoding& source = plan.source();
    const ColorEncoding& destination = plan.destination();
    switch (stage)
    {
    case ColorStage::DecodeTransfer: {
        const double gamma = source.transferGamma.value_or(1.0);
        for (double& channel : channels)
        {
            channel = decodeTransfer(source.transfer, gamma, channel, preview);
        }
        return;
    }
    case ColorStage::EncodeTransfer: {
        const double gamma = destination.transferGamma.value_or(1.0);
        for (double& channel : channels)
        {
            channel = encodeTransfer(destination.transfer, gamma, channel, preview);
        }
        return;
    }
    case ColorStage::LinearRgbToXyz:
        channels = applyColorMatrix(plan.rgbToXyz(), channels);
        return;
    case ColorStage::XyzToLinearRgb:
        channels = applyColorMatrix(plan.xyzToRgb(), channels);
        return;
    case ColorStage::AdaptWhitePoint:
        channels = applyColorMatrix(plan.adaptation(), channels);
        return;
    case ColorStage::XyzToLab: {
        const std::array<double, 3u> white = whitePointTristimulus(encodingWhitePoint(destination));
        const double fx = labForward(channels[0] / white[0], preview);
        const double fy = labForward(channels[1] / white[1], preview);
        const double fz = labForward(channels[2] / white[2], preview);
        channels = {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
        return;
    }
    case ColorStage::LabToXyz: {
        const std::array<double, 3u> white = whitePointTristimulus(encodingWhitePoint(source));
        const double fy = (channels[0] + 16.0) / 116.0;
        const double fx = fy + channels[1] / 500.0;
        const double fz = fy - channels[2] / 200.0;
        channels = {white[0] * labInverse(fx), white[1] * labInverse(fy), white[2] * labInverse(fz)};
        return;
    }
    case ColorStage::LabToLch: {
        const double chroma = std::hypot(channels[1], channels[2]);
        double hue = std::atan2(channels[2], channels[1]) * 180.0 / std::numbers::pi;
        if (hue < 0.0)
        {
            hue += 360.0;
        }
        channels = {channels[0], chroma, hue};
        return;
    }
    case ColorStage::LchToLab: {
        const double radians = channels[2] * std::numbers::pi / 180.0;
        channels = {channels[0], channels[1] * std::cos(radians), channels[1] * std::sin(radians)};
        return;
    }
    case ColorStage::PackPerceptual:
        if (destination.schema == ChannelSchema::Lch)
        {
            const double signedHue = channels[2] >= 180.0 ? channels[2] - 360.0 : channels[2];
            channels = {channels[0] / 100.0, channels[1] / 200.0, (signedHue + 180.0) / 360.0};
        }
        else
        {
            channels = {channels[0] / 100.0, (channels[1] + 128.0) / 255.0, (channels[2] + 128.0) / 255.0};
        }
        return;
    case ColorStage::UnpackPerceptual:
        if (source.schema == ChannelSchema::Lch)
        {
            const double signedHue = channels[2] * 360.0 - 180.0;
            channels = {channels[0] * 100.0, channels[1] * 200.0, signedHue < 0.0 ? signedHue + 360.0 : signedHue};
        }
        else
        {
            channels = {channels[0] * 100.0, channels[1] * 255.0 - 128.0, channels[2] * 255.0 - 128.0};
        }
        return;
    case ColorStage::RgbToHsl: {
        const double maximum = (std::max)(channels[0], (std::max)(channels[1], channels[2]));
        const double minimum = (std::min)(channels[0], (std::min)(channels[1], channels[2]));
        const double delta = maximum - minimum;
        const double lightness = (maximum + minimum) * 0.5;
        const double saturation =
            delta == 0.0 ? 0.0 : (lightness <= 0.5 ? delta / (maximum + minimum) : delta / (2.0 - maximum - minimum));
        double hue = hueTurnFraction(channels[0], channels[1], channels[2], maximum, delta);
        if (destination.hue == HueConvention::Degrees)
        {
            hue *= 360.0;
        }
        channels = {hue, saturation, lightness};
        return;
    }
    case ColorStage::HslToRgb: {
        const double hue = wrapTurn(source.hue == HueConvention::Degrees ? channels[0] / 360.0 : channels[0]);
        const double saturation = channels[1];
        const double lightness = channels[2];
        if (saturation == 0.0)
        {
            channels = {lightness, lightness, lightness};
            return;
        }
        const double high =
            lightness < 0.5 ? lightness * (1.0 + saturation) : lightness + saturation - lightness * saturation;
        const double low = 2.0 * lightness - high;
        channels = {hueSegment(low, high, wrapTurn(hue + 1.0 / 3.0)), hueSegment(low, high, hue),
                    hueSegment(low, high, wrapTurn(hue - 1.0 / 3.0))};
        return;
    }
    case ColorStage::RgbToHsv: {
        const double maximum = (std::max)(channels[0], (std::max)(channels[1], channels[2]));
        const double minimum = (std::min)(channels[0], (std::min)(channels[1], channels[2]));
        const double delta = maximum - minimum;
        const double saturation = maximum == 0.0 ? 0.0 : delta / maximum;
        double hue = hueTurnFraction(channels[0], channels[1], channels[2], maximum, delta);
        if (destination.hue == HueConvention::Degrees)
        {
            hue *= 360.0;
        }
        channels = {hue, saturation, maximum};
        return;
    }
    case ColorStage::HsvToRgb: {
        const double hue = wrapTurn(source.hue == HueConvention::Degrees ? channels[0] / 360.0 : channels[0]) * 6.0;
        const double saturation = channels[1];
        const double value = channels[2];
        const double sextant = std::floor(hue);
        const double fraction = hue - sextant;
        const double falling = value * (1.0 - saturation);
        const double descending = value * (1.0 - saturation * fraction);
        const double ascending = value * (1.0 - saturation * (1.0 - fraction));
        switch (static_cast<int>(sextant))
        {
        case 0:
            channels = {value, ascending, falling};
            return;
        case 1:
            channels = {descending, value, falling};
            return;
        case 2:
            channels = {falling, value, ascending};
            return;
        case 3:
            channels = {falling, descending, value};
            return;
        case 4:
            channels = {ascending, falling, value};
            return;
        default:
            channels = {value, falling, descending};
            return;
        }
    }
    case ColorStage::RgbToHsi: {
        const double intensity = (channels[0] + channels[1] + channels[2]) / 3.0;
        const double minimum = (std::min)(channels[0], (std::min)(channels[1], channels[2]));
        const double saturation = intensity == 0.0 ? 0.0 : 1.0 - minimum / intensity;
        const double numerator = 0.5 * ((channels[0] - channels[1]) + (channels[0] - channels[2]));
        const double denominator = std::sqrt((channels[0] - channels[1]) * (channels[0] - channels[1]) +
                                             (channels[0] - channels[2]) * (channels[1] - channels[2]));
        double hue = 0.0;
        if (denominator != 0.0)
        {
            const double cosine = std::clamp(numerator / denominator, -1.0, 1.0);
            const double angle = std::acos(cosine);
            const double turns =
                (channels[2] <= channels[1] ? angle : 2.0 * std::numbers::pi - angle) / (2.0 * std::numbers::pi);
            hue = wrapTurn(turns);
        }
        if (destination.hue == HueConvention::Degrees)
        {
            hue *= 360.0;
        }
        channels = {hue, saturation, intensity};
        return;
    }
    case ColorStage::HsiToRgb: {
        const double saturation = channels[1];
        const double intensity = channels[2];
        if (saturation == 0.0)
        {
            channels = {intensity, intensity, intensity};
            return;
        }
        const double degrees =
            wrapTurn(source.hue == HueConvention::Degrees ? channels[0] / 360.0 : channels[0]) * 360.0;
        const double radians = std::numbers::pi / 180.0;
        const double sector = std::floor(degrees / 120.0);
        const double within = (degrees - sector * 120.0) * radians;
        // The divisor is the cosine of an angle between minus and plus sixty degrees, so it never
        // falls below one half and needs no guard; an epsilon added to it would bias every hue it
        // touched rather than protect anything.
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
        return;
    }
    case ColorStage::RgbToYCbCr: {
        const std::array<double, 3u> weights = lumaWeights(destination.yCbCr);
        const double luma = weights[0] * channels[0] + weights[1] * channels[1] + weights[2] * channels[2];
        channels = {luma, (channels[2] - luma) / (2.0 * (1.0 - weights[2])) + 0.5,
                    (channels[0] - luma) / (2.0 * (1.0 - weights[0])) + 0.5};
        return;
    }
    case ColorStage::YCbCrToRgb: {
        const std::array<double, 3u> weights = lumaWeights(source.yCbCr);
        const double luma = channels[0];
        const double red = luma + 2.0 * (1.0 - weights[0]) * (channels[2] - 0.5);
        const double blue = luma + 2.0 * (1.0 - weights[2]) * (channels[1] - 0.5);
        channels = {red, (luma - weights[0] * red - weights[2] * blue) / weights[1], blue};
        return;
    }
    }
}

/// Where one channel's first sample of one row sits, in bytes from the buffer's first.
///
/// @param layout the geometry the channel belongs to.
/// @param channelIndex which channel, below the layout's channel count.
/// @param y the row.
/// @return the byte offset.
[[nodiscard]] std::uint64_t channelRowByteOffset(const ImageLayout& layout, std::uint32_t channelIndex,
                                                 std::uint64_t y) noexcept
{
    const std::uint32_t plane = channelIndex / layout.samplesPerPixel();
    const std::uint32_t within = channelIndex % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
           static_cast<std::uint64_t>(within) * static_cast<std::uint64_t>(layout.elementSizeBytes());
}

/// Converts one row's pixels, each channel's samples a step apart from the first one given.
///
/// @tparam SampleT the resident representation, which does not change.
/// @param plan the validated conversion.
/// @param in each source channel's first sample of the row.
/// @param sourceStep the samples between two of a channel's.
/// @param out each destination channel's first sample of the row.
/// @param destinationStep the samples between two of a channel's.
/// @param width the pixels of the row.
/// @param preview the preview power tables, or null for production arithmetic.
template <typename SampleT>
void convertRowPixels(const ColorConversionPlan& plan, const std::array<const SampleT*, kMaxColorChannels>& in,
                      std::uint64_t sourceStep, const std::array<SampleT*, kMaxColorChannels>& out,
                      std::uint64_t destinationStep, std::uint32_t width,
                      const RealtimePreviewColorConversionPlan* preview) noexcept
{
    const std::uint32_t channelCount = plan.sourceChannelCount();
    const std::span<const ColorStage> stages = plan.stages();
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        std::array<double, 3u> colorChannels{static_cast<double>(in[0][x * sourceStep]),
                                             static_cast<double>(in[1][x * sourceStep]),
                                             static_cast<double>(in[2][x * sourceStep])};
        const double alpha = channelCount == 4u ? static_cast<double>(in[3][x * sourceStep]) : 0.0;
        for (const ColorStage stage : stages)
        {
            runStage(stage, plan, colorChannels, preview);
        }
        out[0][x * destinationStep] = static_cast<SampleT>(colorChannels[0]);
        out[1][x * destinationStep] = static_cast<SampleT>(colorChannels[1]);
        out[2][x * destinationStep] = static_cast<SampleT>(colorChannels[2]);
        if (channelCount == 4u)
        {
            out[3][x * destinationStep] = static_cast<SampleT>(alpha);
        }
    }
}

/// Converts one row of one representation.
///
/// @tparam SampleT the resident representation, which does not change.
/// @param plan the validated conversion.
/// @param sourceBytes the source image's first byte.
/// @param sourceLayout where the source samples sit.
/// @param destinationBytes the destination image's first byte.
/// @param destinationLayout where the destination samples sit.
/// @param y which row.
/// @param preview the preview power tables, or null for production arithmetic.
template <typename SampleT>
void convertRow(const ColorConversionPlan& plan, const std::uint8_t* sourceBytes, const ImageLayout& sourceLayout,
                std::uint8_t* destinationBytes, const ImageLayout& destinationLayout, std::uint64_t y,
                const RealtimePreviewColorConversionPlan* preview) noexcept
{
    const std::uint32_t channelCount = plan.sourceChannelCount();
    std::array<const SampleT*, kMaxColorChannels> in{};
    std::array<SampleT*, kMaxColorChannels> out{};
    for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
    {
        in[channel] = reinterpret_cast<const SampleT*>(sourceBytes + channelRowByteOffset(sourceLayout, channel, y));
        out[channel] =
            reinterpret_cast<SampleT*>(destinationBytes + channelRowByteOffset(destinationLayout, channel, y));
    }
    convertRowPixels<SampleT>(plan, in, static_cast<std::uint64_t>(sourceLayout.samplesPerPixel()), out,
                              static_cast<std::uint64_t>(destinationLayout.samplesPerPixel()),
                              sourceLayout.extent().width(), preview);
}

/// Whether a plan is exactly decode, to XYZ, to Lab and pack from three-channel RGB, the shape the
/// direct row loops below run without the stage walker.
///
/// @param plan the checked conversion.
/// @return true for that four-stage shape and no other.
[[nodiscard]] bool isDirectRgbToUnitLabPlan(const ColorConversionPlan& plan) noexcept
{
    const std::span<const ColorStage> stages = plan.stages();
    return plan.sourceChannelCount() == 3u && plan.destinationChannelCount() == 3u &&
           plan.destination().schema == ChannelSchema::Lab &&
           plan.destination().perceptual == PerceptualEncoding::UnitPacked && stages.size() == 4u &&
           stages[0] == ColorStage::DecodeTransfer && stages[1] == ColorStage::LinearRgbToXyz &&
           stages[2] == ColorStage::XyzToLab && stages[3] == ColorStage::PackPerceptual;
}

template <bool RealtimePreview>
[[nodiscard]] double decodeDirectSrgb(double value, const RealtimePreviewColorConversionPlan* preview) noexcept
{
    const double magnitude = std::abs(value);
    const double linear = magnitude <= 0.04045
                              ? magnitude / 12.92
                              : (RealtimePreview ? preview->powerTwoPointFour((magnitude + 0.055) / 1.055)
                                                 : std::pow((magnitude + 0.055) / 1.055, 2.4));
    return value < 0.0 ? -linear : linear;
}

template <bool RealtimePreview>
[[nodiscard]] double directLabForward(double ratio, const RealtimePreviewColorConversionPlan* preview) noexcept
{
    if (ratio <= kLabDelta * kLabDelta * kLabDelta)
    {
        return ratio / (3.0 * kLabDelta * kLabDelta) + 4.0 / 29.0;
    }
    if constexpr (RealtimePreview)
    {
        return preview->powerOneThird(ratio);
    }
    return std::pow(ratio, 1.0 / 3.0);
}

/// The RGB-to-normalized-XYZ matrix used by the direct Lab path.
///
/// @param plan the checked RGB-to-Lab conversion.
/// @return the plan's matrix with each XYZ row divided by the destination white.
[[nodiscard]] std::array<double, 9u> directLabMatrix(const ColorConversionPlan& plan) noexcept
{
    const std::array<double, 9u>& matrix = plan.rgbToXyz().coefficients;
    const std::array<double, 3u> white = whitePointTristimulus(encodingWhitePoint(plan.destination()));
    return {matrix[0] / white[0], matrix[1] / white[0], matrix[2] / white[0],
            matrix[3] / white[1], matrix[4] / white[1], matrix[5] / white[1],
            matrix[6] / white[2], matrix[7] / white[2], matrix[8] / white[2]};
}

/// Converts one contiguous range through the direct sRGB-to-packed-Lab path.
///
/// @tparam SampleT the resident floating-point representation.
/// @tparam RealtimePreview whether to use the preview power tables.
/// @param in the three planar source channels.
/// @param out the three planar destination channels.
/// @param begin first pixel in the range.
/// @param end one past the last pixel in the range.
/// @param matrix the RGB-to-normalized-XYZ matrix.
/// @param preview the preview power tables, or null for production arithmetic.
template <typename SampleT, bool RealtimePreview>
void convertDirectSrgbToUnitLabRange(const std::array<const SampleT*, 3u>& in, const std::array<SampleT*, 3u>& out,
                                     std::uint64_t begin, std::uint64_t end, const std::array<double, 9u>& matrix,
                                     const RealtimePreviewColorConversionPlan* preview) noexcept
{
    for (std::uint64_t pixel = begin; pixel < end; ++pixel)
    {
        const double red = decodeDirectSrgb<RealtimePreview>(static_cast<double>(in[0][pixel]), preview);
        const double green = decodeDirectSrgb<RealtimePreview>(static_cast<double>(in[1][pixel]), preview);
        const double blue = decodeDirectSrgb<RealtimePreview>(static_cast<double>(in[2][pixel]), preview);
        const double fx =
            directLabForward<RealtimePreview>(matrix[0] * red + matrix[1] * green + matrix[2] * blue, preview);
        const double fy =
            directLabForward<RealtimePreview>(matrix[3] * red + matrix[4] * green + matrix[5] * blue, preview);
        const double fz =
            directLabForward<RealtimePreview>(matrix[6] * red + matrix[7] * green + matrix[8] * blue, preview);
        const double lightness = 116.0 * fy - 16.0;
        const double greenRed = 500.0 * (fx - fy);
        const double blueYellow = 200.0 * (fy - fz);
        out[0][pixel] = static_cast<SampleT>(lightness / 100.0);
        out[1][pixel] = static_cast<SampleT>((greenRed + 128.0) / 255.0);
        out[2][pixel] = static_cast<SampleT>((blueYellow + 128.0) / 255.0);
    }
}

// RGB-to-Lab dominates several production and preview controls. The general stage walker keeps the
// complete colour vocabulary, but this common shape should pay neither a transfer-function switch
// nor three white-point divisions for every pixel. The row-normalized matrix is algebraically the
// same transform; keeping the production pow calls and the preview tables separate also prevents a
// preview-policy branch from entering the production inner loop.
template <typename SampleT, bool RealtimePreview>
void convertDirectSrgbToUnitLabRow(const std::uint8_t* sourceBytes, const ImageLayout& sourceLayout,
                                   std::uint8_t* destinationBytes, const ImageLayout& destinationLayout,
                                   std::uint64_t y, const std::array<double, 9u>& matrix,
                                   const RealtimePreviewColorConversionPlan* preview) noexcept
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint64_t sourceStep = sourceLayout.samplesPerPixel();
    const std::uint64_t destinationStep = destinationLayout.samplesPerPixel();
    std::array<const SampleT*, 3u> in{};
    std::array<SampleT*, 3u> out{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        in[channel] = reinterpret_cast<const SampleT*>(sourceBytes + channelRowByteOffset(sourceLayout, channel, y));
        out[channel] =
            reinterpret_cast<SampleT*>(destinationBytes + channelRowByteOffset(destinationLayout, channel, y));
    }

    if (sourceStep == 1u && destinationStep == 1u)
    {
        convertDirectSrgbToUnitLabRange<SampleT, RealtimePreview>(in, out, 0u, width, matrix, preview);
        return;
    }

    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const double red = decodeDirectSrgb<RealtimePreview>(static_cast<double>(in[0][x * sourceStep]), preview);
        const double green = decodeDirectSrgb<RealtimePreview>(static_cast<double>(in[1][x * sourceStep]), preview);
        const double blue = decodeDirectSrgb<RealtimePreview>(static_cast<double>(in[2][x * sourceStep]), preview);
        const double fx =
            directLabForward<RealtimePreview>(matrix[0] * red + matrix[1] * green + matrix[2] * blue, preview);
        const double fy =
            directLabForward<RealtimePreview>(matrix[3] * red + matrix[4] * green + matrix[5] * blue, preview);
        const double fz =
            directLabForward<RealtimePreview>(matrix[6] * red + matrix[7] * green + matrix[8] * blue, preview);
        const double lightness = 116.0 * fy - 16.0;
        const double greenRed = 500.0 * (fx - fy);
        const double blueYellow = 200.0 * (fy - fz);
        out[0][x * destinationStep] = static_cast<SampleT>(lightness / 100.0);
        out[1][x * destinationStep] = static_cast<SampleT>((greenRed + 128.0) / 255.0);
        out[2][x * destinationStep] = static_cast<SampleT>((blueYellow + 128.0) / 255.0);
    }
}

/// Whether three planes form one tightly packed run per channel.
///
/// @param layout the checked layout to inspect.
/// @param elementSize bytes in the resident floating-point representation.
/// @return true when rows and planes carry no padding.
[[nodiscard]] bool isTightPlanarColorLayout(const ImageLayout& layout, std::uint64_t elementSize) noexcept
{
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(layout.extent().width()) * elementSize;
    return layout.elementSizeBytes() == elementSize && layout.samplesPerPixel() == 1u && layout.planeCount() == 3u &&
           layout.rowStrideBytes() == rowBytes &&
           layout.planeStrideBytes() == rowBytes * static_cast<std::uint64_t>(layout.extent().height());
}

/// How many chunks each worker is offered for the tight RGB-to-Lab loop.
///
/// Six powers a pixel make every chunk of this loop long and every chunk alike, so the loop ends when
/// its last chunk does. With the scheduler's usual four a worker, sixteen workers on a host also
/// running other work took 8% longer than the same arithmetic divided by oneTBB's own partitioner as
/// it goes, while one and four workers did not; sixteen chunks a worker recovered most of the
/// difference.
constexpr std::uint32_t kTightLabChunksPerWorker = 16u;

/// Converts a tightly packed three-plane sRGB image without rebuilding row views in the hot loop.
///
/// @tparam SampleT the resident floating-point representation.
/// @param execution the operation's admitted scheduler and policy.
/// @param sourceBytes first byte of the source planes.
/// @param sourceLayout the tightly packed source layout.
/// @param destinationBytes first byte of the destination planes.
/// @param destinationLayout the tightly packed destination layout.
/// @param matrix the RGB-to-normalized-XYZ matrix.
/// @param preview the preview power tables, or null for production arithmetic.
/// @param cost scheduler estimate for one pixel.
/// @return success, cancellation, deadline expiry or a translated worker failure.
template <typename SampleT>
[[nodiscard]] Status
convertTightDirectSrgbToUnitLab(const ExecutionContext& execution, const std::uint8_t* sourceBytes,
                                const ImageLayout& sourceLayout, std::uint8_t* destinationBytes,
                                const ImageLayout& destinationLayout, const std::array<double, 9u>& matrix,
                                const RealtimePreviewColorConversionPlan* preview, const TaskCost& cost)
{
    const std::uint64_t pixels = static_cast<std::uint64_t>(sourceLayout.extent().width()) *
                                 static_cast<std::uint64_t>(sourceLayout.extent().height());
    std::array<const SampleT*, 3u> in{};
    std::array<SampleT*, 3u> out{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        in[channel] = reinterpret_cast<const SampleT*>(sourceBytes + static_cast<std::uint64_t>(channel) *
                                                                         sourceLayout.planeStrideBytes());
        out[channel] = reinterpret_cast<SampleT*>(destinationBytes + static_cast<std::uint64_t>(channel) *
                                                                         destinationLayout.planeStrideBytes());
    }

    if (preview == nullptr)
    {
        return parallelForInChunks(execution, pixels, cost, kTightLabChunksPerWorker, [&](TaskRange range) -> Status {
            convertDirectSrgbToUnitLabRange<SampleT, false>(in, out, range.begin, range.end, matrix, nullptr);
            return Status::success();
        });
    }
    return parallelForInChunks(execution, pixels, cost, kTightLabChunksPerWorker, [&](TaskRange range) -> Status {
        convertDirectSrgbToUnitLabRange<SampleT, true>(in, out, range.begin, range.end, matrix, preview);
        return Status::success();
    });
}

template <typename SampleT>
void convertDirectRgbToUnitLabRow(const ColorConversionPlan& plan, const std::uint8_t* sourceBytes,
                                  const ImageLayout& sourceLayout, std::uint8_t* destinationBytes,
                                  const ImageLayout& destinationLayout, std::uint64_t y,
                                  const RealtimePreviewColorConversionPlan* preview) noexcept
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint64_t sourceStep = sourceLayout.samplesPerPixel();
    const std::uint64_t destinationStep = destinationLayout.samplesPerPixel();
    std::array<const SampleT*, 3u> in{};
    std::array<SampleT*, 3u> out{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        in[channel] = reinterpret_cast<const SampleT*>(sourceBytes + channelRowByteOffset(sourceLayout, channel, y));
        out[channel] =
            reinterpret_cast<SampleT*>(destinationBytes + channelRowByteOffset(destinationLayout, channel, y));
    }

    const ColorEncoding& sourceEncoding = plan.source();
    const double gamma = sourceEncoding.transferGamma.value_or(1.0);
    const std::array<double, 9u>& matrix = plan.rgbToXyz().coefficients;
    const std::array<double, 3u> white = whitePointTristimulus(encodingWhitePoint(plan.destination()));
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const double red =
            decodeTransfer(sourceEncoding.transfer, gamma, static_cast<double>(in[0][x * sourceStep]), preview);
        const double green =
            decodeTransfer(sourceEncoding.transfer, gamma, static_cast<double>(in[1][x * sourceStep]), preview);
        const double blue =
            decodeTransfer(sourceEncoding.transfer, gamma, static_cast<double>(in[2][x * sourceStep]), preview);
        const double tristimulusX = matrix[0] * red + matrix[1] * green + matrix[2] * blue;
        const double tristimulusY = matrix[3] * red + matrix[4] * green + matrix[5] * blue;
        const double tristimulusZ = matrix[6] * red + matrix[7] * green + matrix[8] * blue;
        const double fx = labForward(tristimulusX / white[0], preview);
        const double fy = labForward(tristimulusY / white[1], preview);
        const double fz = labForward(tristimulusZ / white[2], preview);
        const double lightness = 116.0 * fy - 16.0;
        const double greenRed = 500.0 * (fx - fy);
        const double blueYellow = 200.0 * (fy - fz);
        out[0][x * destinationStep] = static_cast<SampleT>(lightness / 100.0);
        out[1][x * destinationStep] = static_cast<SampleT>((greenRed + 128.0) / 255.0);
        out[2][x * destinationStep] = static_cast<SampleT>((blueYellow + 128.0) / 255.0);
    }
}

/// Whether a plan is exactly unpack, to XYZ and to linear RGB, with the sRGB curve or no curve after it, from
/// unit-packed Lab to three-channel RGB under one white: the shape the direct inverse loops below run without the
/// stage walker.
///
/// @param plan the checked conversion.
/// @return true for that three- or four-stage shape and no other.
[[nodiscard]] bool isDirectUnitLabToRgbPlan(const ColorConversionPlan& plan) noexcept
{
    const std::span<const ColorStage> stages = plan.stages();
    const ColorTransferFunction transfer = plan.destination().transfer;
    const bool encoded = transfer == ColorTransferFunction::Srgb;
    if (plan.sourceChannelCount() != 3u || plan.destinationChannelCount() != 3u ||
        plan.source().schema != ChannelSchema::Lab || plan.source().perceptual != PerceptualEncoding::UnitPacked ||
        plan.destination().schema != ChannelSchema::Rgb || (!encoded && transfer != ColorTransferFunction::Linear))
    {
        return false;
    }
    return stages.size() == (encoded ? 4u : 3u) && stages[0] == ColorStage::UnpackPerceptual &&
           stages[1] == ColorStage::LabToXyz && stages[2] == ColorStage::XyzToLinearRgb &&
           (!encoded || stages[3] == ColorStage::EncodeTransfer);
}

/// The sRGB curve as the stage walker applies it in production, written out so a row loop can inline it.
///
/// @param value the linear value.
/// @return the encoded value, the sign restored.
[[nodiscard]] double encodeDirectSrgb(double value) noexcept
{
    const double magnitude = std::abs(value);
    const double encoded = magnitude <= 0.0031308 ? 12.92 * magnitude : 1.055 * std::pow(magnitude, 1.0 / 2.4) - 0.055;
    return std::copysign(encoded, value);
}

/// Converts one unit-packed Lab pixel to RGB through the same expressions, in the same order, as the stage walker.
///
/// @tparam Encode whether the destination carries the sRGB curve.
/// @param packed the three unit-packed channels.
/// @param matrix the destination primaries' matrix from XYZ.
/// @param white the source white's tristimulus values.
/// @return the three RGB channels.
template <bool Encode>
[[nodiscard]] std::array<double, 3u> directUnitLabToRgb(const std::array<double, 3u>& packed,
                                                        const std::array<double, 9u>& matrix,
                                                        const std::array<double, 3u>& white) noexcept
{
    const double lightness = packed[0] * 100.0;
    const double greenRed = packed[1] * 255.0 - 128.0;
    const double blueYellow = packed[2] * 255.0 - 128.0;
    const double fy = (lightness + 16.0) / 116.0;
    const double fx = fy + greenRed / 500.0;
    const double fz = fy - blueYellow / 200.0;
    const double x = white[0] * labInverse(fx);
    const double y = white[1] * labInverse(fy);
    const double z = white[2] * labInverse(fz);
    double red = matrix[0] * x + matrix[1] * y + matrix[2] * z;
    double green = matrix[3] * x + matrix[4] * y + matrix[5] * z;
    double blue = matrix[6] * x + matrix[7] * y + matrix[8] * z;
    if constexpr (Encode)
    {
        red = encodeDirectSrgb(red);
        green = encodeDirectSrgb(green);
        blue = encodeDirectSrgb(blue);
    }
    return {red, green, blue};
}

/// Converts one contiguous range of planar unit-packed Lab to planar RGB.
///
/// @tparam SampleT the resident floating-point representation.
/// @tparam Encode whether the destination carries the sRGB curve.
/// @param in the three planar source channels.
/// @param out the three planar destination channels; may be the source's own.
/// @param begin first pixel in the range.
/// @param end one past the last pixel in the range.
/// @param matrix the destination primaries' matrix from XYZ.
/// @param white the source white's tristimulus values.
template <typename SampleT, bool Encode>
void convertDirectUnitLabToRgbRange(const std::array<const SampleT*, 3u>& in, const std::array<SampleT*, 3u>& out,
                                    std::uint64_t begin, std::uint64_t end, const std::array<double, 9u>& matrix,
                                    const std::array<double, 3u>& white) noexcept
{
    for (std::uint64_t pixel = begin; pixel < end; ++pixel)
    {
        const std::array<double, 3u> rgb = directUnitLabToRgb<Encode>(
            {static_cast<double>(in[0][pixel]), static_cast<double>(in[1][pixel]), static_cast<double>(in[2][pixel])},
            matrix, white);
        out[0][pixel] = static_cast<SampleT>(rgb[0]);
        out[1][pixel] = static_cast<SampleT>(rgb[1]);
        out[2][pixel] = static_cast<SampleT>(rgb[2]);
    }
}

/// Converts one row's pixels of unit-packed Lab to RGB, each channel's samples a step apart from the first one given.
///
/// @tparam SampleT the resident floating-point representation.
/// @tparam Encode whether the destination carries the sRGB curve.
template <typename SampleT, bool Encode>
void convertDirectUnitLabToRgbRowPixels(const std::array<const SampleT*, 3u>& in, std::uint64_t sourceStep,
                                        const std::array<SampleT*, 3u>& out, std::uint64_t destinationStep,
                                        std::uint32_t width, const std::array<double, 9u>& matrix,
                                        const std::array<double, 3u>& white) noexcept
{
    if (sourceStep == 1u && destinationStep == 1u)
    {
        convertDirectUnitLabToRgbRange<SampleT, Encode>(in, out, 0u, width, matrix, white);
        return;
    }
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const std::array<double, 3u> rgb = directUnitLabToRgb<Encode>({static_cast<double>(in[0][x * sourceStep]),
                                                                       static_cast<double>(in[1][x * sourceStep]),
                                                                       static_cast<double>(in[2][x * sourceStep])},
                                                                      matrix, white);
        out[0][x * destinationStep] = static_cast<SampleT>(rgb[0]);
        out[1][x * destinationStep] = static_cast<SampleT>(rgb[1]);
        out[2][x * destinationStep] = static_cast<SampleT>(rgb[2]);
    }
}

/// Converts one row of unit-packed Lab to RGB in any arrangement.
///
/// @tparam SampleT the resident floating-point representation.
/// @tparam Encode whether the destination carries the sRGB curve.
template <typename SampleT, bool Encode>
void convertDirectUnitLabToRgbRow(const std::uint8_t* sourceBytes, const ImageLayout& sourceLayout,
                                  std::uint8_t* destinationBytes, const ImageLayout& destinationLayout, std::uint64_t y,
                                  const std::array<double, 9u>& matrix, const std::array<double, 3u>& white) noexcept
{
    std::array<const SampleT*, 3u> in{};
    std::array<SampleT*, 3u> out{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        in[channel] = reinterpret_cast<const SampleT*>(sourceBytes + channelRowByteOffset(sourceLayout, channel, y));
        out[channel] =
            reinterpret_cast<SampleT*>(destinationBytes + channelRowByteOffset(destinationLayout, channel, y));
    }
    convertDirectUnitLabToRgbRowPixels<SampleT, Encode>(in, sourceLayout.samplesPerPixel(), out,
                                                        destinationLayout.samplesPerPixel(),
                                                        sourceLayout.extent().width(), matrix, white);
}

/// Converts unit-packed Lab to RGB through the direct loops: whole planes in chunks when both sides are tightly
/// packed planes, row by row otherwise.
///
/// @tparam SampleT the resident floating-point representation.
/// @param execution the operation's admitted scheduler and policy.
/// @param plan the checked conversion, of the shape `isDirectUnitLabToRgbPlan` accepts.
/// @param sourceBytes first byte of the source.
/// @param sourceLayout the source layout.
/// @param destinationBytes first byte of the destination; may be the source's own under an identical layout.
/// @param destinationLayout the destination layout.
/// @param pixelCost scheduler estimate for one pixel.
/// @return success, cancellation, deadline expiry or a translated worker failure.
template <typename SampleT>
[[nodiscard]] Status convertDirectUnitLabToRgb(const ExecutionContext& execution, const ColorConversionPlan& plan,
                                               const std::uint8_t* sourceBytes, const ImageLayout& sourceLayout,
                                               std::uint8_t* destinationBytes, const ImageLayout& destinationLayout,
                                               const TaskCost& pixelCost)
{
    const std::array<double, 9u>& matrix = plan.xyzToRgb().coefficients;
    const std::array<double, 3u> white = whitePointTristimulus(encodingWhitePoint(plan.source()));
    const bool encode = plan.destination().transfer == ColorTransferFunction::Srgb;
    const std::uint64_t elementSize = sizeof(SampleT);
    if (isTightPlanarColorLayout(sourceLayout, elementSize) && isTightPlanarColorLayout(destinationLayout, elementSize))
    {
        const std::uint64_t pixels = static_cast<std::uint64_t>(sourceLayout.extent().width()) *
                                     static_cast<std::uint64_t>(sourceLayout.extent().height());
        std::array<const SampleT*, 3u> in{};
        std::array<SampleT*, 3u> out{};
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            in[channel] = reinterpret_cast<const SampleT*>(sourceBytes + static_cast<std::uint64_t>(channel) *
                                                                             sourceLayout.planeStrideBytes());
            out[channel] = reinterpret_cast<SampleT*>(destinationBytes + static_cast<std::uint64_t>(channel) *
                                                                             destinationLayout.planeStrideBytes());
        }
        return parallelForInChunks(
            execution, pixels, pixelCost, kTightLabChunksPerWorker, [&](TaskRange range) -> Status {
                if (encode)
                {
                    convertDirectUnitLabToRgbRange<SampleT, true>(in, out, range.begin, range.end, matrix, white);
                }
                else
                {
                    convertDirectUnitLabToRgbRange<SampleT, false>(in, out, range.begin, range.end, matrix, white);
                }
                return Status::success();
            });
    }
    const TaskCost rowCost{.bytesPerItem = pixelCost.bytesPerItem * sourceLayout.extent().width(),
                           .operationsPerItem = pixelCost.operationsPerItem * sourceLayout.extent().width()};
    return parallelFor(execution, sourceLayout.extent().height(), rowCost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            if (encode)
            {
                convertDirectUnitLabToRgbRow<SampleT, true>(sourceBytes, sourceLayout, destinationBytes,
                                                            destinationLayout, y, matrix, white);
            }
            else
            {
                convertDirectUnitLabToRgbRow<SampleT, false>(sourceBytes, sourceLayout, destinationBytes,
                                                             destinationLayout, y, matrix, white);
            }
        }
        return Status::success();
    });
}

/// `convertImageColor` over three source planes at addresses of their own, each laid out as one plane: the same loops
/// over the same samples, each channel read from its own plane. The production arithmetic only.
///
/// @tparam SampleT the resident floating-point representation.
/// @param execution the operation's admitted scheduler and policy.
/// @param plan the checked conversion, of three channels on either side.
/// @param planes each source channel's first sample.
/// @param planeLayout where each plane's samples sit.
/// @param destinationBytes first byte of the destination.
/// @param destinationLayout the destination layout.
/// @return success, cancellation, deadline expiry or a translated worker failure.
template <typename SampleT>
[[nodiscard]] Status convertPlanesTyped(const ExecutionContext& execution, const ColorConversionPlan& plan,
                                        const std::array<const std::uint8_t*, 3u>& planes,
                                        const ImageLayout& planeLayout, std::uint8_t* destinationBytes,
                                        const ImageLayout& destinationLayout)
{
    const std::uint32_t width = planeLayout.extent().width();
    const std::uint64_t height = planeLayout.extent().height();
    const std::uint64_t elementSize = sizeof(SampleT);
    const std::uint64_t rowStride = planeLayout.rowStrideBytes();
    const std::uint64_t destinationStep = destinationLayout.samplesPerPixel();
    const std::uint64_t channelCount = 3u;
    const TaskCost pixelCost{.bytesPerItem = channelCount * elementSize * 2u,
                             .operationsPerItem =
                                 (static_cast<std::uint64_t>(plan.stages().size()) + 1u) * channelCount};
    const TaskCost rowCost{.bytesPerItem = pixelCost.bytesPerItem * width,
                           .operationsPerItem = pixelCost.operationsPerItem * width};
    const auto rowOf = [&](std::uint64_t y) {
        std::array<const SampleT*, 3u> in{};
        std::array<SampleT*, 3u> out{};
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            in[channel] = reinterpret_cast<const SampleT*>(planes[channel] + y * rowStride);
            out[channel] =
                reinterpret_cast<SampleT*>(destinationBytes + channelRowByteOffset(destinationLayout, channel, y));
        }
        return std::pair{in, out};
    };

    if (isDirectUnitLabToRgbPlan(plan))
    {
        const std::array<double, 9u>& matrix = plan.xyzToRgb().coefficients;
        const std::array<double, 3u> white = whitePointTristimulus(encodingWhitePoint(plan.source()));
        const bool encode = plan.destination().transfer == ColorTransferFunction::Srgb;
        if (rowStride == width * elementSize && isTightPlanarColorLayout(destinationLayout, elementSize))
        {
            const std::array<const SampleT*, 3u> in{reinterpret_cast<const SampleT*>(planes[0]),
                                                    reinterpret_cast<const SampleT*>(planes[1]),
                                                    reinterpret_cast<const SampleT*>(planes[2])};
            std::array<SampleT*, 3u> out{};
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                out[channel] = reinterpret_cast<SampleT*>(destinationBytes + static_cast<std::uint64_t>(channel) *
                                                                                 destinationLayout.planeStrideBytes());
            }
            return parallelForInChunks(
                execution, static_cast<std::uint64_t>(width) * height, pixelCost, kTightLabChunksPerWorker,
                [&](TaskRange range) -> Status {
                    if (encode)
                    {
                        convertDirectUnitLabToRgbRange<SampleT, true>(in, out, range.begin, range.end, matrix, white);
                    }
                    else
                    {
                        convertDirectUnitLabToRgbRange<SampleT, false>(in, out, range.begin, range.end, matrix, white);
                    }
                    return Status::success();
                });
        }
        return parallelFor(execution, height, rowCost, [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const auto [in, out] = rowOf(y);
                if (encode)
                {
                    convertDirectUnitLabToRgbRowPixels<SampleT, true>(in, 1u, out, destinationStep, width, matrix,
                                                                      white);
                }
                else
                {
                    convertDirectUnitLabToRgbRowPixels<SampleT, false>(in, 1u, out, destinationStep, width, matrix,
                                                                       white);
                }
            }
            return Status::success();
        });
    }
    return parallelFor(execution, height, rowCost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto [in, out] = rowOf(y);
            convertRowPixels<SampleT>(plan, {in[0], in[1], in[2], nullptr}, 1u, {out[0], out[1], out[2], nullptr},
                                      destinationStep, width, nullptr);
        }
        return Status::success();
    });
}

} // namespace

namespace color_detail
{

Status convertImageColorFromPlanes(const ExecutionContext& execution, const ColorConversionPlan& plan,
                                   SampleType sampleType, const std::array<const void*, 3u>& planes,
                                   const ImageLayout& planeLayout, void* destination,
                                   const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "an operation that holds nothing converts nothing",
                                            .operation = kImageOperation,
                                            .domain = ErrorDomain::Algorithms});
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return colorFailure(kImageOperation, ErrorCode::IncompatibleArguments,
                            "a colour transform is nonlinear, so it runs on a Float32 or Float64 working image",
                            "sampleType");
    }
    if (planes[0] == nullptr || planes[1] == nullptr || planes[2] == nullptr || destination == nullptr)
    {
        return colorFailure(kImageOperation, ErrorCode::InvalidArgument,
                            "this argument does not describe samples this engine can convert",
                            destination == nullptr ? "destination" : "planes");
    }
    const std::uint64_t elementSize = sampleTypeSizeBytes(sampleType);
    if (planeLayout.elementSizeBytes() != elementSize || planeLayout.samplesPerPixel() != 1u ||
        planeLayout.extent().channelCount() != 1u)
    {
        return colorFailure(kImageOperation, ErrorCode::InvalidArgument,
                            "each source plane is one channel of the stated representation", "planeLayout");
    }
    if (destinationLayout.elementSizeBytes() != elementSize)
    {
        return colorFailure(kImageOperation, ErrorCode::InvalidArgument,
                            "this layout does not describe the stated representation", "destinationLayout");
    }
    if (plan.sourceChannelCount() != 3u || plan.destinationChannelCount() != 3u ||
        destinationLayout.extent().channelCount() != 3u)
    {
        return colorFailure(kImageOperation, ErrorCode::IncompatibleArguments,
                            "three source planes convert into three destination channels", "plan");
    }
    if (planeLayout.extent().width() != destinationLayout.extent().width() ||
        planeLayout.extent().height() != destinationLayout.extent().height())
    {
        return colorFailure(kImageOperation, ErrorCode::IncompatibleArguments,
                            "a colour transform writes the picture it was given, so both sides are the same width "
                            "and height",
                            "destinationLayout");
    }
    // A plane may be the destination's own channel of the same index, each pixel read before it is written; any other
    // sharing of memory would make what a sample ends up as depend on the order rows are scheduled in.
    auto* const destinationBytes = static_cast<std::uint8_t*>(destination);
    std::array<const std::uint8_t*, 3u> sources{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        sources[channel] = static_cast<const std::uint8_t*>(planes[channel]);
        const bool aliased =
            sources[channel] == destinationBytes + channelRowByteOffset(destinationLayout, channel, 0u) &&
            destinationLayout.samplesPerPixel() == 1u &&
            destinationLayout.rowStrideBytes() == planeLayout.rowStrideBytes();
        if (!aliased && regionsOverlap(sources[channel], planeLayout.totalBytes(), destinationBytes,
                                       destinationLayout.totalBytes()))
        {
            return colorFailure(kImageOperation, ErrorCode::IncompatibleArguments,
                                "a source plane partly shares memory with the destination", "planes");
        }
    }
    if (sampleType == SampleType::Float32)
    {
        return convertPlanesTyped<float>(execution, plan, sources, planeLayout, destinationBytes, destinationLayout);
    }
    return convertPlanesTyped<double>(execution, plan, sources, planeLayout, destinationBytes, destinationLayout);
}

} // namespace color_detail

const char* colorEncodingErrorName(ColorEncodingError error) noexcept
{
    switch (error)
    {
    case ColorEncodingError::None:
        return "None";
    case ColorEncodingError::UnknownChannelSchema:
        return "UnknownChannelSchema";
    case ColorEncodingError::UnsupportedChannelSchema:
        return "UnsupportedChannelSchema";
    case ColorEncodingError::UnknownRgbPrimaries:
        return "UnknownRgbPrimaries";
    case ColorEncodingError::UnknownTransferFunction:
        return "UnknownTransferFunction";
    case ColorEncodingError::UnknownWhitePoint:
        return "UnknownWhitePoint";
    case ColorEncodingError::UnknownPerceptualEncoding:
        return "UnknownPerceptualEncoding";
    case ColorEncodingError::UnknownYCbCrVariant:
        return "UnknownYCbCrVariant";
    case ColorEncodingError::UnknownHueConvention:
        return "UnknownHueConvention";
    case ColorEncodingError::TransferGammaMissing:
        return "TransferGammaMissing";
    case ColorEncodingError::TransferGammaNotAllowed:
        return "TransferGammaNotAllowed";
    case ColorEncodingError::TransferGammaNotPositive:
        return "TransferGammaNotPositive";
    case ColorEncodingError::PrimariesNotAllowed:
        return "PrimariesNotAllowed";
    case ColorEncodingError::TransferNotAllowed:
        return "TransferNotAllowed";
    case ColorEncodingError::WhitePointDisagreesWithPrimaries:
        return "WhitePointDisagreesWithPrimaries";
    case ColorEncodingError::PerceptualEncodingNotAllowed:
        return "PerceptualEncodingNotAllowed";
    case ColorEncodingError::YCbCrVariantNotAllowed:
        return "YCbCrVariantNotAllowed";
    case ColorEncodingError::HueConventionNotAllowed:
        return "HueConventionNotAllowed";
    }
    return "UnknownColorEncodingError";
}

ColorEncodingError checkColorEncoding(const ColorEncoding& encoding) noexcept
{
    if (!isChannelSchema(encoding.schema))
    {
        return ColorEncodingError::UnknownChannelSchema;
    }
    if (!isTransformableSchema(encoding.schema))
    {
        return ColorEncodingError::UnsupportedChannelSchema;
    }
    if (!isRgbPrimaries(encoding.primaries))
    {
        return ColorEncodingError::UnknownRgbPrimaries;
    }
    if (!isColorTransferFunction(encoding.transfer))
    {
        return ColorEncodingError::UnknownTransferFunction;
    }
    if (!isChromaticWhitePoint(encoding.whitePoint))
    {
        return ColorEncodingError::UnknownWhitePoint;
    }
    if (!isPerceptualEncoding(encoding.perceptual))
    {
        return ColorEncodingError::UnknownPerceptualEncoding;
    }
    if (!isYCbCrVariant(encoding.yCbCr))
    {
        return ColorEncodingError::UnknownYCbCrVariant;
    }
    if (!isHueConvention(encoding.hue))
    {
        return ColorEncodingError::UnknownHueConvention;
    }

    if (encoding.transfer == ColorTransferFunction::PureGamma)
    {
        if (!encoding.transferGamma.has_value())
        {
            return ColorEncodingError::TransferGammaMissing;
        }
        if (!std::isfinite(*encoding.transferGamma) || *encoding.transferGamma <= 0.0)
        {
            return ColorEncodingError::TransferGammaNotPositive;
        }
    }
    else if (encoding.transferGamma.has_value())
    {
        return ColorEncodingError::TransferGammaNotAllowed;
    }

    const ColorFamily family = familyOf(encoding.schema);
    if (family == ColorFamily::Rgb)
    {
        if (encoding.whitePoint != whitePointOfPrimaries(encoding.primaries))
        {
            return ColorEncodingError::WhitePointDisagreesWithPrimaries;
        }
    }
    else
    {
        if (encoding.primaries != RgbPrimaries::Srgb)
        {
            return ColorEncodingError::PrimariesNotAllowed;
        }
        if (encoding.transfer != ColorTransferFunction::Linear)
        {
            return ColorEncodingError::TransferNotAllowed;
        }
    }

    if (family != ColorFamily::Perceptual && encoding.perceptual != PerceptualEncoding::NaturalUnits)
    {
        return ColorEncodingError::PerceptualEncodingNotAllowed;
    }
    if (encoding.schema != ChannelSchema::YCbCr && encoding.yCbCr != YCbCrVariant::Rec601FullRange)
    {
        return ColorEncodingError::YCbCrVariantNotAllowed;
    }
    const bool cylindrical = encoding.schema == ChannelSchema::Hsl || encoding.schema == ChannelSchema::Hsv ||
                             encoding.schema == ChannelSchema::Hsi;
    if (!cylindrical && encoding.hue != HueConvention::TurnFraction)
    {
        return ColorEncodingError::HueConventionNotAllowed;
    }
    return ColorEncodingError::None;
}

std::array<double, 3u> whitePointTristimulus(ChromaticWhitePoint whitePoint) noexcept
{
    return standardWhite(whitePoint);
}

std::optional<std::uint32_t> channelCountOfEncoding(const ColorEncoding& encoding) noexcept
{
    if (checkColorEncoding(encoding) != ColorEncodingError::None)
    {
        return std::nullopt;
    }
    return channelCountOf(encoding.schema);
}

std::optional<ColorSpace> colorSpaceOfEncoding(const ColorEncoding& encoding) noexcept
{
    if (checkColorEncoding(encoding) != ColorEncodingError::None)
    {
        return std::nullopt;
    }
    switch (familyOf(encoding.schema))
    {
    case ColorFamily::Xyz:
        return ColorSpace::CieXyz;
    case ColorFamily::Perceptual:
        return ColorSpace::CieLab;
    case ColorFamily::Rgb:
        break;
    }
    return encoding.transfer == ColorTransferFunction::Linear ? ColorSpace::LinearRgb : ColorSpace::DisplayRgb;
}

std::optional<NumericDomain> numericDomainOfEncoding(const ColorEncoding& encoding) noexcept
{
    const std::optional<ColorSpace> space = colorSpaceOfEncoding(encoding);
    if (!space.has_value())
    {
        return std::nullopt;
    }
    return *space == ColorSpace::LinearRgb || *space == ColorSpace::CieXyz ? NumericDomain::NormalizedLinear
                                                                           : NumericDomain::EncodedNonlinear;
}

ColorMatrix3 multiplyColorMatrices(const ColorMatrix3& left, const ColorMatrix3& right) noexcept
{
    ColorMatrix3 product{};
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            double sum = 0.0;
            for (std::size_t inner = 0u; inner < 3u; ++inner)
            {
                sum += left.coefficients[row * 3u + inner] * right.coefficients[inner * 3u + column];
            }
            product.coefficients[row * 3u + column] = sum;
        }
    }
    return product;
}

std::array<double, 3u> applyColorMatrix(const ColorMatrix3& matrix, const std::array<double, 3u>& channels) noexcept
{
    return std::array<double, 3u>{matrix.coefficients[0] * channels[0] + matrix.coefficients[1] * channels[1] +
                                      matrix.coefficients[2] * channels[2],
                                  matrix.coefficients[3] * channels[0] + matrix.coefficients[4] * channels[1] +
                                      matrix.coefficients[5] * channels[2],
                                  matrix.coefficients[6] * channels[0] + matrix.coefficients[7] * channels[1] +
                                      matrix.coefficients[8] * channels[2]};
}

Result<ColorMatrix3> invertColorMatrix(const ColorMatrix3& matrix)
{
    for (const double coefficient : matrix.coefficients)
    {
        if (!std::isfinite(coefficient))
        {
            return colorFailure("an::invertColorMatrix", ErrorCode::InvalidArgument,
                                "a colour matrix coefficient is not a finite number", "matrix");
        }
    }
    const std::array<double, 9u>& m = matrix.coefficients;
    const double determinant = determinantOf(m);
    if (determinant == 0.0 || !std::isfinite(determinant))
    {
        return colorFailure("an::invertColorMatrix", ErrorCode::InvalidArgument,
                            "a singular colour matrix has no inverse", "matrix");
    }
    ColorMatrix3 inverse{};
    inverse.coefficients[0] = (m[4] * m[8] - m[5] * m[7]) / determinant;
    inverse.coefficients[1] = (m[2] * m[7] - m[1] * m[8]) / determinant;
    inverse.coefficients[2] = (m[1] * m[5] - m[2] * m[4]) / determinant;
    inverse.coefficients[3] = (m[5] * m[6] - m[3] * m[8]) / determinant;
    inverse.coefficients[4] = (m[0] * m[8] - m[2] * m[6]) / determinant;
    inverse.coefficients[5] = (m[2] * m[3] - m[0] * m[5]) / determinant;
    inverse.coefficients[6] = (m[3] * m[7] - m[4] * m[6]) / determinant;
    inverse.coefficients[7] = (m[1] * m[6] - m[0] * m[7]) / determinant;
    inverse.coefficients[8] = (m[0] * m[4] - m[1] * m[3]) / determinant;
    return inverse;
}

Result<ColorMatrix3> rgbToXyzMatrix(RgbPrimaries primaries)
{
    if (!isRgbPrimaries(primaries))
    {
        return colorFailure("an::rgbToXyzMatrix", ErrorCode::InvalidArgument,
                            "these are not primaries this build knows", "primaries");
    }
    const PrimaryChromaticities chromaticities = chromaticitiesOf(primaries);
    const std::array<double, 3u> red = tristimulusOf(chromaticities.red);
    const std::array<double, 3u> green = tristimulusOf(chromaticities.green);
    const std::array<double, 3u> blue = tristimulusOf(chromaticities.blue);

    const ColorMatrix3 unscaled{{red[0], green[0], blue[0], red[1], green[1], blue[1], red[2], green[2], blue[2]}};
    const Result<ColorMatrix3> inverse = invertColorMatrix(unscaled);
    if (!inverse.hasValue())
    {
        return inverse.status();
    }
    const std::array<double, 3u> white = whitePointTristimulus(whitePointOfPrimaries(primaries));
    const std::array<double, 3u> scale = applyColorMatrix(*inverse, white);

    ColorMatrix3 matrix{};
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            matrix.coefficients[row * 3u + column] = unscaled.coefficients[row * 3u + column] * scale[column];
        }
    }
    return matrix;
}

Result<ColorMatrix3> xyzToRgbMatrix(RgbPrimaries primaries)
{
    if (!isRgbPrimaries(primaries))
        return colorFailure("an::xyzToRgbMatrix", ErrorCode::InvalidArgument,
                            "these are not primaries this build knows", "primaries");
    const Result<ColorMatrix3> forward = rgbToXyzMatrix(primaries);
    if (!forward.hasValue())
    {
        return forward.status();
    }
    return invertColorMatrix(*forward);
}

Result<ColorMatrix3> chromaticAdaptationMatrix(ChromaticWhitePoint from, ChromaticWhitePoint to)
{
    if (!isChromaticWhitePoint(from) || !isChromaticWhitePoint(to))
    {
        return colorFailure("an::chromaticAdaptationMatrix", ErrorCode::InvalidArgument,
                            "this is not a white point this build knows", "whitePoint");
    }
    if (from == to)
    {
        return ColorMatrix3{};
    }
    const Result<ColorMatrix3> inverseCone = invertColorMatrix(kBradford);
    if (!inverseCone.hasValue())
    {
        return inverseCone.status();
    }
    const std::array<double, 3u> sourceCone = applyColorMatrix(kBradford, whitePointTristimulus(from));
    const std::array<double, 3u> destinationCone = applyColorMatrix(kBradford, whitePointTristimulus(to));
    const ColorMatrix3 ratio{{destinationCone[0] / sourceCone[0], 0.0, 0.0, 0.0, destinationCone[1] / sourceCone[1],
                              0.0, 0.0, 0.0, destinationCone[2] / sourceCone[2]}};
    return multiplyColorMatrices(*inverseCone, multiplyColorMatrices(ratio, kBradford));
}

const char* colorStageName(ColorStage stage) noexcept
{
    switch (stage)
    {
    case ColorStage::DecodeTransfer:
        return "DecodeTransfer";
    case ColorStage::EncodeTransfer:
        return "EncodeTransfer";
    case ColorStage::LinearRgbToXyz:
        return "LinearRgbToXyz";
    case ColorStage::XyzToLinearRgb:
        return "XyzToLinearRgb";
    case ColorStage::AdaptWhitePoint:
        return "AdaptWhitePoint";
    case ColorStage::XyzToLab:
        return "XyzToLab";
    case ColorStage::LabToXyz:
        return "LabToXyz";
    case ColorStage::LabToLch:
        return "LabToLch";
    case ColorStage::LchToLab:
        return "LchToLab";
    case ColorStage::PackPerceptual:
        return "PackPerceptual";
    case ColorStage::UnpackPerceptual:
        return "UnpackPerceptual";
    case ColorStage::RgbToHsl:
        return "RgbToHsl";
    case ColorStage::HslToRgb:
        return "HslToRgb";
    case ColorStage::RgbToHsv:
        return "RgbToHsv";
    case ColorStage::HsvToRgb:
        return "HsvToRgb";
    case ColorStage::RgbToHsi:
        return "RgbToHsi";
    case ColorStage::HsiToRgb:
        return "HsiToRgb";
    case ColorStage::RgbToYCbCr:
        return "RgbToYCbCr";
    case ColorStage::YCbCrToRgb:
        return "YCbCrToRgb";
    }
    return "UnknownColorStage";
}

ColorConversionPlan::ColorConversionPlan(const ColorEncoding& source, const ColorEncoding& destination,
                                         std::span<const ColorStage> stages, const ColorMatrix3& toXyz,
                                         const ColorMatrix3& adapt, const ColorMatrix3& fromXyz,
                                         std::optional<ColorMatrix3> linearRgb) noexcept
    : m_source(source), m_destination(destination), m_toXyz(toXyz), m_adapt(adapt), m_fromXyz(fromXyz),
      m_linearRgb(linearRgb)
{
    m_stageCount = static_cast<std::uint32_t>((std::min)(stages.size(), static_cast<std::size_t>(kMaxColorStages)));
    for (std::uint32_t index = 0u; index < m_stageCount; ++index)
    {
        m_stages[index] = stages[index];
    }
}

const ColorEncoding& ColorConversionPlan::source() const noexcept
{
    return m_source;
}

const ColorEncoding& ColorConversionPlan::destination() const noexcept
{
    return m_destination;
}

std::span<const ColorStage> ColorConversionPlan::stages() const noexcept
{
    return std::span<const ColorStage>(m_stages.data(), m_stageCount);
}

bool ColorConversionPlan::isIdentity() const noexcept
{
    return m_stageCount == 0u;
}

std::uint32_t ColorConversionPlan::sourceChannelCount() const noexcept
{
    return channelCountOf(m_source.schema).value_or(3u);
}

std::uint32_t ColorConversionPlan::destinationChannelCount() const noexcept
{
    return channelCountOf(m_destination.schema).value_or(3u);
}

bool ColorConversionPlan::passesAlphaThrough() const noexcept
{
    return m_source.schema == ChannelSchema::Rgba && m_destination.schema == ChannelSchema::Rgba;
}

std::optional<ColorMatrix3> ColorConversionPlan::linearRgbMatrix() const noexcept
{
    return m_linearRgb;
}

bool ColorConversionPlan::isPureChannelMatrix() const noexcept
{
    for (std::uint32_t index = 0u; index < m_stageCount; ++index)
    {
        const ColorStage stage = m_stages[index];
        if (stage != ColorStage::LinearRgbToXyz && stage != ColorStage::XyzToLinearRgb &&
            stage != ColorStage::AdaptWhitePoint)
        {
            return false;
        }
    }
    return m_linearRgb.has_value();
}

const ColorMatrix3& ColorConversionPlan::rgbToXyz() const noexcept
{
    return m_toXyz;
}

const ColorMatrix3& ColorConversionPlan::adaptation() const noexcept
{
    return m_adapt;
}

const ColorMatrix3& ColorConversionPlan::xyzToRgb() const noexcept
{
    return m_fromXyz;
}

RealtimePreviewColorConversionPlan::RealtimePreviewColorConversionPlan(ColorConversionPlan productionPlan) noexcept
    : m_productionPlan(std::move(productionPlan))
{
    const double oneThird = fillPreviewPowerTable(m_oneThird, 1.0 / 3.0);
    const double oneOverTwoPointFour = fillPreviewPowerTable(m_oneOverTwoPointFour, 1.0 / 2.4);
    const double twoPointFour = fillPreviewPowerTable(m_twoPointFour, 2.4);
    m_maximumAbsolutePowerError = (std::max)({oneThird, oneOverTwoPointFour, twoPointFour});
}

const ColorConversionPlan& RealtimePreviewColorConversionPlan::productionPlan() const noexcept
{
    return m_productionPlan;
}

double RealtimePreviewColorConversionPlan::powerOneThird(double base) const noexcept
{
    return previewPower(m_oneThird, 1.0 / 3.0, base);
}

double RealtimePreviewColorConversionPlan::powerOneOverTwoPointFour(double base) const noexcept
{
    return previewPower(m_oneOverTwoPointFour, 1.0 / 2.4, base);
}

double RealtimePreviewColorConversionPlan::powerTwoPointFour(double base) const noexcept
{
    return previewPower(m_twoPointFour, 2.4, base);
}

double RealtimePreviewColorConversionPlan::maximumAbsolutePowerError() const noexcept
{
    return m_maximumAbsolutePowerError;
}

Result<ColorConversionPlan> planColorConversion(const ColorEncoding& source, const ColorEncoding& destination)
{
    const ColorEncodingError sourceReason = checkColorEncoding(source);
    if (sourceReason != ColorEncodingError::None)
    {
        return encodingFailure(sourceReason, "source");
    }
    const ColorEncodingError destinationReason = checkColorEncoding(destination);
    if (destinationReason != ColorEncodingError::None)
    {
        return encodingFailure(destinationReason, "destination");
    }
    if (schemaHasAlpha(source.schema) != schemaHasAlpha(destination.schema))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "a colour transform neither invents nor discards opacity, so both ends carry an "
                                    "alpha channel or neither does",
                         .operation = kPlanOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "destination")}});
    }

    const ColorFamily sourceFamily = familyOf(source.schema);
    const ColorFamily destinationFamily = familyOf(destination.schema);
    const ChromaticWhitePoint sourceWhite = encodingWhitePoint(source);
    const ChromaticWhitePoint destinationWhite = encodingWhitePoint(destination);

    ColorMatrix3 toXyz{};
    ColorMatrix3 adapt{};
    ColorMatrix3 fromXyz{};
    if (sourceFamily == ColorFamily::Rgb)
    {
        const Result<ColorMatrix3> made = rgbToXyzMatrix(source.primaries);
        if (!made.hasValue())
        {
            return made.status();
        }
        toXyz = *made;
    }
    if (destinationFamily == ColorFamily::Rgb)
    {
        const Result<ColorMatrix3> made = xyzToRgbMatrix(destination.primaries);
        if (!made.hasValue())
        {
            return made.status();
        }
        fromXyz = *made;
    }
    if (sourceWhite != destinationWhite)
    {
        const Result<ColorMatrix3> made = chromaticAdaptationMatrix(sourceWhite, destinationWhite);
        if (!made.hasValue())
        {
            return made.status();
        }
        adapt = *made;
    }

    std::array<ColorStage, kMaxColorStages> stages{};
    std::uint32_t stageCount = 0u;
    const auto add = [&stages, &stageCount](ColorStage stage) noexcept {
        stages[stageCount] = stage;
        ++stageCount;
    };

    // The source side, down to its family's canonical value: RGB in the source's own transfer,
    // tristimulus values under the source white, or Lab in CIE units under it.
    if (sourceFamily == ColorFamily::Perceptual)
    {
        if (source.perceptual == PerceptualEncoding::UnitPacked)
        {
            add(ColorStage::UnpackPerceptual);
        }
        if (source.schema == ChannelSchema::Lch)
        {
            add(ColorStage::LchToLab);
        }
    }
    else if (sourceFamily == ColorFamily::Rgb)
    {
        switch (source.schema)
        {
        case ChannelSchema::Hsl:
            add(ColorStage::HslToRgb);
            break;
        case ChannelSchema::Hsv:
            add(ColorStage::HsvToRgb);
            break;
        case ChannelSchema::Hsi:
            add(ColorStage::HsiToRgb);
            break;
        case ChannelSchema::YCbCr:
            add(ColorStage::YCbCrToRgb);
            break;
        default:
            break;
        }
    }

    // Whether the connection space has to be visited at all. Two encodings of one family that agree
    // about the thing the connection space would have resolved are joined directly, which is what
    // keeps a repacking of Lab or a hue-convention change from costing two matrix multiplications
    // and a pair of cube roots.
    const bool stayInRgb = sourceFamily == ColorFamily::Rgb && destinationFamily == ColorFamily::Rgb &&
                           source.primaries == destination.primaries;
    const bool stayInPerceptual = sourceFamily == ColorFamily::Perceptual &&
                                  destinationFamily == ColorFamily::Perceptual && sourceWhite == destinationWhite;
    const bool stayInXyz = sourceFamily == ColorFamily::Xyz && destinationFamily == ColorFamily::Xyz;

    if (stayInRgb)
    {
        if (source.transfer != destination.transfer || source.transferGamma != destination.transferGamma)
        {
            if (source.transfer != ColorTransferFunction::Linear)
            {
                add(ColorStage::DecodeTransfer);
            }
            if (destination.transfer != ColorTransferFunction::Linear)
            {
                add(ColorStage::EncodeTransfer);
            }
        }
    }
    else if (stayInXyz)
    {
        if (sourceWhite != destinationWhite)
        {
            add(ColorStage::AdaptWhitePoint);
        }
    }
    else if (!stayInPerceptual)
    {
        if (sourceFamily == ColorFamily::Rgb)
        {
            if (source.transfer != ColorTransferFunction::Linear)
            {
                add(ColorStage::DecodeTransfer);
            }
            add(ColorStage::LinearRgbToXyz);
        }
        else if (sourceFamily == ColorFamily::Perceptual)
        {
            add(ColorStage::LabToXyz);
        }
        if (sourceWhite != destinationWhite)
        {
            add(ColorStage::AdaptWhitePoint);
        }
        if (destinationFamily == ColorFamily::Rgb)
        {
            add(ColorStage::XyzToLinearRgb);
            if (destination.transfer != ColorTransferFunction::Linear)
            {
                add(ColorStage::EncodeTransfer);
            }
        }
        else if (destinationFamily == ColorFamily::Perceptual)
        {
            add(ColorStage::XyzToLab);
        }
    }

    // The destination side, from the canonical value to what the destination schema holds.
    if (destinationFamily == ColorFamily::Perceptual)
    {
        if (destination.schema == ChannelSchema::Lch)
        {
            add(ColorStage::LabToLch);
        }
        if (destination.perceptual == PerceptualEncoding::UnitPacked)
        {
            add(ColorStage::PackPerceptual);
        }
    }
    else if (destinationFamily == ColorFamily::Rgb)
    {
        switch (destination.schema)
        {
        case ChannelSchema::Hsl:
            add(ColorStage::RgbToHsl);
            break;
        case ChannelSchema::Hsv:
            add(ColorStage::RgbToHsv);
            break;
        case ChannelSchema::Hsi:
            add(ColorStage::RgbToHsi);
            break;
        case ChannelSchema::YCbCr:
            add(ColorStage::RgbToYCbCr);
            break;
        default:
            break;
        }
    }

    // Present only for the two schemas that *are* red, green and blue. A cylindrical or luma-chroma
    // schema shares their family because it is computed from them, but a caller cannot execute a
    // conversion into one by multiplying a matrix.
    const bool rgbSchemas = (source.schema == ChannelSchema::Rgb || source.schema == ChannelSchema::Rgba) &&
                            (destination.schema == ChannelSchema::Rgb || destination.schema == ChannelSchema::Rgba);
    std::optional<ColorMatrix3> linearRgb{};
    if (rgbSchemas)
    {
        linearRgb = stayInRgb ? ColorMatrix3{} : multiplyColorMatrices(fromXyz, multiplyColorMatrices(adapt, toXyz));
    }

    return ColorConversionPlan{
        source, destination, std::span<const ColorStage>(stages.data(), stageCount), toXyz, adapt, fromXyz, linearRgb};
}

Result<RealtimePreviewColorConversionPlan> planRealtimePreviewColorConversion(const ColorEncoding& source,
                                                                              const ColorEncoding& destination)
{
    Result<ColorConversionPlan> production = planColorConversion(source, destination);
    if (!production.hasValue())
    {
        return production.status();
    }
    return RealtimePreviewColorConversionPlan{std::move(*production)};
}

Status convertColorPixel(const ColorConversionPlan& plan, std::span<const double> source, std::span<double> destination)
{
    if (source.size() != static_cast<std::size_t>(plan.sourceChannelCount()))
    {
        return colorFailure(kPixelOperation, ErrorCode::InvalidArgument,
                            "a pixel carries exactly as many values as its encoding has channels", "source");
    }
    if (destination.size() != static_cast<std::size_t>(plan.destinationChannelCount()))
    {
        return colorFailure(kPixelOperation, ErrorCode::InvalidArgument,
                            "a pixel carries exactly as many values as its encoding has channels", "destination");
    }
    std::array<double, 3u> channels{source[0], source[1], source[2]};
    const double alpha = source.size() == 4u ? source[3] : 0.0;
    for (const ColorStage stage : plan.stages())
    {
        runStage(stage, plan, channels);
    }
    destination[0] = channels[0];
    destination[1] = channels[1];
    destination[2] = channels[2];
    if (destination.size() == 4u)
    {
        destination[3] = alpha;
    }
    return Status::success();
}

Status convertRealtimePreviewColorPixel(const RealtimePreviewColorConversionPlan& plan, std::span<const double> source,
                                        std::span<double> destination)
{
    const ColorConversionPlan& production = plan.productionPlan();
    if (source.size() != static_cast<std::size_t>(production.sourceChannelCount()))
    {
        return colorFailure(kPreviewPixelOperation, ErrorCode::InvalidArgument,
                            "a pixel carries exactly as many values as its encoding has channels", "source");
    }
    if (destination.size() != static_cast<std::size_t>(production.destinationChannelCount()))
    {
        return colorFailure(kPreviewPixelOperation, ErrorCode::InvalidArgument,
                            "a pixel carries exactly as many values as its encoding has channels", "destination");
    }
    std::array<double, 3u> channels{source[0], source[1], source[2]};
    const double alpha = source.size() == 4u ? source[3] : 0.0;
    for (const ColorStage stage : production.stages())
    {
        runStage(stage, production, channels, &plan);
    }
    destination[0] = channels[0];
    destination[1] = channels[1];
    destination[2] = channels[2];
    if (destination.size() == 4u)
    {
        destination[3] = alpha;
    }
    return Status::success();
}

namespace
{

[[nodiscard]] Status convertImageColorImplementation(const ExecutionContext& execution, const ColorConversionPlan& plan,
                                                     const RealtimePreviewColorConversionPlan* preview,
                                                     SampleType sampleType, const void* source,
                                                     const ImageLayout& sourceLayout, void* destination,
                                                     const ImageLayout& destinationLayout, const char* operation)
{
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "an operation that holds nothing converts nothing",
                                            .operation = operation,
                                            .domain = ErrorDomain::Algorithms});
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return colorFailure(operation, ErrorCode::IncompatibleArguments,
                            "a colour transform is nonlinear, so it runs on a Float32 or Float64 working image; "
                            "integer storage converts once through the conversion surface first",
                            "sampleType");
    }
    if (source == nullptr || destination == nullptr)
    {
        return colorFailure(operation, ErrorCode::InvalidArgument,
                            "this argument does not describe samples this engine can convert",
                            source == nullptr ? "source" : "destination");
    }
    if (sampleTypeSizeBytes(sampleType) != sourceLayout.elementSizeBytes())
    {
        return colorFailure(operation, ErrorCode::InvalidArgument,
                            "this layout does not describe the stated representation", "sourceLayout");
    }
    if (sampleTypeSizeBytes(sampleType) != destinationLayout.elementSizeBytes())
    {
        return colorFailure(operation, ErrorCode::InvalidArgument,
                            "this layout does not describe the stated representation", "destinationLayout");
    }
    if (sourceLayout.extent().channelCount() != plan.sourceChannelCount())
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{
                .message = "the source does not have as many channels as the plan's source encoding",
                .operation = operation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofText(ContextKey::Parameter, "sourceLayout"),
                            StatusContextField::ofCount(ContextKey::Requested, plan.sourceChannelCount()),
                            StatusContextField::ofCount(ContextKey::Available, sourceLayout.extent().channelCount())}});
    }
    if (destinationLayout.extent().channelCount() != plan.destinationChannelCount())
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the destination does not have as many channels as the plan's destination encoding",
                         .operation = operation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "destinationLayout"),
                                     StatusContextField::ofCount(ContextKey::Requested, plan.destinationChannelCount()),
                                     StatusContextField::ofCount(ContextKey::Available,
                                                                 destinationLayout.extent().channelCount())}});
    }
    if (sourceLayout.extent().width() != destinationLayout.extent().width() ||
        sourceLayout.extent().height() != destinationLayout.extent().height())
    {
        return colorFailure(operation, ErrorCode::IncompatibleArguments,
                            "a colour transform writes the picture it was given, so both sides are the same width "
                            "and height",
                            "destinationLayout");
    }

    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const bool exactlyAliased = source == destination && sourceLayout == destinationLayout;
    if (!exactlyAliased &&
        regionsOverlap(sourceBytes, sourceLayout.totalBytes(), destinationBytes, destinationLayout.totalBytes()))
    {
        return colorFailure(operation, ErrorCode::IncompatibleArguments,
                            "a destination that partly shares memory with the source would make what a sample ends "
                            "up as depend on the order the rows happened to be scheduled in",
                            "destination");
    }

    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t channelCount = plan.sourceChannelCount();
    const std::uint64_t elementSize = static_cast<std::uint64_t>(sourceLayout.elementSizeBytes());
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channelCount * elementSize * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) *
                                             (static_cast<std::uint64_t>(plan.stages().size()) + 1u) * channelCount};
    const bool directRgbToUnitLab = isDirectRgbToUnitLabPlan(plan);
    const bool directSrgbToUnitLab = directRgbToUnitLab && plan.source().transfer == ColorTransferFunction::Srgb;
    const std::array<double, 9u> normalizedLabMatrix =
        directSrgbToUnitLab ? directLabMatrix(plan) : std::array<double, 9u>{};

    if (directSrgbToUnitLab && isTightPlanarColorLayout(sourceLayout, elementSize) &&
        isTightPlanarColorLayout(destinationLayout, elementSize))
    {
        const TaskCost pixelCost{.bytesPerItem = channelCount * elementSize * 2u,
                                 .operationsPerItem =
                                     (static_cast<std::uint64_t>(plan.stages().size()) + 1u) * channelCount};
        if (sampleType == SampleType::Float32)
        {
            return convertTightDirectSrgbToUnitLab<float>(execution, sourceBytes, sourceLayout, destinationBytes,
                                                          destinationLayout, normalizedLabMatrix, preview, pixelCost);
        }
        return convertTightDirectSrgbToUnitLab<double>(execution, sourceBytes, sourceLayout, destinationBytes,
                                                       destinationLayout, normalizedLabMatrix, preview, pixelCost);
    }

    // The inverse of the common shape above: unit-packed Lab back to RGB, linear or sRGB-encoded. The direct loops
    // replay the stage walker's expressions in its order, so they are bit for bit what it computes; they exist so
    // that the loop carries no stage switch and the compiler can vectorize its powers.
    if (preview == nullptr && isDirectUnitLabToRgbPlan(plan))
    {
        const TaskCost pixelCost{.bytesPerItem = channelCount * elementSize * 2u,
                                 .operationsPerItem =
                                     (static_cast<std::uint64_t>(plan.stages().size()) + 1u) * channelCount};
        if (sampleType == SampleType::Float32)
        {
            return convertDirectUnitLabToRgb<float>(execution, plan, sourceBytes, sourceLayout, destinationBytes,
                                                    destinationLayout, pixelCost);
        }
        return convertDirectUnitLabToRgb<double>(execution, plan, sourceBytes, sourceLayout, destinationBytes,
                                                 destinationLayout, pixelCost);
    }

    return parallelFor(
        execution, static_cast<std::uint64_t>(sourceLayout.extent().height()), cost, [&](TaskRange range) -> Status {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                if (sampleType == SampleType::Float32)
                {
                    if (directSrgbToUnitLab)
                    {
                        if (preview == nullptr)
                        {
                            convertDirectSrgbToUnitLabRow<float, false>(sourceBytes, sourceLayout, destinationBytes,
                                                                        destinationLayout, y, normalizedLabMatrix,
                                                                        nullptr);
                        }
                        else
                        {
                            convertDirectSrgbToUnitLabRow<float, true>(sourceBytes, sourceLayout, destinationBytes,
                                                                       destinationLayout, y, normalizedLabMatrix,
                                                                       preview);
                        }
                    }
                    else if (directRgbToUnitLab)
                    {
                        convertDirectRgbToUnitLabRow<float>(plan, sourceBytes, sourceLayout, destinationBytes,
                                                            destinationLayout, y, preview);
                    }
                    else
                    {
                        convertRow<float>(plan, sourceBytes, sourceLayout, destinationBytes, destinationLayout, y,
                                          preview);
                    }
                }
                else
                {
                    if (directSrgbToUnitLab)
                    {
                        if (preview == nullptr)
                        {
                            convertDirectSrgbToUnitLabRow<double, false>(sourceBytes, sourceLayout, destinationBytes,
                                                                         destinationLayout, y, normalizedLabMatrix,
                                                                         nullptr);
                        }
                        else
                        {
                            convertDirectSrgbToUnitLabRow<double, true>(sourceBytes, sourceLayout, destinationBytes,
                                                                        destinationLayout, y, normalizedLabMatrix,
                                                                        preview);
                        }
                    }
                    else if (directRgbToUnitLab)
                    {
                        convertDirectRgbToUnitLabRow<double>(plan, sourceBytes, sourceLayout, destinationBytes,
                                                             destinationLayout, y, preview);
                    }
                    else
                    {
                        convertRow<double>(plan, sourceBytes, sourceLayout, destinationBytes, destinationLayout, y,
                                           preview);
                    }
                }
            }
            return Status::success();
        });
}

} // namespace

Status convertImageColor(const ExecutionContext& execution, const ColorConversionPlan& plan, SampleType sampleType,
                         const void* source, const ImageLayout& sourceLayout, void* destination,
                         const ImageLayout& destinationLayout)
{
    return convertImageColorImplementation(execution, plan, nullptr, sampleType, source, sourceLayout, destination,
                                           destinationLayout, kImageOperation);
}

Status convertRealtimePreviewImageColor(const ExecutionContext& execution,
                                        const RealtimePreviewColorConversionPlan& plan, SampleType sampleType,
                                        const void* source, const ImageLayout& sourceLayout, void* destination,
                                        const ImageLayout& destinationLayout)
{
    return convertImageColorImplementation(execution, plan.productionPlan(), &plan, sampleType, source, sourceLayout,
                                           destination, destinationLayout, kPreviewImageOperation);
}

Result<ImageDescriptor> colorTransformedDescriptor(const ImageDescriptor& source, const ColorConversionPlan& plan)
{
    const std::optional<ColorSpace> sourceSpace = colorSpaceOfEncoding(plan.source());
    const std::optional<NumericDomain> sourceDomain = numericDomainOfEncoding(plan.source());
    if (!sourceSpace.has_value() || !sourceDomain.has_value())
    {
        return colorFailure(kDescriptorOperation, ErrorCode::InvalidArgument,
                            "the plan's source encoding describes no image", "plan");
    }
    if (source.channelSchema() != plan.source().schema || source.colorSpace() != *sourceSpace ||
        source.numericDomain() != *sourceDomain)
    {
        return colorFailure(kDescriptorOperation, ErrorCode::IncompatibleArguments,
                            "the image does not describe itself the way the plan's source encoding does, so the "
                            "conversion would be reading values that mean something else",
                            "source");
    }

    const std::optional<ColorSpace> destinationSpace = colorSpaceOfEncoding(plan.destination());
    const std::optional<NumericDomain> destinationDomain = numericDomainOfEncoding(plan.destination());
    if (!destinationSpace.has_value() || !destinationDomain.has_value())
    {
        return colorFailure(kDescriptorOperation, ErrorCode::InvalidArgument,
                            "the plan's destination encoding describes no image", "plan");
    }

    const std::optional<ImageExtent> extent = ImageExtent::create(
        source.extent().width(), source.extent().height(), plan.destinationChannelCount(), defaultEngineLimits());
    if (!extent.has_value())
    {
        return colorFailure(kDescriptorOperation, ErrorCode::InvalidArgument,
                            "the converted image would have a shape no image can have", "source");
    }

    ImageDescriptorRequest request = source.toRequest();
    request.extent = *extent;
    request.channelSchema = plan.destination().schema;
    request.colorSpace = *destinationSpace;
    request.numericDomain = *destinationDomain;

    const std::optional<ImageDescriptor> made = ImageDescriptor::create(request);
    if (!made.has_value())
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = descriptorErrorName(ImageDescriptor::check(request)),
                                            .operation = kDescriptorOperation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "source")}});
    }
    return *made;
}

Result<PointwiseProgram> buildChannelMatrixProgram(const PointwisePlanShape& shape, std::span<const double> rowMajor)
{
    if (shape.sampleType != SampleType::Float32 && shape.sampleType != SampleType::Float64)
    {
        return colorFailure(kMatrixOperation, ErrorCode::IncompatibleArguments,
                            "a channel matrix mixes values in a stated domain, so it runs on a Float32 or named "
                            "Float64 working image",
                            "shape");
    }
    if (!isNumericDomain(shape.numericDomain))
    {
        return colorFailure(kMatrixOperation, ErrorCode::InvalidArgument,
                            "the channel matrix's numeric domain is not known", "shape");
    }
    const std::size_t expected = static_cast<std::size_t>(shape.channelCount) * shape.channelCount;
    if (rowMajor.size() != expected)
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{
                .message = "a channel matrix has one coefficient per pair of channels",
                .operation = kMatrixOperation,
                .domain = ErrorDomain::Algorithms,
                .context = {
                    StatusContextField::ofText(ContextKey::Parameter, "rowMajor"),
                    StatusContextField::ofCount(ContextKey::Requested, static_cast<std::uint64_t>(expected)),
                    StatusContextField::ofCount(ContextKey::Available, static_cast<std::uint64_t>(rowMajor.size()))}});
    }
    for (const double coefficient : rowMajor)
    {
        if (!std::isfinite(coefficient))
        {
            return colorFailure(kMatrixOperation, ErrorCode::InvalidArgument,
                                "a channel matrix coefficient is not a finite number", "rowMajor");
        }
    }

    PointwiseProgramBuilder builder;
    builder.channels(shape.channelCount)
        .sources(1u)
        .readingSamples(shape.sampleType)
        .readingDomain(shape.numericDomain)
        .writingDomain(shape.numericDomain)
        .promising(shape.outputContract)
        .computingIn(shape.precision, shape.precisionJustification);
    if (shape.allowContraction)
    {
        builder.allowingContraction();
    }
    const std::uint32_t table = builder.addTable(rowMajor);
    builder.append(pointwiseChannelMatrix(table));
    return builder.build();
}

Result<PointwiseProgram> buildColorMatrixProgram(const PointwisePlanShape& shape, const ColorMatrix3& matrix)
{
    return buildChannelMatrixProgram(shape, std::span<const double>(matrix.coefficients));
}

// What running one plan does to the image it writes.
//
// The identity is the only case worth separating. A plan with no stages writes the samples it read,
// so declaring that it restated the description would move an image's semantic generation and
// discard its source cards for a copy.
OperationEffect colorConversionEffect(const ColorConversionPlan& plan) noexcept
{
    if (plan.isIdentity())
    {
        return OperationEffect{};
    }

    OperationEffect effect{};
    effect.samples = SampleEffect::Writes;
    effect.description = DescriptionEffect::Restates;
    effect.semantics = SemanticEffect::Restates;
    effect.solution = SolutionEffect::Preserves;
    effect.codecMetadata = CodecMetadataEffect::Drops;
    effect.outputIntent = OutputIntentEffect::Withdraws;
    effect.validity.policy = ValidityPolicy::Preserve;
    return effect;
}
} // namespace an
