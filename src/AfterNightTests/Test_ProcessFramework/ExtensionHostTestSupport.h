// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the extension host cases share beside `ExtensionTestSupport.h`: the hosts, the bundled interpreter and its
/// runtime found beside the suite, the checkout's bundled extensions and author templates, environments made and
/// provisioned as the application makes them, a session pumped until its request lands and stopped however the case
/// ends, images of the suite's session handed to and taken back from shared buffers, and the answers the bundled
/// demos give. The host manager's cases and the extension regression share them.
#pragma once

#include "Test_ProcessFramework/ExtensionTestSupport.h"
#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"
#include "fixtures/TestImageGenerators.h"

#include "app/ExtensionCommon.h"
#include "app/ExtensionEnvironmentManager.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionManager.h"
#include "app/SharedBufferManager.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace an::tests::processes::framework
{

// The session the suite's images belong to: the suite context's.
[[nodiscard]] inline const an::consumer::EngineSession& testSession()
{
    return an::tests::processes::framework::suiteContext().engineSession();
}

inline std::filesystem::path uniqueSharedBufferRootForTest(const std::string& suffix)
{
    return std::filesystem::temp_directory_path() / ("an_ext_host_roundtrip_" + suffix);
}

// Removes a tree through the long-path prefix, retrying while a host process may still hold a file in it.
inline void removeAllNoThrow(const std::filesystem::path& path)
{
    (void)an::tests::processes::framework::removeTree(path);
}

inline std::filesystem::path extensionHostPathForCurrentAppDir()
{
    return an::ui::extensions::bundled_extension_host_executable_for_app_dir(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()));
}

inline std::filesystem::path extensionHostLitePathForCurrentAppDir()
{
    return an::ui::extensions::bundled_extension_host_lite_executable_for_app_dir(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()));
}

inline std::filesystem::path runtimeRootForCurrentAppDir()
{
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()) / "PythonLib";
}

inline std::filesystem::path runtimeSitePackagesPath(const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    return runtime_root / "Lib" / "site-packages";
#else
    return runtime_root / "lib" / "python3.14" / "site-packages";
#endif
}

inline std::filesystem::path environmentSitePackagesPath(const std::filesystem::path& environment_root)
{
#ifdef _WIN32
    return environment_root / "Lib" / "site-packages";
#else
    return environment_root / "lib" / "python3.14" / "site-packages";
#endif
}

inline std::filesystem::path bundledPythonPathForCurrentAppDir()
{
    return an::ui::extensions::bundled_python_executable_for_app_dir(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()));
}

inline std::filesystem::path findRepositoryBundledExtensionsRoot()
{
    std::error_code ec;
    auto root = std::filesystem::current_path(ec);
    if (ec)
    {
        return {};
    }

    while (!root.empty())
    {
        const auto rt_preview_manifest = root / "extensions" / "an_rt_preview_demo" / "extension.json";
        const auto custom_ui_manifest = root / "extensions" / "an_custom_ui_demo" / "extension.json";
        const auto standalone_preview_manifest = root / "extensions" / "an_standalone_preview_demo" / "extension.json";
        if (std::filesystem::exists(rt_preview_manifest) && std::filesystem::exists(custom_ui_manifest) &&
            std::filesystem::exists(standalone_preview_manifest))
        {
            return (root / "extensions").lexically_normal();
        }

        const auto parent = root.parent_path();
        if (parent == root)
        {
            break;
        }
        root = parent;
    }

    return {};
}

inline std::filesystem::path findRepositoryRootForTests()
{
    std::error_code ec;
    auto root = std::filesystem::current_path(ec);
    if (ec)
    {
        return {};
    }

    while (!root.empty())
    {
        const auto agents_file = root / "AGENTS.md";
        const auto manager_source = root / "src" / "AfterNightControls" / "app" / "ExtensionHostManager.cpp";
        if (std::filesystem::exists(agents_file) && std::filesystem::exists(manager_source))
        {
            return root;
        }

        const auto parent = root.parent_path();
        if (parent == root)
        {
            break;
        }
        root = parent;
    }

    return {};
}

inline double expectedRtPreviewDemoValue(const double source_value, const double strength, const double lift,
                                         const QString& mode, const bool preserve_background)
{
    double scale = 0.0;
    double offset = 0.0;

    if (mode == QStringLiteral("punchy"))
    {
        scale = 1.0 + strength * 1.2;
        offset = -strength * 0.5 + lift;
    }
    else if (mode == QStringLiteral("gentle"))
    {
        scale = 1.0 - strength * 0.4;
        offset = strength * 0.2 + lift;
    }
    else
    {
        scale = 1.0 + strength * 0.6;
        offset = -strength * 0.25 + lift;
    }

    if (preserve_background)
    {
        offset *= 0.5;
    }

    return source_value * scale + offset;
}

inline double expectedCustomUiDemoValue(const double source_value, const double strength, const double sigma,
                                        const int iterations, const QString& mode, const bool use_mask = true,
                                        const QString& quality_preset = QStringLiteral("balanced"))
{
    const int effective_passes = (mode == QStringLiteral("quality")) ? iterations : 1;
    double per_pass = (mode == QStringLiteral("quality")) ? strength * 0.15 : strength * 0.3;

    if (quality_preset == QStringLiteral("fast"))
    {
        per_pass *= 0.8;
    }
    else if (quality_preset == QStringLiteral("best"))
    {
        per_pass *= 1.2;
    }

    const double warmth = (sigma - 1.0) * 0.02;
    const double scale = 1.0 - per_pass;
    double offset = per_pass * 0.6 + warmth;
    if (use_mask)
    {
        offset *= 0.7;
    }

    double result = source_value;
    for (int i = 0; i < effective_passes; ++i)
    {
        result = result * scale + offset;
    }
    return result;
}

/// How long a compiler or script a case runs may take. QProcess waits 30 s by default, which a compile outlasts on a
/// machine a parallel build loads.
inline constexpr int kToolProcessTimeoutMs = 300000;

inline bool runProcessExpectSuccess(const std::filesystem::path& program, const QStringList& arguments,
                                    const std::filesystem::path& working_directory, std::string* output)
{
    QProcess process;
    process.setProgram(QString::fromStdString(program.string()));
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    if (!working_directory.empty())
    {
        process.setWorkingDirectory(QString::fromStdString(working_directory.string()));
    }

    process.start();
    if (!process.waitForStarted())
    {
        if (output)
        {
            *output = "Failed to start process: " + program.string();
        }
        return false;
    }
    if (!process.waitForFinished(kToolProcessTimeoutMs))
    {
        process.kill();
        process.waitForFinished();
    }

    const std::string merged_output = process.readAll().toStdString();
    if (output)
    {
        *output = merged_output;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

inline bool pumpUntilStateChanges(an::ui::extensions::ExtensionHostManager& manager,
                                  an::ui::extensions::ExtensionHostRunningSession* running_session,
                                  const int timeout_ms, std::string* error)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        if (!manager.pumpSession(running_session, 20, error))
        {
            return false;
        }
    }

    return true;
}

inline bool pumpUntilRequestCompletes(an::ui::extensions::ExtensionHostManager& manager,
                                      an::ui::extensions::ExtensionHostRunningSession* running_session,
                                      const int timeout_ms, std::string* error)
{
    const int initial_completed_request_count = running_session ? running_session->completed_request_count : 0;
    const std::string initial_request_kind = running_session ? running_session->in_flight_request_kind : std::string();

    // A request a host answers as it ends completes only once the host has exited.
    const int bound_ms = timeout_ms + hostExitAllowanceMs();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < bound_ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        if (!manager.pumpSession(running_session, 20, error))
        {
            return false;
        }
        if (running_session->completed_request_count > initial_completed_request_count &&
            running_session->in_flight_request_id.empty())
        {
            if (initial_request_kind == "execute")
            {
                return running_session->last_view_commit.has_value();
            }
            if (initial_request_kind == "preview")
            {
                return running_session->last_preview_result.has_value();
            }
            return true;
        }
    }

    if (error)
    {
        *error = "Timed out waiting for the request to complete";
    }
    return false;
}

inline std::filesystem::path uniqueExtensionRuntimeRootForTest(const std::string& suffix)
{
    return std::filesystem::temp_directory_path() / ("an_extension_runtime_test_" + suffix);
}

// RAII guard that ensures stopSession is always called, even when ASSERT_TRUE
// exits the test early.  Without this, a failed assertion leaks an orphaned
// AfterNightExtensionHost process whose named-pipe resources may not be fully
// released before subsequent tests start on Windows.
struct SessionGuard
{
    an::ui::extensions::ExtensionHostManager& manager;
    an::ui::extensions::ExtensionHostRunningSession& session;
    ~SessionGuard()
    {
        manager.stopSession(&session);
    }
};

inline bool provisionBundledRuntimePackageIntoEnvironment(const std::filesystem::path& runtime_root,
                                                          const std::filesystem::path& environment_root,
                                                          const std::string_view package_prefix)
{
    const auto source_site_packages = runtimeSitePackagesPath(runtime_root);
    const auto destination_site_packages = environmentSitePackagesPath(environment_root);
    std::error_code ec;
    std::filesystem::create_directories(destination_site_packages, ec);
    if (ec)
    {
        ADD_FAILURE() << "Failed to create env site-packages: " << ec.message();
        return false;
    }

    bool copied_any = false;
    for (const auto& entry : std::filesystem::directory_iterator(source_site_packages, ec))
    {
        if (ec)
        {
            ADD_FAILURE() << "Failed to enumerate bundled runtime site-packages: " << ec.message();
            return false;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(std::string(package_prefix), 0) != 0)
        {
            continue;
        }

        copied_any = true;
        const auto destination = destination_site_packages / entry.path().filename();
        removeAllNoThrow(destination);
        ec.clear();
        std::filesystem::copy(
            entry.path(), destination,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            ADD_FAILURE() << "Failed to copy bundled runtime package artifact from " << entry.path() << " to "
                          << destination << ": " << ec.message();
            return false;
        }
    }

    return copied_any;
}

// Imports a buffer as a new asset of the suite's session - the copy the application makes of an extension's result -
// reporting a refusal as text.
inline bool importBufferOrReport(const an::ui::extensions::SharedBufferManager& manager,
                                 const an::ui::extensions::SharedBufferDescriptor& descriptor,
                                 std::optional<an::consumer::ImageAsset>* out_image, std::string* error)
{
    an::Result<an::ui::extensions::ImportedImage> imported = manager.importImage(testSession(), descriptor);
    if (!imported.hasValue())
    {
        if (error)
        {
            *error = imported.status().message();
        }
        return false;
    }
    out_image->emplace(std::move(imported->asset));
    return true;
}

// Exports an asset to a new buffer under a manager's root, as the application lends an image to an extension,
// reporting a refusal as text.
inline bool exportImageOrReport(const an::ui::extensions::SharedBufferManager& manager,
                                const an::consumer::ImageAsset& image, const std::string_view session_id,
                                const an::ui::extensions::SharedBufferRole role,
                                an::ui::extensions::SharedImageBuffer* out_buffer, std::string* error,
                                const std::string_view buffer_id = {}, const std::string_view symbolic_name = {},
                                const an::ui::extensions::SharedBufferAccessMode access_mode =
                                    an::ui::extensions::SharedBufferAccessMode::ReadWrite)
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
    an::ui::extensions::ImageExportRequest request;
    request.sessionId = std::string(session_id);
    request.role = role;
    request.accessMode = access_mode;
    request.bufferId = std::string(buffer_id);
    request.symbolicName = std::string(symbolic_name);
    an::Result<an::ui::extensions::SharedImageBuffer> buffer = manager.exportImage(lease->image(), request);
    if (!buffer.hasValue())
    {
        if (error)
        {
            *error = buffer.status().message();
        }
        return false;
    }
    *out_buffer = std::move(*buffer);
    return true;
}

// The width, height and channel count of the image an asset holds; zero when it holds none.
[[nodiscard]] inline std::uint32_t widthOf(const an::consumer::ImageAsset& image)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    return descriptor.has_value() ? descriptor->extent().width() : 0u;
}

[[nodiscard]] inline std::uint32_t heightOf(const an::consumer::ImageAsset& image)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    return descriptor.has_value() ? descriptor->extent().height() : 0u;
}

[[nodiscard]] inline std::uint32_t channelsOf(const an::consumer::ImageAsset& image)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    return descriptor.has_value() ? descriptor->extent().channelCount() : 0u;
}

// Whether the image an asset holds is of a representation.
[[nodiscard]] inline bool hasSampleType(const an::consumer::ImageAsset& image, const an::SampleType type)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    return descriptor.has_value() && descriptor->sampleType() == type;
}

// The stored sample of an asset at a position and channel, as a double; NaN when it cannot be read.
template <typename X, typename Y, typename C>
[[nodiscard]] inline double sampleAt(const an::consumer::ImageAsset& image, const X x, const Y y, const C channel)
{
    return an::tests::processes::assetSample(image, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                             static_cast<std::uint32_t>(channel));
}

// A colour Float32 gradient of the suite's session: red rises along the rows, green down the columns, blue is a half;
// null when it cannot be made.
[[nodiscard]] inline std::shared_ptr<an::consumer::ImageAsset> makeRgbGradientImage(const std::uint32_t width,
                                                                                    const std::uint32_t height)
{
    an::Result<an::consumer::ImageAsset> made =
        an::tests::fixtures::createRgbGradientAsset(testSession(), width, height);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::make_shared<an::consumer::ImageAsset>(std::move(*made)) : nullptr;
}

// A grey Float32 image of the suite's session whose samples run from `base_value` by `step` in row-major order; null
// when it cannot be made. The name is the case's for the image and is not part of it.
inline std::shared_ptr<an::consumer::ImageAsset> makeMonoFloatImage(const std::string_view /*name*/,
                                                                    const uint32_t width, const uint32_t height,
                                                                    const float base_value, const float step)
{
    std::optional<an::consumer::ImageAsset> made = an::tests::processes::makeAsset<float>(
        testSession(), width, height, an::tests::processes::Channels::Grey,
        [width, base_value, step](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return base_value + step * static_cast<float>(y * width + x);
        });
    return made.has_value() ? std::make_shared<an::consumer::ImageAsset>(std::move(*made)) : nullptr;
}

// A grey UInt16 image of the suite's session whose codes run from `base_value` by `step` in row-major order; null
// when it cannot be made.
inline std::shared_ptr<an::consumer::ImageAsset> makeMonoUInt16Image(const std::string_view /*name*/,
                                                                     const uint32_t width, const uint32_t height,
                                                                     const std::uint16_t base_value,
                                                                     const std::uint16_t step)
{
    std::optional<an::consumer::ImageAsset> made = an::tests::processes::makeAsset<std::uint16_t>(
        testSession(), width, height, an::tests::processes::Channels::Grey,
        [width, base_value, step](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(base_value + step * static_cast<std::uint16_t>(y * width + x));
        });
    return made.has_value() ? std::make_shared<an::consumer::ImageAsset>(std::move(*made)) : nullptr;
}

inline an::ui::extensions::SharedImageBuffer
makeImageBuffer(const std::filesystem::path& root_path, const std::string& session_id,
                const std::shared_ptr<an::consumer::ImageAsset>& image, const an::ui::extensions::SharedBufferRole role,
                const an::ui::extensions::SharedBufferAccessMode access_mode, const std::string_view symbolic_name,
                std::string* error)
{
    if (!image)
    {
        if (error)
        {
            *error = "the image to export could not be made";
        }
        return {};
    }
    an::ui::extensions::SharedBufferManager manager(root_path);
    an::ui::extensions::SharedImageBuffer buffer;
    if (!exportImageOrReport(manager, *image, session_id, role, &buffer, error, {}, symbolic_name, access_mode))
    {
        return {};
    }

    return buffer;
}

// The author templates' root in the checkout, or empty.
inline std::filesystem::path authorTemplatesRoot()
{
    const std::filesystem::path root = findRepositoryRootForTests();
    return root.empty() ? std::filesystem::path() : (root / "docs" / "extension-examples").lexically_normal();
}

// Where a run makes the extension environments its cases provision through discovery: a short directory of the run's
// own under the temporary directory, removed once every case has run. An environment of a shared profile lies under a
// sixty-four-character fingerprint and a site-packages tree, and under a runner's long storage root most of its
// libraries' paths pass the length Windows loads a library from.
class ExtensionEnvironmentsRoot final : public ::testing::Environment
{
public:
    [[nodiscard]] const std::filesystem::path& root()
    {
        if (m_root.empty())
        {
            m_root = std::filesystem::path(QDir::tempPath().toStdWString()) /
                     ("anx-" + QUuid::createUuid().toString(QUuid::Id128).left(8).toStdString());
            std::filesystem::create_directories(m_root);
        }
        return m_root;
    }

    void TearDown() override
    {
        if (!m_root.empty())
        {
            static_cast<void>(removeTree(m_root));
            m_root.clear();
        }
    }

private:
    std::filesystem::path m_root;
};

inline ExtensionEnvironmentsRoot* const g_extensionEnvironmentsRoot =
    static_cast<ExtensionEnvironmentsRoot*>(::testing::AddGlobalTestEnvironment(new ExtensionEnvironmentsRoot()));

// The run's short environments root; see `ExtensionEnvironmentsRoot`.
[[nodiscard]] inline const std::filesystem::path& shortExtensionEnvironmentsRoot()
{
    return g_extensionEnvironmentsRoot->root();
}

// Discovers the packages under a root as the application discovers its bundled packages and prepares each one's
// environment as the application does - a shared host profile, or a locked wheelhouse installed from the package -
// and answers the discovery.
//
// `environments_root` names where the environments are made, or is empty for the application's own place under the
// storage root.
inline std::vector<an::ui::extensions::ExtensionLoadResult>
discoverBundledPackages(const std::filesystem::path& packages_root, const std::filesystem::path& host_path,
                        const std::filesystem::path& runtime_root, std::string* error,
                        const std::filesystem::path& environments_root = {})
{
    an::ui::extensions::ExtensionDiscoveryOptions options;
    if (!environments_root.empty())
    {
        options.context.environments_root = environments_root;
    }
    options.bundled_roots = {packages_root};
    options.context.runtime_target = an::ui::extensions::current_runtime_target();
    options.context.runtime_root = runtime_root;
    options.context.bundled_python_executable = bundledPythonPathForCurrentAppDir();
    options.context.extension_host_executable = host_path;
    options.context.extension_host_lite_executable = extensionHostLitePathForCurrentAppDir();
    options.context.workflow_launch_mode_available = true;
    options.include_development_only_extensions = true;

    an::ui::extensions::ExtensionManager extension_manager;
    std::vector<an::ui::extensions::ExtensionLoadResult> discovered = extension_manager.discoverExtensions(options);

    an::ui::extensions::ExtensionEnvironmentManager environment_manager;
    const int queued = environment_manager.queueBundledBootstrap(discovered, error);
    if (!error->empty())
    {
        return {};
    }
    if (queued > 0 && environment_manager.processQueuedBundledBootstrap(discovered, error) != queued)
    {
        return {};
    }
    return extension_manager.discoverExtensions(options);
}

// Discovers the author templates as `discoverBundledPackages` discovers a root, into the run's short environments
// root.
inline std::vector<an::ui::extensions::ExtensionLoadResult>
discoverAuthorTemplates(const std::filesystem::path& host_path, const std::filesystem::path& runtime_root,
                        std::string* error)
{
    return discoverBundledPackages(authorTemplatesRoot(), host_path, runtime_root, error,
                                   shortExtensionEnvironmentsRoot());
}

template <typename Predicate>
inline bool pumpCoordinatorUntil(an::ui::extensions::ExtensionHostLaunchCoordinator& coordinator, const int timeout_ms,
                                 Predicate&& predicate)
{
    // A session's readiness, its end and a schema read's landing each wait on a host that may be exiting.
    const int bound_ms = timeout_ms + hostExitAllowanceMs();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < bound_ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        coordinator.pumpSessions();
        if (predicate())
        {
            return true;
        }
        QThread::msleep(20);
    }

    return predicate();
}

// How long a launch may read its parameter schema before its session starts: the bound the coordinator gives the read.
inline constexpr int kParameterSchemaReadBoundMs =
    an::ui::extensions::ExtensionHostLaunchCoordinator::kDefaultParameterSchemaReadBoundMs;

// The coordinator a case launches through: the product's, whose parameter schema reads are given the exit allowance,
// because a read ends when its host exits.
class CaseLaunchCoordinator : public an::ui::extensions::ExtensionHostLaunchCoordinator
{
public:
    CaseLaunchCoordinator()
    {
        setParameterSchemaReadBoundMs(kParameterSchemaReadBoundMs + hostExitAllowanceMs());
    }
};

// The session is ready within timeout_ms of its start: a launch that reads its parameter schema first starts its
// session once the read lands, and the read, an import of the extension in a host of its own, is waited for apart.
inline bool pumpCoordinatorUntilNativeSessionReady(an::ui::extensions::ExtensionHostLaunchCoordinator& coordinator,
                                                   const std::string_view descriptor_id, const int timeout_ms)
{
    if (!pumpCoordinatorUntil(coordinator, kParameterSchemaReadBoundMs, [&coordinator, descriptor_id]() {
            return !coordinator.isReadingParameterSchemaFor(descriptor_id);
        }))
    {
        return false;
    }
    return pumpCoordinatorUntil(coordinator, timeout_ms, [&coordinator, descriptor_id]() {
        return coordinator.hasReadySessionForDescriptor(descriptor_id);
    });
}

inline bool stopCoordinatorAndWaitForSessionAbsence(an::ui::extensions::ExtensionHostLaunchCoordinator& coordinator,
                                                    const int timeout_ms)
{
    coordinator.stopAllSessions();
    return pumpCoordinatorUntil(coordinator, timeout_ms,
                                [&coordinator]() { return coordinator.activeSessionCount() == 0u; });
}

// A view showing a document of an image, as the application makes one, for a case whose session reads its source from
// the view; null when the image could not be made.
[[nodiscard]] inline an::tests::processes::ViewHandle
targetViewOf(const std::string& name, const std::shared_ptr<an::consumer::ImageAsset>& image,
             const std::string& file_name = {})
{
    if (!image)
    {
        ADD_FAILURE() << "the target view's image could not be made";
        return {};
    }
    an::Result<an::consumer::AssetReadLease> lease = image->read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return {};
    }
    an::Result<an::consumer::ImageAsset> copy = lease->snapshot();
    EXPECT_TRUE(copy.hasValue()) << copy.status().message();
    if (!copy.hasValue())
    {
        return {};
    }
    lease->release();
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*copy), an::ui::DocumentNames{QString::fromStdString(name), QString::fromStdString(file_name)});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return {};
    }
    an::tests::processes::ViewHandle view = an::tests::processes::makeTargetView(name, std::move(*document));
    EXPECT_TRUE(an::tests::processes::settleView(*view));
    return view;
}

} // namespace an::tests::processes::framework
