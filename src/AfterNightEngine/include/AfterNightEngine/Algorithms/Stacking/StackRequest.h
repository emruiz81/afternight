// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Everything a caller states about a stack, as one plain aggregate.
///
/// `StackRequest` gathers the input's description with the combination, the rejection, the frame
/// and local weighting, the normalization and the output, each a small value from its own header.
/// A request is only a statement: `StackModel::check` in `Algorithms/Stacking/StackingModel.h`
/// decides whether it describes a stack, and `StackModel::create` or `makeStackModel` is the only
/// way from a request to the checked model every stacking operation takes.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <cstdint>

namespace an
{

/// What the stack is told about its input frames before any of them is read.
///
/// Every frame of one stack is described alike: one extent, one representation, one channel
/// schema, one colour basis, one numeric domain and one filter array. A frame that differs is
/// refused where it is read, against this description. Which samples of each frame mean something
/// is each frame's own claim and travels with the frame rather than here.
struct StackInputDescription
{
    /// How many frames the stack combines. Between one and `kMaxStackFrames`.
    std::uint32_t frameCount = 0u;

    /// The description every frame must match.
    ImageDescriptor descriptor;

    /// Whether two descriptions state the same input.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackInputDescription& left,
                                                   const StackInputDescription& right) noexcept = default;
};

/// Everything a caller states about a stack.
///
/// A plain aggregate, built with designated initializers so that a call site names what it sets.
/// It has no default constructor because the input description has none: a stack of no described
/// frames is not a stack.
struct StackRequest
{
    /// The frames.
    StackInputDescription input;

    /// How the surviving samples of a pixel become its value.
    StackCombination combination = StackCombination::Average;

    /// Which samples survive.
    StackRejectionRequest rejection{};

    /// Where frame weights come from.
    StackWeightingRequest weighting{};

    /// Whether a local weight surface is applied.
    LocalWeightingRequest localWeighting{};

    /// Which normalization is applied first.
    StackNormalizationRequest normalization{};

    /// What is written.
    StackOutputRequest output{};

    /// Whether two requests state the same stack.
    ///
    /// Not `noexcept`, because the normalization request's comparison is not.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackRequest& left, const StackRequest& right) = default;
};

} // namespace an
