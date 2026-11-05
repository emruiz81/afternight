// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a caller holds ready before a stack runs, counted as the plan counts it.
///
/// A stack plan carries one figure the planner cannot derive: `StackPlanningRequest::preparedStateBytes`,
/// the memory the caller already holds for this stack and keeps resident through every pass - the
/// normalization it measured beforehand, the local-normalization document it read from disk, the
/// photometric weights it computed. The planner counts that figure as persistent state so the peak
/// it reports is the process's peak and not only the engine's. The walk over what a caller holds is
/// written once, beside the types it walks, so a caller states what it holds and gets the figure the
/// plan takes.
///
/// Residency is what is reserved rather than what is used: a vector's capacity, not its size, is
/// what stays resident, so a document whose vectors grew and were trimmed counts what they kept.
/// The object itself is counted too, wherever it lives, because it is resident for the whole run.
/// Nothing here is checked arithmetic, on purpose: an object that exists in memory occupies fewer
/// bytes than the address space holds, so its residency is representable by construction, and a
/// sum over a handful of such objects is too.

#pragma once

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>

#include <cstdint>
#include <span>

namespace an
{

/// The bytes a set of global normalization matches keeps resident: one record per frame.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param matches the matches the caller holds, one per frame.
/// @return their residency in bytes.
[[nodiscard]] constexpr std::uint64_t preparedStateBytesOf(std::span<const FrameNormalization> matches) noexcept
{
    return static_cast<std::uint64_t>(matches.size()) * sizeof(FrameNormalization);
}

/// The bytes a local normalization document keeps resident: the record itself, the reference
/// grid, every frame's record, and every frame's background and scale grids, each at its
/// capacity.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param document the document the caller holds.
/// @return its residency in bytes.
[[nodiscard]] constexpr std::uint64_t preparedStateBytesOf(const LocalNormalizationDocument& document) noexcept
{
    std::uint64_t bytes = sizeof(LocalNormalizationDocument);
    bytes += static_cast<std::uint64_t>(document.referenceBackground.capacity()) * sizeof(double);
    bytes += static_cast<std::uint64_t>(document.frames.capacity()) * sizeof(LocalNormalizationFrameCoefficients);
    for (const LocalNormalizationFrameCoefficients& frame : document.frames)
    {
        bytes += static_cast<std::uint64_t>(frame.background.capacity()) * sizeof(double);
        bytes += static_cast<std::uint64_t>(frame.scaleGrid.capacity()) * sizeof(double);
    }
    return bytes;
}

/// The bytes a set of stack-ready photometric weights keeps resident: one record per frame.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param weights the weights the caller holds, one per frame.
/// @return their residency in bytes.
[[nodiscard]] constexpr std::uint64_t preparedStateBytesOf(std::span<const PhotometricStackWeight> weights) noexcept
{
    return static_cast<std::uint64_t>(weights.size()) * sizeof(PhotometricStackWeight);
}

/// What a caller holds ready for a stack, borrowed for one accounting call.
///
/// A plain aggregate with an empty default for every member, so a caller names only what it
/// holds. Nothing here is owned: the spans and the pointer borrow the caller's objects for the
/// duration of `preparedStateBytesOf` and no longer.
struct StackPreparedState
{
    /// The global normalization matches, one per frame, or nothing.
    std::span<const FrameNormalization> normalization{};

    /// The local normalization document, or null.
    const LocalNormalizationDocument* localNormalization = nullptr;

    /// The stack-ready photometric weights, one per frame, or nothing.
    std::span<const PhotometricStackWeight> photometricWeights{};
};

/// The bytes everything a caller holds ready keeps resident, which is the figure
/// `StackPlanningRequest::preparedStateBytes` takes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param state what the caller holds.
/// @return the sum of each member's residency.
[[nodiscard]] constexpr std::uint64_t preparedStateBytesOf(const StackPreparedState& state) noexcept
{
    std::uint64_t bytes = preparedStateBytesOf(state.normalization) + preparedStateBytesOf(state.photometricWeights);
    if (state.localNormalization != nullptr)
    {
        bytes += preparedStateBytesOf(*state.localNormalization);
    }
    return bytes;
}

} // namespace an
