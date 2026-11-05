// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowLogger.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace
{

// Helper for formatDouble since it was missing
QString formatDouble(double value, int precision)
{
    return QString::number(value, 'f', precision);
}

// ============================================================================
// Visual Design System for Workflow Logging
// ============================================================================
//
// Goals:
//   1. Clear visual hierarchy: Stage > Group > Operation > Detail
//   2. Compact output that fits well in ~80 char width log panel
//   3. Clean Unicode symbols that render consistently across fonts
//   4. Informative but not noisy
//
// Stage headers use double-line borders for prominence
// Groups are bracketed with index counters [1/3]
// Operations use arrow (→) for in-progress, checkmark (✓) for success
// Indentation creates visual tree structure

// Unicode characters (UTF-16 safe for Windows)
const QString kStageSeparator = QString::fromWCharArray(L"\u2550\u2550\u2550"); // ═══
const QString kArrowRight = QString::fromWCharArray(L"\u2192"); // →
const QString kCheckmark = QString::fromWCharArray(L"\u2713"); // ✓
const QString kWarningSign = QString::fromWCharArray(L"\u26A0"); // ⚠
const QString kCrossmark = QString::fromWCharArray(L"\u2717"); // ✗
const QString kSkipSign = QString::fromWCharArray(L"\u2205"); // ∅ (empty set)
const QString kBullet = QString::fromWCharArray(L"\u2022"); // •
const QString kPhaseMarker = QString::fromWCharArray(L"\u25B8"); // ▸
const QString kEmDash = QString::fromWCharArray(L"\u2014"); // —

// Indentation levels for visual hierarchy
constexpr const char* kIndent1 = "  "; // Group level
constexpr const char* kIndent2 = "    "; // Operation level
constexpr const char* kIndent3 = "      "; // Detail level

} // namespace

WorkflowLogger::WorkflowLogger(LogCallback callback) : m_callback(std::move(callback)) {}

void WorkflowLogger::workflowStarted(const QString& outputDirectory, int totalStages)
{
    if (!m_callback)
        return;

    // Single clean header for the new console-style log panel.
    // The dialog's onLogMessage detects "WORKFLOW" and renders it prominently.
    m_callback(LogCategory::Header, QStringLiteral("WORKFLOW STARTED"));

    m_callback(LogCategory::Detail,
               QStringLiteral("%1%2 Output: %3").arg(kIndent1).arg(kBullet).arg(shortenPath(outputDirectory, 50)));
    m_callback(LogCategory::Detail, QStringLiteral("%1%2 Stages: %3").arg(kIndent1).arg(kBullet).arg(totalStages));
}

void WorkflowLogger::workflowCompleted(qint64 elapsedMs, int warningCount)
{
    if (!m_callback)
        return;

    m_callback(LogCategory::Success, QStringLiteral("WORKFLOW COMPLETED SUCCESSFULLY"));

    m_callback(LogCategory::Success,
               QStringLiteral("%1%2 Total time: %3").arg(kIndent1).arg(kCheckmark).arg(formatDuration(elapsedMs)));
    if (warningCount > 0)
    {
        m_callback(LogCategory::Warning, QStringLiteral("%1%2 %3 warning%4 occurred")
                                             .arg(kIndent1)
                                             .arg(kWarningSign)
                                             .arg(warningCount)
                                             .arg(warningCount == 1 ? QStringLiteral("") : QStringLiteral("s")));
    }
}

void WorkflowLogger::workflowCancelled()
{
    if (!m_callback)
        return;

    m_callback(LogCategory::Warning, QStringLiteral("WORKFLOW CANCELLED BY USER"));
}

namespace
{

// Format stage name: "LightsStacking" -> "Lights Stacking"
QString prettifyStageName(const QString& name)
{
    QString out;
    out.reserve(name.size() + 8);
    for (int i = 0; i < name.size(); ++i)
    {
        const QChar ch = name[i];
        if (i > 0 && ch.isUpper() && name[i - 1].isLower())
        {
            out.append(' ');
        }
        out.append(ch);
    }
    return out;
}

// Truncate group name intelligently for display
QString truncateGroupName(const QString& name, int maxLen = 40)
{
    if (name.length() <= maxLen)
        return name;

    // Try to find a good break point (underscore, hyphen)
    int breakPoint = name.lastIndexOf('_', maxLen - 3);
    if (breakPoint < maxLen / 2)
        breakPoint = name.lastIndexOf('-', maxLen - 3);
    if (breakPoint < maxLen / 2)
        breakPoint = maxLen - 3;

    return name.left(breakPoint) + QStringLiteral("...");
}

} // namespace

void WorkflowLogger::stageStarted(const QString& stageName, int totalGroups)
{
    if (!m_callback)
        return;
    const QString title = prettifyStageName(stageName);

    // Clean stage header with group count
    // Format: "═══ STAGE: Lights Stacking (3 groups) ═══"
    QString msg = QStringLiteral("\n%1 STAGE: %2 (%3 group%4) %1")
                      .arg(kStageSeparator)
                      .arg(title)
                      .arg(totalGroups)
                      .arg(totalGroups == 1 ? QStringLiteral("") : QStringLiteral("s"));
    m_callback(LogCategory::Header, msg);
}

void WorkflowLogger::stageCompleted(const QString& stageName, int processedGroups, qint64 elapsedMs)
{
    if (!m_callback)
        return;
    const QString title = prettifyStageName(stageName);

    // Format: "═══ Lights Stacking Complete ═══ (3 groups, 1m 23s)"
    QString msg = QStringLiteral("%1 %2 Complete %1 (%3 group%4, %5)")
                      .arg(kStageSeparator)
                      .arg(title)
                      .arg(processedGroups)
                      .arg(processedGroups == 1 ? QStringLiteral("") : QStringLiteral("s"))
                      .arg(formatDuration(elapsedMs));
    m_callback(LogCategory::Success, msg);
}

void WorkflowLogger::stageSkipped(const QString& stageName, const QString& reason)
{
    if (!m_callback)
        return;
    const QString title = prettifyStageName(stageName);

    // Format: "∅ Skipped: Bias Stacking — No frames configured"
    QString msg = QStringLiteral("%1 Skipped: %2").arg(kSkipSign, title);
    if (!reason.isEmpty())
    {
        msg += QStringLiteral(" — %1").arg(reason);
    }
    m_callback(LogCategory::Detail, msg);
}

void WorkflowLogger::stageFailed(const QString& stageName, const QString& error)
{
    if (!m_callback)
        return;
    const QString title = prettifyStageName(stageName);

    // Format: "✗ Lights Stacking FAILED: Out of memory"
    QString msg = QStringLiteral("%1 %2 FAILED: %3").arg(kCrossmark, title, error);
    m_callback(LogCategory::Error, msg);
}

void WorkflowLogger::groupStarted(int index, int total, const QString& groupName)
{
    if (!m_callback)
        return;

    // Format: "  [1/3] Processing: M42_2024_Filter-Ha..."
    const QString displayName = truncateGroupName(groupName);
    QString msg = QStringLiteral("%1[%2/%3] Processing: %4").arg(kIndent1).arg(index).arg(total).arg(displayName);
    m_callback(LogCategory::Header, msg);
}

void WorkflowLogger::groupCompleted(int index, int total, const QString& groupName, qint64 elapsedMs)
{
    if (!m_callback)
        return;
    Q_UNUSED(groupName);

    // Format: "    ✓ Group 1/3 completed (12s)"
    QString msg = QStringLiteral("%1%2 Group %3/%4 completed (%5)")
                      .arg(kIndent2)
                      .arg(kCheckmark)
                      .arg(index)
                      .arg(total)
                      .arg(formatDuration(elapsedMs));
    m_callback(LogCategory::Success, msg);
}

void WorkflowLogger::groupSkipped(int index, int total, const QString& groupName, const QString& reason)
{
    if (!m_callback)
        return;

    // Format: "  [1/3] ∅ Skipped: M42_... — Output up-to-date"
    const QString displayName = truncateGroupName(groupName);
    QString msg =
        QStringLiteral("%1[%2/%3] %4 Skipped: %5").arg(kIndent1).arg(index).arg(total).arg(kSkipSign).arg(displayName);
    if (!reason.isEmpty())
    {
        msg += QStringLiteral(" — %1").arg(reason);
    }
    m_callback(LogCategory::Detail, msg);
}

void WorkflowLogger::frameProgress(int current, int total, const QString& filename)
{
    if (!m_callback)
        return;

    // Format: "      → Frame 5/24: IMG_0023.fit"
    QString msg = QStringLiteral("%1%2 Frame %3/%4: %5")
                      .arg(kIndent3)
                      .arg(kArrowRight)
                      .arg(current)
                      .arg(total)
                      .arg(QFileInfo(filename).fileName());
    m_callback(LogCategory::Progress, msg);
}

void WorkflowLogger::phaseStarted(int index, int total, const QString& name, const QString& detail)
{
    if (!m_callback)
        return;

    // Format: "    ▸ PHASE 1/2: Star Detection — 714 frame(s)"
    QString msg =
        QStringLiteral("%1%2 PHASE %3/%4: %5").arg(kIndent2).arg(kPhaseMarker).arg(index).arg(total).arg(name);
    if (!detail.isEmpty())
    {
        msg += QStringLiteral(" %1 %2").arg(kEmDash).arg(detail);
    }
    m_callback(LogCategory::Header, msg);
}

void WorkflowLogger::phaseDetail(const QString& detail)
{
    if (!m_callback)
        return;

    // Format: "      • Detector: StarProfilerV2 | radius 5 | sigma 1.00"
    m_callback(LogCategory::Detail, QStringLiteral("%1%2 %3").arg(kIndent3).arg(kBullet).arg(detail));
}

void WorkflowLogger::phaseProgress(const QString& verb, int processed, int total, const QString& detail)
{
    if (!m_callback)
        return;

    const int percent = (total > 0) ? (processed * 100 / total) : 0;

    // Format: "      → Detected 350/714 (49%) — 6m 2s elapsed, ~6m 18s remaining"
    QString msg = QStringLiteral("%1%2 %3 %4/%5 (%6%)")
                      .arg(kIndent3)
                      .arg(kArrowRight)
                      .arg(verb)
                      .arg(processed)
                      .arg(total)
                      .arg(percent);
    if (!detail.isEmpty())
    {
        msg += QStringLiteral(" %1 %2").arg(kEmDash).arg(detail);
    }
    m_callback(LogCategory::Progress, msg);
}

void WorkflowLogger::phaseCompleted(int index, int total, const QString& name, qint64 elapsedMs, const QString& summary)
{
    if (!m_callback)
        return;

    // Format: "    ✓ PHASE 1/2 Star Detection complete (12m 20s)"
    QString msg = QStringLiteral("%1%2 PHASE %3/%4 %5 complete (%6)")
                      .arg(kIndent2)
                      .arg(kCheckmark)
                      .arg(index)
                      .arg(total)
                      .arg(name)
                      .arg(formatDuration(elapsedMs));
    if (!summary.isEmpty())
    {
        msg += QStringLiteral(" %1 %2").arg(kEmDash).arg(summary);
    }
    m_callback(LogCategory::Success, msg);
}

void WorkflowLogger::frameBatchProgress(int processed, int total, int batchReportInterval, const QString& verb)
{
    if (!m_callback)
        return;
    const int interval = (std::max)(1, batchReportInterval);
    if (processed == 1 || processed == total || processed % interval == 0)
    {
        int percent = (total > 0) ? (processed * 100 / total) : 0;

        // Format: "      → Processed 12/24 frames (50%)"
        QString msg = QStringLiteral("%1%2 %3 %4/%5 frames (%6%)")
                          .arg(kIndent3)
                          .arg(kArrowRight)
                          .arg(verb.isEmpty() ? QStringLiteral("Processed") : verb)
                          .arg(processed)
                          .arg(total)
                          .arg(percent);
        m_callback(LogCategory::Progress, msg);
    }
}

void WorkflowLogger::operationDetail(const QString& operation, const QString& detail)
{
    if (!m_callback)
        return;

    // Format: "    → Loading frames..." or "    → Calibration: Using master bias"
    QString msg = QStringLiteral("%1%2 %3").arg(kIndent2, kArrowRight, operation);
    if (!detail.isEmpty())
    {
        msg += QStringLiteral(": %1").arg(detail);
    }
    m_callback(LogCategory::Detail, msg);
}

void WorkflowLogger::artifactCreated(const QString& type, const QString& path)
{
    if (!m_callback)
        return;

    // Format: "      • Output created: master_bias.fit"
    m_callback(LogCategory::Success,
               QStringLiteral("%1%2 %3: %4").arg(kIndent3).arg(kBullet).arg(type).arg(shortenPath(path)));
}

void WorkflowLogger::settingsApplied(const QString& setting, const QString& value)
{
    if (!m_callback)
        return;

    // Format: "      • Stacking method: Sigma clipping"
    m_callback(LogCategory::Detail, QStringLiteral("%1%2 %3: %4").arg(kIndent3).arg(kBullet).arg(setting).arg(value));
}

void WorkflowLogger::warning(const QString& message)
{
    if (!m_callback)
        return;

    // Format: "    ⚠ Reference frame not found, using first available"
    m_callback(LogCategory::Warning, QStringLiteral("%1%2 %3").arg(kIndent2).arg(kWarningSign).arg(message));
}

void WorkflowLogger::error(const QString& message, const QString& detail)
{
    if (!m_callback)
        return;

    // Format: "    ✗ Failed to load frame: File not found"
    QString msg = QStringLiteral("%1%2 %3").arg(kIndent2, kCrossmark, message);
    if (!detail.isEmpty())
    {
        msg += QStringLiteral(": %1").arg(detail);
    }
    m_callback(LogCategory::Error, msg);
}

QString WorkflowLogger::formatDuration(qint64 ms)
{
    if (ms < 1000)
        return QStringLiteral("%1 ms").arg(ms);
    qint64 s = ms / 1000;
    if (s < 60)
        return QStringLiteral("%1 s").arg(s);
    qint64 m = s / 60;
    s %= 60;
    if (m < 60)
        return QStringLiteral("%1m %2s").arg(m).arg(s);
    qint64 h = m / 60;
    m %= 60;
    return QStringLiteral("%1h %2m").arg(h).arg(m);
}

QString WorkflowLogger::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QStringLiteral("%1 KB").arg(formatDouble(kb, 1));
    double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QStringLiteral("%1 MB").arg(formatDouble(mb, 1));
    double gb = mb / 1024.0;
    return QStringLiteral("%1 GB").arg(formatDouble(gb, 2));
}

QString WorkflowLogger::shortenPath(const QString& path, int maxChars)
{
    if (path.length() <= maxChars)
        return path;
    QFileInfo info(path);
    QString fileName = info.fileName();
    if (fileName.length() >= maxChars)
        return fileName;

    QString dir = info.path();
    // Simple truncation: ".../filename"
    int available = maxChars - fileName.length() - 4; // for ".../"
    if (available <= 0)
        return QStringLiteral(".../") + fileName;

    return dir.left(available) + QStringLiteral(".../") + fileName;
}
