// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent display presentation definition and complete-output qualification.
#include "PresentationOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace an::bench
{
namespace
{

/// One over the inverse of the standard normal distribution at three quarters.
constexpr long double kNormalizedDeviation = 1.4826022185056018L;

[[nodiscard]] double middleOf(std::vector<double>& values)
{
    const std::size_t count = values.size();
    const auto upper = values.begin() + static_cast<std::ptrdiff_t>(count / 2u);
    std::nth_element(values.begin(), upper, values.end());
    const double high = *upper;
    if (count % 2u == 1u)
    {
        return high;
    }
    const double low = *std::max_element(values.begin(), upper);
    return (low + high) / 2.0;
}

} // namespace

PresentationLocation presentationLocation(std::span<const float> samples)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const float sample : samples)
    {
        if (std::isfinite(sample))
        {
            values.push_back(std::clamp(static_cast<double>(sample), 0.0, 1.0));
        }
    }
    PresentationLocation location;
    if (values.empty())
    {
        return location;
    }
    std::vector<double> deviations = values;
    location.median = middleOf(values);
    for (double& deviation : deviations)
    {
        deviation = std::abs(deviation - location.median);
    }
    location.deviation = middleOf(deviations);
    return location;
}

long double presentationTransfer(long double value, const PresentationTransfer& transfer)
{
    if (value <= transfer.shadows)
    {
        return 0.0L;
    }
    if (value >= transfer.highlights)
    {
        return 1.0L;
    }
    const long double x = (value - transfer.shadows) / (transfer.highlights - transfer.shadows);
    const long double m = transfer.midtones;
    return ((m - 1.0L) * x) / ((2.0L * m - 1.0L) * x - m);
}

std::vector<PresentationTransfer> unlinkedPresentationTransfers(std::span<const PresentationLocation> locations,
                                                                double clipping, double target)
{
    const bool inverted = std::all_of(locations.begin(), locations.end(),
                                      [](const PresentationLocation& location) { return location.median > 0.5; });
    std::vector<PresentationTransfer> transfers;
    for (const PresentationLocation& location : locations)
    {
        long double sigma = static_cast<long double>(location.deviation) * kNormalizedDeviation;
        if (sigma == 0.0L)
        {
            sigma = 0.001L;
        }
        const long double median = location.median;
        PresentationTransfer transfer;
        const PresentationTransfer balance{.midtones = target, .shadows = 0.0L, .highlights = 1.0L};
        if (!inverted)
        {
            transfer.shadows = (std::max)(0.0L, median + static_cast<long double>(clipping) * sigma);
            transfer.midtones = presentationTransfer(median - transfer.shadows, balance);
        }
        else
        {
            transfer.highlights = (std::min)(1.0L, median - static_cast<long double>(clipping) * sigma);
            transfer.midtones = 1.0L - presentationTransfer(transfer.highlights - median, balance);
        }
        transfers.push_back(transfer);
    }
    return transfers;
}

std::uint8_t presentationByte(long double value)
{
    if (!(value > 0.0L))
    {
        return 0u;
    }
    if (value >= 1.0L)
    {
        return 255u;
    }
    return static_cast<std::uint8_t>(std::nearbyint(static_cast<double>(value * 255.0L)));
}

bool requiresPresentationOracle(const ScenarioSpec& scenario)
{
    return scenario.group == "presentation";
}

void checkPresentationAutostretchOutput(std::span<const float> planarInput, std::uint32_t width, std::uint32_t height,
                                        std::span<const std::uint8_t> rgba, ResultRecord& result)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const std::size_t channels = pixels == 0u ? 0u : planarInput.size() / pixels;
    std::vector<PresentationLocation> locations;
    for (std::size_t channel = 0u; channel < channels; ++channel)
    {
        locations.push_back(presentationLocation(planarInput.subspan(channel * pixels, pixels)));
    }
    const std::vector<PresentationTransfer> transfers = unlinkedPresentationTransfers(locations, -2.8, 0.25);
    double largest = 0.0;
    double differing = 0.0;
    double translucent = 0.0;
    for (std::size_t pixel = 0u; pixel < pixels; ++pixel)
    {
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            const std::size_t source = channels == 1u ? 0u : channel;
            const float sample = planarInput[source * pixels + pixel];
            const long double value = std::clamp(static_cast<long double>(sample), 0.0L, 1.0L);
            const std::uint8_t expected = presentationByte(presentationTransfer(value, transfers[source]));
            const double difference =
                std::abs(static_cast<double>(rgba[pixel * 4u + channel]) - static_cast<double>(expected));
            largest = (std::max)(largest, difference);
            differing += difference != 0.0 ? 1.0 : 0.0;
        }
        translucent += rgba[pixel * 4u + 3u] != 255u ? 1.0 : 0.0;
    }
    result.correctnessMetric = translucent == 0.0 ? largest : 255.0;
    result.counters["presentationOracleVersion"] = 1.0;
    result.counters["presentationOracleSamples"] = static_cast<double>(pixels * 3u);
    result.counters["presentationOracleMaxCodes"] = largest;
    result.counters["presentationOracleDifferingSamples"] = differing;
    result.counters["presentationTranslucentPixels"] = translucent;
    for (std::size_t channel = 0u; channel < transfers.size(); ++channel)
    {
        const std::string suffix = std::to_string(channel);
        result.counters["presentationMedian" + suffix] = locations[channel].median;
        result.counters["presentationShadows" + suffix] = static_cast<double>(transfers[channel].shadows);
        result.counters["presentationMidtones" + suffix] = static_cast<double>(transfers[channel].midtones);
    }
}

void checkPresentationSubsampledOutput(std::span<const std::uint16_t> input, std::uint32_t width, std::uint32_t height,
                                       std::uint32_t factor, std::span<const std::uint8_t> output, ResultRecord& result)
{
    const std::uint32_t outputWidth = (width + factor - 1u) / factor;
    const std::uint32_t outputHeight = (height + factor - 1u) / factor;
    double largest = 0.0;
    double differing = 0.0;
    for (std::uint32_t y = 0u; y < outputHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < outputWidth; ++x)
        {
            const std::uint16_t code =
                input[static_cast<std::size_t>(y) * factor * width + static_cast<std::size_t>(x) * factor];
            const std::uint8_t expected = presentationByte(static_cast<long double>(code) / 65535.0L);
            const double difference =
                std::abs(static_cast<double>(output[static_cast<std::size_t>(y) * outputWidth + x]) -
                         static_cast<double>(expected));
            largest = (std::max)(largest, difference);
            differing += difference != 0.0 ? 1.0 : 0.0;
        }
    }
    result.correctnessMetric = largest;
    result.counters["presentationOracleVersion"] = 1.0;
    result.counters["presentationOracleSamples"] = static_cast<double>(outputWidth) * outputHeight;
    result.counters["presentationOracleMaxCodes"] = largest;
    result.counters["presentationOracleDifferingSamples"] = differing;
}

} // namespace an::bench
