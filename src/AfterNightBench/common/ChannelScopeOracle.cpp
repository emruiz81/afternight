// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definition of a process run on the CIE lightness of an sRGB-encoded colour frame.

#include "ChannelScopeOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace an::bench
{

namespace
{

using Matrix = std::array<long double, 9u>;
using Triple = std::array<long double, 3u>;

/// The tabulated D65 white, the one every published sRGB conversion is stated against.
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

/// The sRGB primaries' matrix to XYZ: each primary's chromaticity scaled so the three sum to the white.
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

} // namespace

std::array<long double, 3u> channelScopeLightnessGamma(const std::array<long double, 3u>& encoded, long double gamma)
{
    static const Matrix toXyz = srgbToXyz();
    static const Matrix fromXyz = inverse(toXyz);
    const Triple linear{decode(encoded[0]), decode(encoded[1]), decode(encoded[2])};
    const Triple xyz = multiply(toXyz, linear);
    const long double fx = labForward(xyz[0] / kD65[0]);
    const long double fy = labForward(xyz[1] / kD65[1]);
    const long double fz = labForward(xyz[2] / kD65[2]);
    const long double lightness = 116.0L * fy - 16.0L;
    const long double a = 500.0L * (fx - fy);
    const long double b = 200.0L * (fy - fz);
    // The process sees the lightness packed into the unit interval.
    const long double packed = lightness / 100.0L;
    const long double processed = packed > 0.0L ? std::pow(packed, 1.0L / gamma) : 0.0L;
    const long double fyBack = (100.0L * processed + 16.0L) / 116.0L;
    const Triple back{kD65[0] * labInverse(fyBack + a / 500.0L), kD65[1] * labInverse(fyBack),
                      kD65[2] * labInverse(fyBack - b / 200.0L)};
    const Triple rgb = multiply(fromXyz, back);
    return {encode(rgb[0]), encode(rgb[1]), encode(rgb[2])};
}

bool requiresChannelScopeOracle(const ScenarioSpec& scenario)
{
    return scenario.id == "color.channel_scope.lab_l.f32";
}

void checkChannelScopeOutput(std::span<const float> planarInput, std::uint32_t width, std::uint32_t height,
                             std::span<const float> planarOutput, double gamma, ResultRecord& result)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    double largest = 0.0;
    double outside = 0.0;
    const auto unit = [](long double value) { return std::clamp(value, 0.0L, 1.0L); };
    for (std::size_t pixel = 0u; pixel < pixels; ++pixel)
    {
        const Triple encoded{planarInput[pixel], planarInput[pixels + pixel], planarInput[2u * pixels + pixel]};
        const Triple expected = channelScopeLightnessGamma(encoded, static_cast<long double>(gamma));
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            const long double produced = planarOutput[channel * pixels + pixel];
            const double difference = static_cast<double>(std::fabs(unit(produced) - unit(expected[channel])));
            largest = std::isnan(difference) ? 1.0 : (std::max)(largest, difference);
            outside += expected[channel] < 0.0L || expected[channel] > 1.0L ? 1.0 : 0.0;
        }
    }
    result.correctnessMetric = largest;
    result.counters["channelScopeOracleVersion"] = 1.0;
    result.counters["channelScopeOracleSamples"] = static_cast<double>(pixels * 3u);
    result.counters["channelScopeOracleMaxAbs"] = largest;
    result.counters["channelScopeOracleOutsideUnit"] = outside;
}

} // namespace an::bench
