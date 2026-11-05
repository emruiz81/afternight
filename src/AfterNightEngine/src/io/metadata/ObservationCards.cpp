// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - carrying a source's observing context onto a derived product's FITS cards.
//
// The facts and their order are documented on `withObservationCards`. The readers here - `reading`,
// `states`, `numberOrRatio`, `statedInteger` and the date readers - are lenient where headers in the
// wild need it: a number is whatever a value begins with, and a creation time ignores whatever follows
// its seconds. Writing goes through `FitsCardEdit`, which places a card by one rule, and
// `DestinationCards` adds the keep-or-replace policy on top.

#include <AfterNightEngine/IO/ObservationCards.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include "io/metadata/FitsCardEdit.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace an
{
namespace
{
using std::string_view_literals::operator""sv;

constexpr const char* kOperation = "an::withObservationCards";

constexpr std::array<std::string_view, 44u> kCopiedKeywords = {
    "TELESCOP"sv, "TELESCOPE"sv, "FOCALLEN"sv, "FOCRATIO"sv, "APTDIA"sv,   "APTAREA"sv,  "XPIXSZ"sv,  "YPIXSZ"sv,
    "XBINNING"sv, "YBINNING"sv,  "EGAIN"sv,    "SITELAT"sv,  "SITELONG"sv, "SITEELEV"sv, "SITEALT"sv, "OBS-LAT"sv,
    "OBS-LONG"sv, "LAT-OBS"sv,   "LONG-OBS"sv, "OBSGEO-B"sv, "OBSGEO-L"sv, "RA"sv,       "DEC"sv,     "OBJCTRA"sv,
    "OBJCTDEC"sv, "OBJCTROT"sv,  "OBJRA"sv,    "OBJDEC"sv,   "TELRA"sv,    "TELDEC"sv,   "CENTALT"sv, "CENTAZ"sv,
    "AIRMASS"sv,  "PIERSIDE"sv,  "CAMERAID"sv, "SET-TEMP"sv, "USBLIMIT"sv, "FWHEEL"sv,   "FOCNAME"sv, "FOCPOS"sv,
    "FOCUSPOS"sv, "FOCUSSZ"sv,   "FOCTEMP"sv,  "FOCUSTEM"sv};

/// The namespaces whose cards carry Exif, in the order a keyword is looked up.
constexpr std::array kExifNamespaces = {CodecNamespace::Jpeg, CodecNamespace::Tiff, CodecNamespace::Png,
                                        CodecNamespace::CameraRaw};

/// What the C library counts as white space.
constexpr std::string_view kWhitespace = " \t\n\v\f\r"sv;

/// What is stripped from both ends of a card's value before it is read.
constexpr std::string_view kReadingTrim = "' .\t\r\n"sv;

std::string_view stripped(std::string_view text, std::string_view characters) noexcept
{
    const std::size_t first = text.find_first_not_of(characters);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(characters) - first + 1u);
}

/// A card's value as the readers here read it.
std::string_view reading(std::string_view value) noexcept
{
    return stripped(value, kReadingTrim);
}

/// Whether text states a value: it is not blank, and it is not the `-` that stands in for a missing one.
bool states(std::string_view text) noexcept
{
    const std::string_view value = stripped(text, kWhitespace);
    return !value.empty() && value != "-"sv;
}

std::string_view firstStated(std::string_view preferred, std::string_view fallback) noexcept
{
    return states(preferred) ? preferred : fallback;
}

bool exifOrigin(CodecNamespace origin) noexcept
{
    return std::find(kExifNamespaces.begin(), kExifNamespaces.end(), origin) != kExifNamespaces.end();
}

/// Which vocabularies a source holds, which decides where a fact with two spellings is read.
struct SourceKind
{
    bool fits = false;
    bool exif = false;
};

SourceKind kindOf(const CodecMetadata& source) noexcept
{
    SourceKind kind;
    for (const CodecCard& card : source.cards())
    {
        kind.fits = kind.fits || card.origin() == CodecNamespace::Fits;
        kind.exif = kind.exif || (exifOrigin(card.origin()) && card.keyword().starts_with("Exif."sv));
    }
    return kind;
}

/// A FITS card's value as the file wrote it, or nothing when the source has no such card.
std::string_view fitsValue(const CodecMetadata& source, std::string_view keyword) noexcept
{
    const CodecCard* const card = source.find(CodecNamespace::Fits, keyword);
    return card != nullptr ? std::string_view(card->value()) : std::string_view{};
}

/// An Exif card's value as the file wrote it, or nothing when the source has no such card.
std::string_view exifValue(const CodecMetadata& source, std::string_view keyword) noexcept
{
    for (const CodecNamespace origin : kExifNamespaces)
        if (const CodecCard* const card = source.find(origin, keyword); card != nullptr)
            return card->value();
    return {};
}

/// The number a text begins with. A result that is not finite is no number.
std::optional<double> leadingNumber(std::string_view text) noexcept
{
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1u);
    if (text.empty())
        return std::nullopt;
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::optional<double> statedNumber(std::string_view text) noexcept
{
    return states(text) ? leadingNumber(text) : std::optional<double>{};
}

/// A decimal, or a ratio of two when the text holds exactly one slash and both sides read.
std::optional<double> numberOrRatio(std::string_view text) noexcept
{
    text = stripped(text, kWhitespace);
    if (!states(text))
        return std::nullopt;
    const std::size_t slash = text.find('/');
    if (slash != std::string_view::npos && text.find('/', slash + 1u) == std::string_view::npos)
    {
        const std::optional<double> numerator = statedNumber(stripped(text.substr(0u, slash), kWhitespace));
        const std::optional<double> denominator = statedNumber(stripped(text.substr(slash + 1u), kWhitespace));
        if (numerator.has_value() && denominator.has_value() &&
            std::fabs(*denominator) > std::numeric_limits<double>::epsilon())
        {
            const double ratio = *numerator / *denominator;
            return std::isfinite(ratio) ? std::optional<double>(ratio) : std::nullopt;
        }
    }
    return leadingNumber(text);
}

/// The first three numbers of a text, split at every character that cannot be part of one.
struct LeadingNumbers
{
    std::array<double, 3u> values{};
    std::size_t count = 0u;
};

bool numericCharacter(char character) noexcept
{
    return (character >= '0' && character <= '9') || character == '+' || character == '-' || character == '.' ||
           character == '/' || character == 'e' || character == 'E';
}

LeadingNumbers leadingNumbersIn(std::string_view text) noexcept
{
    LeadingNumbers numbers;
    std::size_t start = 0u;
    for (std::size_t end = 0u; end <= text.size(); ++end)
    {
        if (end < text.size() && numericCharacter(text[end]))
            continue;
        if (end > start && numbers.count < numbers.values.size())
            if (const std::optional<double> value = numberOrRatio(text.substr(start, end - start)); value.has_value())
                numbers.values[numbers.count++] = *value;
        start = end + 1u;
    }
    return numbers;
}

/// The integer a text begins with, read as the C library's `atoi` reads one, except that text beginning with
/// no integer states none.
std::optional<std::int64_t> statedInteger(std::string_view text) noexcept
{
    text = stripped(text, kWhitespace);
    if (!states(text))
        return std::nullopt;
    if (text.front() == '+')
    {
        text.remove_prefix(1u);
        if (text.empty() || text.front() < '0' || text.front() > '9')
            return std::nullopt;
    }
    std::int64_t value = 0;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{})
        return std::nullopt;
    return value;
}

/// A number with at most six decimals and no trailing zero; nothing for a value that is not finite.
std::string sixDecimalText(double value)
{
    if (!std::isfinite(value))
        return {};
    // The widest finite double in fixed notation is 309 digits, a sign, a point and six decimals.
    std::array<char, 400u> buffer{};
    const std::to_chars_result result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed, 6);
    if (result.ec != std::errc{})
        return {};
    std::string text(buffer.data(), result.ptr);
    while (text.size() > 1u && text.back() == '0')
        text.pop_back();
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

/// Text as a quoted FITS string: kept when it arrived quoted, and otherwise quoted with each quote doubled.
std::string quotedText(std::string_view value)
{
    value = stripped(value, kWhitespace);
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
        return std::string(value);
    std::string quoted;
    quoted.reserve(value.size() + 2u);
    quoted.push_back('\'');
    for (const char character : value)
    {
        quoted.push_back(character);
        if (character == '\'')
            quoted.push_back('\'');
    }
    quoted.push_back('\'');
    return quoted;
}

std::string upperText(std::string_view text)
{
    std::string upper(text);
    for (char& character : upper)
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    return upper;
}

struct CivilDate
{
    unsigned year = 0u;
    unsigned month = 0u;
    unsigned day = 0u;
};

struct ClockTime
{
    unsigned hour = 0u;
    unsigned minute = 0u;
    unsigned second = 0u;
};

bool readDigits(std::string_view& text, std::size_t count, unsigned& value) noexcept
{
    if (text.size() < count)
        return false;
    value = 0u;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const char character = text[i];
        if (character < '0' || character > '9')
            return false;
        value = value * 10u + static_cast<unsigned>(character - '0');
    }
    text.remove_prefix(count);
    return true;
}

bool readCharacter(std::string_view& text, char expected) noexcept
{
    if (text.empty() || text.front() != expected)
        return false;
    text.remove_prefix(1u);
    return true;
}

unsigned daysInMonth(unsigned year, unsigned month) noexcept
{
    constexpr std::array<unsigned, 12u> kDays = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};
    const bool leap = year % 4u == 0u && (year % 100u != 0u || year % 400u == 0u);
    return month == 2u && leap ? 29u : kDays[month - 1u];
}

/// A date of four, two and two digits with one separator between them, naming a day of the calendar.
std::optional<CivilDate> readDate(std::string_view& text, char separator) noexcept
{
    CivilDate date;
    if (!readDigits(text, 4u, date.year) || !readCharacter(text, separator) || !readDigits(text, 2u, date.month) ||
        !readCharacter(text, separator) || !readDigits(text, 2u, date.day))
        return std::nullopt;
    if (date.month < 1u || date.month > 12u || date.day < 1u || date.day > daysInMonth(date.year, date.month))
        return std::nullopt;
    return date;
}

bool isClockTime(const ClockTime& time) noexcept
{
    // A sixty-first second is a leap second.
    return time.hour <= 23u && time.minute <= 59u && time.second <= 60u;
}

bool readClock(std::string_view& text, ClockTime& time) noexcept
{
    return readDigits(text, 2u, time.hour) && readCharacter(text, ':') && readDigits(text, 2u, time.minute) &&
           readCharacter(text, ':') && readDigits(text, 2u, time.second) && isClockTime(time);
}

void appendDigits(std::string& text, unsigned value, std::size_t count)
{
    std::array<char, 4u> digits{};
    for (std::size_t i = count; i > 0u; --i)
    {
        digits[i - 1u] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    }
    text.append(digits.data(), count);
}

std::string dateTimeText(const CivilDate& date, const ClockTime& time)
{
    std::string text;
    text.reserve(19u);
    appendDigits(text, date.year, 4u);
    text.push_back('-');
    appendDigits(text, date.month, 2u);
    text.push_back('-');
    appendDigits(text, date.day, 2u);
    text.push_back('T');
    appendDigits(text, time.hour, 2u);
    text.push_back(':');
    appendDigits(text, time.minute, 2u);
    text.push_back(':');
    appendDigits(text, time.second, 2u);
    return text;
}

/// A creation time in one of three spellings. Whatever follows the seconds - a fraction, a zone - is not
/// read.
std::optional<std::string> creationDateTime(std::string_view text)
{
    text = stripped(text, kWhitespace);
    if (!states(text))
        return std::nullopt;
    // The date's separator, and what stands between the date and the time: a space reads any run of white
    // space, including none, as the standard library's time reader does.
    constexpr std::array<std::pair<char, char>, 3u> kSpellings = {std::pair{':', ' '}, std::pair{'-', ' '},
                                                                  std::pair{'-', 'T'}};
    for (const auto& [dateSeparator, timeSeparator] : kSpellings)
    {
        std::string_view rest = text;
        const std::optional<CivilDate> date = readDate(rest, dateSeparator);
        if (!date.has_value())
            continue;
        if (timeSeparator == ' ')
            rest.remove_prefix((std::min)(rest.find_first_not_of(kWhitespace), rest.size()));
        else if (!readCharacter(rest, timeSeparator))
            continue;
        if (ClockTime time; readClock(rest, time))
            return dateTimeText(*date, time);
    }
    return std::nullopt;
}

/// The GPS date and time: a date in either separator and up to three numbers rounded to the clock.
std::optional<std::string> gpsDateTime(const CodecMetadata& source)
{
    const std::string_view dateText = reading(exifValue(source, "Exif.GPSInfo.GPSDateStamp"sv));
    const std::string_view timeText = reading(exifValue(source, "Exif.GPSInfo.GPSTimeStamp"sv));
    if (!states(dateText) || !states(timeText))
        return std::nullopt;
    std::string_view rest = dateText;
    std::optional<CivilDate> date = readDate(rest, ':');
    if (!date.has_value())
    {
        rest = dateText;
        date = readDate(rest, '-');
    }
    const LeadingNumbers numbers = leadingNumbersIn(timeText);
    if (!date.has_value() || numbers.count == 0u)
        return std::nullopt;
    std::array<unsigned, 3u> clock{};
    for (std::size_t i = 0u; i < numbers.count; ++i)
    {
        const double rounded = std::round(numbers.values[i]);
        if (!(rounded >= 0.0 && rounded <= 60.0))
            return std::nullopt;
        clock[i] = static_cast<unsigned>(rounded);
    }
    const ClockTime time{.hour = clock[0], .minute = clock[1], .second = clock[2]};
    if (!isClockTime(time))
        return std::nullopt;
    return dateTimeText(*date, time);
}

/// A pixel's size in micrometres from a focal-plane resolution and the unit it is stated in.
std::optional<double> pixelSizeMicrometres(const CodecMetadata& source, std::string_view resolutionKeyword)
{
    const std::optional<double> resolution = numberOrRatio(exifValue(source, resolutionKeyword));
    const std::optional<double> unit = numberOrRatio(exifValue(source, "Exif.Photo.FocalPlaneResolutionUnit"sv));
    if (!resolution.has_value() || !unit.has_value() || *resolution <= std::numeric_limits<double>::epsilon())
        return std::nullopt;
    // Inches, centimetres, millimetres and micrometres, by the codes the Exif standard gives them.
    const double code = std::round(*unit);
    const double millimetres = code == 2.0 ? 25.4 : code == 3.0 ? 10.0 : code == 4.0 ? 1.0 : code == 5.0 ? 0.001 : 0.0;
    if (millimetres <= 0.0)
        return std::nullopt;
    return millimetres * 1000.0 / *resolution;
}

/// A GPS latitude or longitude in degrees, negative towards the south or the west.
std::optional<double> gpsCoordinate(const CodecMetadata& source, std::string_view valueKeyword,
                                    std::string_view referenceKeyword)
{
    const std::string_view text = reading(exifValue(source, valueKeyword));
    if (!states(text))
        return std::nullopt;
    const LeadingNumbers parts = leadingNumbersIn(text);
    if (parts.count == 0u)
        return std::nullopt;
    double coordinate = std::fabs(parts.values[0]);
    if (parts.count > 1u)
        coordinate += std::fabs(parts.values[1]) / 60.0;
    if (parts.count > 2u)
        coordinate += std::fabs(parts.values[2]) / 3600.0;
    bool negative = parts.values[0] < 0.0;
    if (const std::string_view reference = reading(exifValue(source, referenceKeyword)); states(reference))
    {
        const char hemisphere = upperText(reference.substr(0u, 1u)).front();
        if (hemisphere == 'S' || hemisphere == 'W')
            negative = true;
        else if (hemisphere == 'N' || hemisphere == 'E')
            negative = false;
    }
    return negative ? -coordinate : coordinate;
}

/// A GPS altitude in metres, negative below sea level.
std::optional<double> gpsAltitude(const CodecMetadata& source)
{
    const std::optional<double> altitude = numberOrRatio(exifValue(source, "Exif.GPSInfo.GPSAltitude"sv));
    if (!altitude.has_value())
        return std::nullopt;
    const std::optional<double> reference = numberOrRatio(exifValue(source, "Exif.GPSInfo.GPSAltitudeRef"sv));
    return reference.has_value() && std::round(*reference) == 1.0 ? -*altitude : *altitude;
}

/// The destination's cards while facts are written onto them, under the policy the caller chose.
class DestinationCards
{
public:
    DestinationCards(const CodecMetadata& destination, ObservationCardPolicy policy)
        : m_cards(destination, kOperation), m_replace(policy == ObservationCardPolicy::Replace)
    {
    }

    /// Whether the destination's first FITS card of a keyword states a value.
    [[nodiscard]] bool holds(std::string_view keyword) const noexcept
    {
        const CodecCard* const card = m_cards.first(keyword);
        return card != nullptr && states(reading(card->value()));
    }

    /// Whether a fact about a keyword is written: always when replacing, otherwise when nothing states it.
    [[nodiscard]] bool open(std::string_view keyword) const noexcept
    {
        return m_replace || !holds(keyword);
    }

    [[nodiscard]] bool replaces() const noexcept
    {
        return m_replace;
    }

    /// A value written as it reads, the way a copied card, a number or a temperature is.
    void offer(std::string_view keyword, std::string_view value)
    {
        if (states(value) && open(keyword))
            m_cards.write(keyword, std::string(stripped(value, kWhitespace)));
    }

    /// A value written as a quoted string, the way a name, a date or a lens is.
    void offerText(std::string_view keyword, std::string_view value)
    {
        if (states(value) && open(keyword))
            m_cards.write(keyword, quotedText(value));
    }

    /// A value written whatever the destination states, once its own condition has been checked.
    void write(std::string_view keyword, std::string value)
    {
        m_cards.write(keyword, std::move(value));
    }

    [[nodiscard]] Result<CodecMetadata> finish() &&
    {
        return std::move(m_cards).finish();
    }

private:
    FitsCardEdit m_cards;
    bool m_replace = false;
};

void copyFitsContext(DestinationCards& cards, const CodecMetadata& source)
{
    for (const std::string_view keyword : kCopiedKeywords)
        cards.offer(keyword, fitsValue(source, keyword));
    cards.offer("OBSGEO-H"sv, fitsValue(source, "OBSGEO-H"sv));
    for (const std::string_view keyword : {"CAMMAKE"sv, "LENSMODE"sv, "DATE-OBS"sv, "DATE-LOC"sv})
        cards.offerText(keyword, fitsValue(source, keyword));
}

void writeSettings(DestinationCards& cards, const CodecMetadata& source, SourceKind kind)
{
    std::string_view exposure;
    if (kind.exif)
        exposure = reading(exifValue(source, "Exif.Photo.ExposureTime"sv));
    else if (kind.fits)
        exposure = firstStated(reading(fitsValue(source, "EXPOSURE"sv)), reading(fitsValue(source, "EXPTIME"sv)));
    if (const std::optional<double> seconds = numberOrRatio(exposure);
        seconds.has_value() && (cards.replaces() || (!cards.holds("EXPOSURE"sv) && !cards.holds("EXPTIME"sv))))
        cards.write("EXPOSURE"sv, sixDecimalText(*seconds));

    std::string_view setting;
    bool isoSpeed = false;
    if (kind.exif)
    {
        setting = reading(exifValue(source, "Exif.Photo.ISOSpeedRatings"sv));
        isoSpeed = true;
    }
    else if (kind.fits)
    {
        setting = reading(fitsValue(source, "ISOSPEED"sv));
        isoSpeed = states(setting);
        if (!isoSpeed)
            setting = reading(fitsValue(source, "GAIN"sv));
    }
    if (const std::optional<std::int64_t> value = statedInteger(setting);
        value.has_value() && (cards.replaces() || (!cards.holds("ISOSPEED"sv) && !cards.holds("GAIN"sv))))
        cards.write(isoSpeed ? "ISOSPEED"sv : "GAIN"sv, std::to_string(*value));

    if (!kind.exif && kind.fits)
        if (const std::optional<std::int64_t> offset = statedInteger(reading(fitsValue(source, "OFFSET"sv)));
            offset.has_value() && cards.open("OFFSET"sv))
            cards.write("OFFSET"sv, std::to_string(*offset));
}

void writeNames(DestinationCards& cards, const CodecMetadata& source, SourceKind kind)
{
    if (kind.exif)
        cards.offerText("INSTRUME"sv, reading(exifValue(source, "Exif.Image.Model"sv)));
    else if (kind.fits)
        cards.offerText("INSTRUME"sv, reading(fitsValue(source, "INSTRUME"sv)));

    cards.offerText("FILTER"sv, reading(fitsValue(source, "FILTER"sv)));
    std::string frameType = upperText(reading(fitsValue(source, "IMAGETYP"sv)));
    if (frameType == "OFFSET"sv)
        frameType = "BIAS";
    cards.offerText("IMAGETYP"sv, frameType);
    cards.offerText("OBJECT"sv, reading(fitsValue(source, "OBJECT"sv)));

    if (kind.exif)
        cards.offerText("SWCREATE"sv, reading(exifValue(source, "Exif.Image.Software"sv)));
    else if (kind.fits)
        cards.offerText("SWCREATE"sv, firstStated(reading(fitsValue(source, "SOFTWARE"sv)),
                                                  reading(fitsValue(source, "SWCREATE"sv))));

    if (kind.exif)
        cards.offer("CCD-TEMP"sv, reading(exifValue(source, "Exif.MakerNote.SensorTemperature"sv)));
    else if (kind.fits)
        cards.offer("CCD-TEMP"sv, reading(fitsValue(source, "CCD-TEMP"sv)));
}

void writeTimes(DestinationCards& cards, const CodecMetadata& source, SourceKind kind)
{
    std::string_view created;
    if (kind.exif)
        created = firstStated(reading(exifValue(source, "Exif.Photo.DateTimeOriginal"sv)),
                              reading(exifValue(source, "Exif.Photo.DateTimeDigitized"sv)));
    else if (kind.fits)
        created = firstStated(reading(fitsValue(source, "DATE"sv)), reading(fitsValue(source, "DATE-LOC"sv)));
    if (const std::optional<std::string> local = creationDateTime(created); local.has_value())
    {
        cards.offerText("DATE-LOC"sv, *local);
        cards.offerText("DATE-OBS"sv, *local);
    }
    if (const std::optional<std::string> satellite = gpsDateTime(source); satellite.has_value())
        cards.offerText("DATE-OBS"sv, *satellite);
}

void writeCamera(DestinationCards& cards, const CodecMetadata& source)
{
    cards.offerText("CAMMAKE"sv, reading(exifValue(source, "Exif.Image.Make"sv)));
    cards.offerText("LENSMODE"sv, firstStated(firstStated(reading(exifValue(source, "Exif.Photo.LensModel"sv)),
                                                          reading(exifValue(source, "Exif.Image.LensModel"sv))),
                                              reading(exifValue(source, "Exif.CanonCs.LensModel"sv))));

    const std::optional<double> focalLength = numberOrRatio(exifValue(source, "Exif.Photo.FocalLength"sv));
    if (focalLength.has_value())
        cards.offer("FOCALLEN"sv, sixDecimalText(*focalLength));
    const std::optional<double> fNumber = numberOrRatio(exifValue(source, "Exif.Photo.FNumber"sv));
    if (focalLength.has_value() && fNumber.has_value() && *fNumber > std::numeric_limits<double>::epsilon())
    {
        const double diameter = *focalLength / *fNumber;
        cards.offer("APTDIA"sv, sixDecimalText(diameter));
        cards.offer("APTAREA"sv, sixDecimalText(std::numbers::pi * 0.25 * diameter * diameter));
    }

    if (const std::optional<double> width = pixelSizeMicrometres(source, "Exif.Photo.FocalPlaneXResolution"sv))
        cards.offer("XPIXSZ"sv, sixDecimalText(*width));
    if (const std::optional<double> height = pixelSizeMicrometres(source, "Exif.Photo.FocalPlaneYResolution"sv))
        cards.offer("YPIXSZ"sv, sixDecimalText(*height));
}

void writeSite(DestinationCards& cards, const CodecMetadata& source)
{
    if (const std::optional<double> latitude =
            gpsCoordinate(source, "Exif.GPSInfo.GPSLatitude"sv, "Exif.GPSInfo.GPSLatitudeRef"sv))
        cards.offer("SITELAT"sv, sixDecimalText(*latitude));
    if (const std::optional<double> longitude =
            gpsCoordinate(source, "Exif.GPSInfo.GPSLongitude"sv, "Exif.GPSInfo.GPSLongitudeRef"sv))
        cards.offer("SITELONG"sv, sixDecimalText(*longitude));
    if (const std::optional<double> elevation = gpsAltitude(source))
        cards.offer("SITEELEV"sv, sixDecimalText(*elevation));
}
} // namespace

std::span<const std::string_view> copiedObservationKeywords() noexcept
{
    return kCopiedKeywords;
}

Result<CodecMetadata> withObservationCards(const CodecMetadata& destination, const CodecMetadata& source,
                                           ObservationCardPolicy policy)
{
    if (!isObservationCardPolicy(policy))
        return Status::failure(ErrorCode::InvalidArgument,
                               {.message = "the policy for a card the destination states is outside its enumeration",
                                .operation = kOperation,
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, "policy")}});

    const SourceKind kind = kindOf(source);
    DestinationCards cards(destination, policy);
    if (kind.fits)
        copyFitsContext(cards, source);
    writeSettings(cards, source, kind);
    writeNames(cards, source, kind);
    writeTimes(cards, source, kind);
    writeCamera(cards, source);
    writeSite(cards, source);
    return std::move(cards).finish();
}

} // namespace an
