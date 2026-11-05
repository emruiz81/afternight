// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameQuality.h"
#include "FilterStatistics.h"
#include "processes/ImageIntegration/core/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <QSet>
#include <limits>
#include <QHash>
#include <QString>

using ImageIntegration::clamp01;

namespace
{
[[nodiscard]] bool hasFiniteNonZero(const double value)
{
    return std::isfinite(value) && std::abs(value) > 1.0e-12;
}

[[nodiscard]] bool hasFinitePositive(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

void appendUnique(std::vector<FrameId>& ids, const FrameId& id)
{
    if (std::find(ids.begin(), ids.end(), id) == ids.end())
    {
        ids.push_back(id);
    }
}

template <typename ValueFn, typename IsOutlierFn>
void detectMetricOutliers(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates, ValueFn&& valueFn,
                          IsOutlierFn&& isOutlierFn, const double sigma, std::vector<FrameId>& outliers)
{
    if (!std::isfinite(sigma) || sigma <= 0.0)
    {
        return;
    }

    std::vector<double> values;
    values.reserve(candidates.size());
    for (const auto& [_, metrics] : candidates)
    {
        if (!metrics.succeeded)
        {
            continue;
        }

        const double value = valueFn(metrics);
        if (std::isfinite(value))
        {
            values.push_back(value);
        }
    }

    if (values.size() < 2)
    {
        return;
    }

    const double median = ImageIntegration::medianOf(values);

    double sum_sq = 0.0;
    for (const double value : values)
    {
        const double delta = value - median;
        sum_sq += delta * delta;
    }

    const double stddev = std::sqrt(sum_sq / static_cast<double>(values.size()));
    if (!std::isfinite(stddev) || stddev <= 0.0)
    {
        return;
    }

    for (const auto& [id, metrics] : candidates)
    {
        if (!metrics.succeeded)
        {
            continue;
        }

        const double value = valueFn(metrics);
        if (!std::isfinite(value))
        {
            continue;
        }

        if (isOutlierFn(value, median, stddev, sigma))
        {
            appendUnique(outliers, id);
        }
    }
}
} // namespace

double frameQualityScore(const FrameQualityMetrics& metrics)
{
    // Quality score combining multiple frame metrics.
    // Higher scores indicate better quality frames suitable as references.
    //
    // Components:
    // - stars_score: More detected stars = better (log scale for diminishing returns)
    // - snr_score: Higher signal-to-noise = better (median star amplitude / noise)
    // - fwhm_penalty: Lower FWHM = sharper stars = better
    // - ecc_penalty: Lower eccentricity = rounder stars = better
    //
    // NOTE: FWHM varies by filter type (narrowband typically larger than broadband).
    // This score is best used for comparing frames of the SAME filter type within
    // a registration group. Cross-filter comparisons may unfairly penalize narrowband.
    // For filter-fair comparison, use the overload with FilterStatistics.
    //
    // Formula:
    //   log(1+stars)*10 + log(1+snr)*2 - fwhm*5 - eccentricity*10
    //   + zero_point*4 - zero_point_scatter*20 - astro_rms*8
    //   + log(1+catalog_matched)*2 + limiting_mag*0.5

    if (!metrics.succeeded)
    {
        return -std::numeric_limits<double>::infinity();
    }

    const double stars_score = std::log1p(static_cast<double>((std::max)(0, metrics.stars))) * 10.0;

    // Lower is better.
    const double fwhm_penalty = (std::max)(0.0, metrics.fwhm) * 5.0;

    // Higher is better.
    const double snr_score = std::log1p((std::max)(0.0, metrics.snr)) * 2.0;

    // Lower is better. Eccentricity is typically [0..1].
    const double ecc_penalty = clamp01(metrics.eccentricity) * 10.0;

    const double zero_point_score =
        hasFiniteNonZero(metrics.photometric_zero_point) ? metrics.photometric_zero_point * 4.0 : 0.0;

    const double scatter_penalty =
        hasFinitePositive(metrics.zero_point_scatter) ? metrics.zero_point_scatter * 20.0 : 0.0;

    const double astrometric_penalty =
        hasFinitePositive(metrics.astrometric_rms_arcsec) ? metrics.astrometric_rms_arcsec * 8.0 : 0.0;

    const double catalog_match_score =
        std::log1p(static_cast<double>((std::max)(0, metrics.catalog_matched_stars))) * 2.0;

    const double limiting_mag_score =
        hasFinitePositive(metrics.limiting_magnitude) ? metrics.limiting_magnitude * 0.5 : 0.0;

    return stars_score + snr_score - fwhm_penalty - ecc_penalty + zero_point_score - scatter_penalty -
           astrometric_penalty + catalog_match_score + limiting_mag_score;
}

double frameQualityScore(const FrameQualityMetrics& metrics, const std::optional<FilterStatistics>& filterStats)
{
    // Delegate to FilterStatisticsEngine which handles both absolute
    // and filter-relative scoring depending on filterStats validity
    return FilterStatisticsEngine::computeQualityScore(metrics, filterStats);
}

std::optional<FrameId> selectBestReferenceFrame(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates)
{
    if (candidates.empty())
    {
        return std::nullopt;
    }

    const auto best_it = std::max_element(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        const double sa = frameQualityScore(a.second);
        const double sb = frameQualityScore(b.second);
        if (sa == sb)
        {
            return a.first < b.first;
        }
        return sa < sb;
    });

    if (best_it == candidates.end() || !best_it->second.succeeded)
    {
        return std::nullopt;
    }

    return best_it->first;
}

std::optional<FrameId>
selectBestReferenceFrameAuto(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates,
                             const std::function<QString(const FrameId&)>& getFilter,
                             const AutoReferenceSelection& selection)
{
    if (candidates.empty())
    {
        return std::nullopt;
    }

    if (selection.mode == AutoReferenceMode::AbsoluteAll)
    {
        return selectBestReferenceFrame(candidates);
    }

    if (selection.mode == AutoReferenceMode::AbsoluteSingleFilter)
    {
        std::vector<std::pair<FrameId, FrameQualityMetrics>> filtered;
        filtered.reserve(candidates.size());
        for (const auto& c : candidates)
        {
            const QString filter = getFilter(c.first);
            if (filter.compare(selection.filterName, Qt::CaseInsensitive) == 0)
            {
                filtered.push_back(c);
            }
        }
        return selectBestReferenceFrame(filtered);
    }

    // FilterNormalizedAcrossFilters
    FilterStatisticsEngine engine;
    const auto statsByFilter = engine.computeStatistics(candidates, getFilter);

    const auto best_it = std::max_element(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        auto scoreFor = [&](const auto& p) -> double {
            if (!p.second.succeeded)
            {
                return -std::numeric_limits<double>::infinity();
            }

            const QString filter = getFilter(p.first);
            const auto it = statsByFilter.find(filter);
            const std::optional<FilterStatistics> stats =
                (it != statsByFilter.end()) ? std::optional<FilterStatistics>(it->second) : std::nullopt;
            return frameQualityScore(p.second, stats);
        };

        const double sa = scoreFor(a);
        const double sb = scoreFor(b);
        if (sa == sb)
        {
            return a.first < b.first;
        }
        return sa < sb;
    });

    if (best_it == candidates.end() || !best_it->second.succeeded)
    {
        return std::nullopt;
    }

    return best_it->first;
}

AutoReferenceDecision
selectBestReferenceFrameAutoSmart(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates,
                                  const std::function<QString(const FrameId&)>& getFilter)
{
    AutoReferenceDecision out;
    if (candidates.empty())
    {
        return out;
    }

    QSet<QString> named;
    named.reserve(static_cast<qsizetype>(candidates.size()));
    for (const auto& c : candidates)
    {
        const QString f = getFilter ? getFilter(c.first).trimmed() : QString();
        if (f.isEmpty() || f == QStringLiteral("(unknown)"))
        {
            continue;
        }
        named.insert(f);
    }
    out.namedFilterCount = named.size();

    if (named.size() >= 2 && getFilter)
    {
        out.modeUsed = AutoReferenceMode::FilterNormalizedAcrossFilters;

        std::vector<std::pair<FrameId, FrameQualityMetrics>> normalized_candidates;
        normalized_candidates.reserve(candidates.size());
        for (const auto& c : candidates)
        {
            const QString f = getFilter(c.first).trimmed();
            if (f.isEmpty() || f == QStringLiteral("(unknown)"))
            {
                ++out.excludedUnknownCount;
                continue;
            }
            normalized_candidates.push_back(c);
        }

        if (!normalized_candidates.empty())
        {
            AutoReferenceSelection sel;
            sel.mode = AutoReferenceMode::FilterNormalizedAcrossFilters;
            out.best = selectBestReferenceFrameAuto(normalized_candidates, getFilter, sel);
        }

        if (!out.best.has_value())
        {
            out.modeUsed = AutoReferenceMode::AbsoluteAll;
            out.best = selectBestReferenceFrame(candidates);
        }

        return out;
    }

    out.modeUsed = AutoReferenceMode::AbsoluteAll;
    out.best = selectBestReferenceFrame(candidates);
    return out;
}

std::vector<FrameId> detectOutliers(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& candidates,
                                    double sigmaLow)
{
    std::vector<FrameId> outliers;
    bool hasExtendedMetrics = false;

    for (const auto& pair : candidates)
    {
        if (!pair.second.succeeded)
        {
            appendUnique(outliers, pair.first);
        }
        else if (hasFiniteNonZero(pair.second.photometric_zero_point) ||
                 hasFinitePositive(pair.second.astrometric_rms_arcsec))
        {
            hasExtendedMetrics = true;
        }
    }

    if (candidates.empty())
    {
        return outliers;
    }

    if (hasExtendedMetrics)
    {
        detectMetricOutliers(
            candidates,
            [](const FrameQualityMetrics& metrics) -> double {
                return hasFiniteNonZero(metrics.photometric_zero_point) ? metrics.photometric_zero_point
                                                                        : std::numeric_limits<double>::quiet_NaN();
            },
            [](double value, double median, double stddev, double sigma) { return value < (median - sigma * stddev); },
            sigmaLow, outliers);

        detectMetricOutliers(
            candidates,
            [](const FrameQualityMetrics& metrics) -> double {
                return hasFinitePositive(metrics.astrometric_rms_arcsec) ? metrics.astrometric_rms_arcsec
                                                                         : std::numeric_limits<double>::quiet_NaN();
            },
            [](double value, double median, double stddev, double sigma) { return value > (median + sigma * stddev); },
            sigmaLow, outliers);

        return outliers;
    }

    std::vector<double> scores;
    scores.reserve(candidates.size());
    for (const auto& pair : candidates)
    {
        if (pair.second.succeeded)
        {
            scores.push_back(frameQualityScore(pair.second));
        }
    }

    if (scores.empty())
    {
        return outliers;
    }

    std::sort(scores.begin(), scores.end());
    const size_t middle = scores.size() / 2;
    const double median = (scores.size() % 2) == 0 ? (scores[middle - 1] + scores[middle]) * 0.5 : scores[middle];

    double sq_sum = 0.0;
    for (const double s : scores)
    {
        sq_sum += (s - median) * (s - median);
    }
    const double stddev = std::sqrt(sq_sum / static_cast<double>(scores.size()));

    const double threshold = median - (sigmaLow * stddev);

    for (const auto& pair : candidates)
    {
        if (pair.second.succeeded)
        {
            const double s = frameQualityScore(pair.second);
            if (s < threshold)
            {
                appendUnique(outliers, pair.first);
            }
        }
    }

    return outliers;
}

std::vector<FrameId>
rankFramesByQualityScore(const std::vector<FrameId>& frameIds,
                         const std::function<std::optional<FrameQualityMetrics>(const FrameId&)>& getMetrics,
                         const std::function<QString(const FrameId&)>& getFilter)
{
    struct Candidate
    {
        FrameId id;
        FrameQualityMetrics metrics;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(frameIds.size());

    bool hasAnyNamedFilter = false;
    for (const auto& id : frameIds)
    {
        const auto metrics = getMetrics(id);
        if (!metrics.has_value())
        {
            continue;
        }

        const QString filter = getFilter(id).trimmed();
        if (!filter.isEmpty())
        {
            hasAnyNamedFilter = true;
        }

        candidates.push_back({id, *metrics});
    }

    if (candidates.empty())
    {
        return {};
    }

    QHash<QString, std::vector<FrameQualityMetrics>> metricsByFilter;
    metricsByFilter.reserve(static_cast<int>(candidates.size()));

    const auto filterKeyFor = [&](const FrameId& id) {
        const QString filter = getFilter(id).trimmed();
        if (filter.isEmpty())
        {
            return hasAnyNamedFilter ? QStringLiteral("(unknown)") : QStringLiteral("(no filter)");
        }
        return filter;
    };

    for (const auto& c : candidates)
    {
        const QString key = filterKeyFor(c.id);
        metricsByFilter[key].push_back(c.metrics);
    }

    QHash<QString, FilterStatistics> filterStats;
    for (auto it = metricsByFilter.begin(); it != metricsByFilter.end(); ++it)
    {
        const QString key = it.key();
        if (hasAnyNamedFilter && key == QStringLiteral("(unknown)"))
        {
            continue;
        }

        const auto& list = it.value();
        if (list.size() < 2)
        {
            continue;
        }

        filterStats.insert(key, FilterStatisticsEngine::computeStatistics(key, list));
    }

    struct Scored
    {
        FrameId id;
        double score = -std::numeric_limits<double>::infinity();
    };

    std::vector<Scored> scored;
    scored.reserve(candidates.size());

    for (const auto& c : candidates)
    {
        const QString key = filterKeyFor(c.id);
        double score = frameQualityScore(c.metrics);
        if (filterStats.contains(key))
        {
            score = frameQualityScore(c.metrics, filterStats.value(key));
        }

        scored.push_back({c.id, score});
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score == b.score)
        {
            return a.id < b.id;
        }
        return a.score > b.score;
    });

    std::vector<FrameId> ordered;
    ordered.reserve(scored.size());
    for (const auto& entry : scored)
    {
        ordered.push_back(entry.id);
    }

    return ordered;
}
