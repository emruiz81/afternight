// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The complete autostretch's definition and the evidence a run records for it.
#include "AutostretchOracle.h"

#include "AutostretchScenarios.h"
#include "CapturedStatistics.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace an::bench
{
namespace
{

/// One over the inverse of the standard normal distribution at three quarters.
constexpr long double kNormalizedDeviation = 1.4826022185056018L;

/// A channel's deviation as a sigma, a zero one counting as one thousandth.
[[nodiscard]] long double guardedSigma(const PresentationLocation& location)
{
    const long double sigma = static_cast<long double>(location.deviation) * kNormalizedDeviation;
    return sigma == 0.0L ? 0.001L : sigma;
}

/// The value one held to the unit interval.
[[nodiscard]] long double unitInterval(float value)
{
    return std::clamp(static_cast<long double>(value), 0.0L, 1.0L);
}

/// The row's timed boundary.
[[nodiscard]] std::string boundaryOf(const ScenarioSpec& scenario)
{
    const auto found = scenario.parameters.find(kAutostretchBoundaryParameter);
    return found == scenario.parameters.end() ? std::string{} : found->second;
}

/// Whether the row takes one transfer for every channel.
[[nodiscard]] bool linkedOf(const ScenarioSpec& scenario, std::uint32_t channels)
{
    const auto found = scenario.parameters.find("linked");
    return channels == 3u && found != scenario.parameters.end() && found->second == "true";
}

/// The transfers the definition derives from some statistics, one per channel.
[[nodiscard]] std::vector<PresentationTransfer> definedTransfers(std::span<const PresentationLocation> locations,
                                                                 bool linked)
{
    if (linked)
    {
        return std::vector<PresentationTransfer>(
            locations.size(),
            linkedPresentationTransfer(locations, kAutostretchShadowsClipping, kAutostretchTargetBackground));
    }
    return unlinkedPresentationTransfers(locations, kAutostretchShadowsClipping, kAutostretchTargetBackground);
}

} // namespace

std::vector<float> makeBinnedColourCapture(std::span<const std::uint8_t> codes, std::uint32_t width,
                                           std::uint32_t height)
{
    if (codes.size() != static_cast<std::size_t>(width) * height * 2u)
    {
        return {};
    }
    const auto code = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t index = static_cast<std::size_t>(y) * width + x;
        return static_cast<std::uint32_t>(codes[2u * index] | (codes[2u * index + 1u] << 8u));
    };
    const std::uint32_t binnedWidth = width / 2u;
    const std::uint32_t binnedHeight = height / 2u;
    const std::size_t plane = static_cast<std::size_t>(binnedWidth) * binnedHeight;
    std::vector<float> planes(plane * 3u);
    for (std::uint32_t y = 0u; y < binnedHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < binnedWidth; ++x)
        {
            const std::size_t pixel = static_cast<std::size_t>(y) * binnedWidth + x;
            planes[pixel] = static_cast<float>(code(2u * x, 2u * y)) / 65535.0f;
            planes[plane + pixel] =
                static_cast<float>(code(2u * x + 1u, 2u * y) + code(2u * x, 2u * y + 1u)) / 131070.0f;
            planes[2u * plane + pixel] = static_cast<float>(code(2u * x + 1u, 2u * y + 1u)) / 65535.0f;
        }
    }
    return planes;
}

PresentationTransfer linkedPresentationTransfer(std::span<const PresentationLocation> locations, double clipping,
                                                double target)
{
    PresentationTransfer transfer;
    if (locations.empty())
    {
        return transfer;
    }
    const bool inverted = std::all_of(locations.begin(), locations.end(),
                                      [](const PresentationLocation& location) { return location.median > 0.5; });
    long double endpoint = 0.0L;
    long double median = 0.0L;
    for (const PresentationLocation& location : locations)
    {
        median += location.median;
        endpoint += inverted ? location.median - static_cast<long double>(clipping) * guardedSigma(location)
                             : location.median + static_cast<long double>(clipping) * guardedSigma(location);
    }
    endpoint /= static_cast<long double>(locations.size());
    median /= static_cast<long double>(locations.size());
    const PresentationTransfer balance{.midtones = target, .shadows = 0.0L, .highlights = 1.0L};
    if (!inverted)
    {
        transfer.shadows = (std::max)(0.0L, endpoint);
        transfer.midtones = presentationTransfer(median - transfer.shadows, balance);
    }
    else
    {
        transfer.highlights = (std::min)(1.0L, endpoint);
        transfer.midtones = 1.0L - presentationTransfer(transfer.highlights - median, balance);
    }
    return transfer;
}

std::vector<PresentationLocation> exactAutostretchLocations(std::span<const float> samples, std::uint32_t channels)
{
    std::vector<PresentationLocation> locations;
    if (channels == 0u || samples.size() % channels != 0u)
    {
        return locations;
    }
    const std::size_t plane = samples.size() / channels;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        locations.push_back(presentationLocation(samples.subspan(channel * plane, plane)));
    }
    return locations;
}

std::vector<PresentationLocation> exactCapturedLocations(std::span<const std::uint8_t> codes)
{
    const std::optional<OrderStatisticsOracle> oracle = computeCapturedOrderStatisticsOracle(codes);
    if (!oracle.has_value())
    {
        return {};
    }
    return {PresentationLocation{.median = oracle->median, .deviation = oracle->medianAbsoluteDeviation}};
}

std::string autostretchStatisticsText(const AutostretchReport& report)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(17);
    for (std::uint32_t channel = 0u; channel < (std::min)(report.channels, 3u); ++channel)
    {
        text << channel << ' ' << report.medians[channel] << ' ' << report.deviations[channel] << '\n';
    }
    return text.str();
}

void checkAutostretchOutput(const ScenarioSpec& scenario, std::span<const float> input, std::span<const float> output,
                            const AutostretchReport& reported, std::span<const PresentationLocation> exact,
                            ResultRecord& result)
{
    const std::uint32_t channels = reported.channels;
    result.counters["autostretchChannels"] = static_cast<double>(channels);
    if (channels == 0u || channels > 3u || exact.size() != channels || input.size() % channels != 0u)
    {
        return;
    }
    const bool statisticsOnly = boundaryOf(scenario) == kAutostretchStatistics;
    std::vector<PresentationLocation> locations;
    double statisticsError = 0.0;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::string suffix = std::to_string(channel);
        result.counters["autostretchMedian" + suffix] = reported.medians[channel];
        result.counters["autostretchDeviation" + suffix] = reported.deviations[channel];
        locations.push_back({.median = reported.medians[channel], .deviation = reported.deviations[channel]});
        statisticsError = (std::max)({statisticsError, std::abs(reported.medians[channel] - exact[channel].median),
                                      std::abs(reported.deviations[channel] - exact[channel].deviation)});
    }
    if (!std::isfinite(statisticsError))
    {
        statisticsError = (std::numeric_limits<double>::max)();
    }
    result.counters["autostretchStatisticsError"] = statisticsError;
    if (statisticsOnly)
    {
        result.correctnessMetric = statisticsError;
        return;
    }

    const bool linked = linkedOf(scenario, channels);
    const std::vector<PresentationTransfer> fromReported = definedTransfers(locations, linked);
    const std::vector<PresentationTransfer> fromExact = definedTransfers(exact, linked);
    double transferError = 0.0;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::string suffix = std::to_string(channel);
        const PresentationTransfer& transfer = reported.transfers[channel];
        result.counters["autostretchMidtones" + suffix] = static_cast<double>(transfer.midtones);
        result.counters["autostretchShadows" + suffix] = static_cast<double>(transfer.shadows);
        result.counters["autostretchHighlights" + suffix] = static_cast<double>(transfer.highlights);
        transferError =
            (std::max)({transferError,
                        static_cast<double>(std::abs(transfer.midtones - fromReported[channel].midtones)),
                        static_cast<double>(std::abs(transfer.shadows - fromReported[channel].shadows)),
                        static_cast<double>(std::abs(transfer.highlights - fromReported[channel].highlights))});
    }
    result.counters["autostretchTransferError"] =
        std::isfinite(transferError) ? transferError : (std::numeric_limits<double>::max)();

    const std::size_t plane = input.size() / channels;
    double outputError = output.size() == input.size() ? 0.0 : (std::numeric_limits<double>::max)();
    double definitionError = 0.0;
    double compared = 0.0;
    if (output.size() == input.size())
    {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::size_t sample = 0u; sample < plane; ++sample)
            {
                const std::size_t index = channel * plane + sample;
                const long double value = unitInterval(input[index]);
                const long double produced = static_cast<long double>(output[index]);
                const long double own = presentationTransfer(value, reported.transfers[channel]);
                const long double defined = presentationTransfer(value, fromExact[channel]);
                const double error = std::isfinite(output[index]) ? static_cast<double>(std::abs(produced - own))
                                                                  : (std::numeric_limits<double>::max)();
                outputError = (std::max)(outputError, error);
                definitionError = (std::max)(definitionError, static_cast<double>(std::abs(produced - defined)));
                compared += 1.0;
            }
        }
    }
    result.counters["autostretchOutputError"] = outputError;
    result.counters["autostretchDefinitionError"] = definitionError;
    result.counters["autostretchSamplesCompared"] = compared;
    result.correctnessMetric = outputError;
}

bool requiresAutostretchOracle(const ScenarioSpec& scenario)
{
    return scenario.group == "autostretch";
}

bool autostretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& record)
{
    const auto counter = [&](const char* name) -> std::optional<double> {
        const auto found = record.counters.find(name);
        return found == record.counters.end() ? std::nullopt : std::optional<double>(found->second);
    };
    const std::optional<double> statistics = counter("autostretchStatisticsError");
    if (!record.correctnessMetric.has_value() || !statistics.has_value() ||
        !(*statistics <= kAutostretchStatisticsTolerance))
    {
        return false;
    }
    if (boundaryOf(scenario) == kAutostretchStatistics)
    {
        return true;
    }
    const std::optional<double> transfer = counter("autostretchTransferError");
    const std::optional<double> output = counter("autostretchOutputError");
    const std::optional<double> compared = counter("autostretchSamplesCompared");
    return transfer.has_value() && output.has_value() && compared.has_value() &&
           *transfer <= kAutostretchTransferTolerance && *output <= scenario.tolerance &&
           *compared == static_cast<double>(scenario.elementsProcessed);
}

} // namespace an::bench
