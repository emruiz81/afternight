// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a hosted process shares with the session of its extension's host: the requests its runs make of the host - a
/// frame of its preview, its Apply - and the host's answers, what the host tells of the panel it serves, and what a
/// person does in the panel, told to the host.
///
/// A run makes its request on its worker and waits there for the answer; the GUI thread sends it, hears the answer when
/// the session's messages are pumped and hands it over, and never waits for either. The host takes one request at a
/// time, so a request waits its turn. A request its run gave up on - a frame superseded, an Apply cancelled - is
/// withdrawn while it waits and cancelled at the host once sent; what it lent stays lent until the host has answered,
/// since the host may still write it. When the session ends every request still on its way is answered with why, so no
/// run waits for a host that has gone.
///
/// The session object outlives the sessions it is bound to: a process whose host died keeps it, and a restart binds it
/// to the new session.

#include "AfterNightControlsExport.h"

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionSessionFeed.h"

#include <QCborMap>
#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace an::ui::extensions
{

class ExtensionHostManager;
class PreviewFrameBufferPool;
struct ExtensionSessionController;

/// One request a hosted process's run makes of its host. Made on the run's worker, sent and answered on the GUI thread,
/// waited for on the worker.
class AFTERNIGHTCONTROLS_API HostedRequest final
{
public:
    enum class Kind
    {
        /// A frame of the preview: answered by `execution.result`.
        Preview,
        /// An Apply: answered by `view.commit`, or by `execution.result` when it failed or was cancelled.
        Execute
    };

    /// How the request ended.
    struct Answer
    {
        /// Whether the host answered; false when the request never reached it, or its session ended first, `error`
        /// saying why.
        bool answered = false;

        /// The host's word: "succeeded", "failed" or "cancelled".
        std::string status;

        /// What the host said of a failure.
        std::string detail;

        /// The buffers it wrote: the output the request lent, and for an Apply the masks it made.
        std::vector<SharedBufferDescriptor> outputs;

        /// The history step the host named for an Apply; empty when it named none.
        std::string historyStepName;
        std::string historyStepDescription;

        /// Why the request was not answered.
        std::string error;

        /// Whether the host answered that the request succeeded.
        [[nodiscard]] bool succeeded() const noexcept
        {
            return answered && status == "succeeded";
        }
    };

    /// @param kind what the request asks.
    /// @param params the parameters the run reads.
    /// @param buffers the buffers it lends the host, in the protocol's terms.
    /// @param lent what the buffers belong to, kept until the request ends: a frame's pooled buffers, an Apply's
    /// export.
    HostedRequest(Kind kind, QCborMap params, std::vector<SharedBufferDescriptor> buffers, std::shared_ptr<void> lent);

    HostedRequest(const HostedRequest&) = delete;
    HostedRequest& operator=(const HostedRequest&) = delete;

    [[nodiscard]] Kind kind() const noexcept;
    [[nodiscard]] const QCborMap& params() const noexcept;
    [[nodiscard]] const std::vector<SharedBufferDescriptor>& buffers() const noexcept;

    /// Waits for the request to end. Never on the GUI thread.
    ///
    /// @param stop the run's stop: the wait ends when it is requested, the request then still on its way.
    /// @return the answer; none when `stop` was requested first.
    [[nodiscard]] std::optional<Answer> wait(std::stop_token stop);

    /// Whether the request has ended.
    [[nodiscard]] bool ended() const;

private:
    friend class ExtensionHostedSession;

    /// Ends the request, once: the waiting run wakes, and what it lent goes when nothing else holds it.
    void end(Answer answer);

    const Kind m_kind;
    const QCborMap m_params;
    const std::vector<SharedBufferDescriptor> m_buffers;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_ended;
    std::optional<Answer> m_answer;
    std::shared_ptr<void> m_lent;

    /// GUI thread: the id the host knows it by once sent, and whether its run gave up on it.
    std::string m_requestId;
    bool m_withdrawn = false;
    bool m_cancelSent = false;
};

/// What a hosted process shares with its host's session. Made on the GUI thread; submitted to from any thread.
class AFTERNIGHTCONTROLS_API ExtensionHostedSession final : public std::enable_shared_from_this<ExtensionHostedSession>
{
public:
    /// What the session tells its process, on the GUI thread.
    struct Listener
    {
        /// The host's runtime is ready: requests are sent from now on.
        std::function<void()> ready;

        /// The host told the extension's parameters (`ui.describe`), whole and in its own terms; an empty reason with
        /// the list, or why it was refused with none.
        std::function<void(const QJsonArray& params, const QString& refusal)> described;

        /// The host answered an action (`ui.patch`): the values it set, and why it failed when it did; or the answer
        /// was refused, `error` saying why.
        std::function<void(const QString& actionId, const QCborMap& values, const QString& error)> answered;

        /// The host told how far the run of an Apply has come.
        std::function<void(std::optional<double> progress, const QString& text)> progressed;

        /// The session ended: every request on its way has been answered with why.
        std::function<void(const ExtensionSessionEnd& end)> ended;
    };

    ExtensionHostedSession();
    ~ExtensionHostedSession();

    ExtensionHostedSession(const ExtensionHostedSession&) = delete;
    ExtensionHostedSession& operator=(const ExtensionHostedSession&) = delete;

    /// GUI thread.
    void setListener(Listener listener);

    /// Binds the object to a session that started: its requests go to that session's host. A session bound before is
    /// let go first, as `unbind` does. GUI thread.
    ///
    /// @param manager the seam the session's messages are written through; outlives the binding.
    /// @param session the session; outlives the binding, which the coordinator ends before it goes.
    void bind(ExtensionHostManager& manager, ExtensionSessionController& session);

    /// The session ended: every request on its way is answered with why, the frames' buffers go, and the listener is
    /// told. Nothing while unbound. GUI thread.
    void unbind(const ExtensionSessionEnd& end);

    /// Whether a session is bound. GUI thread.
    [[nodiscard]] bool bound() const noexcept;

    /// Whether the bound session's host is running and its runtime ready, so a request is sent. GUI thread.
    [[nodiscard]] bool ready() const noexcept;

    /// The bound session's id; empty while unbound. GUI thread.
    [[nodiscard]] const std::string& sessionId() const noexcept;

    /// The pool the preview's frames are lent through while a session is bound, the buffers under the session's
    /// directory; null while unbound. Any thread.
    [[nodiscard]] std::shared_ptr<PreviewFrameBufferPool> framePool() const;

    /// The directory the session's buffers go in: an Apply's export, and the frames'. Empty while unbound. Any thread.
    [[nodiscard]] std::string bufferSessionId() const;

    /// Makes a request of the host: sent once the host is ready and takes it, or answered with why when no session is
    /// bound. Any thread.
    [[nodiscard]] std::shared_ptr<HostedRequest> submit(HostedRequest::Kind kind, QCborMap params,
                                                        std::vector<SharedBufferDescriptor> buffers,
                                                        std::shared_ptr<void> lent);

    /// Gives a request up: withdrawn while it waits its turn, cancelled at the host once sent. What it lent stays lent
    /// until the host has answered. Any thread.
    void withdraw(const std::shared_ptr<HostedRequest>& request);

    /// Tells the host the parameters changed (`ui.event` `parameters_changed`): the whole set, the keys that changed
    /// and why. GUI thread.
    [[nodiscard]] bool tellParametersChanged(const QCborMap& params, const QStringList& changedKeys,
                                             const QString& reason, std::string* error = nullptr);

    /// Tells the host a person pressed an action (`ui.event` `clicked`) with the parameters as they stand; its answer
    /// comes as `Listener::answered`. GUI thread.
    [[nodiscard]] bool tellAction(const QString& actionId, const QCborMap& params, std::string* error = nullptr);

    /// Tells the host what the panel's tool configuration resolved (`ui.event` `tool_record`), which the extension
    /// reads as its settings before the ones stored, as its window's always answered them. GUI thread.
    [[nodiscard]] bool tellToolRecord(const QCborMap& record, std::string* error = nullptr);

    /// Takes what the session's messages brought - the answer of the request on its way, what the host told of the
    /// panel and of an Apply's progress - once they were pumped, and tells the listener. GUI thread.
    void route();

    /// Sends the next request waiting its turn, when the host is ready and nothing is on its way. GUI thread.
    void dispatch();

    /// How many requests were sent to a host, and how many of them were cancelled there.
    [[nodiscard]] std::uint64_t requestsSent() const noexcept;
    [[nodiscard]] std::uint64_t cancelsSent() const noexcept;

private:
    void post(void (ExtensionHostedSession::*step)());
    void withdrawOnGuiThread();
    void endEverything(const std::string& why);

    ExtensionHostManager* m_manager = nullptr;
    ExtensionSessionController* m_session = nullptr;
    std::string m_sessionId;
    bool m_wasReady = false;
    std::optional<SessionActivityMessage> m_toldActivity;
    Listener m_listener;

    mutable std::mutex m_mutex;
    std::deque<std::shared_ptr<HostedRequest>> m_waiting;
    std::shared_ptr<HostedRequest> m_onItsWay;
    std::shared_ptr<PreviewFrameBufferPool> m_pool;
    std::string m_bufferSessionId;

    std::uint64_t m_requestsSent = 0u;
    std::uint64_t m_cancelsSent = 0u;
};

} // namespace an::ui::extensions
