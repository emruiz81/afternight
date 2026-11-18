// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What an import of frames says while it reads them: the file on its way, how many are read of how many and how long
/// the rest may take, the issues it met, and Pause, Resume and Cancel.

#include "AfterNightProcessesExport.h"

#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;

namespace an::ui
{
class AnProgressCard;
} // namespace an::ui

namespace ImageIntegration
{

/// The progress of an import, as a part a window shows in a dialog of its own (`ImportProgressDialog`) and a page
/// shows in itself, so that the rest of the application stays usable while frames are read.
///
/// It shows what it is told (`setProgress`, `setCurrentFile`, `setEtaMs`, `setIssueCount`, `setIssueDetails`) and
/// decides nothing: whoever drives the import hears its buttons. `Pause` becomes `Resume` and back at each press
/// (`isPaused`); `Cancel` is pressed once - by the person, or for them (`cancel`) - after which the panel says it is
/// cancelling and the button cannot be pressed again (`isCancelled`). Used on the GUI thread.
class AFTERNIGHTPROCESSES_API ImportProgressPanel : public QWidget
{
public:
    /// A panel for an import of `totalFiles` files.
    explicit ImportProgressPanel(int totalFiles, QWidget* parent = nullptr);

    /// How many files are read: never more than there are.
    void setProgress(int completed);
    [[nodiscard]] int completedFiles() const noexcept;
    [[nodiscard]] int totalFiles() const noexcept;

    void setCurrentFile(const QString& fileName);
    void setEtaMs(long long remainingMs);
    void setIssueCount(int count);
    void setIssueDetails(const QStringList& lines);

    [[nodiscard]] QPushButton* pauseButton() const;
    [[nodiscard]] QPushButton* cancelButton() const;
    [[nodiscard]] QLabel* currentLabel() const;
    [[nodiscard]] QLabel* issuesLabel() const;
    [[nodiscard]] QTextEdit* issuesTextEdit() const;

    [[nodiscard]] bool isCancelled() const noexcept;
    [[nodiscard]] bool isPaused() const noexcept;

    /// Cancels the import as a press of Cancel does: whoever hears the button hears it. Nothing once cancelled.
    void cancel();

    /// Marks the import cancelled without a press: what holds the panel went away under it.
    void markCancelled() noexcept;

private:
    void onCancel();
    void onTogglePause();

    int m_totalFiles = 0;
    int m_completed = 0;

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
