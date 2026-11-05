// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How the histogram and statistics windows read a representation as a fraction of its range: an unsigned integer
/// over its largest code, a signed integer from its smallest value to its largest, and a floating sample as it is.
///
/// This is what those windows show - a signed sixteen-bit image's -32768 is the left edge of its histogram and 32767
/// the right - and it is a statement about a display, not about what the samples mean: the image's own numeric domain
/// is never relabelled by it.
#pragma once

#include <AfterNightEngine/Image/SampleType.h>

namespace an::ui
{

/// One representation's display range: a sample `v` is shown as `(v - lowest) / range`.
struct DisplayNormalization
{
    /// The value shown as zero.
    double lowest = 0.0;

    /// How far above `lowest` the value shown as one is.
    double range = 1.0;

    /// Whether the representation is an integer one, whose values are whole codes.
    bool integer = false;
};

/// The display range of a representation.
///
/// @param sampleType the representation.
/// @return its range; the unit interval for a floating representation or a value outside the enumeration.
[[nodiscard]] constexpr DisplayNormalization displayNormalization(const an::SampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case an::SampleType::UInt8:
        return DisplayNormalization{.lowest = 0.0, .range = 255.0, .integer = true};
    case an::SampleType::Int8:
        return DisplayNormalization{.lowest = -128.0, .range = 255.0, .integer = true};
    case an::SampleType::UInt16:
        return DisplayNormalization{.lowest = 0.0, .range = 65535.0, .integer = true};
    case an::SampleType::Int16:
        return DisplayNormalization{.lowest = -32768.0, .range = 65535.0, .integer = true};
    case an::SampleType::UInt32:
        return DisplayNormalization{.lowest = 0.0, .range = 4294967295.0, .integer = true};
    case an::SampleType::Int32:
        return DisplayNormalization{.lowest = -2147483648.0, .range = 4294967295.0, .integer = true};
    default:
        return DisplayNormalization{};
    }
}

/// A sample as its display shows it.
///
/// @param normalization the representation's display range.
/// @param value the sample, in the representation's own numbers.
/// @return the fraction of the range it stands at; outside the unit interval for a floating sample outside it.
[[nodiscard]] constexpr double displayFraction(const DisplayNormalization& normalization, const double value) noexcept
{
    return (value - normalization.lowest) / normalization.range;
}

} // namespace an::ui
