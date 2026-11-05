// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"

#include "app/GaiaCatalogSettings.h"
#include "app/SpccCatalogSettings.h"
#include "app/SpccResponseSettings.h"

#include <optional>

class GlobalSettingsPersistence;
class PathPickerWidget;
class QComboBox;
class QLabel;
class QPushButton;

namespace an::ui
{
class CatalogDownloadService;
}

class AFTERNIGHTCONTROLS_API CatalogsDataSettingsPage : public SettingsPage
{
    W_OBJECT(CatalogsDataSettingsPage)

public:
    explicit CatalogsDataSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~CatalogsDataSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;

protected:
    struct SpccCatalogDownloadSelection
    {
        QString packId;
        QString installDirectory;
    };
    struct GaiaCatalogDownloadSelection
    {
        QString packId;
        QString installDirectory;
    };

    [[nodiscard]] virtual QVector<an::ui::CatalogPackDescriptor> availableGaiaCatalogPacksForDownload() const;
    [[nodiscard]] virtual QVector<an::ui::CatalogPackDescriptor> availableSpccCatalogPacksForDownload() const;
    [[nodiscard]] virtual QVector<an::ui::SpccResponseRepositoryDescriptor>
    availableSpccResponseRepositoriesForDownload() const;
    [[nodiscard]] virtual std::optional<an::ui::SpccRegionalCatalogRequest>
    loadPendingSpccRegionalCatalogRequest() const;
    virtual bool clearPendingSpccRegionalCatalogRequest();
    [[nodiscard]] virtual std::optional<GaiaCatalogDownloadSelection> runGaiaCatalogDownloadAssistant();
    [[nodiscard]] virtual std::optional<SpccCatalogDownloadSelection> runSpccCatalogDownloadAssistant();
    [[nodiscard]] virtual std::optional<SpccCatalogDownloadSelection>
    runSpccCatalogDownloadAssistant(const QVector<an::ui::CatalogPackDescriptor>& packs);
    [[nodiscard]] virtual bool confirmDeleteSelectedSpccCatalogPack(const QString& directoryPath,
                                                                    bool exactPathRequired);
    [[nodiscard]] virtual bool confirmDeleteAllDownloadedSpccCatalogPacks(const QString& rootDirectory, int packCount);
    virtual void installManagedSpccResponseRepository();

private:
    void setupUi();
    void connectSignals();
    void disconnectSignals();
    void applyGaiaCatalogDownloadSelection(const GaiaCatalogDownloadSelection& selection);
    void applySpccCatalogDownloadSelection(const SpccCatalogDownloadSelection& selection);
    void evictSelectedGaiaCatalogPack();
    void maybeClearSatisfiedSpccRegionalRequest(const SpccCatalogDownloadSelection& selection);
    void ensureManagedSpccResponseRepositoryForFullCatalog(const SpccCatalogDownloadSelection& selection);
    void installMatchingSpccRegionalCatalogPack();
    void evictSelectedSpccCatalogPack();
    void deleteAllDownloadedSpccCatalogPacks();
    void refreshGaiaCatalogPackOptions(const QString& preferredPack = QString());
    void refreshSpccCatalogPackOptions(const QString& preferredPack = QString());
    [[nodiscard]] QString currentGaiaCatalogPack() const;
    [[nodiscard]] QString currentSpccCatalogPack() const;
    void updateGaiaCatalogStatus();
    void updateSpccCatalogStatus();
    void updateSpccRegionalRequestStatus();

    GlobalSettingsPersistence* m_Settings = nullptr;
    PathPickerWidget* m_GaiaCatalogDirectoryPicker = nullptr;
    QComboBox* m_GaiaCatalogPackCombo = nullptr;
    QPushButton* m_GaiaCatalogDownloadButton = nullptr;
    QPushButton* m_GaiaCatalogEvictButton = nullptr;
    PathPickerWidget* m_SpccCatalogDirectoryPicker = nullptr;
    QComboBox* m_SpccCatalogPackCombo = nullptr;
    QPushButton* m_SpccCatalogDownloadButton = nullptr;
    QPushButton* m_SpccCatalogRepairButton = nullptr;
    QPushButton* m_SpccCatalogEvictButton = nullptr;
    QPushButton* m_SpccCatalogDeleteAllButton = nullptr;
    QLabel* m_SpccRegionalRequestTitleLabel = nullptr;
    QLabel* m_SpccRegionalRequestLabel = nullptr;
    QPushButton* m_SpccRegionalInstallButton = nullptr;
    QString m_SpccResponseRepositoryDirectory;
    an::ui::CatalogDownloadService* m_SpccResponseDownloadService = nullptr;
};
