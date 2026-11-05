// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the shared polynomial distortion the coordinate-map row measures.
//
// One model, spelled once, so two runs cannot drift apart in the thing being compared. The
// coefficients are a second-order forward-only fit of the kind a plate solve produces for a short
// refractor: a few thousandths of a pixel at the centre growing to about half a pixel at the
// corners of a one-megapixel frame.
//
// Forward-only is the point of the row. A header that carries the written inverse polynomials makes
// the inverse map one polynomial evaluation per pixel; a header that does not makes it a Newton
// solve per pixel, and that is the expensive path the engine actually takes on solver output that
// omits `AP`/`BP`.

#pragma once

#include <cstdint>

namespace an::bench
{

/// Width of the mapped grid, in pixels.
inline constexpr std::uint32_t kSipFixtureWidth = 1024u;

/// Height of the mapped grid, in pixels.
inline constexpr std::uint32_t kSipFixtureHeight = 1024u;

/// The reference pixel, in the engine's coordinates - the first pixel's centre is (0.5, 0.5).
///
/// The frame centre, which is where a solver puts it.
inline constexpr double kSipFixtureReferenceX = 512.0;

/// The reference pixel's row, in the same convention.
inline constexpr double kSipFixtureReferenceY = 512.0;

/// The horizontal component's quadratic coefficients, for the terms in `u^2`, `u*v` and `v^2`.
inline constexpr double kSipFixtureA20 = 1.7400000000e-06;
inline constexpr double kSipFixtureA11 = -6.2000000000e-07;
inline constexpr double kSipFixtureA02 = 4.1000000000e-07;

/// The vertical component's quadratic coefficients, for the same three terms.
inline constexpr double kSipFixtureB20 = 3.9000000000e-07;
inline constexpr double kSipFixtureB11 = 1.3100000000e-06;
inline constexpr double kSipFixtureB02 = -1.5800000000e-06;

/// The name the scenario declares, so it cannot be answered by a different model.
inline constexpr const char* kSipFixtureMapping = "sip-inverse-map-v1";

} // namespace an::bench
