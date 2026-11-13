// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file SummaryTab.h
 * @brief Summary tab for Image Integration.
 *
 * History & Execution Hub:
 * - Readiness banner with blocking issues
 * - Workflow progress overview
 * - Integration summary + session notes
 * - Run history with actions
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <memory>
#include <QDateTime>
#include <QStringList>
#include <QVariantList>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/ValidationResult.h"

class IntegrationDataModel;
class QEvent;

/**
 * @class SummaryTab
 * @brief Summary tab with readiness, history, and execution actions.
 */
class AFTERNIGHTPROCESSES_API SummaryTab : public QWidget
{
    W_OBJECT(SummaryTab)

public:
    explicit SummaryTab(IntegrationDataModel* model, QWidget* parent = nullptr);
    ~SummaryTab() override;

    void setLastSuccessfulRun(const QDateTime& whenUtc, int lightFrameCount, const QString& outputDir);

public slots:
    void refreshAll();

    // Navigation signal - emitted when user clicks a "Fix" link
    void fixRequested(FixTarget target) W_SIGNAL(fixRequested, target);

    // Navigation signal with group context - emitted when fix should also select a group
    void fixWithGroupRequested(FixTarget target, const QString& groupKey)
        W_SIGNAL(fixWithGroupRequested, target, groupKey);

    // Emitted when user clicks Execute
    void executeRequested() W_SIGNAL(executeRequested);

    // Intent signals for tests and navigation
    void openRequested(const QString& path) W_SIGNAL(openRequested, path);
    void compareRequested(const QStringList& paths, const QStringList& labels, const QStringList& contexts,
                          const QVariantList& historyRows, const QString& groupKey)
        W_SIGNAL(compareRequested, paths, labels, contexts, historyRows, groupKey);
    void exportReportRequested(const QString& runDir) W_SIGNAL(exportReportRequested, runDir);
    /// Emitted when the person asks for a run's report as one file that opens on its own.
    void saveReportAsOneFileRequested(const QString& runDir) W_SIGNAL(saveReportAsOneFileRequested, runDir);
    void rerunRequested(const QString& runDir) W_SIGNAL(rerunRequested, runDir);
    void notesChanged(const QString& notes) W_SIGNAL(notesChanged, notes);
    void refreshed() W_SIGNAL(refreshed);

protected:
    void changeEvent(QEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
