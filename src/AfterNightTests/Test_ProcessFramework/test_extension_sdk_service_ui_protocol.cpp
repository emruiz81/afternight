// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the SDK service does with an extension's interface requests: a declarative description validated and kept, its
/// interaction flags kept, an unknown widget or a duplicate identity refused; a patch validated against the description
/// it follows, refused without one, for an unknown widget or while events are off; and the error, confirmation and
/// open-image requests handed to the handlers the application configured, refused without one. The view lifecycle
/// messages keep the selector's tooltip through the envelope.

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionSdkService.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QString>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

namespace
{

an::ui::extensions::ExtensionProtocolError
decodeProtocolError(const an::ui::extensions::ExtensionProtocolMessage& response)
{
    std::string error;
    an::ui::extensions::ExtensionProtocolError protocol_error;
    EXPECT_TRUE(an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")),
                                                               &protocol_error, &error))
        << error;
    return protocol_error;
}

an::ui::extensions::ExtensionProtocolMessage makeProtocolMessage(const std::string& type, const std::string& session_id,
                                                                 const std::string& request_id, QCborMap fields)
{
    an::ui::extensions::ExtensionProtocolMessage message;
    message.type = type;
    message.session_id = session_id;
    message.request_id = request_id;
    message.fields = std::move(fields);
    return message;
}

QCborMap makeWidgetNode(const QString& id, const QString& widget, const QString& label = QString(),
                        const QCborArray& children = {})
{
    QCborMap node;
    node.insert(QStringLiteral("id"), id);
    node.insert(QStringLiteral("widget"), widget);
    if (!label.isEmpty())
    {
        node.insert(QStringLiteral("label"), label);
    }
    if (!children.isEmpty())
    {
        node.insert(QStringLiteral("children"), children);
    }
    return node;
}

QCborMap makeUiDescribeFields(const QCborArray& children)
{
    QCborMap root;
    root.insert(QStringLiteral("widget"), QStringLiteral("column"));
    root.insert(QStringLiteral("children"), children);

    QCborMap tree;
    tree.insert(QStringLiteral("root"), root);

    QCborMap fields;
    fields.insert(QStringLiteral("tree"), tree);
    return fields;
}

QCborMap makePatchOperation(const QString& id, const QString& property_name, const QCborValue& value)
{
    QCborMap operation;
    operation.insert(QStringLiteral("id"), id);
    operation.insert(QStringLiteral("prop"), property_name);
    operation.insert(QStringLiteral("value"), value);
    return operation;
}

QCborMap makeUiPatchFields(const QCborArray& patches)
{
    QCborMap fields;
    fields.insert(QStringLiteral("patches"), patches);
    return fields;
}

} // anonymous namespace

// A created and a renamed view keep the selector's tooltip through the envelope, and a created view its channels.
TEST(ExtensionProtocolTest, ViewLifecycleMessagesRoundTripSelectorTooltip)
{
    an::ui::extensions::ViewCreatedMessage created;
    created.session_id = "session-1";
    created.request_id = "request-created";
    created.view_id = "view-1";
    created.view_name = "Alpha";
    created.file_name = "/tmp/internal.dmp";
    created.selector_tooltip = "<b>Alpha</b><br><b>Source:</b> In-memory file";
    created.channels = 3;

    std::string error;
    an::ui::extensions::ViewCreatedMessage parsed_created;
    ASSERT_TRUE(an::ui::extensions::viewCreatedFromProtocolMessage(an::ui::extensions::toProtocolMessage(created),
                                                                   &parsed_created, &error))
        << error;
    EXPECT_EQ(parsed_created.selector_tooltip, created.selector_tooltip);
    ASSERT_TRUE(parsed_created.channels.has_value());
    EXPECT_EQ(*parsed_created.channels, 3);

    an::ui::extensions::ViewRenamedMessage renamed;
    renamed.session_id = "session-1";
    renamed.request_id = "request-renamed";
    renamed.view_id = "view-1";
    renamed.view_name = "Alpha_Renamed";
    renamed.file_name = "/tmp/internal_renamed.dmp";
    renamed.selector_tooltip = "<b>Alpha_Renamed</b><br><b>Source:</b> In-memory file";

    an::ui::extensions::ViewRenamedMessage parsed_renamed;
    ASSERT_TRUE(an::ui::extensions::viewRenamedFromProtocolMessage(an::ui::extensions::toProtocolMessage(renamed),
                                                                   &parsed_renamed, &error))
        << error;
    EXPECT_EQ(parsed_renamed.selector_tooltip, renamed.selector_tooltip);
}

// A valid description is accepted, answered with its root, widget count and identities, and kept as the latest.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesUiDescribeAndRetainsValidatedTree)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray advanced_children;
    advanced_children.append(
        makeWidgetNode(QStringLiteral("use_mask"), QStringLiteral("checkbox"), QStringLiteral("Use Mask")));

    QCborArray children;
    children.append(makeWidgetNode(QStringLiteral("strength"), QStringLiteral("slider"), QStringLiteral("Strength")));
    children.append(makeWidgetNode(QStringLiteral("mode"), QStringLiteral("combo"), QStringLiteral("Mode")));
    children.append(makeWidgetNode(QStringLiteral("advanced"), QStringLiteral("collapsible_section"),
                                   QStringLiteral("Advanced"), advanced_children));

    auto request = makeProtocolMessage("ui.describe", "session-ui-describe", "request-ui-describe",
                                       makeUiDescribeFields(children));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.describe.result");
    EXPECT_TRUE(response.fields.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("root_widget")).toString(), QStringLiteral("column"));
    EXPECT_EQ(response.fields.value(QStringLiteral("widget_count")).toInteger(), 5);

    const QCborArray widget_ids = response.fields.value(QStringLiteral("widget_ids")).toArray();
    ASSERT_EQ(widget_ids.size(), 4);
    EXPECT_EQ(widget_ids.at(0).toString(), QStringLiteral("strength"));
    EXPECT_EQ(widget_ids.at(1).toString(), QStringLiteral("mode"));
    EXPECT_EQ(widget_ids.at(2).toString(), QStringLiteral("advanced"));
    EXPECT_EQ(widget_ids.at(3).toString(), QStringLiteral("use_mask"));

    ASSERT_TRUE(service.latestUiDescription().has_value());
    EXPECT_EQ(service.latestUiDescription()->extension_id, "an.demo");
    EXPECT_EQ(service.latestUiDescription()->session_id, "session-ui-describe");
    EXPECT_EQ(service.latestUiDescription()->request_id, "request-ui-describe");
    EXPECT_EQ(service.latestUiDescription()->root_widget, QStringLiteral("column"));
    EXPECT_EQ(service.latestUiDescription()->widget_count, 5);
    ASSERT_EQ(service.latestUiDescription()->widget_ids.size(), 4);
    EXPECT_EQ(service.latestUiDescription()->widget_ids.at(0), QStringLiteral("strength"));
    EXPECT_TRUE(service.latestUiDescription()->supports_ui_events);
    EXPECT_TRUE(service.latestUiDescription()->supports_ui_validation);
    EXPECT_TRUE(service.latestUiDescription()->tree.contains(QStringLiteral("root")));
}

// A description that turns events and validation off is kept with them off.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRetainsUiDescribeInteractionFlags)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray children;
    children.append(makeWidgetNode(QStringLiteral("strength"), QStringLiteral("slider")));

    QCborMap fields = makeUiDescribeFields(children);
    fields.insert(QStringLiteral("supports_ui_events"), false);
    fields.insert(QStringLiteral("supports_ui_validation"), false);

    auto request =
        makeProtocolMessage("ui.describe", "session-ui-describe-flags", "request-ui-describe-flags", std::move(fields));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.describe.result");

    ASSERT_TRUE(service.latestUiDescription().has_value());
    EXPECT_FALSE(service.latestUiDescription()->supports_ui_events);
    EXPECT_FALSE(service.latestUiDescription()->supports_ui_validation);
}

// A description naming a widget the application does not know is refused by name and not kept.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsUiDescribeUnknownWidgetType)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray children;
    children.append(makeWidgetNode(QStringLiteral("curve"), QStringLiteral("curve_editor")));

    auto request = makeProtocolMessage("ui.describe", "session-ui-describe-bad-widget",
                                       "request-ui-describe-bad-widget", makeUiDescribeFields(children));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.describe.error");

    const auto protocol_error = decodeProtocolError(response);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(protocol_error.code),
              an::ui::extensions::ExtensionProtocolErrorDomain::Host);
    EXPECT_NE(protocol_error.details.find("unsupported widget type"), std::string::npos);
    EXPECT_FALSE(service.latestUiDescription().has_value());
}

// A description that names one widget identity twice is refused and not kept.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsUiDescribeDuplicateWidgetIds)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray children;
    children.append(makeWidgetNode(QStringLiteral("strength"), QStringLiteral("slider")));
    children.append(makeWidgetNode(QStringLiteral("strength"), QStringLiteral("spin_double")));

    auto request = makeProtocolMessage("ui.describe", "session-ui-describe-dup", "request-ui-describe-dup",
                                       makeUiDescribeFields(children));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.describe.error");

    const auto protocol_error = decodeProtocolError(response);
    EXPECT_NE(protocol_error.details.find("duplicate widget ids"), std::string::npos);
    EXPECT_FALSE(service.latestUiDescription().has_value());
}

// A patch over the described widgets is accepted and kept with each operation in order.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesUiPatchAndRetainsValidatedOperations)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray describe_children;
    describe_children.append(makeWidgetNode(QStringLiteral("strength"), QStringLiteral("slider")));
    describe_children.append(makeWidgetNode(QStringLiteral("advanced"), QStringLiteral("collapsible_section")));

    auto describe_request = makeProtocolMessage("ui.describe", "session-ui-patch", "request-ui-describe-for-patch",
                                                makeUiDescribeFields(describe_children));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        describe_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.describe.result");

    QCborArray patches;
    patches.append(makePatchOperation(QStringLiteral("strength"), QStringLiteral("value"), QCborValue(0.8)));
    patches.append(makePatchOperation(QStringLiteral("advanced"), QStringLiteral("visible"), QCborValue(false)));

    auto patch_request =
        makeProtocolMessage("ui.patch", "session-ui-patch", "request-ui-patch", makeUiPatchFields(patches));

    ASSERT_TRUE(service.dispatchProtocolMessage(
        patch_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.patch.result");
    EXPECT_TRUE(response.fields.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("patch_count")).toInteger(), 2);

    const QCborArray widget_ids = response.fields.value(QStringLiteral("widget_ids")).toArray();
    ASSERT_EQ(widget_ids.size(), 2);
    EXPECT_EQ(widget_ids.at(0).toString(), QStringLiteral("strength"));
    EXPECT_EQ(widget_ids.at(1).toString(), QStringLiteral("advanced"));

    ASSERT_TRUE(service.latestUiPatchState().has_value());
    EXPECT_EQ(service.latestUiPatchState()->extension_id, "an.demo");
    EXPECT_EQ(service.latestUiPatchState()->session_id, "session-ui-patch");
    EXPECT_EQ(service.latestUiPatchState()->request_id, "request-ui-patch");
    EXPECT_EQ(service.latestUiPatchState()->patch_count, 2);
    ASSERT_EQ(service.latestUiPatchState()->operations.size(), 2u);
    EXPECT_EQ(service.latestUiPatchState()->operations.at(0).widget_id, QStringLiteral("strength"));
    EXPECT_EQ(service.latestUiPatchState()->operations.at(0).property_name, QStringLiteral("value"));
    EXPECT_EQ(service.latestUiPatchState()->operations.at(0).value.toDouble(), 0.8);
    EXPECT_EQ(service.latestUiPatchState()->operations.at(1).widget_id, QStringLiteral("advanced"));
    EXPECT_EQ(service.latestUiPatchState()->operations.at(1).property_name, QStringLiteral("visible"));
    EXPECT_FALSE(service.latestUiPatchState()->operations.at(1).value.toBool());
}

// A patch with no description before it is refused and not kept.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsUiPatchWithoutMatchingUiDescribe)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray patches;
    patches.append(makePatchOperation(QStringLiteral("strength"), QStringLiteral("value"), QCborValue(0.5)));

    auto request = makeProtocolMessage("ui.patch", "session-ui-patch-missing", "request-ui-patch-missing",
                                       makeUiPatchFields(patches));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.patch.error");

    const auto protocol_error = decodeProtocolError(response);
    EXPECT_NE(protocol_error.message.find("prior ui.describe"), std::string::npos);
    EXPECT_FALSE(service.latestUiPatchState().has_value());
}

// A patch naming a widget the description does not hold is refused by name.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsUiPatchForUnknownWidgetId)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray describe_children;
    describe_children.append(makeWidgetNode(QStringLiteral("strength"), QStringLiteral("slider")));

    auto describe_request = makeProtocolMessage("ui.describe", "session-ui-patch-bad-id", "request-ui-describe-bad-id",
                                                makeUiDescribeFields(describe_children));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        describe_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;

    QCborArray patches;
    patches.append(makePatchOperation(QStringLiteral("missing"), QStringLiteral("visible"), QCborValue(true)));

    auto patch_request = makeProtocolMessage("ui.patch", "session-ui-patch-bad-id", "request-ui-patch-bad-id",
                                             makeUiPatchFields(patches));

    ASSERT_TRUE(service.dispatchProtocolMessage(
        patch_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.patch.error");

    const auto protocol_error = decodeProtocolError(response);
    EXPECT_NE(protocol_error.details.find("unknown widget id"), std::string::npos);
}

// A patch to a description that turned events off is refused, saying why.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsUiPatchWhenUiEventsAreDisabled)
{
    an::ui::extensions::ExtensionSdkService service;

    QCborArray describe_children;
    describe_children.append(makeWidgetNode(QStringLiteral("strength"), QStringLiteral("slider")));

    QCborMap describe_fields = makeUiDescribeFields(describe_children);
    describe_fields.insert(QStringLiteral("supports_ui_events"), false);

    auto describe_request = makeProtocolMessage("ui.describe", "session-ui-patch-disabled",
                                                "request-ui-describe-disabled", std::move(describe_fields));

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        describe_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.describe.result");

    QCborArray patches;
    patches.append(makePatchOperation(QStringLiteral("strength"), QStringLiteral("value"), QCborValue(0.8)));

    auto patch_request = makeProtocolMessage("ui.patch", "session-ui-patch-disabled", "request-ui-patch-disabled",
                                             makeUiPatchFields(patches));

    ASSERT_TRUE(service.dispatchProtocolMessage(
        patch_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.patch.error");

    const auto protocol_error = decodeProtocolError(response);
    EXPECT_NE(protocol_error.message.find("not supported"), std::string::npos);
    EXPECT_NE(protocol_error.details.find("supports_ui_events=false"), std::string::npos);
    EXPECT_FALSE(service.latestUiPatchState().has_value());
}

// An error with no title reaches the application's handler under the default title.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesUiShowErrorWithDefaultTitle)
{
    an::ui::extensions::ExtensionSdkService service;
    std::string received_session_id;
    std::string received_request_id;
    QString received_title;
    QString received_message;

    an::ui::extensions::ExtensionSdkUiInteractionHandler handler;
    handler.show_error = [&](const std::string_view session_id, const std::string_view request_id, const QString& title,
                             const QString& message, std::string*) -> bool {
        received_session_id = std::string(session_id);
        received_request_id = std::string(request_id);
        received_title = title;
        received_message = message;
        return true;
    };
    service.setUiInteractionHandler(std::move(handler));

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "ui.show_error";
    request.session_id = "session-ui-1";
    request.request_id = "request-ui-error";
    request.fields = QCborMap{{QStringLiteral("message"), QStringLiteral("The extension could not load its preset")}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.show_error.result");
    EXPECT_TRUE(response.fields.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("title")).toString(), QStringLiteral("Extension Error"));
    EXPECT_EQ(received_session_id, "session-ui-1");
    EXPECT_EQ(received_request_id, "request-ui-error");
    EXPECT_EQ(received_title, QStringLiteral("Extension Error"));
    EXPECT_EQ(received_message, QStringLiteral("The extension could not load its preset"));
}

// A confirmation is answered only once the application's handler completes it, with what the person chose.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesUiConfirmAndReturnsConfirmation)
{
    an::ui::extensions::ExtensionSdkService service;
    QString received_title;
    QString received_message;
    bool received_warn = false;
    an::ui::extensions::ExtensionSdkUiConfirmCompletion completion;

    an::ui::extensions::ExtensionSdkUiInteractionHandler handler;
    handler.confirm = [&](const std::string_view, const std::string_view, const QString& title, const QString& message,
                          const bool warn, an::ui::extensions::ExtensionSdkUiConfirmCompletion deferred_completion,
                          std::string*) -> bool {
        received_title = title;
        received_message = message;
        received_warn = warn;
        completion = std::move(deferred_completion);
        return true;
    };
    service.setUiInteractionHandler(std::move(handler));

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "ui.confirm";
    request.session_id = "session-ui-2";
    request.request_id = "request-ui-confirm";
    request.fields = QCborMap{{QStringLiteral("title"), QStringLiteral("Replace Preview")},
                              {QStringLiteral("message"), QStringLiteral("Overwrite the active preview image?")},
                              {QStringLiteral("warn"), true}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_TRUE(response.type.empty());
    EXPECT_EQ(received_title, QStringLiteral("Replace Preview"));
    EXPECT_EQ(received_message, QStringLiteral("Overwrite the active preview image?"));
    EXPECT_TRUE(received_warn);
    ASSERT_TRUE(static_cast<bool>(completion));

    auto deferred = service.takeDeferredProtocolResponses(request.session_id);
    EXPECT_TRUE(deferred.empty());

    completion(true, {});
    deferred = service.takeDeferredProtocolResponses(request.session_id);
    ASSERT_EQ(deferred.size(), 1u);
    EXPECT_EQ(deferred.front().type, "ui.confirm.result");
    EXPECT_TRUE(deferred.front().fields.value(QStringLiteral("confirmed")).toBool());
    EXPECT_TRUE(deferred.front().fields.value(QStringLiteral("warn")).toBool());
}

// An interface request with no handler configured is refused as unavailable.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsUiRequestsWithoutConfiguredHandler)
{
    an::ui::extensions::ExtensionSdkService service;

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "ui.show_info";
    request.session_id = "session-ui-3";
    request.request_id = "request-ui-missing-handler";
    request.fields = QCborMap{{QStringLiteral("message"), QStringLiteral("Background calibration completed")}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.show_info.error");

    const auto protocol_error = decodeProtocolError(response);
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(protocol_error.code),
              an::ui::extensions::ExtensionProtocolErrorDomain::Host);
    EXPECT_NE(protocol_error.message.find("unavailable"), std::string::npos);
}

// An open-image request reaches the application's handler with its buffer and name and is answered as accepted.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesUiOpenImage)
{
    an::ui::extensions::ExtensionSdkService service;
    std::string received_session_id;
    std::string received_request_id;
    QString received_title;
    an::ui::extensions::SharedBufferDescriptor received_buffer;

    service.setUiOpenImageHandler([&](const std::string_view session_id, const std::string_view request_id,
                                      const an::ui::extensions::SharedBufferDescriptor& buffer, const QString& title,
                                      std::string*) -> bool {
        received_session_id = std::string(session_id);
        received_request_id = std::string(request_id);
        received_title = title;
        received_buffer = buffer;
        return true;
    });

    an::ui::extensions::SharedBufferDescriptor descriptor;
    descriptor.buffer_id = "buffer-ui-open-image";
    descriptor.symbolic_name = "generated_image";
    descriptor.mapping_name = "session-ui-open-image.buffer-ui-open-image";
    descriptor.mapped_file_path = "/tmp/session-ui-open-image/buffer-ui-open-image.bin";
    descriptor.width = 64;
    descriptor.height = 32;
    descriptor.channels = 1;
    descriptor.pixel_type = an::ui::extensions::SharedBufferPixelType::Float32;
    descriptor.pixel_format = an::ui::extensions::SharedBufferPixelFormat::Mono;
    descriptor.color_space = an::ui::extensions::SharedBufferColorSpace::Grayscale;
    descriptor.row_stride = 64u * 4u;
    descriptor.plane_stride = descriptor.row_stride * 32u;
    descriptor.byte_size = descriptor.plane_stride;
    descriptor.access_mode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    descriptor.buffer_role = an::ui::extensions::SharedBufferRole::Destination;

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "ui.open_image";
    request.session_id = "session-ui-open-image";
    request.request_id = "request-ui-open-image";
    request.fields =
        QCborMap{{QStringLiteral("title"), QStringLiteral("Generated Background")},
                 {QStringLiteral("buffer"), an::ui::extensions::sharedBufferDescriptorToCborMap(descriptor)}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "ui.open_image.result");
    EXPECT_TRUE(response.fields.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("title")).toString(), QStringLiteral("Generated Background"));
    EXPECT_EQ(received_session_id, "session-ui-open-image");
    EXPECT_EQ(received_request_id, "request-ui-open-image");
    EXPECT_EQ(received_title, QStringLiteral("Generated Background"));
    EXPECT_EQ(received_buffer.buffer_id, descriptor.buffer_id);
    EXPECT_EQ(received_buffer.width, descriptor.width);
    EXPECT_EQ(received_buffer.height, descriptor.height);
}