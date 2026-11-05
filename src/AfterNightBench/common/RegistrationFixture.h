// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the shared registration transform the coordinate-map row measures.
//
// One model, spelled once, so two runs cannot drift apart in the thing being compared. The
// linear part is what a star match of two frames of one night produces - a little under a degree of
// field rotation, a scale within a part in a thousand, and a translation of a few pixels - and the
// polynomial on top of it is a second-order correction of the size a plate with real field
// curvature leaves behind once that linear part has been removed.
//
// The polynomial is the point of the row. A linear transform inverts in closed form and costs a
// matrix multiply per pixel; a polynomial does not invert at all, so every pixel of the destination
// grid is its own Newton solve of the composed model. That is the expensive half of registration
// planning in the engine, and it is what a warp of a distorted frame actually pays.
//
// The coefficients are stated in the term order the engine uses: ascending total degree and, in
// one degree, ascending vertical degree - `1, x, y, x^2, xy, y^2`.

#pragma once

#include <cstdint>

namespace an::bench
{

/// Width of the mapped destination grid, in pixels.
inline constexpr std::uint32_t kRegistrationFixtureWidth = 1024u;

/// Height of the mapped destination grid, in pixels.
inline constexpr std::uint32_t kRegistrationFixtureHeight = 1024u;

/// The linear part, row-major, mapping a source position to a reference position.
inline constexpr double kRegistrationFixtureMatrix00 = 0.99987;
inline constexpr double kRegistrationFixtureMatrix01 = -0.01310;
inline constexpr double kRegistrationFixtureMatrix02 = 7.25;
inline constexpr double kRegistrationFixtureMatrix10 = 0.01310;
inline constexpr double kRegistrationFixtureMatrix11 = 0.99987;
inline constexpr double kRegistrationFixtureMatrix12 = -3.75;

/// The offsets the polynomial's normalization measures from, and the divisor it uses.
inline constexpr double kRegistrationFixtureNormalizationOriginX = 0.0;
inline constexpr double kRegistrationFixtureNormalizationOriginY = 0.0;
inline constexpr double kRegistrationFixtureNormalizationScaleX = 1024.0;
inline constexpr double kRegistrationFixtureNormalizationScaleY = 1024.0;

/// The horizontal component's coefficients, for the terms `1, x, y, x^2, xy, y^2`.
inline constexpr double kRegistrationFixtureX0 = 0.0;
inline constexpr double kRegistrationFixtureX1 = 1.0;
inline constexpr double kRegistrationFixtureX2 = 0.0;
inline constexpr double kRegistrationFixtureX3 = 3.1000000000e-03;
inline constexpr double kRegistrationFixtureX4 = -1.4000000000e-03;
inline constexpr double kRegistrationFixtureX5 = 8.0000000000e-04;

/// The vertical component's coefficients, for the same six terms.
inline constexpr double kRegistrationFixtureY0 = 0.0;
inline constexpr double kRegistrationFixtureY1 = 0.0;
inline constexpr double kRegistrationFixtureY2 = 1.0;
inline constexpr double kRegistrationFixtureY3 = 9.0000000000e-04;
inline constexpr double kRegistrationFixtureY4 = 2.7000000000e-03;
inline constexpr double kRegistrationFixtureY5 = -1.2000000000e-03;

/// The name the scenario declares, so it cannot be answered by a different model.
inline constexpr const char* kRegistrationFixtureMapping = "registration-destination-map-v1";

} // namespace an::bench
