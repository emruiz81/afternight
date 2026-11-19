// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The full host when the application draws the session's panel: it states that it can serve one, shows no window when
/// the application's acknowledgement says it draws the panel and its window as before when it does not, tells the
/// parameter list in a description the application reads back whole, reads what a person did in the panel and refuses
/// what it does not serve, answers an action with what it set or a plain reason, and takes the output the application
/// lent a request rather than making one.

#include "HostedPanelService.h"

#include "app/AfterNightExtensionProtocol.h"

#include <QCborArray>
#include <QCborMap>
#include <QJsonArray>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

using namespace an::ui::extensions;

[[nodiscard]] RuntimeHelloAckMessage acknowledgement(const bool nativeWindow, const bool hostedPanel)
{
    RuntimeHelloAckMessage ack;
    ack.native_process_window = nativeWindow;
    ack.capabilities = {"execute", "preview"};
    if (hostedPanel)
    {
        ack.capabilities.emplace_back(kHostedPanelCapability);
    }
    return ack;
}

[[nodiscard]] ExtensionProtocolMessage panelEvent(const std::string& widget, const std::string& name,
                                                  const QCborValue& value)
{
    UiEventMessage event;
    event.session_id = "session-windowless";
    event.widget_id = widget;
    event.event_name = name;
    event.value = value;
    return toProtocolMessage(event);
}

[[nodiscard]] SharedBufferDescriptor buffer(const std::string& id, const SharedBufferRole role,
                                            const SharedBufferAccessMode access)
{
    SharedBufferDescriptor descriptor;
    descriptor.buffer_id = id;
    descriptor.buffer_role = role;
    descriptor.access_mode = access;
    return descriptor;
}

TEST(Windowless, TheHostStatesItCanServeAPanel)
{
    const std::vector<std::string_view> stated = fullHostCapabilities();
    for (const std::string_view capability :
         {kHostedPanelCapability, kSessionActivityCapability, kWindowRaiseCapability})
    {
        EXPECT_NE(std::find(stated.begin(), stated.end(), capability), stated.end()) << capability;
    }
}

TEST(Windowless, TheCapabilityKeepsTheWindowAway)
{
    const RuntimeHelloAckMessage hosted = acknowledgement(true, true);
    EXPECT_TRUE(servesHostedPanel(hosted));
    EXPECT_FALSE(showsNativeWindow(hosted, false)) << "the application draws the panel";
    EXPECT_FALSE(showsNativeWindow(hosted, true));
}

TEST(Windowless, WithoutItTheNativeWindowAppearsAsToday)
{
    const RuntimeHelloAckMessage windowed = acknowledgement(true, false);
    EXPECT_FALSE(servesHostedPanel(windowed));
    EXPECT_TRUE(showsNativeWindow(windowed, false));
    EXPECT_FALSE(showsNativeWindow(windowed, true)) << "a headless platform shows no window, as before";
    EXPECT_FALSE(showsNativeWindow(acknowledgement(false, false), false)) << "nor a session the application drew";
}

TEST(Windowless, TellsTheParameterListTheApplicationReadsBackWhole)
{
    QJsonArray params;
    params.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("window_meta")},
                              {QStringLiteral("type"), QStringLiteral("meta")},
                              {QStringLiteral("sub_area"), true}});
    params.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("strength")},
                              {QStringLiteral("type"), QStringLiteral("float")},
                              {QStringLiteral("default"), 0.45}});
    const ExtensionProtocolMessage told = hostedPanelDescription("session-windowless", params);
    EXPECT_EQ(told.type, "ui.describe");
    EXPECT_TRUE(told.request_id.empty()) << "a notification: the host waits for no answer";

    PanelDescribeMessage read;
    std::string error;
    ASSERT_TRUE(panelDescribeFromProtocolMessage(told, &read, &error)) << error;
    EXPECT_EQ(read.session_id, "session-windowless");
    EXPECT_EQ(read.params.toJsonArray(), params);
}

TEST(Windowless, ReadsWhatAPersonDidInThePanel)
{
    const QCborMap params{{QStringLiteral("strength"), 0.6}};
    HostedPanelEvent event;
    std::string error;
    ASSERT_TRUE(
        hostedPanelEventFrom(panelEvent("panel", "parameters_changed",
                                        QCborMap{{QStringLiteral("params"), params},
                                                 {QStringLiteral("changed"), QCborArray{QStringLiteral("strength")}},
                                                 {QStringLiteral("reason"), QStringLiteral("user")}}),
                             &event, &error))
        << error;
    EXPECT_EQ(event.kind, HostedPanelEvent::Kind::ParametersChanged);
    EXPECT_EQ(event.params, params);
    EXPECT_EQ(event.changedKeys, QStringList{QStringLiteral("strength")});
    EXPECT_EQ(event.reason, QStringLiteral("user"));

    ASSERT_TRUE(hostedPanelEventFrom(panelEvent("auto", "clicked", QCborMap{{QStringLiteral("params"), params}}),
                                     &event, &error))
        << error;
    EXPECT_EQ(event.kind, HostedPanelEvent::Kind::Action);
    EXPECT_EQ(event.actionId, "auto");
    EXPECT_EQ(event.params, params);

    const QCborMap record{{QStringLiteral("resolved_cli_executable"), QStringLiteral("C:/tools/tool.exe")}};
    ASSERT_TRUE(hostedPanelEventFrom(panelEvent("panel", "tool_record", record), &event, &error)) << error;
    EXPECT_EQ(event.kind, HostedPanelEvent::Kind::ToolRecord);
    EXPECT_EQ(event.toolRecord, record);
}

TEST(Windowless, RefusesAnEventItDoesNotServe)
{
    HostedPanelEvent event;
    std::string error;
    EXPECT_FALSE(hostedPanelEventFrom(panelEvent("panel", "dragged", QCborMap()), &event, &error));
    EXPECT_NE(error.find("parameters_changed"), std::string::npos) << error;
    EXPECT_FALSE(
        hostedPanelEventFrom(panelEvent("panel", "parameters_changed", QStringLiteral("strength")), &event, &error));
    EXPECT_FALSE(hostedPanelEventFrom(
        panelEvent("panel", "parameters_changed", QCborMap{{QStringLiteral("params"), QCborMap()}}), &event, &error))
        << "the changed keys and the reason are stated";
    EXPECT_FALSE(hostedPanelEventFrom(panelEvent("", "clicked", QCborMap{{QStringLiteral("params"), QCborMap()}}),
                                      &event, &error))
        << "an action is named";
}

TEST(Windowless, AnswersAnActionWithWhatItSetOrAPlainReason)
{
    const QCborMap values{{QStringLiteral("strength"), 0.75}};
    PanelPatchMessage read;
    std::string error;
    ASSERT_TRUE(panelPatchFromProtocolMessage(hostedActionAnswer("session-windowless", "auto", values), &read, &error))
        << error;
    EXPECT_EQ(read.action_id, "auto");
    EXPECT_EQ(read.values, values);
    EXPECT_TRUE(read.error.empty());

    // A failure says why, in plain text within the bound, and sets nothing.
    const std::string failure = std::string("the tool\x01 failed\nsee its log") + std::string(3000u, 'x');
    ASSERT_TRUE(
        panelPatchFromProtocolMessage(hostedActionAnswer("session-windowless", "auto", values, failure), &read, &error))
        << error;
    EXPECT_TRUE(read.values.isEmpty());
    EXPECT_EQ(read.error.rfind("the tool failed\nsee its log", 0), 0u) << read.error;
    EXPECT_LE(read.error.size(), kPanelPatchErrorLimit);
}

TEST(Windowless, TakesTheOutputTheApplicationLent)
{
    const std::vector<SharedBufferDescriptor> frame{
        buffer("source", SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite),
        buffer("output", SharedBufferRole::Preview, SharedBufferAccessMode::ReadWrite),
        buffer("selection", SharedBufferRole::Mask, SharedBufferAccessMode::ReadOnly)};
    const SharedBufferDescriptor* lent = lentOutputOf("preview", frame);
    ASSERT_NE(lent, nullptr);
    EXPECT_EQ(lent->buffer_id, "output");
    EXPECT_EQ(lentOutputOf("execute", frame), nullptr) << "a frame's output is no Apply's destination";

    const std::vector<SharedBufferDescriptor> apply{
        buffer("source", SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite),
        buffer("destination", SharedBufferRole::Destination, SharedBufferAccessMode::ReadWrite)};
    ASSERT_NE(lentOutputOf("execute", apply), nullptr);
    EXPECT_EQ(lentOutputOf("execute", apply)->buffer_id, "destination");

    // A request that lends no writable output is answered from an output the host makes, as any other request is.
    const std::vector<SharedBufferDescriptor> readOnly{
        buffer("source", SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite),
        buffer("output", SharedBufferRole::Preview, SharedBufferAccessMode::ReadOnly)};
    EXPECT_EQ(lentOutputOf("preview", readOnly), nullptr);
}

} // namespace
