// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading acquisition facts out of preserved cards, and stating them as cards again.
//
// One fact has several spellings - an Exif field a camera wrote, one or more FITS keywords a capture
// program wrote - and each fact has a precedence list below: the first spelling present with a
// nonempty value decides it. A value that cannot be read, or that the typed acquisition record would
// refuse, is counted as malformed and the next spelling is tried, rather than failing the projection:
// a decoder builds that record from the projection, and one bad card would otherwise cost the caller
// the whole file. The reverse direction states a projection's facts as FITS cards or as the Exif
// fields a raster container carries, and refuses every other target.
//
// Dates are converted by integer arithmetic on the proleptic Gregorian calendar with no time-zone
// lookup, so a date card is read as UTC and the answer does not depend on the host.

#include <AfterNightEngine/IO/CodecMetadataProjection.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace an
{
namespace
{
using std::string_view_literals::operator""sv;

Status dataFailure(const char* operation, const char* message)
{
    return Status::failure(ErrorCode::InvalidData,
                           {.message = message, .operation = operation, .domain = ErrorDomain::Io});
}

bool blank(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && blank(text.front()))
        text.remove_prefix(1u);
    while (!text.empty() && blank(text.back()))
        text.remove_suffix(1u);
    return text;
}

/// The value as a file wrote it: surrounding whitespace and one pair of single quotes removed, and
/// the blanks a FITS string pads its value with removed again inside them.
std::string_view cardText(std::string_view value) noexcept
{
    value = trimmed(value);
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
        value = trimmed(value.substr(1u, value.size() - 2u));
    return value;
}

std::optional<double> parseDecimal(std::string_view text) noexcept
{
    text = trimmed(text);
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1u);
    if (text.empty())
        return std::nullopt;
    // A FITS real may spell its exponent with a D (standard 4.2.4), which no decimal parser of the language reads:
    // such a number is read with the letter made an E, from a copy on the stack.
    std::array<char, 64u> spelled{};
    if (text.find_first_of("Dd") != std::string_view::npos)
    {
        if (text.size() > spelled.size())
            return std::nullopt;
        std::transform(text.begin(), text.end(), spelled.begin(),
                       [](char letter) { return letter == 'D' || letter == 'd' ? 'E' : letter; });
        text = std::string_view(spelled.data(), text.size());
    }
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

/// Which cards a spelling is looked up in.
enum class Vocabulary : std::uint8_t
{
    Exif, ///< The Jpeg, Tiff, Png and CameraRaw namespaces, which all carry Exif.
    Fits ///< The Fits namespace.
};

struct Spelling
{
    Vocabulary vocabulary;
    std::string_view keyword;
};

constexpr std::array kExifNamespaces = {CodecNamespace::Jpeg, CodecNamespace::Tiff, CodecNamespace::Png,
                                        CodecNamespace::CameraRaw};

/// Whether the typed record admits one stated value of one of its numeric facts.
///
/// Asked of the record itself rather than restated here, so the projection and the record cannot come
/// to disagree about where an exposure stops being one. A record holding only that value is built and
/// discarded; the refusal it describes is the cold path.
[[nodiscard]] bool recordAdmits(std::optional<double> AcquisitionMetadataRequest::* fact, double value)
{
    AcquisitionMetadataRequest request;
    request.*fact = value;
    return AcquisitionMetadata::create(request).hasValue();
}

const CodecCard* lookup(const CodecMetadata& codec, const Spelling& spelling) noexcept
{
    if (spelling.vocabulary == Vocabulary::Fits)
        return codec.find(CodecNamespace::Fits, spelling.keyword);
    for (const CodecNamespace origin : kExifNamespaces)
        if (const CodecCard* card = codec.find(origin, spelling.keyword); card != nullptr)
            return card;
    return nullptr;
}

// Precedence lists: Exif spellings first, then FITS, each in the order the header documents.
constexpr std::array kInstrument = {Spelling{Vocabulary::Exif, "Exif.Image.Model"sv},
                                    Spelling{Vocabulary::Fits, "INSTRUME"sv}};
constexpr std::array kTelescope = {Spelling{Vocabulary::Fits, "TELESCOP"sv}};
constexpr std::array kObserver = {Spelling{Vocabulary::Fits, "OBSERVER"sv}};
constexpr std::array kObject = {Spelling{Vocabulary::Fits, "OBJECT"sv}};
constexpr std::array kFilter = {Spelling{Vocabulary::Fits, "FILTER"sv}};
constexpr std::array kExposure = {Spelling{Vocabulary::Exif, "Exif.Photo.ExposureTime"sv},
                                  Spelling{Vocabulary::Fits, "EXPOSURE"sv}, Spelling{Vocabulary::Fits, "EXPTIME"sv}};
constexpr std::array kTemperature = {Spelling{Vocabulary::Exif, "Exif.MakerNote.SensorTemperature"sv},
                                     Spelling{Vocabulary::Fits, "CCD-TEMP"sv}};
constexpr std::array kElectronGain = {Spelling{Vocabulary::Fits, "EGAIN"sv}};
constexpr std::array kExposureStart = {Spelling{Vocabulary::Exif, "Exif.Photo.DateTimeOriginal"sv},
                                       Spelling{Vocabulary::Fits, "DATE-OBS"sv}};
constexpr std::array kIsoSpeed = {Spelling{Vocabulary::Exif, "Exif.Photo.ISOSpeedRatings"sv},
                                  Spelling{Vocabulary::Fits, "ISOSPEED"sv}};
constexpr std::array kGainSetting = {Spelling{Vocabulary::Fits, "GAIN"sv}};
constexpr std::array kOffset = {Spelling{Vocabulary::Fits, "OFFSET"sv}};
constexpr std::array kFrameType = {Spelling{Vocabulary::Fits, "IMAGETYP"sv}};
constexpr std::array kSoftware = {Spelling{Vocabulary::Exif, "Exif.Image.Software"sv},
                                  Spelling{Vocabulary::Fits, "SOFTWARE"sv}, Spelling{Vocabulary::Fits, "SWCREATE"sv}};
constexpr std::array kCreationTime = {Spelling{Vocabulary::Exif, "Exif.Photo.DateTimeDigitized"sv},
                                      Spelling{Vocabulary::Fits, "DATE"sv}, Spelling{Vocabulary::Fits, "DATE-LOC"sv}};
constexpr Spelling kBinningX{Vocabulary::Fits, "XBINNING"sv};
constexpr Spelling kBinningY{Vocabulary::Fits, "YBINNING"sv};

class Reader
{
public:
    explicit Reader(const CodecMetadata& codec, AcquisitionProjection& projection) noexcept
        : m_codec(codec), m_projection(projection)
    {
    }

    /// The first nonempty text among the spellings.
    [[nodiscard]] std::string text(std::span<const Spelling> spellings)
    {
        for (const Spelling& spelling : spellings)
        {
            const CodecCard* card = lookup(m_codec, spelling);
            if (card == nullptr)
                continue;
            const std::string_view value = cardText(card->value());
            if (value.empty())
                continue;
            ++m_projection.consultedCards;
            return std::string(value);
        }
        return {};
    }

    /// The first readable number among the spellings; a malformed card is counted and skipped.
    ///
    /// When `fact` names one of the typed record's members, a readable number the record refuses for
    /// that member is malformed too: a decoder builds the record from this projection, and a value it
    /// cannot hold would otherwise cost the caller the whole file rather than one fact.
    [[nodiscard]] std::optional<double> number(std::span<const Spelling> spellings,
                                               std::optional<double> AcquisitionMetadataRequest::* fact = nullptr)
    {
        for (const Spelling& spelling : spellings)
        {
            const CodecCard* card = lookup(m_codec, spelling);
            if (card == nullptr || cardText(card->value()).empty())
                continue;
            const Result<double> value = parseNumericCard(card->value());
            if (!value.hasValue() || (fact != nullptr && !recordAdmits(fact, *value)))
            {
                ++m_projection.malformedCards;
                continue;
            }
            ++m_projection.consultedCards;
            return *value;
        }
        return std::nullopt;
    }

    /// The first readable integer among the spellings.
    [[nodiscard]] std::optional<std::int64_t> integer(std::span<const Spelling> spellings)
    {
        for (const Spelling& spelling : spellings)
        {
            const CodecCard* card = lookup(m_codec, spelling);
            if (card == nullptr || cardText(card->value()).empty())
                continue;
            const std::optional<std::int64_t> value = integerOf(card->value());
            if (!value)
            {
                ++m_projection.malformedCards;
                continue;
            }
            ++m_projection.consultedCards;
            return value;
        }
        return std::nullopt;
    }

    /// The first readable date among the spellings.
    [[nodiscard]] std::optional<UtcInstant> date(std::span<const Spelling> spellings)
    {
        for (const Spelling& spelling : spellings)
        {
            const CodecCard* card = lookup(m_codec, spelling);
            if (card == nullptr || cardText(card->value()).empty())
                continue;
            const Result<UtcInstant> value = parseDateCard(card->value());
            if (!value.hasValue())
            {
                ++m_projection.malformedCards;
                continue;
            }
            ++m_projection.consultedCards;
            return *value;
        }
        return std::nullopt;
    }

    /// Both binning factors, or nothing; a lone axis or an unreadable one is malformed.
    [[nodiscard]] std::optional<PixelBinning> binning()
    {
        const CodecCard* x = lookup(m_codec, kBinningX);
        const CodecCard* y = lookup(m_codec, kBinningY);
        if (x == nullptr && y == nullptr)
            return std::nullopt;
        if (x == nullptr || y == nullptr)
        {
            ++m_projection.malformedCards;
            return std::nullopt;
        }
        const std::optional<std::int64_t> horizontal = integerOf(x->value());
        const std::optional<std::int64_t> vertical = integerOf(y->value());
        std::uint32_t malformed = 0u;
        if (!horizontal || *horizontal <= 0 || *horizontal > std::numeric_limits<std::uint32_t>::max())
            ++malformed;
        if (!vertical || *vertical <= 0 || *vertical > std::numeric_limits<std::uint32_t>::max())
            ++malformed;
        if (malformed != 0u)
        {
            m_projection.malformedCards += malformed;
            return std::nullopt;
        }
        m_projection.consultedCards += 2u;
        return PixelBinning{static_cast<std::uint32_t>(*horizontal), static_cast<std::uint32_t>(*vertical)};
    }

private:
    [[nodiscard]] static std::optional<std::int64_t> integerOf(std::string_view value)
    {
        const Result<double> number = parseNumericCard(value);
        if (!number.hasValue())
            return std::nullopt;
        const double whole = std::trunc(*number);
        if (whole != *number || whole < -9.2e18 || whole > 9.2e18)
            return std::nullopt;
        return static_cast<std::int64_t>(whole);
    }

    const CodecMetadata& m_codec;
    AcquisitionProjection& m_projection;
};

FrameType frameTypeOf(std::string_view text) noexcept
{
    const auto contains = [text](std::string_view word) noexcept {
        return std::search(text.begin(), text.end(), word.begin(), word.end(), [](char value, char upper) noexcept {
                   if (value >= 'a' && value <= 'z')
                       value = static_cast<char>(value - ('a' - 'A'));
                   return value == upper;
               }) != text.end();
    };
    if (contains("LIGHT"))
        return FrameType::Light;
    if (contains("DARK"))
        return FrameType::Dark;
    if (contains("FLAT"))
        return FrameType::Flat;
    if (contains("BIAS") || contains("OFFSET"))
        return FrameType::Bias;
    return FrameType::Unknown;
}

std::string decimalText(double value)
{
    std::array<char, 64u> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

std::string integerText(std::int64_t value)
{
    return std::to_string(value);
}

/// An Exif exposure: a whole or decimal count of seconds, or the reciprocal fraction below one.
std::string exifExposureText(double seconds)
{
    if (seconds > 0.0 && seconds < 1.0)
    {
        const double denominator = std::round(1.0 / seconds);
        if (denominator >= 1.0 && denominator < 1e15)
            return "1/" + decimalText(denominator);
    }
    return decimalText(seconds);
}

bool rasterTarget(CodecNamespace target) noexcept
{
    return target == CodecNamespace::Jpeg || target == CodecNamespace::Tiff || target == CodecNamespace::Png;
}

// Civil calendar arithmetic on the proleptic Gregorian calendar, day zero being 1970-01-01. These
// are Howard Hinnant's `days_from_civil` and `civil_from_days`, which he placed in the public domain:
// shifting the year to start in March puts the leap day last, so a day of the year is a closed
// formula of the month, and a 400-year era of 146097 days makes negative years exact.
std::int64_t daysFromCivil(std::int64_t year, unsigned month, unsigned day) noexcept
{
    year -= month <= 2u ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = month > 2u ? month - 3u : month + 9u;
    const unsigned dayOfYear = (153u * shiftedMonth + 2u) / 5u + day - 1u;
    const unsigned dayOfEra = yearOfEra * 365u + yearOfEra / 4u - yearOfEra / 100u + dayOfYear;
    return era * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

void civilFromDays(std::int64_t days, std::int64_t& year, unsigned& month, unsigned& day) noexcept
{
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460u + dayOfEra / 36524u - dayOfEra / 146096u) / 365u;
    year = static_cast<std::int64_t>(yearOfEra) + era * 400;
    const unsigned dayOfYear = dayOfEra - (365u * yearOfEra + yearOfEra / 4u - yearOfEra / 100u);
    const unsigned shifted = (5u * dayOfYear + 2u) / 153u;
    day = dayOfYear - (153u * shifted + 2u) / 5u + 1u;
    month = shifted < 10u ? shifted + 3u : shifted - 9u;
    year += month <= 2u ? 1 : 0;
}

unsigned daysInMonth(std::int64_t year, unsigned month) noexcept
{
    constexpr std::array<unsigned, 12u> kDays = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return month == 2u && leap ? 29u : kDays[month - 1u];
}

bool digits(std::string_view text, std::size_t count, unsigned& value) noexcept
{
    if (text.size() < count)
        return false;
    value = 0u;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const char c = text[i];
        if (c < '0' || c > '9')
            return false;
        value = value * 10u + static_cast<unsigned>(c - '0');
    }
    return true;
}

void appendTwoDigits(std::string& out, unsigned value)
{
    out.push_back(static_cast<char>('0' + value / 10u));
    out.push_back(static_cast<char>('0' + value % 10u));
}

Result<CodecCard> statedCard(CodecNamespace origin, std::string_view keyword, std::string value,
                             std::string_view comment)
{
    return CodecCard::create(origin, std::string(keyword), std::move(value), std::string(comment));
}
} // namespace

const char* frameTypeName(FrameType type) noexcept
{
    switch (type)
    {
    case FrameType::Unknown:
        return "Unknown";
    case FrameType::Light:
        return "Light";
    case FrameType::Dark:
        return "Dark";
    case FrameType::Flat:
        return "Flat";
    case FrameType::Bias:
        return "Bias";
    }
    return "UnknownFrameType";
}

Result<double> parseNumericCard(std::string_view text)
{
    const std::string_view value = cardText(text);
    if (value.empty())
        return dataFailure("parseNumericCard", "a numeric card has a value");
    const std::size_t slash = value.find('/');
    if (slash == std::string_view::npos)
    {
        const std::optional<double> number = parseDecimal(value);
        if (!number)
            return dataFailure("parseNumericCard", "the value is not a finite decimal number");
        return *number;
    }
    const std::optional<double> numerator = parseDecimal(value.substr(0u, slash));
    const std::optional<double> denominator = parseDecimal(value.substr(slash + 1u));
    if (!numerator || !denominator)
        return dataFailure("parseNumericCard", "the value is not a fraction of two finite decimal numbers");
    if (*denominator == 0.0)
        return dataFailure("parseNumericCard", "the fraction's denominator is zero");
    const double quotient = *numerator / *denominator;
    if (!std::isfinite(quotient))
        return dataFailure("parseNumericCard", "the fraction is not a finite number");
    return quotient;
}

Result<UtcInstant> parseDateCard(std::string_view text)
{
    constexpr const char* kOperation = "parseDateCard";
    std::string_view value = cardText(text);
    unsigned year = 0u, month = 0u, day = 0u, hour = 0u, minute = 0u, second = 0u;
    if (!digits(value, 4u, year) || value.size() < 10u || (value[4] != '-' && value[4] != ':') ||
        !digits(value.substr(5u), 2u, month) || value[7] != value[4] || !digits(value.substr(8u), 2u, day))
        return dataFailure(kOperation, "a date card starts with a four-digit year, a month and a day");
    value.remove_prefix(10u);
    std::int64_t fraction = 0;
    bool timeOfDay = false;
    if (!value.empty() && (value.front() == 'T' || value.front() == ' '))
    {
        value.remove_prefix(1u);
        if (!digits(value, 2u, hour) || value.size() < 8u || value[2] != ':' || !digits(value.substr(3u), 2u, minute) ||
            value[5] != ':' || !digits(value.substr(6u), 2u, second))
            return dataFailure(kOperation, "a time of day is hours, minutes and seconds, each of two digits");
        value.remove_prefix(8u);
        timeOfDay = true;
        if (!value.empty() && (value.front() == '.' || value.front() == ','))
        {
            value.remove_prefix(1u);
            std::size_t count = 0u;
            std::int64_t scale = 100000000;
            while (!value.empty() && value.front() >= '0' && value.front() <= '9')
            {
                if (count < 9u)
                {
                    fraction += static_cast<std::int64_t>(value.front() - '0') * scale;
                    scale /= 10;
                }
                ++count;
                value.remove_prefix(1u);
            }
            if (count == 0u)
                return dataFailure(kOperation, "a fraction of a second has at least one digit");
        }
    }
    std::int64_t offsetSeconds = 0;
    if (!value.empty() && (value.front() == 'Z' || value.front() == 'z'))
    {
        value.remove_prefix(1u);
    }
    else if (timeOfDay && !value.empty() && (value.front() == '+' || value.front() == '-'))
    {
        // A zone offset after the time of day, as capture programs write one: hours, then minutes with or without a
        // colon, or none.
        const std::int64_t sign = value.front() == '-' ? -1 : 1;
        value.remove_prefix(1u);
        unsigned offsetHours = 0u;
        unsigned offsetMinutes = 0u;
        if (!digits(value, 2u, offsetHours))
            return dataFailure(kOperation, "a zone offset is two digits of hours, then optionally two of minutes");
        value.remove_prefix(2u);
        const bool colon = !value.empty() && value.front() == ':';
        if (colon)
            value.remove_prefix(1u);
        if (colon || !value.empty())
        {
            if (!digits(value, 2u, offsetMinutes))
                return dataFailure(kOperation, "a zone offset is two digits of hours, then optionally two of minutes");
            value.remove_prefix(2u);
        }
        if (offsetHours > 23u || offsetMinutes > 59u)
            return dataFailure(kOperation, "a zone offset is less than a day");
        offsetSeconds =
            sign * (static_cast<std::int64_t>(offsetHours) * 3600 + static_cast<std::int64_t>(offsetMinutes) * 60);
    }
    if (!value.empty())
        return dataFailure(kOperation, "the date carries text after its last field");
    if (month < 1u || month > 12u || day < 1u || day > daysInMonth(year, month) || hour > 23u || minute > 59u ||
        second > 60u)
        return dataFailure(kOperation, "a field of the date is outside its range");
    const std::int64_t days = daysFromCivil(static_cast<std::int64_t>(year), month, day);
    const std::int64_t seconds = days * 86400 + static_cast<std::int64_t>(hour) * 3600 +
                                 static_cast<std::int64_t>(minute) * 60 + static_cast<std::int64_t>(second) -
                                 offsetSeconds;
    return UtcInstant{seconds * 1000000000 + fraction};
}

std::string formatDateCard(UtcInstant instant, CodecNamespace target)
{
    constexpr std::int64_t kNanosecondsPerSecond = 1000000000;
    std::int64_t seconds = instant.nanosecondsSinceEpoch / kNanosecondsPerSecond;
    std::int64_t fraction = instant.nanosecondsSinceEpoch % kNanosecondsPerSecond;
    if (fraction < 0)
    {
        fraction += kNanosecondsPerSecond;
        --seconds;
    }
    std::int64_t days = seconds / 86400;
    std::int64_t remainder = seconds % 86400;
    if (remainder < 0)
    {
        remainder += 86400;
        --days;
    }
    std::int64_t year = 0;
    unsigned month = 0u, day = 0u;
    civilFromDays(days, year, month, day);
    const auto hour = static_cast<unsigned>(remainder / 3600);
    const auto minute = static_cast<unsigned>(remainder % 3600 / 60);
    const auto second = static_cast<unsigned>(remainder % 60);
    const bool exif = rasterTarget(target) || target == CodecNamespace::CameraRaw;
    std::string out;
    std::string yearText = std::to_string(year < 0 ? -year : year);
    while (yearText.size() < 4u)
        yearText.insert(yearText.begin(), '0');
    if (year < 0)
        out.push_back('-');
    out += yearText;
    const char dateSeparator = exif ? ':' : '-';
    out.push_back(dateSeparator);
    appendTwoDigits(out, month);
    out.push_back(dateSeparator);
    appendTwoDigits(out, day);
    out.push_back(exif ? ' ' : 'T');
    appendTwoDigits(out, hour);
    out.push_back(':');
    appendTwoDigits(out, minute);
    out.push_back(':');
    appendTwoDigits(out, second);
    if (!exif && fraction != 0)
    {
        std::string digitsText = std::to_string(fraction);
        while (digitsText.size() < 9u)
            digitsText.insert(digitsText.begin(), '0');
        while (!digitsText.empty() && digitsText.back() == '0')
            digitsText.pop_back();
        out.push_back('.');
        out += digitsText;
    }
    return out;
}

AcquisitionProjection projectAcquisition(const CodecMetadata& codec)
{
    AcquisitionProjection projection;
    Reader reader(codec, projection);
    projection.acquisition.instrumentName = reader.text(kInstrument);
    projection.acquisition.telescopeName = reader.text(kTelescope);
    projection.acquisition.observerName = reader.text(kObserver);
    projection.acquisition.objectName = reader.text(kObject);
    projection.acquisition.filterName = reader.text(kFilter);
    projection.acquisition.exposureSeconds = reader.number(kExposure, &AcquisitionMetadataRequest::exposureSeconds);
    projection.acquisition.sensorTemperatureCelsius =
        reader.number(kTemperature, &AcquisitionMetadataRequest::sensorTemperatureCelsius);
    projection.acquisition.gainElectronsPerAdu =
        reader.number(kElectronGain, &AcquisitionMetadataRequest::gainElectronsPerAdu);
    projection.acquisition.binning = reader.binning();
    projection.acquisition.exposureStart = reader.date(kExposureStart);
    projection.isoSpeed = reader.integer(kIsoSpeed);
    projection.gainSetting = reader.integer(kGainSetting);
    projection.cameraOffset = reader.integer(kOffset);
    projection.frameTypeText = reader.text(kFrameType);
    projection.frameType = frameTypeOf(projection.frameTypeText);
    projection.software = reader.text(kSoftware);
    projection.creationTime = reader.date(kCreationTime);
    return projection;
}

Result<std::vector<CodecCard>> acquisitionCards(const AcquisitionProjection& projection, CodecNamespace target)
{
    std::vector<CodecCard> cards;
    const auto add = [&](std::string_view keyword, std::string value, std::string_view comment) -> Status {
        Result<CodecCard> card = statedCard(target, keyword, std::move(value), comment);
        if (!card.hasValue())
            return card.status();
        cards.push_back(std::move(*card));
        return Status::success();
    };
    const auto addText = [&](std::string_view keyword, const std::string& value, std::string_view comment) -> Status {
        return value.empty() ? Status::success() : add(keyword, value, comment);
    };
    const AcquisitionMetadataRequest& acquisition = projection.acquisition;
    Status status = Status::success();
    const auto step = [&](Status next) {
        if (status.ok())
            status = std::move(next);
    };
    if (target == CodecNamespace::Fits)
    {
        step(addText("INSTRUME"sv, acquisition.instrumentName, "camera or sensor"sv));
        step(addText("TELESCOP"sv, acquisition.telescopeName, "telescope or lens"sv));
        step(addText("OBSERVER"sv, acquisition.observerName, "observer"sv));
        step(addText("OBJECT"sv, acquisition.objectName, "target"sv));
        step(addText("FILTER"sv, acquisition.filterName, "filter in the light path"sv));
        if (acquisition.exposureSeconds)
            step(add("EXPOSURE"sv, decimalText(*acquisition.exposureSeconds), "exposure time in seconds"sv));
        if (acquisition.sensorTemperatureCelsius)
            step(add("CCD-TEMP"sv, decimalText(*acquisition.sensorTemperatureCelsius),
                     "sensor temperature in degrees Celsius"sv));
        if (acquisition.gainElectronsPerAdu)
            step(add("EGAIN"sv, decimalText(*acquisition.gainElectronsPerAdu), "electrons per ADU"sv));
        if (acquisition.binning)
        {
            step(add("XBINNING"sv, integerText(acquisition.binning->horizontal), "sensor columns per pixel"sv));
            step(add("YBINNING"sv, integerText(acquisition.binning->vertical), "sensor rows per pixel"sv));
        }
        if (acquisition.exposureStart)
            step(add("DATE-OBS"sv, formatDateCard(*acquisition.exposureStart, target), "exposure start, UTC"sv));
        if (projection.isoSpeed)
            step(add("ISOSPEED"sv, integerText(*projection.isoSpeed), "film-speed equivalent"sv));
        if (projection.gainSetting)
            step(add("GAIN"sv, integerText(*projection.gainSetting), "camera gain setting"sv));
        if (projection.cameraOffset)
            step(add("OFFSET"sv, integerText(*projection.cameraOffset), "camera offset setting"sv));
        if (projection.frameType != FrameType::Unknown)
        {
            std::string kind = frameTypeName(projection.frameType);
            for (char& value : kind)
                if (value >= 'a' && value <= 'z')
                    value = static_cast<char>(value - ('a' - 'A'));
            step(add("IMAGETYP"sv, std::move(kind), "frame kind"sv));
        }
        else
            step(addText("IMAGETYP"sv, projection.frameTypeText, "frame kind as the source spelled it"sv));
        step(addText("SWCREATE"sv, projection.software, "software that wrote the file"sv));
        if (projection.creationTime)
            step(add("DATE"sv, formatDateCard(*projection.creationTime, target), "file creation, UTC"sv));
    }
    else if (rasterTarget(target))
    {
        step(addText("Exif.Image.Model"sv, acquisition.instrumentName, "camera model"sv));
        if (acquisition.exposureSeconds)
            step(add("Exif.Photo.ExposureTime"sv, exifExposureText(*acquisition.exposureSeconds),
                     "exposure time in seconds"sv));
        if (acquisition.sensorTemperatureCelsius)
            step(add("Exif.MakerNote.SensorTemperature"sv, decimalText(*acquisition.sensorTemperatureCelsius),
                     "sensor temperature in degrees Celsius"sv));
        if (projection.isoSpeed)
            step(add("Exif.Photo.ISOSpeedRatings"sv, integerText(*projection.isoSpeed), "film-speed equivalent"sv));
        step(addText("Exif.Image.Software"sv, projection.software, "software that wrote the file"sv));
        if (acquisition.exposureStart)
            step(add("Exif.Photo.DateTimeOriginal"sv, formatDateCard(*acquisition.exposureStart, target),
                     "exposure start"sv));
        if (projection.creationTime)
            step(add("Exif.Photo.DateTimeDigitized"sv, formatDateCard(*projection.creationTime, target),
                     "file creation"sv));
    }
    else
        return Status::failure(ErrorCode::Unsupported,
                               {.message = "acquisition facts are stated as FITS cards or Exif fields",
                                .operation = "acquisitionCards",
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, "target")}});
    if (status.failed())
        return status;
    return cards;
}
} // namespace an
