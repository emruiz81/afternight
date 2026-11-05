// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The bridge between a header's world-coordinate cards and the typed model.
///
/// This parses *cards*, not files. A card is a keyword, a value and a comment that some decoder has
/// already extracted; whatever opened the file and found them is somebody else's job, and the image
/// codecs own it. That separation is what lets this engine be tested against real telescope headers
/// before it has a reader of its own: the preserved cards a fixture carries are the same cards a
/// codec will hand over later, and this is the only code that turns either into a solution.
///
/// **One conversion, in one place.** The convention counts pixels from one with the centre of the
/// first pixel at exactly one; this engine counts from zero with that centre at one half. The half
/// pixel between the two conventions is subtracted here on the way in and added back here on the way
/// out, and nowhere else, so no path a number takes - projection, distortion or a solver's header -
/// can apply it twice or not at all.
///
/// **What is not understood is refused by name.** A projection this build does not implement, an
/// axis order it does not read, a unit that is not degrees, a keyword that appears twice saying two
/// different things: each is a named reason rather than a solution quietly built from whichever card
/// was seen first.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace an
{

/// Why a header's cards do not describe a solution this engine can use.
///
/// A closed vocabulary, returned by `checkWcsCards`, because the answers a caller gives differ: a
/// header with no solution is an unsolved frame and is ordinary, a projection this build does not
/// implement is a file to open elsewhere, and a duplicated keyword is a file to repair.
enum class WcsCardError : std::uint8_t
{
    /// The cards describe a solution this engine can use.
    None,

    /// The cards carry no world-coordinate keywords at all. An unsolved frame, not a broken one.
    NoSolution,

    /// A keyword the rest of the header requires is not there.
    MissingKeyword,

    /// A value is not the kind of thing its keyword names - a coordinate that is not a number, an
    /// order that is not an integer, a projection code that is not a projection code.
    MalformedValue,

    /// The same keyword appears more than once with different values, so the card a reader reached
    /// first would decide the solution.
    DuplicateKeyword,

    /// The projection code is a projection this build does not implement.
    UnsupportedProjection,

    /// The cards name a celestial frame this build does not read: axes other than right ascension
    /// and declination, a `RADESYS` or `RADECSYS` other than `ICRS`, an `EQUINOX` or `EPOCH` on a
    /// header that does not state `ICRS`, or a `LONPOLE` that turns the sky from the convention's
    /// default. A `LATPOLE` is read as a number and never moves the pole, since a zenithal
    /// projection's native pole is its reference point. Equatorial ICRS directions are what a plate
    /// solve in this domain produces; a galactic, ecliptic or older equatorial frame is a different
    /// sky, not a different spelling of this one.
    UnsupportedFrame,

    /// The header contradicts itself: the two axes name different projections, one carries the
    /// distortion suffix and the other does not, or the projection codes and the distortion
    /// keywords disagree about whether the frame has a distortion at all.
    InconsistentAxes,

    /// The celestial longitude is not the first axis. No writer in this domain produces it, and
    /// reading it as if it were would transpose the sky.
    UnsupportedAxisOrder,

    /// An axis unit is stated and is not degrees.
    UnsupportedUnit,

    /// A value is a number and is outside what it can mean - a declination past a pole, a negative
    /// equinox or epoch, a polynomial order outside `kMinimumSipOrder` to `kMaximumSipOrder`, or a
    /// nonzero coefficient of a degree its polynomial's order excludes.
    ValueOutOfRange,

    /// The linear matrix cannot be inverted, so the solution maps the whole frame to one direction.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SingularTransform
};

/// Whether a value is one of the reasons this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isWcsCardError(WcsCardError error) noexcept
{
    return static_cast<std::uint8_t>(error) <= static_cast<std::uint8_t>(WcsCardError::SingularTransform);
}

/// The reason's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownWcsCardError"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* wcsCardErrorName(WcsCardError error) noexcept;

/// Whether a keyword belongs to the world-coordinate vocabulary this bridge owns.
///
/// True for the projection, unit, reference, matrix, scale and rotation keywords of any axis -
/// every keyword that begins `CTYPE`, `CUNIT`, `CRVAL`, `CRPIX`, `CDELT`, `CROTA`, `CD1_` or
/// `CD2_`, and every `PC`, `PV` or `PS` followed by a digit - for `EQUINOX`, `EPOCH`, `RADESYS`,
/// `RADECSYS`, `LONPOLE`, `LATPOLE`, `WCSAXES` and `WCSNAME`, and for every keyword of the
/// polynomial distortion: the order, `DMAX` and coefficient keywords of `A`, `B`, `AP` and `BP`.
/// It is what decides which cards a solution replaces when it is written into a header and which
/// cards are removed when one is withdrawn, so a caller and this bridge cannot disagree about the
/// boundary.
///
/// Thread-safe and allocation-free.
///
/// @param keyword the keyword to classify, without its value and compared case-sensitively, as
///        header keywords are.
/// @return true when this bridge reads or writes that keyword.
[[nodiscard]] AFTERNIGHTENGINE_API bool isWcsKeyword(std::string_view keyword) noexcept;

/// Whether these cards describe a solution, and why not when they do not.
///
/// Reads only cards in the `CodecNamespace::Fits` namespace, because the vocabulary is that
/// format's; a card preserved under another namespace that happens to spell `CRVAL1` means whatever
/// its own format means by it.
///
/// Thread-safe and reentrant. Allocates only what reading a card's text needs.
///
/// @param cards the preserved header cards, in any order.
/// @return `WcsCardError::None` when `parseWcsCards` will succeed on a frame of nonzero size;
///         otherwise the first reason the cards were refused.
[[nodiscard]] AFTERNIGHTENGINE_API WcsCardError checkWcsCards(std::span<const CodecCard> cards);

/// The solution these cards describe, for a frame of this size.
///
/// The reference pixel is returned in engine pixel coordinates - `CRPIX1` and `CRPIX2` less one
/// half - and becomes the distortion's reference pixel too. `CRVAL1` is folded into `[0, 360)`.
///
/// The linear matrix is taken from the `CD` keywords when they are present, from `PC` scaled by
/// `CDELT` when they are not, and from `CDELT` with `CROTA2` when neither is - which is the order
/// the convention itself gives and the order older files need. A missing `CD` entry is zero, a
/// missing `PC` entry is the identity's, and a missing `CDELT` is one.
/// Directions must be ICRS, stated explicitly or implied by the absence of frame and epoch
/// keywords. Other reference frames and non-default celestial poles are refused, never
/// interpreted as ICRS. `EQUINOX` on an explicitly ICRS header has no coordinate effect.
///
/// Thread-safe and reentrant. Allocates only what reading the cards' text needs.
///
/// @param cards the preserved header cards, in any order.
/// @param imageWidth the frame's width in pixels, from the image rather than from a card, because
///        the image is what the solution will be used with. Must not be zero.
/// @param imageHeight the frame's height in pixels. Must not be zero.
/// @return the solution; `ErrorCode::InvalidArgument` for a zero dimension, or for a missing
///         keyword, a malformed value or a value out of range; `ErrorCode::InvalidData` for a
///         duplicated keyword, inconsistent axes or a singular matrix; `ErrorCode::Unsupported`
///         for a projection, frame, axis order or unit this build does not read; or
///         `ErrorCode::EmptySelection` when the cards carry no solution at all. A card refusal
///         carries its `WcsCardError` name as the `Parameter` context, and its message names the
///         responsible keyword where there is one.
[[nodiscard]] AFTERNIGHTENGINE_API Result<WcsSolution>
parseWcsCards(std::span<const CodecCard> cards, std::uint32_t imageWidth, std::uint32_t imageHeight);

/// The cards that write one solution into a header.
///
/// `RADESYS = 'ICRS'`, the two projection codes, the two units, `CRPIX`, `CRVAL` and the four `CD`
/// entries, then the order and every nonzero coefficient of `A`, `B`, `AP` and `BP` that the model
/// carries - in that fixed order, with no comments, and every number written as the shortest
/// decimal that reads back as itself. The reference pixel is converted back to the convention's
/// one-based centres by adding one half. A purely linear solution produces no distortion keywords
/// and no distortion suffix on its projection codes.
///
/// The header has one `CRPIX` for both the matrix and the polynomials, so a distortion whose
/// reference pixel differs from the solution's cannot be written; such a model must be rebased
/// explicitly before export.
///
/// The cards say nothing about the frame's size: `NAXIS` describes the data array and belongs to
/// whatever writes the data.
///
/// Thread-safe and reentrant.
///
/// @param solution the solution to write.
/// @return the cards, in the `CodecNamespace::Fits` namespace; `ErrorCode::IncompatibleArguments`
///         when the distortion's reference pixel is not the solution's; or whatever
///         `CodecCard::create` refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<CodecCard>> buildWcsCards(const WcsSolution& solution);

/// The same preserved metadata with every world-coordinate card removed.
///
/// What a frame's header should become when an operation invalidates its solution. The properties
/// of an XISF image's astrometric solution - XISF's `AstrometricSolution:` namespace and the
/// `PCL:AstrometricSolution:` namespace PixInsight writes - state the same solution in that format's
/// vocabulary and are removed with it. Every other card is kept in its original order, so a header
/// does not lose what the astrometry had nothing to do with.
///
/// Thread-safe and reentrant.
///
/// @param metadata the preserved metadata to withdraw a solution from.
/// @return the metadata without those cards, or whatever `CodecMetadata::create` refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata> withoutWcsCards(const CodecMetadata& metadata);

/// The same preserved metadata carrying this solution and no other.
///
/// Every existing world-coordinate card, and every property of an XISF astrometric solution as
/// `withoutWcsCards` removes them, is removed and this solution's cards are appended, so a header
/// cannot end up describing two solutions. Everything else is kept in its original order.
///
/// Thread-safe and reentrant.
///
/// @param metadata the preserved metadata to write into.
/// @param solution the solution to write.
/// @return the metadata carrying the solution, or whatever `buildWcsCards` or `CodecMetadata`
///         refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata> withWcsCards(const CodecMetadata& metadata,
                                                                      const WcsSolution& solution);

} // namespace an
