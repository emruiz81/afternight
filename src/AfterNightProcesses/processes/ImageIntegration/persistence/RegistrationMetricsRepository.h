// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

#include <QString>

#include <functional>
#include <optional>
#include <vector>

struct RegistrationMetricsCandidate
{
    FrameId id;
    FrameQualityMetrics metrics;
};

struct AutoReferencePickResult
{
    FrameId id;
    FrameQualityMetrics metrics;
    double score = 0.0;
};

class AFTERNIGHTPROCESSES_API RegistrationReferenceScorer
{
public:
    [[nodiscard]] static std::optional<AutoReferencePickResult>
    pickBest(const QString& groupKey, const std::vector<RegistrationMetricsCandidate>& candidates,
             const std::function<QString(const FrameId&)>& getFilter, QString& errorOut);
};

/**
 * @brief Repository for loading registration metrics candidates from SQLite.
 */
class AFTERNIGHTPROCESSES_API RegistrationMetricsRepository
{
public:
    explicit RegistrationMetricsRepository(QString metricsPath,
                                           WorkflowExecutionCounterAccumulator* counters = nullptr);

    [[nodiscard]] bool loadCandidates(const QString& groupKey, int settingsVersion,
                                      const std::vector<FrameId>& allowedFrames,
                                      std::vector<RegistrationMetricsCandidate>& outCandidates,
                                      QString& errorOut) const;

    [[nodiscard]] std::optional<AutoReferencePickResult>
    pickBestReference(const QString& groupKey, int settingsVersion, const std::vector<FrameId>& allowedFrames,
                      const std::function<QString(const FrameId&)>& getFilter, QString& errorOut) const;

private:
    QString m_metricsPath;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
};
