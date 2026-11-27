// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the readout shows of an image, as values a card is handed: the six statistics it reads out, each taken from the
/// statistics window's figures at the window's scales, the text it writes them in, the change against the original a
/// task's readout writes under each, the share of the samples a histogram counted at either end of the range, and the
/// image's solution and facts as text. No widget: the shell core makes these and the cards show them.

#include "AfterNightControlsExport.h"
#include "controls/ImageStatisticsFigures.h"
#include "widgets/HistogramDisplayData.h"

#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>

namespace an::ui
{

/// A statistic the readout reads out.
enum class ReadoutStatistic : std::uint8_t
{
    Minimum,
    Maximum,
    Mean,
    Median,
    StandardDeviation,
    MedianAbsoluteDeviation
};

/// The statistics card's six, in its order.
inline constexpr std::array<ReadoutStatistic, 6> kReadoutStatistics{
    ReadoutStatistic::Minimum, ReadoutStatistic::Maximum,           ReadoutStatistic::Mean,
    ReadoutStatistic::Median,  ReadoutStatistic::StandardDeviation, ReadoutStatistic::MedianAbsoluteDeviation};

/// The compact readout's five, in its order.
inline constexpr std::array<ReadoutStatistic, 5> kCompactReadoutStatistics{
    ReadoutStatistic::Minimum, ReadoutStatistic::Maximum, ReadoutStatistic::Mean, ReadoutStatistic::Median,
    ReadoutStatistic::StandardDeviation};

/// A statistic's short name: "Min", "Max", "Mean", "Median", "StdDev", "MAD".
[[nodiscard]] AFTERNIGHTCONTROLS_API QString readoutStatisticName(ReadoutStatistic statistic);

/// A statistic of one channel at a scale: the statistics window's figure of the same row
/// (`scaledStatisticsFigures`), or NaN - written "-" - for a channel that kept no sample, every one clipped or left out
/// by its validity, whose figures the window writes as zero beside its count.
[[nodiscard]] AFTERNIGHTCONTROLS_API double readoutFigure(const ImageStatisticsChannel& channel,
                                                          ReadoutStatistic statistic, StatisticsScale scale) noexcept;

/// A statistic over an image's colour channels at a scale - the mean of the first three channels' figures - or of its
/// one channel when it has fewer than three: what the compact readout shows for an image.
///
/// @return the figure; NaN for a report with no channel, or with a channel that kept no sample.
[[nodiscard]] AFTERNIGHTCONTROLS_API double
readoutImageFigure(const ImageStatisticsReport& report, ReadoutStatistic statistic, StatisticsScale scale) noexcept;

/// How many channels the readout shows of a report: three for colour, one otherwise.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t readoutChannelCount(const ImageStatisticsReport& report) noexcept;

/// A figure written as the readout writes it: four decimals for fractions of the range, two for eight-bit codes, one
/// for sixteen-bit codes and none for thirty-two-bit codes; "-" for a figure that is not a number.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString formatReadoutFigure(double value, StatisticsScale scale);

/// A change against the original as a task's readout writes it: "+0.0123" or "-0.0123" with a minus sign, and
/// "±0" for a change under 0.00005 either way.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString formatReadoutDelta(double change);

/// The shares of a histogram's samples at or below zero and at or above one, in percent, over every channel.
struct ClippingShare
{
    double low = 0.0;
    double high = 0.0;
};

/// The shares of the samples a histogram counted that are clipped at either end; zero for one with no sample.
[[nodiscard]] AFTERNIGHTCONTROLS_API ClippingShare clippingShare(const HistogramDisplayData& counts) noexcept;

/// A share in percent with three decimals and its sign: "0.235 %".
[[nodiscard]] AFTERNIGHTCONTROLS_API QString formatClipShare(double percent);

/// A resolution as the histogram window's combo box names it: "8-bit (256)", "100 levels".
[[nodiscard]] AFTERNIGHTCONTROLS_API QString histogramResolutionName(HistogramResolution resolution);

/// A resolution in short: "8-bit", "100 levels".
[[nodiscard]] AFTERNIGHTCONTROLS_API QString histogramResolutionShortName(HistogramResolution resolution);

/// What the astrometry card reads out of an image's solution, as text.
struct AstrometryReadout
{
    /// Whether the image states a solution; the rest is empty when it does not.
    bool solved = false;

    /// The field centre's right ascension and declination, sexagesimal.
    QString rightAscension;
    QString declination;

    /// The scale in arcseconds a pixel and the rotation in degrees, as numbers: the card writes their units.
    QString scale;
    QString rotation;

    /// The solver the image's cards name - "Internal Gaia" - or empty.
    QString solver;

    /// The distortion's orders - "SIP 4/4" - or empty for a linear solution.
    QString distortion;

    [[nodiscard]] bool operator==(const AstrometryReadout&) const = default;
};

/// What the image card says of an image, as text.
struct ImageFactsReadout
{
    /// "4432 × 3840".
    QString dimensions;

    /// "16 bit unsigned integer".
    QString format;

    /// "97.38 MB".
    QString sizeInMemory;

    /// "RGB" or "Grayscale".
    QString channels;

    /// The facts in one line for the folded card's header: "4432x3840 · 16 bit uint · 97.38 MB".
    QString summary;

    [[nodiscard]] bool operator==(const ImageFactsReadout&) const = default;
};

/// Every resolution, in the window's order.
inline constexpr std::array<HistogramResolution, 11> kHistogramResolutions{
    HistogramResolution::Bits6,      HistogramResolution::Bits7,      HistogramResolution::Bits8,
    HistogramResolution::Bits9,      HistogramResolution::Bits10,     HistogramResolution::Bits12,
    HistogramResolution::Bits14,     HistogramResolution::Bits16,     HistogramResolution::Levels100,
    HistogramResolution::Levels1000, HistogramResolution::Levels10000};

} // namespace an::ui
