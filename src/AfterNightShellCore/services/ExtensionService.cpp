// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "services/ExtensionService.h"

#include "actions/ActionStateCoordinator.h"
#include "actions/ProcessMenuModel.h"
#include "core/ApplicationCore.h"
#include "core/IShell.h"
#include "services/DialogService.h"
#include "services/ProcessLaunchService.h"
#include "views/ViewService.h"

#include "app/ExtensionCommon.h"
#include "app/ExtensionEnvironmentManager.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionSdkService.h"
#include "app/FileCompanions.h"
#include "app/FileDialogUtils.h"
#include "app/ProcessRegistry.h"
#include "app/SharedBufferManager.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/SequenceSelection.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QCborMap>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <array>
#include <filesystem>
#include <optional>
#include <utility>

namespace an::ui::shell
{

namespace
{

using an::ui::extensions::ExtensionLoadResult;

std::string viewControlModeName(const ViewControlMode mode)
{
    switch (mode)
    {
    case ViewControlMode::standard:
        return "standard";
    case ViewControlMode::rt_preview:
        return "rt_preview";
    case ViewControlMode::st_preview:
        return "st_preview";
    }
    return "unknown";
}

std::string imageChannelViewModeName(const ImageChannelViewMode mode)
{
    switch (mode)
    {
    case ImageChannelViewMode::RGB_K_MODE:
        return "rgb";
    case ImageChannelViewMode::RGB_R_MODE:
        return "rgb_r";
    case ImageChannelViewMode::RGB_G_MODE:
        return "rgb_g";
    case ImageChannelViewMode::RGB_B_MODE:
        return "rgb_b";
    case ImageChannelViewMode::CIE_AB_MODE:
        return "cie_ab";
    case ImageChannelViewMode::CIE_L_MODE:
        return "cie_l";
    case ImageChannelViewMode::CIE_a_MODE:
        return "cie_a";
    case ImageChannelViewMode::CIE_b_MODE:
        return "cie_b";
    case ImageChannelViewMode::CIE_c_MODE:
        return "cie_c";
    case ImageChannelViewMode::CIE_h_MODE:
        return "cie_h";
    case ImageChannelViewMode::CIE_X_MODE:
        return "cie_x";
    case ImageChannelViewMode::CIE_Y_MODE:
        return "cie_y";
    case ImageChannelViewMode::CIE_Z_MODE:
        return "cie_z";
    case ImageChannelViewMode::HSL_H_MODE:
        return "hsl_h";
    case ImageChannelViewMode::HSL_S_MODE:
        return "hsl_s";
    case ImageChannelViewMode::HSL_L_MODE:
        return "hsl_l";
    case ImageChannelViewMode::HSV_H_MODE:
        return "hsv_h";
    case ImageChannelViewMode::HSV_S_MODE:
        return "hsv_s";
    case ImageChannelViewMode::HSV_V_MODE:
        return "hsv_v";
    case ImageChannelViewMode::HSI_H_MODE:
        return "hsi_h";
    case ImageChannelViewMode::HSI_S_MODE:
        return "hsi_s";
    case ImageChannelViewMode::HSI_I_MODE:
        return "hsi_i";
    case ImageChannelViewMode::YCBCR_Y_MODE:
        return "ycbcr_y";
    case ImageChannelViewMode::YCBCR_CB_MODE:
        return "ycbcr_cb";
    case ImageChannelViewMode::YCBCR_CR_MODE:
        return "ycbcr_cr";
    }
    return "unknown";
}

std::string imageDisplayModeName(const ImageViewMode mode)
{
    switch (mode)
    {
    case ImageViewMode::LINEAR_MODE:
        return "linear";
    case ImageViewMode::AUTOSTRETCH_MODE:
        return "autostretch";
    }
    return "unknown";
}

std::string maskRenderingModeName(const MaskRenderingMode mode)
{
    switch (mode)
    {
    case MaskRenderingMode::red_mode:
        return "red";
    case MaskRenderingMode::green_mode:
        return "green";
    case MaskRenderingMode::blue_mode:
        return "blue";
    case MaskRenderingMode::yellow_mode:
        return "yellow";
    case MaskRenderingMode::magenta_mode:
        return "magenta";
    case MaskRenderingMode::cyan_mode:
        return "cyan";
    case MaskRenderingMode::replace_mode:
        return "replace";
    case MaskRenderingMode::multiply_mode:
        return "multiply";
    }
    return "unknown";
}

QString colorTokenHex(const QColor& color)
{
    return color.name(QColor::HexArgb);
}

QCborMap fontTokenToCborMap(const QFont& font)
{
    return QCborMap{
        {QStringLiteral("family"), font.family()},       {QStringLiteral("point_size"), font.pointSizeF()},
        {QStringLiteral("weight"), font.weight()},       {QStringLiteral("italic"), font.italic()},
        {QStringLiteral("underline"), font.underline()}, {QStringLiteral("fixed_pitch"), font.fixedPitch()}};
}

QCborMap currentExtensionThemeTokens()
{
    const auto& tokenSet = an::ui::tokens();
    return QCborMap{{QStringLiteral("is_dark"), an::ui::isDarkTheme()},
                    {QStringLiteral("contrast_profile"), static_cast<int>(an::ui::themeContrastProfile())},
                    {QStringLiteral("accent_family"), static_cast<int>(an::ui::themeAccentFamily())},
                    {QStringLiteral("surface_tint_family"), static_cast<int>(an::ui::themeSurfaceTintFamily())},
                    {QStringLiteral("colors"),
                     QCborMap{{QStringLiteral("bgPrimary"), colorTokenHex(tokenSet.colors.bgPrimary)},
                              {QStringLiteral("bgSurface"), colorTokenHex(tokenSet.colors.bgSurface)},
                              {QStringLiteral("bgCard"), colorTokenHex(tokenSet.colors.bgCard)},
                              {QStringLiteral("textPrimary"), colorTokenHex(tokenSet.colors.textPrimary)},
                              {QStringLiteral("textSecondary"), colorTokenHex(tokenSet.colors.textSecondary)},
                              {QStringLiteral("textTertiary"), colorTokenHex(tokenSet.colors.textTertiary)},
                              {QStringLiteral("textOnAccent"), colorTokenHex(tokenSet.colors.textOnAccent)},
                              {QStringLiteral("accentPrimary"), colorTokenHex(tokenSet.colors.accentPrimary)},
                              {QStringLiteral("accentHover"), colorTokenHex(tokenSet.colors.accentHover)},
                              {QStringLiteral("borderPrimary"), colorTokenHex(tokenSet.colors.borderPrimary)},
                              {QStringLiteral("borderSubtle"), colorTokenHex(tokenSet.colors.borderSubtle)},
                              {QStringLiteral("statusSuccess"), colorTokenHex(tokenSet.colors.statusSuccess)},
                              {QStringLiteral("statusWarning"), colorTokenHex(tokenSet.colors.statusWarning)},
                              {QStringLiteral("statusError"), colorTokenHex(tokenSet.colors.statusError)},
                              {QStringLiteral("statusInfo"), colorTokenHex(tokenSet.colors.statusInfo)}}},
                    {QStringLiteral("spacing"), QCborMap{{QStringLiteral("xs"), tokenSet.spacing.xs},
                                                         {QStringLiteral("sm"), tokenSet.spacing.sm},
                                                         {QStringLiteral("md"), tokenSet.spacing.md},
                                                         {QStringLiteral("lg"), tokenSet.spacing.lg},
                                                         {QStringLiteral("xl"), tokenSet.spacing.xl}}},
                    {QStringLiteral("radius"), QCborMap{{QStringLiteral("none"), tokenSet.radius.none},
                                                        {QStringLiteral("sm"), tokenSet.radius.sm},
                                                        {QStringLiteral("md"), tokenSet.radius.md},
                                                        {QStringLiteral("lg"), tokenSet.radius.lg},
                                                        {QStringLiteral("xl"), tokenSet.radius.xl}}},
                    {QStringLiteral("typography"),
                     QCborMap{{QStringLiteral("heading1"), fontTokenToCborMap(tokenSet.typography.heading1)},
                              {QStringLiteral("heading2"), fontTokenToCborMap(tokenSet.typography.heading2)},
                              {QStringLiteral("heading3"), fontTokenToCborMap(tokenSet.typography.heading3)},
                              {QStringLiteral("body"), fontTokenToCborMap(tokenSet.typography.body)},
                              {QStringLiteral("caption"), fontTokenToCborMap(tokenSet.typography.caption)},
                              {QStringLiteral("monospace"), fontTokenToCborMap(tokenSet.typography.monospace)}}}};
}

an::ui::extensions::ExtensionThemeSnapshot currentExtensionThemeSnapshot()
{
    an::ui::extensions::ExtensionThemeSnapshot snapshot;
    snapshot.theme_name = an::ui::isDarkTheme() ? "AfterNight Dark" : "AfterNight Light";
    snapshot.tokens = currentExtensionThemeTokens();
    return snapshot;
}

/// Binds every process of the ready packages `discovered` to the extension host that runs it: the full host for a
/// package of the runtime backend, the Lite host for one of the protocol backend.
void bindDiscoveredExtensionLaunches(an::ui::extensions::ExtensionHostLaunchCoordinator& coordinator,
                                     const std::filesystem::path& appDir,
                                     const std::vector<ExtensionLoadResult>& discovered)
{
    auto& registry = an::ui::process_registry::ProcessRegistry::instance();
    const std::filesystem::path hostExecutable =
        an::ui::extensions::bundled_extension_host_executable_for_app_dir(appDir);
    const std::filesystem::path liteHostExecutable =
        an::ui::extensions::bundled_extension_host_lite_executable_for_app_dir(appDir);

    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status != an::ui::extensions::ExtensionLoadStatus::Ready ||
            !result.environment || result.environment->state != an::ui::extensions::ExtensionEnvironmentState::Ready)
        {
            continue;
        }

        const auto sdkBackend = result.manifest->sdk_backend;
        if (sdkBackend == an::ui::extensions::ExtensionSdkBackend::Rpc)
        {
            continue;
        }
        const bool protocolBackend = sdkBackend == an::ui::extensions::ExtensionSdkBackend::Protocol;
        const std::filesystem::path selectedHostExecutable = protocolBackend ? liteHostExecutable : hostExecutable;
        if (selectedHostExecutable.empty())
        {
            continue;
        }

        for (const auto& process : result.manifest->processes)
        {
            auto* descriptor = registry.findMutable(process.id);
            if (!descriptor)
            {
                continue;
            }

            an::ui::extensions::ExtensionHostLaunchBinding binding;
            binding.descriptor_id = process.id;
            binding.extension_id = result.manifest->id;
            binding.display_name = descriptor->display_name;
            binding.sdk_backend = protocolBackend ? "protocol" : "runtime";
            binding.launch_mode = result.manifest->launch_mode;
            binding.host_version = QCoreApplication::applicationVersion().isEmpty()
                                       ? "dev"
                                       : QCoreApplication::applicationVersion().toStdString();
            binding.host_arguments.push_back("--extension-package-root");
            binding.host_arguments.push_back(result.manifest->package_root);
            binding.host_arguments.push_back("--entry-point");
            binding.host_arguments.push_back(result.manifest->entry_point);
            binding.host_arguments.push_back("--process-class");
            binding.host_arguments.push_back(process.class_name);
            binding.host_arguments.push_back("--environment-root");
            binding.host_arguments.push_back(result.environment->environment_path.string());
            binding.host_arguments.push_back("--runtime-root");
            binding.host_arguments.push_back((appDir / "PythonLib").string());
            const bool workflowLaunch =
                result.manifest->launch_mode == an::ui::extensions::ExtensionLaunchMode::Workflow;
            if (process.capabilities.execute || workflowLaunch)
            {
                binding.capabilities.push_back("execute");
            }
            if (process.capabilities.preview)
            {
                binding.capabilities.push_back("preview");
            }
            if (process.capabilities.keep_open)
            {
                binding.capabilities.push_back("keep_open");
            }
            if (workflowLaunch)
            {
                binding.capabilities.push_back("workflow");
            }
            binding.process_capabilities = process.capabilities;
            binding.host_executable = selectedHostExecutable;
            binding.extension_host_owns_process_window = !protocolBackend;
            if (!protocolBackend && result.settings_schema &&
                result.settings_schema->state == an::ui::extensions::ExtensionSettingsSchemaState::Ready)
            {
                const auto paramsSchema = result.settings_schema->process_params_schemas.find(process.class_name);
                if (paramsSchema != result.settings_schema->process_params_schemas.end())
                {
                    binding.has_cached_native_param_schema = true;
                    binding.cached_native_param_schema = paramsSchema->second;
                }
            }
            if (workflowLaunch)
            {
                coordinator.bindWorkflowDescriptor(descriptor, binding);
            }
            else
            {
                coordinator.bindSingleImageDescriptor(descriptor, binding);
            }
        }
    }
}

void cleanupUnusedExtensionEnvironmentsAtStartup(const std::vector<ExtensionLoadResult>& discovered,
                                                 const an::ui::extensions::ExtensionDiscoveryOptions& discoveryOptions)
{
    an::ui::extensions::ExtensionEnvironmentManager environmentManager;

    // An environment a ready one of the same package's context replaced goes at once; one no installed package's
    // context names waits for the grace period.
    std::vector<std::filesystem::path> replacedPaths;
    std::uintmax_t replacedBytes = 0;
    if (environmentManager.removeReplacedEnvironments(discovered, discoveryOptions.context, &replacedPaths,
                                                      &replacedBytes, discoveryOptions.cancellation_token) &&
        !replacedPaths.empty() && an::platform::isLoggingInitialized())
    {
        LOG_INFO("Removed {} replaced extension environment(s) and reclaimed {} byte(s)", replacedPaths.size(),
                 replacedBytes);
    }
    if (discoveryOptions.cancellation_token.isCancellationRequested())
    {
        return;
    }

    std::vector<std::filesystem::path> removedPaths;
    std::uintmax_t reclaimedBytes = 0;
    std::string error;
    if (!environmentManager.removeUnusedEnvironments(
            discovered, discoveryOptions.context, an::ui::extensions::automatic_extension_environment_cleanup_policy(),
            &removedPaths, &reclaimedBytes, &error, discoveryOptions.cancellation_token))
    {
        if (!discoveryOptions.cancellation_token.isCancellationRequested() && an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Automatic extension environment cleanup failed: {}", error);
        }
        return;
    }

    if (!removedPaths.empty() && an::platform::isLoggingInitialized())
    {
        LOG_INFO("Automatic extension environment cleanup removed {} environment(s) and reclaimed {} byte(s)",
                 removedPaths.size(), reclaimedBytes);
    }
}

// A document around an asset an extension produced, shown under `name`; an empty name leaves the view's name to the
// view creation coordinator.
std::optional<an::ui::ImageDocument> documentOfAsset(an::consumer::ImageAsset&& asset, const std::string& name)
{
    an::ui::DocumentNames names;
    names.displayName = QString::fromStdString(name);
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(asset), std::move(names));
    if (!document.hasValue())
    {
        LOG_WARNING("An extension's image could not be shown: {}", document.status().message());
        return std::nullopt;
    }
    return std::move(*document);
}

// An image of a description with every sample zero, a new asset of the session.
an::Result<an::consumer::ImageAsset> zeroedImageOf(const an::consumer::EngineSession& session,
                                                   const an::ImageDescriptor& descriptor)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }

    an::Result<an::consumer::AnyImage> image =
        an::Status::failure(an::ErrorCode::Unsupported, "the image's samples are of no representation it can hold");
    (void)an::dispatchSampleType(descriptor.sampleType(), [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> created = an::Image<SampleT>::create(
            hold->engine(), descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
        image = created.hasValue() ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage(std::move(*created)))
                                   : an::Result<an::consumer::AnyImage>(created.status());
    });
    if (!image.hasValue())
    {
        return image.status();
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*image));
}

// Exports what a document holds to a new shared buffer, read under a lease taken without waiting: a document a worker
// is writing is refused rather than waited for on the GUI thread.
std::optional<an::ui::extensions::SharedBufferDescriptor>
exportDocumentSnapshot(const an::ui::ImageDocument& document, const an::ui::extensions::ImageExportRequest& request,
                       std::string* error)
{
    an::Result<an::consumer::AssetReadLease> lease = document.tryRead();
    if (!lease.hasValue())
    {
        if (error)
        {
            *error = lease.status().code() == an::ErrorCode::Busy
                         ? std::string("Requested view is being changed; ask for its snapshot again")
                         : std::string("Requested view has no exportable image: ") + lease.status().message();
        }
        return std::nullopt;
    }

    const an::ui::extensions::SharedBufferManager sharedBufferManager;
    an::Result<an::ui::extensions::SharedImageBuffer> buffer = sharedBufferManager.exportImage(lease->image(), request);
    if (!buffer.hasValue())
    {
        if (error)
        {
            *error = std::string("Requested view could not be exported: ") + buffer.status().message();
        }
        return std::nullopt;
    }
    return buffer->descriptor();
}

} // namespace

struct ExtensionService::Impl
{
    explicit Impl(ApplicationCore& owner) : core(owner) {}

    ApplicationCore& core;
    Hooks hooks;

    /// What the work coming back from the workers runs against: gone with the service, so that work is dropped.
    std::unique_ptr<QObject> guard = std::make_unique<QObject>();

    std::unique_ptr<an::ui::extensions::ExtensionHostLaunchCoordinator> launches =
        std::make_unique<an::ui::extensions::ExtensionHostLaunchCoordinator>();
    std::unique_ptr<QTimer> pumpTimer;
    bool pumpWakePending = false;
    std::unique_ptr<QFileSystemWatcher> installWatcher;
    std::unique_ptr<QTimer> installRefreshTimer;
    an::ui::extensions::ExtensionOperationCancellationToken cancellation;
    bool started = false;

    [[nodiscard]] QWidget* window() const
    {
        IShell* const shell = core.shell();
        return shell != nullptr ? shell->mainWindow() : nullptr;
    }

    /// The installed packages are as `discovered` says: registered, bound to their hosts and put in the menus.
    void registerAndShow(const std::filesystem::path& appDir, const std::vector<ExtensionLoadResult>& discovered,
                         const bool bindLaunches)
    {
        an::ui::extensions::ExtensionManager extensionManager;
        extensionManager.registerDiscoveredProcesses(discovered, an::ui::process_registry::ProcessRegistry::instance());
        if (bindLaunches)
        {
            bindDiscoveredExtensionLaunches(*launches, appDir, discovered);
        }
        core.processMenus().setExtensions(discovered);
        listsChanged();
    }

    /// The stored process lists drop what the registry no longer holds, and the actions follow the registry.
    void listsChanged()
    {
        core.processMenus().pruneMissingOnNextRefresh();
        core.processMenus().refreshStoredLists();
        core.actionState().updateActiveImageActions();
    }

    /// Opens a sequence a workflow wrote on a worker and shows it on the GUI thread, under the file's name; a sequence
    /// that cannot be read or shown is said and logged there. The session outlives the pool's work: the application
    /// drains the pool before it shuts the session down.
    void openWorkflowSequenceOutput(const an::consumer::EngineSession& session, const QString& path,
                                    const an::SequenceFormat format)
    {
        QPointer<QObject> alive(guard.get());
        QThreadPool::globalInstance()->start([this, alive, &session, path, format]() {
            auto opened = std::make_shared<an::Result<an::ui::ImageDocument>>(
                an::ui::openSequenceDocument(session, path, format, {}));
            QMetaObject::invokeMethod(
                qApp,
                [this, alive, opened, path]() {
                    if (!alive)
                    {
                        return;
                    }

                    const std::string pathText = path.toStdString();
                    if (!opened->hasValue())
                    {
                        LOG_WARNING("Failed to load workflow sequence output '{}': {}", pathText,
                                    opened->status().message());
                        core.dialogService().notifyErrorMessage(
                            window(), QStringLiteral("Workflow Output"),
                            QStringLiteral("Failed to load workflow sequence output: %1\n%2")
                                .arg(path, QString::fromUtf8(opened->status().message())));
                        return;
                    }

                    an::ui::ImageDocument document = std::move(**opened);
                    an::ui::DocumentNames names = document.names();
                    names.displayName = QFileInfo(path).completeBaseName();
                    names.sourcePath = path;
                    document.setNames(std::move(names));
                    an::ui::IViewFactory& factory = core.viewService();
                    ImageViewControl* const created = factory.createNewImageViewControlFromSequence(
                        nullptr, std::move(document), window(), false,
                        fmt::format("Workflow sequence loaded from '{}'", pathText), ViewControlMode::standard, nullptr,
                        false, false);
                    if (!created)
                    {
                        if (document.valid())
                        {
                            document.close();
                        }
                        LOG_WARNING("Failed to create a view for workflow sequence output '{}'", pathText);
                        core.dialogService().notifyErrorMessage(
                            window(), QStringLiteral("Workflow Output"),
                            QStringLiteral("Failed to create a view for workflow sequence output: %1").arg(path));
                        return;
                    }

                    created->fitToWindowAsync();
                },
                Qt::QueuedConnection);
        });
    }

    void discoverInstalled();
    void refreshInstalled();
};

ExtensionService::ExtensionService(ApplicationCore& core) : m_impl(std::make_unique<Impl>(core))
{
    Impl& impl = *m_impl;
    impl.launches->setManagedImageHooks(
        // An execute's result is an edit of the target view, recorded in its history before it is shown.
        [](ImageViewControl* control, an::consumer::ImageAsset&& image, const std::string& historyStepName,
           const std::string& historyStepDescription) {
            if (!control || !image.valid())
            {
                return;
            }

            auto result = std::make_shared<an::consumer::ImageAsset>(std::move(image));
            control->editImage(historyStepName, historyStepDescription,
                               [result](const an::consumer::EngineSession&, const an::consumer::AssetWriteLease&,
                                        std::stop_token) -> an::Result<an::consumer::ImageAsset> {
                                   if (!result->valid())
                                   {
                                       return an::Status::failure(an::ErrorCode::InvalidState,
                                                                  "the extension's result was already shown");
                                   }
                                   return std::move(*result);
                               });
        },
        // A preview lands in the preview view the session keeps, made beside the target the first time.
        [&impl](ImageViewControl* control, ImageViewControl* previewControl,
                an::consumer::ImageAsset&& image) -> ImageViewControl* {
            if (!control || !image.valid())
            {
                return previewControl;
            }

            if (previewControl)
            {
                previewControl->replaceImage(std::move(image), true);
                return previewControl;
            }

            std::optional<an::ui::ImageDocument> document = documentOfAsset(std::move(image), std::string());
            if (!document.has_value())
            {
                return nullptr;
            }
            an::ui::IViewFactory& factory = impl.core.viewService();
            ImageViewControl* const createdPreview =
                factory.createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, std::string(),
                                                           ViewControlMode::rt_preview, control, false, true);
            if (createdPreview)
            {
                createdPreview->fitToWindowAsync();
            }
            return createdPreview;
        },
        // The target's mask travels read-only as the selection; an execute also carries an output mask of the
        // target's shape, every sample zero - nothing masked - for the extension to write.
        [&impl](ImageViewControl* control,
                const bool forPreview) -> std::vector<an::ui::extensions::ManagedAuxiliaryImage> {
            std::vector<an::ui::extensions::ManagedAuxiliaryImage> auxiliaryImages;
            if (!control)
            {
                return auxiliaryImages;
            }

            ImageViewControl* const currentMask = control->getCurrentMask();
            if (currentMask)
            {
                if (std::shared_ptr<const an::consumer::ImageAsset> maskImage = currentMask->document().asset())
                {
                    auxiliaryImages.push_back({an::ui::extensions::SharedBufferRole::Mask,
                                               an::ui::extensions::SharedBufferAccessMode::ReadOnly, "selection",
                                               std::move(maskImage)});
                }
            }

            if (forPreview)
            {
                return auxiliaryImages;
            }

            std::optional<an::ImageDescriptor> templateDescriptor;
            if (currentMask)
            {
                templateDescriptor = currentMask->imageDescriptor();
            }
            if (!templateDescriptor.has_value())
            {
                templateDescriptor = control->imageDescriptor();
            }
            an::ui::IEngineSessionProvider* const provider = impl.core.engineSessionProvider();
            if (!templateDescriptor.has_value() || provider == nullptr)
            {
                return auxiliaryImages;
            }

            an::Result<an::consumer::ImageAsset> outputMask =
                zeroedImageOf(provider->engineSession(), *templateDescriptor);
            if (!outputMask.hasValue())
            {
                LOG_WARNING("The extension's output mask could not be made: {}", outputMask.status().message());
                return auxiliaryImages;
            }
            auxiliaryImages.push_back({an::ui::extensions::SharedBufferRole::Mask,
                                       an::ui::extensions::SharedBufferAccessMode::ReadWrite, "output_mask",
                                       std::make_shared<const an::consumer::ImageAsset>(std::move(*outputMask))});
            return auxiliaryImages;
        },
        // The masks an execute wrote open as views beside the target, and the first becomes its mask.
        [&impl](ImageViewControl* control, std::vector<an::ui::extensions::ManagedNamedImageResult>&& outputMasks) {
            if (!control || outputMasks.empty())
            {
                return;
            }

            ImageViewControl* firstMaskControl = nullptr;
            for (auto& outputMask : outputMasks)
            {
                if (!outputMask.image.valid())
                {
                    continue;
                }

                const std::string symbolicName =
                    outputMask.symbolic_name.empty() ? std::string("output_mask") : outputMask.symbolic_name;
                std::optional<an::ui::ImageDocument> document =
                    documentOfAsset(std::move(outputMask.image), symbolicName);
                if (!document.has_value())
                {
                    continue;
                }
                an::ui::IViewFactory& factory = impl.core.viewService();
                ImageViewControl* const createdControl = factory.createNewImageViewControlFromImage(
                    nullptr, std::move(*document), nullptr,
                    fmt::format("Extension mask '{}' created from '{}'", symbolicName, control->getUniqueName()),
                    ViewControlMode::standard, control, false, true);
                if (!firstMaskControl && createdControl)
                {
                    firstMaskControl = createdControl;
                }
            }

            if (firstMaskControl)
            {
                control->setCurrentMask(firstMaskControl);
                firstMaskControl->fitToWindowAsync();
            }
        },
        // A preview is computed over the selection, when the target's view has one.
        [](ImageViewControl* control) -> std::optional<QRect> {
            if (!control)
            {
                return std::nullopt;
            }

            auto* const viewWidget = control->getViewWidget();
            const std::optional<an::ImageDescriptor> descriptor = control->imageDescriptor();
            if (!viewWidget || !descriptor.has_value())
            {
                return std::nullopt;
            }

            return an::ui::extensions::normalizeManagedPreviewSubArea(
                viewWidget->isSelectionAreaEnabled(),
                QSize(static_cast<int>(descriptor->extent().width()), static_cast<int>(descriptor->extent().height())),
                viewWidget->getCurrentSelectionArea());
        },
        // A result an extension generates rather than applies opens as a new view beside the target.
        [&impl](ImageViewControl* control, an::consumer::ImageAsset&& image, const std::string& historyStepName,
                const std::string& historyStepDescription) -> ImageViewControl* {
            if (!control || !image.valid())
            {
                return nullptr;
            }

            const std::string historyDescription =
                historyStepDescription.empty() ? historyStepName : historyStepDescription;
            std::optional<an::ui::ImageDocument> document = documentOfAsset(std::move(image), historyStepName);
            if (!document.has_value())
            {
                return nullptr;
            }
            an::ui::IViewFactory& factory = impl.core.viewService();
            ImageViewControl* const createdControl =
                factory.createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, historyDescription,
                                                           ViewControlMode::standard, control, false, true);
            if (createdControl)
            {
                createdControl->fitToWindowAsync();
            }
            return createdControl;
        });
    impl.launches->setManagedViewNameProvider(
        [](ImageViewControl* control) { return control ? control->getUniqueName() : std::string(); });
    impl.launches->setSessionPumpWakeCallback([&impl]() {
        if (impl.pumpWakePending)
        {
            return;
        }

        impl.pumpWakePending = true;
        QTimer::singleShot(0, impl.guard.get(), [&impl]() {
            impl.pumpWakePending = false;
            impl.launches->pumpSessions();
        });
    });
    impl.pumpTimer = std::make_unique<QTimer>();
    impl.pumpTimer->setInterval(100);
    QObject::connect(impl.pumpTimer.get(), &QTimer::timeout, impl.guard.get(),
                     [&impl]() { impl.launches->pumpSessions(); });
    impl.pumpTimer->start();
}

ExtensionService::~ExtensionService()
{
    // The core's other services may be gone already: only what the service owns is stopped here.
    m_impl->cancellation.requestCancellation();
    stopSessions();
}

void ExtensionService::setHooks(Hooks hooks)
{
    m_impl->hooks = std::move(hooks);
}

void ExtensionService::start()
{
    Impl& impl = *m_impl;
    if (impl.started)
    {
        return;
    }
    impl.started = true;
    impl.discoverInstalled();

    impl.installRefreshTimer = std::make_unique<QTimer>();
    impl.installRefreshTimer->setSingleShot(true);
    impl.installRefreshTimer->setInterval(250);
    QObject::connect(impl.installRefreshTimer.get(), &QTimer::timeout, impl.guard.get(),
                     [&impl]() { impl.refreshInstalled(); });

    impl.installWatcher = std::make_unique<QFileSystemWatcher>();
    const std::array<std::filesystem::path, 2> watchRoots = {an::ui::extensions::repository_extensions_root(),
                                                             an::ui::extensions::user_extensions_root()};
    for (const auto& watchRoot : watchRoots)
    {
        std::error_code ec;
        std::filesystem::create_directories(watchRoot, ec);
        if (!ec)
        {
            impl.installWatcher->addPath(QString::fromStdString(watchRoot.string()));
        }
    }
    QObject::connect(impl.installWatcher.get(), &QFileSystemWatcher::directoryChanged, impl.guard.get(),
                     [&impl](const QString& path) {
                         if (!impl.installWatcher->directories().contains(path) && QFileInfo::exists(path))
                         {
                             impl.installWatcher->addPath(path);
                         }
                         impl.installRefreshTimer->start();
                     });
}

void ExtensionService::refreshSoon()
{
    if (m_impl->installRefreshTimer)
    {
        m_impl->installRefreshTimer->start();
    }
}

void ExtensionService::Impl::discoverInstalled()
{
    const auto discoveryOptions = defaultExtensionDiscoveryOptions(false, cancellation);
    const std::filesystem::path appDir(QCoreApplication::applicationDirPath().toStdString());
    GlobalSettingsPersistence* const settings = core.globalSettingsStore();
    const bool automaticEnvironmentCleanupEnabled =
        !settings ||
        settings->select(QString::fromLatin1(an::ui::extensions::kAutomaticExtensionEnvironmentCleanupSetting),
                         QStringLiteral("1")) != QStringLiteral("0");

    // The catalog is intentionally a small, guarded snapshot of only known-ready installations. It avoids recalculating
    // package dependency/source hashes on the UI thread before extension actions and launch bindings are available.
    const auto startupCatalog = an::ui::extensions::load_ready_extension_startup_catalog(discoveryOptions);
    if (!startupCatalog.empty())
    {
        registerAndShow(appDir, startupCatalog, true);
    }

    QPointer<QObject> alive(guard.get());
    QThreadPool::globalInstance()->start([this, alive, appDir, discoveryOptions, automaticEnvironmentCleanupEnabled,
                                          startupCatalogForCleanup = startupCatalog]() mutable {
        const auto cancellationRequested = [&discoveryOptions]() {
            return discoveryOptions.cancellation_token.isCancellationRequested();
        };
        an::ui::extensions::ExtensionManager extensionManager;
        auto discovered = extensionManager.discoverExtensions(discoveryOptions);
        if (cancellationRequested())
        {
            return;
        }
        if (automaticEnvironmentCleanupEnabled)
        {
            auto cleanupDiscovered = discovered;
            cleanupDiscovered.insert(cleanupDiscovered.end(), startupCatalogForCleanup.begin(),
                                     startupCatalogForCleanup.end());
            cleanupUnusedExtensionEnvironmentsAtStartup(cleanupDiscovered, discoveryOptions);
        }
        if (cancellationRequested())
        {
            return;
        }
        if (!an::ui::extensions::store_ready_extension_startup_catalog(discovered, discoveryOptions.context) &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to update the extension startup activation catalog");
        }
        if (discovered.empty())
        {
            if (alive)
            {
                QMetaObject::invokeMethod(
                    alive.data(),
                    [this, alive, appDir, discovered = std::move(discovered)]() mutable {
                        if (alive)
                        {
                            registerAndShow(appDir, discovered, false);
                        }
                    },
                    Qt::QueuedConnection);
            }
            return;
        }

        // Every installed package's environment that is not ready is prepared in the background, whatever its source:
        // refreshed in place for this build, taken over from an earlier version of the extension, or provisioned -
        // from an index only while the run may reach one.
        an::ui::extensions::ExtensionEnvironmentManager environmentManager;
        std::string bootstrapError;
        const int queuedBootstraps = environmentManager.queueEnvironmentMaintenance(
            discovered, !an::ui::extensions::extensionNetworkDisabledByPolicy(), &bootstrapError,
            discoveryOptions.cancellation_token);
        if (cancellationRequested())
        {
            return;
        }
        if (!bootstrapError.empty() && an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Extension environment maintenance queueing reported: {}", bootstrapError);
        }
        else if (queuedBootstraps > 0 && an::platform::isLoggingInitialized())
        {
            LOG_INFO("Queued {} extension environment maintenance operation(s)", queuedBootstraps);
        }

        if (alive)
        {
            auto discoveredForUi = discovered;
            QMetaObject::invokeMethod(
                alive.data(),
                [this, alive, appDir, discoveredForUi = std::move(discoveredForUi)]() mutable {
                    if (alive)
                    {
                        registerAndShow(appDir, discoveredForUi, true);
                    }
                },
                Qt::QueuedConnection);
        }

        bool requiresNativeParamCacheMigration = false;
        for (const auto& result : discovered)
        {
            if (cancellationRequested())
            {
                return;
            }
            if (result.status == an::ui::extensions::ExtensionLoadStatus::Ready && result.environment &&
                result.environment->state == an::ui::extensions::ExtensionEnvironmentState::Ready &&
                !hasCachedNativeParameterSchemas(result))
            {
                requiresNativeParamCacheMigration = true;
                break;
            }
        }

        if (requiresNativeParamCacheMigration)
        {
            const bool cachesUpdated = primeMissingNativeParameterSchemas(discovered, discoveryOptions);
            if (cancellationRequested())
            {
                return;
            }

            if (cachesUpdated && alive)
            {
                an::ui::extensions::ExtensionManager refreshedManager;
                auto refreshedDiscovered = refreshedManager.discoverExtensions(discoveryOptions);
                if (cancellationRequested())
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    alive.data(),
                    [this, alive, appDir, refreshedDiscovered = std::move(refreshedDiscovered)]() mutable {
                        if (alive)
                        {
                            bindDiscoveredExtensionLaunches(*launches, appDir, refreshedDiscovered);
                        }
                    },
                    Qt::QueuedConnection);
            }
        }

        if (queuedBootstraps <= 0)
        {
            return;
        }

        std::string workerError;
        const int processedBootstraps = environmentManager.processQueuedEnvironmentMaintenance(
            discovered, &workerError, discoveryOptions.cancellation_token);
        if (cancellationRequested())
        {
            return;
        }
        if (!workerError.empty())
        {
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Extension environment maintenance processing reported: {}", workerError);
            }
        }
        else if (processedBootstraps > 0 && an::platform::isLoggingInitialized())
        {
            LOG_INFO("Processed {} extension environment maintenance operation(s)", processedBootstraps);
        }

        if (!alive)
        {
            return;
        }

        an::ui::extensions::ExtensionManager refreshManager;
        auto refreshedDiscovered = refreshManager.discoverExtensions(discoveryOptions);
        if (cancellationRequested())
        {
            return;
        }
        // An environment the maintenance made ready has no native parameter schemas cached for what it is now.
        if (primeMissingNativeParameterSchemas(refreshedDiscovered, discoveryOptions))
        {
            if (cancellationRequested())
            {
                return;
            }
            refreshedDiscovered = refreshManager.discoverExtensions(discoveryOptions);
            if (cancellationRequested())
            {
                return;
            }
        }
        if (automaticEnvironmentCleanupEnabled)
        {
            auto cleanupDiscovered = refreshedDiscovered;
            cleanupDiscovered.insert(cleanupDiscovered.end(), startupCatalogForCleanup.begin(),
                                     startupCatalogForCleanup.end());
            cleanupUnusedExtensionEnvironmentsAtStartup(cleanupDiscovered, discoveryOptions);
        }
        if (cancellationRequested())
        {
            return;
        }
        if (!an::ui::extensions::store_ready_extension_startup_catalog(refreshedDiscovered, discoveryOptions.context) &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to refresh the extension startup activation catalog after bootstrap");
        }
        if (refreshedDiscovered.empty())
        {
            return;
        }

        QMetaObject::invokeMethod(
            alive.data(),
            [this, alive, appDir, refreshedDiscovered = std::move(refreshedDiscovered)]() mutable {
                if (!alive)
                {
                    return;
                }

                ProcessLaunchService::refreshRegisteredExtensionDescriptors(refreshedDiscovered);
                bindDiscoveredExtensionLaunches(*launches, appDir, refreshedDiscovered);
                core.processMenus().rebindExtensions(refreshedDiscovered);
                listsChanged();
            },
            Qt::QueuedConnection);
    });
}

void ExtensionService::Impl::refreshInstalled()
{
    an::ui::invalidateInstalledExtensionStatusCache(true);

    const auto discoveryOptions = defaultExtensionDiscoveryOptions(false, cancellation);
    const std::filesystem::path appDir(QCoreApplication::applicationDirPath().toStdString());
    QPointer<QObject> alive(guard.get());
    QThreadPool::globalInstance()->start([this, alive, appDir, discoveryOptions]() mutable {
        an::ui::extensions::ExtensionManager extensionManager;
        auto discovered = extensionManager.discoverExtensions(discoveryOptions);
        if (discoveryOptions.cancellation_token.isCancellationRequested())
        {
            return;
        }
        if (!an::ui::extensions::store_ready_extension_startup_catalog(discovered, discoveryOptions.context) &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to refresh the extension startup activation catalog after extension changes");
        }
        if (!alive)
        {
            return;
        }

        QMetaObject::invokeMethod(
            alive.data(),
            [this, alive, appDir, discovered = std::move(discovered)]() mutable {
                if (alive)
                {
                    registerAndShow(appDir, discovered, true);
                }
            },
            Qt::QueuedConnection);
    });
}

void ExtensionService::configureSdk()
{
    Impl& impl = *m_impl;
    an::ui::extensions::ExtensionSdkUiInteractionHandler interaction;
    interaction.show_error = [&impl](const std::string_view, const std::string_view, const QString& title,
                                     const QString& message, std::string*) -> bool {
        impl.core.dialogService().notifyErrorMessage(impl.window(), title, message);
        return true;
    };
    interaction.show_warning = [&impl](const std::string_view, const std::string_view, const QString& title,
                                       const QString& message, std::string*) -> bool {
        impl.core.dialogService().notifyWarnMessage(impl.window(), title, message);
        return true;
    };
    interaction.show_info = [&impl](const std::string_view, const std::string_view, const QString& title,
                                    const QString& message, std::string*) -> bool {
        impl.core.dialogService().notifyInfoMessage(impl.window(), title, message);
        return true;
    };
    interaction.confirm = [&impl](const std::string_view, const std::string_view, const QString& title,
                                  const QString& message, const bool warn,
                                  an::ui::extensions::ExtensionSdkUiConfirmCompletion completion,
                                  std::string* error) -> bool {
        if (!completion)
        {
            if (error)
            {
                *error = "Extension SDK confirmation completion is null";
            }
            return false;
        }

        if (!impl.hooks.askYesNo)
        {
            if (error)
            {
                *error = "Extension SDK confirmation dialog coordinator is unavailable";
            }
            return false;
        }

        impl.hooks.askYesNo(title, message, warn, [completion = std::move(completion)](const int response) mutable {
            completion(response == QMessageBox::Yes, {});
        });
        return true;
    };
    interaction.open_extension_settings =
        [&impl](const std::string_view, const std::string_view, const std::string_view extensionId,
                an::ui::extensions::ExtensionSdkOpenExtensionSettingsCompletion completion,
                std::string* error) -> bool {
        if (!completion)
        {
            if (error)
            {
                *error = "Extension SDK settings completion is null";
            }
            return false;
        }

        if (!impl.hooks.openExtensionSettings)
        {
            if (error)
            {
                *error = "Extension SDK settings dialog coordinator is unavailable";
            }
            return false;
        }

        const std::string extensionIdCopy(extensionId);
        QMetaObject::invokeMethod(
            impl.guard.get(),
            [&impl, extensionIdCopy, completion = std::move(completion)]() mutable {
                if (!impl.hooks.openExtensionSettings)
                {
                    completion("Extension SDK settings dialog coordinator is unavailable");
                    return;
                }

                QString diagnostic;
                if (!impl.hooks.openExtensionSettings(extensionIdCopy, &diagnostic))
                {
                    completion(diagnostic.trimmed().isEmpty() ? "Could not open extension settings."
                                                              : diagnostic.toStdString());
                    return;
                }

                completion({});
            },
            Qt::QueuedConnection);
        return true;
    };

    impl.launches->configureSdkService(
        impl.core.globalSettingsStore(),
        [&impl]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            std::vector<an::ui::extensions::ExtensionViewState> viewStates;
            ImageViewControlManager& manager = impl.core.viewService().manager();
            const auto controls = manager.getListOfImageViewControls();
            viewStates.reserve(controls.size());
            const ImageViewControl* activeView = manager.getActiveImageViewControl();
            for (ImageViewControl* control : controls)
            {
                if (!control)
                {
                    continue;
                }

                an::ui::extensions::ExtensionViewState viewState;
                const std::optional<an::ImageDescriptor> image = control->imageDescriptor();
                const bool hasSequence = control->document().sequence().has_value();
                const QPoint viewportOffset = control->getViewportOffset();
                viewState.view_name = control->getUniqueName();
                viewState.file_name = control->getFileName();
                viewState.selector_tooltip = an::ui::buildImageSelectorTooltip(control).toStdString();
                viewState.view_mode = viewControlModeName(control->getViewControlMode());
                viewState.image_display_mode = control->getViewWidget()
                                                   ? imageDisplayModeName(control->getViewWidget()->getViewMode())
                                                   : std::string();
                viewState.channel_view_mode =
                    control->getViewWidget() ? imageChannelViewModeName(control->getViewWidget()->getChannelViewMode())
                                             : std::string();
                viewState.mask_rendering_mode = maskRenderingModeName(control->getMaskRenderingMode());
                viewState.width = image.has_value() ? static_cast<int>(image->extent().width()) : 0;
                viewState.height = image.has_value() ? static_cast<int>(image->extent().height()) : 0;
                viewState.channels = image.has_value() ? static_cast<int>(image->extent().channelCount()) : 0;
                viewState.zoom_percent = control->getCurrentZoom();
                viewState.viewport_offset_x = viewportOffset.x();
                viewState.viewport_offset_y = viewportOffset.y();
                viewState.is_active = control == activeView;
                viewState.has_image = image.has_value();
                viewState.has_sequence = hasSequence;
                viewState.has_mask = control->isMasked();
                viewState.mask_enabled = control->isMaskEnabled();
                viewState.show_mask = control->isShowMaskEnabled();
                viewState.mask_inverted = control->isMaskInverted();
                viewState.has_alternate_image = control->hasAlternateImage();
                if (auto* viewWidget = control->getViewWidget())
                {
                    const QRect selectionArea = viewWidget->getCurrentSelectionArea();
                    viewState.selection_area_enabled = viewWidget->isSelectionAreaEnabled();
                    viewState.selection_area_x = selectionArea.x();
                    viewState.selection_area_y = selectionArea.y();
                    viewState.selection_area_width = selectionArea.width();
                    viewState.selection_area_height = selectionArea.height();
                }
                viewStates.push_back(std::move(viewState));
            }

            return viewStates;
        },
        [&impl](const std::string_view sessionId, const std::string_view requestId, const std::string_view viewName,
                an::ui::extensions::ExtensionViewSnapshot* outSnapshot, std::string* error) -> bool {
            if (!outSnapshot)
            {
                if (error)
                {
                    *error = "Extension SDK snapshot output is null";
                }
                return false;
            }

            ImageViewControl* control =
                impl.core.viewService().manager().getImageViewControlByName(std::string(viewName));
            if (!control)
            {
                if (error)
                {
                    *error = "Requested view no longer exists";
                }
                return false;
            }

            if (!control->hasImage())
            {
                if (error)
                {
                    *error = "Requested view has no exportable image";
                }
                return false;
            }

            *outSnapshot = an::ui::extensions::ExtensionViewSnapshot{};

            an::ui::extensions::ImageExportRequest request;
            request.sessionId = std::string(sessionId);
            request.role = an::ui::extensions::SharedBufferRole::Preview;
            request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
            request.bufferId = std::string(requestId) + ".view_snapshot";
            request.symbolicName = std::string(viewName) + ".snapshot";
            request.fileName = control->getFileName();
            std::optional<an::ui::extensions::SharedBufferDescriptor> source =
                exportDocumentSnapshot(control->document(), request, error);
            if (!source.has_value())
            {
                return false;
            }
            outSnapshot->source_buffer = std::move(*source);

            if (ImageViewControl* const currentMask = control->getCurrentMask())
            {
                if (currentMask->hasImage())
                {
                    an::ui::extensions::ImageExportRequest maskRequest;
                    maskRequest.sessionId = std::string(sessionId);
                    maskRequest.role = an::ui::extensions::SharedBufferRole::Mask;
                    maskRequest.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
                    maskRequest.bufferId = std::string(requestId) + ".view_snapshot.selection";
                    maskRequest.symbolicName = "selection";
                    std::optional<an::ui::extensions::SharedBufferDescriptor> mask =
                        exportDocumentSnapshot(currentMask->document(), maskRequest, error);
                    if (!mask.has_value())
                    {
                        return false;
                    }

                    outSnapshot->auxiliary_buffers.push_back(std::move(*mask));
                }
            }

            return true;
        },
        []() -> an::ui::extensions::ExtensionThemeSnapshot { return currentExtensionThemeSnapshot(); },
        std::move(interaction),
        [&impl](const std::string_view sessionId, const std::string_view viewName, std::string* error) -> bool {
            return impl.launches->bindSessionTargetView(sessionId, viewName, error);
        },
        [&impl](const std::string_view, const std::string_view,
                const an::ui::extensions::SharedBufferDescriptor& buffer, const QString& title,
                std::string* error) -> bool {
            an::ui::IEngineSessionProvider* const provider = impl.core.engineSessionProvider();
            if (provider == nullptr)
            {
                if (error)
                {
                    *error = "ui.open_image found no engine session to open the image in";
                }
                return false;
            }

            const an::ui::extensions::SharedBufferManager sharedBufferManager(
                an::ui::extensions::extensionHostManagedRootPath());
            an::Result<an::ui::extensions::ImportedImage> imported =
                sharedBufferManager.importImage(provider->engineSession(), buffer);
            if (!imported.hasValue())
            {
                if (error)
                {
                    *error = std::string("ui.open_image did not materialize an image: ") + imported.status().message();
                }
                return false;
            }

            std::optional<an::ui::ImageDocument> document = documentOfAsset(
                std::move(imported->asset), title.isEmpty() ? std::string("Extension Image") : title.toStdString());
            if (!document.has_value())
            {
                if (error)
                {
                    *error = "ui.open_image did not materialize an image";
                }
                return false;
            }

            an::ui::IViewFactory& factory = impl.core.viewService();
            ImageViewControl* const createdControl = factory.createNewImageViewControlFromImage(
                nullptr, std::move(*document), impl.window(), "Opened extension-generated image",
                ViewControlMode::standard, nullptr, false, true);
            if (!createdControl)
            {
                if (error)
                {
                    *error = "Failed to create an image view for the generated extension image";
                }
                return false;
            }

            return true;
        },
        // A sequence a workflow wrote is opened on a worker and shown once it is read; a sequence that cannot be read
        // or shown is said then, under the workflow's title, and logged.
        [&impl](const std::string_view, const std::vector<an::ui::extensions::ExtensionWorkflowOutput>& outputs,
                std::string* error) -> bool {
            for (const auto& output : outputs)
            {
                if (output.kind != "sequence")
                {
                    continue;
                }

                const QString outputPath = QString::fromStdWString(output.path.wstring());
                const std::string outputPathText = outputPath.toStdString();
                const std::optional<an::SequenceFormat> format = an::sequenceFormatForPath(outputPathText);
                if (!an::ui::dialogs::detail::isSupportedSequenceSuffix(QFileInfo(outputPath).suffix()) ||
                    !format.has_value())
                {
                    LOG_INFO("Workflow output '{}' is not a supported sequence file; leaving it on disk.",
                             outputPathText);
                    continue;
                }

                an::ui::IEngineSessionProvider* const provider = impl.core.engineSessionProvider();
                if (provider == nullptr)
                {
                    if (error)
                    {
                        *error = "Failed to open workflow sequence output: no engine session is open";
                    }
                    return false;
                }
                impl.openWorkflowSequenceOutput(provider->engineSession(), outputPath, *format);
            }

            return true;
        });
    // The settings store is open: the rate a native extension window previews at is read now, off the GUI thread, so
    // the first launch passes the stored one.
    impl.launches->refreshPreviewFrameRate();
}

std::size_t ExtensionService::activeSessionCount() const
{
    return m_impl->launches->activeSessionCount();
}

void ExtensionService::stopSessions()
{
    if (m_impl->pumpTimer)
    {
        m_impl->pumpTimer->stop();
    }
    m_impl->launches->stopAllSessions();
}

void ExtensionService::cancelBackgroundWork()
{
    m_impl->cancellation.requestCancellation();
    m_impl->core.processLaunch().cancelPreparations();
}

const an::ui::extensions::ExtensionOperationCancellationToken& ExtensionService::cancellationToken() const noexcept
{
    return m_impl->cancellation;
}

void ExtensionService::themeChanged()
{
    m_impl->launches->handleThemeChanged();
}

void ExtensionService::settingsApplied()
{
    m_impl->launches->refreshPreviewFrameRate();
}

void ExtensionService::viewCreated(const ImageViewControl* view)
{
    if (view)
    {
        const std::string viewName = view->getUniqueName();
        m_impl->launches->handleViewControlCreated(view, viewName, viewName, view->getFileName(),
                                                   an::ui::buildImageSelectorTooltip(view).toStdString());
    }
}

void ExtensionService::viewUpdated(const ImageViewControl* view)
{
    m_impl->launches->handleViewControlUpdated(view);
}

void ExtensionService::viewRenamed(const std::string& newName, const std::string& oldName, const ImageViewControl* view)
{
    m_impl->launches->handleViewControlRenamed(newName, oldName, view, view ? view->getFileName() : std::string(),
                                               view ? an::ui::buildImageSelectorTooltip(view).toStdString()
                                                    : std::string());
}

void ExtensionService::viewDestroyed(const ImageViewControl* view)
{
    if (view)
    {
        m_impl->launches->handleViewControlDestroyed(view, view->getUniqueName());
    }
}

void ExtensionService::extensionsPrepared(const std::vector<ExtensionLoadResult>& discovered)
{
    const std::filesystem::path appDir(QCoreApplication::applicationDirPath().toStdString());
    bindDiscoveredExtensionLaunches(*m_impl->launches, appDir, discovered);
    m_impl->core.processMenus().rebindExtensions(discovered);
    m_impl->listsChanged();
}

an::ui::extensions::ExtensionHostLaunchCoordinator& ExtensionService::launchCoordinator() noexcept
{
    return *m_impl->launches;
}

QString ExtensionService::statusPageTitle()
{
    return QCoreApplication::translate("ExtensionStatusPage", "Extensions");
}

QString ExtensionService::managerPageTitle()
{
    return QCoreApplication::translate("ExtensionManagerPage", "Extension Manager");
}

bool ExtensionService::isExtensionsPageTitle(const QString& pageName)
{
    const QString requested = pageName.trimmed();
    const QString browse = QCoreApplication::translate("ExtensionsDialog", "Browse");
    const QString installed = QCoreApplication::translate("ExtensionsDialog", "Installed");
    return QString::compare(requested, statusPageTitle(), Qt::CaseInsensitive) == 0 ||
           QString::compare(requested, managerPageTitle(), Qt::CaseInsensitive) == 0 ||
           QString::compare(requested, browse, Qt::CaseInsensitive) == 0 ||
           QString::compare(requested, installed, Qt::CaseInsensitive) == 0;
}

} // namespace an::ui::shell
