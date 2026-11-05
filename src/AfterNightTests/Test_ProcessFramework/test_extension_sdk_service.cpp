// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SDK service an extension host talks to: which namespaces a launch mode offers, the extension's own settings -
/// scoped, typed, keyed and refused when unsafe or unbacked - the workflow context's directories, outputs and their
/// isolation between sessions, structured logging and diagnostics, the theme and view messages it builds from the
/// application's providers, and the view requests - describe, active image, file name, snapshot and bind - with a
/// snapshot that carries a real shared buffer and the image's cards, EXIF fields and solution. Settings live in an
/// in-memory database and every file a case writes under a scratch directory of its own.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"
#include "Test_ProcessFramework/ExtensionTestSupport.h"
#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionSdkService.h"
#include "app/SharedBufferManager.h"
#include "controls/GlobalSettingsPersistence.h"

#include <QCborArray>
#include <QCborMap>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using an::tests::processes::framework::InMemorySettings;
using an::tests::processes::framework::RemovePathOnExit;
using an::tests::processes::framework::ScratchDirectory;

an::ui::extensions::ExtensionSdkViewStateProvider
makeViewStateProvider(std::vector<an::ui::extensions::ExtensionViewState> view_states)
{
    return [view_states = std::move(view_states)]() { return view_states; };
}

an::ui::extensions::ExtensionSdkThemeSnapshotProvider
makeThemeSnapshotProvider(an::ui::extensions::ExtensionThemeSnapshot snapshot)
{
    return [snapshot = std::move(snapshot)]() { return snapshot; };
}

an::ui::extensions::SharedBufferDescriptor makeSnapshotDescriptor()
{
    an::ui::extensions::SharedBufferDescriptor descriptor;
    descriptor.buffer_id = "buffer-1";
    descriptor.symbolic_name = "alpha.snapshot";
    descriptor.mapping_name = "session-1.buffer-1";
    descriptor.mapped_file_path = "/tmp/buffer-1.bin";
    descriptor.width = 4;
    descriptor.height = 3;
    descriptor.channels = 3;
    descriptor.pixel_type = an::ui::extensions::SharedBufferPixelType::Float32;
    descriptor.pixel_format = an::ui::extensions::SharedBufferPixelFormat::RGB;
    descriptor.color_space = an::ui::extensions::SharedBufferColorSpace::Linear;
    descriptor.row_stride = 16;
    descriptor.plane_stride = 48;
    descriptor.byte_size = 144;
    descriptor.access_mode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
    descriptor.buffer_role = an::ui::extensions::SharedBufferRole::Preview;
    return descriptor;
}

an::ui::extensions::SharedBufferDescriptor makeSelectionMaskDescriptor()
{
    auto descriptor = makeSnapshotDescriptor();
    descriptor.buffer_id = "mask-buffer-1";
    descriptor.symbolic_name = "selection";
    descriptor.mapping_name = "session-1.mask-buffer-1";
    descriptor.mapped_file_path = "/tmp/mask-buffer-1.bin";
    descriptor.channels = 1;
    descriptor.pixel_format = an::ui::extensions::SharedBufferPixelFormat::Mono;
    descriptor.color_space = an::ui::extensions::SharedBufferColorSpace::Grayscale;
    descriptor.row_stride = 16;
    descriptor.plane_stride = 48;
    descriptor.byte_size = 48;
    descriptor.access_mode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
    descriptor.buffer_role = an::ui::extensions::SharedBufferRole::Mask;
    return descriptor;
}

bool writeSnapshotMetadataSidecar(const std::filesystem::path& path, const QJsonObject& metadata)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }

    const QByteArray payload = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    return file.write(payload) == payload.size();
}

// Exports an asset under its read lease to a new buffer under a root, as the application's snapshot provider does, and
// hands the buffer's descriptor to the snapshot; the mapping is let go and the file stays for the extension to open.
bool exportSnapshot(const std::filesystem::path& root, const an::consumer::ImageAsset& image,
                    const an::ui::extensions::ImageExportRequest& request,
                    an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    if (!lease.hasValue())
    {
        if (error)
        {
            *error = lease.status().message();
        }
        return false;
    }
    an::Result<an::ui::extensions::SharedImageBuffer> buffer =
        an::ui::extensions::SharedBufferManager(root).exportImage(lease->image(), request);
    if (!buffer.hasValue())
    {
        if (error)
        {
            *error = buffer.status().message();
        }
        return false;
    }
    out_snapshot->source_buffer = buffer->descriptor();
    return true;
}

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

} // anonymous namespace

// Each namespace is offered in the launch modes the application serves it in: settings always, workflow only to a
// workflow, and the deferred engine calls to none - each refusal saying why.
TEST(ExtensionSdkServiceTest, NamespaceAvailabilityMatchesUiOwnedServices)
{
    using an::ui::extensions::ExtensionLaunchMode;
    using an::ui::extensions::ExtensionSdkService;

    const auto settings =
        ExtensionSdkService::resolveNamespaceAvailability("settings.get", ExtensionLaunchMode::SingleImage);
    EXPECT_TRUE(settings.available);
    EXPECT_EQ(settings.namespace_name, "settings");

    const auto workflow_single_image =
        ExtensionSdkService::resolveNamespaceAvailability("workflow.session_info", ExtensionLaunchMode::SingleImage);
    EXPECT_FALSE(workflow_single_image.available);
    EXPECT_NE(workflow_single_image.diagnostic.find("workflow"), std::string::npos);

    const auto workflow =
        ExtensionSdkService::resolveNamespaceAvailability("workflow.session_info", ExtensionLaunchMode::Workflow);
    EXPECT_TRUE(workflow.available);

    const auto sdk =
        ExtensionSdkService::resolveNamespaceAvailability("sdk.core.apply_filter", ExtensionLaunchMode::Workflow);
    EXPECT_FALSE(sdk.available);
    EXPECT_NE(sdk.diagnostic.find("deferred"), std::string::npos);
}

// An extension's setting is stored under its own scope of the global settings, read back, and removed.
TEST(ExtensionSdkServiceTest, ExtensionSettingsRoundTripThroughNamespacedGlobalSettings)
{
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService service(&settings);

    std::string error;
    ASSERT_TRUE(service.upsertExtensionSetting("an.demo", "tools/executable_path", QStringLiteral("/opt/demo"), &error))
        << error;

    QString value;
    ASSERT_TRUE(service.selectExtensionSetting("an.demo", "tools/executable_path", &value, {}, &error)) << error;
    EXPECT_EQ(value, QStringLiteral("/opt/demo"));

    QString namespaced_value = settings.select(QStringLiteral("extension/an.demo/tools/executable_path"));
    EXPECT_EQ(namespaced_value, QStringLiteral("/opt/demo"));

    ASSERT_TRUE(service.removeExtensionSetting("an.demo", "tools/executable_path", &error)) << error;
    EXPECT_TRUE(settings.select(QStringLiteral("extension/an.demo/tools/executable_path")).isEmpty());
}

// An extension identity or a setting key that could leave its scope is refused by name.
TEST(ExtensionSdkServiceTest, RejectsUnsafeExtensionSettingIdentifiers)
{
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService service(&settings);

    std::string error;
    EXPECT_FALSE(service.upsertExtensionSetting("an/demo", "good_key", QStringLiteral("value"), &error));
    EXPECT_NE(error.find("invalid extension id"), std::string::npos);

    error.clear();
    EXPECT_FALSE(service.upsertExtensionSetting("an.demo", "../bad", QStringLiteral("value"), &error));
    EXPECT_NE(error.find("invalid setting key"), std::string::npos);
}

// With no settings store the service refuses a write and says it has none.
TEST(ExtensionSdkServiceTest, RejectsOperationsWithoutBackingPersistence)
{
    an::ui::extensions::ExtensionSdkService service;

    std::string error;
    EXPECT_FALSE(service.upsertExtensionSetting("an.demo", "good_key", QStringLiteral("value"), &error));
    EXPECT_NE(error.find("GlobalSettingsPersistence"), std::string::npos);
}

// A setting an extension sets over the protocol is the one it gets back.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesSettingsRoundTrip)
{
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService service(&settings);

    an::ui::extensions::ExtensionProtocolMessage set_request;
    set_request.type = "settings.set";
    set_request.session_id = "session-1";
    set_request.request_id = "request-set";
    set_request.fields = QCborMap{{QStringLiteral("key"), QStringLiteral("tools/executable_path")},
                                  {QStringLiteral("value"), QStringLiteral("/opt/demo")}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        set_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "settings.set.result");
    EXPECT_EQ(response.request_id, set_request.request_id);

    an::ui::extensions::ExtensionProtocolMessage get_request;
    get_request.type = "settings.get";
    get_request.session_id = "session-1";
    get_request.request_id = "request-get";
    get_request.fields = QCborMap{{QStringLiteral("key"), QStringLiteral("tools/executable_path")},
                                  {QStringLiteral("default_value"), QStringLiteral("missing")}};

    ASSERT_TRUE(service.dispatchProtocolMessage(
        get_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "settings.get.result");
    EXPECT_EQ(response.request_id, get_request.request_id);
    EXPECT_EQ(response.fields.value(QStringLiteral("key")).toString(), QStringLiteral("tools/executable_path"));
    EXPECT_EQ(response.fields.value(QStringLiteral("value")).toString(), QStringLiteral("/opt/demo"));
}

// A structured setting keeps its types - booleans, integers, reals, arrays and maps - through the store.
TEST(ExtensionSdkServiceTest, TypedExtensionSettingsRoundTripWithoutStringifyingValues)
{
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService service(&settings);

    const QCborMap complex_value =
        QCborMap{{QStringLiteral("enabled"), true},
                 {QStringLiteral("count"), 3},
                 {QStringLiteral("threshold"), 1.25},
                 {QStringLiteral("labels"), QCborArray{QStringLiteral("alpha"), QStringLiteral("beta")}},
                 {QStringLiteral("nested"), QCborMap{{QStringLiteral("mode"), QStringLiteral("fast")}}}};

    an::ui::extensions::ExtensionProtocolMessage set_request;
    set_request.type = "settings.set";
    set_request.session_id = "session-typed";
    set_request.request_id = "request-set-typed";
    set_request.fields =
        QCborMap{{QStringLiteral("key"), QStringLiteral("complex")}, {QStringLiteral("value"), complex_value}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        set_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "settings.set.result");

    QCborValue stored_value;
    ASSERT_TRUE(service.selectExtensionSetting("an.demo", "complex", &stored_value, QCborValue(), &error)) << error;
    ASSERT_TRUE(stored_value.isMap());
    EXPECT_EQ(stored_value.toMap().value(QStringLiteral("count")).toInteger(), 3);

    an::ui::extensions::ExtensionProtocolMessage get_request;
    get_request.type = "settings.get";
    get_request.session_id = "session-typed";
    get_request.request_id = "request-get-typed";
    get_request.fields = QCborMap{{QStringLiteral("key"), QStringLiteral("complex")},
                                  {QStringLiteral("default_value"), QCborMap{{QStringLiteral("enabled"), false}}}};

    ASSERT_TRUE(service.dispatchProtocolMessage(
        get_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "settings.get.result");

    const QCborValue response_value = response.fields.value(QStringLiteral("value"));
    ASSERT_TRUE(response_value.isMap());
    const QCborMap result = response_value.toMap();
    EXPECT_TRUE(result.value(QStringLiteral("enabled")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("count")).toInteger(), 3);
    EXPECT_DOUBLE_EQ(result.value(QStringLiteral("threshold")).toDouble(), 1.25);

    const QCborArray labels = result.value(QStringLiteral("labels")).toArray();
    ASSERT_EQ(labels.size(), 2);
    EXPECT_EQ(labels.at(0).toString(), QStringLiteral("alpha"));
    EXPECT_EQ(labels.at(1).toString(), QStringLiteral("beta"));

    const QCborMap nested = result.value(QStringLiteral("nested")).toMap();
    EXPECT_EQ(nested.value(QStringLiteral("mode")).toString(), QStringLiteral("fast"));
}

// A request in a namespace the launch mode does not offer is answered with a protocol error, not a failed dispatch.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageReturnsProtocolErrorForUnavailableNamespace)
{
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService service(&settings);

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "workflow.session_info";
    request.session_id = "session-1";
    request.request_id = "request-workflow";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "workflow.session_info.error");
    EXPECT_EQ(response.request_id, request.request_id);

    an::ui::extensions::ExtensionProtocolError protocol_error;
    ASSERT_TRUE(an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")),
                                                               &protocol_error, &error))
        << error;
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(protocol_error.code),
              an::ui::extensions::ExtensionProtocolErrorDomain::Host);
    EXPECT_NE(protocol_error.details.find("workflow"), std::string::npos);
}

// A workflow session's temporary, working and output directories exist when they are named, and a result sequence
// it opens is recorded as one of its outputs.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesWorkflowContextRequests)
{
    an::ui::extensions::ExtensionSdkService service;
    const std::string session_id = "workflow-session-sdk-service";
    const auto session_root = an::ui::extensions::extensionHostSessionRootPath(session_id);
    (void)an::tests::processes::framework::removeTree(session_root);
    const RemovePathOnExit remove_session_root{session_root};

    auto dispatch = [&](const std::string& type, const QCborMap& fields = QCborMap{}) {
        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = type;
        request.session_id = session_id;
        request.request_id = type + ".request";
        request.fields = fields;

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        EXPECT_TRUE(service.dispatchProtocolMessage(
            request, "an.demo.workflow", an::ui::extensions::ExtensionLaunchMode::Workflow, &response, &error))
            << error;
        return response;
    };

    const auto temp_response = dispatch("workflow.temp_dir");
    ASSERT_EQ(temp_response.type, "workflow.temp_dir.result");
    const std::filesystem::path temp_dir = temp_response.fields.value(QStringLiteral("path")).toString().toStdString();
    EXPECT_TRUE(std::filesystem::is_directory(temp_dir));

    const auto set_response = dispatch("workflow.set_working_dir",
                                       QCborMap{{QStringLiteral("path"), QString::fromStdString(temp_dir.string())}});
    ASSERT_EQ(set_response.type, "workflow.set_working_dir.result");

    const auto working_response = dispatch("workflow.get_working_dir");
    EXPECT_EQ(working_response.fields.value(QStringLiteral("path")).toString().toStdString(),
              set_response.fields.value(QStringLiteral("path")).toString().toStdString());

    const auto output_response =
        dispatch("workflow.output_path", QCborMap{{QStringLiteral("name"), QStringLiteral("reports/demo.txt")}});
    ASSERT_EQ(output_response.type, "workflow.output_path.result");
    const std::filesystem::path output_path =
        output_response.fields.value(QStringLiteral("path")).toString().toStdString();
    EXPECT_EQ(output_path.filename().string(), "demo.txt");
    EXPECT_TRUE(std::filesystem::is_directory(output_path.parent_path()));

    const auto sequences_response = dispatch("workflow.get_sequences");
    ASSERT_EQ(sequences_response.type, "workflow.get_sequences.result");
    EXPECT_TRUE(sequences_response.fields.value(QStringLiteral("sequences")).isArray());
    EXPECT_EQ(sequences_response.fields.value(QStringLiteral("sequence_count")).toInteger(), 0);

    const auto open_response =
        dispatch("workflow.open_result_sequence",
                 QCborMap{{QStringLiteral("path"), QString::fromStdString(output_path.string())}});
    ASSERT_EQ(open_response.type, "workflow.open_result_sequence.result");
    EXPECT_TRUE(open_response.fields.value(QStringLiteral("accepted")).toBool());
    ASSERT_EQ(service.workflowOutputs().size(), 1u);
    EXPECT_EQ(service.workflowOutputs().front().extension_id, "an.demo.workflow");
    EXPECT_EQ(service.workflowOutputs().front().kind, "sequence");
    EXPECT_EQ(service.workflowOutputs().front().path, output_path);
}

// Two workflow sessions keep their directories and outputs apart; presenting one session's outputs hands over
// only its own and removes its temporary directory, leaving the other's.
TEST(ExtensionSdkServiceTest, WorkflowContextStateIsIsolatedAndCleanedBetweenSessions)
{
    an::ui::extensions::ExtensionSdkService service;
    const std::string session_a = "workflow-session-isolation-a";
    const std::string session_b = "workflow-session-isolation-b";
    const auto root_a = an::ui::extensions::extensionHostSessionRootPath(session_a);
    const auto root_b = an::ui::extensions::extensionHostSessionRootPath(session_b);
    (void)an::tests::processes::framework::removeTree(root_a);
    (void)an::tests::processes::framework::removeTree(root_b);
    const RemovePathOnExit remove_root_a{root_a};
    const RemovePathOnExit remove_root_b{root_b};

    std::vector<std::vector<an::ui::extensions::ExtensionWorkflowOutput>> presented_outputs;
    service.setWorkflowOutputHandler(
        [&](std::string_view, const std::vector<an::ui::extensions::ExtensionWorkflowOutput>& outputs, std::string*) {
            presented_outputs.push_back(outputs);
            return true;
        });

    auto dispatch = [&](const std::string& session_id, const std::string& type, const QCborMap& fields = QCborMap{}) {
        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = type;
        request.session_id = session_id;
        request.request_id = session_id + "." + type + ".request";
        request.fields = fields;

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        EXPECT_TRUE(service.dispatchProtocolMessage(
            request, "an.demo.workflow", an::ui::extensions::ExtensionLaunchMode::Workflow, &response, &error))
            << error;
        return response;
    };

    const auto temp_response_a = dispatch(session_a, "workflow.temp_dir");
    ASSERT_EQ(temp_response_a.type, "workflow.temp_dir.result");
    const std::filesystem::path temp_dir_a =
        temp_response_a.fields.value(QStringLiteral("path")).toString().toStdString();
    ASSERT_TRUE(std::filesystem::is_directory(temp_dir_a));
    const std::filesystem::path marker_a = temp_dir_a / "session-a-marker.txt";
    {
        QFile marker(QString::fromStdString(marker_a.string()));
        ASSERT_TRUE(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_GT(marker.write("session-a"), 0);
    }

    const auto set_working_a =
        dispatch(session_a, "workflow.set_working_dir",
                 QCborMap{{QStringLiteral("path"), QString::fromStdString(temp_dir_a.string())}});
    ASSERT_EQ(set_working_a.type, "workflow.set_working_dir.result");

    const auto temp_response_b = dispatch(session_b, "workflow.temp_dir");
    ASSERT_EQ(temp_response_b.type, "workflow.temp_dir.result");
    const std::filesystem::path temp_dir_b =
        temp_response_b.fields.value(QStringLiteral("path")).toString().toStdString();
    ASSERT_TRUE(std::filesystem::is_directory(temp_dir_b));
    EXPECT_NE(temp_dir_a, temp_dir_b);

    const auto output_response_a =
        dispatch(session_a, "workflow.output_path",
                 QCborMap{{QStringLiteral("name"), QStringLiteral("reports/session-a.anseq")}});
    ASSERT_EQ(output_response_a.type, "workflow.output_path.result");
    const std::filesystem::path output_path_a =
        output_response_a.fields.value(QStringLiteral("path")).toString().toStdString();

    const auto output_response_b =
        dispatch(session_b, "workflow.output_path",
                 QCborMap{{QStringLiteral("name"), QStringLiteral("reports/session-b.anseq")}});
    ASSERT_EQ(output_response_b.type, "workflow.output_path.result");
    const std::filesystem::path output_path_b =
        output_response_b.fields.value(QStringLiteral("path")).toString().toStdString();
    EXPECT_NE(output_path_a, output_path_b);

    const auto open_response_a =
        dispatch(session_a, "workflow.open_result_sequence",
                 QCborMap{{QStringLiteral("path"), QString::fromStdString(output_path_a.string())}});
    ASSERT_EQ(open_response_a.type, "workflow.open_result_sequence.result");
    const auto open_response_b =
        dispatch(session_b, "workflow.open_result_sequence",
                 QCborMap{{QStringLiteral("path"), QString::fromStdString(output_path_b.string())}});
    ASSERT_EQ(open_response_b.type, "workflow.open_result_sequence.result");
    ASSERT_EQ(service.workflowOutputs().size(), 2u);

    std::string error;
    ASSERT_TRUE(service.presentWorkflowOutputs(session_a, &error)) << error;
    ASSERT_EQ(presented_outputs.size(), 1u);
    ASSERT_EQ(presented_outputs.front().size(), 1u);
    EXPECT_EQ(presented_outputs.front().front().session_id, session_a);
    ASSERT_EQ(service.workflowOutputs().size(), 1u);
    EXPECT_EQ(service.workflowOutputs().front().session_id, session_b);
    EXPECT_EQ(service.workflowOutputs().front().path, output_path_b);
    EXPECT_FALSE(std::filesystem::exists(marker_a)) << marker_a;

    const auto working_response_a = dispatch(session_a, "workflow.get_working_dir");
    ASSERT_EQ(working_response_a.type, "workflow.get_working_dir.result");
    EXPECT_NE(working_response_a.fields.value(QStringLiteral("path")).toString().toStdString(), temp_dir_a.string());

    ASSERT_TRUE(service.presentWorkflowOutputs(session_b, &error)) << error;
    ASSERT_EQ(presented_outputs.size(), 2u);
    ASSERT_EQ(presented_outputs.back().size(), 1u);
    EXPECT_EQ(presented_outputs.back().front().session_id, session_b);
    EXPECT_TRUE(service.workflowOutputs().empty());
}

// A logging event is accepted and kept with its extension, session, severity, component, message and payload.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesLoggingEventAndRetainsStructuredEvent)
{
    an::ui::extensions::ExtensionSdkService service;

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "logging.event";
    request.session_id = "session-1";
    request.request_id = "request-log";
    request.fields =
        QCborMap{{QStringLiteral("severity"), QStringLiteral("warning")},
                 {QStringLiteral("component"), QStringLiteral("extension.test")},
                 {QStringLiteral("message"), QStringLiteral("Parameter validation was skipped")},
                 {QStringLiteral("payload"), QCborMap{{QStringLiteral("stage"), QStringLiteral("validation")}}}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "logging.event.result");
    EXPECT_EQ(response.request_id, request.request_id);
    EXPECT_TRUE(response.fields.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
    EXPECT_EQ(response.fields.value(QStringLiteral("component")).toString(), QStringLiteral("extension.test"));

    const auto& events = service.recentLogEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().extension_id, "an.demo");
    EXPECT_EQ(events.front().session_id, "session-1");
    EXPECT_EQ(events.front().request_id, "request-log");
    EXPECT_EQ(events.front().severity, "warning");
    EXPECT_EQ(events.front().component, "extension.test");
    EXPECT_EQ(events.front().message, "Parameter validation was skipped");
    EXPECT_TRUE(events.front().payload.isMap());
}

// A logging event of a severity the protocol does not name is refused and not kept.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsInvalidLoggingSeverity)
{
    an::ui::extensions::ExtensionSdkService service;

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "logging.event";
    request.session_id = "session-1";
    request.request_id = "request-log-invalid";
    request.fields = QCborMap{{QStringLiteral("severity"), QStringLiteral("loud")},
                              {QStringLiteral("component"), QStringLiteral("extension.test")},
                              {QStringLiteral("message"), QStringLiteral("bad severity")}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "logging.event.error");

    an::ui::extensions::ExtensionProtocolError protocol_error;
    ASSERT_TRUE(an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")),
                                                               &protocol_error, &error))
        << error;
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(protocol_error.code),
              an::ui::extensions::ExtensionProtocolErrorDomain::Logging);
    EXPECT_NE(protocol_error.message.find("severity"), std::string::npos);
    EXPECT_TRUE(service.recentLogEvents().empty());
}

// A diagnostics checkpoint is accepted and kept with its name, detail and fields.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesDiagnosticsCheckpointAndRetainsFields)
{
    an::ui::extensions::ExtensionSdkService service;

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "diagnostics.checkpoint";
    request.session_id = "session-1";
    request.request_id = "request-diag";
    request.fields = QCborMap{{QStringLiteral("name"), QStringLiteral("before_execute")},
                              {QStringLiteral("detail"), QStringLiteral("Captured state before host apply")},
                              {QStringLiteral("fields"), QCborMap{{QStringLiteral("tile_count"), 8}}}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "diagnostics.checkpoint.result");
    EXPECT_EQ(response.request_id, request.request_id);
    EXPECT_TRUE(response.fields.value(QStringLiteral("accepted")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("name")).toString(), QStringLiteral("before_execute"));

    const auto& checkpoints = service.diagnosticCheckpoints();
    ASSERT_EQ(checkpoints.size(), 1u);
    EXPECT_EQ(checkpoints.front().extension_id, "an.demo");
    EXPECT_EQ(checkpoints.front().session_id, "session-1");
    EXPECT_EQ(checkpoints.front().request_id, "request-diag");
    EXPECT_EQ(checkpoints.front().name, "before_execute");
    EXPECT_EQ(checkpoints.front().detail, "Captured state before host apply");
    EXPECT_TRUE(checkpoints.front().fields.isMap());
}

// The first theme message a session receives is the snapshot the application's provider gives.
TEST(ExtensionSdkServiceTest, BuildInitialThemeDescribeMessageUsesConfiguredThemeSnapshot)
{
    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionThemeSnapshot snapshot;
    snapshot.theme_name = "AfterNight Dark";
    snapshot.tokens =
        QCborMap{{QStringLiteral("colors"), QCborMap{{QStringLiteral("textPrimary"), QStringLiteral("#fffef8")}}},
                 {QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}};
    service.setThemeSnapshotProvider(makeThemeSnapshotProvider(snapshot));

    an::ui::extensions::ThemeDescribeMessage message;
    bool has_message = false;
    std::string error;
    ASSERT_TRUE(service.buildInitialThemeDescribeMessage("session-1", &message, &has_message, &error)) << error;
    EXPECT_TRUE(has_message);
    EXPECT_EQ(message.session_id, "session-1");
    EXPECT_FALSE(message.request_id.empty());
    EXPECT_EQ(message.theme_name, "AfterNight Dark");
    EXPECT_EQ(message.tokens.value(QStringLiteral("spacing")).toMap().value(QStringLiteral("md")).toInteger(), 12);
}

// A theme change message carries the snapshot the application's provider gives now.
TEST(ExtensionSdkServiceTest, BuildThemeChangedMessageUsesConfiguredThemeSnapshot)
{
    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionThemeSnapshot snapshot;
    snapshot.theme_name = "AfterNight Light";
    snapshot.tokens =
        QCborMap{{QStringLiteral("colors"), QCborMap{{QStringLiteral("textPrimary"), QStringLiteral("#1f1a17")}}},
                 {QStringLiteral("is_dark"), false}};
    service.setThemeSnapshotProvider(makeThemeSnapshotProvider(snapshot));

    an::ui::extensions::ThemeChangedMessage message;
    bool has_message = false;
    std::string error;
    ASSERT_TRUE(service.buildThemeChangedMessage("session-1", &message, &has_message, &error)) << error;
    EXPECT_TRUE(has_message);
    EXPECT_EQ(message.session_id, "session-1");
    EXPECT_FALSE(message.request_id.empty());
    EXPECT_EQ(message.theme_name, "AfterNight Light");
    EXPECT_FALSE(message.tokens.value(QStringLiteral("is_dark")).toBool());
}

// The first view message describes the active view - its snapshot buffers, the cards its sidecar holds and its
// display settings - or the view the session was bound to when that is another.
TEST(ExtensionSdkServiceTest, BuildInitialViewDescribeMessageExportsActiveAndExplicitlyBoundViewSnapshots)
{
    an::ui::extensions::ExtensionSdkService service;
    const ScratchDirectory scratch("sdk-snapshot");
    const auto metadata_sidecar = scratch.path() / "snapshot.metadata.json";
    ASSERT_TRUE(writeSnapshotMetadataSidecar(
        metadata_sidecar, QJsonObject{{QStringLiteral("FILENAME"), QStringLiteral("/tmp/alpha-sidecar.fit")},
                                      {QStringLiteral("OBJECT"), QStringLiteral("M31")},
                                      {QStringLiteral("FILTER"), QStringLiteral("L")}}));

    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.file_name = "/tmp/alpha.fit";
    alpha.view_mode = "standard";
    alpha.image_display_mode = "autostretch";
    alpha.channel_view_mode = "rgb";
    alpha.mask_rendering_mode = "multiply";
    alpha.zoom_percent = 125.0;
    alpha.viewport_offset_x = 11;
    alpha.viewport_offset_y = 17;
    alpha.is_active = true;
    alpha.has_image = true;
    alpha.has_mask = true;
    alpha.mask_enabled = true;
    alpha.show_mask = true;
    an::ui::extensions::ExtensionViewState beta;
    beta.view_name = "beta";
    beta.file_name = "/tmp/beta.fit";
    beta.is_active = false;
    beta.has_image = true;
    service.setViewStateProvider(makeViewStateProvider({alpha, beta}));
    service.setViewSnapshotProvider(
        [metadata_sidecar](const std::string_view session_id, const std::string_view request_id,
                           const std::string_view view_name, an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                           std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }

            EXPECT_EQ(session_id, "session-1");
            EXPECT_EQ(view_name, "alpha");
            EXPECT_FALSE(request_id.empty());
            out_snapshot->source_buffer = makeSnapshotDescriptor();
            out_snapshot->source_buffer.metadata_sidecar = metadata_sidecar.lexically_normal().generic_string();
            out_snapshot->auxiliary_buffers = {makeSelectionMaskDescriptor()};
            return true;
        });

    an::ui::extensions::ViewDescribeMessage message;
    bool has_message = false;
    std::string error;
    ASSERT_TRUE(service.buildInitialViewDescribeMessage("session-1", &message, &has_message, &error)) << error;
    EXPECT_TRUE(has_message);
    EXPECT_EQ(message.session_id, "session-1");
    EXPECT_FALSE(message.request_id.empty());
    EXPECT_EQ(message.view_id, "alpha");
    EXPECT_EQ(message.view_name, "alpha");
    EXPECT_EQ(message.file_name, "/tmp/alpha.fit");
    EXPECT_EQ(message.source_buffer.buffer_id, "buffer-1");
    ASSERT_EQ(message.auxiliary_buffers.size(), 1u);
    EXPECT_EQ(message.auxiliary_buffers.front().symbolic_name, "selection");
    EXPECT_EQ(message.auxiliary_buffers.front().buffer_role, an::ui::extensions::SharedBufferRole::Mask);
    EXPECT_EQ(message.metadata.value(QStringLiteral("FILENAME")).toString(), QStringLiteral("/tmp/alpha-sidecar.fit"));
    EXPECT_EQ(message.metadata.value(QStringLiteral("OBJECT")).toString(), QStringLiteral("M31"));
    EXPECT_EQ(message.display_settings.value(QStringLiteral("view_mode")).toString(), QStringLiteral("standard"));
    EXPECT_EQ(message.display_settings.value(QStringLiteral("image_display_mode")).toString(),
              QStringLiteral("autostretch"));
    EXPECT_EQ(message.display_settings.value(QStringLiteral("zoom_percent")).toDouble(), 125.0);
    EXPECT_EQ(message.display_settings.value(QStringLiteral("viewport_offset"))
                  .toMap()
                  .value(QStringLiteral("x"))
                  .toInteger(),
              11);
    EXPECT_TRUE(message.display_settings.value(QStringLiteral("mask_enabled")).toBool());

    alpha.is_active = false;
    beta.is_active = true;
    service.setViewStateProvider(makeViewStateProvider({alpha, beta}));

    an::ui::extensions::ViewDescribeMessage bound_message;
    has_message = false;
    ASSERT_TRUE(service.buildInitialViewDescribeMessage("session-1", "alpha", &bound_message, &has_message, &error))
        << error;
    EXPECT_TRUE(has_message);
    EXPECT_EQ(bound_message.view_id, "alpha");
    EXPECT_EQ(bound_message.view_name, "alpha");
    EXPECT_EQ(bound_message.file_name, "/tmp/alpha.fit");
}

// A view update describes the named view under the session's stable view identity, with its snapshot, cards and
// display settings.
TEST(ExtensionSdkServiceTest, BuildViewUpdatedMessageExportsNamedViewSnapshot)
{
    an::ui::extensions::ExtensionSdkService service;
    const ScratchDirectory scratch("sdk-snapshot");
    const auto metadata_sidecar = scratch.path() / "snapshot.metadata.json";
    ASSERT_TRUE(writeSnapshotMetadataSidecar(
        metadata_sidecar, QJsonObject{{QStringLiteral("FILENAME"), QStringLiteral("/tmp/alpha-updated.fit")},
                                      {QStringLiteral("OBJECT"), QStringLiteral("M42")},
                                      {QStringLiteral("FILTER"), QStringLiteral("Ha")}}));

    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.file_name = "/tmp/alpha.fit";
    alpha.view_mode = "standard";
    alpha.image_display_mode = "autostretch";
    alpha.channel_view_mode = "rgb";
    alpha.mask_rendering_mode = "multiply";
    alpha.zoom_percent = 175.0;
    alpha.viewport_offset_x = 7;
    alpha.viewport_offset_y = 9;
    alpha.has_image = true;
    alpha.has_mask = true;
    alpha.mask_enabled = true;
    service.setViewStateProvider(makeViewStateProvider({alpha}));
    service.setViewSnapshotProvider(
        [metadata_sidecar](const std::string_view session_id, const std::string_view request_id,
                           const std::string_view view_name, an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                           std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }

            EXPECT_EQ(session_id, "session-2");
            EXPECT_EQ(view_name, "alpha");
            EXPECT_FALSE(request_id.empty());
            out_snapshot->source_buffer = makeSnapshotDescriptor();
            out_snapshot->source_buffer.metadata_sidecar = metadata_sidecar.lexically_normal().generic_string();
            out_snapshot->auxiliary_buffers = {makeSelectionMaskDescriptor()};
            return true;
        });

    an::ui::extensions::ViewUpdatedMessage message;
    bool has_message = false;
    std::string error;
    ASSERT_TRUE(
        service.buildViewUpdatedMessage("session-2", "view-alpha-stable", "alpha", &message, &has_message, &error))
        << error;
    EXPECT_TRUE(has_message);
    EXPECT_EQ(message.session_id, "session-2");
    EXPECT_FALSE(message.request_id.empty());
    EXPECT_EQ(message.view_id, "view-alpha-stable");
    EXPECT_EQ(message.source_buffer.buffer_id, "buffer-1");
    ASSERT_EQ(message.auxiliary_buffers.size(), 1u);
    EXPECT_EQ(message.auxiliary_buffers.front().symbolic_name, "selection");
    EXPECT_EQ(message.auxiliary_buffers.front().buffer_role, an::ui::extensions::SharedBufferRole::Mask);
    EXPECT_EQ(message.metadata.value(QStringLiteral("FILENAME")).toString(), QStringLiteral("/tmp/alpha-updated.fit"));
    EXPECT_EQ(message.metadata.value(QStringLiteral("OBJECT")).toString(), QStringLiteral("M42"));
    EXPECT_EQ(message.display_settings.value(QStringLiteral("view_mode")).toString(), QStringLiteral("standard"));
    EXPECT_EQ(message.display_settings.value(QStringLiteral("image_display_mode")).toString(),
              QStringLiteral("autostretch"));
    EXPECT_EQ(message.display_settings.value(QStringLiteral("zoom_percent")).toDouble(), 175.0);
    EXPECT_EQ(message.display_settings.value(QStringLiteral("viewport_offset"))
                  .toMap()
                  .value(QStringLiteral("y"))
                  .toInteger(),
              9);
    EXPECT_TRUE(message.display_settings.value(QStringLiteral("mask_enabled")).toBool());
}

// Describing the views lists every view by name, each with its display state, and names the active one.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesViewsDescribeWithSortedViewState)
{
    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionViewState zeta;
    zeta.view_name = "zeta";
    zeta.file_name = "/tmp/zeta.fit";
    zeta.selector_tooltip = "<b>Zeta</b><br><b>Source:</b> In-memory file";
    zeta.view_mode = "standard";
    zeta.channel_view_mode = "rgb";
    zeta.width = 1024;
    zeta.height = 512;
    zeta.channels = 3;
    zeta.zoom_percent = 125.0;
    zeta.viewport_offset_x = 12;
    zeta.viewport_offset_y = 34;
    zeta.has_image = true;
    zeta.has_alternate_image = true;

    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.view_mode = "rt_preview";
    alpha.channel_view_mode = "rgb_r";
    alpha.mask_rendering_mode = "multiply";
    alpha.zoom_percent = 200.0;
    alpha.viewport_offset_x = 1;
    alpha.viewport_offset_y = 2;
    alpha.is_active = true;
    alpha.has_mask = true;
    alpha.mask_enabled = true;
    alpha.show_mask = true;
    alpha.mask_inverted = false;

    service.setViewStateProvider(makeViewStateProvider({zeta, alpha}));

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "views.describe";
    request.session_id = "session-1";
    request.request_id = "request-views-describe";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.describe.result");
    EXPECT_EQ(response.fields.value(QStringLiteral("view_count")).toInteger(), 2);
    EXPECT_FALSE(response.fields.value(QStringLiteral("has_active_image")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("active_view_name")).toString(), QStringLiteral("alpha"));

    const QCborArray view_array = response.fields.value(QStringLiteral("views")).toArray();
    ASSERT_EQ(view_array.size(), 2);
    const QCborMap first_view = view_array.at(0).toMap();
    const QCborMap second_view = view_array.at(1).toMap();
    EXPECT_EQ(first_view.value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));
    EXPECT_EQ(first_view.value(QStringLiteral("view_mode")).toString(), QStringLiteral("rt_preview"));
    EXPECT_EQ(first_view.value(QStringLiteral("channel_view_mode")).toString(), QStringLiteral("rgb_r"));
    EXPECT_EQ(first_view.value(QStringLiteral("mask_rendering_mode")).toString(), QStringLiteral("multiply"));
    EXPECT_EQ(first_view.value(QStringLiteral("zoom_percent")).toDouble(), 200.0);
    EXPECT_EQ(first_view.value(QStringLiteral("viewport_offset")).toMap().value(QStringLiteral("x")).toInteger(), 1);
    EXPECT_FALSE(first_view.value(QStringLiteral("has_image")).toBool());
    EXPECT_TRUE(first_view.value(QStringLiteral("is_active")).toBool());
    EXPECT_TRUE(first_view.value(QStringLiteral("has_mask")).toBool());
    EXPECT_TRUE(first_view.value(QStringLiteral("mask_enabled")).toBool());
    EXPECT_EQ(second_view.value(QStringLiteral("name")).toString(), QStringLiteral("zeta"));
    EXPECT_EQ(second_view.value(QStringLiteral("file_name")).toString(), QStringLiteral("/tmp/zeta.fit"));
    EXPECT_EQ(second_view.value(QStringLiteral("selector_tooltip")).toString(),
              QStringLiteral("<b>Zeta</b><br><b>Source:</b> In-memory file"));
    EXPECT_EQ(second_view.value(QStringLiteral("width")).toInteger(), 1024);
    EXPECT_EQ(second_view.value(QStringLiteral("height")).toInteger(), 512);
    EXPECT_EQ(second_view.value(QStringLiteral("channels")).toInteger(), 3);
    EXPECT_TRUE(second_view.value(QStringLiteral("has_alternate_image")).toBool());
}

// An extension can ask whether the active view holds an image and what file a named view shows.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesViewsHasActiveImageAndFilenameLookup)
{
    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.file_name = "/tmp/alpha.fit";
    alpha.is_active = true;
    alpha.has_image = true;

    an::ui::extensions::ExtensionViewState beta;
    beta.view_name = "beta";
    beta.file_name = "/tmp/beta.fit";
    beta.has_image = true;

    service.setViewStateProvider(makeViewStateProvider({alpha, beta}));

    an::ui::extensions::ExtensionProtocolMessage has_image_request;
    has_image_request.type = "views.has_active_image";
    has_image_request.session_id = "session-1";
    has_image_request.request_id = "request-views-active";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        has_image_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.has_active_image.result");
    EXPECT_TRUE(response.fields.value(QStringLiteral("has_active_image")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("active_view_name")).toString(), QStringLiteral("alpha"));

    an::ui::extensions::ExtensionProtocolMessage filename_request;
    filename_request.type = "views.get_filename";
    filename_request.session_id = "session-1";
    filename_request.request_id = "request-views-filename";
    filename_request.fields = QCborMap{{QStringLiteral("view_name"), QStringLiteral("beta")}};

    ASSERT_TRUE(service.dispatchProtocolMessage(
        filename_request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.get_filename.result");
    EXPECT_TRUE(response.fields.value(QStringLiteral("resolved")).toBool());
    EXPECT_EQ(response.fields.value(QStringLiteral("view_name")).toString(), QStringLiteral("beta"));
    EXPECT_EQ(response.fields.value(QStringLiteral("file_name")).toString(), QStringLiteral("/tmp/beta.fit"));
    EXPECT_TRUE(response.fields.value(QStringLiteral("has_image")).toBool());
}

// Asking for the file of a view that does not exist is refused, saying which view was asked for.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsViewsGetFilenameWhenNamedViewIsMissing)
{
    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.file_name = "/tmp/alpha.fit";
    alpha.is_active = true;
    alpha.has_image = true;
    service.setViewStateProvider(makeViewStateProvider({alpha}));

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "views.get_filename";
    request.session_id = "session-1";
    request.request_id = "request-views-missing";
    request.fields = QCborMap{{QStringLiteral("view_name"), QStringLiteral("missing")}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.get_filename.error");

    an::ui::extensions::ExtensionProtocolError protocol_error;
    ASSERT_TRUE(an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")),
                                                               &protocol_error, &error))
        << error;
    EXPECT_EQ(an::ui::extensions::classifyProtocolErrorCode(protocol_error.code),
              an::ui::extensions::ExtensionProtocolErrorDomain::Host);
    EXPECT_NE(protocol_error.message.find("Requested view"), std::string::npos);
}

// A snapshot answers the provider's buffer and mask descriptors and the cards the buffer's sidecar holds.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesViewsSnapshotAndReturnsDescriptor)
{
    an::ui::extensions::ExtensionSdkService service;
    const ScratchDirectory scratch("sdk-snapshot");
    const auto metadata_sidecar = scratch.path() / "snapshot.metadata.json";
    ASSERT_TRUE(writeSnapshotMetadataSidecar(
        metadata_sidecar, QJsonObject{{QStringLiteral("FILENAME"), QStringLiteral("/tmp/alpha-snapshot.fit")},
                                      {QStringLiteral("OBJECT"), QStringLiteral("NGC7000")}}));
    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.file_name = "/tmp/alpha.fit";
    alpha.is_active = true;
    alpha.has_image = true;
    service.setViewStateProvider(makeViewStateProvider({alpha}));
    service.setViewSnapshotProvider(
        [metadata_sidecar](const std::string_view session_id, const std::string_view request_id,
                           const std::string_view view_name, an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                           std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }

            EXPECT_EQ(session_id, "session-1");
            EXPECT_EQ(request_id, "request-snapshot");
            EXPECT_EQ(view_name, "alpha");
            out_snapshot->source_buffer = makeSnapshotDescriptor();
            out_snapshot->source_buffer.metadata_sidecar = metadata_sidecar.lexically_normal().generic_string();
            out_snapshot->auxiliary_buffers = {makeSelectionMaskDescriptor()};
            return true;
        });

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "views.snapshot";
    request.session_id = "session-1";
    request.request_id = "request-snapshot";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.snapshot.result");
    EXPECT_EQ(response.fields.value(QStringLiteral("view_name")).toString(), QStringLiteral("alpha"));
    EXPECT_EQ(response.fields.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("FILENAME")).toString(),
              QStringLiteral("/tmp/alpha-snapshot.fit"));
    EXPECT_EQ(response.fields.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("OBJECT")).toString(),
              QStringLiteral("NGC7000"));

    an::ui::extensions::SharedBufferDescriptor descriptor;
    ASSERT_TRUE(an::ui::extensions::sharedBufferDescriptorFromCborValue(response.fields.value(QStringLiteral("buffer")),
                                                                        &descriptor, &error))
        << error;
    EXPECT_EQ(descriptor.buffer_id, "buffer-1");
    EXPECT_EQ(descriptor.buffer_role, an::ui::extensions::SharedBufferRole::Preview);
    EXPECT_EQ(descriptor.width, 4);
    EXPECT_EQ(descriptor.height, 3);

    const QCborArray auxiliary_buffers = response.fields.value(QStringLiteral("auxiliary_buffers")).toArray();
    ASSERT_EQ(auxiliary_buffers.size(), 1);
    an::ui::extensions::SharedBufferDescriptor mask_descriptor;
    ASSERT_TRUE(
        an::ui::extensions::sharedBufferDescriptorFromCborValue(auxiliary_buffers.at(0), &mask_descriptor, &error))
        << error;
    EXPECT_EQ(mask_descriptor.symbolic_name, "selection");
    EXPECT_EQ(mask_descriptor.buffer_role, an::ui::extensions::SharedBufferRole::Mask);
    EXPECT_EQ(mask_descriptor.access_mode, an::ui::extensions::SharedBufferAccessMode::ReadOnly);
}

// Binding a session to a named view tells the application, and answers that view's identity, display settings and
// snapshot.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesViewsBindAndRebindsSession)
{
    an::ui::extensions::ExtensionSdkService service;

    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.file_name = "/tmp/alpha.fit";
    alpha.has_image = true;
    alpha.is_active = true;

    an::ui::extensions::ExtensionViewState beta;
    beta.view_name = "beta";
    beta.file_name = "/tmp/beta.fit";
    beta.view_mode = "preview";
    beta.image_display_mode = "rgb";
    beta.channel_view_mode = "rgbk";
    beta.zoom_percent = 125.0;
    beta.viewport_offset_x = 12;
    beta.viewport_offset_y = 34;
    beta.has_image = true;

    service.setViewStateProvider(makeViewStateProvider({alpha, beta}));

    bool bind_called = false;
    std::string bound_session_id;
    std::string bound_view_name;
    service.setViewBindingHandler(
        [&](const std::string_view session_id, const std::string_view view_name, std::string*) -> bool {
            bind_called = true;
            bound_session_id = std::string(session_id);
            bound_view_name = std::string(view_name);
            return true;
        });

    service.setViewSnapshotProvider(
        [](const std::string_view session_id, const std::string_view request_id, const std::string_view view_name,
           an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }

            EXPECT_EQ(session_id, "session-1");
            EXPECT_EQ(request_id, "request-bind");
            EXPECT_EQ(view_name, "beta");
            out_snapshot->source_buffer = makeSnapshotDescriptor();
            return true;
        });

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "views.bind";
    request.session_id = "session-1";
    request.request_id = "request-bind";
    request.fields = QCborMap{{QStringLiteral("view_name"), QStringLiteral("beta")}};

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.bind.result");
    EXPECT_TRUE(bind_called);
    EXPECT_EQ(bound_session_id, "session-1");
    EXPECT_EQ(bound_view_name, "beta");
    EXPECT_EQ(response.fields.value(QStringLiteral("view_id")).toString(), QStringLiteral("beta"));
    EXPECT_EQ(response.fields.value(QStringLiteral("view_name")).toString(), QStringLiteral("beta"));
    EXPECT_EQ(response.fields.value(QStringLiteral("file_name")).toString(), QStringLiteral("/tmp/beta.fit"));

    const QCborMap display_settings = response.fields.value(QStringLiteral("display_settings")).toMap();
    EXPECT_EQ(display_settings.value(QStringLiteral("zoom_percent")).toDouble(), 125.0);
    EXPECT_EQ(display_settings.value(QStringLiteral("viewport_offset")).toMap().value(QStringLiteral("x")).toInteger(),
              12);
    EXPECT_EQ(display_settings.value(QStringLiteral("viewport_offset")).toMap().value(QStringLiteral("y")).toInteger(),
              34);

    an::ui::extensions::SharedBufferDescriptor descriptor;
    ASSERT_TRUE(an::ui::extensions::sharedBufferDescriptorFromCborValue(response.fields.value(QStringLiteral("buffer")),
                                                                        &descriptor, &error))
        << error;
    EXPECT_EQ(descriptor.buffer_id, "buffer-1");
}

// A view snapshot an extension asks for carries a real shared buffer: the image the provider exported, which imports
// back as the same samples.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageViewsSnapshotRoundTripsRealSharedBuffer)
{
    const ScratchDirectory scratch("sdk-real-roundtrip");
    const std::filesystem::path root = scratch.path() / "SharedBuffers";
    const an::consumer::EngineSession& session = an::tests::processes::framework::suiteContext().engineSession();

    an::tests::seam::SeamImageSpec spec;
    spec.width = 4u;
    spec.height = 3u;
    std::optional<an::consumer::ImageAsset> made = an::tests::seam::makeSeamAsset<float>(
        session, spec, [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.1f + 0.4f * channel; });
    ASSERT_TRUE(made.has_value());
    const auto image = std::make_shared<const an::consumer::ImageAsset>(std::move(*made));

    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.file_name = "/tmp/alpha.fit";
    alpha.is_active = true;
    alpha.has_image = true;
    alpha.width = 4;
    alpha.height = 3;
    alpha.channels = 3;
    service.setViewStateProvider(makeViewStateProvider({alpha}));
    service.setViewSnapshotProvider([root, image](const std::string_view session_id, const std::string_view request_id,
                                                  const std::string_view view_name,
                                                  an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                                                  std::string* error) -> bool {
        if (!out_snapshot)
        {
            if (error)
            {
                *error = "snapshot output is null";
            }
            return false;
        }

        an::ui::extensions::ImageExportRequest request;
        request.sessionId = std::string(session_id);
        request.role = an::ui::extensions::SharedBufferRole::Preview;
        request.bufferId = std::string(request_id) + ".snapshot";
        request.symbolicName = std::string(view_name) + ".snapshot";
        return exportSnapshot(root, *image, request, out_snapshot, error);
    });

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "views.snapshot";
    request.session_id = "session-real";
    request.request_id = "request-real";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;

    an::ui::extensions::SharedBufferDescriptor descriptor;
    ASSERT_TRUE(an::ui::extensions::sharedBufferDescriptorFromCborValue(response.fields.value(QStringLiteral("buffer")),
                                                                        &descriptor, &error))
        << error;

    an::Result<an::ui::extensions::ImportedImage> round_tripped =
        an::ui::extensions::SharedBufferManager(root).importImage(session, descriptor);
    ASSERT_TRUE(round_tripped.hasValue()) << round_tripped.status().message();
    const std::optional<an::ImageDescriptor> described = round_tripped->asset.descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), 4u);
    EXPECT_EQ(described->extent().height(), 3u);
    EXPECT_EQ(described->extent().channelCount(), 3u);
    EXPECT_NEAR(an::tests::processes::assetSample(round_tripped->asset, 0u, 0u, 0u), 0.1, 1e-6);
    EXPECT_NEAR(an::tests::processes::assetSample(round_tripped->asset, 0u, 0u, 1u), 0.5, 1e-6);
    EXPECT_NEAR(an::tests::processes::assetSample(round_tripped->asset, 0u, 0u, 2u), 0.9, 1e-6);
}

// A view snapshot carries the image's file name, its FITS cards, its EXIF fields and its solution in the metadata the
// extension reads, the solver stated as a name - without the FITS string's quotes.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageViewsSnapshotExposesFitsExifAndAstrometryMetadata)
{
    const ScratchDirectory scratch("sdk-real-metadata");
    const std::filesystem::path root = scratch.path() / "SharedBuffers";

    an::tests::seam::SeamImageSpec spec;
    spec.width = 4u;
    spec.height = 3u;
    spec.fitsCards = {{"NAXIS", "2"},
                      {"NAXIS1", "4"},
                      {"NAXIS2", "3"},
                      {"CTYPE1", "'RA---TAN'"},
                      {"CTYPE2", "'DEC--TAN'"},
                      {"CUNIT1", "'deg'"},
                      {"CUNIT2", "'deg'"},
                      {"CRPIX1", "2.0"},
                      {"CRPIX2", "2.0"},
                      {"CRVAL1", "180.0"},
                      {"CRVAL2", "45.0"},
                      {"CD1_1", "-0.0002777777778"},
                      {"CD1_2", "0.0"},
                      {"CD2_1", "0.0"},
                      {"CD2_2", "0.0002777777778"},
                      {"RADESYS", "'ICRS'"},
                      {"PLTSLVR", "'AfterNight internal Gaia'"}};
    spec.exifCards = {{"Exif.Image.Model", "EOS Ra"}};
    std::optional<an::consumer::ImageAsset> made = an::tests::seam::makeSeamAsset<float>(
        an::tests::processes::framework::suiteContext().engineSession(), spec,
        [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.1f + 0.4f * channel; });
    ASSERT_TRUE(made.has_value());
    const auto image = std::make_shared<const an::consumer::ImageAsset>(std::move(*made));

    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionViewState solved;
    solved.view_name = "solved";
    solved.file_name = "/tmp/solved.fit";
    solved.is_active = true;
    solved.has_image = true;
    solved.width = 4;
    solved.height = 3;
    solved.channels = 3;
    service.setViewStateProvider(makeViewStateProvider({solved}));
    service.setViewSnapshotProvider([root, image](const std::string_view session_id, const std::string_view request_id,
                                                  const std::string_view view_name,
                                                  an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                                                  std::string* error) -> bool {
        if (!out_snapshot)
        {
            if (error)
            {
                *error = "snapshot output is null";
            }
            return false;
        }

        an::ui::extensions::ImageExportRequest request;
        request.sessionId = std::string(session_id);
        request.role = an::ui::extensions::SharedBufferRole::Preview;
        request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
        request.bufferId = std::string(request_id) + ".snapshot";
        request.symbolicName = std::string(view_name) + ".snapshot";
        request.fileName = "/tmp/solved.fit";
        return exportSnapshot(root, *image, request, out_snapshot, error);
    });

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "views.snapshot";
    request.session_id = "session-metadata";
    request.request_id = "request-metadata";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.snapshot.result");

    const QCborMap metadata = response.fields.value(QStringLiteral("metadata")).toMap();
    EXPECT_EQ(metadata.value(QStringLiteral("CRVAL1")).toString(), QStringLiteral("180.0"));
    EXPECT_EQ(metadata.value(QStringLiteral("FILENAME")).toString(), QStringLiteral("/tmp/solved.fit"));

    const QCborMap fits_header = metadata.value(QStringLiteral("fits_header")).toMap();
    EXPECT_EQ(fits_header.value(QStringLiteral("CTYPE1")).toString(), QStringLiteral("'RA---TAN'"));
    EXPECT_EQ(fits_header.value(QStringLiteral("PLTSLVR")).toString(), QStringLiteral("'AfterNight internal Gaia'"));

    const QCborMap exif_header = metadata.value(QStringLiteral("exif_header")).toMap();
    EXPECT_EQ(exif_header.value(QStringLiteral("Exif.Image.Model")).toString(), QStringLiteral("EOS Ra"));

    const QCborMap astrometry = metadata.value(QStringLiteral("astrometry")).toMap();
    EXPECT_TRUE(astrometry.value(QStringLiteral("has_solution")).toBool());
    EXPECT_EQ(astrometry.value(QStringLiteral("solver")).toString(), QStringLiteral("AfterNight internal Gaia"));
    EXPECT_NEAR(astrometry.value(QStringLiteral("field_center_ra_deg")).toDouble(), 180.0, 0.1);
    EXPECT_NEAR(astrometry.value(QStringLiteral("image_scale_arcsec_per_pixel")).toDouble(), 1.0, 1e-3);
}

// A snapshot of a view that holds no image it can export is refused, saying so.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageRejectsViewsSnapshotWithoutExportableImage)
{
    an::ui::extensions::ExtensionSdkService service;
    an::ui::extensions::ExtensionViewState alpha;
    alpha.view_name = "alpha";
    alpha.is_active = true;
    alpha.has_sequence = true;
    service.setViewStateProvider(makeViewStateProvider({alpha}));

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "views.snapshot";
    request.session_id = "session-1";
    request.request_id = "request-snapshot-missing-image";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "views.snapshot.error");

    an::ui::extensions::ExtensionProtocolError protocol_error;
    ASSERT_TRUE(an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")),
                                                               &protocol_error, &error))
        << error;
    EXPECT_NE(protocol_error.message.find("exportable image"), std::string::npos);
}

// An extension's setting keys are its own, relative to its scope and in order; another extension's and the
// application's are not among them.
TEST(ExtensionSdkServiceTest, SelectExtensionSettingKeysReturnsOnlyScopedRelativeKeys)
{
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService service(&settings);

    std::string error;
    ASSERT_TRUE(service.upsertExtensionSetting("an.demo", "tools/executable_path", QStringLiteral("/opt/demo"), &error))
        << error;
    ASSERT_TRUE(service.upsertExtensionSetting("an.demo", "gpu/enabled", QStringLiteral("true"), &error)) << error;
    ASSERT_TRUE(
        service.upsertExtensionSetting("an.other", "tools/executable_path", QStringLiteral("/opt/other"), &error))
        << error;
    ASSERT_TRUE(settings.upsert(QStringLiteral("interface/theme"), QStringLiteral("dark")));

    QStringList keys;
    ASSERT_TRUE(service.selectExtensionSettingKeys("an.demo", &keys, &error)) << error;
    ASSERT_EQ(keys.size(), 2);
    EXPECT_EQ(keys[0], QStringLiteral("gpu/enabled"));
    EXPECT_EQ(keys[1], QStringLiteral("tools/executable_path"));
}

// Listing setting keys over the protocol answers the requesting extension's keys alone, in order.
TEST(ExtensionSdkServiceTest, DispatchProtocolMessageHandlesSettingsKeysWithinActiveExtensionScope)
{
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService service(&settings);

    std::string error;
    ASSERT_TRUE(service.upsertExtensionSetting("an.demo", "tools/executable_path", QStringLiteral("/opt/demo"), &error))
        << error;
    ASSERT_TRUE(service.upsertExtensionSetting("an.demo", "gpu/enabled", QStringLiteral("true"), &error)) << error;
    ASSERT_TRUE(
        service.upsertExtensionSetting("an.other", "tools/executable_path", QStringLiteral("/opt/other"), &error))
        << error;

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "settings.keys";
    request.session_id = "session-1";
    request.request_id = "request-keys";

    an::ui::extensions::ExtensionProtocolMessage response;
    ASSERT_TRUE(service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(response.type, "settings.keys.result");
    EXPECT_EQ(response.request_id, request.request_id);

    const QCborArray key_array = response.fields.value(QStringLiteral("keys")).toArray();
    ASSERT_EQ(key_array.size(), 2);
    EXPECT_EQ(key_array.at(0).toString(), QStringLiteral("gpu/enabled"));
    EXPECT_EQ(key_array.at(1).toString(), QStringLiteral("tools/executable_path"));
}
