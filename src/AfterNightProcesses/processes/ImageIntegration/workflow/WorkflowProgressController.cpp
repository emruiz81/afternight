// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowProgressController.h"

#include "processes/ImageIntegration/workflow/DefaultWorkflowStages.h"

#include <AfterNightPlatform/Logging.h>

#include <QMetaObject>
#include <QTimer>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(WorkflowProgressController)

WorkflowProgressController::WorkflowProgressController(IntegrationDataModel* model,
                                                       const an::consumer::EngineSession* session, QObject* parent,
                                                       ExecutorFactory executorFactory)
    : QObject(parent), m_model(model), m_session(session),
      m_executorFactory(executorFactory ? std::move(executorFactory)
                                        : ExecutorFactory(&ImageIntegration::createDefaultWorkflowExecutor))
{
}

WorkflowProgressController::~WorkflowProgressController()
{
    if (m_thread && m_thread->isRunning())
    {
        cancel();
        m_thread->quit();
        m_thread->wait();
    }
}

void WorkflowProgressController::start(const WorkflowManifest& manifest, WorkflowPreparationTiming preparationTiming,
                                       std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot)
{
    if (isRunning())
    {
        return;
    }

    auto* thread = new QThread(this);
    std::unique_ptr<WorkflowExecutor> ownedExecutor =
        m_executorFactory ? m_executorFactory() : ImageIntegration::createDefaultWorkflowExecutor();
    if (!ownedExecutor)
    {
        WorkflowExecutor::Result result;
        result.success = false;
        result.errorMessage = QStringLiteral("Failed to create workflow executor.");
        emit finished(result);
        thread->deleteLater();
        return;
    }

    auto* executor = ownedExecutor.release(); // parentless; will live in worker thread

    m_thread = thread;
    const auto gate = std::make_shared<CancelGate>();
    gate->executor = executor;
    m_cancelGate = gate;

    executor->moveToThread(thread);

    // Connect signals (QueuedConnection is automatic across threads)
    connect(executor, &WorkflowExecutor::progressUpdated, this, &WorkflowProgressController::progressUpdated,
            Qt::QueuedConnection);
    connect(executor, &WorkflowExecutor::logMessage, this, &WorkflowProgressController::logMessage,
            Qt::QueuedConnection);

    // Run execution (lambda runs in the worker thread because the context object is the executor).
    connect(thread, &QThread::started, executor,
            [this, manifest, preparationTiming, preparationSnapshot = std::move(preparationSnapshot), thread, executor,
             gate]() mutable {
                WorkflowExecutionOptions options;
                options.collectCounters = true;
                options.preparationSnapshot = std::move(preparationSnapshot);
                options.session = m_session;
                const WorkflowExecutor::Result result = executor->execute(manifest, preparationTiming, options);

                // The run is over: nothing is left to cancel, and the executor is about to be deleted on this thread,
                // so a cancel from the UI thread must not find it from here on.
                {
                    const std::lock_guard lock(gate->mutex);
                    gate->executor = nullptr;
                }

                // Ensure the UI sees the finished signal from the controller's thread.
                QMetaObject::invokeMethod(this, [this, result]() { emit finished(result); }, Qt::QueuedConnection);

                executor->deleteLater();
                QMetaObject::invokeMethod(thread, &QThread::quit, Qt::QueuedConnection);
            });

    // Cleanup
    connect(thread, &QThread::finished, this, &WorkflowProgressController::onExecutorFinished, Qt::QueuedConnection);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // Start failure handling (rare): if the worker thread never emits started and isn't running
    // after a short grace period, clean up and notify UI.
    auto started_flag = std::make_shared<std::atomic_bool>(false);
    connect(
        thread, &QThread::started, this, [started_flag]() { started_flag->store(true, std::memory_order_relaxed); },
        Qt::DirectConnection);

    thread->start();

    QPointer<QThread> thread_guard(thread);
    QPointer<WorkflowExecutor> executor_guard(executor);
    QTimer::singleShot(1000, this, [this, thread_guard, executor_guard, started_flag, gate]() {
        if (!thread_guard)
        {
            return;
        }

        if (started_flag->load(std::memory_order_relaxed) || thread_guard->isRunning())
        {
            return;
        }

        LOG_ERROR("WorkflowProgressController: worker thread failed to start");

        WorkflowExecutor::Result result;
        result.success = false;
        result.cancelled = false;
        result.errorMessage = QStringLiteral("Failed to start worker thread.");
        emit finished(result);

        {
            const std::lock_guard lock(gate->mutex);
            gate->executor = nullptr;
        }
        if (WorkflowExecutor* ex = executor_guard.data())
        {
            ex->moveToThread(QThread::currentThread());
            delete ex;
        }

        thread_guard->deleteLater();
        m_cancelGate.reset();
        m_thread = nullptr;
    });
}

void WorkflowProgressController::cancel()
{
    if (const std::shared_ptr<CancelGate> gate = m_cancelGate)
    {
        // Cancellation needs to be immediate even if the worker thread is busy, so the executor is asked from here
        // while the gate's lock holds it: the worker takes the same lock to let it go before the run is handed back.
        const std::lock_guard lock(gate->mutex);
        if (gate->executor != nullptr)
        {
            gate->executor->requestCancel();
        }
    }
}

bool WorkflowProgressController::isRunning() const
{
    return m_thread && m_thread->isRunning();
}

void WorkflowProgressController::onExecutorFinished()
{
    m_cancelGate.reset();
    m_thread = nullptr;
}
