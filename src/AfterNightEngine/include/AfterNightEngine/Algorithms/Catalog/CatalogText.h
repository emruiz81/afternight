// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Bounded parsing of a delimited catalog text into owned entries.
///
/// `parseCatalogText` reads the two comma-separated column layouts this engine's own catalog texts
/// are written in and returns `CatalogEntry` values ready for `buildSkyCatalog`, with a
/// `CatalogTextReport` of what it saw and what it repaired. It is a pure function of bytes: this
/// engine opens no file, so the caller supplies the text however it obtained it.
///
/// Every limit - total bytes, entries, line bytes, field bytes - is a refusal that carries the
/// requested and permitted values, never a truncation, and a malformed row stops the parse with the
/// column it failed on. Case folding is ASCII only, so a UTF-8 designation means the same thing
/// under every process locale.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most text one parse reads, in bytes.
inline constexpr std::uint64_t kDefaultMaxCatalogTextBytes = 256u * 1024u * 1024u;

/// The most bytes one line of catalog text may hold.
inline constexpr std::uint32_t kDefaultMaxCatalogLineBytes = 8192u;

/// The most bytes one field of one row may hold.
inline constexpr std::uint32_t kDefaultMaxCatalogFieldBytes = 1024u;

/// The magnitude at or above which a provider's text means "not measured".
///
/// Catalog files in this format write `99` for an object nobody has measured, and treating that as a
/// measurement makes five thousand of the thirteen thousand entries in the shipping deep-sky table
/// fail every magnitude limit they are compared against.
inline constexpr double kDefaultUnknownMagnitudeThreshold = 90.0;

/// Which column layout a catalog text uses.
///
/// A closed pair rather than a caller-supplied column map: these are the two layouts this engine's
/// own catalog texts are written in, and a provider with a different one is an adapter that builds
/// entries directly rather than a column permutation this parser has to be told about.
enum class CatalogTextSchema : std::uint8_t
{
    /// Nine columns: identifier, common name, right ascension, declination, major axis, minor axis,
    /// position angle, object type, magnitude. Angles are degrees and axes are arcminutes.
    ExtendedObject = 0u,

    /// Seven columns: identifier, common name, second designation, right ascension, declination,
    /// magnitude, spectral type. Angles are degrees.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Star = 1u
};

/// Whether a value names a layout this build parses.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param schema the value to inspect.
/// @return true for one of the two declared layouts; false for an out-of-range cast.
[[nodiscard]] constexpr bool isCatalogTextSchema(CatalogTextSchema schema) noexcept
{
    return schema == CatalogTextSchema::ExtendedObject || schema == CatalogTextSchema::Star;
}

/// The layout's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param schema the layout to name.
/// @return a null-terminated string literal compiled into this library, which outlives every
///         caller; `"UnknownCatalogTextSchema"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* catalogTextSchemaName(CatalogTextSchema schema) noexcept;

/// How much text one parse is allowed to read, and how large its pieces may be.
///
/// Every limit is a refusal rather than a truncation: a text that exceeds one is reported with the
/// requested and permitted values, because a caller that silently received the first half of a
/// catalog would go on to answer queries about a sky it only partly knows.
struct CatalogTextLimits
{
    /// The most bytes of text this parse reads.
    std::uint64_t maximumBytes = kDefaultMaxCatalogTextBytes;

    /// The most entries this parse produces.
    std::uint32_t maximumEntries = kMaxCatalogEntries;

    /// The most bytes one line may hold, counting its commas and any carriage return but not the
    /// line feed that ends it.
    std::uint32_t maximumLineBytes = kDefaultMaxCatalogLineBytes;

    /// The most bytes one field may hold, before its surrounding quotes are removed.
    std::uint32_t maximumFieldBytes = kDefaultMaxCatalogFieldBytes;
};

/// What one parse is asked to do.
struct CatalogTextRequest
{
    /// Which column layout the text uses.
    CatalogTextSchema schema = CatalogTextSchema::ExtendedObject;

    /// How much text the parse may read.
    CatalogTextLimits limits{};

    /// The value at or above which a magnitude in the text means "not measured", or absent when the
    /// provider writes no such sentinel and every number is a measurement.
    std::optional<double> unknownMagnitudeAtOrAbove{kDefaultUnknownMagnitudeThreshold};
};

/// What one parse saw, beside the entries it produced.
///
/// Counts rather than a log: a caller deciding whether a catalog file is the one it expected reads
/// these, and a caller showing a person what happened formats them.
struct CatalogTextReport
{
    /// Lines the parse read, including comments, blanks and the header.
    std::uint32_t lineCount = 0u;

    /// Entries the parse produced, equal to the length of the entry list.
    std::uint32_t entryCount = 0u;

    /// Lines beginning with `#` after leading space, which are skipped.
    std::uint32_t commentLineCount = 0u;

    /// Lines whose first field is `id` in any ASCII case, which are skipped as a column header.
    std::uint32_t headerLineCount = 0u;

    /// Lines holding nothing but space, which are skipped.
    std::uint32_t blankLineCount = 0u;

    /// Bytes of text the parse read.
    std::uint64_t byteCount = 0u;

    /// Entries whose magnitude the sentinel rule turned into no measurement.
    std::uint32_t unknownMagnitudeCount = 0u;

    /// Entries whose right ascension was folded into `[0, 360)` by the parse.
    std::uint32_t normalizedRightAscensionCount = 0u;

    /// Entries whose declination the parse clamped into `[-90, 90]`.
    ///
    /// Counted because a catalog text that needs this repair is a catalog text somebody should look
    /// at.
    std::uint32_t clampedDeclinationCount = 0u;
};

/// Everything one successful parse produced.
struct ParsedCatalogText
{
    /// The entries, in the order the text listed them.
    std::vector<CatalogEntry> entries{};

    /// What the parse saw.
    CatalogTextReport report{};
};

/// Parses a delimited catalog text into owned entries.
///
/// Fields are comma-separated with the usual double-quote convention: a quoted field may hold
/// commas, and a doubled quote inside one is a literal quote. A line whose first non-space
/// character is `#` is a comment, a line holding nothing but space is skipped, and a line whose
/// first field is `id` - ignoring surrounding space and ASCII case - is a column header. Every
/// other line must hold exactly the number of columns its schema declares.
///
/// Repairs: right ascension is folded into `[0, 360)` and declination is clamped into `[-90, 90]`,
/// both counted in the report; axis lengths below zero are raised to zero and a blank minor axis
/// takes the major axis, which are not counted. Every
/// extended-object row carries an extent, zero where its axes are blank. A blank magnitude, or one
/// at or above the stated sentinel, becomes no measurement rather than the number `99`.
///
/// Everything else is a refusal that names where it happened. A row with the wrong number of
/// columns, an empty identifier or a field that is not a complete finite number stops the parse:
/// half a catalog is not a catalog, so the whole load fails.
///
/// Thread-safe with respect to `execution` and `text`; the text is only read and need not outlive
/// the call. The parse runs on the calling thread, checks cancellation every few thousand lines,
/// and allocates the entries it returns and the fields of the row it is reading.
///
/// @param execution the operation policy and engine-context lease.
/// @param text the whole catalog text, in UTF-8 or any ASCII-compatible encoding. Bytes above 127
///        are carried through untouched.
/// @param request which layout to read, what limits to hold it to, and the unknown-magnitude
///        sentinel.
/// @return the entries and the report; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for an unknown schema, a zero limit or a non-finite
///         unknown-magnitude sentinel; `ErrorCode::LimitExceeded` when the text, a line, a field or
///         the entry count exceeds its limit, carrying the requested and permitted values;
///         `ErrorCode::InvalidData` for a malformed row, carrying the rejected column's name when
///         one column refused it and how many lines were read before it;
///         `ErrorCode::EmptySelection` when the text holds no entry at all; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ParsedCatalogText>
parseCatalogText(const ExecutionContext& execution, std::string_view text, const CatalogTextRequest& request = {});

} // namespace an
