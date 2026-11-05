// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the bounded object scan the Gaia manifest and chunk headers are read with.

#include "algorithms/catalog/CatalogJson.h"

#include "algorithms/catalog/CatalogInternal.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an::catalog_detail
{
namespace
{

/// Advances past the space between two tokens.
///
/// @param text the document.
/// @param cursor the position to advance, which ends at the next non-space byte or at the end.
void skipSpace(std::string_view text, std::size_t& cursor) noexcept
{
    while (cursor < text.size() && isAsciiSpace(text[cursor]))
    {
        ++cursor;
    }
}

/// Advances past one quoted string, whose opening quote is at the cursor.
///
/// @param text the document.
/// @param cursor the position of the opening quote, which ends past the closing one.
/// @return true when a closing quote was found.
[[nodiscard]] bool skipString(std::string_view text, std::size_t& cursor) noexcept
{
    if (cursor >= text.size() || text[cursor] != '"')
    {
        return false;
    }
    ++cursor;
    while (cursor < text.size())
    {
        const char value = text[cursor];
        if (value == '\\')
        {
            // The escape's own byte is consumed with it, so a quote inside one does not end the
            // string. Whether the escape means anything is decided when the value is read.
            cursor += 2u;
            continue;
        }
        ++cursor;
        if (value == '"')
        {
            return true;
        }
    }
    return false;
}

/// Advances past one value of any kind, whose first byte is at the cursor.
///
/// A nested object or array is walked over rather than descended into, so the whole scan is one
/// pass with a depth counter and never recurses. The walk counts brackets without matching their
/// kinds and accepts any run of bytes up to a delimiter as a scalar; a skipped value is validated
/// only when a caller scans or converts it in turn.
///
/// @param text the document.
/// @param cursor the position of the value's first byte, which ends past its last.
/// @param maximumDepth how deeply the value may nest.
/// @return why the walk stopped.
[[nodiscard]] JsonScanStatus skipValue(std::string_view text, std::size_t& cursor,
                                       const std::uint32_t maximumDepth) noexcept
{
    if (cursor >= text.size())
    {
        return JsonScanStatus::Malformed;
    }
    const char first = text[cursor];
    if (first == '"')
    {
        return skipString(text, cursor) ? JsonScanStatus::Ok : JsonScanStatus::Malformed;
    }
    if (first != '{' && first != '[')
    {
        // A number, or one of the three literals: everything up to the byte that ends a value.
        const std::size_t begin = cursor;
        while (cursor < text.size() && text[cursor] != ',' && text[cursor] != '}' && text[cursor] != ']' &&
               !isAsciiSpace(text[cursor]))
        {
            ++cursor;
        }
        return cursor > begin ? JsonScanStatus::Ok : JsonScanStatus::Malformed;
    }

    std::uint32_t depth = 0u;
    while (cursor < text.size())
    {
        const char value = text[cursor];
        if (value == '"')
        {
            if (!skipString(text, cursor))
            {
                return JsonScanStatus::Malformed;
            }
            continue;
        }
        if (value == '{' || value == '[')
        {
            ++depth;
            if (depth > maximumDepth)
            {
                return JsonScanStatus::TooDeep;
            }
        }
        else if (value == '}' || value == ']')
        {
            if (depth == 0u)
            {
                return JsonScanStatus::Malformed;
            }
            --depth;
            if (depth == 0u)
            {
                ++cursor;
                return JsonScanStatus::Ok;
            }
        }
        ++cursor;
    }
    return JsonScanStatus::Malformed;
}

/// Reads the contents of one quoted string, resolving its escapes.
///
/// @param contents the bytes between the quotes.
/// @param decoded receives the resolved text.
/// @return true when every escape was well formed.
[[nodiscard]] bool decodeEscapes(std::string_view contents, std::string& decoded)
{
    const auto readHexQuad = [](std::string_view text, std::size_t at, std::uint32_t& value) {
        if (at + 4u > text.size())
        {
            return false;
        }
        value = 0u;
        for (std::size_t offset = 0u; offset < 4u; ++offset)
        {
            const char digit = text[at + offset];
            std::uint32_t nibble = 0u;
            if (digit >= '0' && digit <= '9')
            {
                nibble = static_cast<std::uint32_t>(digit - '0');
            }
            else if (digit >= 'a' && digit <= 'f')
            {
                nibble = static_cast<std::uint32_t>(digit - 'a') + 10u;
            }
            else if (digit >= 'A' && digit <= 'F')
            {
                nibble = static_cast<std::uint32_t>(digit - 'A') + 10u;
            }
            else
            {
                return false;
            }
            value = (value << 4u) | nibble;
        }
        return true;
    };

    const auto appendUtf8 = [&decoded](std::uint32_t code) {
        if (code < 0x80u)
        {
            decoded.push_back(static_cast<char>(code));
        }
        else if (code < 0x800u)
        {
            decoded.push_back(static_cast<char>(0xC0u | (code >> 6u)));
            decoded.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else if (code < 0x10000u)
        {
            decoded.push_back(static_cast<char>(0xE0u | (code >> 12u)));
            decoded.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu)));
            decoded.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else
        {
            decoded.push_back(static_cast<char>(0xF0u | (code >> 18u)));
            decoded.push_back(static_cast<char>(0x80u | ((code >> 12u) & 0x3Fu)));
            decoded.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu)));
            decoded.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    };

    decoded.clear();
    decoded.reserve(contents.size());
    std::size_t cursor = 0u;
    while (cursor < contents.size())
    {
        const char value = contents[cursor];
        if (value != '\\')
        {
            decoded.push_back(value);
            ++cursor;
            continue;
        }
        ++cursor;
        if (cursor >= contents.size())
        {
            return false;
        }
        const char escape = contents[cursor];
        ++cursor;
        switch (escape)
        {
        case '"':
            decoded.push_back('"');
            break;
        case '\\':
            decoded.push_back('\\');
            break;
        case '/':
            decoded.push_back('/');
            break;
        case 'b':
            decoded.push_back('\b');
            break;
        case 'f':
            decoded.push_back('\f');
            break;
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        case 'u': {
            std::uint32_t code = 0u;
            if (!readHexQuad(contents, cursor, code))
            {
                return false;
            }
            cursor += 4u;
            if (code >= 0xD800u && code <= 0xDBFFu)
            {
                // A leading surrogate means nothing on its own: the pair spells one character, and
                // half of one written alone is a broken document rather than a character to invent.
                std::uint32_t trailing = 0u;
                if (cursor + 2u > contents.size() || contents[cursor] != '\\' || contents[cursor + 1u] != 'u' ||
                    !readHexQuad(contents, cursor + 2u, trailing) || trailing < 0xDC00u || trailing > 0xDFFFu)
                {
                    return false;
                }
                cursor += 6u;
                code = 0x10000u + ((code - 0xD800u) << 10u) + (trailing - 0xDC00u);
            }
            else if (code >= 0xDC00u && code <= 0xDFFFu)
            {
                return false;
            }
            appendUtf8(code);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

/// The bytes between a string value's quotes.
///
/// @param value the value text.
/// @return the contents, or absent when the value is not quoted.
[[nodiscard]] std::optional<std::string_view> stringContents(std::string_view value) noexcept
{
    if (value.size() < 2u || value.front() != '"' || value.back() != '"')
    {
        return std::nullopt;
    }
    return value.substr(1u, value.size() - 2u);
}

/// Reads one object's members, whose opening brace is at the cursor.
///
/// @param text the document.
/// @param cursor the position of the opening brace, which ends past the closing one.
/// @param limits how many members and how much nesting to admit.
/// @param members receives the members.
/// @return why the scan stopped.
[[nodiscard]] JsonScanStatus readObject(std::string_view text, std::size_t& cursor, const JsonLimits& limits,
                                        std::vector<JsonMember>& members)
{
    if (cursor >= text.size() || text[cursor] != '{')
    {
        return JsonScanStatus::Malformed;
    }
    ++cursor;
    skipSpace(text, cursor);
    if (cursor < text.size() && text[cursor] == '}')
    {
        ++cursor;
        return JsonScanStatus::Ok;
    }

    while (true)
    {
        skipSpace(text, cursor);
        const std::size_t nameBegin = cursor;
        if (!skipString(text, cursor))
        {
            return JsonScanStatus::Malformed;
        }
        const std::string_view name = text.substr(nameBegin + 1u, cursor - nameBegin - 2u);

        skipSpace(text, cursor);
        if (cursor >= text.size() || text[cursor] != ':')
        {
            return JsonScanStatus::Malformed;
        }
        ++cursor;
        skipSpace(text, cursor);

        const std::size_t valueBegin = cursor;
        if (const JsonScanStatus status = skipValue(text, cursor, limits.maximumDepth); status != JsonScanStatus::Ok)
        {
            return status;
        }
        if (members.size() >= limits.maximumEntries)
        {
            return JsonScanStatus::TooManyEntries;
        }
        members.push_back(JsonMember{.name = name, .value = text.substr(valueBegin, cursor - valueBegin)});

        skipSpace(text, cursor);
        if (cursor >= text.size())
        {
            return JsonScanStatus::Malformed;
        }
        if (text[cursor] == ',')
        {
            ++cursor;
            continue;
        }
        if (text[cursor] == '}')
        {
            ++cursor;
            return JsonScanStatus::Ok;
        }
        return JsonScanStatus::Malformed;
    }
}

} // namespace

JsonScanStatus scanJsonObject(const std::string_view text, const JsonLimits& limits, std::vector<JsonMember>& members)
{
    members.clear();
    std::size_t cursor = 0u;
    skipSpace(text, cursor);
    const JsonScanStatus status = readObject(text, cursor, limits, members);
    if (status != JsonScanStatus::Ok)
    {
        members.clear();
        return status;
    }
    skipSpace(text, cursor);
    if (cursor != text.size())
    {
        members.clear();
        return JsonScanStatus::Malformed;
    }
    return JsonScanStatus::Ok;
}

JsonScanStatus scanJsonArray(const std::string_view text, const JsonLimits& limits,
                             std::vector<std::string_view>& elements)
{
    elements.clear();
    std::size_t cursor = 0u;
    skipSpace(text, cursor);
    if (cursor >= text.size() || text[cursor] != '[')
    {
        return JsonScanStatus::Malformed;
    }
    ++cursor;
    skipSpace(text, cursor);
    if (cursor < text.size() && text[cursor] == ']')
    {
        ++cursor;
        skipSpace(text, cursor);
        return cursor == text.size() ? JsonScanStatus::Ok : JsonScanStatus::Malformed;
    }

    while (true)
    {
        skipSpace(text, cursor);
        const std::size_t valueBegin = cursor;
        if (const JsonScanStatus status = skipValue(text, cursor, limits.maximumDepth); status != JsonScanStatus::Ok)
        {
            elements.clear();
            return status;
        }
        if (elements.size() >= limits.maximumEntries)
        {
            elements.clear();
            return JsonScanStatus::TooManyEntries;
        }
        elements.push_back(text.substr(valueBegin, cursor - valueBegin));

        skipSpace(text, cursor);
        if (cursor >= text.size())
        {
            elements.clear();
            return JsonScanStatus::Malformed;
        }
        if (text[cursor] == ',')
        {
            ++cursor;
            continue;
        }
        if (text[cursor] == ']')
        {
            ++cursor;
            skipSpace(text, cursor);
            if (cursor != text.size())
            {
                elements.clear();
                return JsonScanStatus::Malformed;
            }
            return JsonScanStatus::Ok;
        }
        elements.clear();
        return JsonScanStatus::Malformed;
    }
}

const JsonMember* findJsonMember(const std::span<const JsonMember> members, const std::string_view name)
{
    std::string decoded;
    for (const JsonMember& member : members)
    {
        if (member.name.find('\\') == std::string_view::npos)
        {
            if (member.name == name)
            {
                return &member;
            }
            continue;
        }
        if (decodeEscapes(member.name, decoded) && decoded == name)
        {
            return &member;
        }
    }
    return nullptr;
}

std::optional<std::string> jsonString(const std::string_view value)
{
    const std::optional<std::string_view> contents = stringContents(value);
    if (!contents.has_value())
    {
        return std::nullopt;
    }
    std::string decoded;
    if (!decodeEscapes(*contents, decoded))
    {
        return std::nullopt;
    }
    return decoded;
}

std::optional<std::int64_t> jsonInteger(const std::string_view value)
{
    std::int64_t parsed = 0;
    const char* const first = value.data();
    const char* const last = first + value.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != last)
    {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::uint64_t> jsonUnsigned(const std::string_view value)
{
    std::uint64_t parsed = 0u;
    const char* const first = value.data();
    const char* const last = first + value.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != last)
    {
        return std::nullopt;
    }
    return parsed;
}

std::optional<double> jsonNumber(const std::string_view value)
{
    double parsed = 0.0;
    const char* const first = value.data();
    const char* const last = first + value.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last || !std::isfinite(parsed))
    {
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> jsonBoolean(const std::string_view value)
{
    if (value == "true")
    {
        return true;
    }
    if (value == "false")
    {
        return false;
    }
    return std::nullopt;
}

bool jsonIsNull(const std::string_view value) noexcept
{
    return value == "null";
}

bool jsonIsArray(const std::string_view value) noexcept
{
    return !value.empty() && value.front() == '[';
}

} // namespace an::catalog_detail
