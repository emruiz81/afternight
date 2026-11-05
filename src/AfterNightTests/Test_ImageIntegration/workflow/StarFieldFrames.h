// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Star-field lights for the registration and alignment stages' cases: a field of stars at stated positions, moved by a
/// stated motion from one frame to the next, written by the neutral FITS writer with the cards a case states - a
/// world-coordinate solution, a distortion, when the exposure started.
///
/// A star's position is stated as the neutral layer states it, the first pixel's centre at (0, 0); the engine's pixel
/// coordinates, which every transform the stages record is in, put that centre at (0.5, 0.5). The helpers name no
/// engine type, so a case converts where it compares.

#include "fixtures/common/FixtureImage.h"

#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::integration
{

/// The width and height of every star-field light.
inline constexpr std::uint32_t kStarFieldWidth = 160u;
inline constexpr std::uint32_t kStarFieldHeight = 120u;

/// A deterministic field of stars inside the frame, away from its edges.
///
/// @param count how many stars.
/// @param seed which field.
/// @return the stars, in the neutral layer's coordinates.
[[nodiscard]] std::vector<an::tests::fixtures::FixtureStar> starCatalogue(std::size_t count, std::uint64_t seed);

/// The same stars moved by a motion stated in the neutral layer's coordinates.
[[nodiscard]] std::vector<an::tests::fixtures::FixtureStar>
movedStars(const std::vector<an::tests::fixtures::FixtureStar>& stars,
           const std::function<std::pair<double, double>(double, double)>& motion);

/// A gnomonic solution's cards for a frame whose reference pixel - in FITS's one-based convention - is at
/// `(crpix1, crpix2)`, pointing at (83.8, -5.4) with a scale of 0.36 arcseconds per pixel, and a second-order
/// distortion when `distortionA20` is not zero.
[[nodiscard]] std::vector<an::tests::fixtures::FixtureCard> tanSolutionCards(double crpix1, double crpix2,
                                                                             double distortionA20 = 0.0);

/// The card stating when the exposure started.
[[nodiscard]] an::tests::fixtures::FixtureCard exposureStartCard(const std::string& when);

/// A pixel of a star-field light, its column and row in the neutral layer's coordinates.
using StarFieldPixel = std::pair<std::uint32_t, std::uint32_t>;

/// Writes a grey Float32 light holding the stars over a faint noisy background, with the cards stated.
///
/// @param path the file.
/// @param stars the stars; empty for a starless frame.
/// @param cards the cards it carries.
/// @param seed the noise's seed.
/// @param error the reason, when the file cannot be written.
/// @param background the background's level, which the noise and the stars lie on.
/// @param notANumber the pixels whose sample is not a number, as a calibration writes one where its flat is zero.
/// @return whether it was written.
[[nodiscard]] bool writeStarField(const QString& path, const std::vector<an::tests::fixtures::FixtureStar>& stars,
                                  const std::vector<an::tests::fixtures::FixtureCard>& cards, std::uint64_t seed,
                                  std::string& error, double background = 0.05,
                                  const std::vector<StarFieldPixel>& notANumber = {});

/// Writes the same light as a colour camera's filter array samples it: one Float32 plane under an RGGB array, which
/// the registration reads in colour, every channel holding the stars where the grey light holds them.
///
/// @param path the file.
/// @param stars the stars.
/// @param cards the cards it carries, beside the array's.
/// @param seed the noise's seed.
/// @param error the reason, when the file cannot be written.
/// @return whether it was written.
[[nodiscard]] bool writeMosaicStarField(const QString& path, const std::vector<an::tests::fixtures::FixtureStar>& stars,
                                        const std::vector<an::tests::fixtures::FixtureCard>& cards, std::uint64_t seed,
                                        std::string& error);

/// The brightest sample of a plane read back by the neutral reader, and where it is.
struct BrightestSample
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    double value = 0.0;
};

/// The brightest sample within `radius` pixels of a position in the neutral layer's coordinates.
[[nodiscard]] std::optional<BrightestSample> brightestNear(const QString& path, double x, double y, int radius);

} // namespace an::tests::integration
