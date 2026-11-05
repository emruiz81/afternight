// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "processes/ImageIntegration/model/FrameQuality.h"

#include <gtest/gtest.h>

#include <unordered_map>

namespace
{
FrameQualityMetrics makeMetrics(double fwhm, double snr, int stars)
{
    FrameQualityMetrics m;
    m.succeeded = true;
    m.fwhm = fwhm;
    m.snr = snr;
    m.stars = stars;
    m.eccentricity = 0.45;
    return m;
}
} // namespace

TEST(AutoReferenceSelection, AbsoluteSingleFilterRespectsFilter)
{
    const FrameId l1(QStringLiteral("L_01"));
    const FrameId l2(QStringLiteral("L_02"));
    const FrameId ha1(QStringLiteral("Ha_01"));

    // Make Ha frame look best overall (absolute), but we will select only L.
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    candidates.emplace_back(l1, makeMetrics(2.0, 8.0, 200));
    candidates.emplace_back(l2, makeMetrics(2.1, 9.0, 250));
    candidates.emplace_back(ha1, makeMetrics(5.0, 50.0, 500));

    std::unordered_map<FrameId, QString> filterById;
    filterById.emplace(l1, QStringLiteral("L"));
    filterById.emplace(l2, QStringLiteral("L"));
    filterById.emplace(ha1, QStringLiteral("Ha"));

    const auto getFilter = [&](const FrameId& id) -> QString {
        const auto it = filterById.find(id);
        return (it != filterById.end()) ? it->second : QStringLiteral("(unknown)");
    };

    AutoReferenceSelection sel;
    sel.mode = AutoReferenceMode::AbsoluteSingleFilter;
    sel.filterName = QStringLiteral("L");

    const auto best = selectBestReferenceFrameAuto(candidates, getFilter, sel);
    ASSERT_TRUE(best.has_value());
    EXPECT_TRUE(best.value() == l2);
}

TEST(AutoReferenceSelection, AllFiltersUsesFilterNormalizedScore)
{
    // Construct two filters where absolute scoring favors L (lower FWHM),
    // but filter-normalized scoring should favor Ha due to being a strong
    // outlier vs its own filter baseline.

    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    std::unordered_map<FrameId, QString> filterById;

    const std::vector<double> fwhmOffsets = {-0.1, 0.0, 0.1, 0.0, 0.1};

    // L filter: snr spread gives best only moderately above median.
    const std::vector<double> lSnr = {9.0, 9.5, 10.0, 10.5, 11.0};
    for (int i = 0; i < static_cast<int>(lSnr.size()); ++i)
    {
        const FrameId id(QStringLiteral("L_%1").arg(i));
        candidates.emplace_back(id, makeMetrics(2.0 + fwhmOffsets[static_cast<size_t>(i)], lSnr[i], 200));
        filterById.emplace(id, QStringLiteral("L"));
    }

    // Ha filter: tighter snr distribution => best has higher z-score.
    const std::vector<double> haSnr = {4.8, 5.0, 5.1, 5.2, 5.4};
    for (int i = 0; i < static_cast<int>(haSnr.size()); ++i)
    {
        const FrameId id(QStringLiteral("Ha_%1").arg(i));
        candidates.emplace_back(id, makeMetrics(6.0 + fwhmOffsets[static_cast<size_t>(i)], haSnr[i], 200));
        filterById.emplace(id, QStringLiteral("Ha"));
    }

    const auto getFilter = [&](const FrameId& id) -> QString {
        const auto it = filterById.find(id);
        return (it != filterById.end()) ? it->second : QStringLiteral("(unknown)");
    };

    // Absolute scoring should pick an L frame due to much lower FWHM.
    const auto absBest = selectBestReferenceFrame(candidates);
    ASSERT_TRUE(absBest.has_value());
    EXPECT_TRUE(getFilter(*absBest) == QStringLiteral("L"));

    // Filter-normalized scoring across filters should pick the best Ha frame.
    AutoReferenceSelection sel;
    sel.mode = AutoReferenceMode::FilterNormalizedAcrossFilters;

    const auto normBest = selectBestReferenceFrameAuto(candidates, getFilter, sel);
    ASSERT_TRUE(normBest.has_value());
    EXPECT_TRUE(getFilter(*normBest) == QStringLiteral("Ha"));
}

TEST(AutoReferenceSelection, AutoSmartFallsBackWhenFiltersUnknown)
{
    const FrameId a(QStringLiteral("A_01"));
    const FrameId b(QStringLiteral("A_02"));

    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    candidates.emplace_back(a, makeMetrics(2.0, 8.0, 200));
    candidates.emplace_back(b, makeMetrics(1.9, 7.0, 180));

    const auto getFilter = [](const FrameId&) -> QString { return QStringLiteral("(unknown)"); };

    const auto expected = selectBestReferenceFrame(candidates);
    ASSERT_TRUE(expected.has_value());

    const AutoReferenceDecision decision = selectBestReferenceFrameAutoSmart(candidates, getFilter);
    ASSERT_TRUE(decision.best.has_value());
    EXPECT_TRUE(decision.best.value() == expected.value());
    EXPECT_EQ(decision.modeUsed, AutoReferenceMode::AbsoluteAll);
}

TEST(AutoReferenceSelection, SmartUsesFilterNormalizedWhenMultipleFilters)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    std::unordered_map<FrameId, QString> filterById;

    // L filter: low absolute FWHM with modest spread.
    const std::vector<double> fwhmOffsets = {-0.1, 0.0, 0.1, 0.0, 0.1};
    const std::vector<double> lSnr = {9.0, 9.5, 10.0, 10.5, 11.0};
    for (int i = 0; i < static_cast<int>(lSnr.size()); ++i)
    {
        const FrameId id(QStringLiteral("L_%1").arg(i));
        candidates.emplace_back(id, makeMetrics(2.0 + fwhmOffsets[static_cast<size_t>(i)], lSnr[i], 200));
        filterById.emplace(id, QStringLiteral("L"));
    }

    // Ha filter: larger absolute FWHM, but best frame is strong vs Ha baseline.
    const std::vector<double> haSnr = {4.8, 5.0, 5.1, 5.2, 6.5};
    for (int i = 0; i < static_cast<int>(haSnr.size()); ++i)
    {
        const FrameId id(QStringLiteral("Ha_%1").arg(i));
        candidates.emplace_back(id, makeMetrics(6.0 + fwhmOffsets[static_cast<size_t>(i)], haSnr[i], 200));
        filterById.emplace(id, QStringLiteral("Ha"));
    }

    const auto getFilter = [&](const FrameId& id) -> QString {
        const auto it = filterById.find(id);
        return (it != filterById.end()) ? it->second : QStringLiteral("(unknown)");
    };

    const AutoReferenceDecision decision = selectBestReferenceFrameAutoSmart(candidates, getFilter);
    EXPECT_EQ(decision.modeUsed, AutoReferenceMode::FilterNormalizedAcrossFilters);
    EXPECT_EQ(decision.namedFilterCount, 2);
    ASSERT_TRUE(decision.best.has_value());

    AutoReferenceSelection normalized_sel;
    normalized_sel.mode = AutoReferenceMode::FilterNormalizedAcrossFilters;
    const auto normalized_best = selectBestReferenceFrameAuto(candidates, getFilter, normalized_sel);
    ASSERT_TRUE(normalized_best.has_value());
    EXPECT_TRUE(*decision.best == *normalized_best);
}

TEST(AutoReferenceSelection, SmartFallsBackToAbsoluteForSingleFilter)
{
    const FrameId a(QStringLiteral("L_01"));
    const FrameId b(QStringLiteral("L_02"));

    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    candidates.emplace_back(a, makeMetrics(2.2, 10.0, 200));
    candidates.emplace_back(b, makeMetrics(2.0, 9.0, 200));

    const auto getFilter = [&](const FrameId&) -> QString { return QStringLiteral("L"); };

    const AutoReferenceDecision decision = selectBestReferenceFrameAutoSmart(candidates, getFilter);
    EXPECT_EQ(decision.modeUsed, AutoReferenceMode::AbsoluteAll);
    EXPECT_EQ(decision.namedFilterCount, 1);
    ASSERT_TRUE(decision.best.has_value());

    const auto absBest = selectBestReferenceFrame(candidates);
    ASSERT_TRUE(absBest.has_value());
    EXPECT_TRUE(*decision.best == *absBest);
}

TEST(AutoReferenceSelection, SmartExcludesUnknownWhenMixedFilters)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    std::unordered_map<FrameId, QString> filterById;

    const FrameId l1(QStringLiteral("L_01"));
    const FrameId ha1(QStringLiteral("Ha_01"));
    const FrameId unknown(QStringLiteral("X_01"));

    // Unknown looks best in absolute terms, but should be excluded from the normalized pool.
    candidates.emplace_back(l1, makeMetrics(2.0, 10.0, 200));
    candidates.emplace_back(ha1, makeMetrics(6.0, 6.5, 200));
    candidates.emplace_back(unknown, makeMetrics(1.0, 100.0, 500));

    filterById.emplace(l1, QStringLiteral("L"));
    filterById.emplace(ha1, QStringLiteral("Ha"));
    // unknown intentionally missing

    const auto getFilter = [&](const FrameId& id) -> QString {
        const auto it = filterById.find(id);
        return (it != filterById.end()) ? it->second : QStringLiteral("(unknown)");
    };

    const AutoReferenceDecision decision = selectBestReferenceFrameAutoSmart(candidates, getFilter);
    EXPECT_EQ(decision.modeUsed, AutoReferenceMode::FilterNormalizedAcrossFilters);
    EXPECT_EQ(decision.namedFilterCount, 2);
    EXPECT_EQ(decision.excludedUnknownCount, 1);
    ASSERT_TRUE(decision.best.has_value());
    EXPECT_TRUE(*decision.best != unknown);
}
