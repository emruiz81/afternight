// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the file workflows reach of the sequences other parts of Controls own: opening a sequence file as a document
/// that shows its first frame, and writing the sequence a document states to another file.
///
/// The open and save workflows decide which files are sequences and run on workers; reading and writing a sequence's
/// frames is the sequence player's. Each is declared here and defined beside the sequence player, so the workflows
/// compile, and are tested, without it. Both are called on a worker and hold nothing of a view: they read the
/// document's stated sequence, never the player a view shows it in, so a view closed meanwhile changes nothing they do.
#pragma once

#include "AfterNightControlsExport.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QString>

#include <stop_token>

namespace an::ui
{

/// Opens a sequence file as a document showing its first frame: the document states the sequence - its file, its
/// container and what its source described - and holds the first frame as a new asset of the session, as recorded.
/// Defined beside the sequence player.
///
/// @param session the process's session.
/// @param path the file.
/// @param format the container its path names.
/// @param stop the open's stop.
/// @return the document; `ErrorCode::Cancelled` when stopped; or why the sequence could not be opened.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ImageDocument>
openSequenceDocument(const an::consumer::EngineSession& session, const QString& path, an::SequenceFormat format,
                     std::stop_token stop);

/// Writes the sequence a document states to a file, in the container its path names: every frame, published whole or
/// not at all. Defined beside the sequence player.
///
/// @param session the process's session.
/// @param sequence the sequence, as the document states it.
/// @param path the file.
/// @param stop the write's stop.
/// @return success; `ErrorCode::Cancelled` when stopped; or why nothing was written - nothing is left at `path`.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status writeSequenceFile(const an::consumer::EngineSession& session,
                                                                  const DocumentSequence& sequence, const QString& path,
                                                                  std::stop_token stop);

} // namespace an::ui
