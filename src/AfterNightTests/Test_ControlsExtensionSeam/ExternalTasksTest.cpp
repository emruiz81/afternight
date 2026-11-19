// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The host manager's sessions as a shell shows them, with a stand-in host: a session controller in the states a host
/// brings it to, and a local socket the case holds as the host's end of the session. A session's host stands starting,
/// then ready; the run of the window it shows reaches the standing with its progress, from a `session.activity` only a
/// host that stated the capability may send; a standing that says nothing new is not told again and an end is told
/// once; a host that stopped answering or crashed ends failed with why, and one whose window closed ends as it should;
/// Show sends `window.raise` down the session's socket only to a host that raises its window; and the protocol carries
/// both messages and refuses a malformed activity whole.

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionSessionFeed.h"
#include "controls/ProcessPresentation.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{

using an::ui::ProcessRunState;
using an::ui::extensions::endOf;
using an::ui::extensions::ExtensionHostManager;
using an::ui::extensions::ExtensionHostSessionState;
using an::ui::extensions::ExtensionHostStanding;
using an::ui::extensions::ExtensionProtocolMessage;
using an::ui::extensions::ExtensionSessionController;
using an::ui::extensions::ExtensionSessionEnd;
using an::ui::extensions::ExtensionSessionReport;
using an::ui::extensions::ExtensionSessionStanding;
using an::ui::extensions::IExtensionSessionFeed;
using an::ui::extensions::kSessionActivityCapability;
using an::ui::extensions::kWindowRaiseCapability;
using an::ui::extensions::SessionActivityMessage;
using an::ui::extensions::standingOf;
using an::ui::extensions::takeSessionActivity;
using an::ui::extensions::WindowRaiseMessage;

/// A controller whose session a host started: active, its runtime not ready yet, its hello stating `capabilities`.
[[nodiscard]] std::unique_ptr<ExtensionSessionController> startedSession(const std::vector<std::string>& capabilities)
{
    auto controller = std::make_unique<ExtensionSessionController>();
    controller->descriptor_id = "seam.extension.process";
    controller->display_name = "Seam Extension";
    controller->running_session.prepared_session.session_id = "session-1";
    controller->running_session.state = ExtensionHostSessionState::Active;
    controller->running_session.runtime_ready = false;
    controller->running_session.handshake.hello.capabilities = capabilities;
    return controller;
}

/// The `session.activity` a host sends for a session.
[[nodiscard]] ExtensionProtocolMessage activity(const bool running, const std::optional<double> progress = {},
                                                const std::string& text = {}, const std::string& outcome = {},
                                                const std::string& sessionId = "session-1")
{
    SessionActivityMessage message;
    message.session_id = sessionId;
    message.request_id = "activity-1";
    message.running = running;
    message.progress = progress;
    message.text = text;
    message.outcome = outcome;
    return an::ui::extensions::toProtocolMessage(message);
}

/// What a report tells its observer, in order.
struct Told
{
    std::vector<ExtensionSessionStanding> stood;
    std::vector<ExtensionSessionEnd> ended;

    [[nodiscard]] IExtensionSessionFeed::Observer observer()
    {
        return {[this](const ExtensionSessionStanding& standing) { stood.push_back(standing); },
                [this](const ExtensionSessionEnd& end) { ended.push_back(end); }};
    }
};

/// The host's end of a session: a local server the session's socket connects to, read as a host reads it.
class StandInHost
{
public:
    StandInHost() : m_name(QStringLiteral("an-seam-") + QUuid::createUuid().toString(QUuid::Id128).left(12))
    {
        QLocalServer::removeServer(m_name);
        EXPECT_TRUE(m_server.listen(m_name)) << m_server.errorString().toStdString();
    }

    /// Connects a session's socket to this host.
    [[nodiscard]] std::unique_ptr<QLocalSocket> connect()
    {
        auto socket = std::make_unique<QLocalSocket>();
        socket->connectToServer(m_name);
        EXPECT_TRUE(socket->waitForConnected(5000));
        EXPECT_TRUE(m_server.waitForNewConnection(5000));
        m_end = m_server.nextPendingConnection();
        EXPECT_NE(m_end, nullptr);
        return socket;
    }

    /// The messages the host has been sent, read within a bound.
    [[nodiscard]] std::vector<ExtensionProtocolMessage> received(const int boundMs = 2000)
    {
        std::vector<ExtensionProtocolMessage> messages;
        if (m_end == nullptr)
        {
            return messages;
        }
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < boundMs)
        {
            m_end->waitForReadyRead(20);
            m_buffered.append(m_end->readAll());
            ExtensionProtocolMessage message;
            std::string error;
            if (an::ui::extensions::tryConsumeFramedProtocolMessage(&m_buffered, &message, &error) ==
                an::ui::extensions::ProtocolFrameDecodeStatus::Complete)
            {
                messages.push_back(std::move(message));
                break;
            }
            QCoreApplication::processEvents();
        }
        return messages;
    }

private:
    QString m_name;
    QLocalServer m_server;
    QLocalSocket* m_end = nullptr;
    QByteArray m_buffered;
};

} // namespace

// A session's host stands starting until its runtime is ready, ready then; the session names its process and its
// descriptor; a report tells a standing that says something new and never one that says the same again; an end is told
// once, and not at all for a session never told of.
TEST(ExternalTasks, ASessionStandsStartingThenReadyAndItsEndIsToldOnce)
{
    std::unique_ptr<ExtensionSessionController> session = startedSession({});
    ExtensionSessionStanding standing = standingOf(*session);
    EXPECT_EQ(standing.sessionId, "session-1");
    EXPECT_EQ(standing.descriptorId, "seam.extension.process");
    EXPECT_EQ(standing.displayName, QStringLiteral("Seam Extension"));
    EXPECT_EQ(standing.host, ExtensionHostStanding::Starting);
    EXPECT_EQ(standing.run.phase, ProcessRunState::Phase::Idle);

    ExtensionSessionReport report;
    Told told;
    const std::size_t token = report.observe(told.observer());
    report.stood(standing);
    report.stood(standingOf(*session));
    ASSERT_EQ(told.stood.size(), 1u) << "a standing that says the same is told once";

    session->running_session.runtime_ready = true;
    standing = standingOf(*session);
    EXPECT_EQ(standing.host, ExtensionHostStanding::Ready);
    report.stood(standing);
    ASSERT_EQ(told.stood.size(), 2u);
    EXPECT_EQ(report.told().size(), 1u);

    report.ended(ExtensionSessionEnd{.sessionId = "session-9", .failed = false, .reason = {}});
    EXPECT_TRUE(told.ended.empty()) << "a session never told of has no end to tell";
    report.ended(endOf(*session));
    report.ended(endOf(*session));
    ASSERT_EQ(told.ended.size(), 1u);
    EXPECT_FALSE(told.ended.front().failed);
    EXPECT_TRUE(report.told().empty());
    report.stopObserving(token);
}

// What a host tells of its window's run reaches the standing: a run on its way with how far it has come and what it
// does, then how it ended. The application's own execute in flight is a run too.
TEST(ExternalTasks, TheHostsRunReachesTheStandingWithItsProgress)
{
    std::unique_ptr<ExtensionSessionController> session = startedSession({std::string(kSessionActivityCapability)});
    session->running_session.runtime_ready = true;
    std::string error;
    ASSERT_TRUE(takeSessionActivity(&session->running_session, activity(true, 0.42, "Denoising"), &error)) << error;
    ExtensionSessionStanding standing = standingOf(*session);
    ASSERT_TRUE(standing.run.running());
    ASSERT_TRUE(standing.run.progress.has_value());
    EXPECT_DOUBLE_EQ(*standing.run.progress, 0.42);
    EXPECT_EQ(standing.run.text, QStringLiteral("Denoising"));

    ASSERT_TRUE(takeSessionActivity(&session->running_session, activity(false, {}, {}, "succeeded"), &error)) << error;
    EXPECT_EQ(standingOf(*session).run.phase, ProcessRunState::Phase::Succeeded);
    ASSERT_TRUE(takeSessionActivity(&session->running_session, activity(false, {}, "Out of memory", "failed"), &error))
        << error;
    standing = standingOf(*session);
    EXPECT_EQ(standing.run.phase, ProcessRunState::Phase::Failed);
    EXPECT_EQ(standing.run.text, QStringLiteral("Out of memory"));
    ASSERT_TRUE(takeSessionActivity(&session->running_session, activity(false, {}, {}, "cancelled"), &error)) << error;
    EXPECT_EQ(standingOf(*session).run.phase, ProcessRunState::Phase::Cancelled);

    session->running_session.in_flight_request_kind = "execute";
    EXPECT_TRUE(standingOf(*session).run.running()) << "the application's execute in flight is a run";
}

// A host that did not state the capability may not tell of its run, nor a host of another session; a malformed
// activity is refused whole and leaves what the session knew.
TEST(ExternalTasks, AnActivityTheHostMayNotSendIsRefusedWhole)
{
    std::unique_ptr<ExtensionSessionController> silent = startedSession({});
    std::string error;
    EXPECT_FALSE(takeSessionActivity(&silent->running_session, activity(true), &error));
    EXPECT_NE(error.find("session_activity"), std::string::npos) << error;

    std::unique_ptr<ExtensionSessionController> session = startedSession({std::string(kSessionActivityCapability)});
    EXPECT_FALSE(takeSessionActivity(&session->running_session, activity(true, {}, {}, {}, "session-2"), &error));
    ASSERT_TRUE(takeSessionActivity(&session->running_session, activity(true, 0.25), &error)) << error;

    const std::vector<ExtensionProtocolMessage> malformed = {
        activity(true, 1.5), // progress past the whole
        activity(true, -0.1), // progress before nothing
        activity(true, {}, {}, "succeeded"), // an outcome while running
        activity(false, {}, {}, {}), // no outcome once ended
        activity(false, {}, {}, "exploded"), // an outcome the protocol does not name
        activity(false, 0.5, {}, "succeeded"), // a progress once ended
        activity(true, {}, std::string(201, 'x')), // text over its limit
        activity(true, {},
                 std::string("line\x01"
                             "break")), // text that is not plain
    };
    for (const ExtensionProtocolMessage& message : malformed)
    {
        error.clear();
        EXPECT_FALSE(takeSessionActivity(&session->running_session, message, &error));
        EXPECT_FALSE(error.empty());
    }
    ExtensionProtocolMessage unstated = activity(true);
    unstated.fields.remove(QStringLiteral("running"));
    EXPECT_FALSE(takeSessionActivity(&session->running_session, unstated, &error));

    const ExtensionSessionStanding standing = standingOf(*session);
    ASSERT_TRUE(standing.run.progress.has_value());
    EXPECT_DOUBLE_EQ(*standing.run.progress, 0.25) << "a refused activity changes nothing";
}

// A host that stopped answering stands not responding and ends failed, saying so; one that crashed ends failed with
// what its session's error says, without the output it carries; one whose window closed ends as it should; one that
// went before its runtime was ready failed to start.
TEST(ExternalTasks, AHostThatStoppedAnsweringOrCrashedEndsFailed)
{
    std::unique_ptr<ExtensionSessionController> session = startedSession({});
    session->running_session.runtime_ready = true;
    session->running_session.state = ExtensionHostSessionState::Unresponsive;
    EXPECT_EQ(standingOf(*session).host, ExtensionHostStanding::NotResponding);
    session->ended_as = ExtensionHostSessionState::Unresponsive;
    session->running_session.state = ExtensionHostSessionState::Stopped;
    ExtensionSessionEnd end = endOf(*session);
    EXPECT_TRUE(end.failed);
    EXPECT_TRUE(end.reason.contains(QStringLiteral("stopped responding"))) << end.reason.toStdString();

    session->ended_as = ExtensionHostSessionState::Crashed;
    session->running_session.last_error = "Extension host process crashed output: Traceback (most recent call last)";
    end = endOf(*session);
    EXPECT_TRUE(end.failed);
    EXPECT_EQ(end.reason, QStringLiteral("Extension host process crashed"));

    session->ended_as = ExtensionHostSessionState::Disconnected;
    session->running_session.native_window_close_requested = true;
    EXPECT_FALSE(endOf(*session).failed) << "a window closed by the person ends its session as it should";

    session->ended_as = ExtensionHostSessionState::Active;
    session->running_session.native_window_close_requested = false;
    EXPECT_FALSE(endOf(*session).failed) << "a session the application stopped while it ran ends as it should";

    session->ended_as = ExtensionHostSessionState::Exited;
    session->running_session.runtime_ready = false;
    session->running_session.last_error.clear();
    end = endOf(*session);
    EXPECT_TRUE(end.failed) << "a host that went before its runtime was ready failed to start";
    EXPECT_FALSE(end.reason.isEmpty());
}

// Show asks the host: `window.raise` goes down the session's socket to a host that stated it raises its window, and is
// never sent to one that did not - a host refuses a message it does not know - nor to one still starting, nor to a
// session that is not running.
TEST(ExternalTasks, ShowSendsTheRaiseOnlyToAHostThatRaisesItsWindow)
{
    const ExtensionHostManager manager;
    StandInHost host;
    std::unique_ptr<ExtensionSessionController> session = startedSession({});
    session->running_session.runtime_ready = true;
    session->running_session.socket = host.connect();
    ASSERT_NE(session->running_session.socket, nullptr);

    std::string error;
    EXPECT_FALSE(standingOf(*session).raisesItsWindow);
    EXPECT_FALSE(manager.sendWindowRaise(&session->running_session, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(host.received(200).empty()) << "nothing is sent to a host that does not raise its window";

    session->running_session.handshake.hello.capabilities = {std::string(kWindowRaiseCapability)};
    session->running_session.runtime_ready = false;
    EXPECT_FALSE(standingOf(*session).raisesItsWindow) << "a host is asked nothing while it starts";
    EXPECT_FALSE(manager.sendWindowRaise(&session->running_session, &error));
    EXPECT_TRUE(host.received(200).empty());
    session->running_session.runtime_ready = true;
    EXPECT_TRUE(standingOf(*session).raisesItsWindow);
    ASSERT_TRUE(manager.sendWindowRaise(&session->running_session, &error)) << error;
    const std::vector<ExtensionProtocolMessage> received = host.received();
    ASSERT_EQ(received.size(), 1u);
    WindowRaiseMessage raise;
    ASSERT_TRUE(an::ui::extensions::windowRaiseFromProtocolMessage(received.front(), &raise, &error)) << error;
    EXPECT_EQ(raise.session_id, "session-1");
    EXPECT_FALSE(raise.request_id.empty());

    session->running_session.state = ExtensionHostSessionState::Crashed;
    EXPECT_FALSE(standingOf(*session).raisesItsWindow);
    EXPECT_FALSE(manager.sendWindowRaise(&session->running_session, &error));
    session->running_session.socket->disconnectFromServer();
}

// An extension that states it has no interface (`capabilities.interface` `none`) shows nothing to raise: its session
// raises no window whatever its host states, and the full host is launched to show none, where the Lite host, which
// never shows one of its own, is launched as it was.
TEST(ExternalTasks, AnExtensionWithNoInterfaceRaisesNoWindowAndItsFullHostShowsNone)
{
    std::unique_ptr<ExtensionSessionController> session = startedSession({std::string(kWindowRaiseCapability)});
    session->running_session.runtime_ready = true;
    EXPECT_TRUE(standingOf(*session).raisesItsWindow);
    session->running_session.prepared_session.no_interface = true;
    EXPECT_FALSE(standingOf(*session).raisesItsWindow);

    const ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "seam.extension";
    request.sdk_backend = "runtime";
    request.host_executable = "host";
    request.no_interface = true;
    const auto has = [](const std::vector<std::string>& arguments) {
        return std::find(arguments.begin(), arguments.end(), "--no-process-window") != arguments.end();
    };
    const an::ui::extensions::ExtensionHostPreparedSession full = manager.prepareSession(request);
    EXPECT_TRUE(full.no_interface);
    EXPECT_TRUE(has(full.host_arguments));
    request.sdk_backend = "protocol";
    EXPECT_FALSE(has(manager.prepareSession(request).host_arguments)) << "the Lite host refuses what it does not know";
    request.sdk_backend = "runtime";
    request.no_interface = false;
    EXPECT_FALSE(has(manager.prepareSession(request).host_arguments));
}

// Both messages go over the wire and come back as they went.
TEST(ExternalTasks, TheProtocolCarriesARunsActivityAndARaise)
{
    std::string error;
    SessionActivityMessage sent;
    sent.session_id = "session-1";
    sent.request_id = "activity-2";
    sent.running = true;
    sent.progress = 0.5;
    sent.text = "Step 2 of 4";
    const QByteArray bytes = an::ui::extensions::serializeProtocolMessage(an::ui::extensions::toProtocolMessage(sent));
    ExtensionProtocolMessage read;
    ASSERT_TRUE(an::ui::extensions::deserializeProtocolMessage(bytes, &read, &error)) << error;
    EXPECT_EQ(read.type, "session.activity");
    SessionActivityMessage back;
    ASSERT_TRUE(an::ui::extensions::sessionActivityFromProtocolMessage(read, &back, &error)) << error;
    EXPECT_TRUE(back.running);
    ASSERT_TRUE(back.progress.has_value());
    EXPECT_DOUBLE_EQ(*back.progress, 0.5);
    EXPECT_EQ(back.text, "Step 2 of 4");
    EXPECT_TRUE(back.outcome.empty());

    WindowRaiseMessage raise;
    raise.session_id = "session-1";
    raise.request_id = "raise-1";
    const ExtensionProtocolMessage raiseMessage = an::ui::extensions::toProtocolMessage(raise);
    EXPECT_EQ(raiseMessage.type, "window.raise");
    WindowRaiseMessage raiseBack;
    ASSERT_TRUE(an::ui::extensions::windowRaiseFromProtocolMessage(raiseMessage, &raiseBack, &error)) << error;
    EXPECT_EQ(raiseBack.request_id, "raise-1");
    EXPECT_FALSE(an::ui::extensions::windowRaiseFromProtocolMessage(read, &raiseBack, &error))
        << "an activity is no raise";
}
