// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - recognizing pending output payloads and sweeping the ones a run abandoned.

#include <AfterNightEngine/IO/PendingOutput.h>

#include "core/Utf8.h"
#include "io/output/FileSystem.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace an
{
namespace
{
Status directoryFailure(ErrorCode code, const char* message, std::string_view directory, int libraryCode)
{
    std::vector<StatusContextField> context{StatusContextField::ofText(ContextKey::Path, std::string(directory))};
    if (libraryCode != 0)
        context.push_back(StatusContextField::ofNumber(ContextKey::LibraryCode, libraryCode));
    return Status::failure(code, {.message = message,
                                  .operation = "sweepPendingOutputs",
                                  .domain = ErrorDomain::Io,
                                  .context = std::move(context)});
}
} // namespace

bool isPendingOutputPath(std::string_view path) noexcept
{
    if (path.size() <= kPendingOutputSuffix.size() || !path.ends_with(kPendingOutputSuffix))
        return false;
    const char before = path[path.size() - kPendingOutputSuffix.size() - 1u];
    return before != '/' && before != '\\';
}

Result<std::uint64_t> sweepPendingOutputs(std::string_view directory)
{
    if (!isBoundaryPath(directory))
        return directoryFailure(ErrorCode::InvalidArgument,
                                "a directory to sweep is a nonempty bounded UTF-8 path without NUL", directory, 0);
    const std::filesystem::path root = outputPathFromUtf8(directory);
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(root, error);
    if (error == std::errc::no_such_file_or_directory)
        return std::uint64_t{0u};
    if (error)
        return directoryFailure(ErrorCode::ExternalFailure, "the directory to sweep could not be inspected", directory,
                                error.value());
    if (!std::filesystem::exists(status))
        return std::uint64_t{0u};
    if (!std::filesystem::is_directory(status))
        return directoryFailure(ErrorCode::InvalidArgument, "the path to sweep names a file, not a directory",
                                directory, 0);
    std::filesystem::directory_iterator entries(root, error);
    if (error)
        return directoryFailure(ErrorCode::ExternalFailure, "the directory to sweep could not be read", directory,
                                error.value());
    std::uint64_t removed = 0u;
    // The iterator is advanced with an error code, so a directory that becomes unreadable midway
    // is a returned failure rather than an exception crossing the boundary.
    for (const std::filesystem::directory_iterator end; entries != end; entries.increment(error))
    {
        if (error)
            return directoryFailure(ErrorCode::ExternalFailure, "the directory to sweep could not be read in full",
                                    directory, error.value());
        if (!isPendingOutputPath(utf8FromOutputPath(entries->path().filename())))
            continue;
        const bool regular = entries->is_regular_file(error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            continue;
        }
        if (error)
            return directoryFailure(ErrorCode::ExternalFailure, "a pending output could not be inspected", directory,
                                    error.value());
        if (!regular)
            continue;
        if (std::filesystem::remove(entries->path(), error))
            ++removed;
        if (error)
            return directoryFailure(ErrorCode::ExternalFailure, "a pending output could not be removed", directory,
                                    error.value());
    }
    if (error)
        return directoryFailure(ErrorCode::ExternalFailure, "the directory to sweep could not be read in full",
                                directory, error.value());
    return removed;
}
} // namespace an
