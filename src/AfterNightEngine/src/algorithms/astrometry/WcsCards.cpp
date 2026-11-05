// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading and writing the world-coordinate cards of a header.
//
// The vocabulary is the FITS world-coordinate convention for celestial axes (Calabretta and
// Greisen, 2002) with the Simple Imaging Polynomial distortion keywords (Shupe et al., 2005),
// restricted to what `WcsSolution` can hold: a tangent-plane projection of ICRS right ascension
// and declination, in degrees, in that axis order. Reading refuses anything else by name rather
// than approximating it, and the one conversion between the convention's one-based pixel centres
// and the engine's half-pixel centres happens here in both directions.

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include "image/SolutionCards.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kParseOperation = "an::parseWcsCards";
constexpr const char* kBuildOperation = "an::buildWcsCards";
constexpr const char* kWithoutOperation = "an::withoutWcsCards";
constexpr const char* kWithOperation = "an::withWcsCards";

/// The offset between the convention's pixel numbering and this engine's.
///
/// The convention puts the centre of the first pixel at exactly one; this engine puts it at one
/// half. Every reference pixel that crosses this bridge moves by this much, in one direction on the
/// way in and the other on the way out.
constexpr double kPixelOriginOffset = 0.5;

constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

/// The keywords this bridge owns exactly, beyond the indexed families below.
constexpr std::array<const char*, 17> kExactKeywords = {
    "EQUINOX",  "RADESYS",  "RADECSYS", "LONPOLE", "LATPOLE", "WCSAXES", "WCSNAME", "A_ORDER", "B_ORDER",
    "AP_ORDER", "BP_ORDER", "A_DMAX",   "B_DMAX",  "AP_DMAX", "BP_DMAX", "CROTA2",  "EPOCH"};

/// The keyword families this bridge owns by prefix, where the rest of the keyword is an axis number.
constexpr std::array<const char*, 8> kKeywordPrefixes = {"CTYPE", "CRVAL", "CRPIX", "CD1_",
                                                         "CD2_",  "CDELT", "CUNIT", "CROTA"};

/// The keyword families whose prefix is two letters followed immediately by a digit.
///
/// `PCi_j`, `PVi_ma` and `PSi_ma`. The prefix alone is not enough: `PCOUNT` is a mandatory keyword of
/// every FITS extension and has nothing to do with a solution, and a match on `PC` alone would
/// remove it along with the world-coordinate cards.
constexpr std::array<const char*, 3> kIndexedKeywordPrefixes = {"PC", "PV", "PS"};

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] Status cardFailure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a character is header whitespace.
///
/// @param character the character to test.
/// @return true for a space or a tab.
[[nodiscard]] constexpr bool isBlank(const char character) noexcept
{
    return character == ' ' || character == '\t';
}

/// The value of a card without its surrounding blanks and, for a text value, its quotes.
///
/// @param value the card's value exactly as the file carried it.
/// @return the value a reader means.
[[nodiscard]] std::string_view unquoted(std::string_view value) noexcept
{
    while (!value.empty() && isBlank(value.front()))
    {
        value.remove_prefix(1u);
    }
    while (!value.empty() && isBlank(value.back()))
    {
        value.remove_suffix(1u);
    }
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
    {
        value.remove_prefix(1u);
        value.remove_suffix(1u);
        while (!value.empty() && isBlank(value.back()))
        {
            value.remove_suffix(1u);
        }
    }
    return value;
}

/// Reads a header value as a number.
///
/// Accepts the Fortran exponent letter `D`, in either case, that the convention allows and a
/// leading plus sign, neither of which the standard library's locale-free conversion takes, and
/// drops a trailing decimal point. Nothing else is repaired: a value with trailing text after its
/// number is a malformed value, not a number with a comment attached, because a header's comment
/// lives in its own field. A number that is not finite is refused.
///
/// @param text the already-unquoted value.
/// @param out the number.
/// @return false when the text is not exactly one number.
[[nodiscard]] bool readNumber(const std::string_view text, double& out)
{
    if (text.empty())
    {
        return false;
    }

    std::string normalized;
    normalized.reserve(text.size());
    for (const char character : text)
    {
        if (character == 'D' || character == 'd')
        {
            normalized.push_back('E');
            continue;
        }
        normalized.push_back(character);
    }

    std::string_view digits = normalized;
    if (digits.front() == '+')
    {
        digits.remove_prefix(1u);
    }
    if (digits.size() > 1u && digits.back() == '.')
    {
        digits.remove_suffix(1u);
    }

    const char* const first = digits.data();
    const char* const last = first + digits.size();
    const std::from_chars_result parsed = std::from_chars(first, last, out);
    return parsed.ec == std::errc{} && parsed.ptr == last && std::isfinite(out);
}

/// Reads a header value as a non-negative whole number.
///
/// @param text the already-unquoted value.
/// @param out the number.
/// @return false when the text is not exactly one non-negative integer.
[[nodiscard]] bool readInteger(const std::string_view text, std::uint32_t& out)
{
    if (text.empty())
    {
        return false;
    }
    std::string_view digits = text;
    if (digits.front() == '+')
    {
        digits.remove_prefix(1u);
    }
    const char* const first = digits.data();
    const char* const last = first + digits.size();
    const std::from_chars_result parsed = std::from_chars(first, last, out, 10);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

/// A number written so that reading it back produces the same number.
///
/// @param value the number to write.
/// @return the shortest decimal that round-trips.
[[nodiscard]] std::string shortestDecimal(const double value)
{
    // Twenty-four characters is more than the longest shortest-round-trip form of any binary64
    // value, so the conversion cannot run out of room and there is no second path to test.
    std::array<char, 24> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

/// Whether a keyword names one coefficient of a distortion polynomial, and which.
///
/// @param keyword the keyword to classify.
/// @param component which of the four polynomials it belongs to, as an index into the request's
///        components: zero for `A`, one for `B`, two for `AP`, three for `BP`.
/// @param i the horizontal degree.
/// @param j the vertical degree.
/// @return true when the keyword is a coefficient of one polynomial.
[[nodiscard]] bool readCoefficientKeyword(const std::string_view keyword, std::size_t& component, std::uint32_t& i,
                                          std::uint32_t& j)
{
    constexpr std::array<const char*, 4> kPrefixes = {"A_", "B_", "AP_", "BP_"};
    for (std::size_t candidate = 0u; candidate < kPrefixes.size(); ++candidate)
    {
        const std::string_view prefix = kPrefixes[candidate];
        if (!keyword.starts_with(prefix))
        {
            continue;
        }
        const std::string_view indices = keyword.substr(prefix.size());
        const std::size_t separator = indices.find('_');
        if (separator == std::string_view::npos)
        {
            continue;
        }
        std::uint32_t first = 0u;
        std::uint32_t second = 0u;
        if (!readInteger(indices.substr(0u, separator), first) || !readInteger(indices.substr(separator + 1u), second))
        {
            continue;
        }
        component = candidate;
        i = first;
        j = second;
        return true;
    }
    return false;
}

/// Whether a keyword is one of those a solution is actually built from.
///
/// Narrower than `isWcsKeyword`, which owns everything a solution's removal should take with it. A
/// header carrying only an equinox or a coordinate frame name has no solution in it, and saying so
/// is different from saying its solution is broken.
///
/// @param keyword the keyword to classify.
/// @return true when the keyword contributes to the reference point, the reference pixel or the
///         linear matrix.
[[nodiscard]] bool namesSolution(const std::string_view keyword) noexcept
{
    constexpr std::array<const char*, 4> kPrefixes = {"CTYPE", "CRVAL", "CRPIX", "CDELT"};
    for (const char* const prefix : kPrefixes)
    {
        if (keyword.starts_with(prefix))
        {
            return true;
        }
    }
    if (keyword.starts_with("PC") && keyword.size() > 2u && keyword[2] >= '0' && keyword[2] <= '9')
    {
        return true;
    }
    return keyword.starts_with("CD1_") || keyword.starts_with("CD2_");
}

/// One card's value, or the reason it could not be looked up.
struct CardLookup
{
    /// Whether a card with that keyword was found.
    bool present = false;

    /// Whether two cards carried that keyword with different values.
    bool duplicated = false;

    /// The found card's value, without its blanks or quotes.
    std::string_view value{};
};

/// Finds the one card with a keyword.
///
/// @param cards the preserved header cards.
/// @param keyword the keyword to find.
/// @return whether it is there, whether it is there twice with two values, and its value.
[[nodiscard]] CardLookup lookup(const std::span<const CodecCard> cards, const std::string_view keyword)
{
    CardLookup found;
    for (const CodecCard& card : cards)
    {
        if (card.origin() != CodecNamespace::Fits || card.keyword() != keyword)
        {
            continue;
        }
        const std::string_view value = unquoted(card.value());
        if (!found.present)
        {
            found.present = true;
            found.value = value;
            continue;
        }
        if (found.value != value)
        {
            found.duplicated = true;
            return found;
        }
    }
    return found;
}

/// What reading a header's cards produced.
struct CardReading
{
    /// Why the cards were refused, or `WcsCardError::None`.
    WcsCardError error = WcsCardError::None;

    /// The keyword the refusal is about, empty when no single card is responsible.
    std::string keyword{};

    /// The solution's fields, with the frame size left for the caller to supply.
    WcsSolutionRequest request{};
};

/// Builds a refusal that names one card.
///
/// @param error why the cards were refused.
/// @param keyword the card responsible, or empty.
/// @return the refusal.
[[nodiscard]] CardReading refuse(const WcsCardError error, std::string keyword)
{
    CardReading reading;
    reading.error = error;
    reading.keyword = std::move(keyword);
    return reading;
}

/// What one axis's projection code said.
struct AxisType
{
    /// The celestial coordinate the axis carries - `RA`, `DEC`, or whatever the header named.
    std::string_view coordinate{};

    /// The three-letter projection algorithm code.
    std::string_view algorithm{};

    /// Whether the code carried the distortion suffix.
    bool distorted = false;
};

/// Splits one projection code into its parts.
///
/// @param value the already-unquoted value of a `CTYPE` card.
/// @param out the parts.
/// @return false when the value is not a projection code at all.
[[nodiscard]] bool readAxisType(std::string_view value, AxisType& out)
{
    out = AxisType{};
    if (value.ends_with("-SIP"))
    {
        out.distorted = true;
        value.remove_suffix(4u);
    }
    if (value.size() < 5u)
    {
        return false;
    }

    out.algorithm = value.substr(value.size() - 3u);
    std::string_view coordinate = value.substr(0u, value.size() - 3u);
    while (!coordinate.empty() && coordinate.back() == '-')
    {
        coordinate.remove_suffix(1u);
    }
    if (coordinate.empty())
    {
        return false;
    }
    out.coordinate = coordinate;
    return true;
}

/// Reads the two projection codes and what they agree on.
///
/// @param cards the preserved header cards.
/// @param projection the projection both axes name.
/// @param distorted whether both axes carry the distortion suffix.
/// @return `WcsCardError::None`, or the reason with its keyword.
[[nodiscard]] CardReading readProjection(const std::span<const CodecCard> cards, WcsProjection& projection,
                                         bool& distorted)
{
    const CardLookup first = lookup(cards, "CTYPE1");
    const CardLookup second = lookup(cards, "CTYPE2");
    if (first.duplicated)
    {
        return refuse(WcsCardError::DuplicateKeyword, "CTYPE1");
    }
    if (second.duplicated)
    {
        return refuse(WcsCardError::DuplicateKeyword, "CTYPE2");
    }
    if (!first.present)
    {
        return refuse(WcsCardError::MissingKeyword, "CTYPE1");
    }
    if (!second.present)
    {
        return refuse(WcsCardError::MissingKeyword, "CTYPE2");
    }

    AxisType axis1{};
    AxisType axis2{};
    if (!readAxisType(first.value, axis1))
    {
        return refuse(WcsCardError::MalformedValue, "CTYPE1");
    }
    if (!readAxisType(second.value, axis2))
    {
        return refuse(WcsCardError::MalformedValue, "CTYPE2");
    }
    if (axis1.algorithm != axis2.algorithm || axis1.distorted != axis2.distorted)
    {
        return refuse(WcsCardError::InconsistentAxes, "CTYPE1");
    }
    if (axis1.coordinate == "DEC" && axis2.coordinate == "RA")
    {
        return refuse(WcsCardError::UnsupportedAxisOrder, "CTYPE1");
    }
    if (axis1.coordinate != "RA" || axis2.coordinate != "DEC")
    {
        return refuse(WcsCardError::UnsupportedFrame, "CTYPE1");
    }
    if (axis1.algorithm != "TAN")
    {
        return refuse(WcsCardError::UnsupportedProjection, "CTYPE1");
    }

    projection = WcsProjection::Gnomonic;
    distorted = axis1.distorted;
    return CardReading{};
}

/// Reads one axis's stated unit.
///
/// @param cards the preserved header cards.
/// @param keyword the `CUNIT` keyword to read.
/// @return `WcsCardError::None` when the unit is absent or is degrees.
[[nodiscard]] CardReading readUnit(const std::span<const CodecCard> cards, const char* const keyword)
{
    const CardLookup unit = lookup(cards, keyword);
    if (unit.duplicated)
    {
        return refuse(WcsCardError::DuplicateKeyword, keyword);
    }
    if (!unit.present || unit.value.empty())
    {
        return CardReading{};
    }
    if (unit.value != "deg")
    {
        return refuse(WcsCardError::UnsupportedUnit, keyword);
    }
    return CardReading{};
}

/// Reads one required numeric card.
///
/// @param cards the preserved header cards.
/// @param keyword the keyword to read.
/// @param out the number.
/// @return `WcsCardError::None` when the card is there and is one number.
[[nodiscard]] CardReading readRequiredNumber(const std::span<const CodecCard> cards, const char* const keyword,
                                             double& out)
{
    const CardLookup card = lookup(cards, keyword);
    if (card.duplicated)
    {
        return refuse(WcsCardError::DuplicateKeyword, keyword);
    }
    if (!card.present)
    {
        return refuse(WcsCardError::MissingKeyword, keyword);
    }
    if (!readNumber(card.value, out))
    {
        return refuse(WcsCardError::MalformedValue, keyword);
    }
    return CardReading{};
}

/// Reads one optional numeric card.
///
/// @param cards the preserved header cards.
/// @param keyword the keyword to read.
/// @param fallback what the convention means when the card is absent.
/// @param out the number.
/// @param found whether the card was there.
/// @return `WcsCardError::None` when the card is absent or is one number.
[[nodiscard]] CardReading readOptionalNumber(const std::span<const CodecCard> cards, const char* const keyword,
                                             const double fallback, double& out, bool& found)
{
    const CardLookup card = lookup(cards, keyword);
    if (card.duplicated)
    {
        return refuse(WcsCardError::DuplicateKeyword, keyword);
    }
    found = card.present;
    if (!card.present)
    {
        out = fallback;
        return CardReading{};
    }
    if (!readNumber(card.value, out))
    {
        return refuse(WcsCardError::MalformedValue, keyword);
    }
    return CardReading{};
}

/// Reads the linear matrix from whichever of the three spellings the header uses.
///
/// @param cards the preserved header cards.
/// @param linear the matrix.
/// @return `WcsCardError::None`, or the reason with its keyword.
[[nodiscard]] CardReading readLinearTransform(const std::span<const CodecCard> cards, WcsLinearTransform& linear)
{
    constexpr std::array<const char*, 4> kCdKeywords = {"CD1_1", "CD1_2", "CD2_1", "CD2_2"};
    std::array<double, 4> cd{};
    bool anyCd = false;
    for (std::size_t index = 0u; index < kCdKeywords.size(); ++index)
    {
        bool found = false;
        if (const CardReading failed = readOptionalNumber(cards, kCdKeywords[index], 0.0, cd[index], found);
            failed.error != WcsCardError::None)
        {
            return failed;
        }
        anyCd = anyCd || found;
    }
    if (anyCd)
    {
        linear = WcsLinearTransform{.cd11 = cd[0], .cd12 = cd[1], .cd21 = cd[2], .cd22 = cd[3]};
        return CardReading{};
    }

    double cdelt1 = 1.0;
    double cdelt2 = 1.0;
    bool foundCdelt1 = false;
    bool foundCdelt2 = false;
    if (const CardReading failed = readOptionalNumber(cards, "CDELT1", 1.0, cdelt1, foundCdelt1);
        failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (const CardReading failed = readOptionalNumber(cards, "CDELT2", 1.0, cdelt2, foundCdelt2);
        failed.error != WcsCardError::None)
    {
        return failed;
    }

    constexpr std::array<const char*, 4> kPcKeywords = {"PC1_1", "PC1_2", "PC2_1", "PC2_2"};
    constexpr std::array<double, 4> kPcDefaults = {1.0, 0.0, 0.0, 1.0};
    std::array<double, 4> pc = kPcDefaults;
    bool anyPc = false;
    for (std::size_t index = 0u; index < kPcKeywords.size(); ++index)
    {
        bool found = false;
        if (const CardReading failed =
                readOptionalNumber(cards, kPcKeywords[index], kPcDefaults[index], pc[index], found);
            failed.error != WcsCardError::None)
        {
            return failed;
        }
        anyPc = anyPc || found;
    }
    if (anyPc)
    {
        linear = WcsLinearTransform{
            .cd11 = cdelt1 * pc[0], .cd12 = cdelt1 * pc[1], .cd21 = cdelt2 * pc[2], .cd22 = cdelt2 * pc[3]};
        return CardReading{};
    }

    if (!foundCdelt1 && !foundCdelt2)
    {
        return refuse(WcsCardError::MissingKeyword, "CD1_1");
    }

    double rotation = 0.0;
    bool foundRotation = false;
    if (const CardReading failed = readOptionalNumber(cards, "CROTA2", 0.0, rotation, foundRotation);
        failed.error != WcsCardError::None)
    {
        return failed;
    }
    const double cosine = std::cos(rotation * kRadiansPerDegree);
    const double sine = std::sin(rotation * kRadiansPerDegree);
    linear = WcsLinearTransform{
        .cd11 = cdelt1 * cosine, .cd12 = -cdelt2 * sine, .cd21 = cdelt1 * sine, .cd22 = cdelt2 * cosine};
    return CardReading{};
}

/// Reads one polynomial component's order and coefficients.
///
/// @param cards the preserved header cards.
/// @param prefix the component's keyword prefix - `A`, `B`, `AP` or `BP`.
/// @param component the polynomial to fill.
/// @param present whether the component's order keyword was there.
/// @return `WcsCardError::None`, or the reason with its keyword.
[[nodiscard]] CardReading readPolynomial(const std::span<const CodecCard> cards, const std::string_view prefix,
                                         SipPolynomial& component, bool& present)
{
    component = SipPolynomial{};
    const std::string orderKeyword = std::string(prefix) + "_ORDER";
    const CardLookup order = lookup(cards, orderKeyword);
    if (order.duplicated)
    {
        return refuse(WcsCardError::DuplicateKeyword, orderKeyword);
    }
    present = order.present;
    if (!order.present)
    {
        return CardReading{};
    }

    std::uint32_t value = 0u;
    if (!readInteger(order.value, value))
    {
        return refuse(WcsCardError::MalformedValue, orderKeyword);
    }
    if (value < kMinimumSipOrder || value > kMaximumSipOrder)
    {
        return refuse(WcsCardError::ValueOutOfRange, orderKeyword);
    }
    component.order = value;

    for (std::uint32_t i = 0u; i <= value; ++i)
    {
        for (std::uint32_t j = 0u; j <= value - i; ++j)
        {
            const std::string keyword = std::string(prefix) + "_" + std::to_string(i) + "_" + std::to_string(j);
            const CardLookup coefficient = lookup(cards, keyword);
            if (coefficient.duplicated)
            {
                return refuse(WcsCardError::DuplicateKeyword, keyword);
            }
            if (!coefficient.present)
            {
                continue;
            }
            double parsed = 0.0;
            if (!readNumber(coefficient.value, parsed))
            {
                return refuse(WcsCardError::MalformedValue, keyword);
            }
            component.coefficients[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = parsed;
        }
    }
    return CardReading{};
}

/// Refuses a coefficient card of a degree its own component's order excludes.
///
/// Evaluating the polynomial would drop such a card without saying so, which makes a header that
/// says one thing behave as if it said another.
///
/// @param cards the preserved header cards.
/// @param orders the four components' orders, absent for a component the header does not carry.
/// @return `WcsCardError::None`, or the reason with the offending keyword.
[[nodiscard]] CardReading checkCoefficientDegrees(const std::span<const CodecCard> cards,
                                                  const std::array<std::uint32_t, 4>& orders)
{
    for (const CodecCard& card : cards)
    {
        if (card.origin() != CodecNamespace::Fits)
        {
            continue;
        }
        std::size_t component = 0u;
        std::uint32_t i = 0u;
        std::uint32_t j = 0u;
        if (!readCoefficientKeyword(card.keyword(), component, i, j))
        {
            continue;
        }
        double value = 0.0;
        if (!readNumber(unquoted(card.value()), value))
        {
            return refuse(WcsCardError::MalformedValue, card.keyword());
        }
        if (value == 0.0)
        {
            continue;
        }
        if (i + j > orders[component] || i >= kSipCoefficientRowCount || j >= kSipCoefficientRowCount)
        {
            return refuse(WcsCardError::ValueOutOfRange, card.keyword());
        }
    }
    return CardReading{};
}

/// Reads every world-coordinate card of a header into the fields of a solution.
///
/// The checks run in a fixed order, and the first that fails is the answer: whether any card
/// names a solution, the projection codes, the units, `CRPIX` and `CRVAL`, the frame and pole
/// keywords, the linear matrix and its determinant, each polynomial's order and coefficients, the
/// pairing of `A` with `B` and `AP` with `BP`, the agreement of the distortion suffix with the
/// polynomials, and the degree of every coefficient card.
///
/// @param cards the preserved header cards.
/// @return the fields, or the first reason the cards were refused.
[[nodiscard]] CardReading readCards(const std::span<const CodecCard> cards)
{
    bool anyCore = false;
    for (const CodecCard& card : cards)
    {
        if (card.origin() == CodecNamespace::Fits && namesSolution(card.keyword()))
        {
            anyCore = true;
            break;
        }
    }
    if (!anyCore)
    {
        return refuse(WcsCardError::NoSolution, std::string{});
    }

    CardReading reading;
    bool distorted = false;
    if (const CardReading failed = readProjection(cards, reading.request.projection, distorted);
        failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (const CardReading failed = readUnit(cards, "CUNIT1"); failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (const CardReading failed = readUnit(cards, "CUNIT2"); failed.error != WcsCardError::None)
    {
        return failed;
    }

    double crpix1 = 0.0;
    double crpix2 = 0.0;
    double crval1 = 0.0;
    double crval2 = 0.0;
    if (const CardReading failed = readRequiredNumber(cards, "CRPIX1", crpix1); failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (const CardReading failed = readRequiredNumber(cards, "CRPIX2", crpix2); failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (const CardReading failed = readRequiredNumber(cards, "CRVAL1", crval1); failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (const CardReading failed = readRequiredNumber(cards, "CRVAL2", crval2); failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (crval2 < -90.0 || crval2 > 90.0)
    {
        return refuse(WcsCardError::ValueOutOfRange, "CRVAL2");
    }

    double rightAscension = std::fmod(crval1, 360.0);
    if (rightAscension < 0.0)
    {
        rightAscension += 360.0;
    }
    if (rightAscension >= 360.0)
    {
        rightAscension = 0.0;
    }

    reading.request.referencePixel =
        PixelCoordinate{.x = crpix1 - kPixelOriginOffset, .y = crpix2 - kPixelOriginOffset};
    reading.request.referencePoint =
        SkyCoordinate{.rightAscensionDegrees = rightAscension, .declinationDegrees = crval2};

    // The model's directions are ICRS. RA/DEC axis names alone do not establish a
    // reference frame: an equinox without RADESYS defaults to FK4 or FK5, not ICRS.
    bool explicitIcrs = false;
    for (const auto* keyword : {"RADESYS", "RADECSYS"})
    {
        const auto frame = lookup(cards, keyword);
        if (frame.duplicated)
        {
            return refuse(WcsCardError::DuplicateKeyword, keyword);
        }
        if (frame.present)
        {
            if (frame.value != "ICRS")
            {
                return refuse(WcsCardError::UnsupportedFrame, keyword);
            }
            explicitIcrs = true;
        }
    }
    // An equinox or epoch is accepted only beside an explicit ICRS. LONPOLE is accepted only when
    // it states the convention's default for a zenithal projection - 180 degrees, or 0 when the
    // reference point is the north pole - because any other value turns the sky about the reference
    // point, which the model cannot represent. LATPOLE has to be a number and is otherwise ignored:
    // a zenithal projection's native pole is its reference point, so the celestial pole is fixed
    // without it, and astrometry.net writes it as 0 in every solution.
    for (const auto* keyword : {"EQUINOX", "EPOCH", "LONPOLE", "LATPOLE"})
    {
        const auto found = lookup(cards, keyword);
        if (found.duplicated)
        {
            return refuse(WcsCardError::DuplicateKeyword, keyword);
        }
        if (!found.present)
        {
            continue;
        }
        double value = 0.0;
        if (!readNumber(found.value, value))
        {
            return refuse(WcsCardError::MalformedValue, keyword);
        }
        const std::string_view name(keyword);
        if (name == "EQUINOX" || name == "EPOCH")
        {
            if (value < 0.0)
            {
                return refuse(WcsCardError::ValueOutOfRange, keyword);
            }
            if (!explicitIcrs)
            {
                return refuse(WcsCardError::UnsupportedFrame, keyword);
            }
        }
        else if (name == "LONPOLE" && std::remainder(value - (crval2 == 90.0 ? 0.0 : 180.0), 360.0) != 0.0)
        {
            return refuse(WcsCardError::UnsupportedFrame, keyword);
        }
    }

    if (const CardReading failed = readLinearTransform(cards, reading.request.linear);
        failed.error != WcsCardError::None)
    {
        return failed;
    }
    if (!std::isfinite(reading.request.linear.determinant()) || reading.request.linear.determinant() == 0.0)
    {
        return refuse(WcsCardError::SingularTransform, "CD1_1");
    }

    SipDistortionRequest distortion;
    distortion.referencePixel = reading.request.referencePixel;
    std::array<SipPolynomial*, 4> components = {&distortion.forwardX, &distortion.forwardY, &distortion.inverseX,
                                                &distortion.inverseY};
    constexpr std::array<const char*, 4> kPrefixes = {"A", "B", "AP", "BP"};
    std::array<bool, 4> found{};
    for (std::size_t index = 0u; index < components.size(); ++index)
    {
        if (const CardReading failed = readPolynomial(cards, kPrefixes[index], *components[index], found[index]);
            failed.error != WcsCardError::None)
        {
            return failed;
        }
    }

    const bool hasForward = found[0] || found[1];
    const bool hasInverse = found[2] || found[3];
    if (hasForward && (!found[0] || !found[1]))
    {
        return refuse(WcsCardError::MissingKeyword, found[0] ? "B_ORDER" : "A_ORDER");
    }
    if (hasInverse && (!found[2] || !found[3]))
    {
        return refuse(WcsCardError::MissingKeyword, found[2] ? "BP_ORDER" : "AP_ORDER");
    }
    if (distorted != (hasForward || hasInverse))
    {
        return refuse(WcsCardError::InconsistentAxes, distorted ? "A_ORDER" : "CTYPE1");
    }

    const std::array<std::uint32_t, 4> orders = {components[0]->order, components[1]->order, components[2]->order,
                                                 components[3]->order};
    if (const CardReading failed = checkCoefficientDegrees(cards, orders); failed.error != WcsCardError::None)
    {
        return failed;
    }

    Result<SipDistortion> model = SipDistortion::create(distortion);
    if (!model.hasValue())
    {
        return refuse(WcsCardError::ValueOutOfRange, "A_ORDER");
    }
    reading.request.distortion = *model;
    return reading;
}

/// The error code a card refusal is reported under.
///
/// @param error why the cards were refused.
/// @return the code a caller branches on.
[[nodiscard]] ErrorCode codeForCardError(const WcsCardError error) noexcept
{
    switch (error)
    {
    case WcsCardError::None:
        return ErrorCode::Ok;
    case WcsCardError::NoSolution:
        return ErrorCode::EmptySelection;
    case WcsCardError::MissingKeyword:
    case WcsCardError::MalformedValue:
    case WcsCardError::ValueOutOfRange:
        return ErrorCode::InvalidArgument;
    case WcsCardError::DuplicateKeyword:
    case WcsCardError::InconsistentAxes:
    case WcsCardError::SingularTransform:
        return ErrorCode::InvalidData;
    case WcsCardError::UnsupportedProjection:
    case WcsCardError::UnsupportedFrame:
    case WcsCardError::UnsupportedAxisOrder:
    case WcsCardError::UnsupportedUnit:
        return ErrorCode::Unsupported;
    }
    return ErrorCode::Internal;
}

/// Appends one card to a growing header.
///
/// @param cards the header being built.
/// @param keyword the keyword to write.
/// @param value the value to write.
/// @return success, or whatever `CodecCard::create` refused.
[[nodiscard]] Status appendCard(std::vector<CodecCard>& cards, std::string keyword, std::string value)
{
    Result<CodecCard> card =
        CodecCard::create(CodecNamespace::Fits, std::move(keyword), std::move(value), std::string{});
    if (!card.hasValue())
    {
        return card.status();
    }
    cards.push_back(std::move(*card));
    return Status::success();
}

/// Appends one polynomial component's order and every nonzero coefficient it carries.
///
/// @param cards the header being built.
/// @param prefix the component's keyword prefix.
/// @param component the polynomial to write.
/// @return success, or whatever `CodecCard::create` refused.
[[nodiscard]] Status appendPolynomial(std::vector<CodecCard>& cards, const std::string_view prefix,
                                      const SipPolynomial& component)
{
    if (component.order == 0u)
    {
        return Status::success();
    }
    if (const Status written = appendCard(cards, std::string(prefix) + "_ORDER", std::to_string(component.order));
        !written.ok())
    {
        return written;
    }
    for (std::uint32_t i = 0u; i <= component.order; ++i)
    {
        for (std::uint32_t j = 0u; j <= component.order - i; ++j)
        {
            const double value = component.coefficients[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
            if (value == 0.0)
            {
                continue;
            }
            if (const Status written =
                    appendCard(cards, std::string(prefix) + "_" + std::to_string(i) + "_" + std::to_string(j),
                               shortestDecimal(value));
                !written.ok())
            {
                return written;
            }
        }
    }
    return Status::success();
}

} // namespace

const char* wcsCardErrorName(const WcsCardError error) noexcept
{
    switch (error)
    {
    case WcsCardError::None:
        return "None";
    case WcsCardError::NoSolution:
        return "NoSolution";
    case WcsCardError::MissingKeyword:
        return "MissingKeyword";
    case WcsCardError::MalformedValue:
        return "MalformedValue";
    case WcsCardError::DuplicateKeyword:
        return "DuplicateKeyword";
    case WcsCardError::UnsupportedProjection:
        return "UnsupportedProjection";
    case WcsCardError::UnsupportedFrame:
        return "UnsupportedFrame";
    case WcsCardError::InconsistentAxes:
        return "InconsistentAxes";
    case WcsCardError::UnsupportedAxisOrder:
        return "UnsupportedAxisOrder";
    case WcsCardError::UnsupportedUnit:
        return "UnsupportedUnit";
    case WcsCardError::ValueOutOfRange:
        return "ValueOutOfRange";
    case WcsCardError::SingularTransform:
        return "SingularTransform";
    }
    return "UnknownWcsCardError";
}

bool isWcsKeyword(const std::string_view keyword) noexcept
{
    if (keyword.empty())
    {
        return false;
    }
    for (const char* const candidate : kExactKeywords)
    {
        if (keyword == candidate)
        {
            return true;
        }
    }
    for (const char* const prefix : kKeywordPrefixes)
    {
        if (keyword.starts_with(prefix))
        {
            return true;
        }
    }
    for (const char* const prefix : kIndexedKeywordPrefixes)
    {
        const std::string_view rest = keyword.starts_with(prefix) ? keyword.substr(2u) : std::string_view{};
        if (!rest.empty() && rest.front() >= '0' && rest.front() <= '9')
        {
            return true;
        }
    }
    std::size_t component = 0u;
    std::uint32_t i = 0u;
    std::uint32_t j = 0u;
    return readCoefficientKeyword(keyword, component, i, j);
}

WcsCardError checkWcsCards(const std::span<const CodecCard> cards)
{
    return readCards(cards).error;
}

Result<WcsSolution> parseWcsCards(const std::span<const CodecCard> cards, const std::uint32_t imageWidth,
                                  const std::uint32_t imageHeight)
{
    if (imageWidth == 0u || imageHeight == 0u)
    {
        return cardFailure(ErrorCode::InvalidArgument,
                           "a parsed solution describes a frame with a nonzero width and height", kParseOperation);
    }

    CardReading reading = readCards(cards);
    if (reading.error != WcsCardError::None)
    {
        std::string message =
            std::string("the header's world-coordinate cards were refused: ") + wcsCardErrorName(reading.error);
        if (!reading.keyword.empty())
        {
            message += " at ";
            message += reading.keyword;
        }
        Status failure = Status::failure(codeForCardError(reading.error),
                                         StatusDetail{.message = std::move(message),
                                                      .operation = kParseOperation,
                                                      .domain = ErrorDomain::Algorithms,
                                                      .context = {StatusContextField::ofText(
                                                          ContextKey::Parameter, wcsCardErrorName(reading.error))}});
        return failure;
    }

    reading.request.imageWidth = imageWidth;
    reading.request.imageHeight = imageHeight;
    return WcsSolution::create(reading.request);
}

Result<std::vector<CodecCard>> buildWcsCards(const WcsSolution& solution)
{
    const SipDistortion& distortion = solution.distortion();
    const bool distorted = distortion.hasForwardModel() || distortion.hasInverseModel();
    if (distorted && distortion.referencePixel() != solution.referencePixel())
    {
        return cardFailure(ErrorCode::IncompatibleArguments,
                           "FITS SIP coefficients must use the solution's reference pixel", kBuildOperation);
    }
    const std::string suffix = distorted ? "-SIP" : "";

    std::vector<CodecCard> cards;
    cards.reserve(16u);
    const std::array<std::pair<std::string, std::string>, 9> header = {
        std::pair<std::string, std::string>{"RADESYS", "'ICRS'"},
        std::pair<std::string, std::string>{"CTYPE1", "'RA---TAN" + suffix + "'"},
        std::pair<std::string, std::string>{"CTYPE2", "'DEC--TAN" + suffix + "'"},
        std::pair<std::string, std::string>{"CUNIT1", "'deg'"},
        std::pair<std::string, std::string>{"CUNIT2", "'deg'"},
        std::pair<std::string, std::string>{"CRPIX1",
                                            shortestDecimal(solution.referencePixel().x + kPixelOriginOffset)},
        std::pair<std::string, std::string>{"CRPIX2",
                                            shortestDecimal(solution.referencePixel().y + kPixelOriginOffset)},
        std::pair<std::string, std::string>{"CRVAL1", shortestDecimal(solution.referencePoint().rightAscensionDegrees)},
        std::pair<std::string, std::string>{"CRVAL2", shortestDecimal(solution.referencePoint().declinationDegrees)}};
    for (const auto& entry : header)
    {
        if (const Status written = appendCard(cards, entry.first, entry.second); !written.ok())
        {
            return written;
        }
    }

    const std::array<std::pair<const char*, double>, 4> matrix = {
        std::pair<const char*, double>{"CD1_1", solution.linear().cd11},
        std::pair<const char*, double>{"CD1_2", solution.linear().cd12},
        std::pair<const char*, double>{"CD2_1", solution.linear().cd21},
        std::pair<const char*, double>{"CD2_2", solution.linear().cd22}};
    for (const auto& entry : matrix)
    {
        if (const Status written = appendCard(cards, entry.first, shortestDecimal(entry.second)); !written.ok())
        {
            return written;
        }
    }

    if (const Status written = appendPolynomial(cards, "A", distortion.forwardX()); !written.ok())
    {
        return written;
    }
    if (const Status written = appendPolynomial(cards, "B", distortion.forwardY()); !written.ok())
    {
        return written;
    }
    if (const Status written = appendPolynomial(cards, "AP", distortion.inverseX()); !written.ok())
    {
        return written;
    }
    if (const Status written = appendPolynomial(cards, "BP", distortion.inverseY()); !written.ok())
    {
        return written;
    }

    if (cards.empty())
    {
        return cardFailure(ErrorCode::Internal, "a solution produced no cards", kBuildOperation);
    }
    return cards;
}

Result<CodecMetadata> withoutWcsCards(const CodecMetadata& metadata)
{
    std::vector<CodecCard> kept;
    kept.reserve(metadata.cards().size());
    for (const CodecCard& card : metadata.cards())
    {
        if ((card.origin() == CodecNamespace::Fits && isWcsKeyword(card.keyword())) || isXisfSolutionCard(card))
        {
            continue;
        }
        kept.push_back(card);
    }

    Result<CodecMetadata> stripped = CodecMetadata::create(metadata.sourceEncoding(), std::move(kept));
    if (!stripped.hasValue())
    {
        return Status::failure(stripped.status().code(), StatusDetail{.message = stripped.status().message(),
                                                                      .operation = kWithoutOperation,
                                                                      .domain = ErrorDomain::Algorithms});
    }
    return stripped;
}

Result<CodecMetadata> withWcsCards(const CodecMetadata& metadata, const WcsSolution& solution)
{
    Result<std::vector<CodecCard>> written = buildWcsCards(solution);
    if (!written.hasValue())
    {
        return written.status();
    }

    std::vector<CodecCard> kept;
    kept.reserve(metadata.cards().size() + written->size());
    for (const CodecCard& card : metadata.cards())
    {
        if ((card.origin() == CodecNamespace::Fits && isWcsKeyword(card.keyword())) || isXisfSolutionCard(card))
        {
            continue;
        }
        kept.push_back(card);
    }
    for (CodecCard& card : *written)
    {
        kept.push_back(std::move(card));
    }

    Result<CodecMetadata> combined = CodecMetadata::create(metadata.sourceEncoding(), std::move(kept));
    if (!combined.hasValue())
    {
        return Status::failure(combined.status().code(), StatusDetail{.message = combined.status().message(),
                                                                      .operation = kWithOperation,
                                                                      .domain = ErrorDomain::Algorithms});
    }
    return combined;
}

} // namespace an
