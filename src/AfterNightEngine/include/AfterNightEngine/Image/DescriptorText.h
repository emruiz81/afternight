// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An image descriptor as one line of text, so a test can state one and a
/// failure can print one.
///
/// A descriptor has eight fields across five vocabularies. Written out in C++ that is a dozen lines
/// before the case being tested begins, and printed by a failing assertion it is an address. Both
/// problems are the same problem, and one lossless text form solves them: a fixture is a line, an
/// expectation is a line, and a mismatch is two lines a reader can diff by eye.
///
/// Lossless is the property that makes it worth having rather than a convenience. Every descriptor
/// formats to text that parses back to exactly the descriptor it came from, the pixel aspect
/// included - which is why the ratio is written through the shortest representation that round-trips
/// rather than through a fixed number of decimals.
///
/// This is not a file format and there is no reader of files here. The form is what this engine
/// writes and this engine reads, so the parser has no compatibility to keep and is strict about
/// everything: every field exactly once, no field it does not know, and no value that is not the
/// shape its field takes. A parser that guessed would make a fixture that says something other than
/// what its author wrote still pass.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <optional>
#include <string>
#include <string_view>

namespace an
{

/// One descriptor as a line of text.
///
/// The canonical form is eight space-separated `key=value` pairs in this order:
///
/// @code
/// extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb domain=NormalizedLinear
/// cfa=None@0,0 origin=TopLeft aspect=1
/// @endcode
///
/// Every enumerated value is spelled exactly as its enumerator, which is what the name functions in
/// the vocabulary headers return, so a value in the text and a value in a log line are the same
/// word. The filter array is its pattern, an `@`, and its two phases; the aspect is the shortest
/// decimal that reads back as the same `double`.
///
/// A tabulated mosaic carries its table in the same field, after a colon, as thirty-six letters in
/// the order the block is stored - the first letter of each filter colour, row-major from the block
/// origin:
///
/// @code
/// cfa=XTrans@0,0:GGRGGBGGBGGRBRGRBGGGBGGRGGRGGBRBGBRG
/// @endcode
///
/// The table is part of the filter-array field rather than a field of its own, because it is part
/// of the same fact. A line that names the tabulated pattern without a table describes no mosaic
/// and is refused; a line that carries a table for a named pattern states the same thing twice and
/// is refused as well.
///
/// Thread-safe. Allocates the returned string and nothing else.
///
/// @param descriptor the descriptor to write. Every descriptor that exists has been validated, so
///        there is no failure to report.
/// @return the canonical form, which `parseImageDescriptor` accepts and which reproduces exactly
///         this descriptor.
[[nodiscard]] AFTERNIGHTENGINE_API std::string formatImageDescriptor(const ImageDescriptor& descriptor);

/// Whether text describes an image, and if not, why not.
///
/// This is the entry point for a caller that has to report the reason; a caller that only needs the
/// value uses `parseImageDescriptor`. The two do the same work, so a caller that wants both a
/// descriptor and a reason parses once and treats an empty result as the reason it asks for.
///
/// The order is a contract, and it is the one the descriptor's own checking uses: the text has to
/// be pairs at all, then every field has to be present exactly once and known, then each value is
/// read in the canonical field order - a value that names nothing, or a filter-array field that
/// breaks its own table or phase rules, is refused where it is reached - and only then are the rules
/// that combine fields applied. So a line with a misspelled enumerator reports that rather than
/// whichever combination rule the misspelling happened to break.
///
/// Thread-safe. May allocate while parsing and keeps nothing.
///
/// @param text the line to read. Fields may be given in any order; the canonical order is what
///        `formatImageDescriptor` writes, not what this requires.
/// @param limits the configured maxima the dimensions must satisfy, which are the caller's rather
///        than the text's - the same line describes an image one configuration admits and another
///        refuses.
/// @return `DescriptorError::None` when a descriptor can be built; `MalformedText`, `UnknownField`,
///         `MissingField` or `DuplicateField` for text that is not this form; the matching
///         `Unknown...` reason for a value that names no enumerator, `UnknownCfaColor` included
///         for a table letter; `ExtentRefused` when the dimensions read but build no extent under
///         these limits; `CfaTableRequired` or `CfaTableNotAllowed` when a table is missing or
///         surplus, or whichever table or phase rule `CfaDescriptor::check` refused; and otherwise
///         the combination rule that refused, exactly as `ImageDescriptor::check` would name it.
[[nodiscard]] AFTERNIGHTENGINE_API DescriptorError checkImageDescriptorText(std::string_view text,
                                                                            const EngineLimits& limits);

/// The descriptor this text describes, where it describes one.
///
/// Thread-safe. May allocate while parsing and keeps nothing.
///
/// @param text the line to read, in the form `formatImageDescriptor` writes; fields may be given in
///        any order.
/// @param limits the configured maxima the dimensions must satisfy.
/// @return the descriptor, or an empty optional exactly when `checkImageDescriptorText` would name
///         a reason.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageDescriptor> parseImageDescriptor(std::string_view text,
                                                                                       const EngineLimits& limits);

} // namespace an
