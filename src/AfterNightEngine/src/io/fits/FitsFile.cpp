// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - opening a FITS file, translating what OpenFITS throws, the sample type table, and the version
// report of the FITS library.

#include "io/fits/FitsFile.h"

#include "runtime/DependencyVersions.h"

#include <AfterNightEngine/Image/CodecMetadata.h>

#include <openfits/limits.h>
#include <openfits/version.h>

#include <algorithm>
#include <string>
#include <utility>

namespace an
{
namespace
{
/// The most bytes one header of a file may hold: far beyond the records of `kMaxCodecCards` cards, which is all of a
/// header an image keeps, and beyond the headers of the files of every instrument and capture program in use. The
/// library keeps the headers of the units it walks, so a hostile file is held to these two bounds rather than to the
/// library's own, which suit a service that holds many files' headers at once.
constexpr std::uint64_t kMaxFitsHeaderBytes = std::uint64_t{64} << 20u;

/// The most bytes the headers of every unit walked may hold together.
constexpr std::uint64_t kMaxFitsTotalHeaderBytes = std::uint64_t{256} << 20u;

/// The engine classification of an exception class and code, and the reason it refines it with.
struct Classification
{
    ErrorCode code;
    CodecFailure reason;
};

/// What one library failure means here, by its class and code.
Classification classify(const openfits::error& error, FitsStage stage) noexcept
{
    const auto readReason =
        stage == FitsStage::Open ? CodecFailure::MetadataUnreadable : CodecFailure::PayloadUnreadable;
    if (const auto* const io = dynamic_cast<const openfits::io_error*>(&error); io != nullptr)
    {
        const auto code = io->code();
        if (stage == FitsStage::Write)
        {
            if (code == openfits::errc::open_failed || code == openfits::errc::not_a_regular_file)
                return {ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable};
            return {ErrorCode::ExternalFailure, CodecFailure::ShortWrite};
        }
        if (code == openfits::errc::end_of_data)
            return {ErrorCode::InvalidData, readReason};
        if (code == openfits::errc::read_failed && stage == FitsStage::Read)
            return {ErrorCode::InvalidData, CodecFailure::PayloadUnreadable};
        return {ErrorCode::ExternalFailure, readReason};
    }
    if (dynamic_cast<const openfits::integrity_error*>(&error) != nullptr)
        return {ErrorCode::InvalidData, CodecFailure::PayloadUnreadable};
    if (dynamic_cast<const openfits::unsupported_error*>(&error) != nullptr)
        return {ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation};
    if (dynamic_cast<const openfits::limit_error*>(&error) != nullptr)
        return {ErrorCode::LimitExceeded, readReason};
    if (dynamic_cast<const openfits::validation_error*>(&error) != nullptr)
        return {ErrorCode::InvalidArgument, CodecFailure::InvalidMetadata};
    if (dynamic_cast<const openfits::cancelled_error*>(&error) != nullptr)
        return {ErrorCode::Cancelled, readReason};
    if (dynamic_cast<const openfits::usage_error*>(&error) != nullptr)
        return {ErrorCode::Internal, CodecFailure::CallbackFailure};
    // Every other class the library throws is input that departs from the standard.
    return {ErrorCode::InvalidData, stage == FitsStage::Write ? CodecFailure::CallbackFailure : readReason};
}

/// What a failure of the file itself says, by its code alone.
///
/// The library's own text names the file and adds the operating system's description of the failure, which is in
/// the system's language and differs from one system to the next; the path travels as the failure's context instead,
/// so the same failure reads the same on every host.
/// @param code the library's code.
/// @return the description, or null for a code that names no failure of a file.
const char* fileFailureText(openfits::errc code) noexcept
{
    switch (code)
    {
    case openfits::errc::open_failed:
        return "the file could not be opened";
    case openfits::errc::not_a_regular_file:
        return "the path names something other than a regular file";
    case openfits::errc::read_failed:
        return "the file could not be read";
    case openfits::errc::end_of_data:
        return "the file ends before the data its header describes";
    case openfits::errc::write_failed:
        return "the file could not be written";
    case openfits::errc::file_exists:
        return "a file already stands at the path";
    default:
        return nullptr;
    }
}
} // namespace

Status fitsFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message)
{
    return codecFailure(
        code, reason, CodecFormat::Fits, operation, path,
        Status::failure(
            code, {.message = std::string(message), .operation = std::string(operation), .domain = ErrorDomain::Io}));
}

Status fitsLibraryFailure(const openfits::error& error, FitsStage stage, std::string_view operation,
                          std::string_view path)
{
    const auto [code, reason] = classify(error, stage);
    const char* const fileText =
        dynamic_cast<const openfits::io_error*>(&error) != nullptr ? fileFailureText(error.code()) : nullptr;
    auto cause = translateExternalCode(kFitsBoundary, code, static_cast<std::int64_t>(error.code()),
                                       fileText != nullptr ? fileText : error.what());
    // A model the writer refused names the keyword it would have been written as, which is what a caller fixes.
    if (code == ErrorCode::InvalidArgument && !error.context().keyword.empty())
        cause = Status::failure(code,
                                {.message = std::string(error.what()),
                                 .operation = std::string(operation),
                                 .domain = ErrorDomain::Io,
                                 .context = {StatusContextField::ofText(ContextKey::Parameter, error.context().keyword),
                                             StatusContextField::ofText(ContextKey::Library, "OpenFITS")}});
    return codecFailure(code, reason, CodecFormat::Fits, operation, path, std::move(cause));
}

Result<FitsHandle> openFits(std::string_view path, const EngineLimits& limits)
{
    auto opened = evaluateAcrossBoundary(kFitsBoundary, [&]() -> Result<FitsHandle> {
        openfits::read_options options;
        // A unit of the file that cannot be used - a header that gives no size, a damaged compressed unit - leaves the
        // units before it readable, as the library walks them; the selected image's own problem is the one a read
        // reports.
        options.strict = false;
        // A tile the library decodes and the content of a gzip-compressed file it inflates stay outside the engine's
        // budget, so no more of either than one image may hold is allocated; nothing is inflated into a temporary
        // file, since no directory is named for one.
        options.limits.max_allocation = limits.maxImageBytes;
        options.limits.max_inflated_bytes = (std::min)(options.limits.max_inflated_bytes, limits.maxImageBytes);
        options.limits.max_tile_bytes = (std::min)(options.limits.max_tile_bytes, limits.maxImageBytes);
        options.limits.max_header_bytes = kMaxFitsHeaderBytes;
        options.limits.max_total_header_bytes = kMaxFitsTotalHeaderBytes;
        // One unit past the bound a probe refuses at, so that a file with more units than an image index reaches is
        // told from one with exactly as many.
        options.limits.max_hdus = kMaxCodecCards + 1u;
        try
        {
            return FitsHandle{std::make_unique<openfits::reader>(path, options)};
        }
        catch (const openfits::error& error)
        {
            return fitsLibraryFailure(error, FitsStage::Open, "open FITS", path);
        }
    });
    if (!opened.hasValue())
        return codecFailure(opened.status().code(), CodecFailure::MetadataUnreadable, CodecFormat::Fits, "open FITS",
                            path, opened.status());
    if (!opened->hasValue())
        return opened->status();
    return std::move(**opened);
}

std::optional<openfits::numeric_type> fitsNumericType(RawSampleFormat format) noexcept
{
    if (format.sampleClass == RawSampleClass::FloatingPoint)
    {
        if (format.sizeBytes == 4u)
            return openfits::numeric_type::float32;
        if (format.sizeBytes == 8u)
            return openfits::numeric_type::float64;
        return std::nullopt;
    }
    const bool isSigned = format.sampleClass == RawSampleClass::SignedInteger;
    if (!isSigned && format.sampleClass != RawSampleClass::UnsignedInteger)
        return std::nullopt;
    switch (format.sizeBytes)
    {
    case 1u:
        return isSigned ? openfits::numeric_type::int8 : openfits::numeric_type::uint8;
    case 2u:
        return isSigned ? openfits::numeric_type::int16 : openfits::numeric_type::uint16;
    case 4u:
        return isSigned ? openfits::numeric_type::int32 : openfits::numeric_type::uint32;
    case 8u:
        return isSigned ? openfits::numeric_type::int64 : openfits::numeric_type::uint64;
    default:
        return std::nullopt;
    }
}

namespace dependency_detail
{

void reportOpenFits(DependencyVersion& entry) noexcept
{
    entry.name = "openfits";
    // The shared library its own release builds, beside the engine on both platforms.
    entry.linkage = DependencyLinkage::SharedLibrary;
    setVersionTextFromParts(entry.compiled, OPENFITS_VERSION_MAJOR, OPENFITS_VERSION_MINOR, OPENFITS_VERSION_PATCH);
    // The loaded copy is asked for its numbers rather than its text: the text is a view the library does not promise
    // to terminate, and the numbers are what it says to compare.
    const openfits::version_info loaded = openfits::version_numbers();
    setVersionTextFromParts(entry.loaded, static_cast<std::uint32_t>(loaded.major),
                            static_cast<std::uint32_t>(loaded.minor), static_cast<std::uint32_t>(loaded.patch));
}

} // namespace dependency_detail

} // namespace an
