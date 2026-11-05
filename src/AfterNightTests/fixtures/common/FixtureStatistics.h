// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// Statistics of a sample set, computed here rather than by the engine, so an expected value a test compares
// the engine's answer with is not the engine's own answer. Every figure is stated: the sum and mean accumulate
// in long double - which is double under MSVC, so a sum is exact while it stays below 2^53 - over the finite samples
// in row order, and the median of an even count is the mean of the two middle values.

#include "FixtureImage.h"

#include <cstdint>
#include <vector>

namespace an::tests::fixtures
{

/// One channel's statistics over its finite samples; `nonFinite` counts the rest.
struct FixtureChannelStatistics
{
    std::uint64_t count = 0u;
    std::uint64_t nonFinite = 0u;
    double minimum = 0.0;
    double maximum = 0.0;
    double sum = 0.0;
    double mean = 0.0;
    double median = 0.0;
};

/// Per-channel statistics, one entry per channel. A channel with no finite sample reports zeros and its count
/// of non-finite ones.
[[nodiscard]] std::vector<FixtureChannelStatistics> computeFixtureStatistics(const FixtureSamples& samples);

} // namespace an::tests::fixtures
