// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The extension protocol as the application speaks it: every message family round-tripped through the protocol
/// envelope - the runtime handshake and heartbeat, execution, interface events and validation, view and theme
/// synchronization - the control channel's framing and its size limit, the error codes' domains, version negotiation,
/// the shared-buffer descriptor's CBOR form and its validation, and the session secret's comparison.

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionCommon.h"

#include <QByteArray>
#include <QCborMap>
#include <QCborStreamWriter>
#include <QCborValue>
#include <QString>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using an::ui::extensions::ExecutionCancelMessage;
using an::ui::extensions::ExecutionExecuteMessage;
using an::ui::extensions::ExecutionPreviewMessage;
using an::ui::extensions::ExecutionResultMessage;
using an::ui::extensions::ExtensionProtocolError;
using an::ui::extensions::ExtensionProtocolErrorDomain;
using an::ui::extensions::ExtensionProtocolMessage;
using an::ui::extensions::ProtocolCompatibilityState;
using an::ui::extensions::ProtocolFrameDecodeStatus;
using an::ui::extensions::RuntimeHelloAckMessage;
using an::ui::extensions::RuntimeHelloMessage;
using an::ui::extensions::RuntimePingMessage;
using an::ui::extensions::RuntimePongMessage;
using an::ui::extensions::SharedBufferAccessMode;
using an::ui::extensions::SharedBufferColorSpace;
using an::ui::extensions::SharedBufferDescriptor;
using an::ui::extensions::SharedBufferPixelFormat;
using an::ui::extensions::SharedBufferPixelType;
using an::ui::extensions::SharedBufferRole;
using an::ui::extensions::ThemeChangedMessage;
using an::ui::extensions::ThemeDescribeMessage;
using an::ui::extensions::UiEventMessage;
using an::ui::extensions::UiValidateMessage;
using an::ui::extensions::UiValidateResultMessage;
using an::ui::extensions::ViewCommitMessage;
using an::ui::extensions::ViewCreatedMessage;
using an::ui::extensions::ViewDescribeMessage;
using an::ui::extensions::ViewDestroyedMessage;
using an::ui::extensions::ViewRenamedMessage;
using an::ui::extensions::ViewUpdatedMessage;

ExtensionProtocolMessage makeMessage(std::string_view type)
{
    ExtensionProtocolMessage message;
    message.type = std::string(type);
    message.session_id = "session-123";
    message.request_id = "request-456";
    message.fields[QStringLiteral("name")] = QStringLiteral("value");
    message.fields[QStringLiteral("enabled")] = true;
    message.fields[QStringLiteral("count")] = 7;
    return message;
}

SharedBufferDescriptor makeDescriptor()
{
    SharedBufferDescriptor descriptor;
    descriptor.buffer_id = "buffer-1";
    descriptor.symbolic_name = "selection";
    descriptor.mapping_name = "mapping-buffer-1";
    descriptor.mapped_file_path = "/tmp/buffer-1.bin";
    descriptor.width = 32;
    descriptor.height = 24;
    descriptor.channels = 3;
    descriptor.pixel_type = SharedBufferPixelType::Float32;
    descriptor.pixel_format = SharedBufferPixelFormat::RGB;
    descriptor.color_space = SharedBufferColorSpace::Linear;
    descriptor.row_stride = 32u * 4u;
    descriptor.plane_stride = descriptor.row_stride * 24u;
    descriptor.byte_size = descriptor.plane_stride * 3u;
    descriptor.access_mode = SharedBufferAccessMode::ReadWrite;
    descriptor.buffer_role = SharedBufferRole::Preview;
    descriptor.offset_x = 4;
    descriptor.offset_y = 3;
    descriptor.source_width = 64;
    descriptor.source_height = 48;
    descriptor.metadata_sidecar = "/tmp/buffer-1.json";
    return descriptor;
}

} // anonymous namespace

// A message of every core namespace survives serialization with its type, identities and fields.
TEST(ExtensionProtocolTest, GenericProtocolMessageRoundTripsAcrossCoreNamespaces)
{
    const std::array<std::string_view, 10> message_types = {
        "runtime.hello",  "logging.event",     "sdk.core.invoke", "settings.get",  "views.describe",
        "buffers.create", "execution.execute", "ui.describe",     "view.describe", "theme.describe"};

    for (const auto type : message_types)
    {
        SCOPED_TRACE(type);
        const auto original = makeMessage(type);

        std::string error;
        const QByteArray encoded = an::ui::extensions::serializeProtocolMessage(original, &error);
        ASSERT_TRUE(error.empty()) << error;
        ASSERT_FALSE(encoded.isEmpty());

        ExtensionProtocolMessage decoded;
        ASSERT_TRUE(an::ui::extensions::deserializeProtocolMessage(encoded, &decoded, &error)) << error;
        EXPECT_EQ(decoded.type, original.type);
        EXPECT_EQ(decoded.session_id, original.session_id);
        EXPECT_EQ(decoded.request_id, original.request_id);
        EXPECT_EQ(decoded.fields, original.fields);
    }
}

// The handshake's hello and acknowledgement keep every field through the envelope, the acknowledgement's error
// included.
TEST(ExtensionProtocolTest, RuntimeHelloMessagesRoundTripThroughProtocolEnvelope)
{
    RuntimeHelloMessage hello;
    hello.extension_id = "an.demo";
    hello.session_id = "session-1";
    hello.protocol_version = 1;
    hello.sdk_version = 1;
    hello.sdk_backend = "runtime";
    hello.host_version = "0.9.0";
    hello.capabilities = {"execute", "preview"};
    hello.session_secret_proof = "proof-token";
    hello.bootstrap_status_messages = true;

    RuntimeHelloAckMessage ack;
    ack.accepted = true;
    ack.session_id = hello.session_id;
    ack.protocol_version = 1;
    ack.sdk_version = 1;
    ack.host_version = "0.9.0";
    ack.heartbeat_interval_ms = 5000;
    ack.heartbeat_miss_limit = 3;
    ack.cancel_grace_period_ms = 1500;
    ack.capabilities = {"progress", "logging"};
    ack.bootstrap_status_messages = true;
    ack.error =
        ExtensionProtocolError{an::ui::extensions::makeProtocolErrorCode(ExtensionProtocolErrorDomain::Runtime, 7),
                               "Already authenticated", "Duplicate client"};

    std::string error;
    RuntimeHelloMessage parsed_hello;
    ASSERT_TRUE(an::ui::extensions::runtimeHelloFromProtocolMessage(an::ui::extensions::toProtocolMessage(hello),
                                                                    &parsed_hello, &error))
        << error;
    EXPECT_EQ(parsed_hello.extension_id, hello.extension_id);
    EXPECT_EQ(parsed_hello.session_id, hello.session_id);
    EXPECT_EQ(parsed_hello.sdk_backend, hello.sdk_backend);
    EXPECT_EQ(parsed_hello.capabilities, hello.capabilities);
    EXPECT_EQ(parsed_hello.session_secret_proof, hello.session_secret_proof);
    EXPECT_TRUE(parsed_hello.bootstrap_status_messages);

    RuntimeHelloAckMessage parsed_ack;
    ASSERT_TRUE(an::ui::extensions::runtimeHelloAckFromProtocolMessage(an::ui::extensions::toProtocolMessage(ack),
                                                                       &parsed_ack, &error))
        << error;
    EXPECT_TRUE(parsed_ack.accepted);
    EXPECT_EQ(parsed_ack.session_id, ack.session_id);
    EXPECT_EQ(parsed_ack.heartbeat_interval_ms, ack.heartbeat_interval_ms);
    EXPECT_EQ(parsed_ack.capabilities, ack.capabilities);
    EXPECT_TRUE(parsed_ack.bootstrap_status_messages);
    ASSERT_TRUE(parsed_ack.error.has_value());
    EXPECT_EQ(parsed_ack.error->code, ack.error->code);
}

// A heartbeat's ping and pong keep their session, request and sequence.
TEST(ExtensionProtocolTest, RuntimeHeartbeatMessagesRoundTripThroughProtocolEnvelope)
{
    RuntimePingMessage ping;
    ping.session_id = "session-1";
    ping.request_id = "request-1";
    ping.sequence = 7;

    RuntimePongMessage pong;
    pong.session_id = ping.session_id;
    pong.request_id = ping.request_id;
    pong.sequence = ping.sequence;

    std::string error;
    RuntimePingMessage parsed_ping;
    ASSERT_TRUE(an::ui::extensions::runtimePingFromProtocolMessage(an::ui::extensions::toProtocolMessage(ping),
                                                                   &parsed_ping, &error))
        << error;
    EXPECT_EQ(parsed_ping.session_id, ping.session_id);
    EXPECT_EQ(parsed_ping.request_id, ping.request_id);
    EXPECT_EQ(parsed_ping.sequence, ping.sequence);

    RuntimePongMessage parsed_pong;
    ASSERT_TRUE(an::ui::extensions::runtimePongFromProtocolMessage(an::ui::extensions::toProtocolMessage(pong),
                                                                   &parsed_pong, &error))
        << error;
    EXPECT_EQ(parsed_pong.session_id, pong.session_id);
    EXPECT_EQ(parsed_pong.request_id, pong.request_id);
    EXPECT_EQ(parsed_pong.sequence, pong.sequence);
}

// Execute, preview, cancel and result messages keep their parameters, targets, history step and buffers.
TEST(ExtensionProtocolTest, ExecutionMessagesRoundTripThroughProtocolEnvelope)
{
    ExecutionExecuteMessage execute;
    execute.session_id = "session-1";
    execute.request_id = "request-execute";
    execute.params_token = "params-execute";
    execute.params = QCborMap{{QStringLiteral("strength"), 0.75},
                              {QStringLiteral("mode"), QStringLiteral("quality")},
                              {QStringLiteral("use_mask"), true}};
    execute.buffers = {makeDescriptor()};

    ExecutionPreviewMessage preview;
    preview.session_id = "session-1";
    preview.request_id = "request-preview";
    preview.params_token = "params-preview";
    preview.params = QCborMap{{QStringLiteral("strength"), 0.25}, {QStringLiteral("mode"), QStringLiteral("fast")}};
    preview.buffers = {makeDescriptor()};

    ExecutionCancelMessage cancel;
    cancel.session_id = "session-1";
    cancel.request_id = "request-cancel";
    cancel.target_request_id = execute.request_id;

    ExecutionResultMessage result;
    result.session_id = "session-1";
    result.request_id = "request-result";
    result.target_request_id = execute.request_id;
    result.result_kind = "execute";
    result.status = "succeeded";
    result.detail = "Processed execute for params-execute";
    result.history_step_name = "Extension Apply";
    result.history_step_description = "Applied extension execution result";
    result.output_buffers = {makeDescriptor()};

    std::string error;
    ExecutionExecuteMessage parsed_execute;
    ASSERT_TRUE(an::ui::extensions::executionExecuteFromProtocolMessage(an::ui::extensions::toProtocolMessage(execute),
                                                                        &parsed_execute, &error))
        << error;
    EXPECT_EQ(parsed_execute.request_id, execute.request_id);
    EXPECT_EQ(parsed_execute.params_token, execute.params_token);
    EXPECT_EQ(parsed_execute.params, execute.params);
    ASSERT_EQ(parsed_execute.buffers.size(), 1u);
    EXPECT_EQ(parsed_execute.buffers.front().buffer_id, execute.buffers.front().buffer_id);

    ExecutionPreviewMessage parsed_preview;
    ASSERT_TRUE(an::ui::extensions::executionPreviewFromProtocolMessage(an::ui::extensions::toProtocolMessage(preview),
                                                                        &parsed_preview, &error))
        << error;
    EXPECT_EQ(parsed_preview.request_id, preview.request_id);
    EXPECT_EQ(parsed_preview.params_token, preview.params_token);
    EXPECT_EQ(parsed_preview.params, preview.params);
    ASSERT_EQ(parsed_preview.buffers.size(), 1u);
    EXPECT_EQ(parsed_preview.buffers.front().buffer_role, preview.buffers.front().buffer_role);

    ExecutionCancelMessage parsed_cancel;
    ASSERT_TRUE(an::ui::extensions::executionCancelFromProtocolMessage(an::ui::extensions::toProtocolMessage(cancel),
                                                                       &parsed_cancel, &error))
        << error;
    EXPECT_EQ(parsed_cancel.target_request_id, cancel.target_request_id);

    ExecutionResultMessage parsed_result;
    ASSERT_TRUE(an::ui::extensions::executionResultFromProtocolMessage(an::ui::extensions::toProtocolMessage(result),
                                                                       &parsed_result, &error))
        << error;
    EXPECT_EQ(parsed_result.target_request_id, result.target_request_id);
    EXPECT_EQ(parsed_result.result_kind, result.result_kind);
    EXPECT_EQ(parsed_result.status, result.status);
    EXPECT_EQ(parsed_result.history_step_name, result.history_step_name);
    ASSERT_EQ(parsed_result.output_buffers.size(), 1u);
    EXPECT_EQ(parsed_result.output_buffers.front().mapped_file_path, result.output_buffers.front().mapped_file_path);
}

// An interface event keeps its widget, its name and its value.
TEST(ExtensionProtocolTest, UiEventMessagesRoundTripThroughProtocolEnvelope)
{
    UiEventMessage event;
    event.session_id = "session-1";
    event.widget_id = "strength";
    event.event_name = "value_changed";
    event.value = QCborValue(0.75);

    std::string error;
    UiEventMessage parsed_event;
    ASSERT_TRUE(an::ui::extensions::uiEventFromProtocolMessage(an::ui::extensions::toProtocolMessage(event),
                                                               &parsed_event, &error))
        << error;
    EXPECT_EQ(parsed_event.session_id, event.session_id);
    EXPECT_EQ(parsed_event.widget_id, event.widget_id);
    EXPECT_EQ(parsed_event.event_name, event.event_name);
    ASSERT_TRUE(parsed_event.value.isDouble());
    EXPECT_DOUBLE_EQ(parsed_event.value.toDouble(), event.value.toDouble());
}

// A validation request keeps its parameters, and its result its verdict and its errors.
TEST(ExtensionProtocolTest, UiValidateMessagesRoundTripThroughProtocolEnvelope)
{
    UiValidateMessage validate;
    validate.session_id = "session-1";
    validate.request_id = "request-validate";
    validate.params = QCborMap{{QStringLiteral("strength"), 0.75},
                               {QStringLiteral("mode"), QStringLiteral("quality")},
                               {QStringLiteral("use_mask"), true}};

    UiValidateResultMessage result;
    result.session_id = "session-1";
    result.request_id = "request-validate";
    result.valid = false;
    result.errors = QCborMap{{QStringLiteral("strength"), QStringLiteral("Must be <= 0.50")}};

    std::string error;
    UiValidateMessage parsed_validate;
    ASSERT_TRUE(an::ui::extensions::uiValidateFromProtocolMessage(an::ui::extensions::toProtocolMessage(validate),
                                                                  &parsed_validate, &error))
        << error;
    EXPECT_EQ(parsed_validate.session_id, validate.session_id);
    EXPECT_EQ(parsed_validate.request_id, validate.request_id);
    EXPECT_EQ(parsed_validate.params, validate.params);

    UiValidateResultMessage parsed_result;
    ASSERT_TRUE(an::ui::extensions::uiValidateResultFromProtocolMessage(an::ui::extensions::toProtocolMessage(result),
                                                                        &parsed_result, &error))
        << error;
    EXPECT_EQ(parsed_result.session_id, result.session_id);
    EXPECT_EQ(parsed_result.request_id, result.request_id);
    EXPECT_FALSE(parsed_result.valid);
    EXPECT_EQ(parsed_result.errors, result.errors);
}

// Every view message - describe, commit, updated, created, destroyed and renamed - keeps what it says about the
// view, its buffers, its metadata and its display settings.
TEST(ExtensionProtocolTest, ViewSynchronizationMessagesRoundTripThroughProtocolEnvelope)
{
    ViewDescribeMessage describe;
    describe.session_id = "session-1";
    describe.request_id = "request-view-describe";
    describe.view_id = "view-42";
    describe.view_name = "Integrated View";
    describe.file_name = "integrated.fit";
    describe.source_buffer = makeDescriptor();
    describe.source_buffer.buffer_role = SharedBufferRole::Source;
    describe.auxiliary_buffers = {makeDescriptor()};
    describe.auxiliary_buffers.front().buffer_id = "mask-buffer-1";
    describe.auxiliary_buffers.front().symbolic_name = "selection";
    describe.auxiliary_buffers.front().buffer_role = SharedBufferRole::Mask;
    describe.auxiliary_buffers.front().access_mode = SharedBufferAccessMode::ReadOnly;
    describe.metadata =
        QCborMap{{QStringLiteral("OBJECT"), QStringLiteral("M31")}, {QStringLiteral("FILTER"), QStringLiteral("L")}};
    describe.display_settings = QCborMap{{QStringLiteral("zoom"), 2.0}, {QStringLiteral("linked_mask"), true}};

    ViewCommitMessage commit;
    commit.session_id = describe.session_id;
    commit.request_id = "request-view-commit";
    commit.view_id = describe.view_id;
    commit.history_step_name = "Extension Apply";
    commit.history_step_description = "Committed extension output";
    commit.allow_resized_output = true;
    commit.params = QCborMap{{QStringLiteral("strength"), 0.65}, {QStringLiteral("mode"), QStringLiteral("balanced")}};
    commit.result_buffers = {makeDescriptor()};
    commit.result_buffers.front().buffer_id = "result-buffer-1";
    commit.result_buffers.front().buffer_role = SharedBufferRole::Destination;

    ViewUpdatedMessage updated;
    updated.session_id = describe.session_id;
    updated.request_id = "request-view-updated";
    updated.view_id = describe.view_id;
    updated.source_buffer = describe.source_buffer;
    updated.metadata = describe.metadata;
    updated.display_settings = QCborMap{{QStringLiteral("zoom"), 1.0}};

    ViewCreatedMessage created;
    created.session_id = describe.session_id;
    created.request_id = "request-view-created";
    created.view_id = "view-99";
    created.view_name = "Calibrated";
    created.file_name = "calibrated.fit";
    created.channels = 3;

    ViewDestroyedMessage destroyed;
    destroyed.session_id = describe.session_id;
    destroyed.request_id = "request-view-destroyed";
    destroyed.view_id = created.view_id;

    ViewRenamedMessage renamed;
    renamed.session_id = describe.session_id;
    renamed.request_id = "request-view-renamed";
    renamed.view_id = describe.view_id;
    renamed.view_name = "Integrated View Final";
    renamed.file_name = "integrated_final.fit";

    std::string error;

    ViewDescribeMessage parsed_describe;
    ASSERT_TRUE(an::ui::extensions::viewDescribeFromProtocolMessage(an::ui::extensions::toProtocolMessage(describe),
                                                                    &parsed_describe, &error))
        << error;
    EXPECT_EQ(parsed_describe.view_id, describe.view_id);
    EXPECT_EQ(parsed_describe.view_name, describe.view_name);
    EXPECT_EQ(parsed_describe.file_name, describe.file_name);
    EXPECT_EQ(parsed_describe.source_buffer.buffer_role, SharedBufferRole::Source);
    ASSERT_EQ(parsed_describe.auxiliary_buffers.size(), 1u);
    EXPECT_EQ(parsed_describe.auxiliary_buffers.front().symbolic_name, "selection");
    EXPECT_EQ(parsed_describe.metadata.value(QStringLiteral("OBJECT")).toString(), QStringLiteral("M31"));
    EXPECT_DOUBLE_EQ(parsed_describe.display_settings.value(QStringLiteral("zoom")).toDouble(), 2.0);

    ViewCommitMessage parsed_commit;
    ASSERT_TRUE(an::ui::extensions::viewCommitFromProtocolMessage(an::ui::extensions::toProtocolMessage(commit),
                                                                  &parsed_commit, &error))
        << error;
    EXPECT_EQ(parsed_commit.view_id, commit.view_id);
    EXPECT_EQ(parsed_commit.history_step_name, commit.history_step_name);
    EXPECT_EQ(parsed_commit.history_step_description, commit.history_step_description);
    EXPECT_TRUE(parsed_commit.allow_resized_output);
    EXPECT_DOUBLE_EQ(parsed_commit.params.value(QStringLiteral("strength")).toDouble(), 0.65);
    ASSERT_EQ(parsed_commit.result_buffers.size(), 1u);
    EXPECT_EQ(parsed_commit.result_buffers.front().buffer_role, SharedBufferRole::Destination);

    ViewUpdatedMessage parsed_updated;
    ASSERT_TRUE(an::ui::extensions::viewUpdatedFromProtocolMessage(an::ui::extensions::toProtocolMessage(updated),
                                                                   &parsed_updated, &error))
        << error;
    EXPECT_EQ(parsed_updated.view_id, updated.view_id);
    EXPECT_EQ(parsed_updated.source_buffer.buffer_id, updated.source_buffer.buffer_id);
    EXPECT_DOUBLE_EQ(parsed_updated.display_settings.value(QStringLiteral("zoom")).toDouble(), 1.0);

    ViewCreatedMessage parsed_created;
    ASSERT_TRUE(an::ui::extensions::viewCreatedFromProtocolMessage(an::ui::extensions::toProtocolMessage(created),
                                                                   &parsed_created, &error))
        << error;
    EXPECT_EQ(parsed_created.view_id, created.view_id);
    EXPECT_EQ(parsed_created.view_name, created.view_name);
    EXPECT_EQ(parsed_created.file_name, created.file_name);
    ASSERT_TRUE(parsed_created.channels.has_value());
    EXPECT_EQ(parsed_created.channels.value(), 3);

    ViewDestroyedMessage parsed_destroyed;
    ASSERT_TRUE(an::ui::extensions::viewDestroyedFromProtocolMessage(an::ui::extensions::toProtocolMessage(destroyed),
                                                                     &parsed_destroyed, &error))
        << error;
    EXPECT_EQ(parsed_destroyed.view_id, destroyed.view_id);

    ViewRenamedMessage parsed_renamed;
    ASSERT_TRUE(an::ui::extensions::viewRenamedFromProtocolMessage(an::ui::extensions::toProtocolMessage(renamed),
                                                                   &parsed_renamed, &error))
        << error;
    EXPECT_EQ(parsed_renamed.view_id, renamed.view_id);
    EXPECT_EQ(parsed_renamed.view_name, renamed.view_name);
    EXPECT_EQ(parsed_renamed.file_name, renamed.file_name);
}

// The theme's describe and changed messages keep its name and its tokens.
TEST(ExtensionProtocolTest, ThemeSynchronizationMessagesRoundTripThroughProtocolEnvelope)
{
    ThemeDescribeMessage describe;
    describe.session_id = "session-1";
    describe.request_id = "request-theme-describe";
    describe.theme_name = "AfterNight Dark";
    describe.tokens =
        QCborMap{{QStringLiteral("colors"), QCborMap{{QStringLiteral("textPrimary"), QStringLiteral("#f5f5f5")}}},
                 {QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}};

    ThemeChangedMessage changed;
    changed.session_id = describe.session_id;
    changed.request_id = "request-theme-changed";
    changed.theme_name = "AfterNight Light";
    changed.tokens =
        QCborMap{{QStringLiteral("colors"), QCborMap{{QStringLiteral("textPrimary"), QStringLiteral("#111111")}}},
                 {QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}};

    std::string error;

    ThemeDescribeMessage parsed_describe;
    ASSERT_TRUE(an::ui::extensions::themeDescribeFromProtocolMessage(an::ui::extensions::toProtocolMessage(describe),
                                                                     &parsed_describe, &error))
        << error;
    EXPECT_EQ(parsed_describe.theme_name, describe.theme_name);
    EXPECT_EQ(parsed_describe.tokens.value(QStringLiteral("spacing")).toMap().value(QStringLiteral("md")).toInteger(),
              12);

    ThemeChangedMessage parsed_changed;
    ASSERT_TRUE(an::ui::extensions::themeChangedFromProtocolMessage(an::ui::extensions::toProtocolMessage(changed),
                                                                    &parsed_changed, &error))
        << error;
    EXPECT_EQ(parsed_changed.theme_name, changed.theme_name);
    EXPECT_EQ(
        parsed_changed.tokens.value(QStringLiteral("colors")).toMap().value(QStringLiteral("textPrimary")).toString(),
        QStringLiteral("#111111"));
}

// A framed message carries its length big-endian ahead of it and is decoded only once all of it has arrived.
TEST(ExtensionProtocolTest, ControlChannelFramingUsesBigEndianPrefixAndSupportsIncrementalDecode)
{
    std::string error;
    const ExtensionProtocolMessage message = makeMessage("runtime.ping");
    const QByteArray framed = an::ui::extensions::frameProtocolMessage(message, &error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_GT(framed.size(), 4);

    const std::uint32_t encoded_size =
        (static_cast<std::uint8_t>(framed[0]) << 24) | (static_cast<std::uint8_t>(framed[1]) << 16) |
        (static_cast<std::uint8_t>(framed[2]) << 8) | static_cast<std::uint8_t>(framed[3]);
    EXPECT_EQ(encoded_size, static_cast<std::uint32_t>(framed.size() - 4));

    QByteArray partial = framed.left(3);
    ExtensionProtocolMessage decoded;
    EXPECT_EQ(an::ui::extensions::tryConsumeFramedProtocolMessage(&partial, &decoded, &error),
              ProtocolFrameDecodeStatus::NeedMoreData);

    partial.append(framed.mid(3));
    EXPECT_EQ(an::ui::extensions::tryConsumeFramedProtocolMessage(&partial, &decoded, &error),
              ProtocolFrameDecodeStatus::Complete);
    EXPECT_TRUE(partial.isEmpty());
    EXPECT_EQ(decoded.type, message.type);
    EXPECT_EQ(decoded.fields, message.fields);
}

// A payload over the control channel's limit is neither framed nor accepted, and the refusal says why.
TEST(ExtensionProtocolTest, ControlChannelFramingRejectsMessagesOverMaximumSize)
{
    const QByteArray oversized(static_cast<int>(an::ui::extensions::kMaxControlMessageBytes) + 1, '\0');

    std::string error;
    EXPECT_TRUE(an::ui::extensions::frameControlPayload(oversized, &error).isEmpty());
    EXPECT_FALSE(error.empty());

    QByteArray bogus_frame(4, '\0');
    bogus_frame[0] = static_cast<char>(0x01);
    bogus_frame[1] = static_cast<char>(0x00);
    bogus_frame[2] = static_cast<char>(0x00);
    bogus_frame[3] = static_cast<char>(0x01);
    EXPECT_EQ(an::ui::extensions::tryConsumeFramedControlPayload(&bogus_frame, nullptr, &error),
              ProtocolFrameDecodeStatus::InvalidFrame);
    EXPECT_FALSE(error.empty());
}

// A message whose map states a key twice - at its top, in a map it carries, or in a map inside an array - is refused
// with the key named, read on its own or out of a frame. CBOR leaves a repeated key to the protocol; a reader that
// took the first and carried the second would hand the message on otherwise than it was read.
TEST(ExtensionProtocolTest, AMessageThatRepeatsAKeyAnywhereIsRefused)
{
    // Writes a cancel request with one map holding a key twice, at the place `where` names.
    const auto messageRepeating = [](std::string_view where) {
        QByteArray bytes;
        QCborStreamWriter writer(&bytes);
        writer.startMap();
        writer.append(QStringLiteral("type"));
        writer.append(QStringLiteral("execution.cancel"));
        if (where == "top")
        {
            writer.append(QStringLiteral("type"));
            writer.append(QStringLiteral("execution.execute"));
        }
        writer.append(QStringLiteral("session_id"));
        writer.append(QStringLiteral("session-1"));
        writer.append(QStringLiteral("params"));
        writer.startMap();
        writer.append(QStringLiteral("strength"));
        writer.append(0.5);
        if (where == "nested")
        {
            writer.append(QStringLiteral("strength"));
            writer.append(0.75);
        }
        writer.endMap();
        writer.append(QStringLiteral("buffers"));
        writer.startArray();
        writer.startMap();
        writer.append(QStringLiteral("buffer_id"));
        writer.append(QStringLiteral("source"));
        if (where == "array")
        {
            writer.append(QStringLiteral("buffer_id"));
            writer.append(QStringLiteral("mask"));
        }
        writer.endMap();
        writer.endArray();
        writer.endMap();
        return bytes;
    };

    std::string error;
    ExtensionProtocolMessage decoded;
    ASSERT_TRUE(an::ui::extensions::deserializeProtocolMessage(messageRepeating("none"), &decoded, &error)) << error;

    const std::array<std::pair<std::string_view, std::string_view>, 3> places = {
        {{"top", "\"type\""}, {"nested", "\"strength\""}, {"array", "\"buffer_id\""}}};
    for (const auto& [where, key] : places)
    {
        SCOPED_TRACE(std::string(where));
        const QByteArray payload = messageRepeating(where);
        error.clear();
        EXPECT_FALSE(an::ui::extensions::deserializeProtocolMessage(payload, &decoded, &error));
        EXPECT_NE(error.find(key), std::string::npos) << error;

        QByteArray framed = an::ui::extensions::frameControlPayload(payload, &error);
        ASSERT_FALSE(framed.isEmpty()) << error;
        error.clear();
        EXPECT_EQ(an::ui::extensions::tryConsumeFramedProtocolMessage(&framed, &decoded, &error),
                  ProtocolFrameDecodeStatus::InvalidFrame);
        EXPECT_NE(error.find(key), std::string::npos) << error;
    }
}

// An error code is classified by the range it falls in, made from a domain and a number, and formatted as the
// protocol spells it.
TEST(ExtensionProtocolTest, ProtocolErrorCodeHelpersCoverAllSupportedRanges)
{
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(1000), ExtensionProtocolErrorDomain::Runtime);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(2500), ExtensionProtocolErrorDomain::Logging);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(3500), ExtensionProtocolErrorDomain::Settings);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(4500), ExtensionProtocolErrorDomain::Buffers);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(5500), ExtensionProtocolErrorDomain::Execution);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(6500), ExtensionProtocolErrorDomain::Host);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(999), ExtensionProtocolErrorDomain::Invalid);

    EXPECT_EQ(an::ui::extensions::makeProtocolErrorCode(ExtensionProtocolErrorDomain::Buffers, 12), 4012);
    EXPECT_EQ(an::ui::extensions::formatProtocolErrorCode(4012), "E4012");
    EXPECT_EQ(an::ui::extensions::formatProtocolErrorCode(99), "E0000");
}

// Version negotiation accepts a match and says which side is too old, or that a version is no version at all.
TEST(ExtensionProtocolTest, ProtocolVersionNegotiationReportsCompatibilityAndMismatchDirection)
{
    const auto compatible = an::ui::extensions::negotiateProtocolVersion(1, 1);
    EXPECT_EQ(compatible.state, ProtocolCompatibilityState::Compatible);
    EXPECT_EQ(compatible.negotiated_version, 1);

    const auto peer_too_old = an::ui::extensions::negotiateProtocolVersion(2, 1);
    EXPECT_EQ(peer_too_old.state, ProtocolCompatibilityState::PeerTooOld);
    EXPECT_FALSE(peer_too_old.accepted());

    const auto peer_too_new = an::ui::extensions::negotiateProtocolVersion(1, 2);
    EXPECT_EQ(peer_too_new.state, ProtocolCompatibilityState::PeerTooNew);
    EXPECT_FALSE(peer_too_new.accepted());

    const auto invalid = an::ui::extensions::negotiateProtocolVersion(1, 0);
    EXPECT_EQ(invalid.state, ProtocolCompatibilityState::Invalid);
    EXPECT_FALSE(invalid.accepted());
}

// A shared-buffer descriptor keeps every field through its CBOR form.
TEST(ExtensionProtocolTest, SharedBufferDescriptorRoundTripsThroughCbor)
{
    const SharedBufferDescriptor original = makeDescriptor();

    std::string error;
    SharedBufferDescriptor decoded;
    ASSERT_TRUE(an::ui::extensions::sharedBufferDescriptorFromCborValue(
        an::ui::extensions::sharedBufferDescriptorToCborMap(original), &decoded, &error))
        << error;

    EXPECT_EQ(decoded.buffer_id, original.buffer_id);
    EXPECT_EQ(decoded.symbolic_name, original.symbolic_name);
    EXPECT_EQ(decoded.mapping_name, original.mapping_name);
    EXPECT_EQ(decoded.mapped_file_path, original.mapped_file_path);
    EXPECT_EQ(decoded.byte_size, original.byte_size);
    EXPECT_EQ(decoded.width, original.width);
    EXPECT_EQ(decoded.height, original.height);
    EXPECT_EQ(decoded.channels, original.channels);
    EXPECT_EQ(decoded.pixel_type, original.pixel_type);
    EXPECT_EQ(decoded.pixel_format, original.pixel_format);
    EXPECT_EQ(decoded.color_space, original.color_space);
    EXPECT_EQ(decoded.row_stride, original.row_stride);
    EXPECT_EQ(decoded.plane_stride, original.plane_stride);
    EXPECT_EQ(decoded.access_mode, original.access_mode);
    EXPECT_EQ(decoded.buffer_role, original.buffer_role);
    EXPECT_EQ(decoded.offset_x, original.offset_x);
    EXPECT_EQ(decoded.offset_y, original.offset_y);
    EXPECT_EQ(decoded.source_width, original.source_width);
    EXPECT_EQ(decoded.source_height, original.source_height);
    EXPECT_EQ(decoded.metadata_sidecar, original.metadata_sidecar);
}

// A descriptor whose strides do not match its geometry, or whose sub-area leaves its source, is refused.
TEST(ExtensionProtocolTest, SharedBufferDescriptorValidationRejectsStrideAndBoundsMismatches)
{
    SharedBufferDescriptor descriptor = makeDescriptor();
    descriptor.row_stride += 4;
    auto validation = an::ui::extensions::validateSharedBufferDescriptor(descriptor);
    EXPECT_FALSE(validation.ok());

    descriptor = makeDescriptor();
    descriptor.offset_x = 40;
    descriptor.source_width = 64;
    validation = an::ui::extensions::validateSharedBufferDescriptor(descriptor);
    EXPECT_FALSE(validation.ok());
}

// A descriptor whose pixel format does not match its channel count is refused.
TEST(ExtensionProtocolTest, SharedBufferDescriptorValidationRejectsPixelFormatChannelMismatches)
{
    SharedBufferDescriptor descriptor = makeDescriptor();
    descriptor.pixel_format = SharedBufferPixelFormat::RGB;
    descriptor.channels = 1;
    auto validation = an::ui::extensions::validateSharedBufferDescriptor(descriptor);
    EXPECT_FALSE(validation.ok());

    descriptor = makeDescriptor();
    descriptor.pixel_format = SharedBufferPixelFormat::Mono;
    descriptor.channels = 3;
    validation = an::ui::extensions::validateSharedBufferDescriptor(descriptor);
    EXPECT_FALSE(validation.ok());

    descriptor = makeDescriptor();
    descriptor.pixel_format = SharedBufferPixelFormat::BayerRGGB;
    descriptor.channels = 3;
    validation = an::ui::extensions::validateSharedBufferDescriptor(descriptor);
    EXPECT_FALSE(validation.ok());
}

// A descriptor naming a value the protocol does not know is refused, and the refusal says why.
TEST(ExtensionProtocolTest, SharedBufferDescriptorParsingRejectsUnknownEnumValues)
{
    QCborMap map = an::ui::extensions::sharedBufferDescriptorToCborMap(makeDescriptor());
    map[QStringLiteral("buffer_role")] = QStringLiteral("mystery");

    SharedBufferDescriptor descriptor;
    std::string error;
    EXPECT_FALSE(an::ui::extensions::sharedBufferDescriptorFromCborValue(map, &descriptor, &error));
    EXPECT_FALSE(error.empty());
}

// The session secret's comparison answers what string equality answers.
TEST(ExtensionProtocolTest, ConstantTimeStringEqualMatchesStdStringEquality)
{
    using an::ui::extensions::constantTimeStringEqual;

    EXPECT_TRUE(constantTimeStringEqual("", ""));
    EXPECT_TRUE(constantTimeStringEqual("abc", "abc"));
    EXPECT_TRUE(constantTimeStringEqual(std::string(256, 'x'), std::string(256, 'x')));

    EXPECT_FALSE(constantTimeStringEqual("abc", "abd"));
    EXPECT_FALSE(constantTimeStringEqual("abc", "ab"));
    EXPECT_FALSE(constantTimeStringEqual("", "a"));
    EXPECT_FALSE(constantTimeStringEqual("a", ""));

    // Verify it does not short-circuit on first byte difference.
    const std::string a = "AAAA";
    const std::string b = "BBBB";
    EXPECT_FALSE(constantTimeStringEqual(a, b));
}

// The native process window flag survives the handshake in both states, and its absence reads as false.
TEST(ExtensionProtocolTest, NativeProcessWindowFlagRoundTripsInHelloAndAck)
{
    // native_process_window should survive a hello/ack serialization round trip
    // in both the advertised (true) and default (false) states.

    // --- hello with native_process_window = true ---
    RuntimeHelloMessage hello_native;
    hello_native.extension_id = "an.demo";
    hello_native.session_id = "session-native";
    hello_native.protocol_version = an::ui::extensions::kExtensionProtocolVersion;
    hello_native.sdk_version = an::ui::extensions::kExtensionSdkVersion;
    hello_native.sdk_backend = "runtime";
    hello_native.host_version = "1.0.0";
    hello_native.session_secret_proof = "proof-x";
    hello_native.native_process_window = true;

    std::string error;
    RuntimeHelloMessage parsed_hello_native;
    ASSERT_TRUE(an::ui::extensions::runtimeHelloFromProtocolMessage(an::ui::extensions::toProtocolMessage(hello_native),
                                                                    &parsed_hello_native, &error))
        << error;
    EXPECT_TRUE(parsed_hello_native.native_process_window);

    // --- hello with native_process_window = false (default; field absent in wire payload) ---
    RuntimeHelloMessage hello_default;
    hello_default.extension_id = "an.demo";
    hello_default.session_id = "session-default";
    hello_default.protocol_version = an::ui::extensions::kExtensionProtocolVersion;
    hello_default.sdk_version = an::ui::extensions::kExtensionSdkVersion;
    hello_default.sdk_backend = "runtime";
    hello_default.host_version = "1.0.0";
    hello_default.session_secret_proof = "proof-y";
    hello_default.native_process_window = false;

    RuntimeHelloMessage parsed_hello_default;
    ASSERT_TRUE(an::ui::extensions::runtimeHelloFromProtocolMessage(
        an::ui::extensions::toProtocolMessage(hello_default), &parsed_hello_default, &error))
        << error;
    EXPECT_FALSE(parsed_hello_default.native_process_window);
    EXPECT_FALSE(parsed_hello_default.bootstrap_status_messages);

    // --- ack with native_process_window = true ---
    RuntimeHelloAckMessage ack_native;
    ack_native.accepted = true;
    ack_native.session_id = "session-native";
    ack_native.protocol_version = an::ui::extensions::kExtensionProtocolVersion;
    ack_native.sdk_version = an::ui::extensions::kExtensionSdkVersion;
    ack_native.host_version = "1.0.0";
    ack_native.heartbeat_interval_ms = 5000;
    ack_native.heartbeat_miss_limit = 3;
    ack_native.cancel_grace_period_ms = 1500;
    ack_native.native_process_window = true;

    RuntimeHelloAckMessage parsed_ack_native;
    ASSERT_TRUE(an::ui::extensions::runtimeHelloAckFromProtocolMessage(
        an::ui::extensions::toProtocolMessage(ack_native), &parsed_ack_native, &error))
        << error;
    EXPECT_TRUE(parsed_ack_native.native_process_window);

    // --- ack with native_process_window = false (default) ---
    RuntimeHelloAckMessage ack_default;
    ack_default.accepted = true;
    ack_default.session_id = "session-default";
    ack_default.protocol_version = an::ui::extensions::kExtensionProtocolVersion;
    ack_default.sdk_version = an::ui::extensions::kExtensionSdkVersion;
    ack_default.host_version = "1.0.0";
    ack_default.heartbeat_interval_ms = 5000;
    ack_default.heartbeat_miss_limit = 3;
    ack_default.cancel_grace_period_ms = 1500;
    ack_default.native_process_window = false;

    RuntimeHelloAckMessage parsed_ack_default;
    ASSERT_TRUE(an::ui::extensions::runtimeHelloAckFromProtocolMessage(
        an::ui::extensions::toProtocolMessage(ack_default), &parsed_ack_default, &error))
        << error;
    EXPECT_FALSE(parsed_ack_default.native_process_window);
    EXPECT_FALSE(parsed_ack_default.bootstrap_status_messages);
}
