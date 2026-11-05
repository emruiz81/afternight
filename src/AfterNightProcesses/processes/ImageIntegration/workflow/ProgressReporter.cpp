// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProgressReporter.h"

ProgressReporter::ProgressReporter(Callback callback, std::chrono::milliseconds minimum_interval)
    : m_callback(std::move(callback)), m_minimumInterval(minimum_interval)
{
    m_loggerHelper = std::make_unique<WorkflowLogger>([this](LogCategory cat, const QString& msg) { log(cat, msg); });
}

void ProgressReporter::setCallback(Callback callback)
{
    m_callback = std::move(callback);
}

void ProgressReporter::setLogCallback(LogCallback callback)
{
    m_logCallback = std::move(callback);
}

void ProgressReporter::setMinimumInterval(std::chrono::milliseconds minimum_interval)
{
    m_minimumInterval = minimum_interval;
}

void ProgressReporter::reset()
{
    m_timer.invalidate();
    m_timerStarted = false;
    m_lastProgress.reset();
}

void ProgressReporter::report(const WorkflowProgress& progress)
{
    if (!m_callback)
    {
        return;
    }

    if (m_minimumInterval.count() <= 0)
    {
        m_callback(progress);
        return;
    }

    if (!m_timerStarted)
    {
        m_timer.start();
        m_timerStarted = true;
        m_callback(progress);
        m_lastProgress = progress;
        return;
    }

    if (m_lastProgress.has_value())
    {
        const WorkflowProgress& last = m_lastProgress.value();
        const bool stageChanged = progress.currentStage != last.currentStage;
        const bool groupChanged =
            progress.currentGroup != last.currentGroup || progress.totalGroups != last.totalGroups;
        const bool subStepChanged =
            progress.currentSubStep != last.currentSubStep || progress.totalSubSteps != last.totalSubSteps;
        const bool currentFileChanged = progress.currentFile != last.currentFile;
        const bool operationChanged = progress.currentOperation != last.currentOperation;
        const bool reachedCompletion = progress.stageProgress >= 0.999f && last.stageProgress < 0.999f;

        // These fields drive UI labels and must not get "stuck" due to throttling.
        // In particular, currentFile must be allowed to transition to empty so the UI
        // can clear any stale filename when a stage switches from file-based work to
        // group/compute work.
        if (stageChanged || groupChanged || subStepChanged || currentFileChanged || operationChanged ||
            reachedCompletion)
        {
            m_timer.restart();
            m_callback(progress);
            m_lastProgress = progress;
            return;
        }
    }

    if (m_timer.hasExpired(static_cast<int>(m_minimumInterval.count())))
    {
        m_timer.restart();
        m_callback(progress);
        m_lastProgress = progress;
    }
}

void ProgressReporter::log(LogCategory category, const QString& message)
{
    if (m_logCallback)
    {
        m_logCallback(category, message);
    }
}

WorkflowLogger& ProgressReporter::logger()
{
    return *m_loggerHelper;
}
