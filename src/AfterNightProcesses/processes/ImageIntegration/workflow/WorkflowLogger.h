// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QString>
#include <functional>

enum class LogCategory
{
    Header,
    Progress,
    Detail,
    Success,
    Warning,
    Error
};

class AFTERNIGHTPROCESSES_API WorkflowLogger
{
public:
    using LogCallback = std::function<void(LogCategory, const QString&)>;

    explicit WorkflowLogger(LogCallback callback);

    // Workflow-level logging
    void workflowStarted(const QString& outputDirectory, int totalStages);
    void workflowCompleted(qint64 elapsedMs, int warningCount = 0);
    void workflowCancelled();

    // Stage-level logging
    void stageStarted(const QString& stageName, int totalGroups);
    void stageCompleted(const QString& stageName, int processedGroups, qint64 elapsedMs);
    void stageSkipped(const QString& stageName, const QString& reason);
    void stageFailed(const QString& stageName, const QString& error);

    // Group-level logging
    void groupStarted(int index, int total, const QString& groupName);
    void groupCompleted(int index, int total, const QString& groupName, qint64 elapsedMs);
    void groupSkipped(int index, int total, const QString& groupName, const QString& reason);

    // Phase-level logging. A phase is a long-running sub-step inside a single group (star
    // detection, transform computation) that would otherwise run for minutes without emitting
    // a single line, leaving the log with no trace of where the time went.
    void phaseStarted(int index, int total, const QString& name, const QString& detail = QString());
    void phaseDetail(const QString& detail);
    void phaseProgress(const QString& verb, int processed, int total, const QString& detail = QString());
    void phaseCompleted(int index, int total, const QString& name, qint64 elapsedMs,
                        const QString& summary = QString());

    // Frame-level logging (for detailed progress)
    void frameProgress(int current, int total, const QString& filename);
    void frameBatchProgress(int processed, int total, int batchReportInterval = 10, const QString& verb = QString());

    // Operation details
    void operationDetail(const QString& operation, const QString& detail = QString());
    void artifactCreated(const QString& type, const QString& path);
    void settingsApplied(const QString& setting, const QString& value);

    // Warnings and errors
    void warning(const QString& message);
    void error(const QString& message, const QString& detail = QString());

    // Formatting helpers
    static QString formatDuration(qint64 ms);
    static QString formatSize(qint64 bytes);
    static QString shortenPath(const QString& path, int maxChars = 60);

private:
    LogCallback m_callback;
};
