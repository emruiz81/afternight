// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ProgressReporter.h
 * @brief Progress reporting infrastructure for workflow execution.
 *
 * Defines WorkflowStage enum, WorkflowProgress struct, and ProgressReporter
 * class for communicating execution progress from background threads to UI.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/WorkflowLogger.h"

#include <QElapsedTimer>
#include <QString>

#include <chrono>
#include <functional>
#include <optional>

/**
 * @enum WorkflowStage
 * @brief Identifies the current stage of workflow execution.
 */
enum class WorkflowStage
{
    None,
    Bias,
    Dark,
    Flat,
    LightsCalibration,
    LightsRegistration,
    LightsAlignment,
    LightsNormalization,
    LightsDrizzle,
    LightsStacking
};

struct WorkflowProgress
{
    WorkflowStage currentStage = WorkflowStage::None;
    int currentGroup = 0;
    int totalGroups = 0;
    float stageProgress = 0.0f;
    float overallProgress = 0.0f;
    QString currentOperation;
    QString currentFile;

    /// Sub-step tracking for multi-phase operations within a single group.
    /// When totalSubSteps > 0, the dialog uses sub-step position to avoid
    /// visual progress bar resets when the engine restarts per internal phase.
    int currentSubStep = 0;
    int totalSubSteps = 0;
    QString subStepName;
};

/// Throttles progress updates to prevent UI overload.
class AFTERNIGHTPROCESSES_API ProgressReporter
{
public:
    using Callback = std::function<void(const WorkflowProgress&)>;
    using LogCallback = std::function<void(LogCategory, const QString&)>;

    explicit ProgressReporter(Callback callback = {},
                              std::chrono::milliseconds minimum_interval = std::chrono::milliseconds(50));

    void setCallback(Callback callback);
    void setLogCallback(LogCallback callback);
    void setMinimumInterval(std::chrono::milliseconds minimum_interval);
    void reset();

    void report(const WorkflowProgress& progress);
    void log(LogCategory category, const QString& message);

    // Accessor for the internal logger helper
    WorkflowLogger& logger();

private:
    Callback m_callback;
    LogCallback m_logCallback;
    std::chrono::milliseconds m_minimumInterval;
    QElapsedTimer m_timer;
    bool m_timerStarted = false;
    std::optional<WorkflowProgress> m_lastProgress;

    // Internal helper to format messages before sending to callback
    std::unique_ptr<WorkflowLogger> m_loggerHelper;
};
