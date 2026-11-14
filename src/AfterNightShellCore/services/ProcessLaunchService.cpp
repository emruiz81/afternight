// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/ProcessLaunchService.h"

#include "core/ApplicationCore.h"
#include "core/IShell.h"
#include "services/DialogService.h"
#include "services/RecentProcessesModel.h"
#include "views/ViewService.h"

#include "app/ExtensionLaunchPreparations.h"
#include "app/ProcessRegistry.h"

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>

#include <algorithm>
#include <map>
#include <utility>

namespace an::ui::shell
{

namespace
{

using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::process_registry::ProcessAvailability;

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("ProcessLaunchService", text);
}

[[nodiscard]] ProcessAvailability refreshedExtensionProcessAvailability(const ExtensionLoadResult& result)
{
    if (result.status == ExtensionLoadStatus::Incompatible)
    {
        return ProcessAvailability::Incompatible;
    }
    if (!result.environment)
    {
        return ProcessAvailability::Unavailable;
    }

    switch (result.environment->state)
    {
    case ExtensionEnvironmentState::Ready:
        return ProcessAvailability::Available;
    case ExtensionEnvironmentState::Bootstrapping:
        return ProcessAvailability::Bootstrapping;
    case ExtensionEnvironmentState::Missing:
        return ProcessAvailability::InstallNeeded;
    case ExtensionEnvironmentState::Stale:
    case ExtensionEnvironmentState::Failed:
        return ProcessAvailability::RepairNeeded;
    }

    return ProcessAvailability::Unavailable;
}

[[nodiscard]] std::string refreshedExtensionAvailabilityReason(const ExtensionLoadResult& result)
{
    if (result.status == ExtensionLoadStatus::Ready && result.environment &&
        result.environment->state == ExtensionEnvironmentState::Ready)
    {
        return {};
    }

    if (!result.diagnostic.empty())
    {
        return result.diagnostic;
    }
    if (result.environment && !result.environment->diagnostic.empty())
    {
        return result.environment->diagnostic;
    }

    return "Extension process launch is not wired into this build yet.";
}

[[nodiscard]] QString extensionFamilyDisplayName(const an::ui::extensions::ExtensionManifest& manifest)
{
    if (!manifest.name.empty())
    {
        return QString::fromUtf8(manifest.name);
    }
    if (!manifest.id.empty())
    {
        return QString::fromUtf8(manifest.id);
    }
    return QCoreApplication::translate("AfterNightAppClass", "Extension");
}

} // namespace

an::ui::extensions::ExtensionDiscoveryOptions
defaultExtensionDiscoveryOptions(const bool eagerSettingsSchemaInspection,
                                 const an::ui::extensions::ExtensionOperationCancellationToken& cancellationToken)
{
    auto options = an::ui::extensions::default_extension_discovery_options();
    options.context.eager_settings_schema_inspection = eagerSettingsSchemaInspection;
    options.cancellation_token = cancellationToken;
    return options;
}

bool hasCachedNativeParameterSchemas(const ExtensionLoadResult& result)
{
    if (!result.manifest)
    {
        return false;
    }
    if (result.manifest->sdk_backend != an::ui::extensions::ExtensionSdkBackend::Runtime)
    {
        return true;
    }
    if (!result.settings_schema ||
        result.settings_schema->state != an::ui::extensions::ExtensionSettingsSchemaState::Ready)
    {
        return false;
    }

    return std::all_of(result.manifest->processes.begin(), result.manifest->processes.end(),
                       [&result](const an::ui::extensions::ExtensionProcessManifest& process) {
                           return result.settings_schema->process_params_schemas.contains(process.class_name);
                       });
}

bool primeMissingNativeParameterSchemas(const std::vector<ExtensionLoadResult>& discovered,
                                        const an::ui::extensions::ExtensionDiscoveryOptions& discoveryOptions)
{
    bool cachesUpdated = false;
    for (const auto& result : discovered)
    {
        if (discoveryOptions.cancellation_token.isCancellationRequested())
        {
            return cachesUpdated;
        }
        if (result.status != ExtensionLoadStatus::Ready || !result.environment ||
            result.environment->state != ExtensionEnvironmentState::Ready || hasCachedNativeParameterSchemas(result))
        {
            continue;
        }

        an::ui::extensions::ExtensionStartupCachePrimingOptions primingOptions;
        primingOptions.context = discoveryOptions.context;
        primingOptions.require_settings_schema = true;
        primingOptions.cancellation_token = discoveryOptions.cancellation_token;
        const auto primingResult = an::ui::extensions::prime_extension_startup_caches(result, primingOptions);
        cachesUpdated = cachesUpdated || primingResult.success;
        if (!primingResult.success && !discoveryOptions.cancellation_token.isCancellationRequested() &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to refresh native parameter cache extension id='{}': {}",
                        result.manifest ? result.manifest->id : std::string(), primingResult.diagnostic);
        }
    }
    return cachesUpdated;
}

ProcessLaunchService::ProcessLaunchService(ApplicationCore& core) : m_core(core) {}

ProcessLaunchService::~ProcessLaunchService() = default;

void ProcessLaunchService::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
}

an::ui::extensions::ExtensionLaunchPreparations& ProcessLaunchService::preparations()
{
    if (!m_preparations)
    {
        IShell* const shell = m_core.shell();
        m_preparations = std::make_unique<an::ui::extensions::ExtensionLaunchPreparations>(
            shell != nullptr ? shell->mainWindow() : nullptr,
            [](const std::string& extensionId,
               const an::ui::extensions::ExtensionLaunchPreparations::Progress& progress,
               const an::ui::extensions::ExtensionOperationCancellationToken& cancellation) {
                return an::ui::extensions::prepareExtensionEnvironmentForLaunch(
                    extensionId, defaultExtensionDiscoveryOptions(false), primeMissingNativeParameterSchemas, progress,
                    cancellation);
            },
            // The preparations stop with this service, so a completion still queued then finds nothing to complete.
            [this](const std::string& processId, const std::string& extensionId,
                   const an::ui::extensions::ExtensionLaunchPreparationOutcome& outcome) {
                completePreparedExtensionLaunch(processId, extensionId, outcome);
            });
    }
    return *m_preparations;
}

bool ProcessLaunchService::launch(const std::string& processId)
{
    using namespace an::ui::process_registry;

    const ProcessDescriptor* descriptor = ProcessRegistry::instance().find(processId);
    if (!descriptor)
    {
        LOG_WARNING("Registry descriptor not found for id={}", processId);
        return false;
    }

    const auto displayName = [&descriptor]() -> std::string {
        if (!descriptor->display_name.empty())
        {
            return descriptor->display_name;
        }
        if (!descriptor->process_name.empty())
        {
            return descriptor->process_name;
        }
        if (!descriptor->action_object_name.empty())
        {
            return descriptor->action_object_name;
        }
        return descriptor->id;
    }();

    const auto recordRecent = [this, &processId, &displayName]() {
        if (m_core.recentProcesses().record(processId, displayName) && m_hooks.recentProcessesChanged)
        {
            m_hooks.recentProcessesChanged();
        }
    };

    DialogService& dialogs = m_core.dialogService();
    const auto availability = evaluateProcessAvailability(*descriptor);
    if (!availability.ok())
    {
        // An extension whose environment is still to be installed, refreshed or repaired is prepared and then
        // launched, rather than refused with what to do in the extension manager.
        if (preparesBeforeLaunch(*descriptor))
        {
            preparations().launch(processId, descriptor->extension_id, displayName);
            return true;
        }
        dialogs.notifyErrorMessage(nullptr, QString::fromUtf8(availability.title.c_str()),
                                   QString::fromUtf8(availability.message.c_str()));
        return false;
    }

    if (descriptor->launch_mode == ProcessLaunchMode::TopLevel)
    {
        if (!descriptor->launch_top_level)
        {
            LOG_WARNING("Registry descriptor id={} is missing a top-level launch handler", processId);
            return false;
        }

        descriptor->launch_top_level();
        recordRecent();
        return true;
    }

    if (descriptor->launch_mode == ProcessLaunchMode::NonInteractive)
    {
        if (!descriptor->launch_non_interactive)
        {
            LOG_WARNING("Registry descriptor id={} is missing a non-interactive launch handler", processId);
            return false;
        }

        descriptor->launch_non_interactive();
        recordRecent();
        return true;
    }

    if (!descriptor->launch_single_image && !descriptor->launch_single_image_checked)
    {
        LOG_WARNING("Registry descriptor id={} is missing a single-image launch handler", processId);
        return false;
    }

    ImageViewControl* const control = m_core.viewService().subjectView();
    if (!control)
    {
        QString title = QString::fromUtf8(descriptor->process_name.c_str());
        if (title.isEmpty())
        {
            title = QString::fromUtf8(descriptor->display_name.c_str());
        }
        if (title.isEmpty())
        {
            title = QStringLiteral("Process");
        }

        dialogs.notifyErrorMessage(nullptr, title, QStringLiteral("No selected image to process"));
        return true;
    }

    // The guards read the view as the menu's action state and the launcher read it.
    const LaunchGuardInput launchGuardInput = launchGuardInputFor(*descriptor, control);
    const auto launchGuard = evaluateSingleImageLaunchGuards(*descriptor, launchGuardInput);
    if (!launchGuard.ok())
    {
        dialogs.notifyErrorMessage(nullptr, QString::fromUtf8(launchGuard.title.c_str()),
                                   QString::fromUtf8(launchGuard.message.c_str()));
        return false;
    }

    // Canonical duplicate/exclusive-access checks remain in ProcessLauncher,
    // where real process metadata (static process name / exclusivity) is available.
    if (descriptor->launch_single_image_checked)
    {
        std::string launchError;
        if (!descriptor->launch_single_image_checked(control, &launchError))
        {
            dialogs.notifyErrorMessage(nullptr, QString::fromUtf8(displayName.c_str()),
                                       QString::fromUtf8(launchError.c_str()));
            return false;
        }
    }
    else
    {
        descriptor->launch_single_image(control);
    }
    recordRecent();
    return true;
}

void ProcessLaunchService::completePreparedExtensionLaunch(
    const std::string& processId, const std::string& extensionId,
    const an::ui::extensions::ExtensionLaunchPreparationOutcome& outcome)
{
    refreshRegisteredExtensionDescriptors(outcome.discovered);
    if (m_hooks.extensionsPrepared)
    {
        m_hooks.extensionsPrepared(outcome.discovered);
    }

    const auto* descriptor = an::ui::process_registry::ProcessRegistry::instance().find(processId);
    if (outcome.prepared && descriptor && descriptor->is_available())
    {
        launch(processId);
        return;
    }

    // What still stands in the way is reported once, with where to deal with it, and not prepared again.
    QString message = QString::fromStdString(!outcome.diagnostic.empty()
                                                 ? outcome.diagnostic
                                                 : (descriptor ? descriptor->availability_reason : std::string()));
    if (message.isEmpty())
    {
        message = translated("The extension's environment could not be made ready.");
    }
    message += QStringLiteral("\n\n") + translated("Open Extensions > Manage Extensions... > Installed to install or "
                                                   "repair it.");
    m_core.dialogService().notifyErrorMessage(nullptr,
                                              QString::fromStdString(descriptor && !descriptor->display_name.empty()
                                                                         ? descriptor->display_name
                                                                         : extensionId),
                                              message);
}

void ProcessLaunchService::cancelPreparations()
{
    if (m_preparations)
    {
        m_preparations->cancelAll();
    }
}

void ProcessLaunchService::releasePreparations()
{
    m_preparations.reset();
}

void ProcessLaunchService::refreshRegisteredExtensionDescriptors(const std::vector<ExtensionLoadResult>& discovered)
{
    auto& registry = an::ui::process_registry::ProcessRegistry::instance();

    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status == ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        const auto availability = refreshedExtensionProcessAvailability(result);
        const std::string availabilityReason = refreshedExtensionAvailabilityReason(result);
        for (const auto& process : result.manifest->processes)
        {
            auto* descriptor = registry.findMutable(process.id);
            if (!descriptor)
            {
                continue;
            }

            descriptor->availability = availability;
            descriptor->availability_reason = availabilityReason;
            descriptor->source_channel_filter = process.capabilities.source_channel_filter;
            if (!descriptor->is_available())
            {
                descriptor->launch_single_image_checked = {};
                descriptor->launch_top_level = {};
            }
        }
    }
}

std::vector<ExtensionFamily> ProcessLaunchService::extensionFamilies(const std::vector<ExtensionLoadResult>& discovered)
{
    std::map<std::string, ExtensionFamily, std::less<>> familiesById;
    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status == ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        const auto& manifest = *result.manifest;
        if (manifest.id.empty())
        {
            continue;
        }

        auto& family = familiesById[manifest.id];
        family.displayName = extensionFamilyDisplayName(manifest);
        family.homepageUrl = manifest.repository_metadata.homepage_url;
        family.repositoryUrl = manifest.repository_metadata.repository_url;
        family.supportUrl = manifest.repository_metadata.support_url;
    }

    std::vector<ExtensionFamily> families;
    families.reserve(familiesById.size());
    for (auto& item : familiesById)
    {
        families.push_back(std::move(item.second));
    }

    std::sort(families.begin(), families.end(), [](const auto& lhs, const auto& rhs) {
        return QString::compare(lhs.displayName, rhs.displayName, Qt::CaseInsensitive) < 0;
    });
    return families;
}

} // namespace an::ui::shell
