// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "processes/core/StandaloneRegistrationUtils.h"

namespace
{
/// A light as the standalone registration lists it: its path and the filter its header names.
an::ui::StandaloneRegistrationFrame makeLightFrame(const QString& path, const QString& filter)
{
    return an::ui::StandaloneRegistrationFrame{path, filter};
}

an::ui::RegistrationMetricRecord makeMetric(const QString& path, const int stars, const double fwhm,
                                            const double eccentricity, const double snr, const bool succeeded = true)
{
    an::ui::RegistrationMetricRecord record;
    record.frameId = FrameId(path);
    record.metrics.stars = stars;
    record.metrics.fwhm = fwhm;
    record.metrics.eccentricity = eccentricity;
    record.metrics.snr = snr;
    record.metrics.succeeded = succeeded;
    return record;
}
} // namespace

TEST(StandaloneRegistrationUtils, AutoReferenceUsesRegistrationReferenceScorer)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString l1Path = QDir(dir.path()).filePath(QStringLiteral("L_01.fit"));
    const QString l2Path = QDir(dir.path()).filePath(QStringLiteral("L_02.fit"));
    const QString ha1Path = QDir(dir.path()).filePath(QStringLiteral("Ha_01.fit"));

    const auto l1 = makeLightFrame(l1Path, QStringLiteral("L"));
    const auto l2 = makeLightFrame(l2Path, QStringLiteral("L"));
    const auto ha1 = makeLightFrame(ha1Path, QStringLiteral("Ha"));
    const std::vector<an::ui::StandaloneRegistrationFrame> images{l1, l2, ha1};

    std::unordered_map<QString, an::ui::RegistrationMetricRecord> metricsByFrameId;
    metricsByFrameId.emplace(FrameId(l1Path).canonicalPath(), makeMetric(l1Path, 220, 2.0, 0.40, 9.0));
    metricsByFrameId.emplace(FrameId(l2Path).canonicalPath(), makeMetric(l2Path, 260, 2.1, 0.42, 10.0));
    metricsByFrameId.emplace(FrameId(ha1Path).canonicalPath(), makeMetric(ha1Path, 240, 6.0, 0.40, 6.5));

    std::vector<RegistrationMetricsCandidate> candidates;
    candidates.reserve(metricsByFrameId.size());
    for (const auto& [_, metric] : metricsByFrameId)
    {
        candidates.push_back(RegistrationMetricsCandidate{metric.frameId, metric.metrics});
    }
    const auto getFilter = [&](const FrameId& id) {
        for (const auto& image : images)
        {
            if (FrameId(image.path).canonicalPath() == id.canonicalPath())
            {
                return image.filter.trimmed();
            }
        }
        return QString();
    };

    QString scorerError;
    const auto expected =
        RegistrationReferenceScorer::pickBest(QStringLiteral("standalone"), candidates, getFilter, scorerError);
    ASSERT_TRUE(expected.has_value()) << scorerError.toStdString();

    QString error;
    const auto pick = an::ui::pickStandaloneAutoReference(images, metricsByFrameId, error);
    ASSERT_TRUE(pick.has_value()) << error.toStdString();
    EXPECT_EQ(pick->id.canonicalPath(), expected->id.canonicalPath());
    EXPECT_DOUBLE_EQ(pick->score, expected->score);
}

TEST(StandaloneRegistrationUtils, ThresholdsExcludeOnlyFramesOutsideConfiguredBounds)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString goodPath = QDir(dir.path()).filePath(QStringLiteral("good.fit"));
    const QString softPath = QDir(dir.path()).filePath(QStringLiteral("soft.fit"));
    const QString elongatedPath = QDir(dir.path()).filePath(QStringLiteral("elongated.fit"));

    const std::vector<an::ui::StandaloneRegistrationFrame> images{makeLightFrame(goodPath, QStringLiteral("L")),
                                                                  makeLightFrame(softPath, QStringLiteral("L")),
                                                                  makeLightFrame(elongatedPath, QStringLiteral("L"))};

    std::unordered_map<QString, an::ui::RegistrationMetricRecord> metricsByFrameId;
    metricsByFrameId.emplace(FrameId(goodPath).canonicalPath(), makeMetric(goodPath, 250, 2.0, 0.40, 10.0));
    metricsByFrameId.emplace(FrameId(softPath).canonicalPath(), makeMetric(softPath, 120, 4.5, 0.35, 8.0));
    metricsByFrameId.emplace(FrameId(elongatedPath).canonicalPath(), makeMetric(elongatedPath, 260, 2.2, 0.72, 9.0));

    an::ui::StandaloneRegistrationQualityThresholds thresholds;
    thresholds.minStars = 150;
    thresholds.maxFwhm = 3.0;
    thresholds.maxEccentricity = 0.6;

    const auto excluded = an::ui::computeStandaloneThresholdExcludedFrameIds(metricsByFrameId, images, thresholds);
    EXPECT_FALSE(excluded.contains(FrameId(goodPath).canonicalPath()));
    EXPECT_TRUE(excluded.contains(FrameId(softPath).canonicalPath()));
    EXPECT_TRUE(excluded.contains(FrameId(elongatedPath).canonicalPath()));
}

TEST(StandaloneRegistrationUtils, ManualExclusionTakesPriorityOverThresholdExclusion)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString path = QDir(dir.path()).filePath(QStringLiteral("light.fit"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("frame");
    file.close();

    const QString canonical = FrameId(path).canonicalPath();

    std::unordered_set<QString> manualExcluded{canonical};
    std::unordered_set<QString> thresholdExcluded{canonical};

    const auto decision = an::ui::evaluateStandaloneFrameExclusion(path, manualExcluded, thresholdExcluded);
    EXPECT_TRUE(decision.excluded);
    EXPECT_EQ(decision.reason, an::ui::StandaloneRegistrationExclusionReason::Manual);
    EXPECT_EQ(decision.detail, QStringLiteral("Excluded manually"));
}

TEST(StandaloneRegistrationUtils, FailedMetricDoesNotPassThresholds)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString path = QDir(dir.path()).filePath(QStringLiteral("failed.fit"));

    // A metric where succeeded=false should always fail regardless of numeric values
    const auto failedMetric = makeMetric(path, 300, 1.8, 0.30, 12.0, /*succeeded=*/false);

    an::ui::StandaloneRegistrationQualityThresholds thresholds;
    thresholds.minStars = 100;
    thresholds.maxFwhm = 5.0;
    thresholds.maxEccentricity = 0.8;

    EXPECT_FALSE(an::ui::standaloneRegistrationMetricPassesThresholds(failedMetric, thresholds));
}

TEST(StandaloneRegistrationUtils, AutoReferencePicksCorrectFilterGroupWinner)
{
    // Verify that pickStandaloneAutoReference picks the frame with the best absolute
    // quality when all frames share the same filter (absolute scoring mode). One frame
    // is dramatically better than the others.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString bestPath = QDir(dir.path()).filePath(QStringLiteral("L_best.fit"));
    const QString poorPath = QDir(dir.path()).filePath(QStringLiteral("L_poor.fit"));
    const QString badPath = QDir(dir.path()).filePath(QStringLiteral("L_bad.fit"));

    const std::vector<an::ui::StandaloneRegistrationFrame> images{makeLightFrame(bestPath, QStringLiteral("L")),
                                                                  makeLightFrame(poorPath, QStringLiteral("L")),
                                                                  makeLightFrame(badPath, QStringLiteral("L"))};

    std::unordered_map<QString, an::ui::RegistrationMetricRecord> metricsByFrameId;
    // best: many stars, tight PSF, low eccentricity, high SNR
    metricsByFrameId.emplace(FrameId(bestPath).canonicalPath(), makeMetric(bestPath, 500, 1.6, 0.28, 14.0));
    // poor: few stars, soft PSF
    metricsByFrameId.emplace(FrameId(poorPath).canonicalPath(), makeMetric(poorPath, 80, 4.5, 0.45, 6.0));
    // bad: even fewer stars, very soft PSF
    metricsByFrameId.emplace(FrameId(badPath).canonicalPath(), makeMetric(badPath, 40, 6.0, 0.55, 4.0));

    QString error;
    const auto pick = an::ui::pickStandaloneAutoReference(images, metricsByFrameId, error);
    ASSERT_TRUE(pick.has_value()) << error.toStdString();
    EXPECT_EQ(pick->id.canonicalPath(), FrameId(bestPath).canonicalPath());
}

TEST(StandaloneRegistrationUtils, AutoReferenceSkipsIgnoredFrames)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString bestPath = QDir(dir.path()).filePath(QStringLiteral("L_best.fit"));
    const QString nextBestPath = QDir(dir.path()).filePath(QStringLiteral("L_next.fit"));

    const std::vector<an::ui::StandaloneRegistrationFrame> images{makeLightFrame(bestPath, QStringLiteral("L")),
                                                                  makeLightFrame(nextBestPath, QStringLiteral("L"))};

    std::unordered_map<QString, an::ui::RegistrationMetricRecord> metricsByFrameId;
    metricsByFrameId.emplace(FrameId(bestPath).canonicalPath(), makeMetric(bestPath, 450, 1.7, 0.25, 14.0));
    metricsByFrameId.emplace(FrameId(nextBestPath).canonicalPath(), makeMetric(nextBestPath, 320, 2.0, 0.28, 11.0));

    std::unordered_set<QString> ignoredFrameIds{FrameId(bestPath).canonicalPath()};

    QString error;
    const auto pick = an::ui::pickStandaloneAutoReference(images, metricsByFrameId, ignoredFrameIds, error);
    ASSERT_TRUE(pick.has_value()) << error.toStdString();
    EXPECT_EQ(pick->id.canonicalPath(), FrameId(nextBestPath).canonicalPath());
}
