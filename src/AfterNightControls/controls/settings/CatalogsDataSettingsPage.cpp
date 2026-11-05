// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "CatalogsDataSettingsPage.h"

#include "PathPickerWidget.h"
#include "SettingsDefaults.h"
#include "SettingsKeys.h"
#include "../GlobalSettingsPersistence.h"
#include "app/CatalogDownloadService.h"
#include "app/GaiaCatalogSettings.h"
#include "app/SpccCatalogSettings.h"
#include "app/SpccResponseSettings.h"
#include "dialogs/CatalogDownloadDialog.h"
#include "theme/AnDesignTokens.h"

#include <verdigris/wobjectimpl.h>

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

W_OBJECT_IMPL(CatalogsDataSettingsPage)

namespace
{

[[nodiscard]] QString spccCatalogPackLabel(const QString& packId, const QString& displayName)
{
    if (displayName.isEmpty() || displayName == packId)
    {
        return packId;
    }
    if (displayName.contains(packId, Qt::CaseInsensitive))
    {
        return displayName;
    }

    return QObject::tr("%1 (%2)").arg(displayName, packId);
}

[[nodiscard]] QString gaiaCatalogPackLabel(const QString& packId, const QString& displayName)
{
    if (displayName.isEmpty() || displayName == packId)
    {
        return packId;
    }

    return QObject::tr("%1 (%2)").arg(displayName, packId);
}

[[nodiscard]] bool pathIsInsideDirectory(const QString& path, const QString& directory)
{
    const QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QString root = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    if (candidate.isEmpty() || root.isEmpty())
    {
        return false;
    }

    if (!root.endsWith(QLatin1Char('/')) && !root.endsWith(QLatin1Char('\\')))
    {
        root += QLatin1Char('/');
    }

    return candidate.startsWith(root, Qt::CaseInsensitive);
}

} // namespace

CatalogsDataSettingsPage::CatalogsDataSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString CatalogsDataSettingsPage::pageTitle() const
{
    return tr("Catalogs & Data");
}

QIcon CatalogsDataSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("folder_open"));
}

QStringList CatalogsDataSettingsPage::searchKeywords() const
{
    return QStringList{
        tr("Catalogs"), tr("Data"), tr("SPCC"), tr("Gaia XP"), tr("Spectrophotometric"), tr("Download"), tr("Cache"),
    };
}

void CatalogsDataSettingsPage::setupUi()
{
    const auto& tokenSet = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(tokenSet.spacing.lg);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* gaiaGroup = new QGroupBox(tr("Gaia DR3 Astrometric Catalogs"), this);
    auto* gaiaLayout = new QFormLayout(gaiaGroup);
    gaiaLayout->setSpacing(tokenSet.spacing.md);

    m_GaiaCatalogDirectoryPicker = new PathPickerWidget(PathPickerWidget::Mode::Directory, gaiaGroup);
    m_GaiaCatalogDirectoryPicker->setObjectName(QStringLiteral("gaiaCatalogDirectoryPicker"));
    m_GaiaCatalogDirectoryPicker->setPlaceholderText(tr("Path to Gaia DR3 catalog root directory..."));
    m_GaiaCatalogDirectoryPicker->setDialogTitle(tr("Select Gaia Catalog Directory"));
    gaiaLayout->addRow(tr("Catalog root:"), m_GaiaCatalogDirectoryPicker);

    m_GaiaCatalogPackCombo = new QComboBox(gaiaGroup);
    m_GaiaCatalogPackCombo->setObjectName(QStringLiteral("gaiaCatalogPackCombo"));
    gaiaLayout->addRow(tr("Catalog pack:"), m_GaiaCatalogPackCombo);

    m_GaiaCatalogDownloadButton = new QPushButton(tr("Download Catalog..."), gaiaGroup);
    m_GaiaCatalogDownloadButton->setObjectName(QStringLiteral("gaiaCatalogDownloadButton"));
    m_GaiaCatalogDownloadButton->setToolTip(
        tr("Open the Gaia astrometric catalog download assistant and activate the installed pack."));
    m_GaiaCatalogEvictButton = new QPushButton(tr("Evict"), gaiaGroup);
    m_GaiaCatalogEvictButton->setObjectName(QStringLiteral("gaiaCatalogEvictButton"));
    m_GaiaCatalogEvictButton->setToolTip(tr("Remove the selected local Gaia astrometric catalog pack from disk."));

    auto* gaiaActionsLayout = new QHBoxLayout();
    gaiaActionsLayout->setContentsMargins(0, 0, 0, 0);
    gaiaActionsLayout->setSpacing(tokenSet.spacing.sm);
    gaiaActionsLayout->addWidget(m_GaiaCatalogDownloadButton);
    gaiaActionsLayout->addWidget(m_GaiaCatalogEvictButton);
    gaiaLayout->addRow(QString(), gaiaActionsLayout);

    gaiaLayout->addRow(
        QString(),
        createInfoLabel(
            tr("Used by internal plate solving and PCC. SPCC Gaia XP sampled catalogs are managed separately below.")));
    mainLayout->addWidget(gaiaGroup);

    auto* spccGroup = new QGroupBox(tr("SPCC Calibration Data"), this);
    auto* spccLayout = new QFormLayout(spccGroup);
    spccLayout->setSpacing(tokenSet.spacing.md);

    m_SpccCatalogDirectoryPicker = new PathPickerWidget(PathPickerWidget::Mode::Directory, spccGroup);
    m_SpccCatalogDirectoryPicker->setObjectName(QStringLiteral("spccCatalogDirectoryPicker"));
    m_SpccCatalogDirectoryPicker->setPlaceholderText(tr("Path to SPCC catalog root directory..."));
    m_SpccCatalogDirectoryPicker->setDialogTitle(tr("Select SPCC Catalog Directory"));
    spccLayout->addRow(tr("Catalog root:"), m_SpccCatalogDirectoryPicker);

    m_SpccCatalogPackCombo = new QComboBox(spccGroup);
    m_SpccCatalogPackCombo->setObjectName(QStringLiteral("spccCatalogPackCombo"));
    spccLayout->addRow(tr("Catalog pack:"), m_SpccCatalogPackCombo);

    m_SpccCatalogDownloadButton = new QPushButton(tr("Download Full Offline Catalog..."), spccGroup);
    m_SpccCatalogDownloadButton->setObjectName(QStringLiteral("spccCatalogDownloadButton"));
    m_SpccCatalogDownloadButton->setToolTip(
        tr("Download the full SPCC Gaia XP sampled catalog for offline use. Any other required SPCC data is prepared "
           "automatically if needed."));
    m_SpccCatalogRepairButton = new QPushButton(tr("Verify && Repair"), spccGroup);
    m_SpccCatalogRepairButton->setObjectName(QStringLiteral("spccCatalogRepairButton"));
    m_SpccCatalogRepairButton->setToolTip(tr("Repair or reinstall the selected SPCC catalog pack."));
    m_SpccCatalogEvictButton = new QPushButton(tr("Delete Selected"), spccGroup);
    m_SpccCatalogEvictButton->setObjectName(QStringLiteral("spccCatalogEvictButton"));
    m_SpccCatalogEvictButton->setToolTip(tr("Remove the selected local SPCC catalog pack from disk."));
    m_SpccCatalogDeleteAllButton = new QPushButton(tr("Delete All Downloaded Catalogs..."), spccGroup);
    m_SpccCatalogDeleteAllButton->setObjectName(QStringLiteral("spccCatalogDeleteAllButton"));
    m_SpccCatalogDeleteAllButton->setToolTip(
        tr("Remove all SPCC catalog packs from the AfterNight-managed catalog cache."));

    auto* spccPrimaryActionsLayout = new QHBoxLayout();
    spccPrimaryActionsLayout->setContentsMargins(0, 0, 0, 0);
    spccPrimaryActionsLayout->setSpacing(tokenSet.spacing.sm);
    spccPrimaryActionsLayout->addWidget(m_SpccCatalogDownloadButton);
    spccPrimaryActionsLayout->addWidget(m_SpccCatalogRepairButton);
    spccPrimaryActionsLayout->addStretch();
    spccLayout->addRow(QString(), spccPrimaryActionsLayout);

    auto* spccCleanupActionsLayout = new QHBoxLayout();
    spccCleanupActionsLayout->setContentsMargins(0, 0, 0, 0);
    spccCleanupActionsLayout->setSpacing(tokenSet.spacing.sm);
    spccCleanupActionsLayout->addWidget(m_SpccCatalogEvictButton);
    spccCleanupActionsLayout->addWidget(m_SpccCatalogDeleteAllButton);
    spccCleanupActionsLayout->addStretch();
    spccLayout->addRow(QString(), spccCleanupActionsLayout);

    auto* spccInfo = createInfoLabel(tr("SPCC automatically prepares the data needed by the current image. Download "
                                        "the full offline catalog only when you want SPCC to work without on-demand "
                                        "catalog fetches."));
    spccLayout->addRow(QString(), spccInfo);

    m_SpccRegionalRequestLabel = new QLabel(spccGroup);
    m_SpccRegionalRequestLabel->setObjectName(QStringLiteral("spccRegionalCacheRequestLabel"));
    m_SpccRegionalRequestLabel->setWordWrap(true);
    applySecondaryTextStyle(m_SpccRegionalRequestLabel);
    m_SpccRegionalRequestTitleLabel = new QLabel(tr("Pending region:"), spccGroup);
    spccLayout->addRow(m_SpccRegionalRequestTitleLabel, m_SpccRegionalRequestLabel);
    m_SpccRegionalRequestLabel->setVisible(false);
    m_SpccRegionalRequestTitleLabel->setVisible(false);

    m_SpccRegionalInstallButton = new QPushButton(tr("Download Matching Region..."), spccGroup);
    m_SpccRegionalInstallButton->setObjectName(QStringLiteral("spccRegionalCacheInstallButton"));
    m_SpccRegionalInstallButton->setToolTip(
        tr("Download the best approved SPCC regional catalog pack that covers the pending WCS footprint."));
    spccLayout->addRow(QString(), m_SpccRegionalInstallButton);
    m_SpccRegionalInstallButton->setVisible(false);
    m_SpccRegionalInstallButton->setEnabled(false);

    mainLayout->addWidget(spccGroup);

    m_SpccResponseDownloadService = new an::ui::CatalogDownloadService(this);

    mainLayout->addStretch(1);
}

void CatalogsDataSettingsPage::connectSignals()
{
    connect(m_GaiaCatalogDirectoryPicker, &PathPickerWidget::pathChanged, this, [this]() {
        markAsModified();
        updateGaiaCatalogStatus();
    });
    connect(m_GaiaCatalogPackCombo, &QComboBox::currentIndexChanged, this, [this]() {
        markAsModified();
        updateGaiaCatalogStatus();
    });
    connect(m_GaiaCatalogDownloadButton, &QPushButton::clicked, this, [this]() {
        const auto selection = runGaiaCatalogDownloadAssistant();
        if (selection.has_value())
        {
            applyGaiaCatalogDownloadSelection(*selection);
        }
    });
    connect(m_GaiaCatalogEvictButton, &QPushButton::clicked, this, [this]() { evictSelectedGaiaCatalogPack(); });
    connect(m_SpccCatalogDirectoryPicker, &PathPickerWidget::pathChanged, this, [this]() {
        markAsModified();
        updateSpccCatalogStatus();
    });
    connect(m_SpccCatalogPackCombo, &QComboBox::currentIndexChanged, this, [this]() {
        markAsModified();
        updateSpccCatalogStatus();
    });
    connect(m_SpccCatalogDownloadButton, &QPushButton::clicked, this, [this]() {
        const auto selection = runSpccCatalogDownloadAssistant();
        if (selection.has_value())
        {
            applySpccCatalogDownloadSelection(*selection);
        }
    });
    connect(m_SpccCatalogRepairButton, &QPushButton::clicked, this, [this]() {
        const auto selection = runSpccCatalogDownloadAssistant();
        if (selection.has_value())
        {
            applySpccCatalogDownloadSelection(*selection);
        }
    });
    connect(m_SpccCatalogEvictButton, &QPushButton::clicked, this, [this]() { evictSelectedSpccCatalogPack(); });
    connect(m_SpccCatalogDeleteAllButton, &QPushButton::clicked, this,
            [this]() { deleteAllDownloadedSpccCatalogPacks(); });
    connect(m_SpccRegionalInstallButton, &QPushButton::clicked, this,
            [this]() { installMatchingSpccRegionalCatalogPack(); });
    connect(m_SpccResponseDownloadService, &an::ui::CatalogDownloadService::downloadFinished, this,
            [this](const an::ui::CatalogDownloadResult& result) {
                if (result.success)
                {
                    m_SpccResponseRepositoryDirectory = result.installDirectory.trimmed();
                    if (m_Settings)
                    {
                        m_Settings->upsert(SettingsKeys::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY,
                                           m_SpccResponseRepositoryDirectory);
                    }
                    return;
                }
            });
}

void CatalogsDataSettingsPage::disconnectSignals()
{
    m_GaiaCatalogDirectoryPicker->disconnect(this);
    m_GaiaCatalogPackCombo->disconnect(this);
    m_GaiaCatalogDownloadButton->disconnect(this);
    m_GaiaCatalogEvictButton->disconnect(this);
    m_SpccCatalogDirectoryPicker->disconnect(this);
    m_SpccCatalogPackCombo->disconnect(this);
    m_SpccCatalogDownloadButton->disconnect(this);
    m_SpccCatalogRepairButton->disconnect(this);
    m_SpccCatalogEvictButton->disconnect(this);
    m_SpccCatalogDeleteAllButton->disconnect(this);
    m_SpccRegionalInstallButton->disconnect(this);
    m_SpccResponseDownloadService->disconnect(this);
}

void CatalogsDataSettingsPage::loadSettings()
{
    disconnectSignals();

    const QString configuredGaiaDirectory =
        m_Settings ? m_Settings->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                        QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY))
                   : QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY);
    const QString gaiaRootDirectory = configuredGaiaDirectory.trimmed().isEmpty()
                                          ? an::ui::defaultGaiaCatalogDirectory()
                                          : configuredGaiaDirectory.trimmed();
    m_GaiaCatalogDirectoryPicker->setPath(gaiaRootDirectory);
    refreshGaiaCatalogPackOptions(m_Settings ? an::ui::resolveGaiaCatalogPack(m_Settings)
                                             : an::ui::defaultGaiaCatalogPack());

    const QString configuredDirectory =
        m_Settings ? m_Settings->select(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY,
                                        QString(SettingsDefaults::CATALOGS_SPCC_CATALOG_DIRECTORY))
                   : QString(SettingsDefaults::CATALOGS_SPCC_CATALOG_DIRECTORY);
    const QString rootDirectory =
        configuredDirectory.trimmed().isEmpty() ? an::ui::defaultSpccCatalogDirectory() : configuredDirectory.trimmed();

    m_SpccCatalogDirectoryPicker->setPath(rootDirectory);
    refreshSpccCatalogPackOptions(m_Settings ? an::ui::resolveSpccCatalogPack(m_Settings)
                                             : an::ui::defaultSpccCatalogPack());
    m_SpccResponseRepositoryDirectory = an::ui::resolveSpccResponseRepositoryDirectory(m_Settings);

    connectSignals();
    clearModified();
    updateGaiaCatalogStatus();
    updateSpccCatalogStatus();
    updateSpccRegionalRequestStatus();
}

void CatalogsDataSettingsPage::saveSettings()
{
    if (m_Settings)
    {
        m_Settings->upsert(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY, m_GaiaCatalogDirectoryPicker->path());
        m_Settings->upsert(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK, currentGaiaCatalogPack());
        m_Settings->upsert(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY, m_SpccCatalogDirectoryPicker->path());
        m_Settings->upsert(SettingsKeys::CATALOGS_SPCC_CATALOG_PACK, currentSpccCatalogPack());
        m_Settings->upsert(SettingsKeys::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY,
                           m_SpccResponseRepositoryDirectory.trimmed().isEmpty()
                               ? an::ui::defaultSpccResponseRepositoryDirectory()
                               : m_SpccResponseRepositoryDirectory.trimmed());
    }

    maybeClearSatisfiedSpccRegionalRequest(SpccCatalogDownloadSelection{
        .packId = currentSpccCatalogPack(),
        .installDirectory = m_SpccCatalogDirectoryPicker ? m_SpccCatalogDirectoryPicker->path() : QString(),
    });
    clearModified();
}

void CatalogsDataSettingsPage::resetToDefaults()
{
    disconnectSignals();
    m_GaiaCatalogDirectoryPicker->setPath(an::ui::defaultGaiaCatalogDirectory());
    m_GaiaCatalogDirectoryPicker->clearStatus();
    refreshGaiaCatalogPackOptions(an::ui::defaultGaiaCatalogPack());
    m_SpccCatalogDirectoryPicker->setPath(an::ui::defaultSpccCatalogDirectory());
    m_SpccCatalogDirectoryPicker->clearStatus();
    m_SpccResponseRepositoryDirectory = an::ui::defaultSpccResponseRepositoryDirectory();
    refreshSpccCatalogPackOptions(an::ui::defaultSpccCatalogPack());
    connectSignals();
    updateGaiaCatalogStatus();
    updateSpccCatalogStatus();
    updateSpccRegionalRequestStatus();
    markAsModified();
}

void CatalogsDataSettingsPage::applyGaiaCatalogDownloadSelection(const GaiaCatalogDownloadSelection& selection)
{
    if (selection.packId.trimmed().isEmpty() || selection.installDirectory.trimmed().isEmpty())
    {
        return;
    }

    m_GaiaCatalogDirectoryPicker->setPath(selection.installDirectory.trimmed());
    refreshGaiaCatalogPackOptions(selection.packId.trimmed());
    updateGaiaCatalogStatus();
    markAsModified();
}

void CatalogsDataSettingsPage::applySpccCatalogDownloadSelection(const SpccCatalogDownloadSelection& selection)
{
    if (selection.packId.trimmed().isEmpty() || selection.installDirectory.trimmed().isEmpty())
    {
        return;
    }

    m_SpccCatalogDirectoryPicker->setPath(selection.installDirectory.trimmed());
    refreshSpccCatalogPackOptions(selection.packId.trimmed());
    updateSpccCatalogStatus();
    ensureManagedSpccResponseRepositoryForFullCatalog(selection);
    markAsModified();
}

void CatalogsDataSettingsPage::maybeClearSatisfiedSpccRegionalRequest(const SpccCatalogDownloadSelection& selection)
{
    if (selection.packId.trimmed().isEmpty() || selection.installDirectory.trimmed().isEmpty())
    {
        return;
    }

    const std::optional<an::ui::SpccRegionalCatalogRequest> request = loadPendingSpccRegionalCatalogRequest();
    if (!request.has_value())
    {
        return;
    }

    const QVector<an::ui::CatalogPackDescriptor> packs = availableSpccCatalogPacksForDownload();
    const auto descriptorIt = std::find_if(packs.cbegin(), packs.cend(), [&selection](const auto& descriptor) {
        return descriptor.id.compare(selection.packId.trimmed(), Qt::CaseInsensitive) == 0;
    });
    if (descriptorIt == packs.cend())
    {
        return;
    }

    if (!an::ui::spccCatalogPackInstallSatisfiesRegionalRequest(*descriptorIt, selection.installDirectory.trimmed(),
                                                                *request))
    {
        return;
    }

    (void)clearPendingSpccRegionalCatalogRequest();
    updateSpccRegionalRequestStatus();
}

void CatalogsDataSettingsPage::ensureManagedSpccResponseRepositoryForFullCatalog(
    const SpccCatalogDownloadSelection& selection)
{
    const QString packId = selection.packId.trimmed();
    if (packId.isEmpty())
    {
        return;
    }

    const QVector<an::ui::CatalogPackDescriptor> packs = availableSpccCatalogPacksForDownload();
    const auto descriptorIt = std::find_if(packs.cbegin(), packs.cend(), [&packId](const auto& descriptor) {
        return descriptor.id.compare(packId, Qt::CaseInsensitive) == 0;
    });
    if (descriptorIt == packs.cend() ||
        descriptorIt->accessProfile.compare(QString::fromLatin1(an::ui::SPCC_LOCAL_FULL_ACCESS_PROFILE),
                                            Qt::CaseInsensitive) != 0)
    {
        return;
    }

    const an::ui::SpccResponseRepositoryInspection responseInspection =
        an::ui::inspectSpccResponseRepositoryDetailed(m_SpccResponseRepositoryDirectory);
    if (responseInspection.status == an::ui::SpccResponseRepositoryStatus::Ready)
    {
        return;
    }

    installManagedSpccResponseRepository();
}

void CatalogsDataSettingsPage::installMatchingSpccRegionalCatalogPack()
{
    const std::optional<an::ui::SpccRegionalCatalogRequest> request = loadPendingSpccRegionalCatalogRequest();
    if (!request.has_value())
    {
        updateSpccRegionalRequestStatus();
        return;
    }

    const QVector<an::ui::CatalogPackDescriptor> matchingPacks =
        an::ui::spccRegionalCatalogPacksForRequest(availableSpccCatalogPacksForDownload(), *request);
    if (matchingPacks.isEmpty())
    {
        updateSpccRegionalRequestStatus();
        return;
    }

    const auto selection = runSpccCatalogDownloadAssistant(matchingPacks);
    if (selection.has_value())
    {
        applySpccCatalogDownloadSelection(*selection);
    }
}

void CatalogsDataSettingsPage::installManagedSpccResponseRepository()
{
    const QVector<an::ui::SpccResponseRepositoryDescriptor> repositories =
        availableSpccResponseRepositoriesForDownload();
    if (repositories.isEmpty() || !m_SpccResponseDownloadService || m_SpccResponseDownloadService->isBusy())
    {
        return;
    }

    const an::ui::SpccResponseRepositoryDescriptor repository = repositories.front();
    m_SpccResponseDownloadService->setSpccResponseRepositoryRootDirectory(
        an::ui::defaultSpccResponseRepositoryDirectory());
    m_SpccResponseDownloadService->downloadSpccResponseRepository(repository);
}

bool CatalogsDataSettingsPage::confirmDeleteSelectedSpccCatalogPack(const QString& directoryPath,
                                                                    const bool exactPathRequired)
{
    const QString cleanPath = QDir::cleanPath(directoryPath);
    if (cleanPath.isEmpty())
    {
        return false;
    }

    if (exactPathRequired)
    {
        bool accepted = false;
        const QString typedPath = QInputDialog::getText(this, tr("Delete Selected SPCC Catalog"),
                                                        tr("This catalog is outside the AfterNight managed catalog "
                                                           "cache. Type the exact folder path to delete it:\n%1")
                                                            .arg(cleanPath),
                                                        QLineEdit::Normal, QString(), &accepted);
        return accepted && QDir::cleanPath(typedPath) == cleanPath;
    }

    return QMessageBox::warning(this, tr("Delete Selected SPCC Catalog"),
                                tr("Delete the selected SPCC catalog folder?\n%1").arg(cleanPath),
                                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}

bool CatalogsDataSettingsPage::confirmDeleteAllDownloadedSpccCatalogPacks(const QString& rootDirectory,
                                                                          const int packCount)
{
    if (packCount <= 0)
    {
        return false;
    }

    return QMessageBox::warning(this, tr("Delete Downloaded SPCC Catalogs"),
                                tr("Delete %n AfterNight-managed SPCC catalog folder(s) under:\n%1", nullptr, packCount)
                                    .arg(QDir::cleanPath(rootDirectory)),
                                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}

void CatalogsDataSettingsPage::evictSelectedGaiaCatalogPack()
{
    an::ui::PlateSolveSettings settings;
    settings.gaiaCatalogDirectory = m_GaiaCatalogDirectoryPicker ? m_GaiaCatalogDirectoryPicker->path() : QString();
    settings.gaiaCatalogPack = currentGaiaCatalogPack();

    const QString resolvedDirectory = an::ui::resolveGaiaCatalogDirectory(settings);
    if (resolvedDirectory.trimmed().isEmpty())
    {
        return;
    }

    const an::ui::GaiaCatalogDirectoryInspection inspection =
        an::ui::inspectGaiaCatalogDirectoryDetailed(resolvedDirectory);
    if (inspection.status == an::ui::GaiaCatalogDirectoryStatus::Missing)
    {
        return;
    }

    QDir packDir(QDir::cleanPath(resolvedDirectory));
    if (!packDir.exists() || !packDir.removeRecursively())
    {
        updateGaiaCatalogStatus();
        return;
    }

    refreshGaiaCatalogPackOptions(currentGaiaCatalogPack());
    updateGaiaCatalogStatus();
    markAsModified();
}

void CatalogsDataSettingsPage::evictSelectedSpccCatalogPack()
{
    const QString resolvedDirectory = an::ui::resolveSpccCatalogDirectory(
        m_SpccCatalogDirectoryPicker ? m_SpccCatalogDirectoryPicker->path() : QString(), currentSpccCatalogPack(),
        false);
    if (resolvedDirectory.trimmed().isEmpty())
    {
        return;
    }

    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(resolvedDirectory, false);
    if (inspection.status == an::ui::SpccCatalogDirectoryStatus::Missing)
    {
        return;
    }

    const bool exactPathRequired = !pathIsInsideDirectory(resolvedDirectory, an::ui::defaultSpccCatalogDirectory());
    if (!confirmDeleteSelectedSpccCatalogPack(resolvedDirectory, exactPathRequired))
    {
        return;
    }

    QDir packDir(QDir::cleanPath(resolvedDirectory));
    if (!packDir.exists() || !packDir.removeRecursively())
    {
        updateSpccCatalogStatus();
        return;
    }

    refreshSpccCatalogPackOptions(currentSpccCatalogPack());
    updateSpccCatalogStatus();
    markAsModified();
}

void CatalogsDataSettingsPage::deleteAllDownloadedSpccCatalogPacks()
{
    const QString rootDirectory = an::ui::defaultSpccCatalogDirectory();
    const QVector<an::ui::SpccCatalogPackLocation> packs = an::ui::scanSpccCatalogPacks(rootDirectory, false);
    if (!confirmDeleteAllDownloadedSpccCatalogPacks(rootDirectory, packs.size()))
    {
        return;
    }

    bool removedAny = false;
    for (const an::ui::SpccCatalogPackLocation& pack : packs)
    {
        QDir packDir(QDir::cleanPath(pack.directoryPath));
        if (packDir.exists() && packDir.removeRecursively())
        {
            removedAny = true;
        }
    }

    if (removedAny)
    {
        refreshSpccCatalogPackOptions(currentSpccCatalogPack());
        updateSpccCatalogStatus();
        markAsModified();
    }
}

void CatalogsDataSettingsPage::refreshGaiaCatalogPackOptions(const QString& preferredPack)
{
    if (!m_GaiaCatalogPackCombo)
    {
        return;
    }

    const QString preferred = preferredPack.trimmed().isEmpty() ? currentGaiaCatalogPack() : preferredPack.trimmed();
    const QVector<an::ui::GaiaCatalogPackLocation> discoveredPacks =
        an::ui::scanGaiaCatalogPacks(m_GaiaCatalogDirectoryPicker ? m_GaiaCatalogDirectoryPicker->path() : QString());
    const QVector<an::ui::CatalogPackDescriptor> builtInPacks = availableGaiaCatalogPacksForDownload();

    const QSignalBlocker blocker(m_GaiaCatalogPackCombo);
    m_GaiaCatalogPackCombo->clear();

    if (!discoveredPacks.isEmpty())
    {
        for (const an::ui::GaiaCatalogPackLocation& pack : discoveredPacks)
        {
            m_GaiaCatalogPackCombo->addItem(gaiaCatalogPackLabel(pack.packId, pack.displayName), pack.packId);
        }
    }
    else
    {
        for (const an::ui::CatalogPackDescriptor& pack : builtInPacks)
        {
            m_GaiaCatalogPackCombo->addItem(gaiaCatalogPackLabel(pack.id, pack.displayName), pack.id);
        }
    }

    int selectedIndex = preferred.isEmpty() ? -1 : m_GaiaCatalogPackCombo->findData(preferred);
    if (selectedIndex < 0)
    {
        selectedIndex = m_GaiaCatalogPackCombo->findData(an::ui::defaultGaiaCatalogPack());
    }
    if (selectedIndex < 0 && m_GaiaCatalogPackCombo->count() > 0)
    {
        selectedIndex = 0;
    }

    if (selectedIndex >= 0)
    {
        m_GaiaCatalogPackCombo->setCurrentIndex(selectedIndex);
    }
}

void CatalogsDataSettingsPage::refreshSpccCatalogPackOptions(const QString& preferredPack)
{
    if (!m_SpccCatalogPackCombo)
    {
        return;
    }

    const QString preferred = preferredPack.trimmed().isEmpty() ? currentSpccCatalogPack() : preferredPack.trimmed();
    const QVector<an::ui::SpccCatalogPackLocation> discoveredPacks = an::ui::scanSpccCatalogPacks(
        m_SpccCatalogDirectoryPicker ? m_SpccCatalogDirectoryPicker->path() : QString(), false);
    const QVector<an::ui::CatalogPackDescriptor> builtInPacks = availableSpccCatalogPacksForDownload();

    const QSignalBlocker blocker(m_SpccCatalogPackCombo);
    m_SpccCatalogPackCombo->clear();

    if (!discoveredPacks.isEmpty())
    {
        for (const an::ui::SpccCatalogPackLocation& pack : discoveredPacks)
        {
            m_SpccCatalogPackCombo->addItem(spccCatalogPackLabel(pack.manifestId, pack.displayName), pack.manifestId);
        }
    }
    else
    {
        for (const an::ui::CatalogPackDescriptor& pack : builtInPacks)
        {
            m_SpccCatalogPackCombo->addItem(spccCatalogPackLabel(pack.id, pack.displayName), pack.id);
        }
    }

    int selectedIndex = preferred.isEmpty() ? -1 : m_SpccCatalogPackCombo->findData(preferred);
    if (selectedIndex < 0)
    {
        selectedIndex = m_SpccCatalogPackCombo->findData(an::ui::defaultSpccCatalogPack());
    }
    if (selectedIndex < 0 && m_SpccCatalogPackCombo->count() > 0)
    {
        selectedIndex = 0;
    }

    if (selectedIndex >= 0)
    {
        m_SpccCatalogPackCombo->setCurrentIndex(selectedIndex);
    }
}

QString CatalogsDataSettingsPage::currentSpccCatalogPack() const
{
    if (!m_SpccCatalogPackCombo)
    {
        return QString();
    }

    const QString packId = m_SpccCatalogPackCombo->currentData().toString().trimmed();
    return packId.isEmpty() ? an::ui::defaultSpccCatalogPack() : packId;
}

QString CatalogsDataSettingsPage::currentGaiaCatalogPack() const
{
    if (!m_GaiaCatalogPackCombo)
    {
        return QString();
    }

    const QString packId = m_GaiaCatalogPackCombo->currentData().toString().trimmed();
    return packId.isEmpty() ? an::ui::defaultGaiaCatalogPack() : packId;
}

void CatalogsDataSettingsPage::updateGaiaCatalogStatus()
{
    refreshGaiaCatalogPackOptions();

    an::ui::PlateSolveSettings settings;
    settings.gaiaCatalogDirectory = m_GaiaCatalogDirectoryPicker ? m_GaiaCatalogDirectoryPicker->path() : QString();
    settings.gaiaCatalogPack = currentGaiaCatalogPack();

    const QString resolvedDirectory = an::ui::resolveGaiaCatalogDirectory(settings);
    const an::ui::GaiaCatalogDirectoryInspection inspection =
        an::ui::inspectGaiaCatalogDirectoryDetailed(resolvedDirectory);
    const bool hasDownloadableGaiaCatalogPacks = !availableGaiaCatalogPacksForDownload().isEmpty();
    if (m_GaiaCatalogDownloadButton)
    {
        m_GaiaCatalogDownloadButton->setEnabled(hasDownloadableGaiaCatalogPacks);
    }
    if (m_GaiaCatalogEvictButton)
    {
        m_GaiaCatalogEvictButton->setEnabled(inspection.status != an::ui::GaiaCatalogDirectoryStatus::Missing);
    }

    switch (inspection.status)
    {
    case an::ui::GaiaCatalogDirectoryStatus::Ready: {
        const QString catalogLabel =
            !inspection.catalogName.isEmpty()
                ? (!inspection.catalogId.isEmpty() && inspection.catalogName != inspection.catalogId
                       ? tr("%1 (%2)").arg(inspection.catalogName, inspection.catalogId)
                       : inspection.catalogName)
                : (!inspection.catalogId.isEmpty() ? inspection.catalogId : tr("Gaia chunk catalog"));
        m_GaiaCatalogDirectoryPicker->setStatus(tr("Selected Gaia catalog: %1").arg(catalogLabel), false);
        return;
    }

    case an::ui::GaiaCatalogDirectoryStatus::UnsupportedFormat:
        m_GaiaCatalogDirectoryPicker->setWarningStatus(
            tr("AfterNight does not read this Gaia catalog's format. Download it again with Download Catalog..."));
        return;

    case an::ui::GaiaCatalogDirectoryStatus::IncompleteInstall:
        m_GaiaCatalogDirectoryPicker->setWarningStatus(
            tr("Gaia catalog manifest was found, but one or more chunk files are missing or incomplete."));
        return;

    case an::ui::GaiaCatalogDirectoryStatus::Invalid:
        m_GaiaCatalogDirectoryPicker->setStatus(tr("Catalog directory does not contain a valid Gaia catalog manifest"),
                                                true);
        return;

    case an::ui::GaiaCatalogDirectoryStatus::Missing:
    default:
        m_GaiaCatalogDirectoryPicker->setWarningStatus(
            hasDownloadableGaiaCatalogPacks
                ? tr("Gaia catalog not installed. Use Download Catalog... to install a pack.")
                : tr("Gaia catalog not installed. No downloadable Gaia catalog packs are currently configured."));
        return;
    }
}

void CatalogsDataSettingsPage::updateSpccCatalogStatus()
{
    refreshSpccCatalogPackOptions();
    updateSpccRegionalRequestStatus();

    const QString resolvedDirectory = an::ui::resolveSpccCatalogDirectory(
        m_SpccCatalogDirectoryPicker ? m_SpccCatalogDirectoryPicker->path() : QString(), currentSpccCatalogPack(),
        false);
    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(resolvedDirectory, false);
    const bool hasDownloadableSpccCatalogPacks = !availableSpccCatalogPacksForDownload().isEmpty();
    if (m_SpccCatalogDownloadButton)
    {
        m_SpccCatalogDownloadButton->setEnabled(hasDownloadableSpccCatalogPacks);
    }
    if (m_SpccCatalogRepairButton)
    {
        m_SpccCatalogRepairButton->setEnabled(hasDownloadableSpccCatalogPacks);
    }
    if (m_SpccCatalogEvictButton)
    {
        m_SpccCatalogEvictButton->setEnabled(inspection.status != an::ui::SpccCatalogDirectoryStatus::Missing);
    }
    if (m_SpccCatalogDeleteAllButton)
    {
        m_SpccCatalogDeleteAllButton->setEnabled(
            !an::ui::scanSpccCatalogPacks(an::ui::defaultSpccCatalogDirectory(), false).isEmpty());
    }

    switch (inspection.status)
    {
    case an::ui::SpccCatalogDirectoryStatus::Ready: {
        const QString catalogLabel =
            !inspection.manifestId.isEmpty() ? inspection.manifestId : tr("SPCC Gaia XP sampled catalog");
        const QString accessProfile = inspection.accessProfile.isEmpty() ? tr("unknown") : inspection.accessProfile;
        m_SpccCatalogDirectoryPicker->setStatus(tr("Selected SPCC catalog: %1 (%2)").arg(catalogLabel, accessProfile),
                                                false);
        return;
    }

    case an::ui::SpccCatalogDirectoryStatus::PartialCache:
        m_SpccCatalogDirectoryPicker->setWarningStatus(
            tr("SPCC chunk cache: %1/%2 chunks cached (%3 missing). Missing chunks are downloaded when needed.")
                .arg(inspection.chunkFilesPresent)
                .arg(inspection.chunkCount)
                .arg(inspection.chunkFilesMissing));
        return;

    case an::ui::SpccCatalogDirectoryStatus::IncompleteInstall:
        m_SpccCatalogDirectoryPicker->setWarningStatus(
            tr("SPCC catalog metadata was found, but required metadata or sidecar files are incomplete."));
        return;

    case an::ui::SpccCatalogDirectoryStatus::Invalid:
        m_SpccCatalogDirectoryPicker->setStatus(tr("Catalog directory does not contain a valid SPCC catalog manifest"),
                                                true);
        return;

    case an::ui::SpccCatalogDirectoryStatus::Missing:
    default:
        m_SpccCatalogDirectoryPicker->setWarningStatus(hasDownloadableSpccCatalogPacks
                                                           ? tr("SPCC catalog cache is empty. SPCC can download "
                                                                "needed chunks on demand, or use Download Full "
                                                                "Offline Catalog... for offline use.")
                                                           : tr("SPCC catalog not installed. Select an existing SPCC "
                                                                "catalog directory; no approved downloadable SPCC "
                                                                "catalog packs are currently configured."));
        return;
    }
}

void CatalogsDataSettingsPage::updateSpccRegionalRequestStatus()
{
    if (!m_SpccRegionalRequestLabel)
    {
        return;
    }

    const std::optional<an::ui::SpccRegionalCatalogRequest> request = loadPendingSpccRegionalCatalogRequest();
    if (!request.has_value())
    {
        m_SpccRegionalRequestLabel->clear();
        m_SpccRegionalRequestLabel->setVisible(false);
        if (m_SpccRegionalInstallButton)
        {
            m_SpccRegionalInstallButton->setVisible(false);
            m_SpccRegionalInstallButton->setEnabled(false);
        }
        if (m_SpccRegionalRequestTitleLabel)
        {
            m_SpccRegionalRequestTitleLabel->setVisible(false);
        }
        return;
    }

    const QString targetName =
        request->targetName.trimmed().isEmpty() ? tr("current image") : request->targetName.trimmed();
    QString statusText = tr("%1: RA %2 deg, Dec %3 deg, radius %4 deg, G <= %5")
                             .arg(targetName)
                             .arg(request->centerRaDeg, 0, 'f', 5)
                             .arg(request->centerDecDeg, 0, 'f', 5)
                             .arg(request->radiusDeg, 0, 'f', 4)
                             .arg(request->magnitudeLimit, 0, 'f', 1);

    const QVector<an::ui::CatalogPackDescriptor> approvedPacks = availableSpccCatalogPacksForDownload();
    const QVector<an::ui::CatalogPackDescriptor> matchingPacks =
        an::ui::spccRegionalCatalogPacksForRequest(approvedPacks, *request);
    if (approvedPacks.isEmpty())
    {
        statusText += QLatin1Char('\n');
        statusText += tr("No approved downloadable SPCC regional descriptors are configured yet.");
    }
    else if (matchingPacks.isEmpty())
    {
        statusText += QLatin1Char('\n');
        statusText += tr("No approved downloadable SPCC regional pack covers this pending region yet.");
    }
    else
    {
        const an::ui::CatalogPackDescriptor& bestMatch = matchingPacks.front();
        const QString matchLabel =
            bestMatch.displayName.trimmed().isEmpty() ? bestMatch.id.trimmed() : bestMatch.displayName.trimmed();
        statusText += QLatin1Char('\n');
        statusText += tr("Matching approved regional pack: %1").arg(matchLabel);
    }

    m_SpccRegionalRequestLabel->setText(statusText);
    m_SpccRegionalRequestLabel->setVisible(true);
    if (m_SpccRegionalInstallButton)
    {
        m_SpccRegionalInstallButton->setVisible(true);
        m_SpccRegionalInstallButton->setEnabled(!matchingPacks.isEmpty());
    }
    if (m_SpccRegionalRequestTitleLabel)
    {
        m_SpccRegionalRequestTitleLabel->setVisible(true);
    }
}

QVector<an::ui::CatalogPackDescriptor> CatalogsDataSettingsPage::availableGaiaCatalogPacksForDownload() const
{
    return an::ui::availableGaiaCatalogPacks();
}

QVector<an::ui::CatalogPackDescriptor> CatalogsDataSettingsPage::availableSpccCatalogPacksForDownload() const
{
    return an::ui::availableSpccCatalogPacks();
}

QVector<an::ui::SpccResponseRepositoryDescriptor>
CatalogsDataSettingsPage::availableSpccResponseRepositoriesForDownload() const
{
    return an::ui::availableSpccResponseRepositories();
}

std::optional<an::ui::SpccRegionalCatalogRequest>
CatalogsDataSettingsPage::loadPendingSpccRegionalCatalogRequest() const
{
    return an::ui::loadSpccRegionalCatalogRequest(m_Settings);
}

bool CatalogsDataSettingsPage::clearPendingSpccRegionalCatalogRequest()
{
    return an::ui::clearSpccRegionalCatalogRequest(m_Settings);
}

std::optional<CatalogsDataSettingsPage::GaiaCatalogDownloadSelection>
CatalogsDataSettingsPage::runGaiaCatalogDownloadAssistant()
{
    const QVector<an::ui::CatalogPackDescriptor> packs = availableGaiaCatalogPacksForDownload();
    if (packs.isEmpty())
    {
        return std::nullopt;
    }

    an::ui::CatalogDownloadDialog dialog(packs, this);
    dialog.exec();

    if (!dialog.hasSuccessfulInstallation())
    {
        return std::nullopt;
    }

    return GaiaCatalogDownloadSelection{
        .packId = dialog.successfulPackId(),
        .installDirectory = dialog.successfulCatalogRootDirectory(),
    };
}

std::optional<CatalogsDataSettingsPage::SpccCatalogDownloadSelection>
CatalogsDataSettingsPage::runSpccCatalogDownloadAssistant()
{
    return runSpccCatalogDownloadAssistant(availableSpccCatalogPacksForDownload());
}

std::optional<CatalogsDataSettingsPage::SpccCatalogDownloadSelection>
CatalogsDataSettingsPage::runSpccCatalogDownloadAssistant(const QVector<an::ui::CatalogPackDescriptor>& packs)
{
    if (packs.isEmpty())
    {
        return std::nullopt;
    }

    an::ui::CatalogDownloadDialog dialog(
        packs, an::ui::CatalogDownloadInstallInspector(an::ui::spccCatalogDownloadInstallInspection), this);
    dialog.exec();

    if (!dialog.hasSuccessfulInstallation())
    {
        return std::nullopt;
    }

    return SpccCatalogDownloadSelection{
        .packId = dialog.successfulPackId(),
        .installDirectory = dialog.successfulCatalogRootDirectory(),
    };
}
