// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The order a drawing's edits land in, and the operation they land under.
///
/// Every edit of a drawing runs under an execution of the session, and the GUI thread never waits for one: a
/// drawing window asks for its operation with `tryAdmit`, as a preview, and gets one or is told the session is busy.
/// An input event cannot be dropped for that - a stroke missing the points that arrived while a preview held the
/// session's threads would be a different stroke - so every edit is queued in the order it was made and the queue
/// lands as many as it can whenever it is given an operation: at once, or from a retry a moment later. An edit the
/// document refuses with `ErrorCode::Busy` - the mask is being read - stays at the head and is retried the same way;
/// any other refusal drops that edit, is kept as the last failure, and the next edit runs.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightEngine/Core/Status.h>

#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui::drawing
{

class DrawingDocument;

/// One edit of a drawing, run once under an admitted execution.
using DrawingEdit = std::function<an::Status(DrawingDocument& document, const an::ExecutionContext& execution)>;

/// What one landing did.
struct DrawingEditReport
{
    /// Edits that ran and succeeded.
    std::size_t applied = 0u;

    /// Edits that ran and were refused for a reason other than a busy session or mask, and were dropped.
    std::size_t dropped = 0u;

    /// Edits still waiting.
    std::size_t pending = 0u;
};

/// The queue of one drawing window. Lives on the GUI thread, as the timer it retries with does.
class AFTERNIGHTCONTROLS_API DrawingEditQueue
{
public:
    /// How long the queue waits before it asks again for an operation it was refused.
    static constexpr std::chrono::milliseconds kDefaultRetryInterval{15};

    /// A queue with no document.
    ///
    /// @param landed called on the GUI thread after a landing that ran at least one edit.
    explicit DrawingEditQueue(std::function<void(const DrawingEditReport&)> landed = {});

    ~DrawingEditQueue();
    DrawingEditQueue(const DrawingEditQueue&) = delete;
    DrawingEditQueue& operator=(const DrawingEditQueue&) = delete;
    DrawingEditQueue(DrawingEditQueue&&) = delete;
    DrawingEditQueue& operator=(DrawingEditQueue&&) = delete;

    /// Makes the queue edit another document; the edits waiting for the previous one are dropped.
    ///
    /// @param document the document, or null for none.
    void setDocument(std::shared_ptr<DrawingDocument> document);

    /// The document the queue edits.
    [[nodiscard]] const std::shared_ptr<DrawingDocument>& document() const noexcept;

    /// Queues an edit after every edit already queued and lands what it can now.
    ///
    /// @param edit the edit; an empty one is ignored. Dropped when there is no document.
    void submit(DrawingEdit edit);

    /// Lands every queued edit it can, in order, under one admitted operation, and arranges a retry for what is left.
    ///
    /// @return true when nothing is left waiting.
    bool flush();

    /// How many edits are waiting.
    [[nodiscard]] std::size_t pending() const noexcept;

    /// How often the queue has been refused an operation since it was made.
    [[nodiscard]] std::uint64_t refusals() const noexcept;

    /// Why the last dropped edit was refused; success when none was.
    [[nodiscard]] const an::Status& lastFailure() const noexcept;

    /// Changes how long a retry waits.
    ///
    /// @param interval the wait; zero retries at the event loop's next turn.
    void setRetryInterval(std::chrono::milliseconds interval);

private:
    void scheduleRetry();

    std::shared_ptr<DrawingDocument> m_document;
    std::deque<DrawingEdit> m_edits;
    std::function<void(const DrawingEditReport&)> m_landed;
    QTimer m_retry;
    an::Status m_lastFailure = an::Status::success();
    std::uint64_t m_refusals = 0u;
    bool m_flushing = false;
};

} // namespace an::ui::drawing
