// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <QStringList>

class QCloseEvent;
class QLabel;
class QPushButton;
class QTextEdit;

namespace ImageIntegration
{
class ImportProgressPanel;

/// The progress of an import in a dialog over the window that started it: the import's panel
/// (`widgets/ImportProgressPanel.h`), modal to that window. Rejecting or closing the dialog cancels the import, and it
/// closes itself once every file is read. A process shown as a page shows the panel in its page instead.
class AFTERNIGHTPROCESSES_API ImportProgressDialog : public QDialog
{

public:
    explicit ImportProgressDialog(int totalFiles, QWidget* parent = nullptr);

    /// The panel the dialog shows.
    [[nodiscard]] ImportProgressPanel* panel() const noexcept;

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
    ImportProgressPanel* m_panel = nullptr;
};
} // namespace ImageIntegration
