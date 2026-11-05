// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - text handling and failure building shared by the catalog domain.
//
// Several of this domain's units answer questions about names: the text parser recognises its
// header row by one, the catalog indexes and searches them, and the designation canonicalizer and
// the alias relation compare them. Writing the case rule once is what stops those units
// disagreeing about what "the same name" means - and the rule itself matters, because
// the catalogs this engine ships carry UTF-8 designations. `std::tolower` is locale-dependent for
// every byte above 127, so folding one with it makes the meaning of a name depend on what the
// process happened to call `setlocale` with. Nothing here looks above 127.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/// The catalog domain's private helpers: ASCII text handling, failure builders, the bounded JSON
/// scan, little-endian field readers and the persistent catalog allocator.
namespace an::catalog_detail
{

/// The ASCII lower case of one byte.
///
/// @param value the byte to fold.
/// @return the folded byte; every byte outside `A`-`Z` is returned unchanged.
[[nodiscard]] constexpr char foldAscii(char value) noexcept
{
    return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

/// Whether a byte is one of the ASCII space characters this domain trims.
///
/// @param value the byte to test.
/// @return true for space, tab, carriage return, line feed, vertical tab and form feed.
[[nodiscard]] constexpr bool isAsciiSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\v' || value == '\f';
}

/// Whether a byte is an ASCII decimal digit.
///
/// @param value the byte to test.
/// @return true for `0` through `9`.
[[nodiscard]] constexpr bool isAsciiDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

/// The same text without its leading and trailing ASCII space.
///
/// @param text the text to trim, which is only read.
/// @return a view into `text`, which must outlive the result.
[[nodiscard]] inline std::string_view trimAscii(std::string_view text) noexcept
{
    std::size_t begin = 0u;
    while (begin < text.size() && isAsciiSpace(text[begin]))
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && isAsciiSpace(text[end - 1u]))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

/// Whether two texts are equal ignoring ASCII case.
///
/// @param left one text.
/// @param right the other.
/// @return true when they have the same length and every byte folds to the same value.
[[nodiscard]] inline bool equalsFolded(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        if (foldAscii(left[index]) != foldAscii(right[index]))
        {
            return false;
        }
    }
    return true;
}

/// Whether one text begins with another, ignoring ASCII case.
///
/// @param text the text to inspect.
/// @param prefix what it might begin with.
/// @return true when `text` is at least as long as `prefix` and their leading bytes fold equal.
[[nodiscard]] inline bool startsWithFolded(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() && equalsFolded(text.substr(0u, prefix.size()), prefix);
}

/// Whether one text occurs inside another.
///
/// @param haystack the text to search.
/// @param needle what to look for. An empty needle is never found, which is what stops an empty
///        query matching every entry.
/// @param folded whether to ignore ASCII case.
/// @return true when the needle occurs.
[[nodiscard]] inline bool containsText(std::string_view haystack, std::string_view needle, bool folded) noexcept
{
    if (needle.empty() || needle.size() > haystack.size())
    {
        return false;
    }
    if (!folded)
    {
        return haystack.find(needle) != std::string_view::npos;
    }
    const std::size_t last = haystack.size() - needle.size();
    for (std::size_t start = 0u; start <= last; ++start)
    {
        if (equalsFolded(haystack.substr(start, needle.size()), needle))
        {
            return true;
        }
    }
    return false;
}

/// The ASCII lower case of a whole text, as an owned string.
///
/// @param text the text to fold.
/// @return the folded copy.
[[nodiscard]] inline std::string foldedCopy(std::string_view text)
{
    std::string folded;
    folded.reserve(text.size());
    for (const char value : text)
    {
        folded.push_back(foldAscii(value));
    }
    return folded;
}

/// Builds one described failure in the algorithms domain.
///
/// @param code why the operation failed.
/// @param message what a person reads.
/// @param operation the engine operation that failed.
/// @return the failure.
[[nodiscard]] inline Status catalogFailure(ErrorCode code, const char* message, const char* operation)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Builds a ceiling failure carrying the requested and permitted counts.
///
/// @param message what a person reads.
/// @param operation the engine operation that failed.
/// @param requested how much was asked for.
/// @param limit how much is allowed.
/// @return the failure.
[[nodiscard]] inline Status catalogLimitFailure(const char* message, const char* operation, std::uint64_t requested,
                                                std::uint64_t limit)
{
    return Status::failure(ErrorCode::LimitExceeded,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                    StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

} // namespace an::catalog_detail
