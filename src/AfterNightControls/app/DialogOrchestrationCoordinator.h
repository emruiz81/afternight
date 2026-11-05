// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "app/ExtensionManager.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QString>
#include <QStringList>

class MainDashboard;
class GlobalSettingsPersistence;
class QWidget;

namespace an::ui
{

inline constexpr size_t kSettingsPageCount = 7;
inline constexpr size_t kExtensionsDialogPageCount = 2;
inline constexpr size_t kDefaultRecentFilesLimit = 20;
inline constexpr const char* kAfterNightUserGuideOnlineUrl = "https://emruiz81.github.io/afternight/";
inline constexpr const char* kAfterNightDisableOnlineUserGuideEnv = "AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE";

inline size_t settingsPageRegistrationCount()
{
    return kSettingsPageCount;
}

inline size_t extensionsDialogPageRegistrationCount()
{
    return kExtensionsDialogPageCount;
}

inline size_t computeRecentFilesTrimCount(const size_t currentCount, const size_t limit)
{
    return currentCount > limit ? currentCount - limit : 0;
}

inline int firstExistingHelpCandidateIndex(const std::vector<bool>& candidateExists)
{
    for (size_t i = 0; i < candidateExists.size(); ++i)
    {
        if (candidateExists[i])
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

[[nodiscard]] inline QStringList bundledUserGuideCandidatePaths(const QString& applicationDir)
{
    const QDir appDir(applicationDir);
    return {
        appDir.filePath(QStringLiteral("docs/index.html")),
        appDir.filePath(QStringLiteral("../share/doc/AfterNight/index.html")),
        appDir.filePath(QStringLiteral("../../../docs/user-guide/site/index.html")),
        appDir.filePath(QStringLiteral("../../docs/user-guide/site/index.html")),
    };
}

[[nodiscard]] inline bool onlineUserGuideFallbackAllowed(QString policyValue)
{
    policyValue = policyValue.trimmed().toLower();
    return !(policyValue == QStringLiteral("1") || policyValue == QStringLiteral("true") ||
             policyValue == QStringLiteral("yes") || policyValue == QStringLiteral("on"));
}

[[nodiscard]] inline bool onlineUserGuideFallbackAllowed()
{
    return onlineUserGuideFallbackAllowed(qEnvironmentVariable(kAfterNightDisableOnlineUserGuideEnv));
}

inline QWidget* resolveDialogParent(QWidget* fallbackParent, QWidget* requestedParent)
{
    return requestedParent ? requestedParent : fallbackParent;
}

struct ExtensionStartupUpdateCheckResult
{
    bool success = false;
    QString diagnostic;
    std::vector<extensions::ExtensionRepositoryBrowseEntry> updates;
};

[[nodiscard]] inline QString
extensionStartupUpdateEntryDisplayName(const extensions::ExtensionRepositoryBrowseEntry& entry)
{
    if (!entry.name.empty())
    {
        return QString::fromStdString(entry.name);
    }
    if (!entry.id.empty())
    {
        return QString::fromStdString(entry.id);
    }

    return QStringLiteral("Extension");
}

[[nodiscard]] inline int
countExtensionStartupUpdates(const std::vector<extensions::ExtensionRepositoryBrowseEntry>& entries)
{
    int count = 0;
    for (const auto& entry : entries)
    {
        if (entry.install_state == extensions::ExtensionRepositoryInstallState::UpdateAvailable)
        {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] inline bool shouldNotifyExtensionStartupUpdates(const ExtensionStartupUpdateCheckResult& result)
{
    return result.success && !result.updates.empty();
}

[[nodiscard]] inline QString extensionStartupUpdateNotificationText(const ExtensionStartupUpdateCheckResult& result)
{
    if (result.updates.empty())
    {
        return QCoreApplication::translate("ExtensionStartupUpdates", "No extension updates are available.");
    }

    QStringList updateLines;
    const int displayedCount = (std::min)(static_cast<int>(result.updates.size()), 4);
    updateLines.reserve(displayedCount + 1);
    for (int i = 0; i < displayedCount; ++i)
    {
        const auto& entry = result.updates[static_cast<std::size_t>(i)];
        updateLines.push_back(QCoreApplication::translate("ExtensionStartupUpdates", "%1: %2 -> %3")
                                  .arg(extensionStartupUpdateEntryDisplayName(entry),
                                       QString::fromStdString(entry.installed_version),
                                       QString::fromStdString(entry.latest_version)));
    }
    if (static_cast<int>(result.updates.size()) > displayedCount)
    {
        updateLines.push_back(QCoreApplication::translate("ExtensionStartupUpdates", "And %1 more...")
                                  .arg(static_cast<int>(result.updates.size()) - displayedCount));
    }

    if (result.updates.size() == 1u)
    {
        return QCoreApplication::translate("ExtensionStartupUpdates",
                                           "An installed extension has an update available:\n\n%1")
            .arg(updateLines.join(QStringLiteral("\n")));
    }

    return QCoreApplication::translate("ExtensionStartupUpdates",
                                       "%1 installed extensions have updates available:\n\n%2")
        .arg(static_cast<int>(result.updates.size()))
        .arg(updateLines.join(QStringLiteral("\n")));
}

class AFTERNIGHTCONTROLS_API DialogOrchestrationCoordinator
{
public:
    struct Dependencies
    {
        QWidget* mainWindow = nullptr;
        bool* darkMode = nullptr;
        std::deque<std::string>* recentFiles = nullptr;
        MainDashboard** mainDashboard = nullptr;
        extensions::ExtensionOperationCancellationToken extensionBackgroundWorkCancellationToken;

        std::function<GlobalSettingsPersistence*()> globalSettingsPersistence;
        std::function<size_t()> getRecentFilesLimit;
        std::function<QIcon(const std::string&)> iconProvider;
        std::function<void(const std::string&)> removeRecentFileThumbnail;
        std::function<void()> persistRecentFiles;
        std::function<void()> updateRecentFilesMenuEntries;
        // Called on the GUI thread each time the Settings dialog applies what it holds, after its pages have stored it.
        std::function<void()> settingsApplied;
    };

    explicit DialogOrchestrationCoordinator(const Dependencies& dependencies);

    void about() const;
    void openHelp() const;
    void openSettings() const;
    void openSettingsAt(const QString& pageName) const;
    void openExtensions() const;
    void openExtensionsAt(const QString& pageName) const;
    [[nodiscard]] bool openExtensionSettingsDialog(std::string_view extension_id, QString* diagnostic = nullptr) const;
    void checkExtensionUpdatesOnStartup() const;
    void systemInformation() const;

    void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text) const;
    void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) const;
    void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) const;
    void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret) const;
    void openMessageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn,
                             std::function<void(int result)> onFinished) const;

private:
    Dependencies m_dependencies;
};

} // namespace an::ui
