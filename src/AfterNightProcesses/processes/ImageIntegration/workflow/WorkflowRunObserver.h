// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the widget a workflow is launched from is told of the run as it goes.

struct WorkflowProgress;

/// A front end that shows where a run stands beside the run's own window: a list of the job's stages, a row of jobs.
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
