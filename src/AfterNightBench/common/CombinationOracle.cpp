// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definitions of the image combination and external-tool hand-off rows.

#include "CombinationOracle.h"

#include "PresentationOracle.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace an::bench
{

namespace
{

using Matrix = std::array<long double, 9u>;
using Triple = std::array<long double, 3u>;

/// The tabulated D65 white.
constexpr Triple kD65{0.95047L, 1.0L, 1.08883L};

[[nodiscard]] Triple multiply(const Matrix& matrix, const Triple& value)
{
    return {matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2],
            matrix[3] * value[0] + matrix[4] * value[1] + matrix[5] * value[2],
            matrix[6] * value[0] + matrix[7] * value[1] + matrix[8] * value[2]};
}

[[nodiscard]] Matrix inverse(const Matrix& m)
{
    const long double a = m[4] * m[8] - m[5] * m[7];
    const long double b = m[5] * m[6] - m[3] * m[8];
    const long double c = m[3] * m[7] - m[4] * m[6];
    const long double determinant = m[0] * a + m[1] * b + m[2] * c;
    return {a / determinant, (m[2] * m[7] - m[1] * m[8]) / determinant, (m[1] * m[5] - m[2] * m[4]) / determinant,
            b / determinant, (m[0] * m[8] - m[2] * m[6]) / determinant, (m[2] * m[3] - m[0] * m[5]) / determinant,
            c / determinant, (m[1] * m[6] - m[0] * m[7]) / determinant, (m[0] * m[4] - m[1] * m[3]) / determinant};
}

/// The sRGB primaries' matrix to XYZ, each primary's chromaticity scaled so the three sum to the white.
[[nodiscard]] Matrix srgbToXyz()
{
    const std::array<std::array<long double, 2u>, 3u> chromaticity{{{0.64L, 0.33L}, {0.30L, 0.60L}, {0.15L, 0.06L}}};
    Matrix primaries{};
    for (std::size_t column = 0u; column < 3u; ++column)
    {
        const long double x = chromaticity[column][0];
        const long double y = chromaticity[column][1];
        primaries[column] = x / y;
        primaries[3u + column] = 1.0L;
        primaries[6u + column] = (1.0L - x - y) / y;
    }
    const Triple scale = multiply(inverse(primaries), kD65);
    Matrix matrix = primaries;
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            matrix[row * 3u + column] *= scale[column];
        }
    }
    return matrix;
}

[[nodiscard]] long double decode(long double value)
{
    const long double magnitude = std::fabs(value);
    const long double linear =
        magnitude <= 0.04045L ? magnitude / 12.92L : std::pow((magnitude + 0.055L) / 1.055L, 2.4L);
    return std::copysign(linear, value);
}

[[nodiscard]] long double encode(long double value)
{
    const long double magnitude = std::fabs(value);
    const long double encoded =
        magnitude <= 0.0031308L ? magnitude * 12.92L : 1.055L * std::pow(magnitude, 1.0L / 2.4L) - 0.055L;
    return std::copysign(encoded, value);
}

constexpr long double kDelta = 6.0L / 29.0L;

[[nodiscard]] long double labForward(long double t)
{
    return t > kDelta * kDelta * kDelta ? std::cbrt(t) : t / (3.0L * kDelta * kDelta) + 4.0L / 29.0L;
}

[[nodiscard]] long double labInverse(long double f)
{
    return f > kDelta ? f * f * f : 3.0L * kDelta * kDelta * (f - 4.0L / 29.0L);
}

const Matrix& toXyz()
{
    static const Matrix matrix = srgbToXyz();
    return matrix;
}

const Matrix& fromXyz()
{
    static const Matrix matrix = inverse(srgbToXyz());
    return matrix;
}

/// CIE L*a*b* of an sRGB-encoded colour, in CIE units.
[[nodiscard]] Triple lab(const Triple& encoded)
{
    const Triple xyz = multiply(toXyz(), Triple{decode(encoded[0]), decode(encoded[1]), decode(encoded[2])});
    const long double fx = labForward(xyz[0] / kD65[0]);
    const long double fy = labForward(xyz[1] / kD65[1]);
    const long double fz = labForward(xyz[2] / kD65[2]);
    return {116.0L * fy - 16.0L, 500.0L * (fx - fy), 200.0L * (fy - fz)};
}

/// The sRGB-encoded colour of CIE L*a*b*, unclamped.
[[nodiscard]] Triple rgbOfLab(const Triple& value)
{
    const long double fy = (value[0] + 16.0L) / 116.0L;
    const Triple xyz{kD65[0] * labInverse(fy + value[1] / 500.0L), kD65[1] * labInverse(fy),
                     kD65[2] * labInverse(fy - value[2] / 200.0L)};
    const Triple linear = multiply(fromXyz(), xyz);
    return {encode(linear[0]), encode(linear[1]), encode(linear[2])};
}

[[nodiscard]] long double unitHeld(long double value)
{
    return value > 0.0L ? (value < 1.0L ? value : 1.0L) : 0.0L;
}

/// The midtones transfer between zero and one.
[[nodiscard]] long double midtones(long double value, long double balance)
{
    const long double x = unitHeld(value);
    if (x <= 0.0L || x >= 1.0L)
    {
        return x;
    }
    return (balance - 1.0L) * x / ((2.0L * balance - 1.0L) * x - balance);
}

/// The HSI conversion whose hue the blend's hue and saturation modes exchange.
void rgbToHsi(long double r, long double g, long double b, long double& h, long double& s, long double& i)
{
    i = (r + g + b) / 3.0L;
    if (i <= 0.0L)
    {
        s = 0.0L;
        h = 0.0L;
        return;
    }
    s = 1.0L - (std::min)(r, (std::min)(g, b)) / i;
    if (s <= 0.0L)
    {
        h = 0.0L;
        return;
    }
    const long double num = 0.5L * ((r - g) + (r - b));
    const long double den = std::sqrt((r - g) * (r - g) + (r - b) * (g - b));
    const long double x = std::clamp(den > 1.0e-12L ? num / (den + 1.0e-12L) : 1.0L, -1.0L, 1.0L);
    const long double theta = std::acos(x);
    h = (b <= g ? theta : 2.0L * std::numbers::pi_v<long double> - theta) / (2.0L * std::numbers::pi_v<long double>);
    if (h >= 1.0L)
    {
        h -= 1.0L;
    }
}

[[nodiscard]] Triple hsiToRgb(long double h, long double s, long double i)
{
    if (s <= 0.0L)
    {
        return {i, i, i};
    }
    h -= std::floor(h);
    const long double degrees = h * 360.0L;
    const long double radian = std::numbers::pi_v<long double> / 180.0L;
    const auto sector = [&](long double angle) {
        return i * (1.0L + (s * std::cos(angle * radian)) / (std::cos((60.0L - angle) * radian) + 1.0e-12L));
    };
    Triple rgb{};
    if (degrees < 120.0L)
    {
        rgb[2] = i * (1.0L - s);
        rgb[0] = sector(degrees);
        rgb[1] = 3.0L * i - (rgb[0] + rgb[2]);
    }
    else if (degrees < 240.0L)
    {
        rgb[0] = i * (1.0L - s);
        rgb[1] = sector(degrees - 120.0L);
        rgb[2] = 3.0L * i - (rgb[0] + rgb[1]);
    }
    else
    {
        rgb[1] = i * (1.0L - s);
        rgb[2] = sector(degrees - 240.0L);
        rgb[0] = 3.0L * i - (rgb[1] + rgb[2]);
    }
    return {unitHeld(rgb[0]), unitHeld(rgb[1]), unitHeld(rgb[2])};
}

/// One sample of a separable mode, as the product's blend formulas define it.
[[nodiscard]] long double separable(const std::string& mode, long double a, long double b)
{
    long double value = 0.0L;
    if (mode == "Replace")
        return b;
    if (mode == "Darken/Min")
        return b < a ? b : a;
    if (mode == "Lighten/Max")
        return a < b ? b : a;
    if (mode == "Multiply")
        return a * b;
    if (mode == "Linear Dodge/Add")
        return (std::min)(a + b, 1.0L);
    if (mode == "Subtract")
        return (std::max)(a - b, 0.0L);
    if (mode == "Divide")
        return b == 0.0L ? 0.0L : std::clamp(a / b, 0.0L, 1.0L);
    if (mode == "Color Burn")
        value = b == 0.0L ? 0.0L : 1.0L - (std::min)((1.0L - a) / b, 1.0L);
    else if (mode == "Linear Burn")
        value = a + b - 1.0L;
    else if (mode == "Screen")
        value = 1.0L - (1.0L - a) * (1.0L - b);
    else if (mode == "Color Dodge")
        value = b == 1.0L ? 0.0L : a / (1.0L - b);
    else if (mode == "Overlay")
        value = a > 0.5L ? 1.0L - 2.0L * (1.0L - a) * (1.0L - b) : 2.0L * a * b;
    else if (mode == "Soft Light")
        value = (1.0L - 2.0L * b) * a * a + 2.0L * a * b;
    else if (mode == "Hard Light")
        value = b > 0.5L ? 1.0L - 2.0L * (1.0L - a) * (1.0L - b) : 2.0L * a * b;
    else if (mode == "Vivid Light")
        value = b == 0.0L  ? 0.0L
                : b < 0.5L ? (std::max)(0.0L, 1.0L - (1.0L - a) / (2.0L * b))
                           : (std::min)(1.0L, a / (2.0L * (1.0L - b)));
    else if (mode == "Linear Light")
        value = b == 0.0L ? 0.0L : 2.0L * b + a - 1.0L;
    else if (mode == "Pin Light")
        value = b == 0.0L ? 0.0L : (std::max)(2.0L * b - 1.0L, (std::min)(a, 2.0L * b));
    else if (mode == "Difference")
        value = std::fabs(a - b);
    else if (mode == "Exclusion")
        value = 0.5L - 2.0L * (a - 0.5L) * (b - 0.5L);
    else if (mode == "Power")
        value = std::pow(a, b);
    else if (mode == "Arctan")
        value = std::atan2(a, b);
    return std::clamp(value, 0.0L, 1.0L);
}

/// One pixel of a colour-selecting or colour-exchanging mode.
[[nodiscard]] Triple colorMode(const std::string& mode, const Triple& a, const Triple& b)
{
    if (mode == "Darker Color" || mode == "Lighter Color")
    {
        const long double la = lab(a)[0];
        const long double lb = lab(b)[0];
        if (mode == "Darker Color")
        {
            return la <= lb ? a : b;
        }
        return la > lb ? a : b;
    }
    if (mode == "Hue" || mode == "Saturation")
    {
        long double ha = 0.0L, sa = 0.0L, ia = 0.0L, hb = 0.0L, sb = 0.0L, ib = 0.0L;
        rgbToHsi(a[0], a[1], a[2], ha, sa, ia);
        rgbToHsi(b[0], b[1], b[2], hb, sb, ib);
        return mode == "Hue" ? hsiToRgb(hb, sa, ia) : hsiToRgb(ha, sb, ia);
    }
    const Triple labA = lab(a);
    const Triple labB = lab(b);
    const Triple mixed = mode == "Color" ? Triple{labA[0], labB[1], labB[2]} : Triple{labB[0], labA[1], labA[2]};
    const Triple rgb = rgbOfLab(mixed);
    return {unitHeld(rgb[0]), unitHeld(rgb[1]), unitHeld(rgb[2])};
}

/// An exact location of a channel: the median and the median absolute deviation, each the mean of the two middle
/// values for an even count.
[[nodiscard]] PresentationLocation locationOf(std::vector<double> values)
{
    const auto median = [](std::vector<double>& samples) {
        const std::size_t half = samples.size() / 2u;
        std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(half), samples.end());
        const double upper = samples[half];
        if (samples.size() % 2u == 1u)
        {
            return upper;
        }
        const double lower = *std::max_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(half));
        return 0.5 * (lower + upper);
    };
    PresentationLocation location;
    location.median = median(values);
    for (double& value : values)
    {
        value = std::fabs(value - location.median);
    }
    location.deviation = median(values);
    return location;
}

/// The definition of one sample of a plane tightened by `amount`: the sample less `amount` times a twentieth of the
/// sum of its four Sobel magnitudes, a row past the plane's edge read as zero, held to the unit interval. The column is
/// an interior one.
[[nodiscard]] long double tightenedDefinition(const float* plane, std::uint32_t width, std::uint32_t height,
                                              std::uint32_t x, std::uint32_t y, double amount)
{
    // The four Sobel kernels, each indexed [column offset + 1][row offset + 1].
    constexpr int kernels[4][3][3] = {{{1, 2, 1}, {0, 0, 0}, {-1, -2, -1}},
                                      {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}},
                                      {{2, 1, 0}, {1, 0, -1}, {0, -1, -2}},
                                      {{0, 1, 2}, {-1, 0, 1}, {-2, -1, 0}}};
    long double magnitude = 0.0L;
    for (const auto& kernel : kernels)
    {
        long double response = 0.0L;
        for (int dy = -1; dy <= 1; ++dy)
        {
            const std::int64_t row = static_cast<std::int64_t>(y) + dy;
            if (row < 0 || row >= static_cast<std::int64_t>(height))
            {
                continue;
            }
            for (int dx = -1; dx <= 1; ++dx)
            {
                response += static_cast<long double>(plane[static_cast<std::size_t>(row) * width + x + dx]) *
                            kernel[dx + 1][dy + 1];
            }
        }
        magnitude += std::fabs(response);
    }
    const std::size_t index = static_cast<std::size_t>(y) * width + x;
    return unitHeld(plane[index] - amount * 0.05L * magnitude);
}

} // namespace

bool requiresCombinationOracle(const ScenarioSpec& scenario)
{
    return scenario.id == "color.lrgb_combine.f32" || scenario.id == "pointwise.blend_modes.f32" ||
           scenario.id == "stars.recombine.f32" || scenario.id == "pointwise.external_handoff.f32" ||
           scenario.id == "filter.edge_tighten.f32";
}

std::vector<float> lrgbLuminanceFrame(std::span<const float> color, std::uint32_t width, std::uint32_t height)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::vector<float> luminance(pixels, 0.0f);
    for (std::size_t index = 0u; index < pixels && 3u * pixels <= color.size(); ++index)
    {
        const double value = 0.3 * static_cast<double>(color[index]) +
                             0.6 * static_cast<double>(color[pixels + index]) +
                             0.1 * static_cast<double>(color[2u * pixels + index]) + 0.02;
        luminance[index] = static_cast<float>((std::min)(value, 1.0));
    }
    return luminance;
}

std::vector<float> blendSecondFrame(std::span<const float> first, std::uint32_t width, std::uint32_t height)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::vector<float> second(3u * pixels, 0.0f);
    for (std::uint32_t c = 0u; c < 3u && 3u * pixels <= first.size(); ++c)
    {
        const std::uint32_t from = (c + 1u) % 3u;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double value =
                    1.0 -
                    static_cast<double>(first[from * pixels + static_cast<std::size_t>(y) * width + (width - 1u - x)]);
                second[c * pixels + static_cast<std::size_t>(y) * width + x] = static_cast<float>(value);
            }
        }
    }
    return second;
}

std::vector<float> recombinationStarless(std::uint32_t width, std::uint32_t height)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::vector<float> starless(3u * pixels, 0.0f);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double value = 0.04 + 0.03 * static_cast<double>(x) / width +
                                     0.02 * static_cast<double>(y) / height + 0.01 * static_cast<double>(c);
                starless[c * pixels + static_cast<std::size_t>(y) * width + x] = static_cast<float>(value);
            }
        }
    }
    return starless;
}

std::vector<float> recombinationMask(std::span<const float> stars, std::uint32_t width, std::uint32_t height)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::vector<float> mask(pixels, 0.0f);
    for (std::size_t index = 0u; index < pixels && index < stars.size(); ++index)
    {
        mask[index] = static_cast<float>((std::min)(4.0 * static_cast<double>(stars[index]), 1.0));
    }
    return mask;
}

void checkLrgbOutput(std::span<const float> color, std::span<const float> luminance, std::uint32_t width,
                     std::uint32_t height, std::span<const float> output, ResultRecord& result)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    double largest = output.size() >= 3u * pixels && color.size() >= 3u * pixels ? 0.0 : 1.0;
    double differing = 0.0;
    for (std::size_t index = 0u; largest < 1.0 && index < pixels; ++index)
    {
        const Triple value = lab({color[index], color[pixels + index], color[2u * pixels + index]});
        long double packedLightness = value[0] / 100.0L;
        long double packedChroma = std::hypot(value[1], value[2]) / 200.0L;
        const long double hue = std::atan2(value[2], value[1]);
        const long double lum = midtones(luminance[index], kLrgbLightnessMidtones);
        packedChroma = midtones(packedChroma, kLrgbSaturationMidtones);
        packedLightness = packedLightness * (1.0L - kLrgbLightnessWeight) + lum * kLrgbLightnessWeight;
        const long double chroma = packedChroma * 200.0L;
        const Triple rgb = rgbOfLab({packedLightness * 100.0L, chroma * std::cos(hue), chroma * std::sin(hue)});
        for (std::size_t c = 0u; c < 3u; ++c)
        {
            const long double expected = unitHeld(unitHeld(rgb[c]) * kLrgbChannelWeights[c]);
            const double difference =
                std::fabs(static_cast<double>(output[c * pixels + index]) - static_cast<double>(expected));
            largest = std::isnan(difference) ? 1.0 : (std::max)(largest, difference);
            differing += difference > 1.0e-6 ? 1.0 : 0.0;
        }
    }
    result.correctnessMetric = largest;
    result.counters["lrgbOracleVersion"] = 1.0;
    result.counters["lrgbOracleSamples"] = static_cast<double>(3u * pixels);
    result.counters["lrgbOracleMaxAbs"] = largest;
    result.counters["lrgbOracleSamplesBeyondMicro"] = differing;
}

void checkBlendModeOutput(std::size_t mode, std::span<const float> target, std::span<const float> blend,
                          std::uint32_t width, std::uint32_t height, std::span<const float> output,
                          BlendModesCheck& check)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (mode >= kBlendModeLabels.size() || output.size() < 3u * pixels || target.size() < 3u * pixels ||
        blend.size() < 3u * pixels)
    {
        check.largest = 1.0;
        return;
    }
    const std::string label = kBlendModeLabels[mode];
    const bool colour = label == "Darker Color" || label == "Lighter Color" || label == "Hue" ||
                        label == "Saturation" || label == "Color" || label == "Lightness";
    for (std::size_t index = 0u; index < pixels; ++index)
    {
        const Triple a{target[index], target[pixels + index], target[2u * pixels + index]};
        const Triple b{blend[index], blend[pixels + index], blend[2u * pixels + index]};
        const Triple expected =
            colour ? colorMode(label, a, b)
                   : Triple{separable(label, a[0], b[0]), separable(label, a[1], b[1]), separable(label, a[2], b[2])};
        for (std::size_t c = 0u; c < 3u; ++c)
        {
            const double difference =
                std::fabs(static_cast<double>(output[c * pixels + index]) - static_cast<double>(expected[c]));
            check.largest = std::isnan(difference) ? 1.0 : (std::max)(check.largest, difference);
            check.differing += difference > 1.0e-6 ? 1.0 : 0.0;
        }
    }
    check.samples += static_cast<double>(3u * pixels);
}

void finishBlendModesCheck(const BlendModesCheck& check, ResultRecord& result)
{
    result.correctnessMetric = check.largest;
    result.counters["blendModesOracleVersion"] = 1.0;
    result.counters["blendModesOracleSamples"] = check.samples;
    result.counters["blendModesOracleMaxAbs"] = check.largest;
    result.counters["blendModesOracleSamplesBeyondMicro"] = check.differing;
}

void checkRecombinationOutput(std::span<const float> stars, std::span<const float> starless,
                              std::span<const float> mask, std::uint32_t width, std::uint32_t height,
                              std::span<const float> output, ResultRecord& result)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    double largest = output.size() >= 3u * pixels && stars.size() >= 3u * pixels && starless.size() >= 3u * pixels &&
                             mask.size() >= pixels
                         ? 0.0
                         : 1.0;
    double compared = 0.0;
    for (std::uint32_t c = 0u; largest < 1.0 && c < 3u; ++c)
    {
        const float* const plane = stars.data() + c * pixels;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 1u; x + 1u < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                const long double tightened = tightenedDefinition(plane, width, height, x, y, kRecombinationTightening);
                const long double base = starless[c * pixels + index];
                const long double sum = (std::min)(base + tightened, 1.0L);
                const long double weight = unitHeld(mask[index]);
                const long double expected = base * (1.0L - weight) + sum * weight;
                const double difference =
                    std::fabs(static_cast<double>(output[c * pixels + index]) - static_cast<double>(expected));
                largest = std::isnan(difference) ? 1.0 : (std::max)(largest, difference);
                compared += 1.0;
            }
        }
    }
    result.correctnessMetric = largest;
    result.counters["recombinationOracleVersion"] = 1.0;
    result.counters["recombinationOracleSamples"] = compared;
    result.counters["recombinationOracleMaxAbs"] = largest;
}

void checkEdgeTightenOutput(std::span<const float> stars, std::uint32_t width, std::uint32_t height,
                            std::span<const float> output, ResultRecord& result)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    double largest = output.size() >= 3u * pixels && stars.size() >= 3u * pixels ? 0.0 : 1.0;
    double compared = 0.0;
    for (std::uint32_t c = 0u; largest < 1.0 && c < 3u; ++c)
    {
        const float* const plane = stars.data() + c * pixels;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 1u; x + 1u < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                const long double expected = tightenedDefinition(plane, width, height, x, y, kEdgeTightenAmount);
                const double difference =
                    std::fabs(static_cast<double>(output[c * pixels + index]) - static_cast<double>(expected));
                largest = std::isnan(difference) ? 1.0 : (std::max)(largest, difference);
                compared += 1.0;
            }
        }
    }
    result.correctnessMetric = largest;
    result.counters["edgeTightenOracleVersion"] = 1.0;
    result.counters["edgeTightenOracleSamples"] = compared;
    result.counters["edgeTightenOracleMaxAbs"] = largest;
}

void checkHandoffOutput(std::span<const float> source, std::uint32_t width, std::uint32_t height,
                        std::span<const float> restored, ResultRecord& result)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (source.size() < 3u * pixels || restored.size() < 3u * pixels)
    {
        result.correctnessMetric = 1.0;
        return;
    }
    long double low = 0.0L;
    long double high = 1.0L;
    for (std::size_t index = 0u; index < 3u * pixels; ++index)
    {
        if (std::isfinite(source[index]))
        {
            low = (std::min)(low, static_cast<long double>(source[index]));
            high = (std::max)(high, static_cast<long double>(source[index]));
        }
    }
    const long double span = high - low;
    std::vector<PresentationLocation> locations;
    for (std::size_t c = 0u; c < 3u; ++c)
    {
        std::vector<double> mapped(pixels);
        for (std::size_t index = 0u; index < pixels; ++index)
        {
            mapped[index] = static_cast<double>((static_cast<long double>(source[c * pixels + index]) - low) / span);
        }
        locations.push_back(locationOf(std::move(mapped)));
    }
    const std::vector<PresentationTransfer> transfers = unlinkedPresentationTransfers(locations, -2.8, 0.25);
    double largest = 0.0;
    for (std::size_t c = 0u; c < 3u; ++c)
    {
        const PresentationTransfer& transfer = transfers[c];
        for (std::size_t index = 0u; index < pixels; ++index)
        {
            const long double mapped = (static_cast<long double>(source[c * pixels + index]) - low) / span;
            const long double code = std::nearbyint(unitHeld(presentationTransfer(mapped, transfer)) * 65535.0L);
            const long double answer = code / 65535.0L;
            long double back = transfer.highlights;
            if (answer <= 0.0L)
            {
                back = transfer.shadows;
            }
            else if (answer < 1.0L)
            {
                const long double balance = transfer.midtones;
                const long double fraction = balance * answer / ((1.0L - balance) * (1.0L - answer) + balance * answer);
                back = transfer.shadows + (transfer.highlights - transfer.shadows) * fraction;
            }
            const long double expected = low + span * back;
            const double difference =
                std::fabs(static_cast<double>(restored[c * pixels + index]) - static_cast<double>(expected));
            largest = std::isnan(difference) ? 1.0 : (std::max)(largest, difference);
        }
    }
    result.correctnessMetric = largest;
    result.counters["handoffOracleVersion"] = 1.0;
    result.counters["handoffOracleSamples"] = static_cast<double>(3u * pixels);
    result.counters["handoffOracleMaxAbs"] = largest;
}

} // namespace an::bench
