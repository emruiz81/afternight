// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared basic-statistics scenario.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace an::bench
{

/// The result fields the engine exposes for the basic-statistics benchmark.
struct BasicStatisticsOracle
{
    std::size_t count = 0u;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double populationVariance = 0.0;
};

/// A rectangular benchmark selection with a deterministic periodic validity mask.
///
/// Coordinates are in the full source plane. `excludedEvery` counts pixels in row-major order
/// within the rectangle: zero selects every pixel and a positive value excludes ordinal zero and
/// every multiple after it. Both runner adapters use this object to build their engine-specific
/// validity representation, so the oracle and the measured calls cannot disagree about a bit.
struct BasicStatisticsOracleWindow
{
    std::uint32_t sourceWidth = 0u;
    std::uint32_t sourceHeight = 0u;
    std::uint32_t originX = 0u;
    std::uint32_t originY = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t excludedEvery = 0u;

    /// Whether the rectangle is non-empty and contained by the source.
    [[nodiscard]] bool isValid() const noexcept;

    /// Whether one full-source coordinate participates in the selection.
    [[nodiscard]] bool selects(std::uint32_t x, std::uint32_t y) const noexcept;
};

/// Computes the benchmark's independent two-pass reference in `long double`.
///
/// Returns no value for an empty input or a non-finite sample, because neither is a valid fixture
/// for the scenario that uses this oracle.
[[nodiscard]] std::optional<BasicStatisticsOracle>
computeBasicStatisticsOracle(std::span<const float> samples) noexcept;

/// Computes the same independent reference over a rectangular periodic-mask selection.
///
/// Returns no value when the window is invalid, the source span does not exactly cover its stated
/// dimensions, the mask selects nothing, or a selected sample is non-finite.
[[nodiscard]] std::optional<BasicStatisticsOracle>
computeBasicStatisticsOracle(std::span<const float> samples, const BasicStatisticsOracleWindow& window) noexcept;

/// The largest normalized error in `actual` relative to `expected`.
///
/// Values whose magnitude is below one use absolute error; larger values use relative error. A
/// count mismatch or non-finite field returns the largest finite double so a result remains
/// schema-valid and fails its declared tolerance.
[[nodiscard]] double basicStatisticsError(const BasicStatisticsOracle& actual,
                                          const BasicStatisticsOracle& expected) noexcept;

/// A canonical full-output representation shared by both runner adapters.
[[nodiscard]] std::string basicStatisticsText(const BasicStatisticsOracle& statistics);

} // namespace an::bench
