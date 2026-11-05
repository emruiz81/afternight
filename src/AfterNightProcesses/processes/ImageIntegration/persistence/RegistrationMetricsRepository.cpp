// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationMetricsRepository.h"

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"

#include <AfterNightPlatform/Logging.h>

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

namespace
{
[[nodiscard]] bool preferIdentityRow(const qint64 candidate_updated_at, const QString& candidate_stored_frame_id,
                                     const FrameId& candidate_id, const qint64 selected_updated_at,
                                     const QString& selected_stored_frame_id, const FrameId& selected_id)
{
    if (candidate_updated_at != selected_updated_at)
    {
        return candidate_updated_at > selected_updated_at;
    }

    const bool candidate_canonical = candidate_stored_frame_id == candidate_id.canonicalPath();
    const bool selected_canonical = selected_stored_frame_id == selected_id.canonicalPath();
    if (candidate_canonical != selected_canonical)
    {
        return candidate_canonical;
    }

    const int canonical_comparison =
        QString::compare(candidate_id.canonicalPath(), selected_id.canonicalPath(), Qt::CaseSensitive);
    return canonical_comparison != 0
               ? canonical_comparison < 0
               : QString::compare(candidate_stored_frame_id, selected_stored_frame_id, Qt::CaseSensitive) < 0;
}
} // namespace

RegistrationMetricsRepository::RegistrationMetricsRepository(QString metricsPath,
                                                             WorkflowExecutionCounterAccumulator* counters)
    : m_metricsPath(std::move(metricsPath)), m_counters(counters)
{
}

bool RegistrationMetricsRepository::loadCandidates(const QString& groupKey, int settingsVersion,
                                                   const std::vector<FrameId>& allowedFrames,
                                                   std::vector<RegistrationMetricsCandidate>& outCandidates,
                                                   QString& errorOut) const
{
    errorOut.clear();
    outCandidates.clear();

    if (m_metricsPath.trimmed().isEmpty())
    {
        errorOut = QStringLiteral("Missing metrics database path.");
        return false;
    }
    if (!QFileInfo::exists(m_metricsPath))
    {
        errorOut = QStringLiteral("Registration metrics database not found.");
        return false;
    }

    core::SqliteConnection connection(m_metricsPath, QStringLiteral("ImageIntegration_RegMetricsRepo"), m_counters);
    if (!connection.open(&errorOut))
    {
        return false;
    }

    QSet<FrameId> allowed;
    allowed.reserve(static_cast<int>(allowedFrames.size()));
    for (const auto& id : allowedFrames)
    {
        allowed.insert(id);
    }

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("SELECT frame_id, stars, fwhm, snr, eccentricity, "
                                 "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, "
                                 "astrometric_rms_arcsec, limiting_magnitude, catalog_matched_stars, "
                                 "succeeded, updated_at "
                                 "FROM registration_metrics "
                                 "WHERE group_key = :group_key AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);

    if (!connection.execute(query))
    {
        errorOut = query.lastError().text();
        return false;
    }

    struct SelectedCandidate
    {
        QString storedFrameId;
        qint64 updatedAt = 0;
        RegistrationMetricsCandidate candidate;
    };
    QHash<QString, SelectedCandidate> selected_by_identity;
    while (query.next())
    {
        const QString frameId = query.value(0).toString().trimmed();
        if (frameId.isEmpty())
        {
            continue;
        }

        FrameId id;
        try
        {
            id = FrameId(frameId);
        }
        catch (...)
        {
            continue;
        }

        if (!allowed.isEmpty() && !allowed.contains(id))
        {
            continue;
        }

        RegistrationMetricsCandidate candidate;
        candidate.id = id;
        candidate.metrics.stars = query.value(1).toInt();
        candidate.metrics.fwhm = query.value(2).toDouble();
        candidate.metrics.snr = query.value(3).toDouble();
        candidate.metrics.eccentricity = query.value(4).toDouble();
        candidate.metrics.photometric_zero_point = query.value(5).toDouble();
        candidate.metrics.zero_point_scatter = query.value(6).toDouble();
        candidate.metrics.astrometric_rms_pixels = query.value(7).toDouble();
        candidate.metrics.astrometric_rms_arcsec = query.value(8).toDouble();
        candidate.metrics.limiting_magnitude = query.value(9).toDouble();
        candidate.metrics.catalog_matched_stars = query.value(10).toInt();
        candidate.metrics.succeeded = (query.value(11).toInt() != 0);
        const qint64 updated_at = query.value(12).toLongLong();
        auto selected = selected_by_identity.find(id.identityKey());
        if (selected == selected_by_identity.end() ||
            preferIdentityRow(updated_at, frameId, id, selected->updatedAt, selected->storedFrameId,
                              selected->candidate.id))
        {
            selected_by_identity.insert(id.identityKey(), SelectedCandidate{frameId, updated_at, std::move(candidate)});
        }
    }

    outCandidates.reserve(static_cast<size_t>(selected_by_identity.size()));
    for (auto it = selected_by_identity.begin(); it != selected_by_identity.end(); ++it)
    {
        outCandidates.push_back(std::move(it->candidate));
    }
    std::sort(outCandidates.begin(), outCandidates.end(), [](const auto& left, const auto& right) {
        return QString::compare(left.id.canonicalPath(), right.id.canonicalPath(), Qt::CaseSensitive) < 0;
    });

    return true;
}

std::optional<AutoReferencePickResult>
RegistrationReferenceScorer::pickBest(const QString& groupKey,
                                      const std::vector<RegistrationMetricsCandidate>& candidates,
                                      const std::function<QString(const FrameId&)>& getFilter, QString& errorOut)
{
    errorOut.clear();
    if (candidates.empty())
    {
        errorOut = QStringLiteral("No registration metrics available for this group.");
        return std::nullopt;
    }

    std::vector<std::pair<FrameId, FrameQualityMetrics>> scoreCandidates;
    scoreCandidates.reserve(candidates.size());
    for (const auto& candidate : candidates)
    {
        scoreCandidates.emplace_back(candidate.id, candidate.metrics);
    }

    const AutoReferenceDecision decision = selectBestReferenceFrameAutoSmart(scoreCandidates, getFilter);
    if (decision.modeUsed == AutoReferenceMode::FilterNormalizedAcrossFilters)
    {
        if (decision.excludedUnknownCount > 0)
        {
            LOG_INFO("ImageIntegrationProcess: auto-reference using filter-normalized scoring (group='{}', filters={}, "
                     "excluded_unknown={})",
                     groupKey.toStdString(), decision.namedFilterCount, decision.excludedUnknownCount);
        }
        else
        {
            LOG_INFO("ImageIntegrationProcess: auto-reference using filter-normalized scoring (group='{}', filters={})",
                     groupKey.toStdString(), decision.namedFilterCount);
        }
    }
    else
    {
        if (decision.namedFilterCount == 1 && getFilter)
        {
            QString firstFilter = QStringLiteral("(unknown)");
            for (const auto& candidate : candidates)
            {
                const QString filter = getFilter(candidate.id).trimmed();
                if (!filter.isEmpty() && filter != QStringLiteral("(unknown)"))
                {
                    firstFilter = filter;
                    break;
                }
            }
            LOG_INFO("ImageIntegrationProcess: auto-reference using absolute scoring (group='{}', single_filter='{}')",
                     groupKey.toStdString(), firstFilter.toStdString());
        }
        else
        {
            LOG_INFO("ImageIntegrationProcess: auto-reference using absolute scoring (group='{}')",
                     groupKey.toStdString());
        }
    }

    if (!decision.best.has_value())
    {
        errorOut = QStringLiteral("Unable to auto-detect a suitable reference frame from the available metrics.");
        return std::nullopt;
    }

    const auto it =
        std::find_if(candidates.begin(), candidates.end(), [&](const RegistrationMetricsCandidate& candidate) {
            return candidate.id == decision.best.value();
        });
    if (it == candidates.end())
    {
        errorOut = QStringLiteral("Auto-reference selection failed (best frame not found in candidate list).");
        return std::nullopt;
    }

    double score = frameQualityScore(it->metrics);
    if (decision.modeUsed == AutoReferenceMode::FilterNormalizedAcrossFilters)
    {
        std::vector<std::pair<FrameId, FrameQualityMetrics>> scoreCandidatesWithIds;
        scoreCandidatesWithIds.reserve(candidates.size());
        for (const auto& candidate : candidates)
        {
            scoreCandidatesWithIds.emplace_back(candidate.id, candidate.metrics);
        }

        FilterStatisticsEngine engine;
        const auto statsByFilter = engine.computeStatistics(scoreCandidatesWithIds, getFilter);
        const QString filter = getFilter(it->id);
        if (const auto statsIt = statsByFilter.find(filter); statsIt != statsByFilter.end())
        {
            score = frameQualityScore(it->metrics, statsIt->second);
        }
    }

    AutoReferencePickResult out;
    out.id = it->id;
    out.metrics = it->metrics;
    out.score = score;
    return out;
}

std::optional<AutoReferencePickResult> RegistrationMetricsRepository::pickBestReference(
    const QString& groupKey, int settingsVersion, const std::vector<FrameId>& allowedFrames,
    const std::function<QString(const FrameId&)>& getFilter, QString& errorOut) const
{
    std::vector<RegistrationMetricsCandidate> candidates;
    if (!loadCandidates(groupKey, settingsVersion, allowedFrames, candidates, errorOut))
    {
        return std::nullopt;
    }
    return RegistrationReferenceScorer::pickBest(groupKey, candidates, getFilter, errorOut);
}
