// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what a frame's cards say about its sky before it is solved.
//
// Every reading here looks a keyword up the way `CodecMetadata::find` does - the first card of the
// FITS namespace that carries it - and reads its value without allocating: blanks and a FITS string's
// quotes are stripped from a view, and a number is converted from a copy in a fixed buffer, where the
// Fortran exponent letter becomes the standard one. The mean direction sums the unit vectors in the
// order the directions are given.

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>

#include <AfterNightEngine/Algorithms/Astrometry/Sexagesimal.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

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

constexpr const char* kReadPointingOperation = "an::readPointingHint";
constexpr const char* kPlateScaleOperation = "an::computePlateScaleArcsecPerPixel";
constexpr const char* kFocalLengthOperation = "an::computeFocalLengthMillimetres";
constexpr const char* kEffectivePixelOperation = "an::resolveEffectivePixelSizeMicrometres";
constexpr const char* kScaleHintOperation = "an::resolvePlateScaleHint";
constexpr const char* kMeanDirectionOperation = "an::sphericalMeanDirection";

/// The order a plate solve reads its position hint in.
constexpr std::array<PointingCards, 5> kPlateSolveHintCards{PointingCards::ReferenceValue, PointingCards::Pointing,
                                                            PointingCards::Object, PointingCards::ObjectShort,
                                                            PointingCards::Telescope};

/// The order a frame's observed pointing is read in.
constexpr std::array<PointingCards, 4> kObservedPointingCards{PointingCards::Pointing, PointingCards::Object,
                                                              PointingCards::ObjectShort, PointingCards::Telescope};

/// The keywords each optical fact is read from, in the order they are tried.
constexpr std::array<std::string_view, 2> kFocalLengthKeywords{"FOCALLEN", "FOCAL"};
constexpr std::array<std::string_view, 3> kPixelSizeKeywords{"XPIXSZ", "PIXSIZE", "PIXSIZE1"};
constexpr std::array<std::string_view, 1> kDrizzleScaleKeywords{"D001SCAL"};
constexpr std::array<std::string_view, 1> kDrizzleMultiplierKeywords{"DRIZMULT"};

/// J2000.0, 2000-01-01T12:00:00, in seconds since the Unix epoch.
constexpr std::int64_t kJ2000UnixSeconds = 946728000;

/// Seconds in one Julian year of 365.25 days.
constexpr double kSecondsPerJulianYear = 31557600.0;

/// Nanoseconds in one second.
constexpr std::int64_t kNanosecondsPerSecond = 1000000000;

/// The conversion constants between degrees and radians the mean direction computes with.
constexpr double kRadiansPerDegree = 0.017453292519943295769;
constexpr double kDegreesPerRadian = 57.295779513082320877;

/// The shortest mean resultant - the unit vectors' sum over their count - that has a direction.
constexpr double kMinimumMeanResultant = 1.0e-9;

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param operation the public operation that failed.
/// @param message the explanation for a person.
/// @return the complete failure.
[[nodiscard]] Status hintFailure(const ErrorCode code, const char* const operation, std::string message)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Builds a failure naming the parameter or member at fault.
///
/// @param operation the public operation that failed.
/// @param parameter the name of what was refused.
/// @param message the explanation for a person.
/// @return the complete failure, carrying the name as its `Parameter` context.
[[nodiscard]] Status argumentFailure(const char* const operation, const char* const parameter, std::string message)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = std::move(message),
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// The first card of the FITS namespace that carries a keyword.
///
/// @param cards the cards to search.
/// @param keyword the keyword, compared exactly.
/// @return the card, or null.
[[nodiscard]] const CodecCard* findFitsCard(const std::span<const CodecCard> cards,
                                            const std::string_view keyword) noexcept
{
    for (const CodecCard& card : cards)
    {
        if (card.origin() == CodecNamespace::Fits && card.keyword() == keyword)
        {
            return &card;
        }
    }
    return nullptr;
}

/// Whether a character is a blank a card's value may carry around itself.
///
/// @param character the character to test.
/// @return true for a space or a tab.
[[nodiscard]] constexpr bool isBlank(const char character) noexcept
{
    return character == ' ' || character == '\t';
}

/// A card's value without its surrounding blanks and, for a string, its quotes and inner blanks.
///
/// @param value the value as the card carries it.
/// @return the view a reader means.
[[nodiscard]] std::string_view valueText(std::string_view value) noexcept
{
    const auto trim = [](std::string_view text) {
        while (!text.empty() && isBlank(text.front()))
        {
            text.remove_prefix(1u);
        }
        while (!text.empty() && isBlank(text.back()))
        {
            text.remove_suffix(1u);
        }
        return text;
    };
    value = trim(value);
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
    {
        value = trim(value.substr(1u, value.size() - 2u));
    }
    return value;
}

/// Reads a card's value as a positive finite number, without allocating.
///
/// A leading plus sign and the Fortran exponent letter `D`, in either case, are read as the convention
/// allows them; anything else that is not exactly one number states nothing.
///
/// @param value the value as the card carries it.
/// @return the number, or nothing.
[[nodiscard]] std::optional<double> positiveNumber(const std::string_view value) noexcept
{
    std::string_view text = valueText(value);
    if (!text.empty() && text.front() == '+')
    {
        text.remove_prefix(1u);
    }
    std::array<char, 64> buffer{};
    if (text.empty() || text.size() > buffer.size())
    {
        return std::nullopt;
    }
    for (std::size_t index = 0u; index < text.size(); ++index)
    {
        const char character = text[index];
        if (character == 'D' || character == 'd')
        {
            buffer[index] = 'E';
        }
        else if ((character >= '0' && character <= '9') || character == '.' || character == '-' || character == '+' ||
                 character == 'e' || character == 'E')
        {
            buffer[index] = character;
        }
        else
        {
            // Only digits, signs, points and exponents spell a number, which keeps `inf` and `nan` out.
            return std::nullopt;
        }
    }
    double number = 0.0;
    const char* const last = buffer.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(buffer.data(), last, number);
    if (parsed.ec != std::errc{} || parsed.ptr != last || !std::isfinite(number) || number <= 0.0)
    {
        return std::nullopt;
    }
    return number;
}

/// The first of a fact's keywords whose value reads as a positive finite number.
///
/// @param cards the cards to search.
/// @param keywords the keywords to try, in order.
/// @return the number, or nothing.
template <std::size_t Count>
[[nodiscard]] std::optional<double> firstPositiveNumber(const std::span<const CodecCard> cards,
                                                        const std::array<std::string_view, Count>& keywords) noexcept
{
    for (const std::string_view keyword : keywords)
    {
        if (const CodecCard* const card = findFitsCard(cards, keyword); card != nullptr)
        {
            if (const std::optional<double> number = positiveNumber(card->value()); number.has_value())
            {
                return number;
            }
        }
    }
    return std::nullopt;
}

/// Whether a value is finite and positive.
///
/// @param value the value to test.
/// @return true when it is.
[[nodiscard]] bool finitePositive(const double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// Checks every stated member of an optical record.
///
/// @param optics the record.
/// @param operation the public operation checking it.
/// @return success, or `ErrorCode::InvalidArgument` naming the first member that is not finite and
///         positive.
[[nodiscard]] Status checkOptics(const OpticalHints& optics, const char* const operation)
{
    const std::array<std::pair<const std::optional<double>*, const char*>, 4> members{
        {{&optics.focalLengthMillimetres, "focalLengthMillimetres"},
         {&optics.pixelSizeMicrometres, "pixelSizeMicrometres"},
         {&optics.drizzleScaleArcsecPerPixel, "drizzleScaleArcsecPerPixel"},
         {&optics.drizzleMultiplier, "drizzleMultiplier"}}};
    for (const auto& [member, name] : members)
    {
        if (member->has_value() && !finitePositive(**member))
        {
            return argumentFailure(operation, name, "a stated optical fact is a finite positive number");
        }
    }
    return Status::success();
}

/// The effective pixel size of an already checked record, or why it has none.
///
/// @param optics the checked record.
/// @param operation the public operation resolving it.
/// @return micrometres, or the failure the public operation documents.
[[nodiscard]] Result<double> effectivePixelSize(const OpticalHints& optics, const char* const operation)
{
    if (!optics.pixelSizeMicrometres.has_value())
    {
        return hintFailure(ErrorCode::EmptySelection, operation, "no pixel size is stated");
    }
    const double pixel = *optics.pixelSizeMicrometres;
    std::optional<double> multiplier = optics.drizzleMultiplier;
    if (!multiplier.has_value() && optics.drizzleScaleArcsecPerPixel.has_value() &&
        optics.focalLengthMillimetres.has_value())
    {
        const Result<double> native = computePlateScaleArcsecPerPixel(pixel, *optics.focalLengthMillimetres);
        if (!native.hasValue())
        {
            return native.status();
        }
        multiplier = *native / *optics.drizzleScaleArcsecPerPixel;
    }
    const double size = multiplier.has_value() ? pixel / *multiplier : pixel;
    if (!finitePositive(size))
    {
        return hintFailure(ErrorCode::OutOfRange, operation,
                           "the effective pixel size is not a finite positive number");
    }
    return size;
}

} // namespace

const char* pointingCardsName(const PointingCards cards) noexcept
{
    switch (cards)
    {
    case PointingCards::ReferenceValue:
        return "ReferenceValue";
    case PointingCards::Pointing:
        return "Pointing";
    case PointingCards::Object:
        return "Object";
    case PointingCards::ObjectShort:
        return "ObjectShort";
    case PointingCards::Telescope:
        return "Telescope";
    }
    return "UnknownPointingCards";
}

PointingKeywords pointingCardKeywords(const PointingCards cards) noexcept
{
    switch (cards)
    {
    case PointingCards::ReferenceValue:
        return {.rightAscension = "CRVAL1", .declination = "CRVAL2"};
    case PointingCards::Pointing:
        return {.rightAscension = "RA", .declination = "DEC"};
    case PointingCards::Object:
        return {.rightAscension = "OBJCTRA", .declination = "OBJCTDEC"};
    case PointingCards::ObjectShort:
        return {.rightAscension = "OBJRA", .declination = "OBJDEC"};
    case PointingCards::Telescope:
        return {.rightAscension = "TELRA", .declination = "TELDEC"};
    }
    return {};
}

std::span<const PointingCards> plateSolveHintCards() noexcept
{
    return kPlateSolveHintCards;
}

std::span<const PointingCards> observedPointingCards() noexcept
{
    return kObservedPointingCards;
}

Result<PointingHint> readPointingHint(const std::span<const CodecCard> cards,
                                      const std::span<const PointingCards> order)
{
    for (const PointingCards pair : order)
    {
        if (!isPointingCards(pair))
        {
            return argumentFailure(kReadPointingOperation, "order", "a pointing pair is outside its enumeration");
        }
    }
    for (const PointingCards pair : order)
    {
        const PointingKeywords keywords = pointingCardKeywords(pair);
        const CodecCard* const rightAscension = findFitsCard(cards, keywords.rightAscension);
        const CodecCard* const declination = findFitsCard(cards, keywords.declination);
        if (rightAscension == nullptr || declination == nullptr)
        {
            continue;
        }
        const Result<double> ra = parseRightAscensionDegrees(rightAscension->value());
        const Result<double> dec = parseDeclinationDegrees(declination->value());
        if (ra.hasValue() && dec.hasValue())
        {
            return PointingHint{.direction = {.rightAscensionDegrees = *ra, .declinationDegrees = *dec},
                                .source = pair};
        }
    }
    return hintFailure(ErrorCode::EmptySelection, kReadPointingOperation, "no pair of the order states a pointing");
}

OpticalHints readOpticalHints(const std::span<const CodecCard> cards) noexcept
{
    return OpticalHints{.focalLengthMillimetres = firstPositiveNumber(cards, kFocalLengthKeywords),
                        .pixelSizeMicrometres = firstPositiveNumber(cards, kPixelSizeKeywords),
                        .drizzleScaleArcsecPerPixel = firstPositiveNumber(cards, kDrizzleScaleKeywords),
                        .drizzleMultiplier = firstPositiveNumber(cards, kDrizzleMultiplierKeywords)};
}

Result<double> computePlateScaleArcsecPerPixel(const double pixelSizeMicrometres, const double focalLengthMillimetres)
{
    if (!finitePositive(pixelSizeMicrometres))
    {
        return argumentFailure(kPlateScaleOperation, "pixelSizeMicrometres",
                               "a pixel size is a finite positive number");
    }
    if (!finitePositive(focalLengthMillimetres))
    {
        return argumentFailure(kPlateScaleOperation, "focalLengthMillimetres",
                               "a focal length is a finite positive number");
    }
    const double scale = kArcsecondsPerRadian * pixelSizeMicrometres / (1000.0 * focalLengthMillimetres);
    if (!finitePositive(scale))
    {
        return hintFailure(ErrorCode::OutOfRange, kPlateScaleOperation, "the pixel scale is not a finite number");
    }
    return scale;
}

Result<double> computeFocalLengthMillimetres(const double pixelSizeMicrometres, const double plateScaleArcsecPerPixel)
{
    if (!finitePositive(pixelSizeMicrometres))
    {
        return argumentFailure(kFocalLengthOperation, "pixelSizeMicrometres",
                               "a pixel size is a finite positive number");
    }
    if (!finitePositive(plateScaleArcsecPerPixel))
    {
        return argumentFailure(kFocalLengthOperation, "plateScaleArcsecPerPixel",
                               "a pixel scale is a finite positive number");
    }
    const double focalLength = kArcsecondsPerRadian * pixelSizeMicrometres / (1000.0 * plateScaleArcsecPerPixel);
    if (!finitePositive(focalLength))
    {
        return hintFailure(ErrorCode::OutOfRange, kFocalLengthOperation, "the focal length is not a finite number");
    }
    return focalLength;
}

Result<double> resolveEffectivePixelSizeMicrometres(const OpticalHints& optics)
{
    if (const Status checked = checkOptics(optics, kEffectivePixelOperation); checked.failed())
    {
        return checked;
    }
    return effectivePixelSize(optics, kEffectivePixelOperation);
}

Result<double> resolvePlateScaleHint(const OpticalHints& optics)
{
    if (const Status checked = checkOptics(optics, kScaleHintOperation); checked.failed())
    {
        return checked;
    }
    if (optics.drizzleScaleArcsecPerPixel.has_value())
    {
        return *optics.drizzleScaleArcsecPerPixel;
    }
    if (!optics.pixelSizeMicrometres.has_value() || !optics.focalLengthMillimetres.has_value())
    {
        return hintFailure(ErrorCode::EmptySelection, kScaleHintOperation,
                           "neither a drizzle scale nor both a pixel size and a focal length are stated");
    }
    const Result<double> pixel = effectivePixelSize(optics, kScaleHintOperation);
    if (!pixel.hasValue())
    {
        return pixel.status();
    }
    return computePlateScaleArcsecPerPixel(*pixel, *optics.focalLengthMillimetres);
}

double julianEpochYear(const UtcInstant& instant) noexcept
{
    // Whole seconds and the fraction are separated first, so the subtraction is exact in integers and
    // no instant a 64-bit count can hold overflows it.
    const std::int64_t seconds = instant.nanosecondsSinceEpoch / kNanosecondsPerSecond;
    const std::int64_t nanoseconds = instant.nanosecondsSinceEpoch % kNanosecondsPerSecond;
    const double elapsed = static_cast<double>(seconds - kJ2000UnixSeconds) +
                           static_cast<double>(nanoseconds) / static_cast<double>(kNanosecondsPerSecond);
    return 2000.0 + elapsed / kSecondsPerJulianYear;
}

Result<SkyCoordinate> sphericalMeanDirection(const std::span<const SkyCoordinate> directions)
{
    if (directions.empty())
    {
        return hintFailure(ErrorCode::EmptySelection, kMeanDirectionOperation, "no direction to take the mean of");
    }
    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;
    for (std::size_t index = 0u; index < directions.size(); ++index)
    {
        const SkyCoordinate& direction = directions[index];
        if (!isSkyCoordinate(direction))
        {
            return Status::failure(
                ErrorCode::InvalidArgument,
                StatusDetail{.message = "a direction is not a finite in-range sky direction",
                             .operation = kMeanDirectionOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofCount(ContextKey::Completed, index)}});
        }
        const double rightAscension = direction.rightAscensionDegrees * kRadiansPerDegree;
        const double declination = direction.declinationDegrees * kRadiansPerDegree;
        const double cosineDeclination = std::cos(declination);
        sumX += cosineDeclination * std::cos(rightAscension);
        sumY += cosineDeclination * std::sin(rightAscension);
        sumZ += std::sin(declination);
    }
    const double length = std::hypot(std::hypot(sumX, sumY), sumZ);
    if (!std::isfinite(length) || length < kMinimumMeanResultant * static_cast<double>(directions.size()))
    {
        return hintFailure(ErrorCode::InvalidData, kMeanDirectionOperation,
                           "the directions cancel, so they have no mean direction");
    }
    return SkyCoordinate{.rightAscensionDegrees =
                             normalizeRightAscensionDegrees(std::atan2(sumY, sumX) * kDegreesPerRadian),
                         .declinationDegrees = std::atan2(sumZ, std::hypot(sumX, sumY)) * kDegreesPerRadian};
}

} // namespace an
