// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/ReadoutFigures.h"

#include "theme/ComponentParts.h"

#include <cmath>
#include <limits>

namespace an::ui
{
namespace
{

/// A statistic's row among the statistics window's figures (`scaledStatisticsFigures`).
[[nodiscard]] std::size_t windowRow(const ReadoutStatistic statistic) noexcept
{
    switch (statistic)
    {
    case ReadoutStatistic::Minimum:
        return 3u;
    case ReadoutStatistic::Maximum:
        return 4u;
    case ReadoutStatistic::Mean:
        return 0u;
    case ReadoutStatistic::Median:
        return 2u;
    case ReadoutStatistic::StandardDeviation:
        return 5u;
    case ReadoutStatistic::MedianAbsoluteDeviation:
        return 9u;
    }
    return 0u;
}

} // namespace

QString readoutStatisticName(const ReadoutStatistic statistic)
{
    switch (statistic)
    {
    case ReadoutStatistic::Minimum:
        return QStringLiteral("Min");
    case ReadoutStatistic::Maximum:
        return QStringLiteral("Max");
    case ReadoutStatistic::Mean:
        return QStringLiteral("Mean");
    case ReadoutStatistic::Median:
        return QStringLiteral("Median");
    case ReadoutStatistic::StandardDeviation:
        return QStringLiteral("StdDev");
    case ReadoutStatistic::MedianAbsoluteDeviation:
        return QStringLiteral("MAD");
    }
    return QString();
}

double readoutFigure(const ImageStatisticsChannel& channel, const ReadoutStatistic statistic,
                     const StatisticsScale scale) noexcept
{
    return scaledStatisticsFigures(channel, scale)[windowRow(statistic)];
}

std::size_t readoutChannelCount(const ImageStatisticsReport& report) noexcept
{
    return report.channels.size() >= 3u ? 3u : (report.channels.empty() ? 0u : 1u);
}

double readoutImageFigure(const ImageStatisticsReport& report, const ReadoutStatistic statistic,
                          const StatisticsScale scale) noexcept
{
    const std::size_t channels = readoutChannelCount(report);
    if (channels == 0u)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double sum = 0.0;
    for (std::size_t channel = 0u; channel < channels; ++channel)
    {
        sum += readoutFigure(report.channels[channel], statistic, scale);
    }
    return sum / static_cast<double>(channels);
}

QString formatReadoutFigure(const double value, const StatisticsScale scale)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("-");
    }
    int decimals = 4;
    switch (scale)
    {
    case StatisticsScale::Normalized:
        decimals = 4;
        break;
    case StatisticsScale::Bits8:
        decimals = 2;
        break;
    case StatisticsScale::Bits16:
        decimals = 1;
        break;
    case StatisticsScale::Bits32:
        decimals = 0;
        break;
    }
    return QString::number(value, 'f', decimals);
}

QString formatReadoutDelta(const double change)
{
    if (!std::isfinite(change))
    {
        return QStringLiteral("-");
    }
    if (std::abs(change) < parts::kReadoutDeltaFloor)
    {
        return QStringLiteral("±0");
    }
    return (change > 0.0 ? QStringLiteral("+") : QStringLiteral("−")) + QString::number(std::abs(change), 'f', 4);
}

ClippingShare clippingShare(const HistogramDisplayData& counts) noexcept
{
    std::uint64_t samples = 0u;
    std::uint64_t low = 0u;
    std::uint64_t high = 0u;
    for (const HistogramDisplayChannel& channel : counts.channels)
    {
        samples += channel.samples;
        low += channel.clippedLow;
        high += channel.clippedHigh;
    }
    if (samples == 0u)
    {
        return {};
    }
    return ClippingShare{.low = static_cast<double>(low) * 100.0 / static_cast<double>(samples),
                         .high = static_cast<double>(high) * 100.0 / static_cast<double>(samples)};
}

QString formatClipShare(const double percent)
{
    return QStringLiteral("%1 %").arg(QString::number(percent, 'f', 3));
}

QString histogramResolutionName(const HistogramResolution resolution)
{
    switch (resolution)
    {
    case HistogramResolution::Bits6:
        return QStringLiteral("6-bit (64)");
    case HistogramResolution::Bits7:
        return QStringLiteral("7-bit (128)");
    case HistogramResolution::Bits8:
        return QStringLiteral("8-bit (256)");
    case HistogramResolution::Bits9:
        return QStringLiteral("9-bit (512)");
    case HistogramResolution::Bits10:
        return QStringLiteral("10-bit (1024)");
    case HistogramResolution::Bits12:
        return QStringLiteral("12-bit (4096)");
    case HistogramResolution::Bits14:
        return QStringLiteral("14-bit (16K)");
    case HistogramResolution::Bits16:
        return QStringLiteral("16-bit (64K)");
    case HistogramResolution::Levels100:
        return QStringLiteral("100 levels");
    case HistogramResolution::Levels1000:
        return QStringLiteral("1000 levels");
    case HistogramResolution::Levels10000:
        return QStringLiteral("10000 levels");
    }
    return QStringLiteral("8-bit (256)");
}

QString histogramResolutionShortName(const HistogramResolution resolution)
{
    const QString name = histogramResolutionName(resolution);
    const qsizetype bracket = name.indexOf(QStringLiteral(" ("));
    return bracket < 0 ? name : name.left(bracket);
}

} // namespace an::ui
