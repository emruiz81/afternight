// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Commit tickets and the tracker that issues them: a request's own standing - cancelled or superseded - in state its
/// tickets share, and the documents' revisions compared again when the verdict is asked for.

#include "session/CommitTicket.h"

#include "session/DocumentState.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <atomic>
#include <mutex>
#include <string>
#include <utility>

namespace an::ui
{

namespace detail
{

/// One request's standing, shared by every copy of its ticket.
class RequestState
{
public:
    explicit RequestState(std::uint64_t id) noexcept : m_id(id) {}

    [[nodiscard]] std::uint64_t id() const noexcept
    {
        return m_id;
    }

    [[nodiscard]] std::stop_token stopToken() const noexcept
    {
        return m_stop.get_token();
    }

    [[nodiscard]] bool cancelled() const noexcept
    {
        return m_cancelled.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool superseded() const noexcept
    {
        return m_superseded.load(std::memory_order_acquire);
    }

    /// Marks the request cancelled, then stops its work.
    void cancel() noexcept
    {
        m_cancelled.store(true, std::memory_order_release);
        m_stop.request_stop();
    }

    /// Marks the request superseded, then stops its work.
    void supersede() noexcept
    {
        m_superseded.store(true, std::memory_order_release);
        m_stop.request_stop();
    }

private:
    std::uint64_t m_id;
    std::stop_source m_stop;
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_superseded{false};
};

/// A tracker's current request and the count of requests it issued.
class TrackerState
{
public:
    std::mutex mutex;
    std::uint64_t issued = 0u;
    std::shared_ptr<RequestState> current;
};

} // namespace detail

namespace
{

/// Why a document cannot be named by a request, or success when it can.
[[nodiscard]] an::Status checkNamed(const ImageDocument* document, const char* role)
{
    if (document == nullptr || !document->valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized,
                                   std::string("the request's ") + role + " is a document that holds nothing");
    }
    if (!document->isOpen())
    {
        return an::Status::failure(an::ErrorCode::InvalidState,
                                   std::string("the request's ") + role + " is a closed document");
    }
    return an::Status::success();
}

/// Makes a new request the tracker's current one and supersedes the one it replaces, outside the tracker's lock.
[[nodiscard]] std::shared_ptr<detail::RequestState> makeCurrent(detail::TrackerState& tracker)
{
    std::shared_ptr<detail::RequestState> request;
    std::shared_ptr<detail::RequestState> superseded;
    {
        std::lock_guard lock(tracker.mutex);
        request = std::make_shared<detail::RequestState>(++tracker.issued);
        superseded = std::exchange(tracker.current, request);
    }
    if (superseded != nullptr)
    {
        superseded->supersede();
    }
    return request;
}

} // namespace

const char* commitVerdictName(CommitVerdict verdict) noexcept
{
    switch (verdict)
    {
    case CommitVerdict::Commit:
        return "commit";
    case CommitVerdict::Cancelled:
        return "cancelled";
    case CommitVerdict::Superseded:
        return "superseded";
    case CommitVerdict::DocumentClosed:
        return "document closed";
    case CommitVerdict::SourceChanged:
        return "source changed";
    case CommitVerdict::SecondaryChanged:
        return "secondary changed";
    case CommitVerdict::Empty:
        return "empty";
    }
    return "unknown";
}

CommitTicket::CommitTicket() noexcept = default;

CommitTicket::~CommitTicket() = default;

CommitTicket::CommitTicket(const CommitTicket& other) = default;

CommitTicket& CommitTicket::operator=(const CommitTicket& other) = default;

CommitTicket::CommitTicket(CommitTicket&& other) noexcept = default;

CommitTicket& CommitTicket::operator=(CommitTicket&& other) noexcept = default;

bool CommitTicket::valid() const noexcept
{
    return m_request != nullptr;
}

std::uint64_t CommitTicket::requestId() const noexcept
{
    return m_request != nullptr ? m_request->id() : 0u;
}

const DocumentRevision& CommitTicket::source() const noexcept
{
    return m_source;
}

const std::vector<DocumentRevision>& CommitTicket::secondary() const noexcept
{
    return m_secondary;
}

std::stop_token CommitTicket::stopToken() const noexcept
{
    return m_request != nullptr ? m_request->stopToken() : std::stop_token{};
}

CommitVerdict CommitTicket::verdict() const
{
    if (m_request == nullptr)
    {
        return CommitVerdict::Empty;
    }
    if (m_request->cancelled())
    {
        return CommitVerdict::Cancelled;
    }
    if (m_request->superseded())
    {
        return CommitVerdict::Superseded;
    }
    if (!m_readsDocuments)
    {
        return CommitVerdict::Commit;
    }
    const std::shared_ptr<detail::DocumentState> source = m_sourceDocument.lock();
    if (source == nullptr || source->closed())
    {
        return CommitVerdict::DocumentClosed;
    }
    std::vector<std::shared_ptr<detail::DocumentState>> secondary;
    secondary.reserve(m_secondaryDocuments.size());
    for (const std::weak_ptr<detail::DocumentState>& named : m_secondaryDocuments)
    {
        std::shared_ptr<detail::DocumentState> document = named.lock();
        if (document == nullptr || document->closed())
        {
            return CommitVerdict::DocumentClosed;
        }
        secondary.push_back(std::move(document));
    }
    if (source->revision() != m_source)
    {
        return CommitVerdict::SourceChanged;
    }
    for (std::size_t index = 0u; index < secondary.size(); ++index)
    {
        if (secondary[index]->revision() != m_secondary[index])
        {
            return CommitVerdict::SecondaryChanged;
        }
    }
    return CommitVerdict::Commit;
}

RequestTracker::RequestTracker() : m_state(std::make_shared<detail::TrackerState>()) {}

RequestTracker::~RequestTracker()
{
    cancel();
}

an::Result<CommitTicket> RequestTracker::issue(const ImageDocument& source,
                                               std::span<const ImageDocument* const> secondary)
{
    if (an::Status named = checkNamed(&source, "source"); !named.ok())
    {
        return named;
    }
    for (const ImageDocument* document : secondary)
    {
        if (an::Status named = checkNamed(document, "secondary input"); !named.ok())
        {
            return named;
        }
    }

    CommitTicket ticket;
    ticket.m_sourceDocument = source.m_state;
    ticket.m_source = source.revision();
    ticket.m_secondaryDocuments.reserve(secondary.size());
    ticket.m_secondary.reserve(secondary.size());
    for (const ImageDocument* document : secondary)
    {
        ticket.m_secondaryDocuments.push_back(document->m_state);
        ticket.m_secondary.push_back(document->revision());
    }

    ticket.m_request = makeCurrent(*m_state);
    return ticket;
}

CommitTicket RequestTracker::issueUnbound()
{
    CommitTicket ticket;
    ticket.m_readsDocuments = false;
    ticket.m_request = makeCurrent(*m_state);
    return ticket;
}

void RequestTracker::cancel() noexcept
{
    std::shared_ptr<detail::RequestState> current;
    {
        std::lock_guard lock(m_state->mutex);
        current = std::exchange(m_state->current, nullptr);
    }
    if (current != nullptr)
    {
        current->cancel();
    }
}

std::uint64_t RequestTracker::currentRequest() const noexcept
{
    std::lock_guard lock(m_state->mutex);
    return m_state->current != nullptr ? m_state->current->id() : 0u;
}

} // namespace an::ui
