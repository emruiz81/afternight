// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "dialogs/CatalogDownloadDialog.h"

#include "app/FileDialogUtils.h"
#include "app/GaiaCatalogSettings.h"
#include "app/SpccCatalogSettings.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <algorithm>

namespace
{

[[nodiscard]] QString formatBytes(const qint64 bytes)
{
    if (bytes <= 0)
    {
        return QObject::tr("Unknown");
    }

    static const char* suffixes[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int suffix_index = 0;
    while (value >= 1024.0 && suffix_index < 3)
    {
        value /= 1024.0;
        ++suffix_index;
    }

    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', suffix_index == 0 ? 0 : 1),
                                       QString::fromLatin1(suffixes[suffix_index]));
}

[[nodiscard]] QString displayNameForPack(const an::ui::CatalogPackDescriptor& pack)
{
    if (pack.recommended)
    {
        return QObject::tr("%1 (Recommended)").arg(pack.displayName);
    }

    return pack.displayName;
}

[[nodiscard]] QString displayNameForPack(const an::ui::CatalogPackDescriptor& pack,
                                         const an::ui::CatalogDownloadInstallInspection& inspection)
{
    QString label = displayNameForPack(pack);
    switch (inspection.status)
    {
    case an::ui::CatalogDownloadInstallStatus::Ready:
        return QObject::tr("%1 (Downloaded)").arg(label);
    case an::ui::CatalogDownloadInstallStatus::PartialCache:
        return QObject::tr("%1 (Partial cache)").arg(label);
    case an::ui::CatalogDownloadInstallStatus::IncompleteInstall:
        return QObject::tr("%1 (Incomplete install)").arg(label);
    case an::ui::CatalogDownloadInstallStatus::UnsupportedFormat:
        return QObject::tr("%1 (Unsupported format)").arg(label);
    case an::ui::CatalogDownloadInstallStatus::Missing:
    case an::ui::CatalogDownloadInstallStatus::Invalid:
    default:
        return label;
    }
}

[[nodiscard]] QString installationStatusToolTip(const QString& installDirectory,
                                                const an::ui::CatalogDownloadInstallInspection& inspection)
{
    switch (inspection.status)
    {
    case an::ui::CatalogDownloadInstallStatus::Ready:
        return QObject::tr("Installed in %1").arg(installDirectory);
    case an::ui::CatalogDownloadInstallStatus::PartialCache:
        return QObject::tr("Partial chunk cache in %1").arg(installDirectory);
    case an::ui::CatalogDownloadInstallStatus::IncompleteInstall:
        return QObject::tr("Partially downloaded in %1").arg(installDirectory);
    case an::ui::CatalogDownloadInstallStatus::UnsupportedFormat:
        return QObject::tr("Catalog in an unsupported format found in %1").arg(installDirectory);
    case an::ui::CatalogDownloadInstallStatus::Missing:
    case an::ui::CatalogDownloadInstallStatus::Invalid:
    default:
        return QString();
    }
}

[[nodiscard]] an::ui::IGlobalSettingsProvider* currentGlobalSettings()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getGlobalSettings();
    }

    return nullptr;
}

} // namespace

W_OBJECT_IMPL(an::ui::CatalogDownloadDialog)

namespace an::ui
{

CatalogDownloadInstallInspection gaiaCatalogDownloadInstallInspection(const QString& catalogRootDirectory,
                                                                      const CatalogPackDescriptor& pack)
{
    const GaiaCatalogDirectoryInspection gaiaInspection = inspectGaiaCatalogPackInstall(catalogRootDirectory, pack.id);

    CatalogDownloadInstallInspection inspection;
    inspection.directoryPath = gaiaInspection.directoryPath;
    switch (gaiaInspection.status)
    {
    case GaiaCatalogDirectoryStatus::Ready:
        inspection.status = CatalogDownloadInstallStatus::Ready;
        break;
    case GaiaCatalogDirectoryStatus::UnsupportedFormat:
        inspection.status = CatalogDownloadInstallStatus::UnsupportedFormat;
        break;
    case GaiaCatalogDirectoryStatus::IncompleteInstall:
        inspection.status = CatalogDownloadInstallStatus::IncompleteInstall;
        break;
    case GaiaCatalogDirectoryStatus::Invalid:
        inspection.status = CatalogDownloadInstallStatus::Invalid;
        break;
    case GaiaCatalogDirectoryStatus::Missing:
    default:
        inspection.status = CatalogDownloadInstallStatus::Missing;
        break;
    }
    return inspection;
}

CatalogDownloadInstallInspection spccCatalogDownloadInstallInspection(const QString& catalogRootDirectory,
                                                                      const CatalogPackDescriptor& pack)
{
    const SpccCatalogDirectoryInspection spccInspection =
        inspectSpccCatalogPackInstall(catalogRootDirectory, pack.id, false);

    CatalogDownloadInstallInspection inspection;
    inspection.directoryPath = spccInspection.directoryPath;
    switch (spccInspection.status)
    {
    case SpccCatalogDirectoryStatus::Ready:
        inspection.status = CatalogDownloadInstallStatus::Ready;
        break;
    case SpccCatalogDirectoryStatus::PartialCache:
        inspection.status = CatalogDownloadInstallStatus::PartialCache;
        break;
    case SpccCatalogDirectoryStatus::IncompleteInstall:
        inspection.status = CatalogDownloadInstallStatus::IncompleteInstall;
        break;
    case SpccCatalogDirectoryStatus::Invalid:
        inspection.status = CatalogDownloadInstallStatus::Invalid;
        break;
    case SpccCatalogDirectoryStatus::Missing:
    default:
        inspection.status = CatalogDownloadInstallStatus::Missing;
        break;
    }
    inspection.cachedChunks = spccInspection.chunkFilesPresent;
    inspection.totalChunks = spccInspection.chunkCount;
    inspection.missingBytes = spccInspection.chunkBytesMissing;
    return inspection;
}

CatalogDownloadDialog::CatalogDownloadDialog(QVector<CatalogPackDescriptor> packs, QWidget* parent)
    : CatalogDownloadDialog(std::move(packs), CatalogDownloadInstallInspector(gaiaCatalogDownloadInstallInspection),
                            parent)
{
}

CatalogDownloadDialog::CatalogDownloadDialog(QVector<CatalogPackDescriptor> packs,
                                             CatalogDownloadInstallInspector installInspector, QWidget* parent)
    : QDialog(parent), m_service(new CatalogDownloadService(this)), m_packs(std::move(packs)),
      m_installInspector(std::move(installInspector))
{
    if (!m_installInspector)
    {
        m_installInspector = gaiaCatalogDownloadInstallInspection;
    }

    setModal(true);
    setWindowTitle(tr("Download Catalog"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const auto& token_set = an::ui::tokens();

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(token_set.spacing.lg, token_set.spacing.lg, token_set.spacing.lg,
                                    token_set.spacing.lg);
    root_layout->setSpacing(token_set.spacing.md);

    auto* form_layout = new QFormLayout();
    form_layout->setHorizontalSpacing(token_set.spacing.md);
    form_layout->setVerticalSpacing(token_set.spacing.sm);

    m_packCombo = new QComboBox(this);
    m_packCombo->setObjectName(QStringLiteral("catalogPackCombo"));
    form_layout->addRow(tr("Catalog pack:"), m_packCombo);

    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setObjectName(QStringLiteral("catalogSizeLabel"));
    m_sizeLabel->setWordWrap(true);
    form_layout->addRow(tr("Size:"), m_sizeLabel);

    m_descriptionEdit = new QPlainTextEdit(this);
    m_descriptionEdit->setObjectName(QStringLiteral("catalogDescriptionEdit"));
    m_descriptionEdit->setReadOnly(true);
    m_descriptionEdit->setMinimumHeight(fontMetrics().lineSpacing() * 5 + token_set.spacing.md * 2);
    form_layout->addRow(tr("Description:"), m_descriptionEdit);

    auto* target_layout = new QHBoxLayout();
    target_layout->setSpacing(token_set.spacing.sm);
    m_targetDirEdit = new QLineEdit(CatalogDownloadService::defaultCatalogRootDirectory(), this);
    m_targetDirEdit->setObjectName(QStringLiteral("catalogTargetDirectoryEdit"));
    auto* browse_button = new QPushButton(tr("Browse"), this);
    browse_button->setObjectName(QStringLiteral("catalogBrowseButton"));
    target_layout->addWidget(m_targetDirEdit, 1);
    target_layout->addWidget(browse_button);
    form_layout->addRow(tr("Install to:"), target_layout);

    root_layout->addLayout(form_layout);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("catalogDownloadProgressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    root_layout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("catalogDownloadStatusLabel"));
    m_statusLabel->setWordWrap(true);
    root_layout->addWidget(m_statusLabel);

    auto* button_box = new QDialogButtonBox(this);
    m_downloadButton = button_box->addButton(tr("Download"), QDialogButtonBox::AcceptRole);
    m_downloadButton->setObjectName(QStringLiteral("catalogDownloadButton"));
    m_cancelButton = button_box->addButton(tr("Cancel Download"), QDialogButtonBox::ActionRole);
    m_cancelButton->setObjectName(QStringLiteral("catalogCancelDownloadButton"));
    m_closeButton = button_box->addButton(QDialogButtonBox::Close);
    m_closeButton->setObjectName(QStringLiteral("catalogCloseButton"));
    root_layout->addWidget(button_box);

    connect(m_packCombo, &QComboBox::currentIndexChanged, this, [this]() {
        refreshPackDetails();
        updateUiState();
    });
    connect(m_targetDirEdit, &QLineEdit::textChanged, this, [this]() {
        refreshPackChoices();
        refreshPackDetails();
        updateUiState();
    });
    connect(browse_button, &QPushButton::clicked, this, [this]() {
        const QString directory = an::ui::dialogs::getExistingDirectory(
            currentGlobalSettings(), this, tr("Choose catalog directory"), m_targetDirEdit->text());
        if (!directory.isEmpty())
        {
            m_targetDirEdit->setText(directory);
        }
    });
    connect(m_downloadButton, &QPushButton::clicked, this, [this]() {
        m_service->setCatalogRootDirectory(m_targetDirEdit->text());
        m_busy = true;
        updateUiState();
        m_service->downloadCatalogPack(currentPack());
    });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { m_service->cancel(); });
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_service, &CatalogDownloadService::downloadProgress, this, &CatalogDownloadDialog::applyProgress);
    connect(m_service, &CatalogDownloadService::downloadFinished, this, &CatalogDownloadDialog::applyResult);

    refreshPackChoices();
    refreshPackDetails();
    updateUiState();
    resize(640, 420);
}

CatalogDownloadDialog::~CatalogDownloadDialog()
{
    if (m_service)
    {
        m_service->cancel();
    }
}

bool CatalogDownloadDialog::hasSuccessfulInstallation() const
{
    return m_lastResult.success && !m_lastResult.packId.isEmpty() && !m_lastResult.installDirectory.isEmpty();
}

QString CatalogDownloadDialog::successfulPackId() const
{
    return hasSuccessfulInstallation() ? m_lastResult.packId : QString();
}

QString CatalogDownloadDialog::successfulCatalogRootDirectory() const
{
    return hasSuccessfulInstallation() && m_targetDirEdit ? m_targetDirEdit->text().trimmed() : QString();
}

QString CatalogDownloadDialog::successfulInstallDirectory() const
{
    return hasSuccessfulInstallation() ? m_lastResult.installDirectory : QString();
}

CatalogPackDescriptor CatalogDownloadDialog::currentPack() const
{
    const int index = m_packCombo ? m_packCombo->currentIndex() : -1;
    if (index < 0 || index >= m_packs.size())
    {
        return {};
    }

    return m_packs[index];
}

void CatalogDownloadDialog::refreshPackChoices()
{
    if (!m_packCombo)
    {
        return;
    }

    const QString currentPackId = m_packCombo->currentData().toString().trimmed();
    const QString installRoot = m_targetDirEdit ? m_targetDirEdit->text().trimmed() : QString();

    const QSignalBlocker blocker(m_packCombo);
    m_packCombo->clear();

    for (const CatalogPackDescriptor& pack : m_packs)
    {
        const CatalogDownloadInstallInspection inspection = m_installInspector(installRoot, pack);
        const QString installDirectory = inspection.directoryPath;

        m_packCombo->addItem(displayNameForPack(pack, inspection), pack.id);
        const int itemIndex = m_packCombo->count() - 1;
        const QString toolTip = installationStatusToolTip(installDirectory, inspection);
        if (!toolTip.isEmpty())
        {
            m_packCombo->setItemData(itemIndex, toolTip, Qt::ToolTipRole);
        }
    }

    int selectedIndex = currentPackId.isEmpty() ? -1 : m_packCombo->findData(currentPackId);
    if (selectedIndex < 0 && m_packCombo->count() > 0)
    {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0)
    {
        m_packCombo->setCurrentIndex(selectedIndex);
    }
}

void CatalogDownloadDialog::refreshPackDetails()
{
    const CatalogPackDescriptor pack = currentPack();
    const CatalogDownloadInstallInspection inspection =
        m_installInspector(m_targetDirEdit ? m_targetDirEdit->text() : QString(), pack);
    const QString installDirectory = inspection.directoryPath;

    if (pack.downloadSizeBytes > 0 && pack.installedSizeBytes > 0 && pack.downloadSizeBytes != pack.installedSizeBytes)
    {
        m_sizeLabel->setText(tr("Download %1, installed approximately %2.")
                                 .arg(formatBytes(pack.downloadSizeBytes), formatBytes(pack.installedSizeBytes)));
    }
    else if (pack.downloadSizeBytes > 0)
    {
        m_sizeLabel->setText(tr("Download and install approximately %1.").arg(formatBytes(pack.downloadSizeBytes)));
    }
    else
    {
        m_sizeLabel->setText(tr("Install size approximately %1.").arg(formatBytes(pack.installedSizeBytes)));
    }
    m_descriptionEdit->setPlainText(pack.description);
    if (pack.id.isEmpty())
    {
        m_statusLabel->setText(
            tr("Built-in Gaia downloads are unavailable. Configure a local catalog directory manually."));
    }
    else if (!isUsableCatalogManifestUrl(pack.manifestUrl))
    {
        m_statusLabel->setText(tr("This pack has no manifest URL configured."));
    }
    else if (inspection.status == CatalogDownloadInstallStatus::Ready)
    {
        m_statusLabel->setText(
            tr("This pack is already downloaded in %1. Download again to verify and repair it.").arg(installDirectory));
    }
    else if (inspection.status == CatalogDownloadInstallStatus::PartialCache)
    {
        m_statusLabel->setText(tr("A partial SPCC chunk cache was found in %1 (%2/%3 chunks cached, %4 missing).")
                                   .arg(installDirectory)
                                   .arg(inspection.cachedChunks)
                                   .arg(inspection.totalChunks)
                                   .arg(formatBytes(inspection.missingBytes)));
    }
    else if (inspection.status == CatalogDownloadInstallStatus::IncompleteInstall)
    {
        m_statusLabel->setText(
            tr("An incomplete installation was found in %1. Download again to repair missing metadata.")
                .arg(installDirectory));
    }
    else if (inspection.status == CatalogDownloadInstallStatus::UnsupportedFormat)
    {
        m_statusLabel->setText(tr("A catalog in an unsupported format was found in %1. Download again to replace it.")
                                   .arg(installDirectory));
    }
    else if (!m_busy)
    {
        m_statusLabel->setText(tr("Select a destination and start the download."));
    }
}

void CatalogDownloadDialog::updateUiState()
{
    const CatalogPackDescriptor pack = currentPack();
    const bool has_pack = !pack.id.isEmpty();
    const bool can_download = has_pack && isUsableCatalogManifestUrl(pack.manifestUrl);
    const CatalogDownloadInstallInspection inspection =
        m_installInspector(m_targetDirEdit ? m_targetDirEdit->text() : QString(), pack);

    m_packCombo->setEnabled(!m_busy);
    m_targetDirEdit->setEnabled(!m_busy);
    m_downloadButton->setVisible(!m_busy);
    m_downloadButton->setEnabled(!m_busy && can_download);
    if (!m_busy)
    {
        if (inspection.status == CatalogDownloadInstallStatus::PartialCache)
        {
            m_downloadButton->setText(tr("Download Missing"));
        }
        else if (inspection.status == CatalogDownloadInstallStatus::Ready ||
                 inspection.status == CatalogDownloadInstallStatus::IncompleteInstall ||
                 inspection.status == CatalogDownloadInstallStatus::UnsupportedFormat)
        {
            m_downloadButton->setText(tr("Verify && Repair"));
        }
        else
        {
            m_downloadButton->setText(tr("Download"));
        }
    }
    m_cancelButton->setVisible(m_busy);
    m_cancelButton->setEnabled(m_busy);
    m_closeButton->setEnabled(!m_busy);
}

void CatalogDownloadDialog::applyProgress(const CatalogDownloadProgress& progress)
{
    if (progress.bytesTotal > 0)
    {
        m_progressBar->setRange(0, 100);
        const int percent = static_cast<int>((100.0 * static_cast<double>(progress.bytesReceived)) /
                                             static_cast<double>(progress.bytesTotal));
        m_progressBar->setValue(std::clamp(percent, 0, 100));
    }
    else
    {
        m_progressBar->setRange(0, 0);
    }

    if (progress.phase == QStringLiteral("download"))
    {
        const int chunkNumber =
            progress.filesTotal > 0 ? (std::min)(progress.filesCompleted + 1, progress.filesTotal) : 0;
        if (progress.filesTotal > 0)
        {
            m_statusLabel->setText(tr("Downloading chunk %1 of %2 (%3)... %4 / %5")
                                       .arg(chunkNumber)
                                       .arg(progress.filesTotal)
                                       .arg(progress.currentFile)
                                       .arg(formatBytes(progress.bytesReceived), formatBytes(progress.bytesTotal)));
        }
        else
        {
            m_statusLabel->setText(tr("Downloading catalog files... %1 / %2")
                                       .arg(formatBytes(progress.bytesReceived), formatBytes(progress.bytesTotal)));
        }
    }
    else if (progress.phase == QStringLiteral("manifest"))
    {
        m_statusLabel->setText(tr("Fetching catalog manifest..."));
    }
    else if (progress.phase == QStringLiteral("verify"))
    {
        const int chunkNumber =
            progress.filesTotal > 0 ? (std::min)(progress.filesCompleted + 1, progress.filesTotal) : 0;
        if (progress.filesTotal > 0)
        {
            m_statusLabel->setText(tr("Verifying chunk %1 of %2 (%3)...")
                                       .arg(chunkNumber)
                                       .arg(progress.filesTotal)
                                       .arg(progress.currentFile));
        }
        else
        {
            m_statusLabel->setText(tr("Verifying catalog files..."));
        }
    }
    else
    {
        m_statusLabel->setText(tr("Preparing..."));
    }
}

void CatalogDownloadDialog::applyResult(const CatalogDownloadResult& result)
{
    m_lastResult = result;
    m_busy = false;
    refreshPackChoices();
    refreshPackDetails();
    updateUiState();

    if (result.success)
    {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_statusLabel->setText(tr("Catalog installed to %1").arg(result.installDirectory));
        return;
    }

    if (result.cancelled)
    {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_statusLabel->setText(tr("Catalog download cancelled."));
        return;
    }

    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_statusLabel->setText(result.errorMessage.isEmpty() ? tr("Catalog download failed.") : result.errorMessage);
}

} // namespace an::ui
