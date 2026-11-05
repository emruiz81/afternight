// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - an XISF property as a card's text and comment, and a card as an XISF property.
//
// A property's card carries its value as text and its type in the comment, the way an Exif field's card carries its
// Exif type, so a unit read and written again states every property as its writer did: a matrix stays a matrix of
// its element type and dimensions, a time stays a TimePoint, an integer keeps its width. The text is exact and is
// what the writer parses back: a string is its own text, a TimePoint is ISO 8601 in UTC, and every number - a scalar,
// the parts of a complex number, the elements of a vector or matrix - is its shortest exact decimal, the elements
// separated by commas and the rows of a matrix by semicolons. A comment of the property's own follows its type after
// a semicolon. A card whose comment names no XISF type, or whose text is not a value of the type it names, is written
// as a String with its comment kept, which is what a card another source stated becomes.

#include "io/xisf/XisfFile.h"

#include <openxisf/format.h>
#include <openxisf/property.h>

#include <array>
#include <charconv>
#include <complex>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace an
{
namespace
{
/// Whether a type is a vector a property value holds its elements in.
template <typename T> struct IsElementVector : std::false_type
{
};
template <typename E> struct IsElementVector<std::vector<E>> : std::true_type
{
};

/// Whether a type is a complex number of standard parts.
template <typename T> struct IsStandardComplex : std::false_type
{
};
template <typename E> struct IsStandardComplex<std::complex<E>> : std::true_type
{
};

/// Appends a number in its shortest exact text: the text a card carries for every numeric value, so a round trip
/// keeps the value rather than a rounding of it.
template <typename T> void appendNumber(std::string& text, T value)
{
    std::array<char, 48> buffer{};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (written.ec == std::errc{})
        text.append(buffer.data(), written.ptr);
}

/// Appends one scalar: a Boolean as a word, a number in its shortest exact text, a complex number as `(real,imag)`.
template <typename T> void appendScalar(std::string& text, const T& value)
{
    if constexpr (std::is_same_v<T, bool>)
        text += value ? "true" : "false";
    else if constexpr (IsStandardComplex<T>::value)
    {
        text += '(';
        appendNumber(text, value.real());
        text += ',';
        appendNumber(text, value.imag());
        text += ')';
    }
    else
        appendNumber(text, value);
}

/// Appends element `index` of a vector or matrix value, of type `E`.
template <typename E>
void appendElement(std::string& text, const openxisf::property_value& value, std::size_t index, const E& element)
{
    if constexpr (std::is_same_v<E, openxisf::int128> || std::is_same_v<E, openxisf::uint128>)
        // Integers beyond 64 bits have no standard text of their own, and the library's decimal text is exact.
        text += openxisf::format_element(value, index);
    else if constexpr (std::is_same_v<E, openxisf::float128>)
        appendNumber(text, openxisf::to_double(element));
    else if constexpr (std::is_same_v<E, openxisf::complex128>)
    {
        text += '(';
        appendNumber(text, openxisf::to_double(element.real));
        text += ',';
        appendNumber(text, openxisf::to_double(element.imag));
        text += ')';
    }
    else
        appendScalar(text, element);
}

/// Whether a property type is one of the matrix types, which the enumeration lists last.
bool isMatrixType(openxisf::property_type type) noexcept
{
    return static_cast<int>(type) >= static_cast<int>(openxisf::property_type::i8_matrix);
}

/// The property type a name in the specification names, such as `F64Matrix`.
std::optional<openxisf::property_type> typeNamed(std::string_view name) noexcept
{
    for (auto type = static_cast<int>(openxisf::property_type::boolean);
         type <= static_cast<int>(openxisf::property_type::c128_matrix); ++type)
        if (openxisf::property_type_name(static_cast<openxisf::property_type>(type)) == name)
            return static_cast<openxisf::property_type>(type);
    return std::nullopt;
}

/// A decimal count with no sign, as a matrix's dimensions are written.
std::optional<std::uint64_t> countOf(std::string_view text) noexcept
{
    std::uint64_t value = 0u;
    const auto* const last = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), last, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != last)
        return std::nullopt;
    return value;
}

/// What a card's comment says about the property it states.
struct StatedType
{
    openxisf::property_type type = openxisf::property_type::string; ///< The type, a String when none is named.
    std::uint64_t rows = 0u; ///< A matrix's rows.
    std::uint64_t columns = 0u; ///< A matrix's columns.
    std::string comment; ///< The property's own comment.
    bool named = false; ///< Whether the comment named the type.
};

/// The type a card's comment names - its first word, with a matrix's `rows`x`columns` after it - and the property's own
/// comment after a semicolon. A comment that does not begin so names no type and is the property's comment whole.
StatedType statedType(std::string_view comment)
{
    const StatedType untyped{.comment = std::string(comment)};
    const auto nameEnd = comment.find_first_of(" ;");
    const auto type = typeNamed(comment.substr(0u, nameEnd));
    if (!type)
        return untyped;
    StatedType stated{.type = *type, .named = true};
    auto rest = nameEnd == std::string_view::npos ? std::string_view{} : comment.substr(nameEnd);
    if (isMatrixType(*type))
    {
        if (!rest.starts_with(' '))
            return untyped;
        const auto dimensionsEnd = rest.find(';');
        const auto dimensions =
            rest.substr(1u, dimensionsEnd == std::string_view::npos ? std::string_view::npos : dimensionsEnd - 1u);
        const auto times = dimensions.find('x');
        const auto rows = times == std::string_view::npos ? std::nullopt : countOf(dimensions.substr(0u, times));
        const auto columns = times == std::string_view::npos ? std::nullopt : countOf(dimensions.substr(times + 1u));
        if (!rows || !columns)
            return untyped;
        stated.rows = *rows;
        stated.columns = *columns;
        rest = dimensionsEnd == std::string_view::npos ? std::string_view{} : rest.substr(dimensionsEnd);
    }
    if (rest.empty())
        return stated;
    if (!rest.starts_with("; "))
        return untyped;
    stated.comment = std::string(rest.substr(2u));
    return stated;
}

/// A number in the whole of a token, in the text `appendNumber` writes.
template <typename T> std::optional<T> numberOf(std::string_view token) noexcept
{
    T value{};
    const auto* const last = token.data() + token.size();
    const auto parsed = std::from_chars(token.data(), last, value);
    if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != last)
        return std::nullopt;
    return value;
}

/// A 128-bit integer in the whole of a token, in the decimal text the library writes one in.
template <typename T> std::optional<T> wideIntegerOf(std::string_view token) noexcept
{
    constexpr bool kSigned = std::is_same_v<T, openxisf::int128>;
    const bool negative = kSigned && token.starts_with('-');
    if (negative)
        token.remove_prefix(1u);
    if (token.empty())
        return std::nullopt;
    // The magnitude, accumulated in two halves: times ten is times eight plus times two.
    std::uint64_t high = 0u;
    std::uint64_t low = 0u;
    const auto shifted = [](std::uint64_t h, std::uint64_t l, int bits) {
        return std::pair{(h << bits) | (l >> (64 - bits)), l << bits};
    };
    for (const char c : token)
    {
        // A magnitude of 2^125 or more is past every 128-bit value once it is multiplied by eight.
        if (c < '0' || c > '9' || (high >> 61) != 0u)
            return std::nullopt;
        const auto [h8, l8] = shifted(high, low, 3);
        const auto [h2, l2] = shifted(high, low, 1);
        std::uint64_t sumLow = l8 + l2;
        std::uint64_t sumHigh = h8 + h2 + (sumLow < l8 ? 1u : 0u);
        if (sumHigh < h8)
            return std::nullopt;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        low = sumLow + digit;
        high = sumHigh + (low < sumLow ? 1u : 0u);
        if (high < sumHigh)
            return std::nullopt;
    }
    if constexpr (kSigned)
    {
        // A magnitude of at most 2^127 - 1, or 2^127 for a negative value, which is the one with only the top bit.
        constexpr std::uint64_t kTop = std::uint64_t{1u} << 63;
        if (high > kTop || (high == kTop && (low != 0u || !negative)))
            return std::nullopt;
        if (negative)
        {
            high = ~high;
            low = ~low + 1u;
            if (low == 0u)
                ++high;
        }
        return openxisf::int128{.high = static_cast<std::int64_t>(high), .low = low};
    }
    else
        return openxisf::uint128{.high = high, .low = low};
}

/// The binary128 value a double is: every double is one exactly.
openxisf::float128 float128Of(double value) noexcept
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof bits);
    const std::uint64_t sign = bits >> 63;
    auto exponent = static_cast<std::int64_t>((bits >> 52) & 0x7ffu);
    std::uint64_t fraction = bits & ((std::uint64_t{1u} << 52) - 1u);
    std::uint64_t biased = 0u;
    if (exponent == 0x7ff)
        biased = 0x7fffu;
    else if (exponent != 0 || fraction != 0u)
    {
        if (exponent == 0)
        {
            // A subnormal double is a normal binary128 value: its leading one moves into the implicit place.
            exponent = 1;
            while ((fraction & (std::uint64_t{1u} << 52)) == 0u)
            {
                fraction <<= 1;
                --exponent;
            }
            fraction &= (std::uint64_t{1u} << 52) - 1u;
        }
        biased = static_cast<std::uint64_t>(exponent - 1023 + 16383);
    }
    return openxisf::float128{.high = (sign << 63) | (biased << 48) | (fraction >> 4), .low = fraction << 60};
}

/// A date and time in the text the library writes one in, `YYYY-MM-DDTHH:MM:SS[.fraction]Z`, when it is one the
/// library can write: a year of four digits, a day its month has, and a second up to a leap second.
std::optional<openxisf::date_time> timePointOf(std::string_view text) noexcept
{
    const auto field = [&](std::size_t at, std::size_t digits) -> std::optional<unsigned> {
        if (at + digits > text.size())
            return std::nullopt;
        unsigned value = 0u;
        for (std::size_t index = at; index < at + digits; ++index)
        {
            if (text[index] < '0' || text[index] > '9')
                return std::nullopt;
            value = value * 10u + static_cast<unsigned>(text[index] - '0');
        }
        return value;
    };
    if (text.size() < 20u || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text.back() != 'Z')
        return std::nullopt;
    const auto year = field(0u, 4u), month = field(5u, 2u), day = field(8u, 2u), hour = field(11u, 2u),
               minute = field(14u, 2u), second = field(17u, 2u);
    if (!year || !month || !day || !hour || !minute || !second)
        return std::nullopt;
    std::uint32_t nanosecond = 0u;
    const auto fraction = text.substr(19u, text.size() - 20u);
    if (!fraction.empty())
    {
        if (fraction.size() < 2u || fraction.size() > 10u || fraction[0] != '.')
            return std::nullopt;
        const auto digits = field(20u, fraction.size() - 1u);
        if (!digits)
            return std::nullopt;
        nanosecond = *digits;
        for (std::size_t scale = fraction.size() - 1u; scale < 9u; ++scale)
            nanosecond *= 10u;
    }
    const bool leap = (*year % 4u == 0u && *year % 100u != 0u) || *year % 400u == 0u;
    constexpr std::array<unsigned, 12u> kDays = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};
    if (*month < 1u || *month > 12u || *day < 1u || *day > kDays[*month - 1u] + (*month == 2u && leap ? 1u : 0u) ||
        *hour > 23u || *minute > 59u || *second > 60u)
        return std::nullopt;
    return openxisf::date_time{.year = static_cast<int>(*year),
                               .month = *month,
                               .day = *day,
                               .hour = *hour,
                               .minute = *minute,
                               .second = *second,
                               .nanosecond = nanosecond};
}

/// A complex number written `(real,imag)` from `at`, which is moved past it.
template <typename P> std::optional<std::pair<P, P>> complexAt(std::string_view text, std::size_t& at) noexcept
{
    if (at >= text.size() || text[at] != '(')
        return std::nullopt;
    const auto close = text.find(')', at);
    if (close == std::string_view::npos)
        return std::nullopt;
    const auto inner = text.substr(at + 1u, close - at - 1u);
    const auto comma = inner.find(',');
    if (comma == std::string_view::npos)
        return std::nullopt;
    const auto real = numberOf<P>(inner.substr(0u, comma));
    const auto imag = numberOf<P>(inner.substr(comma + 1u));
    if (!real || !imag)
        return std::nullopt;
    at = close + 1u;
    return std::pair{*real, *imag};
}

/// One element of type `E` from `at`, which is moved to the separator after it or to the end.
template <typename E> std::optional<E> elementAt(std::string_view text, std::size_t& at) noexcept
{
    if constexpr (IsStandardComplex<E>::value)
    {
        const auto parts = complexAt<typename E::value_type>(text, at);
        if (!parts)
            return std::nullopt;
        return E(parts->first, parts->second);
    }
    else if constexpr (std::is_same_v<E, openxisf::complex128>)
    {
        const auto parts = complexAt<double>(text, at);
        if (!parts)
            return std::nullopt;
        return openxisf::complex128{.real = float128Of(parts->first), .imag = float128Of(parts->second)};
    }
    else
    {
        const auto end = (std::min)(text.find_first_of(",;", at), text.size());
        const auto token = text.substr(at, end - at);
        at = end;
        if constexpr (std::is_same_v<E, openxisf::int128> || std::is_same_v<E, openxisf::uint128>)
            return wideIntegerOf<E>(token);
        else if constexpr (std::is_same_v<E, openxisf::float128>)
        {
            const auto value = numberOf<double>(token);
            if (!value)
                return std::nullopt;
            return float128Of(*value);
        }
        else
            return numberOf<E>(token);
    }
}

/// The vector, or the matrix of `rows` by `columns`, of elements of type `E` that a card's text states, its elements
/// separated by commas and, in a matrix, its rows by semicolons.
template <typename E>
std::optional<openxisf::property_value> arrayOf(std::string_view text, bool matrix, std::uint64_t rows,
                                                std::uint64_t columns)
{
    if (matrix && rows != 0u && columns > std::numeric_limits<std::uint64_t>::max() / rows)
        return std::nullopt;
    std::vector<E> elements;
    for (std::size_t at = 0u; at < text.size();)
    {
        const auto element = elementAt<E>(text, at);
        if (!element)
            return std::nullopt;
        elements.push_back(*element);
        if (at == text.size())
            break;
        const bool rowEnd = matrix && columns != 0u && elements.size() % columns == 0u;
        if (text[at] != (rowEnd ? ';' : ',') || ++at == text.size())
            return std::nullopt;
    }
    if (!matrix)
        return openxisf::property_value(std::move(elements));
    if (elements.size() != rows * columns)
        return std::nullopt;
    return openxisf::property_value::matrix(rows, columns, std::move(elements));
}

/// The value of a stated type a card's text is, or nothing when the text is not one.
std::optional<openxisf::property_value> valueOf(const StatedType& stated, std::string_view text)
{
    using openxisf::property_type;
    const bool matrix = isMatrixType(stated.type);
    const auto array = [&]<typename E>() { return arrayOf<E>(text, matrix, stated.rows, stated.columns); };
    switch (stated.type)
    {
    case property_type::boolean:
        if (text == "true" || text == "false")
            return openxisf::property_value(text == "true");
        return std::nullopt;
    case property_type::int8:
        if (const auto value = numberOf<std::int8_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::uint8:
        if (const auto value = numberOf<std::uint8_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::int16:
        if (const auto value = numberOf<std::int16_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::uint16:
        if (const auto value = numberOf<std::uint16_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::int32:
        if (const auto value = numberOf<std::int32_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::uint32:
        if (const auto value = numberOf<std::uint32_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::int64:
        if (const auto value = numberOf<std::int64_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::uint64:
        if (const auto value = numberOf<std::uint64_t>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::int128:
        if (const auto value = wideIntegerOf<openxisf::int128>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::uint128:
        if (const auto value = wideIntegerOf<openxisf::uint128>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::float32:
        if (const auto value = numberOf<float>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::float64:
        if (const auto value = numberOf<double>(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::float128:
        return openxisf::property_value::from_float128_text(text);
    case property_type::complex32: {
        std::size_t at = 0u;
        const auto value = elementAt<std::complex<float>>(text, at);
        if (!value || at != text.size())
            return std::nullopt;
        return openxisf::property_value(*value);
    }
    case property_type::complex64: {
        std::size_t at = 0u;
        const auto value = elementAt<std::complex<double>>(text, at);
        if (!value || at != text.size())
            return std::nullopt;
        return openxisf::property_value(*value);
    }
    case property_type::complex128:
        return openxisf::property_value::from_complex128_text(text);
    case property_type::string:
        return openxisf::property_value(std::string(text));
    case property_type::time_point:
        if (const auto value = timePointOf(text))
            return openxisf::property_value(*value);
        return std::nullopt;
    case property_type::i8_vector:
    case property_type::i8_matrix:
        return array.template operator()<std::int8_t>();
    case property_type::ui8_vector:
    case property_type::ui8_matrix:
        return array.template operator()<std::uint8_t>();
    case property_type::i16_vector:
    case property_type::i16_matrix:
        return array.template operator()<std::int16_t>();
    case property_type::ui16_vector:
    case property_type::ui16_matrix:
        return array.template operator()<std::uint16_t>();
    case property_type::i32_vector:
    case property_type::i32_matrix:
        return array.template operator()<std::int32_t>();
    case property_type::ui32_vector:
    case property_type::ui32_matrix:
        return array.template operator()<std::uint32_t>();
    case property_type::i64_vector:
    case property_type::i64_matrix:
        return array.template operator()<std::int64_t>();
    case property_type::ui64_vector:
    case property_type::ui64_matrix:
        return array.template operator()<std::uint64_t>();
    case property_type::i128_vector:
    case property_type::i128_matrix:
        return array.template operator()<openxisf::int128>();
    case property_type::ui128_vector:
    case property_type::ui128_matrix:
        return array.template operator()<openxisf::uint128>();
    case property_type::f32_vector:
    case property_type::f32_matrix:
        return array.template operator()<float>();
    case property_type::f64_vector:
    case property_type::f64_matrix:
        return array.template operator()<double>();
    case property_type::f128_vector:
    case property_type::f128_matrix:
        return array.template operator()<openxisf::float128>();
    case property_type::c32_vector:
    case property_type::c32_matrix:
        return array.template operator()<std::complex<float>>();
    case property_type::c64_vector:
    case property_type::c64_matrix:
        return array.template operator()<std::complex<double>>();
    case property_type::c128_vector:
    case property_type::c128_matrix:
        return array.template operator()<openxisf::complex128>();
    }
    return std::nullopt;
}
} // namespace

std::optional<std::string> xisfPropertyText(const openxisf::property_value& value, std::size_t bound)
{
    // Every element takes a byte and all but the last a separator, so a value of more elements than that is refused
    // before an element is written.
    if (value.length() > bound / 2u + 1u)
        return std::nullopt;
    std::string text;
    const bool fits = std::visit(
        [&](const auto& held) -> bool {
            using Held = std::remove_cvref_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::string>)
            {
                // A String, and the text a Float128 or Complex128 scalar is kept as.
                if (held.size() > bound)
                    return false;
                text = held;
            }
            else if constexpr (std::is_same_v<Held, openxisf::date_time> || std::is_same_v<Held, openxisf::int128> ||
                               std::is_same_v<Held, openxisf::uint128>)
                text = openxisf::format_value(value);
            else if constexpr (IsElementVector<Held>::value)
            {
                const auto columns = value.columns();
                for (std::size_t index = 0u; index < held.size(); ++index)
                {
                    if (index != 0u)
                        text += columns != 0u && index % columns == 0u ? ';' : ',';
                    appendElement(text, value, index, held[index]);
                    if (text.size() > bound)
                        return false;
                }
            }
            else
                appendScalar(text, held);
            return text.size() <= bound;
        },
        value.data());
    if (!fits)
        return std::nullopt;
    return text;
}

std::string xisfPropertyComment(const openxisf::property& property)
{
    const auto type = property.value.type();
    std::string comment(openxisf::property_type_name(type));
    if (isMatrixType(type))
        comment += ' ' + std::to_string(property.value.rows()) + 'x' + std::to_string(property.value.columns());
    if (!property.comment.empty())
    {
        comment += "; ";
        comment += property.comment;
    }
    return comment;
}

openxisf::property xisfPropertyOf(std::string id, std::string_view text, std::string_view comment)
{
    auto stated = statedType(comment);
    std::optional<openxisf::property_value> value;
    if (stated.named)
    {
        try
        {
            value = valueOf(stated, text);
        }
        catch (const openxisf::error&)
        {
            // A Float128 or Complex128 scalar's text the library does not read as one.
        }
    }
    return openxisf::property{.id = std::move(id),
                              .value = value ? std::move(*value) : openxisf::property_value(std::string(text)),
                              .format = {},
                              .comment = std::move(stated.comment)};
}
} // namespace an
