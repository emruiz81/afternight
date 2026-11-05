// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The metadata of an image made by combining several: what it carries forward from its sources, and the
/// fresh claims it makes instead of inheriting its first source's.
///
/// A blend, an LRGB combination, a channel composition or a star recombination writes one image from two or
/// more. Its metadata is a function of the sources - the exposures summed, a value kept only where every
/// source agrees, the filter removed, and the processing state cleared so the result makes its own claim -
/// written in the style of `IO/ObservationCards.h`.
///
/// It is not a `MetadataTransition`, and cannot be: a transition has no door to the acquisition record, and
/// the summed exposure is an acquisition fact. A combination is a new image, so its metadata is built whole
/// from its sources, and `Image/OperationTransition.h` supplies what an operation's declared effect and its
/// outcome contribute - the meaning, the solution, the lineage entry. It lives at the codec boundary rather
/// than beside the transition builder because the cards are half of it: the first source's world coordinate
/// cards have to survive a combination that keeps its pixel grid, and a card stating a source's exposure or
/// its processing state must not survive to contradict the result's.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>

#include <span>

namespace an
{

/// The metadata of an image combined from several sources.
///
/// The first source is the one whose pixel grid the result shares - a blend's target, a combination's
/// luminance - and the effect is the combining operation's published declaration. Each of the six objects
/// is decided by one rule:
///
/// - **Acquisition.** The exposure is the sum of the exposures the sources state, and absent when none states
///   one. Every other fact - the instrument, telescope, observer, object and filter names, the sensor
///   temperature, the electron gain, the binning, the exposure start and the camera record - is kept when every
///   source states the same value, and absent otherwise.
/// - **Meaning.** An effect that restates the meaning takes the outcome's, the fresh claim the operation made.
///   One that preserves it keeps what every source agrees on: the transfer function and gamma when they are
///   common and `TransferFunction::Unspecified` otherwise, each processing step only when every source has had
///   it, and the validity the effect's policy gives over the sources' rules, which `applyValidityPolicy`
///   refuses when sources it may not combine disagree.
/// - **Solution.** The first source's, when the effect preserves the solution; none when it invalidates it.
/// - **Lineage.** The first source's, with the outcome's entry appended.
/// - **Cards.** None, when the effect drops them. Otherwise the first source's cards that every other source
///   states identically - the same namespace, keyword and value; the comment may differ - in the first source's
///   order, and the source encoding when every source states the same one. A value continued across several cards
///   of one keyword is compared whole and kept or dropped whole. Three kinds of card follow their own rule
///   instead: the first source's world coordinate cards, those `isWcsKeyword` names, with the properties of an XISF
///   astrometric solution as `withoutWcsCards` removes them, are kept whole when the solution is kept and dropped
///   when it is not; a card stating an exposure - `EXPOSURE` and `EXPTIME`, or the
///   Exif `ExposureTime` - is dropped whenever there is more than one source, because the result's exposure is
///   none of theirs; and the processing-state cards `withoutProcessingStateCards` removes, and the artifact and
///   run identity cards, are dropped, because each describes one file.
/// - **Output intent.** None when the effect withdraws it; otherwise the intent when every source states the
///   same one, and none when they differ.
///
/// Thread-safe with respect to its arguments, which are only read. Allocates the result.
///
/// @param sources the images combined, the first sharing the result's pixel grid; at least one, none null.
/// @param effect the combining operation's published declaration.
/// @param outcome what the completed combination produced: its name and detail for the lineage, and its meaning
///        when the effect restates one.
/// @return the result's metadata; `ErrorCode::InvalidArgument` for no source, a null source, an inconsistent
///         declaration, an outcome that disagrees with it - the message naming the `OperationEffectError` or
///         `OperationOutcomeError` at fault - an empty or overlong lineage text, or a summed exposure the
///         acquisition record refuses; `ErrorCode::IncompatibleArguments` when the sources' validity rules cannot
///         be combined as the effect declares; or `ErrorCode::LimitExceeded` when the first source's lineage is
///         already full.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageMetadata>
combinedImageMetadata(std::span<const ImageMetadata* const> sources, const OperationEffect& effect,
                      const OperationOutcome& outcome);

} // namespace an
