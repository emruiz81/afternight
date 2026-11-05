// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the executor commits a run through once its stages end: the run's outputs and history, its report, its timing
/// and its log.
///
/// The executor decides when a run is committed, reverted or partially committed; what committing does - copying the
/// outputs a run displaced into its history, rendering its report, writing its timing and its log - is the archive's.
/// The executor reaches it through this interface, built for each run by the factory its composer gives it, so the
/// executor runs and is tested without the report builder and the thumbnails the archive depends on.

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QString>

#include <functional>

/// One run's archive.
class IWorkflowRunArchive
{
public:
    /// What a commit did.
    struct CommitOutcome
    {
        bool success = false;
        /// The report the commit wrote; empty when it wrote none. The executor offers the person the report's HTML
        /// rendering, `workflow_report.html` beside it, when that file exists.
        QString reportPath;
        QString error;
        qint64 archiveMs = 0;
        qint64 reportMs = 0;
    };

    /// The clock of the executor's exclusive phases: each call returns the milliseconds since the last.
    using CapturePhaseDuration = std::function<qint64()>;

    virtual ~IWorkflowRunArchive() = default;

    /// Commits the run's outputs, its history and its report.
    ///
    /// @param manifest the run's manifest.
    /// @param result the run's result, which the commit may complete.
    /// @param capturePhase the executor's phase clock.
    /// @return what the commit did.
    virtual CommitOutcome commit(const WorkflowManifest& manifest, WorkflowExecutor::Result& result,
                                 const CapturePhaseDuration& capturePhase) = 0;

    /// Puts back every output a failed or cancelled run displaced.
    virtual void revertStagedFiles(const WorkflowExecutor::Result& result) = 0;

    /// Writes the run's final timing beside its committed history.
    virtual void finalizeTimingArtifacts(const WorkflowExecutor::Result& result) = 0;

    /// Writes the run's final log beside its committed history.
    virtual void finalizeLogArtifact(const WorkflowExecutor::Result& result) = 0;
};
