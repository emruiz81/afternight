// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The private sibling a generated file is written to before it exists.
///
/// A file at its destination name is complete by construction: the payload is written beside it
/// under a name that ends in the pending suffix, and renamed into place only once it is whole. So
/// the suffix is a contract with everything that walks an output directory - a reader that meets
/// one has found an uncommitted payload, and a run that starts after a crash sweeps them.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <string_view>

namespace an
{

/// The suffix that marks a payload not yet committed to its destination name.
inline constexpr std::string_view kPendingOutputSuffix = ".antmp";

/// Whether a path names an uncommitted payload rather than a finished artifact.
///
/// A byte comparison of the path's tail against `kPendingOutputSuffix`, case-sensitively, with at
/// least one byte before it that is not a path separator. No filesystem access.
///
/// Thread-safe and allocation-free.
///
/// @param path a UTF-8 path in either slash form.
/// @return true when the path ends in the pending suffix.
[[nodiscard]] AFTERNIGHTENGINE_API bool isPendingOutputPath(std::string_view path) noexcept;

/// Removes the pending payloads an interrupted run left directly below a directory.
///
/// Only regular files whose name ends in `kPendingOutputSuffix` are removed; committed artifacts,
/// subdirectories and anything below them are never touched. A payload that cannot be inspected or
/// removed returns a failure. A directory that does not exist reports zero; an unreadable directory
/// or a path that exists and is not a directory is refused. Earlier removals remain on a later failure.
///
/// Not safe to run against a directory another process is writing into at the same moment: a
/// payload that process has reserved and not yet committed looks exactly like an abandoned one.
///
/// @param directory a UTF-8 directory path: nonempty, at most 131072 bytes, with no NUL.
/// @return how many payloads were removed; `ErrorCode::InvalidArgument` for a path that is not
///         such a path or that names a file; or `ErrorCode::ExternalFailure` carrying the operating
///         system's code when inspection, iteration or a payload removal fails.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint64_t> sweepPendingOutputs(std::string_view directory);

} // namespace an
