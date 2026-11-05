// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "CosmeticCorrectionOracle.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace an::bench
{
namespace
{

/// The margin no implant may sit inside, in pixels, which keeps every replacement window interior.
constexpr std::uint32_t kEdgeMargin = 8u;

/// The lattice pitch between implant cells, in pixels, which keeps implants out of each other's
/// windows.
constexpr std::uint32_t kLatticePitch = 48u;

/// A small deterministic position hash, so the implants land unevenly without a generator.
///
/// @param x the cell column.
/// @param y the cell row.
/// @return well-mixed bits.
[[nodiscard]] std::uint64_t mixCell(std::uint64_t x, std::uint64_t y) noexcept
{
    std::uint64_t bits = (x << 32) ^ y ^ 0x9E3779B97F4A7C15ull;
    bits = (bits ^ (bits >> 30)) * 0xBF58476D1CE4E5B9ull;
    bits = (bits ^ (bits >> 27)) * 0x94D049BB133111EBull;
    return bits ^ (bits >> 31);
}

/// The exact median of a window, an even count resolved as the mean of its two middle samples.
///
/// Evaluated the way the engine evaluates it - the two Float32 middles averaged in Float64 and
/// narrowed once - so the expected repair is comparable to a produced sample bit for bit.
///
/// @param window the surviving samples, reordered in place.
/// @return the median, as the Float32 value a repair stores.
[[nodiscard]] double windowMedian(std::vector<float>& window) noexcept
{
    const std::size_t upper = window.size() / 2u;
    std::nth_element(window.begin(), window.begin() + static_cast<std::ptrdiff_t>(upper), window.end());
    if (window.size() % 2u == 1u)
    {
        return static_cast<double>(window[upper]);
    }
    const float lower = *std::max_element(window.begin(), window.begin() + static_cast<std::ptrdiff_t>(upper));
    return static_cast<double>(
        static_cast<float>((static_cast<double>(lower) + static_cast<double>(window[upper])) / 2.0));
}

/// One value's normalized deviation from what the oracle expected.
///
/// @param produced what the runner wrote.
/// @param expected what the harness computed.
/// @return the absolute error below magnitude one, the relative error above it, and the largest
///         finite double for a produced value that is not finite.
[[nodiscard]] double normalizedDeviation(double produced, double expected) noexcept
{
    if (!std::isfinite(produced))
    {
        return std::numeric_limits<double>::max();
    }
    const double magnitude = std::abs(expected);
    const double difference = std::abs(produced - expected);
    return magnitude > 1.0 ? difference / magnitude : difference;
}

} // namespace

std::vector<CosmeticImplant> plantCosmeticDefects(std::span<float> samples, std::uint32_t width, std::uint32_t height)
{
    std::vector<CosmeticImplant> implants;
    if (width <= 2u * kEdgeMargin + kLatticePitch || height <= 2u * kEdgeMargin + kLatticePitch)
    {
        return implants;
    }
    for (std::uint32_t cellY = kEdgeMargin; cellY + kLatticePitch <= height - kEdgeMargin; cellY += kLatticePitch)
    {
        for (std::uint32_t cellX = kEdgeMargin; cellX + kLatticePitch <= width - kEdgeMargin; cellX += kLatticePitch)
        {
            // Jitter within the first quarter of the cell, so two implants stay at least half a
            // pitch apart - far outside each other's replacement windows.
            const std::uint64_t bits = mixCell(cellX, cellY);
            const std::uint32_t x = cellX + static_cast<std::uint32_t>(bits % (kLatticePitch / 4u));
            const std::uint32_t y = cellY + static_cast<std::uint32_t>((bits >> 16) % (kLatticePitch / 4u));
            CosmeticImplant implant{};
            implant.x = x;
            implant.y = y;
            implant.hot = ((bits >> 40) & 1u) != 0u;
            implant.value = implant.hot ? 1.0f : 0.0f;
            samples[static_cast<std::size_t>(y) * width + x] = implant.value;
            implants.push_back(implant);
        }
    }
    return implants;
}

CosmeticOracle computeCosmeticOracle(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                                     const std::vector<CosmeticImplant>& implants, double coldSigma, double hotSigma,
                                     std::uint32_t hotRadius, std::uint32_t coldRadius)
{
    CosmeticOracle oracle{};
    oracle.implants = implants;
    if (samples.empty())
    {
        return oracle;
    }

    // The exact statistics, in double: the median linearly interpolated between the two middle
    // ranks of an even count, and the population deviation around the mean.
    std::vector<double> sorted{samples.begin(), samples.end()};
    std::sort(sorted.begin(), sorted.end());
    const std::size_t count = sorted.size();
    oracle.median = count % 2u == 1u ? sorted[count / 2u] : (sorted[count / 2u - 1u] + sorted[count / 2u]) * 0.5;
    double sum = 0.0;
    for (const double value : sorted)
    {
        sum += value;
    }
    const double mean = sum / static_cast<double>(count);
    double squares = 0.0;
    for (const double value : sorted)
    {
        squares += (value - mean) * (value - mean);
    }
    oracle.deviation = std::sqrt(squares / static_cast<double>(count));
    oracle.coldThreshold = oracle.median - coldSigma * oracle.deviation;
    oracle.hotThreshold = oracle.median + hotSigma * oracle.deviation;

    // The population counts against the exact thresholds, in the comparison's own Float32 domain.
    const auto coldEdge = static_cast<float>(oracle.coldThreshold);
    const auto hotEdge = static_cast<float>(oracle.hotThreshold);
    for (const float value : samples)
    {
        if (value >= hotEdge)
        {
            ++oracle.hotCount;
        }
        else if (value <= coldEdge)
        {
            ++oracle.coldCount;
        }
    }

    // The expected repairs, each from the defective frame as it stands.
    std::vector<float> window;
    for (const CosmeticImplant& implant : implants)
    {
        const std::uint32_t radius = implant.hot ? hotRadius : coldRadius;
        window.clear();
        for (std::uint32_t y = implant.y - radius; y <= implant.y + radius; ++y)
        {
            for (std::uint32_t x = implant.x - radius; x <= implant.x + radius; ++x)
            {
                if (x != implant.x || y != implant.y)
                {
                    window.push_back(samples[static_cast<std::size_t>(y) * width + x]);
                }
            }
        }
        oracle.replacements.push_back(windowMedian(window));
    }

    // Untouched probes at a fixed stride, skipping anything an implant occupies.
    constexpr std::uint32_t kUntouchedStride = 149u;
    for (std::size_t index = 0u; index < samples.size(); index += kUntouchedStride)
    {
        const auto x = static_cast<std::uint32_t>(index % width);
        const auto y = static_cast<std::uint32_t>(index / width);
        const bool planted = std::any_of(implants.begin(), implants.end(), [&](const CosmeticImplant& implant) {
            return implant.x == x && implant.y == y;
        });
        if (!planted)
        {
            oracle.untouched.push_back(static_cast<std::uint32_t>(index));
        }
    }
    return oracle;
}

double cosmeticCorrectionError(std::span<const float> produced, std::span<const float> source, std::uint32_t width,
                               const CosmeticOracle& oracle) noexcept
{
    double worst = 0.0;
    for (std::size_t index = 0u; index < oracle.implants.size(); ++index)
    {
        const CosmeticImplant& implant = oracle.implants[index];
        const double value = produced[static_cast<std::size_t>(implant.y) * width + implant.x];
        worst = (std::max)(worst, normalizedDeviation(value, oracle.replacements[index]));
    }
    for (const std::uint32_t index : oracle.untouched)
    {
        worst = (std::max)(worst, normalizedDeviation(produced[index], source[index]));
    }
    return worst;
}

std::string cosmeticCorrectionText(std::span<const float> produced, std::uint32_t width, const CosmeticOracle& oracle)
{
    std::ostringstream stream;
    stream << std::setprecision(9) << std::scientific;
    for (const CosmeticImplant& implant : oracle.implants)
    {
        stream << (implant.hot ? 'h' : 'c') << implant.x << ',' << implant.y << '='
               << produced[static_cast<std::size_t>(implant.y) * width + implant.x] << '\n';
    }
    for (const std::uint32_t index : oracle.untouched)
    {
        stream << 'u' << index << '=' << produced[index] << '\n';
    }
    return stream.str();
}

double cosmeticDetectionError(double coldThreshold, double hotThreshold, std::uint64_t coldCount,
                              std::uint64_t hotCount, const CosmeticOracle& oracle) noexcept
{
    double worst = normalizedDeviation(coldThreshold, oracle.coldThreshold);
    worst = (std::max)(worst, normalizedDeviation(hotThreshold, oracle.hotThreshold));
    const double coldBase = static_cast<double>((std::max<std::uint64_t>)(oracle.coldCount, 1u));
    const double hotBase = static_cast<double>((std::max<std::uint64_t>)(oracle.hotCount, 1u));
    const double coldMiss = std::abs(static_cast<double>(coldCount) - static_cast<double>(oracle.coldCount)) / coldBase;
    const double hotMiss = std::abs(static_cast<double>(hotCount) - static_cast<double>(oracle.hotCount)) / hotBase;
    worst = (std::max)(worst, coldMiss);
    worst = (std::max)(worst, hotMiss);
    return worst;
}

std::string cosmeticDetectionText(double coldThreshold, double hotThreshold, std::uint64_t coldCount,
                                  std::uint64_t hotCount)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    stream << "coldThreshold=" << coldThreshold << '\n';
    stream << "hotThreshold=" << hotThreshold << '\n';
    stream << "coldCount=" << coldCount << '\n';
    stream << "hotCount=" << hotCount << '\n';
    return stream.str();
}

} // namespace an::bench
