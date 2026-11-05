// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a bounded reader for the small object documents a catalog provider ships.
//
// Two of the Gaia container's descriptions are JSON objects: the manifest beside the catalog and
// the header inside every chunk file. They are a few kilobytes of flat members and one array of
// flat objects, and this reads exactly that shape.
//
// It is here rather than behind a library because the algorithms layer adapts none: a general
// document model, its allocator behaviour and its exception surface are a large dependency to
// acquire for two descriptions whose whole grammar is an object of scalars. What is here instead is
// a scan with a member ceiling and a nesting ceiling that never recurses, so a document cannot cost
// more than the bytes it occupies, and a value is converted only when a caller asks for it as
// something.
//
// It deliberately does not build a document. A scan yields views into the caller's own text, and a
// nested structure is skipped over rather than descended into until a caller scans it in turn.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an::catalog_detail
{

/// How much structure one scan admits.
struct JsonLimits
{
    /// The most members one object, or elements one array, may hold.
    std::uint32_t maximumEntries = 4096u;

    /// How deeply a skipped value may nest before the scan refuses it.
    ///
    /// A ceiling rather than a capacity: nothing in these documents nests past an array of objects,
    /// and a document that does is not one of them.
    std::uint32_t maximumDepth = 16u;
};

/// Why a scan stopped.
enum class JsonScanStatus : std::uint8_t
{
    /// The scan read the whole value.
    Ok = 0u,

    /// The text is not the structure that was asked for.
    Malformed = 1u,

    /// The text holds more members or elements than the limit admits.
    TooManyEntries = 2u,

    /// A value nests deeper than the limit admits.
    TooDeep = 3u
};

/// One member of a scanned object: its name and the text of its value.
struct JsonMember
{
    /// The member's name, exactly as written between its quotes and still carrying any escapes.
    std::string_view name{};

    /// The member's value, as the text that spells it, with no surrounding space.
    std::string_view value{};
};

/// Reads the top-level members of one JSON object.
///
/// The text must hold one object and nothing else but space. Member values are returned as the text
/// that spells them: a nested object or array is skipped over intact and can be scanned in turn.
///
/// @param text the document, which is only read and must outlive the members.
/// @param limits how many members and how much nesting to admit.
/// @param members receives one entry per member, in document order. Cleared first.
/// @return why the scan stopped.
[[nodiscard]] JsonScanStatus scanJsonObject(std::string_view text, const JsonLimits& limits,
                                            std::vector<JsonMember>& members);

/// Reads the elements of one JSON array.
///
/// @param text the array, which is only read and must outlive the elements.
/// @param limits how many elements and how much nesting to admit.
/// @param elements receives one entry per element, in document order. Cleared first.
/// @return why the scan stopped.
[[nodiscard]] JsonScanStatus scanJsonArray(std::string_view text, const JsonLimits& limits,
                                           std::vector<std::string_view>& elements);

/// The member of that name, or nothing.
///
/// Names are compared byte for byte, and a name written with escape sequences is decoded before the
/// comparison so that it still matches the plain spelling it stands for.
///
/// @param members the members one scan produced.
/// @param name the name to look for, in its plain spelling.
/// @return a pointer into `members`, or null when no member has that name.
[[nodiscard]] const JsonMember* findJsonMember(std::span<const JsonMember> members, std::string_view name);

/// The text of a string value, with its escapes resolved.
///
/// @param value the value text, which must begin and end with a quote to be a string.
/// @return the decoded contents, or absent when the value is not a well-formed string.
[[nodiscard]] std::optional<std::string> jsonString(std::string_view value);

/// A number value as a signed integer.
///
/// @param value the value text: decimal digits with an optional leading minus, so `1.0` and
///        `1e3` are refused even though they name whole numbers.
/// @return the number, or absent when the text is not a whole number in range.
[[nodiscard]] std::optional<std::int64_t> jsonInteger(std::string_view value);

/// A number value as an unsigned integer.
///
/// @param value the value text: decimal digits only.
/// @return the number, or absent when the text is not a non-negative whole number in range.
[[nodiscard]] std::optional<std::uint64_t> jsonUnsigned(std::string_view value);

/// A number value as a double.
///
/// @param value the value text.
/// @return the number, or absent when the text is not a finite number.
[[nodiscard]] std::optional<double> jsonNumber(std::string_view value);

/// A boolean value.
///
/// @param value the value text.
/// @return the value, or absent when the text is neither `true` nor `false`.
[[nodiscard]] std::optional<bool> jsonBoolean(std::string_view value);

/// Whether a value is the null literal.
///
/// @param value the value text.
/// @return true for exactly `null`.
[[nodiscard]] bool jsonIsNull(std::string_view value) noexcept;

/// Whether a value is an array.
///
/// @param value the value text.
/// @return true when it begins with an opening bracket.
[[nodiscard]] bool jsonIsArray(std::string_view value) noexcept;

} // namespace an::catalog_detail
