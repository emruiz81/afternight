// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ExecutionPreparationSnapshot.h"

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QSet>
#include <QSqlQuery>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
[[nodiscard]] QString pathKey(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    return FrameId(path).identityKey();
}

[[nodiscard]] QString canonicalPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    return FrameId(path).canonicalPath();
}

[[nodiscard]] std::optional<FrameId> persistedFrameId(const QString& stored_frame_id)
{
    if (stored_frame_id.trimmed().isEmpty())
        return std::nullopt;
    try
    {
        return FrameId(stored_frame_id);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

[[nodiscard]] bool preferPersistedRow(const qint64 candidate_updated_at, const QString& candidate_stored_frame_id,
                                      const qint64 selected_updated_at, const QString& selected_stored_frame_id)
{
    if (candidate_updated_at != selected_updated_at)
        return candidate_updated_at > selected_updated_at;
    const auto candidate = persistedFrameId(candidate_stored_frame_id);
    const auto selected = persistedFrameId(selected_stored_frame_id);
    if (!candidate.has_value())
        return false;
    if (!selected.has_value())
        return true;
    const bool candidate_canonical = candidate_stored_frame_id == candidate->canonicalPath();
    const bool selected_canonical = selected_stored_frame_id == selected->canonicalPath();
    if (candidate_canonical != selected_canonical)
        return candidate_canonical;
    const int canonical_comparison =
        QString::compare(candidate->canonicalPath(), selected->canonicalPath(), Qt::CaseSensitive);
    return canonical_comparison != 0
               ? canonical_comparison < 0
               : QString::compare(candidate_stored_frame_id, selected_stored_frame_id, Qt::CaseSensitive) < 0;
}

[[nodiscard]] ExecutionPreparationSnapshot::FileStamp
captureFileStamp(const QString& path, const std::function<QString(const QString&)>& file_identity_provider = {})
{
    ExecutionPreparationSnapshot::FileStamp stamp;
    stamp.canonicalPath = canonicalPath(path);
    const QFileInfo info(stamp.canonicalPath);
    stamp.exists = info.exists();
    stamp.isFile = stamp.exists && info.isFile();
    stamp.sizeBytes = stamp.exists ? info.size() : 0;
    stamp.modifiedAtMs = stamp.exists ? info.lastModified().toMSecsSinceEpoch() : 0;
    stamp.identityToken = stamp.isFile
                              ? (file_identity_provider ? file_identity_provider(stamp.canonicalPath)
                                                        : ArtifactTracker::fileIdentityForPath(stamp.canonicalPath))
                              : QString();
    return stamp;
}

[[nodiscard]] bool sameFileStamp(const ExecutionPreparationSnapshot::FileStamp& lhs,
                                 const ExecutionPreparationSnapshot::FileStamp& rhs)
{
    return lhs.canonicalPath == rhs.canonicalPath && lhs.exists == rhs.exists && lhs.isFile == rhs.isFile &&
           lhs.sizeBytes == rhs.sizeBytes && lhs.modifiedAtMs == rhs.modifiedAtMs &&
           lhs.identityToken == rhs.identityToken;
}

[[nodiscard]] bool sameFileStamps(const QHash<QString, ExecutionPreparationSnapshot::FileStamp>& lhs,
                                  const QHash<QString, ExecutionPreparationSnapshot::FileStamp>& rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (auto it = lhs.cbegin(); it != lhs.cend(); ++it)
    {
        const auto other = rhs.constFind(it.key());
        if (other == rhs.cend() || !sameFileStamp(it.value(), other.value()))
            return false;
    }
    return true;
}

[[nodiscard]] QString groupVersionKey(const QString& group_key, const int settings_version)
{
    return QStringLiteral("%1\n%2").arg(group_key).arg(settings_version);
}

[[nodiscard]] QString starCacheKey(const QString& group_key, const QString& detection_hash, const QString& frame_key)
{
    return QStringLiteral("%1\n%2\n%3").arg(group_key, detection_hash, frame_key);
}

[[nodiscard]] QString calibrationSubsetKey(const std::vector<FrameId>& light_files)
{
    QStringList paths;
    paths.reserve(static_cast<qsizetype>(light_files.size()));
    for (const auto& frame : light_files)
    {
        const QString key = pathKey(frame.canonicalPath());
        paths.push_back(key.isEmpty() ? frame.toString() : key);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    QJsonArray array;
    for (const auto& path : paths)
        array.append(path);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString outputPath(const WorkflowManifest& manifest, const QString& relative_or_absolute)
{
    if (relative_or_absolute.trimmed().isEmpty())
        return {};
    const QFileInfo info(relative_or_absolute);
    return info.isAbsolute() ? relative_or_absolute : QDir(manifest.outputDirectory).filePath(relative_or_absolute);
}

[[nodiscard]] QString normalizationArtifactPath(const WorkflowManifest& manifest,
                                                const WorkflowManifest::IntegrationGroup& group)
{
    const QString configured = group.normalizationCoefficientsArtifactPath.value_or(QString());
    if (!configured.trimmed().isEmpty())
        return configured;
    const QString group_dir_name = ImageIntegration::sanitizeFilename(group.groupKey.toString());
    return QDir(manifest.outputDirectory)
        .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/%1/coefficients.json")
                      .arg(group_dir_name));
}

[[nodiscard]] QString drizzlePixmapDirectory(const WorkflowManifest& manifest,
                                             const WorkflowManifest::IntegrationGroup& group)
{
    const QString configured = group.drizzlePixmapArtifactDir.value_or(QString());
    if (!configured.trimmed().isEmpty())
        return configured;
    const QString group_dir_name = ImageIntegration::sanitizeFilename(group.groupKey.toString());
    return QDir(manifest.outputDirectory)
        .filePath(QStringLiteral(".afternight/image_integration/artifacts/drizzle/%1/pixmaps").arg(group_dir_name));
}

[[nodiscard]] ExecutionPreparationSnapshot::ParsedMetadata parseMetadata(const QString& path,
                                                                         std::uint64_t& parsed_records)
{
    ExecutionPreparationSnapshot::ParsedMetadata result;
    result.path = path;
    QFile file(path);
    QByteArray data;
    if (!ImageIntegration::schema::readMetadataSidecarBounded(file, data))
        return result;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return result;

    result.parsed = true;
    result.object = document.object();
    ++parsed_records;
    return result;
}

[[nodiscard]] ExecutionPreparationSnapshot::DrizzleSourceGeometryFacts
captureDrizzleSourceGeometry(const FrameId& frame, const ImageIntegration::FrameHeaderReader& headerReader)
{
    ExecutionPreparationSnapshot::DrizzleSourceGeometryFacts facts;
    facts.canonicalPath = frame.canonicalPath();
    if (!headerReader)
        return facts;
    const auto header = headerReader(facts.canonicalPath);
    if (!header.has_value() || header->width == 0u || header->height == 0u)
        return facts;

    facts.metadataProbeSucceeded = true;
    facts.sourceWidth = header->width;
    facts.sourceHeight = header->height;
    facts.inheritedSourceDistortion = header->distortion;
    facts.inheritedSourceDistortionFingerprint = stage_utils::fingerprintSourceDistortionHex(
        stage_utils::SourceDistortionPolicy::InheritMetadata, facts.inheritedSourceDistortion);
    return facts;
}

[[nodiscard]] bool sameDrizzleSourceGeometry(const ExecutionPreparationSnapshot::DrizzleSourceGeometryFacts& lhs,
                                             const ExecutionPreparationSnapshot::DrizzleSourceGeometryFacts& rhs)
{
    return lhs.canonicalPath == rhs.canonicalPath && lhs.metadataProbeSucceeded == rhs.metadataProbeSucceeded &&
           lhs.sourceWidth == rhs.sourceWidth && lhs.sourceHeight == rhs.sourceHeight &&
           lhs.inheritedSourceDistortionFingerprint == rhs.inheritedSourceDistortionFingerprint;
}

/// The identity an image published for the manifest entry of `manifestHash` carries in its header; empty when the
/// fingerprint reduces to no identity, which no header can then match.
[[nodiscard]] QString manifestEntryIdentity(const QString& manifestHash)
{
    const an::Result<an::ArtifactIdentity> identity = ImageIntegration::workflowArtifactIdentity(manifestHash, {});
    return identity.hasValue() ? QString::fromStdString(identity->artifactId()) : QString();
}

[[nodiscard]] std::optional<qint64> sqliteDataVersion(QSqlDatabase& database)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA data_version")) || !query.next())
        return std::nullopt;
    return query.value(0).toLongLong();
}
} // namespace

struct ExecutionPreparationSnapshot::Data
{
    std::optional<WorkflowManifest> previousManifest;
    ExecutionPreparationDiagnostics diagnostics;
    QHash<QString, FileStamp> fileStamps;
    QHash<QString, FileStamp> stateFileStamps;
    QHash<QString, WorkflowManifest::LightsCalibrationParams> calibrationAssignments;
    QHash<QString, CalibrationSubsetFacts> calibrationSubsets;
    QHash<QString, ArtifactRecordSnapshot> artifactRecords;
    /// The identity each moved master's own header carried when it was read, keyed as the file stamps are.
    QHash<QString, QString> strictArtifactHeaderIdentities;
    bool registrationDatabaseUsable = false;
    bool sourceFactsConsistent = true;
    QHash<QString, RegistrationGroupState> registrationStates;
    QHash<QString, QString> latestRegistrationStateKeys;
    QHash<QString, RegistrationMetrics> registrationMetrics;
    QHash<QString, int> latestRegistrationMetricVersions;
    QHash<QString, StarCacheInputStamp> starCacheInputStamps;
    QHash<QString, ParsedMetadata> normalizationMetadata;
    QHash<QString, QString> normalizationCoefficientDigests;
    QHash<QString, ParsedMetadata> drizzleMetadata;
    QHash<QString, DrizzleSourceGeometryFacts> drizzleSourceGeometry;
    QSet<QString> strictArtifactPaths;
    ImageIntegration::FrameHeaderReader headerReader;
};

class ExecutionPreparationSnapshotBuilder
{
public:
    ExecutionPreparationSnapshotBuilder(const WorkflowManifest& manifest,
                                        std::optional<WorkflowManifest> previous_manifest,
                                        QString previous_manifest_path = {}, std::function<bool()> should_cancel = {},
                                        ImageIntegration::FrameHeaderReader header_reader = {})
        : m_manifest(manifest), m_data(std::make_unique<ExecutionPreparationSnapshot::Data>()),
          m_shouldCancel(std::move(should_cancel))
    {
        m_data->headerReader = std::move(header_reader);
        m_data->previousManifest = std::move(previous_manifest);
        if (m_data->previousManifest.has_value())
            ++m_data->diagnostics.parsedRecords;
        if (!previous_manifest_path.trimmed().isEmpty())
            queueStatePath(previous_manifest_path);
    }

    [[nodiscard]] std::unique_ptr<ExecutionPreparationSnapshot::Data> build()
    {
        throwIfCancelled();
        collectManifestFacts();
        throwIfCancelled();
        captureDrizzleSourceGeometryFacts();
        throwIfCancelled();
        queueDatabaseStatePaths();
        capturePaths(m_statePaths, m_boundaryStateFileStamps, nullptr);
        throwIfCancelled();
        captureArtifactRecords();
        throwIfCancelled();
        captureRegistrationRecords();
        throwIfCancelled();
        captureMetadata();
        throwIfCancelled();
        captureQueuedFileStamps();
        m_data->sourceFactsConsistent =
            m_data->sourceFactsConsistent && sameFileStamps(m_boundaryStateFileStamps, m_data->stateFileStamps);
        return std::move(m_data);
    }

private:
    void throwIfCancelled() const
    {
        if (m_shouldCancel && m_shouldCancel())
            throw std::runtime_error("Workflow preparation cancelled");
    }

    void queuePath(const QString& path)
    {
        const QString key = pathKey(path);
        if (!key.isEmpty())
            m_paths.tryInsert(key, canonicalPath(path));
    }

    void queueStatePath(const QString& path)
    {
        const QString key = pathKey(path);
        if (!key.isEmpty())
            m_statePaths.tryInsert(key, canonicalPath(path));
    }

    void queueFrame(const FrameId& frame)
    {
        queuePath(frame.canonicalPath());
    }

    void queueOptionalFrame(const std::optional<FrameId>& frame)
    {
        if (frame.has_value())
            queueFrame(*frame);
    }

    void queueStrictArtifact(const QString& path)
    {
        const QString key = pathKey(path);
        if (!key.isEmpty())
        {
            m_paths.tryInsert(key, canonicalPath(path));
            m_data->strictArtifactPaths.insert(key);
        }
    }

    static void capturePaths(const QHash<QString, QString>& paths,
                             QHash<QString, ExecutionPreparationSnapshot::FileStamp>& stamps,
                             std::uint64_t* operation_count)
    {
        stamps.reserve(paths.size());
        for (auto it = paths.cbegin(); it != paths.cend(); ++it)
        {
            stamps.insert(it.key(), captureFileStamp(it.value()));
            if (operation_count != nullptr)
                ++*operation_count;
        }
    }

    void queueDatabaseStatePaths()
    {
        if (m_manifest.outputDirectory.trimmed().isEmpty())
            return;

        const QString artifact_database =
            QDir(m_manifest.outputDirectory).filePath(QStringLiteral(".afternight/image_integration/artifacts.db"));
        queueStatePath(artifact_database);
        queueStatePath(artifact_database + QStringLiteral("-wal"));
        if (!m_manifest.registrationGroups.empty())
        {
            const QString registration_database =
                QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
            queueStatePath(registration_database);
            queueStatePath(registration_database + QStringLiteral("-wal"));
        }
    }

    void collectCalibrationStage(const std::optional<WorkflowManifest::CalibrationStage>& stage)
    {
        if (!stage.has_value())
            return;
        for (const auto& group : stage->groups)
        {
            for (const auto& input : group.inputFiles)
                queueFrame(input);
            queueOptionalFrame(group.existingMaster);
            queueOptionalFrame(group.biasMaster);
            queueOptionalFrame(group.darkMaster);
            if (!group.existingMaster.has_value())
                queueStrictArtifact(outputPath(m_manifest, group.outputFilename));
        }
    }

    void collectManifestFacts()
    {
        collectCalibrationStage(m_manifest.biasStage);
        collectCalibrationStage(m_manifest.darkStage);
        collectCalibrationStage(m_manifest.flatStage);

        m_data->calibrationAssignments.reserve(static_cast<qsizetype>(m_manifest.lightsCalibration.size()));
        for (const auto& params : m_manifest.lightsCalibration)
        {
            const QString key = pathKey(params.lightPath.canonicalPath());
            if (!key.isEmpty())
            {
                m_data->calibrationAssignments.insert(key, params);
                ++m_data->diagnostics.calibrationAssignmentsIndexed;
            }
            queueFrame(params.lightPath);
            queueOptionalFrame(params.biasPath);
            queueOptionalFrame(params.darkPath);
            queueOptionalFrame(params.flatPath);
            if (!m_manifest.calibrateLightsInMemory)
                queuePath(calibratedLightOutputPathFor(m_manifest.outputDirectory, params.lightPath));
        }

        for (const auto& group : m_manifest.registrationGroups)
        {
            for (const auto& frame : group.lightFiles)
            {
                queueFrame(frame);
                queuePath(calibratedLightOutputPathFor(m_manifest.outputDirectory, frame));
                queuePath(chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, frame));
            }
            queueOptionalFrame(group.referenceFrame);
        }

        for (const auto& group : m_manifest.integrationGroups)
        {
            for (const auto& frame : group.lightFiles)
                queueFrame(frame);
            for (const auto& frame : group.normalizationReferenceFrames)
                queueFrame(frame);
            queueStrictArtifact(outputPath(m_manifest, group.outputFilename));
            if (group.stacking.normalizationMode != NormalizationMode::None ||
                group.normalizationCoefficientsArtifactPath.has_value())
            {
                const QString normalization_path = normalizationArtifactPath(m_manifest, group);
                const QString normalization_metadata_path = normalization_path + QStringLiteral(".meta.json");
                queuePath(normalization_path);
                queuePath(normalization_metadata_path);
                queueStatePath(normalization_metadata_path);
            }
            if ((group.drizzle.has_value() && group.drizzle->enabled) || group.drizzlePixmapArtifactDir.has_value())
            {
                const QString drizzle_metadata_path =
                    QDir(drizzlePixmapDirectory(m_manifest, group)).filePath(QStringLiteral(".meta.json"));
                queuePath(drizzle_metadata_path);
                queueStatePath(drizzle_metadata_path);
            }
        }
    }

    void captureDrizzleSourceGeometryFacts()
    {
        for (const auto& group : m_manifest.integrationGroups)
        {
            if (!group.drizzle.has_value() || !group.drizzle->enabled)
                continue;
            for (const auto& frame : group.lightFiles)
            {
                const QString key = frame.identityKey();
                if (!key.isEmpty() && !m_data->drizzleSourceGeometry.contains(key))
                    m_data->drizzleSourceGeometry.insert(key,
                                                         captureDrizzleSourceGeometry(frame, m_data->headerReader));
            }
        }
    }

    void captureArtifactRecords()
    {
        if (m_manifest.outputDirectory.trimmed().isEmpty())
            return;

        const QString registry_path =
            QDir(m_manifest.outputDirectory).filePath(QStringLiteral(".afternight/image_integration/artifacts.db"));
        queueStatePath(registry_path);
        queueStatePath(registry_path + QStringLiteral("-wal"));

        if (!QFileInfo::exists(registry_path))
            return;
        core::SqliteConnection connection(registry_path, QStringLiteral("ExecutionPreparationArtifacts"));
        if (!connection.openReadOnly())
            return;
        ++m_data->diagnostics.databaseOpenOperations;
        QSqlQuery query(connection.db());
        // The registry is opened read-only, so its format is whatever the last writing run left behind. Its records
        // are read only from a store of the format this version writes: one of another format is never read, so every
        // artifact it recorded is decided again, and the run's tracker sets the store aside.
        if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next() ||
            query.value(0).toInt() != ImageIntegration::schema::kArtifactTrackerSchemaVersion)
        {
            return;
        }
        query.finish();
        if (!query.exec(QStringLiteral("SELECT path, hash, size_bytes, modified_at, content_digest, "
                                       "digest_provenance, artifact_stamp, identity_token "
                                       "FROM artifacts ORDER BY path")))
        {
            return;
        }
        while (query.next())
        {
            ArtifactRecordSnapshot record;
            record.path = query.value(0).toString();
            record.manifestHash = query.value(1).toString();
            record.sizeBytes = query.value(2).toLongLong();
            record.modifiedAtMs = query.value(3).toLongLong();
            record.contentDigest = query.value(4).toString();
            record.digestProvenance = query.value(5).toString();
            record.artifactStamp = query.value(6).toString();
            record.identityToken = query.value(7).toString();
            const QString key = pathKey(record.path);
            if (key.isEmpty())
                continue;
            m_data->artifactRecords.insert(key, record);
            ++m_data->diagnostics.parsedRecords;
        }
    }

    void captureRegistrationRecords()
    {
        if (m_manifest.outputDirectory.trimmed().isEmpty() || m_manifest.registrationGroups.empty())
            return;

        const QString database_path =
            QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
        queueStatePath(database_path);
        queueStatePath(database_path + QStringLiteral("-wal"));
        if (!QFileInfo::exists(database_path))
            return;
        core::SqliteConnection connection(database_path, QStringLiteral("ExecutionPreparationSnapshot"));
        if (!connection.openReadOnly())
            return;
        ++m_data->diagnostics.databaseOpenOperations;

        const std::optional<qint64> data_version_before = sqliteDataVersion(connection.db());
        if (!connection.db().transaction())
            return;

        const auto schema = stage_utils::inspectRegistrationMetricsSchema(connection.db());
        if (!schema.opened || schema.requiresReset())
        {
            connection.db().rollback();
            return;
        }

        QStringList group_keys;
        group_keys.reserve(static_cast<qsizetype>(m_manifest.registrationGroups.size()));
        QSet<QString> seen_group_keys;
        for (const auto& group : m_manifest.registrationGroups)
        {
            const QString group_key = group.groupKey.toString();
            if (!group_key.isEmpty() && !seen_group_keys.contains(group_key))
            {
                seen_group_keys.insert(group_key);
                group_keys.push_back(group_key);
            }
        }

        constexpr qsizetype kGroupQueryBatchSize = 400;
        const auto query_requested_groups = [&](const QString& select_sql, const auto& consume_row) {
            for (qsizetype offset = 0; offset < group_keys.size(); offset += kGroupQueryBatchSize)
            {
                const qsizetype count = (std::min)(kGroupQueryBatchSize, group_keys.size() - offset);
                QStringList placeholders;
                placeholders.reserve(count);
                for (qsizetype index = 0; index < count; ++index)
                    placeholders.push_back(QStringLiteral("?"));

                QSqlQuery query(connection.db());
                if (!query.prepare(select_sql + QStringLiteral(" WHERE group_key IN (") +
                                   placeholders.join(QLatin1Char(',')) + QLatin1Char(')')))
                    return false;
                for (qsizetype index = 0; index < count; ++index)
                    query.addBindValue(group_keys.at(offset + index));
                if (!query.exec())
                    return false;
                while (query.next())
                    consume_row(query);
            }
            return true;
        };

        const bool states_loaded = query_requested_groups(
            QStringLiteral(
                "SELECT group_key, settings_version, detection_hash, registration_hash, reference_frame_id, "
                "light_files_hash, calibration_hash, output_wcs_json, updated_at FROM registration_group_state"),
            [&](const QSqlQuery& query) {
                ExecutionPreparationSnapshot::RegistrationGroupState state;
                const QString group_key = query.value(0).toString();
                state.settingsVersion = query.value(1).toInt();
                state.detectionHash = query.value(2).toString();
                state.registrationHash = query.value(3).toString();
                state.referenceFrameId = query.value(4).toString();
                state.lightFilesHash = query.value(5).toString();
                state.calibrationHash = query.value(6).toString();
                state.outputWcsJson = query.value(7).toString();
                state.updatedAt = query.value(8).toLongLong();
                const QString state_key = groupVersionKey(group_key, state.settingsVersion);
                const auto latest_key = m_data->latestRegistrationStateKeys.constFind(group_key);
                const bool is_latest =
                    latest_key == m_data->latestRegistrationStateKeys.cend() ||
                    m_data->registrationStates.value(latest_key.value()).settingsVersion < state.settingsVersion;
                m_data->registrationStates.insert(state_key, std::move(state));
                if (is_latest)
                    m_data->latestRegistrationStateKeys.insert(group_key, state_key);
                ++m_data->diagnostics.parsedRecords;
            });

        const bool metrics_loaded = query_requested_groups(
            QStringLiteral("SELECT group_key, settings_version, frame_id, succeeded, output_path, output_size_bytes, "
                           "output_modified_at_ms, output_identity_token, transform_json, updated_at "
                           "FROM registration_metrics"),
            [&](const QSqlQuery& query) {
                const QString group_key = query.value(0).toString();
                const int settings_version = query.value(1).toInt();
                const QString stored_frame_id = query.value(2).toString();
                const auto frame_id = persistedFrameId(stored_frame_id);
                if (!frame_id.has_value())
                    return;

                ExecutionPreparationSnapshot::RegistrationMetric metric;
                metric.succeeded = query.value(3).toInt() != 0;
                metric.outputPath = query.value(4).toString();
                metric.outputSizeBytes = query.value(5).toLongLong();
                metric.outputModifiedAtMs = query.value(6).toLongLong();
                metric.outputIdentityToken = query.value(7).toString();
                metric.updatedAt = query.value(9).toLongLong();
                const QString transform_json = query.value(8).toString();
                if (metric.succeeded && !transform_json.isEmpty())
                {
                    an::RegistrationTransform transform;
                    QString parse_error;
                    metric.transformParsed = stage_utils::parseRegistrationTransformJson(
                        transform_json, transform, &metric.sourceDistortion, parse_error);
                    if (metric.transformParsed)
                        metric.transform = std::move(transform);
                }
                ++m_data->diagnostics.parsedRecords;
                const QString version_key = groupVersionKey(group_key, settings_version);
                const QString rank_key = version_key + QLatin1Char('\n') + frame_id->identityKey();
                const auto selected_spelling = m_registrationMetricStoredFrameIds.constFind(rank_key);
                const auto& version_metrics = m_data->registrationMetrics[version_key];
                const auto selected_metric = version_metrics.constFind(*frame_id);
                if (selected_metric != version_metrics.cend() &&
                    selected_spelling != m_registrationMetricStoredFrameIds.cend() &&
                    !preferPersistedRow(metric.updatedAt, stored_frame_id, selected_metric->updatedAt,
                                        selected_spelling.value()))
                    return;
                queuePath(metric.outputPath);
                m_data->registrationMetrics[version_key].insert(*frame_id, std::move(metric));
                m_registrationMetricStoredFrameIds.insert(rank_key, stored_frame_id);
                const auto latest_version = m_data->latestRegistrationMetricVersions.constFind(group_key);
                if (latest_version == m_data->latestRegistrationMetricVersions.cend() ||
                    latest_version.value() < settings_version)
                {
                    m_data->latestRegistrationMetricVersions.insert(group_key, settings_version);
                }
            });

        const bool stars_loaded = query_requested_groups(
            QStringLiteral("SELECT group_key, detection_hash, frame_id, input_path, input_mtime, input_size_bytes, "
                           "input_modified_at_ms, input_identity_token, updated_at "
                           "FROM star_detection_cache"),
            [&](const QSqlQuery& query) {
                const QString stored_frame_id = query.value(2).toString();
                const auto frame_id = persistedFrameId(stored_frame_id);
                if (!frame_id.has_value())
                    return;
                ++m_data->diagnostics.parsedRecords;
                const QString cache_key =
                    starCacheKey(query.value(0).toString(), query.value(1).toString(), frame_id->identityKey());
                const qint64 updated_at = query.value(8).toLongLong();
                const auto selected_updated_at = m_starCacheUpdatedAts.constFind(cache_key);
                if (selected_updated_at != m_starCacheUpdatedAts.cend() &&
                    !preferPersistedRow(updated_at, stored_frame_id, selected_updated_at.value(),
                                        m_starCacheStoredFrameIds.value(cache_key)))
                    return;
                const QString input_path = canonicalPath(query.value(3).toString());
                m_data->starCacheInputStamps.insert(
                    cache_key, ExecutionPreparationSnapshot::StarCacheInputStamp{
                                   input_path, query.value(4).toLongLong(), query.value(5).toLongLong(),
                                   query.value(6).toLongLong(), query.value(7).toString()});
                queuePath(input_path);
                m_starCacheUpdatedAts.insert(cache_key, updated_at);
                m_starCacheStoredFrameIds.insert(cache_key, stored_frame_id);
            });

        if (!states_loaded || !metrics_loaded || !stars_loaded || !connection.db().commit())
        {
            connection.db().rollback();
            m_data->registrationStates.clear();
            m_data->latestRegistrationStateKeys.clear();
            m_data->registrationMetrics.clear();
            m_data->latestRegistrationMetricVersions.clear();
            m_data->starCacheInputStamps.clear();
            m_registrationMetricStoredFrameIds.clear();
            m_starCacheUpdatedAts.clear();
            m_starCacheStoredFrameIds.clear();
            return;
        }

        m_data->registrationDatabaseUsable = true;
        const std::optional<qint64> data_version_after = sqliteDataVersion(connection.db());
        if (!data_version_before.has_value() || !data_version_after.has_value() ||
            data_version_before.value() != data_version_after.value())
        {
            m_data->sourceFactsConsistent = false;
        }
    }

    void captureMetadata()
    {
        for (const auto& group : m_manifest.integrationGroups)
        {
            if (group.stacking.normalizationMode != NormalizationMode::None ||
                group.normalizationCoefficientsArtifactPath.has_value())
            {
                const QString coefficient_path = normalizationArtifactPath(m_manifest, group);
                const QString normalization_path = coefficient_path + QStringLiteral(".meta.json");
                QLockFile publication_lock(coefficient_path + QStringLiteral(".publish.lock"));
                publication_lock.setStaleLockTime(0);
                if (publication_lock.tryLock(0))
                {
                    m_data->normalizationMetadata.insert(
                        group.groupKey.toString(),
                        parseMetadata(normalization_path, m_data->diagnostics.parsedRecords));
                    // A bounded document, not an image: its digest is the content identity its sidecar records.
                    WorkflowExecutionCounterAccumulator hashed;
                    const QString digest =
                        ArtifactTracker::contentDigestForFile(coefficient_path, &hashed, m_shouldCancel);
                    m_data->diagnostics.bytesHashed += hashed.snapshot().bytesHashed();
                    throwIfCancelled();
                    if (digest.size() == 64)
                    {
                        m_data->normalizationCoefficientDigests.insert(group.groupKey.toString(), digest);
                    }
                }
            }

            if ((group.drizzle.has_value() && group.drizzle->enabled) || group.drizzlePixmapArtifactDir.has_value())
            {
                const QString drizzle_path =
                    QDir(drizzlePixmapDirectory(m_manifest, group)).filePath(QStringLiteral(".meta.json"));
                ExecutionPreparationSnapshot::ParsedMetadata drizzle =
                    parseMetadata(drizzle_path, m_data->diagnostics.parsedRecords);
                if (drizzle.parsed)
                {
                    const QJsonArray frames = drizzle.object.value(QStringLiteral("frames")).toArray();
                    for (const auto& entry : frames)
                    {
                        // Each light's descriptor document; the planner reads it whole and stats the dense payload
                        // it names, so both are pinned here.
                        const QString descriptor_file =
                            entry.toObject().value(QStringLiteral("descriptorFile")).toString();
                        if (descriptor_file.trimmed().isEmpty())
                            continue;
                        const QString descriptor_path =
                            QDir(drizzlePixmapDirectory(m_manifest, group)).filePath(descriptor_file);
                        queuePath(descriptor_path);
                        QString descriptor_error;
                        if (const auto descriptor =
                                ImageIntegration::readDrizzleDescriptor(descriptor_path, descriptor_error);
                            descriptor.has_value() && descriptor->densePayload.has_value())
                        {
                            queuePath(QDir(drizzlePixmapDirectory(m_manifest, group))
                                          .filePath(descriptor->densePayload->fileName));
                        }
                    }
                }
                m_data->drizzleMetadata.insert(group.groupKey.toString(), std::move(drizzle));
            }
        }
    }

    void captureQueuedFileStamps()
    {
        capturePaths(m_paths, m_data->fileStamps, &m_data->diagnostics.fileStatOperations);
        capturePaths(m_statePaths, m_data->stateFileStamps, nullptr);
    }

    const WorkflowManifest& m_manifest;
    std::unique_ptr<ExecutionPreparationSnapshot::Data> m_data;
    QHash<QString, QString> m_paths;
    QHash<QString, QString> m_statePaths;
    QHash<QString, ExecutionPreparationSnapshot::FileStamp> m_boundaryStateFileStamps;
    QHash<QString, QString> m_registrationMetricStoredFrameIds;
    QHash<QString, qint64> m_starCacheUpdatedAts;
    QHash<QString, QString> m_starCacheStoredFrameIds;
    std::function<bool()> m_shouldCancel;
};

ExecutionPreparationSnapshot::ExecutionPreparationSnapshot(std::unique_ptr<Data> data, const WorkflowManifest& manifest,
                                                           const std::function<bool()>& shouldCancel)
    : m_data(std::move(data))
{
    buildCalibrationSubsetFacts(manifest);
    captureStrictArtifactIdentities(shouldCancel);
}

ExecutionPreparationSnapshot::~ExecutionPreparationSnapshot() = default;

void ExecutionPreparationSnapshot::captureStrictArtifactIdentities(const std::function<bool()>& shouldCancel)
{
    // A master is decided as the tracker decides every artifact it records. One untouched since its record was made is
    // answered by the stat already taken; only one moved or restored under a fresh platform identity, whose record
    // holds the stamp to compare with, has its header read - the header alone, never its payload. The stat pins that
    // revision: a later write moves the platform identity, which the launch check sees.
    if (!m_data->headerReader)
        return;
    for (const QString& key : std::as_const(m_data->strictArtifactPaths))
    {
        const auto record = m_data->artifactRecords.constFind(key);
        const auto stamp = m_data->fileStamps.constFind(key);
        if (record == m_data->artifactRecords.cend() || stamp == m_data->fileStamps.cend() || !stamp->isFile ||
            record->sizeBytes != stamp->sizeBytes || record->modifiedAtMs != stamp->modifiedAtMs ||
            record->artifactStamp.isEmpty() ||
            (!record->identityToken.isEmpty() && record->identityToken == stamp->identityToken))
            continue;

        if (shouldCancel && shouldCancel())
            throw std::runtime_error("Workflow preparation cancelled");
        const auto header = m_data->headerReader(stamp->canonicalPath);
        ++m_data->diagnostics.strictHeaderReads;
        if (header.has_value() && !header->artifactIdentity.isEmpty())
            m_data->strictArtifactHeaderIdentities.insert(key, header->artifactIdentity);
    }
}

std::shared_ptr<const ExecutionPreparationSnapshot>
ExecutionPreparationSnapshot::build(const WorkflowManifest& manifest, const std::function<bool()>& shouldCancel,
                                    ImageIntegration::FrameHeaderReader headerReader)
{
    constexpr int kCaptureAttempts = 3;
    WorkflowRunStateStore store(manifest.outputDirectory);
    const QString previous_manifest_path =
        store.lastSuccessfulManifestPath(manifest.executionScope, manifest.executionScopeKey);
    std::unique_ptr<Data> last_capture;
    ExecutionPreparationDiagnostics cumulative_diagnostics;
    const auto accumulate_diagnostics = [&cumulative_diagnostics](const ExecutionPreparationDiagnostics& current) {
        cumulative_diagnostics.fileStatOperations += current.fileStatOperations;
        cumulative_diagnostics.databaseOpenOperations += current.databaseOpenOperations;
        cumulative_diagnostics.parsedRecords += current.parsedRecords;
        cumulative_diagnostics.strictHeaderReads += current.strictHeaderReads;
        cumulative_diagnostics.calibrationAssignmentsIndexed += current.calibrationAssignmentsIndexed;
        cumulative_diagnostics.calibrationAssignmentVisits += current.calibrationAssignmentVisits;
        cumulative_diagnostics.bytesHashed += current.bytesHashed;
        ++cumulative_diagnostics.captureAttempts;
    };
    for (int attempt = 0; attempt < kCaptureAttempts; ++attempt)
    {
        if (shouldCancel && shouldCancel())
            throw std::runtime_error("Workflow preparation cancelled");
        const FileStamp previous_manifest_before = captureFileStamp(previous_manifest_path);
        std::optional<WorkflowManifest> previous_manifest =
            store.loadLastSuccessfulManifest(manifest.executionScope, manifest.executionScopeKey);
        const FileStamp previous_manifest_after = captureFileStamp(previous_manifest_path);

        ExecutionPreparationSnapshotBuilder builder(manifest, std::move(previous_manifest), previous_manifest_path,
                                                    shouldCancel, headerReader);
        std::unique_ptr<Data> capture = builder.build();
        capture->sourceFactsConsistent =
            capture->sourceFactsConsistent && sameFileStamp(previous_manifest_before, previous_manifest_after);
        accumulate_diagnostics(capture->diagnostics);
        if (capture->sourceFactsConsistent)
        {
            cumulative_diagnostics.consistencyRetries = cumulative_diagnostics.captureAttempts - 1;
            capture->diagnostics = cumulative_diagnostics;
            return std::shared_ptr<const ExecutionPreparationSnapshot>(
                new ExecutionPreparationSnapshot(std::move(capture), manifest, shouldCancel));
        }
        last_capture = std::move(capture);
    }

    cumulative_diagnostics.consistencyRetries = cumulative_diagnostics.captureAttempts - 1;
    last_capture->diagnostics = cumulative_diagnostics;

    return std::shared_ptr<const ExecutionPreparationSnapshot>(
        new ExecutionPreparationSnapshot(std::move(last_capture), manifest, shouldCancel));
}

std::shared_ptr<const ExecutionPreparationSnapshot>
ExecutionPreparationSnapshot::build(const WorkflowManifest& manifest, std::optional<WorkflowManifest> previousManifest,
                                    const std::function<bool()>& shouldCancel,
                                    ImageIntegration::FrameHeaderReader headerReader)
{
    constexpr int kCaptureAttempts = 3;
    std::unique_ptr<Data> last_capture;
    ExecutionPreparationDiagnostics cumulative_diagnostics;
    const auto accumulate_diagnostics = [&cumulative_diagnostics](const ExecutionPreparationDiagnostics& current) {
        cumulative_diagnostics.fileStatOperations += current.fileStatOperations;
        cumulative_diagnostics.databaseOpenOperations += current.databaseOpenOperations;
        cumulative_diagnostics.parsedRecords += current.parsedRecords;
        cumulative_diagnostics.strictHeaderReads += current.strictHeaderReads;
        cumulative_diagnostics.calibrationAssignmentsIndexed += current.calibrationAssignmentsIndexed;
        cumulative_diagnostics.calibrationAssignmentVisits += current.calibrationAssignmentVisits;
        cumulative_diagnostics.bytesHashed += current.bytesHashed;
        ++cumulative_diagnostics.captureAttempts;
    };
    for (int attempt = 0; attempt < kCaptureAttempts; ++attempt)
    {
        if (shouldCancel && shouldCancel())
            throw std::runtime_error("Workflow preparation cancelled");
        ExecutionPreparationSnapshotBuilder builder(manifest, std::move(previousManifest), {}, shouldCancel,
                                                    headerReader);
        std::unique_ptr<Data> capture = builder.build();
        accumulate_diagnostics(capture->diagnostics);
        if (capture->sourceFactsConsistent)
        {
            cumulative_diagnostics.consistencyRetries = cumulative_diagnostics.captureAttempts - 1;
            capture->diagnostics = cumulative_diagnostics;
            return std::shared_ptr<const ExecutionPreparationSnapshot>(
                new ExecutionPreparationSnapshot(std::move(capture), manifest, shouldCancel));
        }
        if (attempt + 1 < kCaptureAttempts)
            previousManifest = capture->previousManifest;
        last_capture = std::move(capture);
    }

    cumulative_diagnostics.consistencyRetries = cumulative_diagnostics.captureAttempts - 1;
    last_capture->diagnostics = cumulative_diagnostics;

    return std::shared_ptr<const ExecutionPreparationSnapshot>(
        new ExecutionPreparationSnapshot(std::move(last_capture), manifest, shouldCancel));
}

const std::optional<WorkflowManifest>& ExecutionPreparationSnapshot::previousManifest() const noexcept
{
    return m_data->previousManifest;
}

const ExecutionPreparationDiagnostics& ExecutionPreparationSnapshot::diagnostics() const noexcept
{
    return m_data->diagnostics;
}

bool ExecutionPreparationSnapshot::matchesCurrentFacts(
    const std::function<bool()>& shouldCancel, const std::function<QString(const QString&)>& fileIdentityProvider) const
{
    if (!m_data->sourceFactsConsistent)
        return false;

    const auto cancellation_requested = [&shouldCancel]() { return shouldCancel && shouldCancel(); };
    // A stat per file: its size, modification time and platform identity as the plan saw them. Nothing rewrites a
    // file without moving its platform identity, so no artifact's payload, a master's included, is read again here.
    const auto stamps_match = [&cancellation_requested,
                               &fileIdentityProvider](const QHash<QString, FileStamp>& stamps) {
        for (auto it = stamps.cbegin(); it != stamps.cend(); ++it)
        {
            if (cancellation_requested())
            {
                return false;
            }

            const FileStamp& captured = it.value();
            if (!sameFileStamp(captured, captureFileStamp(captured.canonicalPath, fileIdentityProvider)))
                return false;
        }
        return true;
    };

    if (!stamps_match(m_data->fileStamps) || !stamps_match(m_data->stateFileStamps))
        return false;

    for (auto it = m_data->drizzleSourceGeometry.cbegin(); it != m_data->drizzleSourceGeometry.cend(); ++it)
    {
        if (cancellation_requested())
            return false;
        const auto current = captureDrizzleSourceGeometry(FrameId(it->canonicalPath), m_data->headerReader);
        if (!sameDrizzleSourceGeometry(it.value(), current))
            return false;
    }
    return true;
}

const ExecutionPreparationSnapshot::FileStamp* ExecutionPreparationSnapshot::fileStamp(const QString& path) const
{
    const auto it = m_data->fileStamps.constFind(pathKey(path));
    return it == m_data->fileStamps.cend() ? nullptr : &it.value();
}

QJsonObject ExecutionPreparationSnapshot::fileStampJson(const QString& path) const
{
    QJsonObject object;
    if (path.trimmed().isEmpty())
    {
        object.insert(QStringLiteral("path"), QString());
        object.insert(QStringLiteral("mtimeMs"), 0);
        object.insert(QStringLiteral("sizeBytes"), 0);
        object.insert(QStringLiteral("identityToken"), QString());
        return object;
    }

    const FileStamp* stamp = fileStamp(path);
    object.insert(QStringLiteral("path"), stamp ? stamp->canonicalPath : canonicalPath(path));
    object.insert(QStringLiteral("mtimeMs"), stamp && stamp->exists ? stamp->modifiedAtMs : 0);
    object.insert(QStringLiteral("sizeBytes"), stamp && stamp->exists ? stamp->sizeBytes : 0);
    object.insert(QStringLiteral("identityToken"), stamp && stamp->isFile ? stamp->identityToken : QString());
    return object;
}

const WorkflowManifest::LightsCalibrationParams*
ExecutionPreparationSnapshot::calibrationAssignment(const FrameId& light) const
{
    const auto it = m_data->calibrationAssignments.constFind(pathKey(light.canonicalPath()));
    return it == m_data->calibrationAssignments.cend() ? nullptr : &it.value();
}

const ExecutionPreparationSnapshot::CalibrationSubsetFacts*
ExecutionPreparationSnapshot::calibrationSubsetFacts(const std::vector<FrameId>& lightFiles) const
{
    const auto it = m_data->calibrationSubsets.constFind(calibrationSubsetKey(lightFiles));
    return it == m_data->calibrationSubsets.cend() ? nullptr : &it.value();
}

void ExecutionPreparationSnapshot::buildCalibrationSubsetFacts(const WorkflowManifest& manifest)
{
    const auto collect_facts = [&](const std::vector<FrameId>& light_files) {
        const QString subset_key = calibrationSubsetKey(light_files);
        if (m_data->calibrationSubsets.contains(subset_key))
            return;

        CalibrationSubsetFacts facts;
        std::vector<WorkflowManifest::LightsCalibrationParams> matching_assignments;
        matching_assignments.reserve(light_files.size());
        QSet<QString> visited_lights;
        visited_lights.reserve(static_cast<qsizetype>(light_files.size()));

        const auto add_dependency = [&](const std::optional<FrameId>& dependency) {
            if (!dependency.has_value())
                return;
            const QString key = pathKey(dependency->canonicalPath());
            if (!key.isEmpty())
                facts.dependencyPaths.insert(dependency->canonicalPath());
        };

        for (const auto& light : light_files)
        {
            const QString key = pathKey(light.canonicalPath());
            if (key.isEmpty() || visited_lights.contains(key))
                continue;
            visited_lights.insert(key);
            ++m_data->diagnostics.calibrationAssignmentVisits;

            const auto assignment = m_data->calibrationAssignments.constFind(key);
            if (assignment == m_data->calibrationAssignments.cend())
                continue;
            matching_assignments.push_back(assignment.value());
            ++facts.matchingAssignmentCount;
            if (assignment->calibrates())
            {
                ++facts.calibratingAssignmentCount;
            }
            add_dependency(assignment->biasPath);
            add_dependency(assignment->darkPath);
            add_dependency(assignment->flatPath);
        }

        if (!matching_assignments.empty())
        {
            facts.subsetHash = computeLightsCalibrationHash(matching_assignments, this);
            facts.allCalibratedArtifactsReusable =
                std::all_of(matching_assignments.cbegin(), matching_assignments.cend(), [&](const auto& params) {
                    // A light with nothing to calibrate it has no calibrated copy to make.
                    if (!params.calibrates())
                    {
                        return true;
                    }
                    const QString output_path =
                        calibratedLightOutputPathFor(manifest.outputDirectory, params.lightPath);
                    return isArtifactValid(output_path, computeCalibratedLightArtifactHash(params, this),
                                           PreparationArtifactValidation::MetadataOnly);
                });
        }

        m_data->calibrationSubsets.insert(subset_key, std::move(facts));
    };

    m_data->calibrationSubsets.reserve(
        static_cast<qsizetype>(manifest.registrationGroups.size() + manifest.integrationGroups.size()));
    for (const auto& group : manifest.registrationGroups)
        collect_facts(group.lightFiles);
    for (const auto& group : manifest.integrationGroups)
        collect_facts(group.lightFiles);
}

bool ExecutionPreparationSnapshot::hasArtifactRecord(const QString& path) const
{
    return m_data->artifactRecords.contains(pathKey(path));
}

bool ExecutionPreparationSnapshot::isArtifactValid(const QString& path, const QString& expectedManifestHash,
                                                   const PreparationArtifactValidation validation) const
{
    const QString key = pathKey(path);
    const auto record = m_data->artifactRecords.constFind(key);
    const auto stamp = m_data->fileStamps.constFind(key);
    if (record == m_data->artifactRecords.cend() || stamp == m_data->fileStamps.cend() || !stamp->isFile ||
        record->manifestHash != expectedManifestHash || record->sizeBytes != stamp->sizeBytes ||
        record->modifiedAtMs != stamp->modifiedAtMs)
        return false;

    // A row must carry at least one form of provenance. Records written by the workflow always
    // do; this only rejects a row written by hand.
    if (record->contentDigest.isEmpty() && record->artifactStamp.isEmpty() && record->identityToken.isEmpty())
        return false;

    if (validation == PreparationArtifactValidation::MetadataOnly)
        return true;

    // A master is decided from a stat or a header read, never from its payload: untouched since it was recorded, or
    // moved or restored and still carrying in its own header the stamp the record holds, which is the manifest
    // entry's identity. A record with no stamp, a header that could not be read and one naming another identity are
    // recomputed. The digest a master's record holds is the run archive's provenance and decides nothing here; see
    // docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md.
    if (!record->identityToken.isEmpty() && record->identityToken == stamp->identityToken)
        return true;

    const auto header = m_data->strictArtifactHeaderIdentities.constFind(key);
    return header != m_data->strictArtifactHeaderIdentities.cend() && !record->artifactStamp.isEmpty() &&
           header.value() == record->artifactStamp && header.value() == manifestEntryIdentity(expectedManifestHash);
}

bool ExecutionPreparationSnapshot::registrationDatabaseUsable() const noexcept
{
    return m_data->registrationDatabaseUsable;
}

const ExecutionPreparationSnapshot::RegistrationGroupState*
ExecutionPreparationSnapshot::registrationGroupState(const QString& groupKey, const int settingsVersion) const
{
    const auto it = m_data->registrationStates.constFind(groupVersionKey(groupKey, settingsVersion));
    return it == m_data->registrationStates.cend() ? nullptr : &it.value();
}

const ExecutionPreparationSnapshot::RegistrationGroupState*
ExecutionPreparationSnapshot::latestRegistrationGroupState(const QString& groupKey) const
{
    const auto latest_key = m_data->latestRegistrationStateKeys.constFind(groupKey);
    if (latest_key == m_data->latestRegistrationStateKeys.cend())
        return nullptr;
    const auto state = m_data->registrationStates.constFind(latest_key.value());
    return state == m_data->registrationStates.cend() ? nullptr : &state.value();
}

const ExecutionPreparationSnapshot::RegistrationMetrics*
ExecutionPreparationSnapshot::registrationMetrics(const QString& groupKey, const int settingsVersion) const
{
    const auto it = m_data->registrationMetrics.constFind(groupVersionKey(groupKey, settingsVersion));
    return it == m_data->registrationMetrics.cend() ? nullptr : &it.value();
}

std::optional<int> ExecutionPreparationSnapshot::resolveRegistrationSettingsVersion(const QString& groupKey,
                                                                                    const int preferredVersion) const
{
    const RegistrationMetrics* preferred = registrationMetrics(groupKey, preferredVersion);
    if (preferred != nullptr && !preferred->isEmpty())
        return preferredVersion;

    const auto latest = m_data->latestRegistrationMetricVersions.constFind(groupKey);
    return latest == m_data->latestRegistrationMetricVersions.cend() ? std::nullopt
                                                                     : std::optional<int>(latest.value());
}

std::optional<qint64> ExecutionPreparationSnapshot::starCacheMtime(const QString& groupKey,
                                                                   const QString& detectionHash,
                                                                   const FrameId& frame) const
{
    const auto* stamp = starCacheInputStamp(groupKey, detectionHash, frame);
    return stamp == nullptr ? std::nullopt : std::optional<qint64>(stamp->inputMtime);
}

std::optional<QString> ExecutionPreparationSnapshot::starCacheInputPath(const QString& groupKey,
                                                                        const QString& detectionHash,
                                                                        const FrameId& frame) const
{
    const auto* stamp = starCacheInputStamp(groupKey, detectionHash, frame);
    return stamp == nullptr ? std::nullopt : std::optional<QString>(stamp->inputPath);
}

const ExecutionPreparationSnapshot::StarCacheInputStamp*
ExecutionPreparationSnapshot::starCacheInputStamp(const QString& groupKey, const QString& detectionHash,
                                                  const FrameId& frame) const
{
    const auto it =
        m_data->starCacheInputStamps.constFind(starCacheKey(groupKey, detectionHash, pathKey(frame.canonicalPath())));
    return it == m_data->starCacheInputStamps.cend() ? nullptr : &it.value();
}

const ExecutionPreparationSnapshot::ParsedMetadata*
ExecutionPreparationSnapshot::normalizationMetadata(const IntegrationGroupKey& groupKey) const
{
    const auto it = m_data->normalizationMetadata.constFind(groupKey.toString());
    return it == m_data->normalizationMetadata.cend() ? nullptr : &it.value();
}

std::optional<QString>
ExecutionPreparationSnapshot::normalizationCoefficientDigest(const IntegrationGroupKey& groupKey) const
{
    const auto it = m_data->normalizationCoefficientDigests.constFind(groupKey.toString());
    return it == m_data->normalizationCoefficientDigests.cend() ? std::nullopt : std::optional<QString>(it.value());
}

const ExecutionPreparationSnapshot::ParsedMetadata*
ExecutionPreparationSnapshot::drizzleMetadata(const IntegrationGroupKey& groupKey) const
{
    const auto it = m_data->drizzleMetadata.constFind(groupKey.toString());
    return it == m_data->drizzleMetadata.cend() ? nullptr : &it.value();
}

const ExecutionPreparationSnapshot::DrizzleSourceGeometryFacts*
ExecutionPreparationSnapshot::drizzleSourceGeometry(const FrameId& frame) const
{
    const auto it = m_data->drizzleSourceGeometry.constFind(frame.identityKey());
    return it == m_data->drizzleSourceGeometry.cend() ? nullptr : &it.value();
}
