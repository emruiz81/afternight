// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the seam between the developer dump format and its persistent reader.
//
// The format owns file access and header validation; `DumpReader` owns scheduling, the engine
// context lease and the session's lifetime. A session is a prepared read that is never consumed: the
// reader decodes from it again and again, so it also exposes the size check a one-shot decode makes
// only once, and the transfer counters the reader and the pool report.

#pragma once

#include <AfterNightEngine/IO/DumpReader.h>

#include "io/registry/PreparedCodecRead.h"

namespace an
{
/// One open, header-validated dump that a `DumpReader` reads repeatedly.
class DumpReaderSession : public PreparedCodecRead
{
public:
    /// Whether the file is exactly the header plus the samples the header describes.
    /// @param path the path to name in a failure.
    /// @return success, or `ErrorCode::InvalidData` with `CodecFailure::PayloadUnreadable`.
    virtual Status validateSize(std::string_view path) const = 0;
    /// The session's transfer counters, read under their own lock so a read in progress is not waited
    /// for; the handle counts are left zero for the caller to fill.
    /// @return a consistent copy of the counters.
    virtual DumpIoCounters snapshot() const noexcept = 0;
};

/// Opens a version-one dump and validates its header, without the size check or any pixel allocation.
/// @param engine the engine context whose limits bound the header's geometry.
/// @param execution the operation; its cancellation is checked before the file is opened.
/// @param path the dump's UTF-8 path.
/// @return the session, or the failure the dump adapter returns for an unreadable header.
Result<std::unique_ptr<DumpReaderSession>>
openDumpReaderSession(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);
} // namespace an
