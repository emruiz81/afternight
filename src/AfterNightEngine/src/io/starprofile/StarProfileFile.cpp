// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading and publishing star-profile documents, and the version-one star
// ordering.
//
// A read sizes the file before it allocates, so an oversized document is refused without being read.
// A write serializes the whole document in memory, then writes it into a sequence output's pending
// payload under that output's writer claim and commits it, so the destination changes only by the
// output's atomic rename. The version-one sort is stable, so stars with equal keys keep their order.

#include <AfterNightEngine/IO/StarProfileFile.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "core/Utf8.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace an
{
namespace
{
[[nodiscard]] Status argument(const char* operation, const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           {.message = message,
                            .operation = operation,
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] Status fileFailure(const char* operation, const char* message, std::string_view path,
                                 std::int64_t code = 0)
{
    std::vector<StatusContextField> context{StatusContextField::ofText(ContextKey::Path, std::string(path))};
    if (code != 0)
        context.push_back(StatusContextField::ofNumber(ContextKey::LibraryCode, code));
    return Status::failure(
        ErrorCode::ExternalFailure,
        {.message = message, .operation = operation, .domain = ErrorDomain::Io, .context = std::move(context)});
}

[[nodiscard]] std::filesystem::path nativePath(std::string_view utf8)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

[[nodiscard]] double field(const StarProfileV1Star& star, StarProfileV1SortKey key) noexcept
{
    switch (key)
    {
    case StarProfileV1SortKey::Intensity:
        return star.intensity;
    case StarProfileV1SortKey::Percentage:
        return star.percentage;
    case StarProfileV1SortKey::DeltaRadius:
        return star.deltaRadius;
    case StarProfileV1SortKey::Quality:
        return star.quality;
    case StarProfileV1SortKey::MeanRadius:
        return star.meanRadius;
    case StarProfileV1SortKey::Radius:
        return star.radius;
    case StarProfileV1SortKey::PositionX:
        return star.x;
    case StarProfileV1SortKey::PositionY:
        return star.y;
    case StarProfileV1SortKey::LargeMajorAxis:
        return star.largeMajorAxis;
    case StarProfileV1SortKey::SmallMajorAxis:
        return star.smallMajorAxis;
    case StarProfileV1SortKey::LargeMinorAxis:
        return star.largeMinorAxis;
    case StarProfileV1SortKey::SmallMinorAxis:
        return star.smallMinorAxis;
    case StarProfileV1SortKey::MajorAxisAngle:
        return star.majorAxisAngle;
    }
    return 0.0;
}

[[nodiscard]] bool validLimits(const StarProfileReadLimits& limits) noexcept
{
    return limits.maximumFileBytes != 0u && limits.maximumStars != 0u && limits.maximumStringBytes != 0u;
}
} // namespace

Status sortStarProfileV1(std::vector<StarProfileV1Star>& stars, StarProfileV1SortKey key,
                         StarProfileSortDirection direction)
{
    if (static_cast<std::uint8_t>(key) > static_cast<std::uint8_t>(StarProfileV1SortKey::MajorAxisAngle) ||
        (direction != StarProfileSortDirection::Ascending && direction != StarProfileSortDirection::Descending))
        return Status::failure(ErrorCode::InvalidArgument,
                               {.message = "the version-one sort key and direction must name declared values",
                                .operation = "an::sortStarProfileV1",
                                .domain = ErrorDomain::Io});
    const bool ascending = direction == StarProfileSortDirection::Ascending;
    return callAcrossBoundary(
        {.library = "standard-library", .operation = "an::sortStarProfileV1", .domain = ErrorDomain::Io}, [&] {
            std::stable_sort(stars.begin(), stars.end(), [key, ascending](const auto& left, const auto& right) {
                return ascending ? field(left, key) < field(right, key) : field(left, key) > field(right, key);
            });
        });
}

Result<StarProfileFile> readStarProfileFile(const ExecutionContext& execution, std::string_view path,
                                            const StarProfileReadLimits& limits)
{
    if (!execution.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (!isBoundaryPath(path))
        return argument("an::readStarProfileFile", "a star-profile path is nonempty bounded UTF-8 without NUL", "path");
    if (!validLimits(limits))
        return argument("an::readStarProfileFile", "star-profile bounds are positive", "limits");
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    constexpr ExternalBoundary kBoundary{
        .library = "filesystem", .operation = "an::readStarProfileFile", .domain = ErrorDomain::Io};
    try
    {
        const std::filesystem::path native = nativePath(path);
        std::error_code sizeError;
        const std::uint64_t bytes = std::filesystem::file_size(native, sizeError);
        if (sizeError)
            return fileFailure("an::readStarProfileFile", "the star-profile file cannot be inspected", path,
                               sizeError.value());
        if (bytes > limits.maximumFileBytes)
            return Status::failure(
                ErrorCode::LimitExceeded,
                {.message = "the star-profile file exceeds the configured byte bound",
                 .operation = "an::readStarProfileFile",
                 .domain = ErrorDomain::Io,
                 .context = {StatusContextField::ofCount(ContextKey::Requested, bytes),
                             StatusContextField::ofCount(ContextKey::Limit, limits.maximumFileBytes)}});
        std::ifstream input(native, std::ios::binary);
        if (!input.is_open())
            return fileFailure("an::readStarProfileFile", "the star-profile file cannot be opened", path);
        std::string text(static_cast<std::size_t>(bytes), '\0');
        if (bytes != 0u)
            input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!input || static_cast<std::uint64_t>(input.gcount()) != bytes)
            return fileFailure("an::readStarProfileFile", "the star-profile file could not be read completely", path);
        auto parsed = parseStarProfileDocument(text, limits);
        if (!parsed)
            return parsed.status();
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        return std::move(*parsed);
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

Result<StarProfileWriteReport> writeStarProfileFile(const EngineContext& engine, const ExecutionContext& execution,
                                                    const StarProfileWriteRequest& request,
                                                    const StarProfileFile& profile)
{
    if (!engine.valid() || !execution.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (engine.id() != execution.engineContextId())
        return Status::failure(ErrorCode::IncompatibleArguments);
    if (!isBoundaryPath(request.destinationPath))
        return argument("an::writeStarProfileFile", "a star-profile destination is nonempty bounded UTF-8 without NUL",
                        "destinationPath");
    if ((request.durability != StarProfileDurability::FlushBeforePublish &&
         request.durability != StarProfileDurability::CloseOnly) ||
        (request.replacePolicy != StarProfileReplacePolicy::ReplaceExisting &&
         request.replacePolicy != StarProfileReplacePolicy::RefuseExisting))
    {
        return argument("an::writeStarProfileFile", "the durability or replacement policy is invalid", "request");
    }
    if (!validLimits(request.limits))
        return argument("an::writeStarProfileFile", "star-profile bounds are positive", "limits");
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    auto text = writeStarProfileDocument(profile, request.limits);
    if (!text)
        return text.status();
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    auto output = SequenceOutput::begin(engine, execution,
                                        {.destinationPath = request.destinationPath,
                                         .durability = static_cast<OutputDurability>(request.durability),
                                         .replacePolicy = static_cast<OutputReplacePolicy>(request.replacePolicy)});
    if (!output)
        return output.status();
    auto claim = output->claimWriter();
    if (!claim)
        return claim.status();
    const Status written = callAcrossBoundary(
        {.library = "filesystem", .operation = "an::writeStarProfileFile", .domain = ErrorDomain::Io}, [&] {
            const std::filesystem::path native = nativePath(claim->pendingPath());
            std::ofstream stream(native, std::ios::binary | std::ios::trunc);
            if (!stream.is_open())
                throw std::system_error(std::make_error_code(std::errc::io_error));
            stream.write(text->data(), static_cast<std::streamsize>(text->size()));
            stream.close();
            if (!stream)
                throw std::system_error(std::make_error_code(std::errc::io_error));
        });
    if (written.failed())
        return written;
    claim->complete();
    claim->release();
    auto committed = output->commit(execution, {.framesWritten = 1u, .bytesWritten = text->size()});
    if (!committed)
        return committed.status();
    return StarProfileWriteReport{.bytesWritten = committed->bytesWritten};
}

} // namespace an
