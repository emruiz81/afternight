// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file RegistrationMetricsStateLoader.h
 * @brief Shared SQLite reader and reconstruction helper for registration metrics.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/core/RegistrationMetricsPersistenceTypes.h"

#include <QString>

namespace an::ui
{
/**
 * @brief Reads persisted registration state and rebuilds alignment-ready payloads.
 */
class AFTERNIGHTPROCESSES_API RegistrationMetricsStateLoader
{
public:
    explicit RegistrationMetricsStateLoader(QString databasePath,
                                            QString connectionPrefix = QStringLiteral("RegistrationMetricsStateLoader"),
                                            WorkflowExecutionCounterAccumulator* counters = nullptr);

    [[nodiscard]] bool open(QString* error = nullptr);
    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] bool loadAvailableStates(std::vector<RegistrationMetricsGroupVersion>& out, QString* error = nullptr);
    [[nodiscard]] std::optional<RegistrationGroupState> loadGroupState(const QString& groupKey, int settingsVersion,
                                                                       QString* error = nullptr);
    [[nodiscard]] std::optional<RegistrationGroupState> loadLatestGroupState(const QString& groupKey,
                                                                             QString* error = nullptr);
    [[nodiscard]] bool loadMetricStatuses(const QString& groupKey, int settingsVersion,
                                          std::unordered_map<QString, RegistrationMetricStatus>& out,
                                          QString* error = nullptr);
    [[nodiscard]] bool loadStarCacheForGroup(const QString& groupKey, const QString& detectionHash,
                                             std::unordered_map<QString, RegistrationStarCacheEntry>& out,
                                             QString* error = nullptr);
    [[nodiscard]] bool loadMetricRecords(const QString& groupKey, int settingsVersion,
                                         std::vector<RegistrationMetricRecord>& out, QString* error = nullptr);
    [[nodiscard]] bool loadMatchedSources(const QString& groupKey, int settingsVersion,
                                          std::vector<RegistrationMatchedSourceRecord>& out, QString* error = nullptr);
    [[nodiscard]] bool loadMatchedSourcesForFrame(const QString& groupKey, int settingsVersion, const FrameId& frameId,
                                                  std::vector<RegistrationMatchedSourceRecord>& out,
                                                  QString* error = nullptr);
    [[nodiscard]] std::optional<CometTrackingRunRecord>
    loadActiveCometRunForContext(const QString& groupKey, const QString& registrationHash,
                                 const QString& lightFilesHash, const QString& calibrationHash,
                                 const QString& resolvedReferenceFrameId, QString* error = nullptr);
    [[nodiscard]] std::optional<CometTrackingRunSummaryRecord> loadCometTrackingRunSummary(const QString& cometRunId,
                                                                                           QString* error = nullptr);
    [[nodiscard]] bool loadCometTrackingPoints(const QString& cometRunId, std::vector<CometTrackingPointRecord>& out,
                                               QString* error = nullptr);
    [[nodiscard]] std::optional<CometRoleReadinessRecord>
    loadCometRoleReadiness(const QString& cometRunId, const QString& role, QString* error = nullptr);
    [[nodiscard]] std::optional<CometOutputPlaneRecord> loadCometOutputPlane(const QString& cometOutputPlaneId,
                                                                             QString* error = nullptr);
    [[nodiscard]] std::optional<CometOutputPlaneRecord>
    loadCometOutputPlaneForRole(const QString& cometRunId, const QString& role, QString* error = nullptr);
    [[nodiscard]] std::optional<CometTransformRoleRecord> loadCometTransformRole(const QString& cometRunId,
                                                                                 const FrameId& frameId,
                                                                                 const QString& role,
                                                                                 QString* error = nullptr);
    [[nodiscard]] bool loadCometTransformRoles(const QString& cometRunId, const QString& role,
                                               std::vector<CometTransformRoleRecord>& out, QString* error = nullptr);
    [[nodiscard]] std::optional<CometTransformRoleProduct>
    loadCometTransformRoleProduct(const QString& cometRunId, const QString& role, QString* error = nullptr);
    [[nodiscard]] std::optional<CometRegisteredOutputRecord> loadCometRegisteredOutput(const QString& cometRunId,
                                                                                       const FrameId& frameId,
                                                                                       const QString& role,
                                                                                       QString* error = nullptr);
    [[nodiscard]] bool loadStackingCometArtifacts(const QString& cometRunId,
                                                  std::vector<StackingCometArtifactRecord>& out,
                                                  QString* error = nullptr);
    [[nodiscard]] std::optional<RegistrationMetricsLoadResult> loadState(const QString& groupKey, int settingsVersion,
                                                                         const std::vector<FrameId>& currentFrames,
                                                                         QString* error = nullptr);

private:
    core::SqliteConnection m_connection;
};
} // namespace an::ui
