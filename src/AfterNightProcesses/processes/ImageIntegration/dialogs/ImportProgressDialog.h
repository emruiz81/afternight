// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <QStringList>
#include "components/AnProgressCard.h"

class QCloseEvent;
class QLabel;
class QPushButton;
class QTextEdit;

namespace ImageIntegration
{
class AFTERNIGHTPROCESSES_API ImportProgressDialog : public QDialog
{

public:
    explicit ImportProgressDialog(int totalFiles, QWidget* parent = nullptr);

    void setProgress(int completed);
    void setCurrentFile(const QString& fileName);
    void setEtaMs(long long remainingMs);
    void setIssueCount(int count);
    void setIssueDetails(const QStringList& lines);

    QPushButton* pauseButton() const;
    QPushButton* cancelButton() const;

    // For tests
    QLabel* currentLabel() const;
    QLabel* issuesLabel() const;
    QTextEdit* issuesTextEdit() const;

    bool isCancelled() const;
    bool isPaused() const;

public slots:
    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void onCancel();
    void onTogglePause();

    int m_totalFiles = 0;

    an::ui::AnProgressCard* m_progress = nullptr;
    QLabel* m_lblCurrent = nullptr;
    QLabel* m_lblIssues = nullptr;
    QTextEdit* m_txtIssues = nullptr;
    QPushButton* m_btnPause = nullptr;
    QPushButton* m_btnCancel = nullptr;

    bool m_cancelled = false;
    bool m_paused = false;
};
} // namespace ImageIntegration
