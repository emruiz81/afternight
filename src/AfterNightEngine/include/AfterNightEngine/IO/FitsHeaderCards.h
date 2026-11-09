// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Reading the cards of a FITS header without decoding an image - a plate solver's `.wcs` output - and
/// writing a card back as its header record.
///
/// A solver that writes its answer beside the image - astrometry.net's `solution.wcs`, ASTAP's
/// `<name>.wcs` - writes a header and no data. The FITS codec opens images, and a header with no image
/// is not one; this reads the cards themselves, from the bytes of the document or from its file, and
/// hands back the `CodecCard`s `Algorithms/Astrometry/WcsCards.h`'s `parseWcsCards` takes with the
/// caller's image dimensions.
///
/// **Two layouts.** A FITS header is a sequence of eighty-byte records with no line breaks, padded to
/// whole 2880-byte blocks; some tools write the same cards as text lines instead. The first eighty-two
/// bytes decide - a card and a carriage return and line feed after it: a line feed among them means
/// lines - each line one card, a carriage return before its line feed dropped, a blank line skipped, no
/// line longer than eighty bytes once its trailing blanks are removed - and none means records, eighty
/// bytes each.
///
/// **One reading of a card, the codec's.** A card's keyword, value and comment are read by the same
/// reading the FITS codec reads an image's header with - the reading CFITSIO's readers gave it, so a
/// card stored by any earlier release reads back unchanged - and a header read here and the same
/// header decoded from an image carry equal cards: the keyword runs to the first blank or `=`, or after
/// `HIERARCH ` to the `=`; a value follows `= ` in columns nine and ten, or the first `=` of a card
/// that has one elsewhere;
/// `COMMENT`, `HISTORY`, `CONTINUE` and blank-keyword records carry their text as a comment and no
/// value. A string value keeps its quotes, its doubled quotes and its leading blanks and loses its
/// trailing ones; a long string continued on `CONTINUE` records is joined into its first card. A record
/// whose keyword is blank is skipped, as the codec skips it.
///
/// **What ends a header is stated, not assumed.** Reading stops at the first `END` record and nothing
/// after it is read: padding, or an image's data. A header without one is refused - a truncated
/// download whose last polynomial coefficients are missing would otherwise read as a whole solution -
/// and so is a record holding a control character, a complex value with no closing parenthesis, and a
/// header of more cards than `kMaxCodecCards`.
///
/// **And back.** `formatFitsCardRecord` writes a card as the record CFITSIO would write for it, for a
/// consumer that hands a header's records on beside its cards.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most bytes `readFitsHeaderCards` reads from a file unless told otherwise.
///
/// One mebibyte holds `kMaxCodecCards` records with their line breaks, more than any header whose cards
/// an image could preserve.
inline constexpr std::uint64_t kDefaultFitsHeaderReadBytes = 1048576u;

/// The cards a header-only document carries, from its bytes.
///
/// Thread-safe and reentrant. Allocates the returned cards and their text.
///
/// @param header the document's bytes, borrowed for the call: records or lines, as this header states.
///        Bytes after the `END` record are not read.
/// @return the cards in the order the header carries them, each in the `CodecNamespace::Fits`
///         namespace; `ErrorCode::InvalidData`, with the number of cards read before the fault as its
///         `Completed` context, for a header with no `END` record, a record holding a byte below 32 or
///         the byte 127, a line longer than eighty bytes, or a complex value with no closing
///         parenthesis; `ErrorCode::LimitExceeded` carrying the count and the bound for more cards
///         than `kMaxCodecCards`; or whatever `CodecCard::create` refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<CodecCard>> parseFitsHeaderCards(std::string_view header);

/// The cards a header-only document carries, from its file.
///
/// Reads at most `maximumBytes` bytes from the start of the file and parses them with
/// `parseFitsHeaderCards`. A file longer than that is read up to it, so the primary header of an image
/// can be read too without reading its data.
///
/// @param execution the operation; its cancellation is checked before the file is read and after it is
///        parsed.
/// @param path the file's UTF-8 path: nonempty, at most 131072 bytes, with no NUL; borrowed for the
///        call.
/// @param maximumBytes how many bytes may be read, positive.
/// @return the cards; `ErrorCode::NotInitialized` for an execution that is not valid;
///         `ErrorCode::InvalidArgument` for a path that is not such a path or a zero bound;
///         `ErrorCode::LimitExceeded`, carrying the bound, when the file is longer than the bound and no
///         `END` record lies within it; `ErrorCode::ExternalFailure` with the path, and the operating
///         system's code where it gave one, when the file cannot be inspected, opened or read; the
///         failures `parseFitsHeaderCards` returns; or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<CodecCard>>
readFitsHeaderCards(const ExecutionContext& execution, std::string_view path,
                    std::uint64_t maximumBytes = kDefaultFitsHeaderReadBytes);

/// The header record a card is written as, in the fixed format CFITSIO writes it.
///
/// The way back from a card to the eighty columns a header holds it in, as CFITSIO's own writer lays a
/// keyword out, so a card read from a file CFITSIO wrote comes back as the record the file holds. `COMMENT`
/// and `HISTORY` are the keyword and their text from column nine - the value, or the comment when the value
/// is empty, as the codec reads them - and `CONTINUE` is the keyword, two blanks and the value and comment.
/// A keyword of at most eight characters from `A` to `Z`, `0` to `9`, `-` and `_` is padded to eight and
/// followed by `= ` in columns nine and ten; any other keyword is written after `HIERARCH ` with ` = `, or
/// `= ` when the record would not otherwise fit. A string value is quoted as CFITSIO quotes one, at least
/// eight characters between its quotes, and starts in column eleven; any other value ends in column thirty,
/// or later when it is longer. A keyword with no value has no `=`, and its comment follows from column
/// nine, where it is read from. A comment follows ` / `, after a short string not before column thirty, and
/// is cut where the record ends. Trailing blanks are not part of the record.
///
/// Thread-safe and reentrant. Allocates the record.
///
/// @param card the card, in any namespace; its keyword, value and comment are read.
/// @return the record, at most eighty characters; `ErrorCode::InvalidArgument` for an empty keyword or
///         one holding `=`; `ErrorCode::LimitExceeded` carrying the record's length and the bound when
///         the keyword and value alone do not fit one record, which is a string CFITSIO continues on
///         `CONTINUE` records or a commentary text longer than seventy-two characters.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::string> formatFitsCardRecord(const CodecCard& card);

} // namespace an
