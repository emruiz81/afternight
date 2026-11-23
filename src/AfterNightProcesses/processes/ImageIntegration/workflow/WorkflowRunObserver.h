// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the widget a workflow is launched from is told of the run as it goes, and asked as it starts.

struct WorkflowProgress;
class WorkflowRunView;

/// A front end that shows where a run stands beside the run's own view: a list of the job's stages, a row of jobs.
/// The widget a workflow is launched from implements it, and the launch (`WorkflowExecutionController`) tells it each
/// progress the run reports, on the GUI thread, for as long as the widget lives. A front end that implements none is
/// told nothing; how the run ended is told to every front end the same way, by the launch's completion.
class IWorkflowRunObserver
{
public:
    /// The run moved on: the stage it is at and how far the stage and the whole have come.
    virtual void workflowRunProgressed(const WorkflowProgress& progress) = 0;

protected:
    ~IWorkflowRunObserver() = default;
};

/// A front end that shows a run in itself: the process a workflow is launched from. The widget a workflow is launched
/// from implements it, and the launch asks it once, as the run starts, with the run's view
/// (`widgets/WorkflowRunView.h`); a launch from a widget that does not starts no run.
class IWorkflowRunHost
{
public:
    /// Takes the view of a run that starts and shows it in the front end, which owns it from then on - for as long as
    /// the run is on its way at the least - or answers false and leaves the view alone: the run is then not started.
    ///
    /// @return whether the front end took the view.
    virtual bool showWorkflowRun(WorkflowRunView* view) = 0;

protected:
    ~IWorkflowRunHost() = default;
};
