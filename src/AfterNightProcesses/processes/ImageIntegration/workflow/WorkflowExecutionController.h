// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QWidget;
class IntegrationDataModel;

/**
 * @brief Focused controller for launching workflow executions from the UI.
 *
 * Encapsulates manifest validation + async execution orchestration, plus
 * registration-only manifest construction used by ImageIntegrationProcess.
 *
 * Threading contract:
 * Execution consumes a WorkflowManifest snapshot; background stages must not
 * read IntegrationDataModel directly. Callers that use prepared execution must
 * build the manifest from an immutable snapshot rather than touching UI-owned
 * model state from the preparation worker.
 *
 * Where the run is shown:
 * A run is followed in its view (`widgets/WorkflowRunView.h`), which the widget the run is launched from is asked to
 * show in itself (`IWorkflowRunHost`, `workflow/WorkflowRunObserver.h`). A widget that is no run host, or that refuses
 * the view, gets no run: the refusal is logged, the run is not started and the launch answers false, so that no run
 * goes on unseen. The process window the run is launched from says the run's standing as its run state: the step it is
 * at among its stages and how far the whole has come.
 */
class AFTERNIGHTPROCESSES_API WorkflowExecutionController
{
public:
    using CompletionCallback = std::function<void(const WorkflowExecutor::Result&)>;
    using ManifestPreparationCallback = std::function<WorkflowManifest()>;
    /// What makes the executor of a run with its stages: the application's when none is given.
    using Pipeline = std::function<std::unique_ptr<WorkflowExecutor>()>;

    WorkflowExecutionController(IntegrationDataModel* model, QWidget* parent);

    /// Runs go through the stages `pipeline` makes in place of the application's - a suite's own - and a manifest
    /// given to `runAsyncPrevalidated` is then run as it is, with no preparation of its own.
    void setPipeline(Pipeline pipeline);

    [[nodiscard]] bool
    runAsyncValidated(const WorkflowManifest& manifest, CompletionCallback onFinished = {},
                      WorkflowPreparationTiming preparationTiming = {},
                      std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot = {}) const;
    [[nodiscard]] bool
    runAsyncPrevalidated(const WorkflowManifest& manifest, CompletionCallback onFinished = {},
                         WorkflowPreparationTiming preparationTiming = {},
                         std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot = {}) const;
    [[nodiscard]] bool runAsyncPrepared(ManifestPreparationCallback prepareManifest,
                                        CompletionCallback onFinished = {}) const;
    static void normalizeManifestForExecution(WorkflowManifest& manifest,
                                              std::optional<std::uint64_t> availablePhysicalBytes = std::nullopt);
    [[nodiscard]] bool buildRegistrationOnlyManifestForGroup(const QString& registrationGroupKey,
                                                             bool reprocessExisting,
                                                             WorkflowManifest& outManifest) const;
    [[nodiscard]] WorkflowManifest buildRegistrationOnlyManifestAll(bool reprocessExisting) const;

private:
    [[nodiscard]] bool validateManifestAndWarn(const WorkflowManifest& manifest) const;
    static void filterRegistrationOnlyStageSequence(WorkflowManifest& manifest);

    IntegrationDataModel* m_model = nullptr;
    QWidget* m_parent = nullptr;
    Pipeline m_pipeline;
};
