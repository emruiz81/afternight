// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definition of the comparison view's image difference.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// The second frame of the difference row: the first, read from a fixed offset of seven rows and three samples
/// further on and wrapped at the end, so the two frames differ almost everywhere and every run derives the same one.
[[nodiscard]] std::vector<float> differenceSecondInput(std::span<const float> first, std::uint32_t width);

/// True for the image difference row.
[[nodiscard]] bool requiresDifferenceOracle(const ScenarioSpec& scenario);

/// Holds a planar Float32 difference to its definition: every sample the absolute difference of the two inputs'
/// samples, computed in double and narrowed once. Sets the correctness metric to the largest absolute difference from
/// it, and counts the samples that differ at all.
void checkImageDifferenceOutput(std::span<const float> first, std::span<const float> second,
                                std::span<const float> output, ResultRecord& result);

} // namespace an::bench
