// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The extension regression: every bundled extension and every author template, discovered and provisioned as the
/// application discovers and provisions them, run on the host its backend names - previewed where it previews,
/// executed and committed where it executes, and cancelled the way the application cancels it - with each answer held
/// to what the subject's own code computes. Beside them, the versions both hosts and every package speak: protocol 1,
/// SDK 1, package format 1, and a host or a package of another version refused.
///
/// A subject is cancelled as the product cancels it. The application cancels a preview it supersedes, and a cancelled
/// preview answers with nothing and lets the next one land; an execute or a workflow is cancelled while it is in
/// flight, and nothing it would have committed or opened reaches the application. Each in-flight request is held in
/// flight by the host's own delay, so the cancellation lands at the same point on every run.

#include "Test_ProcessFramework/ExtensionHostTestSupport.h"
#include "Test_ProcessFramework/ExtensionTestSupport.h"

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionSdkService.h"
#include "app/SharedBufferManager.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/SidecarCases.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/IO/FitsHeaderCards.h>

#include <QCborMap>
#include <QCborValue>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace an::tests::processes::framework;
using an::ui::extensions::ExtensionHostManager;
using an::ui::extensions::ExtensionHostRunningSession;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::SharedBufferAccessMode;
using an::ui::extensions::SharedBufferDescriptor;
using an::ui::extensions::SharedBufferRole;

/// Where a subject ships: the application's bundled extensions, or the author templates of the documentation.
enum class Origin
{
    Bundled,
    Template
};

/// The root a subject's package lies under, in the checkout.
[[nodiscard]] std::filesystem::path originRoot(const Origin origin)
{
    return origin == Origin::Bundled ? findRepositoryBundledExtensionsRoot() : authorTemplatesRoot();
}

/// The packages of an origin, discovered and provisioned once for the run, as the application does at start.
[[nodiscard]] const std::vector<ExtensionLoadResult>& discoveredPackages(const Origin origin)
{
    static std::map<Origin, std::vector<ExtensionLoadResult>> discovered;
    auto found = discovered.find(origin);
    if (found == discovered.end())
    {
        std::string error;
        std::vector<ExtensionLoadResult> packages =
            discoverBundledPackages(originRoot(origin), extensionHostPathForCurrentAppDir(),
                                    runtimeRootForCurrentAppDir(), &error, shortExtensionEnvironmentsRoot());
        EXPECT_TRUE(error.empty()) << error;
        found = discovered.emplace(origin, std::move(packages)).first;
    }
    return found->second;
}

/// The discovered, provisioned package a subject's directory holds; fails the case when it is not ready.
[[nodiscard]] const ExtensionLoadResult* readySubject(const Origin origin, const std::string_view directory)
{
    const std::vector<ExtensionLoadResult>& packages = discoveredPackages(origin);
    const auto found = std::find_if(packages.begin(), packages.end(), [directory](const ExtensionLoadResult& result) {
        return result.manifest.has_value() &&
               std::filesystem::path(result.manifest->package_root).filename() == std::filesystem::path(directory);
    });
    if (found == packages.end())
    {
        ADD_FAILURE() << directory << " was not discovered";
        return nullptr;
    }
    EXPECT_EQ(found->status, an::ui::extensions::ExtensionLoadStatus::Ready) << directory << ": " << found->diagnostic;
    EXPECT_TRUE(found->environment.has_value() &&
                found->environment->state == an::ui::extensions::ExtensionEnvironmentState::Ready)
        << directory;
    if (found->status != an::ui::extensions::ExtensionLoadStatus::Ready || !found->environment.has_value() ||
        found->manifest->processes.empty())
    {
        return nullptr;
    }
    return &*found;
}

/// The launch request the application makes for a subject: its backend's host, its process class and its provisioned
/// environment, with the host's extra arguments after them.
[[nodiscard]] an::ui::extensions::ExtensionHostLaunchRequest
launchRequestFor(const ExtensionLoadResult& subject, const std::vector<std::string>& extra_arguments = {})
{
    const an::ui::extensions::ExtensionManifest& manifest = *subject.manifest;
    const bool protocol = manifest.sdk_backend == an::ui::extensions::ExtensionSdkBackend::Protocol;
    const bool workflow = manifest.launch_mode == an::ui::extensions::ExtensionLaunchMode::Workflow;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = manifest.id;
    request.display_name = manifest.name;
    request.sdk_backend = protocol ? "protocol" : "runtime";
    request.launch_mode = manifest.launch_mode;
    request.host_executable = protocol ? extensionHostLitePathForCurrentAppDir() : extensionHostPathForCurrentAppDir();
    request.capabilities =
        workflow ? std::vector<std::string>{"execute", "workflow"} : std::vector<std::string>{"execute", "preview"};
    request.host_arguments = {"--extension-package-root",
                              manifest.package_root,
                              "--entry-point",
                              manifest.entry_point,
                              "--process-class",
                              manifest.processes.front().class_name,
                              "--environment-root",
                              subject.environment->environment_path.string(),
                              "--runtime-root",
                              runtimeRootForCurrentAppDir().string(),
                              "--stay-alive-ms",
                              std::to_string(8000 * kIpcTimingMultiplier)};
    request.host_arguments.insert(request.host_arguments.end(), extra_arguments.begin(), extra_arguments.end());
    return request;
}

/// A source the application lends a run, with the shared-buffer root it lives under.
struct LentImage
{
    std::filesystem::path root;
    an::ui::extensions::SharedImageBuffer buffer;
};

/// Imports the buffer a run committed or previewed under a root, failing the case when it cannot.
[[nodiscard]] std::optional<an::consumer::ImageAsset> importResult(const std::filesystem::path& root,
                                                                   const SharedBufferDescriptor& descriptor)
{
    an::ui::extensions::SharedBufferManager manager(root);
    std::optional<an::consumer::ImageAsset> image;
    std::string error;
    EXPECT_TRUE(importBufferOrReport(manager, descriptor, &image, &error)) << error;
    return image;
}

/// The committed buffer of a role, or null.
[[nodiscard]] const SharedBufferDescriptor* resultOfRole(const ExtensionHostRunningSession& session,
                                                         const SharedBufferRole role, const std::string_view name = {})
{
    if (!session.last_view_commit.has_value())
    {
        return nullptr;
    }
    for (const SharedBufferDescriptor& descriptor : session.last_view_commit->result_buffers)
    {
        if (descriptor.buffer_role == role && (name.empty() || descriptor.symbolic_name == name))
        {
            return &descriptor;
        }
    }
    return nullptr;
}

/// The sidecar beside a result buffer, as a JSON object; empty when it has none.
[[nodiscard]] QJsonObject sidecarOf(const SharedBufferDescriptor& descriptor)
{
    QFile file(QString::fromStdString(descriptor.metadata_sidecar));
    if (descriptor.metadata_sidecar.empty() || !file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

/// Expects every sample of a result to be `expected(source sample, x, y, channel)`.
template <typename Expected>
void expectSamples(const an::consumer::ImageAsset& result, const an::consumer::ImageAsset& source,
                   const Expected& expected, const double tolerance = 1e-5)
{
    ASSERT_EQ(widthOf(result), widthOf(source));
    ASSERT_EQ(heightOf(result), heightOf(source));
    ASSERT_EQ(channelsOf(result), channelsOf(source));
    for (std::uint32_t channel = 0u; channel < channelsOf(source); ++channel)
    {
        for (std::uint32_t y = 0u; y < heightOf(source); ++y)
        {
            for (std::uint32_t x = 0u; x < widthOf(source); ++x)
            {
                const double value = sampleAt(source, x, y, channel);
                EXPECT_NEAR(sampleAt(result, x, y, channel), expected(value, x, y, channel), tolerance)
                    << "at " << x << "," << y << " channel " << channel;
            }
        }
    }
}

/// One subject's session: launched as the application launches it, stopped however the case ends.
class SubjectSession
{
public:
    SubjectSession(const ExtensionLoadResult& subject, const std::vector<std::string>& extra_arguments = {},
                   an::ui::extensions::ExtensionSdkService* sdk_service = nullptr)
        : m_root(uniqueSharedBufferRootForTest("regression-" + subject.manifest->id)), m_removeRoot{m_root}
    {
        removeAllNoThrow(m_root);
        if (sdk_service != nullptr)
        {
            m_manager.setSdkService(sdk_service);
        }
        const auto prepared = m_manager.prepareSession(launchRequestFor(subject, extra_arguments));
        m_launched = m_manager.launchSession(prepared, &m_session, &m_error);
        m_guard.emplace(m_manager, m_session);
    }

    SubjectSession(const SubjectSession&) = delete;
    SubjectSession& operator=(const SubjectSession&) = delete;

    [[nodiscard]] bool launched() const noexcept
    {
        return m_launched;
    }

    [[nodiscard]] ExtensionHostManager& manager() noexcept
    {
        return m_manager;
    }

    [[nodiscard]] ExtensionHostRunningSession& session() noexcept
    {
        return m_session;
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept
    {
        return m_root;
    }

    [[nodiscard]] std::string& error() noexcept
    {
        return m_error;
    }

    /// Lends an image to the session as the application lends one, under the session's root.
    [[nodiscard]] SharedBufferDescriptor lend(const std::shared_ptr<an::consumer::ImageAsset>& image,
                                              const SharedBufferRole role, const SharedBufferAccessMode access,
                                              const std::string_view name)
    {
        m_lent.push_back(
            makeImageBuffer(m_root, m_session.prepared_session.session_id, image, role, access, name, &m_error));
        EXPECT_TRUE(m_lent.back().valid()) << m_error;
        return m_lent.back().descriptor();
    }

    /// Sends an execute and pumps until it lands; false, with the reason in `error()`, when it does not.
    [[nodiscard]] bool execute(const QCborMap& params, const std::vector<SharedBufferDescriptor>& buffers)
    {
        std::string request_id;
        if (!m_manager.sendExecuteRequest(&m_session, "regression-execute", params, buffers, &request_id, &m_error))
        {
            return false;
        }
        return pumpUntilRequestCompletes(m_manager, &m_session, 15000 * kIpcTimingMultiplier, &m_error);
    }

    /// Sends a preview and pumps until it lands.
    [[nodiscard]] bool preview(const QCborMap& params, const std::vector<SharedBufferDescriptor>& buffers)
    {
        std::string request_id;
        if (!m_manager.sendPreviewRequest(&m_session, "regression-preview", params, buffers, &request_id, &m_error))
        {
            return false;
        }
        return pumpUntilRequestCompletes(m_manager, &m_session, 15000 * kIpcTimingMultiplier, &m_error);
    }

private:
    ExtensionHostManager m_manager;
    ExtensionHostRunningSession m_session;
    std::filesystem::path m_root;
    RemovePathOnExit m_removeRoot;
    std::vector<an::ui::extensions::SharedImageBuffer> m_lent;
    std::optional<SessionGuard> m_guard;
    std::string m_error;
    bool m_launched = false;
};

/// Skips a case that needs the full host where it is not deployed, or fails it where the runner requires it.
#define AN_REGRESSION_REQUIRES_FULL_HOST() AN_REQUIRE_FULL_EXTENSION_HOST(extensionHostPathForCurrentAppDir())

/// A grey Float32 image whose samples run through the unit interval in row-major order.
[[nodiscard]] std::shared_ptr<an::consumer::ImageAsset> unitRamp(const std::uint32_t width, const std::uint32_t height)
{
    return makeMonoFloatImage("ramp", width, height, 0.05f, 0.9f / static_cast<float>(width * height));
}

// ---- Versions.

/// Every subject's manifest, as JSON.
[[nodiscard]] std::vector<std::pair<std::string, QJsonObject>> subjectManifests()
{
    std::vector<std::pair<std::string, QJsonObject>> manifests;
    for (const Origin origin : {Origin::Bundled, Origin::Template})
    {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(originRoot(origin)))
        {
            const std::filesystem::path path = entry.path() / "extension.json";
            QFile file(QString::fromStdString(path.string()));
            if (entry.is_directory() && file.open(QIODevice::ReadOnly))
            {
                manifests.emplace_back(entry.path().filename().string(),
                                       QJsonDocument::fromJson(file.readAll()).object());
            }
        }
    }
    return manifests;
}

} // namespace

TEST(ExtensionRegression, EveryBundledExtensionAndTemplateStatesTheVersionsTheProductSpeaks)
{
    EXPECT_EQ(an::ui::extensions::kExtensionProtocolVersion, 1);
    EXPECT_EQ(an::ui::extensions::kExtensionSdkVersion, 1);
    const auto manifests = subjectManifests();
    ASSERT_EQ(manifests.size(), 12u) << "five bundled extensions and seven author templates";
    for (const auto& [directory, manifest] : manifests)
    {
        EXPECT_EQ(manifest.value(QStringLiteral("package_format_version")).toInt(), 1) << directory;
        EXPECT_EQ(manifest.value(QStringLiteral("protocol_version")).toInt(),
                  an::ui::extensions::kExtensionProtocolVersion)
            << directory;
        EXPECT_EQ(manifest.value(QStringLiteral("sdk_version")).toInt(), an::ui::extensions::kExtensionSdkVersion)
            << directory;
    }
    for (const Origin origin : {Origin::Bundled, Origin::Template})
    {
        for (const ExtensionLoadResult& result : discoveredPackages(origin))
        {
            EXPECT_EQ(result.status, an::ui::extensions::ExtensionLoadStatus::Ready)
                << result.package_root << ": " << result.diagnostic;
        }
    }
}

// A package that states another package format, protocol or SDK version is refused by name, as a repository release
// of one is.
TEST(ExtensionRegression, APackageOfAnotherFormatProtocolOrSdkVersionIsRefused)
{
    const ScratchDirectory scratch("regression-versions");
    const std::filesystem::path source = authorTemplatesRoot() / "basic_process";
    for (const char* field : {"package_format_version", "protocol_version", "sdk_version"})
    {
        const std::filesystem::path root = scratch.path() / field;
        const std::filesystem::path package = root / "basic_process";
        std::filesystem::create_directories(package);
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(source))
        {
            if (entry.is_regular_file())
            {
                std::filesystem::copy_file(entry.path(), package / entry.path().filename());
            }
        }
        QFile file(QString::fromStdString((package / "extension.json").string()));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        manifest.insert(QString::fromLatin1(field), 2);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(manifest).toJson());
        file.close();

        an::ui::extensions::ExtensionDiscoveryOptions options;
        options.bundled_roots = {root};
        options.context.runtime_target = an::ui::extensions::current_runtime_target();
        options.include_development_only_extensions = true;
        an::ui::extensions::ExtensionManager manager;
        const std::vector<ExtensionLoadResult> discovered = manager.discoverExtensions(options);
        ASSERT_EQ(discovered.size(), 1u) << field;
        EXPECT_NE(discovered.front().status, an::ui::extensions::ExtensionLoadStatus::Ready) << field;
        EXPECT_NE(discovered.front().diagnostic.find("version 1"), std::string::npos)
            << field << ": " << discovered.front().diagnostic;
    }
}

// Both hosts greet the application at protocol 1 and SDK 1, and the application acknowledges at the same versions.
TEST(ExtensionRegression, EachHostHandshakesAtTheVersionsTheApplicationSpeaks)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    for (const auto& [origin, directory] :
         {std::pair{Origin::Template, "basic_process"}, std::pair{Origin::Template, "lite_protocol_process"}})
    {
        const ExtensionLoadResult* subject = readySubject(origin, directory);
        ASSERT_NE(subject, nullptr);
        SubjectSession session(*subject);
        ASSERT_TRUE(session.launched()) << directory << ": " << session.error();
        const an::ui::extensions::ExtensionHostHandshakeResult& handshake = session.session().handshake;
        EXPECT_EQ(handshake.hello.protocol_version, an::ui::extensions::kExtensionProtocolVersion) << directory;
        EXPECT_EQ(handshake.hello.sdk_version, an::ui::extensions::kExtensionSdkVersion) << directory;
        EXPECT_TRUE(handshake.ack.accepted) << directory;
        EXPECT_EQ(handshake.ack.protocol_version, an::ui::extensions::kExtensionProtocolVersion) << directory;
        EXPECT_EQ(handshake.ack.sdk_version, an::ui::extensions::kExtensionSdkVersion) << directory;
        EXPECT_EQ(handshake.hello.sdk_backend,
                  std::string(directory) == "lite_protocol_process" ? std::string("protocol") : std::string("runtime"));
    }
}

// A host that greets the application at another SDK version is refused before the session starts.
TEST(ExtensionRegression, AHostThatSpeaksAnotherSdkVersionIsRefused)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "basic_process");
    ASSERT_NE(subject, nullptr);
    SubjectSession session(*subject, {"--hello-sdk-version", "2"});
    EXPECT_FALSE(session.launched());
    EXPECT_NE(session.error().find("SDK"), std::string::npos) << session.error();
}

// ---- The bundled extensions.

// The echo demo inverts its source in the source's own range - a fraction from one, a 16-bit code from 65535 - and
// records the run on the result.
TEST(ExtensionRegression, TheEchoDemoInvertsItsSourceAndRecordsTheRun)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Bundled, "an_echo_demo");
    ASSERT_NE(subject, nullptr);

    for (const bool integer : {false, true})
    {
        SCOPED_TRACE(integer ? "UInt16" : "Float32");
        SubjectSession session(*subject);
        ASSERT_TRUE(session.launched()) << session.error();
        const auto source = integer ? makeMonoUInt16Image("echo", 6, 4, 100, 997) : unitRamp(6, 4);
        ASSERT_TRUE(source);
        const SharedBufferDescriptor lent =
            session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source");
        ASSERT_TRUE(session.execute({}, {lent})) << session.error();

        const SharedBufferDescriptor* destination = resultOfRole(session.session(), SharedBufferRole::Destination);
        ASSERT_NE(destination, nullptr);
        const std::optional<an::consumer::ImageAsset> result = importResult(session.root(), *destination);
        ASSERT_TRUE(result);
        EXPECT_EQ(hasSampleType(*result, an::SampleType::UInt16), integer);
        const double full = integer ? 65535.0 : 1.0;
        expectSamples(*result, *source, [full](double value, auto, auto, auto) { return full - value; });

        const QJsonObject sidecar = sidecarOf(*destination);
        EXPECT_EQ(sidecar.value(QStringLiteral("an.demo.echo.label")).toString(), QStringLiteral("Echo Demo"));
        EXPECT_EQ(sidecar.value(QStringLiteral("an.demo.echo.pixel_type")).toString(),
                  integer ? QStringLiteral("uint16") : QStringLiteral("float32"));
        EXPECT_EQ(sidecar.value(QStringLiteral("an.demo.echo.width")).toString(), QStringLiteral("6"));
        EXPECT_EQ(sidecar.value(QStringLiteral("an.demo.echo.session_id")).toString().toStdString(),
                  session.session().prepared_session.session_id);
        EXPECT_EQ(sidecar.value(QStringLiteral("an.demo.echo.extension_id")).toString(),
                  QStringLiteral("an.demo.echo"));
    }
}

// The real-time preview demo previews the transform it commits, and its preview leaves the source as it was.
TEST(ExtensionRegression, TheRealTimePreviewDemoPreviewsWhatItCommits)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Bundled, "an_rt_preview_demo");
    ASSERT_NE(subject, nullptr);
    SubjectSession session(*subject);
    ASSERT_TRUE(session.launched()) << session.error();
    const auto source = makeRgbGradientImage(6, 5);
    ASSERT_TRUE(source);
    const QCborMap params{{QStringLiteral("strength"), 0.6},
                          {QStringLiteral("lift"), 0.02},
                          {QStringLiteral("mode"), QStringLiteral("punchy")},
                          {QStringLiteral("preserve_background"), false}};
    const auto expected = [](double value, auto, auto, auto) {
        return expectedRtPreviewDemoValue(value, 0.6, 0.02, QStringLiteral("punchy"), false);
    };

    const SharedBufferDescriptor lent =
        session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadOnly, "source");
    ASSERT_TRUE(session.preview(params, {lent})) << session.error();
    ASSERT_TRUE(session.session().last_preview_result.has_value());
    EXPECT_EQ(session.session().last_preview_result->status, "succeeded")
        << session.session().last_preview_result->detail;
    ASSERT_FALSE(session.session().last_preview_result->output_buffers.empty());
    const std::optional<an::consumer::ImageAsset> preview =
        importResult(session.root(), session.session().last_preview_result->output_buffers.front());
    ASSERT_TRUE(preview);
    expectSamples(*preview, *source, expected);

    const SharedBufferDescriptor target =
        session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source");
    ASSERT_TRUE(session.execute(params, {target})) << session.error();
    const SharedBufferDescriptor* destination = resultOfRole(session.session(), SharedBufferRole::Destination);
    ASSERT_NE(destination, nullptr);
    const std::optional<an::consumer::ImageAsset> committed = importResult(session.root(), *destination);
    ASSERT_TRUE(committed);
    expectSamples(*committed, *source, expected);
}

// ---- The author templates.

// The basic template scales its source by the strength it is given, on the full host.
TEST(ExtensionRegression, TheBasicTemplateScalesItsSourceByItsStrength)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "basic_process");
    ASSERT_NE(subject, nullptr);
    SubjectSession session(*subject);
    ASSERT_TRUE(session.launched()) << session.error();
    const auto source = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source);
    const SharedBufferDescriptor lent =
        session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source");
    ASSERT_TRUE(session.execute(QCborMap{{QStringLiteral("strength"), 0.5}}, {lent})) << session.error();
    const SharedBufferDescriptor* destination = resultOfRole(session.session(), SharedBufferRole::Destination);
    ASSERT_NE(destination, nullptr);
    const std::optional<an::consumer::ImageAsset> result = importResult(session.root(), *destination);
    ASSERT_TRUE(result);
    expectSamples(*result, *source, [](double value, auto, auto, auto) { return 0.5 * value; });
}

// The settings and diagnostics template scales its source, records the run on the result, stores what it last did in
// the extension's settings and writes its report into the session's artifacts.
TEST(ExtensionRegression, TheSettingsDiagnosticsTemplateScalesRecordsAndReports)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "settings_diagnostics_process");
    ASSERT_NE(subject, nullptr);
    InMemorySettings settings_store;
    an::ui::extensions::ExtensionSdkService sdk_service(&settings_store.repository());
    SubjectSession session(*subject, {}, &sdk_service);
    ASSERT_TRUE(session.launched()) << session.error();
    const auto source = unitRamp(5, 3);
    ASSERT_TRUE(source);
    const SharedBufferDescriptor lent =
        session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source");
    ASSERT_TRUE(
        session.execute(QCborMap{{QStringLiteral("strength"), 0.5}, {QStringLiteral("write_artifact"), true}}, {lent}))
        << session.error();
    const SharedBufferDescriptor* destination = resultOfRole(session.session(), SharedBufferRole::Destination);
    ASSERT_NE(destination, nullptr);
    const std::optional<an::consumer::ImageAsset> result = importResult(session.root(), *destination);
    ASSERT_TRUE(result);
    expectSamples(*result, *source, [](double value, auto, auto, auto) { return 0.5 * value; });

    const QJsonObject sidecar = sidecarOf(*destination);
    EXPECT_EQ(sidecar.value(QStringLiteral("example.settings_diagnostics.strength")).toString(), QStringLiteral("0.5"));
    EXPECT_EQ(sidecar.value(QStringLiteral("example.settings_diagnostics.session_id")).toString().toStdString(),
              session.session().prepared_session.session_id);
    const std::string artifact =
        sidecar.value(QStringLiteral("example.settings_diagnostics.artifact")).toString().toStdString();
    ASSERT_FALSE(artifact.empty()) << "the template writes its report where the session keeps artifacts";
    std::ifstream report{std::filesystem::path(artifact)};
    std::stringstream text;
    text << report.rdbuf();
    EXPECT_NE(text.str().find("strength=0.500"), std::string::npos) << text.str();

    QCborValue strength;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting(subject->manifest->id, "last_strength", &strength, {}, &session.error()))
        << session.error();
    EXPECT_DOUBLE_EQ(strength.toDouble(), 0.5);
}

// The auxiliary buffers template blends the selection mask and the weight map the application lends it into its
// result, and hands back the output mask they combine to.
TEST(ExtensionRegression, TheAuxiliaryBuffersTemplateBlendsItsMaskAndWeightAndReturnsAMask)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "auxiliary_buffers_process");
    ASSERT_NE(subject, nullptr);
    SubjectSession session(*subject);
    ASSERT_TRUE(session.launched()) << session.error();
    const auto source = makeMonoFloatImage("source", 5, 4, 0.1f, 0.02f);
    const auto selection = makeMonoFloatImage("selection", 5, 4, 0.0f, 0.05f);
    const auto weight = makeMonoFloatImage("weight", 5, 4, 0.3f, -0.01f);
    const auto mask_template = makeMonoFloatImage("output", 5, 4, 0.0f, 0.0f);
    ASSERT_TRUE(source && selection && weight && mask_template);
    const std::vector<SharedBufferDescriptor> buffers = {
        session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source"),
        session.lend(selection, SharedBufferRole::Mask, SharedBufferAccessMode::ReadOnly, "selection"),
        session.lend(weight, SharedBufferRole::Weight, SharedBufferAccessMode::ReadOnly, "weight_map"),
        session.lend(mask_template, SharedBufferRole::Mask, SharedBufferAccessMode::ReadWrite, "output_mask")};
    ASSERT_TRUE(session.execute(
        QCborMap{{QStringLiteral("mask_strength"), 0.4}, {QStringLiteral("weight_boost"), 0.25}}, buffers))
        << session.error();

    const SharedBufferDescriptor* destination = resultOfRole(session.session(), SharedBufferRole::Destination);
    const SharedBufferDescriptor* output_mask = resultOfRole(session.session(), SharedBufferRole::Mask, "output_mask");
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(output_mask, nullptr);
    const std::optional<an::consumer::ImageAsset> result = importResult(session.root(), *destination);
    const std::optional<an::consumer::ImageAsset> mask = importResult(session.root(), *output_mask);
    ASSERT_TRUE(result && mask);
    expectSamples(*result, *source, [&](double value, std::uint32_t x, std::uint32_t y, std::uint32_t) {
        const double m = sampleAt(*selection, x, y, 0u);
        const double w = sampleAt(*weight, x, y, 0u);
        return std::clamp(value * (1.0 - m * 0.4) + m * 0.4 + w * 0.25, 0.0, 1.0);
    });
    expectSamples(*mask, *source, [&](double, std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return std::clamp((std::max)(sampleAt(*selection, x, y, 0u), sampleAt(*weight, x, y, 0u)), 0.0, 1.0);
    });
}

// The locked wheelhouse template runs from the environment installed from its own wheelhouse and leaves its source as
// it was.
TEST(ExtensionRegression, TheLockedWheelhouseTemplateRunsFromItsInstalledWheelhouse)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "locked_wheelhouse_process");
    ASSERT_NE(subject, nullptr);
    EXPECT_EQ(subject->manifest->dependencies.dependency_context,
              an::ui::extensions::ExtensionDependencyContext::Private);
    SubjectSession session(*subject);
    ASSERT_TRUE(session.launched()) << session.error();
    const auto source = unitRamp(4, 4);
    ASSERT_TRUE(source);
    const SharedBufferDescriptor lent =
        session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source");
    ASSERT_TRUE(session.execute({}, {lent})) << session.error();
    const SharedBufferDescriptor* destination = resultOfRole(session.session(), SharedBufferRole::Destination);
    ASSERT_NE(destination, nullptr);
    const std::optional<an::consumer::ImageAsset> result = importResult(session.root(), *destination);
    ASSERT_TRUE(result);
    expectSamples(*result, *source, [](double value, auto, auto, auto) { return value; });
}

// The real-time preview template previews its transform and commits it with the scale and offset it applied.
TEST(ExtensionRegression, TheRealTimePreviewTemplatePreviewsAndCommitsItsTransform)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "rt_preview_process");
    ASSERT_NE(subject, nullptr);
    SubjectSession session(*subject);
    ASSERT_TRUE(session.launched()) << session.error();
    const auto source = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source);
    const QCborMap params{{QStringLiteral("strength"), 0.5},
                          {QStringLiteral("black_point"), 0.05},
                          {QStringLiteral("mode"), QStringLiteral("darken")}};
    const double scale = 1.0 - 0.5 * 0.30;
    const double offset = -0.5 * 0.10 + 0.05;
    const auto expected = [scale, offset](double value, auto, auto, auto) { return scale * value + offset; };

    ASSERT_TRUE(session.preview(
        params, {session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadOnly, "source")}))
        << session.error();
    ASSERT_TRUE(session.session().last_preview_result.has_value());
    ASSERT_FALSE(session.session().last_preview_result->output_buffers.empty());
    const std::optional<an::consumer::ImageAsset> preview =
        importResult(session.root(), session.session().last_preview_result->output_buffers.front());
    ASSERT_TRUE(preview);
    expectSamples(*preview, *source, expected);

    ASSERT_TRUE(session.execute(
        params, {session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source")}))
        << session.error();
    const SharedBufferDescriptor* destination = resultOfRole(session.session(), SharedBufferRole::Destination);
    ASSERT_NE(destination, nullptr);
    const std::optional<an::consumer::ImageAsset> committed = importResult(session.root(), *destination);
    ASSERT_TRUE(committed);
    expectSamples(*committed, *source, expected);
    const QJsonObject sidecar = sidecarOf(*destination);
    EXPECT_NEAR(sidecar.value(QStringLiteral("example.rt_preview.scale")).toString().toDouble(), scale, 1e-12);
    EXPECT_NEAR(sidecar.value(QStringLiteral("example.rt_preview.offset")).toString().toDouble(), offset, 1e-12);
}

// ---- Cancellation, as the application cancels each kind of subject.

// A preview the application supersedes while it is in flight answers that it was cancelled, with nothing to show, and
// the next preview lands with the subject's transform - for each subject that previews.
TEST(ExtensionRegression, APreviewCancelledInFlightShowsNothingAndTheNextOneLands)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    struct Previewing
    {
        Origin origin;
        const char* directory;
    };
    for (const Previewing previewing :
         {Previewing{Origin::Bundled, "an_rt_preview_demo"}, Previewing{Origin::Template, "rt_preview_process"}})
    {
        SCOPED_TRACE(previewing.directory);
        const ExtensionLoadResult* subject = readySubject(previewing.origin, previewing.directory);
        ASSERT_NE(subject, nullptr);
        SubjectSession session(*subject, {"--preview-delay-ms", std::to_string(600 * kIpcTimingMultiplier)});
        ASSERT_TRUE(session.launched()) << session.error();
        const auto source = makeRgbGradientImage(4, 3);
        ASSERT_TRUE(source);
        const SharedBufferDescriptor lent =
            session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadOnly, "source");

        std::string request_id;
        ASSERT_TRUE(session.manager().sendPreviewRequest(&session.session(), "superseded", QCborMap{}, {lent},
                                                         &request_id, &session.error()))
            << session.error();
        ASSERT_TRUE(session.manager().sendCancelRequest(&session.session(), nullptr, &session.error()))
            << session.error();
        ASSERT_TRUE(pumpUntilRequestCompletes(session.manager(), &session.session(), 5000 * kIpcTimingMultiplier,
                                              &session.error()))
            << session.error();
        ASSERT_TRUE(session.session().last_preview_result.has_value());
        EXPECT_EQ(session.session().last_preview_result->result_kind, "preview");
        EXPECT_EQ(session.session().last_preview_result->status, "cancelled");
        EXPECT_TRUE(session.session().last_preview_result->output_buffers.empty());

        ASSERT_TRUE(session.preview(QCborMap{}, {lent})) << session.error();
        EXPECT_EQ(session.session().last_preview_result->status, "succeeded");
        ASSERT_FALSE(session.session().last_preview_result->output_buffers.empty());
        const std::optional<an::consumer::ImageAsset> preview =
            importResult(session.root(), session.session().last_preview_result->output_buffers.front());
        ASSERT_TRUE(preview);
        EXPECT_EQ(widthOf(*preview), widthOf(*source));
        EXPECT_GT(std::fabs(sampleAt(*preview, 3u, 2u, 0u) - sampleAt(*source, 3u, 2u, 0u)), 1e-6)
            << "the next preview applies the subject's transform";
    }
}

// An execute cancelled while it is in flight commits nothing, leaves the image it was lent as it was, and ends the
// session: the application accepts no execute that answers without a commit - for each subject that executes.
TEST(ExtensionRegression, AnExecuteCancelledInFlightCommitsNothingAndLeavesItsSourceAsItWas)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    struct Executing
    {
        Origin origin;
        const char* directory;
    };
    for (const Executing executing :
         {Executing{Origin::Bundled, "an_echo_demo"}, Executing{Origin::Bundled, "an_custom_ui_demo"},
          Executing{Origin::Bundled, "an_rt_preview_demo"}, Executing{Origin::Template, "basic_process"},
          Executing{Origin::Template, "auxiliary_buffers_process"},
          Executing{Origin::Template, "locked_wheelhouse_process"}, Executing{Origin::Template, "rt_preview_process"},
          Executing{Origin::Template, "settings_diagnostics_process"}})
    {
        SCOPED_TRACE(executing.directory);
        const ExtensionLoadResult* subject = readySubject(executing.origin, executing.directory);
        ASSERT_NE(subject, nullptr);
        SubjectSession session(*subject, {"--execute-delay-ms", std::to_string(600 * kIpcTimingMultiplier)});
        ASSERT_TRUE(session.launched()) << session.error();
        const auto source = unitRamp(4, 3);
        ASSERT_TRUE(source);
        const SharedBufferDescriptor lent =
            session.lend(source, SharedBufferRole::Source, SharedBufferAccessMode::ReadWrite, "source");

        std::string request_id;
        ASSERT_TRUE(session.manager().sendExecuteRequest(&session.session(), "cancelled", QCborMap{}, {lent},
                                                         &request_id, &session.error()))
            << session.error();
        ASSERT_TRUE(session.manager().sendCancelRequest(&session.session(), nullptr, &session.error()))
            << session.error();
        EXPECT_FALSE(pumpUntilRequestCompletes(session.manager(), &session.session(), 5000 * kIpcTimingMultiplier,
                                               &session.error()));
        EXPECT_FALSE(session.session().last_view_commit.has_value());
        EXPECT_EQ(session.session().state, an::ui::extensions::ExtensionHostSessionState::Failed);
        EXPECT_NE(session.error().find("view.commit"), std::string::npos) << session.error();

        const std::optional<an::consumer::ImageAsset> after = importResult(session.root(), lent);
        ASSERT_TRUE(after);
        expectSamples(*after, *source, [](double value, auto, auto, auto) { return value; }, 0.0);
    }
}

// The sequence workflow demo cancelled while it is in flight answers as a cancelled workflow and opens nothing.
TEST(ExtensionRegression, TheSequenceWorkflowDemoCancelledInFlightOpensNothing)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Bundled, "an_sequence_workflow_demo");
    ASSERT_NE(subject, nullptr);
    InMemorySettings settings_store;
    an::ui::extensions::ExtensionSdkService sdk_service(&settings_store.repository());
    SubjectSession session(*subject, {"--execute-delay-ms", std::to_string(600 * kIpcTimingMultiplier)}, &sdk_service);
    ASSERT_TRUE(session.launched()) << session.error();

    std::string request_id;
    ASSERT_TRUE(session.manager().sendExecuteRequest(&session.session(), "cancelled-workflow",
                                                     QCborMap{{QStringLiteral("synthetic_frames"), 3}}, {}, &request_id,
                                                     &session.error()))
        << session.error();
    ASSERT_TRUE(session.manager().sendCancelRequest(&session.session(), nullptr, &session.error())) << session.error();
    ASSERT_TRUE(
        pumpUntilRequestCompletes(session.manager(), &session.session(), 5000 * kIpcTimingMultiplier, &session.error()))
        << session.error();
    ASSERT_TRUE(session.session().last_preview_result.has_value());
    EXPECT_EQ(session.session().last_preview_result->result_kind, "workflow");
    EXPECT_EQ(session.session().last_preview_result->status, "cancelled");
    EXPECT_TRUE(sdk_service.workflowOutputs().empty());
    EXPECT_EQ(session.session().state, an::ui::extensions::ExtensionHostSessionState::Active);
}

// ---- The sidecar beside a lent buffer, against its record.

namespace
{

/// A FITS value by its meaning: a string without its quotes, doubled quotes undone and trailing blanks dropped; a
/// number in one spelling; anything else as written.
[[nodiscard]] QString fitsValueMeaning(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))
    {
        QString content = value.mid(1, value.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
        while (content.endsWith(QLatin1Char(' ')))
        {
            content.chop(1);
        }
        return QStringLiteral("text:") + content;
    }
    bool number = false;
    const double parsed = value.toDouble(&number);
    return number ? QStringLiteral("number:") + QString::number(parsed, 'g', 12) : value;
}

/// A record by the card it reads as, or the record itself when it reads as none.
[[nodiscard]] QString recordMeaning(const QString& record)
{
    std::string header = record.toStdString();
    header.resize(80u, ' ');
    header += std::string("END") + std::string(77u, ' ');
    const an::Result<std::vector<an::CodecCard>> cards = an::parseFitsHeaderCards(header);
    if (!cards.hasValue() || cards->size() != 1u)
    {
        return QStringLiteral("unread:") + record;
    }
    const an::CodecCard& card = cards->front();
    return QString::fromStdString(card.keyword()) + QStringLiteral("=") +
           fitsValueMeaning(QString::fromStdString(card.value())) + QStringLiteral(" / ") +
           QString::fromStdString(card.comment()).trimmed();
}

/// A number to nine significant digits, so two writers' last places agree.
[[nodiscard]] QJsonValue numberMeaning(const QJsonValue& value)
{
    return value.isDouble() ? QJsonValue(QString::number(value.toDouble(), 'g', 9)) : value;
}

/// A SIP coefficient matrix by its terms that are not zero, whatever size the writer laid it out in.
[[nodiscard]] QJsonObject sipTerms(const QJsonArray& rows)
{
    QJsonObject terms;
    for (qsizetype row = 0; row < rows.size(); ++row)
    {
        const QJsonArray values = rows.at(row).toArray();
        for (qsizetype column = 0; column < values.size(); ++column)
        {
            if (values.at(column).isDouble() && values.at(column).toDouble() != 0.0)
            {
                terms.insert(QStringLiteral("%1,%2").arg(row).arg(column), numberMeaning(values.at(column)));
            }
        }
    }
    return terms;
}

/// What a sidecar states, by meaning: the file name without its directory, every card's value by its meaning, every
/// card's record by the card it reads as, numbers to nine digits and SIP matrices by their terms.
[[nodiscard]] QJsonObject sidecarMeaning(const QJsonObject& sidecar)
{
    QJsonObject meaning;
    for (auto it = sidecar.begin(); it != sidecar.end(); ++it)
    {
        const QString key = it.key();
        if (key == QStringLiteral("FILENAME"))
        {
            meaning.insert(key, QString::fromStdString(
                                    std::filesystem::path(it.value().toString().toStdString()).filename().string()));
        }
        else if (key == QStringLiteral("fits_header"))
        {
            QJsonObject header;
            const QJsonObject values = it.value().toObject();
            for (auto card = values.begin(); card != values.end(); ++card)
            {
                header.insert(card.key(), fitsValueMeaning(card.value().toString()));
            }
            meaning.insert(key, header);
        }
        else if (key == QStringLiteral("fits_cards"))
        {
            QJsonObject cards;
            for (const QJsonValue& entry : it.value().toArray())
            {
                const QJsonObject card = entry.toObject();
                QJsonObject stated{
                    {QStringLiteral("value"), fitsValueMeaning(card.value(QStringLiteral("value")).toString())}};
                if (card.contains(QStringLiteral("comment")))
                {
                    stated.insert(QStringLiteral("comment"),
                                  card.value(QStringLiteral("comment")).toString().trimmed());
                }
                if (card.contains(QStringLiteral("raw")))
                {
                    stated.insert(QStringLiteral("raw"), recordMeaning(card.value(QStringLiteral("raw")).toString()));
                }
                cards.insert(card.value(QStringLiteral("key")).toString(), stated);
            }
            meaning.insert(key, cards);
        }
        else if (key == QStringLiteral("astrometry"))
        {
            QJsonObject astrometry;
            const QJsonObject values = it.value().toObject();
            for (auto field = values.begin(); field != values.end(); ++field)
            {
                if (field.key() != QStringLiteral("sip"))
                {
                    astrometry.insert(field.key(), numberMeaning(field.value()));
                    continue;
                }
                QJsonObject sip;
                const QJsonObject stated = field.value().toObject();
                for (auto term = stated.begin(); term != stated.end(); ++term)
                {
                    sip.insert(term.key(), term.value().isArray() ? QJsonValue(sipTerms(term.value().toArray()))
                                                                  : numberMeaning(term.value()));
                }
                astrometry.insert(field.key(), sip);
            }
            meaning.insert(key, astrometry);
        }
        else if (it.value().isString())
        {
            meaning.insert(key, fitsValueMeaning(it.value().toString()));
        }
        else
        {
            meaning.insert(key, it.value());
        }
    }
    return meaning;
}

/// A descriptor as the record states one: its geometry, representation, format, colour space, strides, role, access
/// and name, and the area it covers when it covers one - nothing the session or the paths decide.
[[nodiscard]] QJsonObject descriptorRecord(const SharedBufferDescriptor& descriptor)
{
    const auto pixelType = [](const an::ui::extensions::SharedBufferPixelType type) {
        switch (type)
        {
        case an::ui::extensions::SharedBufferPixelType::UInt8:
            return QStringLiteral("uint8");
        case an::ui::extensions::SharedBufferPixelType::UInt16:
            return QStringLiteral("uint16");
        case an::ui::extensions::SharedBufferPixelType::Float32:
            return QStringLiteral("float32");
        case an::ui::extensions::SharedBufferPixelType::Float64:
            return QStringLiteral("float64");
        }
        return QString();
    };
    const auto pixelFormat = [](const an::ui::extensions::SharedBufferPixelFormat format) {
        switch (format)
        {
        case an::ui::extensions::SharedBufferPixelFormat::Mono:
            return QStringLiteral("mono");
        case an::ui::extensions::SharedBufferPixelFormat::RGB:
            return QStringLiteral("rgb");
        case an::ui::extensions::SharedBufferPixelFormat::BayerRGGB:
            return QStringLiteral("bayer_rggb");
        case an::ui::extensions::SharedBufferPixelFormat::BayerBGGR:
            return QStringLiteral("bayer_bggr");
        case an::ui::extensions::SharedBufferPixelFormat::BayerGRBG:
            return QStringLiteral("bayer_grbg");
        case an::ui::extensions::SharedBufferPixelFormat::BayerGBRG:
            return QStringLiteral("bayer_gbrg");
        }
        return QString();
    };
    const auto colorSpace = [](const an::ui::extensions::SharedBufferColorSpace space) {
        switch (space)
        {
        case an::ui::extensions::SharedBufferColorSpace::Linear:
            return QStringLiteral("linear");
        case an::ui::extensions::SharedBufferColorSpace::SRGB:
            return QStringLiteral("srgb");
        case an::ui::extensions::SharedBufferColorSpace::Grayscale:
            return QStringLiteral("grayscale");
        }
        return QString();
    };
    QJsonObject record{{QStringLiteral("width"), descriptor.width},
                       {QStringLiteral("height"), descriptor.height},
                       {QStringLiteral("channels"), descriptor.channels},
                       {QStringLiteral("pixel_type"), pixelType(descriptor.pixel_type)},
                       {QStringLiteral("pixel_format"), pixelFormat(descriptor.pixel_format)},
                       {QStringLiteral("color_space"), colorSpace(descriptor.color_space)},
                       {QStringLiteral("row_stride"), static_cast<qint64>(descriptor.row_stride)},
                       {QStringLiteral("plane_stride"), static_cast<qint64>(descriptor.plane_stride)},
                       {QStringLiteral("byte_size"), static_cast<qint64>(descriptor.byte_size)},
                       {QStringLiteral("buffer_role"),
                        QString::fromUtf8(an::ui::extensions::sharedBufferRoleName(descriptor.buffer_role))},
                       {QStringLiteral("access_mode"), descriptor.access_mode == SharedBufferAccessMode::ReadOnly
                                                           ? QStringLiteral("read_only")
                                                           : QStringLiteral("read_write")},
                       {QStringLiteral("symbolic_name"), QString::fromStdString(descriptor.symbolic_name)}};
    if (descriptor.offset_x.has_value())
    {
        record.insert(QStringLiteral("offset_x"), *descriptor.offset_x);
        record.insert(QStringLiteral("offset_y"), descriptor.offset_y.value_or(0));
        record.insert(QStringLiteral("source_width"), descriptor.source_width.value_or(0));
        record.insert(QStringLiteral("source_height"), descriptor.source_height.value_or(0));
    }
    return record;
}

/// One place two documents differ: its path and what each side states there.
struct SidecarDifference
{
    std::string path;
    std::string before;
    std::string after;
};

/// A JSON value as compact text, for a failure message; `(absent)` for a key one side lacks.
[[nodiscard]] std::string jsonText(const QJsonValue& value)
{
    if (value.isUndefined())
    {
        return "(absent)";
    }
    if (value.isObject())
    {
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString();
    }
    if (value.isArray())
    {
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).toStdString();
    }
    return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).toStdString();
}

/// Every place two JSON values differ, as paths under a prefix: a key one side lacks, or a value that is not equal.
void collectDifferences(const QString& path, const QJsonValue& before, const QJsonValue& after,
                        std::vector<SidecarDifference>& differences)
{
    if (before.isObject() && after.isObject())
    {
        const QJsonObject left = before.toObject();
        const QJsonObject right = after.toObject();
        QStringList keys = left.keys();
        for (const QString& key : right.keys())
        {
            if (!keys.contains(key))
            {
                keys.push_back(key);
            }
        }
        for (const QString& key : keys)
        {
            collectDifferences(path + QLatin1Char('/') + key, left.value(key), right.value(key), differences);
        }
        return;
    }
    if (before != after)
    {
        differences.push_back({path.toStdString(), jsonText(before), jsonText(after)});
    }
}

/// Whether a registered pattern names a path: segment by segment, a `*` standing for any one segment, and a pattern
/// that ends in `/**` naming everything below it.
[[nodiscard]] bool patternNames(const QString& pattern, const QString& path)
{
    const QStringList wanted = pattern.split(QLatin1Char('/'));
    const QStringList segments = path.split(QLatin1Char('/'));
    for (qsizetype index = 0; index < wanted.size(); ++index)
    {
        if (wanted.at(index) == QStringLiteral("**"))
        {
            return true;
        }
        if (index >= segments.size() ||
            (wanted.at(index) != QStringLiteral("*") && wanted.at(index) != segments.at(index)))
        {
            return false;
        }
    }
    return wanted.size() == segments.size();
}

/// A checked-in JSON document under the expectation directory, or an empty object.
[[nodiscard]] QJsonObject expectationDocument(const std::string_view name)
{
    std::string error;
    const std::optional<std::filesystem::path> directory = an::tests::fixtures::expectationDirectory(error);
    EXPECT_TRUE(directory.has_value()) << error;
    if (!directory.has_value())
    {
        return {};
    }
    QFile file(QString::fromStdString((*directory / std::filesystem::path(name)).string()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << name;
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

// Each sidecar case is read as the viewer reads a file and lent as the host manager lends a view's image; what the
// buffer's descriptor and sidecar state is held, by meaning, to the recorded sidecar for the same file, and every place
// they differ to the register beside the record - the corrections it states and nothing else, in both directions.
TEST(ExtensionRegression, TheSidecarSaysWhatItsRecordSaysButForItsRegisteredCorrections)
{
    const QJsonObject record = expectationDocument(an::tests::fixtures::kSidecarRecordFile);
    const QJsonArray calls =
        record.value(QStringLiteral("contract")).toObject().value(QStringLiteral("calls")).toArray();
    ASSERT_EQ(calls.size(), static_cast<qsizetype>(an::tests::fixtures::sidecarCases().size()));
    const QJsonArray registered = expectationDocument(an::tests::fixtures::kSidecarDifferencesFile)
                                      .value(QStringLiteral("differences"))
                                      .toArray();
    ASSERT_FALSE(registered.isEmpty());

    const ScratchDirectory scratch("regression-sidecars");
    const std::filesystem::path root = scratch.path() / "buffers";
    std::vector<SidecarDifference> differences;
    for (qsizetype index = 0; index < calls.size(); ++index)
    {
        const an::tests::fixtures::SidecarCase& item =
            an::tests::fixtures::sidecarCases()[static_cast<std::size_t>(index)];
        const QJsonObject recorded = calls.at(index).toObject();
        ASSERT_EQ(recorded.value(QStringLiteral("call")).toString().toStdString(), item.call);

        std::string error;
        const std::optional<an::tests::fixtures::FixtureSamples> samples =
            an::tests::fixtures::materializeFixture(item.image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        const std::filesystem::path path = scratch.path() / (item.image.name + ".fits");
        ASSERT_TRUE(an::tests::fixtures::writeFixtureFits(item.image, *samples, path, error)) << error;
        an::ui::ViewReadRequest read;
        read.path = QString::fromStdString(path.string());
        an::Result<an::ui::ViewReadResult> image = an::ui::readViewImage(testSession(), read, {}, nullptr);
        ASSERT_TRUE(image.hasValue()) << item.call << ": " << image.status().message();

        an::Result<an::consumer::AssetReadLease> lease = image->asset.read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        an::ui::extensions::ImageExportRequest request;
        request.sessionId = "regression-sidecars";
        request.role = SharedBufferRole::Source;
        request.accessMode = SharedBufferAccessMode::ReadWrite;
        request.symbolicName = "source";
        request.fileName = path.string();
        if (item.region.has_value())
        {
            request.region = QRect(static_cast<int>(item.region->x), static_cast<int>(item.region->y),
                                   static_cast<int>(item.region->width), static_cast<int>(item.region->height));
        }
        an::Result<an::ui::extensions::SharedImageBuffer> lent =
            an::ui::extensions::SharedBufferManager(root).exportImage(lease->image(), request);
        ASSERT_TRUE(lent.hasValue()) << item.call << ": " << lent.status().message();

        const QJsonObject before = recorded.value(QStringLiteral("result")).toObject();
        const QJsonObject after{{QStringLiteral("descriptor"), descriptorRecord(lent->descriptor())},
                                {QStringLiteral("sidecar"), sidecarOf(lent->descriptor())}};
        collectDifferences(QString::fromStdString(item.call) + QStringLiteral("/descriptor"),
                           before.value(QStringLiteral("descriptor")), after.value(QStringLiteral("descriptor")),
                           differences);
        collectDifferences(QString::fromStdString(item.call) + QStringLiteral("/sidecar"),
                           sidecarMeaning(before.value(QStringLiteral("sidecar")).toObject()),
                           sidecarMeaning(after.value(QStringLiteral("sidecar")).toObject()), differences);
    }

    for (const SidecarDifference& difference : differences)
    {
        const bool named = std::any_of(registered.begin(), registered.end(), [&difference](const QJsonValue& entry) {
            return patternNames(entry.toObject().value(QStringLiteral("path")).toString(),
                                QString::fromStdString(difference.path));
        });
        EXPECT_TRUE(named) << difference.path << " differs from the recorded sidecar - " << difference.before
                           << " recorded, " << difference.after << " written - and no registered correction names it";
    }
    for (const QJsonValue& entry : registered)
    {
        const QString pattern = entry.toObject().value(QStringLiteral("path")).toString();
        EXPECT_FALSE(entry.toObject().value(QStringLiteral("reason")).toString().isEmpty()) << pattern.toStdString();
        const bool seen =
            std::any_of(differences.begin(), differences.end(), [&pattern](const SidecarDifference& difference) {
                return patternNames(pattern, QString::fromStdString(difference.path));
            });
        EXPECT_TRUE(seen) << pattern.toStdString() << " is registered and no longer differs";
    }
}

// ---- The bundled runtime's scientific packages.

// The bundled runtime carries NumPy, SciPy and scikit-image, because the shared host profile every bundled extension
// declares curates them: an extension run in the environment the application provisions for one imports all three,
// and uses them.
TEST(ExtensionRegression, TheScientificCoreProfileGivesAnExtensionNumpySciPyAndScikitImage)
{
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Bundled, "an_echo_demo");
    ASSERT_NE(subject, nullptr);
    EXPECT_EQ(subject->manifest->dependencies.dependency_context,
              an::ui::extensions::ExtensionDependencyContext::SharedHost);
    EXPECT_EQ(subject->manifest->dependencies.shared_host_profile, "scientific_core");

    // A probe package of the case's own, run in the echo demo's environment: it blurs its source with SciPy and
    // records the three libraries' versions and scikit-image's view of the result.
    const ScratchDirectory scratch("regression-scientific-core");
    const std::filesystem::path package = scratch.path() / "probe";
    std::filesystem::create_directories(package);
    std::ofstream(package / "regression_scientific_core.py")
        << "import numpy\n"
        << "import scipy\n"
        << "import scipy.ndimage\n"
        << "import skimage\n"
        << "import skimage.util\n"
        << "from afternight import ui\n\n"
        << "class ScientificCoreProbe(ui.ProcessWindow):\n"
        << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,\n"
        << "                output_masks=None):\n"
        << "        source = numpy.asarray(src_image.to_numpy(), dtype=numpy.float32)\n"
        << "        dst_image.from_numpy(scipy.ndimage.uniform_filter(source, size=3).astype(numpy.float32))\n"
        << "        dst_image.set_metadata('probe.numpy', numpy.__version__)\n"
        << "        dst_image.set_metadata('probe.scipy', scipy.__version__)\n"
        << "        dst_image.set_metadata('probe.skimage', skimage.__version__)\n"
        << "        dst_image.set_metadata('probe.skimage_dtype', str(skimage.util.img_as_float32(source).dtype))\n";

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request = launchRequestFor(*subject);
    request.extension_id = "an.regression.scientific_core";
    request.host_arguments = {"--extension-package-root",
                              package.string(),
                              "--entry-point",
                              "regression_scientific_core",
                              "--process-class",
                              "ScientificCoreProbe",
                              "--environment-root",
                              subject->environment->environment_path.string(),
                              "--runtime-root",
                              runtimeRootForCurrentAppDir().string(),
                              "--stay-alive-ms",
                              std::to_string(8000 * kIpcTimingMultiplier)};
    an::ui::extensions::ExtensionHostRunningSession session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(manager.prepareSession(request), &session, &error)) << error;
    SessionGuard guard{manager, session};

    const std::filesystem::path root = uniqueSharedBufferRootForTest("regression-scientific-core");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    const auto source = makeMonoFloatImage("probe", 5, 4, 0.0f, 0.05f);
    ASSERT_TRUE(source);
    auto lent = makeImageBuffer(root, session.prepared_session.session_id, source, SharedBufferRole::Source,
                                SharedBufferAccessMode::ReadWrite, "source", &error);
    ASSERT_TRUE(lent.valid()) << error;
    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&session, "scientific-core", {lent.descriptor()}, &request_id, &error))
        << error;
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &session, 15000 * kIpcTimingMultiplier, &error)) << error;
    const SharedBufferDescriptor* destination = resultOfRole(session, SharedBufferRole::Destination);
    ASSERT_NE(destination, nullptr);
    const QJsonObject sidecar = sidecarOf(*destination);
    EXPECT_FALSE(sidecar.value(QStringLiteral("probe.numpy")).toString().isEmpty());
    EXPECT_FALSE(sidecar.value(QStringLiteral("probe.scipy")).toString().isEmpty());
    EXPECT_FALSE(sidecar.value(QStringLiteral("probe.skimage")).toString().isEmpty());
    EXPECT_EQ(sidecar.value(QStringLiteral("probe.skimage_dtype")).toString(), QStringLiteral("float32"));
    const std::optional<an::consumer::ImageAsset> result = importResult(root, *destination);
    ASSERT_TRUE(result);
    // The centre of a three-by-three mean over a ramp is the ramp's own value there.
    EXPECT_NEAR(sampleAt(*result, 2u, 1u, 0u), sampleAt(*source, 2u, 1u, 0u), 1e-5);
}

// ---- The standalone dialogs, driven as a person drives them.

namespace
{

/// A package that runs a subject's own process class and presses its own dialog, as a person would: its module
/// subclasses the subject's class, imported from the subject's package, and `drive` runs on the dialog's event loop
/// once the subject has opened it. Written under a scratch directory; launched in the subject's own environment.
class DialogDriver
{
public:
    DialogDriver(const ExtensionLoadResult& subject, const std::filesystem::path& directory, const std::string& base,
                 const std::string& drive)
        : m_root(directory / "driver")
    {
        std::filesystem::create_directories(m_root);
        const an::ui::extensions::ExtensionManifest& manifest = *subject.manifest;
        std::ofstream module(m_root / "regression_dialog_driver.py");
        module << "import sys\n"
               << "sys.path.insert(0, r'" << manifest.package_root << "')\n"
               << "from PySide6 import QtCore\n"
               << "import afternight\n"
               << "from afternight import ui\n"
               << "from " << manifest.entry_point << " import " << manifest.processes.front().class_name << "\n\n"
               << "class RegressionDialogDriver(" << manifest.processes.front().class_name << "):\n"
               << base << "\n"
               << drive << "\n";
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept
    {
        return m_root;
    }

private:
    std::filesystem::path m_root;
};

/// What the application saw of a driven dialog's session: the images it was asked to commit and to open.
struct DialogOutcome
{
    std::vector<an::consumer::ImageAsset> committed;
    int opened = 0;
    std::vector<std::string> openedTitles;
    /// Whether the session had ended by itself when the pump stopped, before the case stopped it.
    bool sessionEnded = false;
};

/// Launches a dialog driver over a view showing an image, as the application binds a process to a view, and pumps
/// until `done` holds, the session ends or `bound_ms` passes.
template <typename Done>
void runDialogDriver(const ExtensionLoadResult& subject, const DialogDriver& driver,
                     const std::shared_ptr<an::consumer::ImageAsset>& image, DialogOutcome& outcome, Done&& done,
                     const int bound_ms = 10000 * kIpcTimingMultiplier)
{
    const std::filesystem::path root = uniqueSharedBufferRootForTest("regression-dialog-" + subject.manifest->id);
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    const std::string view_name = "regression-dialog-view";
    an::tests::processes::ViewHandle target = targetViewOf(view_name, image, "regression-dialog-view.fits");
    ASSERT_NE(target, nullptr);

    InMemorySettings settings_store;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        &settings_store.repository(),
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = view_name;
            view_state.is_active = true;
            view_state.has_image = true;
            view_state.width = static_cast<int>(widthOf(*image));
            view_state.height = static_cast<int>(heightOf(*image));
            view_state.channels = static_cast<int>(channelsOf(*image));
            return {view_state};
        },
        [&image, &root](const std::string_view session_id, const std::string_view request_id, const std::string_view,
                        an::ui::extensions::ExtensionViewSnapshot* snapshot, std::string* snapshot_error) -> bool {
            *snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(an::ui::extensions::SharedBufferManager(root), *image, session_id,
                                     SharedBufferRole::Preview, &buffer, snapshot_error,
                                     std::string(request_id) + ".snapshot", "snapshot",
                                     SharedBufferAccessMode::ReadOnly))
            {
                return false;
            }
            snapshot->source_buffer = buffer.descriptor();
            return true;
        },
        {}, {}, {},
        [&outcome](std::string_view, std::string_view, const SharedBufferDescriptor&, const QString& title,
                   std::string*) {
            ++outcome.opened;
            outcome.openedTitles.push_back(title.toStdString());
            return true;
        });
    coordinator.setManagedImageHooks(
        [&outcome](ImageViewControl*, an::consumer::ImageAsset&& result, const std::string&, const std::string&) {
            outcome.committed.push_back(std::move(result));
        });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "regression.dialog." + subject.manifest->id;
    binding.extension_id = subject.manifest->id;
    binding.display_name = subject.manifest->name;
    binding.host_executable = extensionHostPathForCurrentAppDir();
    binding.capabilities = {"execute"};
    binding.process_capabilities = subject.manifest->processes.front().capabilities;
    binding.host_arguments = {"--extension-package-root",
                              driver.root().string(),
                              "--entry-point",
                              "regression_dialog_driver",
                              "--process-class",
                              "RegressionDialogDriver",
                              "--environment-root",
                              subject.environment->environment_path.string(),
                              "--runtime-root",
                              runtimeRootForCurrentAppDir().string()};
    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    static_cast<void>(pumpCoordinatorUntil(coordinator, bound_ms,
                                           [&]() { return done() || coordinator.activeSessionCount() == 0u; }));
    outcome.sessionEnded = coordinator.activeSessionCount() == 0u;
    EXPECT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 3000 * kIpcTimingMultiplier));
}

/// Skips a case whose dialog needs PySide6 where the bundled wheels cannot attach to a debug Qt.
#if defined(_MSC_VER) && !defined(NDEBUG)
#define AN_REGRESSION_REQUIRES_RELEASE_PYSIDE() \
    GTEST_SKIP() << "PySide6 wheels target the release Qt runtime and cannot attach to an MSVC debug QApplication"
#else
#define AN_REGRESSION_REQUIRES_RELEASE_PYSIDE() static_cast<void>(0)
#endif

} // namespace

// The standalone preview demo's own dialog renders the active view with the choices made in it and commits the result
// to the view, as its Execute button does.
TEST(ExtensionRegression, TheStandaloneDemosDialogCommitsWhatItRenders)
{
    AN_REGRESSION_REQUIRES_RELEASE_PYSIDE();
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Bundled, "an_standalone_preview_demo");
    ASSERT_NE(subject, nullptr);
    const ScratchDirectory scratch("regression-standalone-demo");
    const DialogDriver driver(*subject, scratch.path(),
                              "    def __init__(self, context):\n"
                              "        super().__init__(context)\n"
                              "        if self._dialog is not None:\n"
                              "            QtCore.QTimer.singleShot(0, self._drive)\n",
                              "    def _drive(self):\n"
                              "        if self._execute_button is None or not self._execute_button.isEnabled():\n"
                              "            QtCore.QTimer.singleShot(50, self._drive)\n"
                              "            return\n"
                              "        self._strength_spin.setValue(0.3)\n"
                              "        self._lift_spin.setValue(0.02)\n"
                              "        self._mode_combo.setCurrentIndex(self._mode_combo.findData('gentle'))\n"
                              "        self._apply_mask_checkbox.setChecked(False)\n"
                              "        self._apply_weight_checkbox.setChecked(False)\n"
                              "        self._execute_button.click()\n");
    const auto image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(image);
    DialogOutcome outcome;
    runDialogDriver(*subject, driver, image, outcome, [&outcome]() { return !outcome.committed.empty(); });
    ASSERT_EQ(outcome.committed.size(), 1u);
    const double gamma = 1.0 / (1.0 + 0.3 * 0.5);
    expectSamples(outcome.committed.front(), *image,
                  [gamma](double value, auto, auto, auto) { return std::pow((std::max)(value, 0.0), gamma) + 0.02; });
}

// Closing the standalone preview demo's dialog without executing commits nothing - how a person cancels it.
TEST(ExtensionRegression, TheStandaloneDemosDialogClosedCommitsNothing)
{
    AN_REGRESSION_REQUIRES_RELEASE_PYSIDE();
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Bundled, "an_standalone_preview_demo");
    ASSERT_NE(subject, nullptr);
    const ScratchDirectory scratch("regression-standalone-demo-close");
    const DialogDriver driver(*subject, scratch.path(),
                              "    def __init__(self, context):\n"
                              "        super().__init__(context)\n"
                              "        if self._dialog is not None:\n"
                              "            QtCore.QTimer.singleShot(0, self._drive)\n",
                              "    def _drive(self):\n"
                              "        if self._execute_button is None or not self._execute_button.isEnabled():\n"
                              "            QtCore.QTimer.singleShot(50, self._drive)\n"
                              "            return\n"
                              "        self._dialog.close()\n");
    const auto image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(image);
    DialogOutcome outcome;
    runDialogDriver(*subject, driver, image, outcome, []() { return false; }, 3000 * kIpcTimingMultiplier);
    EXPECT_TRUE(outcome.committed.empty());
    // The demo hides its process window for a dialog of its own and closes the window when the dialog closes, which
    // ends its session: one left running refuses the next launch over the image.
    EXPECT_TRUE(outcome.sessionEnded) << "closing the dialog left the extension's session running";
}

// The standalone preview template's dialog opens its preview as an image of its own and commits the same rendering to
// the view, as its Preview and Commit buttons do.
TEST(ExtensionRegression, TheStandaloneTemplatesDialogPreviewsAndCommitsItsRendering)
{
    AN_REGRESSION_REQUIRES_RELEASE_PYSIDE();
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "standalone_preview_process");
    ASSERT_NE(subject, nullptr);
    const ScratchDirectory scratch("regression-standalone-template");
    const DialogDriver driver(*subject, scratch.path(),
                              "    def _open_dialog(self):\n"
                              "        super()._open_dialog()\n"
                              "        QtCore.QTimer.singleShot(0, self._drive)\n",
                              "    def _drive(self):\n"
                              "        if self._current_source() is None:\n"
                              "            QtCore.QTimer.singleShot(50, self._drive)\n"
                              "            return\n"
                              "        self._strength.setValue(0.5)\n"
                              "        self._preview()\n"
                              "        self._commit()\n");
    const auto image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(image);
    DialogOutcome outcome;
    runDialogDriver(*subject, driver, image, outcome, [&outcome]() { return !outcome.committed.empty(); });
    EXPECT_EQ(outcome.opened, 1);
    EXPECT_EQ(outcome.openedTitles, std::vector<std::string>{"Standalone Preview Result"});
    ASSERT_EQ(outcome.committed.size(), 1u);
    expectSamples(outcome.committed.front(), *image,
                  [](double value, auto, auto, auto) { return (1.0 + 0.5 * 0.35) * value - 0.5 * 0.12; });
}

// Closing the standalone preview template's dialog commits nothing and opens nothing.
TEST(ExtensionRegression, TheStandaloneTemplatesDialogClosedCommitsNothing)
{
    AN_REGRESSION_REQUIRES_RELEASE_PYSIDE();
    AN_REGRESSION_REQUIRES_FULL_HOST();
    const ExtensionLoadResult* subject = readySubject(Origin::Template, "standalone_preview_process");
    ASSERT_NE(subject, nullptr);
    const ScratchDirectory scratch("regression-standalone-template-close");
    const DialogDriver driver(*subject, scratch.path(),
                              "    def _open_dialog(self):\n"
                              "        super()._open_dialog()\n"
                              "        QtCore.QTimer.singleShot(0, self._drive)\n",
                              "    def _drive(self):\n"
                              "        if self._current_source() is None:\n"
                              "            QtCore.QTimer.singleShot(50, self._drive)\n"
                              "            return\n"
                              "        self._dialog.close()\n");
    const auto image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(image);
    DialogOutcome outcome;
    runDialogDriver(*subject, driver, image, outcome, []() { return false; }, 3000 * kIpcTimingMultiplier);
    EXPECT_TRUE(outcome.committed.empty());
    EXPECT_EQ(outcome.opened, 0);
}
