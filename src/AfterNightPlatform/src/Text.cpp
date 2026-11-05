// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Prefix and suffix tests, the upper-case copy, and the UTF-8 conversions.

#include <AfterNightPlatform/Text.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <iconv.h>
#endif

namespace an::platform
{

namespace
{

[[nodiscard]] bool sameText(const std::string_view left, const std::string_view right,
                            const CaseSensitivity sensitivity)
{
    if (left.length() != right.length())
    {
        return false;
    }
    if (sensitivity == CaseSensitivity::Sensitive)
    {
        return left == right;
    }
    for (std::size_t index = 0; index < left.length(); ++index)
    {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
        {
            return false;
        }
    }
    return true;
}

#ifndef _WIN32
/// Converts `length` bytes between two iconv encodings; throws std::runtime_error when iconv refuses.
[[nodiscard]] std::string convertEncoding(const char* fromEncoding, const char* toEncoding, const char* input,
                                          const std::size_t length)
{
    const iconv_t converter = iconv_open(toEncoding, fromEncoding);
    if (converter == reinterpret_cast<iconv_t>(-1))
    {
        throw std::runtime_error("iconv_open failed");
    }

    // Four output bytes per input byte covers the widest direction, UTF-8 to UTF-32.
    const std::size_t capacity = length * 4 + 4;
    std::vector<char> output(capacity);
    char* outputCursor = output.data();
    const char* inputCursor = input;
    std::size_t inputLeft = length;
    std::size_t outputLeft = capacity;

    if (iconv(converter, const_cast<char**>(&inputCursor), &inputLeft, &outputCursor, &outputLeft) ==
        static_cast<std::size_t>(-1))
    {
        iconv_close(converter);
        throw std::runtime_error("iconv failed");
    }

    iconv_close(converter);
    return std::string(output.data(), capacity - outputLeft);
}
#endif

} // namespace

bool startsWith(const std::string_view text, const std::string_view prefix, const CaseSensitivity sensitivity)
{
    if (text.length() < prefix.length() || prefix.empty())
    {
        return false;
    }
    return sameText(text.substr(0, prefix.length()), prefix, sensitivity);
}

bool endsWith(const std::string_view text, const std::string_view suffix, const CaseSensitivity sensitivity)
{
    if (text.length() < suffix.length() || suffix.empty())
    {
        return false;
    }
    return sameText(text.substr(text.length() - suffix.length()), suffix, sensitivity);
}

std::string toUpper(const std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](const char character) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    });
    return result;
}

std::string wideToUtf8(const std::wstring_view text)
{
#ifdef _WIN32
    std::string result;
    if (!text.empty())
    {
        const int required = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.length()), nullptr, 0,
                                                   nullptr, nullptr);
        result.resize(static_cast<std::size_t>(required), 0);
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.length()), result.data(),
                              static_cast<int>(result.size()), nullptr, nullptr);
    }
    return result;
#else
    return convertEncoding("WCHAR_T", "UTF-8", reinterpret_cast<const char*>(text.data()),
                           text.size() * sizeof(wchar_t));
#endif
}

std::wstring utf8ToWide(const std::string_view text)
{
#ifdef _WIN32
    std::wstring result;
    if (!text.empty())
    {
        const int required =
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.length()), nullptr, 0);
        result.resize(static_cast<std::size_t>(required), 0);
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.length()), result.data(),
                              static_cast<int>(result.size()));
    }
    return result;
#else
    const std::string converted = convertEncoding("UTF-8", "WCHAR_T", text.data(), text.size());
    return std::wstring(reinterpret_cast<const wchar_t*>(converted.data()), converted.size() / sizeof(wchar_t));
#endif
}

} // namespace an::platform
