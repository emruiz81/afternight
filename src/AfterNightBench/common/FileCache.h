// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - putting a set of input files into a stated storage state.
//
// A row that reads its frames from files measures the drive as much as the engine unless it says
// which of the two it means, and the page cache decides that silently: the same stage over the same
// frames took 2.4 seconds with its frames cached and 6.0 seconds without them on the Windows host.
// These two calls let a row choose, outside the clock, and neither needs elevated rights or writes a
// byte of the files.
#pragma once

#include <span>
#include <string>

namespace an::bench
{

/// Evicts every named file's pages from the operating system's cache, so its next read comes from
/// storage.
///
/// On Windows a handle opened without buffering makes the cache manager write back and drop the
/// file's cached pages; on POSIX systems `posix_fadvise` with `POSIX_FADV_DONTNEED` drops the clean
/// pages of the whole file. The files are opened for reading only.
///
/// @param paths the files, as UTF-8 paths.
/// @param error receives which file could not be opened.
/// @return true when every file was opened and advised.
[[nodiscard]] bool dropFilesFromCache(std::span<const std::string> paths, std::string& error);

/// Reads every named file to its end through the operating system's cache, so its next read is
/// served from memory.
///
/// @param paths the files, as UTF-8 paths.
/// @param error receives which file could not be read.
/// @return true when every file was read to its end.
[[nodiscard]] bool readFilesThroughCache(std::span<const std::string> paths, std::string& error);

} // namespace an::bench
