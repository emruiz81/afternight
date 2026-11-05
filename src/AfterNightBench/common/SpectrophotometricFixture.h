// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - shared spectrophotometric fixtures: a table of sampled spectra with
// an analytic synthetic-photometry oracle, colour samples with planted channel scales for the
// repeated-median fit, and the scaled-sky oracle for the gain-and-offset apply.
//
// The engine adapter integrates the spectra through one constant response, fits the samples and
// applies the gains to the planted field. Every row is scored against a truth that is known before
// the engine runs, so an error shows up as the engine's own distance from that truth rather than as
// a digest nobody can attribute.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// How many wavelength samples one fixture spectrum carries: the sampled Gaia XP grid's count.
inline constexpr std::uint32_t kSpectrophotometricFixtureSampleCount = 343u;

/// The first sampled wavelength, in nanometres.
inline constexpr double kSpectrophotometricFixtureStartNm = 336.0;

/// The sampling step, in nanometres.
inline constexpr double kSpectrophotometricFixtureStepNm = 2.0;

/// The last sampled wavelength, in nanometres.
inline constexpr double kSpectrophotometricFixtureEndNm = 1020.0;

/// How many spectra the synthetic-photometry row integrates.
inline constexpr std::uint32_t kSyntheticPhotometryFixtureSpectra = 1024u;

/// The constant throughput of each channel of the fixture response: red, green, blue.
inline constexpr std::array<double, 3u> kSyntheticPhotometryFixtureResponse{2.0, 1.0, 0.5};

/// Stable name of the engine-neutral spectrum mapping.
inline constexpr const char* kSyntheticPhotometryFixtureMapping = "xp-linear-spectra-v1";

/// The red channel scale the fit samples were rendered with; a perfect fit recovers one over it.
inline constexpr double kSpectrophotometricFitFixtureRedScale = 1.25;

/// The blue channel scale the fit samples were rendered with.
inline constexpr double kSpectrophotometricFitFixtureBlueScale = 0.8;

/// Stable name of the engine-neutral colour-sample mapping.
inline constexpr const char* kSpectrophotometricFitFixtureMapping = "spcc-color-samples-v1";

/// The gains the apply row writes, red, green and blue, already normalized so the largest is one.
inline constexpr std::array<double, 3u> kSpectrophotometricApplyFixtureGains{0.64, 0.8, 1.0};

/// Stable name of the apply row's mapping: the planted aperture field under these gains with no
/// sky neutralization, so the row times the fused gain-and-offset pass alone.
inline constexpr const char* kSpectrophotometricApplyFixtureMapping = "planted-rgb-spcc-apply-v1";

/// The wavelength of one fixture grid sample.
///
/// @param index the sample.
/// @return the wavelength, in nanometres.
[[nodiscard]] inline double spectrophotometricFixtureWavelengthNm(std::uint32_t index) noexcept
{
    return kSpectrophotometricFixtureStartNm + static_cast<double>(index) * kSpectrophotometricFixtureStepNm;
}

/// A table of linear spectra: `flux(lambda) = intercept + slope * lambda`, with a constant
/// uncertainty per spectrum.
struct SyntheticPhotometryFixture
{
    /// How many spectra.
    std::size_t count = 0u;

    /// One intercept per spectrum.
    std::vector<double> intercepts{};

    /// One slope per spectrum, per nanometre.
    std::vector<double> slopes{};

    /// One uncertainty per spectrum, applied to every sample.
    std::vector<double> errors{};

    /// `count * kSpectrophotometricFixtureSampleCount` flux samples, spectrum-major.
    std::vector<float> flux{};

    /// The same number of uncertainties, spectrum-major.
    std::vector<float> fluxError{};
};

/// Derives linear spectra from a benchmark fixture both adapters already share.
///
/// The intercept sits in [0.5, 1.5) and the slope in [-4e-4, 4e-4) per nanometre, so every sample
/// stays positive across the grid; the uncertainty is one per cent of the intercept. Using the
/// fixture samples as the entropy source makes its recorded digest prove the identity of the spectra.
///
/// @param source finite normalized samples, at least `count` of them.
/// @param count how many spectra to derive.
/// @return the table.
[[nodiscard]] inline SyntheticPhotometryFixture makeSyntheticPhotometryFixture(std::span<const float> source,
                                                                               std::size_t count)
{
    SyntheticPhotometryFixture fixture;
    fixture.count = (std::min)(count, source.size());
    fixture.intercepts.resize(fixture.count);
    fixture.slopes.resize(fixture.count);
    fixture.errors.resize(fixture.count);
    fixture.flux.resize(fixture.count * kSpectrophotometricFixtureSampleCount);
    fixture.fluxError.resize(fixture.count * kSpectrophotometricFixtureSampleCount);
    for (std::size_t spectrum = 0u; spectrum < fixture.count; ++spectrum)
    {
        const double value = std::abs(static_cast<double>(source[spectrum]));
        const double intercept = 0.5 + std::fmod(value * 3.1 + static_cast<double>(spectrum % 89u) * 0.011, 1.0);
        const double slope =
            -4.0e-4 + 8.0e-4 * std::fmod(value * 5.3 + static_cast<double>(spectrum % 61u) * 0.017, 1.0);
        fixture.intercepts[spectrum] = intercept;
        fixture.slopes[spectrum] = slope;
        fixture.errors[spectrum] = 0.01 * intercept;
        for (std::uint32_t index = 0u; index < kSpectrophotometricFixtureSampleCount; ++index)
        {
            const std::size_t at = spectrum * kSpectrophotometricFixtureSampleCount + index;
            fixture.flux[at] = static_cast<float>(intercept + slope * spectrophotometricFixtureWavelengthNm(index));
            fixture.fluxError[at] = static_cast<float>(fixture.errors[spectrum]);
        }
    }
    return fixture;
}

/// The analytic photon-weighted count of one linear spectrum through one constant response.
///
/// `response * integral of lambda * (intercept + slope * lambda)` over the grid's interval, which
/// is what an exact integration of the sampled product returns.
///
/// @param intercept the spectrum's intercept.
/// @param slope the spectrum's slope.
/// @param response the channel's constant throughput.
/// @return the count.
[[nodiscard]] inline double expectedSyntheticCount(double intercept, double slope, double response) noexcept
{
    const double start = kSpectrophotometricFixtureStartNm;
    const double end = kSpectrophotometricFixtureEndNm;
    const double quadratic = (end * end - start * start) * 0.5;
    const double cubic = (end * end * end - start * start * start) / 3.0;
    return response * (intercept * quadratic + slope * cubic);
}

/// The analytic propagated uncertainty of one constant-error spectrum through one constant response
/// under the trapezoidal weighting the engine applies.
///
/// @param error the spectrum's per-sample uncertainty.
/// @param response the channel's constant throughput.
/// @return the standard uncertainty of the count.
[[nodiscard]] inline double expectedSyntheticUncertainty(double error, double response) noexcept
{
    double variance = 0.0;
    for (std::uint32_t index = 0u; index < kSpectrophotometricFixtureSampleCount; ++index)
    {
        const double lambda = spectrophotometricFixtureWavelengthNm(index);
        const double endpoint = (index == 0u || index + 1u == kSpectrophotometricFixtureSampleCount) ? 0.5 : 1.0;
        const double weight = endpoint * kSpectrophotometricFixtureStepNm * lambda * response;
        variance += (error * weight) * (error * weight);
    }
    return std::sqrt(variance);
}

/// How far a set of integrated counts and uncertainties sits from the analytic answer.
///
/// @param fixture the spectra.
/// @param counts `3 * count` counts, spectrum-major, red then green then blue.
/// @param uncertainties `3 * count` uncertainties in the same order.
/// @return the largest relative distance over every spectrum and channel, or one for an empty set.
[[nodiscard]] inline double scoreSyntheticPhotometry(const SyntheticPhotometryFixture& fixture,
                                                     const std::vector<double>& counts,
                                                     const std::vector<double>& uncertainties) noexcept
{
    if (fixture.count == 0u || counts.size() < fixture.count * 3u || uncertainties.size() < fixture.count * 3u)
    {
        return 1.0;
    }
    double worst = 0.0;
    for (std::size_t spectrum = 0u; spectrum < fixture.count; ++spectrum)
    {
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            const double response = kSyntheticPhotometryFixtureResponse[channel];
            const double expectedCount =
                expectedSyntheticCount(fixture.intercepts[spectrum], fixture.slopes[spectrum], response);
            const double expectedUncertainty = expectedSyntheticUncertainty(fixture.errors[spectrum], response);
            worst = (std::max)(worst, std::abs(counts[spectrum * 3u + channel] / expectedCount - 1.0));
            worst = (std::max)(worst, std::abs(uncertainties[spectrum * 3u + channel] / expectedUncertainty - 1.0));
        }
    }
    return worst;
}

/// One spectrophotometric colour sample, in engine-neutral terms.
struct SpectrophotometricFitFixtureSample
{
    /// The catalog's predicted red-to-green ratio.
    double catalogRed = 1.0;

    /// The catalog's predicted blue-to-green ratio.
    double catalogBlue = 1.0;

    /// The measured red-to-green ratio.
    double imageRed = 1.0;

    /// The measured blue-to-green ratio.
    double imageBlue = 1.0;
};

/// Derives colour samples from a benchmark fixture both adapters already share.
///
/// The catalog ratios span [0.6, 1.6) in red and [0.5, 1.5) in blue; the image ratios are the
/// catalog ratios times the planted scales, except that every seventeenth sample is a gross red
/// outlier and every nineteenth a gross blue one, which the robust line is expected to clip.
///
/// @param source finite normalized samples, one per colour sample.
/// @return one deterministic colour sample per source sample, in the same order.
[[nodiscard]] inline std::vector<SpectrophotometricFitFixtureSample>
makeSpectrophotometricFitFixture(std::span<const float> source)
{
    std::vector<SpectrophotometricFitFixtureSample> result(source.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        const double value = std::abs(static_cast<double>(source[index]));
        SpectrophotometricFitFixtureSample& sample = result[index];
        sample.catalogRed = 0.6 + std::fmod(value * 2.7 + static_cast<double>(index % 101u) * 0.013, 1.0);
        sample.catalogBlue = 0.5 + std::fmod(value * 4.3 + static_cast<double>(index % 67u) * 0.019, 1.0);
        sample.imageRed = sample.catalogRed * kSpectrophotometricFitFixtureRedScale;
        sample.imageBlue = sample.catalogBlue * kSpectrophotometricFitFixtureBlueScale;
        if (index % 17u == 0u)
        {
            sample.imageRed *= 3.0;
        }
        if (index % 19u == 0u)
        {
            sample.imageBlue *= 0.25;
        }
    }
    return result;
}

/// How far recovered gains sit from the planted answer.
///
/// The raw gains are one over the planted scales, normalized so the largest is one, so the expected
/// red gain is `(1 / 1.25) / (1 / 0.8)` and the expected blue gain is one.
///
/// @param redGain the fitted red gain.
/// @param blueGain the fitted blue gain.
/// @return the larger relative distance of the two from their expected values.
[[nodiscard]] inline double scoreSpectrophotometricFit(double redGain, double blueGain) noexcept
{
    const double rawRed = 1.0 / kSpectrophotometricFitFixtureRedScale;
    const double rawBlue = 1.0 / kSpectrophotometricFitFixtureBlueScale;
    const double largest = (std::max)((std::max)(rawRed, 1.0), rawBlue);
    const double red = std::abs(redGain / (rawRed / largest) - 1.0);
    const double blue = std::abs(blueGain / (rawBlue / largest) - 1.0);
    return red > blue ? red : blue;
}

/// How far a written frame's sky sits from the planted sky times the gains.
///
/// Each channel's median over the whole plane stands for its sky; with no neutralization a perfectly
/// applied frame has every channel's median at the planted background times that channel's gain.
///
/// @param destination the three written planes, each `planeSize` long, concatenated.
/// @param planeSize how many samples one plane holds.
/// @param plantedBackground the per-channel sky the field was planted with.
/// @param gains the per-channel gains that were written.
/// @return the largest relative distance of a channel's median from its expected sky.
[[nodiscard]] inline double scoreSpectrophotometricApply(std::span<const float> destination, std::size_t planeSize,
                                                         const double (&plantedBackground)[3],
                                                         const std::array<double, 3u>& gains)
{
    if (planeSize == 0u || destination.size() < planeSize * 3u)
    {
        return 1.0;
    }
    double worst = 0.0;
    std::vector<float> plane(planeSize);
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        std::copy(destination.begin() + static_cast<std::ptrdiff_t>(channel * planeSize),
                  destination.begin() + static_cast<std::ptrdiff_t>((channel + 1u) * planeSize), plane.begin());
        std::nth_element(plane.begin(), plane.begin() + static_cast<std::ptrdiff_t>(planeSize / 2u), plane.end());
        const double median = static_cast<double>(plane[planeSize / 2u]);
        const double expected = plantedBackground[channel] * gains[channel];
        worst = (std::max)(worst, std::abs(median / expected - 1.0));
    }
    return worst;
}

} // namespace an::bench
