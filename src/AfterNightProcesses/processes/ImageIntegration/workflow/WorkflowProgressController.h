// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file WorkflowProgressController.h
 * @brief Controller bridging UI thread and background workflow execution.
 *
 * Manages a background QThread running WorkflowExecutor, forwarding
 * progress signals to the UI thread for display in the workflow progress dialog.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QObject>
#include <QPointer>
#include <QThread>
#include <verdigris/wobjectdefs.h>

#include "WorkflowExecutor.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <functional>
#include <memory>
#include <mutex>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

/**
 * @class WorkflowProgressController
 * @brief Thread-safe controller for background workflow execution.
 *
 * Responsibilities:
 * - Starts WorkflowExecutor in a dedicated QThread
 * - Forwards progressUpdated(), logMessage(), finished() signals to UI
 * - Handles cancellation requests from UI
 * - Ensures proper thread cleanup on completion
 *
 * Threading contract:
 * - WorkflowManifest is the immutable execution snapshot passed to worker stages.
 * - Stages must not access IntegrationDataModel directly from the worker thread.
 * - IntegrationDataModel is retained only as UI-side context for controller wiring.
 *
 * Used by ImageIntegrationProcess to execute workflows without blocking UI.
 */
class AFTERNIGHTPROCESSES_API WorkflowProgressController : public QObject
{
    W_OBJECT(WorkflowProgressController)

public:
    using ExecutorFactory = std::function<std::unique_ptr<WorkflowExecutor>()>;

    /// A controller whose runs do their engine work in `session`, which outlives every run it starts. Without a
    /// factory the executor of every stage and the archiver is created.
    WorkflowProgressController(IntegrationDataModel* model, const an::consumer::EngineSession* session,
                               QObject* parent = nullptr, ExecutorFactory executorFactory = {});
    ~WorkflowProgressController() override;

    // Start execution in a background thread
    void start(const WorkflowManifest& manifest, WorkflowPreparationTiming preparationTiming = {},
               std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot = {});

    // Request cancellation
    void cancel();

    bool isRunning() const;

    // Signals to UI
    void progressUpdated(const WorkflowProgress& progress) W_SIGNAL(progressUpdated, progress);
    void logMessage(WorkflowLogLevel level, const QString& message) W_SIGNAL(logMessage, level, message);
    void finished(const WorkflowExecutor::Result& result) W_SIGNAL(finished, result);

private:
    /// The executor a cancel may reach, and the lock that keeps it alive while one does. The worker thread clears it
    /// before it hands the finished run back and deletes the executor, so a cancel arriving then finds nothing.
    struct CancelGate
    {
        std::mutex mutex;
        WorkflowExecutor* executor = nullptr;
    };

    void onExecutorFinished();

    IntegrationDataModel* m_model;
    const an::consumer::EngineSession* m_session = nullptr;
    ExecutorFactory m_executorFactory;
    QThread* m_thread = nullptr;
    std::shared_ptr<CancelGate> m_cancelGate;
};
