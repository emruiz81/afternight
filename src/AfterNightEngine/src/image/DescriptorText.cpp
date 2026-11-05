// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/DescriptorText.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// Writing and reading the one-line form.
//
// Every enumerated value is written through the same name function a log line uses, and read by
// comparing against that same function over the enumeration. That is deliberately not a second
// table: a table here would be a second place for a spelling to live, and the first symptom of the
// two disagreeing would be a fixture that stopped parsing months after the enumerator was renamed.
// Comparing against the name function costs a linear scan over at most eleven short strings, on a
// path that exists for tests and diagnostics.
//
// The parser reads every field into a borrowed piece of the caller's text first and interprets them
// afterwards, in the order the canonical form writes them. Two properties come out of that: the
// fields may be given in any order, and a line with two problems reports the same one on every
// build, because the reporting order is the declaration order rather than whatever order the caller
// happened to write.

namespace an
{
namespace
{

/// The fields of the canonical form, in the order it writes them.
enum class Field : std::size_t
{
    Extent,
    Sample,
    Channels,
    Color,
    Domain,
    Cfa,
    Origin,
    Aspect
};

/// How many fields the canonical form has.
constexpr std::size_t kFieldCount = static_cast<std::size_t>(Field::Aspect) + 1u;

/// The key each field is written under, indexed by the field.
constexpr std::array<std::string_view, kFieldCount> kFieldKeys = {"extent", "sample", "channels", "color",
                                                                  "domain", "cfa",    "origin",   "aspect"};

/// Every sample representation, for reading a name back into a value.
constexpr std::array<SampleType, 8> kSampleTypes = {SampleType::UInt8,   SampleType::Int8,   SampleType::UInt16,
                                                    SampleType::Int16,   SampleType::UInt32, SampleType::Int32,
                                                    SampleType::Float32, SampleType::Float64};

/// Every channel schema, for reading a name back into a value.
constexpr std::array<ChannelSchema, 11> kChannelSchemas = {
    ChannelSchema::Mono, ChannelSchema::Rgb,   ChannelSchema::Rgba, ChannelSchema::Xyz,
    ChannelSchema::Lab,  ChannelSchema::Lch,   ChannelSchema::Hsl,  ChannelSchema::Hsv,
    ChannelSchema::Hsi,  ChannelSchema::YCbCr, ChannelSchema::Named};

/// Every colour basis, for reading a name back into a value.
constexpr std::array<ColorSpace, 6> kColorSpaces = {ColorSpace::Unspecified, ColorSpace::LinearRgb,
                                                    ColorSpace::DisplayRgb,  ColorSpace::CieXyz,
                                                    ColorSpace::CieLab,      ColorSpace::NarrowbandMapping};

/// Every numeric domain, for reading a name back into a value.
constexpr std::array<NumericDomain, 4> kNumericDomains = {NumericDomain::NormalizedLinear,
                                                          NumericDomain::EncodedNonlinear, NumericDomain::RawSensorCode,
                                                          NumericDomain::PhysicalScaled};

/// Every filter arrangement, for reading a name back into a value.
constexpr std::array<CfaPattern, 6> kCfaPatterns = {CfaPattern::None, CfaPattern::Rggb, CfaPattern::Bggr,
                                                    CfaPattern::Grbg, CfaPattern::Gbrg, CfaPattern::XTrans};

/// Every filter colour, for reading a table entry back into a value.
constexpr std::array<CfaColor, 3> kCfaColors = {CfaColor::Red, CfaColor::Green, CfaColor::Blue};

/// The letter one filter colour is written as inside a table.
///
/// A letter rather than a name, because a tabulated mosaic writes thirty-six of them and a line of
/// thirty-six names is not a line anybody reads. The letter is the first character of the name that
/// `cfaColorName` gives, rather than a second table, so a renamed enumerator changes both spellings
/// at once.
///
/// @param color the colour to write.
/// @return its letter.
[[nodiscard]] char cfaColorLetter(CfaColor color)
{
    return cfaColorName(color)[0];
}

/// Every coordinate origin, for reading a name back into a value.
constexpr std::array<CoordinateOrigin, 2> kCoordinateOrigins = {CoordinateOrigin::TopLeft,
                                                                CoordinateOrigin::BottomLeft};

/// What one parse produced: a reason, and a descriptor exactly when the reason is `None`.
struct ParseOutcome
{
    /// Why the text was refused, or `DescriptorError::None`.
    DescriptorError error = DescriptorError::MalformedText;

    /// The descriptor, present exactly when `error` is `DescriptorError::None`.
    std::optional<ImageDescriptor> descriptor{};
};

/// The value an enumeration name stands for, by asking the name function.
///
/// @param values every enumerator of the enumeration.
/// @param name the name function for it.
/// @param text the spelling to look up.
/// @return the matching enumerator, or an empty optional when no enumerator is spelled that way.
template <typename EnumT, std::size_t CountV>
[[nodiscard]] std::optional<EnumT> valueNamed(const std::array<EnumT, CountV>& values,
                                              const char* (*name)(EnumT) noexcept, std::string_view text)
{
    for (const EnumT value : values)
    {
        if (text == name(value))
        {
            return value;
        }
    }
    return std::nullopt;
}

/// A whole unsigned decimal number, with nothing before or after it.
///
/// @param text the spelling to read.
/// @return the number, or an empty optional when the text is not exactly one.
[[nodiscard]] std::optional<std::uint32_t> wholeNumber(std::string_view text)
{
    std::uint32_t value = 0u;
    const char* const first = text.data();
    const char* const last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last || text.empty())
    {
        return std::nullopt;
    }
    return value;
}

/// The text before the first occurrence of a separator, and the text after it.
///
/// @param text the text to split.
/// @param separator the character to split at.
/// @return the two halves, or an empty optional when the separator is absent or either half is
///         empty - both of which are the field being malformed rather than a value to interpret.
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> splitOnce(std::string_view text,
                                                                                     char separator)
{
    const std::size_t position = text.find(separator);
    if (position == std::string_view::npos || position == 0u || position + 1u == text.size())
    {
        return std::nullopt;
    }
    return std::pair<std::string_view, std::string_view>{text.substr(0u, position), text.substr(position + 1u)};
}

/// The dimensions written as `<width>x<height>x<channels>`.
///
/// @param text the field value.
/// @param limits the configured maxima the dimensions must satisfy.
/// @param extent set to the extent when this returns `DescriptorError::None`; untouched otherwise.
/// @return `None`, `MalformedText` when the three numbers are not there, or `ExtentRefused` when
///         they are and no extent can be built from them.
[[nodiscard]] DescriptorError readExtent(std::string_view text, const EngineLimits& limits,
                                         std::optional<ImageExtent>& extent)
{
    const std::optional<std::pair<std::string_view, std::string_view>> first = splitOnce(text, 'x');
    if (!first.has_value())
    {
        return DescriptorError::MalformedText;
    }
    const std::optional<std::pair<std::string_view, std::string_view>> second = splitOnce(first->second, 'x');
    if (!second.has_value())
    {
        return DescriptorError::MalformedText;
    }
    const std::optional<std::uint32_t> width = wholeNumber(first->first);
    const std::optional<std::uint32_t> height = wholeNumber(second->first);
    const std::optional<std::uint32_t> channelCount = wholeNumber(second->second);
    if (!width.has_value() || !height.has_value() || !channelCount.has_value())
    {
        return DescriptorError::MalformedText;
    }
    extent = ImageExtent::create(*width, *height, *channelCount, limits);
    return extent.has_value() ? DescriptorError::None : DescriptorError::ExtentRefused;
}

/// The thirty-six letters of a tabulated mosaic, read back into filter colours.
///
/// @param text the letters, which must be exactly `kCfaTableEntries` of them.
/// @param entries filled in when this returns `DescriptorError::None`; untouched otherwise.
/// @return `None`, `MalformedText` when the count is wrong, or `UnknownCfaColor` when a letter
///         stands for no filter colour.
[[nodiscard]] DescriptorError readCfaTable(std::string_view text, CfaColorEntries& entries)
{
    if (text.size() != kCfaTableEntries)
    {
        return DescriptorError::MalformedText;
    }
    CfaColorEntries read{};
    for (std::size_t index = 0u; index < kCfaTableEntries; ++index)
    {
        bool found = false;
        for (const CfaColor color : kCfaColors)
        {
            if (text[index] == cfaColorLetter(color))
            {
                read[index] = color;
                found = true;
                break;
            }
        }
        if (!found)
        {
            return DescriptorError::UnknownCfaColor;
        }
    }
    entries = read;
    return DescriptorError::None;
}

/// The filter array written as `<pattern>@<originX>,<originY>`, with `:<thirty-six letters>` after
/// the phase when the pattern is a tabulated one.
///
/// The table is part of this field rather than a field of its own, because it is part of the same
/// fact: a line naming a tabulated pattern without its entries describes no mosaic, and a line
/// carrying entries for a named pattern states the same thing twice.
///
/// @param text the field value.
/// @param cfa set to the filter array when this returns `DescriptorError::None`; untouched
///        otherwise.
/// @return `None`, `MalformedText` when the shape is wrong, `UnknownCfaPattern` when the pattern
///         names nothing, or whichever table or phase rule `CfaDescriptor::check` refused.
[[nodiscard]] DescriptorError readCfa(std::string_view text, CfaDescriptor& cfa)
{
    const std::optional<std::pair<std::string_view, std::string_view>> atSign = splitOnce(text, '@');
    if (!atSign.has_value())
    {
        return DescriptorError::MalformedText;
    }
    const std::optional<std::pair<std::string_view, std::string_view>> comma = splitOnce(atSign->second, ',');
    if (!comma.has_value())
    {
        return DescriptorError::MalformedText;
    }
    const std::optional<CfaPattern> pattern = valueNamed(kCfaPatterns, cfaPatternName, atSign->first);
    if (!pattern.has_value())
    {
        return DescriptorError::UnknownCfaPattern;
    }
    const std::optional<std::pair<std::string_view, std::string_view>> colon = splitOnce(comma->second, ':');
    const std::string_view originYText = colon.has_value() ? colon->first : comma->second;
    const std::optional<std::uint32_t> originX = wholeNumber(comma->first);
    const std::optional<std::uint32_t> originY = wholeNumber(originYText);
    if (!originX.has_value() || !originY.has_value())
    {
        return DescriptorError::MalformedText;
    }
    if (cfaPatternIsTabulated(*pattern))
    {
        if (!colon.has_value())
        {
            return DescriptorError::CfaTableRequired;
        }
        CfaColorEntries entries{};
        const DescriptorError tableError = readCfaTable(colon->second, entries);
        if (tableError != DescriptorError::None)
        {
            return tableError;
        }
        const std::optional<CfaDescriptor> parsed = CfaDescriptor::create(entries, *originX, *originY);
        if (!parsed.has_value())
        {
            return CfaDescriptor::check(entries, *originX, *originY);
        }
        cfa = *parsed;
        return DescriptorError::None;
    }
    if (colon.has_value())
    {
        return DescriptorError::CfaTableNotAllowed;
    }
    const std::optional<CfaDescriptor> parsed = CfaDescriptor::create(*pattern, *originX, *originY);
    if (!parsed.has_value())
    {
        return CfaDescriptor::check(*pattern, *originX, *originY);
    }
    cfa = *parsed;
    return DescriptorError::None;
}

/// The pixel aspect, read back exactly as it was written.
///
/// @param text the field value.
/// @param aspect set to the ratio when this returns true; untouched otherwise.
/// @return true when the text is exactly one floating-point number. Whether the number describes a
///         pixel is the descriptor's rule, not this one's.
[[nodiscard]] bool readAspect(std::string_view text, double& aspect)
{
    const char* const first = text.data();
    const char* const last = first + text.size();
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last || text.empty())
    {
        return false;
    }
    aspect = value;
    return true;
}

/// Splits the line into its fields, refusing anything that is not the form.
///
/// @param text the whole line.
/// @param values set to each field's borrowed value, indexed by field.
/// @return `None`, or the reason the line is not a set of exactly these fields.
[[nodiscard]] DescriptorError readFields(std::string_view text, std::array<std::string_view, kFieldCount>& values)
{
    std::array<bool, kFieldCount> seen{};
    std::size_t tokenCount = 0u;
    std::size_t position = 0u;
    while (position < text.size())
    {
        const std::size_t start = text.find_first_not_of(' ', position);
        if (start == std::string_view::npos)
        {
            break;
        }
        std::size_t end = text.find(' ', start);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        const std::string_view token = text.substr(start, end - start);
        position = end;
        ++tokenCount;

        const std::optional<std::pair<std::string_view, std::string_view>> pair = splitOnce(token, '=');
        if (!pair.has_value())
        {
            return DescriptorError::MalformedText;
        }
        std::size_t field = kFieldCount;
        for (std::size_t candidate = 0u; candidate < kFieldCount; ++candidate)
        {
            if (kFieldKeys[candidate] == pair->first)
            {
                field = candidate;
                break;
            }
        }
        if (field == kFieldCount)
        {
            return DescriptorError::UnknownField;
        }
        if (seen[field])
        {
            return DescriptorError::DuplicateField;
        }
        seen[field] = true;
        values[field] = pair->second;
    }

    if (tokenCount == 0u)
    {
        return DescriptorError::MalformedText;
    }
    for (const bool present : seen)
    {
        if (!present)
        {
            return DescriptorError::MissingField;
        }
    }
    return DescriptorError::None;
}

/// Reads a whole line into a descriptor, or into the reason it is not one.
///
/// @param text the line to read.
/// @param limits the configured maxima the dimensions must satisfy.
/// @return the outcome, whose descriptor is present exactly when its reason is
///         `DescriptorError::None`.
[[nodiscard]] ParseOutcome parseLine(std::string_view text, const EngineLimits& limits)
{
    std::array<std::string_view, kFieldCount> values{};
    const DescriptorError shape = readFields(text, values);
    if (shape != DescriptorError::None)
    {
        return ParseOutcome{shape, std::nullopt};
    }

    std::optional<ImageExtent> extent;
    const DescriptorError extentError = readExtent(values[static_cast<std::size_t>(Field::Extent)], limits, extent);
    if (extentError != DescriptorError::None)
    {
        return ParseOutcome{extentError, std::nullopt};
    }

    const std::optional<SampleType> sampleType =
        valueNamed(kSampleTypes, sampleTypeName, values[static_cast<std::size_t>(Field::Sample)]);
    if (!sampleType.has_value())
    {
        return ParseOutcome{DescriptorError::UnknownSampleType, std::nullopt};
    }
    const std::optional<ChannelSchema> channelSchema =
        valueNamed(kChannelSchemas, channelSchemaName, values[static_cast<std::size_t>(Field::Channels)]);
    if (!channelSchema.has_value())
    {
        return ParseOutcome{DescriptorError::UnknownChannelSchema, std::nullopt};
    }
    const std::optional<ColorSpace> colorSpace =
        valueNamed(kColorSpaces, colorSpaceName, values[static_cast<std::size_t>(Field::Color)]);
    if (!colorSpace.has_value())
    {
        return ParseOutcome{DescriptorError::UnknownColorSpace, std::nullopt};
    }
    const std::optional<NumericDomain> numericDomain =
        valueNamed(kNumericDomains, numericDomainName, values[static_cast<std::size_t>(Field::Domain)]);
    if (!numericDomain.has_value())
    {
        return ParseOutcome{DescriptorError::UnknownNumericDomain, std::nullopt};
    }

    CfaDescriptor cfa;
    const DescriptorError cfaError = readCfa(values[static_cast<std::size_t>(Field::Cfa)], cfa);
    if (cfaError != DescriptorError::None)
    {
        return ParseOutcome{cfaError, std::nullopt};
    }

    const std::optional<CoordinateOrigin> coordinateOrigin =
        valueNamed(kCoordinateOrigins, coordinateOriginName, values[static_cast<std::size_t>(Field::Origin)]);
    if (!coordinateOrigin.has_value())
    {
        return ParseOutcome{DescriptorError::UnknownCoordinateOrigin, std::nullopt};
    }

    double aspect = 0.0;
    if (!readAspect(values[static_cast<std::size_t>(Field::Aspect)], aspect))
    {
        return ParseOutcome{DescriptorError::MalformedText, std::nullopt};
    }

    const ImageDescriptorRequest request{.extent = *extent,
                                         .sampleType = *sampleType,
                                         .channelSchema = *channelSchema,
                                         .colorSpace = *colorSpace,
                                         .numericDomain = *numericDomain,
                                         .cfa = cfa,
                                         .coordinateOrigin = *coordinateOrigin,
                                         .pixelAspectRatio = aspect};
    const DescriptorError combination = ImageDescriptor::check(request);
    if (combination != DescriptorError::None)
    {
        return ParseOutcome{combination, std::nullopt};
    }
    return ParseOutcome{DescriptorError::None, ImageDescriptor::create(request)};
}

/// A number written so that reading it back produces the same number.
///
/// @param value the ratio to write.
/// @return the shortest decimal that round-trips, which is what makes the whole form lossless.
[[nodiscard]] std::string shortestDecimal(double value)
{
    // Twenty-four characters holds the longest shortest-round-trip form of any binary64 value - a
    // sign, seventeen significant digits, a point and a four-character exponent - so the conversion
    // cannot run out of room and there is no second path to test.
    std::array<char, 24> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

} // namespace

std::string formatImageDescriptor(const ImageDescriptor& descriptor)
{
    std::string text;
    text.reserve(128u);
    text += "extent=";
    text += std::to_string(descriptor.extent().width());
    text += 'x';
    text += std::to_string(descriptor.extent().height());
    text += 'x';
    text += std::to_string(descriptor.extent().channelCount());
    text += " sample=";
    text += sampleTypeName(descriptor.sampleType());
    text += " channels=";
    text += channelSchemaName(descriptor.channelSchema());
    text += " color=";
    text += colorSpaceName(descriptor.colorSpace());
    text += " domain=";
    text += numericDomainName(descriptor.numericDomain());
    text += " cfa=";
    text += cfaPatternName(descriptor.cfa().pattern());
    text += '@';
    text += std::to_string(descriptor.cfa().originX());
    text += ',';
    text += std::to_string(descriptor.cfa().originY());
    if (descriptor.cfa().hasFilterTable())
    {
        text += ':';
        for (std::size_t index = 0u; index < kCfaTableEntries; ++index)
        {
            text += cfaColorLetter(descriptor.cfa().filterTable().entries()[index]);
        }
    }
    text += " origin=";
    text += coordinateOriginName(descriptor.coordinateOrigin());
    text += " aspect=";
    text += shortestDecimal(descriptor.pixelAspectRatio());
    return text;
}

DescriptorError checkImageDescriptorText(std::string_view text, const EngineLimits& limits)
{
    return parseLine(text, limits).error;
}

std::optional<ImageDescriptor> parseImageDescriptor(std::string_view text, const EngineLimits& limits)
{
    return parseLine(text, limits).descriptor;
}

} // namespace an
