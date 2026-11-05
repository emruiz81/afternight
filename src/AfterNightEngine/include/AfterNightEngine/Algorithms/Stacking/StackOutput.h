// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a stack request asks to be written: the result's representation and the maps beside it.
///
/// `StackOutputRequest` names the combined image's floating representation and which per-pixel
/// maps are written - the rejection maps in the form `RejectionMapForm` states, the coverage, the
/// variance, the signal-to-noise gain and the weight sum. The maps are handed over as the counts
/// and values the stack computed, not divided by the frame count or clamped to the unit interval.
#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <cstdint>

namespace an
{

/// Which rejection maps a stack writes beside its result.
///
/// A rejection map counts, per output pixel, how many samples the rejection removed or replaced.
enum class RejectionMapForm : std::uint8_t
{
    /// No map is written.
    None,

    /// One map counting rejections on either side together.
    Merged,

    /// Two maps, one counting samples rejected below the centre and one above it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    LowAndHigh
};

/// Whether a value names a map form this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param form the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRejectionMapForm(RejectionMapForm form) noexcept
{
    return static_cast<std::uint8_t>(form) <= static_cast<std::uint8_t>(RejectionMapForm::LowAndHigh);
}

/// The form's own name.
///
/// Thread-safe and allocation-free.
///
/// @param form the form to name. A value outside the enumeration is named
///        `"UnknownRejectionMapForm"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* rejectionMapFormName(RejectionMapForm form) noexcept;

/// What the stack writes, beyond the combined image.
struct StackOutputRequest
{
    /// The representation of the combined image. `SampleType::Float32` or `SampleType::Float64`:
    /// the accumulation is floating and an integer output is a conversion the caller states
    /// afterwards, under a conversion policy of its own.
    SampleType sampleType = SampleType::Float32;

    /// Which rejection maps are written. Anything but `RejectionMapForm::None` needs a rejection.
    RejectionMapForm rejectionMaps = RejectionMapForm::None;

    /// Whether a map counting the samples that contributed to each output pixel is written.
    bool coverageMap = false;

    /// Whether a map of each output pixel's sample variance is written.
    bool varianceMap = false;

    /// Whether a map of each output pixel's signal-to-noise gain is written.
    bool signalToNoiseGainMap = false;

    /// Whether a map of each output pixel's summed weight is written.
    bool weightSumMap = false;

    /// Whether two requests state the same output.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackOutputRequest& left,
                                                   const StackOutputRequest& right) noexcept = default;
};

} // namespace an
