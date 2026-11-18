// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"

#include <memory>

class QCloseEvent;
class WorkflowProgressController;
class WorkflowRunView;

/**
 * @class WorkflowProgressDialogV2
 * @brief The Processing window: a run of the workflow shown over the application.
 *
 * It holds the run's view (`widgets/WorkflowRunView.h`) - the run's stages, what it does, its log, its times, Cancel,
 * and Open Report and Close once it has ended - and adds what a window of its own adds: it stands over the
 * application until the run has ended, closing it during a run asks the run to stop and leaves it open, and the run's
 * end is said in a message over it. A front end that shows a run in itself holds the same view and opens no window.
 */
class AFTERNIGHTPROCESSES_API WorkflowProgressDialogV2 : public QDialog
{
    W_OBJECT(WorkflowProgressDialogV2)

public:
    explicit WorkflowProgressDialogV2(WorkflowProgressController* controller, QWidget* parent = nullptr);
    ~WorkflowProgressDialogV2() override;

    /// The run's view the window holds.
    [[nodiscard]] WorkflowRunView* runView() const noexcept;

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
    /// Says how the run ended in a message over the window.
    void announceEnd(const WorkflowExecutor::Result& result);

    void reject() override;
    void closeEvent(QCloseEvent* event) override;

    WorkflowRunView* m_view = nullptr;
};
