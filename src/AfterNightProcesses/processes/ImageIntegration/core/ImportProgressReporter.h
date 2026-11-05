// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QString>

#include <atomic>
#include <mutex>
#include <vector>

namespace ImageIntegration
{
class AFTERNIGHTPROCESSES_API ImportProgressReporter
{
public:
    struct Error
    {
        QString path;
        QString message;
    };

    void setTotalFiles(int total);

    void onFileStarted(const QString& path);

    // completed: number of files completed so far
    // elapsedMs: elapsed time since start (ms)
    void onFilesCompleted(int completed, long long elapsedMs);

    void onMetadataError(const QString& path, const QString& message);

    QString currentFile() const;
    long long estimatedRemainingMs() const;

    std::vector<Error> errors() const;
    bool hasWarnings() const;

    int totalFiles() const;
    int completedFiles() const;

    void pause();
    void resume();
    bool isPaused() const;

private:
    mutable std::mutex m_mutex;

    int m_totalFiles = 0;
    int m_completedFiles = 0;
    QString m_currentFile;

    std::atomic<long long> m_estimatedRemainingMs{0};
    std::vector<Error> m_errors;

    std::atomic<bool> m_paused{false};
};
} // namespace ImageIntegration
