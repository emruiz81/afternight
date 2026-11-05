// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The registration database read by the stages that consume a registration: each frame's latest output record and
/// transform, the group's output plane and reference, and the frames' widths. Every transform is read through the
/// format 5 record reader, which reads engine pixel coordinates and refuses a record of another format.

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace stage_utils
{
struct LatestRegistrationFrameRecord
{
    FrameQualityMetrics metrics;
    std::optional<an::RegistrationTransform> transform;
};

struct RegistrationOutputRecord
{
    bool succeeded = false;
    QString outputPath;
    qint64 outputSizeBytes = -1;
    qint64 outputModifiedAtMs = -1;
    QString outputIdentityToken;
    qint64 updatedAt = 0;
};

[[nodiscard]] inline std::optional<FrameId> persistedRegistrationFrameId(const QString& storedFrameId)
{
    if (storedFrameId.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    try
    {
        return FrameId(storedFrameId);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

[[nodiscard]] inline bool preferPersistedRegistrationRow(const qint64 candidateUpdatedAt,
                                                         const QString& candidateStoredFrameId,
                                                         const qint64 selectedUpdatedAt,
                                                         const QString& selectedStoredFrameId)
{
    if (candidateUpdatedAt != selectedUpdatedAt)
    {
        return candidateUpdatedAt > selectedUpdatedAt;
    }
    const auto candidate = persistedRegistrationFrameId(candidateStoredFrameId);
    const auto selected = persistedRegistrationFrameId(selectedStoredFrameId);
    if (!candidate.has_value())
    {
        return false;
    }
    if (!selected.has_value())
    {
        return true;
    }
    const bool candidateCanonical = candidateStoredFrameId == candidate->canonicalPath();
    const bool selectedCanonical = selectedStoredFrameId == selected->canonicalPath();
    if (candidateCanonical != selectedCanonical)
    {
        return candidateCanonical;
    }
    const int canonicalComparison =
        QString::compare(candidate->canonicalPath(), selected->canonicalPath(), Qt::CaseSensitive);
    return canonicalComparison != 0
               ? canonicalComparison < 0
               : QString::compare(candidateStoredFrameId, selectedStoredFrameId, Qt::CaseSensitive) < 0;
}

template <typename Value>
[[nodiscard]] inline bool rekeyRegistrationFrameLookupByIdentity(std::unordered_map<QString, Value>& values,
                                                                 QString* error = nullptr)
{
    std::unordered_map<QString, QString> identityByStoredKey;
    identityByStoredKey.reserve(values.size());
    std::unordered_set<QString> identities;
    identities.reserve(values.size());
    for (const auto& [storedKey, _] : values)
    {
        const auto frameId = persistedRegistrationFrameId(storedKey);
        if (!frameId.has_value())
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Invalid persisted registration frame ID '%1'.").arg(storedKey);
            }
            return false;
        }
        const QString identityKey = frameId->identityKey();
        if (!identities.insert(identityKey).second)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Duplicate persisted registration frame identity '%1'.").arg(identityKey);
            }
            return false;
        }
        identityByStoredKey.emplace(storedKey, identityKey);
    }

    std::unordered_map<QString, Value> byIdentity;
    byIdentity.reserve(values.size());
    for (auto& [storedKey, value] : values)
    {
        byIdentity.emplace(identityByStoredKey.at(storedKey), std::move(value));
    }
    values = std::move(byIdentity);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

class RegistrationMetricsReader
{
public:
    class ScopedReadSnapshot
    {
    public:
        explicit ScopedReadSnapshot(RegistrationMetricsReader* owner) noexcept : m_owner(owner) {}
        ~ScopedReadSnapshot()
        {
            if (m_owner != nullptr)
                (void)m_owner->m_connection.rollbackTransaction();
        }

        ScopedReadSnapshot(const ScopedReadSnapshot&) = delete;
        ScopedReadSnapshot& operator=(const ScopedReadSnapshot&) = delete;
        ScopedReadSnapshot(ScopedReadSnapshot&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)) {}
        ScopedReadSnapshot& operator=(ScopedReadSnapshot&&) = delete;

    private:
        RegistrationMetricsReader* m_owner = nullptr;
    };

    explicit RegistrationMetricsReader(QString databasePath, QString connectionPrefix,
                                       WorkflowExecutionCounterAccumulator* counters = nullptr)
        : m_connection(std::move(databasePath), std::move(connectionPrefix), counters)
    {
    }

    [[nodiscard]] bool open(QString* error = nullptr)
    {
        return m_connection.open(error);
    }

    [[nodiscard]] bool isOpen() const
    {
        return m_connection.isOpen();
    }

    [[nodiscard]] std::optional<ScopedReadSnapshot> beginReadSnapshot()
    {
        if (!m_connection.isOpen() || !m_connection.beginTransaction())
            return std::nullopt;
        return ScopedReadSnapshot(this);
    }

    [[nodiscard]] std::optional<int> maxSettingsVersionForGroup(const QString& group_key)
    {
        if (!m_connection.isOpen())
        {
            return std::nullopt;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(
            QStringLiteral("SELECT MAX(settings_version) FROM registration_metrics WHERE group_key = :group_key"));
        query.bindValue(QStringLiteral(":group_key"), group_key);
        if (!m_connection.execute(query))
        {
            return std::nullopt;
        }

        if (!query.next())
        {
            return std::nullopt;
        }

        if (query.value(0).isNull())
        {
            return std::nullopt;
        }

        return query.value(0).toInt();
    }

    [[nodiscard]] bool hasMetricsForGroupVersion(const QString& group_key, int settings_version)
    {
        if (!m_connection.isOpen())
        {
            return false;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(QStringLiteral("SELECT 1 FROM registration_metrics "
                                     "WHERE group_key = :group_key AND settings_version = :settings_version "
                                     "LIMIT 1"));
        query.bindValue(QStringLiteral(":group_key"), group_key);
        query.bindValue(QStringLiteral(":settings_version"), settings_version);
        if (!m_connection.execute(query))
        {
            return false;
        }

        return query.next();
    }

    [[nodiscard]] std::optional<int> resolveSettingsVersionForGroup(const QString& group_key,
                                                                    std::optional<int> preferred_version)
    {
        if (preferred_version.has_value() && hasMetricsForGroupVersion(group_key, *preferred_version))
        {
            return preferred_version;
        }

        return maxSettingsVersionForGroup(group_key);
    }

    [[nodiscard]] bool loadOutputs(const QString& group_key, int settings_version,
                                   std::unordered_map<QString, QString>& out_path_by_frame)
    {
        std::unordered_map<QString, RegistrationOutputRecord> records;
        if (!loadOutputRecords(group_key, settings_version, records))
        {
            return false;
        }

        out_path_by_frame.clear();
        for (const auto& [frame_id, record] : records)
        {
            if (record.succeeded && !record.outputPath.isEmpty())
            {
                out_path_by_frame[frame_id] = record.outputPath;
            }
        }

        return true;
    }

    [[nodiscard]] bool loadOutputRecords(const QString& group_key, int settings_version,
                                         std::unordered_map<QString, RegistrationOutputRecord>& out_by_frame)
    {
        out_by_frame.clear();

        if (!m_connection.isOpen())
        {
            return false;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(
            QStringLiteral("SELECT frame_id, succeeded, output_path, output_size_bytes, "
                           "output_modified_at_ms, output_identity_token, updated_at FROM registration_metrics "
                           "WHERE group_key = :group_key AND settings_version = :settings_version"));
        query.bindValue(QStringLiteral(":group_key"), group_key);
        query.bindValue(QStringLiteral(":settings_version"), settings_version);
        if (!m_connection.execute(query))
        {
            return false;
        }

        struct SelectedOutput
        {
            QString storedFrameId;
            FrameId frameId;
            RegistrationOutputRecord record;
        };
        std::unordered_map<QString, SelectedOutput> selectedByIdentity;
        while (query.next())
        {
            const QString storedFrameId = query.value(0).toString();
            const auto frameId = persistedRegistrationFrameId(storedFrameId);
            if (!frameId.has_value())
            {
                continue;
            }

            RegistrationOutputRecord record;
            record.succeeded = query.value(1).toInt() != 0;
            record.outputPath = query.value(2).toString();
            record.outputSizeBytes = query.value(3).toLongLong();
            record.outputModifiedAtMs = query.value(4).toLongLong();
            record.outputIdentityToken = query.value(5).toString();
            record.updatedAt = query.value(6).toLongLong();
            const auto selected = selectedByIdentity.find(frameId->identityKey());
            if (selected == selectedByIdentity.end() ||
                preferPersistedRegistrationRow(record.updatedAt, storedFrameId, selected->second.record.updatedAt,
                                               selected->second.storedFrameId))
            {
                selectedByIdentity.insert_or_assign(frameId->identityKey(),
                                                    SelectedOutput{storedFrameId, *frameId, std::move(record)});
            }
        }
        out_by_frame.reserve(selectedByIdentity.size());
        for (auto& [_, selected] : selectedByIdentity)
        {
            out_by_frame.emplace(selected.frameId.canonicalPath(), std::move(selected.record));
        }

        return true;
    }

    [[nodiscard]] bool loadTransforms(const QString& group_key, int settings_version,
                                      std::unordered_map<QString, an::RegistrationTransform>& out, QString& error,
                                      std::unordered_map<QString, SourceDistortionProvenance>* out_provenance = nullptr)
    {
        out.clear();
        if (out_provenance != nullptr)
        {
            out_provenance->clear();
        }
        if (!m_connection.isOpen())
        {
            error = QStringLiteral("Registration metrics database is not open.");
            return false;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(QStringLiteral("SELECT frame_id, succeeded, transform_json, updated_at FROM registration_metrics "
                                     "WHERE group_key = :group_key AND settings_version = :settings_version"));
        query.bindValue(QStringLiteral(":group_key"), group_key);
        query.bindValue(QStringLiteral(":settings_version"), settings_version);
        if (!m_connection.execute(query))
        {
            error = QStringLiteral("Failed to read registration transforms.");
            return false;
        }

        struct SelectedTransform
        {
            QString storedFrameId;
            FrameId frameId;
            bool succeeded = false;
            QString transformJson;
            qint64 updatedAt = 0;
        };
        std::unordered_map<QString, SelectedTransform> selectedByIdentity;
        while (query.next())
        {
            const QString storedFrameId = query.value(0).toString();
            const auto frameId = persistedRegistrationFrameId(storedFrameId);
            if (!frameId.has_value())
            {
                continue;
            }

            SelectedTransform candidate;
            candidate.storedFrameId = storedFrameId;
            candidate.frameId = *frameId;
            candidate.succeeded = query.value(1).toInt() != 0;
            candidate.transformJson = query.value(2).toString();
            candidate.updatedAt = query.value(3).toLongLong();
            const auto selected = selectedByIdentity.find(frameId->identityKey());
            if (selected == selectedByIdentity.end() ||
                preferPersistedRegistrationRow(candidate.updatedAt, storedFrameId, selected->second.updatedAt,
                                               selected->second.storedFrameId))
            {
                selectedByIdentity.insert_or_assign(frameId->identityKey(), std::move(candidate));
            }
        }

        for (const auto& [_, selected] : selectedByIdentity)
        {
            if (!selected.succeeded || selected.transformJson.isEmpty())
            {
                continue;
            }

            an::RegistrationTransform transform;
            SourceDistortionProvenance provenance;
            QString parse_error;
            if (!stage_utils::parseRegistrationTransformJson(
                    selected.transformJson, transform, out_provenance != nullptr ? &provenance : nullptr, parse_error))
            {
                error = parse_error;
                return false;
            }

            out.insert_or_assign(selected.frameId.canonicalPath(), transform);
            if (out_provenance != nullptr)
            {
                (*out_provenance)[selected.frameId.canonicalPath()] = std::move(provenance);
            }
        }

        return true;
    }

    [[nodiscard]] bool loadLatestRecordsForFrames(const std::vector<FrameId>& frames,
                                                  std::unordered_map<FrameId, LatestRegistrationFrameRecord>& out,
                                                  QString& error)
    {
        out.clear();
        error.clear();

        if (!m_connection.isOpen())
        {
            error = QStringLiteral("Registration metrics database is not open.");
            return false;
        }

        QSet<FrameId> allowedFrames;
        allowedFrames.reserve(static_cast<int>(frames.size()));
        for (const auto& frame : frames)
        {
            if (frame.isValid())
            {
                allowedFrames.insert(frame);
            }
        }

        if (allowedFrames.isEmpty())
        {
            return true;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(QStringLiteral(
            "SELECT frame_id, stars, fwhm, snr, eccentricity, photometric_zero_point, zero_point_scatter, "
            "astrometric_rms_pixels, astrometric_rms_arcsec, limiting_magnitude, catalog_matched_stars, "
            "succeeded, transform_json "
            "FROM registration_metrics "
            "ORDER BY updated_at DESC, settings_version DESC, group_key DESC, frame_id ASC"));
        if (!m_connection.execute(query))
        {
            error = query.lastError().text();
            return false;
        }

        QSet<FrameId> seenFrames;
        seenFrames.reserve(allowedFrames.size());
        while (query.next())
        {
            const QString frame_id = query.value(0).toString().trimmed();
            if (frame_id.isEmpty())
            {
                continue;
            }

            const FrameId canonical(frame_id);
            if (!allowedFrames.contains(canonical) || seenFrames.contains(canonical))
            {
                continue;
            }
            seenFrames.insert(canonical);

            LatestRegistrationFrameRecord record;
            record.metrics.stars = query.value(1).toInt();
            record.metrics.fwhm = query.value(2).toDouble();
            record.metrics.snr = query.value(3).toDouble();
            record.metrics.eccentricity = query.value(4).toDouble();
            record.metrics.photometric_zero_point = query.value(5).toDouble();
            record.metrics.zero_point_scatter = query.value(6).toDouble();
            record.metrics.astrometric_rms_pixels = query.value(7).toDouble();
            record.metrics.astrometric_rms_arcsec = query.value(8).toDouble();
            record.metrics.limiting_magnitude = query.value(9).toDouble();
            record.metrics.catalog_matched_stars = query.value(10).toInt();
            record.metrics.succeeded = query.value(11).toInt() != 0;

            const QString transform_json = query.value(12).toString();
            if (record.metrics.succeeded && !transform_json.isEmpty())
            {
                an::RegistrationTransform transform;
                QString parse_error;
                if (!stage_utils::parseRegistrationTransformJson(transform_json, transform, nullptr, parse_error))
                {
                    error = parse_error;
                    return false;
                }
                record.transform = std::move(transform);
            }

            out[canonical] = std::move(record);
            if (seenFrames.size() == allowedFrames.size())
            {
                break;
            }
        }

        return true;
    }

    [[nodiscard]] std::optional<QString> readOutputWcsJson(const GroupKey& group_key, int settings_version)
    {
        if (!m_connection.isOpen())
        {
            return std::nullopt;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(QStringLiteral("SELECT output_wcs_json FROM registration_group_state "
                                     "WHERE group_key = :group_key AND settings_version = :settings_version"));
        query.bindValue(QStringLiteral(":group_key"), group_key.toString());
        query.bindValue(QStringLiteral(":settings_version"), settings_version);
        if (!m_connection.execute(query) || !query.next())
        {
            return std::nullopt;
        }

        return query.value(0).toString();
    }

    [[nodiscard]] std::optional<QString> readReferenceFrameId(const GroupKey& group_key, int settings_version)
    {
        if (!m_connection.isOpen())
        {
            return std::nullopt;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(QStringLiteral("SELECT reference_frame_id FROM registration_group_state "
                                     "WHERE group_key = :group_key AND settings_version = :settings_version"));
        query.bindValue(QStringLiteral(":group_key"), group_key.toString());
        query.bindValue(QStringLiteral(":settings_version"), settings_version);
        if (!m_connection.execute(query) || !query.next())
        {
            return std::nullopt;
        }

        return query.value(0).toString();
    }

    /**
     * @brief Load per-frame FWHM values for a registration group.
     *
     * @param group_key        Registration group key
     * @param settings_version Settings version to query
     * @param out              Output: FWHM values (pixels) for all succeeded frames
     * @return True on success, false on query failure
     */
    [[nodiscard]] bool loadFwhmValues(const QString& group_key, int settings_version, std::vector<double>& out)
    {
        if (!m_connection.isOpen())
        {
            return false;
        }

        QSqlQuery query(m_connection.db());
        query.prepare(QStringLiteral("SELECT fwhm FROM registration_metrics "
                                     "WHERE group_key = :group_key AND settings_version = :settings_version "
                                     "AND succeeded = 1 AND fwhm > 0"));
        query.bindValue(QStringLiteral(":group_key"), group_key);
        query.bindValue(QStringLiteral(":settings_version"), settings_version);
        if (!m_connection.execute(query))
        {
            return false;
        }

        while (query.next())
        {
            out.push_back(query.value(0).toDouble());
        }

        return true;
    }

private:
    core::SqliteConnection m_connection;
};
} // namespace stage_utils
