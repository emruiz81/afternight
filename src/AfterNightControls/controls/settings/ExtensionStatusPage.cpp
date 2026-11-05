// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ExtensionStatusPage.h"

#include "PathPickerWidget.h"
#include "controls/settings/ExtensionSettingsSection.h"
#include "app/DesktopUtils.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionEnvironmentManager.h"
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "components/AnStatusBadge.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/CrashHandler.h>
#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <verdigris/wobjectimpl.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cctype>

W_OBJECT_IMPL(ExtensionStatusPage)

namespace
{

using an::ui::extensions::ExtensionDependencyContext;
using an::ui::extensions::ExtensionDependencySpec;
using an::ui::extensions::ExtensionDiscoveryOptions;
using an::ui::extensions::ExtensionEnvironmentSnapshot;
using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionLaunchMode;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::extensions::ExtensionManager;
using an::ui::extensions::ExtensionPackageRemoveRequest;
using an::ui::extensions::ExtensionPackageRepairRequest;
using an::ui::extensions::ExtensionPackageSignatureState;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionPackageTransactionService;
using an::ui::extensions::ExtensionRepositoryMetadata;
using an::ui::extensions::ExtensionRuntimeLaunchOutcome;
using an::ui::extensions::ExtensionRuntimeTerminationKind;
using an::ui::extensions::ExtensionSdkBackend;
using an::ui::extensions::ExtensionSettingsSchemaState;

void logExtensionStatusPageIssue(const char* operation, const QString& message, const bool error)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    const std::string detail = message.toStdString();
    if (error)
    {
        LOG_ERROR("Extension Status {}: {}", operation, detail);
        return;
    }

    LOG_WARNING("Extension Status {}: {}", operation, detail);
}

QString sanitizeObjectToken(const std::string_view value)
{
    QString sanitized;
    sanitized.reserve(static_cast<int>(value.size()));

    bool previous_was_separator = false;
    for (const char ch : value)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0)
        {
            sanitized.push_back(QChar::fromLatin1(static_cast<char>(std::tolower(uch))));
            previous_was_separator = false;
            continue;
        }

        if (!previous_was_separator)
        {
            sanitized.push_back(QChar::fromLatin1('_'));
            previous_was_separator = true;
        }
    }

    while (sanitized.endsWith(QChar::fromLatin1('_')))
    {
        sanitized.chop(1);
    }

    return sanitized.isEmpty() ? QStringLiteral("extension") : sanitized;
}

QString fieldObjectName(const std::string_view extension_id, const char* field_name)
{
    return QStringLiteral("anExtensionStatus_%1_%2")
        .arg(QString::fromLatin1(field_name), sanitizeObjectToken(extension_id));
}

QString buttonObjectName(const std::string_view extension_id, const char* button_name)
{
    return QStringLiteral("anExtensionStatusButton_%1_%2")
        .arg(QString::fromLatin1(button_name), sanitizeObjectToken(extension_id));
}

QString sourceTierText(const ExtensionPackageSource source)
{
    switch (source)
    {
    case ExtensionPackageSource::Bundled:
        return QObject::tr("Bundled");
    case ExtensionPackageSource::Repository:
        return QObject::tr("Repository");
    case ExtensionPackageSource::LocalPackage:
        return QObject::tr("Local package");
    case ExtensionPackageSource::User:
        return QObject::tr("User-installed");
    }

    return QObject::tr("Unknown");
}

QString signatureStateText(const ExtensionPackageSignatureState state)
{
    switch (state)
    {
    case ExtensionPackageSignatureState::Unknown:
        return QObject::tr("Unknown");
    case ExtensionPackageSignatureState::Unsigned:
        return QObject::tr("Unsigned");
    case ExtensionPackageSignatureState::Verified:
        return QObject::tr("Verified");
    case ExtensionPackageSignatureState::Failed:
        return QObject::tr("Failed");
    }

    return QObject::tr("Unknown");
}

QString sdkBackendText(const ExtensionSdkBackend backend)
{
    return QString::fromStdString(an::ui::extensions::extension_sdk_backend_id(backend));
}

QString hostModeText(const ExtensionSdkBackend backend)
{
    return QString::fromStdString(an::ui::extensions::extension_host_mode_label(backend));
}

QString hostModeDetailText(const ExtensionSdkBackend backend)
{
    return QString::fromStdString(an::ui::extensions::extension_host_mode_description(backend));
}

QString launchModeText(const ExtensionLaunchMode launch_mode)
{
    switch (launch_mode)
    {
    case ExtensionLaunchMode::SingleImage:
        return QStringLiteral("single_image");
    case ExtensionLaunchMode::Workflow:
        return QStringLiteral("workflow");
    }

    return QObject::tr("unknown");
}

QString manifestStateText(const ExtensionLoadResult& result)
{
    switch (result.status)
    {
    case ExtensionLoadStatus::Ready:
        return QObject::tr("Ready");
    case ExtensionLoadStatus::Incompatible:
        return QObject::tr("Incompatible");
    case ExtensionLoadStatus::Invalid:
        return QObject::tr("Invalid");
    }

    return QObject::tr("Unknown");
}

QString environmentStateText(const std::optional<ExtensionEnvironmentSnapshot>& environment)
{
    if (!environment.has_value())
    {
        return QObject::tr("Unavailable");
    }

    switch (environment->state)
    {
    case ExtensionEnvironmentState::Ready:
        return QObject::tr("Ready");
    case ExtensionEnvironmentState::Missing:
        return QObject::tr("Install Needed");
    case ExtensionEnvironmentState::Stale:
        return QObject::tr("Repair Needed");
    case ExtensionEnvironmentState::Failed:
        return QObject::tr("Validation Failed");
    case ExtensionEnvironmentState::Bootstrapping:
        return QObject::tr("Bootstrapping");
    }

    return QObject::tr("Unknown");
}

bool hasPackageManagerReceipt(const ExtensionLoadResult& result)
{
    if (result.provenance.receipt_path.empty())
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::is_regular_file(result.provenance.receipt_path, ec);
}

QString environmentStateText(const ExtensionLoadResult& result)
{
    return environmentStateText(result.environment);
}

QString availabilityStateText(const ExtensionLoadResult& result)
{
    if (result.status == ExtensionLoadStatus::Incompatible)
    {
        return QObject::tr("Incompatible");
    }
    if (!result.environment.has_value())
    {
        return QObject::tr("Unavailable");
    }

    switch (result.environment->state)
    {
    case ExtensionEnvironmentState::Ready:
        return QObject::tr("Available");
    case ExtensionEnvironmentState::Bootstrapping:
        return QObject::tr("Bootstrapping");
    case ExtensionEnvironmentState::Missing:
        return QObject::tr("Install Needed");
    case ExtensionEnvironmentState::Stale:
        return QObject::tr("Repair Needed");
    case ExtensionEnvironmentState::Failed:
        return QObject::tr("Validation Failed");
    }

    return QObject::tr("Unavailable");
}

QString processSummaryText(const ExtensionLoadResult& result)
{
    if (!result.manifest.has_value() || result.manifest->processes.empty())
    {
        return QObject::tr("No registered processes");
    }

    QStringList names;
    names.reserve(static_cast<int>(result.manifest->processes.size()));
    for (const auto& process : result.manifest->processes)
    {
        names.push_back(QString::fromStdString(process.display_name));
    }

    return names.join(QStringLiteral(", "));
}

QString validationResultText(const ExtensionLoadResult& result)
{
    if (!result.diagnostic.empty())
    {
        return QString::fromStdString(result.diagnostic);
    }
    if (result.environment.has_value() && !result.environment->diagnostic.empty())
    {
        return QString::fromStdString(result.environment->diagnostic);
    }
    if (result.status == ExtensionLoadStatus::Ready)
    {
        return QObject::tr("Manifest and environment validation passed.");
    }

    return QObject::tr("No validation result recorded.");
}

QString settingsSchemaStateText(const ExtensionLoadResult& result)
{
    if (!result.settings_schema.has_value())
    {
        return QObject::tr("Unavailable");
    }

    switch (result.settings_schema->state)
    {
    case ExtensionSettingsSchemaState::Ready:
        return result.settings_schema->from_cache ? QObject::tr("Ready (cached)") : QObject::tr("Ready");
    case ExtensionSettingsSchemaState::Failed:
        return QObject::tr("Failed");
    case ExtensionSettingsSchemaState::Unavailable:
    default:
        return QObject::tr("Unavailable");
    }
}

QString settingsSchemaDetailText(const ExtensionLoadResult& result)
{
    if (result.settings_schema.has_value() && !result.settings_schema->diagnostic.empty())
    {
        return QString::fromStdString(result.settings_schema->diagnostic);
    }

    if (!result.environment.has_value() || result.environment->state != ExtensionEnvironmentState::Ready)
    {
        return QObject::tr("Settings schema is discovered only after the extension environment reaches Ready state.");
    }

    return QObject::tr("No cached settings schema is available.");
}

QString bootstrapStateText(const ExtensionLoadResult& result)
{
    if (!result.environment.has_value())
    {
        return QObject::tr("Bootstrap state is unavailable.");
    }
    if (result.environment->state == ExtensionEnvironmentState::Bootstrapping)
    {
        return result.environment->diagnostic.empty()
                   ? QObject::tr("Bundled environment bootstrap is queued or in progress.")
                   : QString::fromStdString(result.environment->diagnostic);
    }
    if (result.source == ExtensionPackageSource::Bundled)
    {
        return QObject::tr("Bundled extension bootstrap is idle.");
    }
    return QObject::tr("Bootstrap is only used for bundled extensions.");
}

QString installRepairText(const ExtensionLoadResult& result)
{
    if (!result.environment.has_value())
    {
        return QObject::tr("No install or repair result is available.");
    }

    switch (result.environment->state)
    {
    case ExtensionEnvironmentState::Missing:
        return QObject::tr("Installation is required before this extension can launch.");
    case ExtensionEnvironmentState::Stale:
        return QObject::tr("Environment repair is required because the fingerprint is stale.");
    case ExtensionEnvironmentState::Failed:
        return result.environment->diagnostic.empty()
                   ? QObject::tr("The last environment validation or repair attempt failed.")
                   : QString::fromStdString(result.environment->diagnostic);
    case ExtensionEnvironmentState::Bootstrapping:
        return QObject::tr("Provisioning is in progress; the final install result is pending.");
    case ExtensionEnvironmentState::Ready:
        return QObject::tr("No install or repair action is currently required.");
    }

    return QObject::tr("No install or repair result is available.");
}

QString launchResultText(const ExtensionLoadResult& result)
{
    if (!result.runtime_status.has_value() || !result.runtime_status->has_launch_result)
    {
        return QObject::tr("No launch result has been recorded yet.");
    }

    const auto& runtime_status = *result.runtime_status;
    const QString detail =
        runtime_status.launch_detail.empty() ? QString() : QString::fromStdString(runtime_status.launch_detail);
    if (runtime_status.launch_outcome == ExtensionRuntimeLaunchOutcome::Succeeded)
    {
        return detail.isEmpty() ? QObject::tr("The latest launch succeeded.")
                                : QObject::tr("Succeeded: %1").arg(detail);
    }

    return detail.isEmpty() ? QObject::tr("The latest launch failed.") : QObject::tr("Failed: %1").arg(detail);
}

QString terminationKindText(const ExtensionRuntimeTerminationKind kind)
{
    switch (kind)
    {
    case ExtensionRuntimeTerminationKind::Crashed:
        return QObject::tr("Crashed");
    case ExtensionRuntimeTerminationKind::ForcedStop:
        return QObject::tr("Forced Stop");
    case ExtensionRuntimeTerminationKind::Disconnected:
        return QObject::tr("Disconnected");
    case ExtensionRuntimeTerminationKind::Unresponsive:
        return QObject::tr("Unresponsive");
    case ExtensionRuntimeTerminationKind::Failed:
        return QObject::tr("Failed");
    case ExtensionRuntimeTerminationKind::Exited:
        return QObject::tr("Exited");
    }

    return QObject::tr("Unknown");
}

QString terminationDiagnosticsText(const an::ui::extensions::ExtensionRuntimeStatusSnapshot& runtime_status)
{
    const auto& diagnostics = runtime_status.termination_diagnostics;
    QStringList fragments;
    if (!diagnostics.session_id.empty())
    {
        fragments.push_back(QObject::tr("session %1").arg(QString::fromStdString(diagnostics.session_id)));
    }
    if (diagnostics.process_exit_code >= 0)
    {
        fragments.push_back(QObject::tr("exit code %1").arg(diagnostics.process_exit_code));
    }
    if (diagnostics.last_heartbeat_elapsed_ms >= 0)
    {
        fragments.push_back(QObject::tr("last heartbeat %1 ms").arg(diagnostics.last_heartbeat_elapsed_ms));
    }
    if (!diagnostics.in_flight_request_id.empty())
    {
        fragments.push_back(QObject::tr("request %1").arg(QString::fromStdString(diagnostics.in_flight_request_id)));
    }
    if (!diagnostics.outstanding_buffer_ids.empty())
    {
        fragments.push_back(
            QObject::tr("%1 outstanding buffer(s)").arg(static_cast<int>(diagnostics.outstanding_buffer_ids.size())));
    }

    return fragments.join(QStringLiteral(", "));
}

QString crashResultText(const ExtensionLoadResult& result)
{
    if (!result.runtime_status.has_value() || !result.runtime_status->has_termination_result)
    {
        return QObject::tr("No crash or forced-termination history is recorded yet.");
    }

    const auto& runtime_status = *result.runtime_status;
    const QString kind_text = terminationKindText(runtime_status.termination_kind);
    const QString detail = runtime_status.termination_detail.empty()
                               ? QString()
                               : QString::fromStdString(runtime_status.termination_detail);
    const QString diagnostics = terminationDiagnosticsText(runtime_status);
    const QString summary = detail.isEmpty() ? kind_text : QObject::tr("%1: %2").arg(kind_text, detail);
    return diagnostics.isEmpty() ? summary : QObject::tr("%1 (%2)").arg(summary, diagnostics);
}

QString recoveryGuidanceText(const ExtensionLoadResult& result)
{
    if (result.runtime_status.has_value() && result.runtime_status->has_launch_result &&
        result.runtime_status->launch_outcome == ExtensionRuntimeLaunchOutcome::Failed)
    {
        return QObject::tr("Review the validation details and extension logs, repair the environment if needed, and "
                           "then relaunch the extension.");
    }

    if (!result.runtime_status.has_value() || !result.runtime_status->has_termination_result)
    {
        return {};
    }

    switch (result.runtime_status->termination_kind)
    {
    case ExtensionRuntimeTerminationKind::Crashed:
        return QObject::tr("The extension host crashed. Review the log directory and crash dumps, then relaunch. If it "
                           "repeats, rebuild or reinstall the extension environment.");
    case ExtensionRuntimeTerminationKind::ForcedStop:
        return QObject::tr(
            "The session was stopped before completion. Relaunch the extension when you are ready to continue.");
    case ExtensionRuntimeTerminationKind::Disconnected:
        return QObject::tr("The extension host lost its control channel. Relaunch the extension, then review logs and "
                           "crash dumps if the disconnect repeats.");
    case ExtensionRuntimeTerminationKind::Unresponsive:
        return QObject::tr("The extension host stopped responding and was terminated. Reduce workload or adjust "
                           "settings, then relaunch. Review logs and crash dumps if it happens again.");
    case ExtensionRuntimeTerminationKind::Failed:
        return QObject::tr("The extension session failed before completion. Review diagnostics and logs, then relaunch "
                           "or rebuild the extension environment.");
    case ExtensionRuntimeTerminationKind::Exited:
        return QObject::tr("The extension host exited unexpectedly. Relaunch it and inspect logs or crash dumps if no "
                           "explicit error was recorded.");
    }

    return {};
}

an::ui::AnInfoBanner::Tone recoveryGuidanceTone(const ExtensionLoadResult& result)
{
    if (!result.runtime_status.has_value() || !result.runtime_status->has_termination_result)
    {
        return an::ui::AnInfoBanner::Tone::Info;
    }

    return result.runtime_status->termination_kind == ExtensionRuntimeTerminationKind::ForcedStop
               ? an::ui::AnInfoBanner::Tone::Info
               : an::ui::AnInfoBanner::Tone::Warning;
}

QString environmentPathText(const std::optional<ExtensionEnvironmentSnapshot>& environment)
{
    if (!environment.has_value() || environment->environment_path.empty())
    {
        return QObject::tr("Not provisioned");
    }

    return QString::fromStdString(environment->environment_path.string());
}

QString manifestPathText(const ExtensionLoadResult& result)
{
    return result.manifest_path.empty() ? QObject::tr("Unavailable")
                                        : QString::fromStdString(result.manifest_path.string());
}

QString packageRootText(const ExtensionLoadResult& result)
{
    return result.package_root.empty() ? QObject::tr("Unavailable")
                                       : QString::fromStdString(result.package_root.string());
}

QString metadataText(const std::string& value)
{
    return value.empty() ? QObject::tr("Not declared") : QString::fromStdString(value);
}

QString listText(const std::vector<std::string>& values)
{
    if (values.empty())
    {
        return QObject::tr("Not declared");
    }

    QStringList items;
    items.reserve(static_cast<int>(values.size()));
    for (const std::string& value : values)
    {
        items.push_back(QString::fromStdString(value));
    }
    return items.join(QStringLiteral(", "));
}

QString packageHashText(const ExtensionLoadResult& result)
{
    if (result.manifest.has_value() && !result.manifest->repository_metadata.package_hash_sha256.empty())
    {
        return QString::fromStdString(result.manifest->repository_metadata.package_hash_sha256);
    }

    return QObject::tr("Not recorded");
}

QString hashText(const ExtensionLoadResult& result)
{
    if (!result.provenance.archive_hash_sha256.empty())
    {
        return QString::fromStdString(result.provenance.archive_hash_sha256);
    }
    if (result.manifest.has_value() && !result.manifest->repository_metadata.archive_hash_sha256.empty())
    {
        return QString::fromStdString(result.manifest->repository_metadata.archive_hash_sha256);
    }

    return QObject::tr("Not recorded");
}

QString signatureDetailText(const ExtensionLoadResult& result)
{
    if (!result.provenance.signature_detail.empty())
    {
        return QString::fromStdString(result.provenance.signature_detail);
    }
    if (result.manifest.has_value() && !result.manifest->repository_metadata.signature_detail.empty())
    {
        return QString::fromStdString(result.manifest->repository_metadata.signature_detail);
    }

    return QObject::tr("Not recorded");
}

QString repositoryAssetsText(const ExtensionRepositoryMetadata& metadata)
{
    QStringList fragments;
    if (!metadata.icon_path.empty())
    {
        fragments.push_back(QObject::tr("Icon: %1").arg(QString::fromStdString(metadata.icon_path)));
    }
    if (!metadata.screenshots.empty())
    {
        fragments.push_back(QObject::tr("Screenshots: %1").arg(listText(metadata.screenshots)));
    }

    return fragments.empty() ? QObject::tr("Not declared") : fragments.join(QStringLiteral(" | "));
}

QString dependencyContextText(const ExtensionDependencyContext context)
{
    switch (context)
    {
    case ExtensionDependencyContext::Private:
        return QObject::tr("Private");
    case ExtensionDependencyContext::SharedGroup:
        return QObject::tr("Shared group");
    case ExtensionDependencyContext::SharedHost:
        return QObject::tr("Shared host");
    }

    return QObject::tr("Unknown");
}

void appendListFragment(QStringList& fragments, const QString& label, const std::vector<std::string>& values)
{
    if (values.empty())
    {
        return;
    }

    fragments.push_back(QObject::tr("%1: %2").arg(label, listText(values)));
}

QString dependencyText(const ExtensionDependencySpec& dependencies)
{
    if (!dependencies.declared())
    {
        return QObject::tr("Not declared");
    }

    QStringList fragments;
    fragments.push_back(QObject::tr("Context: %1").arg(dependencyContextText(dependencies.dependency_context)));
    if (!dependencies.shared_group.empty())
    {
        fragments.push_back(QObject::tr("Shared group: %1").arg(QString::fromStdString(dependencies.shared_group)));
    }
    if (!dependencies.shared_host_profile.empty())
    {
        fragments.push_back(
            QObject::tr("Shared host profile: %1").arg(QString::fromStdString(dependencies.shared_host_profile)));
    }
    if (!dependencies.requirements_file.empty())
    {
        fragments.push_back(
            QObject::tr("Requirements: %1").arg(QString::fromStdString(dependencies.requirements_file)));
    }

    appendListFragment(fragments, QObject::tr("Find links"), dependencies.pip.find_links);
    appendListFragment(fragments, QObject::tr("Index URLs"), dependencies.pip.index_urls);
    appendListFragment(fragments, QObject::tr("Extra index URLs"), dependencies.pip.extra_index_urls);
    appendListFragment(fragments, QObject::tr("Bundled runtime packages"), dependencies.bundled_runtime_packages);
    if (dependencies.pip.require_hashes)
    {
        fragments.push_back(QObject::tr("pip require_hashes"));
    }
    if (dependencies.pip.no_deps)
    {
        fragments.push_back(QObject::tr("pip no_deps"));
    }

    return fragments.join(QStringLiteral(" | "));
}

QString packageProvenanceText(const ExtensionLoadResult& result)
{
    QStringList fragments;
    fragments.push_back(sourceTierText(result.source));
    if (!result.provenance.source_repository_url.empty())
    {
        fragments.push_back(
            QObject::tr("repo %1").arg(QString::fromStdString(result.provenance.source_repository_url)));
    }
    if (!result.provenance.selected_release_asset.empty())
    {
        fragments.push_back(
            QObject::tr("asset %1").arg(QString::fromStdString(result.provenance.selected_release_asset)));
    }
    if (!result.provenance.installed_at_utc.empty())
    {
        fragments.push_back(
            QObject::tr("installed %1").arg(QString::fromStdString(result.provenance.installed_at_utc)));
    }

    return fragments.join(QStringLiteral(" | "));
}

QString formatVersionLabel(const ExtensionLoadResult& result)
{
    if (!result.manifest.has_value())
    {
        return QObject::tr("Unknown version");
    }

    return QObject::tr("ID: %1 | Version: %2")
        .arg(QString::fromStdString(result.manifest->id), QString::fromStdString(result.manifest->version));
}

QString compactExtensionCardTitle(const ExtensionLoadResult& result)
{
    if (!result.manifest.has_value())
    {
        return QObject::tr("Extension");
    }

    QStringList fragments;
    fragments.push_back(QString::fromStdString(result.manifest->name));
    if (!result.manifest->version.empty())
    {
        fragments.push_back(QObject::tr("v%1").arg(QString::fromStdString(result.manifest->version)));
    }
    fragments.push_back(QObject::tr("Status: %1").arg(availabilityStateText(result)));

    return fragments.join(QStringLiteral(" | "));
}

QString normalizedStatusFieldName(const QString& label_text)
{
    QString field_name = label_text.trimmed();
    while (field_name.endsWith(QChar::fromLatin1(':')))
    {
        field_name.chop(1);
    }

    return field_name;
}

bool isUndeclaredPlaceholder(const QString& value_text)
{
    const QString trimmed = value_text.trimmed();
    return trimmed.compare(QObject::tr("Not declared"), Qt::CaseInsensitive) == 0 ||
           trimmed.compare(QObject::tr("Not recorded"), Qt::CaseInsensitive) == 0 ||
           trimmed.compare(QObject::tr("Unknown"), Qt::CaseInsensitive) == 0 ||
           trimmed.compare(QObject::tr("Unavailable"), Qt::CaseInsensitive) == 0;
}

QString availabilityValueTooltip(const QString& value_text)
{
    if (value_text == QObject::tr("Available"))
    {
        return QObject::tr("The extension package and its runtime environment are ready to launch.");
    }
    if (value_text == QObject::tr("Install Needed"))
    {
        return QObject::tr("AfterNight found the package, but the runtime environment has not been installed yet. Use "
                           "Manage Extensions to install or repair it.");
    }
    if (value_text == QObject::tr("Repair Needed"))
    {
        return QObject::tr("The package exists, but the runtime environment fingerprint is stale and should be "
                           "repaired before launch.");
    }
    if (value_text == QObject::tr("Validation Failed"))
    {
        return QObject::tr("AfterNight tried to validate the runtime environment, but the validation failed. Expand "
                           "the section for diagnostics.");
    }
    if (value_text == QObject::tr("Bootstrapping"))
    {
        return QObject::tr("AfterNight is currently preparing the runtime environment. Refresh this page to see the "
                           "latest state.");
    }
    if (value_text == QObject::tr("Incompatible"))
    {
        return QObject::tr("The manifest was discovered, but it is not compatible with this AfterNight runtime or "
                           "platform.");
    }
    if (value_text == QObject::tr("Unavailable"))
    {
        return QObject::tr("AfterNight cannot determine whether this extension can launch yet.");
    }

    return QObject::tr("Current availability value: %1.").arg(value_text);
}

QString environmentValueTooltip(const QString& value_text)
{
    if (value_text == QObject::tr("Ready"))
    {
        return QObject::tr("The runtime environment is present, validated, and ready for extension execution.");
    }
    if (value_text == QObject::tr("Install Needed"))
    {
        return QObject::tr("The runtime environment is missing and must be provisioned before this extension can run.");
    }
    if (value_text == QObject::tr("Repair Needed"))
    {
        return QObject::tr("The runtime environment exists, but its dependency fingerprint no longer matches the "
                           "manifest.");
    }
    if (value_text == QObject::tr("Validation Failed"))
    {
        return QObject::tr("The runtime environment exists, but validation failed. Expand the section to inspect the "
                           "recorded diagnostic.");
    }
    if (value_text == QObject::tr("Bootstrapping"))
    {
        return QObject::tr("The runtime environment is being installed or repaired in the background.");
    }
    if (value_text == QObject::tr("Unavailable"))
    {
        return QObject::tr("No runtime environment status was reported for this package.");
    }

    return QObject::tr("Current runtime environment value: %1.").arg(value_text);
}

QString statusValueTooltip(const QString& field_name, const QString& value_text)
{
    if (field_name == QObject::tr("Availability"))
    {
        return availabilityValueTooltip(value_text);
    }
    if (field_name == QObject::tr("Environment State"))
    {
        return environmentValueTooltip(value_text);
    }
    if (field_name == QObject::tr("Source Tier"))
    {
        return QObject::tr("Where AfterNight discovered this package: bundled with the app, installed from a "
                           "repository, installed from a local package, or user-installed.");
    }
    if (field_name == QObject::tr("Manifest State"))
    {
        return QObject::tr("Whether the manifest parsed successfully and is compatible with this build.");
    }
    if (field_name == QObject::tr("Host Mode"))
    {
        if (value_text == hostModeText(ExtensionSdkBackend::Runtime))
        {
            return hostModeDetailText(ExtensionSdkBackend::Runtime);
        }
        if (value_text == hostModeText(ExtensionSdkBackend::Protocol))
        {
            return hostModeDetailText(ExtensionSdkBackend::Protocol);
        }
        if (value_text == hostModeText(ExtensionSdkBackend::Rpc))
        {
            return hostModeDetailText(ExtensionSdkBackend::Rpc);
        }
    }
    if (field_name == QObject::tr("SDK Backend"))
    {
        return QObject::tr("The manifest backend switch that selects this package's extension host process and SDK "
                           "surface.");
    }
    if (field_name == QObject::tr("Settings Schema"))
    {
        return QObject::tr("Whether AfterNight has a cached settings schema that can be rendered in the Extension "
                           "Settings section.");
    }
    if (field_name == QObject::tr("Install / Repair"))
    {
        return QObject::tr("Summarizes the next environment maintenance action, if one is needed.");
    }
    if (field_name == QObject::tr("Bootstrap State"))
    {
        return QObject::tr("Shows bundled-extension provisioning progress or why bootstrapping is not active.");
    }
    if (field_name == QObject::tr("Validation Result"))
    {
        return QObject::tr("The latest manifest or runtime-environment validation message.");
    }
    if (field_name == QObject::tr("Last Launch"))
    {
        return QObject::tr("The most recent recorded launch outcome for this extension.");
    }
    if (field_name == QObject::tr("Last Crash / Termination"))
    {
        return QObject::tr(
            "The latest recorded crash, disconnect, forced stop, or unexpected exit for this extension.");
    }

    return QObject::tr("%1: %2").arg(field_name, value_text);
}

QString statusRowTooltip(const QString& label_text, const QString& value_text)
{
    const QString field_name = normalizedStatusFieldName(label_text);
    return QObject::tr("%1\nValue: %2\n%3").arg(field_name, value_text, statusValueTooltip(field_name, value_text));
}

QString compactExtensionCardTooltip(const ExtensionLoadResult& result)
{
    if (!result.manifest.has_value())
    {
        return {};
    }

    const QString availability = availabilityStateText(result);
    const QString environment = environmentStateText(result);
    QStringList lines;
    lines.push_back(QObject::tr("Extension: %1").arg(QString::fromStdString(result.manifest->name)));
    lines.push_back(QObject::tr("Host mode: %1").arg(hostModeText(result.manifest->sdk_backend)));
    lines.push_back(hostModeDetailText(result.manifest->sdk_backend));
    lines.push_back(QObject::tr("Availability: %1").arg(availability));
    lines.push_back(availabilityValueTooltip(availability));
    lines.push_back(QObject::tr("Runtime environment: %1").arg(environment));
    lines.push_back(environmentValueTooltip(environment));
    lines.push_back(QObject::tr("Expand to inspect package metadata, diagnostics, paths, and maintenance actions."));
    return lines.join(QChar::fromLatin1('\n'));
}

QString resolveLogDirectoryPath()
{
    an::platform::LoggingConfig config;
    an::platform::loadLoggingConfig(config);
    if (!config.logDirectory.empty())
    {
        return QString::fromStdString(config.logDirectory);
    }

    return QString::fromStdString(an::platform::LoggingConfig::defaultLogDirectory().string());
}

QString resolveCrashDumpDirectoryPath()
{
    std::filesystem::path database_path = an::platform::crashDatabasePath();
    if (database_path.empty())
    {
        const auto config = an::platform::defaultCrashHandlerConfig(
            std::filesystem::path(QCoreApplication::applicationFilePath().toStdString()));
        database_path = config.databasePath;
    }

    if (database_path.empty())
    {
        return {};
    }

    const std::filesystem::path pending_path = database_path / "pending";
    std::error_code ec;
    if (std::filesystem::exists(pending_path, ec))
    {
        return QString::fromStdString(pending_path.string());
    }

    return QString::fromStdString(database_path.string());
}

ExtensionDiscoveryOptions buildDefaultDiscoveryOptions()
{
    return an::ui::extensions::default_extension_discovery_options();
}

std::vector<ExtensionLoadResult> defaultDiscoveryResults()
{
    ExtensionManager manager;
    auto results = manager.discoverExtensions(buildDefaultDiscoveryOptions());
    results.erase(std::remove_if(results.begin(), results.end(),
                                 [](const ExtensionLoadResult& result) { return !result.manifest.has_value(); }),
                  results.end());

    std::sort(results.begin(), results.end(), [](const ExtensionLoadResult& lhs, const ExtensionLoadResult& rhs) {
        const QString lhs_name = lhs.manifest.has_value() ? QString::fromStdString(lhs.manifest->name) : QString();
        const QString rhs_name = rhs.manifest.has_value() ? QString::fromStdString(rhs.manifest->name) : QString();
        return lhs_name.localeAwareCompare(rhs_name) < 0;
    });
    return results;
}

struct InstalledExtensionStatusCache
{
    std::vector<ExtensionLoadResult> results;
    QPointer<QFutureWatcher<std::vector<ExtensionLoadResult>>> watcher;
    quint64 generation = 0;
    bool ready = false;
    bool stale = true;
};

InstalledExtensionStatusCache& installedExtensionStatusCache()
{
    static InstalledExtensionStatusCache cache;
    return cache;
}

bool cachedInstalledExtensionStatusResults(std::vector<ExtensionLoadResult>* outResults, const bool allowStale = false)
{
    const auto& cache = installedExtensionStatusCache();
    if (!cache.ready || (!allowStale && cache.stale))
    {
        return false;
    }

    if (outResults)
    {
        *outResults = cache.results;
    }
    return true;
}

QFutureWatcher<std::vector<ExtensionLoadResult>>* startInstalledExtensionStatusCacheRefresh(const bool forceRefresh)
{
    auto& cache = installedExtensionStatusCache();
    if (!forceRefresh && cache.ready && !cache.stale)
    {
        return nullptr;
    }
    if (cache.watcher && !forceRefresh)
    {
        return cache.watcher;
    }

    cache.stale = true;
    const quint64 generation = ++cache.generation;
    auto* parent = QCoreApplication::instance() ? static_cast<QObject*>(QCoreApplication::instance()) : nullptr;
    auto* watcher = new QFutureWatcher<std::vector<ExtensionLoadResult>>(parent);
    cache.watcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<std::vector<ExtensionLoadResult>>::finished, watcher,
                     [watcher, generation]() {
                         auto& finished_cache = installedExtensionStatusCache();
                         if (finished_cache.generation == generation)
                         {
                             finished_cache.results = watcher->result();
                             finished_cache.ready = true;
                             finished_cache.stale = false;
                         }
                         if (finished_cache.watcher == watcher)
                         {
                             finished_cache.watcher = nullptr;
                         }
                         watcher->deleteLater();
                     });
    watcher->setFuture(QtConcurrent::run([]() { return defaultDiscoveryResults(); }));
    return watcher;
}

bool canUninstallExtension(const ExtensionLoadResult& result)
{
    return result.manifest.has_value() && !result.package_root.empty();
}

bool canRepairExtensionEnvironment(const ExtensionLoadResult& result)
{
    if (!result.manifest.has_value() || result.source == ExtensionPackageSource::Bundled ||
        result.package_root.empty() || !hasPackageManagerReceipt(result) || !result.environment.has_value())
    {
        return false;
    }
    switch (result.environment->state)
    {
    case ExtensionEnvironmentState::Missing:
    case ExtensionEnvironmentState::Stale:
    case ExtensionEnvironmentState::Failed:
        return true;
    case ExtensionEnvironmentState::Ready:
    case ExtensionEnvironmentState::Bootstrapping:
        return false;
    }

    return false;
}

QString environmentRepairActionText(const ExtensionLoadResult& result)
{
    if (result.environment.has_value() && result.environment->state == ExtensionEnvironmentState::Missing)
    {
        return QObject::tr("Install Environment");
    }

    return QObject::tr("Repair Environment");
}

an::ui::ExtensionStatusPageUninstallResult defaultUninstallProvider(const ExtensionLoadResult& result)
{
    an::ui::ExtensionStatusPageUninstallResult page_result;
    if (!result.manifest.has_value() || result.manifest->id.empty())
    {
        page_result.diagnostic = QObject::tr("Extension id is required for uninstall.");
        return page_result;
    }
    if (result.package_root.empty())
    {
        page_result.diagnostic = QObject::tr("Extension package root is unavailable.");
        return page_result;
    }

    ExtensionPackageRemoveRequest request;
    request.install_root = result.package_root.parent_path();
    request.extension_id = result.manifest->id;
    request.require_existing = true;

    ExtensionPackageTransactionService service;
    const auto transaction_result = service.removeInstalledPackage(request);
    page_result.success = transaction_result.success;
    page_result.diagnostic = QString::fromStdString(transaction_result.diagnostic);
    return page_result;
}

an::ui::ExtensionStatusPageEnvironmentRepairResult defaultEnvironmentRepairProvider(const ExtensionLoadResult& result)
{
    an::ui::ExtensionStatusPageEnvironmentRepairResult page_result;
    if (!result.manifest.has_value() || result.manifest->id.empty())
    {
        page_result.diagnostic = QObject::tr("Extension id is required for environment repair.");
        return page_result;
    }
    if (!canRepairExtensionEnvironment(result))
    {
        page_result.diagnostic = QObject::tr("This extension does not currently need environment repair.");
        return page_result;
    }

    ExtensionPackageRepairRequest request;
    request.install_root = result.package_root.parent_path();
    request.extension_id = result.manifest->id;
    request.context = buildDefaultDiscoveryOptions().context;
    request.source = result.source;
    request.require_repository_metadata = result.source == ExtensionPackageSource::Repository;
    request.require_current_runtime_compatibility = true;
    request.provision_environment = true;
    request.prime_startup_caches = true;
    request.require_settings_schema_cache = true;
    request.precompile_python_bytecode = true;
    request.require_existing = true;

    ExtensionPackageTransactionService service;
    page_result.transaction = service.repairInstalledPackage(request);
    page_result.success = page_result.transaction.success;
    page_result.cancelled = page_result.transaction.cancelled;
    if (!page_result.success)
    {
        page_result.diagnostic = QString::fromStdString(page_result.transaction.diagnostic);
        return page_result;
    }

    page_result.diagnostic =
        QObject::tr("%1 for %2.")
            .arg(result.environment.has_value() && result.environment->state == ExtensionEnvironmentState::Missing
                     ? QObject::tr("Installed environment")
                     : QObject::tr("Repaired environment"),
                 QString::fromStdString(result.manifest->name.empty() ? result.manifest->id : result.manifest->name));
    return page_result;
}

void addStatusRow(QGridLayout* layout, const int row, const int col, const QString& label_text,
                  const QString& value_text, const QString& object_name, const bool selectable = false)
{
    if (!layout)
    {
        return;
    }
    if (isUndeclaredPlaceholder(value_text))
    {
        return;
    }

    const QString tooltip = statusRowTooltip(label_text, value_text);

    auto* label = new QLabel(label_text, layout->parentWidget());
    label->setWordWrap(true);
    label->setProperty("anStatusTone", QStringLiteral("secondary"));
    label->setToolTip(tooltip);
    layout->addWidget(label, row, col * 2, Qt::AlignTop);

    QString display_text = value_text;
    // Inject zero-width spaces after slashes and hyphens to allow long paths/hashes to word-wrap natively.
    const QString wrap_hint(QChar(0x200b));
    display_text.replace(QLatin1Char('/'), QString(QLatin1Char('/')) + wrap_hint);
    display_text.replace(QLatin1Char('\\'), QString(QLatin1Char('\\')) + wrap_hint);
    display_text.replace(QLatin1Char('-'), QString(QLatin1Char('-')) + wrap_hint);

    auto* value = new QLabel(display_text, layout->parentWidget());
    value->setObjectName(object_name);
    value->setWordWrap(true);
    value->setToolTip(tooltip);
    if (selectable)
    {
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    layout->addWidget(value, row, col * 2 + 1);
}

} // anonymous namespace

namespace an::ui
{

void warmInstalledExtensionStatusCache()
{
    (void)startInstalledExtensionStatusCacheRefresh(false);
}

void invalidateInstalledExtensionStatusCache(const bool refreshInBackground)
{
    auto& cache = installedExtensionStatusCache();
    cache.stale = true;
    if (refreshInBackground)
    {
        (void)startInstalledExtensionStatusCacheRefresh(true);
    }
}

} // namespace an::ui

ExtensionStatusPage::ExtensionStatusPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    (void)m_Settings;
    setupUi();
}

QString ExtensionStatusPage::pageTitle() const
{
    return tr("Extensions");
}

QIcon ExtensionStatusPage::pageIcon() const
{
    return contextIcon(QStringLiteral("computer"));
}

QStringList ExtensionStatusPage::searchKeywords() const
{
    return {tr("Extensions"),
            tr("Extension Status"),
            tr("Shared Extension Settings"),
            tr("Extension Settings"),
            tr("Environment"),
            tr("Bootstrap"),
            tr("Runtime"),
            tr("Manifest"),
            tr("Python"),
            tr("Bundled"),
            tr("User-installed"),
            tr("Configure"),
            tr("Schema"),
            tr("Crash"),
            tr("Recovery")};
}

void ExtensionStatusPage::loadSettings()
{
    refreshStatus(false);
    clearModified();
}

void ExtensionStatusPage::saveSettings()
{
    clearModified();
}

void ExtensionStatusPage::resetToDefaults()
{
    refreshStatus(true);
    clearModified();
}

void ExtensionStatusPage::refreshStyles()
{
    SettingsPage::refreshStyles();

    if (m_SummaryBanner)
    {
        m_SummaryBanner->refreshTheme();
    }
    for (auto* card : m_Cards)
    {
        if (card)
        {
            card->refreshTheme();
        }
    }
}

void ExtensionStatusPage::setDiscoveryProvider(DiscoveryProvider discoveryProvider)
{
    m_DiscoveryProvider = std::move(discoveryProvider);
}

void ExtensionStatusPage::setUninstallConfirmationProvider(UninstallConfirmationProvider confirmationProvider)
{
    m_UninstallConfirmationProvider = std::move(confirmationProvider);
}

void ExtensionStatusPage::setUninstallProvider(UninstallProvider uninstallProvider)
{
    m_UninstallProvider = std::move(uninstallProvider);
}

void ExtensionStatusPage::setEnvironmentRepairProvider(EnvironmentRepairProvider repairProvider)
{
    m_EnvironmentRepairProvider = std::move(repairProvider);
}

void ExtensionStatusPage::refreshStatus(const bool forceRefresh)
{
    reloadStatus(forceRefresh);
}

void ExtensionStatusPage::setupUi()
{
    const auto& t = an::ui::tokens();
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(t.spacing.lg);
    main_layout->setContentsMargins(0, 0, 0, 0);

    m_SummaryBanner = new an::ui::AnInfoBanner({}, an::ui::AnInfoBanner::Tone::Info, this);
    m_SummaryBanner->setObjectName(QStringLiteral("anExtensionStatusSummaryBanner"));
    main_layout->addWidget(m_SummaryBanner);

    auto* actions_layout = new QHBoxLayout();
    actions_layout->setContentsMargins(0, 0, 0, 0);
    actions_layout->setSpacing(t.spacing.sm);

    m_RefreshButton = new QPushButton(tr("Refresh Status"), this);
    m_RefreshButton->setObjectName(QStringLiteral("anExtensionStatusRefreshButton"));
    m_RefreshButton->setToolTip(
        tr("Run extension discovery again and rebuild the installed status report from the current manifests, "
           "runtime environment records, cached settings schemas, and recent launch diagnostics. This refreshes "
           "the view only; it does not install, repair, update, or uninstall any extension."));
    QObject::connect(m_RefreshButton, &QPushButton::clicked, this, [this]() { refreshStatus(true); });
    actions_layout->addWidget(m_RefreshButton);
    actions_layout->addStretch(1);

    main_layout->addLayout(actions_layout);

    m_ContentScrollArea = new QScrollArea(this);
    m_ContentScrollArea->setObjectName(QStringLiteral("anExtensionStatusScrollArea"));
    m_ContentScrollArea->setWidgetResizable(true);
    m_ContentScrollArea->setFrameShape(QFrame::NoFrame);
    m_ContentScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_ScrollContent = new QWidget(m_ContentScrollArea);
    m_ScrollContent->setObjectName(QStringLiteral("anExtensionStatusScrollContent"));
    auto* scroll_layout = new QVBoxLayout(m_ScrollContent);
    scroll_layout->setObjectName(QStringLiteral("anExtensionStatusScrollLayout"));
    scroll_layout->setSpacing(t.spacing.lg);
    scroll_layout->setContentsMargins(0, 0, 0, 0);

    m_CardsContainer = new QWidget(m_ScrollContent);
    m_CardsContainer->setObjectName(QStringLiteral("anExtensionStatusCardsContainer"));
    m_CardsLayout = new QVBoxLayout(m_CardsContainer);
    m_CardsLayout->setObjectName(QStringLiteral("anExtensionStatusCardsLayout"));
    m_CardsLayout->setSpacing(t.spacing.md);
    m_CardsLayout->setContentsMargins(0, 0, 0, 0);
    scroll_layout->addWidget(m_CardsContainer);
    scroll_layout->addStretch(1);

    m_ContentScrollArea->setWidget(m_ScrollContent);
    main_layout->addWidget(m_ContentScrollArea, 1);
}

void ExtensionStatusPage::reloadStatus(const bool forceRefresh)
{
    if (m_DiscoveryWatcher && !forceRefresh)
    {
        return;
    }
    if (forceRefresh)
    {
        m_DiscoveryWatcher = nullptr;
    }

    if (m_DiscoveryProvider)
    {
        applyDiscoveryResults(m_DiscoveryProvider());
        return;
    }

    std::vector<ExtensionLoadResult> cached_results;
    if (!forceRefresh && cachedInstalledExtensionStatusResults(&cached_results))
    {
        applyDiscoveryResults(std::move(cached_results));
        return;
    }

    std::vector<ExtensionLoadResult> stale_results;
    bool displayed_stale_results = false;
    if (cachedInstalledExtensionStatusResults(&stale_results, true))
    {
        applyDiscoveryResults(std::move(stale_results));
        displayed_stale_results = true;
    }
    else if (!m_LastResults.empty())
    {
        stale_results = m_LastResults;
        applyDiscoveryResults(std::move(stale_results));
        displayed_stale_results = true;
    }

    if (displayed_stale_results)
    {
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
            m_SummaryBanner->setMessage(tr("Refreshing installed extension status in the background..."));
        }
    }
    else
    {
        clearCards();
        m_LastResults.clear();
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
            m_SummaryBanner->setMessage(tr("Loading installed extension status..."));
        }
        if (m_CardsLayout)
        {
            m_CardsLayout->addWidget(
                createInfoLabel(tr("Discovering installed extension packages and runtime status.")));
            m_CardsLayout->addStretch(1);
        }
        refreshStyles();
    }
    setOperationInProgress(true);

    auto* watcher = startInstalledExtensionStatusCacheRefresh(forceRefresh);
    if (!watcher)
    {
        std::vector<ExtensionLoadResult> refreshed_results;
        if (cachedInstalledExtensionStatusResults(&refreshed_results))
        {
            setOperationInProgress(false);
            applyDiscoveryResults(std::move(refreshed_results));
        }
        return;
    }

    m_DiscoveryWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<std::vector<ExtensionLoadResult>>::finished, this, [this, watcher]() {
        if (m_DiscoveryWatcher != watcher)
        {
            return;
        }
        m_DiscoveryWatcher = nullptr;
        setOperationInProgress(false);
        std::vector<ExtensionLoadResult> results;
        if (cachedInstalledExtensionStatusResults(&results))
        {
            applyDiscoveryResults(std::move(results));
        }
    });
}

void ExtensionStatusPage::applyDiscoveryResults(std::vector<ExtensionLoadResult> results)
{
    clearCards();

    results.erase(std::remove_if(results.begin(), results.end(),
                                 [](const ExtensionLoadResult& result) { return !result.manifest.has_value(); }),
                  results.end());

    m_LastResults = std::move(results);

    if (m_LastResults.empty())
    {
        m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
        m_SummaryBanner->setMessage(tr("No bundled or user-installed extension packages were discovered."));
        m_CardsLayout->addWidget(createInfoLabel(
            tr("Open this page again after adding or provisioning an extension package to refresh the status list.")));
        m_CardsLayout->addStretch(1);
        refreshStyles();
        return;
    }

    int available_count = 0;
    int bootstrapping_count = 0;
    int needs_attention_count = 0;
    int incompatible_count = 0;
    for (const auto& result : m_LastResults)
    {
        const QString availability = availabilityStateText(result);
        if (availability == tr("Available"))
        {
            ++available_count;
        }
        else if (availability == tr("Bootstrapping"))
        {
            ++bootstrapping_count;
        }
        else if (availability == tr("Incompatible"))
        {
            ++incompatible_count;
        }
        else
        {
            ++needs_attention_count;
        }
    }

    const QString summary =
        tr("%1 extension package(s) discovered: %2 available, %3 bootstrapping, %4 incompatible, %5 needing attention.")
            .arg(m_LastResults.size())
            .arg(available_count)
            .arg(bootstrapping_count)
            .arg(incompatible_count)
            .arg(needs_attention_count);
    m_SummaryBanner->setTone((needs_attention_count > 0 || incompatible_count > 0) ? an::ui::AnInfoBanner::Tone::Warning
                                                                                   : an::ui::AnInfoBanner::Tone::Info);
    m_SummaryBanner->setMessage(summary);

    m_CardsLayout->addWidget(
        createInfoLabel(tr("This page re-runs extension discovery when refreshed, so availability and environment "
                           "diagnostics reflect the current package and environment state.")));

    for (const auto& result : m_LastResults)
    {
        if (QWidget* card = buildExtensionCard(result))
        {
            m_CardsLayout->addWidget(card);
        }
    }
    m_CardsLayout->addStretch(1);

    refreshStyles();
}

void ExtensionStatusPage::clearCards()
{
    m_Cards.clear();
    m_ActionButtons.clear();
    m_DetailButtons.clear();
    if (!m_CardsLayout)
    {
        return;
    }

    while (QLayoutItem* item = m_CardsLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            delete widget;
        }
        delete item;
    }
}

bool ExtensionStatusPage::openExtensionSettingsDialog(const std::string_view extension_id)
{
    if (extension_id.empty())
    {
        return false;
    }

    const auto open_loaded_settings = [&]() -> bool {
        for (const auto& result : m_LastResults)
        {
            if (result.manifest.has_value() && result.manifest->id == extension_id)
            {
                openSettingsDialog(result);
                return true;
            }
        }

        return false;
    };

    if (!m_DiscoveryProvider)
    {
        std::vector<ExtensionLoadResult> cached_results;
        if (cachedInstalledExtensionStatusResults(&cached_results))
        {
            applyDiscoveryResults(std::move(cached_results));
        }

        if (open_loaded_settings())
        {
            return true;
        }

        if (m_LastResults.empty())
        {
            reloadStatus(false);
        }

        return open_loaded_settings();
    }

    if (open_loaded_settings())
    {
        return true;
    }

    applyDiscoveryResults(m_DiscoveryProvider());
    return open_loaded_settings();
}

bool ExtensionStatusPage::openExtensionSettings(const std::string_view extension_id)
{
    if (extension_id.empty())
    {
        return false;
    }

    auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(
        findChild<QWidget*>(QStringLiteral("anExtensionSettingsSection_%1").arg(sanitizeObjectToken(extension_id))));
    if (!section)
    {
        return false;
    }

    section->setExpanded(true);
    if (m_ContentScrollArea)
    {
        m_ContentScrollArea->ensureWidgetVisible(section);
    }
    return true;
}

void ExtensionStatusPage::openSettingsDialog(const an::ui::extensions::ExtensionLoadResult& result)
{
    if (!result.manifest.has_value() || !result.settings_schema.has_value() ||
        result.settings_schema->state != ExtensionSettingsSchemaState::Ready ||
        result.settings_schema->schema.isEmpty())
    {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("%1 Settings").arg(QString::fromStdString(result.manifest->name)));
    dialog.setMinimumWidth(500);
    dialog.setMinimumHeight(400);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(an::ui::tokens().spacing.lg);

    auto* form = new ExtensionSettingsSection(m_Settings, &dialog);
    std::string error;
    if (!form->bindExtension(result, &error))
    {
        const QString diagnostic =
            tr("Failed to render cached settings for %1: %2")
                .arg(QString::fromStdString(result.manifest->name), QString::fromStdString(error));
        logExtensionStatusPageIssue("settings render failed", diagnostic, false);
        QMessageBox::warning(this, tr("Error"), diagnostic);
        return;
    }

    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidget(form);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);

    // Apply button roles natively using properties instead of non-existent helper function
    auto* saveButton = buttonBox->button(QDialogButtonBox::Save);
    if (saveButton)
    {
        saveButton->setProperty("anButtonVariant", QStringLiteral("primary"));
        saveButton->setProperty("anProcessButtonRole", QStringLiteral("primary"));
    }
    auto* cancelButton = buttonBox->button(QDialogButtonBox::Cancel);
    if (cancelButton)
    {
        cancelButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
        cancelButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    }

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [&]() {
        std::string save_err;
        if (form->saveSettings(&save_err))
        {
            dialog.accept();
        }
        else
        {
            QMessageBox::warning(&dialog, tr("Error Saving Settings"), QString::fromStdString(save_err));
        }
    });
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(buttonBox);

    dialog.exec();
}

void ExtensionStatusPage::setOperationInProgress(const bool inProgress)
{
    m_OperationInProgress = inProgress;
    if (m_RefreshButton)
    {
        m_RefreshButton->setEnabled(!inProgress);
    }
    for (QPushButton* button : m_ActionButtons)
    {
        if (!button)
        {
            continue;
        }
        const bool available = button->property("anExtensionStatusActionAvailable").toBool();
        button->setEnabled(available && !inProgress);
    }
    for (QToolButton* button : m_DetailButtons)
    {
        if (button)
        {
            button->setEnabled(!inProgress);
        }
    }
}

void ExtensionStatusPage::registerActionButton(QPushButton* button, const bool available)
{
    if (!button)
    {
        return;
    }

    button->setProperty("anExtensionStatusActionAvailable", available);
    button->setEnabled(available && !m_OperationInProgress);
    m_ActionButtons.push_back(button);
}

void ExtensionStatusPage::uninstallExtension(const ExtensionLoadResult& result, QPushButton* actionButton)
{
    if (m_OperationInProgress || !result.manifest.has_value() || !canUninstallExtension(result))
    {
        return;
    }

    bool confirmed = false;
    if (m_UninstallConfirmationProvider)
    {
        confirmed = m_UninstallConfirmationProvider(result);
    }
    else
    {
        const QString extension_name =
            QString::fromStdString(result.manifest->name.empty() ? result.manifest->id : result.manifest->name);
        QMessageBox confirmation(this);
        confirmation.setIcon(QMessageBox::Warning);
        confirmation.setWindowTitle(tr("Uninstall Extension"));
        confirmation.setText(tr("Uninstall %1?").arg(extension_name));
        confirmation.setInformativeText(tr("This removes the installed extension package from disk. Running extension "
                                           "sessions for this package should be closed first."));
        confirmation.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        confirmed = confirmation.exec() == QMessageBox::Yes;
    }

    if (!confirmed)
    {
        return;
    }

    const UninstallProvider provider =
        m_UninstallProvider ? m_UninstallProvider : UninstallProvider(defaultUninstallProvider);

    if (actionButton)
    {
        actionButton->setText(tr("Uninstalling..."));
    }
    setOperationInProgress(true);
    if (m_SummaryBanner)
    {
        const QString extension_name =
            QString::fromStdString(result.manifest->name.empty() ? result.manifest->id : result.manifest->name);
        m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
        m_SummaryBanner->setMessage(tr("Uninstalling %1...").arg(extension_name));
    }

    auto* watcher = new QFutureWatcher<an::ui::ExtensionStatusPageUninstallResult>(this);
    m_UninstallWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<an::ui::ExtensionStatusPageUninstallResult>::finished, this,
                     [this, watcher, result]() {
                         const an::ui::ExtensionStatusPageUninstallResult uninstall_result = watcher->result();
                         watcher->deleteLater();
                         if (m_UninstallWatcher == watcher)
                         {
                             m_UninstallWatcher = nullptr;
                         }
                         completeUninstallOperation(result, uninstall_result);
                     });
    watcher->setFuture(QtConcurrent::run([provider, result]() { return provider(result); }));
}

void ExtensionStatusPage::completeUninstallOperation(const ExtensionLoadResult& request,
                                                     const an::ui::ExtensionStatusPageUninstallResult& result)
{
    setOperationInProgress(false);
    if (!result.success)
    {
        const QString diagnostic = result.diagnostic.isEmpty() ? tr("Extension uninstall failed.") : result.diagnostic;
        logExtensionStatusPageIssue("uninstall failed", diagnostic, true);
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Error);
            m_SummaryBanner->setMessage(diagnostic);
        }
        return;
    }

    const QString extension_name =
        request.manifest.has_value()
            ? QString::fromStdString(request.manifest->name.empty() ? request.manifest->id : request.manifest->name)
            : tr("extension");
    const QString success_message =
        result.diagnostic.isEmpty() ? tr("Uninstalled %1.").arg(extension_name) : result.diagnostic;
    an::ui::invalidateInstalledExtensionStatusCache(false);
    refreshStatus(true);
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Success);
        m_SummaryBanner->setMessage(success_message);
    }
}

void ExtensionStatusPage::repairExtensionEnvironment(const ExtensionLoadResult& result, QPushButton* actionButton)
{
    if (m_EnvironmentRepairWatcher || !canRepairExtensionEnvironment(result))
    {
        return;
    }

    if (m_SummaryBanner && result.manifest.has_value())
    {
        m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Info);
        m_SummaryBanner->setMessage(
            tr("%1 for %2...")
                .arg(environmentRepairActionText(result),
                     QString::fromStdString(result.manifest->name.empty() ? result.manifest->id
                                                                          : result.manifest->name)));
    }

    const EnvironmentRepairProvider provider = m_EnvironmentRepairProvider
                                                   ? m_EnvironmentRepairProvider
                                                   : EnvironmentRepairProvider(defaultEnvironmentRepairProvider);
    if (actionButton)
    {
        const bool installs_environment =
            !result.environment.has_value() || result.environment->state == ExtensionEnvironmentState::Missing;
        actionButton->setText(installs_environment ? tr("Installing...") : tr("Repairing..."));
    }
    setOperationInProgress(true);

    auto* watcher = new QFutureWatcher<an::ui::ExtensionStatusPageEnvironmentRepairResult>(this);
    m_EnvironmentRepairWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<an::ui::ExtensionStatusPageEnvironmentRepairResult>::finished, this,
                     [this, watcher]() {
                         const an::ui::ExtensionStatusPageEnvironmentRepairResult result = watcher->result();
                         watcher->deleteLater();
                         if (m_EnvironmentRepairWatcher == watcher)
                         {
                             m_EnvironmentRepairWatcher = nullptr;
                         }
                         completeEnvironmentRepair(result);
                     });
    watcher->setFuture(QtConcurrent::run([provider, result]() { return provider(result); }));
}

void ExtensionStatusPage::completeEnvironmentRepair(const an::ui::ExtensionStatusPageEnvironmentRepairResult& result)
{
    setOperationInProgress(false);

    if (result.cancelled)
    {
        const QString diagnostic =
            result.diagnostic.isEmpty() ? tr("Extension environment repair cancelled.") : result.diagnostic;
        logExtensionStatusPageIssue("environment repair cancelled", diagnostic, false);
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Warning);
            m_SummaryBanner->setMessage(diagnostic);
        }
        return;
    }

    if (!result.success)
    {
        const QString diagnostic =
            result.diagnostic.isEmpty() ? tr("Extension environment repair failed.") : result.diagnostic;
        logExtensionStatusPageIssue("environment repair failed", diagnostic, true);
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Error);
            m_SummaryBanner->setMessage(diagnostic);
        }
        return;
    }

    const QString success_message =
        result.diagnostic.isEmpty() ? tr("Extension environment repaired successfully.") : result.diagnostic;
    an::ui::invalidateInstalledExtensionStatusCache(false);
    refreshStatus(true);
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(an::ui::AnInfoBanner::Tone::Success);
        m_SummaryBanner->setMessage(success_message);
    }
}

QWidget* ExtensionStatusPage::buildExtensionCard(const ExtensionLoadResult& result)
{
    if (!result.manifest.has_value())
    {
        return nullptr;
    }

    const auto& manifest = *result.manifest;
    auto* card = new an::ui::AnCardWidget({}, m_CardsContainer);
    card->setObjectName(QStringLiteral("anExtensionStatusCard_%1").arg(sanitizeObjectToken(manifest.id)));
    card->setProperty("anCardCompact", true);
    card->setProperty("anCardBodyPadded", true);

    const auto& t = an::ui::tokens();
    auto* layout = new QVBoxLayout();
    layout->setObjectName(fieldObjectName(manifest.id, "contentLayout"));
    layout->setSpacing(t.spacing.sm);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* title_row = new QHBoxLayout();
    title_row->setObjectName(fieldObjectName(manifest.id, "titleLayout"));
    title_row->setSpacing(t.spacing.sm);
    title_row->setContentsMargins(0, 0, 0, 0);

    auto* title_label = new QLabel(QString::fromStdString(manifest.name), card->contentWidget());
    title_label->setObjectName(fieldObjectName(manifest.id, "title"));
    title_label->setFont(t.typography.heading3);
    title_label->setWordWrap(true);
    title_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    title_row->addWidget(title_label, 1);

    const QString availability = availabilityStateText(result);
    an::ui::AnStatusBadge::Status availability_status = an::ui::AnStatusBadge::Status::Info;
    if (availability == tr("Available"))
        availability_status = an::ui::AnStatusBadge::Status::Success;
    else if (availability == tr("Incompatible") || availability == tr("Validation Failed"))
        availability_status = an::ui::AnStatusBadge::Status::Error;
    else if (availability == tr("Install Needed") || availability == tr("Repair Needed"))
        availability_status = an::ui::AnStatusBadge::Status::Warning;

    auto* availability_badge = new an::ui::AnStatusBadge(availability, availability_status, card->contentWidget());
    availability_badge->setToolTip(availabilityValueTooltip(availability));
    title_row->addWidget(availability_badge, 0, Qt::AlignVCenter);

    if (!manifest.version.empty())
    {
        auto* version_badge =
            new an::ui::AnStatusBadge(QObject::tr("v%1").arg(QString::fromStdString(manifest.version)),
                                      an::ui::AnStatusBadge::Status::Info, card->contentWidget());
        title_row->addWidget(version_badge, 0, Qt::AlignVCenter);
    }

    title_row->addSpacing(t.spacing.md);
    title_row->addStretch(1);

    auto* actions_layout = new QHBoxLayout();
    actions_layout->setContentsMargins(0, 0, 0, 0);
    actions_layout->setSpacing(t.spacing.sm);

    if (canRepairExtensionEnvironment(result))
    {
        auto* repair_environment_button = new QPushButton(environmentRepairActionText(result), card->contentWidget());
        repair_environment_button->setObjectName(
            buttonObjectName(manifest.id, result.environment.has_value() &&
                                                  result.environment->state == ExtensionEnvironmentState::Missing
                                              ? "installEnvironment"
                                              : "repairEnvironment"));
        repair_environment_button->setProperty("anButtonVariant", QStringLiteral("primary"));
        repair_environment_button->setToolTip(
            (!result.environment.has_value() || result.environment->state == ExtensionEnvironmentState::Missing)
                ? tr("Creates the managed runtime environment for this already installed extension package.")
                : tr("Rebuilds or revalidates the managed runtime environment for this installed extension."));
        registerActionButton(repair_environment_button, true);
        QObject::connect(repair_environment_button, &QPushButton::clicked, card,
                         [this, result, repair_environment_button]() {
                             repairExtensionEnvironment(result, repair_environment_button);
                         });
        actions_layout->addWidget(repair_environment_button);
    }

    if (canUninstallExtension(result))
    {
        auto* uninstall_button = new QPushButton(tr("Uninstall"), card->contentWidget());
        uninstall_button->setObjectName(buttonObjectName(manifest.id, "uninstall"));
        uninstall_button->setProperty("anButtonVariant", QStringLiteral("danger"));
        uninstall_button->setProperty("anProcessButtonRole", QStringLiteral("danger"));
        uninstall_button->setToolTip(tr("Removes the installed package files for this extension."));
        registerActionButton(uninstall_button, true);
        QObject::connect(uninstall_button, &QPushButton::clicked, card,
                         [this, result, uninstall_button]() { uninstallExtension(result, uninstall_button); });
        actions_layout->addWidget(uninstall_button);
    }

    if (result.settings_schema.has_value() && !result.settings_schema->schema.isEmpty() &&
        result.settings_schema->state == ExtensionSettingsSchemaState::Ready)
    {
        auto* settings_button = new QPushButton(tr("Settings"), card->contentWidget());
        settings_button->setObjectName(buttonObjectName(manifest.id, "settings"));
        settings_button->setProperty("anButtonVariant", QStringLiteral("secondary"));
        settings_button->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
        settings_button->setToolTip(tr("Configure shared extension settings."));
        registerActionButton(settings_button, true);
        QObject::connect(settings_button, &QPushButton::clicked, card,
                         [this, id = manifest.id]() { (void)openExtensionSettingsDialog(id); });
        actions_layout->addWidget(settings_button);
    }

    auto* reveal_package_button = new QPushButton(tr("Reveal Package"), card->contentWidget());
    reveal_package_button->setObjectName(buttonObjectName(manifest.id, "revealPackage"));
    reveal_package_button->setProperty("anButtonVariant", QStringLiteral("secondary"));
    reveal_package_button->setToolTip(tr("Reveal this extension's package folder in the file manager."));
    const bool reveal_package_available =
        !result.package_root.empty() && QFileInfo::exists(QString::fromStdString(result.package_root.string()));
    registerActionButton(reveal_package_button, reveal_package_available);
    QObject::connect(reveal_package_button, &QPushButton::clicked, card, [path = result.package_root]() {
        if (!path.empty())
        {
            an::ui::revealInFileManager(QString::fromStdString(path.string()));
        }
    });
    actions_layout->addWidget(reveal_package_button);

    auto* details_button = new QToolButton(card->contentWidget());
    details_button->setObjectName(buttonObjectName(manifest.id, "details"));
    details_button->setText(tr("Details"));
    details_button->setCheckable(true);
    details_button->setArrowType(Qt::RightArrow);
    details_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details_button->setEnabled(!m_OperationInProgress);
    actions_layout->addWidget(details_button);
    m_DetailButtons.push_back(details_button);

    title_row->addLayout(actions_layout);
    layout->addLayout(title_row);

    const QString summary_text = tr("Source: %1 | Host Mode: %2 | ID: %3")
                                     .arg(sourceTierText(result.source), hostModeText(manifest.sdk_backend),
                                          QString::fromStdString(manifest.id));
    auto* summary_label = new QLabel(summary_text, card->contentWidget());
    summary_label->setObjectName(fieldObjectName(manifest.id, "summary"));
    summary_label->setFont(t.typography.body);
    summary_label->setWordWrap(true);
    summary_label->setProperty("anStatusTone", QStringLiteral("secondary"));
    layout->addWidget(summary_label);

    auto* details_widget = new QWidget(card->contentWidget());
    details_widget->setObjectName(fieldObjectName(manifest.id, "detailsPanel"));

    auto* details_layout = new QVBoxLayout(details_widget);
    details_layout->setObjectName(fieldObjectName(manifest.id, "detailsLayout"));
    details_layout->setSpacing(t.spacing.sm);
    details_layout->setContentsMargins(0, 0, 0, 0);

    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(t.spacing.md);
    grid->setVerticalSpacing(t.spacing.sm);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    int row = 0;
    addStatusRow(grid, row, 0, tr("Package Root:"), packageRootText(result),
                 fieldObjectName(manifest.id, "packageRoot"), true);
    addStatusRow(grid, row++, 1, tr("Environment Path:"), environmentPathText(result.environment),
                 fieldObjectName(manifest.id, "environmentPath"), true);
    addStatusRow(grid, row, 0, tr("Manifest File:"), manifestPathText(result),
                 fieldObjectName(manifest.id, "manifestPath"), true);
    addStatusRow(grid, row++, 1, tr("Runtime Targets:"), listText(manifest.runtime_targets),
                 fieldObjectName(manifest.id, "runtimeTargets"));
    addStatusRow(grid, row, 0, tr("Validation Result:"), validationResultText(result),
                 fieldObjectName(manifest.id, "validation"));
    addStatusRow(grid, row++, 1, tr("Install / Repair:"), installRepairText(result),
                 fieldObjectName(manifest.id, "installRepair"));
    addStatusRow(grid, row, 0, tr("Last Launch:"), launchResultText(result), fieldObjectName(manifest.id, "launch"));
    addStatusRow(grid, row++, 1, tr("Last Crash / Termination:"), crashResultText(result),
                 fieldObjectName(manifest.id, "crash"));

    details_layout->addLayout(grid);

    if (!result.diagnostic.empty() || (result.environment.has_value() && !result.environment->diagnostic.empty()) ||
        (result.settings_schema.has_value() && result.settings_schema->state == ExtensionSettingsSchemaState::Failed))
    {
        auto* diagnostic_banner = new an::ui::AnInfoBanner(
            result.settings_schema.has_value() && result.settings_schema->state == ExtensionSettingsSchemaState::Failed
                ? settingsSchemaDetailText(result)
                : validationResultText(result),
            result.settings_schema.has_value() && result.settings_schema->state == ExtensionSettingsSchemaState::Failed
                ? an::ui::AnInfoBanner::Tone::Warning
            : result.status == ExtensionLoadStatus::Ready ? an::ui::AnInfoBanner::Tone::Info
                                                          : an::ui::AnInfoBanner::Tone::Warning,
            card->contentWidget());
        diagnostic_banner->setObjectName(fieldObjectName(manifest.id, "diagnosticBanner"));
        details_layout->addWidget(diagnostic_banner);
    }

    const QString recovery_guidance = recoveryGuidanceText(result);
    if (!recovery_guidance.isEmpty())
    {
        auto* recovery_banner =
            new an::ui::AnInfoBanner(recovery_guidance, recoveryGuidanceTone(result), card->contentWidget());
        recovery_banner->setObjectName(fieldObjectName(manifest.id, "recoveryBanner"));
        details_layout->addWidget(recovery_banner);
    }

    details_widget->setVisible(false);
    QObject::connect(details_button, &QToolButton::toggled, card, [details_button, details_widget](const bool checked) {
        if (details_widget)
        {
            details_widget->setVisible(checked);
        }
        if (details_button)
        {
            details_button->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
            details_button->setText(checked ? QObject::tr("Hide details") : QObject::tr("Details"));
        }
    });

    layout->addWidget(details_widget);
    card->setContentLayout(layout);

    m_Cards.push_back(card);
    return card;
}
