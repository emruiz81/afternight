// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared cosmetic-correction scenarios.
//
// The scenarios plant known defects in a known frame, so the harness can state the answer a run
// must produce rather than trusting the engine's own detection. The implants stand at extreme
// values far from any plausible threshold - which is what makes the classification of a *probe*
// independent of small differences in the engine's statistics, while the population counts the
// engine reports remain measured against the oracle's exact thresholds.
//
// **The replacement definition is stated here.** A repaired sample is the median of
// the window around it - the centre excluded, the window truncated at the frame edge, every value
// read from the defective frame as it stands - and an even surviving count resolves to the mean of
// the two middle samples, computed in Float64 and narrowed once, which is what the engine names
// `QuantileInterpolation::Linear`. The oracle is that
// definition evaluated in the harness, which is what lets the repair row compare bit for bit.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// One planted defect: where it is, what it was forced to, and which polarity it belongs to.
struct CosmeticImplant
{
    /// The defect column.
    std::uint32_t x = 0u;

    /// The defect row.
    std::uint32_t y = 0u;

    /// The value written over the frame's own sample.
    float value = 0.0f;

    /// True for a hot implant, false for a cold one.
    bool hot = false;
};

/// What the oracle derived and expects of a corrected frame.
struct CosmeticOracle
{
    /// The planted defects, in ascending row-major order.
    std::vector<CosmeticImplant> implants;

    /// The exact median of the defective frame.
    double median = 0.0;

    /// The population standard deviation of the defective frame.
    double deviation = 0.0;

    /// `median - coldSigma * deviation`.
    double coldThreshold = 0.0;

    /// `median + hotSigma * deviation`.
    double hotThreshold = 0.0;

    /// How many samples of the defective frame sit at or below the cold threshold.
    std::uint64_t coldCount = 0u;

    /// How many samples sit at or above the hot threshold.
    std::uint64_t hotCount = 0u;

    /// The expected repaired value at each implant, in the implants' order.
    std::vector<double> replacements;

    /// Untouched positions a correct correction must carry across unchanged, row-major.
    std::vector<std::uint32_t> untouched;
};

/// Plants the scenarios' defects into a frame, in place.
///
/// The positions come from a fixed coarse lattice with hashed jitter, at least eight pixels from
/// every frame edge and at least eight pixels apart, so no implant sits in another's replacement
/// window and every window is a complete interior one. Hot implants are forced to one and cold
/// implants to zero, which for the unit-interval synthetic frames is far beyond any threshold a
/// reasonable sigma derives.
///
/// Deterministic: the same extent plants the same defects.
///
/// @param samples the frame to deface, `width * height` contiguous samples.
/// @param width the frame width.
/// @param height the frame height.
/// @return the implants, in ascending row-major order. Empty when the frame is too small to hold
///         the lattice.
std::vector<CosmeticImplant> plantCosmeticDefects(std::span<float> samples, std::uint32_t width, std::uint32_t height);

/// Derives the exact thresholds, counts and expected repairs of a defective frame.
///
/// @param samples the defective frame, implants already planted.
/// @param width the frame width.
/// @param height the frame height.
/// @param implants what was planted.
/// @param coldSigma how many deviations below the median a cold sample sits.
/// @param hotSigma how many deviations above the median a hot sample sits.
/// @param hotRadius half the hot replacement window, in pixels.
/// @param coldRadius half the cold replacement window, in pixels.
/// @return the oracle. Empty implants produce an oracle with statistics and no expectations.
[[nodiscard]] CosmeticOracle computeCosmeticOracle(std::span<const float> samples, std::uint32_t width,
                                                   std::uint32_t height, const std::vector<CosmeticImplant>& implants,
                                                   double coldSigma, double hotSigma, std::uint32_t hotRadius,
                                                   std::uint32_t coldRadius);

/// The largest normalized deviation of a corrected frame from the oracle's expectations.
///
/// Checked at every implant against its expected replacement and at every untouched probe against
/// the defective frame's own sample. Values whose magnitude is below one use absolute error; larger
/// ones use relative error. A non-finite produced sample returns the largest finite double.
///
/// @param produced the corrected frame the runner wrote.
/// @param source the defective frame the runner read.
/// @param width the frame width.
/// @param oracle what the harness expects.
/// @return the largest normalized deviation.
[[nodiscard]] double cosmeticCorrectionError(std::span<const float> produced, std::span<const float> source,
                                             std::uint32_t width, const CosmeticOracle& oracle) noexcept;

/// A canonical representation of a corrected frame at the oracle's probe positions.
///
/// @param produced the corrected frame the runner wrote.
/// @param width the frame width.
/// @param oracle whose implants and untouched probes are reported.
/// @return one line per probe, implants first, in the oracle's order.
[[nodiscard]] std::string cosmeticCorrectionText(std::span<const float> produced, std::uint32_t width,
                                                 const CosmeticOracle& oracle);

/// The deviation of a detection's derived thresholds and counts from the oracle's.
///
/// The thresholds compare as normalized deviations; the counts compare as the fraction of the
/// oracle's population the run missed or invented, so a handful of samples astride a threshold -
/// where a median's precision legitimately decides - stays inside a stated tolerance
/// instead of failing a comparison about nothing.
///
/// @param coldThreshold the threshold the engine derived.
/// @param hotThreshold the threshold the engine derived.
/// @param coldCount the samples the engine marked cold.
/// @param hotCount the samples the engine marked hot.
/// @param oracle what the harness derived.
/// @return the largest of the four deviations.
[[nodiscard]] double cosmeticDetectionError(double coldThreshold, double hotThreshold, std::uint64_t coldCount,
                                            std::uint64_t hotCount, const CosmeticOracle& oracle) noexcept;

/// A canonical representation of what a detection derived and found.
///
/// @param coldThreshold the threshold the engine derived.
/// @param hotThreshold the threshold the engine derived.
/// @param coldCount the samples the engine marked cold.
/// @param hotCount the samples the engine marked hot.
/// @return the four values, one line each.
[[nodiscard]] std::string cosmeticDetectionText(double coldThreshold, double hotThreshold, std::uint64_t coldCount,
                                                std::uint64_t hotCount);

} // namespace an::bench
