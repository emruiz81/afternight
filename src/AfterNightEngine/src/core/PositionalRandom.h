// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a random draw that is a function of where it is, not of when it happened.
//
// Two subjects need the same property and for the same reason: a pixel expression that draws a
// random number, and an operation that adds noise to a frame. Both are evaluated on however many
// threads the operation was admitted, in whatever order the scheduler chooses, and both have to give
// the same frame every time. A generator with a state that advances per draw cannot: the value a
// pixel gets would depend on how many pixels were drawn before it, which is a property of the thread
// count. Keying the draw on the coordinate instead removes the question.
//
// The mixing function is the one the pixel expression evaluator has always used, moved here so that
// the second subject cannot acquire a different stream by writing its own.

#pragma once

#include <cstdint>

namespace an
{

/// One deterministic draw for one site.
///
/// The seed and the coordinates are folded together by odd multipliers and then run through
/// SplitMix64's finalizing mix, so two sites that differ in one coordinate by one produce unrelated
/// values. The top 53 bits of the result become the fraction, which is exactly representable in a
/// double. Thread-safe, allocation-free and a pure function of its arguments.
///
/// @param seed the execution's own seed.
/// @param x the sample column.
/// @param y the sample row.
/// @param channel the sample channel.
/// @param index which draw at this site, for a site that needs more than one.
/// @return one value in the half-open interval [0, 1).
[[nodiscard]] inline double positionalUniformDraw(const std::uint64_t seed, const std::uint32_t x,
                                                  const std::uint32_t y, const std::uint32_t channel,
                                                  const std::uint64_t index) noexcept
{
    std::uint64_t state = seed;
    state += static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull;
    state += static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4Full;
    state += static_cast<std::uint64_t>(channel) * 0x165667B19E3779F9ull;
    state += index * 0x27D4EB2F165667C5ull;
    state ^= state >> 30u;
    state *= 0xBF58476D1CE4E5B9ull;
    state ^= state >> 27u;
    state *= 0x94D049BB133111EBull;
    state ^= state >> 31u;
    return static_cast<double>(state >> 11u) * 0x1.0p-53;
}

} // namespace an
