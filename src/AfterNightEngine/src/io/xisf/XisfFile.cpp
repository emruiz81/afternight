// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - opening an XISF unit, translating what OpenXISF throws, the sample format tables, the text forms
// of the cards that carry the image's structural declarations, and the version report of the XISF library.

#include "io/xisf/XisfFile.h"

#include "runtime/DependencyVersions.h"

#include <openxisf/limits.h>
#include <openxisf/version.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <utility>

namespace an
{
namespace
{
/// The largest side of a filter array's repeating block a card is read back as.
constexpr std::size_t kMaxXisfFilterBlock = 16u;

/// The text of one number field, or nothing when it is not one.
std::optional<double> parseNumber(std::string_view field)
{
    double value = 0.0;
    const auto* const last = field.data() + field.size();
    const auto parsed = std::from_chars(field.data(), last, value);
    if (field.empty() || parsed.ec != std::errc{} || parsed.ptr != last)
        return std::nullopt;
    return value;
}

/// The fields of a text separated by one character, the last one taking the rest of the text when `count` limits them.
std::vector<std::string_view> split(std::string_view text, char separator, std::size_t count = 0u)
{
    std::vector<std::string_view> fields;
    while (true)
    {
        const auto at = text.find(separator);
        if (at == std::string_view::npos || (count != 0u && fields.size() + 1u == count))
        {
            fields.push_back(text);
            return fields;
        }
        fields.push_back(text.substr(0u, at));
        text.remove_prefix(at + 1u);
    }
}

/// `count` comma-separated numbers, or nothing when the text holds another number of them or one is not a number.
template <std::size_t Count> std::optional<std::array<double, Count>> parseNumbers(std::string_view text)
{
    const auto fields = split(text, ',');
    if (fields.size() != Count)
        return std::nullopt;
    std::array<double, Count> values{};
    for (std::size_t index = 0u; index < Count; ++index)
    {
        const auto value = parseNumber(fields[index]);
        if (!value)
            return std::nullopt;
        values[index] = *value;
    }
    return values;
}

/// Numbers joined by commas, each in its shortest exact text.
template <std::size_t Count> std::string numbersText(const std::array<double, Count>& values)
{
    std::string text;
    for (std::size_t index = 0u; index < Count; ++index)
        text += (index == 0u ? "" : ",") + xisfNumberText(values[index]);
    return text;
}

/// The engine classification of an exception class and code, and the reason it refines it with.
struct Classification
{
    ErrorCode code;
    CodecFailure reason;
};

/// What one library failure means here, by its class and code.
Classification classify(const openxisf::error& error, XisfStage stage) noexcept
{
    const auto readReason =
        stage == XisfStage::Open ? CodecFailure::MetadataUnreadable : CodecFailure::PayloadUnreadable;
    if (const auto* const io = dynamic_cast<const openxisf::io_error*>(&error); io != nullptr)
    {
        const auto code = io->code();
        if (stage == XisfStage::Write)
        {
            if (code == openxisf::errc::open_failed || code == openxisf::errc::not_a_regular_file)
                return {ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable};
            return {ErrorCode::ExternalFailure, CodecFailure::ShortWrite};
        }
        if (code == openxisf::errc::end_of_data)
            return {ErrorCode::InvalidData, readReason};
        if (code == openxisf::errc::read_failed && stage == XisfStage::Read)
            return {ErrorCode::InvalidData, CodecFailure::PayloadUnreadable};
        return {ErrorCode::ExternalFailure, readReason};
    }
    if (dynamic_cast<const openxisf::integrity_error*>(&error) != nullptr)
        return {ErrorCode::InvalidData, CodecFailure::PayloadUnreadable};
    if (dynamic_cast<const openxisf::unsupported_error*>(&error) != nullptr)
        return {ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation};
    if (dynamic_cast<const openxisf::limit_error*>(&error) != nullptr)
        return {ErrorCode::LimitExceeded, readReason};
    if (dynamic_cast<const openxisf::validation_error*>(&error) != nullptr)
        return {ErrorCode::InvalidArgument, CodecFailure::InvalidMetadata};
    if (dynamic_cast<const openxisf::cancelled_error*>(&error) != nullptr)
        return {ErrorCode::Cancelled, readReason};
    if (dynamic_cast<const openxisf::usage_error*>(&error) != nullptr)
        return {ErrorCode::Internal, CodecFailure::CallbackFailure};
    // Every other class the library throws is input that violates the specification.
    return {ErrorCode::InvalidData, stage == XisfStage::Write ? CodecFailure::CallbackFailure : readReason};
}
} // namespace

Status xisfFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message)
{
    return codecFailure(
        code, reason, CodecFormat::Xisf, operation, path,
        Status::failure(
            code, {.message = std::string(message), .operation = std::string(operation), .domain = ErrorDomain::Io}));
}

Status xisfLibraryFailure(const openxisf::error& error, XisfStage stage, std::string_view operation,
                          std::string_view path)
{
    const auto [code, reason] = classify(error, stage);
    auto cause = translateExternalCode(kXisfBoundary, code, static_cast<std::int64_t>(error.code()), error.what());
    // A model the writer refused names the element it would have been written as, which is what a caller fixes.
    if (code == ErrorCode::InvalidArgument && !error.context().element.empty())
        cause = Status::failure(code,
                                {.message = std::string(error.what()),
                                 .operation = std::string(operation),
                                 .domain = ErrorDomain::Io,
                                 .context = {StatusContextField::ofText(ContextKey::Parameter, error.context().element),
                                             StatusContextField::ofText(ContextKey::Library, "OpenXISF")}});
    return codecFailure(code, reason, CodecFormat::Xisf, operation, path, std::move(cause));
}

Result<XisfHandle> openXisf(std::string_view path, const EngineLimits& limits, bool headerOnly)
{
    auto opened = evaluateAcrossBoundary(kXisfBoundary, [&]() -> Result<XisfHandle> {
        openxisf::read_options options;
        // An object of the unit that cannot be used - an image whose block is compressed with a codec the library
        // does not have, a thumbnail that fails its checksum - leaves the rest of the unit readable, as the library
        // lists it; the selected image's own problem is the one a decode reports.
        options.strict = false;
        options.header_only = headerOnly;
        options.limits.max_header_size = kMaxXisfHeaderBytes;
        options.limits.max_allocation = limits.maxImageBytes;
        // The ancillary data a full open loads stay outside the engine's budget, so no more of them than one image
        // may hold is loaded.
        options.limits.max_ancillary_data = (std::min)(options.limits.max_ancillary_data, limits.maxImageBytes);
        try
        {
            return XisfHandle{std::make_unique<openxisf::reader>(path, std::move(options))};
        }
        catch (const openxisf::error& error)
        {
            return xisfLibraryFailure(error, XisfStage::Open, "open XISF", path);
        }
    });
    if (!opened.hasValue())
        return codecFailure(opened.status().code(), CodecFailure::MetadataUnreadable, CodecFormat::Xisf, "open XISF",
                            path, opened.status());
    if (!opened->hasValue())
        return opened->status();
    return std::move(**opened);
}

std::optional<RawSampleFormat> xisfSampleFormat(openxisf::sample_format format) noexcept
{
    switch (format)
    {
    case openxisf::sample_format::uint8:
        return RawSampleFormat{1u, RawSampleClass::UnsignedInteger};
    case openxisf::sample_format::uint16:
        return RawSampleFormat{2u, RawSampleClass::UnsignedInteger};
    case openxisf::sample_format::uint32:
        return RawSampleFormat{4u, RawSampleClass::UnsignedInteger};
    case openxisf::sample_format::uint64:
        return RawSampleFormat{8u, RawSampleClass::UnsignedInteger};
    case openxisf::sample_format::float32:
        return RawSampleFormat{4u, RawSampleClass::FloatingPoint};
    case openxisf::sample_format::float64:
        return RawSampleFormat{8u, RawSampleClass::FloatingPoint};
    case openxisf::sample_format::complex32:
    case openxisf::sample_format::complex64:
        break;
    }
    return std::nullopt;
}

std::optional<openxisf::sample_format> xisfSampleFormatOf(const RawSampleFormat& format) noexcept
{
    if (format.sampleClass == RawSampleClass::UnsignedInteger)
    {
        switch (format.sizeBytes)
        {
        case 1u:
            return openxisf::sample_format::uint8;
        case 2u:
            return openxisf::sample_format::uint16;
        case 4u:
            return openxisf::sample_format::uint32;
        case 8u:
            return openxisf::sample_format::uint64;
        default:
            return std::nullopt;
        }
    }
    if (format.sampleClass == RawSampleClass::FloatingPoint)
    {
        if (format.sizeBytes == 4u)
            return openxisf::sample_format::float32;
        if (format.sizeBytes == 8u)
            return openxisf::sample_format::float64;
    }
    // XISF names no signed integer representation and no one-byte floating one. Such a frame is
    // refused rather than reinterpreted through an offset nobody asked for.
    return std::nullopt;
}

std::string xisfNumberText(double value)
{
    std::array<char, 32> text{};
    const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
    return written.ec == std::errc{} ? std::string(text.data(), written.ptr) : std::string{};
}

std::string xisfFilterArrayText(const openxisf::color_filter_array& filters)
{
    auto text = std::to_string(filters.width) + "x" + std::to_string(filters.height) + ":" + filters.pattern;
    if (!filters.name.empty())
        text += ";" + filters.name;
    return text;
}

std::optional<openxisf::color_filter_array> parseXisfFilterArray(std::string_view text)
{
    const auto cross = text.find('x');
    const auto colon = text.find(':');
    if (cross == std::string_view::npos || colon == std::string_view::npos || cross >= colon)
        return std::nullopt;
    const auto number = [](std::string_view field) -> std::optional<std::size_t> {
        std::size_t value = 0u;
        const auto* const last = field.data() + field.size();
        const auto parsed = std::from_chars(field.data(), last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last || value == 0u || value > kMaxXisfFilterBlock)
            return std::nullopt;
        return value;
    };
    const auto width = number(text.substr(0u, cross));
    const auto height = number(text.substr(cross + 1u, colon - cross - 1u));
    if (!width || !height)
        return std::nullopt;
    // The letters of a filter array never hold a semicolon, so the first one after them starts the array's name.
    auto pattern = text.substr(colon + 1u);
    std::string_view name;
    if (const auto semicolon = pattern.find(';'); semicolon != std::string_view::npos)
    {
        name = pattern.substr(semicolon + 1u);
        pattern = pattern.substr(0u, semicolon);
    }
    if (pattern.size() != *width * *height)
        return std::nullopt;
    return openxisf::color_filter_array{
        .pattern = std::string(pattern), .width = *width, .height = *height, .name = std::string(name)};
}

std::string xisfWorkingSpaceText(const openxisf::rgb_working_space& space)
{
    return (space.gamma ? xisfNumberText(*space.gamma) : std::string("sRGB")) + ";" + numbersText(space.x) + ";" +
           numbersText(space.y) + ";" + numbersText(space.luminance) + ";" + space.name;
}

std::optional<openxisf::rgb_working_space> parseXisfWorkingSpace(std::string_view text)
{
    const auto fields = split(text, ';', 5u);
    if (fields.size() != 5u)
        return std::nullopt;
    openxisf::rgb_working_space space;
    if (fields[0] != "sRGB")
    {
        space.gamma = parseNumber(fields[0]);
        if (!space.gamma)
            return std::nullopt;
    }
    const auto x = parseNumbers<3u>(fields[1]);
    const auto y = parseNumbers<3u>(fields[2]);
    const auto luminance = parseNumbers<3u>(fields[3]);
    if (!x || !y || !luminance)
        return std::nullopt;
    space.x = *x;
    space.y = *y;
    space.luminance = *luminance;
    space.name = std::string(fields[4]);
    return space;
}

std::string xisfDisplayFunctionText(const openxisf::display_function& function)
{
    return numbersText(function.midtones) + ";" + numbersText(function.shadows) + ";" +
           numbersText(function.highlights) + ";" + numbersText(function.shadows_expansion) + ";" +
           numbersText(function.highlights_expansion) + ";" + function.name;
}

std::optional<openxisf::display_function> parseXisfDisplayFunction(std::string_view text)
{
    const auto fields = split(text, ';', 6u);
    if (fields.size() != 6u)
        return std::nullopt;
    const auto midtones = parseNumbers<4u>(fields[0]);
    const auto shadows = parseNumbers<4u>(fields[1]);
    const auto highlights = parseNumbers<4u>(fields[2]);
    const auto low = parseNumbers<4u>(fields[3]);
    const auto high = parseNumbers<4u>(fields[4]);
    if (!midtones || !shadows || !highlights || !low || !high)
        return std::nullopt;
    return openxisf::display_function{.midtones = *midtones,
                                      .shadows = *shadows,
                                      .highlights = *highlights,
                                      .shadows_expansion = *low,
                                      .highlights_expansion = *high,
                                      .name = std::string(fields[5])};
}

std::string xisfResolutionText(const openxisf::resolution& resolution)
{
    return xisfNumberText(resolution.horizontal) + ";" + xisfNumberText(resolution.vertical) + ";" +
           std::string(openxisf::resolution_unit_name(resolution.unit));
}

std::optional<openxisf::resolution> parseXisfResolution(std::string_view text)
{
    const auto fields = split(text, ';');
    if (fields.size() != 3u)
        return std::nullopt;
    const auto horizontal = parseNumber(fields[0]);
    const auto vertical = parseNumber(fields[1]);
    if (!horizontal || !vertical)
        return std::nullopt;
    for (const auto unit : {openxisf::resolution_unit::inch, openxisf::resolution_unit::centimeter})
        if (fields[2] == openxisf::resolution_unit_name(unit))
            return openxisf::resolution{.horizontal = *horizontal, .vertical = *vertical, .unit = unit};
    return std::nullopt;
}

namespace dependency_detail
{

void reportOpenXisf(DependencyVersion& entry) noexcept
{
    entry.name = "openxisf";
    // Windows links the packaged DLL. Linux links the packaged position-independent archive, as it
    // links CFITSIO's, so the copy inside this binary is fixed at build time there and replaceable here.
#if defined(_WIN32)
    entry.linkage = DependencyLinkage::SharedLibrary;
#else
    entry.linkage = DependencyLinkage::StaticArchive;
#endif
    setVersionTextFromParts(entry.compiled, OPENXISF_VERSION_MAJOR, OPENXISF_VERSION_MINOR, OPENXISF_VERSION_PATCH);
    // The loaded copy is asked for its numbers rather than its text: the text is a view the library
    // does not promise to terminate, and the numbers are what it says to compare.
    const openxisf::version_info loaded = openxisf::version_numbers();
    setVersionTextFromParts(entry.loaded, static_cast<std::uint32_t>(loaded.major),
                            static_cast<std::uint32_t>(loaded.minor), static_cast<std::uint32_t>(loaded.patch));
}

} // namespace dependency_detail

} // namespace an
