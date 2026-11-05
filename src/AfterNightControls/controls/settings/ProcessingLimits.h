// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The processing limits the settings store holds - the thread ceiling and the engine's memory budget - and how they
/// relate to the session that is running.
///
/// **A limit takes effect when a session is built, never while one runs.** The engine fixes a context's thread ceiling
/// and memory budget when it is built, and replacing a context needs every image, lease and source it lent let go, so
/// the application builds its session from the stored limits at start and a change waits for the next start. The
/// Processing page shows both: what the session in use runs with, and whether what is stored differs from it, which is
/// a change pending a restart. A preview's threads are not a stored limit: the session reserves them out of its
/// width, only while a preview is open.
///
/// **The preview frame rate is not a limit.** How many previews a process window starts in a second while a control is
/// dragged is read when the window paces its next frame, so a change takes effect at once, with no restart.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/FilePolicies.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <cstdint>
#include <optional>

namespace an::ui
{

/// The limits the settings store states for the next session.
struct ProcessingLimits
{
    /// The most threads a session's work uses at once; zero lets the session use the host's concurrency.
    std::uint32_t threads = 0u;

    /// The engine's memory budget in mebibytes; zero sets none, the engine's default.
    std::uint64_t memoryLimitMegabytes = 0u;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const ProcessingLimits& left,
                                         const ProcessingLimits& right) noexcept = default;
};

/// Reads the processing limits from the settings store: the batch-thread count and the memory limit, each a whole
/// number; a text that is not one, or is negative, reads as zero - automatic - and a thread count above what an
/// engine context may be built with reads as that most.
///
/// @param read the settings store.
/// @return the limits.
[[nodiscard]] AFTERNIGHTCONTROLS_API ProcessingLimits readProcessingLimits(const SettingReader& read);

/// Stores the processing limits, in the texts `readProcessingLimits` reads.
///
/// @param limits the limits.
/// @param write the settings store.
AFTERNIGHTCONTROLS_API void writeProcessingLimits(const ProcessingLimits& limits, const SettingWriter& write);

/// The fewest previews a second a process window may be set to start while a control is dragged.
inline constexpr std::uint32_t kMinPreviewFrameRate = 5u;

/// The most previews a second a process window may be set to start while a control is dragged.
inline constexpr std::uint32_t kMaxPreviewFrameRate = 60u;

/// Reads a frame rate from its text: a whole number, held to `kMinPreviewFrameRate` and `kMaxPreviewFrameRate`; nothing
/// for a text that is not one, which a caller leaves out rather than storing as a rate.
///
/// @param text the text, blanks around it allowed.
/// @return the previews a second, or nothing.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<std::uint32_t> parsePreviewFrameRate(const QString& text);

/// Reads how many previews a second a process window starts while a control is dragged: a whole number held to
/// `kMinPreviewFrameRate` and `kMaxPreviewFrameRate`; a text that is not one, and no store, read as the default,
/// thirty.
///
/// @param read the settings store.
/// @return the previews a second.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint32_t readPreviewFrameRate(const SettingReader& read);

/// Stores how many previews a second a process window starts while a control is dragged, held to the range
/// `readPreviewFrameRate` reads.
///
/// @param rate the previews a second.
/// @param write the settings store.
AFTERNIGHTCONTROLS_API void writePreviewFrameRate(std::uint32_t rate, const SettingWriter& write);

/// The session settings a session is built with under the stored limits: `base` with its thread ceiling and its
/// engine memory budget replaced by the limits'.
///
/// @param limits the stored limits.
/// @param base everything else the session is built with.
/// @return the settings.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::consumer::EngineSessionSettings
sessionSettingsForLimits(const ProcessingLimits& limits, an::consumer::EngineSessionSettings base = {});

/// The limits a session's settings state, in the stored form.
///
/// @param settings the session's settings.
/// @return the limits they state.
[[nodiscard]] AFTERNIGHTCONTROLS_API ProcessingLimits
limitsOfSessionSettings(const an::consumer::EngineSessionSettings& settings) noexcept;

/// What the session in use runs with, and whether the stored limits wait for the next start.
struct ProcessingLimitsState
{
    /// The threads the session runs with; zero while it holds no context.
    std::uint32_t width = 0u;

    /// The engine's memory budget the session runs with, in bytes; `an::kDefaultMemoryBudgetBytes` for none.
    std::uint64_t memoryBudgetBytes = 0u;

    /// The threads reserved for previews while one is open: none at a width of one, where all work is serialized; one
    /// up to a width of four; a quarter of the width above it.
    std::uint32_t previewThreads = 0u;

    /// Whether the stored thread ceiling differs from the session's.
    bool threadsPendingRestart = false;

    /// Whether the stored memory budget differs from the session's.
    bool memoryPendingRestart = false;
};

/// What the session in use runs with, held against the limits the page or the store states.
///
/// @param session the process's session.
/// @param stated the limits stated for the next start.
/// @return the state.
[[nodiscard]] AFTERNIGHTCONTROLS_API ProcessingLimitsState
processingLimitsState(const an::consumer::EngineSession& session, const ProcessingLimits& stated);

} // namespace an::ui
