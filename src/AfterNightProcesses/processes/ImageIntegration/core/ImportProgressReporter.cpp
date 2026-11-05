// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImportProgressReporter.h"

#include <QFileInfo>

#include <algorithm>

namespace ImageIntegration
{
void ImportProgressReporter::setTotalFiles(int total)
{
    std::scoped_lock lock(m_mutex);
    m_totalFiles = (std::max)(0, total);
    m_completedFiles = 0;
    m_currentFile.clear();
    m_errors.clear();
    m_estimatedRemainingMs.store(0);
    m_paused.store(false);
}

void ImportProgressReporter::onFileStarted(const QString& path)
{
    const QString fileName = QFileInfo(path).fileName();
    std::scoped_lock lock(m_mutex);
    m_currentFile = fileName.isEmpty() ? path : fileName;
}

void ImportProgressReporter::onFilesCompleted(int completed, long long elapsedMs)
{
    if (elapsedMs <= 0)
    {
        m_estimatedRemainingMs.store(0);
    }

    const long long clampedElapsed = (std::max)(0LL, elapsedMs);

    std::scoped_lock lock(m_mutex);
    m_completedFiles = (std::max)(0, completed);

    if (m_totalFiles <= 0 || m_completedFiles <= 0)
    {
        m_estimatedRemainingMs.store(0);
        return;
    }

    const int remaining = (std::max)(0, m_totalFiles - m_completedFiles);

    const double msPerFile = static_cast<double>(clampedElapsed) / static_cast<double>(m_completedFiles);
    const long long estimate = static_cast<long long>(msPerFile * static_cast<double>(remaining));
    m_estimatedRemainingMs.store((std::max)(0LL, estimate));
}

void ImportProgressReporter::onMetadataError(const QString& path, const QString& message)
{
    std::scoped_lock lock(m_mutex);
    m_errors.push_back(Error{path, message});
}

QString ImportProgressReporter::currentFile() const
{
    std::scoped_lock lock(m_mutex);
    return m_currentFile;
}

long long ImportProgressReporter::estimatedRemainingMs() const
{
    return m_estimatedRemainingMs.load();
}

std::vector<ImportProgressReporter::Error> ImportProgressReporter::errors() const
{
    std::scoped_lock lock(m_mutex);
    return m_errors;
}

bool ImportProgressReporter::hasWarnings() const
{
    std::scoped_lock lock(m_mutex);
    return !m_errors.empty();
}

int ImportProgressReporter::totalFiles() const
{
    std::scoped_lock lock(m_mutex);
    return m_totalFiles;
}

int ImportProgressReporter::completedFiles() const
{
    std::scoped_lock lock(m_mutex);
    return m_completedFiles;
}

void ImportProgressReporter::pause()
{
    m_paused.store(true);
}

void ImportProgressReporter::resume()
{
    m_paused.store(false);
}

bool ImportProgressReporter::isPaused() const
{
    return m_paused.load();
}
} // namespace ImageIntegration
