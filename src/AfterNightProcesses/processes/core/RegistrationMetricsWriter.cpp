// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationMetricsWriter.h"

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"

#include <AfterNightPlatform/Logging.h>

#include <QDateTime>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace
{
void assignError(QString* error, const QString& value)
{
    if (error != nullptr)
    {
        *error = value;
    }
}

/// The text the database holds for a Gaia source identifier.
QString sourceIdText(const std::uint64_t identifier)
{
    return QString::number(identifier);
}

QString fromStdString(const std::string& value)
{
    return QString::fromStdString(value);
}

QVariant nullableDouble(const double value)
{
    return std::isfinite(value) ? QVariant(value) : QVariant();
}

QVariant nullableDouble(const std::optional<double>& value)
{
    return value.has_value() ? nullableDouble(*value) : QVariant();
}

qint64 timestampOrNow(const qint64 value)
{
    return value > 0 ? value : QDateTime::currentSecsSinceEpoch();
}

QString registrationMetricUpsertSql()
{
    return QStringLiteral("INSERT OR REPLACE INTO registration_metrics ("
                          "frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
                          "background_noise, global_mad, global_scale, "
                          "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, "
                          "astrometric_rms_arcsec, limiting_magnitude, catalog_matched_stars, "
                          "succeeded, output_path, output_size_bytes, output_modified_at_ms, "
                          "output_identity_token, transform_json, updated_at) "
                          "VALUES (:frame_id, :group_key, :settings_version, :stars, :fwhm, :snr, :eccentricity, "
                          ":background_noise, :global_mad, :global_scale, "
                          ":photometric_zero_point, :zero_point_scatter, :astrometric_rms_pixels, "
                          ":astrometric_rms_arcsec, :limiting_magnitude, :catalog_matched_stars, "
                          ":succeeded, :output_path, :output_size_bytes, :output_modified_at_ms, "
                          ":output_identity_token, :transform_json, :updated_at)");
}

void bindRegistrationMetric(QSqlQuery& query, const an::ui::RegistrationMetricRecord& metric)
{
    qint64 output_size_bytes = metric.outputSizeBytes;
    qint64 output_modified_at_ms = metric.outputModifiedAtMs;
    QString output_identity_token = metric.outputIdentityToken;
    if (metric.metrics.succeeded &&
        (output_size_bytes < 0 || output_modified_at_ms < 0 || output_identity_token.isEmpty()))
    {
        const QFileInfo output_info(metric.outputPath);
        if (output_info.isFile())
        {
            output_size_bytes = output_info.size();
            output_modified_at_ms = output_info.lastModified().toMSecsSinceEpoch();
            output_identity_token = ArtifactTracker::fileIdentityForPath(output_info.absoluteFilePath());
        }
    }

    query.bindValue(QStringLiteral(":frame_id"), metric.frameId.canonicalPath());
    query.bindValue(QStringLiteral(":group_key"), metric.groupKey);
    query.bindValue(QStringLiteral(":settings_version"), metric.settingsVersion);
    query.bindValue(QStringLiteral(":stars"), metric.metrics.stars);
    query.bindValue(QStringLiteral(":fwhm"), metric.metrics.fwhm);
    query.bindValue(QStringLiteral(":snr"), metric.metrics.snr);
    query.bindValue(QStringLiteral(":eccentricity"), metric.metrics.eccentricity);
    query.bindValue(QStringLiteral(":background_noise"), metric.noise.background_noise);
    query.bindValue(QStringLiteral(":global_mad"), metric.noise.global_mad);
    query.bindValue(QStringLiteral(":global_scale"), metric.noise.global_scale);
    // Standalone registration rows intentionally use zero rather than NULL for
    // astrometric fields that were not populated by a plate solve.
    query.bindValue(QStringLiteral(":photometric_zero_point"), metric.metrics.photometric_zero_point);
    query.bindValue(QStringLiteral(":zero_point_scatter"), metric.metrics.zero_point_scatter);
    query.bindValue(QStringLiteral(":astrometric_rms_pixels"), metric.metrics.astrometric_rms_pixels);
    query.bindValue(QStringLiteral(":astrometric_rms_arcsec"), metric.metrics.astrometric_rms_arcsec);
    query.bindValue(QStringLiteral(":limiting_magnitude"), metric.metrics.limiting_magnitude);
    query.bindValue(QStringLiteral(":catalog_matched_stars"), metric.metrics.catalog_matched_stars);
    query.bindValue(QStringLiteral(":succeeded"), metric.metrics.succeeded ? 1 : 0);
    query.bindValue(QStringLiteral(":output_path"), metric.outputPath);
    query.bindValue(QStringLiteral(":output_size_bytes"), output_size_bytes);
    query.bindValue(QStringLiteral(":output_modified_at_ms"), output_modified_at_ms);
    query.bindValue(QStringLiteral(":output_identity_token"), output_identity_token);
    query.bindValue(QStringLiteral(":transform_json"), metric.transformJson);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(metric.updatedAt));
}

QString registrationStarCacheUpsertSql()
{
    return QStringLiteral("INSERT OR REPLACE INTO star_detection_cache ("
                          "frame_id, group_key, detection_hash, input_path, input_mtime, input_size_bytes, "
                          "input_modified_at_ms, input_identity_token, stars_count, fwhm, snr, roundness, "
                          "background_noise, global_mad, global_scale, "
                          "stars_blob, updated_at) "
                          "VALUES (:frame_id, :group_key, :detection_hash, :input_path, :input_mtime, "
                          ":input_size_bytes, :input_modified_at_ms, :input_identity_token, :stars_count, :fwhm, "
                          ":snr, :roundness, :background_noise, :global_mad, :global_scale, "
                          ":stars_blob, :updated_at)");
}

void bindRegistrationStarCache(QSqlQuery& query, const an::ui::RegistrationStarCacheEntry& entry)
{
    query.bindValue(QStringLiteral(":frame_id"), entry.frameId.canonicalPath());
    query.bindValue(QStringLiteral(":group_key"), entry.groupKey);
    query.bindValue(QStringLiteral(":detection_hash"), entry.detectionHash);
    query.bindValue(QStringLiteral(":input_path"), entry.inputPath);
    query.bindValue(QStringLiteral(":input_mtime"), entry.inputMtime);
    query.bindValue(QStringLiteral(":input_size_bytes"), entry.inputSizeBytes);
    query.bindValue(QStringLiteral(":input_modified_at_ms"), entry.inputModifiedAtMs);
    query.bindValue(QStringLiteral(":input_identity_token"), entry.inputIdentityToken);
    query.bindValue(QStringLiteral(":stars_count"), entry.starsCount);
    query.bindValue(QStringLiteral(":fwhm"), entry.fwhm);
    query.bindValue(QStringLiteral(":snr"), entry.snr);
    query.bindValue(QStringLiteral(":roundness"), entry.roundness);
    query.bindValue(QStringLiteral(":background_noise"), entry.noise.background_noise);
    query.bindValue(QStringLiteral(":global_mad"), entry.noise.global_mad);
    query.bindValue(QStringLiteral(":global_scale"), entry.noise.global_scale);
    query.bindValue(QStringLiteral(":stars_blob"), entry.starsBlob);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(entry.updatedAt));
}

QString registrationMatchedSourceUpsertSql()
{
    return QStringLiteral("INSERT OR REPLACE INTO registration_matched_sources ("
                          "frame_id, group_key, settings_version, gaia_source_id, "
                          "astrometric_catalog_family_id, astrometric_catalog_manifest_id, "
                          "spcc_catalog_family_id, spcc_catalog_manifest_id, wcs_solution_id, "
                          "observation_epoch, proper_motion_model_version, match_separation_arcsec, "
                          "residual_px, lookup_status, updated_at) "
                          "VALUES (:frame_id, :group_key, :settings_version, :gaia_source_id, "
                          ":astrometric_catalog_family_id, :astrometric_catalog_manifest_id, "
                          ":spcc_catalog_family_id, :spcc_catalog_manifest_id, :wcs_solution_id, "
                          ":observation_epoch, :proper_motion_model_version, :match_separation_arcsec, "
                          ":residual_px, :lookup_status, :updated_at)");
}

void bindRegistrationMatchedSource(QSqlQuery& query, const an::ui::RegistrationMatchedSourceRecord& record)
{
    query.bindValue(QStringLiteral(":frame_id"), record.frameId.canonicalPath());
    query.bindValue(QStringLiteral(":group_key"), record.groupKey);
    query.bindValue(QStringLiteral(":settings_version"), record.settingsVersion);
    query.bindValue(QStringLiteral(":gaia_source_id"), sourceIdText(record.reference.sourceIdentifier));
    query.bindValue(QStringLiteral(":astrometric_catalog_family_id"),
                    fromStdString(record.reference.astrometricCatalogFamilyId));
    query.bindValue(QStringLiteral(":astrometric_catalog_manifest_id"),
                    fromStdString(record.reference.astrometricCatalogManifestId));
    query.bindValue(QStringLiteral(":spcc_catalog_family_id"), fromStdString(record.reference.spectrumCatalogFamilyId));
    query.bindValue(QStringLiteral(":spcc_catalog_manifest_id"),
                    fromStdString(record.reference.spectrumCatalogManifestId));
    query.bindValue(QStringLiteral(":wcs_solution_id"), fromStdString(record.reference.wcsSolutionId));
    query.bindValue(QStringLiteral(":observation_epoch"), fromStdString(record.reference.observationEpoch));
    query.bindValue(QStringLiteral(":proper_motion_model_version"),
                    fromStdString(record.reference.properMotionModelVersion));
    query.bindValue(QStringLiteral(":match_separation_arcsec"), nullableDouble(record.reference.separationArcseconds));
    query.bindValue(QStringLiteral(":residual_px"), nullableDouble(record.reference.residualPixels));
    query.bindValue(QStringLiteral(":lookup_status"), record.lookupStatus.trimmed().isEmpty()
                                                          ? QStringLiteral("untested")
                                                          : record.lookupStatus.trimmed());
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));
}

QString versionedBatchItemError(const QString& operation, const size_t index, const QString& canonicalFrameId,
                                const QString& groupKey, const int settingsVersion, const QString& sqliteError)
{
    return QStringLiteral("%1 failed at batch_item_index=%2 (frame_id='%3', group_key='%4', settings_version=%5): "
                          "SQLite error: %6")
        .arg(operation)
        .arg(index)
        .arg(canonicalFrameId, groupKey)
        .arg(settingsVersion)
        .arg(sqliteError);
}

QString starCacheBatchItemError(const QString& operation, const size_t index, const QString& canonicalFrameId,
                                const QString& groupKey, const QString& detectionHash, const QString& sqliteError)
{
    return QStringLiteral("%1 failed at batch_item_index=%2 (frame_id='%3', group_key='%4', detection_hash='%5'): "
                          "SQLite error: %6")
        .arg(operation)
        .arg(index)
        .arg(canonicalFrameId, groupKey, detectionHash, sqliteError);
}

bool rollbackBatch(core::SqliteConnection& connection, const QString& failure, QString* error)
{
    if (connection.rollbackTransaction())
    {
        assignError(error, failure);
        return false;
    }

    assignError(error, QStringLiteral("%1; rollback error: %2").arg(failure, connection.db().lastError().text()));
    return false;
}

bool isStackingCometArtifactKind(const QString& kind)
{
    return kind == QStringLiteral("FinalComposite") || kind == QStringLiteral("CometLayer") ||
           kind == QStringLiteral("CometMask") || kind == QStringLiteral("OverlayPreview");
}

bool findStackingCometArtifactConflict(QSqlDatabase& db, const an::ui::StackingCometArtifactRecord& record,
                                       const QString& columnName, const QString& value, QString& conflictKind,
                                       QString* error, WorkflowExecutionCounterAccumulator* counters)
{
    conflictKind.clear();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT artifact_kind FROM stacking_comet_artifacts "
                                 "WHERE comet_run_id = :comet_run_id "
                                 "AND integration_group_key = :integration_group_key "
                                 "AND stacking_hash = :stacking_hash "
                                 "AND comet_output_plane_id = :comet_output_plane_id "
                                 "AND %1 = :value "
                                 "AND artifact_kind <> :artifact_kind "
                                 "LIMIT 1")
                      .arg(columnName));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":integration_group_key"), record.integrationGroupKey);
    query.bindValue(QStringLiteral(":stacking_hash"), record.stackingHash);
    query.bindValue(QStringLiteral(":comet_output_plane_id"), record.cometOutputPlaneId);
    query.bindValue(QStringLiteral(":value"), value);
    query.bindValue(QStringLiteral(":artifact_kind"), record.artifactKind);
    if (!core::executeSqliteStatement(query, counters))
    {
        assignError(error, query.lastError().text());
        return false;
    }
    if (query.next())
    {
        conflictKind = query.value(0).toString();
    }
    return true;
}

bool ensureCometRoleReady(QSqlDatabase& db, const QString& cometRunId, const QString& role,
                          const bool requireMaterializedOutputs, QString* error,
                          WorkflowExecutionCounterAccumulator* counters)
{
    QSqlQuery planeQuery(db);
    planeQuery.prepare(QStringLiteral("SELECT 1 FROM comet_output_planes "
                                      "WHERE comet_run_id = :comet_run_id AND role = :role LIMIT 1"));
    planeQuery.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    planeQuery.bindValue(QStringLiteral(":role"), role);
    if (!core::executeSqliteStatement(planeQuery, counters))
    {
        assignError(error, planeQuery.lastError().text());
        return false;
    }
    if (!planeQuery.next())
    {
        assignError(error, QStringLiteral("Comet run '%1' has no output plane for role '%2'.").arg(cometRunId, role));
        return false;
    }

    QSqlQuery readinessQuery(db);
    readinessQuery.prepare(QStringLiteral("SELECT readiness_status, selected_frame_count, "
                                          "successful_transform_count, failed_transform_count, "
                                          "missing_transform_count, output_plane_status, "
                                          "materialized_output_count, missing_output_count, failed_output_count "
                                          "FROM comet_role_readiness "
                                          "WHERE comet_run_id = :comet_run_id AND role = :role"));
    readinessQuery.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    readinessQuery.bindValue(QStringLiteral(":role"), role);
    if (!core::executeSqliteStatement(readinessQuery, counters))
    {
        assignError(error, readinessQuery.lastError().text());
        return false;
    }
    if (!readinessQuery.next())
    {
        assignError(error, QStringLiteral("Comet run '%1' has no readiness row for role '%2'.").arg(cometRunId, role));
        return false;
    }

    const QString readinessStatus = readinessQuery.value(0).toString();
    const int selectedFrameCount = readinessQuery.value(1).toInt();
    const int successfulTransformCount = readinessQuery.value(2).toInt();
    const int failedTransformCount = readinessQuery.value(3).toInt();
    const int missingTransformCount = readinessQuery.value(4).toInt();
    const QString outputPlaneStatus = readinessQuery.value(5).toString();
    const int materializedOutputCount = readinessQuery.value(6).toInt();
    const int missingOutputCount = readinessQuery.value(7).toInt();
    const int failedOutputCount = readinessQuery.value(8).toInt();

    if (outputPlaneStatus != QStringLiteral("Ready"))
    {
        assignError(error, QStringLiteral("Comet run '%1' role '%2' output plane is not ready.").arg(cometRunId, role));
        return false;
    }
    if (selectedFrameCount <= 0 || successfulTransformCount < selectedFrameCount || failedTransformCount > 0 ||
        missingTransformCount > 0)
    {
        assignError(error, QStringLiteral("Comet run '%1' role '%2' transforms are incomplete.").arg(cometRunId, role));
        return false;
    }

    if (!requireMaterializedOutputs)
    {
        if (readinessStatus == QStringLiteral("TransformReady") || readinessStatus == QStringLiteral("Ready"))
        {
            return true;
        }

        assignError(error, QStringLiteral("Comet run '%1' role '%2' is not transform-ready.").arg(cometRunId, role));
        return false;
    }

    if (readinessStatus != QStringLiteral("Ready") || materializedOutputCount < selectedFrameCount ||
        missingOutputCount > 0 || failedOutputCount > 0)
    {
        assignError(
            error,
            QStringLiteral("Comet run '%1' role '%2' materialized outputs are incomplete.").arg(cometRunId, role));
        return false;
    }

    return true;
}
} // namespace

namespace an::ui
{
RegistrationMetricsWriter::RegistrationMetricsWriter(QString databasePath, QString connectionPrefix,
                                                     WorkflowExecutionCounterAccumulator* counters)
    : m_databasePath(databasePath), m_connection(std::move(databasePath), std::move(connectionPrefix), counters)
{
}

bool RegistrationMetricsWriter::initialize(QString* error)
{
    assignError(error, QString());

    QString openError;
    if (!m_connection.open(&openError))
    {
        assignError(error, openError);
        LOG_ERROR("RegistrationMetricsWriter: failed to open registration metrics DB: {}", openError.toStdString());
        return false;
    }

    // A database of another format is refused before any table is created or written.
    const stage_utils::RegistrationMetricsSchemaInspection inspection =
        stage_utils::inspectRegistrationMetricsSchema(m_connection.db(), m_connection.counterAccumulator());
    if (const QString refusal = stage_utils::registrationMetricsFormatRefusal(inspection, m_databasePath);
        !refusal.isEmpty())
    {
        const QString message = refusal + QStringLiteral(" They were not changed.");
        assignError(error, message);
        LOG_WARNING("RegistrationMetricsWriter: {}", message.toStdString());
        m_connection.close();
        return false;
    }

    QSqlQuery query(m_connection.db());
    const auto execSql = [&](const QString& sql) -> bool {
        if (!m_connection.execute(query, sql))
        {
            const QString queryError = query.lastError().text();
            assignError(error, queryError);
            LOG_ERROR("RegistrationMetricsWriter: schema SQL failed: {}", queryError.toStdString());
            return false;
        }
        return true;
    };

    // The tables and the format they are, in one transaction: a database is made whole or not at all, so an
    // interrupted first write never leaves tables that state no format.
    if (!m_connection.beginTransaction())
    {
        const QString transactionError = m_connection.db().lastError().text();
        assignError(error, transactionError);
        LOG_ERROR("RegistrationMetricsWriter: could not begin the schema transaction: {}",
                  transactionError.toStdString());
        return false;
    }
    const bool made =
        execSql(stage_utils::registrationMetricsCreateSql()) &&
        execSql(stage_utils::registrationGroupStateCreateSql()) &&
        execSql(stage_utils::starDetectionCacheCreateSql()) &&
        execSql(stage_utils::registrationMatchedSourcesCreateSql()) &&
        execSql(stage_utils::registrationMatchedSourcesGroupIndexCreateSql()) &&
        execSql(stage_utils::registrationMatchedSourcesSourceIndexCreateSql()) &&
        execSql(stage_utils::cometTrackingRunsCreateSql()) &&
        execSql(stage_utils::cometTrackingRunsActiveContextIndexCreateSql()) &&
        execSql(stage_utils::cometTrackingRunsStatusIndexCreateSql()) &&
        execSql(stage_utils::cometTrackingRunSummaryCreateSql()) &&
        execSql(stage_utils::cometTrackingPointsCreateSql()) &&
        execSql(stage_utils::cometTrackingPointsFrameIndexCreateSql()) &&
        execSql(stage_utils::cometTrackingPointsAcceptedFrameIndexCreateSql()) &&
        execSql(stage_utils::cometTransformRolesCreateSql()) &&
        execSql(stage_utils::cometTransformRolesPlaneIndexCreateSql()) &&
        execSql(stage_utils::cometOutputPlanesCreateSql()) && execSql(stage_utils::cometRegisteredOutputsCreateSql()) &&
        execSql(stage_utils::cometRegisteredOutputsPlaneIndexCreateSql()) &&
        execSql(stage_utils::cometRegisteredOutputsPathIndexCreateSql()) &&
        execSql(stage_utils::cometRoleReadinessCreateSql()) &&
        execSql(stage_utils::cometRoleReadinessStatusIndexCreateSql()) &&
        execSql(stage_utils::stackingCometArtifactsCreateSql()) &&
        execSql(QStringLiteral("PRAGMA user_version = %1").arg(stage_utils::kRegistrationMetricsSchemaVersion));
    if (!made || !m_connection.commitTransaction())
    {
        if (made)
        {
            const QString commitError = m_connection.db().lastError().text();
            assignError(error, commitError);
            LOG_ERROR("RegistrationMetricsWriter: could not commit the schema: {}", commitError.toStdString());
        }
        (void)m_connection.rollbackTransaction();
        return false;
    }
    return true;
}

bool RegistrationMetricsWriter::openExisting(QString* error)
{
    assignError(error, QString());
    return m_connection.open(error);
}

bool RegistrationMetricsWriter::isOpen() const
{
    return m_connection.isOpen();
}

bool RegistrationMetricsWriter::upsertGroupState(const RegistrationGroupState& state, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(
        QStringLiteral("INSERT OR REPLACE INTO registration_group_state ("
                       "group_key, settings_version, detection_hash, registration_hash, reference_frame_id, "
                       "light_files_hash, calibration_hash, output_wcs_json, updated_at) "
                       "VALUES (:group_key, :settings_version, :detection_hash, :registration_hash, "
                       ":reference_frame_id, :light_files_hash, :calibration_hash, :output_wcs_json, :updated_at)"));
    query.bindValue(QStringLiteral(":group_key"), state.groupKey);
    query.bindValue(QStringLiteral(":settings_version"), state.settingsVersion);
    query.bindValue(QStringLiteral(":detection_hash"), state.detectionHash);
    query.bindValue(QStringLiteral(":registration_hash"), state.registrationHash);
    query.bindValue(QStringLiteral(":reference_frame_id"), state.referenceFrameId);
    query.bindValue(QStringLiteral(":light_files_hash"), state.lightFilesHash);
    query.bindValue(QStringLiteral(":calibration_hash"), state.calibrationHash);
    query.bindValue(QStringLiteral(":output_wcs_json"), state.outputWcsJson);
    query.bindValue(QStringLiteral(":updated_at"),
                    state.updatedAt > 0 ? state.updatedAt : QDateTime::currentSecsSinceEpoch());

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertMetric(const RegistrationMetricRecord& metric, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(registrationMetricUpsertSql());
    bindRegistrationMetric(query, metric);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertStarCache(const RegistrationStarCacheEntry& entry, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(registrationStarCacheUpsertSql());
    bindRegistrationStarCache(query, entry);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertMatchedSource(const RegistrationMatchedSourceRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    const QString frameId = record.frameId.canonicalPath();
    if (frameId.trimmed().isEmpty() || record.groupKey.trimmed().isEmpty() || record.reference.sourceIdentifier == 0u)
    {
        assignError(error, QStringLiteral("Matched source rows require frame_id, group_key, and gaia_source_id."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(registrationMatchedSourceUpsertSql());
    bindRegistrationMatchedSource(query, record);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertMetricsForGroup(const QString& groupKey, const int settingsVersion,
                                                      const std::vector<RegistrationMetricRecord>& metrics,
                                                      QString* error)
{
    assignError(error, QString());
    if (metrics.empty())
    {
        return true;
    }
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, QStringLiteral("metric batch failed to begin transaction "
                                          "(group_key='%1', settings_version=%2): SQLite error: %3")
                               .arg(groupKey)
                               .arg(settingsVersion)
                               .arg(m_connection.db().lastError().text()));
        return false;
    }

    std::unordered_map<FrameId, QString> canonicalFrameIdByIdentity;
    canonicalFrameIdByIdentity.reserve(metrics.size());
    std::unordered_map<FrameId, size_t> firstMetricIndexByIdentity;
    firstMetricIndexByIdentity.reserve(metrics.size());
    for (size_t index = 0; index < metrics.size(); ++index)
    {
        const auto& metric = metrics[index];
        const QString canonicalFrameId = metric.frameId.canonicalPath();
        if (canonicalFrameId.trimmed().isEmpty() || metric.groupKey != groupKey ||
            metric.settingsVersion != settingsVersion)
        {
            return rollbackBatch(m_connection,
                                 QStringLiteral("metric batch validation failed at batch_item_index=%1 "
                                                "(frame_id='%2', group_key='%3', settings_version=%4; "
                                                "expected group_key='%5', settings_version=%6).")
                                     .arg(index)
                                     .arg(canonicalFrameId, metric.groupKey)
                                     .arg(metric.settingsVersion)
                                     .arg(groupKey)
                                     .arg(settingsVersion),
                                 error);
        }
        const auto [identityIt, inserted] = firstMetricIndexByIdentity.emplace(metric.frameId, index);
        if (!inserted)
        {
            return rollbackBatch(
                m_connection,
                QStringLiteral("metric batch validation failed at batch_item_index=%1 "
                               "(frame_id='%2', group_key='%3', settings_version=%4): duplicate frame identity "
                               "already supplied at batch_item_index=%5.")
                    .arg(index)
                    .arg(canonicalFrameId, groupKey)
                    .arg(settingsVersion)
                    .arg(identityIt->second),
                error);
        }
        canonicalFrameIdByIdentity.emplace(metric.frameId, canonicalFrameId);
    }

    QSqlQuery query(m_connection.db());
    if (!query.prepare(registrationMetricUpsertSql()))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("metric batch failed to prepare upsert "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(query.lastError().text()),
                             error);
    }

    for (size_t index = 0; index < metrics.size(); ++index)
    {
        const auto& metric = metrics[index];
        bindRegistrationMetric(query, metric);
        if (!m_connection.execute(query))
        {
            const QString failure =
                versionedBatchItemError(QStringLiteral("metric upsert"), index, metric.frameId.canonicalPath(),
                                        groupKey, settingsVersion, query.lastError().text());
            return rollbackBatch(m_connection, failure, error);
        }
    }

    QSqlQuery identityIndexQuery(m_connection.db());
    if (!identityIndexQuery.prepare(
            QStringLiteral("SELECT frame_id FROM registration_metrics "
                           "WHERE group_key = :group_key AND settings_version = :settings_version")))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("metric batch failed to prepare identity cleanup scan "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(identityIndexQuery.lastError().text()),
                             error);
    }
    identityIndexQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    identityIndexQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    if (!m_connection.execute(identityIndexQuery))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("metric batch identity cleanup scan failed "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(identityIndexQuery.lastError().text()),
                             error);
    }

    std::vector<QString> aliasFrameIds;
    while (identityIndexQuery.next())
    {
        const QString storedFrameId = identityIndexQuery.value(0).toString();
        if (storedFrameId.trimmed().isEmpty())
        {
            continue;
        }
        const auto canonicalIt = canonicalFrameIdByIdentity.find(FrameId(storedFrameId));
        if (canonicalIt != canonicalFrameIdByIdentity.end() && storedFrameId != canonicalIt->second)
        {
            aliasFrameIds.push_back(storedFrameId);
        }
    }
    identityIndexQuery.finish();

    if (!aliasFrameIds.empty())
    {
        QSqlQuery deleteQuery(m_connection.db());
        if (!deleteQuery.prepare(QStringLiteral("DELETE FROM registration_metrics "
                                                "WHERE frame_id = :frame_id AND group_key = :group_key "
                                                "AND settings_version = :settings_version")))
        {
            return rollbackBatch(m_connection,
                                 QStringLiteral("metric batch failed to prepare identity cleanup "
                                                "(group_key='%1', settings_version=%2): SQLite error: %3")
                                     .arg(groupKey)
                                     .arg(settingsVersion)
                                     .arg(deleteQuery.lastError().text()),
                                 error);
        }

        for (const auto& aliasFrameId : aliasFrameIds)
        {
            deleteQuery.bindValue(QStringLiteral(":frame_id"), aliasFrameId);
            deleteQuery.bindValue(QStringLiteral(":group_key"), groupKey);
            deleteQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
            if (!m_connection.execute(deleteQuery))
            {
                return rollbackBatch(m_connection,
                                     QStringLiteral("metric batch identity cleanup failed "
                                                    "(frame_id='%1', group_key='%2', settings_version=%3): "
                                                    "SQLite error: %4")
                                         .arg(aliasFrameId, groupKey)
                                         .arg(settingsVersion)
                                         .arg(deleteQuery.lastError().text()),
                                     error);
            }
        }
    }

    if (!m_connection.commitTransaction())
    {
        const QString failure = QStringLiteral("metric batch failed to commit "
                                               "(group_key='%1', settings_version=%2): SQLite error: %3")
                                    .arg(groupKey)
                                    .arg(settingsVersion)
                                    .arg(m_connection.db().lastError().text());
        return rollbackBatch(m_connection, failure, error);
    }

    return true;
}

bool RegistrationMetricsWriter::upsertStarCacheForGroup(const QString& groupKey, const QString& detectionHash,
                                                        const std::vector<RegistrationStarCacheEntry>& entries,
                                                        QString* error)
{
    assignError(error, QString());
    if (entries.empty())
    {
        return true;
    }
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, QStringLiteral("star-cache batch failed to begin transaction "
                                          "(group_key='%1', detection_hash='%2'): SQLite error: %3")
                               .arg(groupKey, detectionHash, m_connection.db().lastError().text()));
        return false;
    }

    std::unordered_map<FrameId, QString> canonicalFrameIdByIdentity;
    canonicalFrameIdByIdentity.reserve(entries.size());
    std::unordered_map<FrameId, size_t> firstEntryIndexByIdentity;
    firstEntryIndexByIdentity.reserve(entries.size());
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        const QString canonicalFrameId = entry.frameId.canonicalPath();
        if (canonicalFrameId.trimmed().isEmpty() || entry.groupKey != groupKey || entry.detectionHash != detectionHash)
        {
            return rollbackBatch(
                m_connection,
                QStringLiteral("star-cache batch validation failed at batch_item_index=%1 "
                               "(frame_id='%2', group_key='%3', detection_hash='%4'; "
                               "expected group_key='%5', detection_hash='%6').")
                    .arg(index)
                    .arg(canonicalFrameId, entry.groupKey, entry.detectionHash, groupKey, detectionHash),
                error);
        }
        const auto [identityIt, inserted] = firstEntryIndexByIdentity.emplace(entry.frameId, index);
        if (!inserted)
        {
            return rollbackBatch(
                m_connection,
                QStringLiteral("star-cache batch validation failed at batch_item_index=%1 "
                               "(frame_id='%2', group_key='%3', detection_hash='%4'): duplicate frame identity "
                               "already supplied at batch_item_index=%5.")
                    .arg(index)
                    .arg(canonicalFrameId, groupKey, detectionHash)
                    .arg(identityIt->second),
                error);
        }
        canonicalFrameIdByIdentity.emplace(entry.frameId, canonicalFrameId);
    }

    QSqlQuery query(m_connection.db());
    if (!query.prepare(registrationStarCacheUpsertSql()))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("star-cache batch failed to prepare upsert "
                                            "(group_key='%1', detection_hash='%2'): SQLite error: %3")
                                 .arg(groupKey, detectionHash, query.lastError().text()),
                             error);
    }

    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        bindRegistrationStarCache(query, entry);
        if (!m_connection.execute(query))
        {
            const QString failure =
                starCacheBatchItemError(QStringLiteral("star-cache upsert"), index, entry.frameId.canonicalPath(),
                                        groupKey, detectionHash, query.lastError().text());
            return rollbackBatch(m_connection, failure, error);
        }
    }

    QSqlQuery identityIndexQuery(m_connection.db());
    if (!identityIndexQuery.prepare(QStringLiteral("SELECT frame_id FROM star_detection_cache "
                                                   "WHERE group_key = :group_key AND detection_hash = "
                                                   ":detection_hash")))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("star-cache batch failed to prepare identity cleanup scan "
                                            "(group_key='%1', detection_hash='%2'): SQLite error: %3")
                                 .arg(groupKey, detectionHash, identityIndexQuery.lastError().text()),
                             error);
    }
    identityIndexQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    identityIndexQuery.bindValue(QStringLiteral(":detection_hash"), detectionHash);
    if (!m_connection.execute(identityIndexQuery))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("star-cache batch identity cleanup scan failed "
                                            "(group_key='%1', detection_hash='%2'): SQLite error: %3")
                                 .arg(groupKey, detectionHash, identityIndexQuery.lastError().text()),
                             error);
    }

    std::vector<QString> aliasFrameIds;
    while (identityIndexQuery.next())
    {
        const QString storedFrameId = identityIndexQuery.value(0).toString();
        if (storedFrameId.trimmed().isEmpty())
        {
            continue;
        }
        try
        {
            const auto canonicalIt = canonicalFrameIdByIdentity.find(FrameId(storedFrameId));
            if (canonicalIt != canonicalFrameIdByIdentity.end() && storedFrameId != canonicalIt->second)
            {
                aliasFrameIds.push_back(storedFrameId);
            }
        }
        catch (...)
        {
            continue;
        }
    }
    identityIndexQuery.finish();

    if (!aliasFrameIds.empty())
    {
        QSqlQuery deleteQuery(m_connection.db());
        if (!deleteQuery.prepare(QStringLiteral("DELETE FROM star_detection_cache "
                                                "WHERE frame_id = :frame_id AND group_key = :group_key "
                                                "AND detection_hash = :detection_hash")))
        {
            return rollbackBatch(m_connection,
                                 QStringLiteral("star-cache batch failed to prepare identity cleanup "
                                                "(group_key='%1', detection_hash='%2'): SQLite error: %3")
                                     .arg(groupKey, detectionHash, deleteQuery.lastError().text()),
                                 error);
        }
        for (const auto& aliasFrameId : aliasFrameIds)
        {
            deleteQuery.bindValue(QStringLiteral(":frame_id"), aliasFrameId);
            deleteQuery.bindValue(QStringLiteral(":group_key"), groupKey);
            deleteQuery.bindValue(QStringLiteral(":detection_hash"), detectionHash);
            if (!m_connection.execute(deleteQuery))
            {
                return rollbackBatch(m_connection,
                                     QStringLiteral("star-cache batch identity cleanup failed "
                                                    "(frame_id='%1', group_key='%2', detection_hash='%3'): "
                                                    "SQLite error: %4")
                                         .arg(aliasFrameId, groupKey, detectionHash, deleteQuery.lastError().text()),
                                     error);
            }
        }
    }

    if (!m_connection.commitTransaction())
    {
        const QString failure = QStringLiteral("star-cache batch failed to commit "
                                               "(group_key='%1', detection_hash='%2'): SQLite error: %3")
                                    .arg(groupKey, detectionHash, m_connection.db().lastError().text());
        return rollbackBatch(m_connection, failure, error);
    }

    return true;
}

bool RegistrationMetricsWriter::replaceMatchedSourcesForGroup(
    const QString& groupKey, const int settingsVersion,
    const std::vector<RegistrationMatchedSourceReplacement>& replacements, QString* error)
{
    assignError(error, QString());
    if (replacements.empty())
    {
        return true;
    }
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, QStringLiteral("matched-source batch failed to begin transaction "
                                          "(group_key='%1', settings_version=%2): SQLite error: %3")
                               .arg(groupKey)
                               .arg(settingsVersion)
                               .arg(m_connection.db().lastError().text()));
        return false;
    }

    std::unordered_map<FrameId, QString> canonicalFrameIdByIdentity;
    canonicalFrameIdByIdentity.reserve(replacements.size());
    std::unordered_map<FrameId, size_t> replacementIndexByIdentity;
    replacementIndexByIdentity.reserve(replacements.size());
    for (size_t index = 0; index < replacements.size(); ++index)
    {
        const auto& replacement = replacements[index];
        const QString canonicalFrameId = replacement.frameId.canonicalPath();
        if (canonicalFrameId.trimmed().isEmpty() || groupKey.trimmed().isEmpty())
        {
            return rollbackBatch(m_connection,
                                 QStringLiteral("matched-source batch validation failed at batch_item_index=%1 "
                                                "(frame_id='%2', group_key='%3', settings_version=%4): "
                                                "frame_id and group_key are required.")
                                     .arg(index)
                                     .arg(canonicalFrameId, groupKey)
                                     .arg(settingsVersion),
                                 error);
        }
        const auto [identityIt, inserted] = replacementIndexByIdentity.emplace(replacement.frameId, index);
        if (!inserted)
        {
            return rollbackBatch(
                m_connection,
                QStringLiteral("matched-source batch validation failed at batch_item_index=%1 "
                               "(frame_id='%2', group_key='%3', settings_version=%4): duplicate frame identity "
                               "already supplied at batch_item_index=%5.")
                    .arg(index)
                    .arg(canonicalFrameId, groupKey)
                    .arg(settingsVersion)
                    .arg(identityIt->second),
                error);
        }
        canonicalFrameIdByIdentity.emplace(replacement.frameId, canonicalFrameId);
        std::unordered_set<std::uint64_t> source_ids;
        source_ids.reserve(replacement.references.size());
        for (size_t referenceIndex = 0; referenceIndex < replacement.references.size(); ++referenceIndex)
        {
            const std::uint64_t source_id = replacement.references[referenceIndex].sourceIdentifier;
            if (source_id == 0u)
            {
                return rollbackBatch(
                    m_connection,
                    QStringLiteral("matched-source batch validation failed at batch_item_index=%1, "
                                   "reference_index=%2 (frame_id='%3', group_key='%4', settings_version=%5): "
                                   "gaia_source_id is required.")
                        .arg(index)
                        .arg(referenceIndex)
                        .arg(canonicalFrameId, groupKey)
                        .arg(settingsVersion),
                    error);
            }
            if (!source_ids.insert(source_id).second)
            {
                return rollbackBatch(
                    m_connection,
                    QStringLiteral("matched-source batch validation failed at batch_item_index=%1, "
                                   "reference_index=%2 (frame_id='%3', group_key='%4', settings_version=%5): "
                                   "duplicate gaia_source_id '%6'.")
                        .arg(index)
                        .arg(referenceIndex)
                        .arg(canonicalFrameId, groupKey)
                        .arg(settingsVersion)
                        .arg(sourceIdText(source_id)),
                    error);
            }
        }
    }

    struct StoredMatchedSourceIdentity
    {
        QString storedFrameId;
        size_t replacementIndex = 0;
    };
    std::vector<StoredMatchedSourceIdentity> storedIdentities;
    QSqlQuery identityIndexQuery(m_connection.db());
    if (!identityIndexQuery.prepare(QStringLiteral("SELECT DISTINCT frame_id FROM registration_matched_sources "
                                                   "WHERE group_key = :group_key AND settings_version = "
                                                   ":settings_version")))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("matched-source batch failed to prepare identity scan "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(identityIndexQuery.lastError().text()),
                             error);
    }
    identityIndexQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    identityIndexQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    if (!m_connection.execute(identityIndexQuery))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("matched-source batch identity scan failed "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(identityIndexQuery.lastError().text()),
                             error);
    }
    while (identityIndexQuery.next())
    {
        const QString storedFrameId = identityIndexQuery.value(0).toString();
        if (storedFrameId.trimmed().isEmpty())
        {
            continue;
        }
        try
        {
            const auto replacementIt = replacementIndexByIdentity.find(FrameId(storedFrameId));
            if (replacementIt != replacementIndexByIdentity.end())
            {
                storedIdentities.push_back({storedFrameId, replacementIt->second});
            }
        }
        catch (...)
        {
            continue;
        }
    }
    identityIndexQuery.finish();

    QSqlQuery deleteQuery(m_connection.db());
    if (!deleteQuery.prepare(QStringLiteral("DELETE FROM registration_matched_sources "
                                            "WHERE frame_id = :frame_id AND group_key = :group_key "
                                            "AND settings_version = :settings_version")))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("matched-source batch failed to prepare delete "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(deleteQuery.lastError().text()),
                             error);
    }

    for (const auto& storedIdentity : storedIdentities)
    {
        deleteQuery.bindValue(QStringLiteral(":frame_id"), storedIdentity.storedFrameId);
        deleteQuery.bindValue(QStringLiteral(":group_key"), groupKey);
        deleteQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
        if (!m_connection.execute(deleteQuery))
        {
            const QString failure = versionedBatchItemError(
                QStringLiteral("matched-source delete"), storedIdentity.replacementIndex,
                canonicalFrameIdByIdentity.at(replacements[storedIdentity.replacementIndex].frameId), groupKey,
                settingsVersion, deleteQuery.lastError().text());
            return rollbackBatch(m_connection, failure, error);
        }
    }

    QSqlQuery insertQuery(m_connection.db());
    if (!insertQuery.prepare(registrationMatchedSourceUpsertSql()))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("matched-source batch failed to prepare insert "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(insertQuery.lastError().text()),
                             error);
    }

    for (size_t index = 0; index < replacements.size(); ++index)
    {
        const auto& replacement = replacements[index];
        const QString canonicalFrameId = replacement.frameId.canonicalPath();
        for (size_t referenceIndex = 0; referenceIndex < replacement.references.size(); ++referenceIndex)
        {
            RegistrationMatchedSourceRecord record;
            record.frameId = replacement.frameId;
            record.groupKey = groupKey;
            record.settingsVersion = settingsVersion;
            record.reference = replacement.references[referenceIndex];
            record.updatedAt = replacement.updatedAt;
            bindRegistrationMatchedSource(insertQuery, record);
            if (!m_connection.execute(insertQuery))
            {
                const QString sqliteError =
                    QStringLiteral("reference_index=%1: %2").arg(referenceIndex).arg(insertQuery.lastError().text());
                const QString failure =
                    versionedBatchItemError(QStringLiteral("matched-source insert"), index, canonicalFrameId, groupKey,
                                            settingsVersion, sqliteError);
                return rollbackBatch(m_connection, failure, error);
            }
        }
    }

    if (!m_connection.commitTransaction())
    {
        const QString failure = QStringLiteral("matched-source batch failed to commit "
                                               "(group_key='%1', settings_version=%2): SQLite error: %3")
                                    .arg(groupKey)
                                    .arg(settingsVersion)
                                    .arg(m_connection.db().lastError().text());
        return rollbackBatch(m_connection, failure, error);
    }

    return true;
}

bool RegistrationMetricsWriter::updateAlignedOutputPathsForGroup(
    const QString& groupKey, const int settingsVersion, const std::vector<RegistrationAlignedOutputPathUpdate>& updates,
    QString* error, RegistrationAlignedOutputPathBatchDiagnostics* diagnostics)
{
    assignError(error, QString());
    if (diagnostics != nullptr)
    {
        *diagnostics = {};
    }
    if (updates.empty())
    {
        return true;
    }
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, QStringLiteral("aligned output-path batch failed to begin transaction "
                                          "(group_key='%1', settings_version=%2): SQLite error: %3")
                               .arg(groupKey)
                               .arg(settingsVersion)
                               .arg(m_connection.db().lastError().text()));
        return false;
    }

    std::unordered_map<FrameId, size_t> updateIndexByFrame;
    updateIndexByFrame.reserve(updates.size());
    for (size_t index = 0; index < updates.size(); ++index)
    {
        const QString canonicalFrameId = updates[index].frameId.canonicalPath();
        if (canonicalFrameId.trimmed().isEmpty() || groupKey.trimmed().isEmpty())
        {
            return rollbackBatch(m_connection,
                                 QStringLiteral("aligned output-path batch validation failed at batch_item_index=%1 "
                                                "(frame_id='%2', group_key='%3', settings_version=%4): "
                                                "frame_id and group_key are required.")
                                     .arg(index)
                                     .arg(canonicalFrameId, groupKey)
                                     .arg(settingsVersion),
                                 error);
        }
        const auto [existingUpdate, inserted] = updateIndexByFrame.emplace(updates[index].frameId, index);
        if (!inserted)
        {
            return rollbackBatch(
                m_connection,
                QStringLiteral("aligned output-path batch validation failed at batch_item_index=%1 "
                               "(frame_id='%2', group_key='%3', settings_version=%4): "
                               "duplicate logical frame update already appeared at batch_item_index=%5.")
                    .arg(index)
                    .arg(canonicalFrameId, groupKey)
                    .arg(settingsVersion)
                    .arg(existingUpdate->second),
                error);
        }
    }

    std::unordered_map<FrameId, std::vector<QString>> storedFrameIdsByCanonicalId;
    storedFrameIdsByCanonicalId.reserve(updates.size());
    QSqlQuery indexQuery(m_connection.db());
    if (!indexQuery.prepare(QStringLiteral("SELECT frame_id FROM registration_metrics "
                                           "WHERE group_key = :group_key AND settings_version = :settings_version")))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("aligned output-path batch failed to prepare canonical index scan "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(indexQuery.lastError().text()),
                             error);
    }
    indexQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    indexQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    if (!m_connection.execute(indexQuery))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("aligned output-path batch canonical index scan failed "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(indexQuery.lastError().text()),
                             error);
    }
    if (diagnostics != nullptr)
    {
        diagnostics->groupRowScans = 1;
    }

    while (indexQuery.next())
    {
        const QString storedFrameId = indexQuery.value(0).toString();
        if (storedFrameId.trimmed().isEmpty())
        {
            continue;
        }

        try
        {
            storedFrameIdsByCanonicalId[FrameId(storedFrameId)].push_back(storedFrameId);
            if (diagnostics != nullptr)
            {
                ++diagnostics->storedRowsIndexed;
            }
        }
        catch (...)
        {
            // An invalid stored key cannot safely satisfy a canonical update.
        }
    }

    if (diagnostics != nullptr)
    {
        for (const auto& [canonicalId, storedFrameIds] : storedFrameIdsByCanonicalId)
        {
            (void)canonicalId;
            if (storedFrameIds.size() > 1)
            {
                ++diagnostics->canonicalCollisions;
            }
        }
    }

    struct ResolvedAlignedOutputPathUpdate
    {
        size_t batchItemIndex = 0;
        QString canonicalFrameId;
        std::vector<QString> storedFrameIds;
        QString outputPath;
        qint64 outputSizeBytes = -1;
        qint64 outputModifiedAtMs = -1;
        QString outputIdentityToken;
    };

    std::vector<ResolvedAlignedOutputPathUpdate> resolvedUpdates;
    resolvedUpdates.reserve(updates.size());
    for (size_t index = 0; index < updates.size(); ++index)
    {
        const auto& update = updates[index];
        const QString canonicalFrameId = update.frameId.canonicalPath();
        if (diagnostics != nullptr)
        {
            ++diagnostics->canonicalLookups;
        }

        const auto storedIt = storedFrameIdsByCanonicalId.find(update.frameId);
        if (storedIt == storedFrameIdsByCanonicalId.end())
        {
            const QString failure = versionedBatchItemError(
                QStringLiteral("aligned output-path update"), index, canonicalFrameId, groupKey, settingsVersion,
                QStringLiteral("no registration_metrics row matched the canonical frame ID"));
            return rollbackBatch(m_connection, failure, error);
        }

        std::vector<QString> storedFrameIds = storedIt->second;
        std::sort(storedFrameIds.begin(), storedFrameIds.end());
        resolvedUpdates.push_back({index, canonicalFrameId, std::move(storedFrameIds), update.outputPath,
                                   update.outputSizeBytes, update.outputModifiedAtMs, update.outputIdentityToken});
        if (diagnostics != nullptr)
        {
            ++diagnostics->resolvedUpdates;
        }
    }

    QSqlQuery query(m_connection.db());
    if (!query.prepare(QStringLiteral("UPDATE registration_metrics SET output_path = :output_path, "
                                      "output_size_bytes = :output_size_bytes, "
                                      "output_modified_at_ms = :output_modified_at_ms, "
                                      "output_identity_token = :output_identity_token "
                                      "WHERE frame_id = :frame_id AND group_key = :group_key "
                                      "AND settings_version = :settings_version")))
    {
        return rollbackBatch(m_connection,
                             QStringLiteral("aligned output-path batch failed to prepare update "
                                            "(group_key='%1', settings_version=%2): SQLite error: %3")
                                 .arg(groupKey)
                                 .arg(settingsVersion)
                                 .arg(query.lastError().text()),
                             error);
    }

    for (const auto& update : resolvedUpdates)
    {
        for (const auto& storedFrameId : update.storedFrameIds)
        {
            query.bindValue(QStringLiteral(":output_path"), update.outputPath);
            query.bindValue(QStringLiteral(":output_size_bytes"), update.outputSizeBytes);
            query.bindValue(QStringLiteral(":output_modified_at_ms"), update.outputModifiedAtMs);
            query.bindValue(QStringLiteral(":output_identity_token"),
                            update.outputIdentityToken.isNull() ? QStringLiteral("") : update.outputIdentityToken);
            query.bindValue(QStringLiteral(":frame_id"), storedFrameId);
            query.bindValue(QStringLiteral(":group_key"), groupKey);
            query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
            if (!m_connection.execute(query))
            {
                const QString failure = versionedBatchItemError(QStringLiteral("aligned output-path update"),
                                                                update.batchItemIndex, update.canonicalFrameId,
                                                                groupKey, settingsVersion, query.lastError().text());
                return rollbackBatch(m_connection, failure, error);
            }
            if (query.numRowsAffected() <= 0)
            {
                const QString failure = versionedBatchItemError(
                    QStringLiteral("aligned output-path update"), update.batchItemIndex, update.canonicalFrameId,
                    groupKey, settingsVersion,
                    QStringLiteral("persisted row key '%1' was not updated").arg(storedFrameId));
                return rollbackBatch(m_connection, failure, error);
            }
        }
    }

    if (!m_connection.commitTransaction())
    {
        const QString failure = QStringLiteral("aligned output-path batch failed to commit "
                                               "(group_key='%1', settings_version=%2): SQLite error: %3")
                                    .arg(groupKey)
                                    .arg(settingsVersion)
                                    .arg(m_connection.db().lastError().text());
        return rollbackBatch(m_connection, failure, error);
    }

    return true;
}

bool RegistrationMetricsWriter::replaceMatchedSourcesForFrame(const QString& groupKey, const int settingsVersion,
                                                              const FrameId& frameId,
                                                              const std::vector<an::MatchedSourceReference>& references,
                                                              QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    const QString canonicalFrameId = frameId.canonicalPath();
    if (canonicalFrameId.trimmed().isEmpty() || groupKey.trimmed().isEmpty())
    {
        assignError(error, QStringLiteral("Matched source replacement requires frame_id and group_key."));
        return false;
    }
    std::unordered_set<std::uint64_t> sourceIds;
    sourceIds.reserve(references.size());
    for (size_t referenceIndex = 0; referenceIndex < references.size(); ++referenceIndex)
    {
        const std::uint64_t sourceId = references[referenceIndex].sourceIdentifier;
        if (sourceId == 0u)
        {
            assignError(error, QStringLiteral("Matched source replacement reference_index=%1 requires "
                                              "gaia_source_id.")
                                   .arg(referenceIndex));
            return false;
        }
        if (!sourceIds.insert(sourceId).second)
        {
            assignError(error, QStringLiteral("Matched source replacement reference_index=%1 has duplicate "
                                              "gaia_source_id '%2'.")
                                   .arg(referenceIndex)
                                   .arg(sourceIdText(sourceId)));
            return false;
        }
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        return false;
    }

    std::vector<QString> storedFrameIds;
    QSqlQuery identityIndexQuery(m_connection.db());
    identityIndexQuery.prepare(QStringLiteral("SELECT DISTINCT frame_id FROM registration_matched_sources "
                                              "WHERE group_key = :group_key AND settings_version = "
                                              ":settings_version"));
    identityIndexQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    identityIndexQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    if (!m_connection.execute(identityIndexQuery))
    {
        assignError(error, identityIndexQuery.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }
    while (identityIndexQuery.next())
    {
        const QString storedFrameId = identityIndexQuery.value(0).toString();
        if (storedFrameId.trimmed().isEmpty())
        {
            continue;
        }
        try
        {
            if (FrameId(storedFrameId) == frameId)
            {
                storedFrameIds.push_back(storedFrameId);
            }
        }
        catch (...)
        {
            continue;
        }
    }
    identityIndexQuery.finish();

    QSqlQuery deleteQuery(m_connection.db());
    deleteQuery.prepare(QStringLiteral("DELETE FROM registration_matched_sources "
                                       "WHERE frame_id = :frame_id AND group_key = :group_key "
                                       "AND settings_version = :settings_version"));
    for (const auto& storedFrameId : storedFrameIds)
    {
        deleteQuery.bindValue(QStringLiteral(":frame_id"), storedFrameId);
        deleteQuery.bindValue(QStringLiteral(":group_key"), groupKey);
        deleteQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
        if (!m_connection.execute(deleteQuery))
        {
            assignError(error, deleteQuery.lastError().text());
            (void)m_connection.rollbackTransaction();
            return false;
        }
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSqlQuery insertQuery(m_connection.db());
    if (!insertQuery.prepare(registrationMatchedSourceUpsertSql()))
    {
        assignError(error, insertQuery.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }
    for (const auto& reference : references)
    {
        RegistrationMatchedSourceRecord record;
        record.frameId = frameId;
        record.groupKey = groupKey;
        record.settingsVersion = settingsVersion;
        record.reference = reference;
        record.updatedAt = now;
        bindRegistrationMatchedSource(insertQuery, record);
        if (!m_connection.execute(insertQuery))
        {
            assignError(error, insertQuery.lastError().text());
            (void)m_connection.rollbackTransaction();
            return false;
        }
    }

    if (!m_connection.commitTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertCometTrackingRun(const CometTrackingRunRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (record.status == QStringLiteral("Active"))
    {
        QSqlQuery activeQuery(m_connection.db());
        activeQuery.prepare(QStringLiteral("SELECT comet_run_id FROM comet_tracking_runs "
                                           "WHERE group_key = :group_key AND registration_hash = :registration_hash "
                                           "AND light_files_hash = :light_files_hash "
                                           "AND calibration_hash = :calibration_hash "
                                           "AND resolved_reference_frame_id = :resolved_reference_frame_id "
                                           "AND status = 'Active' AND comet_run_id <> :comet_run_id LIMIT 1"));
        activeQuery.bindValue(QStringLiteral(":group_key"), record.groupKey);
        activeQuery.bindValue(QStringLiteral(":registration_hash"), record.registrationHash);
        activeQuery.bindValue(QStringLiteral(":light_files_hash"), record.lightFilesHash);
        activeQuery.bindValue(QStringLiteral(":calibration_hash"), record.calibrationHash);
        activeQuery.bindValue(QStringLiteral(":resolved_reference_frame_id"), record.resolvedReferenceFrameId);
        activeQuery.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
        if (!m_connection.execute(activeQuery))
        {
            assignError(error, activeQuery.lastError().text());
            return false;
        }
        if (activeQuery.next())
        {
            assignError(error, QStringLiteral("UNIQUE active comet run context would be violated."));
            return false;
        }
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO comet_tracking_runs ("
                                 "comet_run_id, group_key, settings_version, comet_context_id, detection_hash, "
                                 "registration_hash, light_files_hash, calibration_hash, resolved_reference_frame_id, "
                                 "reference_epoch, mode, status, created_at, updated_at) "
                                 "VALUES (:comet_run_id, :group_key, :settings_version, :comet_context_id, "
                                 ":detection_hash, :registration_hash, :light_files_hash, :calibration_hash, "
                                 ":resolved_reference_frame_id, :reference_epoch, :mode, :status, :created_at, "
                                 ":updated_at)"));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":group_key"), record.groupKey);
    query.bindValue(QStringLiteral(":settings_version"), record.settingsVersion);
    query.bindValue(QStringLiteral(":comet_context_id"), record.cometContextId);
    query.bindValue(QStringLiteral(":detection_hash"), record.detectionHash);
    query.bindValue(QStringLiteral(":registration_hash"), record.registrationHash);
    query.bindValue(QStringLiteral(":light_files_hash"), record.lightFilesHash);
    query.bindValue(QStringLiteral(":calibration_hash"), record.calibrationHash);
    query.bindValue(QStringLiteral(":resolved_reference_frame_id"), record.resolvedReferenceFrameId);
    query.bindValue(QStringLiteral(":reference_epoch"), record.referenceEpoch);
    query.bindValue(QStringLiteral(":mode"), record.mode);
    query.bindValue(QStringLiteral(":status"), record.status);
    query.bindValue(QStringLiteral(":created_at"), timestampOrNow(record.createdAt));
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertCometTrackingRunSummary(const CometTrackingRunSummaryRecord& record,
                                                              QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO comet_tracking_run_summary ("
                                 "comet_run_id, fit_model, reference_epoch, fit_time_origin, rms_pixels, "
                                 "max_residual_pixels, accepted_input_count, rejected_input_count, "
                                 "predicted_only_frame_count, warning_flags, updated_at) "
                                 "VALUES (:comet_run_id, :fit_model, :reference_epoch, :fit_time_origin, "
                                 ":rms_pixels, :max_residual_pixels, :accepted_input_count, :rejected_input_count, "
                                 ":predicted_only_frame_count, :warning_flags, :updated_at)"));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":fit_model"), record.fitModel);
    query.bindValue(QStringLiteral(":reference_epoch"), record.referenceEpoch);
    query.bindValue(QStringLiteral(":fit_time_origin"), record.fitTimeOrigin);
    query.bindValue(QStringLiteral(":rms_pixels"), record.rmsPixels);
    query.bindValue(QStringLiteral(":max_residual_pixels"), record.maxResidualPixels);
    query.bindValue(QStringLiteral(":accepted_input_count"), record.acceptedInputCount);
    query.bindValue(QStringLiteral(":rejected_input_count"), record.rejectedInputCount);
    query.bindValue(QStringLiteral(":predicted_only_frame_count"), record.predictedOnlyFrameCount);
    query.bindValue(QStringLiteral(":warning_flags"), record.warningFlags);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertCometTrackingPoint(const CometTrackingPointRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO comet_tracking_points ("
                                 "point_id, comet_run_id, original_frame_id, refinement_source_path, epoch, "
                                 "source_x, source_y, transform_domain_x, transform_domain_y, predicted_source_x, "
                                 "predicted_source_y, reference_plane_x, reference_plane_y, "
                                 "predicted_reference_plane_x, predicted_reference_plane_y, confidence, "
                                 "residual_pixels, measured_fwhm_pixels, measured_diameter_pixels, "
                                 "measured_ellipticity, aperture_radius_pixels, local_background, snr, roi_x, "
                                 "roi_y, roi_width, roi_height, measurement_method, origin, fit_state, "
                                 "rejection_reason, updated_at) "
                                 "VALUES (:point_id, :comet_run_id, :original_frame_id, :refinement_source_path, "
                                 ":epoch, :source_x, :source_y, :transform_domain_x, :transform_domain_y, "
                                 ":predicted_source_x, :predicted_source_y, :reference_plane_x, :reference_plane_y, "
                                 ":predicted_reference_plane_x, :predicted_reference_plane_y, :confidence, "
                                 ":residual_pixels, :measured_fwhm_pixels, :measured_diameter_pixels, "
                                 ":measured_ellipticity, :aperture_radius_pixels, :local_background, :snr, "
                                 ":roi_x, :roi_y, :roi_width, :roi_height, :measurement_method, :origin, "
                                 ":fit_state, :rejection_reason, :updated_at)"));
    query.bindValue(QStringLiteral(":point_id"), record.pointId);
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":original_frame_id"), record.originalFrameId.canonicalPath());
    query.bindValue(QStringLiteral(":refinement_source_path"), record.refinementSourcePath);
    query.bindValue(QStringLiteral(":epoch"), record.epoch);
    query.bindValue(QStringLiteral(":source_x"), record.sourcePosition.x());
    query.bindValue(QStringLiteral(":source_y"), record.sourcePosition.y());
    query.bindValue(QStringLiteral(":transform_domain_x"), record.transformDomainPosition.x());
    query.bindValue(QStringLiteral(":transform_domain_y"), record.transformDomainPosition.y());
    query.bindValue(QStringLiteral(":predicted_source_x"), record.predictedSourcePosition.x());
    query.bindValue(QStringLiteral(":predicted_source_y"), record.predictedSourcePosition.y());
    query.bindValue(QStringLiteral(":reference_plane_x"), record.referencePlanePosition.x());
    query.bindValue(QStringLiteral(":reference_plane_y"), record.referencePlanePosition.y());
    query.bindValue(QStringLiteral(":predicted_reference_plane_x"), record.predictedReferencePlanePosition.x());
    query.bindValue(QStringLiteral(":predicted_reference_plane_y"), record.predictedReferencePlanePosition.y());
    query.bindValue(QStringLiteral(":confidence"), record.confidence);
    query.bindValue(QStringLiteral(":residual_pixels"), record.residualPixels);
    query.bindValue(QStringLiteral(":measured_fwhm_pixels"), record.measuredFwhmPixels);
    query.bindValue(QStringLiteral(":measured_diameter_pixels"), record.measuredDiameterPixels);
    query.bindValue(QStringLiteral(":measured_ellipticity"), record.measuredEllipticity);
    query.bindValue(QStringLiteral(":aperture_radius_pixels"), record.apertureRadiusPixels);
    query.bindValue(QStringLiteral(":local_background"), record.localBackground);
    query.bindValue(QStringLiteral(":snr"), record.snr);
    query.bindValue(QStringLiteral(":roi_x"), record.roiX);
    query.bindValue(QStringLiteral(":roi_y"), record.roiY);
    query.bindValue(QStringLiteral(":roi_width"), record.roiWidth);
    query.bindValue(QStringLiteral(":roi_height"), record.roiHeight);
    query.bindValue(QStringLiteral(":measurement_method"), record.measurementMethod);
    query.bindValue(QStringLiteral(":origin"), record.origin);
    query.bindValue(QStringLiteral(":fit_state"), record.fitState);
    query.bindValue(QStringLiteral(":rejection_reason"), record.rejectionReason);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertCometTransformRole(const CometTransformRoleRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO comet_transform_roles ("
                                 "comet_run_id, frame_id, role, comet_output_plane_id, transform_json, "
                                 "source_distortion_json, succeeded, failure_reason, updated_at) "
                                 "VALUES (:comet_run_id, :frame_id, :role, :comet_output_plane_id, "
                                 ":transform_json, :source_distortion_json, :succeeded, :failure_reason, "
                                 ":updated_at)"));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":frame_id"), record.frameId.canonicalPath());
    query.bindValue(QStringLiteral(":role"), record.role);
    query.bindValue(QStringLiteral(":comet_output_plane_id"), record.cometOutputPlaneId);
    query.bindValue(QStringLiteral(":transform_json"), record.transformJson);
    query.bindValue(QStringLiteral(":source_distortion_json"), record.sourceDistortionJson);
    query.bindValue(QStringLiteral(":succeeded"), record.succeeded ? 1 : 0);
    query.bindValue(QStringLiteral(":failure_reason"), record.failureReason);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertCometOutputPlane(const CometOutputPlaneRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO comet_output_planes ("
                                 "comet_output_plane_id, comet_run_id, role, alignment_mode, "
                                 "coordinate_frame_kind, canvas_width, canvas_height, canvas_offset_x, "
                                 "canvas_offset_y, sky_wcs_valid, sky_wcs_invalid_reason, descriptor_json, "
                                 "source_fingerprint, updated_at) "
                                 "VALUES (:comet_output_plane_id, :comet_run_id, :role, :alignment_mode, "
                                 ":coordinate_frame_kind, :canvas_width, :canvas_height, :canvas_offset_x, "
                                 ":canvas_offset_y, :sky_wcs_valid, :sky_wcs_invalid_reason, :descriptor_json, "
                                 ":source_fingerprint, :updated_at)"));
    query.bindValue(QStringLiteral(":comet_output_plane_id"), record.cometOutputPlaneId);
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":role"), record.role);
    query.bindValue(QStringLiteral(":alignment_mode"), record.alignmentMode);
    query.bindValue(QStringLiteral(":coordinate_frame_kind"), record.coordinateFrameKind);
    query.bindValue(QStringLiteral(":canvas_width"), record.canvasWidth);
    query.bindValue(QStringLiteral(":canvas_height"), record.canvasHeight);
    query.bindValue(QStringLiteral(":canvas_offset_x"), record.canvasOffsetX);
    query.bindValue(QStringLiteral(":canvas_offset_y"), record.canvasOffsetY);
    query.bindValue(QStringLiteral(":sky_wcs_valid"), record.skyWcsValid ? 1 : 0);
    query.bindValue(QStringLiteral(":sky_wcs_invalid_reason"), record.skyWcsInvalidReason);
    query.bindValue(QStringLiteral(":descriptor_json"), record.descriptorJson);
    query.bindValue(QStringLiteral(":source_fingerprint"), record.sourceFingerprint);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertCometRegisteredOutput(const CometRegisteredOutputRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    qint64 output_size_bytes = record.outputSizeBytes;
    qint64 output_modified_at_ms = record.outputModifiedAtMs;
    QString output_identity_token = record.outputIdentityToken;
    if (record.succeeded && (output_size_bytes < 0 || output_modified_at_ms < 0 || output_identity_token.isEmpty()))
    {
        const QFileInfo output_info(record.outputPath);
        if (output_info.isFile())
        {
            output_size_bytes = output_info.size();
            output_modified_at_ms = output_info.lastModified().toMSecsSinceEpoch();
            output_identity_token = ArtifactTracker::fileIdentityForPath(output_info.absoluteFilePath());
        }
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO comet_registered_outputs ("
                                 "comet_run_id, frame_id, role, comet_output_plane_id, output_path, artifact_hash, "
                                 "output_size_bytes, output_modified_at_ms, output_identity_token, succeeded, "
                                 "failure_reason, updated_at) "
                                 "VALUES (:comet_run_id, :frame_id, :role, :comet_output_plane_id, :output_path, "
                                 ":artifact_hash, :output_size_bytes, :output_modified_at_ms, "
                                 ":output_identity_token, :succeeded, :failure_reason, :updated_at)"));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":frame_id"), record.frameId.canonicalPath());
    query.bindValue(QStringLiteral(":role"), record.role);
    query.bindValue(QStringLiteral(":comet_output_plane_id"), record.cometOutputPlaneId);
    query.bindValue(QStringLiteral(":output_path"), record.outputPath);
    query.bindValue(QStringLiteral(":artifact_hash"), record.artifactHash);
    query.bindValue(QStringLiteral(":output_size_bytes"), output_size_bytes);
    query.bindValue(QStringLiteral(":output_modified_at_ms"), output_modified_at_ms);
    query.bindValue(QStringLiteral(":output_identity_token"), output_identity_token);
    query.bindValue(QStringLiteral(":succeeded"), record.succeeded ? 1 : 0);
    query.bindValue(QStringLiteral(":failure_reason"), record.failureReason);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertCometRoleReadiness(const CometRoleReadinessRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO comet_role_readiness ("
                                 "comet_run_id, role, readiness_status, selected_frame_count, "
                                 "successful_transform_count, failed_transform_count, missing_transform_count, "
                                 "output_plane_status, requires_materialized_outputs, materialized_output_count, "
                                 "missing_output_count, failed_output_count, failure_reason, updated_at) "
                                 "VALUES (:comet_run_id, :role, :readiness_status, :selected_frame_count, "
                                 ":successful_transform_count, :failed_transform_count, :missing_transform_count, "
                                 ":output_plane_status, :requires_materialized_outputs, "
                                 ":materialized_output_count, :missing_output_count, :failed_output_count, "
                                 ":failure_reason, :updated_at)"));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":role"), record.role);
    query.bindValue(QStringLiteral(":readiness_status"), record.readinessStatus);
    query.bindValue(QStringLiteral(":selected_frame_count"), record.selectedFrameCount);
    query.bindValue(QStringLiteral(":successful_transform_count"), record.successfulTransformCount);
    query.bindValue(QStringLiteral(":failed_transform_count"), record.failedTransformCount);
    query.bindValue(QStringLiteral(":missing_transform_count"), record.missingTransformCount);
    query.bindValue(QStringLiteral(":output_plane_status"), record.outputPlaneStatus);
    query.bindValue(QStringLiteral(":requires_materialized_outputs"), record.requiresMaterializedOutputs ? 1 : 0);
    query.bindValue(QStringLiteral(":materialized_output_count"), record.materializedOutputCount);
    query.bindValue(QStringLiteral(":missing_output_count"), record.missingOutputCount);
    query.bindValue(QStringLiteral(":failed_output_count"), record.failedOutputCount);
    query.bindValue(QStringLiteral(":failure_reason"), record.failureReason);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::upsertStackingCometArtifact(const StackingCometArtifactRecord& record, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }
    if (!isStackingCometArtifactKind(record.artifactKind))
    {
        assignError(error, QStringLiteral("Invalid comet stacking artifact kind '%1'.").arg(record.artifactKind));
        return false;
    }
    if (record.cometRunId.trimmed().isEmpty() || record.integrationGroupKey.trimmed().isEmpty() ||
        record.stackingHash.trimmed().isEmpty() || record.starBaseOutputDescriptorId.trimmed().isEmpty() ||
        record.cometOutputPlaneId.trimmed().isEmpty())
    {
        assignError(error, QStringLiteral("Comet stacking artifacts require comet run, integration group, stacking "
                                          "hash, descriptor, and comet output plane identities."));
        return false;
    }
    if (record.succeeded && record.outputPath.trimmed().isEmpty())
    {
        assignError(
            error,
            QStringLiteral("Succeeded comet stacking artifact '%1' requires an output path.").arg(record.artifactKind));
        return false;
    }
    if (record.succeeded && record.artifactHash.trimmed().isEmpty())
    {
        assignError(error, QStringLiteral("Succeeded comet stacking artifact '%1' requires an artifact hash.")
                               .arg(record.artifactKind));
        return false;
    }
    if (!record.outputPath.trimmed().isEmpty())
    {
        QString conflictKind;
        if (!findStackingCometArtifactConflict(m_connection.db(), record, QStringLiteral("output_path"),
                                               record.outputPath, conflictKind, error,
                                               m_connection.counterAccumulator()))
        {
            return false;
        }
        if (!conflictKind.isEmpty())
        {
            assignError(error, QStringLiteral("Comet stacking artifacts must use kind-qualified output paths; "
                                              "'%1' already uses output path '%2'.")
                                   .arg(conflictKind, record.outputPath));
            return false;
        }
    }
    if (!record.artifactHash.trimmed().isEmpty())
    {
        QString conflictKind;
        if (!findStackingCometArtifactConflict(m_connection.db(), record, QStringLiteral("artifact_hash"),
                                               record.artifactHash, conflictKind, error,
                                               m_connection.counterAccumulator()))
        {
            return false;
        }
        if (!conflictKind.isEmpty())
        {
            assignError(error, QStringLiteral("Comet stacking artifacts must use kind-qualified artifact "
                                              "hashes; '%1' already uses artifact hash '%2'.")
                                   .arg(conflictKind, record.artifactHash));
            return false;
        }
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO stacking_comet_artifacts ("
                                 "comet_run_id, integration_group_key, stacking_hash, "
                                 "star_base_output_descriptor_id, comet_output_plane_id, artifact_kind, output_path, "
                                 "artifact_hash, mask_settings_fingerprint, succeeded, failure_reason, updated_at) "
                                 "VALUES (:comet_run_id, :integration_group_key, :stacking_hash, "
                                 ":star_base_output_descriptor_id, :comet_output_plane_id, :artifact_kind, "
                                 ":output_path, :artifact_hash, :mask_settings_fingerprint, :succeeded, "
                                 ":failure_reason, :updated_at)"));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":integration_group_key"), record.integrationGroupKey);
    query.bindValue(QStringLiteral(":stacking_hash"), record.stackingHash);
    query.bindValue(QStringLiteral(":star_base_output_descriptor_id"), record.starBaseOutputDescriptorId);
    query.bindValue(QStringLiteral(":comet_output_plane_id"), record.cometOutputPlaneId);
    query.bindValue(QStringLiteral(":artifact_kind"), record.artifactKind);
    query.bindValue(QStringLiteral(":output_path"), record.outputPath);
    query.bindValue(QStringLiteral(":artifact_hash"), record.artifactHash);
    query.bindValue(QStringLiteral(":mask_settings_fingerprint"), record.maskSettingsFingerprint);
    query.bindValue(QStringLiteral(":succeeded"), record.succeeded ? 1 : 0);
    query.bindValue(QStringLiteral(":failure_reason"), record.failureReason);
    query.bindValue(QStringLiteral(":updated_at"), timestampOrNow(record.updatedAt));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::replaceStackingCometArtifacts(const std::vector<StackingCometArtifactRecord>& records,
                                                              QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }
    if (records.empty())
    {
        return true;
    }

    const auto& first = records.front();
    for (const auto& record : records)
    {
        if (record.cometRunId != first.cometRunId || record.integrationGroupKey != first.integrationGroupKey ||
            record.stackingHash != first.stackingHash || record.cometOutputPlaneId != first.cometOutputPlaneId)
        {
            assignError(error, QStringLiteral("Replacing comet stacking artifacts requires one comet run, "
                                              "integration group, stacking hash, and output plane context."));
            return false;
        }
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        return false;
    }

    if (!deleteStaleStackingCometArtifacts(first.cometRunId, first.integrationGroupKey, first.stackingHash, error))
    {
        (void)m_connection.rollbackTransaction();
        return false;
    }

    for (const auto& record : records)
    {
        if (!upsertStackingCometArtifact(record, error))
        {
            (void)m_connection.rollbackTransaction();
            return false;
        }
    }

    if (!m_connection.commitTransaction())
    {
        const QString commit_error = m_connection.db().lastError().text();
        (void)m_connection.rollbackTransaction();
        assignError(error, commit_error);
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::deleteStaleStackingCometArtifacts(const QString& cometRunId,
                                                                  const QString& integrationGroupKey,
                                                                  const QString& currentStackingHash, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }
    if (cometRunId.trimmed().isEmpty() || integrationGroupKey.trimmed().isEmpty() ||
        currentStackingHash.trimmed().isEmpty())
    {
        assignError(error, QStringLiteral("Deleting stale comet artifacts requires comet run, integration group, and "
                                          "current stacking hash identities."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("DELETE FROM stacking_comet_artifacts "
                                 "WHERE comet_run_id = :comet_run_id "
                                 "AND integration_group_key = :integration_group_key "
                                 "AND stacking_hash <> :current_stacking_hash"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    query.bindValue(QStringLiteral(":integration_group_key"), integrationGroupKey);
    query.bindValue(QStringLiteral(":current_stacking_hash"), currentStackingHash);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::activateCometTrackingRun(const QString& cometRunId, const QStringList& requiredRoles,
                                                         const bool requireMaterializedOutputs, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }
    if (requiredRoles.isEmpty())
    {
        assignError(error, QStringLiteral("Cannot activate comet run '%1' without required roles.").arg(cometRunId));
        return false;
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        return false;
    }

    const auto fail = [&](const QString& message) -> bool {
        assignError(error, message);
        (void)m_connection.rollbackTransaction();
        return false;
    };

    QSqlQuery runQuery(m_connection.db());
    runQuery.prepare(QStringLiteral("SELECT group_key, registration_hash, light_files_hash, calibration_hash, "
                                    "resolved_reference_frame_id, status "
                                    "FROM comet_tracking_runs WHERE comet_run_id = :comet_run_id"));
    runQuery.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    if (!m_connection.execute(runQuery))
    {
        return fail(runQuery.lastError().text());
    }
    if (!runQuery.next())
    {
        return fail(QStringLiteral("Comet run '%1' was not found.").arg(cometRunId));
    }

    const QString groupKey = runQuery.value(0).toString();
    const QString registrationHash = runQuery.value(1).toString();
    const QString lightFilesHash = runQuery.value(2).toString();
    const QString calibrationHash = runQuery.value(3).toString();
    const QString resolvedReferenceFrameId = runQuery.value(4).toString();
    const QString status = runQuery.value(5).toString();
    if (status != QStringLiteral("RegistrationReady") && status != QStringLiteral("Active"))
    {
        return fail(QStringLiteral("Comet run '%1' cannot be activated from status '%2'.").arg(cometRunId, status));
    }

    for (const auto& role : requiredRoles)
    {
        QString readinessError;
        if (!ensureCometRoleReady(m_connection.db(), cometRunId, role, requireMaterializedOutputs, &readinessError,
                                  m_connection.counterAccumulator()))
        {
            return fail(readinessError);
        }
    }

    const qint64 now = timestampOrNow(0);

    QSqlQuery supersedeQuery(m_connection.db());
    supersedeQuery.prepare(QStringLiteral("UPDATE comet_tracking_runs SET status = 'Superseded', updated_at = :now "
                                          "WHERE group_key = :group_key "
                                          "AND registration_hash = :registration_hash "
                                          "AND light_files_hash = :light_files_hash "
                                          "AND calibration_hash = :calibration_hash "
                                          "AND resolved_reference_frame_id = :resolved_reference_frame_id "
                                          "AND status = 'Active' AND comet_run_id <> :comet_run_id"));
    supersedeQuery.bindValue(QStringLiteral(":now"), now);
    supersedeQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    supersedeQuery.bindValue(QStringLiteral(":registration_hash"), registrationHash);
    supersedeQuery.bindValue(QStringLiteral(":light_files_hash"), lightFilesHash);
    supersedeQuery.bindValue(QStringLiteral(":calibration_hash"), calibrationHash);
    supersedeQuery.bindValue(QStringLiteral(":resolved_reference_frame_id"), resolvedReferenceFrameId);
    supersedeQuery.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    if (!m_connection.execute(supersedeQuery))
    {
        return fail(supersedeQuery.lastError().text());
    }

    QSqlQuery activateQuery(m_connection.db());
    activateQuery.prepare(QStringLiteral("UPDATE comet_tracking_runs SET status = 'Active', updated_at = :now "
                                         "WHERE comet_run_id = :comet_run_id"));
    activateQuery.bindValue(QStringLiteral(":now"), now);
    activateQuery.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    if (!m_connection.execute(activateQuery))
    {
        return fail(activateQuery.lastError().text());
    }

    if (!m_connection.commitTransaction())
    {
        const QString commitError = m_connection.db().lastError().text();
        (void)m_connection.rollbackTransaction();
        assignError(error, commitError);
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::copyMetricsToSettingsVersion(const QString& groupKey, int fromSettingsVersion,
                                                             int toSettingsVersion, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (fromSettingsVersion == toSettingsVersion)
    {
        return true;
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        return false;
    }

    // NOTE: updated_at is intentionally inherited from the source rows rather than refreshed.
    // This preserves the original registration timestamp so future readers can distinguish
    // "genuinely re-registered" rows from "version-label-only migrations".
    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO registration_metrics ("
        "frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
        "background_noise, global_mad, global_scale, "
        "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
        "limiting_magnitude, catalog_matched_stars, "
        "succeeded, output_path, output_size_bytes, output_modified_at_ms, output_identity_token, "
        "transform_json, updated_at) "
        "SELECT frame_id, group_key, :to_settings_version, stars, fwhm, snr, eccentricity, "
        "background_noise, global_mad, global_scale, "
        "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
        "limiting_magnitude, catalog_matched_stars, "
        "succeeded, output_path, output_size_bytes, output_modified_at_ms, output_identity_token, "
        "transform_json, updated_at "
        "FROM registration_metrics WHERE group_key = :group_key AND settings_version = :from_settings_version"));
    query.bindValue(QStringLiteral(":to_settings_version"), toSettingsVersion);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":from_settings_version"), fromSettingsVersion);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO registration_matched_sources ("
        "frame_id, group_key, settings_version, gaia_source_id, astrometric_catalog_family_id, "
        "astrometric_catalog_manifest_id, spcc_catalog_family_id, spcc_catalog_manifest_id, wcs_solution_id, "
        "observation_epoch, proper_motion_model_version, match_separation_arcsec, residual_px, lookup_status, "
        "updated_at) "
        "SELECT frame_id, group_key, :to_settings_version, gaia_source_id, astrometric_catalog_family_id, "
        "astrometric_catalog_manifest_id, spcc_catalog_family_id, spcc_catalog_manifest_id, wcs_solution_id, "
        "observation_epoch, proper_motion_model_version, match_separation_arcsec, residual_px, lookup_status, "
        "updated_at "
        "FROM registration_matched_sources WHERE group_key = :group_key AND settings_version = "
        ":from_settings_version"));
    query.bindValue(QStringLiteral(":to_settings_version"), toSettingsVersion);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":from_settings_version"), fromSettingsVersion);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    if (!m_connection.commitTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    return true;
}

bool RegistrationMetricsWriter::deleteAllForGroup(const QString& groupKey, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (!m_connection.beginTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        return false;
    }

    QSqlQuery query(m_connection.db());

    const QStringList cometChildTables = {
        QStringLiteral("comet_tracking_run_summary"), QStringLiteral("comet_tracking_points"),
        QStringLiteral("comet_transform_roles"),      QStringLiteral("comet_output_planes"),
        QStringLiteral("comet_registered_outputs"),   QStringLiteral("comet_role_readiness"),
        QStringLiteral("stacking_comet_artifacts")};
    for (const auto& tableName : cometChildTables)
    {
        query.prepare(QStringLiteral("DELETE FROM %1 WHERE comet_run_id IN ("
                                     "SELECT comet_run_id FROM comet_tracking_runs WHERE group_key = :group_key)")
                          .arg(tableName));
        query.bindValue(QStringLiteral(":group_key"), groupKey);
        if (!m_connection.execute(query))
        {
            assignError(error, query.lastError().text());
            (void)m_connection.rollbackTransaction();
            return false;
        }
    }

    query.prepare(QStringLiteral("DELETE FROM comet_tracking_runs WHERE group_key = :group_key"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    query.prepare(QStringLiteral("DELETE FROM registration_metrics WHERE group_key = :group_key"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    query.prepare(QStringLiteral("DELETE FROM registration_group_state WHERE group_key = :group_key"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    query.prepare(QStringLiteral("DELETE FROM star_detection_cache WHERE group_key = :group_key"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    query.prepare(QStringLiteral("DELETE FROM registration_matched_sources WHERE group_key = :group_key"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    if (!m_connection.commitTransaction())
    {
        assignError(error, m_connection.db().lastError().text());
        (void)m_connection.rollbackTransaction();
        return false;
    }

    return true;
}
} // namespace an::ui
