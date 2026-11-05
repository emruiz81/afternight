// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file RegistrationMetricsWriter.h
 * @brief Shared SQLite writer for registration metrics persistence.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/core/RegistrationMetricsPersistenceTypes.h"

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>

#include <QString>
#include <QStringList>

#include <vector>

namespace an::ui
{
/**
 * @brief Writes `registration_metrics.db` rows using the shared schema contract.
 */
class AFTERNIGHTPROCESSES_API RegistrationMetricsWriter
{
public:
    explicit RegistrationMetricsWriter(QString databasePath,
                                       QString connectionPrefix = QStringLiteral("RegistrationMetricsWriter"),
                                       WorkflowExecutionCounterAccumulator* counters = nullptr);

    /// Opens the database and, when it holds no table, makes it at the current format. A database of another format,
    /// or whose tables are not its format's, is refused before anything is created or written, naming the database,
    /// the format found and the one this version reads.
    [[nodiscard]] bool initialize(QString* error = nullptr);
    [[nodiscard]] bool openExisting(QString* error = nullptr);
    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] bool upsertGroupState(const RegistrationGroupState& state, QString* error = nullptr);
    [[nodiscard]] bool upsertMetric(const RegistrationMetricRecord& metric, QString* error = nullptr);
    [[nodiscard]] bool upsertStarCache(const RegistrationStarCacheEntry& entry, QString* error = nullptr);
    [[nodiscard]] bool upsertMatchedSource(const RegistrationMatchedSourceRecord& record, QString* error = nullptr);

    /**
     * Group batch APIs use all-or-nothing rollback. Every non-empty homogeneous
     * batch runs in exactly one transaction and reuses one prepared statement
     * per SQL operation. Frame-keyed batches reject duplicate incoming FrameId
     * identities. Upsert/replacement batches collapse case/separator variants
     * of a frame's path to the incoming canonical spelling; aligned-output updates apply
     * to every equivalent persisted row without discarding its metric payload.
     * Batch item indexes in errors are zero-based.
     */
    [[nodiscard]] bool upsertMetricsForGroup(const QString& groupKey, int settingsVersion,
                                             const std::vector<RegistrationMetricRecord>& metrics,
                                             QString* error = nullptr);
    [[nodiscard]] bool upsertStarCacheForGroup(const QString& groupKey, const QString& detectionHash,
                                               const std::vector<RegistrationStarCacheEntry>& entries,
                                               QString* error = nullptr);
    [[nodiscard]] bool
    replaceMatchedSourcesForGroup(const QString& groupKey, int settingsVersion,
                                  const std::vector<RegistrationMatchedSourceReplacement>& replacements,
                                  QString* error = nullptr);
    [[nodiscard]] bool updateAlignedOutputPathsForGroup(
        const QString& groupKey, int settingsVersion, const std::vector<RegistrationAlignedOutputPathUpdate>& updates,
        QString* error = nullptr, RegistrationAlignedOutputPathBatchDiagnostics* diagnostics = nullptr);

    [[nodiscard]] bool replaceMatchedSourcesForFrame(const QString& groupKey, int settingsVersion,
                                                     const FrameId& frameId,
                                                     const std::vector<an::MatchedSourceReference>& references,
                                                     QString* error = nullptr);
    [[nodiscard]] bool upsertCometTrackingRun(const CometTrackingRunRecord& record, QString* error = nullptr);
    [[nodiscard]] bool upsertCometTrackingRunSummary(const CometTrackingRunSummaryRecord& record,
                                                     QString* error = nullptr);
    [[nodiscard]] bool upsertCometTrackingPoint(const CometTrackingPointRecord& record, QString* error = nullptr);
    [[nodiscard]] bool upsertCometTransformRole(const CometTransformRoleRecord& record, QString* error = nullptr);
    [[nodiscard]] bool upsertCometOutputPlane(const CometOutputPlaneRecord& record, QString* error = nullptr);
    [[nodiscard]] bool upsertCometRegisteredOutput(const CometRegisteredOutputRecord& record, QString* error = nullptr);
    [[nodiscard]] bool upsertCometRoleReadiness(const CometRoleReadinessRecord& record, QString* error = nullptr);
    [[nodiscard]] bool upsertStackingCometArtifact(const StackingCometArtifactRecord& record, QString* error = nullptr);
    [[nodiscard]] bool replaceStackingCometArtifacts(const std::vector<StackingCometArtifactRecord>& records,
                                                     QString* error = nullptr);
    [[nodiscard]] bool deleteStaleStackingCometArtifacts(const QString& cometRunId, const QString& integrationGroupKey,
                                                         const QString& currentStackingHash, QString* error = nullptr);
    [[nodiscard]] bool activateCometTrackingRun(const QString& cometRunId, const QStringList& requiredRoles,
                                                bool requireMaterializedOutputs, QString* error = nullptr);

    [[nodiscard]] bool copyMetricsToSettingsVersion(const QString& groupKey, int fromSettingsVersion,
                                                    int toSettingsVersion, QString* error = nullptr);
    [[nodiscard]] bool deleteAllForGroup(const QString& groupKey, QString* error = nullptr);

private:
    QString m_databasePath;
    core::SqliteConnection m_connection;
};
} // namespace an::ui
