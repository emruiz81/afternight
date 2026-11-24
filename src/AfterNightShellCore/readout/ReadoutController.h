// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The controller that feeds the readout: it follows the active view, or a task's preview, and measures what it shows
/// on the thread pool, never on the GUI thread.

#include "AfterNightShellCoreExport.h"

#include "controls/ImageStatisticsFigures.h"
#include "widgets/HistogramDisplayData.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class ImageViewControl;

namespace an::consumer
{
class ImageAsset;
} // namespace an::consumer

namespace an::ui
{
class AnCompactReadout;
class AnReadoutColumn;
} // namespace an::ui

namespace an::ui::shell
{

class ApplicationCore;

/// A task's preview as the readout reads it: the frames its preview shows and the image they are previews of.
///
/// A frame is settled once its parameters have, interactive while a control is dragged: only a settled frame is
/// measured, because only an interactive frame's pixels are approximated, never a statistic. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API IReadoutFrameSource
{
public:
    /// One frame the preview shows.
    struct Frame
    {
        /// What the frame shows.
        std::shared_ptr<const an::consumer::ImageAsset> image;

        /// Whether it was made while a control was dragged.
        bool interactive = false;

        /// Its place among the frames, increasing.
        std::uint64_t number = 0u;
    };

    virtual ~IReadoutFrameSource() = default;

    /// The frame shown now, or nothing before the first.
    [[nodiscard]] virtual std::optional<Frame> latestFrame() const = 0;

    /// The image the frames are previews of: the task's target, whose figures each change is against.
    [[nodiscard]] virtual std::shared_ptr<const an::consumer::ImageAsset> original() const = 0;

    /// The target's view, whose name and solution the readout shows; null when it has none.
    [[nodiscard]] virtual ImageViewControl* target() const = 0;

    /// Tells a listener, on the GUI thread, each time a frame is shown; an empty one tells nobody.
    virtual void setFrameListener(std::function<void()> listener) = 0;
};

/// What the controller came to, for the cases that hold it to its rules.
struct ReadoutTelemetry
{
    /// Updates the debounce ran: one per burst of changes.
    std::uint64_t updates = 0u;

    /// Updates that measured nothing, because nothing measured had changed: a channel shown, a name.
    std::uint64_t presented = 0u;

    /// Measurements started on the thread pool.
    std::uint64_t started = 0u;

    /// Measurements shown.
    std::uint64_t published = 0u;

    /// Measurements that landed with a ticket that no longer committed - another view, a closed one, a newer request
    /// - and were dropped.
    std::uint64_t discarded = 0u;

    /// Measurements the session would not admit at once; the figures stayed, dimmed, until a try was admitted.
    std::uint64_t refused = 0u;

    /// Measurements the engine refused, or that found no session.
    std::uint64_t failed = 0u;

    /// Calls the measurements made of the engine and the session - a lease, an admission, a count, the statistics -
    /// and how many of them on the GUI thread, which is never one.
    std::uint64_t engineCalls = 0u;
    std::uint64_t engineCallsOnGuiThread = 0u;

    /// Interactive frames of a task's preview that left the figures as they were, dimmed.
    std::uint64_t interactiveFrames = 0u;

    /// The GUI thread's time in the last update and in the last publication, in nanoseconds.
    std::int64_t lastUpdateNanoseconds = 0;
    std::int64_t lastPublishNanoseconds = 0;
};

/// Feeds the readout: the column of the full readout, and a task's compact readout.
///
/// **What it follows.** The active view, docked or floating, as the application's listeners are told of it; or, while a
/// shell shows a task, the task's preview (`followTask`); or one view whichever is active (`followView`) - a task's
/// target, which the full readout over the task's column reads out. A view that goes is followed no more.
///
/// **When it measures.** A change - another view, the image updated, the histogram's resolution, a settled frame -
/// schedules an update, and a burst of changes within `kDebounce` makes one. An update measures only what changed:
/// the channel the view shows and the view's name are shown at once with nothing measured. A measurement is one
/// operation on the thread pool: the image's read lease, then an admission as `Preview` through `tryAdmit` - so the
/// readout never queues in front of a run - then the histogram's counts and the statistics window's figures, with the
/// clipped samples left out as the window leaves them out when it opens. It is published only when its commit ticket
/// still commits; a refused admission leaves the figures as they were, dimmed, and is tried again by itself - after
/// `kDebounce`, then twice as long each time up to `kRetryLongest` - until it is admitted or a change measures anew.
/// The GUI thread makes no call of the engine.
///
/// **A task.** Only a settled frame is measured, with the original's figures beside it for the change against them; an
/// interactive frame dims the figures and keeps them.
///
/// **What is remembered.** The column's folded cards and its being collapsed, in the global settings store
/// (`interface/readoutCards`, `interface/readoutCollapsed`); a column with no collapse of its own keeps its folds
/// alone.
///
/// Used on the GUI thread only.
class AFTERNIGHTSHELLCORE_API ReadoutController final
{
public:
    /// How long a burst of changes is gathered into one update.
    static constexpr std::chrono::milliseconds kDebounce{150};

    /// The longest wait before a refused measurement is tried again.
    static constexpr std::chrono::milliseconds kRetryLongest{600};

    /// What of the person's choices a column is handed, and keeps.
    enum class ColumnMemory : std::uint8_t
    {
        /// Its folded cards and its being collapsed: the readout column's.
        FoldsAndCollapse,

        /// Its folded cards alone: a column that is not collapsed - the full readout over a task's column.
        Folds
    };

    explicit ReadoutController(ApplicationCore& core);
    ~ReadoutController();

    ReadoutController(const ReadoutController&) = delete;
    ReadoutController& operator=(const ReadoutController&) = delete;

    /// The column it feeds, which it neither owns nor outlives the use of; null feeds none. Its remembered folds - and,
    /// as `memory` says, its being collapsed - are handed to it.
    void setColumn(an::ui::AnReadoutColumn* column, ColumnMemory memory = ColumnMemory::FoldsAndCollapse);

    /// The compact readout it feeds while it follows a task; null feeds none.
    void setCompactReadout(an::ui::AnCompactReadout* compact);

    /// Follows a task's preview; null follows the active view again.
    void followTask(IReadoutFrameSource* source);

    /// Follows one view, whichever view is active, and no task; null follows the active view again.
    void followView(ImageViewControl* view);

    /// The view it reads out: the active view, or a task's target.
    [[nodiscard]] ImageViewControl* followedView() const;

    /// Whether it follows a task.
    [[nodiscard]] bool followsTask() const noexcept;

    /// Whether the figures shown are being measured again, or were refused, or are a task's during an interactive
    /// frame.
    [[nodiscard]] bool isStale() const noexcept;

    /// Whether no update is pending - a change's or a refused measurement's try - and no measurement is on its way.
    [[nodiscard]] bool isIdle() const;

    [[nodiscard]] ReadoutTelemetry telemetry() const;

    /// The figures shown, and the counts.
    [[nodiscard]] const std::optional<an::ui::ImageStatisticsReport>& shownFigures() const noexcept;
    [[nodiscard]] std::shared_ptr<const an::ui::HistogramDisplayData> shownCounts() const;

    /// The original's figures a task's changes are against.
    [[nodiscard]] const std::optional<an::ui::ImageStatisticsReport>& originalFigures() const noexcept;

    /// Runs a pending update now rather than at the debounce's end.
    void updateNow();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
