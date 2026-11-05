// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The commit token a request carries from the GUI thread to its worker and back: what the request saw when it
/// started, and the decision whether its result may still be published.
///
/// A dialog's request is a snapshot. On the GUI thread, before its worker starts, it captures a request identity, the
/// source document's identity and the revision of what it held, and the revisions of every secondary input. The
/// result comes back later, queued, and by then the person may have changed a parameter, cancelled, applied something
/// else to the image or closed it. The GUI thread asks the ticket before it publishes anything; a result whose ticket
/// is refused is discarded with no history, metadata or display change.
///
/// **Cancelling an execution is not enough.** A completion queued before the stop was observed still arrives, so the
/// decision is the ticket's, not the worker's: a cancelled or superseded request is refused however its worker ended.
/// The ticket's stop token is what its execution observes, so the same act stops the work and refuses its result.
///
/// **Issuing supersedes.** A tracker serves one dialog or control. Each request it issues stops the one before and
/// makes it uncommittable, so a burst of parameter changes drains to the latest.
#pragma once

#include "AfterNightControlsExport.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace an::ui
{

namespace detail
{
class DocumentState;
class RequestState;
class TrackerState;
} // namespace detail

/// Whether a request's result may be published, and if not, why.
enum class CommitVerdict : std::uint8_t
{
    /// Every captured identity still holds and the request is the current one: publish.
    Commit,

    /// The request was cancelled.
    Cancelled,

    /// A newer request of the same tracker was issued.
    Superseded,

    /// The source document or a secondary input's document was closed.
    DocumentClosed,

    /// The source document holds another revision than the request read: it was written, transitioned or replaced.
    SourceChanged,

    /// A secondary input's document holds another revision than the request read.
    SecondaryChanged,

    /// The ticket holds no request.
    Empty
};

/// A verdict's name, for messages and records.
///
/// @param verdict the verdict.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* commitVerdictName(CommitVerdict verdict) noexcept;

/// What a request saw when it was issued, and its standing since.
///
/// Copyable: a copy is the same request, so the worker's copy and the GUI thread's agree. Holds the documents it names
/// weakly, so a ticket travelling in a queued completion keeps no image alive. Thread-safe to copy and read; the
/// verdict is the GUI thread's to act on.
class CommitTicket
{
public:
    /// A ticket of no request; its verdict is `CommitVerdict::Empty`.
    AFTERNIGHTCONTROLS_API CommitTicket() noexcept;

    /// Releases the ticket.
    AFTERNIGHTCONTROLS_API ~CommitTicket();

    /// Copies a ticket: the copy names the same request.
    ///
    /// @param other the ticket to copy.
    AFTERNIGHTCONTROLS_API CommitTicket(const CommitTicket& other);

    /// Copies a ticket.
    ///
    /// @param other the ticket to copy.
    /// @return this ticket.
    AFTERNIGHTCONTROLS_API CommitTicket& operator=(const CommitTicket& other);

    /// Takes over a ticket.
    ///
    /// @param other the ticket to take over.
    AFTERNIGHTCONTROLS_API CommitTicket(CommitTicket&& other) noexcept;

    /// Takes over a ticket.
    ///
    /// @param other the ticket to take over.
    /// @return this ticket.
    AFTERNIGHTCONTROLS_API CommitTicket& operator=(CommitTicket&& other) noexcept;

    /// Whether this ticket names a request.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool valid() const noexcept;

    /// The request's identity within its tracker: issued in increasing order from one; zero for an empty ticket.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t requestId() const noexcept;

    /// The source document's revision the request captured; all zero for a request that reads no document.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const DocumentRevision& source() const noexcept;

    /// The secondary inputs' revisions the request captured, in the order they were named.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const std::vector<DocumentRevision>& secondary() const noexcept;

    /// Stopped when the request is cancelled or superseded; the token the request's execution observes.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::stop_token stopToken() const noexcept;

    /// Whether the request's result may be published now. Read on the GUI thread immediately before publishing:
    /// nothing a worker could have decided substitutes for it.
    ///
    /// @return `CommitVerdict::Commit`, or the first reason it may not, in the enumeration's order.
    [[nodiscard]] AFTERNIGHTCONTROLS_API CommitVerdict verdict() const;

private:
    friend class RequestTracker;

    std::shared_ptr<detail::RequestState> m_request;
    std::weak_ptr<detail::DocumentState> m_sourceDocument;
    std::vector<std::weak_ptr<detail::DocumentState>> m_secondaryDocuments;
    DocumentRevision m_source{};
    std::vector<DocumentRevision> m_secondary;
    bool m_readsDocuments = true;
};

/// Issues one dialog's or control's requests and decides which of them is current.
///
/// Issue and cancel on the GUI thread. Not copyable or movable: its tickets refer to it. Destroying it cancels the
/// current request.
class RequestTracker
{
public:
    /// A tracker with no request issued.
    AFTERNIGHTCONTROLS_API RequestTracker();

    /// Cancels the current request.
    AFTERNIGHTCONTROLS_API ~RequestTracker();

    RequestTracker(const RequestTracker&) = delete;
    RequestTracker& operator=(const RequestTracker&) = delete;
    RequestTracker(RequestTracker&&) = delete;
    RequestTracker& operator=(RequestTracker&&) = delete;

    /// Issues a request over a source document and its secondary inputs, capturing their revisions now. The request
    /// issued before it is superseded: its stop is requested and it can no longer commit.
    ///
    /// @param source the document the request reads and may publish to.
    /// @param secondary the other documents it reads - a mask, a reference - in the order the operation names them.
    /// @return the ticket; `ErrorCode::NotInitialized` for a document that holds nothing; `ErrorCode::InvalidState`
    ///         for a closed one. Nothing is superseded when it refuses.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<CommitTicket>
    issue(const ImageDocument& source, std::span<const ImageDocument* const> secondary = {});

    /// Issues a request that reads no document - a file read that will make one - and supersedes the request issued
    /// before it, of either kind. Nothing but its own standing decides its verdict: it commits while it is current and
    /// was not cancelled.
    ///
    /// @return the ticket.
    [[nodiscard]] AFTERNIGHTCONTROLS_API CommitTicket issueUnbound();

    /// Cancels the current request, if any: its stop is requested and it can no longer commit.
    AFTERNIGHTCONTROLS_API void cancel() noexcept;

    /// The current request's identity, or zero when none is current.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t currentRequest() const noexcept;

private:
    std::shared_ptr<detail::TrackerState> m_state;
};

} // namespace an::ui
