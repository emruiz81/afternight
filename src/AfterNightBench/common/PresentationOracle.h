// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent display presentation definition and complete-output qualification.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// One channel's exact location: the median of its samples and the median of their absolute deviations from it,
/// each the mean of the two middle values for an even count.
struct PresentationLocation
{
    double median = 0.0;
    double deviation = 0.0;
};

/// One channel's midtones transfer.
struct PresentationTransfer
{
    long double midtones = 0.5L;
    long double shadows = 0.0L;
    long double highlights = 1.0L;
};

/// The exact location of a channel's samples, each held to the unit interval first, as a display reads them.
[[nodiscard]] PresentationLocation presentationLocation(std::span<const float> samples);

/// The unlinked automatic stretch, written from its definition: per channel, a shadow point `clipping` normalized
/// deviations from the median, no lower than zero, and the balance that maps the median's height above it to
/// `target` - or, when every channel's median is above one half, the mirror of that about the highlights. The
/// normalized deviation is the median absolute deviation over the inverse normal distribution at three quarters,
/// and a zero deviation counts as one thousandth.
[[nodiscard]] std::vector<PresentationTransfer>
unlinkedPresentationTransfers(std::span<const PresentationLocation> locations, double clipping, double target);

/// The midtones transfer, written from its definition.
[[nodiscard]] long double presentationTransfer(long double value, const PresentationTransfer& transfer);

/// A display byte: the value held to the unit interval, times 255, rounded to nearest with ties to even.
[[nodiscard]] std::uint8_t presentationByte(long double value);

/// True for the presentation rows.
[[nodiscard]] bool requiresPresentationOracle(const ScenarioSpec& scenario);

/// Holds an automatic stretch's packed output to the definition: planar Float32 input, four bytes a pixel in red,
/// green, blue and alpha order. Sets the correctness metric to the largest difference in codes, and counts the
/// samples that differ and the alpha bytes that are not opaque.
void checkPresentationAutostretchOutput(std::span<const float> planarInput, std::uint32_t width, std::uint32_t height,
                                        std::span<const std::uint8_t> rgba, ResultRecord& result);

/// Holds a subsampled linear output to the definition: every `factor`-th sample of every `factor`-th row of a
/// sixteen-bit frame, as its fraction of full scale in bytes. Sets the correctness metric to the largest difference in
/// codes.
void checkPresentationSubsampledOutput(std::span<const std::uint16_t> input, std::uint32_t width, std::uint32_t height,
                                       std::uint32_t factor, std::span<const std::uint8_t> output,
                                       ResultRecord& result);

} // namespace an::bench
