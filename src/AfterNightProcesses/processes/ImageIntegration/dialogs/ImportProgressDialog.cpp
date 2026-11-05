// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImportProgressDialog.h"

#include "processes/ImageIntegration/workflow/WorkflowTimeEstimator.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace ImageIntegration
{
ImportProgressDialog::ImportProgressDialog(int totalFiles, QWidget* parent)
    : QDialog(parent), m_totalFiles((std::max)(0, totalFiles))
{
    setWindowTitle(tr("Importing files..."));
    setWindowModality(Qt::WindowModal);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);

    m_lblCurrent = new QLabel(tr("Preparing..."), this);
    m_lblCurrent->setWordWrap(true);
    root->addWidget(m_lblCurrent);

    m_lblIssues = new QLabel(QString(), this);
    m_lblIssues->setProperty("anStatusTone", QStringLiteral("secondary"));
    m_lblIssues->setWordWrap(true);
    root->addWidget(m_lblIssues);

    m_txtIssues = new QTextEdit(this);
    m_txtIssues->setReadOnly(true);
    m_txtIssues->setVisible(false);
    m_txtIssues->setMaximumHeight(80);
    root->addWidget(m_txtIssues);

    // Themed progress card with built-in ETA display
    m_progress = new an::ui::AnProgressCard(tr("Import Progress"), this);
    m_progress->setProgressRange(0, m_totalFiles);
    root->addWidget(m_progress);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);

    m_btnPause = new QPushButton(tr("Pause"), this);
    QObject::connect(m_btnPause, &QPushButton::clicked, this, &ImportProgressDialog::onTogglePause);
    buttons->addWidget(m_btnPause);

    m_btnCancel = new QPushButton(tr("Cancel"), this);
    QObject::connect(m_btnCancel, &QPushButton::clicked, this, &ImportProgressDialog::onCancel);
    buttons->addWidget(m_btnCancel);

    root->addLayout(buttons);
}

void ImportProgressDialog::setProgress(int completed)
{
    if (m_progress)
    {
        m_progress->setProgressValue(std::clamp(completed, 0, m_totalFiles));
        if (completed >= m_totalFiles)
        {
            accept();
        }
    }
}

void ImportProgressDialog::setCurrentFile(const QString& fileName)
{
    if (m_lblCurrent)
    {
        m_lblCurrent->setText(tr("Processing: %1").arg(fileName));
    }
}

void ImportProgressDialog::setEtaMs(long long remainingMs)
{
    if (!m_progress)
    {
        return;
    }

    if (remainingMs <= 0)
    {
        m_progress->setEtaText(QString());
        return;
    }

    const int remainingSeconds = static_cast<int>(remainingMs / 1000);
    m_progress->setEtaText(tr("ETA %1").arg(WorkflowTimeEstimator::formatDuration(remainingSeconds)));
}

void ImportProgressDialog::setIssueCount(int count)
{
    if (!m_lblIssues)
    {
        return;
    }

    if (count <= 0)
    {
        m_lblIssues->clear();
        if (m_txtIssues)
        {
            m_txtIssues->clear();
            m_txtIssues->setVisible(false);
        }
        return;
    }

    m_lblIssues->setText(tr("Issues detected: %1").arg(count));
    if (m_txtIssues)
    {
        m_txtIssues->setVisible(true);
    }
}

void ImportProgressDialog::setIssueDetails(const QStringList& lines)
{
    if (!m_txtIssues)
    {
        return;
    }

    if (lines.isEmpty())
    {
        m_txtIssues->clear();
        m_txtIssues->setVisible(false);
        return;
    }

    m_txtIssues->setVisible(true);
    m_txtIssues->setPlainText(lines.join(QStringLiteral("\n")));
}

QPushButton* ImportProgressDialog::pauseButton() const
{
    return m_btnPause;
}

QPushButton* ImportProgressDialog::cancelButton() const
{
    return m_btnCancel;
}

QLabel* ImportProgressDialog::currentLabel() const
{
    return m_lblCurrent;
}

QLabel* ImportProgressDialog::issuesLabel() const
{
    return m_lblIssues;
}

QTextEdit* ImportProgressDialog::issuesTextEdit() const
{
    return m_txtIssues;
}

bool ImportProgressDialog::isCancelled() const
{
    return m_cancelled;
}

bool ImportProgressDialog::isPaused() const
{
    return m_paused;
}

void ImportProgressDialog::onCancel()
{
    m_cancelled = true;
    m_btnCancel->setEnabled(false);
    if (m_lblCurrent)
    {
        m_lblCurrent->setText(tr("Cancelling..."));
    }
    reject();
}

void ImportProgressDialog::reject()
{
    m_cancelled = true;
    QDialog::reject();
}

void ImportProgressDialog::closeEvent(QCloseEvent* event)
{
    m_cancelled = true;
    QDialog::closeEvent(event);
}

void ImportProgressDialog::onTogglePause()
{
    m_paused = !m_paused;
    if (m_btnPause)
    {
        m_btnPause->setText(m_paused ? tr("Resume") : tr("Pause"));
    }
}
} // namespace ImageIntegration
