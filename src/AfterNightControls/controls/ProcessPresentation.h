// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How a process is presented: the surface its descriptor states, the width of its column, where its run stands, and
/// the presenter a shell lends to show processes in its own window.
///
/// A process window (`controls/TopLevelProcessBaseControl.h`) tells the presenter where its run stands and which image
/// it targets; the presenter shows that - in a rail of tasks, over the image a run writes - and, from the phase that
/// gives processes pages, takes a process into a page of the main window. A shell that lends no presenter shows every
/// process as the window it is.

#include "AfterNightControlsExport.h"

#include <QString>

#include <cstdint>
#include <optional>

class ImageViewControl;
class TopLevelProcessBaseControl;

namespace an
{
class Status;
} // namespace an

namespace an::ui
{

/// Where a process is shown when a presenter takes it.
enum class ProcessSurface : std::uint8_t
{
    /// A window of its own, as in AfterNight 1.1. Every process is one until its descriptor states another surface.
    Window,

    /// A page of the main window: the process's preview as the canvas, its controls in a column beside it.
    PreviewPage,

    /// A page whose canvas is a view of the target, for a process with no preview of its own.
    MirrorPage,

    /// A page the process fills whole, with no column.
    FullPage
};

/// The width of a page's column.
enum class ProcessColumnWidth : std::uint8_t
{
    Standard,
    Wide
};

/// Where a process's run stands: the phase, how far it has come when it can tell, and what it is doing.
struct ProcessRunState
{
    enum class Phase : std::uint8_t
    {
        /// No run has been made, or the result of the last one has been seen.
        Idle,
        Running,
        Succeeded,
        Failed,
        /// Stopped by the person, or its result dropped because the image changed or closed under it.
        Cancelled
    };

    Phase phase = Phase::Idle;

    /// From 0 to 1, when the run can tell.
    std::optional<double> progress;

    /// What the run is doing - "applying", "Frame 3 of 9" - or, once it failed, why.
    QString text;

    [[nodiscard]] bool operator==(const ProcessRunState&) const = default;

    /// Whether the run is on its way.
    [[nodiscard]] bool running() const noexcept
    {
        return phase == Phase::Running;
    }

    /// Whether the run has ended, whatever its end.
    [[nodiscard]] bool ended() const noexcept
    {
        return phase == Phase::Succeeded || phase == Phase::Failed || phase == Phase::Cancelled;
    }
};

/// The text of a run applying a process to its image.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString applyingRunText();

/// A run that starts.
///
/// @param text what it does; "applying" when empty.
/// @param progress how far it has come, when it can tell from the start.
[[nodiscard]] AFTERNIGHTCONTROLS_API ProcessRunState runningState(const QString& text = {},
                                                                  std::optional<double> progress = std::nullopt);

/// How a run ended, from what it came to: succeeded when its result was shown; cancelled when it was stopped, or when
/// it came to nothing it failed for - its image changed or closed under it; failed, with the status's message as its
/// text, otherwise.
///
/// @param status what the run came to.
/// @param published whether its result was shown.
[[nodiscard]] AFTERNIGHTCONTROLS_API ProcessRunState endedRunState(const an::Status& status, bool published);

/// What shows the processes of a shell's main window: the rail's tasks, the notice over an image a run writes and, once
/// processes have pages, the page a process is taken into. A shell lends one through its application context
/// (`IAppContext::getProcessPresenter`); with none, every process is the window it is. Called on the GUI thread.
class AFTERNIGHTCONTROLS_API IProcessPresenter
{
public:
    virtual ~IProcessPresenter() = default;

    /// Asked once, at a process's first show and before it is visible.
    ///
    /// @return true when the presenter took the process into a page; false leaves it a window.
    virtual bool adopt(TopLevelProcessBaseControl& process) = 0;

    /// The process's run changed: it started, moved on, or ended.
    virtual void runStateChanged(TopLevelProcessBaseControl& process, const ProcessRunState& state) = 0;

    /// The process targets another image, or none: the view it targets is closing, or it targets no single image.
    /// Told while the process is made too, once its target is known, before its window is shown.
    virtual void targetChanged(TopLevelProcessBaseControl& process, ImageViewControl* target) = 0;
};

} // namespace an::ui
