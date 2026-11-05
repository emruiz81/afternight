// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The records the workflow tools share with the integration: a standalone registration recorded in the registration
/// database under the integration's group key and read back, reused only while its frames, their files and its settings
/// are as they were; the quality thresholds and the automatic reference over the recorded figures; the stacking groups
/// and their output names; the stacking weights read from the recorded figures or the exposures; the comet roles a
/// stack may read; and the local normalization settings the stacking window keeps.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/persistence/RegistrationMetricsRepository.h"
#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/core/StandaloneRegistrationPersistence.h"
#include "processes/core/StandaloneRegistrationUtils.h"
#include "processes/core/StandaloneStackingSettingsPersistence.h"
#include "processes/core/StandaloneStackingUtils.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <QDir>
#include <QFile>
#include <QSize>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

namespace an::tests::processes::workflow
{
namespace
{

using an::ui::RegisteredFrame;
using an::ui::RegisteredFrameSet;
using an::ui::RegisteredStar;
using an::ui::RegistrationMetricRecord;
using an::ui::StandaloneStackingFrame;
using an::ui::StandaloneStackingGroup;

/// A light frame as the integration's import describes it.
[[nodiscard]] FrameInfo lightFrame(const QString& path, const QString& objectName,
                                   const QString& filter = QStringLiteral("L"))
{
    return FrameInfo(FrameId(path), path,
                     FrameMetadata(::FrameType::Light, QSize(3000, 2000), 60.0, std::nullopt, 100, 10, filter,
                                   objectName, QDateTime(QDate(2025, 1, 1), QTime(22, 0), QTimeZone::UTC), QString()));
}

/// A frame registered with stars at the positions and a translation onto the reference.
[[nodiscard]] RegisteredFrame registered(const QString& path, std::initializer_list<std::pair<double, double>> stars,
                                         double dx, double dy, bool failed = false)
{
    RegisteredFrame frame;
    frame.sourcePath = path;
    for (const auto& [x, y] : stars)
    {
        frame.stars.push_back(RegisteredStar{.x = x, .y = y, .fwhm = 2.0, .roundness = 0.9});
    }
    frame.metrics.stars = static_cast<int>(frame.stars.size());
    frame.failed = failed;
    if (!failed)
    {
        frame.transform = *an::RegistrationTransform::create(
            an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Projective,
                                             .sourceToReference = {1.0, 0.0, dx, 0.0, 1.0, dy, 0.0, 0.0, 1.0}});
    }
    return frame;
}

[[nodiscard]] RegistrationMetricRecord metricOf(const QString& path, int stars, double fwhm, double eccentricity,
                                                double snr, bool succeeded = true)
{
    RegistrationMetricRecord record;
    record.frameId = FrameId(path);
    record.metrics.stars = stars;
    record.metrics.fwhm = fwhm;
    record.metrics.eccentricity = eccentricity;
    record.metrics.snr = snr;
    record.metrics.succeeded = succeeded;
    return record;
}

class WorkflowRecordsTest : public WorkflowCase
{
protected:
    /// An empty file, which the records stat but never read.
    [[nodiscard]] QString touched(const QString& name) const
    {
        const QString path = temporaryPath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        return path;
    }

    /// Settings registering to a reference with a homography.
    [[nodiscard]] static RegistrationSettings settingsFor(const QString& reference)
    {
        RegistrationSettings settings;
        settings.referenceImage = FrameId(reference).canonicalPath();
        settings.transformation = QStringLiteral("Homography");
        return settings;
    }

    [[nodiscard]] QString root() const
    {
        return temporaryPath(QStringLiteral("records"));
    }

    /// A stacking frame of a filter and an exposure, taken at a time.
    [[nodiscard]] StandaloneStackingFrame stackingFrame(const QString& path, const QString& filter, double exposure,
                                                        const QDateTime& captured = {}) const
    {
        an::Result<an::consumer::SessionHold> hold = session().hold();
        EXPECT_TRUE(hold.hasValue());
        std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
            hold->engine(), an::SampleType::UInt16, 3000u, 2000u, an::tests::viewer::Channels::Grey);
        EXPECT_TRUE(descriptor.has_value());
        return StandaloneStackingFrame{
            .entry = ImageListProcessHelpers::ImageListEntry{.path = path, .descriptor = *descriptor, .metadata = {}},
            .filter = filter,
            .capturedAtUtc = captured.isValid() ? captured : QDateTime(QDate(2025, 1, 1), QTime(22, 0), QTimeZone::UTC),
            .exposureSeconds = exposure};
    }

    void SetUp() override
    {
        WorkflowCase::SetUp();
        ASSERT_TRUE(QDir().mkpath(root()));
    }
};

TEST_F(WorkflowRecordsTest, ARegistrationIsRecordedUnderTheIntegrationsGroupKeyAndReadBack)
{
    const QString a = touched(QStringLiteral("light_a.fit"));
    const QString b = touched(QStringLiteral("light_b.fit"));
    const std::vector<FrameInfo> frames{lightFrame(a, QStringLiteral("M42")), lightFrame(b, QStringLiteral("M42"))};
    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, settingsFor(a), &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();
    EXPECT_TRUE(context->interoperableGroup);
    EXPECT_EQ(context->groupKey, QStringLiteral("reg|3000x2000|M42"));
    EXPECT_EQ(context->databasePath, QDir(root()).filePath(QStringLiteral("registration_metrics.db")));

    RegisteredFrameSet set;
    set.referencePath = a;
    set.frames.emplace(a, registered(a, {{10.0, 10.0}, {20.0, 30.0}}, 0.0, 0.0));
    RegisteredFrame second = registered(b, {{15.0, 16.0}, {22.0, 11.0}, {18.0, 40.0}}, 4.0, -3.5);
    second.metrics.fwhm = 2.9;
    second.metrics.eccentricity = 0.12;
    second.metrics.snr = 18.5;
    second.noise.background_noise = 0.0021;
    second.noise.global_mad = 0.0031;
    second.noise.global_scale = 0.0041;
    set.frames.emplace(b, second);
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error)) << error.toStdString();

    const auto loaded = an::ui::loadStandaloneRegistrationState(*context, frames, &error);
    ASSERT_TRUE(loaded.has_value()) << error.toStdString();
    ASSERT_TRUE(loaded->groupState.has_value());
    EXPECT_EQ(loaded->groupState->referenceFrameId, context->referenceFrameId);
    ASSERT_EQ(loaded->metricsByFrameId.size(), 2u);
    const QString key = FrameId(b).canonicalPath();
    const RegistrationMetricRecord& metric = loaded->metricsByFrameId.at(key);
    EXPECT_EQ(metric.metrics.stars, 3);
    EXPECT_DOUBLE_EQ(metric.metrics.fwhm, 2.9);
    EXPECT_NEAR(metric.metrics.eccentricity, 0.12, 1e-9);
    EXPECT_TRUE(metric.metrics.succeeded);
    // The noise figures are the only registration figures the noise weighting reads.
    EXPECT_DOUBLE_EQ(metric.noise.background_noise, 0.0021);
    EXPECT_DOUBLE_EQ(metric.noise.global_mad, 0.0031);
    EXPECT_DOUBLE_EQ(metric.noise.global_scale, 0.0041);

    const RegisteredFrame& back = loaded->registeredImagesByFrameId.at(key);
    EXPECT_FALSE(back.failed);
    ASSERT_EQ(back.stars.size(), 3u);
    // A star comes back at the engine pixel coordinates it was recorded at.
    for (const auto& [x, y] : {std::pair{15.0, 16.0}, std::pair{22.0, 11.0}, std::pair{18.0, 40.0}})
    {
        EXPECT_TRUE(std::any_of(back.stars.begin(), back.stars.end(),
                                [x = x, y = y](const RegisteredStar& star) { return star.x == x && star.y == y; }))
            << x << "," << y;
    }
    ASSERT_TRUE(back.transform.has_value());
    EXPECT_NEAR(back.transform->translation().x, 4.0, 1e-9);
    EXPECT_NEAR(back.transform->translation().y, -3.5, 1e-9);

    const auto reuse = an::ui::checkReusableStandaloneRegistrationState(*context, frames, {}, &error);
    EXPECT_TRUE(reuse.reusable) << reuse.reason.toStdString() << " / " << error.toStdString();
}

TEST_F(WorkflowRecordsTest, ARegistrationTheDatabaseRefusesPartwayLeavesNothingThatReadsAsCurrent)
{
    const QString a = touched(QStringLiteral("light_a.fit"));
    const QString b = touched(QStringLiteral("light_b.fit"));
    const std::vector<FrameInfo> frames{lightFrame(a, QStringLiteral("M33")), lightFrame(b, QStringLiteral("M33"))};
    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, settingsFor(a), &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();
    RegisteredFrameSet set;
    set.referencePath = a;
    set.frames.emplace(a, registered(a, {{6.0, 6.0}}, 0.0, 0.0));
    set.frames.emplace(b, registered(b, {{6.0, 6.0}}, 2.0, 1.0));
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error)) << error.toStdString();
    ASSERT_TRUE(an::ui::checkReusableStandaloneRegistrationState(*context, frames, {}, &error).reusable);

    // The database refuses the second frame's figures, after the first frame's are written.
    {
        core::SqliteConnection connection(context->databasePath, QStringLiteral("WorkflowRecordsRefusal"));
        ASSERT_TRUE(connection.open(&error)) << error.toStdString();
        QSqlQuery query(connection.db());
        ASSERT_TRUE(query.exec(QStringLiteral("CREATE TRIGGER refuse_second BEFORE INSERT ON registration_metrics "
                                              "WHEN NEW.frame_id LIKE '%light_b%' "
                                              "BEGIN SELECT RAISE(ABORT, 'refused'); END")))
            << query.lastError().text().toStdString();
    }
    EXPECT_FALSE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error));
    EXPECT_FALSE(error.isEmpty());

    // What the refused write left is not a registration anything restores or reuses.
    const auto loaded = an::ui::loadStandaloneRegistrationState(*context, frames, &error);
    EXPECT_FALSE(loaded.has_value() && loaded->groupState.has_value());
    EXPECT_FALSE(an::ui::checkReusableStandaloneRegistrationState(*context, frames, {}, &error).reusable);
}

TEST_F(WorkflowRecordsTest, AFileChangedSinceItsRegistrationIsNotReused)
{
    const QString a = touched(QStringLiteral("light_a.fit"));
    const QString b = touched(QStringLiteral("light_b.fit"));
    const std::vector<FrameInfo> frames{lightFrame(a, QStringLiteral("M31")), lightFrame(b, QStringLiteral("M31"))};
    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, settingsFor(a), &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();
    RegisteredFrameSet set;
    set.referencePath = a;
    set.frames.emplace(a, registered(a, {{1.0, 1.0}}, 0.0, 0.0));
    set.frames.emplace(b, registered(b, {{1.0, 1.0}}, 1.0, 0.0));
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error)) << error.toStdString();

    // The records compare modification times to the second.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    QFile file(b);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Append));
    file.write(" ");
    file.close();
    const auto reuse = an::ui::checkReusableStandaloneRegistrationState(*context, frames, {}, &error);
    EXPECT_FALSE(reuse.reusable);
    EXPECT_FALSE(reuse.reason.isEmpty());
}

TEST_F(WorkflowRecordsTest, ARegistrationUnderOtherSettingsDoesNotMatch)
{
    const QString a = touched(QStringLiteral("light_a.fit"));
    const QString b = touched(QStringLiteral("light_b.fit"));
    const std::vector<FrameInfo> frames{lightFrame(a, QStringLiteral("NGC1023")),
                                        lightFrame(b, QStringLiteral("NGC1023"))};
    QString error;
    const RegistrationSettings first = settingsFor(a);
    const auto recorded = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, first, &error);
    ASSERT_TRUE(recorded.has_value()) << error.toStdString();
    RegisteredFrameSet set;
    set.referencePath = a;
    set.frames.emplace(a, registered(a, {{5.0, 5.0}}, 0.0, 0.0));
    set.frames.emplace(b, registered(b, {{5.0, 5.0}}, 2.0, 0.0));
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*recorded, frames, set, &error)) << error.toStdString();

    RegistrationSettings second = first;
    second.transformation = QStringLiteral("Affine");
    const auto asked = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, second, &error);
    ASSERT_TRUE(asked.has_value()) << error.toStdString();
    an::ui::RegistrationMetricsStateLoader loader(recorded->databasePath, QStringLiteral("WorkflowRecordsSettings"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    const auto state = loader.loadGroupState(recorded->groupKey, recorded->settingsVersion, &error);
    ASSERT_TRUE(state.has_value()) << error.toStdString();
    EXPECT_TRUE(an::ui::standaloneRegistrationStateMatchesContext(*recorded, *state));
    EXPECT_FALSE(an::ui::standaloneRegistrationStateMatchesContext(*asked, *state));
}

TEST_F(WorkflowRecordsTest, AFrameAddedSinceTheRegistrationMakesItStale)
{
    const QString a = touched(QStringLiteral("light_a.fit"));
    const QString b = touched(QStringLiteral("light_b.fit"));
    const QString c = touched(QStringLiteral("light_c.fit"));
    const std::vector<FrameInfo> frames{lightFrame(a, QStringLiteral("M45")), lightFrame(b, QStringLiteral("M45"))};
    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, settingsFor(a), &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();
    RegisteredFrameSet set;
    set.referencePath = a;
    set.frames.emplace(a, registered(a, {{8.0, 8.0}}, 0.0, 0.0));
    set.frames.emplace(b, registered(b, {{8.0, 8.0}}, 3.0, 1.0));
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error)) << error.toStdString();

    std::vector<FrameInfo> more = frames;
    more.push_back(lightFrame(c, QStringLiteral("M45")));
    const auto reuse = an::ui::checkReusableStandaloneRegistrationState(*context, more, {}, &error);
    EXPECT_FALSE(reuse.reusable);
    EXPECT_FALSE(reuse.reason.isEmpty());
}

TEST_F(WorkflowRecordsTest, AFailedFrameIsRegisteredAgainUnlessItIsLeftOut)
{
    const QString a = touched(QStringLiteral("light_a.fit"));
    const QString b = touched(QStringLiteral("light_b.fit"));
    const std::vector<FrameInfo> frames{lightFrame(a, QStringLiteral("M57")), lightFrame(b, QStringLiteral("M57"))};
    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, settingsFor(a), &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();
    RegisteredFrameSet set;
    set.referencePath = a;
    set.frames.emplace(a, registered(a, {{10.0, 10.0}}, 0.0, 0.0));
    set.frames.emplace(b, registered(b, {}, 0.0, 0.0, true));
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error)) << error.toStdString();

    const auto again = an::ui::checkReusableStandaloneRegistrationState(*context, frames, {}, &error);
    EXPECT_FALSE(again.reusable);
    EXPECT_FALSE(again.reason.isEmpty());

    const auto leftOut =
        an::ui::checkReusableStandaloneRegistrationState(*context, frames, {FrameId(b).canonicalPath()}, &error);
    EXPECT_TRUE(leftOut.reusable) << leftOut.reason.toStdString();
}

TEST_F(WorkflowRecordsTest, ClearingARegistrationLeavesNothingToReuse)
{
    const QString a = touched(QStringLiteral("light_a.fit"));
    const QString b = touched(QStringLiteral("light_b.fit"));
    const std::vector<FrameInfo> frames{lightFrame(a, QStringLiteral("M1")), lightFrame(b, QStringLiteral("M1"))};
    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, settingsFor(a), &error);
    ASSERT_TRUE(context.has_value());
    RegisteredFrameSet set;
    set.referencePath = a;
    set.frames.emplace(a, registered(a, {{10.0, 10.0}}, 0.0, 0.0));
    set.frames.emplace(b, registered(b, {{10.0, 10.0}}, 1.0, 1.0));
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error));
    ASSERT_TRUE(an::ui::clearStandaloneRegistrationState(*context, &error)) << error.toStdString();
    EXPECT_FALSE(an::ui::checkReusableStandaloneRegistrationState(*context, frames, {}, &error).reusable);
}

TEST(WorkflowRegistrationUtilsTest, TheAutomaticReferenceIsTheIntegrationScorersPick)
{
    const QString l1 = QStringLiteral("/frames/L_01.fit");
    const QString l2 = QStringLiteral("/frames/L_02.fit");
    const QString ha = QStringLiteral("/frames/Ha_01.fit");
    const std::vector<an::ui::StandaloneRegistrationFrame> frames{{.path = l1, .filter = QStringLiteral("L")},
                                                                  {.path = l2, .filter = QStringLiteral("L")},
                                                                  {.path = ha, .filter = QStringLiteral("Ha")}};
    std::unordered_map<QString, RegistrationMetricRecord> metrics;
    metrics.emplace(FrameId(l1).canonicalPath(), metricOf(l1, 220, 2.0, 0.40, 9.0));
    metrics.emplace(FrameId(l2).canonicalPath(), metricOf(l2, 260, 2.1, 0.42, 10.0));
    metrics.emplace(FrameId(ha).canonicalPath(), metricOf(ha, 240, 6.0, 0.40, 6.5));

    std::vector<RegistrationMetricsCandidate> candidates;
    for (const auto& [key, metric] : metrics)
    {
        candidates.push_back(RegistrationMetricsCandidate{metric.frameId, metric.metrics});
    }
    const auto filterOf = [&frames](const FrameId& id) {
        for (const auto& frame : frames)
        {
            if (FrameId(frame.path).canonicalPath() == id.canonicalPath())
            {
                return frame.filter;
            }
        }
        return QString();
    };
    QString error;
    const auto expected =
        RegistrationReferenceScorer::pickBest(QStringLiteral("standalone"), candidates, filterOf, error);
    ASSERT_TRUE(expected.has_value()) << error.toStdString();
    const auto pick = an::ui::pickStandaloneAutoReference(frames, metrics, error);
    ASSERT_TRUE(pick.has_value()) << error.toStdString();
    EXPECT_EQ(pick->id.canonicalPath(), expected->id.canonicalPath());
    EXPECT_DOUBLE_EQ(pick->score, expected->score);
}

TEST(WorkflowRegistrationUtilsTest, TheAutomaticReferenceIsTheBestFrameLeftIn)
{
    const QString best = QStringLiteral("/frames/L_best.fit");
    const QString next = QStringLiteral("/frames/L_next.fit");
    const QString poor = QStringLiteral("/frames/L_poor.fit");
    const std::vector<an::ui::StandaloneRegistrationFrame> frames{{.path = best, .filter = QStringLiteral("L")},
                                                                  {.path = next, .filter = QStringLiteral("L")},
                                                                  {.path = poor, .filter = QStringLiteral("L")}};
    std::unordered_map<QString, RegistrationMetricRecord> metrics;
    metrics.emplace(FrameId(best).canonicalPath(), metricOf(best, 500, 1.6, 0.28, 14.0));
    metrics.emplace(FrameId(next).canonicalPath(), metricOf(next, 320, 2.0, 0.28, 11.0));
    metrics.emplace(FrameId(poor).canonicalPath(), metricOf(poor, 40, 6.0, 0.55, 4.0));
    QString error;
    auto pick = an::ui::pickStandaloneAutoReference(frames, metrics, error);
    ASSERT_TRUE(pick.has_value()) << error.toStdString();
    EXPECT_EQ(pick->id.canonicalPath(), FrameId(best).canonicalPath());
    pick = an::ui::pickStandaloneAutoReference(frames, metrics, {FrameId(best).canonicalPath()}, error);
    ASSERT_TRUE(pick.has_value()) << error.toStdString();
    EXPECT_EQ(pick->id.canonicalPath(), FrameId(next).canonicalPath());
}

TEST(WorkflowRegistrationUtilsTest, AThresholdExcludesOnlyTheFramesOutsideItsBounds)
{
    const QString good = QStringLiteral("/frames/good.fit");
    const QString soft = QStringLiteral("/frames/soft.fit");
    const QString elongated = QStringLiteral("/frames/elongated.fit");
    const std::vector<an::ui::StandaloneRegistrationFrame> frames{{.path = good, .filter = QStringLiteral("L")},
                                                                  {.path = soft, .filter = QStringLiteral("L")},
                                                                  {.path = elongated, .filter = QStringLiteral("L")}};
    std::unordered_map<QString, RegistrationMetricRecord> metrics;
    metrics.emplace(FrameId(good).canonicalPath(), metricOf(good, 250, 2.0, 0.40, 10.0));
    metrics.emplace(FrameId(soft).canonicalPath(), metricOf(soft, 120, 4.5, 0.35, 8.0));
    metrics.emplace(FrameId(elongated).canonicalPath(), metricOf(elongated, 260, 2.2, 0.72, 9.0));
    const an::ui::StandaloneRegistrationQualityThresholds thresholds{
        .minStars = 150, .maxFwhm = 3.0, .maxEccentricity = 0.6};
    const auto excluded = an::ui::computeStandaloneThresholdExcludedFrameIds(metrics, frames, thresholds);
    EXPECT_FALSE(excluded.contains(FrameId(good).canonicalPath()));
    EXPECT_TRUE(excluded.contains(FrameId(soft).canonicalPath()));
    EXPECT_TRUE(excluded.contains(FrameId(elongated).canonicalPath()));

    EXPECT_FALSE(
        an::ui::standaloneRegistrationMetricPassesThresholds(metricOf(good, 300, 1.8, 0.3, 12.0, false), thresholds));

    const QString key = FrameId(good).canonicalPath();
    const auto decision = an::ui::evaluateStandaloneFrameExclusion(good, {key}, {key});
    EXPECT_TRUE(decision.excluded);
    EXPECT_EQ(decision.reason, an::ui::StandaloneRegistrationExclusionReason::Manual);
    EXPECT_EQ(decision.detail, QStringLiteral("Excluded manually"));
}

TEST_F(WorkflowRecordsTest, FramesAreGroupedByFilterAndEachGroupNamesItsOutput)
{
    const std::vector<StandaloneStackingFrame> frames{
        stackingFrame(QStringLiteral("/f/L_001.fit"), QStringLiteral("L"), 300.0),
        stackingFrame(QStringLiteral("/f/L_002.fit"), QStringLiteral("L"), 300.0),
        stackingFrame(QStringLiteral("/f/Ha_001.fit"), QStringLiteral("Ha"), 300.0)};
    const auto groups = an::ui::buildStandaloneStackingGroups(frames, an::ui::StandaloneStackingGroupingOptions{},
                                                              QStringLiteral("stacked_$filter.fit"));
    ASSERT_EQ(groups.size(), 2u);
    const auto find = [&groups](const QString& filter) {
        return std::find_if(groups.begin(), groups.end(),
                            [&filter](const StandaloneStackingGroup& group) { return group.filter == filter; });
    };
    ASSERT_NE(find(QStringLiteral("L")), groups.end());
    EXPECT_EQ(find(QStringLiteral("L"))->frameCount, 2);
    EXPECT_EQ(find(QStringLiteral("L"))->outputFileName, QStringLiteral("stacked_L.fit"));
    EXPECT_EQ(find(QStringLiteral("Ha"))->outputFileName, QStringLiteral("stacked_Ha.fit"));
}

TEST_F(WorkflowRecordsTest, ExposuresWithinTheToleranceShareAGroup)
{
    const std::vector<StandaloneStackingFrame> frames{
        stackingFrame(QStringLiteral("/f/a.fit"), QStringLiteral("L"), 300.0),
        stackingFrame(QStringLiteral("/f/b.fit"), QStringLiteral("L"), 309.0),
        stackingFrame(QStringLiteral("/f/c.fit"), QStringLiteral("L"), 420.0)};
    const auto groups = an::ui::buildStandaloneStackingGroups(
        frames, {.byFilter = false, .byExposure = true, .exposureToleranceFraction = 0.05},
        QStringLiteral("stacked_$exposure.fit"));
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].frameCount, 2);
    EXPECT_EQ(groups[1].frameCount, 1);
    EXPECT_NE(groups[0].outputFileName, groups[1].outputFileName);
}

TEST_F(WorkflowRecordsTest, TwoGroupsOfOneNameAreToldApart)
{
    const std::vector<StandaloneStackingFrame> frames{
        stackingFrame(QStringLiteral("/f/2024-01-01/L_001.fit"), QStringLiteral("L"), 300.0,
                      QDateTime(QDate(2024, 1, 1), QTime(22, 0), QTimeZone::UTC)),
        stackingFrame(QStringLiteral("/f/2024-01-02/L_002.fit"), QStringLiteral("L"), 300.0,
                      QDateTime(QDate(2024, 1, 2), QTime(22, 0), QTimeZone::UTC))};
    const auto groups = an::ui::buildStandaloneStackingGroups(frames, {.byFilter = true, .bySession = true},
                                                              QStringLiteral("stacked_$filter.fit"));
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].outputFileName, QStringLiteral("stacked_L.fit"));
    EXPECT_TRUE(groups[1].outputFileName.startsWith(QStringLiteral("stacked_L_")));
    EXPECT_TRUE(groups[1].outputFileName.endsWith(QStringLiteral(".fit")));
}

TEST_F(WorkflowRecordsTest, NoGroupingMakesOneGroupOfEveryFrame)
{
    const std::vector<StandaloneStackingFrame> frames{
        stackingFrame(QStringLiteral("/f/L.fit"), QStringLiteral("L"), 300.0),
        stackingFrame(QStringLiteral("/f/Ha.fit"), QStringLiteral("Ha"), 600.0)};
    const auto groups =
        an::ui::buildStandaloneStackingGroups(frames, {.byFilter = false}, QStringLiteral("stacked.fit"));
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].frameCount, 2);
    EXPECT_EQ(groups[0].label, QStringLiteral("All Frames"));
    EXPECT_EQ(groups[0].outputFileName, QStringLiteral("stacked.fit"));
}

TEST(WorkflowStackingNamesTest, TheOutputNameIsTheTemplateWithAnExtensionTheToolWrites)
{
    StandaloneStackingGroup group;
    group.filter = QStringLiteral("Broadband");
    group.session = QStringLiteral("2024-01-01");
    group.representativeExposureSeconds = 300.0;
    group.label = QStringLiteral("Broadband");
    // A broadband filter names nothing, and an empty name component reads "group".
    EXPECT_EQ(an::ui::resolveStandaloneStackingOutputName(QStringLiteral("stacked_$filter.fit"), group),
              QStringLiteral("stacked_group.fit"));
    group.filter = QStringLiteral("L");
    EXPECT_EQ(an::ui::resolveStandaloneStackingOutputName(QStringLiteral("stacked_$filter"), group),
              QStringLiteral("stacked_L.fit"));
    group.filter = QStringLiteral("Red");
    EXPECT_EQ(an::ui::resolveStandaloneStackingOutputName(QStringLiteral("master_flat_$filter.fitsa"), group),
              QStringLiteral("master_flat_Red.fit"));
    EXPECT_EQ(an::ui::resolveStandaloneStackingOutputName(QStringLiteral("master_flat_$filter.raw"), group),
              QStringLiteral("master_flat_Red.fit"));
    EXPECT_EQ(an::ui::resolveStandaloneStackingOutputName(QStringLiteral("master_flat_$filter.xisf"), group),
              QStringLiteral("master_flat_Red.xisf"));
}

TEST(WorkflowStackingWeightsTest, OnlyTheMetricModesReadTheRegistrationsFigures)
{
    using Mode = an::ui::StandaloneStackingWeightingMode;
    EXPECT_FALSE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Equal));
    EXPECT_FALSE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Noise));
    EXPECT_FALSE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Exposure));
    EXPECT_TRUE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Fwhm));
    EXPECT_TRUE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Snr));
    EXPECT_TRUE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Stars));
}

TEST_F(WorkflowRecordsTest, AMetricWeightingWithNoDatabaseFallsBackToEqualWeights)
{
    const auto result =
        an::ui::planStandaloneStackingWeights(QDir(root()).filePath(QStringLiteral("registration_metrics.db")),
                                              {stackingFrame(QStringLiteral("/f/one.fit"), QStringLiteral("L"), 300.0),
                                               stackingFrame(QStringLiteral("/f/two.fit"), QStringLiteral("L"), 300.0)},
                                              an::ui::StandaloneStackingWeightingMode::Fwhm);
    EXPECT_TRUE(result.usedFallback);
    EXPECT_FALSE(result.warning.isEmpty());
    EXPECT_EQ(result.method, an::StackWeighting::Uniform);
    EXPECT_TRUE(result.measurements.empty());
}

TEST_F(WorkflowRecordsTest, AnExposureWeightingReadsEachFramesExposure)
{
    const QString database = QDir(root()).filePath(QStringLiteral("registration_metrics.db"));
    auto result =
        an::ui::planStandaloneStackingWeights(database,
                                              {stackingFrame(QStringLiteral("/f/a.fit"), QStringLiteral("L"), 0.0),
                                               stackingFrame(QStringLiteral("/f/b.fit"), QStringLiteral("L"), 0.0)},
                                              an::ui::StandaloneStackingWeightingMode::Exposure);
    EXPECT_TRUE(result.usedFallback);
    EXPECT_FALSE(result.warning.isEmpty());

    result = an::ui::planStandaloneStackingWeights(
        database,
        {stackingFrame(QStringLiteral("/f/short.fit"), QStringLiteral("L"), 100.0),
         stackingFrame(QStringLiteral("/f/long.fit"), QStringLiteral("L"), 300.0)},
        an::ui::StandaloneStackingWeightingMode::Exposure);
    EXPECT_FALSE(result.usedFallback);
    EXPECT_EQ(result.method, an::StackWeighting::Exposure);
    ASSERT_EQ(result.measurements.size(), 2u);
    EXPECT_EQ(result.measurements[0].exposureSeconds, 100.0);
    EXPECT_EQ(result.measurements[1].exposureSeconds, 300.0);
}

TEST_F(WorkflowRecordsTest, AFrameTheRegistrationDidNotMeasureIsWarnedOf)
{
    const QString measured = touched(QStringLiteral("registered.fit"));
    const QString missing = touched(QStringLiteral("missing.fit"));
    const std::vector<FrameInfo> frames{lightFrame(measured, QStringLiteral("M")),
                                        lightFrame(missing, QStringLiteral("M"))};
    QString error;
    const auto context =
        an::ui::buildStandaloneRegistrationPersistenceContext(root(), frames, settingsFor(measured), &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();
    RegisteredFrameSet set;
    set.referencePath = measured;
    RegisteredFrame frame = registered(measured, {{1.0, 1.0}}, 0.0, 0.0);
    frame.metrics.stars = 24;
    frame.metrics.fwhm = 2.4;
    frame.metrics.snr = 18.0;
    set.frames.emplace(measured, frame);
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, frames, set, &error)) << error.toStdString();

    const auto result = an::ui::planStandaloneStackingWeights(
        context->databasePath,
        {stackingFrame(measured, QStringLiteral("L"), 300.0), stackingFrame(missing, QStringLiteral("L"), 300.0)},
        an::ui::StandaloneStackingWeightingMode::Fwhm);
    EXPECT_FALSE(result.usedFallback);
    EXPECT_TRUE(result.partlyMeasured);
    EXPECT_FALSE(result.warning.isEmpty());
    ASSERT_EQ(result.measurements.size(), 2u);
    ASSERT_TRUE(result.measurements[0].fullWidth.has_value());
    EXPECT_DOUBLE_EQ(*result.measurements[0].fullWidth, 2.4);
    EXPECT_FALSE(result.measurements[1].fullWidth.has_value());
}

TEST_F(WorkflowRecordsTest, AMetricRecordedUnderAnotherSpellingOfTheFrameIsFound)
{
    const QString database = QDir(root()).filePath(QStringLiteral("registration_metrics.db"));
    const FrameId current(QStringLiteral("C:/P121/Standalone/Light.fit"));
    const FrameId otherSpelling(QStringLiteral("c:\\p121\\standalone\\light.fit"));
    an::ui::RegistrationMetricsWriter writer(database, QStringLiteral("WorkflowRecordsIdentity"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    RegistrationMetricRecord metric;
    metric.frameId = otherSpelling;
    metric.groupKey = QStringLiteral("reg|standalone-weight");
    metric.settingsVersion = 1;
    metric.metrics.succeeded = true;
    metric.metrics.fwhm = 2.0;
    metric.updatedAt = 200;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();

    const auto result = an::ui::planStandaloneStackingWeights(
        database, {stackingFrame(current.canonicalPath(), QStringLiteral("L"), 300.0)},
        an::ui::StandaloneStackingWeightingMode::Fwhm);
    EXPECT_FALSE(result.usedFallback) << result.warning.toStdString();
    ASSERT_EQ(result.measurements.size(), 1u);
    ASSERT_TRUE(result.measurements[0].fullWidth.has_value());
    EXPECT_DOUBLE_EQ(*result.measurements[0].fullWidth, 2.0);
}

TEST(WorkflowStackingRolesTest, AnOrdinaryStackNeedsNoRegistrationRun)
{
    an::ui::StandaloneStackingTransformRoleRequest request;
    request.stackingMode = QStringLiteral("Stars");
    const auto result = an::ui::validateStandaloneStackingTransformRoleSelection(request);
    EXPECT_TRUE(result.allowed) << result.error.toStdString();
    EXPECT_EQ(result.effectiveRole, QStringLiteral("ordinary"));
    EXPECT_TRUE(result.error.isEmpty());
}

TEST_F(WorkflowRecordsTest, ACometStackWithoutAMatchingCometRunIsRefused)
{
    an::ui::StandaloneStackingTransformRoleRequest request;
    request.registrationMetricsDbPath = QDir(root()).filePath(QStringLiteral("registration_metrics.db"));
    request.stackingMode = QStringLiteral("Comet");
    request.frameIds = {FrameId(touched(QStringLiteral("a.fit"))), FrameId(touched(QStringLiteral("b.fit")))};
    const auto result = an::ui::validateStandaloneStackingTransformRoleSelection(request);
    EXPECT_FALSE(result.allowed);
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(WorkflowLocalNormalizationTest, TheSharedSettingsReplaceAStaleCopy)
{
    LocalNormalizationSettings shared;
    shared.gridSize = 8;
    shared.gridInterpolation = QStringLiteral("Bilinear");
    shared.localScaleEnabled = true;
    shared.referenceStrategy = QStringLiteral("Single Best Frame");
    shared.backgroundEstimator = QStringLiteral("Percentile");
    shared.backgroundPercentile = 0.3;
    shared.maxIntegratedFrames = 12;

    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.gridSize = 2;
    settings.localNormalization.gridInterpolation = QStringLiteral("Bicubic");
    settings.localNormalization.maxIntegratedFrames = 3;
    an::ui::synchronizeStandaloneStackingLocalNormalization(settings, shared);
    EXPECT_TRUE(settings.localNormalization.enabled);
    EXPECT_EQ(settings.localNormalization.gridSize, 8);
    EXPECT_EQ(settings.localNormalization.gridInterpolation, QStringLiteral("Bilinear"));
    EXPECT_EQ(settings.localNormalization.referenceStrategy, QStringLiteral("Single Best Frame"));
    EXPECT_EQ(settings.localNormalization.maxIntegratedFrames, 12);

    settings.normalizationMode = NormalizationMode::None;
    an::ui::synchronizeStandaloneStackingLocalNormalization(settings, shared);
    EXPECT_FALSE(settings.localNormalization.enabled);

    LocalNormalizationSettings reset = shared;
    an::ui::resetStandaloneStackingLocalNormalizationSettings(reset);
    EXPECT_EQ(reset.gridSize, an::ui::standaloneStackingDefaultLocalNormalizationSettings().gridSize);
    EXPECT_EQ(reset.referenceStrategy, an::ui::standaloneStackingDefaultLocalNormalizationSettings().referenceStrategy);
}

// What the stack's local fit reads round-trips; what only the integration's analysis reads is not kept and reads back
// as the defaults.
TEST(WorkflowLocalNormalizationTest, TheSettingsRoundTripAndAMissingBlockReadsAsTheDefaults)
{
    LocalNormalizationSettings input;
    input.gridSize = 8;
    input.gridInterpolation = QStringLiteral("Bilinear");
    input.localScaleEnabled = true;
    input.referenceStrategy = QStringLiteral("Single Best Frame");
    input.maxIntegratedFrames = 32;
    input.evaluationCriteria = QStringLiteral("Median");
    input.backgroundEstimator = QStringLiteral("Percentile");
    input.backgroundPercentile = 0.3;
    input.localScaleSmoothingRadius = 4;
    nlohmann::json params;
    an::ui::serializeStandaloneStackingLocalNormalization(params, input);
    const LocalNormalizationSettings output = an::ui::deserializeStandaloneStackingLocalNormalization(params);
    EXPECT_EQ(output.gridSize, input.gridSize);
    EXPECT_EQ(output.gridInterpolation, input.gridInterpolation);
    EXPECT_EQ(output.localScaleEnabled, input.localScaleEnabled);
    EXPECT_EQ(output.backgroundEstimator, input.backgroundEstimator);
    EXPECT_DOUBLE_EQ(output.backgroundPercentile, input.backgroundPercentile);
    EXPECT_EQ(output.localScaleSmoothingRadius, input.localScaleSmoothingRadius);
    const LocalNormalizationSettings unkept = an::ui::standaloneStackingDefaultLocalNormalizationSettings();
    EXPECT_EQ(output.referenceStrategy, unkept.referenceStrategy);
    EXPECT_EQ(output.maxIntegratedFrames, unkept.maxIntegratedFrames);
    EXPECT_EQ(output.evaluationCriteria, unkept.evaluationCriteria);

    nlohmann::json partial;
    partial["normalizationMode"] = "Local";
    const LocalNormalizationSettings defaults = an::ui::standaloneStackingDefaultLocalNormalizationSettings();
    const LocalNormalizationSettings read = an::ui::deserializeStandaloneStackingLocalNormalization(partial);
    EXPECT_EQ(read.gridSize, defaults.gridSize);
    EXPECT_EQ(read.gridInterpolation, defaults.gridInterpolation);
    EXPECT_DOUBLE_EQ(read.backgroundPercentile, defaults.backgroundPercentile);
}

} // namespace
} // namespace an::tests::processes::workflow
