// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "dialogs/CatalogSearchDialog.h"

#include "SexagesimalFormatter.h"

#include "app/ApplicationStorage.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStringListModel>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <cmath>

namespace
{

constexpr int kMaxRecentObjects = 20;

/// A result's right ascension: hours, minutes and seconds to a hundredth, rounded once.
[[nodiscard]] QString formatRaSexagesimal(const double ra_deg)
{
    return an::ui::rightAscensionLabel(ra_deg, 2u);
}

/// A result's declination: degrees, arcminutes and arcseconds to a tenth, rounded once.
[[nodiscard]] QString formatDecSexagesimal(const double dec_deg)
{
    return an::ui::declinationLabel(dec_deg, 1u);
}

[[nodiscard]] QStringList loadRecentHistory()
{
    QSettings settings = an::ui::applicationSettings();
    return an::ui::catalog_search::updatedRecentHistory(
        settings.value(QString::fromUtf8(SettingsKeys::PLATESOLVE_RECENT_OBJECTS)).toStringList(), QString(),
        kMaxRecentObjects);
}

void saveRecentHistory(const QStringList& history)
{
    QSettings settings = an::ui::applicationSettings();
    settings.setValue(QString::fromUtf8(SettingsKeys::PLATESOLVE_RECENT_OBJECTS), history);
}

QTableWidgetItem* makeReadOnlyItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags((item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

W_OBJECT_IMPL(an::ui::CatalogSearchDialog)

namespace an::ui
{

CatalogSearchDialog::CatalogSearchDialog(const QString& initialQuery, QWidget* parent)
    : QDialog(parent), m_service(new an::ui::CatalogSearchService(this))
{
    setModal(true);
    setWindowTitle(tr("Find Object"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const auto& tokenSet = an::ui::tokens();

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg);
    rootLayout->setSpacing(tokenSet.spacing.md);

    auto* serverLayout = new QHBoxLayout();
    serverLayout->setSpacing(tokenSet.spacing.sm);
    auto* serverLabel = new QLabel(tr("Server:"), this);
    serverLayout->addWidget(serverLabel);

    m_serverCombo = new QComboBox(this);
    m_serverCombo->setObjectName(QStringLiteral("catalogServerCombo"));
    m_serverCombo->addItem(an::ui::catalog_search::displayName(an::ui::CatalogServer::LocalCatalog),
                           static_cast<int>(an::ui::CatalogServer::LocalCatalog));
    m_serverCombo->addItem(an::ui::catalog_search::displayName(an::ui::CatalogServer::Simbad),
                           static_cast<int>(an::ui::CatalogServer::Simbad));
    m_serverCombo->addItem(an::ui::catalog_search::displayName(an::ui::CatalogServer::CdsSesame),
                           static_cast<int>(an::ui::CatalogServer::CdsSesame));
    m_serverCombo->addItem(an::ui::catalog_search::displayName(an::ui::CatalogServer::VizierSesame),
                           static_cast<int>(an::ui::CatalogServer::VizierSesame));
    serverLayout->addWidget(m_serverCombo, 1);
    rootLayout->addLayout(serverLayout);

    auto* queryLayout = new QHBoxLayout();
    queryLayout->setSpacing(tokenSet.spacing.sm);
    auto* queryLabel = new QLabel(tr("Object:"), this);
    queryLayout->addWidget(queryLabel);

    m_queryEdit = new QLineEdit(initialQuery.trimmed(), this);
    m_queryEdit->setObjectName(QStringLiteral("catalogQueryEdit"));
    m_queryEdit->setClearButtonEnabled(true);
    queryLayout->addWidget(m_queryEdit, 1);

    m_historyModel = new QStringListModel(loadRecentHistory(), this);
    m_completer = new QCompleter(m_historyModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_queryEdit->setCompleter(m_completer);

    m_searchButton = new QPushButton(tr("Search"), this);
    m_searchButton->setObjectName(QStringLiteral("catalogSearchButton"));
    queryLayout->addWidget(m_searchButton);
    rootLayout->addLayout(queryLayout);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("catalogProgressBar"));
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    rootLayout->addWidget(m_progressBar);

    m_resultsTable = new QTableWidget(this);
    m_resultsTable->setObjectName(QStringLiteral("catalogResultsTable"));
    m_resultsTable->setColumnCount(4);
    m_resultsTable->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("RA"), tr("Dec")});
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setAlternatingRowColors(true);
    m_resultsTable->setSortingEnabled(false);
    m_resultsTable->verticalHeader()->setVisible(false);
    m_resultsTable->horizontalHeader()->setStretchLastSection(false);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    rootLayout->addWidget(m_resultsTable, 1);

    auto* detailsLayout = new QFormLayout();
    detailsLayout->setHorizontalSpacing(tokenSet.spacing.md);
    detailsLayout->setVerticalSpacing(tokenSet.spacing.sm);

    const int detailsHeight = fontMetrics().lineSpacing() * 4 + tokenSet.spacing.md * 2;

    m_descriptionEdit = new QPlainTextEdit(this);
    m_descriptionEdit->setObjectName(QStringLiteral("catalogDescriptionEdit"));
    m_descriptionEdit->setReadOnly(true);
    m_descriptionEdit->setTabChangesFocus(true);
    m_descriptionEdit->setMinimumHeight(detailsHeight);
    detailsLayout->addRow(tr("Description / Notes:"), m_descriptionEdit);

    m_aliasesEdit = new QPlainTextEdit(this);
    m_aliasesEdit->setObjectName(QStringLiteral("catalogAliasesEdit"));
    m_aliasesEdit->setReadOnly(true);
    m_aliasesEdit->setTabChangesFocus(true);
    m_aliasesEdit->setMinimumHeight(detailsHeight);
    detailsLayout->addRow(tr("Other names:"), m_aliasesEdit);

    rootLayout->addLayout(detailsLayout);

    m_statusLabel = new QLabel(tr("Enter an object name and choose a server."), this);
    m_statusLabel->setObjectName(QStringLiteral("catalogStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    rootLayout->addWidget(m_statusLabel);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_cancelButton = buttonBox->button(QDialogButtonBox::Cancel);
    m_okButton->setObjectName(QStringLiteral("catalogOkButton"));
    m_cancelButton->setObjectName(QStringLiteral("catalogCancelButton"));
    rootLayout->addWidget(buttonBox);

    connect(m_queryEdit, &QLineEdit::textChanged, this, [this]() { updateSearchButtonState(); });
    connect(m_queryEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchButton->isEnabled())
        {
            beginSearch();
        }
    });
    connect(m_searchButton, &QPushButton::clicked, this, &CatalogSearchDialog::beginSearch);
    connect(m_resultsTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateOkButtonState();
        updateDetailsPane();
    });
    connect(m_resultsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        m_resultsTable->selectRow(row);
        acceptSelectedEntry();
    });
    connect(m_okButton, &QPushButton::clicked, this, &CatalogSearchDialog::acceptSelectedEntry);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_service, &an::ui::CatalogSearchService::searchFinished, this,
            [this](const an::ui::CatalogSearchResult& result) {
                updateBusyState(false);
                applySearchResult(result);
            });

    updateBusyState(false);
    updateOkButtonState();
    updateDetailsPane();
    resize(720, 460);
}

CatalogSearchDialog::~CatalogSearchDialog()
{
    if (m_service)
    {
        m_service->cancel();
    }
}

an::ui::CatalogSearchEntry CatalogSearchDialog::selectedEntry() const
{
    return m_selectedEntry;
}

void CatalogSearchDialog::applySearchResult(const an::ui::CatalogSearchResult& result)
{
    if (!result.success)
    {
        populateResults({});
        setStatusMessage(result.errorMessage.isEmpty() ? tr("Catalog search failed.") : result.errorMessage,
                         QStringLiteral("error"));
        return;
    }

    populateResults(result.entries);
    if (result.entries.isEmpty())
    {
        setStatusMessage(tr("No matching objects were found."), QStringLiteral("warning"));
        return;
    }

    setStatusMessage(tr("Found %1 result(s).").arg(QString::number(result.entries.size())));
}

an::ui::CatalogServer CatalogSearchDialog::currentServer() const
{
    return static_cast<an::ui::CatalogServer>(m_serverCombo->currentData().toInt());
}

QString CatalogSearchDialog::currentQuery() const
{
    return m_queryEdit->text().simplified();
}

void CatalogSearchDialog::beginSearch()
{
    const QString query = currentQuery();
    if (query.isEmpty())
    {
        setStatusMessage(tr("Enter an object name to search."), QStringLiteral("warning"));
        return;
    }

    populateResults({});
    m_selectedEntry = {};
    updateBusyState(true);
    setStatusMessage(tr("Searching %1…").arg(an::ui::catalog_search::displayName(currentServer())));
    LOG_INFO("Opening catalog search for '{}' via {}", query.toStdString(),
             an::ui::catalog_search::displayName(currentServer()).toStdString());
    m_service->searchByName(query, currentServer());
}

void CatalogSearchDialog::acceptSelectedEntry()
{
    const auto selected_items = m_resultsTable->selectionModel() != nullptr
                                    ? m_resultsTable->selectionModel()->selectedRows()
                                    : QModelIndexList{};
    if (selected_items.isEmpty())
    {
        updateOkButtonState();
        return;
    }

    const int row = selected_items.front().row();
    if (row < 0 || row >= m_entries.size())
    {
        updateOkButtonState();
        return;
    }

    m_selectedEntry = m_entries.at(row);
    persistAcceptedEntry();
    LOG_INFO("Catalog search accepted '{}' ({}) RA={:.6f} Dec={:.6f}", m_selectedEntry.objectName.toStdString(),
             m_selectedEntry.resolver.toStdString(), m_selectedEntry.raDeg, m_selectedEntry.decDeg);
    accept();
}

void CatalogSearchDialog::updateSearchButtonState()
{
    if (!m_searchButton)
    {
        return;
    }

    m_searchButton->setEnabled(!m_busy && !currentQuery().isEmpty());
}

void CatalogSearchDialog::updateOkButtonState()
{
    const bool has_selection = m_resultsTable != nullptr && m_resultsTable->selectionModel() != nullptr &&
                               !m_resultsTable->selectionModel()->selectedRows().isEmpty();
    m_okButton->setEnabled(!m_busy && has_selection);
}

void CatalogSearchDialog::updateDetailsPane()
{
    if (m_descriptionEdit == nullptr || m_aliasesEdit == nullptr)
    {
        return;
    }

    const auto selected_items = m_resultsTable != nullptr && m_resultsTable->selectionModel() != nullptr
                                    ? m_resultsTable->selectionModel()->selectedRows()
                                    : QModelIndexList{};
    if (selected_items.isEmpty())
    {
        m_descriptionEdit->setPlainText(tr("Select a result to review any available description or notes."));
        m_aliasesEdit->setPlainText(tr("Select a result to review alternate catalog names."));
        return;
    }

    const int row = selected_items.front().row();
    if (row < 0 || row >= m_entries.size())
    {
        m_descriptionEdit->clear();
        m_aliasesEdit->clear();
        return;
    }

    const auto& entry = m_entries.at(row);
    m_descriptionEdit->setPlainText(entry.description.trimmed().isEmpty()
                                        ? tr("No description or notes were returned for this result.")
                                        : entry.description);
    m_aliasesEdit->setPlainText(entry.alternateNames.isEmpty()
                                    ? tr("No alternate catalog names were returned for this result.")
                                    : entry.alternateNames.join(QLatin1Char('\n')));
}

void CatalogSearchDialog::updateBusyState(const bool busy)
{
    m_busy = busy;
    m_progressBar->setVisible(busy);
    m_serverCombo->setEnabled(!busy);
    m_queryEdit->setEnabled(!busy);
    m_resultsTable->setEnabled(!busy);
    updateSearchButtonState();
    updateOkButtonState();
}

void CatalogSearchDialog::setStatusMessage(const QString& message, const QString& tone)
{
    m_statusLabel->setText(message);
    m_statusLabel->setProperty("anStatusTone", tone);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->update();
}

void CatalogSearchDialog::populateResults(const QVector<an::ui::CatalogSearchEntry>& entries)
{
    m_entries = entries;
    m_resultsTable->clearContents();
    m_resultsTable->setRowCount(entries.size());

    for (int row = 0; row < entries.size(); ++row)
    {
        const auto& entry = entries.at(row);
        m_resultsTable->setItem(row, 0, makeReadOnlyItem(entry.objectName));
        m_resultsTable->setItem(row, 1, makeReadOnlyItem(entry.objectType));
        m_resultsTable->setItem(row, 2, makeReadOnlyItem(formatRaSexagesimal(entry.raDeg)));
        m_resultsTable->setItem(row, 3, makeReadOnlyItem(formatDecSexagesimal(entry.decDeg)));
    }

    if (!entries.isEmpty())
    {
        m_resultsTable->resizeRowsToContents();
    }
    m_resultsTable->clearSelection();
    updateOkButtonState();
    updateDetailsPane();
}

void CatalogSearchDialog::persistAcceptedEntry()
{
    if (m_selectedEntry.objectName.trimmed().isEmpty())
    {
        return;
    }

    const QStringList updated = an::ui::catalog_search::updatedRecentHistory(
        m_historyModel->stringList(), m_selectedEntry.objectName, kMaxRecentObjects);
    m_historyModel->setStringList(updated);
    saveRecentHistory(updated);
}

} // namespace an::ui
