// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImportProgressDialog.h"

#include "processes/ImageIntegration/widgets/ImportProgressPanel.h"

#include <QCloseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace ImageIntegration
{
ImportProgressDialog::ImportProgressDialog(int totalFiles, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Importing files..."));
    setWindowModality(Qt::WindowModal);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);
    m_panel = new ImportProgressPanel(totalFiles, this);
    root->addWidget(m_panel);
    // Cancel closes the dialog, as rejecting it cancels the import.
    QObject::connect(m_panel->cancelButton(), &QPushButton::clicked, this, [this]() { reject(); });
}

ImportProgressPanel* ImportProgressDialog::panel() const noexcept
{
    return m_panel;
}

void ImportProgressDialog::setProgress(int completed)
{
    m_panel->setProgress(completed);
    if (completed >= m_panel->totalFiles())
    {
        accept();
    }
}

void ImportProgressDialog::setCurrentFile(const QString& fileName)
{
    m_panel->setCurrentFile(fileName);
}

void ImportProgressDialog::setEtaMs(long long remainingMs)
{
    m_panel->setEtaMs(remainingMs);
}

void ImportProgressDialog::setIssueCount(int count)
{
    m_panel->setIssueCount(count);
}

void ImportProgressDialog::setIssueDetails(const QStringList& lines)
{
    m_panel->setIssueDetails(lines);
}

QPushButton* ImportProgressDialog::pauseButton() const
{
    return m_panel->pauseButton();
}

QPushButton* ImportProgressDialog::cancelButton() const
{
    return m_panel->cancelButton();
}

QLabel* ImportProgressDialog::currentLabel() const
{
    return m_panel->currentLabel();
}

QLabel* ImportProgressDialog::issuesLabel() const
{
    return m_panel->issuesLabel();
}

QTextEdit* ImportProgressDialog::issuesTextEdit() const
{
    return m_panel->issuesTextEdit();
}

bool ImportProgressDialog::isCancelled() const
{
    return m_panel->isCancelled();
}

bool ImportProgressDialog::isPaused() const
{
    return m_panel->isPaused();
}

void ImportProgressDialog::reject()
{
    m_panel->markCancelled();
    QDialog::reject();
}

void ImportProgressDialog::closeEvent(QCloseEvent* event)
{
    m_panel->markCancelled();
    QDialog::closeEvent(event);
}
} // namespace ImageIntegration
