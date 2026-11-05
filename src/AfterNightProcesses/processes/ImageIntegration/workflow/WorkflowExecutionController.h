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
 */
class AFTERNIGHTPROCESSES_API WorkflowExecutionController
{
public:
    using CompletionCallback = std::function<void(const WorkflowExecutor::Result&)>;
    using ManifestPreparationCallback = std::function<WorkflowManifest()>;

    WorkflowExecutionController(IntegrationDataModel* model, QWidget* parent);

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
};
