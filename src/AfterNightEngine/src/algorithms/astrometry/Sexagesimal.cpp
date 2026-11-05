// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading, splitting and writing sky angles in hours or degrees, minutes and
// seconds.
//
// A reading first decides whether the text is one decimal number - degrees - or fields, and a field
// reading accumulates the whole field, the minutes and the seconds in one fixed order of operations,
// so the same text always gives the same double. A split rounds the angle once, to a whole number of
// the smallest increment, in 64-bit integers, and only then divides it into fields, which is what
// makes a carry reach every field and a written label exact.

#include <AfterNightEngine/Algorithms/Astrometry/Sexagesimal.h>

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kParseRightAscension = "an::parseRightAscensionDegrees";
constexpr const char* kParseDeclination = "an::parseDeclinationDegrees";
constexpr const char* kParseCoordinate = "an::parseSkyCoordinate";
constexpr const char* kSplitRightAscension = "an::splitRightAscension";
constexpr const char* kSplitDeclination = "an::splitDeclination";
constexpr const char* kFormatRightAscension = "an::formatRightAscension";
constexpr const char* kFormatDeclination = "an::formatDeclination";

/// The powers of ten a split's increment is drawn from, up to the most digits a split keeps.
constexpr std::array<std::int64_t, kMaxSexagesimalFractionDigits + 1u> kPowersOfTen{1,     10,     100,    1000,
                                                                                    10000, 100000, 1000000};

/// Which axis a reading or a split is for.
enum class Axis : std::uint8_t
{
    RightAscension,
    Declination
};

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param operation the public operation that failed.
/// @param message the explanation for a person.
/// @return the complete failure.
[[nodiscard]] Status angleFailure(const ErrorCode code, const char* const operation, std::string message)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a character is a blank a written angle may carry around or between its fields.
///
/// @param character the character to test.
/// @return true for a space or a tab.
[[nodiscard]] constexpr bool isBlank(const char character) noexcept
{
    return character == ' ' || character == '\t';
}

/// Whether a character is a decimal digit.
///
/// @param character the character to test.
/// @return true for `0` to `9`.
[[nodiscard]] constexpr bool isDigit(const char character) noexcept
{
    return character >= '0' && character <= '9';
}

/// The text without its surrounding blanks.
///
/// @param text the text to trim.
/// @return the trimmed view.
[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && isBlank(text.front()))
    {
        text.remove_prefix(1u);
    }
    while (!text.empty() && isBlank(text.back()))
    {
        text.remove_suffix(1u);
    }
    return text;
}

/// The angle a text writes, without its surrounding blanks and a FITS string's quotes.
///
/// One pair of enclosing single quotes is removed with the blanks inside it, and a doubled quote inside
/// is undone, which is the only case that allocates.
class AngleText
{
public:
    /// Normalizes the text.
    ///
    /// @param text the written angle, which must outlive this object.
    explicit AngleText(std::string_view text)
    {
        std::string_view view = trimmed(text);
        if (view.size() >= 2u && view.front() == '\'' && view.back() == '\'')
        {
            view = trimmed(view.substr(1u, view.size() - 2u));
            if (view.find("''") != std::string_view::npos)
            {
                std::string undone;
                undone.reserve(view.size());
                for (std::size_t index = 0u; index < view.size(); ++index)
                {
                    undone.push_back(view[index]);
                    if (view[index] == '\'' && index + 1u < view.size() && view[index + 1u] == '\'')
                    {
                        ++index;
                    }
                }
                m_storage = std::move(undone);
                m_view = *m_storage;
                return;
            }
        }
        m_view = view;
    }

    AngleText(const AngleText&) = delete;
    AngleText& operator=(const AngleText&) = delete;

    /// The normalized text.
    ///
    /// @return a view of the caller's text or of this object's own storage.
    [[nodiscard]] std::string_view view() const noexcept
    {
        return m_view;
    }

private:
    /// Storage for a text whose doubled quotes were undone.
    std::optional<std::string> m_storage{};

    /// The normalized text.
    std::string_view m_view{};
};

/// Whether a text is one decimal number: a sign, digits with at most one point, and an exponent.
///
/// @param text the normalized text.
/// @return true when the whole text is one number in that form, with at least one digit before the
///         exponent.
[[nodiscard]] bool isDecimalNumber(const std::string_view text) noexcept
{
    std::size_t index = 0u;
    if (index < text.size() && (text[index] == '+' || text[index] == '-'))
    {
        ++index;
    }
    std::size_t digits = 0u;
    bool point = false;
    for (; index < text.size(); ++index)
    {
        if (isDigit(text[index]))
        {
            ++digits;
        }
        else if (text[index] == '.' && !point)
        {
            point = true;
        }
        else
        {
            break;
        }
    }
    if (digits == 0u)
    {
        return false;
    }
    if (index < text.size() && (text[index] == 'e' || text[index] == 'E'))
    {
        ++index;
        if (index < text.size() && (text[index] == '+' || text[index] == '-'))
        {
            ++index;
        }
        std::size_t exponentDigits = 0u;
        while (index < text.size() && isDigit(text[index]))
        {
            ++index;
            ++exponentDigits;
        }
        if (exponentDigits == 0u)
        {
            return false;
        }
    }
    return index == text.size();
}

/// Reads a number already known to be in decimal form.
///
/// @param text digits with an optional sign, point and exponent; a leading plus is allowed.
/// @param out the number.
/// @return false when the number is too large or too small for a double.
[[nodiscard]] bool readDecimal(std::string_view text, double& out) noexcept
{
    if (!text.empty() && text.front() == '+')
    {
        text.remove_prefix(1u);
    }
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

/// How many bytes of a field separator start at a position, or zero when none does.
///
/// A separator is a blank, a colon, one of the unit letters `h`, `d`, `m` and `s` in either case, an
/// arcminute or arcsecond mark, or the degree sign in UTF-8.
///
/// @param text the normalized text.
/// @param index the position to look at, inside the text.
/// @return one or two, or zero.
[[nodiscard]] std::size_t separatorAt(const std::string_view text, const std::size_t index) noexcept
{
    switch (text[index])
    {
    case ' ':
    case '\t':
    case ':':
    case 'h':
    case 'H':
    case 'd':
    case 'D':
    case 'm':
    case 'M':
    case 's':
    case 'S':
    case '\'':
    case '"':
        return 1u;
    case '\xC2':
        return index + 1u < text.size() && text[index + 1u] == '\xB0' ? 2u : 0u;
    default:
        return 0u;
    }
}

/// The fields of a sexagesimal reading, before any of them is range-checked.
struct Fields
{
    /// Whether the first field carried a minus sign.
    bool negative = false;

    /// The fields' text, in order; `count` of them are meaningful.
    std::array<std::string_view, 3> text{};

    /// How many fields the reading has.
    std::size_t count = 0u;
};

/// Splits a normalized text into fields, or says why it is not a field reading.
///
/// @param text the normalized text.
/// @param out the fields.
/// @return null when the text is two or three well-formed fields, otherwise the reason.
[[nodiscard]] const char* splitFields(const std::string_view text, Fields& out) noexcept
{
    std::size_t index = 0u;
    if (index < text.size() && (text[index] == '+' || text[index] == '-'))
    {
        out.negative = text[index] == '-';
        ++index;
    }
    while (true)
    {
        const std::size_t start = index;
        std::size_t digits = 0u;
        std::size_t points = 0u;
        while (index < text.size() && (isDigit(text[index]) || text[index] == '.'))
        {
            digits += isDigit(text[index]) ? 1u : 0u;
            points += text[index] == '.' ? 1u : 0u;
            ++index;
        }
        if (index == start)
        {
            return index < text.size() ? "a field must begin with a digit, and only the first field may carry a sign"
                                       : "the text ends where a field was expected";
        }
        if (digits == 0u || points > 1u)
        {
            return "a field is digits with at most one decimal point";
        }
        if (out.count == out.text.size())
        {
            return "an angle has at most three fields";
        }
        out.text[out.count++] = text.substr(start, index - start);

        const std::size_t separatorStart = index;
        while (index < text.size())
        {
            const std::size_t width = separatorAt(text, index);
            if (width == 0u)
            {
                break;
            }
            index += width;
        }
        if (index == text.size())
        {
            break;
        }
        if (index == separatorStart)
        {
            return "a character that is neither a digit, a decimal point nor a field separator";
        }
    }
    if (out.count < 2u)
    {
        return "an angle is one decimal number or two or three fields";
    }
    for (std::size_t field = 0u; field + 1u < out.count; ++field)
    {
        if (out.text[field].find('.') != std::string_view::npos)
        {
            return "only the last field may carry a fraction";
        }
    }
    return nullptr;
}

/// Reads a written angle on one axis.
///
/// @param text the written angle.
/// @param axis which axis it is for.
/// @param operation the public operation reading it.
/// @return the angle in degrees, or the failure the public operation documents.
[[nodiscard]] Result<double> readAngle(const std::string_view text, const Axis axis, const char* const operation)
{
    const AngleText normalized(text);
    const std::string_view view = normalized.view();
    if (view.empty())
    {
        return angleFailure(ErrorCode::InvalidArgument, operation, "the text holds no angle");
    }

    if (isDecimalNumber(view))
    {
        double degrees = 0.0;
        if (!readDecimal(view, degrees) || !std::isfinite(degrees))
        {
            return angleFailure(ErrorCode::OutOfRange, operation, "the decimal angle is not a finite number");
        }
        if (axis == Axis::RightAscension)
        {
            if (degrees < 0.0 || degrees > 360.0)
            {
                return angleFailure(ErrorCode::OutOfRange, operation,
                                    "a decimal right ascension is in degrees from 0 to 360");
            }
            // Adding zero turns a negative zero into the positive one the half-open turn starts at.
            return normalizeRightAscensionDegrees(degrees) + 0.0;
        }
        if (degrees < -90.0 || degrees > 90.0)
        {
            return angleFailure(ErrorCode::OutOfRange, operation, "a declination lies between the poles");
        }
        return degrees;
    }

    Fields fields;
    if (const char* const reason = splitFields(view, fields); reason != nullptr)
    {
        return angleFailure(ErrorCode::InvalidArgument, operation, reason);
    }
    std::array<double, 3> values{0.0, 0.0, 0.0};
    for (std::size_t field = 0u; field < fields.count; ++field)
    {
        if (!readDecimal(fields.text[field], values[field]))
        {
            return angleFailure(ErrorCode::OutOfRange, operation, "a field is too large to read");
        }
    }
    const double whole = values[0];
    const double minutes = values[1];
    const double seconds = values[2];
    if (minutes >= 60.0 || seconds >= 60.0)
    {
        return angleFailure(ErrorCode::OutOfRange, operation, "minutes and seconds are below sixty");
    }
    if (axis == Axis::RightAscension && fields.negative)
    {
        return angleFailure(ErrorCode::OutOfRange, operation, "a right ascension is never negative");
    }
    const double maximum = axis == Axis::RightAscension ? 24.0 : 90.0;
    if (whole > maximum || (whole == maximum && (minutes > 0.0 || seconds > 0.0)))
    {
        return angleFailure(ErrorCode::OutOfRange, operation,
                            axis == Axis::RightAscension ? "a right ascension is at most twenty-four hours"
                                                         : "a declination lies between the poles");
    }

    // The product's order of operations, so the same text reads as the same double.
    double degrees = whole + minutes / 60.0 + seconds / 3600.0;
    if (axis == Axis::RightAscension)
    {
        degrees *= 15.0;
        return normalizeRightAscensionDegrees(degrees);
    }
    return fields.negative ? -degrees : degrees;
}

/// Splits an angle's magnitude, in hours or degrees, into fields at a stated precision.
///
/// @param units the magnitude, not negative.
/// @param fractionDigits how many digits the seconds keep, already checked.
/// @return the split, not yet signed and not yet wrapped to a day.
[[nodiscard]] SexagesimalAngle splitUnits(const double units, const std::uint32_t fractionDigits) noexcept
{
    const std::int64_t increment = kPowersOfTen[fractionDigits];
    const std::int64_t perMinute = 60 * increment;
    const std::int64_t perWhole = 3600 * increment;
    const std::int64_t count = std::llround(units * (3600.0 * static_cast<double>(increment)));
    const std::int64_t remainder = count % perWhole;
    return SexagesimalAngle{.negative = false,
                            .whole = static_cast<std::uint32_t>(count / perWhole),
                            .minutes = static_cast<std::uint32_t>(remainder / perMinute),
                            .seconds = static_cast<std::uint32_t>((remainder % perMinute) / increment),
                            .fraction = static_cast<std::uint32_t>(remainder % increment),
                            .fractionDigits = fractionDigits};
}

/// Splits a right ascension, or says why it cannot be split.
///
/// @param degrees the angle.
/// @param fractionDigits how many digits the seconds keep.
/// @param operation the public operation splitting it.
/// @return the split, or the failure the public operation documents.
[[nodiscard]] Result<SexagesimalAngle> splitRightAscensionFor(const double degrees, const std::uint32_t fractionDigits,
                                                              const char* const operation)
{
    if (!std::isfinite(degrees))
    {
        return angleFailure(ErrorCode::InvalidArgument, operation, "the angle is not finite");
    }
    if (fractionDigits > kMaxSexagesimalFractionDigits)
    {
        return angleFailure(ErrorCode::InvalidArgument, operation, "the seconds keep at most six digits");
    }
    SexagesimalAngle angle = splitUnits(normalizeRightAscensionDegrees(degrees) / 15.0, fractionDigits);
    angle.whole %= 24u;
    return angle;
}

/// Splits a declination, or says why it cannot be split.
///
/// @param degrees the angle.
/// @param fractionDigits how many digits the seconds keep.
/// @param operation the public operation splitting it.
/// @return the split, or the failure the public operation documents.
[[nodiscard]] Result<SexagesimalAngle> splitDeclinationFor(const double degrees, const std::uint32_t fractionDigits,
                                                           const char* const operation)
{
    if (!std::isfinite(degrees))
    {
        return angleFailure(ErrorCode::InvalidArgument, operation, "the angle is not finite");
    }
    if (fractionDigits > kMaxSexagesimalFractionDigits)
    {
        return angleFailure(ErrorCode::InvalidArgument, operation, "the seconds keep at most six digits");
    }
    if (degrees < -90.0 || degrees > 90.0)
    {
        return angleFailure(ErrorCode::OutOfRange, operation, "a declination lies between the poles");
    }
    SexagesimalAngle angle = splitUnits(std::abs(degrees), fractionDigits);
    angle.negative =
        degrees < 0.0 && (angle.whole != 0u || angle.minutes != 0u || angle.seconds != 0u || angle.fraction != 0u);
    return angle;
}

/// Appends a number as exactly `width` decimal digits, zero-padded.
///
/// @param text the text to append to.
/// @param value the number, which fits the width.
/// @param width how many digits to write.
void appendDigits(std::string& text, std::uint32_t value, const std::uint32_t width)
{
    std::array<char, 10> digits{};
    for (std::uint32_t index = width; index > 0u; --index)
    {
        digits[index - 1u] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    }
    text.append(digits.data(), width);
}

/// Writes a split's fields with a separator between them.
///
/// @param text the text to append to.
/// @param angle the split.
/// @param separator what stands between the fields.
void appendFields(std::string& text, const SexagesimalAngle& angle, const SexagesimalSeparator separator)
{
    const char between = separator == SexagesimalSeparator::Colon ? ':' : ' ';
    appendDigits(text, angle.whole, 2u);
    text.push_back(between);
    appendDigits(text, angle.minutes, 2u);
    text.push_back(between);
    appendDigits(text, angle.seconds, 2u);
    if (angle.fractionDigits > 0u)
    {
        text.push_back('.');
        appendDigits(text, angle.fraction, angle.fractionDigits);
    }
}

} // namespace

const char* sexagesimalSeparatorName(const SexagesimalSeparator separator) noexcept
{
    switch (separator)
    {
    case SexagesimalSeparator::Space:
        return "Space";
    case SexagesimalSeparator::Colon:
        return "Colon";
    }
    return "UnknownSexagesimalSeparator";
}

Result<double> parseRightAscensionDegrees(const std::string_view text)
{
    return readAngle(text, Axis::RightAscension, kParseRightAscension);
}

Result<double> parseDeclinationDegrees(const std::string_view text)
{
    return readAngle(text, Axis::Declination, kParseDeclination);
}

Result<SkyCoordinate> parseSkyCoordinate(const std::string_view rightAscension, const std::string_view declination)
{
    const Result<double> ra = readAngle(rightAscension, Axis::RightAscension, kParseCoordinate);
    if (!ra.hasValue())
    {
        return Status::failure(
            ra.code(), StatusDetail{.message = ra.status().message(),
                                    .operation = kParseCoordinate,
                                    .domain = ErrorDomain::Algorithms,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "rightAscension")}});
    }
    const Result<double> dec = readAngle(declination, Axis::Declination, kParseCoordinate);
    if (!dec.hasValue())
    {
        return Status::failure(
            dec.code(), StatusDetail{.message = dec.status().message(),
                                     .operation = kParseCoordinate,
                                     .domain = ErrorDomain::Algorithms,
                                     .context = {StatusContextField::ofText(ContextKey::Parameter, "declination")}});
    }
    return SkyCoordinate{.rightAscensionDegrees = *ra, .declinationDegrees = *dec};
}

Result<SexagesimalAngle> splitRightAscension(const double degrees, const std::uint32_t fractionDigits)
{
    return splitRightAscensionFor(degrees, fractionDigits, kSplitRightAscension);
}

Result<SexagesimalAngle> splitDeclination(const double degrees, const std::uint32_t fractionDigits)
{
    return splitDeclinationFor(degrees, fractionDigits, kSplitDeclination);
}

Result<std::string> formatRightAscension(const double degrees, const std::uint32_t fractionDigits,
                                         const SexagesimalSeparator separator)
{
    if (!isSexagesimalSeparator(separator))
    {
        return angleFailure(ErrorCode::InvalidArgument, kFormatRightAscension,
                            "the separator is outside its enumeration");
    }
    const Result<SexagesimalAngle> angle = splitRightAscensionFor(degrees, fractionDigits, kFormatRightAscension);
    if (!angle.hasValue())
    {
        return angle.status();
    }
    std::string text;
    text.reserve(16u);
    appendFields(text, *angle, separator);
    return text;
}

Result<std::string> formatDeclination(const double degrees, const std::uint32_t fractionDigits,
                                      const SexagesimalSeparator separator)
{
    if (!isSexagesimalSeparator(separator))
    {
        return angleFailure(ErrorCode::InvalidArgument, kFormatDeclination, "the separator is outside its enumeration");
    }
    const Result<SexagesimalAngle> angle = splitDeclinationFor(degrees, fractionDigits, kFormatDeclination);
    if (!angle.hasValue())
    {
        return angle.status();
    }
    std::string text;
    text.reserve(17u);
    text.push_back(angle->negative ? '-' : '+');
    appendFields(text, *angle, separator);
    return text;
}

} // namespace an
