// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A stand-in for an extension host serving a hosted panel: the far end of the session's socket, read and written by a
/// case, and an idle process of this suite standing for the host's process, so the session is pumped as a running
/// host's is (`ExtensionHostManager::pumpSession`). It answers what a case asks it to - a frame, an Apply, a cancel -
/// writing the buffers it was lent as a host does, and it answers the heartbeat.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionHostedSession.h"
#include "app/SharedBufferManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::seam
{

/// The environment variable that makes this suite's executable stand idle for a host's process until its input closes.
inline constexpr const char* kIdleHostVariable = "AFTERNIGHT_SEAM_IDLE_HOST";

/// A host serving a hosted panel, as a case drives it.
class HostedPanelStandIn
{
public:
    HostedPanelStandIn()
    {
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        const QString name = QStringLiteral("an-hosted-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
        EXPECT_TRUE(m_server.listen(name)) << m_server.errorString().toStdString();

        // The host's process: this executable, idle until its input closes.
        m_process = std::make_unique<QProcess>();
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QString::fromLatin1(kIdleHostVariable), QStringLiteral("1"));
        m_process->setProcessEnvironment(environment);
        m_process->start(QCoreApplication::applicationFilePath(), QStringList());
        EXPECT_TRUE(m_process->waitForStarted(10000));
    }

    ~HostedPanelStandIn()
    {
        if (m_process && m_process->state() == QProcess::Running)
        {
            m_process->kill();
            m_process->waitForFinished(5000);
        }
    }

    HostedPanelStandIn(const HostedPanelStandIn&) = delete;
    HostedPanelStandIn& operator=(const HostedPanelStandIn&) = delete;

    /// Makes a session a running host's whose panel the application draws: connected to this stand-in, its process the
    /// idle one, both sides having stated the capability, its runtime ready unless said otherwise.
    void install(an::ui::extensions::ExtensionSessionController& controller, const std::string& sessionId,
                 const bool runtimeReady = true)
    {
        using an::ui::extensions::kHostedPanelCapability;
        auto& running = controller.running_session;
        running.prepared_session.session_id = sessionId;
        running.prepared_session.extension_id = "seam.extension";
        running.prepared_session.sdk_backend = "runtime";
        running.prepared_session.handshake_timeout_ms = 5000;
        running.handshake.hello.capabilities = {std::string(kHostedPanelCapability)};
        running.handshake.hello.native_process_window = true;
        running.handshake.ack.capabilities = {std::string(kHostedPanelCapability)};
        running.handshake.ack.native_process_window = true;
        running.handshake.ack.heartbeat_interval_ms = 600000;
        running.hosted_panel = true;
        running.state = an::ui::extensions::ExtensionHostSessionState::Active;
        running.runtime_ready = runtimeReady;
        running.initial_sdk_state_attempted = true;
        running.session_clock.start();

        auto socket = std::make_unique<QLocalSocket>();
        socket->connectToServer(m_server.serverName());
        EXPECT_TRUE(socket->waitForConnected(5000));
        EXPECT_TRUE(m_server.waitForNewConnection(5000));
        m_peer.reset(m_server.nextPendingConnection());
        m_peer->setParent(nullptr);
        running.socket = std::move(socket);
        running.process = std::move(m_process);
        m_sessionId = sessionId;
    }

    /// The far end of the session's socket.
    [[nodiscard]] QLocalSocket* peer() const noexcept
    {
        return m_peer.get();
    }

    /// Hands the far end of the session's socket over, to be served from a thread of its own as a host's process serves
    /// it; the stand-in reads and writes nothing more.
    [[nodiscard]] std::unique_ptr<QLocalSocket> releasePeer() noexcept
    {
        return std::move(m_peer);
    }

    /// Sends a message to the application.
    void send(an::ui::extensions::ExtensionProtocolMessage message)
    {
        message.session_id = m_sessionId;
        std::string error;
        const QByteArray framed = an::ui::extensions::frameProtocolMessage(message, &error);
        ASSERT_FALSE(framed.isEmpty()) << error;
        m_peer->write(framed);
        m_peer->flush();
    }

    /// Reads what the application sent, answering its pings; what a case has not taken stays for the next look.
    void read()
    {
        if (!m_peer)
        {
            return;
        }
        m_buffered.append(m_peer->readAll());
        for (;;)
        {
            an::ui::extensions::ExtensionProtocolMessage message;
            std::string error;
            const auto status = an::ui::extensions::tryConsumeFramedProtocolMessage(&m_buffered, &message, &error);
            if (status != an::ui::extensions::ProtocolFrameDecodeStatus::Complete)
            {
                return;
            }
            if (message.type == "runtime.ping")
            {
                an::ui::extensions::RuntimePingMessage ping;
                if (an::ui::extensions::runtimePingFromProtocolMessage(message, &ping))
                {
                    an::ui::extensions::RuntimePongMessage pong;
                    pong.session_id = m_sessionId;
                    pong.request_id = ping.request_id;
                    pong.sequence = ping.sequence;
                    send(an::ui::extensions::toProtocolMessage(pong));
                }
                continue;
            }
            m_received.push_back(std::move(message));
        }
    }

    /// Waits for the next message of a type the application sends, pumping meanwhile; none after `boundMs`.
    [[nodiscard]] std::optional<an::ui::extensions::ExtensionProtocolMessage>
    next(const std::string_view type, const std::function<void()>& pump, const int boundMs = 5000)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < boundMs)
        {
            pump();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (m_peer)
            {
                m_peer->waitForReadyRead(2);
            }
            read();
            const auto found = std::find_if(m_received.begin(), m_received.end(),
                                            [type](const auto& message) { return message.type == type; });
            if (found != m_received.end())
            {
                an::ui::extensions::ExtensionProtocolMessage taken = std::move(*found);
                m_received.erase(found);
                return taken;
            }
        }
        return std::nullopt;
    }

    /// How many messages of a type arrived and were not taken.
    [[nodiscard]] std::size_t waiting(const std::string_view type)
    {
        read();
        return static_cast<std::size_t>(std::count_if(m_received.begin(), m_received.end(),
                                                      [type](const auto& message) { return message.type == type; }));
    }

    /// Tells the application the extension's parameters (`ui.describe`).
    void describe(const QCborArray& params)
    {
        an::ui::extensions::PanelDescribeMessage described;
        described.params = params;
        send(an::ui::extensions::toProtocolMessage(described));
    }

    /// Answers a frame: the output it was lent written from its source, sample by sample.
    void answerPreview(const an::ui::extensions::ExtensionProtocolMessage& request,
                       const std::function<float(float)>& f, const std::string& status = "succeeded")
    {
        using namespace an::ui::extensions;
        ExecutionPreviewMessage preview;
        std::string error;
        ASSERT_TRUE(executionPreviewFromProtocolMessage(request, &preview, &error)) << error;
        const SharedBufferDescriptor* source = roleOf(preview.buffers, SharedBufferRole::Source);
        const SharedBufferDescriptor* output = roleOf(preview.buffers, SharedBufferRole::Preview);
        ASSERT_NE(source, nullptr);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->access_mode, SharedBufferAccessMode::ReadWrite);
        if (status == "succeeded")
        {
            write(*source, *output, f);
        }
        ExecutionResultMessage result;
        result.request_id = generateOpaqueToken();
        result.target_request_id = preview.request_id;
        result.result_kind = "preview";
        result.status = status;
        if (status == "succeeded")
        {
            result.output_buffers = {*output};
        }
        send(toProtocolMessage(result));
    }

    /// Answers an Apply: the destination it was lent written from its source, and the commit.
    void answerExecute(const an::ui::extensions::ExtensionProtocolMessage& request,
                       const std::function<float(float)>& f)
    {
        using namespace an::ui::extensions;
        ExecutionExecuteMessage execute;
        std::string error;
        ASSERT_TRUE(executionExecuteFromProtocolMessage(request, &execute, &error)) << error;
        const SharedBufferDescriptor* source = roleOf(execute.buffers, SharedBufferRole::Source);
        const SharedBufferDescriptor* destination = roleOf(execute.buffers, SharedBufferRole::Destination);
        ASSERT_NE(source, nullptr);
        ASSERT_NE(destination, nullptr);
        write(*source, *destination, f);
        ViewCommitMessage commit;
        commit.request_id = execute.request_id;
        commit.result_buffers = {*destination};
        send(toProtocolMessage(commit));
    }

    /// Answers a request the application cancelled, or one that failed, with its kind's `execution.result`.
    void answerWith(const std::string& targetRequestId, const std::string& kind, const std::string& status,
                    const std::string& detail = {})
    {
        an::ui::extensions::ExecutionResultMessage result;
        result.request_id = an::ui::extensions::generateOpaqueToken();
        result.target_request_id = targetRequestId;
        result.result_kind = kind;
        result.status = status;
        result.detail = detail;
        send(an::ui::extensions::toProtocolMessage(result));
    }

    /// Closes the session's socket from the host's end and stops its process, as a host that died.
    void die(an::ui::extensions::ExtensionSessionController& controller)
    {
        if (controller.running_session.process)
        {
            controller.running_session.process->kill();
            controller.running_session.process->waitForFinished(5000);
        }
        if (m_peer)
        {
            m_peer->abort();
        }
    }

private:
    [[nodiscard]] static const an::ui::extensions::SharedBufferDescriptor*
    roleOf(const std::vector<an::ui::extensions::SharedBufferDescriptor>& buffers,
           const an::ui::extensions::SharedBufferRole role)
    {
        const auto found = std::find_if(buffers.begin(), buffers.end(),
                                        [role](const auto& buffer) { return buffer.buffer_role == role; });
        return found != buffers.end() ? &*found : nullptr;
    }

    static void write(const an::ui::extensions::SharedBufferDescriptor& from,
                      const an::ui::extensions::SharedBufferDescriptor& to, const std::function<float(float)>& f)
    {
        using namespace an::ui::extensions;
        const SharedBufferManager manager(extensionHostManagedRootPath());
        an::Result<SharedImageBuffer> source = manager.openBuffer(from);
        an::Result<SharedImageBuffer> output = manager.openBuffer(to);
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        ASSERT_EQ(from.pixel_type, SharedBufferPixelType::Float32);
        for (int plane = 0; plane < from.channels; ++plane)
        {
            for (int y = 0; y < from.height; ++y)
            {
                for (int x = 0; x < from.width; ++x)
                {
                    const auto px = static_cast<std::uint32_t>(x);
                    const auto py = static_cast<std::uint32_t>(y);
                    const auto pp = static_cast<std::uint32_t>(plane);
                    writeBufferSample<float>(*output, px, py, pp, f(bufferSample<float>(*source, px, py, pp)));
                }
            }
        }
    }

    QLocalServer m_server;
    std::unique_ptr<QLocalSocket> m_peer;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_buffered;
    std::vector<an::ui::extensions::ExtensionProtocolMessage> m_received;
    std::string m_sessionId;
};

} // namespace an::tests::seam
