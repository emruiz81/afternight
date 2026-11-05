// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"

class StageTimelineWidget;
class QProgressBar;
class QLabel;
class QTextEdit;
namespace an::ui
{
class AnCardWidget;
}
class QPushButton;
class WorkflowProgressController;
class QCloseEvent;

/**
 * @class WorkflowProgressDialogV2
 * @brief Redesigned progress dialog with single timeline view.
 */
class AFTERNIGHTPROCESSES_API WorkflowProgressDialogV2 : public QDialog
{
    W_OBJECT(WorkflowProgressDialogV2)

public:
    explicit WorkflowProgressDialogV2(WorkflowProgressController* controller, QWidget* parent = nullptr);
    ~WorkflowProgressDialogV2() override;

    // Setup
    void setStages(const QStringList& stages);

    // Progress updates (slots)
    void onProgressUpdated(const WorkflowProgress& progress);
    void onLogMessage(WorkflowLogLevel level, const QString& message);
    void onFinished(const WorkflowExecutor::Result& result);

    [[nodiscard]] bool wasSuccessful() const;
    [[nodiscard]] bool wasCancelled() const;
    [[nodiscard]] bool wasFailed() const;

signals:
    void cancelRequested() W_SIGNAL(cancelRequested);

private:
    void setupUi();
    void setupConnections();
    void applyTheme();
    void requestCancellation();
    void updateTimeDisplay();
    int stageIndexFor(WorkflowStage stage) const;
    QString formatDuration(qint64 ms) const;

    void reject() override;
    void closeEvent(QCloseEvent* event) override;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
