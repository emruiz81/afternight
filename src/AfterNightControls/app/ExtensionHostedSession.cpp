// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "app/ExtensionHostedSession.h"

#include "app/ExtensionCommon.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionPreviewFrames.h"

#include <AfterNightPlatform/Logging.h>

#include <QCborArray>
#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace an::ui::extensions
{

namespace
{

/// The answer of a request that never reached a host, or whose session ended first.
HostedRequest::Answer unanswered(std::string why)
{
    HostedRequest::Answer answer;
    answer.answered = false;
    answer.error = std::move(why);
    return answer;
}

} // namespace

// ---- HostedRequest

HostedRequest::HostedRequest(const Kind kind, QCborMap params, std::vector<SharedBufferDescriptor> buffers,
                             std::shared_ptr<void> lent)
    : m_kind(kind), m_params(std::move(params)), m_buffers(std::move(buffers)), m_lent(std::move(lent))
{
}

HostedRequest::Kind HostedRequest::kind() const noexcept
{
    return m_kind;
}

const QCborMap& HostedRequest::params() const noexcept
{
    return m_params;
}

const std::vector<SharedBufferDescriptor>& HostedRequest::buffers() const noexcept
{
    return m_buffers;
}

std::optional<HostedRequest::Answer> HostedRequest::wait(std::stop_token stop)
{
    std::unique_lock lock(m_mutex);
    m_ended.wait(lock, stop, [this]() { return m_answer.has_value(); });
    return m_answer;
}

bool HostedRequest::ended() const
{
    const std::lock_guard lock(m_mutex);
    return m_answer.has_value();
}

void HostedRequest::end(Answer answer)
{
    std::shared_ptr<void> lent;
    {
        const std::lock_guard lock(m_mutex);
        if (m_answer.has_value())
        {
            return;
        }
        m_answer = std::move(answer);
        lent = std::move(m_lent);
    }
    m_ended.notify_all();
    // What the request lent goes now unless its run still reads it - the output of a frame it takes back.
    lent.reset();
}

// ---- ExtensionHostedSession

ExtensionHostedSession::ExtensionHostedSession() = default;

ExtensionHostedSession::~ExtensionHostedSession()
{
    endEverything("The extension's process closed");
    if (m_pool)
    {
        m_pool->close();
    }
}

void ExtensionHostedSession::setListener(Listener listener)
{
    m_listener = std::move(listener);
}

void ExtensionHostedSession::bind(ExtensionHostManager& manager, ExtensionSessionController& session)
{
    if (bound())
    {
        unbind(ExtensionSessionEnd{.sessionId = m_sessionId, .failed = false, .reason = {}});
    }
    m_manager = &manager;
    m_session = &session;
    m_sessionId = session.sessionId();
    m_wasReady = false;
    m_toldLoaded = false;
    m_toldActivity.reset();
    {
        const std::lock_guard lock(m_mutex);
        m_bufferSessionId = m_sessionId;
        // The frames are lent from the session's own directory, which the application removes once it has stopped the
        // session, as it removes everything lent there.
        m_pool = std::make_shared<PreviewFrameBufferPool>(SharedBufferManager(extensionHostManagedRootPath()),
                                                          m_bufferSessionId);
        // The host keeps the files of the frame it serves mapped until it is sent the next.
        m_pool->keepRemovalsForTheHost();
    }
    if (m_listener.started)
    {
        m_listener.started(StartStep::HostStarted);
    }
}

void ExtensionHostedSession::unbind(const ExtensionSessionEnd& end)
{
    if (!bound())
    {
        return;
    }
    const std::string why =
        end.reason.isEmpty() ? std::string("The extension's session ended") : end.reason.toStdString();
    // A request made from now on is answered at once: the pool going closes the way in before the requests on their
    // way end.
    std::shared_ptr<PreviewFrameBufferPool> pool;
    {
        const std::lock_guard lock(m_mutex);
        pool = std::move(m_pool);
        m_bufferSessionId.clear();
    }
    m_manager = nullptr;
    m_session = nullptr;
    m_wasReady = false;
    endEverything(why);
    if (pool)
    {
        pool->close();
    }
    m_sessionId.clear();
    if (m_listener.ended)
    {
        m_listener.ended(end);
    }
}

bool ExtensionHostedSession::bound() const noexcept
{
    return m_session != nullptr;
}

ExtensionSessionController* ExtensionHostedSession::boundSession() const noexcept
{
    return m_session;
}

bool ExtensionHostedSession::ready() const noexcept
{
    return m_session != nullptr && m_session->isActive() && m_session->running_session.runtime_ready &&
           m_session->running_session.hosted_panel;
}

const std::string& ExtensionHostedSession::sessionId() const noexcept
{
    return m_sessionId;
}

std::shared_ptr<PreviewFrameBufferPool> ExtensionHostedSession::framePool() const
{
    const std::lock_guard lock(m_mutex);
    return m_pool;
}

std::string ExtensionHostedSession::bufferSessionId() const
{
    const std::lock_guard lock(m_mutex);
    return m_bufferSessionId;
}

std::shared_ptr<HostedRequest> ExtensionHostedSession::submit(const HostedRequest::Kind kind, QCborMap params,
                                                              std::vector<SharedBufferDescriptor> buffers,
                                                              std::shared_ptr<void> lent)
{
    auto request = std::make_shared<HostedRequest>(kind, std::move(params), std::move(buffers), std::move(lent));
    {
        const std::lock_guard lock(m_mutex);
        if (m_pool != nullptr)
        {
            m_waiting.push_back(request);
            post(&ExtensionHostedSession::dispatch);
            return request;
        }
    }
    request->end(unanswered("The extension's host is not running"));
    return request;
}

void ExtensionHostedSession::withdraw(const std::shared_ptr<HostedRequest>& request)
{
    if (!request)
    {
        return;
    }
    {
        const std::lock_guard lock(m_mutex);
        request->m_withdrawn = true;
    }
    post(&ExtensionHostedSession::withdrawOnGuiThread);
}

bool ExtensionHostedSession::tellParametersChanged(const QCborMap& params, const QStringList& changedKeys,
                                                   const QString& reason, std::string* error)
{
    if (!ready())
    {
        if (error)
        {
            *error = "The extension's host is not ready";
        }
        return false;
    }
    QCborArray changed;
    for (const QString& key : changedKeys)
    {
        changed.append(key);
    }
    QCborMap value;
    value[QStringLiteral("params")] = params;
    value[QStringLiteral("changed")] = changed;
    value[QStringLiteral("reason")] = reason;
    UiEventMessage event;
    event.widget_id = "panel";
    event.event_name = "parameters_changed";
    event.value = value;
    return m_manager->sendPanelEvent(&m_session->running_session, event, error);
}

bool ExtensionHostedSession::tellAction(const QString& actionId, const QCborMap& params, std::string* error)
{
    if (!ready())
    {
        if (error)
        {
            *error = "The extension's host is not ready";
        }
        return false;
    }
    QCborMap value;
    value[QStringLiteral("params")] = params;
    UiEventMessage event;
    event.widget_id = actionId.toStdString();
    event.event_name = "clicked";
    event.value = value;
    return m_manager->sendPanelEvent(&m_session->running_session, event, error);
}

bool ExtensionHostedSession::tellToolRecord(const QCborMap& record, std::string* error)
{
    if (!ready())
    {
        if (error)
        {
            *error = "The extension's host is not ready";
        }
        return false;
    }
    UiEventMessage event;
    event.widget_id = "panel";
    event.event_name = "tool_record";
    event.value = record;
    return m_manager->sendPanelEvent(&m_session->running_session, event, error);
}

void ExtensionHostedSession::route()
{
    if (m_session == nullptr)
    {
        return;
    }
    ExtensionHostRunningSession& running = m_session->running_session;

    if (!m_toldLoaded && !m_wasReady && running.runtime_bootstrap_state_ready)
    {
        m_toldLoaded = true;
        if (m_listener.started)
        {
            m_listener.started(StartStep::ExtensionLoaded);
        }
    }
    if (!m_wasReady && ready())
    {
        m_wasReady = true;
        if (m_listener.ready)
        {
            m_listener.ready();
        }
    }

    // The answer of the request on its way: a frame's by execution.result, an Apply's by view.commit, or by
    // execution.result when it failed or was cancelled.
    std::shared_ptr<HostedRequest> on_its_way;
    std::string request_id;
    {
        const std::lock_guard lock(m_mutex);
        on_its_way = m_onItsWay;
        if (on_its_way)
        {
            request_id = on_its_way->m_requestId;
        }
    }
    if (on_its_way && !request_id.empty())
    {
        std::optional<HostedRequest::Answer> answer;
        if (running.last_preview_result.has_value() && running.last_preview_result->target_request_id == request_id)
        {
            HostedRequest::Answer told;
            told.answered = true;
            told.status = running.last_preview_result->status;
            told.detail = running.last_preview_result->detail;
            told.outputs = running.last_preview_result->output_buffers;
            told.historyStepName = running.last_preview_result->history_step_name;
            told.historyStepDescription = running.last_preview_result->history_step_description;
            running.last_preview_result.reset();
            answer = std::move(told);
        }
        else if (running.last_view_commit.has_value() && running.last_view_commit->request_id == request_id)
        {
            HostedRequest::Answer told;
            told.answered = true;
            told.status = "succeeded";
            told.outputs = running.last_view_commit->result_buffers;
            told.historyStepName = running.last_view_commit->history_step_name;
            told.historyStepDescription = running.last_view_commit->history_step_description;
            running.last_view_commit.reset();
            answer = std::move(told);
        }
        if (answer.has_value())
        {
            std::shared_ptr<PreviewFrameBufferPool> pool;
            {
                const std::lock_guard lock(m_mutex);
                m_onItsWay.reset();
                pool = m_pool;
            }
            // The host has let go every file this request did not name: what the pool let go before it was sent goes.
            if (pool)
            {
                pool->requestAnswered(on_its_way->m_sentAs);
            }
            on_its_way->end(std::move(*answer));
        }
    }

    // What the host told of the panel, in order.
    std::vector<ExtensionProtocolMessage> told = std::move(running.panel_inbox);
    running.panel_inbox.clear();
    for (const ExtensionProtocolMessage& message : told)
    {
        std::string refusal;
        if (message.type == "ui.describe")
        {
            PanelDescribeMessage described;
            const bool whole = panelDescribeFromProtocolMessage(message, &described, &refusal);
            if (!whole)
            {
                LOG_WARNING("The extension's parameters were refused: {}", refusal);
            }
            if (m_listener.described)
            {
                m_listener.described(whole ? described.params.toJsonArray() : QJsonArray(),
                                     whole ? QString() : QString::fromStdString(refusal));
            }
        }
        else if (message.type == "ui.patch")
        {
            PanelPatchMessage patch;
            const bool whole = panelPatchFromProtocolMessage(message, &patch, &refusal);
            if (!whole)
            {
                LOG_WARNING("The answer of the extension's action was refused: {}", refusal);
            }
            if (m_listener.answered)
            {
                const QString action = whole ? QString::fromStdString(patch.action_id)
                                             : message.fields.value(QStringLiteral("action")).toString();
                m_listener.answered(action, whole ? patch.values : QCborMap(),
                                    QString::fromStdString(whole ? patch.error : refusal));
            }
        }
    }

    // How far an Apply has come, as the host tells it while it runs one.
    if (running.run_activity.has_value())
    {
        const SessionActivityMessage& activity = *running.run_activity;
        const bool changed = !m_toldActivity.has_value() || m_toldActivity->running != activity.running ||
                             m_toldActivity->progress != activity.progress || m_toldActivity->text != activity.text;
        if (changed)
        {
            m_toldActivity = activity;
            if (activity.running && m_listener.progressed)
            {
                m_listener.progressed(activity.progress, QString::fromStdString(activity.text));
            }
        }
    }
}

void ExtensionHostedSession::dispatch()
{
    while (ready() && m_session->running_session.in_flight_request_id.empty())
    {
        std::shared_ptr<HostedRequest> next;
        std::vector<std::shared_ptr<HostedRequest>> given_up;
        {
            const std::lock_guard lock(m_mutex);
            if (m_onItsWay)
            {
                return;
            }
            while (!m_waiting.empty() && next == nullptr)
            {
                std::shared_ptr<HostedRequest> front = std::move(m_waiting.front());
                m_waiting.pop_front();
                if (front->m_withdrawn)
                {
                    given_up.push_back(std::move(front));
                }
                else
                {
                    next = std::move(front);
                }
            }
            m_onItsWay = next;
        }
        for (const std::shared_ptr<HostedRequest>& request : given_up)
        {
            request->end(unanswered("The run gave the request up"));
        }
        if (next == nullptr)
        {
            return;
        }

        std::string id;
        std::string error;
        const std::string token = "hosted-" + std::to_string(m_requestsSent + 1u);
        ExtensionHostRunningSession* running = &m_session->running_session;
        const bool sent =
            next->kind() == HostedRequest::Kind::Preview
                ? m_manager->sendPreviewRequest(running, token, next->params(), next->buffers(), &id, &error)
                : m_manager->sendExecuteRequest(running, token, next->params(), next->buffers(), &id, &error);
        if (!sent)
        {
            {
                const std::lock_guard lock(m_mutex);
                m_onItsWay.reset();
            }
            next->end(unanswered(error.empty() ? std::string("The request could not be sent to the extension's host")
                                               : error));
            continue;
        }
        ++m_requestsSent;
        bool cancel_now = false;
        {
            const std::lock_guard lock(m_mutex);
            next->m_requestId = id;
            next->m_sentAs = m_pool ? m_pool->requestSent() : 0u;
            cancel_now = next->m_withdrawn;
        }
        if (cancel_now)
        {
            withdrawOnGuiThread();
        }
        return;
    }
}

std::uint64_t ExtensionHostedSession::requestsSent() const noexcept
{
    return m_requestsSent;
}

std::uint64_t ExtensionHostedSession::cancelsSent() const noexcept
{
    return m_cancelsSent;
}

void ExtensionHostedSession::post(void (ExtensionHostedSession::*step)())
{
    QCoreApplication* const application = QCoreApplication::instance();
    if (application == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(
        application,
        [weak = weak_from_this(), step]() {
            if (const std::shared_ptr<ExtensionHostedSession> self = weak.lock())
            {
                ((*self).*step)();
            }
        },
        Qt::QueuedConnection);
}

void ExtensionHostedSession::withdrawOnGuiThread()
{
    std::vector<std::shared_ptr<HostedRequest>> given_up;
    bool cancel = false;
    {
        const std::lock_guard lock(m_mutex);
        const auto withdrawn =
            std::stable_partition(m_waiting.begin(), m_waiting.end(),
                                  [](const std::shared_ptr<HostedRequest>& request) { return !request->m_withdrawn; });
        given_up.assign(std::make_move_iterator(withdrawn), std::make_move_iterator(m_waiting.end()));
        m_waiting.erase(withdrawn, m_waiting.end());
        if (m_onItsWay && m_onItsWay->m_withdrawn && !m_onItsWay->m_requestId.empty() && !m_onItsWay->m_cancelSent)
        {
            m_onItsWay->m_cancelSent = true;
            cancel = true;
        }
    }
    for (const std::shared_ptr<HostedRequest>& request : given_up)
    {
        request->end(unanswered("The run gave the request up"));
    }
    if (cancel && ready())
    {
        std::string error;
        if (m_manager->sendCancelRequest(&m_session->running_session, nullptr, &error))
        {
            ++m_cancelsSent;
        }
        else
        {
            LOG_DEBUG("A hosted panel's request could not be cancelled at its host: {}", error);
        }
    }
}

void ExtensionHostedSession::endEverything(const std::string& why)
{
    std::vector<std::shared_ptr<HostedRequest>> ending;
    {
        const std::lock_guard lock(m_mutex);
        if (m_onItsWay)
        {
            ending.push_back(std::move(m_onItsWay));
            m_onItsWay.reset();
        }
        for (std::shared_ptr<HostedRequest>& request : m_waiting)
        {
            ending.push_back(std::move(request));
        }
        m_waiting.clear();
    }
    for (const std::shared_ptr<HostedRequest>& request : ending)
    {
        request->end(unanswered(why));
    }
}

} // namespace an::ui::extensions
