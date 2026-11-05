// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/core/SqliteConnection.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>

namespace stage_utils
{
/// The format the registration metrics database states in `PRAGMA user_version` and is written in: the tables below,
/// every position in engine pixel coordinates - transform records at format 5, comet points, distortions and plane
/// descriptors with the first pixel's centre at (0.5, 0.5).
inline constexpr int kRegistrationMetricsSchemaVersion = 11;

[[nodiscard]] inline QStringList registrationMetricsColumns()
{
    return {QStringLiteral("frame_id"),
            QStringLiteral("group_key"),
            QStringLiteral("settings_version"),
            QStringLiteral("stars"),
            QStringLiteral("fwhm"),
            QStringLiteral("snr"),
            QStringLiteral("eccentricity"),
            QStringLiteral("background_noise"),
            QStringLiteral("global_mad"),
            QStringLiteral("global_scale"),
            QStringLiteral("photometric_zero_point"),
            QStringLiteral("zero_point_scatter"),
            QStringLiteral("astrometric_rms_pixels"),
            QStringLiteral("astrometric_rms_arcsec"),
            QStringLiteral("limiting_magnitude"),
            QStringLiteral("catalog_matched_stars"),
            QStringLiteral("succeeded"),
            QStringLiteral("output_path"),
            QStringLiteral("output_size_bytes"),
            QStringLiteral("output_modified_at_ms"),
            QStringLiteral("output_identity_token"),
            QStringLiteral("transform_json"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList registrationGroupStateColumns()
{
    return {
        QStringLiteral("group_key"),         QStringLiteral("settings_version"),   QStringLiteral("detection_hash"),
        QStringLiteral("registration_hash"), QStringLiteral("reference_frame_id"), QStringLiteral("light_files_hash"),
        QStringLiteral("calibration_hash"),  QStringLiteral("output_wcs_json"),    QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList starDetectionCacheColumns()
{
    return {QStringLiteral("frame_id"),
            QStringLiteral("group_key"),
            QStringLiteral("detection_hash"),
            QStringLiteral("input_path"),
            QStringLiteral("input_mtime"),
            QStringLiteral("input_size_bytes"),
            QStringLiteral("input_modified_at_ms"),
            QStringLiteral("input_identity_token"),
            QStringLiteral("stars_count"),
            QStringLiteral("fwhm"),
            QStringLiteral("snr"),
            QStringLiteral("roundness"),
            QStringLiteral("background_noise"),
            QStringLiteral("global_mad"),
            QStringLiteral("global_scale"),
            QStringLiteral("stars_blob"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList registrationMatchedSourcesColumns()
{
    return {QStringLiteral("frame_id"),
            QStringLiteral("group_key"),
            QStringLiteral("settings_version"),
            QStringLiteral("gaia_source_id"),
            QStringLiteral("astrometric_catalog_family_id"),
            QStringLiteral("astrometric_catalog_manifest_id"),
            QStringLiteral("spcc_catalog_family_id"),
            QStringLiteral("spcc_catalog_manifest_id"),
            QStringLiteral("wcs_solution_id"),
            QStringLiteral("observation_epoch"),
            QStringLiteral("proper_motion_model_version"),
            QStringLiteral("match_separation_arcsec"),
            QStringLiteral("residual_px"),
            QStringLiteral("lookup_status"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometTrackingRunsColumns()
{
    return {QStringLiteral("comet_run_id"),
            QStringLiteral("group_key"),
            QStringLiteral("settings_version"),
            QStringLiteral("comet_context_id"),
            QStringLiteral("detection_hash"),
            QStringLiteral("registration_hash"),
            QStringLiteral("light_files_hash"),
            QStringLiteral("calibration_hash"),
            QStringLiteral("resolved_reference_frame_id"),
            QStringLiteral("reference_epoch"),
            QStringLiteral("mode"),
            QStringLiteral("status"),
            QStringLiteral("created_at"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometTrackingRunSummaryColumns()
{
    return {QStringLiteral("comet_run_id"),
            QStringLiteral("fit_model"),
            QStringLiteral("reference_epoch"),
            QStringLiteral("fit_time_origin"),
            QStringLiteral("rms_pixels"),
            QStringLiteral("max_residual_pixels"),
            QStringLiteral("accepted_input_count"),
            QStringLiteral("rejected_input_count"),
            QStringLiteral("predicted_only_frame_count"),
            QStringLiteral("warning_flags"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometTrackingPointsColumns()
{
    return {QStringLiteral("point_id"),
            QStringLiteral("comet_run_id"),
            QStringLiteral("original_frame_id"),
            QStringLiteral("refinement_source_path"),
            QStringLiteral("epoch"),
            QStringLiteral("source_x"),
            QStringLiteral("source_y"),
            QStringLiteral("transform_domain_x"),
            QStringLiteral("transform_domain_y"),
            QStringLiteral("predicted_source_x"),
            QStringLiteral("predicted_source_y"),
            QStringLiteral("reference_plane_x"),
            QStringLiteral("reference_plane_y"),
            QStringLiteral("predicted_reference_plane_x"),
            QStringLiteral("predicted_reference_plane_y"),
            QStringLiteral("confidence"),
            QStringLiteral("residual_pixels"),
            QStringLiteral("measured_fwhm_pixels"),
            QStringLiteral("measured_diameter_pixels"),
            QStringLiteral("measured_ellipticity"),
            QStringLiteral("aperture_radius_pixels"),
            QStringLiteral("local_background"),
            QStringLiteral("snr"),
            QStringLiteral("roi_x"),
            QStringLiteral("roi_y"),
            QStringLiteral("roi_width"),
            QStringLiteral("roi_height"),
            QStringLiteral("measurement_method"),
            QStringLiteral("origin"),
            QStringLiteral("fit_state"),
            QStringLiteral("rejection_reason"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometTransformRolesColumns()
{
    return {QStringLiteral("comet_run_id"),   QStringLiteral("frame_id"),
            QStringLiteral("role"),           QStringLiteral("comet_output_plane_id"),
            QStringLiteral("transform_json"), QStringLiteral("source_distortion_json"),
            QStringLiteral("succeeded"),      QStringLiteral("failure_reason"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometOutputPlanesColumns()
{
    return {QStringLiteral("comet_output_plane_id"),
            QStringLiteral("comet_run_id"),
            QStringLiteral("role"),
            QStringLiteral("alignment_mode"),
            QStringLiteral("coordinate_frame_kind"),
            QStringLiteral("canvas_width"),
            QStringLiteral("canvas_height"),
            QStringLiteral("canvas_offset_x"),
            QStringLiteral("canvas_offset_y"),
            QStringLiteral("sky_wcs_valid"),
            QStringLiteral("sky_wcs_invalid_reason"),
            QStringLiteral("descriptor_json"),
            QStringLiteral("source_fingerprint"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometRegisteredOutputsColumns()
{
    return {QStringLiteral("comet_run_id"),
            QStringLiteral("frame_id"),
            QStringLiteral("role"),
            QStringLiteral("comet_output_plane_id"),
            QStringLiteral("output_path"),
            QStringLiteral("artifact_hash"),
            QStringLiteral("output_size_bytes"),
            QStringLiteral("output_modified_at_ms"),
            QStringLiteral("output_identity_token"),
            QStringLiteral("succeeded"),
            QStringLiteral("failure_reason"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometRoleReadinessColumns()
{
    return {QStringLiteral("comet_run_id"),
            QStringLiteral("role"),
            QStringLiteral("readiness_status"),
            QStringLiteral("selected_frame_count"),
            QStringLiteral("successful_transform_count"),
            QStringLiteral("failed_transform_count"),
            QStringLiteral("missing_transform_count"),
            QStringLiteral("output_plane_status"),
            QStringLiteral("requires_materialized_outputs"),
            QStringLiteral("materialized_output_count"),
            QStringLiteral("missing_output_count"),
            QStringLiteral("failed_output_count"),
            QStringLiteral("failure_reason"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList stackingCometArtifactsColumns()
{
    return {QStringLiteral("comet_run_id"),
            QStringLiteral("integration_group_key"),
            QStringLiteral("stacking_hash"),
            QStringLiteral("star_base_output_descriptor_id"),
            QStringLiteral("comet_output_plane_id"),
            QStringLiteral("artifact_kind"),
            QStringLiteral("output_path"),
            QStringLiteral("artifact_hash"),
            QStringLiteral("mask_settings_fingerprint"),
            QStringLiteral("succeeded"),
            QStringLiteral("failure_reason"),
            QStringLiteral("updated_at")};
}

[[nodiscard]] inline QStringList cometSidecarTableNames()
{
    return {QStringLiteral("comet_tracking_run_summary"), QStringLiteral("comet_tracking_points"),
            QStringLiteral("comet_transform_roles"),      QStringLiteral("comet_output_planes"),
            QStringLiteral("comet_registered_outputs"),   QStringLiteral("comet_role_readiness"),
            QStringLiteral("stacking_comet_artifacts"),   QStringLiteral("comet_tracking_runs")};
}

struct RegistrationMetricsSchemaInspection
{
    bool opened = false;
    /// Whether the database holds any table; one that holds none is new, and is made at the format.
    bool holdsTables = false;
    int userVersion = -1;
    bool metricsSchemaOk = false;
    bool stateSchemaOk = false;
    bool starCacheSchemaOk = false;
    bool matchedSourcesSchemaOk = false;
    bool cometRunsSchemaOk = false;
    bool cometRunSummarySchemaOk = false;
    bool cometPointsSchemaOk = false;
    bool cometTransformRolesSchemaOk = false;
    bool cometOutputPlanesSchemaOk = false;
    bool cometRegisteredOutputsSchemaOk = false;
    bool cometRoleReadinessSchemaOk = false;
    bool stackingCometArtifactsSchemaOk = false;
    QString openError;

    [[nodiscard]] bool ordinarySchemaOk() const
    {
        return metricsSchemaOk && stateSchemaOk && starCacheSchemaOk && matchedSourcesSchemaOk;
    }

    [[nodiscard]] bool cometSidecarSchemaOk() const
    {
        return cometRunsSchemaOk && cometRunSummarySchemaOk && cometPointsSchemaOk && cometTransformRolesSchemaOk &&
               cometOutputPlanesSchemaOk && cometRegisteredOutputsSchemaOk && cometRoleReadinessSchemaOk &&
               stackingCometArtifactsSchemaOk;
    }

    [[nodiscard]] bool isCurrent() const
    {
        return opened && userVersion == kRegistrationMetricsSchemaVersion && ordinarySchemaOk() &&
               cometSidecarSchemaOk();
    }

    /// Whether the database is not at the current format, or its tables do not match their format: never read as it is
    /// and never written to.
    [[nodiscard]] bool requiresReset() const
    {
        return opened && (userVersion != kRegistrationMetricsSchemaVersion || !ordinarySchemaOk());
    }
};

// SAFETY: table_name is interpolated directly into the SQL PRAGMA statement.
// Only pass trusted, hardcoded table names — never user-supplied input.
[[nodiscard]] inline bool tableHasColumns(QSqlDatabase& db, const QString& table_name,
                                          const QStringList& expected_columns,
                                          WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    QSqlQuery pragma_query(db);
    if (!core::executeSqliteStatement(pragma_query, QStringLiteral("PRAGMA table_info(%1)").arg(table_name), counters))
    {
        return false;
    }

    QStringList actual_columns;
    while (pragma_query.next())
    {
        actual_columns.push_back(pragma_query.value(1).toString());
    }

    if (actual_columns.isEmpty())
    {
        return false;
    }

    for (const auto& column : expected_columns)
    {
        if (!actual_columns.contains(column))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline bool tableCreateSqlContains(QSqlDatabase& db, const QString& table_name,
                                                 const QStringList& expected_fragments,
                                                 WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = :table_name"));
    query.bindValue(QStringLiteral(":table_name"), table_name);
    if (!core::executeSqliteStatement(query, counters) || !query.next())
    {
        return false;
    }

    const QString create_sql = query.value(0).toString();
    if (create_sql.trimmed().isEmpty())
    {
        return false;
    }

    for (const auto& fragment : expected_fragments)
    {
        if (!create_sql.contains(fragment, Qt::CaseInsensitive))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline bool cometTrackingRunsSchemaOk(QSqlDatabase& db,
                                                    WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("comet_tracking_runs"), cometTrackingRunsColumns(), counters) &&
           tableCreateSqlContains(db, QStringLiteral("comet_tracking_runs"),
                                  {QStringLiteral("CHECK (mode IN ('PureComet'))"), QStringLiteral("CHECK (status IN")},
                                  counters);
}

[[nodiscard]] inline bool cometTrackingRunSummarySchemaOk(QSqlDatabase& db,
                                                          WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("comet_tracking_run_summary"), cometTrackingRunSummaryColumns(),
                           counters);
}

[[nodiscard]] inline bool cometTrackingPointsSchemaOk(QSqlDatabase& db,
                                                      WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("comet_tracking_points"), cometTrackingPointsColumns(), counters) &&
           tableCreateSqlContains(db, QStringLiteral("comet_tracking_points"),
                                  {QStringLiteral("CHECK (origin IN"), QStringLiteral("CHECK (fit_state IN")},
                                  counters);
}

[[nodiscard]] inline bool cometTransformRolesSchemaOk(QSqlDatabase& db,
                                                      WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("comet_transform_roles"), cometTransformRolesColumns(), counters) &&
           tableCreateSqlContains(db, QStringLiteral("comet_transform_roles"),
                                  {QStringLiteral("CHECK (role IN"), QStringLiteral("CHECK (succeeded IN")}, counters);
}

[[nodiscard]] inline bool cometOutputPlanesSchemaOk(QSqlDatabase& db,
                                                    WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("comet_output_planes"), cometOutputPlanesColumns(), counters) &&
           tableCreateSqlContains(
               db, QStringLiteral("comet_output_planes"),
               {QStringLiteral("CHECK (role IN"), QStringLiteral("CHECK (alignment_mode IN ('PureComet'))"),
                QStringLiteral("CHECK (coordinate_frame_kind IN"), QStringLiteral("CHECK (sky_wcs_valid IN")},
               counters);
}

[[nodiscard]] inline bool cometRegisteredOutputsSchemaOk(QSqlDatabase& db,
                                                         WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("comet_registered_outputs"), cometRegisteredOutputsColumns(), counters) &&
           tableCreateSqlContains(db, QStringLiteral("comet_registered_outputs"),
                                  {QStringLiteral("CHECK (role IN"), QStringLiteral("CHECK (succeeded IN")}, counters);
}

[[nodiscard]] inline bool cometRoleReadinessSchemaOk(QSqlDatabase& db,
                                                     WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("comet_role_readiness"), cometRoleReadinessColumns(), counters) &&
           tableCreateSqlContains(db, QStringLiteral("comet_role_readiness"),
                                  {QStringLiteral("CHECK (role IN"), QStringLiteral("CHECK (readiness_status IN"),
                                   QStringLiteral("CHECK (output_plane_status IN"),
                                   QStringLiteral("CHECK (requires_materialized_outputs IN")},
                                  counters);
}

[[nodiscard]] inline bool stackingCometArtifactsSchemaOk(QSqlDatabase& db,
                                                         WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    return tableHasColumns(db, QStringLiteral("stacking_comet_artifacts"), stackingCometArtifactsColumns(), counters) &&
           tableCreateSqlContains(db, QStringLiteral("stacking_comet_artifacts"),
                                  {QStringLiteral("CHECK (artifact_kind IN"), QStringLiteral("CHECK (succeeded IN")},
                                  counters);
}

[[nodiscard]] inline RegistrationMetricsSchemaInspection
inspectRegistrationMetricsSchema(QSqlDatabase& db, WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    RegistrationMetricsSchemaInspection inspection;
    if (!db.isValid() || !db.isOpen())
    {
        return inspection;
    }

    inspection.opened = true;

    QSqlQuery tables_query(db);
    inspection.holdsTables =
        core::executeSqliteStatement(
            tables_query, QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' LIMIT 1"), counters) &&
        tables_query.next();

    QSqlQuery version_query(db);
    if (core::executeSqliteStatement(version_query, QStringLiteral("PRAGMA user_version"), counters) &&
        version_query.next())
    {
        inspection.userVersion = version_query.value(0).toInt();
    }

    inspection.metricsSchemaOk =
        tableHasColumns(db, QStringLiteral("registration_metrics"), registrationMetricsColumns(), counters);
    inspection.stateSchemaOk =
        tableHasColumns(db, QStringLiteral("registration_group_state"), registrationGroupStateColumns(), counters);
    inspection.starCacheSchemaOk =
        tableHasColumns(db, QStringLiteral("star_detection_cache"), starDetectionCacheColumns(), counters);
    inspection.matchedSourcesSchemaOk = tableHasColumns(db, QStringLiteral("registration_matched_sources"),
                                                        registrationMatchedSourcesColumns(), counters);
    inspection.cometRunsSchemaOk = cometTrackingRunsSchemaOk(db, counters);
    inspection.cometRunSummarySchemaOk = cometTrackingRunSummarySchemaOk(db, counters);
    inspection.cometPointsSchemaOk = cometTrackingPointsSchemaOk(db, counters);
    inspection.cometTransformRolesSchemaOk = cometTransformRolesSchemaOk(db, counters);
    inspection.cometOutputPlanesSchemaOk = cometOutputPlanesSchemaOk(db, counters);
    inspection.cometRegisteredOutputsSchemaOk = cometRegisteredOutputsSchemaOk(db, counters);
    inspection.cometRoleReadinessSchemaOk = cometRoleReadinessSchemaOk(db, counters);
    inspection.stackingCometArtifactsSchemaOk = stackingCometArtifactsSchemaOk(db, counters);
    return inspection;
}

[[nodiscard]] inline RegistrationMetricsSchemaInspection
inspectRegistrationMetricsSchema(const QString& database_path, const QString& connection_prefix,
                                 QString* open_error = nullptr, WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (open_error != nullptr)
    {
        open_error->clear();
    }

    core::SqliteConnection connection(database_path, connection_prefix, counters);
    RegistrationMetricsSchemaInspection inspection;
    if (!connection.open(open_error))
    {
        inspection.openError = open_error != nullptr ? *open_error : QStringLiteral("Failed to open database.");
        return inspection;
    }

    return inspectRegistrationMetricsSchema(connection.db(), counters);
}

/// Why a registration database cannot be read as one this version writes - the format it states, or that its tables
/// are not that format's - or an empty string when it is this version's own or a new one: no table and no format
/// stated. A database of another format is never reused, read as this version's or written: the run that would use it
/// is refused.
///
/// @param inspection what the database holds.
/// @param databasePath the database, named in the refusal.
/// @return the refusal, or an empty string.
[[nodiscard]] inline QString registrationMetricsFormatRefusal(const RegistrationMetricsSchemaInspection& inspection,
                                                              const QString& databasePath)
{
    if (!inspection.opened || inspection.isCurrent() || (!inspection.holdsTables && inspection.userVersion == 0))
    {
        return {};
    }
    const QString path = QDir::toNativeSeparators(databasePath);
    if (inspection.userVersion == kRegistrationMetricsSchemaVersion)
    {
        return QStringLiteral(
                   "The registration records '%1' state format %2 but do not hold that format's tables; this "
                   "version of AfterNight reads only the records it writes.")
            .arg(path)
            .arg(kRegistrationMetricsSchemaVersion);
    }
    if (inspection.userVersion <= 0)
    {
        return QStringLiteral("The registration records '%1' state no format version; this version of AfterNight reads "
                              "format %2.")
            .arg(path)
            .arg(kRegistrationMetricsSchemaVersion);
    }
    return QStringLiteral("The registration records '%1' were written in format %2; this version of AfterNight reads "
                          "format %3.")
        .arg(path)
        .arg(inspection.userVersion)
        .arg(kRegistrationMetricsSchemaVersion);
}

[[nodiscard]] inline QString registrationMetricsCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS registration_metrics ("
                          "frame_id TEXT NOT NULL,"
                          "group_key TEXT NOT NULL,"
                          "settings_version INTEGER NOT NULL,"
                          "stars INTEGER NOT NULL,"
                          "fwhm REAL NOT NULL,"
                          "snr REAL NOT NULL,"
                          "eccentricity REAL NOT NULL,"
                          "background_noise REAL NOT NULL DEFAULT 0.0,"
                          "global_mad REAL NOT NULL DEFAULT 0.0,"
                          "global_scale REAL NOT NULL DEFAULT 0.0,"
                          "photometric_zero_point REAL NOT NULL DEFAULT 0.0,"
                          "zero_point_scatter REAL NOT NULL DEFAULT 0.0,"
                          "astrometric_rms_pixels REAL NOT NULL DEFAULT 0.0,"
                          "astrometric_rms_arcsec REAL NOT NULL DEFAULT 0.0,"
                          "limiting_magnitude REAL NOT NULL DEFAULT 0.0,"
                          "catalog_matched_stars INTEGER NOT NULL DEFAULT 0,"
                          "succeeded INTEGER NOT NULL,"
                          "output_path TEXT,"
                          "output_size_bytes INTEGER NOT NULL DEFAULT -1,"
                          "output_modified_at_ms INTEGER NOT NULL DEFAULT -1,"
                          "output_identity_token TEXT NOT NULL DEFAULT '',"
                          "transform_json TEXT,"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (frame_id, group_key, settings_version))");
}

[[nodiscard]] inline QString registrationGroupStateCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS registration_group_state ("
                          "group_key TEXT NOT NULL,"
                          "settings_version INTEGER NOT NULL,"
                          "detection_hash TEXT NOT NULL,"
                          "registration_hash TEXT NOT NULL,"
                          "reference_frame_id TEXT,"
                          "light_files_hash TEXT NOT NULL,"
                          "calibration_hash TEXT NOT NULL DEFAULT '',"
                          "output_wcs_json TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (group_key, settings_version))");
}

[[nodiscard]] inline QString starDetectionCacheCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS star_detection_cache ("
                          "frame_id TEXT NOT NULL,"
                          "group_key TEXT NOT NULL,"
                          "detection_hash TEXT NOT NULL,"
                          "input_path TEXT NOT NULL,"
                          "input_mtime INTEGER NOT NULL,"
                          "input_size_bytes INTEGER NOT NULL DEFAULT -1,"
                          "input_modified_at_ms INTEGER NOT NULL DEFAULT -1,"
                          "input_identity_token TEXT NOT NULL DEFAULT '',"
                          "stars_count INTEGER NOT NULL,"
                          "fwhm REAL NOT NULL,"
                          "snr REAL NOT NULL,"
                          "roundness REAL NOT NULL,"
                          "background_noise REAL NOT NULL DEFAULT 0.0,"
                          "global_mad REAL NOT NULL DEFAULT 0.0,"
                          "global_scale REAL NOT NULL DEFAULT 0.0,"
                          "stars_blob BLOB NOT NULL,"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (frame_id, group_key, detection_hash))");
}

[[nodiscard]] inline QString registrationMatchedSourcesCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS registration_matched_sources ("
                          "frame_id TEXT NOT NULL,"
                          "group_key TEXT NOT NULL,"
                          "settings_version INTEGER NOT NULL,"
                          "gaia_source_id TEXT NOT NULL,"
                          "astrometric_catalog_family_id TEXT NOT NULL DEFAULT '',"
                          "astrometric_catalog_manifest_id TEXT NOT NULL DEFAULT '',"
                          "spcc_catalog_family_id TEXT NOT NULL DEFAULT '',"
                          "spcc_catalog_manifest_id TEXT NOT NULL DEFAULT '',"
                          "wcs_solution_id TEXT NOT NULL DEFAULT '',"
                          "observation_epoch TEXT NOT NULL DEFAULT '',"
                          "proper_motion_model_version TEXT NOT NULL DEFAULT '',"
                          "match_separation_arcsec REAL,"
                          "residual_px REAL,"
                          "lookup_status TEXT NOT NULL DEFAULT 'untested',"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (frame_id, group_key, settings_version, gaia_source_id))");
}

[[nodiscard]] inline QString registrationMatchedSourcesGroupIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_registration_matched_sources_group "
                          "ON registration_matched_sources (group_key, settings_version, frame_id)");
}

[[nodiscard]] inline QString registrationMatchedSourcesSourceIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_registration_matched_sources_source "
                          "ON registration_matched_sources (gaia_source_id, group_key, settings_version)");
}

[[nodiscard]] inline QString cometTrackingRunsCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS comet_tracking_runs ("
                          "comet_run_id TEXT NOT NULL PRIMARY KEY,"
                          "group_key TEXT NOT NULL,"
                          "settings_version INTEGER NOT NULL,"
                          "comet_context_id TEXT NOT NULL DEFAULT '',"
                          "detection_hash TEXT NOT NULL DEFAULT '',"
                          "registration_hash TEXT NOT NULL,"
                          "light_files_hash TEXT NOT NULL,"
                          "calibration_hash TEXT NOT NULL DEFAULT '',"
                          "resolved_reference_frame_id TEXT NOT NULL DEFAULT '',"
                          "reference_epoch REAL NOT NULL DEFAULT 0.0,"
                          "mode TEXT NOT NULL CHECK (mode IN ('PureComet')),"
                          "status TEXT NOT NULL CHECK (status IN ('Building', 'Candidate', 'RegistrationReady', "
                          "'Active', 'Superseded', 'Failed', 'Cancelled', 'Archived')),"
                          "created_at INTEGER NOT NULL,"
                          "updated_at INTEGER NOT NULL)");
}

[[nodiscard]] inline QString cometTrackingRunsActiveContextIndexCreateSql()
{
    return QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_comet_tracking_runs_active_context "
                          "ON comet_tracking_runs (group_key, registration_hash, light_files_hash, "
                          "calibration_hash, resolved_reference_frame_id) WHERE status = 'Active'");
}

[[nodiscard]] inline QString cometTrackingRunsStatusIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_comet_tracking_runs_group_status "
                          "ON comet_tracking_runs (group_key, status, updated_at)");
}

[[nodiscard]] inline QString cometTrackingRunSummaryCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS comet_tracking_run_summary ("
                          "comet_run_id TEXT NOT NULL PRIMARY KEY,"
                          "fit_model TEXT NOT NULL DEFAULT '',"
                          "reference_epoch REAL NOT NULL DEFAULT 0.0,"
                          "fit_time_origin REAL NOT NULL DEFAULT 0.0,"
                          "rms_pixels REAL NOT NULL DEFAULT 0.0,"
                          "max_residual_pixels REAL NOT NULL DEFAULT 0.0,"
                          "accepted_input_count INTEGER NOT NULL DEFAULT 0,"
                          "rejected_input_count INTEGER NOT NULL DEFAULT 0,"
                          "predicted_only_frame_count INTEGER NOT NULL DEFAULT 0,"
                          "warning_flags TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "FOREIGN KEY (comet_run_id) REFERENCES comet_tracking_runs(comet_run_id) ON DELETE CASCADE)");
}

[[nodiscard]] inline QString cometTrackingPointsCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS comet_tracking_points ("
                          "point_id TEXT NOT NULL PRIMARY KEY,"
                          "comet_run_id TEXT NOT NULL,"
                          "original_frame_id TEXT NOT NULL,"
                          "refinement_source_path TEXT NOT NULL DEFAULT '',"
                          "epoch REAL NOT NULL DEFAULT 0.0,"
                          "source_x REAL NOT NULL DEFAULT 0.0,"
                          "source_y REAL NOT NULL DEFAULT 0.0,"
                          "transform_domain_x REAL NOT NULL DEFAULT 0.0,"
                          "transform_domain_y REAL NOT NULL DEFAULT 0.0,"
                          "predicted_source_x REAL NOT NULL DEFAULT 0.0,"
                          "predicted_source_y REAL NOT NULL DEFAULT 0.0,"
                          "reference_plane_x REAL NOT NULL DEFAULT 0.0,"
                          "reference_plane_y REAL NOT NULL DEFAULT 0.0,"
                          "predicted_reference_plane_x REAL NOT NULL DEFAULT 0.0,"
                          "predicted_reference_plane_y REAL NOT NULL DEFAULT 0.0,"
                          "confidence REAL NOT NULL DEFAULT 0.0,"
                          "residual_pixels REAL NOT NULL DEFAULT 0.0,"
                          "measured_fwhm_pixels REAL NOT NULL DEFAULT 0.0,"
                          "measured_diameter_pixels REAL NOT NULL DEFAULT 0.0,"
                          "measured_ellipticity REAL NOT NULL DEFAULT 0.0,"
                          "aperture_radius_pixels REAL NOT NULL DEFAULT 0.0,"
                          "local_background REAL NOT NULL DEFAULT 0.0,"
                          "snr REAL NOT NULL DEFAULT 0.0,"
                          "roi_x REAL NOT NULL DEFAULT 0.0,"
                          "roi_y REAL NOT NULL DEFAULT 0.0,"
                          "roi_width REAL NOT NULL DEFAULT 0.0,"
                          "roi_height REAL NOT NULL DEFAULT 0.0,"
                          "measurement_method TEXT NOT NULL DEFAULT '',"
                          "origin TEXT NOT NULL CHECK (origin IN ('Manual', 'AcceptedAutomatic', 'AutoCandidate', "
                          "'Predicted', 'Imported')),"
                          "fit_state TEXT NOT NULL CHECK (fit_state IN ('Inlier', 'Outlier', 'PredictedOnly', "
                          "'Disabled', 'Failed')),"
                          "rejection_reason TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "FOREIGN KEY (comet_run_id) REFERENCES comet_tracking_runs(comet_run_id) ON DELETE CASCADE)");
}

[[nodiscard]] inline QString cometTrackingPointsFrameIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_comet_tracking_points_frame "
                          "ON comet_tracking_points (comet_run_id, original_frame_id, fit_state)");
}

[[nodiscard]] inline QString cometTrackingPointsAcceptedFrameIndexCreateSql()
{
    return QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_comet_tracking_points_fit_input_frame "
                          "ON comet_tracking_points (comet_run_id, original_frame_id) "
                          "WHERE origin IN ('Manual', 'AcceptedAutomatic', 'Imported') "
                          "AND fit_state IN ('Inlier', 'Outlier')");
}

[[nodiscard]] inline QString cometTransformRolesCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS comet_transform_roles ("
                          "comet_run_id TEXT NOT NULL,"
                          "frame_id TEXT NOT NULL,"
                          "role TEXT NOT NULL CHECK (role IN ('comet_aligned')),"
                          "comet_output_plane_id TEXT NOT NULL DEFAULT '',"
                          "transform_json TEXT NOT NULL DEFAULT '',"
                          "source_distortion_json TEXT NOT NULL DEFAULT '',"
                          "succeeded INTEGER NOT NULL CHECK (succeeded IN (0, 1)),"
                          "failure_reason TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (comet_run_id, frame_id, role),"
                          "FOREIGN KEY (comet_run_id) REFERENCES comet_tracking_runs(comet_run_id) ON DELETE CASCADE)");
}

[[nodiscard]] inline QString cometTransformRolesPlaneIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_comet_transform_roles_plane "
                          "ON comet_transform_roles (comet_output_plane_id, role)");
}

[[nodiscard]] inline QString cometOutputPlanesCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS comet_output_planes ("
                          "comet_output_plane_id TEXT NOT NULL PRIMARY KEY,"
                          "comet_run_id TEXT NOT NULL,"
                          "role TEXT NOT NULL CHECK (role IN ('comet_aligned')),"
                          "alignment_mode TEXT NOT NULL CHECK (alignment_mode IN ('PureComet')),"
                          "coordinate_frame_kind TEXT NOT NULL CHECK (coordinate_frame_kind IN ('CometReference', "
                          "'PixelOnly')),"
                          "canvas_width INTEGER NOT NULL DEFAULT 0,"
                          "canvas_height INTEGER NOT NULL DEFAULT 0,"
                          "canvas_offset_x REAL NOT NULL DEFAULT 0.0,"
                          "canvas_offset_y REAL NOT NULL DEFAULT 0.0,"
                          "sky_wcs_valid INTEGER NOT NULL CHECK (sky_wcs_valid IN (0, 1)),"
                          "sky_wcs_invalid_reason TEXT NOT NULL DEFAULT '',"
                          "descriptor_json TEXT NOT NULL DEFAULT '',"
                          "source_fingerprint TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "UNIQUE (comet_run_id, role),"
                          "FOREIGN KEY (comet_run_id) REFERENCES comet_tracking_runs(comet_run_id) ON DELETE CASCADE)");
}

[[nodiscard]] inline QString cometRegisteredOutputsCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS comet_registered_outputs ("
                          "comet_run_id TEXT NOT NULL,"
                          "frame_id TEXT NOT NULL,"
                          "role TEXT NOT NULL CHECK (role IN ('comet_aligned')),"
                          "comet_output_plane_id TEXT NOT NULL DEFAULT '',"
                          "output_path TEXT NOT NULL DEFAULT '',"
                          "artifact_hash TEXT NOT NULL DEFAULT '',"
                          "output_size_bytes INTEGER NOT NULL DEFAULT -1,"
                          "output_modified_at_ms INTEGER NOT NULL DEFAULT -1,"
                          "output_identity_token TEXT NOT NULL DEFAULT '',"
                          "succeeded INTEGER NOT NULL CHECK (succeeded IN (0, 1)),"
                          "failure_reason TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (comet_run_id, frame_id, role),"
                          "FOREIGN KEY (comet_run_id) REFERENCES comet_tracking_runs(comet_run_id) ON DELETE CASCADE)");
}

[[nodiscard]] inline QString cometRegisteredOutputsPlaneIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_comet_registered_outputs_plane "
                          "ON comet_registered_outputs (comet_output_plane_id, role)");
}

[[nodiscard]] inline QString cometRegisteredOutputsPathIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_comet_registered_outputs_path "
                          "ON comet_registered_outputs (output_path)");
}

[[nodiscard]] inline QString cometRoleReadinessCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS comet_role_readiness ("
                          "comet_run_id TEXT NOT NULL,"
                          "role TEXT NOT NULL CHECK (role IN ('comet_aligned')),"
                          "readiness_status TEXT NOT NULL CHECK (readiness_status IN ('Partial', 'TransformReady', "
                          "'OutputMissing', 'Ready', 'Failed', 'Cancelled')),"
                          "selected_frame_count INTEGER NOT NULL DEFAULT 0,"
                          "successful_transform_count INTEGER NOT NULL DEFAULT 0,"
                          "failed_transform_count INTEGER NOT NULL DEFAULT 0,"
                          "missing_transform_count INTEGER NOT NULL DEFAULT 0,"
                          "output_plane_status TEXT NOT NULL CHECK (output_plane_status IN ('Missing', 'Invalid', "
                          "'Ready')),"
                          "requires_materialized_outputs INTEGER NOT NULL CHECK (requires_materialized_outputs IN "
                          "(0, 1)),"
                          "materialized_output_count INTEGER NOT NULL DEFAULT 0,"
                          "missing_output_count INTEGER NOT NULL DEFAULT 0,"
                          "failed_output_count INTEGER NOT NULL DEFAULT 0,"
                          "failure_reason TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (comet_run_id, role),"
                          "FOREIGN KEY (comet_run_id) REFERENCES comet_tracking_runs(comet_run_id) ON DELETE CASCADE)");
}

[[nodiscard]] inline QString cometRoleReadinessStatusIndexCreateSql()
{
    return QStringLiteral("CREATE INDEX IF NOT EXISTS idx_comet_role_readiness_status "
                          "ON comet_role_readiness (readiness_status, updated_at)");
}

[[nodiscard]] inline QString stackingCometArtifactsCreateSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS stacking_comet_artifacts ("
                          "comet_run_id TEXT NOT NULL,"
                          "integration_group_key TEXT NOT NULL,"
                          "stacking_hash TEXT NOT NULL,"
                          "star_base_output_descriptor_id TEXT NOT NULL DEFAULT '',"
                          "comet_output_plane_id TEXT NOT NULL,"
                          "artifact_kind TEXT NOT NULL CHECK (artifact_kind IN ('FinalComposite', 'CometLayer', "
                          "'CometMask', 'OverlayPreview')),"
                          "output_path TEXT NOT NULL DEFAULT '',"
                          "artifact_hash TEXT NOT NULL DEFAULT '',"
                          "mask_settings_fingerprint TEXT NOT NULL DEFAULT '',"
                          "succeeded INTEGER NOT NULL CHECK (succeeded IN (0, 1)),"
                          "failure_reason TEXT NOT NULL DEFAULT '',"
                          "updated_at INTEGER NOT NULL,"
                          "PRIMARY KEY (comet_run_id, integration_group_key, stacking_hash, artifact_kind, "
                          "comet_output_plane_id),"
                          "FOREIGN KEY (comet_run_id) REFERENCES comet_tracking_runs(comet_run_id) ON DELETE CASCADE)");
}
} // namespace stage_utils
