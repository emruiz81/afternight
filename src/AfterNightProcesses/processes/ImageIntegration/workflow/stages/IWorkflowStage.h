// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file IWorkflowStage.h
 * @brief Interface for workflow stage implementations.
 *
 * IWorkflowStage defines the contract for all workflow stages executed by
 * WorkflowExecutor. Each stage performs a specific processing step (e.g.,
 * bias stacking, registration, light integration) and reports progress.
 *
 * @par Stage Lifecycle
 * 1. WorkflowExecutor calls shouldSkip() to check if stage is needed
 * 2. If not skipped, execute() is called with manifest and cancellation token
 * 3. Stage reports progress via ProgressReporter
 * 4. Stage returns StageResult indicating success, failure, or cancellation
 *
 * @see WorkflowExecutor, StageResult, WorkflowManifest
 */

#include "processes/ImageIntegration/workflow/CancellationToken.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "StageResult.h"

#include <QString>

#include <memory>

class ExecutionPreparationSnapshot;
class WorkflowImageCache;
class WorkflowArtifactPublicationLedger;

namespace ImageIntegration
{
class WorkflowStageServices;
} // namespace ImageIntegration

/**
 * @brief Interface for workflow stages executed by WorkflowExecutor.
 *
 * Implementations include:
 * - BiasStackingStage, DarkStackingStage, FlatStackingStage
 * - LightsCalibrationStage
 * - LightsRegistrationStage
 * - LightsNormalizationStage
 * - LightsDrizzleStage
 * - LightsStackingStage
 */
class AFTERNIGHTPROCESSES_API IWorkflowStage
{
public:
    virtual ~IWorkflowStage() = default;

    /** @brief Return the human-readable stage name. */
    [[nodiscard]] virtual QString name() const = 0;

    /** @brief Return the stage identifier enum value. */
    [[nodiscard]] virtual WorkflowStage stageId() const = 0;

    /**
     * @brief Check if this stage should be skipped.
     * @param manifest The workflow manifest with configuration.
     * @return true if the stage has no work to do.
     */
    [[nodiscard]] virtual bool shouldSkip(const WorkflowManifest& manifest) const
    {
        (void)manifest;
        return false;
    }

    /**
     * @brief Provide a human-friendly reason when the stage is skipped.
     * @param manifest The workflow manifest with configuration.
     * @return a short reason string for logging/UI.
     */
    [[nodiscard]] virtual QString skipReason(const WorkflowManifest& manifest) const
    {
        (void)manifest;
        return QStringLiteral("No work required or disabled.");
    }

    /**
     * @brief Bind optional diagnostics for the duration of one executor call.
     */
    void setExecutionCounterAccumulator(WorkflowExecutionCounterAccumulator* counters) noexcept
    {
        m_executionCounterAccumulator = counters;
    }

    void setExecutionPreparationSnapshot(std::shared_ptr<const ExecutionPreparationSnapshot> snapshot) noexcept
    {
        m_executionPreparationSnapshot = std::move(snapshot);
    }

    void setWorkflowImageCache(std::shared_ptr<WorkflowImageCache> cache) noexcept
    {
        m_workflowImageCache = std::move(cache);
    }

    void setArtifactPublicationLedger(std::shared_ptr<WorkflowArtifactPublicationLedger> ledger) noexcept
    {
        m_artifactPublicationLedger = std::move(ledger);
    }

    /**
     * @brief Lend the run's session, width, RAW treatment and stop for the duration of one executor call.
     */
    void setStageServices(const ImageIntegration::WorkflowStageServices* services) noexcept
    {
        m_stageServices = services;
    }

    /**
     * @brief Execute the stage processing.
     * @param manifest Workflow configuration and file lists.
     * @param token Cancellation token for cooperative cancellation.
     * @param reporter Progress reporting callback.
     * @return StageResult indicating success, failure, or cancellation.
     */
    virtual StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                                ProgressReporter& reporter) = 0;

protected:
    [[nodiscard]] WorkflowExecutionCounterAccumulator* executionCounterAccumulator() const noexcept
    {
        return m_executionCounterAccumulator;
    }

    [[nodiscard]] const ExecutionPreparationSnapshot* executionPreparationSnapshot() const noexcept
    {
        return m_executionPreparationSnapshot.get();
    }

    [[nodiscard]] const std::shared_ptr<WorkflowImageCache>& workflowImageCache() const noexcept
    {
        return m_workflowImageCache;
    }

    [[nodiscard]] const std::shared_ptr<WorkflowArtifactPublicationLedger>& artifactPublicationLedger() const noexcept
    {
        return m_artifactPublicationLedger;
    }

    /// The run's services; null outside an executor call.
    [[nodiscard]] const ImageIntegration::WorkflowStageServices* stageServices() const noexcept
    {
        return m_stageServices;
    }

private:
    WorkflowExecutionCounterAccumulator* m_executionCounterAccumulator = nullptr;
    std::shared_ptr<const ExecutionPreparationSnapshot> m_executionPreparationSnapshot;
    std::shared_ptr<WorkflowImageCache> m_workflowImageCache;
    std::shared_ptr<WorkflowArtifactPublicationLedger> m_artifactPublicationLedger;
    const ImageIntegration::WorkflowStageServices* m_stageServices = nullptr;
};
