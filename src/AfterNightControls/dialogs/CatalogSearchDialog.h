// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "app/CatalogSearchService.h"

#include <QDialog>
#include <verdigris/wobjectdefs.h>

class QComboBox;
class QCompleter;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStringListModel;
class QTableWidget;

namespace an::ui
{

class CatalogSearchService;

class AFTERNIGHTCONTROLS_API CatalogSearchDialog : public QDialog
{
    W_OBJECT(CatalogSearchDialog)

public:
    explicit CatalogSearchDialog(const QString& initialQuery, QWidget* parent = nullptr);
    ~CatalogSearchDialog() override;

    [[nodiscard]] an::ui::CatalogSearchEntry selectedEntry() const;
    void applySearchResult(const an::ui::CatalogSearchResult& result);

private:
    [[nodiscard]] an::ui::CatalogServer currentServer() const;
    [[nodiscard]] QString currentQuery() const;
    void beginSearch();
    void acceptSelectedEntry();
    void updateSearchButtonState();
    void updateOkButtonState();
    void updateDetailsPane();
    void updateBusyState(bool busy);
    void setStatusMessage(const QString& message, const QString& tone = QStringLiteral("secondary"));
    void populateResults(const QVector<an::ui::CatalogSearchEntry>& entries);
    void persistAcceptedEntry();

    an::ui::CatalogSearchService* m_service = nullptr;
    QComboBox* m_serverCombo = nullptr;
    QLineEdit* m_queryEdit = nullptr;
    QPushButton* m_searchButton = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QTableWidget* m_resultsTable = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPlainTextEdit* m_descriptionEdit = nullptr;
    QPlainTextEdit* m_aliasesEdit = nullptr;
    QPushButton* m_okButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QCompleter* m_completer = nullptr;
    QStringListModel* m_historyModel = nullptr;
    QVector<an::ui::CatalogSearchEntry> m_entries;
    an::ui::CatalogSearchEntry m_selectedEntry;
    bool m_busy = false;
};

} // namespace an::ui
