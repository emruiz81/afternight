// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/DialogOrchestrationCoordinator.h"

#include "controls/AboutDialog.h"
#include "controls/MainDashboard.h"
#include "controls/SettingsDialog.h"
#include "controls/SystemInformationDialog.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/CatalogsDataSettingsPage.h"
#include "controls/settings/ExportSettingsPage.h"
#include "controls/settings/ExtensionManagerPage.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "controls/settings/ExternalToolsSettingsPage.h"
#include "controls/settings/GeneralSettingsPage.h"
#include "controls/settings/InterfaceSettingsPage.h"
#include "controls/settings/LoggingSettingsPage.h"
#include "controls/settings/PlateSolvingSettingsPage.h"
#include "controls/settings/ProcessingSettingsPage.h"
#include "controls/settings/RawImageSettingsPage.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsPage.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>

#include "app/DesktopUtils.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <memory>

namespace
{

// Give activation reconciliation time to populate the startup catalog so the
// update check does not launch a duplicate cold discovery pass.
constexpr int kExtensionStartupUpdateDelayMs = 3000;

class ScopedTopLevelFlagRestorer
{
public:
    explicit ScopedTopLevelFlagRestorer(const Qt::WindowFlags clearFlag = Qt::WindowStaysOnTopHint)
        : m_clearFlag(clearFlag)
    {
        const auto topLevels = QApplication::topLevelWidgets();
        for (QWidget* widget : topLevels)
        {
            if (!widget || !widget->isVisible())
            {
                continue;
            }

            const Qt::WindowFlags flags = widget->windowFlags();
            if (flags & clearFlag)
            {
                m_originalFlags[widget] = flags;
                widget->setWindowFlags(flags & ~clearFlag);
                widget->hide();
                widget->show();
            }
        }
    }

    ~ScopedTopLevelFlagRestorer()
    {
        for (auto it = m_originalFlags.begin(); it != m_originalFlags.end(); ++it)
        {
            QWidget* widget = it.key();
            const Qt::WindowFlags flags = it.value();
            if (widget)
            {
                widget->setWindowFlags(flags);
                widget->hide();
                widget->show();
            }
        }
    }

private:
    QMap<QWidget*, Qt::WindowFlags> m_originalFlags;
    Qt::WindowFlags m_clearFlag;
};

void applyMessageBoxButtonRole(QMessageBox& messageBox, const QMessageBox::StandardButton standardButton,
                               const char* role)
{
    if (!role)
    {
        return;
    }

    if (auto* button = messageBox.button(standardButton))
    {
        button->setProperty("anProcessButtonRole", QString::fromUtf8(role));
    }
}

void applyModernMessageBoxButtonRoles(QMessageBox& messageBox)
{
    applyMessageBoxButtonRole(messageBox, QMessageBox::Ok, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Yes, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::YesToAll, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Save, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::SaveAll, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Open, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Apply, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Retry, "primary");

    applyMessageBoxButtonRole(messageBox, QMessageBox::No, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::NoToAll, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Cancel, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Close, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Ignore, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Help, "secondary");

    applyMessageBoxButtonRole(messageBox, QMessageBox::Reset, "neutral");

    applyMessageBoxButtonRole(messageBox, QMessageBox::Discard, "danger");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Abort, "danger");
}

QMessageBox::StandardButton
showModernMessageBox(QWidget* parent, const QMessageBox::Icon icon, const QString& title, const QString& text,
                     const QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                     const QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    QMessageBox messageBox(parent);
    messageBox.setObjectName(QStringLiteral("anMessageBox"));
    messageBox.setProperty("anDialogVariant", QStringLiteral("messageBox"));
    messageBox.setIcon(icon);
    messageBox.setWindowTitle(title);
    messageBox.setText(text);
    messageBox.setStandardButtons(buttons);
    messageBox.setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    messageBox.setTextInteractionFlags(Qt::TextSelectableByMouse);

    if (defaultButton != QMessageBox::NoButton)
    {
        messageBox.setDefaultButton(defaultButton);
    }

    applyModernMessageBoxButtonRoles(messageBox);

    return static_cast<QMessageBox::StandardButton>(messageBox.exec());
}

[[nodiscard]] QString extensionDialogTitle()
{
    return QCoreApplication::translate("ExtensionsDialog", "Extensions");
}

[[nodiscard]] QString extensionBrowseTabTitle()
{
    return QCoreApplication::translate("ExtensionsDialog", "Browse");
}

[[nodiscard]] QString extensionInstalledTabTitle()
{
    return QCoreApplication::translate("ExtensionsDialog", "Installed");
}

[[nodiscard]] QString extensionManagerPageTitle()
{
    return QCoreApplication::translate("ExtensionManagerPage", "Extension Manager");
}

[[nodiscard]] QString extensionStatusPageTitle()
{
    return QCoreApplication::translate("ExtensionStatusPage", "Extensions");
}

[[nodiscard]] bool pageTitleMatches(const QString& requested, const QString& candidate)
{
    return QString::compare(requested.trimmed(), candidate.trimmed(), Qt::CaseInsensitive) == 0;
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

void logStartupExtensionUpdateDebug(const QString& message)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_DEBUG("Startup extension update check: {}", message.toStdString());
}

void logStartupExtensionUpdateInfo(const QString& message)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_INFO("Startup extension update check: {}", message.toStdString());
}

void logStartupExtensionUpdateWarning(const QString& message)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_WARNING("Startup extension update check: {}", message.toStdString());
}

an::ui::ExtensionStartupUpdateCheckResult
collectStartupExtensionUpdates(const an::ui::extensions::ExtensionOperationCancellationToken& cancellation_token)
{
    an::ui::ExtensionStartupUpdateCheckResult result;
    if (cancellation_token.isCancellationRequested())
    {
        return result;
    }

    if (an::ui::extensions::extensionNetworkDisabledByPolicy())
    {
        result.diagnostic = QCoreApplication::translate(
            "ExtensionStartupUpdates", "Extension repository network features are disabled by policy.");
        logStartupExtensionUpdateWarning(result.diagnostic);
        return result;
    }

    an::ui::extensions::ExtensionRepositoryIndexFetchRequest request;
    request.timeout_ms = 10000;
    request.cancellation_token = cancellation_token;
    logStartupExtensionUpdateInfo(
        QCoreApplication::translate("ExtensionStartupUpdates", "Checking official repository for extension updates"));
    logStartupExtensionUpdateDebug(
        QCoreApplication::translate("ExtensionStartupUpdates", "Fetch timeout is %1ms").arg(request.timeout_ms));

    an::ui::extensions::ExtensionRepositoryClient client;
    const auto fetch = client.fetchIndex(request);
    if (cancellation_token.isCancellationRequested())
    {
        return result;
    }
    result.success = fetch.success;
    result.diagnostic = QString::fromStdString(fetch.diagnostic);
    if (!fetch.success)
    {
        logStartupExtensionUpdateWarning(result.diagnostic);
        return result;
    }

    an::ui::extensions::ExtensionDiscoveryOptions discovery_options =
        an::ui::extensions::default_extension_discovery_options();
    discovery_options.cancellation_token = cancellation_token;

    logStartupExtensionUpdateDebug(
        QCoreApplication::translate("ExtensionStartupUpdates", "Loading installed extension activation catalog"));
    std::vector<an::ui::extensions::ExtensionLoadResult> installed =
        an::ui::extensions::load_ready_extension_startup_catalog(discovery_options);
    if (installed.empty())
    {
        logStartupExtensionUpdateDebug(
            QCoreApplication::translate("ExtensionStartupUpdates", "Discovering installed extension manifests"));
        an::ui::extensions::ExtensionManager manager;
        installed = manager.discoverExtensions(discovery_options);
        if (cancellation_token.isCancellationRequested())
        {
            return result;
        }
    }
    installed.erase(std::remove_if(installed.begin(), installed.end(),
                                   [](const an::ui::extensions::ExtensionLoadResult& load_result) {
                                       return !load_result.manifest.has_value();
                                   }),
                    installed.end());
    logStartupExtensionUpdateDebug(
        QCoreApplication::translate("ExtensionStartupUpdates", "Discovered %1 installed extension manifest(s)")
            .arg(static_cast<int>(installed.size())));

    an::ui::extensions::ExtensionRepositoryBrowseOptions browse_options;
    browse_options.context = discovery_options.context;
    const std::vector<an::ui::extensions::ExtensionRepositoryBrowseEntry> entries =
        an::ui::extensions::build_extension_repository_browse_entries(fetch.validation.index, installed,
                                                                      browse_options);
    logStartupExtensionUpdateDebug(
        QCoreApplication::translate("ExtensionStartupUpdates", "Compared %1 repository extension(s) with installed set")
            .arg(static_cast<int>(entries.size())));

    result.updates.reserve(static_cast<std::size_t>(an::ui::countExtensionStartupUpdates(entries)));
    for (const auto& entry : entries)
    {
        if (entry.install_state == an::ui::extensions::ExtensionRepositoryInstallState::UpdateAvailable)
        {
            result.updates.push_back(entry);
        }
    }
    logStartupExtensionUpdateInfo(
        QCoreApplication::translate("ExtensionStartupUpdates", "Startup extension update check found %1 update(s)")
            .arg(static_cast<int>(result.updates.size())));
    return result;
}

class ExtensionsDialog final : public QDialog
{
public:
    explicit ExtensionsDialog(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setupUi(settingsPersistence);
        setupConnections();

        setObjectName(QStringLiteral("extensionsDialog"));
        setProperty("anDialogVariant", QStringLiteral("settings"));
        setWindowTitle(extensionDialogTitle());
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        setMinimumSize(860, 640);
        resize(980, 740);

        onCurrentTabChanged(m_Tabs->currentIndex());
        updateUiState();
    }

    void setCurrentPage(const QString& pageName)
    {
        const int index = resolvePageIndex(pageName);
        if (index >= 0)
        {
            m_Tabs->setCurrentIndex(index);
        }
    }

    void accept() override
    {
        if (!saveLoadedPages())
        {
            return;
        }

        QDialog::accept();
    }

    void reject() override
    {
        if (hasUnsavedChanges() && !confirmDiscardChanges())
        {
            return;
        }

        QDialog::reject();
    }

    [[nodiscard]] bool openExtensionSettingsById(const std::string_view extension_id, QString* diagnostic)
    {
        an::ui::extensions::ExtensionRepositoryBrowseEntry entry;
        entry.id = std::string(extension_id);
        return openInstalledExtensionSettings(entry, diagnostic);
    }

private:
    void setupUi(GlobalSettingsPersistence* settingsPersistence)
    {
        const auto& spacing = an::ui::tokens().spacing;

        auto* root_layout = new QVBoxLayout(this);
        root_layout->setContentsMargins(spacing.lg, spacing.lg, spacing.lg, spacing.lg);
        root_layout->setSpacing(spacing.md);

        m_Tabs = new QTabWidget(this);
        m_Tabs->setObjectName(QStringLiteral("anExtensionsTabs"));
        m_Tabs->setDocumentMode(true);
        if (auto* tab_bar = m_Tabs->tabBar())
        {
            tab_bar->setObjectName(QStringLiteral("anExtensionsTabBar"));
            tab_bar->setExpanding(false);
            tab_bar->setElideMode(Qt::ElideNone);
            tab_bar->setUsesScrollButtons(false);
            tab_bar->setDrawBase(false);
            tab_bar->setProperty("anTabCompact", true);
            tab_bar->setProperty("anTabHeightScale", 1.0);
            tab_bar->setProperty("anTabMinWidth", 170);
            tab_bar->setProperty("anTabMinHeight", 22);
            tab_bar->setProperty("anTabAccentLinePosition", QStringLiteral("top"));
        }
        root_layout->addWidget(m_Tabs, 1);

        m_ManagerPage = new an::ui::ExtensionManagerPage(settingsPersistence, m_Tabs);
        m_StatusPage = new ExtensionStatusPage(settingsPersistence, m_Tabs);
        m_Pages = {m_ManagerPage, m_StatusPage};

        m_Tabs->addTab(m_ManagerPage, m_ManagerPage->pageIcon(), extensionBrowseTabTitle());
        m_Tabs->addTab(m_StatusPage, m_StatusPage->pageIcon(), extensionInstalledTabTitle());

        auto* button_bar = new QWidget(this);
        auto* button_layout = new QHBoxLayout(button_bar);
        button_layout->setContentsMargins(0, 0, 0, 0);
        button_layout->setSpacing(spacing.sm);

        m_ResetButton = new QPushButton(tr("Reset Tab"), button_bar);
        m_ResetButton->setToolTip(tr("Reset the current extension tab to defaults"));
        applyButtonRole(m_ResetButton, QStringLiteral("neutral"));
        button_layout->addWidget(m_ResetButton);

        button_layout->addStretch();

        m_CloseButton = new QPushButton(tr("Close"), button_bar);
        m_CloseButton->setObjectName(QStringLiteral("anExtensionsCloseButton"));
        m_CloseButton->setToolTip(tr("Close the extension manager"));
        m_CloseButton->setDefault(true);
        applyButtonRole(m_CloseButton, QStringLiteral("primary"));
        button_layout->addWidget(m_CloseButton);

        root_layout->addWidget(button_bar);
    }

    void setupConnections()
    {
        connect(m_Tabs, &QTabWidget::currentChanged, this, [this](const int index) { onCurrentTabChanged(index); });
        connect(m_ResetButton, &QPushButton::clicked, this, [this]() { resetCurrentPageToDefaults(); });
        connect(m_CloseButton, &QPushButton::clicked, this, &ExtensionsDialog::reject);

        for (SettingsPage* page : m_Pages)
        {
            connect(page, &SettingsPage::settingsModified, this, [this]() { updateUiState(); });
        }

        connect(m_ManagerPage, &an::ui::ExtensionManagerPage::installedExtensionsChanged, this, [this]() {
            if (!m_StatusPage)
            {
                an::ui::invalidateInstalledExtensionStatusCache(true);
                return;
            }

            m_StatusPage->setSettingsLoaded(false);
            if (m_CurrentPage == m_StatusPage)
            {
                an::ui::invalidateInstalledExtensionStatusCache(false);
                m_StatusPage->refreshStatus(true);
                m_StatusPage->setSettingsLoaded(true);
                return;
            }

            an::ui::invalidateInstalledExtensionStatusCache(true);
        });
    }

    [[nodiscard]] SettingsPage* pageAt(const int index) const
    {
        if (!m_Tabs || index < 0 || index >= m_Tabs->count())
        {
            return nullptr;
        }

        return dynamic_cast<SettingsPage*>(m_Tabs->widget(index));
    }

    [[nodiscard]] int resolvePageIndex(const QString& pageName) const
    {
        const QString requested = pageName.trimmed();
        if (requested.isEmpty())
        {
            return -1;
        }

        if (pageTitleMatches(requested, extensionBrowseTabTitle()) ||
            pageTitleMatches(requested, extensionManagerPageTitle()))
        {
            return 0;
        }

        if (pageTitleMatches(requested, extensionInstalledTabTitle()) ||
            pageTitleMatches(requested, extensionStatusPageTitle()))
        {
            return 1;
        }

        for (int i = 0; i < m_Tabs->count(); ++i)
        {
            SettingsPage* page = pageAt(i);
            if (pageTitleMatches(requested, m_Tabs->tabText(i)) ||
                (page && pageTitleMatches(requested, page->pageTitle())))
            {
                return i;
            }
        }

        return -1;
    }

    void onCurrentTabChanged(const int index)
    {
        SettingsPage* page = pageAt(index);
        if (m_CurrentPage == page)
        {
            refreshPageIfAppropriate(page);
            updateUiState();
            return;
        }

        if (m_CurrentPage)
        {
            m_CurrentPage->onPageDeactivated();
        }

        m_CurrentPage = page;
        refreshPageIfAppropriate(m_CurrentPage);
        if (m_CurrentPage)
        {
            m_CurrentPage->onPageActivated();
        }
        updateUiState();
    }

    void refreshPageIfAppropriate(SettingsPage* page)
    {
        if (!page)
        {
            return;
        }

        if (!page->isSettingsLoaded())
        {
            page->loadSettings();
            page->setSettingsLoaded(true);
        }
    }

    [[nodiscard]] bool validateLoadedPages()
    {
        for (int i = 0; i < m_Tabs->count(); ++i)
        {
            SettingsPage* page = pageAt(i);
            if (!page || !page->isSettingsLoaded())
            {
                continue;
            }

            const ValidationResult result = page->validateSettings();
            if (result.isValid)
            {
                continue;
            }

            m_Tabs->setCurrentIndex(i);
            showModernMessageBox(this, QMessageBox::Warning, tr("Validation Error"), result.errorMessage,
                                 QMessageBox::Ok, QMessageBox::Ok);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool saveLoadedPages()
    {
        if (!validateLoadedPages())
        {
            return false;
        }

        for (SettingsPage* page : m_Pages)
        {
            if (page && page->isEnabled() && page->isSettingsLoaded())
            {
                page->saveSettings();
                page->setSettingsLoaded(true);
            }
        }

        updateUiState();
        return true;
    }

    [[nodiscard]] bool hasUnsavedChanges() const
    {
        for (const SettingsPage* page : m_Pages)
        {
            if (page && page->hasUnsavedChanges())
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool confirmDiscardChanges()
    {
        const auto result = showModernMessageBox(this, QMessageBox::Question, tr("Unsaved Changes"),
                                                 tr("You have unsaved extension changes. Do you want to discard them?"),
                                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        return result == QMessageBox::Discard;
    }

    void resetCurrentPageToDefaults()
    {
        SettingsPage* page = pageAt(m_Tabs->currentIndex());
        if (!page)
        {
            return;
        }

        const auto result = showModernMessageBox(this, QMessageBox::Question, tr("Reset Extensions"),
                                                 tr("Are you sure you want to reset the \"%1\" tab to defaults?")
                                                     .arg(m_Tabs->tabText(m_Tabs->currentIndex())),
                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes)
        {
            return;
        }

        page->resetToDefaults();
        page->setSettingsLoaded(true);
        updateUiState();
    }

    [[nodiscard]] bool openInstalledExtensionSettings(const an::ui::extensions::ExtensionRepositoryBrowseEntry& entry,
                                                      QString* diagnostic)
    {
        if (entry.id.empty())
        {
            if (diagnostic)
            {
                *diagnostic = tr("Extension id is required to open settings.");
            }
            return false;
        }

        setCurrentPage(extensionInstalledTabTitle());
        if (m_StatusPage && !m_StatusPage->isSettingsLoaded())
        {
            m_StatusPage->loadSettings();
            m_StatusPage->setSettingsLoaded(true);
        }

        const bool focused = m_StatusPage && m_StatusPage->openExtensionSettingsDialog(entry.id);
        if (!focused && diagnostic)
        {
            *diagnostic = tr("Extension settings are not available yet. Refresh the Installed tab after the extension "
                             "reaches Ready state.");
        }
        return true;
    }

    void updateUiState()
    {
        const bool has_pages = !m_Pages.isEmpty();
        const bool has_changes = hasUnsavedChanges();

        m_ResetButton->setEnabled(has_pages && pageAt(m_Tabs->currentIndex()));
        m_CloseButton->setEnabled(has_pages);

        const QString base_title = extensionDialogTitle();
        setWindowTitle(has_changes ? base_title + QStringLiteral(" *") : base_title);
    }

    QTabWidget* m_Tabs = nullptr;
    an::ui::ExtensionManagerPage* m_ManagerPage = nullptr;
    ExtensionStatusPage* m_StatusPage = nullptr;
    SettingsPage* m_CurrentPage = nullptr;
    QPushButton* m_ResetButton = nullptr;
    QPushButton* m_CloseButton = nullptr;
    QList<SettingsPage*> m_Pages;
};

} // anonymous namespace

namespace an::ui
{

DialogOrchestrationCoordinator::DialogOrchestrationCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void DialogOrchestrationCoordinator::about() const
{
    if (!m_dependencies.mainWindow)
    {
        return;
    }

    ScopedTopLevelFlagRestorer _;
    AboutDialog dialog(m_dependencies.mainWindow, m_dependencies.darkMode ? *m_dependencies.darkMode : false);
    dialog.exec();
}

void DialogOrchestrationCoordinator::openHelp() const
{
    const QStringList candidates = bundledUserGuideCandidatePaths(QCoreApplication::applicationDirPath());
    for (const auto& candidate : candidates)
    {
        const QFileInfo info(candidate);
        if (!info.exists() || !info.isFile())
        {
            continue;
        }

        if (an::ui::openLocalFile(info.absoluteFilePath()))
        {
            return;
        }

        LOG_WARNING("Failed to open local AfterNight user guide at '{}'", info.absoluteFilePath().toStdString());
    }

    if (!onlineUserGuideFallbackAllowed())
    {
        LOG_WARNING("AfterNight bundled user guide was not found or could not be opened. Online fallback is disabled "
                    "by {}. Checked candidates: {}",
                    kAfterNightDisableOnlineUserGuideEnv, candidates.join(QStringLiteral("; ")).toStdString());
        notifyInfoMessage(m_dependencies.mainWindow, QObject::tr("Help"),
                          QObject::tr("The offline AfterNight User Guide could not be opened, and online fallback is "
                                      "disabled by policy."));
        return;
    }

    const QUrl onlineUrl(QString::fromLatin1(kAfterNightUserGuideOnlineUrl));
    if (QDesktopServices::openUrl(onlineUrl))
    {
        return;
    }

    LOG_WARNING("Failed to open AfterNight user guide locally or online at '{}'. Checked candidates: {}",
                kAfterNightUserGuideOnlineUrl, candidates.join(QStringLiteral("; ")).toStdString());
    notifyInfoMessage(m_dependencies.mainWindow, QObject::tr("Help"),
                      QObject::tr("The AfterNight User Guide could not be opened locally or online."));
}

void DialogOrchestrationCoordinator::openSettings() const
{
    openSettingsAt(QString());
}

void DialogOrchestrationCoordinator::openSettingsAt(const QString& pageName) const
{
    if (!m_dependencies.mainWindow)
    {
        return;
    }

    ScopedTopLevelFlagRestorer _;
    SettingsDialog dialog(m_dependencies.mainWindow);
    GlobalSettingsPersistence* settingsPersistence =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    dialog.addPage(new GeneralSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new RawImageSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new ExternalToolsSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new InterfaceSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new ExportSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new ProcessingSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new LoggingSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new CatalogsDataSettingsPage(settingsPersistence, &dialog));
    dialog.addPage(new PlateSolvingSettingsPage(settingsPersistence, &dialog));

    QObject::connect(&dialog, &SettingsDialog::settingsApplied, m_dependencies.mainWindow, [this]() {
        if (m_dependencies.settingsApplied)
        {
            m_dependencies.settingsApplied();
        }
        if (!m_dependencies.recentFiles)
        {
            return;
        }

        const size_t newLimit =
            m_dependencies.getRecentFilesLimit ? m_dependencies.getRecentFilesLimit() : kDefaultRecentFilesLimit;
        bool listChanged = false;

        while (m_dependencies.recentFiles->size() > newLimit)
        {
            if (m_dependencies.removeRecentFileThumbnail)
            {
                m_dependencies.removeRecentFileThumbnail(m_dependencies.recentFiles->back());
            }
            m_dependencies.recentFiles->pop_back();
            listChanged = true;
        }

        if (!listChanged)
        {
            return;
        }

        if (m_dependencies.persistRecentFiles)
        {
            m_dependencies.persistRecentFiles();
        }
        if (m_dependencies.updateRecentFilesMenuEntries)
        {
            m_dependencies.updateRecentFilesMenuEntries();
        }
        if (m_dependencies.mainDashboard && *m_dependencies.mainDashboard)
        {
            (*m_dependencies.mainDashboard)->reloadRecentFilesThumbnails();
        }
    });

    if (!pageName.isEmpty())
    {
        dialog.setCurrentPage(pageName);
    }

    dialog.exec();
}

void DialogOrchestrationCoordinator::openExtensions() const
{
    openExtensionsAt(QString());
}

void DialogOrchestrationCoordinator::openExtensionsAt(const QString& pageName) const
{
    if (!m_dependencies.mainWindow)
    {
        return;
    }

    ScopedTopLevelFlagRestorer _;
    GlobalSettingsPersistence* settingsPersistence =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    ExtensionsDialog dialog(settingsPersistence, m_dependencies.mainWindow);
    if (!pageName.isEmpty())
    {
        dialog.setCurrentPage(pageName);
    }

    dialog.exec();
}

bool DialogOrchestrationCoordinator::openExtensionSettingsDialog(const std::string_view extension_id,
                                                                 QString* diagnostic) const
{
    if (extension_id.empty())
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("Extension id is required to open settings.");
        }
        return false;
    }
    if (!m_dependencies.mainWindow)
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("The main window is unavailable.");
        }
        return false;
    }

    ScopedTopLevelFlagRestorer _;
    GlobalSettingsPersistence* settingsPersistence =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    ExtensionsDialog dialog(settingsPersistence, m_dependencies.mainWindow);
    QString local_diagnostic;
    (void)dialog.openExtensionSettingsById(extension_id, &local_diagnostic);
    if (!local_diagnostic.isEmpty())
    {
        if (diagnostic)
        {
            *diagnostic = local_diagnostic;
        }
        dialog.exec();
        return false;
    }

    dialog.exec();
    return true;
}

void DialogOrchestrationCoordinator::checkExtensionUpdatesOnStartup() const
{
    if (!m_dependencies.mainWindow)
    {
        return;
    }

    GlobalSettingsPersistence* settingsPersistence =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;
    if (!settingsPersistence)
    {
        return;
    }

    const QString enabled =
        settingsPersistence->select(SettingsKeys::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP,
                                    SettingsDefaults::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP ? "1" : "0");
    if (enabled != QStringLiteral("1"))
    {
        logStartupExtensionUpdateDebug(QObject::tr("Startup extension update check is disabled in settings"));
        return;
    }

    logStartupExtensionUpdateDebug(QObject::tr("Startup extension update check scheduled"));
    const auto cancellation_token = m_dependencies.extensionBackgroundWorkCancellationToken;
    QTimer::singleShot(
        kExtensionStartupUpdateDelayMs, m_dependencies.mainWindow, [this, settingsPersistence, cancellation_token]() {
            if (cancellation_token.isCancellationRequested() || !m_dependencies.mainWindow)
            {
                return;
            }

            auto* watcher = new QFutureWatcher<ExtensionStartupUpdateCheckResult>(m_dependencies.mainWindow);
            QObject::connect(
                watcher, &QFutureWatcher<ExtensionStartupUpdateCheckResult>::finished, m_dependencies.mainWindow,
                [this, watcher, settingsPersistence]() {
                    const ExtensionStartupUpdateCheckResult result = watcher->result();
                    watcher->deleteLater();
                    if (!shouldNotifyExtensionStartupUpdates(result) || !m_dependencies.mainWindow)
                    {
                        if (result.success)
                        {
                            logStartupExtensionUpdateDebug(QObject::tr("No startup extension update dialog is needed"));
                        }
                        else if (!result.diagnostic.isEmpty())
                        {
                            logStartupExtensionUpdateWarning(result.diagnostic);
                        }
                        return;
                    }

                    logStartupExtensionUpdateInfo(QObject::tr("Showing startup update notification for %1 extension(s)")
                                                      .arg(static_cast<int>(result.updates.size())));
                    auto top_level_restorer = std::make_shared<ScopedTopLevelFlagRestorer>();
                    QWidget* dialogParent = resolveDialogParent(m_dependencies.mainWindow, nullptr);

                    auto* messageBox = new QMessageBox(dialogParent);
                    messageBox->setObjectName(QStringLiteral("anExtensionStartupUpdateMessageBox"));
                    messageBox->setProperty("anDialogVariant", QStringLiteral("messageBox"));
                    messageBox->setIcon(QMessageBox::Information);
                    messageBox->setWindowTitle(QObject::tr("Extension Updates Available"));
                    messageBox->setText(extensionStartupUpdateNotificationText(result));
                    messageBox->setWindowModality(dialogParent ? Qt::WindowModal : Qt::ApplicationModal);
                    messageBox->setTextInteractionFlags(Qt::TextSelectableByMouse);

                    auto* openButton = messageBox->addButton(QObject::tr("Open Extensions"), QMessageBox::AcceptRole);
                    openButton->setObjectName(QStringLiteral("anExtensionStartupUpdateOpenButton"));
                    applyButtonRole(openButton, QStringLiteral("primary"));

                    auto* ignoreButton = messageBox->addButton(QObject::tr("Ignore"), QMessageBox::RejectRole);
                    ignoreButton->setObjectName(QStringLiteral("anExtensionStartupUpdateIgnoreButton"));
                    applyButtonRole(ignoreButton, QStringLiteral("secondary"));
                    messageBox->setDefaultButton(openButton);

                    auto* disableCheck =
                        new QCheckBox(QObject::tr("Disable extension update checks on startup"), messageBox);
                    disableCheck->setObjectName(QStringLiteral("anExtensionStartupUpdateDisableCheck"));
                    messageBox->setCheckBox(disableCheck);

                    QObject::connect(messageBox, &QMessageBox::finished, messageBox,
                                     [this, messageBox, openButton, disableCheck, settingsPersistence,
                                      top_level_restorer = std::move(top_level_restorer)](int) mutable {
                                         if (disableCheck && disableCheck->isChecked() && settingsPersistence)
                                         {
                                             settingsPersistence->upsert(
                                                 SettingsKeys::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP,
                                                 QStringLiteral("0"));
                                         }

                                         const bool openExtensions = messageBox->clickedButton() == openButton;
                                         messageBox->deleteLater();
                                         if (openExtensions)
                                         {
                                             openExtensionsAt(extensionManagerPageTitle());
                                         }
                                     });

                    messageBox->open();
                });
            watcher->setFuture(QtConcurrent::run(
                [cancellation_token]() { return collectStartupExtensionUpdates(cancellation_token); }));
        });
}

void DialogOrchestrationCoordinator::systemInformation() const
{
    if (!m_dependencies.mainWindow)
    {
        return;
    }

    ScopedTopLevelFlagRestorer _;
    SystemInformationDialog dialog(m_dependencies.mainWindow);
    dialog.exec();
}

void DialogOrchestrationCoordinator::notifyErrorMessage(QWidget* parent, const QString& title,
                                                        const QString& text) const
{
    ScopedTopLevelFlagRestorer _;
    QWidget* dialogParent = resolveDialogParent(m_dependencies.mainWindow, parent);
    showModernMessageBox(dialogParent, QMessageBox::Critical, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

void DialogOrchestrationCoordinator::notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) const
{
    ScopedTopLevelFlagRestorer _;
    QWidget* dialogParent = resolveDialogParent(m_dependencies.mainWindow, parent);
    showModernMessageBox(dialogParent, QMessageBox::Warning, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

void DialogOrchestrationCoordinator::notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) const
{
    ScopedTopLevelFlagRestorer _;
    QWidget* dialogParent = resolveDialogParent(m_dependencies.mainWindow, parent);
    showModernMessageBox(dialogParent, QMessageBox::Information, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

void DialogOrchestrationCoordinator::messageBoxYesNo(QWidget* parent, const QString& title, const QString& text,
                                                     const bool warn, int* ret) const
{
    ScopedTopLevelFlagRestorer _;
    QWidget* dialogParent = resolveDialogParent(m_dependencies.mainWindow, parent);

    const auto icon = warn ? QMessageBox::Warning : QMessageBox::Question;
    const auto result =
        showModernMessageBox(dialogParent, icon, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret)
    {
        *ret = static_cast<int>(result);
    }
}

void DialogOrchestrationCoordinator::openMessageBoxYesNo(QWidget* parent, const QString& title, const QString& text,
                                                         const bool warn,
                                                         std::function<void(int result)> onFinished) const
{
    auto top_level_restorer = std::make_shared<ScopedTopLevelFlagRestorer>();
    QWidget* dialogParent = resolveDialogParent(m_dependencies.mainWindow, parent);

    auto* messageBox = new QMessageBox(dialogParent);
    messageBox->setObjectName(QStringLiteral("anMessageBox"));
    messageBox->setProperty("anDialogVariant", QStringLiteral("messageBox"));
    messageBox->setIcon(warn ? QMessageBox::Warning : QMessageBox::Question);
    messageBox->setWindowTitle(title);
    messageBox->setText(text);
    messageBox->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    messageBox->setDefaultButton(QMessageBox::No);
    messageBox->setWindowModality(dialogParent ? Qt::WindowModal : Qt::ApplicationModal);
    messageBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
    applyModernMessageBoxButtonRoles(*messageBox);

    QObject::connect(messageBox, &QMessageBox::finished, messageBox,
                     [messageBox, onFinished = std::move(onFinished),
                      top_level_restorer = std::move(top_level_restorer)](int result) mutable {
                         if (onFinished)
                         {
                             onFinished(result);
                         }
                         messageBox->deleteLater();
                     });

    messageBox->open();
}

} // namespace an::ui
