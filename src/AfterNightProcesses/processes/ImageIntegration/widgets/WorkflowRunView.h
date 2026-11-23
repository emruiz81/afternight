// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A run of the workflow as a person follows it, in the process it was launched from.

#include "AfterNightProcessesExport.h"

#include "components/AnStageList.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStanding.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <memory>
#include <optional>
#include <vector>

class WorkflowProgressController;

/// A run of the workflow, shown:
///
/// ```
/// +----------------+-------------------------------------------------+
/// | the run's      | what the run does now                           |
/// | stages, a list | the whole run's bar                             |
/// |                | the file it is at                               |
/// |                | the log                                         |
/// +----------------+-------------------------------------------------+
/// | elapsed - remaining                 Open Report   Cancel | Close |
/// +------------------------------------------------------------------+
/// ```
///
/// **The stages** are the ones the run goes through (`setStages`), a row each (`components/AnStageList.h`): a stage to
/// come, the stage the run is at - which carries how far it has come over its groups, the group it is at, and under
/// its line the parts of what it does to that group, as the run names them - and the stages it has passed, done. When
/// the run ends each stage says the time the run reports for it; a run that failed marks the stage it stopped at as
/// failed, one that was stopped as stopped.
///
/// **The bar** is the whole run's: the stages passed and the share of the one on its way, each stage weighing what
/// the run is expected to spend in it (`workflow/WorkflowRunStanding.h`). It never goes back within a stage. The same
/// standing is what a front end says elsewhere of the run - a task's state, a job's row - and is told when it moves
/// (`standingChanged`).
///
/// **The times** are the time the run has taken and, once it can tell, the time it has left, each in a line of its
/// own.
///
/// **Its acts.** Cancel asks the run to stop (`cancelRequested`, and the controller's cancel) and says so until it
/// has. Once the run has ended, Open Report opens the report it left, when it left one, and Close asks whatever holds
/// the view to put it away (`closeRequested`): the process shows what the view took the place of again.
///
/// It follows the controller it is given - its progress, its log, its end - and may be fed by hand, as a preparation
/// that ends before any run does. Used on the GUI thread.
class AFTERNIGHTPROCESSES_API WorkflowRunView : public QWidget
{
    W_OBJECT(WorkflowRunView)

public:
    /// @param controller the run's controller, whose progress, log and end the view follows; may be null for a view
    ///        fed by hand.
    explicit WorkflowRunView(WorkflowProgressController* controller, QWidget* parent = nullptr);
    ~WorkflowRunView() override;

    WorkflowRunView(const WorkflowRunView&) = delete;
    WorkflowRunView& operator=(const WorkflowRunView&) = delete;

    /// The stages the run goes through, by the names a manifest's stage sequence gives them, and what each weighs in
    /// the whole (`ImageIntegration::workflowStageWeights`); the stages weigh alike without weights.
    void setStages(const QStringList& stages, const std::vector<double>& weights = {});

    void onProgressUpdated(const WorkflowProgress& progress);
    void onLogMessage(WorkflowLogLevel level, const QString& message);
    void onFinished(const WorkflowExecutor::Result& result);

    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] bool wasSuccessful() const;
    [[nodiscard]] bool wasCancelled() const;
    [[nodiscard]] bool wasFailed() const;

    /// Where the run stands among its stages.
    [[nodiscard]] const ImageIntegration::WorkflowRunStanding& standing() const noexcept;

    /// The stages' rows, as the list shows them.
    [[nodiscard]] std::vector<an::ui::StageRow> stageRows() const;

    /// The report the run left; empty while it runs and for a run that left none.
    [[nodiscard]] QString reportPath() const;

    /// Asks the run to stop, as Cancel does; nothing once it has ended or was asked already.
    void requestCancellation();

    /// Says the time the run has left, or takes the line away for none: what the view does by itself, each second and
    /// at each report, from the time the run has taken and how far it has come.
    void showRemaining(std::optional<qint64> remainingMs);

    /// Draws the view again under the look in force.
    void refreshTheme();

signals:
    /// Cancel was asked for.
    void cancelRequested() W_SIGNAL(cancelRequested);

    /// Close was asked for, once the run had ended.
    void closeRequested() W_SIGNAL(closeRequested);

    /// The run's standing among its stages moved.
    void standingChanged() W_SIGNAL(standingChanged);

    /// The run ended, with its result.
    void runEnded(const WorkflowExecutor::Result& result) W_SIGNAL(runEnded, result);

private:
    void setupUi();
    void setupConnections();
    void applyTheme();
    void updateTimeDisplay();
    void publishStageRows();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
