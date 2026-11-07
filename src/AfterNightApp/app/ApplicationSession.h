// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The engine session the application owns, and the order it is shut down in.
///
/// **The application owns one session, and everything in the process runs in it.** It is opened once the settings
/// store has been read, from the processing limits the store states and with the product's codecs, before any image
/// work; Controls and the processes reach it through the application context, which lends it, and never open one of
/// their own. A limit changed while the application runs waits for the next start.
///
/// **Shutting down is ordered.** Admission is closed first, so nothing new starts and every request waiting for a
/// thread is refused; the main window is destroyed - its views, history, caches and extension sessions, while the
/// application context it installed is still there; then the work the thread pool still holds finishes and what it
/// posted back to the GUI thread is delivered, which lets go of the images it held; then the session is shut down,
/// checked.
#pragma once

#include "controls/settings/ProcessingLimits.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Core/Status.h>

#include <functional>
#include <optional>

namespace an::ui
{

/// The version of the product, which the application states: the crash reports, the about and system information
/// dialogs, the dashboard and the extension manager's minimum-version checks read it as the application's version.
/// The engine reports its own beside it.
inline constexpr char kProductVersion[] = "1.0.1";

/// The settings the application's session is opened with under the stored limits: the product's codecs, and the
/// thread ceiling and memory budget the limits state.
///
/// @param limits the limits the settings store states.
/// @return the settings.
[[nodiscard]] an::consumer::EngineSessionSettings applicationSessionSettings(const ProcessingLimits& limits);

/// The session the application owns, lent to Controls through the application context.
class ApplicationSession final : public IEngineSessionProvider
{
public:
    /// A session not opened yet.
    ApplicationSession() = default;

    /// Closes admission and lets the session go: its context is shut down at once when nothing is outstanding, and
    /// when the last of what is outstanding goes otherwise.
    ~ApplicationSession() override;

    ApplicationSession(const ApplicationSession&) = delete;
    ApplicationSession& operator=(const ApplicationSession&) = delete;
    ApplicationSession(ApplicationSession&&) = delete;
    ApplicationSession& operator=(ApplicationSession&&) = delete;

    /// Opens the session from the stored limits.
    ///
    /// @param limits the limits the settings store states.
    /// @return success; `ErrorCode::InvalidState` when the session is already open, in which case nothing changes;
    ///         otherwise why the session could not be opened, in which case it stays unopened.
    an::Status open(const ProcessingLimits& limits);

    /// Whether `open` succeeded.
    [[nodiscard]] bool isOpen() const noexcept;

    /// The session. Calling it before `open` succeeded is a defect.
    [[nodiscard]] const an::consumer::EngineSession& engineSession() const noexcept override;

    /// The first step of shutting down: nothing new is admitted and every request waiting for a thread is refused.
    /// Idempotent, and nothing for a session not opened.
    void closeAdmission() noexcept;

    /// Shuts the session down: closes admission, waits until the global thread pool holds no work, and shuts the
    /// session down, checked.
    ///
    /// @return success, also for a session not opened or already shut down; otherwise `ErrorCode::Busy` naming what is
    ///         still outstanding, which keeps the context alive until the last of it goes.
    an::Status shutdown();

private:
    std::optional<an::consumer::EngineSession> m_session;
};

/// Tears the application down in its order, on the GUI thread once its event loop has returned: admission is closed,
/// so nothing new starts and every request waiting for a thread is refused; the main window is destroyed - its
/// extension sessions and process windows first, so what those still run is stopped, then the work left on the thread
/// pool finishes while the settings stores it may read are still there, then the stores, its views, history and caches,
/// with the application context it installed; the windows closed on the way out are deleted; the thread pool's work
/// finishes and what it posted back to the GUI thread is delivered, letting go of the images it held; and the session
/// is shut down, checked.
///
/// @param session the application's session.
/// @param destroyMainWindow destroys the main window.
/// @return what the session's shutdown answered.
an::Status shutDownApplication(ApplicationSession& session, const std::function<void()>& destroyMainWindow);

} // namespace an::ui
