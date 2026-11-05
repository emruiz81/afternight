// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a histogram widget draws: each channel's counts in the bins a resolution names, and how many of its samples sit
/// at either end of the normalized range.
///
/// A widget owns no image and no engine context. Whoever owns the image counts it through
/// `widgets/HistogramComputation.h`, on a worker, and hands the widget the result on the GUI thread; the widget keeps
/// the counts, its labels and its drawing state, and nothing else.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace an::ui
{

/// How finely a histogram widget bins an image: by a power of two, or by a round number of levels.
enum class HistogramResolution : std::uint8_t
{
    /// Sixty-four bins.
    Bits6,

    /// One hundred and twenty-eight bins.
    Bits7,

    /// Two hundred and fifty-six bins, the default.
    Bits8,

    /// Five hundred and twelve bins.
    Bits9,

    /// One thousand and twenty-four bins.
    Bits10,

    /// Four thousand and ninety-six bins.
    Bits12,

    /// Sixteen thousand three hundred and eighty-four bins.
    Bits14,

    /// Sixty-five thousand five hundred and thirty-six bins.
    Bits16,

    /// One hundred levels.
    Levels100,

    /// One thousand levels.
    Levels1000,

    /// Ten thousand levels.
    Levels10000
};

/// The bin count a resolution names.
///
/// @param resolution the resolution.
/// @return how many bins it has; 256 for a value outside the enumeration.
[[nodiscard]] constexpr std::size_t histogramBinCount(const HistogramResolution resolution) noexcept
{
    switch (resolution)
    {
    case HistogramResolution::Bits6:
        return 64u;
    case HistogramResolution::Bits7:
        return 128u;
    case HistogramResolution::Bits8:
        return 256u;
    case HistogramResolution::Bits9:
        return 512u;
    case HistogramResolution::Bits10:
        return 1024u;
    case HistogramResolution::Bits12:
        return 4096u;
    case HistogramResolution::Bits14:
        return 16384u;
    case HistogramResolution::Bits16:
        return 65536u;
    case HistogramResolution::Levels100:
        return 100u;
    case HistogramResolution::Levels1000:
        return 1000u;
    case HistogramResolution::Levels10000:
        return 10000u;
    }
    return 256u;
}

/// One channel's counts.
struct HistogramDisplayChannel
{
    /// Samples per bin. Of `n` bins, bin `i` holds the normalized values in `[i / (n - 1), (i + 1) / (n - 1))` and the
    /// last bin holds exactly one, so a saturated sample has a bin of its own; a value outside the unit interval is
    /// counted in the end bin it is nearest.
    std::vector<std::uint64_t> bins;

    /// Samples at or below zero.
    std::uint64_t clippedLow = 0u;

    /// Samples at or above one.
    std::uint64_t clippedHigh = 0u;

    /// Samples that are not a number, which enter no bin.
    std::uint64_t notANumber = 0u;

    /// Samples the image's validity admitted, which is what the clipped counts are a share of.
    std::uint64_t samples = 0u;

    /// The largest bin.
    ///
    /// @return the count of the fullest bin, or zero when there are no bins.
    [[nodiscard]] std::uint64_t maxValue() const noexcept
    {
        return bins.empty() ? 0u : *std::max_element(bins.begin(), bins.end());
    }
};

/// A whole image's counts, one channel per image channel.
struct HistogramDisplayData
{
    /// Each channel's counts, in the image's channel order.
    std::vector<HistogramDisplayChannel> channels;

    /// Whether there is anything to draw.
    ///
    /// @return true when the first channel has bins.
    [[nodiscard]] bool isValid() const noexcept
    {
        return !channels.empty() && !channels.front().bins.empty();
    }
};

} // namespace an::ui
