// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ExtensionManagerPage.h"

#include "app/ApplicationStorage.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionEnvironmentManager.h"
#include "app/FileDialogUtils.h"
#include "components/AnCardWidget.h"
#include "components/AnInfoBanner.h"
#include "components/AnStatusBadge.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <verdigris/wobjectimpl.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMargins>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGlobal>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>

W_OBJECT_IMPL(an::ui::ExtensionManagerPage)

namespace
{

using an::ui::ExtensionManagerPageEnvironmentCleanupRequest;
using an::ui::ExtensionManagerPageEnvironmentCleanupResult;
using an::ui::ExtensionManagerPageInstallRequest;
using an::ui::ExtensionManagerPageInstallResult;
using an::ui::ExtensionManagerPageLocalInstallRequest;
using an::ui::ExtensionManagerPageRemoveRequest;
using an::ui::ExtensionManagerPageRemoveResult;
using an::ui::ExtensionManagerPageRepositorySnapshot;
using an::ui::extensions::ExtensionDiscoveryOptions;
using an::ui::extensions::ExtensionEnvironmentManager;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionManager;
using an::ui::extensions::ExtensionOperationCancellationToken;
using an::ui::extensions::ExtensionOperationProgressCallback;
using an::ui::extensions::ExtensionPackageInstallRequest;
using an::ui::extensions::ExtensionPackageRemoveRequest;
using an::ui::extensions::ExtensionPackageSignatureState;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionPackageTransactionService;
using an::ui::extensions::ExtensionRepositoryBrowseEntry;
using an::ui::extensions::ExtensionRepositoryBrowseOptions;
using an::ui::extensions::ExtensionRepositoryClient;
using an::ui::extensions::ExtensionRepositoryIndexFetchRequest;
using an::ui::extensions::ExtensionRepositoryInstallState;

constexpr auto kLastLocalPackageDirectorySetting = "extensions/lastLocalPackageDirectory";
constexpr auto kAllowUnsignedExtensionsSetting = "extensions/allowUnsignedPackages";
constexpr auto kAutomaticEnvironmentCleanupSetting = an::ui::extensions::kAutomaticExtensionEnvironmentCleanupSetting;
constexpr auto kRepositorySnapshotCacheDirectoryEnv = "AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR";
constexpr auto kRepositorySnapshotCacheFileName = "repository_snapshot.json";
constexpr int kRepositorySnapshotCacheFormatVersion = 1;

void logExtensionManagerPageIssue(const char* operation, const QString& message, const bool error)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    const std::string detail = message.toStdString();
    if (error)
    {
        LOG_ERROR("Extension Manager {}: {}", operation, detail);
        return;
    }

    LOG_WARNING("Extension Manager {}: {}", operation, detail);
}

void logExtensionManagerPageInfo(const char* operation, const QString& message)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_INFO("Extension Manager {}: {}", operation, message.toStdString());
}

void logExtensionManagerPageDebug(const char* operation, const QString& message)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_DEBUG("Extension Manager {}: {}", operation, message.toStdString());
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
    return QStringLiteral("anExtensionManager_%1_%2")
        .arg(QString::fromLatin1(field_name), sanitizeObjectToken(extension_id));
}

QString buttonObjectName(const std::string_view extension_id, const char* button_name)
{
    return QStringLiteral("anExtensionManagerButton_%1_%2")
        .arg(QString::fromLatin1(button_name), sanitizeObjectToken(extension_id));
}

QString pendingCancelButtonObjectName(const std::string_view extension_id)
{
    return QStringLiteral("anExtensionManagerButton_cancelPending_%1").arg(sanitizeObjectToken(extension_id));
}

QString metadataText(const std::string& value)
{
    return value.empty() ? QObject::tr("Not declared") : QString::fromStdString(value);
}

bool linkableExtensionMetadata(const ExtensionPackageSignatureState state)
{
    return state != ExtensionPackageSignatureState::Unsigned;
}

QString linkifiedUrlText(const QString& text)
{
    static const QRegularExpression url_pattern(QStringLiteral(R"((https?://[^\s<>"']+))"));

    QString rich_text;
    int cursor = 0;
    bool linked = false;
    const auto matches = url_pattern.globalMatch(text);
    auto iterator = matches;
    while (iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        const QString url = match.captured(1);
        const QUrl parsed_url(url);
        if (!parsed_url.isValid() ||
            (parsed_url.scheme() != QStringLiteral("http") && parsed_url.scheme() != QStringLiteral("https")))
        {
            continue;
        }

        rich_text += text.mid(cursor, match.capturedStart(1) - cursor).toHtmlEscaped();
        const QString escaped_url = url.toHtmlEscaped();
        rich_text += QStringLiteral("<a href=\"%1\">%1</a>").arg(escaped_url);
        cursor = match.capturedEnd(1);
        linked = true;
    }

    if (!linked)
    {
        return {};
    }

    rich_text += text.mid(cursor).toHtmlEscaped();
    return rich_text;
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
        return QObject::tr("Signed");
    case ExtensionPackageSignatureState::Failed:
        return QObject::tr("Signature failed");
    }

    return QObject::tr("Unknown");
}

QString packageSourceText(const ExtensionPackageSource source)
{
    switch (source)
    {
    case ExtensionPackageSource::Bundled:
        return QObject::tr("Bundled");
    case ExtensionPackageSource::Repository:
        return QObject::tr("Repository installed");
    case ExtensionPackageSource::LocalPackage:
        return QObject::tr("Local package");
    case ExtensionPackageSource::User:
        return QObject::tr("User-installed");
    }

    return QObject::tr("Installed");
}

QString installStateText(const ExtensionRepositoryInstallState state)
{
    switch (state)
    {
    case ExtensionRepositoryInstallState::NotInstalled:
        return QObject::tr("Not installed");
    case ExtensionRepositoryInstallState::Installed:
        return QObject::tr("Installed");
    case ExtensionRepositoryInstallState::UpdateAvailable:
        return QObject::tr("Update available");
    case ExtensionRepositoryInstallState::Incompatible:
        return QObject::tr("Incompatible");
    }

    return QObject::tr("Unknown");
}

QString installStateText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.install_state == ExtensionRepositoryInstallState::Installed && entry.installed_source_available)
    {
        return packageSourceText(entry.installed_source);
    }

    return installStateText(entry.install_state);
}

QString installStateTooltipText(const ExtensionRepositoryBrowseEntry& entry)
{
    switch (entry.install_state)
    {
    case ExtensionRepositoryInstallState::NotInstalled:
        return QObject::tr("This extension is available from the repository but is not installed.");
    case ExtensionRepositoryInstallState::Installed:
        if (entry.installed_source_available)
        {
            switch (entry.installed_source)
            {
            case ExtensionPackageSource::Bundled:
                return QObject::tr("This extension is bundled with AfterNight and cannot be uninstalled here.");
            case ExtensionPackageSource::Repository:
                return QObject::tr("This extension is installed from the extension repository.");
            case ExtensionPackageSource::LocalPackage:
                return QObject::tr("This extension was installed from a local package.");
            case ExtensionPackageSource::User:
                return QObject::tr("This extension is installed in the user extension directory.");
            }
        }
        return QObject::tr("This extension is installed.");
    case ExtensionRepositoryInstallState::UpdateAvailable:
        if (!entry.installed_version.empty() && !entry.latest_version.empty())
        {
            return QObject::tr("Version %1 is installed. Version %2 is available from the repository.")
                .arg(QString::fromStdString(entry.installed_version), QString::fromStdString(entry.latest_version));
        }
        return QObject::tr("A newer repository version is available.");
    case ExtensionRepositoryInstallState::Incompatible:
        if (!entry.compatibility_diagnostic.empty())
        {
            return QString::fromStdString(entry.compatibility_diagnostic);
        }
        return QObject::tr("The latest repository release is not compatible with this AfterNight runtime.");
    }

    return QObject::tr("Extension installation state.");
}

QString installedEnvironmentStateText(const an::ui::extensions::ExtensionEnvironmentState state)
{
    switch (state)
    {
    case an::ui::extensions::ExtensionEnvironmentState::Ready:
        return QObject::tr("Ready");
    case an::ui::extensions::ExtensionEnvironmentState::Missing:
        return QObject::tr("Install needed");
    case an::ui::extensions::ExtensionEnvironmentState::Stale:
        return QObject::tr("Repair needed");
    case an::ui::extensions::ExtensionEnvironmentState::Failed:
        return QObject::tr("Failed");
    case an::ui::extensions::ExtensionEnvironmentState::Bootstrapping:
        return QObject::tr("Bootstrapping");
    }

    return QObject::tr("Unknown");
}

QString installedLoadStatusText(const an::ui::extensions::ExtensionLoadStatus status)
{
    switch (status)
    {
    case an::ui::extensions::ExtensionLoadStatus::Ready:
        return QObject::tr("Ready");
    case an::ui::extensions::ExtensionLoadStatus::Incompatible:
        return QObject::tr("Incompatible");
    case an::ui::extensions::ExtensionLoadStatus::Invalid:
        return QObject::tr("Failed");
    }

    return QObject::tr("Unknown");
}

QString installedStatusText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.install_state == ExtensionRepositoryInstallState::Incompatible)
    {
        return QObject::tr("Incompatible");
    }
    if (entry.installed_version.empty())
    {
        return QObject::tr("Not installed");
    }
    if (entry.installed_environment_available &&
        entry.installed_environment_state != an::ui::extensions::ExtensionEnvironmentState::Ready)
    {
        return installedEnvironmentStateText(entry.installed_environment_state);
    }
    if (entry.installed_status_available &&
        entry.installed_load_status != an::ui::extensions::ExtensionLoadStatus::Ready)
    {
        return installedLoadStatusText(entry.installed_load_status);
    }
    if (entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable)
    {
        return QObject::tr("Update available");
    }
    if (entry.installed_environment_available)
    {
        return installedEnvironmentStateText(entry.installed_environment_state);
    }
    if (entry.installed_status_available)
    {
        return installedLoadStatusText(entry.installed_load_status);
    }

    return QObject::tr("Installed");
}

QString runtimeStatusTooltipText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.install_state == ExtensionRepositoryInstallState::Incompatible)
    {
        return entry.compatibility_diagnostic.empty()
                   ? QObject::tr("This extension is incompatible with the current runtime.")
                   : QString::fromStdString(entry.compatibility_diagnostic);
    }
    if (entry.installed_environment_available &&
        entry.installed_environment_state != an::ui::extensions::ExtensionEnvironmentState::Ready)
    {
        if (!entry.installed_environment_diagnostic.empty())
        {
            return QString::fromStdString(entry.installed_environment_diagnostic);
        }
        switch (entry.installed_environment_state)
        {
        case an::ui::extensions::ExtensionEnvironmentState::Missing:
            return QObject::tr("The isolated runtime environment has not been installed yet.");
        case an::ui::extensions::ExtensionEnvironmentState::Stale:
            return QObject::tr("The isolated runtime environment is stale and should be repaired.");
        case an::ui::extensions::ExtensionEnvironmentState::Failed:
            return QObject::tr("The isolated runtime environment failed validation.");
        case an::ui::extensions::ExtensionEnvironmentState::Bootstrapping:
            return QObject::tr("The isolated runtime environment is still being prepared.");
        case an::ui::extensions::ExtensionEnvironmentState::Ready:
            break;
        }
    }
    if (entry.installed_status_available &&
        entry.installed_load_status != an::ui::extensions::ExtensionLoadStatus::Ready)
    {
        if (!entry.installed_status_diagnostic.empty())
        {
            return QString::fromStdString(entry.installed_status_diagnostic);
        }
        if (entry.installed_load_status == an::ui::extensions::ExtensionLoadStatus::Incompatible)
        {
            return QObject::tr("The installed extension manifest is incompatible with this AfterNight runtime.");
        }
        return QObject::tr("The installed extension manifest could not be loaded.");
    }
    if (entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable)
    {
        return QObject::tr("The installed extension is ready, but a newer repository version is available.");
    }
    if (!entry.installed_version.empty())
    {
        return QObject::tr("The installed extension and its runtime environment are ready.");
    }

    return QObject::tr("Runtime status is shown after the extension is installed.");
}

QString installedStatusDetailText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!entry.installed_status_diagnostic.empty())
    {
        return QString::fromStdString(entry.installed_status_diagnostic);
    }
    if (!entry.installed_environment_diagnostic.empty())
    {
        return QString::fromStdString(entry.installed_environment_diagnostic);
    }
    if (entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable)
    {
        return QObject::tr("Installed %1, latest %2 is available.")
            .arg(QString::fromStdString(entry.installed_version), QString::fromStdString(entry.latest_version));
    }
    if (entry.installed_version.empty())
    {
        return QObject::tr("This extension is not installed.");
    }
    if (entry.installed_source_available && entry.installed_source != ExtensionPackageSource::Repository)
    {
        return QObject::tr("%1 extension version %2 is installed.")
            .arg(packageSourceText(entry.installed_source), QString::fromStdString(entry.installed_version));
    }

    return QObject::tr("No diagnostics recorded.");
}

QString extensionDisplayNameText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!entry.name.empty())
    {
        return QString::fromStdString(entry.name);
    }
    if (!entry.id.empty())
    {
        return QString::fromStdString(entry.id);
    }

    return QObject::tr("Unnamed extension");
}

QString extensionSummaryText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!entry.summary.empty())
    {
        return QString::fromStdString(entry.summary);
    }
    if (!entry.description.empty())
    {
        return QString::fromStdString(entry.description);
    }

    return QObject::tr("No summary provided.");
}

QString compactVersionText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!entry.latest_version.empty() && !entry.installed_version.empty() &&
        entry.latest_version != entry.installed_version)
    {
        return QObject::tr("%1 available, %2 installed")
            .arg(QString::fromStdString(entry.latest_version), QString::fromStdString(entry.installed_version));
    }
    if (!entry.latest_version.empty())
    {
        return QObject::tr("Latest %1").arg(QString::fromStdString(entry.latest_version));
    }
    if (!entry.installed_version.empty())
    {
        return QObject::tr("Installed %1").arg(QString::fromStdString(entry.installed_version));
    }

    return QObject::tr("Version unavailable");
}

bool updateAvailable(const ExtensionRepositoryBrowseEntry& entry)
{
    return entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable;
}

std::vector<int> parseVersionComponents(const std::string& value, bool* ok = nullptr)
{
    std::vector<int> components;
    int component = 0;
    bool has_digit = false;
    bool valid = true;

    for (const char ch : value)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isdigit(uch) != 0)
        {
            has_digit = true;
            component = (component * 10) + (ch - '0');
            continue;
        }

        if (ch == '.')
        {
            if (!has_digit)
            {
                valid = false;
                break;
            }
            components.push_back(component);
            component = 0;
            has_digit = false;
            continue;
        }

        valid = false;
        break;
    }

    if (valid && has_digit)
    {
        components.push_back(component);
    }
    else
    {
        valid = false;
        components.clear();
    }

    if (ok)
    {
        *ok = valid;
    }
    return components;
}

int compareVersionText(const std::string& lhs, const std::string& rhs)
{
    bool lhs_ok = false;
    bool rhs_ok = false;
    auto lhs_components = parseVersionComponents(lhs, &lhs_ok);
    auto rhs_components = parseVersionComponents(rhs, &rhs_ok);
    if (lhs_ok && rhs_ok)
    {
        const std::size_t size = (std::max)(lhs_components.size(), rhs_components.size());
        lhs_components.resize(size, 0);
        rhs_components.resize(size, 0);
        for (std::size_t i = 0; i < size; ++i)
        {
            if (lhs_components[i] < rhs_components[i])
            {
                return -1;
            }
            if (lhs_components[i] > rhs_components[i])
            {
                return 1;
            }
        }
        return 0;
    }

    if (lhs == rhs)
    {
        return 0;
    }
    return lhs < rhs ? -1 : 1;
}

ExtensionRepositoryInstallState installStateForCurrentInstalledVersion(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!entry.runtime_compatible)
    {
        return ExtensionRepositoryInstallState::Incompatible;
    }
    if (entry.installed_version.empty())
    {
        return ExtensionRepositoryInstallState::NotInstalled;
    }
    if (entry.installed_source == ExtensionPackageSource::Repository && !entry.latest_version.empty() &&
        compareVersionText(entry.installed_version, entry.latest_version) < 0)
    {
        return ExtensionRepositoryInstallState::UpdateAvailable;
    }
    return ExtensionRepositoryInstallState::Installed;
}

bool installedOverrideShouldReplace(const ExtensionRepositoryBrowseEntry& entry, const std::string& installed_version,
                                    const ExtensionPackageSource installed_source)
{
    if (entry.installed_version.empty())
    {
        return true;
    }

    const int version_compare = compareVersionText(installed_version, entry.installed_version);
    if (version_compare != 0)
    {
        return version_compare > 0;
    }

    return entry.installed_source_available && entry.installed_source != installed_source;
}

QString primaryVersionBadgeText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (updateAvailable(entry) && !entry.latest_version.empty())
    {
        return QObject::tr("New version %1").arg(QString::fromStdString(entry.latest_version));
    }
    if (!entry.installed_version.empty() &&
        (entry.latest_version.empty() || entry.installed_version != entry.latest_version))
    {
        return QObject::tr("Installed %1").arg(QString::fromStdString(entry.installed_version));
    }
    if (!entry.latest_version.empty())
    {
        return QObject::tr("Latest %1").arg(QString::fromStdString(entry.latest_version));
    }
    if (!entry.installed_version.empty())
    {
        return QObject::tr("Installed %1").arg(QString::fromStdString(entry.installed_version));
    }

    return QObject::tr("Version unavailable");
}

QString versionBadgeTooltipText(const ExtensionRepositoryBrowseEntry& entry)
{
    QStringList fragments;
    if (!entry.latest_version.empty())
    {
        fragments.push_back(
            QObject::tr("Latest repository version: %1").arg(QString::fromStdString(entry.latest_version)));
    }
    if (!entry.installed_version.empty())
    {
        fragments.push_back(QObject::tr("Installed version: %1").arg(QString::fromStdString(entry.installed_version)));
    }
    if (!entry.selected_release_version.empty() && entry.selected_release_version != entry.latest_version)
    {
        fragments.push_back(
            QObject::tr("Selected release: %1").arg(QString::fromStdString(entry.selected_release_version)));
    }
    if (updateAvailable(entry))
    {
        fragments.push_back(QObject::tr("Installing or updating will use the selected repository release."));
    }

    return fragments.isEmpty() ? QObject::tr("No version metadata is available.")
                               : fragments.join(QStringLiteral("\n"));
}

QString installedVersionBadgeText(const ExtensionRepositoryBrowseEntry& entry)
{
    return entry.installed_version.empty()
               ? QString()
               : QObject::tr("Installed %1").arg(QString::fromStdString(entry.installed_version));
}

QString updateVersionCalloutText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!updateAvailable(entry))
    {
        return {};
    }

    if (!entry.installed_version.empty() && !entry.latest_version.empty())
    {
        return QObject::tr("Installed %1; %2 is available.")
            .arg(QString::fromStdString(entry.installed_version), QString::fromStdString(entry.latest_version));
    }
    if (!entry.latest_version.empty())
    {
        return QObject::tr("%1 is available.").arg(QString::fromStdString(entry.latest_version));
    }

    return QObject::tr("A newer version is available.");
}

an::ui::AnStatusBadge::Status versionBadgeStatus(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.install_state == ExtensionRepositoryInstallState::Incompatible)
    {
        return an::ui::AnStatusBadge::Status::Error;
    }
    if (updateAvailable(entry))
    {
        return an::ui::AnStatusBadge::Status::Warning;
    }
    if (entry.install_state == ExtensionRepositoryInstallState::Installed)
    {
        return an::ui::AnStatusBadge::Status::Success;
    }

    return an::ui::AnStatusBadge::Status::Info;
}

QString compactOverviewText(const ExtensionRepositoryBrowseEntry& entry)
{
    QStringList fragments;
    fragments.push_back(compactVersionText(entry));
    if (!entry.author.empty())
    {
        fragments.push_back(QString::fromStdString(entry.author));
    }
    if (!entry.license.empty())
    {
        fragments.push_back(QString::fromStdString(entry.license));
    }
    if (!entry.release_launch_mode.empty())
    {
        if (entry.release_launch_mode == "single_image")
        {
            fragments.push_back(QObject::tr("Single image"));
        }
        else if (entry.release_launch_mode == "workflow")
        {
            fragments.push_back(QObject::tr("Workflow"));
        }
        else
        {
            fragments.push_back(QString::fromStdString(entry.release_launch_mode));
        }
    }

    return fragments.join(QStringLiteral(" | "));
}

an::ui::AnStatusBadge::Status installStateBadgeStatus(const ExtensionRepositoryInstallState state)
{
    switch (state)
    {
    case ExtensionRepositoryInstallState::Installed:
        return an::ui::AnStatusBadge::Status::Success;
    case ExtensionRepositoryInstallState::UpdateAvailable:
        return an::ui::AnStatusBadge::Status::Warning;
    case ExtensionRepositoryInstallState::Incompatible:
        return an::ui::AnStatusBadge::Status::Error;
    case ExtensionRepositoryInstallState::NotInstalled:
        return an::ui::AnStatusBadge::Status::Info;
    }

    return an::ui::AnStatusBadge::Status::Info;
}

an::ui::AnStatusBadge::Status runtimeStatusBadgeStatus(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.install_state == ExtensionRepositoryInstallState::Incompatible)
    {
        return an::ui::AnStatusBadge::Status::Error;
    }

    if (entry.installed_environment_available)
    {
        switch (entry.installed_environment_state)
        {
        case an::ui::extensions::ExtensionEnvironmentState::Ready:
            return an::ui::AnStatusBadge::Status::Success;
        case an::ui::extensions::ExtensionEnvironmentState::Failed:
            return an::ui::AnStatusBadge::Status::Error;
        case an::ui::extensions::ExtensionEnvironmentState::Missing:
        case an::ui::extensions::ExtensionEnvironmentState::Stale:
        case an::ui::extensions::ExtensionEnvironmentState::Bootstrapping:
            return an::ui::AnStatusBadge::Status::Warning;
        }
    }

    if (entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable)
    {
        return an::ui::AnStatusBadge::Status::Warning;
    }
    if (entry.install_state == ExtensionRepositoryInstallState::Installed)
    {
        return an::ui::AnStatusBadge::Status::Success;
    }

    return an::ui::AnStatusBadge::Status::Info;
}

an::ui::AnStatusBadge::Status signatureBadgeStatus(const ExtensionPackageSignatureState state)
{
    switch (state)
    {
    case ExtensionPackageSignatureState::Verified:
        return an::ui::AnStatusBadge::Status::Success;
    case ExtensionPackageSignatureState::Unsigned:
        return an::ui::AnStatusBadge::Status::Warning;
    case ExtensionPackageSignatureState::Failed:
        return an::ui::AnStatusBadge::Status::Error;
    case ExtensionPackageSignatureState::Unknown:
        return an::ui::AnStatusBadge::Status::Info;
    }

    return an::ui::AnStatusBadge::Status::Info;
}

QString signatureBadgeTooltipText(const ExtensionRepositoryBrowseEntry& entry)
{
    switch (entry.selected_asset_signature_state)
    {
    case ExtensionPackageSignatureState::Verified: {
        QStringList fragments;
        fragments.push_back(QObject::tr("The selected package signature is verified."));
        if (!entry.selected_asset_signature_key_id.empty())
        {
            fragments.push_back(
                QObject::tr("Signing key: %1").arg(QString::fromStdString(entry.selected_asset_signature_key_id)));
        }
        if (!entry.selected_asset_signature_algorithm.empty())
        {
            fragments.push_back(
                QObject::tr("Algorithm: %1").arg(QString::fromStdString(entry.selected_asset_signature_algorithm)));
        }
        return fragments.join(QStringLiteral("\n"));
    }
    case ExtensionPackageSignatureState::Unsigned:
        return QObject::tr("The selected package is unsigned and cannot be authenticated by AfterNight.");
    case ExtensionPackageSignatureState::Failed:
        return entry.selected_asset_signature_detail.empty()
                   ? QObject::tr("The selected package signature failed verification.")
                   : QString::fromStdString(entry.selected_asset_signature_detail);
    case ExtensionPackageSignatureState::Unknown:
        return QObject::tr("The selected package signature state is unknown.");
    }

    return QObject::tr("Package signature state.");
}

QString primaryActionText(const ExtensionRepositoryInstallState state)
{
    switch (state)
    {
    case ExtensionRepositoryInstallState::NotInstalled:
        return QObject::tr("Install");
    case ExtensionRepositoryInstallState::Installed:
        return QObject::tr("Installed");
    case ExtensionRepositoryInstallState::UpdateAvailable:
        return QObject::tr("Update");
    case ExtensionRepositoryInstallState::Incompatible:
        return QObject::tr("Unavailable");
    }

    return QObject::tr("Unavailable");
}

QString primaryActionText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable && !entry.latest_version.empty())
    {
        return QObject::tr("Update to %1").arg(QString::fromStdString(entry.latest_version));
    }

    return primaryActionText(entry.install_state);
}

bool primaryActionAvailable(const ExtensionRepositoryBrowseEntry& entry)
{
    return entry.install_state == ExtensionRepositoryInstallState::NotInstalled ||
           entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable;
}

QString primaryActionTooltipText(const ExtensionRepositoryBrowseEntry& entry, const bool networkDisabled)
{
    if (networkDisabled && primaryActionAvailable(entry))
    {
        return QObject::tr("Repository install and update actions are disabled by policy.");
    }

    const QString extension_name = extensionDisplayNameText(entry);
    switch (entry.install_state)
    {
    case ExtensionRepositoryInstallState::NotInstalled:
        if (!entry.selected_release_version.empty())
        {
            return QObject::tr("Install %1 version %2 from the extension repository.")
                .arg(extension_name, QString::fromStdString(entry.selected_release_version));
        }
        if (!entry.latest_version.empty())
        {
            return QObject::tr("Install %1 version %2 from the extension repository.")
                .arg(extension_name, QString::fromStdString(entry.latest_version));
        }
        return QObject::tr("Install %1 from the extension repository.").arg(extension_name);
    case ExtensionRepositoryInstallState::Installed:
        return QObject::tr("%1 is already installed.").arg(extension_name);
    case ExtensionRepositoryInstallState::UpdateAvailable:
        if (!entry.latest_version.empty() && !entry.installed_version.empty())
        {
            return QObject::tr("Update %1 from installed version %2 to repository version %3.")
                .arg(extension_name, QString::fromStdString(entry.installed_version),
                     QString::fromStdString(entry.latest_version));
        }
        if (!entry.latest_version.empty())
        {
            return QObject::tr("Update %1 to repository version %2.")
                .arg(extension_name, QString::fromStdString(entry.latest_version));
        }
        return QObject::tr("Update %1 to the selected repository release.").arg(extension_name);
    case ExtensionRepositoryInstallState::Incompatible:
        return entry.compatibility_diagnostic.empty()
                   ? QObject::tr("%1 is not compatible with this AfterNight runtime.").arg(extension_name)
                   : QString::fromStdString(entry.compatibility_diagnostic);
    }

    return QObject::tr("Repository install action for %1.").arg(extension_name);
}

QString installTrustPolicyDiagnostic(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.selected_asset_package_hash_sha256.empty())
    {
        return QObject::tr("Package asset is missing the required SHA-256 hash.");
    }

    switch (entry.selected_asset_signature_state)
    {
    case ExtensionPackageSignatureState::Unknown:
        return QObject::tr("Package signature state is unknown, so installation is blocked.");
    case ExtensionPackageSignatureState::Unsigned:
        return {};
    case ExtensionPackageSignatureState::Verified:
        if (entry.selected_asset_signature_algorithm != "ed25519" || entry.selected_asset_signature_key_id.empty() ||
            entry.selected_asset_signature.empty())
        {
            return QObject::tr("Package signature metadata is incomplete, so installation is blocked.");
        }
        if (!an::ui::extensions::official_extension_signing_key_trusted(entry.selected_asset_signature_key_id))
        {
            return QObject::tr("Package signature key is not trusted by this build, so installation is blocked.");
        }
        return {};
    case ExtensionPackageSignatureState::Failed:
        return QObject::tr("Package signature verification failed, so installation is blocked.");
    }

    return QObject::tr("Package signature state is unknown, so installation is blocked.");
}

QString formatStorageBytes(const std::uintmax_t bytes)
{
    constexpr std::uintmax_t kibibyte = 1024;
    constexpr std::uintmax_t mebibyte = kibibyte * 1024;
    constexpr std::uintmax_t gibibyte = mebibyte * 1024;
    if (bytes >= gibibyte)
    {
        return QObject::tr("%1 GiB").arg(QString::number(static_cast<double>(bytes) / gibibyte, 'f', 1));
    }
    if (bytes >= mebibyte)
    {
        return QObject::tr("%1 MiB").arg(QString::number(static_cast<double>(bytes) / mebibyte, 'f', 1));
    }
    if (bytes >= kibibyte)
    {
        return QObject::tr("%1 KiB").arg(QString::number(static_cast<double>(bytes) / kibibyte, 'f', 1));
    }

    return QObject::tr("%1 bytes").arg(static_cast<qulonglong>(bytes));
}

QString environmentStorageSummaryText(const ExtensionManagerPageRepositorySnapshot& snapshot)
{
    return QObject::tr("Extension environments: %1 total across %2 environment(s) — %3 active, %4 reclaimable in %5 "
                       "environment(s).")
        .arg(formatStorageBytes(snapshot.environment_total_bytes))
        .arg(static_cast<qulonglong>(snapshot.environment_total_count))
        .arg(formatStorageBytes(snapshot.environment_active_bytes))
        .arg(formatStorageBytes(snapshot.environment_reclaimable_bytes))
        .arg(static_cast<qulonglong>(snapshot.environment_reclaimable_count));
}

QString repositoryNetworkDisabledDiagnostic(const ExtensionManagerPageRepositorySnapshot& snapshot)
{
    return snapshot.diagnostic.isEmpty() ? QObject::tr("Repository network features are disabled by policy.")
                                         : snapshot.diagnostic;
}

QJsonArray stringVectorToJson(const std::vector<std::string>& values)
{
    QJsonArray array;
    for (const auto& value : values)
    {
        array.push_back(QString::fromStdString(value));
    }
    return array;
}

std::vector<std::string> stringVectorFromJson(const QJsonValue& value)
{
    std::vector<std::string> values;
    if (!value.isArray())
    {
        return values;
    }

    const QJsonArray array = value.toArray();
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& item : array)
    {
        if (item.isString())
        {
            values.push_back(item.toString().toStdString());
        }
    }
    return values;
}

QJsonObject repositoryEntryToCacheJson(const ExtensionRepositoryBrowseEntry& entry)
{
    return {
        {QStringLiteral("id"), QString::fromStdString(entry.id)},
        {QStringLiteral("name"), QString::fromStdString(entry.name)},
        {QStringLiteral("summary"), QString::fromStdString(entry.summary)},
        {QStringLiteral("description"), QString::fromStdString(entry.description)},
        {QStringLiteral("author"), QString::fromStdString(entry.author)},
        {QStringLiteral("publisher_id"), QString::fromStdString(entry.publisher_id)},
        {QStringLiteral("license"), QString::fromStdString(entry.license)},
        {QStringLiteral("attribution"), QString::fromStdString(entry.attribution)},
        {QStringLiteral("original_author"), QString::fromStdString(entry.original_author)},
        {QStringLiteral("original_project"), QString::fromStdString(entry.original_project)},
        {QStringLiteral("original_source_url"), QString::fromStdString(entry.original_source_url)},
        {QStringLiteral("upstream_commit"), QString::fromStdString(entry.upstream_commit)},
        {QStringLiteral("homepage_url"), QString::fromStdString(entry.homepage_url)},
        {QStringLiteral("repository_url"), QString::fromStdString(entry.repository_url)},
        {QStringLiteral("support_url"), QString::fromStdString(entry.support_url)},
        {QStringLiteral("icon_url"), QString::fromStdString(entry.icon_url)},
        {QStringLiteral("tags"), stringVectorToJson(entry.tags)},
        {QStringLiteral("latest_version"), QString::fromStdString(entry.latest_version)},
        {QStringLiteral("installed_version"), QString::fromStdString(entry.installed_version)},
        {QStringLiteral("installed_source_available"), entry.installed_source_available},
        {QStringLiteral("installed_source"), static_cast<int>(entry.installed_source)},
        {QStringLiteral("selected_release_version"), QString::fromStdString(entry.selected_release_version)},
        {QStringLiteral("release_min_app_version"), QString::fromStdString(entry.release_min_app_version)},
        {QStringLiteral("release_package_format_version"), entry.release_package_format_version},
        {QStringLiteral("release_protocol_version"), entry.release_protocol_version},
        {QStringLiteral("release_sdk_version"), entry.release_sdk_version},
        {QStringLiteral("release_sdk_backend"), QString::fromStdString(entry.release_sdk_backend)},
        {QStringLiteral("release_launch_mode"), QString::fromStdString(entry.release_launch_mode)},
        {QStringLiteral("release_runtime_targets"), stringVectorToJson(entry.release_runtime_targets)},
        {QStringLiteral("release_changelog"), QString::fromStdString(entry.release_changelog)},
        {QStringLiteral("release_published_at_utc"), QString::fromStdString(entry.release_published_at_utc)},
        {QStringLiteral("selected_asset_name"), QString::fromStdString(entry.selected_asset_name)},
        {QStringLiteral("selected_asset_download_url"), QString::fromStdString(entry.selected_asset_download_url)},
        {QStringLiteral("selected_asset_package_hash_sha256"),
         QString::fromStdString(entry.selected_asset_package_hash_sha256)},
        {QStringLiteral("selected_asset_runtime_targets"), stringVectorToJson(entry.selected_asset_runtime_targets)},
        {QStringLiteral("selected_asset_signature_state"), static_cast<int>(entry.selected_asset_signature_state)},
        {QStringLiteral("selected_asset_signature_detail"),
         QString::fromStdString(entry.selected_asset_signature_detail)},
        {QStringLiteral("selected_asset_signature_algorithm"),
         QString::fromStdString(entry.selected_asset_signature_algorithm)},
        {QStringLiteral("selected_asset_signature_key_id"),
         QString::fromStdString(entry.selected_asset_signature_key_id)},
        {QStringLiteral("selected_asset_signature"), QString::fromStdString(entry.selected_asset_signature)},
        {QStringLiteral("runtime_compatible"), entry.runtime_compatible},
        {QStringLiteral("compatibility_diagnostic"), QString::fromStdString(entry.compatibility_diagnostic)},
        {QStringLiteral("install_state"), static_cast<int>(entry.install_state)},
        {QStringLiteral("installed_status_available"), entry.installed_status_available},
        {QStringLiteral("installed_load_status"), static_cast<int>(entry.installed_load_status)},
        {QStringLiteral("installed_environment_available"), entry.installed_environment_available},
        {QStringLiteral("installed_environment_state"), static_cast<int>(entry.installed_environment_state)},
        {QStringLiteral("installed_status_diagnostic"), QString::fromStdString(entry.installed_status_diagnostic)},
        {QStringLiteral("installed_environment_diagnostic"),
         QString::fromStdString(entry.installed_environment_diagnostic)},
    };
}

std::optional<ExtensionRepositoryBrowseEntry> repositoryEntryFromCacheJson(const QJsonValue& value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    ExtensionRepositoryBrowseEntry entry;
    entry.id = object.value(QStringLiteral("id")).toString().toStdString();
    if (entry.id.empty())
    {
        return std::nullopt;
    }

    entry.name = object.value(QStringLiteral("name")).toString().toStdString();
    entry.summary = object.value(QStringLiteral("summary")).toString().toStdString();
    entry.description = object.value(QStringLiteral("description")).toString().toStdString();
    entry.author = object.value(QStringLiteral("author")).toString().toStdString();
    entry.publisher_id = object.value(QStringLiteral("publisher_id")).toString().toStdString();
    entry.license = object.value(QStringLiteral("license")).toString().toStdString();
    entry.attribution = object.value(QStringLiteral("attribution")).toString().toStdString();
    entry.original_author = object.value(QStringLiteral("original_author")).toString().toStdString();
    entry.original_project = object.value(QStringLiteral("original_project")).toString().toStdString();
    entry.original_source_url = object.value(QStringLiteral("original_source_url")).toString().toStdString();
    entry.upstream_commit = object.value(QStringLiteral("upstream_commit")).toString().toStdString();
    entry.homepage_url = object.value(QStringLiteral("homepage_url")).toString().toStdString();
    entry.repository_url = object.value(QStringLiteral("repository_url")).toString().toStdString();
    entry.support_url = object.value(QStringLiteral("support_url")).toString().toStdString();
    entry.icon_url = object.value(QStringLiteral("icon_url")).toString().toStdString();
    entry.tags = stringVectorFromJson(object.value(QStringLiteral("tags")));
    entry.latest_version = object.value(QStringLiteral("latest_version")).toString().toStdString();
    entry.installed_version = object.value(QStringLiteral("installed_version")).toString().toStdString();
    entry.installed_source_available = object.value(QStringLiteral("installed_source_available")).toBool();
    entry.installed_source = static_cast<ExtensionPackageSource>(
        object.value(QStringLiteral("installed_source")).toInt(static_cast<int>(ExtensionPackageSource::User)));
    entry.selected_release_version = object.value(QStringLiteral("selected_release_version")).toString().toStdString();
    entry.release_min_app_version = object.value(QStringLiteral("release_min_app_version")).toString().toStdString();
    entry.release_package_format_version = object.value(QStringLiteral("release_package_format_version")).toInt();
    entry.release_protocol_version = object.value(QStringLiteral("release_protocol_version")).toInt();
    entry.release_sdk_version = object.value(QStringLiteral("release_sdk_version")).toInt();
    entry.release_sdk_backend = object.value(QStringLiteral("release_sdk_backend")).toString().toStdString();
    entry.release_launch_mode = object.value(QStringLiteral("release_launch_mode")).toString().toStdString();
    entry.release_runtime_targets = stringVectorFromJson(object.value(QStringLiteral("release_runtime_targets")));
    entry.release_changelog = object.value(QStringLiteral("release_changelog")).toString().toStdString();
    entry.release_published_at_utc = object.value(QStringLiteral("release_published_at_utc")).toString().toStdString();
    entry.selected_asset_name = object.value(QStringLiteral("selected_asset_name")).toString().toStdString();
    entry.selected_asset_download_url =
        object.value(QStringLiteral("selected_asset_download_url")).toString().toStdString();
    entry.selected_asset_package_hash_sha256 =
        object.value(QStringLiteral("selected_asset_package_hash_sha256")).toString().toStdString();
    entry.selected_asset_runtime_targets =
        stringVectorFromJson(object.value(QStringLiteral("selected_asset_runtime_targets")));
    entry.selected_asset_signature_state = static_cast<ExtensionPackageSignatureState>(
        object.value(QStringLiteral("selected_asset_signature_state"))
            .toInt(static_cast<int>(ExtensionPackageSignatureState::Unknown)));
    entry.selected_asset_signature_detail =
        object.value(QStringLiteral("selected_asset_signature_detail")).toString().toStdString();
    entry.selected_asset_signature_algorithm =
        object.value(QStringLiteral("selected_asset_signature_algorithm")).toString().toStdString();
    entry.selected_asset_signature_key_id =
        object.value(QStringLiteral("selected_asset_signature_key_id")).toString().toStdString();
    entry.selected_asset_signature = object.value(QStringLiteral("selected_asset_signature")).toString().toStdString();
    entry.runtime_compatible = object.value(QStringLiteral("runtime_compatible")).toBool();
    entry.compatibility_diagnostic = object.value(QStringLiteral("compatibility_diagnostic")).toString().toStdString();
    entry.install_state = static_cast<ExtensionRepositoryInstallState>(
        object.value(QStringLiteral("install_state"))
            .toInt(static_cast<int>(ExtensionRepositoryInstallState::NotInstalled)));
    entry.installed_status_available = object.value(QStringLiteral("installed_status_available")).toBool();
    entry.installed_load_status = static_cast<an::ui::extensions::ExtensionLoadStatus>(
        object.value(QStringLiteral("installed_load_status"))
            .toInt(static_cast<int>(an::ui::extensions::ExtensionLoadStatus::Invalid)));
    entry.installed_environment_available = object.value(QStringLiteral("installed_environment_available")).toBool();
    entry.installed_environment_state = static_cast<an::ui::extensions::ExtensionEnvironmentState>(
        object.value(QStringLiteral("installed_environment_state"))
            .toInt(static_cast<int>(an::ui::extensions::ExtensionEnvironmentState::Missing)));
    entry.installed_status_diagnostic =
        object.value(QStringLiteral("installed_status_diagnostic")).toString().toStdString();
    entry.installed_environment_diagnostic =
        object.value(QStringLiteral("installed_environment_diagnostic")).toString().toStdString();
    return entry;
}

std::filesystem::path repositorySnapshotCachePath()
{
    const QString override_dir = qEnvironmentVariable(kRepositorySnapshotCacheDirectoryEnv).trimmed();
    if (!override_dir.isEmpty())
    {
        return std::filesystem::path(override_dir.toStdString()) / kRepositorySnapshotCacheFileName;
    }

    QString cache_root = an::ui::applicationCachePath();
    if (cache_root.isEmpty())
    {
        return {};
    }

    return std::filesystem::path(cache_root.toStdString()) / "extension_manager" / kRepositorySnapshotCacheFileName;
}

std::optional<ExtensionManagerPageRepositorySnapshot> loadCachedRepositorySnapshot()
{
    const std::filesystem::path cache_path = repositorySnapshotCachePath();
    if (cache_path.empty())
    {
        return std::nullopt;
    }

    QFile file(QString::fromStdString(cache_path.string()));
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("cache_format_version")).toInt() != kRepositorySnapshotCacheFormatVersion)
    {
        return std::nullopt;
    }

    ExtensionManagerPageRepositorySnapshot snapshot;
    snapshot.success = true;
    snapshot.from_cache = true;
    snapshot.cache_updated_at_utc = root.value(QStringLiteral("updated_at_utc")).toString();
    snapshot.indexUrl = QUrl(root.value(QStringLiteral("index_url")).toString());
    snapshot.environment_storage_available = root.value(QStringLiteral("environment_storage_available")).toBool(false);
    snapshot.environment_total_bytes = root.value(QStringLiteral("environment_total_bytes")).toString().toULongLong();
    snapshot.environment_active_bytes = root.value(QStringLiteral("environment_active_bytes")).toString().toULongLong();
    snapshot.environment_reclaimable_bytes =
        root.value(QStringLiteral("environment_reclaimable_bytes")).toString().toULongLong();
    snapshot.environment_total_count =
        static_cast<std::size_t>(root.value(QStringLiteral("environment_total_count")).toInteger());
    snapshot.environment_reclaimable_count =
        static_cast<std::size_t>(root.value(QStringLiteral("environment_reclaimable_count")).toInteger());

    const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
    snapshot.entries.reserve(static_cast<std::size_t>(entries.size()));
    for (const QJsonValue& entry_value : entries)
    {
        if (std::optional<ExtensionRepositoryBrowseEntry> entry = repositoryEntryFromCacheJson(entry_value))
        {
            snapshot.entries.push_back(std::move(*entry));
        }
    }

    return snapshot;
}

bool saveRepositorySnapshotCache(const ExtensionManagerPageRepositorySnapshot& snapshot)
{
    if (!snapshot.success || snapshot.network_features_disabled || snapshot.from_cache)
    {
        return false;
    }

    const std::filesystem::path cache_path = repositorySnapshotCachePath();
    if (cache_path.empty())
    {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(cache_path.parent_path(), ec);
    if (ec)
    {
        logExtensionManagerPageIssue(
            "repository cache",
            QObject::tr("Could not create repository cache directory: %1").arg(QString::fromStdString(ec.message())),
            false);
        return false;
    }

    QJsonArray entries;
    for (const auto& entry : snapshot.entries)
    {
        entries.push_back(repositoryEntryToCacheJson(entry));
    }

    const QString updated_at_utc = snapshot.cache_updated_at_utc.isEmpty()
                                       ? QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                                       : snapshot.cache_updated_at_utc;
    const QJsonObject root{
        {QStringLiteral("cache_format_version"), kRepositorySnapshotCacheFormatVersion},
        {QStringLiteral("updated_at_utc"), updated_at_utc},
        {QStringLiteral("index_url"), snapshot.indexUrl.toString()},
        {QStringLiteral("environment_storage_available"), snapshot.environment_storage_available},
        {QStringLiteral("environment_total_bytes"),
         QString::number(static_cast<qulonglong>(snapshot.environment_total_bytes))},
        {QStringLiteral("environment_active_bytes"),
         QString::number(static_cast<qulonglong>(snapshot.environment_active_bytes))},
        {QStringLiteral("environment_reclaimable_bytes"),
         QString::number(static_cast<qulonglong>(snapshot.environment_reclaimable_bytes))},
        {QStringLiteral("environment_total_count"), static_cast<qint64>(snapshot.environment_total_count)},
        {QStringLiteral("environment_reclaimable_count"), static_cast<qint64>(snapshot.environment_reclaimable_count)},
        {QStringLiteral("entries"), entries}};

    QSaveFile file(QString::fromStdString(cache_path.string()));
    if (!file.open(QIODevice::WriteOnly))
    {
        logExtensionManagerPageIssue("repository cache", QObject::tr("Could not open repository cache for writing."),
                                     false);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
    {
        logExtensionManagerPageIssue("repository cache", QObject::tr("Could not commit repository cache update."),
                                     false);
        return false;
    }

    return true;
}

QString cachedSnapshotSourceText(const ExtensionManagerPageRepositorySnapshot& snapshot)
{
    if (!snapshot.from_cache)
    {
        return {};
    }

    QString text = QObject::tr(" from the cached repository snapshot");
    if (!snapshot.cache_updated_at_utc.isEmpty())
    {
        text += QObject::tr(" saved %1").arg(snapshot.cache_updated_at_utc);
    }
    return text;
}

bool removeActionAvailable(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.installed_version.empty())
    {
        return false;
    }
    if (!entry.installed_source_available)
    {
        return true;
    }

    return entry.installed_source == ExtensionPackageSource::Repository ||
           entry.installed_source == ExtensionPackageSource::User;
}

QString removeActionTooltipText(const ExtensionRepositoryBrowseEntry& entry)
{
    const QString extension_name = extensionDisplayNameText(entry);
    if (entry.installed_version.empty())
    {
        return QObject::tr("%1 is not installed, so there is nothing to uninstall.").arg(extension_name);
    }
    if (entry.installed_source_available)
    {
        switch (entry.installed_source)
        {
        case ExtensionPackageSource::Bundled:
            return QObject::tr("%1 is bundled with AfterNight and cannot be uninstalled here.").arg(extension_name);
        case ExtensionPackageSource::LocalPackage:
            return QObject::tr("%1 was installed from a local package and cannot be uninstalled from this "
                               "repository entry.")
                .arg(extension_name);
        case ExtensionPackageSource::Repository:
            return QObject::tr("Uninstall %1 from the repository extension directory.").arg(extension_name);
        case ExtensionPackageSource::User:
            return QObject::tr("Uninstall %1 from the user extension directory.").arg(extension_name);
        }
    }

    return QObject::tr("Uninstall %1 from disk.").arg(extension_name);
}

QMargins flushMargins()
{
    return {};
}

void applyFlushMargins(QLayout* layout)
{
    if (!layout)
    {
        return;
    }

    layout->setContentsMargins(flushMargins());
}

QString buttonRoleForPrimaryAction(const ExtensionRepositoryInstallState state)
{
    if (state == ExtensionRepositoryInstallState::NotInstalled ||
        state == ExtensionRepositoryInstallState::UpdateAvailable)
    {
        return QStringLiteral("primary");
    }

    return QStringLiteral("secondary");
}

void applyButtonRole(QPushButton* button, const QString& role)
{
    if (!button)
    {
        return;
    }

    button->setProperty("anButtonVariant", role);
    button->setProperty("anProcessButtonRole", role);
}

QString extensionStatusPageTitle()
{
    return QCoreApplication::translate("ExtensionStatusPage", "Extensions");
}

QString versionText(const ExtensionRepositoryBrowseEntry& entry)
{
    QStringList fragments;
    if (!entry.latest_version.empty())
    {
        fragments.push_back(QObject::tr("Latest: %1").arg(QString::fromStdString(entry.latest_version)));
    }
    if (!entry.installed_version.empty())
    {
        fragments.push_back(QObject::tr("Installed: %1").arg(QString::fromStdString(entry.installed_version)));
    }
    if (!entry.selected_release_version.empty() && entry.selected_release_version != entry.latest_version)
    {
        fragments.push_back(QObject::tr("Selected: %1").arg(QString::fromStdString(entry.selected_release_version)));
    }

    return fragments.isEmpty() ? QObject::tr("Version unavailable") : fragments.join(QStringLiteral(" | "));
}

QString tagsText(const std::vector<std::string>& tags)
{
    if (tags.empty())
    {
        return QObject::tr("Not declared");
    }

    QStringList values;
    values.reserve(static_cast<int>(tags.size()));
    for (const auto& tag : tags)
    {
        values.push_back(QString::fromStdString(tag));
    }
    return values.join(QStringLiteral(", "));
}

QString releaseSdkText(const ExtensionRepositoryBrowseEntry& entry)
{
    QStringList fragments;
    if (entry.release_package_format_version > 0)
    {
        fragments.push_back(QObject::tr("Package %1").arg(entry.release_package_format_version));
    }
    if (entry.release_protocol_version > 0)
    {
        fragments.push_back(QObject::tr("Protocol %1").arg(entry.release_protocol_version));
    }
    if (entry.release_sdk_version > 0)
    {
        fragments.push_back(QObject::tr("SDK %1").arg(entry.release_sdk_version));
    }
    if (!entry.release_sdk_backend.empty())
    {
        fragments.push_back(QString::fromStdString(entry.release_sdk_backend));
    }

    return fragments.isEmpty() ? QObject::tr("Not declared") : fragments.join(QStringLiteral(" | "));
}

QString launchModeText(const std::string& launch_mode)
{
    if (launch_mode == "single_image")
    {
        return QObject::tr("Single image");
    }
    if (launch_mode == "workflow")
    {
        return QObject::tr("Workflow");
    }

    return metadataText(launch_mode);
}

QString assetText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.selected_asset_name.empty())
    {
        return QObject::tr("No compatible asset selected");
    }

    return QString::fromStdString(entry.selected_asset_name);
}

QString compatibilityText(const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.runtime_compatible)
    {
        return QObject::tr("Compatible with this AfterNight runtime");
    }
    if (!entry.compatibility_diagnostic.empty())
    {
        return QString::fromStdString(entry.compatibility_diagnostic);
    }

    return QObject::tr("Not compatible with this AfterNight runtime");
}

void addMetadataSectionRow(QGridLayout* layout, const int row, const QString& title_text, const QString& object_name)
{
    if (!layout)
    {
        return;
    }

    auto* title = new QLabel(title_text, layout->parentWidget());
    title->setObjectName(object_name);
    QFont title_font = an::ui::tokens().typography.caption;
    title_font.setWeight(QFont::DemiBold);
    title->setFont(title_font);
    title->setProperty("anStatusTone", QStringLiteral("secondary"));
    layout->addWidget(title, row, 0, 1, 4);
}

void addMetadataRow(QGridLayout* layout, const int row, const int col, const QString& label_text,
                    const QString& value_text, const QString& object_name, const bool selectable = false,
                    const bool link_urls = false)
{
    if (!layout)
    {
        return;
    }

    auto* label = new QLabel(label_text, layout->parentWidget());
    label->setWordWrap(true);
    label->setProperty("anStatusTone", QStringLiteral("secondary"));
    layout->addWidget(label, row, col * 2, Qt::AlignTop);

    auto* value = new QLabel(value_text, layout->parentWidget());
    value->setObjectName(object_name);
    value->setWordWrap(true);
    if (link_urls)
    {
        const QString rich_text = linkifiedUrlText(value_text);
        if (!rich_text.isEmpty())
        {
            value->setText(rich_text);
            value->setTextFormat(Qt::RichText);
            value->setOpenExternalLinks(true);
            value->setTextInteractionFlags(Qt::TextBrowserInteraction);
        }
    }
    if (selectable)
    {
        value->setTextInteractionFlags(value->textFormat() == Qt::RichText ? Qt::TextBrowserInteraction
                                                                           : Qt::TextSelectableByMouse);
    }
    layout->addWidget(value, row, col * 2 + 1);
}

void addMetadataBadgeRow(QGridLayout* layout, const int row, const int col, const QString& label_text,
                         const QString& value_text, const an::ui::AnStatusBadge::Status status,
                         const QString& object_name)
{
    if (!layout)
    {
        return;
    }

    auto* label = new QLabel(label_text, layout->parentWidget());
    label->setWordWrap(true);
    label->setProperty("anStatusTone", QStringLiteral("secondary"));
    layout->addWidget(label, row, col * 2, Qt::AlignTop);

    auto* badge = new an::ui::AnStatusBadge(value_text, status, layout->parentWidget());
    badge->setObjectName(object_name);
    layout->addWidget(badge, row, col * 2 + 1, Qt::AlignLeft | Qt::AlignTop);
}

ExtensionDiscoveryOptions buildDefaultDiscoveryOptions()
{
    return an::ui::extensions::default_extension_discovery_options();
}

ExtensionManagerPageRepositorySnapshot defaultRepositorySnapshot(const ExtensionDiscoveryOptions& discovery_options)
{
    ExtensionManagerPageRepositorySnapshot snapshot;
    logExtensionManagerPageDebug("repository refresh", QObject::tr("Discovering installed extensions"));
    ExtensionManager manager;
    std::vector<ExtensionLoadResult> installed = manager.discoverExtensions(discovery_options);
    ExtensionEnvironmentManager environment_manager;
    const auto storage = environment_manager.summarizeEnvironmentStorage(installed, discovery_options.context, {},
                                                                         discovery_options.cancellation_token);
    if (!discovery_options.cancellation_token.isCancellationRequested())
    {
        snapshot.environment_storage_available = true;
        snapshot.environment_total_bytes = storage.total_bytes;
        snapshot.environment_active_bytes = storage.active_bytes;
        snapshot.environment_reclaimable_bytes = storage.reclaimable_bytes;
        snapshot.environment_total_count = storage.total_environment_count;
        snapshot.environment_reclaimable_count = storage.reclaimable_environment_count;
    }
    installed.erase(std::remove_if(installed.begin(), installed.end(),
                                   [](const ExtensionLoadResult& result) { return !result.manifest.has_value(); }),
                    installed.end());
    logExtensionManagerPageDebug(
        "repository refresh",
        QObject::tr("Discovered %1 installed extension manifest(s)").arg(static_cast<int>(installed.size())));

    if (an::ui::extensions::extensionNetworkDisabledByPolicy())
    {
        logExtensionManagerPageIssue("repository refresh",
                                     QObject::tr("Repository network features disabled by policy"), false);
        snapshot.network_features_disabled = true;
        snapshot.diagnostic = QObject::tr("Repository network features are disabled by policy.");
        return snapshot;
    }

    ExtensionRepositoryIndexFetchRequest request;
    request.timeout_ms = 10000;
    logExtensionManagerPageDebug(
        "repository refresh", QObject::tr("Fetching official repository index timeout=%1ms").arg(request.timeout_ms));

    ExtensionRepositoryClient client;
    const auto fetch = client.fetchIndex(request);
    snapshot.success = fetch.success;
    snapshot.diagnostic = QString::fromStdString(fetch.diagnostic);
    snapshot.indexUrl = fetch.index_url;
    if (!fetch.success)
    {
        logExtensionManagerPageIssue("repository refresh", snapshot.diagnostic, false);
        return snapshot;
    }

    ExtensionRepositoryBrowseOptions browse_options;
    browse_options.context = discovery_options.context;
    snapshot.entries = an::ui::extensions::build_extension_repository_browse_entries(fetch.validation.index, installed,
                                                                                     browse_options);
    const int update_count = static_cast<int>(std::count_if(
        snapshot.entries.begin(), snapshot.entries.end(), [](const ExtensionRepositoryBrowseEntry& entry) {
            return entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable;
        }));
    logExtensionManagerPageInfo("repository refresh",
                                QObject::tr("Repository refresh loaded %1 extension(s), %2 update(s) available from %3")
                                    .arg(static_cast<int>(snapshot.entries.size()))
                                    .arg(update_count)
                                    .arg(snapshot.indexUrl.toString()));
    return snapshot;
}

void reportOperationProgress(const ExtensionOperationProgressCallback& callback, std::string phase_id,
                             const QString& message, const int current = 0, const int total = 0,
                             const bool indeterminate = true)
{
    if (!callback)
    {
        return;
    }

    callback({std::move(phase_id), message.toStdString(), current, total, indeterminate});
}

bool cancellationRequested(const ExtensionOperationCancellationToken& cancellation_token)
{
    return cancellation_token.isCancellationRequested();
}

void markInstallCancelled(ExtensionManagerPageInstallResult* result)
{
    if (!result)
    {
        return;
    }

    result->cancelled = true;
    result->diagnostic = QObject::tr("Extension package installation cancelled.");
}

void markRemoveCancelled(ExtensionManagerPageRemoveResult* result)
{
    if (!result)
    {
        return;
    }

    result->cancelled = true;
    result->diagnostic = QObject::tr("Extension package removal cancelled.");
}

void markCleanupCancelled(ExtensionManagerPageEnvironmentCleanupResult* result)
{
    if (!result)
    {
        return;
    }

    result->cancelled = true;
    result->diagnostic = QObject::tr("Unused extension environment cleanup cancelled.");
}

bool copyFileToPath(const QString& source_path, const QString& destination_path,
                    const ExtensionOperationProgressCallback& progress_callback,
                    const ExtensionOperationCancellationToken& cancellation_token, QString* diagnostic)
{
    if (cancellationRequested(cancellation_token))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Extension package installation cancelled.");
        }
        return false;
    }

    QFile source(source_path);
    if (!source.open(QIODevice::ReadOnly))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Failed to open package asset: %1").arg(source.errorString());
        }
        return false;
    }

    QFile destination(destination_path);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Failed to create temporary package archive: %1").arg(destination.errorString());
        }
        return false;
    }

    const qint64 total_bytes = source.size();
    reportOperationProgress(progress_callback, "download", QObject::tr("Copying package asset"), 0, 100, false);
    while (!source.atEnd())
    {
        if (cancellationRequested(cancellation_token))
        {
            destination.close();
            QFile::remove(destination_path);
            if (diagnostic)
            {
                *diagnostic = QObject::tr("Extension package installation cancelled.");
            }
            return false;
        }

        const QByteArray chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFile::NoError)
        {
            if (diagnostic)
            {
                *diagnostic = QObject::tr("Failed to read package asset: %1").arg(source.errorString());
            }
            return false;
        }
        if (destination.write(chunk) != chunk.size())
        {
            if (diagnostic)
            {
                *diagnostic =
                    QObject::tr("Failed to write temporary package archive: %1").arg(destination.errorString());
            }
            return false;
        }

        if (total_bytes > 0)
        {
            const int percent = static_cast<int>((source.pos() * 100) / total_bytes);
            reportOperationProgress(progress_callback, "download", QObject::tr("Copying package asset"), percent, 100,
                                    false);
        }
    }

    reportOperationProgress(progress_callback, "download", QObject::tr("Package asset copied"), 100, 100, false);
    return true;
}

bool downloadHttpAssetToPath(const QUrl& asset_url, const QString& destination_path,
                             const ExtensionOperationProgressCallback& progress_callback,
                             const ExtensionOperationCancellationToken& cancellation_token, QString* diagnostic)
{
    if (cancellationRequested(cancellation_token))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Extension package installation cancelled.");
        }
        return false;
    }

    QNetworkAccessManager network_manager;
    QNetworkRequest network_request(asset_url);
    network_request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AfterNight Extension Manager"));
    network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = network_manager.get(network_request);
    QEventLoop loop;
    QTimer timer;
    QTimer cancellation_timer;
    timer.setSingleShot(true);
    cancellation_timer.setInterval(100);
    reportOperationProgress(progress_callback, "download", QObject::tr("Downloading package asset"), 0, 0, true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [progress_callback](qint64 received, qint64 total) {
                         if (total <= 0)
                         {
                             reportOperationProgress(progress_callback, "download",
                                                     QObject::tr("Downloading package asset"), 0, 0, true);
                             return;
                         }

                         const int percent = static_cast<int>((received * 100) / total);
                         reportOperationProgress(progress_callback, "download",
                                                 QObject::tr("Downloading package asset"), percent, 100, false);
                     });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&cancellation_timer, &QTimer::timeout, &loop, [&]() {
        if (!cancellationRequested(cancellation_token))
        {
            return;
        }

        if (reply->isRunning())
        {
            reply->abort();
        }
        loop.quit();
    });
    timer.start(120000);
    cancellation_timer.start();
    loop.exec();
    cancellation_timer.stop();

    if (cancellationRequested(cancellation_token))
    {
        if (reply->isRunning())
        {
            reply->abort();
        }
        reply->deleteLater();
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Extension package installation cancelled.");
        }
        return false;
    }

    if (!timer.isActive())
    {
        if (reply->isRunning())
        {
            reply->abort();
        }
        reply->deleteLater();
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Package asset download timed out.");
        }
        return false;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError)
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Package asset download failed: %1").arg(reply->errorString());
        }
        reply->deleteLater();
        return false;
    }

    const QVariant status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (status_code.isValid() && status_code.toInt() >= 400)
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Package asset download failed with HTTP status %1").arg(status_code.toInt());
        }
        reply->deleteLater();
        return false;
    }

    QFile destination(destination_path);
    if (cancellationRequested(cancellation_token))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Extension package installation cancelled.");
        }
        reply->deleteLater();
        return false;
    }
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Failed to create temporary package archive: %1").arg(destination.errorString());
        }
        reply->deleteLater();
        return false;
    }

    const QByteArray payload = reply->readAll();
    reply->deleteLater();
    if (destination.write(payload) != payload.size())
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Failed to write temporary package archive: %1").arg(destination.errorString());
        }
        return false;
    }

    reportOperationProgress(progress_callback, "download", QObject::tr("Package asset downloaded"), 100, 100, false);
    return true;
}

bool fetchAssetToPath(const QUrl& asset_url, const QString& destination_path,
                      const ExtensionOperationProgressCallback& progress_callback,
                      const ExtensionOperationCancellationToken& cancellation_token, QString* diagnostic)
{
    const QString scheme = asset_url.scheme().toLower();
    if (asset_url.isLocalFile())
    {
        return copyFileToPath(asset_url.toLocalFile(), destination_path, progress_callback, cancellation_token,
                              diagnostic);
    }
    if (scheme.isEmpty())
    {
        return copyFileToPath(asset_url.toString(), destination_path, progress_callback, cancellation_token,
                              diagnostic);
    }
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
    {
        return downloadHttpAssetToPath(asset_url, destination_path, progress_callback, cancellation_token, diagnostic);
    }

    if (diagnostic)
    {
        *diagnostic = QObject::tr("Unsupported package asset URL scheme: %1").arg(scheme);
    }
    return false;
}

ExtensionManagerPageInstallResult defaultInstallProvider(const ExtensionManagerPageInstallRequest& request)
{
    ExtensionManagerPageInstallResult result;
    logExtensionManagerPageInfo("repository install requested",
                                QObject::tr("id=%1 version=%2 index=%3 asset=%4")
                                    .arg(QString::fromStdString(request.entry.id),
                                         QString::fromStdString(request.entry.selected_release_version.empty()
                                                                    ? request.entry.latest_version
                                                                    : request.entry.selected_release_version),
                                         request.indexUrl.toString(), request.assetUrl.toString()));
    reportOperationProgress(request.progress_callback, "prepare", QObject::tr("Preparing extension package install"), 0,
                            0, true);
    if (cancellationRequested(request.cancellation_token))
    {
        markInstallCancelled(&result);
        return result;
    }
    if (request.entry.selected_asset_download_url.empty())
    {
        result.diagnostic = QObject::tr("No compatible package asset is available for this extension.");
        return result;
    }
    if (request.entry.selected_asset_package_hash_sha256.empty())
    {
        result.diagnostic = QObject::tr("Package asset is missing the required SHA-256 hash.");
        return result;
    }

    QTemporaryDir temp_dir;
    if (!temp_dir.isValid())
    {
        result.diagnostic = QObject::tr("Failed to create a temporary package download directory.");
        return result;
    }

    QString archive_name = QFileInfo(QString::fromStdString(request.entry.selected_asset_name)).fileName();
    if (archive_name.isEmpty())
    {
        archive_name = QFileInfo(request.assetUrl.path()).fileName();
    }
    if (archive_name.isEmpty())
    {
        archive_name = QStringLiteral("package.tar.zst");
    }
    const QString archive_path = QDir(temp_dir.path()).filePath(archive_name);
    QString download_diagnostic;
    if (!fetchAssetToPath(request.assetUrl, archive_path, request.progress_callback, request.cancellation_token,
                          &download_diagnostic))
    {
        if (cancellationRequested(request.cancellation_token))
        {
            markInstallCancelled(&result);
            return result;
        }
        result.diagnostic = download_diagnostic;
        return result;
    }
    if (cancellationRequested(request.cancellation_token))
    {
        markInstallCancelled(&result);
        return result;
    }

    ExtensionPackageInstallRequest transaction_request;
    transaction_request.archive_path = std::filesystem::path(archive_path.toStdString());
    transaction_request.install_root =
        request.installRoot.empty() ? an::ui::extensions::repository_extensions_root() : request.installRoot;
    transaction_request.context = request.context;
    transaction_request.source = ExtensionPackageSource::Repository;
    transaction_request.expected_archive_hash_sha256 = request.entry.selected_asset_package_hash_sha256;
    transaction_request.expected_extension_id = request.entry.id;
    transaction_request.expected_extension_version = request.entry.latest_version;
    transaction_request.source_repository_url = request.indexUrl.toString().toStdString();
    transaction_request.selected_release_asset = request.entry.selected_asset_name;
    transaction_request.selected_asset_runtime_targets = request.entry.selected_asset_runtime_targets;
    transaction_request.signature_state = request.entry.selected_asset_signature_state;
    transaction_request.signature_detail = request.entry.selected_asset_signature_detail;
    transaction_request.signature_algorithm = request.entry.selected_asset_signature_algorithm;
    transaction_request.signature_key_id = request.entry.selected_asset_signature_key_id;
    transaction_request.signature = request.entry.selected_asset_signature;
    transaction_request.require_repository_metadata = true;
    transaction_request.require_current_runtime_compatibility = true;
    transaction_request.provision_environment = true;
    transaction_request.prime_startup_caches = true;
    transaction_request.require_settings_schema_cache = true;
    transaction_request.precompile_python_bytecode = true;
    transaction_request.replace_existing = true;
    transaction_request.progress_callback = request.progress_callback;
    transaction_request.cancellation_token = request.cancellation_token;

    ExtensionPackageTransactionService service;
    result.transaction = service.installArchive(transaction_request);
    result.cancelled = result.transaction.cancelled;
    result.success = result.transaction.success;
    if (!result.success)
    {
        result.diagnostic = QString::fromStdString(result.transaction.diagnostic);
        return result;
    }

    const QString action = request.entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable
                               ? QObject::tr("Updated")
                               : QObject::tr("Installed");
    result.diagnostic = QObject::tr("%1 %2 %3.")
                            .arg(action, QString::fromStdString(request.entry.name),
                                 QString::fromStdString(request.entry.selected_release_version.empty()
                                                            ? request.entry.latest_version
                                                            : request.entry.selected_release_version));
    logExtensionManagerPageInfo("repository install complete", result.diagnostic);
    return result;
}

ExtensionManagerPageInstallResult defaultLocalInstallProvider(const ExtensionManagerPageLocalInstallRequest& request)
{
    ExtensionManagerPageInstallResult result;
    logExtensionManagerPageInfo(
        "local package install requested",
        QObject::tr("archive=%1 install_root=%2")
            .arg(QString::fromStdString(request.archivePath.string()),
                 QString::fromStdString(
                     (request.installRoot.empty() ? an::ui::extensions::user_extensions_root() : request.installRoot)
                         .string())));
    reportOperationProgress(request.progress_callback, "prepare", QObject::tr("Preparing local extension package"), 0,
                            0, true);
    if (cancellationRequested(request.cancellation_token))
    {
        markInstallCancelled(&result);
        return result;
    }

    const std::filesystem::path install_root =
        request.installRoot.empty() ? an::ui::extensions::user_extensions_root() : request.installRoot;
    ExtensionPackageInstallRequest transaction_request;
    transaction_request.archive_path = request.archivePath;
    transaction_request.install_root = install_root;
    transaction_request.context = request.context;
    transaction_request.source = ExtensionPackageSource::User;
    transaction_request.selected_release_asset = request.archivePath.filename().string();
    transaction_request.signature_state = ExtensionPackageSignatureState::Unsigned;
    transaction_request.require_repository_metadata = false;
    transaction_request.require_current_runtime_compatibility = true;
    transaction_request.provision_environment = true;
    transaction_request.prime_startup_caches = true;
    transaction_request.require_settings_schema_cache = true;
    transaction_request.precompile_python_bytecode = true;
    transaction_request.replace_existing = true;
    transaction_request.progress_callback = request.progress_callback;
    transaction_request.cancellation_token = request.cancellation_token;

    ExtensionPackageTransactionService service;
    result.transaction = service.installArchive(transaction_request);
    result.cancelled = result.transaction.cancelled;
    result.success = result.transaction.success;
    if (!result.success)
    {
        result.diagnostic = QString::fromStdString(result.transaction.diagnostic);
        return result;
    }

    QString package_name = QObject::tr("extension package");
    QString version;
    if (result.transaction.validation.load_result.manifest.has_value())
    {
        const auto& manifest = *result.transaction.validation.load_result.manifest;
        package_name = QString::fromStdString(manifest.name.empty() ? manifest.id : manifest.name);
        version = QString::fromStdString(manifest.version);
    }

    result.diagnostic = version.isEmpty() ? QObject::tr("Installed local package %1.").arg(package_name)
                                          : QObject::tr("Installed local package %1 %2.").arg(package_name, version);
    logExtensionManagerPageInfo("local package install complete", result.diagnostic);
    return result;
}

QString installReadinessDiagnostic(const ExtensionManagerPageInstallResult& result)
{
    if (!result.transaction.package_bytecode_precompiled)
    {
        return QObject::tr("Extension Python bytecode was not precompiled after installation.");
    }
    if (!result.transaction.startup_cache_priming.has_value())
    {
        return QObject::tr("Extension startup-cache priming was not reported, so the package cannot be marked ready.");
    }

    const auto& priming = *result.transaction.startup_cache_priming;
    if (!priming.success)
    {
        return QString::fromStdString(priming.diagnostic.empty() ? "Extension startup-cache priming failed."
                                                                 : priming.diagnostic);
    }
    if (!priming.environment_ready)
    {
        return QObject::tr("Extension environment readiness was not verified after installation.");
    }
    if (!priming.settings_schema_ready)
    {
        return QObject::tr("Extension settings schema cache was not primed after installation.");
    }

    return {};
}

QString installSuccessMessage(const ExtensionManagerPageInstallResult& result)
{
    QString message =
        result.diagnostic.isEmpty() ? QObject::tr("Extension package installed successfully.") : result.diagnostic;
    if (!message.endsWith(QChar::fromLatin1('.')))
    {
        message.append(QChar::fromLatin1('.'));
    }

    const auto& priming = *result.transaction.startup_cache_priming;
    message.append(QChar::fromLatin1(' '));
    message.append(priming.settings_schema_from_cache ? QObject::tr("Startup caches verified from cache.")
                                                      : QObject::tr("Startup caches primed."));
    return message;
}

ExtensionManagerPageRemoveResult defaultRemoveProvider(const ExtensionManagerPageRemoveRequest& request)
{
    ExtensionManagerPageRemoveResult result;
    logExtensionManagerPageInfo(
        "uninstall requested",
        QObject::tr("id=%1 install_root=%2")
            .arg(QString::fromStdString(request.entry.id),
                 QString::fromStdString((request.installRoot.empty() ? an::ui::extensions::repository_extensions_root()
                                                                     : request.installRoot)
                                            .string())));
    reportOperationProgress(request.progress_callback, "remove", QObject::tr("Preparing extension package removal"), 0,
                            0, true);
    if (cancellationRequested(request.cancellation_token))
    {
        markRemoveCancelled(&result);
        return result;
    }
    if (request.entry.id.empty())
    {
        result.diagnostic = QObject::tr("Extension id is required for package removal.");
        return result;
    }

    ExtensionPackageRemoveRequest transaction_request;
    transaction_request.install_root =
        request.installRoot.empty() ? an::ui::extensions::repository_extensions_root() : request.installRoot;
    transaction_request.extension_id = request.entry.id;
    transaction_request.require_existing = true;
    transaction_request.progress_callback = request.progress_callback;
    transaction_request.cancellation_token = request.cancellation_token;

    ExtensionPackageTransactionService service;
    result.transaction = service.removeInstalledPackage(transaction_request);
    result.cancelled = result.transaction.cancelled;
    result.success = result.transaction.success;
    if (!result.success)
    {
        result.diagnostic = QString::fromStdString(result.transaction.diagnostic);
        return result;
    }

    result.diagnostic =
        QObject::tr("Removed %1.")
            .arg(QString::fromStdString(request.entry.name.empty() ? request.entry.id : request.entry.name));
    logExtensionManagerPageInfo("uninstall complete", result.diagnostic);
    return result;
}

QString environmentCleanupSuccessMessage(const ExtensionManagerPageEnvironmentCleanupResult& result)
{
    const int removed_count = static_cast<int>(result.removed_paths.size());
    if (!result.diagnostic.isEmpty())
    {
        return result.diagnostic;
    }
    if (removed_count == 0)
    {
        return QObject::tr("No unused extension environments were found.");
    }

    if (result.reclaimed_bytes > 0)
    {
        return QObject::tr("Removed %1 unused extension environment(s) and reclaimed %2.")
            .arg(removed_count)
            .arg(formatStorageBytes(result.reclaimed_bytes));
    }

    return QObject::tr("Removed %1 unused extension environment(s).").arg(removed_count);
}

ExtensionManagerPageEnvironmentCleanupResult
defaultEnvironmentCleanupProvider(const ExtensionManagerPageEnvironmentCleanupRequest& request)
{
    ExtensionManagerPageEnvironmentCleanupResult result;

    logExtensionManagerPageInfo(
        "environment cleanup",
        QObject::tr("Starting unused extension environment cleanup root='%1' runtime='%2'")
            .arg(QString::fromStdString(request.discovery_options.context.environments_root.generic_string()),
                 QString::fromStdString(request.discovery_options.context.runtime_target)));
    reportOperationProgress(request.progress_callback, "cleanup_scan", QObject::tr("Scanning installed extensions"), 0,
                            0, true);
    if (cancellationRequested(request.cancellation_token))
    {
        markCleanupCancelled(&result);
        logExtensionManagerPageInfo("environment cleanup", result.diagnostic);
        return result;
    }
    ExtensionManager discovery_manager;
    const std::vector<ExtensionLoadResult> discovered = discovery_manager.discoverExtensions(request.discovery_options);
    logExtensionManagerPageInfo("environment cleanup",
                                QObject::tr("Discovered %1 installed extension package(s) before environment cleanup")
                                    .arg(static_cast<int>(discovered.size())));

    if (cancellationRequested(request.cancellation_token))
    {
        markCleanupCancelled(&result);
        logExtensionManagerPageInfo("environment cleanup", result.diagnostic);
        return result;
    }

    ExtensionEnvironmentManager environment_manager;
    const std::vector<an::ui::extensions::ExtensionEnvironmentInventoryEntry> unused_environments =
        environment_manager.collectUnusedEnvironmentInventory(discovered, request.discovery_options.context, {},
                                                              request.cancellation_token);
    const int orphaned_count = static_cast<int>(unused_environments.size());
    logExtensionManagerPageInfo("environment cleanup",
                                QObject::tr("Found %1 unused extension environment(s) to remove").arg(orphaned_count));

    if (unused_environments.empty())
    {
        result.success = true;
        result.diagnostic = environmentCleanupSuccessMessage(result);
        reportOperationProgress(request.progress_callback, "cleanup_complete", result.diagnostic, 1, 1, false);
        logExtensionManagerPageInfo("environment cleanup", result.diagnostic);
        return result;
    }

    reportOperationProgress(request.progress_callback, "cleanup_remove",
                            QObject::tr("Removing unused extension environments"), 0, orphaned_count, false);
    std::string error;
    for (int index = 0; index < orphaned_count; ++index)
    {
        if (cancellationRequested(request.cancellation_token))
        {
            markCleanupCancelled(&result);
            logExtensionManagerPageInfo(
                "environment cleanup",
                QObject::tr("Cancelled unused extension environment cleanup after removing %1 of %2 environment(s)")
                    .arg(static_cast<int>(result.removed_paths.size()))
                    .arg(orphaned_count));
            return result;
        }

        const auto& environment = unused_environments[static_cast<std::size_t>(index)];
        const std::filesystem::path& path = environment.environment_path;
        const QString path_label =
            QString::fromStdString(path.filename().empty() ? path.generic_string() : path.filename().generic_string());
        reportOperationProgress(request.progress_callback, "cleanup_remove",
                                QObject::tr("Removing unused extension environment %1 of %2: %3")
                                    .arg(index + 1)
                                    .arg(orphaned_count)
                                    .arg(path_label),
                                index, orphaned_count, false);
        logExtensionManagerPageInfo("environment cleanup",
                                    QObject::tr("Removing unused extension environment %1 of %2 path='%3'")
                                        .arg(index + 1)
                                        .arg(orphaned_count)
                                        .arg(QString::fromStdString(path.generic_string())));

        if (!environment_manager.removeEnvironmentDirectory(path, &error))
        {
            result.diagnostic =
                QString::fromStdString(error.empty() ? "Failed to remove unused extension environments." : error);
            logExtensionManagerPageIssue("environment cleanup failed", result.diagnostic, true);
            return result;
        }

        result.removed_paths.push_back(path);
        result.reclaimed_bytes += environment.size_bytes;
        reportOperationProgress(
            request.progress_callback, "cleanup_remove",
            QObject::tr("Removed unused extension environment %1 of %2").arg(index + 1).arg(orphaned_count), index + 1,
            orphaned_count, false);
    }

    result.diagnostic = environmentCleanupSuccessMessage(result);
    result.success = true;
    reportOperationProgress(request.progress_callback, "cleanup_complete", result.diagnostic, orphaned_count,
                            orphaned_count, false);
    logExtensionManagerPageInfo("environment cleanup", result.diagnostic);
    return result;
}

} // anonymous namespace

namespace an::ui
{

ExtensionManagerPage::ExtensionManagerPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    (void)m_Settings;
    setupUi();
}

ExtensionManagerPage::~ExtensionManagerPage()
{
    if (m_OperationInProgress)
    {
        m_ActiveCancellationToken.requestCancellation();
    }
}

QString ExtensionManagerPage::pageTitle() const
{
    return tr("Extension Manager");
}

QIcon ExtensionManagerPage::pageIcon() const
{
    return contextIcon(QStringLiteral("extension"));
}

QStringList ExtensionManagerPage::searchKeywords() const
{
    return {tr("Extension Manager"), tr("Extensions"), tr("Repository"), tr("Install"), tr("Update"),
            tr("Package"),           tr("Official")};
}

void ExtensionManagerPage::loadSettings()
{
    if (!m_RepositoryProvider)
    {
        std::optional<ExtensionManagerPageRepositorySnapshot> cached_snapshot = loadCachedRepositorySnapshot();
        if (cached_snapshot.has_value())
        {
            if (an::ui::extensions::extensionNetworkDisabledByPolicy())
            {
                cached_snapshot->network_features_disabled = true;
                cached_snapshot->diagnostic = tr("Repository network features are disabled by policy.");
            }
            else
            {
                cached_snapshot->background_refresh_in_progress = true;
            }

            logExtensionManagerPageInfo("repository cache",
                                        tr("Loaded cached repository snapshot with %1 extension(s)")
                                            .arg(static_cast<int>(cached_snapshot->entries.size())));
            applyRepositorySnapshot(std::move(*cached_snapshot));

            if (m_LastSnapshot.network_features_disabled)
            {
                clearModified();
                return;
            }

            refreshRepository(true);
            clearModified();
            return;
        }
    }

    refreshRepository();
    clearModified();
}

void ExtensionManagerPage::saveSettings()
{
    if (m_Settings && m_AllowUnsignedExtensionsCheck)
    {
        m_Settings->upsert(kAllowUnsignedExtensionsSetting,
                           unsignedExtensionsAllowed() ? QStringLiteral("1") : QStringLiteral("0"));
    }
    if (m_Settings && m_AutomaticEnvironmentCleanupCheck)
    {
        m_Settings->upsert(kAutomaticEnvironmentCleanupSetting,
                           m_AutomaticEnvironmentCleanupCheck->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    }
    clearModified();
}

void ExtensionManagerPage::resetToDefaults()
{
    setAutomaticEnvironmentCleanupEnabled(true);
    if (m_AllowUnsignedExtensionsCheck)
    {
        setUnsignedExtensionsAllowed(false);
    }
    if (m_SearchEdit && !m_SearchEdit->text().isEmpty())
    {
        const QSignalBlocker blocker(m_SearchEdit);
        m_SearchEdit->clear();
    }
    if (m_FilterCombo && m_FilterCombo->currentIndex() != 0)
    {
        const QSignalBlocker blocker(m_FilterCombo);
        m_FilterCombo->setCurrentIndex(0);
    }
    if (m_SortCombo && m_SortCombo->currentIndex() != 0)
    {
        const QSignalBlocker blocker(m_SortCombo);
        m_SortCombo->setCurrentIndex(0);
    }
    refreshRepository();
    clearModified();
}

void ExtensionManagerPage::refreshStyles()
{
    SettingsPage::refreshStyles();

    if (m_SummaryBanner)
    {
        m_SummaryBanner->refreshTheme();
    }
    if (m_UnsignedExtensionsWarningBanner)
    {
        m_UnsignedExtensionsWarningBanner->refreshTheme();
    }
    for (auto* card : m_Cards)
    {
        if (card)
        {
            card->refreshTheme();
            for (auto* label : card->findChildren<QLabel*>())
            {
                if (auto* badge = dynamic_cast<AnStatusBadge*>(label))
                {
                    badge->refreshTheme();
                }
            }
            for (auto* child : card->findChildren<QWidget*>())
            {
                if (auto* banner = dynamic_cast<AnInfoBanner*>(child))
                {
                    banner->refreshTheme();
                }
            }
        }
    }
}

void ExtensionManagerPage::setRepositoryProvider(RepositoryProvider repositoryProvider)
{
    m_RepositoryProvider = std::move(repositoryProvider);
}

void ExtensionManagerPage::setDiscoveryOptionsProvider(DiscoveryOptionsProvider discoveryOptionsProvider)
{
    m_DiscoveryOptionsProvider = std::move(discoveryOptionsProvider);
}

void ExtensionManagerPage::setInstallProvider(InstallProvider installProvider)
{
    m_InstallProvider = std::move(installProvider);
}

void ExtensionManagerPage::setLocalInstallProvider(LocalInstallProvider localInstallProvider)
{
    m_LocalInstallProvider = std::move(localInstallProvider);
}

void ExtensionManagerPage::setLocalPackagePathProvider(LocalPackagePathProvider localPackagePathProvider)
{
    m_LocalPackagePathProvider = std::move(localPackagePathProvider);
}

void ExtensionManagerPage::setUnsignedInstallConfirmationProvider(
    UnsignedInstallConfirmationProvider confirmationProvider)
{
    m_UnsignedInstallConfirmationProvider = std::move(confirmationProvider);
}

void ExtensionManagerPage::setPendingOperationsCancellationProvider(
    PendingOperationsCancellationProvider confirmationProvider)
{
    m_PendingOperationsCancellationProvider = std::move(confirmationProvider);
}

void ExtensionManagerPage::setRemoveConfirmationProvider(RemoveConfirmationProvider confirmationProvider)
{
    m_RemoveConfirmationProvider = std::move(confirmationProvider);
}

void ExtensionManagerPage::setRemoveProvider(RemoveProvider removeProvider)
{
    m_RemoveProvider = std::move(removeProvider);
}

void ExtensionManagerPage::setEnvironmentCleanupProvider(EnvironmentCleanupProvider cleanupProvider)
{
    m_EnvironmentCleanupProvider = std::move(cleanupProvider);
}

void ExtensionManagerPage::setExtensionSettingsOpener(ExtensionSettingsOpener settingsOpener)
{
    m_ExtensionSettingsOpener = std::move(settingsOpener);
}

void ExtensionManagerPage::setupUi()
{
    const auto& t = tokens();
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setObjectName(QStringLiteral("anExtensionManagerRootLayout"));
    main_layout->setSpacing(t.spacing.lg);
    applyFlushMargins(main_layout);

    m_SummaryBanner = new AnInfoBanner({}, AnInfoBanner::Tone::Info, this);
    m_SummaryBanner->setObjectName(QStringLiteral("anExtensionManagerSummaryBanner"));
    main_layout->addWidget(m_SummaryBanner);

    m_EnvironmentStorageLabel = new QLabel(this);
    m_EnvironmentStorageLabel->setObjectName(QStringLiteral("anExtensionManagerEnvironmentStorageLabel"));
    m_EnvironmentStorageLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    m_EnvironmentStorageLabel->setWordWrap(true);
    m_EnvironmentStorageLabel->setVisible(false);
    main_layout->addWidget(m_EnvironmentStorageLabel);

    m_ProgressLabel = new QLabel(this);
    m_ProgressLabel->setObjectName(QStringLiteral("anExtensionManagerProgressLabel"));
    m_ProgressLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    m_ProgressLabel->setVisible(false);
    main_layout->addWidget(m_ProgressLabel);

    m_ProgressBar = new QProgressBar(this);
    m_ProgressBar->setObjectName(QStringLiteral("anExtensionManagerProgressBar"));
    m_ProgressBar->setVisible(false);
    m_ProgressBar->setTextVisible(true);
    main_layout->addWidget(m_ProgressBar);

    m_QueueContainer = new QWidget(this);
    m_QueueContainer->setObjectName(QStringLiteral("anExtensionManagerPendingQueue"));
    m_QueueLayout = new QVBoxLayout(m_QueueContainer);
    m_QueueLayout->setObjectName(QStringLiteral("anExtensionManagerPendingQueueLayout"));
    m_QueueLayout->setSpacing(t.spacing.sm);
    applyFlushMargins(m_QueueLayout);
    m_QueueContainer->setVisible(false);
    main_layout->addWidget(m_QueueContainer);

    auto* actions_layout = new QHBoxLayout();
    actions_layout->setObjectName(QStringLiteral("anExtensionManagerActionsLayout"));
    applyFlushMargins(actions_layout);
    actions_layout->setSpacing(t.spacing.sm);

    m_SearchEdit = new QLineEdit(this);
    m_SearchEdit->setObjectName(QStringLiteral("anExtensionManagerSearchEdit"));
    m_SearchEdit->setPlaceholderText(tr("Search extensions"));
    m_SearchEdit->setToolTip(tr("Filter extensions by name, author, or tags"));
    m_SearchEdit->setClearButtonEnabled(true);
    QObject::connect(m_SearchEdit, &QLineEdit::textChanged, this, [this]() { renderCards(); });
    actions_layout->addWidget(m_SearchEdit, 1);

    m_FilterCombo = new QComboBox(this);
    m_FilterCombo->setObjectName(QStringLiteral("anExtensionManagerFilterCombo"));
    m_FilterCombo->setToolTip(tr("Filter extensions by their installation state or publisher"));
    m_FilterCombo->addItem(tr("All Extensions"));
    m_FilterCombo->addItem(tr("Installed Only"));
    m_FilterCombo->addItem(tr("Updates Available"));
    m_FilterCombo->addItem(tr("Incompatible"));
    m_FilterCombo->addItem(tr("Official Extensions"));
    QObject::connect(m_FilterCombo, &QComboBox::currentIndexChanged, this, [this]() { renderCards(); });
    actions_layout->addWidget(m_FilterCombo);

    m_SortCombo = new QComboBox(this);
    m_SortCombo->setObjectName(QStringLiteral("anExtensionManagerSortCombo"));
    m_SortCombo->setToolTip(tr("Sort the list of extensions"));
    m_SortCombo->addItem(tr("Name (A-Z)"));
    m_SortCombo->addItem(tr("Name (Z-A)"));
    m_SortCombo->addItem(tr("Updates Available First"));
    QObject::connect(m_SortCombo, &QComboBox::currentIndexChanged, this, [this]() { renderCards(); });
    actions_layout->addWidget(m_SortCombo);

    m_RefreshButton = new QPushButton(tr("Refresh"), this);
    m_RefreshButton->setObjectName(QStringLiteral("anExtensionManagerRefreshButton"));
    m_RefreshButton->setToolTip(tr("Refresh the extension list from the repository"));
    applyButtonRole(m_RefreshButton, QStringLiteral("secondary"));
    QObject::connect(m_RefreshButton, &QPushButton::clicked, this, [this]() {
        logExtensionManagerPageInfo("repository refresh", tr("Refresh button requested repository update"));
        refreshRepository();
    });
    actions_layout->addWidget(m_RefreshButton);

    m_MoreActionsButton = new QToolButton(this);
    m_MoreActionsButton->setObjectName(QStringLiteral("anExtensionManagerMoreActionsButton"));
    m_MoreActionsButton->setText(tr("More Actions"));
    m_MoreActionsButton->setToolTip(tr("Additional extension management tools"));
    m_MoreActionsButton->setPopupMode(QToolButton::InstantPopup);
    m_MoreActionsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_MoreActionsButton->setProperty("anButtonVariant", QStringLiteral("secondary"));

    m_MoreActionsMenu = new QMenu(m_MoreActionsButton);
    m_MoreActionsMenu->setObjectName(QStringLiteral("anExtensionManagerMoreActionsMenu"));

    auto* install_local_action = m_MoreActionsMenu->addAction(tr("Install local package..."));
    install_local_action->setToolTip(tr("Install an extension from a local package archive"));
    QObject::connect(install_local_action, &QAction::triggered, this, [this]() { startLocalInstallOperation(); });

    auto* clean_environments_action = m_MoreActionsMenu->addAction(tr("Clean unused environments"));
    clean_environments_action->setToolTip(
        tr("Remove superseded environments with ready replacements and environments for uninstalled extensions"));
    QObject::connect(clean_environments_action, &QAction::triggered, this,
                     [this]() { startEnvironmentCleanupOperation(); });

    m_MoreActionsButton->setMenu(m_MoreActionsMenu);
    actions_layout->addWidget(m_MoreActionsButton);

    m_CancelButton = new QPushButton(tr("Cancel"), this);
    m_CancelButton->setObjectName(QStringLiteral("anExtensionManagerCancelButton"));
    applyButtonRole(m_CancelButton, QStringLiteral("danger"));
    m_CancelButton->setToolTip(tr("Cancel the current extension operation"));
    m_CancelButton->setVisible(false);
    QObject::connect(m_CancelButton, &QPushButton::clicked, this, [this]() { cancelActiveOperation(); });
    actions_layout->addWidget(m_CancelButton);

    main_layout->addLayout(actions_layout);

    m_CardsScrollArea = new QScrollArea(this);
    m_CardsScrollArea->setObjectName(QStringLiteral("anExtensionManagerCardsScrollArea"));
    m_CardsScrollArea->setWidgetResizable(true);
    m_CardsScrollArea->setFrameShape(QFrame::NoFrame);
    m_CardsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_CardsContainer = new QWidget(m_CardsScrollArea);
    m_CardsContainer->setObjectName(QStringLiteral("anExtensionManagerCardsContainer"));
    m_CardsLayout = new QVBoxLayout(m_CardsContainer);
    m_CardsLayout->setObjectName(QStringLiteral("anExtensionManagerCardsLayout"));
    m_CardsLayout->setSpacing(t.spacing.sm);
    applyFlushMargins(m_CardsLayout);
    m_CardsScrollArea->setWidget(m_CardsContainer);
    main_layout->addWidget(m_CardsScrollArea, 1);

    m_AutomaticEnvironmentCleanupCheck = new QCheckBox(tr("Automatically remove unused extension environments"), this);
    m_AutomaticEnvironmentCleanupCheck->setObjectName(
        QStringLiteral("anExtensionManagerAutomaticEnvironmentCleanupCheck"));
    m_AutomaticEnvironmentCleanupCheck->setToolTip(
        tr("At startup, remove unused environments that are at least 24 hours old. Superseded environments are "
           "removed only after a ready replacement exists; environments awaiting repair or actively provisioning "
           "remain protected."));
    const bool automatic_cleanup = !m_Settings || m_Settings->select(kAutomaticEnvironmentCleanupSetting,
                                                                     QStringLiteral("1")) != QStringLiteral("0");
    setAutomaticEnvironmentCleanupEnabled(automatic_cleanup);
    QObject::connect(m_AutomaticEnvironmentCleanupCheck, &QCheckBox::toggled, this, [this](const bool checked) {
        if (m_Settings)
        {
            m_Settings->upsert(kAutomaticEnvironmentCleanupSetting,
                               checked ? QStringLiteral("1") : QStringLiteral("0"));
        }
        clearModified();
    });
    main_layout->addWidget(m_AutomaticEnvironmentCleanupCheck);

    m_UnsignedExtensionsWarningBanner =
        new AnInfoBanner(tr("Unsigned extensions can run code that AfterNight cannot authenticate. Only enable this "
                            "for packages from authors and files you trust."),
                         AnInfoBanner::Tone::Warning, this);
    m_UnsignedExtensionsWarningBanner->setObjectName(QStringLiteral("anExtensionManagerUnsignedWarningBanner"));
    m_UnsignedExtensionsWarningBanner->setVisible(false);
    main_layout->addWidget(m_UnsignedExtensionsWarningBanner);

    m_AllowUnsignedExtensionsCheck = new QCheckBox(tr("Allow unsigned extensions"), this);
    m_AllowUnsignedExtensionsCheck->setObjectName(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck"));
    m_AllowUnsignedExtensionsCheck->setToolTip(
        tr("Unsigned extension packages are blocked by default. Enable this only for packages you trust."));
    const bool allow_unsigned =
        m_Settings && m_Settings->select(kAllowUnsignedExtensionsSetting, QStringLiteral("0")) == QStringLiteral("1");
    setUnsignedExtensionsAllowed(allow_unsigned);
    QObject::connect(m_AllowUnsignedExtensionsCheck, &QCheckBox::toggled, this, [this](const bool checked) {
        if (checked)
        {
            const QString warning =
                tr("Unsigned extensions can run code that AfterNight cannot authenticate. Only allow unsigned "
                   "extensions when you trust the author and the package file. Continue?");
            logExtensionManagerPageIssue("unsigned extension policy warning", warning, false);
            if (!confirmUnsignedInstall(tr("Allow Unsigned Extensions?"), warning))
            {
                logExtensionManagerPageInfo("unsigned extension policy cancelled",
                                            tr("User cancelled enabling unsigned extension installs."));
                setUnsignedExtensionsAllowed(false);
                return;
            }
        }

        if (m_Settings)
        {
            m_Settings->upsert(kAllowUnsignedExtensionsSetting, checked ? QStringLiteral("1") : QStringLiteral("0"));
        }
        updateUnsignedExtensionsWarningBanner();
        clearModified();
    });
    main_layout->addWidget(m_AllowUnsignedExtensionsCheck);
}

void ExtensionManagerPage::refreshRepository(const bool preserveCurrentCards)
{
    logExtensionManagerPageDebug("repository refresh", tr("Repository refresh started"));

    if (m_RepositoryProvider)
    {
        applyRepositorySnapshot(m_RepositoryProvider());
        return;
    }

    if (m_RepositoryRefreshWatcher)
    {
        // The refresh in flight may have read the installed packages before the change that asked for this one, so
        // what it brings back is not shown: it is read again when it lands, as this caller asked for it - keeping
        // the cards it asked to keep, whatever the refresh it waits behind was asked.
        m_RepositoryRefreshWaiting = preserveCurrentCards;
        return;
    }

    m_RepositoryRefreshWaiting.reset();
    m_RepositoryRefreshInProgress = true;
    if (!preserveCurrentCards)
    {
        clearCards();
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Info);
            m_SummaryBanner->setMessage(tr("Loading extension repository..."));
        }
        if (m_CardsLayout)
        {
            m_CardsLayout->addWidget(
                createInfoLabel(tr("Checking repository packages and installed extension state.")));
            m_CardsLayout->addStretch(1);
        }
    }
    setOperationInProgress(m_OperationInProgress);
    refreshStyles();

    const ExtensionDiscoveryOptions discovery_options = buildDiscoveryOptions();
    auto* watcher = new QFutureWatcher<ExtensionManagerPageRepositorySnapshot>(this);
    m_RepositoryRefreshWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<ExtensionManagerPageRepositorySnapshot>::finished, this,
                     [this, watcher, preserveCurrentCards]() {
                         ExtensionManagerPageRepositorySnapshot snapshot = watcher->result();
                         watcher->deleteLater();
                         if (m_RepositoryRefreshWatcher == watcher)
                         {
                             m_RepositoryRefreshWatcher = nullptr;
                         }
                         m_RepositoryRefreshInProgress = false;
                         if (const std::optional<bool> waiting = m_RepositoryRefreshWaiting; waiting.has_value())
                         {
                             refreshRepository(*waiting);
                             return;
                         }
                         if (!snapshot.success && preserveCurrentCards && m_LastSnapshot.from_cache &&
                             !m_LastSnapshot.entries.empty())
                         {
                             m_LastSnapshot.background_refresh_in_progress = false;
                             m_LastSnapshot.diagnostic = snapshot.diagnostic;
                             m_LastSnapshot.network_features_disabled = snapshot.network_features_disabled;
                             logExtensionManagerPageIssue(
                                 "repository refresh",
                                 snapshot.diagnostic.isEmpty()
                                     ? tr("Background repository refresh failed; keeping cached repository snapshot.")
                                     : snapshot.diagnostic,
                                 false);
                             renderCards();
                             return;
                         }
                         applyRepositorySnapshot(std::move(snapshot));
                     });
    watcher->setFuture(
        QtConcurrent::run([discovery_options]() { return defaultRepositorySnapshot(discovery_options); }));
}

void ExtensionManagerPage::applyRepositorySnapshot(ExtensionManagerPageRepositorySnapshot snapshot)
{
    applyInstalledEntryOverrides(snapshot);
    if (snapshot.success && !snapshot.from_cache && !snapshot.network_features_disabled)
    {
        snapshot.cache_updated_at_utc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if (!m_RepositoryProvider && saveRepositorySnapshotCache(snapshot))
        {
            logExtensionManagerPageDebug("repository cache", tr("Repository snapshot cache updated"));
        }
    }
    if (!snapshot.from_cache)
    {
        snapshot.background_refresh_in_progress = false;
    }
    m_LastSnapshot = std::move(snapshot);
    if (!m_LastSnapshot.success)
    {
        logExtensionManagerPageIssue("repository refresh",
                                     m_LastSnapshot.diagnostic.isEmpty()
                                         ? tr("Repository refresh failed without a diagnostic")
                                         : m_LastSnapshot.diagnostic,
                                     false);
    }
    else
    {
        const int update_count = static_cast<int>(
            std::count_if(m_LastSnapshot.entries.begin(), m_LastSnapshot.entries.end(),
                          [](const ExtensionRepositoryBrowseEntry& entry) {
                              return entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable;
                          }));
        logExtensionManagerPageInfo("repository refresh",
                                    tr("Repository refresh completed with %1 extension(s), %2 update(s) available")
                                        .arg(static_cast<int>(m_LastSnapshot.entries.size()))
                                        .arg(update_count));
    }
    renderCards();
}

void ExtensionManagerPage::renderCards()
{
    clearCards();

    if (!m_CardsLayout || !m_SummaryBanner)
    {
        return;
    }

    if (m_EnvironmentStorageLabel)
    {
        m_EnvironmentStorageLabel->setVisible(m_LastSnapshot.environment_storage_available);
        m_EnvironmentStorageLabel->setText(
            m_LastSnapshot.environment_storage_available ? environmentStorageSummaryText(m_LastSnapshot) : QString());
    }

    if (m_RefreshButton)
    {
        m_RefreshButton->setEnabled(!m_OperationInProgress && !m_RepositoryRefreshInProgress &&
                                    !m_LastSnapshot.network_features_disabled);
    }
    if (m_MoreActionsButton)
    {
        m_MoreActionsButton->setEnabled(!m_OperationInProgress);
    }

    if (!m_LastSnapshot.success)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
        const QString diagnostic = m_LastSnapshot.diagnostic.isEmpty()
                                       ? tr("The official extension repository could not be loaded.")
                                       : m_LastSnapshot.diagnostic;
        m_SummaryBanner->setMessage(tr("Extension repository unavailable: %1").arg(diagnostic));
        m_CardsLayout->addWidget(createWarningLabel(diagnostic));
        m_CardsLayout->addStretch(1);
        refreshStyles();
        return;
    }

    const QString search_text = m_SearchEdit ? m_SearchEdit->text().trimmed() : QString();
    const int filter_index = m_FilterCombo ? m_FilterCombo->currentIndex() : 0;
    const int sort_index = m_SortCombo ? m_SortCombo->currentIndex() : 0;

    std::vector<const ExtensionRepositoryBrowseEntry*> visible_entries;
    visible_entries.reserve(m_LastSnapshot.entries.size());
    for (const auto& entry : m_LastSnapshot.entries)
    {
        if (!entryMatchesSearch(entry, search_text))
        {
            continue;
        }

        bool matches_filter = true;
        if (filter_index == 1) // Installed Only
        {
            matches_filter = (entry.install_state == ExtensionRepositoryInstallState::Installed ||
                              entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable);
        }
        else if (filter_index == 2) // Updates Available
        {
            matches_filter = (entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable);
        }
        else if (filter_index == 3) // Incompatible
        {
            matches_filter = (entry.install_state == ExtensionRepositoryInstallState::Incompatible);
        }
        else if (filter_index == 4) // Official Extensions
        {
            matches_filter =
                an::ui::extensions::official_extension_signing_key_trusted(entry.selected_asset_signature_key_id) ||
                entry.publisher_id == "afternight";
        }

        if (matches_filter)
        {
            visible_entries.push_back(&entry);
        }
    }

    std::sort(visible_entries.begin(), visible_entries.end(),
              [sort_index](const ExtensionRepositoryBrowseEntry* a, const ExtensionRepositoryBrowseEntry* b) {
                  if (sort_index == 2) // Updates Available First
                  {
                      const bool a_update = a->install_state == ExtensionRepositoryInstallState::UpdateAvailable;
                      const bool b_update = b->install_state == ExtensionRepositoryInstallState::UpdateAvailable;
                      if (a_update != b_update)
                      {
                          return a_update;
                      }
                  }

                  const QString a_name = extensionDisplayNameText(*a);
                  const QString b_name = extensionDisplayNameText(*b);

                  if (sort_index == 1) // Name (Z-A)
                  {
                      return a_name.compare(b_name, Qt::CaseInsensitive) > 0;
                  }
                  // Name (A-Z)
                  return a_name.compare(b_name, Qt::CaseInsensitive) < 0;
              });

    if (visible_entries.empty())
    {
        if (m_LastSnapshot.network_features_disabled)
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
            m_SummaryBanner->setMessage(tr("Extension repository network features are disabled: %1")
                                            .arg(repositoryNetworkDisabledDiagnostic(m_LastSnapshot)));
            m_CardsLayout->addWidget(createInfoLabel(
                search_text.isEmpty() ? tr("Bundled and locally installed extensions remain available from Extensions.")
                                      : tr("Clear or adjust the search text to show local extension records.")));
        }
        else
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Info);
            QString empty_message = search_text.isEmpty()
                                        ? tr("No extension packages are listed by the official repository.")
                                        : tr("No extension packages match the current search.");
            if (m_LastSnapshot.from_cache && search_text.isEmpty())
            {
                empty_message = tr("No extension packages are listed by the cached repository snapshot.");
                if (m_LastSnapshot.background_refresh_in_progress)
                {
                    empty_message += tr(" Refreshing the repository in the background.");
                }
            }
            m_SummaryBanner->setMessage(empty_message);
            m_CardsLayout->addWidget(createInfoLabel(
                search_text.isEmpty() ? tr("Refresh the official repository after packages are published.")
                                      : tr("Clear or adjust the search text to show more extension packages.")));
        }
        m_CardsLayout->addStretch(1);
        refreshStyles();
        return;
    }

    int update_count = 0;
    int incompatible_count = 0;
    for (const auto* entry : visible_entries)
    {
        if (!entry)
        {
            continue;
        }
        if (entry->install_state == ExtensionRepositoryInstallState::UpdateAvailable)
        {
            ++update_count;
        }
        else if (entry->install_state == ExtensionRepositoryInstallState::Incompatible)
        {
            ++incompatible_count;
        }
    }

    QString summary;
    if (m_LastSnapshot.network_features_disabled)
    {
        const QString source_text = search_text.isEmpty()
                                        ? (m_LastSnapshot.from_cache ? cachedSnapshotSourceText(m_LastSnapshot)
                                                                     : tr(" in the local repository snapshot"))
                                        : tr(" for the current search");
        summary = tr("Extension repository network features are disabled: %1 %2 package(s) shown%3: %4 update(s), "
                     "%5 incompatible.")
                      .arg(repositoryNetworkDisabledDiagnostic(m_LastSnapshot))
                      .arg(static_cast<int>(visible_entries.size()))
                      .arg(source_text)
                      .arg(update_count)
                      .arg(incompatible_count);
    }
    else if (m_LastSnapshot.from_cache)
    {
        const QString source_text =
            search_text.isEmpty() ? cachedSnapshotSourceText(m_LastSnapshot) : tr(" for the current search");
        summary = tr("%1 package(s) available%2: %3 update(s), %4 incompatible.")
                      .arg(static_cast<int>(visible_entries.size()))
                      .arg(source_text)
                      .arg(update_count)
                      .arg(incompatible_count);
        if (m_LastSnapshot.background_refresh_in_progress)
        {
            summary += tr(" Refreshing the repository in the background.");
        }
        else if (!m_LastSnapshot.diagnostic.isEmpty())
        {
            summary += tr(" Last background refresh did not complete: %1").arg(m_LastSnapshot.diagnostic);
        }
    }
    else
    {
        const QString source_text =
            search_text.isEmpty() ? tr(" from the official repository") : tr(" for the current search");
        summary = tr("%1 package(s) available%2: %3 update(s), %4 incompatible.")
                      .arg(static_cast<int>(visible_entries.size()))
                      .arg(source_text)
                      .arg(update_count)
                      .arg(incompatible_count);
    }
    const bool cached_refresh_warning = m_LastSnapshot.from_cache && !m_LastSnapshot.background_refresh_in_progress &&
                                        !m_LastSnapshot.diagnostic.isEmpty();
    m_SummaryBanner->setTone(m_LastSnapshot.network_features_disabled || cached_refresh_warning || update_count > 0 ||
                                     incompatible_count > 0
                                 ? AnInfoBanner::Tone::Warning
                                 : AnInfoBanner::Tone::Info);
    m_SummaryBanner->setMessage(summary);

    for (const auto* entry : visible_entries)
    {
        if (!entry)
        {
            continue;
        }
        if (QWidget* card = buildRepositoryCard(*entry))
        {
            m_CardsLayout->addWidget(card);
        }
    }
    m_CardsLayout->addStretch(1);
    updateActionButtonsForOperationState();
    refreshStyles();
}

void ExtensionManagerPage::clearCards()
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

bool ExtensionManagerPage::unsignedExtensionsAllowed() const
{
    return m_AllowUnsignedExtensionsCheck && m_AllowUnsignedExtensionsCheck->isChecked();
}

bool ExtensionManagerPage::confirmUnsignedInstall(const QString& title, const QString& message)
{
    if (m_UnsignedInstallConfirmationProvider)
    {
        return m_UnsignedInstallConfirmationProvider(this, title, message);
    }

    return QMessageBox::warning(this, title, message, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) ==
           QMessageBox::Ok;
}

bool ExtensionManagerPage::confirmCancelPendingOperations(const int pendingCount)
{
    if (pendingCount <= 0)
    {
        return false;
    }
    if (m_PendingOperationsCancellationProvider)
    {
        return m_PendingOperationsCancellationProvider(this, pendingCount);
    }

    return QMessageBox::question(
               this, tr("Cancel Queued Extension Operations?"),
               tr("The current extension operation was cancelled. %1 queued extension operation(s) are waiting. "
                  "Cancel the queued operations too?")
                   .arg(pendingCount),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void ExtensionManagerPage::setUnsignedExtensionsAllowed(const bool allowed)
{
    if (!m_AllowUnsignedExtensionsCheck)
    {
        updateUnsignedExtensionsWarningBanner();
        return;
    }

    const QSignalBlocker blocker(m_AllowUnsignedExtensionsCheck);
    m_AllowUnsignedExtensionsCheck->setChecked(allowed);
    if (m_Settings)
    {
        m_Settings->upsert(kAllowUnsignedExtensionsSetting, allowed ? QStringLiteral("1") : QStringLiteral("0"));
    }
    updateUnsignedExtensionsWarningBanner();
}

void ExtensionManagerPage::setAutomaticEnvironmentCleanupEnabled(const bool enabled)
{
    if (!m_AutomaticEnvironmentCleanupCheck)
    {
        return;
    }

    const QSignalBlocker blocker(m_AutomaticEnvironmentCleanupCheck);
    m_AutomaticEnvironmentCleanupCheck->setChecked(enabled);
    if (m_Settings)
    {
        m_Settings->upsert(kAutomaticEnvironmentCleanupSetting, enabled ? QStringLiteral("1") : QStringLiteral("0"));
    }
}

void ExtensionManagerPage::updateUnsignedExtensionsWarningBanner()
{
    if (!m_UnsignedExtensionsWarningBanner)
    {
        return;
    }

    m_UnsignedExtensionsWarningBanner->setVisible(unsignedExtensionsAllowed());
}

void ExtensionManagerPage::startInstallOperation(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!primaryActionAvailable(entry) || !confirmInstallOperation(entry))
    {
        return;
    }

    const OperationKind kind = entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable
                                   ? OperationKind::Update
                                   : OperationKind::Install;
    if (m_OperationInProgress)
    {
        enqueuePendingOperation(kind, entry);
        return;
    }

    beginInstallOperation(entry);
}

bool ExtensionManagerPage::confirmInstallOperation(const ExtensionRepositoryBrowseEntry& entry)
{
    if (m_LastSnapshot.network_features_disabled)
    {
        const QString diagnostic = repositoryNetworkDisabledDiagnostic(m_LastSnapshot);
        logExtensionManagerPageIssue("install warning", diagnostic, false);
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
            m_SummaryBanner->setMessage(diagnostic);
        }
        return false;
    }

    const QString trust_diagnostic = installTrustPolicyDiagnostic(entry);
    if (!trust_diagnostic.isEmpty())
    {
        logExtensionManagerPageIssue("install blocked", trust_diagnostic, true);
        if (entry.selected_asset_signature_state == ExtensionPackageSignatureState::Failed ||
            entry.selected_asset_signature_state == ExtensionPackageSignatureState::Unknown)
        {
            QMessageBox::warning(this, tr("Extension Cannot Be Installed"), trust_diagnostic, QMessageBox::Ok);
        }
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
            m_SummaryBanner->setMessage(trust_diagnostic);
        }
        return false;
    }
    if (entry.selected_asset_signature_state == ExtensionPackageSignatureState::Unsigned)
    {
        if (!unsignedExtensionsAllowed())
        {
            const QString diagnostic =
                tr("%1 is unsigned. Unsigned extensions are blocked unless you explicitly allow them in the "
                   "extension manager.")
                    .arg(extensionDisplayNameText(entry));
            logExtensionManagerPageIssue("install blocked", diagnostic, true);
            QMessageBox::warning(this, tr("Unsigned Extension Blocked"), diagnostic, QMessageBox::Ok);
            if (m_SummaryBanner)
            {
                m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
                m_SummaryBanner->setMessage(diagnostic);
            }
            return false;
        }

        const QString warning =
            tr("%1 is unsigned. Only install unsigned extensions from authors and packages you trust. Unsigned "
               "packages cannot be authenticated by AfterNight.")
                .arg(extensionDisplayNameText(entry));
        logExtensionManagerPageIssue("install warning", warning, false);
        if (!confirmUnsignedInstall(tr("Install Unsigned Extension?"), warning))
        {
            logExtensionManagerPageInfo("install cancelled", tr("Unsigned extension install cancelled by user."));
            return false;
        }
    }

    return true;
}

void ExtensionManagerPage::beginInstallOperation(const ExtensionRepositoryBrowseEntry& entry)
{
    ExtensionManagerPageInstallRequest request = buildInstallRequest(entry);
    request.progress_callback = makeProgressCallback();
    m_ActiveCancellationToken = ExtensionOperationCancellationToken{};
    m_CancellationRequested = false;
    m_PromptCancelPendingAfterActiveCancellation = false;
    request.cancellation_token = m_ActiveCancellationToken;
    m_ActiveOperationKind = entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable
                                ? OperationKind::Update
                                : OperationKind::Install;
    m_ActiveOperationEntryId = entry.id;
    m_ActiveInstallEntryId = entry.id;
    m_ActiveInstallEntryVersion =
        entry.selected_release_version.empty() ? entry.latest_version : entry.selected_release_version;
    m_ActiveInstallSource = ExtensionPackageSource::Repository;
    if (request.assetUrl.isEmpty() || request.assetUrl.isRelative())
    {
        ExtensionManagerPageInstallResult result;
        result.diagnostic = tr("Package asset URL could not be resolved from the repository index.");
        completeInstallOperation(result);
        return;
    }

    const QString operation_message = entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable
                                          ? tr("Updating %1...").arg(QString::fromStdString(entry.name))
                                          : tr("Installing %1...").arg(QString::fromStdString(entry.name));
    if (auto* action_button = findChild<QPushButton*>(buttonObjectName(entry.id, "primaryAction")))
    {
        action_button->setText(entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable
                                   ? tr("Updating...")
                                   : tr("Installing..."));
    }
    if (m_CancelButton)
    {
        m_CancelButton->setToolTip(
            entry.install_state == ExtensionRepositoryInstallState::UpdateAvailable
                ? tr("Cancel updating %1").arg(QString::fromStdString(entry.name.empty() ? entry.id : entry.name))
                : tr("Cancel installing %1").arg(QString::fromStdString(entry.name.empty() ? entry.id : entry.name)));
    }
    setOperationInProgress(true);
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Info);
        m_SummaryBanner->setMessage(operation_message);
    }
    showOperationProgress(operation_message);

    const InstallProvider provider = m_InstallProvider ? m_InstallProvider : InstallProvider(defaultInstallProvider);
    auto* watcher = new QFutureWatcher<ExtensionManagerPageInstallResult>(this);
    m_InstallWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<ExtensionManagerPageInstallResult>::finished, this, [this, watcher]() {
        const ExtensionManagerPageInstallResult result = watcher->result();
        watcher->deleteLater();
        if (m_InstallWatcher == watcher)
        {
            m_InstallWatcher = nullptr;
        }
        completeInstallOperation(result);
    });
    watcher->setFuture(QtConcurrent::run([provider, request]() { return provider(request); }));
}

void ExtensionManagerPage::startLocalInstallOperation()
{
    if (m_OperationInProgress)
    {
        return;
    }

    if (!unsignedExtensionsAllowed())
    {
        const QString diagnostic =
            tr("Local extension packages are treated as unsigned. Enable unsigned extensions before installing a "
               "local package.");
        logExtensionManagerPageIssue("local install blocked", diagnostic, true);
        QMessageBox::warning(this, tr("Unsigned Extension Blocked"), diagnostic, QMessageBox::Ok);
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
            m_SummaryBanner->setMessage(diagnostic);
        }
        return;
    }

    const QString warning =
        tr("Local extension packages are unsigned unless they come from a trusted signed repository. Only install "
           "local packages from authors and files you trust.");
    logExtensionManagerPageIssue("local install warning", warning, false);
    if (!confirmUnsignedInstall(tr("Install Unsigned Local Extension?"), warning))
    {
        logExtensionManagerPageInfo("local install cancelled",
                                    tr("Unsigned local extension install cancelled by user."));
        return;
    }

    const LocalPackagePathProvider path_provider =
        m_LocalPackagePathProvider ? m_LocalPackagePathProvider : LocalPackagePathProvider([this](QWidget* parent) {
            QString initial_directory;
            if (m_Settings)
            {
                initial_directory = m_Settings->select(kLastLocalPackageDirectorySetting).trimmed();
                if (!initial_directory.isEmpty() && !QFileInfo(initial_directory).isDir())
                {
                    initial_directory.clear();
                }
            }
            return an::ui::dialogs::getOpenFileName(
                m_Settings, parent, tr("Install Local Extension Package"), initial_directory,
                tr("AfterNight extension packages (*.tar.zst *.tar);;All files (*)"));
        });
    const QString archive_path = path_provider(this).trimmed();
    if (archive_path.isEmpty())
    {
        return;
    }

    const QFileInfo archive_info(archive_path);
    if (!archive_info.isFile())
    {
        const QString diagnostic = tr("Local extension package was not found: %1").arg(archive_path);
        logExtensionManagerPageIssue("local install failed", diagnostic, true);
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
            m_SummaryBanner->setMessage(diagnostic);
        }
        return;
    }
    if (m_Settings && !archive_info.absolutePath().isEmpty())
    {
        m_Settings->upsert(kLastLocalPackageDirectorySetting, archive_info.absolutePath());
    }

    ExtensionManagerPageLocalInstallRequest request = buildLocalInstallRequest(archive_path);
    request.progress_callback = makeProgressCallback();
    m_ActiveCancellationToken = ExtensionOperationCancellationToken{};
    m_CancellationRequested = false;
    m_PromptCancelPendingAfterActiveCancellation = false;
    request.cancellation_token = m_ActiveCancellationToken;
    m_ActiveOperationKind = OperationKind::LocalInstall;
    m_ActiveOperationEntryId.clear();
    m_ActiveInstallEntryId.clear();
    m_ActiveInstallEntryVersion.clear();
    m_ActiveInstallSource = ExtensionPackageSource::User;

    const QString operation_message = tr("Installing local extension package...");
    if (m_MoreActionsButton)
    {
        m_MoreActionsButton->setText(tr("Installing..."));
    }
    if (m_CancelButton)
    {
        m_CancelButton->setToolTip(tr("Cancel installing the local extension package"));
    }
    setOperationInProgress(true);
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Info);
        m_SummaryBanner->setMessage(operation_message);
    }
    showOperationProgress(operation_message);

    const LocalInstallProvider provider =
        m_LocalInstallProvider ? m_LocalInstallProvider : LocalInstallProvider(defaultLocalInstallProvider);
    auto* watcher = new QFutureWatcher<ExtensionManagerPageInstallResult>(this);
    m_InstallWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<ExtensionManagerPageInstallResult>::finished, this, [this, watcher]() {
        const ExtensionManagerPageInstallResult result = watcher->result();
        watcher->deleteLater();
        if (m_InstallWatcher == watcher)
        {
            m_InstallWatcher = nullptr;
        }
        completeInstallOperation(result);
    });
    watcher->setFuture(QtConcurrent::run([provider, request]() { return provider(request); }));
}

void ExtensionManagerPage::completeInstallOperation(const ExtensionManagerPageInstallResult& result)
{
    clearOperationProgress();

    if (!m_SummaryBanner)
    {
        finishActiveOperationState();
        renderCards();
        clearActiveInstallEntry();
        continuePendingOperationsAfterCurrent(result.cancelled);
        return;
    }

    if (result.cancelled)
    {
        finishActiveOperationState();
        renderCards();
        clearActiveInstallEntry();
        const QString diagnostic =
            result.diagnostic.isEmpty() ? tr("Extension package installation cancelled.") : result.diagnostic;
        logExtensionManagerPageIssue("install cancelled", diagnostic, false);
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
        m_SummaryBanner->setMessage(diagnostic);
        continuePendingOperationsAfterCurrent(true);
        return;
    }

    if (result.success)
    {
        const QString readiness_diagnostic = installReadinessDiagnostic(result);
        if (!readiness_diagnostic.isEmpty())
        {
            finishActiveOperationState();
            renderCards();
            clearActiveInstallEntry();
            logExtensionManagerPageIssue("install readiness failed", readiness_diagnostic, true);
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
            m_SummaryBanner->setMessage(readiness_diagnostic);
            continuePendingOperationsAfterCurrent(false);
            return;
        }

        const bool installed_entry_marked = markActiveInstallEntryInstalled(result);
        if (installed_entry_marked)
        {
            persistCurrentRepositorySnapshotCache();
        }
        finishActiveOperationState();
        renderCards();
        refreshRepository(true);
        emit installedExtensionsChanged();
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Success);
        m_SummaryBanner->setMessage(installSuccessMessage(result));
        clearActiveInstallEntry();
        continuePendingOperationsAfterCurrent(false);
        return;
    }

    finishActiveOperationState();
    renderCards();
    clearActiveInstallEntry();
    const QString diagnostic =
        result.diagnostic.isEmpty() ? tr("Extension package installation failed.") : result.diagnostic;
    logExtensionManagerPageIssue("install failed", diagnostic, true);
    m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
    m_SummaryBanner->setMessage(diagnostic);
    continuePendingOperationsAfterCurrent(false);
}

bool ExtensionManagerPage::markActiveInstallEntryInstalled(const ExtensionManagerPageInstallResult& result)
{
    std::string installed_id = m_ActiveInstallEntryId;
    std::string installed_version = m_ActiveInstallEntryVersion;
    const auto& load_result = result.transaction.validation.load_result;
    if (load_result.manifest)
    {
        if (installed_id.empty())
        {
            installed_id = load_result.manifest->id;
        }
        if (installed_version.empty())
        {
            installed_version = load_result.manifest->version;
        }
    }

    if (installed_id.empty())
    {
        return false;
    }

    const auto entry_iter =
        std::find_if(m_LastSnapshot.entries.begin(), m_LastSnapshot.entries.end(),
                     [&installed_id](const ExtensionRepositoryBrowseEntry& entry) { return entry.id == installed_id; });
    if (entry_iter == m_LastSnapshot.entries.end())
    {
        return false;
    }

    if (installed_version.empty())
    {
        installed_version = entry_iter->selected_release_version.empty() ? entry_iter->latest_version
                                                                         : entry_iter->selected_release_version;
    }

    entry_iter->install_state = ExtensionRepositoryInstallState::Installed;
    entry_iter->installed_version = installed_version;
    entry_iter->installed_source_available = true;
    entry_iter->installed_source = m_ActiveInstallSource;
    if (load_result.manifest)
    {
        entry_iter->installed_status_available = true;
        entry_iter->installed_load_status = load_result.status;
        entry_iter->installed_status_diagnostic = load_result.diagnostic;
    }
    if (result.transaction.environment)
    {
        entry_iter->installed_environment_available = true;
        entry_iter->installed_environment_state = result.transaction.environment->state;
        entry_iter->installed_environment_diagnostic = result.transaction.environment->diagnostic;
    }
    entry_iter->install_state = installStateForCurrentInstalledVersion(*entry_iter);

    InstalledEntryOverride installed;
    installed.id = entry_iter->id;
    installed.version = entry_iter->installed_version;
    installed.source = entry_iter->installed_source;
    installed.status_available = entry_iter->installed_status_available;
    installed.load_status = entry_iter->installed_load_status;
    installed.status_diagnostic = entry_iter->installed_status_diagnostic;
    installed.environment_available = entry_iter->installed_environment_available;
    installed.environment_state = entry_iter->installed_environment_state;
    installed.environment_diagnostic = entry_iter->installed_environment_diagnostic;
    m_InstalledEntryOverride = std::move(installed);
    return true;
}

void ExtensionManagerPage::applyInstalledEntryOverride(ExtensionRepositoryBrowseEntry& entry) const
{
    if (!m_InstalledEntryOverride || entry.id != m_InstalledEntryOverride->id ||
        m_InstalledEntryOverride->version.empty())
    {
        return;
    }
    if (!installedOverrideShouldReplace(entry, m_InstalledEntryOverride->version, m_InstalledEntryOverride->source))
    {
        return;
    }

    entry.installed_version = m_InstalledEntryOverride->version;
    entry.installed_source_available = true;
    entry.installed_source = m_InstalledEntryOverride->source;
    entry.installed_status_available = m_InstalledEntryOverride->status_available;
    entry.installed_load_status = m_InstalledEntryOverride->load_status;
    entry.installed_status_diagnostic = m_InstalledEntryOverride->status_diagnostic;
    entry.installed_environment_available = m_InstalledEntryOverride->environment_available;
    entry.installed_environment_state = m_InstalledEntryOverride->environment_state;
    entry.installed_environment_diagnostic = m_InstalledEntryOverride->environment_diagnostic;
    entry.install_state = installStateForCurrentInstalledVersion(entry);
}

void ExtensionManagerPage::applyInstalledEntryOverrides(ExtensionManagerPageRepositorySnapshot& snapshot) const
{
    if (!m_InstalledEntryOverride)
    {
        return;
    }

    for (auto& entry : snapshot.entries)
    {
        applyInstalledEntryOverride(entry);
    }
}

void ExtensionManagerPage::persistCurrentRepositorySnapshotCache() const
{
    if (m_RepositoryProvider || m_LastSnapshot.entries.empty())
    {
        return;
    }

    ExtensionManagerPageRepositorySnapshot snapshot = m_LastSnapshot;
    snapshot.success = true;
    snapshot.network_features_disabled = false;
    snapshot.from_cache = false;
    snapshot.background_refresh_in_progress = false;
    snapshot.cache_updated_at_utc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (saveRepositorySnapshotCache(snapshot))
    {
        logExtensionManagerPageDebug("repository cache", tr("Repository snapshot cache updated after package change"));
    }
}

void ExtensionManagerPage::markEntryUninstalled(const std::string& extensionId)
{
    // What an install recorded for this package would otherwise be laid over every later snapshot.
    if (m_InstalledEntryOverride && m_InstalledEntryOverride->id == extensionId)
    {
        m_InstalledEntryOverride.reset();
    }

    const auto entry_iter =
        std::find_if(m_LastSnapshot.entries.begin(), m_LastSnapshot.entries.end(),
                     [&extensionId](const ExtensionRepositoryBrowseEntry& entry) { return entry.id == extensionId; });
    if (entry_iter == m_LastSnapshot.entries.end() || entry_iter->installed_version.empty())
    {
        return;
    }

    const ExtensionRepositoryBrowseEntry not_installed;
    entry_iter->installed_version.clear();
    entry_iter->installed_source_available = not_installed.installed_source_available;
    entry_iter->installed_source = not_installed.installed_source;
    entry_iter->installed_status_available = not_installed.installed_status_available;
    entry_iter->installed_load_status = not_installed.installed_load_status;
    entry_iter->installed_status_diagnostic.clear();
    entry_iter->installed_environment_available = not_installed.installed_environment_available;
    entry_iter->installed_environment_state = not_installed.installed_environment_state;
    entry_iter->installed_environment_diagnostic.clear();
    entry_iter->install_state = installStateForCurrentInstalledVersion(*entry_iter);
    // The cache is what the page shows first the next time it loads, and all it shows while the repository is offline.
    persistCurrentRepositorySnapshotCache();
}

void ExtensionManagerPage::noteExtensionUninstalled(const std::string& extensionId)
{
    if (extensionId.empty())
    {
        return;
    }

    markEntryUninstalled(extensionId);
    renderCards();
    refreshRepository(true);
}

void ExtensionManagerPage::clearActiveInstallEntry()
{
    m_ActiveInstallEntryId.clear();
    m_ActiveInstallEntryVersion.clear();
    m_ActiveInstallSource = ExtensionPackageSource::Repository;
}

void ExtensionManagerPage::startRemoveOperation(const ExtensionRepositoryBrowseEntry& entry)
{
    if (!removeActionAvailable(entry) || !confirmRemoveOperation(entry))
    {
        return;
    }

    if (m_OperationInProgress)
    {
        enqueuePendingOperation(OperationKind::Remove, entry);
        return;
    }

    beginRemoveOperation(entry);
}

bool ExtensionManagerPage::confirmRemoveOperation(const ExtensionRepositoryBrowseEntry& entry)
{
    if (m_RemoveConfirmationProvider)
    {
        return m_RemoveConfirmationProvider(this, entry);
    }

    const QString extension_name = extensionDisplayNameText(entry);
    QMessageBox confirmation(this);
    confirmation.setIcon(QMessageBox::Warning);
    confirmation.setWindowTitle(tr("Uninstall Extension"));
    confirmation.setText(tr("Uninstall %1?").arg(extension_name));
    confirmation.setInformativeText(tr("This removes the installed extension package from disk. Running extension "
                                       "sessions for this package should be closed first."));
    confirmation.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    return confirmation.exec() == QMessageBox::Yes;
}

void ExtensionManagerPage::beginRemoveOperation(const ExtensionRepositoryBrowseEntry& entry)
{
    const ExtensionManagerPageRemoveRequest request = buildRemoveRequest(entry);
    ExtensionManagerPageRemoveRequest progress_request = request;
    progress_request.progress_callback = makeProgressCallback();
    m_ActiveCancellationToken = ExtensionOperationCancellationToken{};
    m_CancellationRequested = false;
    m_PromptCancelPendingAfterActiveCancellation = false;
    progress_request.cancellation_token = m_ActiveCancellationToken;
    m_ActiveOperationKind = OperationKind::Remove;
    m_ActiveOperationEntryId = entry.id;
    if (auto* action_button = findChild<QPushButton*>(buttonObjectName(entry.id, "remove")))
    {
        action_button->setText(tr("Uninstalling..."));
    }
    if (m_CancelButton)
    {
        m_CancelButton->setToolTip(
            tr("Cancel uninstalling %1").arg(QString::fromStdString(entry.name.empty() ? entry.id : entry.name)));
    }
    setOperationInProgress(true);
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Info);
        m_SummaryBanner->setMessage(tr("Removing %1...").arg(QString::fromStdString(entry.name)));
    }
    showOperationProgress(tr("Removing %1...").arg(QString::fromStdString(entry.name)));

    const RemoveProvider provider = m_RemoveProvider ? m_RemoveProvider : RemoveProvider(defaultRemoveProvider);
    auto* watcher = new QFutureWatcher<ExtensionManagerPageRemoveResult>(this);
    m_RemoveWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<ExtensionManagerPageRemoveResult>::finished, this, [this, watcher]() {
        const ExtensionManagerPageRemoveResult result = watcher->result();
        watcher->deleteLater();
        if (m_RemoveWatcher == watcher)
        {
            m_RemoveWatcher = nullptr;
        }
        completeRemoveOperation(result);
    });
    watcher->setFuture(QtConcurrent::run([provider, progress_request]() { return provider(progress_request); }));
}

void ExtensionManagerPage::completeRemoveOperation(const ExtensionManagerPageRemoveResult& result)
{
    clearOperationProgress();

    if (!m_SummaryBanner)
    {
        finishActiveOperationState();
        renderCards();
        continuePendingOperationsAfterCurrent(result.cancelled);
        return;
    }

    if (result.cancelled)
    {
        finishActiveOperationState();
        renderCards();
        const QString diagnostic =
            result.diagnostic.isEmpty() ? tr("Extension package removal cancelled.") : result.diagnostic;
        logExtensionManagerPageIssue("remove cancelled", diagnostic, false);
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
        m_SummaryBanner->setMessage(diagnostic);
        continuePendingOperationsAfterCurrent(true);
        return;
    }

    if (result.success)
    {
        markEntryUninstalled(m_ActiveOperationEntryId);
        finishActiveOperationState();
        renderCards();
        refreshRepository(true);
        emit installedExtensionsChanged();
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Success);
        m_SummaryBanner->setMessage(result.diagnostic.isEmpty() ? tr("Extension package removed successfully.")
                                                                : result.diagnostic);
        continuePendingOperationsAfterCurrent(false);
        return;
    }

    finishActiveOperationState();
    renderCards();
    const QString diagnostic =
        result.diagnostic.isEmpty() ? tr("Extension package removal failed.") : result.diagnostic;
    logExtensionManagerPageIssue("remove failed", diagnostic, true);
    m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
    m_SummaryBanner->setMessage(diagnostic);
    continuePendingOperationsAfterCurrent(false);
}

void ExtensionManagerPage::openExtensionSettings(const extensions::ExtensionRepositoryBrowseEntry& entry)
{
    if (m_OperationInProgress || entry.installed_version.empty())
    {
        return;
    }

    QString diagnostic;
    const bool opened = m_ExtensionSettingsOpener ? m_ExtensionSettingsOpener(entry, this, &diagnostic) : false;
    if (opened)
    {
        return;
    }

    if (diagnostic.isEmpty())
    {
        diagnostic = tr("Extension settings are not available from the manager page.");
    }
    logExtensionManagerPageIssue("settings open failed", diagnostic, false);
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
        m_SummaryBanner->setMessage(diagnostic);
    }
}

void ExtensionManagerPage::startEnvironmentCleanupOperation()
{
    if (m_OperationInProgress)
    {
        return;
    }

    const ExtensionManagerPageEnvironmentCleanupRequest request = buildEnvironmentCleanupRequest();
    ExtensionManagerPageEnvironmentCleanupRequest progress_request = request;
    progress_request.progress_callback = makeProgressCallback();
    m_ActiveCancellationToken = ExtensionOperationCancellationToken{};
    m_CancellationRequested = false;
    m_PromptCancelPendingAfterActiveCancellation = false;
    progress_request.cancellation_token = m_ActiveCancellationToken;
    m_ActiveOperationKind = OperationKind::EnvironmentCleanup;
    m_ActiveOperationEntryId.clear();
    logExtensionManagerPageInfo("environment cleanup", tr("User requested cleanup of unused extension environments"));
    if (m_CancelButton)
    {
        m_CancelButton->setToolTip(tr("Cancel cleaning unused extension environments"));
    }
    setOperationInProgress(true);
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Info);
        m_SummaryBanner->setMessage(tr("Cleaning unused extension environments..."));
    }
    showOperationProgress(tr("Cleaning unused extension environments..."));

    const EnvironmentCleanupProvider provider = m_EnvironmentCleanupProvider
                                                    ? m_EnvironmentCleanupProvider
                                                    : EnvironmentCleanupProvider(defaultEnvironmentCleanupProvider);
    auto* watcher = new QFutureWatcher<ExtensionManagerPageEnvironmentCleanupResult>(this);
    m_EnvironmentCleanupWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<ExtensionManagerPageEnvironmentCleanupResult>::finished, this,
                     [this, watcher]() {
                         const ExtensionManagerPageEnvironmentCleanupResult result = watcher->result();
                         watcher->deleteLater();
                         if (m_EnvironmentCleanupWatcher == watcher)
                         {
                             m_EnvironmentCleanupWatcher = nullptr;
                         }
                         completeEnvironmentCleanupOperation(result);
                     });
    watcher->setFuture(QtConcurrent::run([provider, progress_request]() { return provider(progress_request); }));
}

void ExtensionManagerPage::completeEnvironmentCleanupOperation(
    const ExtensionManagerPageEnvironmentCleanupResult& result)
{
    finishActiveOperationState();
    clearOperationProgress();

    if (result.success)
    {
        refreshRepository(true);
    }
    else
    {
        renderCards();
    }

    if (!m_SummaryBanner)
    {
        continuePendingOperationsAfterCurrent(result.cancelled);
        return;
    }

    if (result.cancelled)
    {
        const QString diagnostic =
            result.diagnostic.isEmpty() ? tr("Unused extension environment cleanup cancelled.") : result.diagnostic;
        logExtensionManagerPageIssue("environment cleanup cancelled", diagnostic, false);
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
        m_SummaryBanner->setMessage(diagnostic);
        continuePendingOperationsAfterCurrent(true);
        return;
    }

    if (result.success)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Success);
        m_SummaryBanner->setMessage(environmentCleanupSuccessMessage(result));
        continuePendingOperationsAfterCurrent(false);
        return;
    }

    const QString diagnostic =
        result.diagnostic.isEmpty() ? tr("Unused extension environment cleanup failed.") : result.diagnostic;
    logExtensionManagerPageIssue("environment cleanup failed", diagnostic, true);
    m_SummaryBanner->setTone(AnInfoBanner::Tone::Error);
    m_SummaryBanner->setMessage(diagnostic);
    continuePendingOperationsAfterCurrent(false);
}

void ExtensionManagerPage::cancelActiveOperation()
{
    if (!m_OperationInProgress || m_CancellationRequested)
    {
        return;
    }

    m_CancellationRequested = true;
    m_PromptCancelPendingAfterActiveCancellation = !m_PendingOperations.empty();
    m_ActiveCancellationToken.requestCancellation();
    logExtensionManagerPageInfo("operation cancel",
                                tr("User requested cancellation of the active extension operation"));
    if (m_CancelButton)
    {
        m_CancelButton->setEnabled(false);
    }
    if (m_SummaryBanner)
    {
        m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
        m_SummaryBanner->setMessage(tr("Cancelling extension operation..."));
    }
    showOperationProgress(tr("Cancelling extension operation..."));
}

void ExtensionManagerPage::setOperationInProgress(const bool inProgress)
{
    m_OperationInProgress = inProgress;
    if (m_SearchEdit)
    {
        m_SearchEdit->setEnabled(true);
    }
    if (m_FilterCombo)
    {
        m_FilterCombo->setEnabled(true);
    }
    if (m_SortCombo)
    {
        m_SortCombo->setEnabled(true);
    }
    if (m_RefreshButton)
    {
        m_RefreshButton->setEnabled(!inProgress && !m_RepositoryRefreshInProgress &&
                                    !m_LastSnapshot.network_features_disabled);
    }
    if (m_MoreActionsButton)
    {
        m_MoreActionsButton->setEnabled(!inProgress);
        if (!inProgress)
        {
            m_MoreActionsButton->setText(tr("More Actions"));
        }
    }
    if (m_AllowUnsignedExtensionsCheck)
    {
        m_AllowUnsignedExtensionsCheck->setEnabled(true);
    }
    if (m_AutomaticEnvironmentCleanupCheck)
    {
        m_AutomaticEnvironmentCleanupCheck->setEnabled(true);
    }
    if (m_CancelButton)
    {
        m_CancelButton->setVisible(inProgress);
        m_CancelButton->setEnabled(inProgress && !m_CancellationRequested);
        if (!inProgress)
        {
            m_CancelButton->setToolTip(tr("Cancel the current extension operation"));
        }
    }

    updateActionButtonsForOperationState();

    for (auto* button : m_DetailButtons)
    {
        if (button)
        {
            button->setEnabled(true);
        }
    }
}

void ExtensionManagerPage::enqueuePendingOperation(const OperationKind kind,
                                                   const ExtensionRepositoryBrowseEntry& entry)
{
    if (entry.id.empty() || kind == OperationKind::None || pendingOperationForEntry(entry.id) != nullptr ||
        m_ActiveOperationEntryId == entry.id)
    {
        return;
    }

    PendingOperation operation;
    operation.sequence = m_NextPendingOperationSequence++;
    operation.kind = kind;
    operation.entry = entry;
    m_PendingOperations.push_back(std::move(operation));

    logExtensionManagerPageInfo("operation queued",
                                tr("Queued extension operation for %1").arg(extensionDisplayNameText(entry)));
    updatePendingOperationsView();
    updateActionButtonsForOperationState();
}

void ExtensionManagerPage::cancelPendingOperation(const std::uint64_t sequence)
{
    const auto iter =
        std::find_if(m_PendingOperations.begin(), m_PendingOperations.end(),
                     [sequence](const PendingOperation& operation) { return operation.sequence == sequence; });
    if (iter == m_PendingOperations.end())
    {
        return;
    }

    logExtensionManagerPageInfo(
        "operation queue",
        tr("Cancelled queued extension operation for %1").arg(extensionDisplayNameText(iter->entry)));
    m_PendingOperations.erase(iter);
    updatePendingOperationsView();
    updateActionButtonsForOperationState();
}

void ExtensionManagerPage::clearPendingOperations()
{
    if (m_PendingOperations.empty())
    {
        return;
    }

    m_PendingOperations.clear();
    updatePendingOperationsView();
    updateActionButtonsForOperationState();
}

void ExtensionManagerPage::startNextPendingOperation()
{
    if (m_OperationInProgress || m_PendingOperations.empty())
    {
        updatePendingOperationsView();
        updateActionButtonsForOperationState();
        return;
    }

    PendingOperation operation = std::move(m_PendingOperations.front());
    m_PendingOperations.erase(m_PendingOperations.begin());
    updatePendingOperationsView();
    updateActionButtonsForOperationState();

    if (operation.kind == OperationKind::Remove)
    {
        beginRemoveOperation(operation.entry);
        return;
    }

    beginInstallOperation(operation.entry);
}

void ExtensionManagerPage::finishActiveOperationState()
{
    m_ActiveOperationKind = OperationKind::None;
    m_ActiveOperationEntryId.clear();
    m_CancellationRequested = false;
    m_ActiveCancellationToken = ExtensionOperationCancellationToken{};
    setOperationInProgress(false);
    updatePendingOperationsView();
    updateActionButtonsForOperationState();
}

void ExtensionManagerPage::continuePendingOperationsAfterCurrent(const bool currentOperationCancelled)
{
    const bool should_prompt_for_pending =
        currentOperationCancelled && m_PromptCancelPendingAfterActiveCancellation && !m_PendingOperations.empty();
    m_PromptCancelPendingAfterActiveCancellation = false;

    if (should_prompt_for_pending && confirmCancelPendingOperations(static_cast<int>(m_PendingOperations.size())))
    {
        const int cancelled_count = static_cast<int>(m_PendingOperations.size());
        clearPendingOperations();
        if (m_SummaryBanner)
        {
            m_SummaryBanner->setTone(AnInfoBanner::Tone::Warning);
            m_SummaryBanner->setMessage(tr("Cancelled %1 queued extension operation(s).").arg(cancelled_count));
        }
        return;
    }

    startNextPendingOperation();
}

void ExtensionManagerPage::updatePendingOperationsView()
{
    if (!m_QueueContainer || !m_QueueLayout)
    {
        return;
    }

    while (QLayoutItem* item = m_QueueLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            delete widget;
        }
        delete item;
    }

    if (m_PendingOperations.empty())
    {
        m_QueueContainer->setVisible(false);
        return;
    }

    const auto& t = tokens();
    auto* header = new QLabel(tr("%1 queued extension operation(s)").arg(static_cast<int>(m_PendingOperations.size())),
                              m_QueueContainer);
    header->setObjectName(QStringLiteral("anExtensionManagerPendingQueueHeader"));
    header->setFont(t.typography.heading3);
    header->setProperty("anStatusTone", QStringLiteral("secondary"));
    m_QueueLayout->addWidget(header);

    for (const PendingOperation& operation : m_PendingOperations)
    {
        auto* row = new QWidget(m_QueueContainer);
        row->setObjectName(QStringLiteral("anExtensionManagerPendingQueueRow_%1").arg(operation.sequence));
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setObjectName(QStringLiteral("anExtensionManagerPendingQueueRowLayout_%1").arg(operation.sequence));
        row_layout->setSpacing(t.spacing.sm);
        applyFlushMargins(row_layout);

        QString action_text;
        switch (operation.kind)
        {
        case OperationKind::Install:
            action_text = tr("Install %1").arg(extensionDisplayNameText(operation.entry));
            break;
        case OperationKind::Update:
            action_text = tr("Update %1").arg(extensionDisplayNameText(operation.entry));
            break;
        case OperationKind::Remove:
            action_text = tr("Uninstall %1").arg(extensionDisplayNameText(operation.entry));
            break;
        case OperationKind::LocalInstall:
        case OperationKind::EnvironmentCleanup:
        case OperationKind::None:
            action_text = extensionDisplayNameText(operation.entry);
            break;
        }

        auto* label = new QLabel(action_text, row);
        label->setObjectName(
            QStringLiteral("anExtensionManagerPendingQueueLabel_%1").arg(sanitizeObjectToken(operation.entry.id)));
        label->setFont(t.typography.body);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        row_layout->addWidget(label, 1);

        auto* cancel_button = new QPushButton(tr("Cancel"), row);
        cancel_button->setObjectName(pendingCancelButtonObjectName(operation.entry.id));
        applyButtonRole(cancel_button, QStringLiteral("secondary"));
        cancel_button->setToolTip(tr("Cancel this queued extension operation"));
        QObject::connect(cancel_button, &QPushButton::clicked, row,
                         [this, sequence = operation.sequence]() { cancelPendingOperation(sequence); });
        row_layout->addWidget(cancel_button);

        m_QueueLayout->addWidget(row);
    }

    m_QueueContainer->setVisible(true);
}

void ExtensionManagerPage::updateActionButtonsForOperationState()
{
    for (auto* button : m_ActionButtons)
    {
        if (!button)
        {
            continue;
        }

        const QString extension_id_text = button->property("anExtensionManagerEntryId").toString();
        const QString action_type = button->property("anExtensionManagerActionType").toString();
        const QString default_text = button->property("anExtensionManagerDefaultText").toString();
        const bool action_available = button->property("anExtensionManagerActionAvailable").toBool();
        const std::string extension_id = extension_id_text.toStdString();

        if (!default_text.isEmpty())
        {
            button->setText(default_text);
        }
        button->setToolTip(button->property("anExtensionManagerDefaultToolTip").toString());

        const bool buttons_blocked_by_refresh = m_RepositoryRefreshInProgress && !m_OperationInProgress;
        bool enabled = action_available && !buttons_blocked_by_refresh;
        if (const PendingOperation* pending = pendingOperationForEntry(extension_id))
        {
            const bool pending_primary =
                pending->kind == OperationKind::Install || pending->kind == OperationKind::Update;
            const QString extension_name = extensionDisplayNameText(pending->entry);
            QString pending_tooltip;
            switch (pending->kind)
            {
            case OperationKind::Install:
                pending_tooltip = tr("Installing %1 is queued. Cancel it from the pending queue to change actions.")
                                      .arg(extension_name);
                break;
            case OperationKind::Update:
                pending_tooltip = tr("Updating %1 is queued. Cancel it from the pending queue to change actions.")
                                      .arg(extension_name);
                break;
            case OperationKind::Remove:
                pending_tooltip = tr("Uninstalling %1 is queued. Cancel it from the pending queue to change actions.")
                                      .arg(extension_name);
                break;
            case OperationKind::LocalInstall:
            case OperationKind::EnvironmentCleanup:
            case OperationKind::None:
                pending_tooltip = tr("This extension has a queued operation.");
                break;
            }
            if ((pending_primary && action_type == QStringLiteral("primary")) ||
                (pending->kind == OperationKind::Remove && action_type == QStringLiteral("remove")))
            {
                button->setText(tr("pending..."));
            }
            button->setToolTip(pending_tooltip);
            enabled = false;
        }
        else if (!m_ActiveOperationEntryId.empty() && m_ActiveOperationEntryId == extension_id)
        {
            QString extension_name = QString::fromStdString(extension_id);
            const auto active_entry = std::find_if(
                m_LastSnapshot.entries.cbegin(), m_LastSnapshot.entries.cend(),
                [&extension_id](const ExtensionRepositoryBrowseEntry& entry) { return entry.id == extension_id; });
            if (active_entry != m_LastSnapshot.entries.cend())
            {
                extension_name = extensionDisplayNameText(*active_entry);
            }
            if ((m_ActiveOperationKind == OperationKind::Install || m_ActiveOperationKind == OperationKind::Update) &&
                action_type == QStringLiteral("primary"))
            {
                button->setText(m_ActiveOperationKind == OperationKind::Update ? tr("Updating...")
                                                                               : tr("Installing..."));
                button->setToolTip(
                    m_ActiveOperationKind == OperationKind::Update
                        ? tr("Updating %1. Use Cancel to stop the current operation.").arg(extension_name)
                        : tr("Installing %1. Use Cancel to stop the current operation.").arg(extension_name));
            }
            else if (m_ActiveOperationKind == OperationKind::Remove && action_type == QStringLiteral("remove"))
            {
                button->setText(tr("Uninstalling..."));
                button->setToolTip(
                    tr("Uninstalling %1. Use Cancel to stop the current operation.").arg(extension_name));
            }
            else
            {
                button->setToolTip(
                    tr("%1 is busy with another operation, so this action is disabled.").arg(extension_name));
            }
            enabled = false;
        }

        button->setEnabled(enabled);
    }
}

const ExtensionManagerPage::PendingOperation*
ExtensionManagerPage::pendingOperationForEntry(const std::string& extensionId) const
{
    const auto iter =
        std::find_if(m_PendingOperations.cbegin(), m_PendingOperations.cend(),
                     [&extensionId](const PendingOperation& operation) { return operation.entry.id == extensionId; });
    return iter == m_PendingOperations.cend() ? nullptr : &(*iter);
}

void ExtensionManagerPage::showOperationProgress(const QString& message, const bool indeterminate, const int current,
                                                 const int total)
{
    if (m_ProgressLabel)
    {
        m_ProgressLabel->setText(message);
        m_ProgressLabel->setVisible(true);
    }
    if (!m_ProgressBar)
    {
        return;
    }

    if (indeterminate || total <= 0)
    {
        m_ProgressBar->setRange(0, 0);
    }
    else
    {
        m_ProgressBar->setRange(0, total);
        m_ProgressBar->setValue(current);
    }
    m_ProgressBar->setVisible(true);
}

void ExtensionManagerPage::updateOperationProgress(const extensions::ExtensionOperationProgress& progress)
{
    showOperationProgress(QString::fromStdString(progress.message), progress.indeterminate, progress.current,
                          progress.total);
}

void ExtensionManagerPage::clearOperationProgress()
{
    if (m_ProgressLabel)
    {
        m_ProgressLabel->clear();
        m_ProgressLabel->setVisible(false);
    }
    if (m_ProgressBar)
    {
        m_ProgressBar->setRange(0, 100);
        m_ProgressBar->setValue(0);
        m_ProgressBar->setVisible(false);
    }
}

extensions::ExtensionOperationProgressCallback ExtensionManagerPage::makeProgressCallback()
{
    const QPointer<ExtensionManagerPage> page(this);
    const QPointer<QObject> dispatcher(QCoreApplication::instance() != nullptr
                                           ? static_cast<QObject*>(QCoreApplication::instance())
                                           : static_cast<QObject*>(this));
    return [page, dispatcher](const extensions::ExtensionOperationProgress& progress) {
        if (!dispatcher)
        {
            return;
        }

        QMetaObject::invokeMethod(
            dispatcher.data(),
            [page, progress]() {
                if (page)
                {
                    page->updateOperationProgress(progress);
                }
            },
            Qt::QueuedConnection);
    };
}

bool ExtensionManagerPage::entryMatchesSearch(const ExtensionRepositoryBrowseEntry& entry,
                                              const QString& search_text) const
{
    if (search_text.isEmpty())
    {
        return true;
    }

    QStringList fields = {QString::fromStdString(entry.id),
                          QString::fromStdString(entry.name),
                          QString::fromStdString(entry.summary),
                          QString::fromStdString(entry.description),
                          QString::fromStdString(entry.author),
                          QString::fromStdString(entry.publisher_id),
                          QString::fromStdString(entry.license),
                          QString::fromStdString(entry.attribution),
                          QString::fromStdString(entry.original_author),
                          QString::fromStdString(entry.original_project),
                          QString::fromStdString(entry.original_source_url),
                          QString::fromStdString(entry.upstream_commit)};
    for (const auto& tag : entry.tags)
    {
        fields.push_back(QString::fromStdString(tag));
    }

    return fields.join(QStringLiteral(" ")).contains(search_text, Qt::CaseInsensitive);
}

ExtensionDiscoveryOptions ExtensionManagerPage::buildDiscoveryOptions() const
{
    return m_DiscoveryOptionsProvider ? m_DiscoveryOptionsProvider() : buildDefaultDiscoveryOptions();
}

ExtensionManagerPageInstallRequest
ExtensionManagerPage::buildInstallRequest(const ExtensionRepositoryBrowseEntry& entry) const
{
    const ExtensionDiscoveryOptions discovery_options = buildDiscoveryOptions();
    ExtensionManagerPageInstallRequest request;
    request.entry = entry;
    request.indexUrl = m_LastSnapshot.indexUrl;
    request.assetUrl =
        an::ui::extensions::resolve_extension_repository_asset_url(request.indexUrl, entry.selected_asset_download_url);
    request.context = discovery_options.context;
    request.installRoot = discovery_options.repository_roots.empty() ? an::ui::extensions::repository_extensions_root()
                                                                     : discovery_options.repository_roots.front();
    return request;
}

ExtensionManagerPageLocalInstallRequest ExtensionManagerPage::buildLocalInstallRequest(const QString& archivePath) const
{
    const ExtensionDiscoveryOptions discovery_options = buildDiscoveryOptions();
    ExtensionManagerPageLocalInstallRequest request;
    request.archivePath = std::filesystem::path(archivePath.toStdString());
    request.context = discovery_options.context;
    request.installRoot = discovery_options.user_roots.empty() ? an::ui::extensions::user_extensions_root()
                                                               : discovery_options.user_roots.front();
    return request;
}

ExtensionManagerPageRemoveRequest
ExtensionManagerPage::buildRemoveRequest(const ExtensionRepositoryBrowseEntry& entry) const
{
    const ExtensionDiscoveryOptions discovery_options = buildDiscoveryOptions();
    ExtensionManagerPageRemoveRequest request;
    request.entry = entry;
    const bool remove_from_user_root =
        entry.installed_source_available && entry.installed_source == ExtensionPackageSource::User;
    if (remove_from_user_root)
    {
        request.installRoot = discovery_options.user_roots.empty() ? an::ui::extensions::user_extensions_root()
                                                                   : discovery_options.user_roots.front();
    }
    else
    {
        request.installRoot = discovery_options.repository_roots.empty()
                                  ? an::ui::extensions::repository_extensions_root()
                                  : discovery_options.repository_roots.front();
    }
    return request;
}

ExtensionManagerPageEnvironmentCleanupRequest ExtensionManagerPage::buildEnvironmentCleanupRequest() const
{
    ExtensionManagerPageEnvironmentCleanupRequest request;
    request.discovery_options = buildDiscoveryOptions();
    return request;
}

QWidget* ExtensionManagerPage::buildRepositoryCard(const ExtensionRepositoryBrowseEntry& entry)
{
    auto* card = new AnCardWidget({}, m_CardsContainer);
    card->setObjectName(QStringLiteral("anExtensionManagerCard_%1").arg(sanitizeObjectToken(entry.id)));
    card->setProperty("anCardCompact", true);
    card->setProperty("anCardBodyPadded", true);
    card->setProperty("anExtensionManagerUpdateAvailable", updateAvailable(entry));

    const auto& t = tokens();
    auto* layout = new QVBoxLayout();
    layout->setObjectName(fieldObjectName(entry.id, "contentLayout"));
    layout->setSpacing(t.spacing.sm);
    applyFlushMargins(layout);

    auto* title_row = new QHBoxLayout();
    title_row->setObjectName(fieldObjectName(entry.id, "titleLayout"));
    title_row->setSpacing(t.spacing.sm);
    applyFlushMargins(title_row);

    auto* title_label = new QLabel(extensionDisplayNameText(entry), card->contentWidget());
    title_label->setObjectName(fieldObjectName(entry.id, "title"));
    title_label->setFont(t.typography.heading3);
    title_label->setWordWrap(true);
    title_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    title_row->addWidget(title_label, 1);

    // Badges
    const QString state_text = installStateText(entry);
    const QString status_text = installedStatusText(entry);
    auto* state_badge =
        new AnStatusBadge(state_text, installStateBadgeStatus(entry.install_state), card->contentWidget());
    state_badge->setObjectName(fieldObjectName(entry.id, "stateBadge"));
    state_badge->setToolTip(installStateTooltipText(entry));
    title_row->addWidget(state_badge, 0, Qt::AlignVCenter);

    if (status_text != state_text)
    {
        auto* status_badge = new AnStatusBadge(status_text, runtimeStatusBadgeStatus(entry), card->contentWidget());
        status_badge->setObjectName(fieldObjectName(entry.id, "statusBadge"));
        status_badge->setToolTip(runtimeStatusTooltipText(entry));
        title_row->addWidget(status_badge, 0, Qt::AlignVCenter);
    }

    auto* version_badge =
        new AnStatusBadge(primaryVersionBadgeText(entry), versionBadgeStatus(entry), card->contentWidget());
    version_badge->setObjectName(fieldObjectName(entry.id, "versionBadge"));
    version_badge->setToolTip(versionBadgeTooltipText(entry));
    title_row->addWidget(version_badge, 0, Qt::AlignVCenter);

    auto* signature_badge =
        new AnStatusBadge(signatureStateText(entry.selected_asset_signature_state),
                          signatureBadgeStatus(entry.selected_asset_signature_state), card->contentWidget());
    signature_badge->setObjectName(fieldObjectName(entry.id, "signatureBadge"));
    signature_badge->setToolTip(signatureBadgeTooltipText(entry));
    title_row->addWidget(signature_badge, 0, Qt::AlignVCenter);

    // Push actions to the right
    title_row->addSpacing(t.spacing.md);
    title_row->addStretch(1);

    auto* actions_layout = new QHBoxLayout();
    actions_layout->setObjectName(fieldObjectName(entry.id, "actionsLayout"));
    applyFlushMargins(actions_layout);
    actions_layout->setSpacing(t.spacing.sm);

    auto* primary_action = new QPushButton(primaryActionText(entry), card->contentWidget());
    primary_action->setObjectName(buttonObjectName(entry.id, "primaryAction"));
    applyButtonRole(primary_action, buttonRoleForPrimaryAction(entry.install_state));
    const bool action_available = primaryActionAvailable(entry) && !m_LastSnapshot.network_features_disabled;
    const QString primary_tooltip = primaryActionTooltipText(entry, m_LastSnapshot.network_features_disabled);
    primary_action->setProperty("anExtensionManagerEntryId", QString::fromStdString(entry.id));
    primary_action->setProperty("anExtensionManagerActionType", QStringLiteral("primary"));
    primary_action->setProperty("anExtensionManagerDefaultText", primary_action->text());
    primary_action->setProperty("anExtensionManagerDefaultToolTip", primary_tooltip);
    primary_action->setProperty("anExtensionManagerActionAvailable", action_available);
    primary_action->setToolTip(primary_tooltip);
    primary_action->setEnabled(action_available && !m_RepositoryRefreshInProgress);
    QObject::connect(primary_action, &QPushButton::clicked, card, [this, entry]() { startInstallOperation(entry); });
    actions_layout->addWidget(primary_action);
    m_ActionButtons.push_back(primary_action);

    auto* remove_action = new QPushButton(tr("Uninstall"), card->contentWidget());
    remove_action->setObjectName(buttonObjectName(entry.id, "remove"));
    applyButtonRole(remove_action, QStringLiteral("danger"));
    const bool remove_available = removeActionAvailable(entry);
    const QString remove_tooltip = removeActionTooltipText(entry);
    remove_action->setProperty("anExtensionManagerEntryId", QString::fromStdString(entry.id));
    remove_action->setProperty("anExtensionManagerActionType", QStringLiteral("remove"));
    remove_action->setProperty("anExtensionManagerDefaultText", remove_action->text());
    remove_action->setProperty("anExtensionManagerDefaultToolTip", remove_tooltip);
    remove_action->setProperty("anExtensionManagerActionAvailable", remove_available);
    remove_action->setToolTip(remove_tooltip);
    remove_action->setEnabled(remove_available && !m_RepositoryRefreshInProgress);
    QObject::connect(remove_action, &QPushButton::clicked, card, [this, entry]() { startRemoveOperation(entry); });
    actions_layout->addWidget(remove_action);
    m_ActionButtons.push_back(remove_action);

    auto* details_button = new QToolButton(card->contentWidget());
    details_button->setObjectName(buttonObjectName(entry.id, "details"));
    details_button->setText(tr("Details"));
    details_button->setCheckable(true);
    details_button->setArrowType(Qt::RightArrow);
    details_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details_button->setEnabled(true);
    actions_layout->addWidget(details_button);
    m_DetailButtons.push_back(details_button);

    title_row->addLayout(actions_layout);
    layout->addLayout(title_row);

    auto* summary_label = new QLabel(extensionSummaryText(entry), card->contentWidget());
    summary_label->setObjectName(fieldObjectName(entry.id, "summary"));
    summary_label->setFont(t.typography.body);
    summary_label->setWordWrap(true);
    summary_label->setProperty("anStatusTone", QStringLiteral("secondary"));
    layout->addWidget(summary_label);

    const QString update_callout_text = updateVersionCalloutText(entry);
    if (!update_callout_text.isEmpty())
    {
        auto* update_callout = new QLabel(update_callout_text, card->contentWidget());
        update_callout->setObjectName(fieldObjectName(entry.id, "versionUpdateCallout"));
        update_callout->setFont(t.typography.caption);
        update_callout->setProperty("anStatusTone", QStringLiteral("warning"));
        update_callout->setWordWrap(true);
        layout->addWidget(update_callout);
    }

    auto* details_widget = new QWidget(card->contentWidget());
    details_widget->setObjectName(fieldObjectName(entry.id, "detailsPanel"));

    auto* details_layout = new QVBoxLayout(details_widget);
    details_layout->setObjectName(fieldObjectName(entry.id, "detailsLayout"));
    details_layout->setSpacing(t.spacing.sm);
    applyFlushMargins(details_layout);

    auto* grid = new QGridLayout();
    grid->setObjectName(fieldObjectName(entry.id, "metadataLayout"));
    applyFlushMargins(grid);
    grid->setHorizontalSpacing(t.spacing.md);
    grid->setVerticalSpacing(t.spacing.sm);
    details_layout->addLayout(grid);

    int row = 0;
    const bool link_urls = linkableExtensionMetadata(entry.selected_asset_signature_state);
    addMetadataSectionRow(grid, row++, tr("Overview"), fieldObjectName(entry.id, "overviewSection"));
    addMetadataRow(grid, row, 0, tr("Description:"), metadataText(entry.description),
                   fieldObjectName(entry.id, "description"), false, link_urls);
    addMetadataRow(grid, row++, 1, tr("State:"), installStateText(entry), fieldObjectName(entry.id, "state"));
    addMetadataRow(grid, row, 0, tr("Status:"), installedStatusText(entry), fieldObjectName(entry.id, "status"));
    addMetadataRow(grid, row++, 1, tr("Version:"), versionText(entry), fieldObjectName(entry.id, "version"));
    addMetadataRow(grid, row, 0, tr("Status Detail:"), installedStatusDetailText(entry),
                   fieldObjectName(entry.id, "statusDetail"), false, link_urls);
    addMetadataRow(grid, row++, 1, tr("Author:"), metadataText(entry.author), fieldObjectName(entry.id, "author"));
    addMetadataRow(grid, row, 0, tr("Publisher:"), metadataText(entry.publisher_id),
                   fieldObjectName(entry.id, "publisher"));
    addMetadataRow(grid, row++, 1, tr("License:"), metadataText(entry.license), fieldObjectName(entry.id, "license"));

    addMetadataSectionRow(grid, row++, tr("Provenance"), fieldObjectName(entry.id, "provenanceSection"));
    addMetadataRow(grid, row, 0, tr("Attribution:"), metadataText(entry.attribution),
                   fieldObjectName(entry.id, "attribution"), false, link_urls);
    addMetadataRow(grid, row++, 1, tr("Original Author:"), metadataText(entry.original_author),
                   fieldObjectName(entry.id, "originalAuthor"));
    addMetadataRow(grid, row, 0, tr("Original Project:"), metadataText(entry.original_project),
                   fieldObjectName(entry.id, "originalProject"));
    addMetadataRow(grid, row++, 1, tr("Original Source:"), metadataText(entry.original_source_url),
                   fieldObjectName(entry.id, "originalSource"), true, link_urls);
    addMetadataRow(grid, row, 0, tr("Upstream Commit:"), metadataText(entry.upstream_commit),
                   fieldObjectName(entry.id, "upstreamCommit"), true);
    addMetadataRow(grid, row++, 1, tr("Tags:"), tagsText(entry.tags), fieldObjectName(entry.id, "tags"));

    addMetadataSectionRow(grid, row++, tr("Links"), fieldObjectName(entry.id, "linksSection"));
    addMetadataRow(grid, row, 0, tr("Homepage:"), metadataText(entry.homepage_url),
                   fieldObjectName(entry.id, "homepage"), true, link_urls);
    addMetadataRow(grid, row++, 1, tr("Source:"), metadataText(entry.repository_url),
                   fieldObjectName(entry.id, "repository"), true, link_urls);
    addMetadataRow(grid, row++, 0, tr("Support:"), metadataText(entry.support_url),
                   fieldObjectName(entry.id, "support"), true, link_urls);

    addMetadataSectionRow(grid, row++, tr("Release"), fieldObjectName(entry.id, "releaseSection"));
    addMetadataRow(grid, row, 0, tr("SDK:"), releaseSdkText(entry), fieldObjectName(entry.id, "sdk"));
    addMetadataRow(grid, row++, 1, tr("Launch:"), launchModeText(entry.release_launch_mode),
                   fieldObjectName(entry.id, "launchMode"));
    addMetadataRow(grid, row, 0, tr("Runtime Targets:"), tagsText(entry.release_runtime_targets),
                   fieldObjectName(entry.id, "runtimeTargets"));
    addMetadataRow(grid, row++, 1, tr("Published:"), metadataText(entry.release_published_at_utc),
                   fieldObjectName(entry.id, "published"));
    addMetadataRow(grid, row, 0, tr("Changelog:"), metadataText(entry.release_changelog),
                   fieldObjectName(entry.id, "changelog"), false, link_urls);
    addMetadataRow(grid, row++, 1, tr("Asset:"), assetText(entry), fieldObjectName(entry.id, "asset"));

    addMetadataSectionRow(grid, row++, tr("Trust"), fieldObjectName(entry.id, "trustSection"));
    addMetadataBadgeRow(grid, row, 0, tr("Signature:"), signatureStateText(entry.selected_asset_signature_state),
                        signatureBadgeStatus(entry.selected_asset_signature_state),
                        fieldObjectName(entry.id, "signature"));
    addMetadataRow(grid, row++, 1, tr("Signature Key:"), metadataText(entry.selected_asset_signature_key_id),
                   fieldObjectName(entry.id, "signatureKey"), true);
    addMetadataRow(grid, row, 0, tr("Package Hash:"), metadataText(entry.selected_asset_package_hash_sha256),
                   fieldObjectName(entry.id, "hash"), true);
    addMetadataRow(grid, row++, 1, tr("Compatibility:"), compatibilityText(entry),
                   fieldObjectName(entry.id, "compatibility"));

    if (!entry.compatibility_diagnostic.empty() || !entry.selected_asset_signature_detail.empty())
    {
        const QString detail = !entry.compatibility_diagnostic.empty()
                                   ? QString::fromStdString(entry.compatibility_diagnostic)
                                   : QString::fromStdString(entry.selected_asset_signature_detail);
        auto* detail_banner =
            new AnInfoBanner(detail, entry.runtime_compatible ? AnInfoBanner::Tone::Info : AnInfoBanner::Tone::Warning,
                             card->contentWidget());
        detail_banner->setObjectName(fieldObjectName(entry.id, "detailBanner"));
        details_layout->addWidget(detail_banner);
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
    updateActionButtonsForOperationState();
    return card;
}

} // namespace an::ui
