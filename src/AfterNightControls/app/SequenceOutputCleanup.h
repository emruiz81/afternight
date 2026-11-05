// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Removing a sequence a run wrote and could not keep: what a process that publishes a sequence and then cannot show
/// it does with the file it published.
///
/// The engine's sinks publish nothing they did not finish, so a run that fails while it writes leaves nothing behind
/// and needs no cleanup. What remains is a sequence published whole and then not wanted - a view that could not be made
/// for it, a run superseded after its output was committed - which is removed as the container it is: an image list's
/// manifest with the frames directory beside it, or any other container's one file.
#pragma once

#include "controls/SequenceContainers.h"

#include <AfterNightPlatform/Logging.h>

#include <QString>

#include <string>

namespace process_utils
{

/// Removes a published sequence nothing will show, logging what could not be removed.
///
/// @param path the sequence's file, UTF-8; empty removes nothing.
inline void cleanupFailedSequenceOutput(const std::string& path) noexcept
{
    if (path.empty())
    {
        return;
    }
    const an::Status removed = an::ui::removeSequenceFromDisk(QString::fromUtf8(path));
    if (removed.failed())
    {
        LOG_WARNING("Could not remove the sequence '{}': {}", path, removed.message());
    }
}

} // namespace process_utils
