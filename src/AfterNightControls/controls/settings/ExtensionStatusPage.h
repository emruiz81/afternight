// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"
#include "app/ExtensionManager.h"

#include <QString>

#include <functional>
#include <map>
#include <string_view>
#include <vector>

#include <verdigris/wobjectdefs.h>

class GlobalSettingsPersistence;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;
template <typename T> class QFutureWatcher;

namespace an::ui
{
class AnCardWidget;
class AnCollapsibleSection;
class AnInfoBanner;
} // namespace an::ui

class ExtensionSettingsSection;

namespace an::ui
{
AFTERNIGHTCONTROLS_API void warmInstalledExtensionStatusCache();
AFTERNIGHTCONTROLS_API void invalidateInstalledExtensionStatusCache(bool refreshInBackground = true);

struct ExtensionStatusPageUninstallResult
{
    bool success = false;
    QString diagnostic;
};

struct ExtensionStatusPageEnvironmentRepairResult
{
    bool success = false;
    bool cancelled = false;
    QString diagnostic;
    extensions::ExtensionPackageRepairResult transaction;
};
} // namespace an::ui

class AFTERNIGHTCONTROLS_API ExtensionStatusPage : public SettingsPage
{
    W_OBJECT(ExtensionStatusPage)

public:
    using DiscoveryProvider = std::function<std::vector<an::ui::extensions::ExtensionLoadResult>()>;
    using UninstallConfirmationProvider = std::function<bool(const an::ui::extensions::ExtensionLoadResult&)>;
    using UninstallProvider =
        std::function<an::ui::ExtensionStatusPageUninstallResult(const an::ui::extensions::ExtensionLoadResult&)>;
    using EnvironmentRepairProvider = std::function<an::ui::ExtensionStatusPageEnvironmentRepairResult(
        const an::ui::extensions::ExtensionLoadResult&)>;

    explicit ExtensionStatusPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~ExtensionStatusPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    void refreshStyles() override;

    void setDiscoveryProvider(DiscoveryProvider discoveryProvider);
    void setUninstallConfirmationProvider(UninstallConfirmationProvider confirmationProvider);
    void setUninstallProvider(UninstallProvider uninstallProvider);
    void setEnvironmentRepairProvider(EnvironmentRepairProvider repairProvider);
    void refreshStatus(bool forceRefresh = true);
    [[nodiscard]] bool openExtensionSettings(std::string_view extension_id);
    [[nodiscard]] bool openExtensionSettingsDialog(std::string_view extension_id);

private:
    void setupUi();
    void reloadStatus(bool forceRefresh = false);
    void applyDiscoveryResults(std::vector<an::ui::extensions::ExtensionLoadResult> results);
    void clearCards();
    void uninstallExtension(const an::ui::extensions::ExtensionLoadResult& result, QPushButton* actionButton = nullptr);
    void completeUninstallOperation(const an::ui::extensions::ExtensionLoadResult& request,
                                    const an::ui::ExtensionStatusPageUninstallResult& result);
    void repairExtensionEnvironment(const an::ui::extensions::ExtensionLoadResult& result,
                                    QPushButton* actionButton = nullptr);
    void completeEnvironmentRepair(const an::ui::ExtensionStatusPageEnvironmentRepairResult& result);
    void setOperationInProgress(bool inProgress);
    void registerActionButton(QPushButton* button, bool available);
    void openSettingsDialog(const an::ui::extensions::ExtensionLoadResult& result);
    [[nodiscard]] QWidget* buildExtensionCard(const an::ui::extensions::ExtensionLoadResult& result);

    GlobalSettingsPersistence* m_Settings = nullptr;
    DiscoveryProvider m_DiscoveryProvider;
    UninstallConfirmationProvider m_UninstallConfirmationProvider;
    UninstallProvider m_UninstallProvider;
    EnvironmentRepairProvider m_EnvironmentRepairProvider;
    an::ui::AnInfoBanner* m_SummaryBanner = nullptr;
    QPushButton* m_RefreshButton = nullptr;
    QScrollArea* m_ContentScrollArea = nullptr;
    QWidget* m_ScrollContent = nullptr;
    QWidget* m_CardsContainer = nullptr;
    QVBoxLayout* m_CardsLayout = nullptr;
    std::vector<an::ui::AnCardWidget*> m_Cards;
    std::vector<QPushButton*> m_ActionButtons;
    std::vector<QToolButton*> m_DetailButtons;
    std::vector<an::ui::extensions::ExtensionLoadResult> m_LastResults;
    QFutureWatcher<std::vector<an::ui::extensions::ExtensionLoadResult>>* m_DiscoveryWatcher = nullptr;
    QFutureWatcher<an::ui::ExtensionStatusPageUninstallResult>* m_UninstallWatcher = nullptr;
    QFutureWatcher<an::ui::ExtensionStatusPageEnvironmentRepairResult>* m_EnvironmentRepairWatcher = nullptr;
    bool m_OperationInProgress = false;
};
