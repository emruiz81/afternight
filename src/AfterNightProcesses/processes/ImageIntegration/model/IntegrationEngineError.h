// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// An engine failure as the integration workflow reports it: the workflow's own error, with the engine's failure as
/// its cause.
///
/// The engine answers an expected failure with a typed `an::Status` - a code, a category, a codec's reason - and never
/// with text to be parsed. A stage that meets one reports its own failure (the dark stack failed, the frame could not
/// be read), names the file, and keeps the engine's words as the cause, so the person reading the report sees what the
/// workflow was doing and what went wrong underneath it. Where the engine's failure says more than the stage could -
/// the run was stopped, the memory ran out, the file is not an image the codecs read - the code says so, so the
/// workflow's own handling (skip a recoverable file, stop on a cancellation) branches on the engine's answer rather
/// than on its message.

#include "processes/ImageIntegration/model/IntegrationError.h"

#include <QString>

namespace an
{
class Status;
} // namespace an

namespace ImageIntegration
{

/// The workflow's code for an engine failure.
///
/// A stop the run asked for is `Cancelled` and a deadline that passed `Timeout`; an allocation the engine or its
/// budget refused is `MemoryExhausted`; a codec that does not read the file's format, or reads no image of its
/// representation, is `InvalidFileFormat`; one that could not read the file's header or samples is `FileNotReadable`;
/// one that could not write or publish its output is `FileNotWritable`. Anything else is the stage's own
/// `stageFailure`. The cause chain is read, so a codec's reason under a callback's failure still decides.
///
/// @param failure the engine's failure; a success maps to `IntegrationErrorCode::None`.
/// @param stageFailure the stage's code for a failure the engine's does not refine.
/// @return the code.
[[nodiscard]] AFTERNIGHTPROCESSES_API IntegrationErrorCode integrationErrorCodeFor(const an::Status& failure,
                                                                                   IntegrationErrorCode stageFailure);

/// The engine's failure in words, the layer that noticed first and every cause beneath it, joined by ": ".
///
/// @param failure the engine's failure.
/// @return the words; empty for a success.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString engineFailureText(const an::Status& failure);

/// A stage's error for an engine failure.
///
/// @param failure the engine's failure.
/// @param stageFailure the stage's code for a failure the engine's does not refine.
/// @param message what the stage was doing, in its own words.
/// @param filePath the file the stage was working on, if any.
/// @return the error: `integrationErrorCodeFor`'s code, the stage's message and file, and `engineFailureText` as the
///         details; a success for a successful status.
[[nodiscard]] AFTERNIGHTPROCESSES_API IntegrationError integrationErrorFromEngine(const an::Status& failure,
                                                                                  IntegrationErrorCode stageFailure,
                                                                                  const QString& message,
                                                                                  const QString& filePath = {});

} // namespace ImageIntegration
