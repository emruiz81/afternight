// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/widgets/ImportProgressPanel.h"

#include "components/AnProgressCard.h"
#include "processes/ImageIntegration/workflow/WorkflowTimeEstimator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace ImageIntegration
{

ImportProgressPanel::ImportProgressPanel(const int totalFiles, QWidget* parent)
    : QWidget(parent), m_totalFiles((std::max)(0, totalFiles))
{
    setObjectName(QStringLiteral("importProgressPanel"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
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
    m_btnPause->setObjectName(QStringLiteral("importPause"));
    QObject::connect(m_btnPause, &QPushButton::clicked, this, &ImportProgressPanel::onTogglePause);
    buttons->addWidget(m_btnPause);

    m_btnCancel = new QPushButton(tr("Cancel"), this);
    m_btnCancel->setObjectName(QStringLiteral("importCancel"));
    QObject::connect(m_btnCancel, &QPushButton::clicked, this, &ImportProgressPanel::onCancel);
    buttons->addWidget(m_btnCancel);

    root->addLayout(buttons);
}

void ImportProgressPanel::setProgress(const int completed)
{
    m_completed = std::clamp(completed, 0, m_totalFiles);
    if (m_progress)
    {
        m_progress->setProgressValue(m_completed);
    }
}

int ImportProgressPanel::completedFiles() const noexcept
{
    return m_completed;
}

int ImportProgressPanel::totalFiles() const noexcept
{
    return m_totalFiles;
}

void ImportProgressPanel::setCurrentFile(const QString& fileName)
{
    if (m_lblCurrent && !m_cancelled)
    {
        m_lblCurrent->setText(tr("Processing: %1").arg(fileName));
    }
}

void ImportProgressPanel::setEtaMs(const long long remainingMs)
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

void ImportProgressPanel::setIssueCount(const int count)
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

void ImportProgressPanel::setIssueDetails(const QStringList& lines)
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

QPushButton* ImportProgressPanel::pauseButton() const
{
    return m_btnPause;
}

QPushButton* ImportProgressPanel::cancelButton() const
{
    return m_btnCancel;
}

QLabel* ImportProgressPanel::currentLabel() const
{
    return m_lblCurrent;
}

QLabel* ImportProgressPanel::issuesLabel() const
{
    return m_lblIssues;
}

QTextEdit* ImportProgressPanel::issuesTextEdit() const
{
    return m_txtIssues;
}

bool ImportProgressPanel::isCancelled() const noexcept
{
    return m_cancelled;
}

bool ImportProgressPanel::isPaused() const noexcept
{
    return m_paused;
}

void ImportProgressPanel::cancel()
{
    if (!m_cancelled && m_btnCancel && m_btnCancel->isEnabled())
    {
        m_btnCancel->click();
    }
}

void ImportProgressPanel::markCancelled() noexcept
{
    m_cancelled = true;
}

void ImportProgressPanel::onCancel()
{
    m_cancelled = true;
    m_btnCancel->setEnabled(false);
    if (m_lblCurrent)
    {
        m_lblCurrent->setText(tr("Cancelling..."));
    }
}

void ImportProgressPanel::onTogglePause()
{
    m_paused = !m_paused;
    if (m_btnPause)
    {
        m_btnPause->setText(m_paused ? tr("Resume") : tr("Pause"));
    }
}

} // namespace ImageIntegration
