// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "app/CatalogDownloadService.h"

#include <QDialog>
#include <QVector>
#include <verdigris/wobjectdefs.h>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QPlainTextEdit;

namespace an::ui
{

enum class CatalogDownloadInstallStatus
{
    Missing,
    Ready,
    PartialCache,
    UnsupportedFormat,
    IncompleteInstall,
    Invalid,
};

struct CatalogDownloadInstallInspection
{
    CatalogDownloadInstallStatus status = CatalogDownloadInstallStatus::Missing;
    QString directoryPath;
    int cachedChunks = 0;
    int totalChunks = 0;
    qint64 missingBytes = 0;
};

using CatalogDownloadInstallInspector = std::function<CatalogDownloadInstallInspection(
    const QString& catalogRootDirectory, const CatalogPackDescriptor& pack)>;

[[nodiscard]] CatalogDownloadInstallInspection gaiaCatalogDownloadInstallInspection(const QString& catalogRootDirectory,
                                                                                    const CatalogPackDescriptor& pack);
[[nodiscard]] AFTERNIGHTCONTROLS_API CatalogDownloadInstallInspection
spccCatalogDownloadInstallInspection(const QString& catalogRootDirectory, const CatalogPackDescriptor& pack);

class AFTERNIGHTCONTROLS_API CatalogDownloadDialog : public QDialog
{
    W_OBJECT(CatalogDownloadDialog)

public:
    explicit CatalogDownloadDialog(QVector<CatalogPackDescriptor> packs, QWidget* parent = nullptr);
    CatalogDownloadDialog(QVector<CatalogPackDescriptor> packs, CatalogDownloadInstallInspector installInspector,
                          QWidget* parent = nullptr);
    ~CatalogDownloadDialog() override;

    [[nodiscard]] bool hasSuccessfulInstallation() const;
    [[nodiscard]] QString successfulPackId() const;
    [[nodiscard]] QString successfulCatalogRootDirectory() const;
    [[nodiscard]] QString successfulInstallDirectory() const;

private:
    [[nodiscard]] CatalogPackDescriptor currentPack() const;
    void refreshPackChoices();
    void refreshPackDetails();
    void updateUiState();
    void applyProgress(const CatalogDownloadProgress& progress);
    void applyResult(const CatalogDownloadResult& result);

    CatalogDownloadService* m_service = nullptr;
    QVector<CatalogPackDescriptor> m_packs;
    CatalogDownloadInstallInspector m_installInspector;
    QComboBox* m_packCombo = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QPlainTextEdit* m_descriptionEdit = nullptr;
    QLineEdit* m_targetDirEdit = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_downloadButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    bool m_busy = false;
    CatalogDownloadResult m_lastResult;
};

} // namespace an::ui
