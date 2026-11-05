// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a process that makes one image out of several states about the result, and what it reads of its sources'
/// meaning.
///
/// The result's metadata is built whole, by the engine's `combinedImageMetadata`, from the sources, the combining
/// operation's published effect and what the process says it did, so a combination states one exposure and only the
/// facts its sources share.
///
/// A source's linearity is what it declares: the transfer function its meaning states, and, where the meaning states
/// none, the `ANLIN` card the product records it under.
#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CombinedMetadata.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>

#include <optional>
#include <span>
#include <string>
#include <utility>

namespace process_utils
{

/// The metadata of an image a process combined from several: the engine's combination of the sources' metadata, with
/// the lineage entry naming the engine operation the process ran and what the process says it did.
///
/// The first source is the one whose pixel grid the result shares. A combination that restates its result's meaning
/// - every combination the engine publishes does - states it here; one that preserves it states nothing.
///
/// @param sources the metadata of the images combined, the first sharing the result's pixel grid; none null.
/// @param effect the combining operation's published declaration.
/// @param operation the engine operation the process ran, as the engine spells it.
/// @param detail what the process records beside it - its name and its parameters; may be empty.
/// @param semantics the result's meaning, when the effect restates it.
/// @return the result's metadata, or whatever `an::combinedImageMetadata` refused.
[[nodiscard]] inline an::Result<an::ImageMetadata>
combinedProcessMetadata(std::span<const an::ImageMetadata* const> sources, const an::OperationEffect& effect,
                        std::string operation, std::string detail = {},
                        std::optional<an::ImageSemantics> semantics = std::nullopt)
{
    return an::combinedImageMetadata(
        sources, effect,
        an::OperationOutcome{.operation = std::move(operation), .detail = std::move(detail), .semantics = semantics});
}

/// Whether an image's samples are declared linear, declared to have been through a nonlinear encoding, or neither.
///
/// The transfer function the meaning states decides when it states one: `Linear` is linear, and sRGB, Rec. 709 and a
/// pure power law are nonlinear. When the meaning states none, the `ANLIN` card decides.
///
/// @param metadata the image's metadata.
/// @return the declared linearity; `RecordedLinearity::Unknown` when neither the meaning nor the card says.
[[nodiscard]] inline an::RecordedLinearity declaredLinearity(const an::ImageMetadata& metadata)
{
    switch (metadata.semantics().transferFunction())
    {
    case an::TransferFunction::Linear:
        return an::RecordedLinearity::Linear;
    case an::TransferFunction::Srgb:
    case an::TransferFunction::Rec709:
    case an::TransferFunction::PureGamma:
        return an::RecordedLinearity::Nonlinear;
    case an::TransferFunction::Unspecified:
    default:
        return an::readProcessingStateCards(metadata.codec()).linearity;
    }
}

/// Whether every source is declared linear, there being at least one. A null source is passed over.
///
/// @param sources the sources' metadata.
/// @return true when at least one source is given and every one given is declared linear.
[[nodiscard]] inline bool allSourcesDeclaredLinear(std::span<const an::ImageMetadata* const> sources)
{
    bool found = false;
    for (const an::ImageMetadata* source : sources)
    {
        if (source == nullptr)
        {
            continue;
        }
        found = true;
        if (declaredLinearity(*source) != an::RecordedLinearity::Linear)
        {
            return false;
        }
    }
    return found;
}

/// Whether any source is declared to have been through a nonlinear encoding. A null source is passed over, and a source
/// that declares nothing is not one.
///
/// @param sources the sources' metadata.
/// @return true when at least one source is declared nonlinear.
[[nodiscard]] inline bool anySourceDeclaredNonlinear(std::span<const an::ImageMetadata* const> sources)
{
    for (const an::ImageMetadata* source : sources)
    {
        if (source != nullptr && declaredLinearity(*source) == an::RecordedLinearity::Nonlinear)
        {
            return true;
        }
    }
    return false;
}

} // namespace process_utils
