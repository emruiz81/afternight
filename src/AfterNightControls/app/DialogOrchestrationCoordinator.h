// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "app/ExtensionManager.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QUrl>

class MainDashboard;
class GlobalSettingsPersistence;
class QWidget;

namespace an::ui
{

inline constexpr size_t kSettingsPageCount = 7;
inline constexpr size_t kExtensionsDialogPageCount = 2;
inline constexpr size_t kDefaultRecentFilesLimit = 20;
inline constexpr const char* kAfterNightUserGuideOnlineUrl = "https://docs.afternightastro.org/";
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

/// The page of the user guide a help path names: the path itself when it names a page inside the guide - a relative
/// path of forward slashes that ends in `.html` and climbs out of nothing - and the guide's home page otherwise, an
/// empty path among them. A path is joined to a folder and to an address, so one that could leave either is not
/// followed.
///
/// @param helpPath a page relative to the guide's root, as a process descriptor states it; may be empty.
/// @return the page to open, relative to the guide's root.
[[nodiscard]] inline QString userGuidePage(const QString& helpPath)
{
    const QString home = QStringLiteral("index.html");
    if (!helpPath.endsWith(QStringLiteral(".html")) || helpPath.startsWith(QLatin1Char('/')) ||
        helpPath.contains(QLatin1Char('\\')) || helpPath.contains(QLatin1Char(':')) ||
        helpPath.contains(QLatin1Char('?')) || helpPath.contains(QLatin1Char('#')))
    {
        return home;
    }
    for (const QString& part : helpPath.split(QLatin1Char('/')))
    {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
        {
            return home;
        }
    }
    return helpPath;
}

/// Where a page of the bundled user guide is looked for, in order: beside the application, where an installation puts
/// the guide, and in the source tree's built site.
///
/// @param applicationDir the folder the application runs from.
/// @param helpPath the page, relative to the guide's root; the home page when empty or no page of the guide.
[[nodiscard]] inline QStringList bundledUserGuideCandidatePaths(const QString& applicationDir,
                                                                const QString& helpPath = QString())
{
    const QDir appDir(applicationDir);
    const QString page = userGuidePage(helpPath);
    return {
        appDir.filePath(QStringLiteral("docs/") + page),
        appDir.filePath(QStringLiteral("../share/doc/AfterNight/") + page),
        appDir.filePath(QStringLiteral("../../../docs/user-guide/site/") + page),
        appDir.filePath(QStringLiteral("../../docs/user-guide/site/") + page),
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

/// The address of a page in the online user guide, which publishes the pages under the paths the bundled guide has;
/// nothing when the policy value turns the online guide off.
///
/// @param helpPath the page, relative to the guide's root; the guide's own address when empty or no page of the guide.
/// @param policyValue the value of the policy that turns the online guide off.
[[nodiscard]] inline std::optional<QUrl> onlineUserGuideUrl(const QString& helpPath, const QString& policyValue)
{
    if (!onlineUserGuideFallbackAllowed(policyValue))
    {
        return std::nullopt;
    }
    const QString page = userGuidePage(helpPath);
    const QString address = QString::fromLatin1(kAfterNightUserGuideOnlineUrl);
    return QUrl(page == QStringLiteral("index.html") ? address : address + page);
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

    /// Opens the user guide at the page of the process window the keyboard focus is in, and at its home page when the
    /// focus is in no process window that states a page: from the bundled guide first, and from the online guide
    /// when the bundled page cannot be opened and the policy allows it.
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
