// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"
#include "app/ExtensionManager.h"

#include <QUrl>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <verdigris/wobjectdefs.h>

class GlobalSettingsPersistence;
class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QToolButton;
class QMenu;
class QScrollArea;
class QVBoxLayout;
class QWidget;
template <typename T> class QFutureWatcher;

namespace an::ui
{

class AnCardWidget;
class AnInfoBanner;

struct ExtensionManagerPageRepositorySnapshot
{
    bool success = true;
    bool network_features_disabled = false;
    bool from_cache = false;
    bool background_refresh_in_progress = false;
    QString diagnostic;
    QString cache_updated_at_utc;
    QUrl indexUrl;
    bool environment_storage_available = false;
    std::uintmax_t environment_total_bytes = 0;
    std::uintmax_t environment_active_bytes = 0;
    std::uintmax_t environment_reclaimable_bytes = 0;
    std::size_t environment_total_count = 0;
    std::size_t environment_reclaimable_count = 0;
    std::vector<extensions::ExtensionRepositoryBrowseEntry> entries;
};

struct ExtensionManagerPageInstallRequest
{
    extensions::ExtensionRepositoryBrowseEntry entry;
    QUrl indexUrl;
    QUrl assetUrl;
    extensions::ExtensionDiscoveryContext context;
    std::filesystem::path installRoot;
    extensions::ExtensionOperationProgressCallback progress_callback;
    extensions::ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionManagerPageInstallResult
{
    bool success = false;
    bool cancelled = false;
    QString diagnostic;
    extensions::ExtensionPackageInstallResult transaction;
};

struct ExtensionManagerPageLocalInstallRequest
{
    std::filesystem::path archivePath;
    extensions::ExtensionDiscoveryContext context;
    std::filesystem::path installRoot;
    extensions::ExtensionOperationProgressCallback progress_callback;
    extensions::ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionManagerPageRemoveRequest
{
    extensions::ExtensionRepositoryBrowseEntry entry;
    std::filesystem::path installRoot;
    extensions::ExtensionOperationProgressCallback progress_callback;
    extensions::ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionManagerPageRemoveResult
{
    bool success = false;
    bool cancelled = false;
    QString diagnostic;
    extensions::ExtensionPackageRemoveResult transaction;
};

struct ExtensionManagerPageEnvironmentCleanupRequest
{
    extensions::ExtensionDiscoveryOptions discovery_options;
    extensions::ExtensionOperationProgressCallback progress_callback;
    extensions::ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionManagerPageEnvironmentCleanupResult
{
    bool success = false;
    bool cancelled = false;
    QString diagnostic;
    std::uintmax_t reclaimed_bytes = 0;
    std::vector<std::filesystem::path> removed_paths;
};

class AFTERNIGHTCONTROLS_API ExtensionManagerPage : public SettingsPage
{
    W_OBJECT(ExtensionManagerPage)

public:
    using RepositoryProvider = std::function<ExtensionManagerPageRepositorySnapshot()>;
    using DiscoveryOptionsProvider = std::function<extensions::ExtensionDiscoveryOptions()>;
    using InstallProvider = std::function<ExtensionManagerPageInstallResult(const ExtensionManagerPageInstallRequest&)>;
    using LocalInstallProvider =
        std::function<ExtensionManagerPageInstallResult(const ExtensionManagerPageLocalInstallRequest&)>;
    using LocalPackagePathProvider = std::function<QString(QWidget*)>;
    using UnsignedInstallConfirmationProvider = std::function<bool(QWidget*, const QString&, const QString&)>;
    using PendingOperationsCancellationProvider = std::function<bool(QWidget*, int)>;
    using RemoveConfirmationProvider = std::function<bool(QWidget*, const extensions::ExtensionRepositoryBrowseEntry&)>;
    using RemoveProvider = std::function<ExtensionManagerPageRemoveResult(const ExtensionManagerPageRemoveRequest&)>;
    using EnvironmentCleanupProvider = std::function<ExtensionManagerPageEnvironmentCleanupResult(
        const ExtensionManagerPageEnvironmentCleanupRequest&)>;
    using ExtensionSettingsOpener =
        std::function<bool(const extensions::ExtensionRepositoryBrowseEntry&, QWidget*, QString*)>;
    explicit ExtensionManagerPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~ExtensionManagerPage() override;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;
    [[nodiscard]] bool usesSettingsDialogCommitButtons() const override
    {
        return false;
    }

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    void refreshStyles() override;

    void setRepositoryProvider(RepositoryProvider repositoryProvider);
    void setDiscoveryOptionsProvider(DiscoveryOptionsProvider discoveryOptionsProvider);
    void setInstallProvider(InstallProvider installProvider);
    void setLocalInstallProvider(LocalInstallProvider localInstallProvider);
    void setLocalPackagePathProvider(LocalPackagePathProvider localPackagePathProvider);
    void setUnsignedInstallConfirmationProvider(UnsignedInstallConfirmationProvider confirmationProvider);
    void setPendingOperationsCancellationProvider(PendingOperationsCancellationProvider confirmationProvider);
    void setRemoveConfirmationProvider(RemoveConfirmationProvider confirmationProvider);
    void setRemoveProvider(RemoveProvider removeProvider);
    void setEnvironmentCleanupProvider(EnvironmentCleanupProvider cleanupProvider);
    void setExtensionSettingsOpener(ExtensionSettingsOpener settingsOpener);

    /// Records that the package of `extensionId` was removed from disk by something other than this page: its card
    /// stops showing an installed version at once, and the repository is read again for what else the removal changed.
    void noteExtensionUninstalled(const std::string& extensionId);

signals:
    void installedExtensionsChanged() W_SIGNAL(installedExtensionsChanged);

private:
    enum class OperationKind
    {
        None,
        Install,
        Update,
        Remove,
        LocalInstall,
        EnvironmentCleanup
    };

    struct PendingOperation
    {
        std::uint64_t sequence = 0;
        OperationKind kind = OperationKind::None;
        extensions::ExtensionRepositoryBrowseEntry entry;
    };

    struct InstalledEntryOverride
    {
        std::string id;
        std::string version;
        extensions::ExtensionPackageSource source = extensions::ExtensionPackageSource::User;
        bool status_available = false;
        extensions::ExtensionLoadStatus load_status = extensions::ExtensionLoadStatus::Invalid;
        std::string status_diagnostic;
        bool environment_available = false;
        extensions::ExtensionEnvironmentState environment_state = extensions::ExtensionEnvironmentState::Missing;
        std::string environment_diagnostic;
    };

    void setupUi();
    void refreshRepository(bool preserveCurrentCards = false);
    void applyRepositorySnapshot(ExtensionManagerPageRepositorySnapshot snapshot);
    void renderCards();
    void clearCards();
    void startInstallOperation(const extensions::ExtensionRepositoryBrowseEntry& entry);
    [[nodiscard]] bool confirmInstallOperation(const extensions::ExtensionRepositoryBrowseEntry& entry);
    void beginInstallOperation(const extensions::ExtensionRepositoryBrowseEntry& entry);
    void startLocalInstallOperation();
    [[nodiscard]] bool unsignedExtensionsAllowed() const;
    [[nodiscard]] bool confirmUnsignedInstall(const QString& title, const QString& message);
    [[nodiscard]] bool confirmCancelPendingOperations(int pendingCount);
    void setUnsignedExtensionsAllowed(bool allowed);
    void setAutomaticEnvironmentCleanupEnabled(bool enabled);
    void updateUnsignedExtensionsWarningBanner();
    void completeInstallOperation(const ExtensionManagerPageInstallResult& result);
    [[nodiscard]] bool markActiveInstallEntryInstalled(const ExtensionManagerPageInstallResult& result);
    void applyInstalledEntryOverride(extensions::ExtensionRepositoryBrowseEntry& entry) const;
    void applyInstalledEntryOverrides(ExtensionManagerPageRepositorySnapshot& snapshot) const;
    void markEntryUninstalled(const std::string& extensionId);
    void persistCurrentRepositorySnapshotCache() const;
    void clearActiveInstallEntry();
    void startRemoveOperation(const extensions::ExtensionRepositoryBrowseEntry& entry);
    [[nodiscard]] bool confirmRemoveOperation(const extensions::ExtensionRepositoryBrowseEntry& entry);
    void beginRemoveOperation(const extensions::ExtensionRepositoryBrowseEntry& entry);
    void completeRemoveOperation(const ExtensionManagerPageRemoveResult& result);
    void openExtensionSettings(const extensions::ExtensionRepositoryBrowseEntry& entry);
    void startEnvironmentCleanupOperation();
    void completeEnvironmentCleanupOperation(const ExtensionManagerPageEnvironmentCleanupResult& result);
    void cancelActiveOperation();
    void setOperationInProgress(bool inProgress);
    void enqueuePendingOperation(OperationKind kind, const extensions::ExtensionRepositoryBrowseEntry& entry);
    void cancelPendingOperation(std::uint64_t sequence);
    void clearPendingOperations();
    void startNextPendingOperation();
    void finishActiveOperationState();
    void continuePendingOperationsAfterCurrent(bool currentOperationCancelled);
    void updatePendingOperationsView();
    void updateActionButtonsForOperationState();
    [[nodiscard]] const PendingOperation* pendingOperationForEntry(const std::string& extensionId) const;
    void showOperationProgress(const QString& message, bool indeterminate = true, int current = 0, int total = 0);
    void updateOperationProgress(const extensions::ExtensionOperationProgress& progress);
    void clearOperationProgress();
    [[nodiscard]] extensions::ExtensionOperationProgressCallback makeProgressCallback();
    [[nodiscard]] bool entryMatchesSearch(const extensions::ExtensionRepositoryBrowseEntry& entry,
                                          const QString& search_text) const;
    [[nodiscard]] extensions::ExtensionDiscoveryOptions buildDiscoveryOptions() const;
    [[nodiscard]] ExtensionManagerPageInstallRequest
    buildInstallRequest(const extensions::ExtensionRepositoryBrowseEntry& entry) const;
    [[nodiscard]] ExtensionManagerPageLocalInstallRequest buildLocalInstallRequest(const QString& archivePath) const;
    [[nodiscard]] ExtensionManagerPageRemoveRequest
    buildRemoveRequest(const extensions::ExtensionRepositoryBrowseEntry& entry) const;
    [[nodiscard]] ExtensionManagerPageEnvironmentCleanupRequest buildEnvironmentCleanupRequest() const;
    [[nodiscard]] QWidget* buildRepositoryCard(const extensions::ExtensionRepositoryBrowseEntry& entry);

    GlobalSettingsPersistence* m_Settings = nullptr;
    RepositoryProvider m_RepositoryProvider;
    DiscoveryOptionsProvider m_DiscoveryOptionsProvider;
    InstallProvider m_InstallProvider;
    LocalInstallProvider m_LocalInstallProvider;
    LocalPackagePathProvider m_LocalPackagePathProvider;
    UnsignedInstallConfirmationProvider m_UnsignedInstallConfirmationProvider;
    PendingOperationsCancellationProvider m_PendingOperationsCancellationProvider;
    RemoveConfirmationProvider m_RemoveConfirmationProvider;
    RemoveProvider m_RemoveProvider;
    EnvironmentCleanupProvider m_EnvironmentCleanupProvider;
    ExtensionSettingsOpener m_ExtensionSettingsOpener;
    ExtensionManagerPageRepositorySnapshot m_LastSnapshot;
    AnInfoBanner* m_SummaryBanner = nullptr;
    QLabel* m_EnvironmentStorageLabel = nullptr;
    AnInfoBanner* m_UnsignedExtensionsWarningBanner = nullptr;
    QLabel* m_ProgressLabel = nullptr;
    QProgressBar* m_ProgressBar = nullptr;
    QWidget* m_QueueContainer = nullptr;
    QVBoxLayout* m_QueueLayout = nullptr;
    QLineEdit* m_SearchEdit = nullptr;
    QComboBox* m_FilterCombo = nullptr;
    QComboBox* m_SortCombo = nullptr;
    QPushButton* m_RefreshButton = nullptr;
    QToolButton* m_MoreActionsButton = nullptr;
    QMenu* m_MoreActionsMenu = nullptr;
    QCheckBox* m_AllowUnsignedExtensionsCheck = nullptr;
    QCheckBox* m_AutomaticEnvironmentCleanupCheck = nullptr;
    QPushButton* m_CancelButton = nullptr;
    QScrollArea* m_CardsScrollArea = nullptr;
    QWidget* m_CardsContainer = nullptr;
    QVBoxLayout* m_CardsLayout = nullptr;
    std::vector<AnCardWidget*> m_Cards;
    std::vector<QPushButton*> m_ActionButtons;
    std::vector<QToolButton*> m_DetailButtons;
    QFutureWatcher<ExtensionManagerPageRepositorySnapshot>* m_RepositoryRefreshWatcher = nullptr;
    QFutureWatcher<ExtensionManagerPageInstallResult>* m_InstallWatcher = nullptr;
    QFutureWatcher<ExtensionManagerPageRemoveResult>* m_RemoveWatcher = nullptr;
    QFutureWatcher<ExtensionManagerPageEnvironmentCleanupResult>* m_EnvironmentCleanupWatcher = nullptr;
    extensions::ExtensionOperationCancellationToken m_ActiveCancellationToken;
    std::vector<PendingOperation> m_PendingOperations;
    std::uint64_t m_NextPendingOperationSequence = 1;
    OperationKind m_ActiveOperationKind = OperationKind::None;
    std::string m_ActiveOperationEntryId;
    std::string m_ActiveInstallEntryId;
    std::string m_ActiveInstallEntryVersion;
    extensions::ExtensionPackageSource m_ActiveInstallSource = extensions::ExtensionPackageSource::Repository;
    std::optional<InstalledEntryOverride> m_InstalledEntryOverride;
    bool m_CancellationRequested = false;
    bool m_PromptCancelPendingAfterActiveCancellation = false;
    bool m_OperationInProgress = false;
    bool m_RepositoryRefreshInProgress = false;
    /// The refresh asked for while another was on its way, as its own caller asked for it - whether to keep the cards
    /// shown: it is run when the one on its way lands. Nothing while no refresh waits.
    std::optional<bool> m_RepositoryRefreshWaiting;
};

} // namespace an::ui
