// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What the workflow core reads of a frame's header before any stage runs: its extent, the distortion its own
/// world-coordinate solution states, which a drizzle map inherits, and the artifact identity a generated image carries.
///
/// The manifest's check that a group's frames are one size, the preparation snapshot's capture of a drizzle source's
/// geometry and its check of a master moved or restored under a fresh file identity each read a header; none decodes a
/// sample. They read it through a function, so the planner and its tests can state a header without a file and the
/// product reads it through the engine.

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>

#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration
{

/// What a frame's header states.
struct FrameHeaderFacts
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    /// The distortion the frame's own world-coordinate solution states, in engine pixel coordinates; nothing when the
    /// header states no solution or one with no distortion.
    std::optional<an::SipDistortion> distortion;

    /// The manifest artifact identity the file carries in its own header - the `ANARTID` card a workflow stage stamps
    /// on every image it generates; empty when the header carries none or a malformed one.
    QString artifactIdentity;
};

/// Reads a frame's header; nothing when the file cannot be read as an image.
using FrameHeaderReader = std::function<std::optional<FrameHeaderFacts>(const QString& path)>;

/// A reader that probes each header through a session's codecs - no sample decoded - in a one-thread batch operation
/// it waits to be admitted to. For a worker thread, never for the GUI thread. The session must outlive the reader.
///
/// @param session the session.
/// @return the reader.
[[nodiscard]] AFTERNIGHTPROCESSES_API FrameHeaderReader
sessionFrameHeaderReader(const an::consumer::EngineSession& session);

} // namespace ImageIntegration
