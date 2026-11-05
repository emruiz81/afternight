// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The workflow report: its sections, figures and previews, built from a run's manifest and result. Figures that read
/// an output image or a frame's header - an output's statistics, the equipment, the metadata completeness, the
/// photometric weights - are read in the engine session the builder is given, and without one they are not stated.

#define NOMINMAX
#undef min
#undef max

#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUuid>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Test_ImageIntegration/ImportSession.h"
#include "Test_ImageIntegration/workflow/StackWeightRules.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/IntegrationFrameFiles.h"

#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/reporting/MarkdownHtmlRenderer.h"
#include "processes/ImageIntegration/reporting/WorkflowReportBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Core/Version.h>

using namespace ImageIntegration::Reporting;

TEST(WorkflowReportBuilder, PrefersReportPreviewsOverThumbnailsWhenPresent)
{
    QTemporaryDir runDir;
    ASSERT_TRUE(runDir.isValid());

    // Thumbnail locations (used by UI/history) + report-grade locations.
    const QString thumbMaster = QStringLiteral("groups/GroupA/preview.png");
    const QString reportMaster = QStringLiteral("report_previews/GroupA/master.jpg");
    const QString thumbRejection = QStringLiteral("groups/GroupA/rejection_map_1.png");
    const QString reportRejection = QStringLiteral("report_previews/GroupA/rejection_map_1.jpg");

    ASSERT_TRUE(QDir(runDir.filePath("groups/GroupA")).mkpath("."));
    ASSERT_TRUE(QDir(runDir.filePath("report_previews/GroupA")).mkpath("."));

    {
        QImage thumb(8, 4, QImage::Format_RGB888);
        thumb.fill(Qt::black);
        ASSERT_TRUE(thumb.save(runDir.filePath(thumbMaster), "PNG"));
    }
    {
        QImage reportImg(32, 16, QImage::Format_RGB888);
        reportImg.fill(Qt::white);
        ASSERT_TRUE(reportImg.save(runDir.filePath(reportMaster), "JPG"));
    }

    {
        QImage thumb(10, 5, QImage::Format_Grayscale8);
        thumb.fill(0);
        thumb.setPixel(0, 0, 255);
        ASSERT_TRUE(thumb.save(runDir.filePath(thumbRejection), "PNG"));
    }
    {
        QImage reportImg(40, 20, QImage::Format_Grayscale8);
        reportImg.fill(0);
        reportImg.setPixel(0, 0, 255);
        ASSERT_TRUE(reportImg.save(runDir.filePath(reportRejection), "JPG"));
    }

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("GroupA");
    group.success = true;
    group.outputFilename = QStringLiteral("stacked_GroupA.fit");
    group.outputBytes = 2048;
    group.lightFrameCount = 1;
    group.durationMs = 1000;
    group.previewPath = thumbMaster;

    RejectionMapResult rejectionMap;
    rejectionMap.title = QStringLiteral("Rejection Map");
    rejectionMap.path = QStringLiteral("/tmp/rejection_map.fit");
    rejectionMap.previewPath = thumbRejection;
    group.rejectionMaps.push_back(rejectionMap);
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result, runDir.path());
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("src=\"report_previews/GroupA/master.jpg\"")));
    EXPECT_FALSE(report.contains(QStringLiteral("src=\"groups/GroupA/preview.png\"")));

    EXPECT_TRUE(report.contains(QStringLiteral("src=\"report_previews/GroupA/rejection_map_1.jpg\"")));
    EXPECT_FALSE(report.contains(QStringLiteral("src=\"groups/GroupA/rejection_map_1.png\"")));

    // Histogram is no longer rendered in reports.
    EXPECT_FALSE(report.contains(QStringLiteral("class=\"an-histogram\"")));
    EXPECT_FALSE(report.contains(QStringLiteral("<svg class=\"an-histogram\"")));
}

TEST(WorkflowReportBuilder, SuccessfulOutputNotRetainedIsReportedWithoutFailure)
{
    QTemporaryDir runDir;
    ASSERT_TRUE(runDir.isValid());

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("GroupA");
    group.success = true;
    group.outputFilename = QStringLiteral("stacked_GroupA.fit");
    group.outputBytes = 2048;
    group.lightFrameCount = 1;
    group.outputRetentionStatus = OutputRetentionStatus::NotRetainedByPolicy;
    group.outputRetentionReason = QStringLiteral("the configured history byte limit is too small for this run");
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result, runDir.path());
    const QString markdown = builder.build();
    EXPECT_TRUE(
        markdown.contains(QStringLiteral("Scientific output succeeded, but its history copy was not retained")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("configured history byte limit")));
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.integrationGroupResults.front().success);

    const QString htmlPath = runDir.filePath(QStringLiteral("report.html"));
    ASSERT_TRUE(writeStyledHtmlFromMarkdown(markdown, htmlPath, QStringLiteral("Retention report")));
    QFile htmlFile(htmlPath);
    ASSERT_TRUE(htmlFile.open(QIODevice::ReadOnly | QIODevice::Text));
    EXPECT_TRUE(QString::fromUtf8(htmlFile.readAll())
                    .contains(QStringLiteral("Scientific output succeeded, but its history copy was not retained")));
}
namespace
{
int countOccurrences(const QString& haystack, const QString& needle)
{
    if (needle.isEmpty())
    {
        return 0;
    }

    int count = 0;
    int from = 0;
    while (true)
    {
        from = haystack.indexOf(needle, from);
        if (from < 0)
        {
            break;
        }
        ++count;
        from += needle.size();
    }
    return count;
}

using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureCardKind;
using an::tests::fixtures::FixtureChannels;

/// A sample of a written image, a fraction of full scale, by position and channel.
using SampleRule = std::function<double(std::uint32_t, std::uint32_t, std::uint32_t)>;

/// A card whose value is text.
FixtureCard textCard(const std::string& keyword, const QString& value)
{
    return FixtureCard{keyword, value.toStdString(), {}, FixtureCardKind::Text};
}

/// Writes a Float32 image by the neutral FITS writer: every sample `level`, or what the rule states, and the cards.
bool writeFloatImage(const QString& path, int width, int height, FixtureChannels channels, double level,
                     const SampleRule& rule, std::vector<FixtureCard> cards)
{
    an::tests::fixtures::FixtureImage image;
    // A description takes plain letters, digits, dots, hyphens and underscores; the file keeps its own name.
    for (const QChar character : QFileInfo(path).completeBaseName())
    {
        const char16_t unit = character.unicode();
        const bool plain = (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') ||
                           (unit >= u'0' && unit <= u'9') || unit == u'.' || unit == u'-' || unit == u'_';
        image.name.push_back(plain ? static_cast<char>(unit) : '_');
    }
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.channels = channels;
    image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
    image.pattern = an::tests::fixtures::FixturePattern::Constant;
    an::tests::fixtures::FixtureChannelLevels levels;
    levels.base = level;
    image.levels.assign(an::tests::fixtures::channelCountOf(channels), levels);
    image.cards = std::move(cards);

    std::string error;
    std::optional<an::tests::fixtures::FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return false;
    }
    if (rule)
    {
        for (std::uint32_t channel = 0u; channel < samples->channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < image.height; ++y)
            {
                for (std::uint32_t x = 0u; x < image.width; ++x)
                {
                    samples->setValue(x, y, channel, static_cast<double>(static_cast<float>(rule(x, y, channel))));
                }
            }
        }
    }
    const bool written =
        an::tests::fixtures::writeFixtureFits(image, *samples, std::filesystem::path(path.toStdWString()), error);
    EXPECT_TRUE(written) << error;
    return written;
}

/// The cards a capture program writes for a camera's ISO speed or gain setting, its offset and the target, followed by
/// the telescope and the site where they are given.
std::vector<FixtureCard> equipmentCards(const an::tests::fixtures::AcquisitionFacts& facts, const QString& telescope,
                                        const QString& siteLat, const QString& siteLong)
{
    std::vector<FixtureCard> cards = an::tests::fixtures::acquisitionCards(facts);
    if (!telescope.trimmed().isEmpty())
    {
        cards.push_back(textCard("TELESCOP", telescope));
    }
    if (!siteLat.trimmed().isEmpty())
    {
        cards.push_back(textCard("SITELAT", siteLat));
    }
    if (!siteLong.trimmed().isEmpty())
    {
        cards.push_back(textCard("SITELONG", siteLong));
    }
    return cards;
}

QString writeMasterWithEquipmentMetadata(const QString& path, int width, int height, const QString& camera,
                                         const QString& telescope, bool isIso, int isoOrGain, int offset,
                                         const QString& siteLat, const QString& siteLong, const QString& objectName)
{
    an::tests::fixtures::AcquisitionFacts facts;
    facts.kind = {};
    facts.instrument = camera.toStdString();
    facts.object = objectName.toStdString();
    if (isIso)
    {
        facts.isoSpeed = isoOrGain;
    }
    else
    {
        facts.gain = isoOrGain;
    }
    facts.offset = offset;
    EXPECT_TRUE(writeFloatImage(path, width, height, FixtureChannels::Mono, 0.25, {},
                                equipmentCards(facts, telescope, siteLat, siteLong)));
    return path;
}

QString writeNoisyMaster(const QString& path, int width, int height, float baseLevel = 0.25f)
{
    // Simple deterministic pattern to ensure the background noise is not zero.
    const SampleRule pattern = [baseLevel](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        const float dx = static_cast<float>(x % 7u) * 0.001f;
        const float dy = static_cast<float>(y % 5u) * 0.001f;
        const float checker = ((x ^ y) & 1u) ? 0.002f : -0.002f;
        return static_cast<double>(baseLevel + dx + dy + checker);
    };
    EXPECT_TRUE(writeFloatImage(path, width, height, FixtureChannels::Mono, baseLevel, pattern,
                                {textCard("OBJECT", QStringLiteral("M83"))}));
    return path;
}

QString writeRgbMaster(const QString& path, int width, int height)
{
    const SampleRule pattern = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        constexpr float channelBase[3] = {0.18f, 0.24f, 0.31f};
        const float offset = static_cast<float>((x + y) % 5u) * 0.004f;
        return static_cast<double>(channelBase[channel] + offset);
    };
    EXPECT_TRUE(
        writeFloatImage(path, width, height, FixtureChannels::Rgb, 0.0, pattern,
                        {textCard("OBJECT", QStringLiteral("M83 RGB")), textCard("FILTER", QStringLiteral("RGB"))}));
    return path;
}

QString writeLightFrameWithMetadata(const QString& path, const std::optional<double>& exposureSeconds,
                                    const std::optional<int>& isoOrGain, bool isoMode, const std::optional<int>& offset,
                                    const QString& objectName, const QString& telescope, const QString& siteLat,
                                    const QString& siteLong)
{
    an::tests::fixtures::AcquisitionFacts facts;
    if (exposureSeconds.has_value() && *exposureSeconds > 0.0)
    {
        facts.exposureSeconds = *exposureSeconds;
    }
    if (isoOrGain.has_value())
    {
        if (isoMode)
        {
            facts.isoSpeed = *isoOrGain;
        }
        else
        {
            facts.gain = *isoOrGain;
        }
    }
    if (offset.has_value())
    {
        facts.offset = *offset;
    }
    facts.object = objectName.trimmed().toStdString();
    EXPECT_TRUE(writeFloatImage(path, 16, 10, FixtureChannels::Mono, 0.2, {},
                                equipmentCards(facts, telescope, siteLat, siteLong)));
    return path;
}

/// The engine session a case's report reads its images and headers in; fails the case when it cannot be opened.
std::optional<an::consumer::EngineSession> openReportSession()
{
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::move(session).value();
}

bool writeRegistrationMetricsDb(const QString& dbPath, const QString& groupKey, int settingsVersion,
                                const QList<QJsonObject>& rows)
{
    const QString connectionName =
        QStringLiteral("ReportTestDb_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPath);
        if (!db.open())
        {
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery createQuery(db);
        if (!createQuery.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS registration_metrics ("
                                             "frame_id TEXT NOT NULL,"
                                             "group_key TEXT NOT NULL,"
                                             "settings_version INTEGER NOT NULL,"
                                             "stars INTEGER NOT NULL,"
                                             "fwhm REAL NOT NULL,"
                                             "snr REAL NOT NULL,"
                                             "eccentricity REAL NOT NULL,"
                                             "photometric_zero_point REAL NOT NULL DEFAULT 0.0,"
                                             "zero_point_scatter REAL NOT NULL DEFAULT 0.0,"
                                             "astrometric_rms_pixels REAL NOT NULL DEFAULT 0.0,"
                                             "astrometric_rms_arcsec REAL NOT NULL DEFAULT 0.0,"
                                             "limiting_magnitude REAL NOT NULL DEFAULT 0.0,"
                                             "catalog_matched_stars INTEGER NOT NULL DEFAULT 0,"
                                             "succeeded INTEGER NOT NULL,"
                                             "output_path TEXT,"
                                             "transform_json TEXT,"
                                             "updated_at INTEGER NOT NULL,"
                                             "PRIMARY KEY (frame_id, group_key, settings_version))")))
        {
            db.close();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery insertQuery(db);
        if (!insertQuery.prepare(
                QStringLiteral("INSERT OR REPLACE INTO registration_metrics "
                               "(frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
                               "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, "
                               "astrometric_rms_arcsec, limiting_magnitude, catalog_matched_stars, "
                               "succeeded, output_path, transform_json, updated_at) "
                               "VALUES (:frame_id, :group_key, :settings_version, :stars, :fwhm, :snr, :eccentricity, "
                               ":photometric_zero_point, :zero_point_scatter, :astrometric_rms_pixels, "
                               ":astrometric_rms_arcsec, :limiting_magnitude, :catalog_matched_stars, "
                               ":succeeded, :output_path, :transform_json, :updated_at)")))
        {
            db.close();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        qint64 ts = 1;
        for (const auto& row : rows)
        {
            insertQuery.bindValue(QStringLiteral(":frame_id"), row.value(QStringLiteral("frame_id")).toString());
            insertQuery.bindValue(QStringLiteral(":group_key"), groupKey);
            insertQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
            insertQuery.bindValue(QStringLiteral(":stars"), row.value(QStringLiteral("stars")).toInt());
            insertQuery.bindValue(QStringLiteral(":fwhm"), row.value(QStringLiteral("fwhm")).toDouble());
            insertQuery.bindValue(QStringLiteral(":snr"), row.value(QStringLiteral("snr")).toDouble());
            insertQuery.bindValue(QStringLiteral(":eccentricity"),
                                  row.value(QStringLiteral("eccentricity")).toDouble());
            insertQuery.bindValue(QStringLiteral(":photometric_zero_point"),
                                  row.contains(QStringLiteral("photometric_zero_point"))
                                      ? row.value(QStringLiteral("photometric_zero_point")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":zero_point_scatter"),
                                  row.contains(QStringLiteral("zero_point_scatter"))
                                      ? row.value(QStringLiteral("zero_point_scatter")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":astrometric_rms_pixels"),
                                  row.contains(QStringLiteral("astrometric_rms_pixels"))
                                      ? row.value(QStringLiteral("astrometric_rms_pixels")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":astrometric_rms_arcsec"),
                                  row.contains(QStringLiteral("astrometric_rms_arcsec"))
                                      ? row.value(QStringLiteral("astrometric_rms_arcsec")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":limiting_magnitude"),
                                  row.contains(QStringLiteral("limiting_magnitude"))
                                      ? row.value(QStringLiteral("limiting_magnitude")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":catalog_matched_stars"),
                                  row.contains(QStringLiteral("catalog_matched_stars"))
                                      ? row.value(QStringLiteral("catalog_matched_stars")).toInt()
                                      : 0);
            insertQuery.bindValue(QStringLiteral(":succeeded"),
                                  row.value(QStringLiteral("succeeded")).toBool() ? 1 : 0);
            insertQuery.bindValue(QStringLiteral(":output_path"), row.value(QStringLiteral("output_path")).toString());
            insertQuery.bindValue(QStringLiteral(":transform_json"),
                                  row.value(QStringLiteral("transform_json")).toString(QStringLiteral("{}")));
            insertQuery.bindValue(QStringLiteral(":updated_at"), ts++);
            if (!insertQuery.exec())
            {
                db.close();
                QSqlDatabase::removeDatabase(connectionName);
                return false;
            }
        }

        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

an::MatchedSourceReference makeSpccHandoffReference(std::uint64_t sourceId, double separationArcsec, double residualPx)
{
    an::MatchedSourceReference reference;
    reference.sourceIdentifier = sourceId;
    reference.astrometricCatalogFamilyId = "gaia_dr3";
    reference.astrometricCatalogManifestId = "gaia-dr3-main-2026-05";
    reference.spectrumCatalogFamilyId = "spcc_gaia_dr3_xp_sampled";
    reference.spectrumCatalogManifestId = "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    reference.wcsSolutionId = "iip-wcs-solution-42";
    reference.observationEpoch = "2025.25";
    reference.properMotionModelVersion = "gaia-dr3-pm-v1";
    reference.separationArcseconds = separationArcsec;
    reference.residualPixels = residualPx;
    return reference;
}
} // namespace

TEST(WorkflowReportBuilder, GeneratesBasicReport)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = "/tmp/test";
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 5000;

    IntegrationGroupResult gr;
    gr.groupKey = "Ha_Group";
    gr.success = true;
    gr.lightFrameCount = 10;
    result.integrationGroupResults.push_back(gr);

    WorkflowReportBuilder builder(manifest, result);
    QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Integration Report")));
    EXPECT_TRUE(report.contains(QStringLiteral("Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("Full"))); // Mode
    EXPECT_TRUE(report.contains(QStringLiteral("5s"))); // Duration
    EXPECT_TRUE(report.contains(QStringLiteral("Stacked Masters")));
    EXPECT_TRUE(report.contains(QStringLiteral("Ha_Group")));
    EXPECT_TRUE(report.contains(QStringLiteral("10"))); // Frames
}

TEST(WorkflowReportBuilder, ReportsExclusivePreparationAndExecutorPhasesWithoutDoubleCountingParents)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 900;
    result.preparationTiming.captured = true;
    result.preparationTiming.manifestBuildMs = 125;
    result.preparationTiming.planAndValidationMs = 275;
    result.executorTiming.captured = true;
    result.executorTiming.stageExecutionMs = 500;
    result.executorTiming.stateCommitMs = 100;
    result.executorTiming.archiveMs = 200;
    result.executorTiming.reportMs = 100;

    WorkflowExecutor::Result::StageTiming stage;
    stage.stageName = QStringLiteral("LightsStacking");
    stage.status = QStringLiteral("completed");
    stage.durationMs = 333;
    result.stageResults.push_back(stage);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Workflow Timing")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Manifest Build | 125ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Plan & Validation | 275ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Preparation Total (derived) | 400ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Executor Total (parent) | 900ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Stage Execution | 500ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| State Commit | 100ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Archive | 200ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Report | 100ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Start to Ready (derived) | 1s |")));
    EXPECT_TRUE(report.contains(QStringLiteral("exclusive sibling phases")));
    EXPECT_TRUE(report.contains(QStringLiteral("parent totals must not be added to their children")));
    EXPECT_TRUE(report.contains(QStringLiteral("skipped phase is represented as 0ms")));
    EXPECT_TRUE(report.contains(QStringLiteral("| 1 | Lights Stacking |")));
    EXPECT_FALSE(report.contains(QStringLiteral("| Accounted Total | 1s |")));
}

TEST(WorkflowReportBuilder, ReportsCapturedZeroPreparationDurations)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.preparationTiming.captured = true;
    result.executorTiming.captured = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("| Manifest Build | 0ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Plan & Validation | 0ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Preparation Total (derived) | 0ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Stage Execution | 0ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| State Commit | 0ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Archive | 0ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Report | 0ms |")));
}

TEST(WorkflowReportBuilder, OptionalExecutionCountersAreOmittedByDefaultAndRenderedExactlyWhenPresent)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");

    WorkflowExecutor::Result result;
    WorkflowReportBuilder defaultBuilder(manifest, result);
    EXPECT_FALSE(defaultBuilder.build().contains(QStringLiteral("### Execution Counters")));

    result.executionCounters.emplace(11, 22, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    WorkflowReportBuilder instrumentedBuilder(manifest, result);
    const QString report = instrumentedBuilder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("### Execution Counters")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Bytes hashed | 11 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Bytes copied | 22 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| SQLite statement executions | 3 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| SQLite transactions started | 4 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| SQLite transactions committed | 5 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| SQLite transactions rolled back | 6 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Input decode operations | 7 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Scientific output encode completions | 8 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Input passes started | 9 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Tile passes started | 10 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Dump payload bytes read | 11 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Dump payload bytes written | 12 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Dense pixmap payload bytes read | 13 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Dense pixmap payload bytes written | 14 |")));
}

TEST(WorkflowReportBuilder, DeferredExecutionCountersUseTheFinalExecutorSnapshot)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result templateResult;
    templateResult.executorTiming.captured = true;

    WorkflowExecutionCounterAccumulator accumulator;
    accumulator.addBytesHashed(1);
    QJsonObject context;
    context.insert(QStringLiteral("deferWorkflowTiming"), true);
    WorkflowReportBuilder builder(manifest, templateResult, QString(), context, &accumulator);
    const QString templateReport = builder.build();
    EXPECT_TRUE(templateReport.contains(QStringLiteral("{{AFTERNIGHT_BYTES_HASHED}}")));

    WorkflowExecutor::Result finalResult = templateResult;
    finalResult.executionCounters.emplace(101, 202, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    const QString finalReport = WorkflowReportBuilder::finalizeTimingPlaceholders(templateReport, finalResult);

    EXPECT_FALSE(finalReport.contains(QStringLiteral("{{AFTERNIGHT_")));
    EXPECT_TRUE(finalReport.contains(QStringLiteral("| Bytes hashed | 101 |")));
    EXPECT_TRUE(finalReport.contains(QStringLiteral("| Bytes copied | 202 |")));
    EXPECT_TRUE(finalReport.contains(QStringLiteral("| SQLite statement executions | 3 |")));
    EXPECT_TRUE(finalReport.contains(QStringLiteral("| Input decode operations | 7 |")));
    EXPECT_TRUE(finalReport.contains(QStringLiteral("| Dense pixmap payload bytes written | 14 |")));
}

TEST(WorkflowReportBuilder, ReportsExclusiveStackFinalizationPhasesWithoutDoubleCountingGroupParent)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("FinalizationGroup");
    group.outputFilename = QStringLiteral("stacked_FinalizationGroup.fit");
    group.outputBytes = 4096;
    group.lightFrameCount = 4;
    group.durationMs = 900;
    group.success = true;
    group.finalizationTiming.captured = true;
    group.finalizationTiming.phases = {
        {.stageName = QStringLiteral("Final Master Save"),
         .status = QStringLiteral("completed"),
         .durationMs = 110,
         .groupsProcessed = 1},
        {.stageName = QStringLiteral("Requested Maps"),
         .status = QStringLiteral("skipped"),
         .durationMs = 0,
         .groupsProcessed = 0},
        {.stageName = QStringLiteral("Final Master WCS"),
         .status = QStringLiteral("completed"),
         .durationMs = 120,
         .groupsProcessed = 1},
        {.stageName = QStringLiteral("Preview Generation"),
         .status = QStringLiteral("completed"),
         .durationMs = 130,
         .groupsProcessed = 1},
        {.stageName = QStringLiteral("Digest & Artifact Recording"),
         .status = QStringLiteral("completed"),
         .durationMs = 140,
         .groupsProcessed = 1},
        {.stageName = QStringLiteral("Completion Publication"),
         .status = QStringLiteral("completed"),
         .durationMs = 150,
         .groupsProcessed = 1},
    };
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Duration (parent): 900ms")));
    EXPECT_TRUE(report.contains(QStringLiteral("exclusive child phases")));
    EXPECT_TRUE(report.contains(QStringLiteral("included in the group parent")));
    EXPECT_TRUE(report.contains(QStringLiteral("must not be added")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Final Master Save | completed | 110ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Requested Maps | skipped | 0ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Final Master WCS | completed | 120ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Preview Generation | completed | 130ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Digest & Artifact Recording | completed | 140ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Completion Publication | completed | 150ms |")));
    EXPECT_EQ(countOccurrences(report, QStringLiteral("900ms")), 1);
    EXPECT_FALSE(report.contains(QStringLiteral("Finalization Total")));
}

TEST(WorkflowReportBuilder, ReportsOnlyExecutedPhasesForFailedStackFinalizationAttempt)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = false;
    result.partialSuccess = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("FailedFinalizationGroup");
    group.outputFilename = QStringLiteral("stacked_FailedFinalizationGroup.fit");
    group.durationMs = 80;
    group.success = false;
    group.finalizationTiming.captured = true;
    group.finalizationTiming.phases = {
        {.stageName = QStringLiteral("Final Master Save"),
         .status = QStringLiteral("failed"),
         .durationMs = 30,
         .groupsProcessed = 1},
        {.stageName = QStringLiteral("Requested Maps"),
         .status = QStringLiteral("skipped"),
         .durationMs = 0,
         .groupsProcessed = 0},
    };
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Incomplete Stack Group Finalization Attempts")));
    EXPECT_TRUE(report.contains(QStringLiteral("FailedFinalizationGroup")));
    EXPECT_TRUE(report.contains(QStringLiteral("Duration (parent): 80ms")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Final Master Save | failed | 30ms |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Requested Maps | skipped | 0ms |")));
    EXPECT_EQ(countOccurrences(report, QStringLiteral("80ms")), 1);
}

TEST(WorkflowReportBuilder, LiteMetadataAppearsInReportSummary)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = "/tmp/test";
    manifest.executionMode = ExecutionMode::Full;
    manifest.processLabel = QStringLiteral("Image Integration Lite");
    manifest.processingProfileId = QStringLiteral("high_quality");
    manifest.processingProfileLabel = QStringLiteral("High Quality");

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("AfterNight Image Integration Lite Report")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Process | Image Integration Lite |")));
    EXPECT_TRUE(report.contains(QStringLiteral("Processing Profile")));
    EXPECT_TRUE(report.contains(QStringLiteral("High Quality (`high_quality`)")));
}

TEST(WorkflowReportBuilder, IncludesWarnings)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.warnings.append("Warning 1");
    result.errors.append("Error 1");

    WorkflowReportBuilder builder(manifest, result);
    QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Warnings & Errors")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-issues-list")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-issue-item an-issue-error")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-issue-item an-issue-warning")));
    EXPECT_TRUE(report.contains(QStringLiteral("Warning 1")));
    EXPECT_TRUE(report.contains(QStringLiteral("ERROR")));
    EXPECT_TRUE(report.contains(QStringLiteral("Error 1")));
}

TEST(WorkflowReportBuilder, IncludesExpectedSectionsForWorkflowData)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = "/tmp/test";
    manifest.executionMode = ExecutionMode::Incremental;

    WorkflowManifest::IntegrationGroup g1;
    g1.groupKey = IntegrationGroupKey{GroupKey{"Ha_Group"}};
    g1.outputFilename = "stacked_Ha.fit";
    g1.lightFiles = {FrameId("/tmp/light1.fit"), FrameId("/tmp/light2.fit")};
    g1.stacking.stackMethod = "Average";
    manifest.integrationGroups.push_back(g1);

    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = "BIAS";
    WorkflowManifest::CalibrationStage::Group biasGroup;
    biasGroup.groupKey = GroupKey{"Bias_Group"};
    biasGroup.inputFiles = {FrameId("/tmp/bias1.fit")};
    biasGroup.outputFilename = "master_bias.fit";
    biasStage.groups.push_back(biasGroup);
    manifest.biasStage = biasStage;

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 65000;

    WorkflowExecutor::Result::StageTiming stage;
    stage.stageName = "LightsStacking";
    stage.status = "completed";
    stage.durationMs = 12000;
    stage.groupsProcessed = 1;
    result.stageResults.push_back(stage);

    IntegrationGroupResult gr;
    gr.groupKey = "Ha_Group";
    gr.success = true;
    gr.outputFilename = "stacked_Ha.fit";
    gr.outputBytes = 12345;
    gr.lightFrameCount = 2;
    result.integrationGroupResults.push_back(gr);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("Pipeline Stages")));
    EXPECT_TRUE(report.contains(QStringLiteral("Stacked Masters")));
    EXPECT_TRUE(report.contains(QStringLiteral("Input Files")));
    EXPECT_TRUE(report.contains(QStringLiteral("high-level run KPI snapshot")));
    EXPECT_TRUE(report.contains(QStringLiteral("concise run metadata and totals")));
    EXPECT_TRUE(report.contains(QStringLiteral("stage-level execution timeline")));
    EXPECT_TRUE(report.contains(QStringLiteral("source frame inventory")));

    // Removed sections should not appear
    EXPECT_FALSE(report.contains(QStringLiteral("Quick Look")));
    EXPECT_FALSE(report.contains(QStringLiteral("Top Changes Since Last Run")));
    EXPECT_FALSE(report.contains(QStringLiteral("Settings Detail")));
    EXPECT_FALSE(report.contains(QStringLiteral("Execution Summary")));
}

TEST(WorkflowReportBuilder, CalibrationSummaryIncludesEffectiveStackingSettings)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::CalibrationStage flatStage;
    flatStage.frameType = QStringLiteral("FLAT");
    WorkflowManifest::CalibrationStage::Group flatGroup;
    flatGroup.groupKey = GroupKey{QStringLiteral("Flat_Group")};
    flatGroup.inputFiles = {FrameId(QStringLiteral("/tmp/flat1.fit")), FrameId(QStringLiteral("/tmp/flat2.fit"))};
    flatGroup.outputFilename = QStringLiteral("master_flat.fit");
    flatGroup.stacking.stackMethod = QStringLiteral("Average");
    flatGroup.stacking.requestedRejectionMethod = QStringLiteral("Auto");
    flatGroup.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
    flatGroup.stacking.normalizationMode = NormalizationMode::Global;
    flatGroup.stacking.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
    flatStage.groups.push_back(flatGroup);
    manifest.flatStage = flatStage;

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Calibration Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("Stacking Settings")));
    EXPECT_TRUE(report.contains(QStringLiteral("Auto (Sigma Clipping)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Global (Multiplicative)")));
    EXPECT_FALSE(report.contains(QStringLiteral("Multiplicative, IKSS")));
}

// A group marked pre-calibrated has no calibration entry, as a group no master matched has none; the calibration
// section names it, so the report tells the two apart - also in a run that loads no calibration frame at all.
TEST(WorkflowReportBuilder, CalibrationSummaryNamesThePreCalibratedGroups)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::PreCalibratedLightGroup group;
    group.groupKey = GroupKey{QStringLiteral("Light_Group_Calibrated_Elsewhere")};
    group.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit")), FrameId(QStringLiteral("/tmp/light2.fit"))};
    manifest.preCalibratedLightGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    const QString report = WorkflowReportBuilder(manifest, result).build();

    EXPECT_TRUE(report.contains(QStringLiteral("Calibration Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("Pre-calibrated light groups")));
    EXPECT_TRUE(report.contains(QStringLiteral("Light_Group_Calibrated_Elsewhere")));
    EXPECT_TRUE(report.contains(QStringLiteral("2 light(s)")));
}

TEST(WorkflowReportBuilder, HandlesCancelledResult)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.cancelled = true;
    result.success = false;
    result.errors.append("Cancelled by user");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Integration Report")));
    EXPECT_TRUE(report.contains(QStringLiteral("Warnings & Errors")));
    EXPECT_TRUE(report.contains(QStringLiteral("Cancelled by user")));
}

TEST(WorkflowReportBuilder, SupportsVariableMasterCounts)
{
    for (const int groupCount : {1, 3, 10})
    {
        WorkflowManifest manifest;
        manifest.executionMode = ExecutionMode::Full;

        WorkflowExecutor::Result result;
        result.success = true;

        for (int i = 0; i < groupCount; ++i)
        {
            IntegrationGroupResult gr;
            gr.groupKey = QStringLiteral("Group_%1").arg(i + 1);
            gr.success = true;
            gr.outputFilename = QStringLiteral("stacked_%1.fit").arg(i + 1);
            gr.outputBytes = 1024 * (i + 1);
            gr.lightFrameCount = i + 2;
            result.integrationGroupResults.push_back(std::move(gr));
        }

        WorkflowReportBuilder builder(manifest, result);
        const QString report = builder.build();

        EXPECT_EQ(countOccurrences(report, QStringLiteral("### Group_")), groupCount);
    }
}

TEST(WorkflowReportBuilder, HandlesPartialAndFailureResults)
{
    WorkflowManifest manifest;
    manifest.executionMode = ExecutionMode::Incremental;

    WorkflowExecutor::Result result;
    result.success = false;
    result.cancelled = false;
    result.totalDurationMs = 12345;
    result.warnings.append(QStringLiteral("One group was skipped"));
    result.errors.append(QStringLiteral("Final integration failed"));

    IntegrationGroupResult okGroup;
    okGroup.groupKey = QStringLiteral("Group_Ok");
    okGroup.success = true;
    okGroup.outputFilename = QStringLiteral("stacked_ok.fit");
    okGroup.outputBytes = 4096;
    okGroup.lightFrameCount = 4;
    result.integrationGroupResults.push_back(okGroup);

    IntegrationGroupResult failedGroup;
    failedGroup.groupKey = QStringLiteral("Group_Failed");
    failedGroup.success = false;
    failedGroup.outputFilename = QStringLiteral("stacked_failed.fit");
    failedGroup.outputBytes = 0;
    failedGroup.lightFrameCount = 4;
    result.integrationGroupResults.push_back(failedGroup);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("Stacked Masters")));
    EXPECT_TRUE(report.contains(QStringLiteral("Group_Ok")));
    EXPECT_FALSE(report.contains(QStringLiteral("### Group_Failed")));
    EXPECT_TRUE(report.contains(QStringLiteral("Failure Diagnostics")));
    EXPECT_TRUE(report.contains(QStringLiteral("Group_Failed")));
    EXPECT_TRUE(report.contains(QStringLiteral("Warnings & Errors")));
    EXPECT_TRUE(report.contains(QStringLiteral("Final integration failed")));
    EXPECT_TRUE(report.contains(QStringLiteral("One group was skipped")));
}

TEST(WorkflowReportBuilder, DisplaysFriendlyGroupKeysInsteadOfRawPipeSeparated)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = "/tmp/test";
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")}};
    integrationGroup.registrationGroupKey = GroupKey{QStringLiteral("3008x3004|-|-|-|-|M 83|-")};
    integrationGroup.outputFilename = QStringLiteral("stacked_m83.fit");
    integrationGroup.lightFiles = {FrameId("/tmp/light1.fit")};
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group biasGroup;
    biasGroup.groupKey = GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")};
    biasGroup.inputFiles = {FrameId("/tmp/bias1.fit")};
    biasGroup.outputFilename = QStringLiteral("master_bias.fit");
    biasStage.groups.push_back(biasGroup);
    manifest.biasStage = biasStage;

    WorkflowManifest::RegistrationGroup registrationGroup;
    registrationGroup.groupKey = GroupKey{QStringLiteral("3008x3004|-|-|-|-|M 83|-")};
    registrationGroup.lightFiles = {FrameId("/tmp/light1.fit")};
    manifest.registrationGroups.push_back(registrationGroup);

    WorkflowExecutor::Result result;
    result.success = true;
    IntegrationGroupResult stacked;
    stacked.groupKey = QStringLiteral("3008x3004|-|-|-|Blue|M 83|-");
    stacked.registrationGroupKey = QStringLiteral("3008x3004|-|-|-|-|M 83|-");
    stacked.outputFilename = QStringLiteral("stacked_m83.fit");
    stacked.success = true;
    stacked.lightFrameCount = 1;
    result.integrationGroupResults.push_back(stacked);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Friendly format: "Blue — M 83 (3008 × 3004)" instead of "3008x3004|Blue|M 83"
    EXPECT_TRUE(report.contains(QStringLiteral("Blue \u2014 M 83")));
    EXPECT_TRUE(report.contains(QStringLiteral("3008 \u00D7 3004")));
    EXPECT_TRUE(report.contains(QStringLiteral("calibration master provenance")));
    EXPECT_TRUE(report.contains(QStringLiteral("alignment provenance per registration group")));
    // Registration group should show as "M 83 (3008 × 3004)"
    EXPECT_TRUE(report.contains(QStringLiteral("M 83 (3008 \u00D7 3004)")));
    // Raw pipe-separated keys should not appear
    EXPECT_FALSE(report.contains(QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")));
    EXPECT_FALSE(report.contains(QStringLiteral("3008x3004|-|-|-|-|M 83|-")));
    EXPECT_FALSE(report.contains(QStringLiteral("3008x3004|Blue|M 83")));
}

// Each master's row says where its world-coordinate solution came from - carried from the registration, solved, or
// none and why - and a run archived before the stage kept it says nothing. The stage said it in the run log alone.
TEST(WorkflowReportBuilder, EachMastersSolutionOutcomeIsPrintedBesideTheMaster)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = "/tmp/test";
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|L|M 83|-")}};
    integrationGroup.outputFilename = QStringLiteral("stacked_m83.fit");
    integrationGroup.lightFiles = {FrameId("/tmp/light1.fit")};
    manifest.integrationGroups.push_back(integrationGroup);

    const auto reportFor = [&](const MasterSolutionOutcome outcome, const QString& detail) {
        WorkflowExecutor::Result result;
        result.success = true;
        IntegrationGroupResult stacked;
        stacked.groupKey = integrationGroup.groupKey.toString();
        stacked.outputFilename = integrationGroup.outputFilename;
        stacked.success = true;
        stacked.lightFrameCount = 1;
        stacked.masterSolution = outcome;
        stacked.masterSolutionDetail = detail;
        result.integrationGroupResults.push_back(stacked);
        WorkflowReportBuilder builder(manifest, result);
        return builder.build();
    };
    const std::vector<std::tuple<MasterSolutionOutcome, QString, QString>> outcomes{
        {MasterSolutionOutcome::Propagated, QStringLiteral("Propagated"),
         QStringLiteral("Carried from the persisted registration output plane.")},
        {MasterSolutionOutcome::Solved, QStringLiteral("Solved"),
         QStringLiteral("Solved by the configured plate solver.")},
        {MasterSolutionOutcome::None, QStringLiteral("None"),
         QStringLiteral("Saved without a valid WCS; fallback plate solving is disabled.")}};
    for (const auto& [outcome, label, detail] : outcomes)
    {
        const QString report = reportFor(outcome, detail);
        EXPECT_TRUE(report.contains(QStringLiteral("- **Master Solution**: %1 - %2").arg(label, detail)))
            << label.toStdString();
    }
    EXPECT_FALSE(reportFor(MasterSolutionOutcome::NotRecorded, QString()).contains(QStringLiteral("Master Solution")));
}

TEST(WorkflowReportBuilder, RegistrationSummaryShowsSipDistortionCorrectionWhenPresent)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::RegistrationGroup registrationGroup;
    registrationGroup.groupKey = GroupKey{QStringLiteral("4000x3000|-|-|-|-|NGC 7000|-")};
    registrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/l1.fit")), FrameId(QStringLiteral("/tmp/l2.fit")),
                                    FrameId(QStringLiteral("/tmp/l3.fit"))};
    manifest.registrationGroups.push_back(registrationGroup);

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("4000x3000|-|-|-|-|NGC 7000|-")}};
    integrationGroup.registrationGroupKey = GroupKey{QStringLiteral("4000x3000|-|-|-|-|NGC 7000|-")};
    integrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/l1.fit"))};
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowExecutor::Result result;
    result.success = true;

    AlignmentGroupResult agr;
    agr.groupKey = QStringLiteral("4000x3000|-|-|-|-|NGC 7000|-");
    agr.totalAligned = 3;
    agr.sipCorrectedCount = 2;
    result.alignmentGroupResults.push_back(agr);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("SIP Distortion Correction")));
    EXPECT_TRUE(report.contains(QStringLiteral("Simple Imaging Polynomial")));
    EXPECT_TRUE(report.contains(QStringLiteral("SIP-corrected frames")));
    // Group should appear as friendly name
    EXPECT_TRUE(report.contains(QStringLiteral("NGC 7000")));
    // The table row should contain both counts
    EXPECT_TRUE(report.contains(QStringLiteral("| 2 | 3 |")));
}

TEST(WorkflowReportBuilder, RegistrationSummaryOmitsSipSectionWhenNoSipCorrection)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::RegistrationGroup registrationGroup;
    registrationGroup.groupKey = GroupKey{QStringLiteral("4000x3000|-|-|-|-|M 31|-")};
    registrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/l1.fit"))};
    manifest.registrationGroups.push_back(registrationGroup);

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("4000x3000|-|-|-|-|M 31|-")}};
    integrationGroup.registrationGroupKey = GroupKey{QStringLiteral("4000x3000|-|-|-|-|M 31|-")};
    integrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/l1.fit"))};
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowExecutor::Result result;
    result.success = true;

    // Alignment ran but no SIP headers were found
    AlignmentGroupResult agr;
    agr.groupKey = QStringLiteral("4000x3000|-|-|-|-|M 31|-");
    agr.totalAligned = 5;
    agr.sipCorrectedCount = 0;
    result.alignmentGroupResults.push_back(agr);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_FALSE(report.contains(QStringLiteral("SIP Distortion Correction")));
}

TEST(WorkflowReportBuilder, RegistrationSummaryExportsSpccHandoffDiagnostics)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir root(tempDir.path());

    const QString groupKey = QStringLiteral("SPCCRegGroup");
    const FrameId frameA(root.filePath(QStringLiteral("light_a.fit")));
    const FrameId frameB(root.filePath(QStringLiteral("light_b.fit")));

    an::ui::RegistrationMetricsWriter writer(root.filePath(QStringLiteral("registration_metrics.db")),
                                             QStringLiteral("WorkflowReportSpccHandoffTest"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(
        writer.upsertMatchedSource({frameA, groupKey, 1, makeSpccHandoffReference(589012345678901100ULL, 9.0, 9.0),
                                    QStringLiteral("stale_match"), 1700000000},
                                   &error))
        << error.toStdString();
    ASSERT_TRUE(
        writer.upsertMatchedSource({frameA, groupKey, 2, makeSpccHandoffReference(589012345678901234ULL, 0.31, 0.20),
                                    QStringLiteral("satisfied"), 1700000001},
                                   &error))
        << error.toStdString();
    ASSERT_TRUE(
        writer.upsertMatchedSource({frameA, groupKey, 2, makeSpccHandoffReference(589012345678901235ULL, 0.61, 0.40),
                                    QStringLiteral("satisfied"), 1700000002},
                                   &error))
        << error.toStdString();
    ASSERT_TRUE(
        writer.upsertMatchedSource({frameB, groupKey, 2, makeSpccHandoffReference(589012345678901236ULL, 1.20, 0.70),
                                    QStringLiteral("missing_chunk_or_cache"), 1700000003},
                                   &error))
        << error.toStdString();

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.registrationGroups.push_back({GroupKey{groupKey},
                                           QStringLiteral("3008x3004"),
                                           QStringLiteral("M31"),
                                           {frameA, frameB},
                                           RegistrationSettings{},
                                           std::nullopt});

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("SPCC Source-ID Handoff Diagnostics")));
    EXPECT_TRUE(report.contains(QStringLiteral("registration_metrics.db")));
    EXPECT_TRUE(report.contains(QStringLiteral("SPCCRegGroup")));
    EXPECT_TRUE(report.contains(QStringLiteral("| v2 | 2 | 3 | 2/3 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("missing chunk/cache: 1")));
    EXPECT_TRUE(report.contains(QStringLiteral("spcc-gaia-dr3-xp-sampled-m31-region-20260510")));
    EXPECT_TRUE(report.contains(QStringLiteral("gaia-dr3-main-2026-05")));
    EXPECT_TRUE(report.contains(QStringLiteral("0.4 px")));
    EXPECT_TRUE(report.contains(QStringLiteral("0.61 arcsec")));
    EXPECT_FALSE(report.contains(QStringLiteral("589012345678901100")));
}

TEST(WorkflowReportBuilder, InputFilesSummaryTableAndCollapsibleLists)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    integrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit")),
                                   FrameId(QStringLiteral("/tmp/light2.fit"))};
    integrationGroup.exposure = QStringLiteral("90");
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group biasGroup;
    biasGroup.groupKey = GroupKey{QStringLiteral("BiasA")};
    biasGroup.inputFiles = {FrameId(QStringLiteral("/tmp/bias1.fit"))};
    biasStage.groups.push_back(biasGroup);
    manifest.biasStage = biasStage;

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Input Files")));
    // Summary table with exposure and integration time
    EXPECT_TRUE(report.contains(QStringLiteral("| Group | Frames | Exposures | Integration Time |")));
    EXPECT_TRUE(report.contains(QStringLiteral("90s")));
    EXPECT_TRUE(report.contains(QStringLiteral("3m 0s"))); // 2 * 90s = 180s = 3m
    EXPECT_TRUE(report.contains(QStringLiteral("**Total**")));
    // Calibration summary
    EXPECT_TRUE(report.contains(QStringLiteral("Calibration frames")));
    EXPECT_TRUE(report.contains(QStringLiteral("1 bias")));
    // Collapsible file lists still present
    EXPECT_TRUE(report.contains(QStringLiteral("<details>\n<summary>Light Frames (2)</summary>")));
    EXPECT_TRUE(report.contains(QStringLiteral("<details>\n<summary>Bias Frames (1)</summary>")));
    EXPECT_FALSE(report.contains(QStringLiteral("<details open>")));
}

TEST(WorkflowReportBuilder, IncludesExecutiveSummaryBeforeDetailedSummary)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit"))};
    group.exposure = QStringLiteral("90");
    manifest.integrationGroups.push_back(group);
    manifest.incrementalIntegrationGroups.push_back(group.groupKey);

    WorkflowExecutor::Result result;
    result.success = true;
    IntegrationGroupResult regenerated;
    regenerated.groupKey = group.groupKey.toString();
    regenerated.success = true;
    result.integrationGroupResults.push_back(regenerated);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    const int executivePos = report.indexOf(QStringLiteral("Executive Summary"));
    const int summaryPos = report.indexOf(QStringLiteral("## 📋 Summary"));
    ASSERT_GE(executivePos, 0);
    ASSERT_GE(summaryPos, 0);
    EXPECT_LT(executivePos, summaryPos);
}

TEST(WorkflowReportBuilder, TechnicalAppendixContainsVerboseArtifacts)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    integrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit"))};
    integrationGroup.exposure = QStringLiteral("90");
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowExecutor::Result result;
    result.success = true;
    result.capturedLog = QStringLiteral("[2026-02-10T23:53:35.857] [HEADER] WORKFLOW STARTED\n");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    const int appendixPos = report.indexOf(QStringLiteral("Technical Appendix"));
    ASSERT_GE(appendixPos, 0);

    const int fileListPos = report.indexOf(QStringLiteral("<summary>Light Frames (1)</summary>"));
    const int logPos = report.indexOf(QStringLiteral("Execution Log"));
    ASSERT_GE(fileListPos, 0);
    ASSERT_GE(logPos, 0);
    EXPECT_GT(fileListPos, appendixPos);
    EXPECT_GT(logPos, appendixPos);
}

TEST(WorkflowReportBuilder, EmbedsMasterAndRejectionMapPreviewsWithStats)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir runDir(tempDir.path());
    ASSERT_TRUE(runDir.mkpath(QStringLiteral("groups/GroupA")));

    const QString outputPath = runDir.filePath(QStringLiteral("stacked_GroupA.fit"));
    {
        QFile outputFile(outputPath);
        ASSERT_TRUE(outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        outputFile.write(QByteArray(2048, 'x'));
    }

    const QString masterPreviewRelative = QStringLiteral("groups/GroupA/preview.png");
    {
        QImage masterPreview(16, 8, QImage::Format_RGB32);
        masterPreview.fill(qRgb(120, 140, 160));
        ASSERT_TRUE(masterPreview.save(runDir.filePath(masterPreviewRelative), "PNG"));
    }

    const QString rejectionPath = runDir.filePath(QStringLiteral("stacked_GroupA_rejection.fit"));
    {
        QFile rejectionFile(rejectionPath);
        ASSERT_TRUE(rejectionFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        rejectionFile.write(QByteArray(4096, 'r'));
    }

    const QString rejectionPreviewRelative = QStringLiteral("groups/GroupA/rejection_map_1.png");
    {
        QImage rejectionPreview(10, 5, QImage::Format_Grayscale8);
        rejectionPreview.fill(0);
        rejectionPreview.setPixel(0, 0, 255);
        rejectionPreview.setPixel(1, 0, 255);
        ASSERT_TRUE(rejectionPreview.save(runDir.filePath(rejectionPreviewRelative), "PNG"));
    }

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    integrationGroup.outputFilename = QStringLiteral("stacked_GroupA.fit");
    integrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/light.fit"))};
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("GroupA");
    group.success = true;
    group.outputPath = outputPath;
    group.outputFilename = QStringLiteral("stacked_GroupA.fit");
    group.outputBytes = 2048;
    group.lightFrameCount = 1;
    group.previewPath = masterPreviewRelative;
    RejectionMapResult rejectionMap;
    rejectionMap.title = QStringLiteral("Rejection Map");
    rejectionMap.path = rejectionPath;
    rejectionMap.previewPath = rejectionPreviewRelative;
    group.rejectionMaps.push_back(rejectionMap);
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result, runDir.path());
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("src=\"groups/GroupA/preview.png\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("Rejection Map")));
    EXPECT_TRUE(report.contains(QStringLiteral("src=\"groups/GroupA/rejection_map_1.png\"")));
    // Rejection maps are presented as secondary data (no stats/histograms for them).
    EXPECT_TRUE(report.contains(QStringLiteral("Rejection maps (secondary)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Rejection Map Coverage")));
    // Master preview still gets stats, but no histogram.
    EXPECT_FALSE(report.contains(QStringLiteral("class=\"an-histogram\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("4.0 KB")));
}

TEST(WorkflowReportBuilder, ExecutiveSummaryShowsFrameRetentionFromLog)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;

    WorkflowManifest::IntegrationGroup g1;
    g1.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    g1.lightFiles = {FrameId(QStringLiteral("/tmp/a.fit"))};
    manifest.integrationGroups.push_back(g1);

    WorkflowManifest::IntegrationGroup g2;
    g2.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupB")}};
    g2.lightFiles = {FrameId(QStringLiteral("/tmp/b.fit"))};
    manifest.integrationGroups.push_back(g2);

    WorkflowExecutor::Result result;
    result.success = true;
    result.capturedLog = QStringLiteral(
        "[2026-02-11T02:54:53.176] [HEADER]   [1/2] Processing: GroupA\n"
        "[2026-02-11T02:54:53.184] [DETAIL]     → Inputs: Lights: 10 | Effective: 8 | Registered: 7\n"
        "[2026-02-11T02:54:53.200] [HEADER]   [2/2] Processing: GroupB\n"
        "[2026-02-11T02:54:53.210] [DETAIL]     → Inputs: Lights: 20 | Effective: 19 | Registered: 18\n");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Frame Retention (Log)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Effective 90.0%")));
    EXPECT_TRUE(report.contains(QStringLiteral("Registered 83.3%")));
    EXPECT_TRUE(report.contains(QStringLiteral("Usable/Eff 92.6%")));
}

TEST(WorkflowReportBuilder, MasterStatsUseRawOutputNotPreviewImage)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir runDir(tempDir.path());
    ASSERT_TRUE(runDir.mkpath(QStringLiteral("groups/GroupA")));

    const QString previewRelative = QStringLiteral("groups/GroupA/preview.png");
    {
        QImage preview(32, 16, QImage::Format_RGB888);
        preview.fill(Qt::white);
        ASSERT_TRUE(preview.save(runDir.filePath(previewRelative), "PNG"));
    }

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    integrationGroup.outputFilename = QStringLiteral("stacked_GroupA.fit");
    integrationGroup.lightFiles = {FrameId(QStringLiteral("/tmp/light.fit"))};
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("GroupA");
    group.success = true;
    group.outputPath = runDir.filePath(QStringLiteral("stacked_GroupA.fit")); // intentionally missing
    group.outputFilename = QStringLiteral("stacked_GroupA.fit");
    group.previewPath = previewRelative;
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result, runDir.path());
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("src=\"groups/GroupA/preview.png\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("(No raw image statistics available)")));
    EXPECT_FALSE(report.contains(QStringLiteral("Mean (8-bit)")));
}

TEST(WorkflowReportBuilder, StackedMastersIncludeCombinedRgbOutputWithRawStats)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir runDir(tempDir.path());

    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());
    const QString registrationKey = QStringLiteral("target=M83|registration=main");
    const QString rgbFilename = ImageIntegration::combinedRgbOutputFilename(registrationKey);
    const QString rgbPath = writeRgbMaster(runDir.filePath(rgbFilename), 16, 12);

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;
    manifest.combineRgbImages = true;

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult rgbGroup;
    rgbGroup.groupKey = ImageIntegration::combinedRgbGroupKey(registrationKey);
    rgbGroup.success = true;
    rgbGroup.outputPath = rgbPath;
    rgbGroup.outputFilename = rgbFilename;
    rgbGroup.outputBytes = QFileInfo(rgbPath).size();
    rgbGroup.lightFrameCount = 9;
    rgbGroup.registrationGroupKey = registrationKey;
    result.integrationGroupResults.push_back(rgbGroup);

    WorkflowReportBuilder builder(manifest, result, runDir.path(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Stacked Masters")));
    EXPECT_TRUE(report.contains(rgbFilename));
    EXPECT_TRUE(report.contains(QStringLiteral("- **Light Frames**: 9")));
    EXPECT_TRUE(report.contains(QStringLiteral("- **Registration Group**:")));
    EXPECT_TRUE(report.contains(QStringLiteral("target=M83")));
    EXPECT_TRUE(report.contains(QStringLiteral("registration=main")));
    EXPECT_TRUE(report.contains(QStringLiteral("- **Quality Snapshot**:")));
    EXPECT_TRUE(report.contains(QStringLiteral("Mean ")));
    EXPECT_TRUE(report.contains(QStringLiteral("BG Noise ")));
}

TEST(WorkflowReportBuilder, OutputStatisticsAreReadOnlyInTheSessionTheBuilderIsGiven)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir runDir(tempDir.path());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString master = writeNoisyMaster(runDir.filePath(QStringLiteral("stacked_GroupA.fit")), 32, 24);

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    integrationGroup.outputFilename = QStringLiteral("stacked_GroupA.fit");
    integrationGroup.resolution = QStringLiteral("32x24");
    manifest.integrationGroups.push_back(integrationGroup);

    WorkflowExecutor::Result result;
    result.success = true;
    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("GroupA");
    group.success = true;
    group.outputPath = master;
    group.outputFilename = integrationGroup.outputFilename;
    group.outputBytes = QFileInfo(master).size();
    group.lightFrameCount = 2;
    result.integrationGroupResults.push_back(group);

    const QString withoutSession = WorkflowReportBuilder(manifest, result, runDir.path()).build();
    EXPECT_FALSE(withoutSession.contains(QStringLiteral("- **Quality Snapshot**:")));

    const QString withSession =
        WorkflowReportBuilder(manifest, result, runDir.path(), QJsonObject(), nullptr, &*session).build();
    EXPECT_TRUE(withSession.contains(QStringLiteral("- **Quality Snapshot**:")));
    EXPECT_TRUE(withSession.contains(QStringLiteral("Mean ")));
    EXPECT_TRUE(withSession.contains(QStringLiteral("BG Noise ")));
}

// The report measures its images at the run's width, the one every stage operation is admitted at: the
// parallel-cores setting where it limits the run, held to the cores the host has, and otherwise no limit at all, so a
// run that asks for every core - or for more than there are - measures on as many as a batch operation may have.
TEST(WorkflowReportBuilder, MeasuresAtTheRunsParallelCoreWidth)
{
    const int detected = ImageIntegration::core::detectedParallelCoreCount();
    EXPECT_EQ(ImageIntegration::core::parallelCoreWidth(0), 0u);
    EXPECT_EQ(ImageIntegration::core::parallelCoreWidth(-4), 0u);
    EXPECT_EQ(ImageIntegration::core::parallelCoreWidth(detected), 0u);
    EXPECT_EQ(ImageIntegration::core::parallelCoreWidth(detected + 8), 0u);
    if (detected > 1)
    {
        EXPECT_EQ(ImageIntegration::core::parallelCoreWidth(1), 1u);
        EXPECT_EQ(ImageIntegration::core::parallelCoreWidth(detected - 1), static_cast<std::uint32_t>(detected - 1));
    }
}

TEST(WorkflowReportBuilder, SummaryShowsStatusAndMasterCounts)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;
    for (int i = 0; i < 3; ++i)
    {
        WorkflowManifest::IntegrationGroup g;
        g.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("Group_%1").arg(i + 1)}};
        manifest.integrationGroups.push_back(g);
    }

    WorkflowExecutor::Result result;
    result.success = false;
    result.warningCount = 2;
    result.errors = {QStringLiteral("Failure A"), QStringLiteral("Failure B")};

    IntegrationGroupResult okGroup;
    okGroup.groupKey = QStringLiteral("Group_A");
    okGroup.success = true;
    result.integrationGroupResults.push_back(okGroup);

    IntegrationGroupResult failedGroup;
    failedGroup.groupKey = QStringLiteral("Group_B");
    failedGroup.success = false;
    result.integrationGroupResults.push_back(failedGroup);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("Integration Groups (Total) | 3")));
    EXPECT_TRUE(report.contains(QStringLiteral("Regenerated This Run | 1")));
    EXPECT_TRUE(report.contains(QStringLiteral("Reused Existing Output | 1")));
    EXPECT_TRUE(report.contains(QStringLiteral("Failed Groups | 1")));
    EXPECT_TRUE(report.contains(QStringLiteral("Failed"))); // Status
    EXPECT_TRUE(report.contains(QStringLiteral("Warnings")));
    EXPECT_TRUE(report.contains(QStringLiteral("Errors")));
    EXPECT_FALSE(report.contains(QStringLiteral("Masters Produced")));
    // Old sections should not exist
    EXPECT_FALSE(report.contains(QStringLiteral("Quick Look")));
    EXPECT_FALSE(report.contains(QStringLiteral("Execution Summary")));
}

TEST(WorkflowReportBuilder, IncrementalGroupCountsStayConsistentAcrossSections)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.executionScope = QStringLiteral("full");

    for (const auto& filter : {QStringLiteral("Blue"), QStringLiteral("Green"), QStringLiteral("Ha"),
                               QStringLiteral("Luminance"), QStringLiteral("Red")})
    {
        WorkflowManifest::IntegrationGroup group;
        group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-%1|M 83|-").arg(filter)}};
        group.filter = filter;
        group.lightFiles = {FrameId(QStringLiteral("/tmp/%1_1.fit").arg(filter.toLower()))};
        manifest.integrationGroups.push_back(group);
        manifest.incrementalIntegrationGroups.push_back(group.groupKey);
    }

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowExecutor::Result::StageTiming stage;
    stage.stageName = QStringLiteral("LightsStacking");
    stage.status = QStringLiteral("completed");
    stage.durationMs = 60000;
    stage.groupsProcessed = 1; // Intentionally mismatched: log says 5
    result.stageResults.push_back(stage);

    IntegrationGroupResult regenerated;
    regenerated.groupKey = manifest.integrationGroups.front().groupKey.toString();
    regenerated.success = true;
    regenerated.outputFilename = QStringLiteral("stacked_blue.fit");
    regenerated.outputBytes = 1024;
    result.integrationGroupResults.push_back(regenerated);

    result.capturedLog =
        QStringLiteral("[2026-02-11T02:54:53.176] [HEADER]\n"
                       "═══ STAGE: Lights Stacking (5 groups) ═══\n"
                       "[2026-02-11T02:54:53.184] [DETAIL]     → Reprocessing: Settings or inputs changed.\n"
                       "[2026-02-11T02:55:54.989] [DETAIL]     → Skipping: Output up-to-date\n"
                       "[2026-02-11T02:55:55.007] [DETAIL]     → Skipping: Output up-to-date\n"
                       "[2026-02-11T02:55:55.030] [DETAIL]     → Skipping: Output up-to-date\n"
                       "[2026-02-11T02:55:55.051] [DETAIL]     → Skipping: Output up-to-date\n");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Integration Groups Updated")));
    EXPECT_TRUE(report.contains(QStringLiteral("5 / 5")));

    EXPECT_TRUE(report.contains(QStringLiteral("Integration Groups (Total) | 5")));
    EXPECT_TRUE(report.contains(QStringLiteral("Regenerated This Run | 1")));
    EXPECT_TRUE(report.contains(QStringLiteral("Reused Existing Output | 4")));
    EXPECT_TRUE(report.contains(QStringLiteral("Failed Groups | 0")));

    // Pipeline stage group count should align with scope/log semantics.
    EXPECT_TRUE(report.contains(QStringLiteral("| 1 | Lights Stacking | <span class=\"an-severity-pill "
                                               "an-severity-success\">✅ completed</span> | 1m 0s | 5 |")));

    // Execution log section should expose stage-group summary for consistency checks.
    EXPECT_TRUE(report.contains(QStringLiteral("Log Stage Groups")));
    EXPECT_TRUE(report.contains(QStringLiteral("Lights Stacking=5")));
}

TEST(WorkflowReportBuilder, ListsChangedGroupsInRunFocusForIncrementalRuns)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.incrementalIntegrationGroups.push_back(
        IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")}});
    manifest.incrementalIntegrationGroups.push_back(
        IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Red|M 83|-")}});

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Changed groups should appear in Run Focus (not Top Changes)
    EXPECT_TRUE(report.contains(QStringLiteral("Changed Integration Groups (2)")));
    // Friendly format
    EXPECT_TRUE(report.contains(QStringLiteral("Blue \u2014 M 83")));
    EXPECT_TRUE(report.contains(QStringLiteral("Red \u2014 M 83")));
    // Top Changes section should not exist
    EXPECT_FALSE(report.contains(QStringLiteral("## Top Changes Since Last Run")));
}

TEST(WorkflowReportBuilder, StackedMastersIncludeInlineSettings)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup groupA;
    groupA.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    groupA.stacking.stackMethod = QStringLiteral("Average");
    groupA.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit"))};
    manifest.integrationGroups.push_back(groupA);

    WorkflowManifest::IntegrationGroup groupB;
    groupB.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupB")}};
    groupB.stacking.stackMethod = QStringLiteral("Median");
    groupB.lightFiles = {FrameId(QStringLiteral("/tmp/light2.fit"))};
    manifest.integrationGroups.push_back(groupB);

    WorkflowExecutor::Result result;
    result.success = true;
    IntegrationGroupResult grA;
    grA.groupKey = QStringLiteral("GroupA");
    grA.success = true;
    grA.lightFrameCount = 1;
    result.integrationGroupResults.push_back(grA);
    IntegrationGroupResult grB;
    grB.groupKey = QStringLiteral("GroupB");
    grB.success = true;
    grB.lightFrameCount = 1;
    result.integrationGroupResults.push_back(grB);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Settings should appear inline in Stacked Masters
    EXPECT_TRUE(report.contains(QStringLiteral("Stacked Masters")));
    EXPECT_TRUE(report.contains(QStringLiteral("**Stacking**")));
    // Separate Settings Detail section should not exist
    EXPECT_FALSE(report.contains(QStringLiteral("Settings Detail")));
}

TEST(WorkflowReportBuilder, IncrementalRunShowsChangedGroupsInRunFocus)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.incrementalIntegrationGroups.push_back(
        IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")}});

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowExecutor::Result::StageTiming stage;
    stage.stageName = QStringLiteral("LightsNormalization");
    stage.status = QStringLiteral("completed");
    stage.durationMs = 1500;
    stage.groupsProcessed = 1;
    result.stageResults.push_back(stage);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Run Focus")));
    EXPECT_TRUE(report.contains(QStringLiteral("Changed Integration Groups")));
    EXPECT_TRUE(report.contains(QStringLiteral("Blue \u2014 M 83")));
    EXPECT_TRUE(report.contains(QStringLiteral("Pipeline Stages")));
    EXPECT_TRUE(report.contains(QStringLiteral("Lights Normalization")));
    // Top Changes removed
    EXPECT_FALSE(report.contains(QStringLiteral("Top Changes Since Last Run")));
}

TEST(WorkflowReportBuilder, IncrementalRunWithNoGeneratedMastersExplainsUpToDate)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.incrementalIntegrationGroups.push_back(
        IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")}});

    WorkflowExecutor::Result result;
    result.success = true;
    // No integrationGroupResults => no newly generated masters

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Stacked Masters")));
    EXPECT_TRUE(report.contains(QStringLiteral("No stacked masters were generated")));
    EXPECT_TRUE(report.contains(QStringLiteral("all integration groups are up to date")));
}

TEST(WorkflowReportBuilder, IntegrationTimeShownPerMasterWhenExposureAvailable)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup blueGroup;
    blueGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")}};
    blueGroup.exposure = QStringLiteral("90");
    blueGroup.lightFiles = {FrameId(QStringLiteral("/tmp/b1.fit")), FrameId(QStringLiteral("/tmp/b2.fit")),
                            FrameId(QStringLiteral("/tmp/b3.fit"))};
    blueGroup.outputFilename = QStringLiteral("stacked_blue.fit");
    manifest.integrationGroups.push_back(blueGroup);

    WorkflowManifest::IntegrationGroup lumGroup;
    lumGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Luminance|M 83|-")}};
    lumGroup.exposure = QStringLiteral("60");
    lumGroup.lightFiles = {FrameId(QStringLiteral("/tmp/l1.fit")), FrameId(QStringLiteral("/tmp/l2.fit"))};
    lumGroup.outputFilename = QStringLiteral("stacked_lum.fit");
    manifest.integrationGroups.push_back(lumGroup);

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 300000;

    IntegrationGroupResult blueResult;
    blueResult.groupKey = blueGroup.groupKey.toString();
    blueResult.success = true;
    blueResult.lightFrameCount = 3;
    blueResult.outputFilename = QStringLiteral("stacked_blue.fit");
    result.integrationGroupResults.push_back(blueResult);

    IntegrationGroupResult lumResult;
    lumResult.groupKey = lumGroup.groupKey.toString();
    lumResult.success = true;
    lumResult.lightFrameCount = 2;
    lumResult.outputFilename = QStringLiteral("stacked_lum.fit");
    result.integrationGroupResults.push_back(lumResult);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Per-master integration time: Blue 3×90s=270s=4m 30s, Lum 2×60s=120s=2m 0s
    EXPECT_TRUE(report.contains(QStringLiteral("Integration Time")));
    EXPECT_TRUE(report.contains(QStringLiteral("4m 30s")));
    EXPECT_TRUE(report.contains(QStringLiteral("2m 0s")));
    // Total integration time in Summary: 270+120=390s=6m 30s
    EXPECT_TRUE(report.contains(QStringLiteral("6m 30s")));
    // Input Files summary table
    EXPECT_TRUE(report.contains(QStringLiteral("90s")));
    EXPECT_TRUE(report.contains(QStringLiteral("60s")));
}

TEST(WorkflowReportBuilder, CalibrationMatchingShownPerMaster)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("HaGroup")}};
    group.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit")), FrameId(QStringLiteral("/tmp/light2.fit"))};
    group.outputFilename = QStringLiteral("stacked_ha.fit");
    manifest.integrationGroups.push_back(group);

    WorkflowManifest::LightsCalibrationParams cal1;
    cal1.lightPath = FrameId(QStringLiteral("/tmp/light1.fit"));
    cal1.biasPath = FrameId(QStringLiteral("/masters/master_bias.fit"));
    cal1.darkPath = FrameId(QStringLiteral("/masters/master_dark_90s.fit"));
    cal1.flatPath = FrameId(QStringLiteral("/masters/master_flat_ha.fit"));
    manifest.lightsCalibration.push_back(cal1);

    WorkflowManifest::LightsCalibrationParams cal2;
    cal2.lightPath = FrameId(QStringLiteral("/tmp/light2.fit"));
    cal2.biasPath = FrameId(QStringLiteral("/masters/master_bias.fit"));
    cal2.darkPath = FrameId(QStringLiteral("/masters/master_dark_90s.fit"));
    cal2.flatPath = FrameId(QStringLiteral("/masters/master_flat_ha.fit"));
    manifest.lightsCalibration.push_back(cal2);

    WorkflowExecutor::Result result;
    result.success = true;
    IntegrationGroupResult gr;
    gr.groupKey = QStringLiteral("HaGroup");
    gr.success = true;
    gr.lightFrameCount = 2;
    gr.outputFilename = QStringLiteral("stacked_ha.fit");
    result.integrationGroupResults.push_back(gr);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("**Calibration**")));
    EXPECT_TRUE(report.contains(QStringLiteral("Bias: master_bias.fit")));
    EXPECT_TRUE(report.contains(QStringLiteral("Dark: master_dark_90s.fit")));
    EXPECT_TRUE(report.contains(QStringLiteral("Flat: master_flat_ha.fit")));
}

TEST(WorkflowReportBuilder, FormatDurationHandlesHours)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group.exposure = QStringLiteral("120");
    // 100 frames × 120s = 12000s = 3h 20m 0s
    for (int i = 0; i < 100; ++i)
    {
        group.lightFiles.push_back(FrameId(QStringLiteral("/tmp/light_%1.fit").arg(i)));
    }
    group.outputFilename = QStringLiteral("stacked.fit");
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 500000;
    IntegrationGroupResult gr;
    gr.groupKey = QStringLiteral("GroupA");
    gr.success = true;
    gr.lightFrameCount = 100;
    gr.outputFilename = QStringLiteral("stacked.fit");
    result.integrationGroupResults.push_back(gr);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // 100×120s = 12000s = 3h 20m 0s
    EXPECT_TRUE(report.contains(QStringLiteral("3h 20m 0s")));
}

TEST(WorkflowReportBuilder, FriendlyGroupKeyHandlesResolutionOnlyKey)
{
    WorkflowManifest manifest;
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group biasGroup;
    biasGroup.groupKey = GroupKey{QStringLiteral("3008x3004")};
    biasGroup.inputFiles = {FrameId(QStringLiteral("/tmp/bias1.fit"))};
    biasGroup.outputFilename = QStringLiteral("master_bias.fit");
    biasStage.groups.push_back(biasGroup);
    manifest.biasStage = biasStage;

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Resolution-only key should use × instead of x
    EXPECT_TRUE(report.contains(QStringLiteral("3008 \u00D7 3004")));
    EXPECT_FALSE(report.contains(QStringLiteral("3008x3004")));
}

TEST(WorkflowReportBuilder, InputFilesSummaryOmitsExposureColumnWhenNotAvailable)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit"))};
    // No exposure set
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Input Files")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Group | Frames |")));
    EXPECT_FALSE(report.contains(QStringLiteral("| Group | Frames | Exposures | Integration Time |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| GroupA | 1 |")));
}
// ---------------------------------------------------------------------------
// Exposure resolution and Acquisition Summary tests
// ---------------------------------------------------------------------------

TEST(WorkflowReportBuilder, ExposureResolvedFromFilenameWhenFieldIsDash)
{
    // Simulates the real-world case where group.exposure is "-" but
    // the filenames contain the exposure, e.g. ..._60.00s_0001.fits
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Luminance|M 83|-")}};
    group.exposure = QStringLiteral("-"); // dash — can't be parsed as double
    group.filter = QStringLiteral("Luminance");
    group.object = QStringLiteral("M 83");
    group.lightFiles = {
        FrameId(QStringLiteral("/data/2025-04-29/LIGHT/2025-04-29_20-54-01_Luminance_-9.90_60.00s_0000.fits")),
        FrameId(QStringLiteral("/data/2025-04-29/LIGHT/2025-04-29_20-55-02_Luminance_-10.00_60.00s_0001.fits")),
        FrameId(QStringLiteral("/data/2025-04-29/LIGHT/2025-04-29_20-56-19_Luminance_-10.00_60.00s_0002.fits"))};
    group.outputFilename = QStringLiteral("stacked_lum.fit");
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 10000;

    IntegrationGroupResult gr;
    gr.groupKey = group.groupKey.toString();
    gr.success = true;
    gr.lightFrameCount = 3;
    gr.outputFilename = QStringLiteral("stacked_lum.fit");
    result.integrationGroupResults.push_back(gr);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Acquisition Summary should show 60s exposure resolved from filename
    EXPECT_TRUE(report.contains(QStringLiteral("Acquisition Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("filter-level exposure inventory")));
    EXPECT_TRUE(report.contains(QStringLiteral("60s")));
    // Integration time: 3 × 60s = 180s = 3m 0s
    EXPECT_TRUE(report.contains(QStringLiteral("3m 0s")));
    EXPECT_TRUE(report.contains(QStringLiteral("3 \u00D7 60s")));
    // Summary section should show total integration time
    EXPECT_TRUE(report.contains(QStringLiteral("Total Integration Time")));
    // Stacked Masters should also show integration time with × format
    EXPECT_TRUE(report.contains(QStringLiteral("**Integration Time**: 3m 0s (3 \u00D7 60s)")));
}

TEST(WorkflowReportBuilder, AcquisitionSummaryShowsCommaSeparatedExposures)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    // Two groups for the same filter but different exposures
    WorkflowManifest::IntegrationGroup group60;
    group60.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group60.exposure = QStringLiteral("60");
    group60.filter = QStringLiteral("Ha");
    group60.lightFiles = {FrameId(QStringLiteral("/tmp/ha1.fit")), FrameId(QStringLiteral("/tmp/ha2.fit"))};
    manifest.integrationGroups.push_back(group60);

    WorkflowManifest::IntegrationGroup group120;
    group120.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupB")}};
    group120.exposure = QStringLiteral("120");
    group120.filter = QStringLiteral("Ha");
    group120.lightFiles = {FrameId(QStringLiteral("/tmp/ha3.fit"))};
    manifest.integrationGroups.push_back(group120);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Acquisition Summary should show comma-separated exposures for Ha
    EXPECT_TRUE(report.contains(QStringLiteral("Acquisition Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("2 \u00D7 60s, 1 \u00D7 120s")));
    // Total integration: 2×60 + 1×120 = 240s = 4m 0s
    EXPECT_TRUE(report.contains(QStringLiteral("4m 0s")));
    // With multiple exposures, the (N × Xs) format should NOT appear
    // because the breakdown doesn't apply neatly to mixed exposures
    EXPECT_FALSE(report.contains(QStringLiteral("3 \u00D7 60s")));
    EXPECT_FALSE(report.contains(QStringLiteral("3 \u00D7 120s")));
}

TEST(WorkflowReportBuilder, MixedExposureSingleGroupUsesPerFrameExposureTotals)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupMixed")}};
    group.filter = QStringLiteral("L");
    group.exposure = QStringLiteral("-");
    group.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit")), FrameId(QStringLiteral("/tmp/light2.fit")),
                        FrameId(QStringLiteral("/tmp/light3.fit")), FrameId(QStringLiteral("/tmp/light4.fit")),
                        FrameId(QStringLiteral("/tmp/light5.fit"))};
    group.lightExposureSeconds = {60.0, 60.0, 90.0, 90.0, 90.0};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult gr;
    gr.groupKey = group.groupKey.toString();
    gr.success = true;
    gr.lightFrameCount = 5;
    gr.outputFilename = QStringLiteral("stacked_mixed.fit");
    result.integrationGroupResults.push_back(gr);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // 2×60 + 3×90 = 390s = 6m 30s
    EXPECT_TRUE(report.contains(QStringLiteral("Total Integration Time")));
    EXPECT_TRUE(report.contains(QStringLiteral("6m 30s")));
    EXPECT_TRUE(report.contains(QStringLiteral("2 \u00D7 60s, 3 \u00D7 90s")));
    EXPECT_TRUE(report.contains(QStringLiteral("**Integration Time**: 6m 30s (2 \u00D7 60s, 3 \u00D7 90s)")));
    EXPECT_TRUE(report.contains(QStringLiteral("| GroupMixed | 5 | 2 \u00D7 60s, 3 \u00D7 90s | 6m 30s |")));
}
// ---------------------------------------------------------------------------
// Execution Log section tests
// ---------------------------------------------------------------------------

TEST(WorkflowReportBuilder, ExecutionLogEmbeddedInCollapsibleSection)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;
    result.capturedLog = QStringLiteral("[2026-02-10T23:53:35.857] [HEADER] WORKFLOW STARTED\n"
                                        "[2026-02-10T23:53:35.857] [DETAIL]   \u2022 Output: /tmp/test\n"
                                        "[2026-02-10T23:53:35.858] [SUCCESS]   \u2713 Group 1/1 completed (0 ms)\n"
                                        "[2026-02-10T23:53:35.858] [WARNING] Something looks off\n"
                                        "[2026-02-10T23:53:35.858] [ERROR] Fatal failure\n");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Section header present
    EXPECT_TRUE(report.contains(QStringLiteral("Execution Log")));
    // Log is inside a collapsible <details> block
    EXPECT_TRUE(report.contains(QStringLiteral("<details>")));
    EXPECT_TRUE(report.contains(QStringLiteral("<summary>Full execution log")));
    EXPECT_TRUE(report.contains(QStringLiteral("</details>")));
    // Log viewer div present
    EXPECT_TRUE(report.contains(QStringLiteral("<div class=\"log-viewer\">")));
    // Timestamps wrapped in css class
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"log-ts\"")));
    // Log levels wrapped in their respective css classes
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"log-header\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"log-detail\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"log-success\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"log-warning\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"log-error\"")));
    // Actual log content present (HTML-escaped)
    EXPECT_TRUE(report.contains(QStringLiteral("WORKFLOW STARTED")));
    EXPECT_TRUE(report.contains(QStringLiteral("Fatal failure")));
}

TEST(WorkflowReportBuilder, ExecutionLogStageHeadersHighlighted)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.success = true;
    result.capturedLog = QStringLiteral("[2026-02-10T23:53:35.858] [HEADER]\n"
                                        "\u2550\u2550\u2550 STAGE: Bias Stacking (1 group) \u2550\u2550\u2550\n"
                                        "[2026-02-10T23:53:35.858] [SUCCESS] \u2550\u2550\u2550 Bias Stacking Complete "
                                        "\u2550\u2550\u2550 (1 group, 0 ms)\n");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Stage header lines (containing ═══) should use log-stage class
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"log-stage\"")));
}

TEST(WorkflowReportBuilder, ExecutionLogOmittedWhenEmpty)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.success = true;
    // No captured log
    result.capturedLog.clear();

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_FALSE(report.contains(QStringLiteral("Execution Log")));
    EXPECT_FALSE(report.contains(QStringLiteral("log-viewer")));
}

TEST(WorkflowReportBuilder, ExecutionLogHtmlEscapesContent)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.success = true;
    // Log line with characters that must be HTML-escaped
    result.capturedLog =
        QStringLiteral("[2026-02-10T23:53:35.857] [DETAIL] Path: C:\\Users\\test <script>alert(1)</script>\n");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Raw HTML must not pass through
    EXPECT_FALSE(report.contains(QStringLiteral("<script>")));
    // Should be escaped
    EXPECT_TRUE(report.contains(QStringLiteral("&lt;script&gt;")));
}
TEST(WorkflowReportBuilder, AcquisitionSingleExposureShowsExpandedFormat)
{
    // When all frames in a filter share one exposure, show N × Xs format
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group.exposure = QStringLiteral("90");
    group.filter = QStringLiteral("Blue");
    group.lightFiles = {FrameId(QStringLiteral("/tmp/b1.fit")), FrameId(QStringLiteral("/tmp/b2.fit")),
                        FrameId(QStringLiteral("/tmp/b3.fit")), FrameId(QStringLiteral("/tmp/b4.fit"))};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Acquisition Summary: 4×90s = 360s = 6m 0s
    EXPECT_TRUE(report.contains(QStringLiteral("Acquisition Summary")));
    EXPECT_TRUE(report.contains(QStringLiteral("90s")));
    EXPECT_TRUE(report.contains(QStringLiteral("6m 0s")));
    // Expanded format: 6m 0s (4 × 90s)
    EXPECT_TRUE(report.contains(QStringLiteral("6m 0s (4 \u00D7 90s)")));
    // The expanded exposure string appears in multiple sections (acquisition row/total, input files, stacked details).
    EXPECT_EQ(countOccurrences(report, QStringLiteral("4 \u00D7 90s")), 4);
}

TEST(WorkflowReportBuilder, AcquisitionSummarySkippedWhenNoExposureData)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    // No exposure field set, generic filenames — exposure cannot be resolved
    group.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit"))};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Acquisition Summary should NOT appear without exposure data
    EXPECT_FALSE(report.contains(QStringLiteral("## \U0001F4F7 Acquisition Summary")));
}

TEST(WorkflowReportBuilder, ExposureFractionalSecondsDisplayedCorrectly)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group.exposure = QStringLiteral("0.5");
    group.filter = QStringLiteral("Luminance");
    group.lightFiles = {FrameId(QStringLiteral("/tmp/l1.fit")), FrameId(QStringLiteral("/tmp/l2.fit"))};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // 0.5s is a fractional exposure — should display with decimal
    EXPECT_TRUE(report.contains(QStringLiteral("0.5s")));
    // 2 × 0.5s = 1s
    EXPECT_TRUE(report.contains(QStringLiteral("2 \u00D7 0.5s")));
}

TEST(WorkflowReportBuilder, MultiFilterDifferentExposuresTotalRowOmitsExpandedFormat)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup haGroup;
    haGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupHa")}};
    haGroup.exposure = QStringLiteral("300");
    haGroup.filter = QStringLiteral("Ha");
    haGroup.lightFiles = {FrameId(QStringLiteral("/tmp/ha1.fit")), FrameId(QStringLiteral("/tmp/ha2.fit"))};
    manifest.integrationGroups.push_back(haGroup);

    WorkflowManifest::IntegrationGroup lumGroup;
    lumGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupLum")}};
    lumGroup.exposure = QStringLiteral("60");
    lumGroup.filter = QStringLiteral("Luminance");
    lumGroup.lightFiles = {FrameId(QStringLiteral("/tmp/l1.fit")), FrameId(QStringLiteral("/tmp/l2.fit")),
                           FrameId(QStringLiteral("/tmp/l3.fit"))};
    manifest.integrationGroups.push_back(lumGroup);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    // Ha row: 10m 0s (2 × 300s), single exposure per filter
    EXPECT_TRUE(report.contains(QStringLiteral("2 \u00D7 300s")));
    // Lum row: 3m 0s (3 × 60s)
    EXPECT_TRUE(report.contains(QStringLiteral("3 \u00D7 60s")));
    // Total: 10m+3m = 13m 0s, but different exposures across filters
    // So the total row should NOT have an expanded (N × Xs) format
    EXPECT_TRUE(report.contains(QStringLiteral("13m 0s")));
    // The total row must not show "5 × 300s" or "5 × 60s"
    EXPECT_FALSE(report.contains(QStringLiteral("5 \u00D7 300s")));
    EXPECT_FALSE(report.contains(QStringLiteral("5 \u00D7 60s")));
}

TEST(WorkflowReportBuilder, HeroMastheadListsAllTargetsForMultiObjectRuns)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;
    manifest.executionId = QStringLiteral("exec-multi-001");

    WorkflowManifest::IntegrationGroup groupA;
    groupA.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M83|-")}};
    groupA.object = QStringLiteral("M83");
    groupA.lightFiles = {FrameId(QStringLiteral("/tmp/m83_1.fit"))};
    manifest.integrationGroups.push_back(groupA);

    WorkflowManifest::IntegrationGroup groupB;
    groupB.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Ha|NGC 5128|-")}};
    groupB.object = QStringLiteral("NGC 5128");
    groupB.lightFiles = {FrameId(QStringLiteral("/tmp/ngc5128_1.fit"))};
    manifest.integrationGroups.push_back(groupB);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("an-report-hero")));
    EXPECT_TRUE(report.contains(QStringLiteral("Targets: 2")));
    EXPECT_TRUE(report.contains(QStringLiteral("M83")));
    EXPECT_TRUE(report.contains(QStringLiteral("NGC 5128")));
}

TEST(WorkflowReportBuilder, IncludesProvenanceBlockFromContextMetadata)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.executionId = QStringLiteral("exec-ctx-001");
    manifest.version = 7;

    WorkflowExecutor::Result result;
    result.success = true;

    QJsonObject context;
    context.insert(QStringLiteral("schemaVersion"), 3);
    context.insert(QStringLiteral("executionId"), QStringLiteral("exec-ctx-001"));
    context.insert(QStringLiteral("appVersion"), QStringLiteral("9.9.9-test"));
    context.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2026-02-11T13:44:00.000"));
    context.insert(QStringLiteral("timeZoneId"), QStringLiteral("America/New_York"));

    WorkflowReportBuilder builder(manifest, result, QString(), context);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Report Provenance")));
    EXPECT_TRUE(report.contains(QStringLiteral("Report Index")));
    EXPECT_TRUE(report.contains(QStringLiteral("AfterNight Version | 9.9.9-test")));
    EXPECT_TRUE(report.contains(QStringLiteral("Execution ID | exec-ctx-001")));
    EXPECT_TRUE(report.contains(QStringLiteral("Report Archive Format Version | 3")));
    EXPECT_TRUE(report.contains(QStringLiteral("Workflow Manifest Version | 7")));
    EXPECT_TRUE(report.contains(QStringLiteral("Generated (Local) | 2026-02-11 13:44:00")));
    EXPECT_TRUE(report.contains(QStringLiteral("Timezone | America/New_York")));
    EXPECT_FALSE(report.contains(QStringLiteral("Toolchain")));
    EXPECT_FALSE(report.contains(QStringLiteral("Qt Runtime")));
}

TEST(WorkflowReportBuilder, VersionUsesEngineVersionWhenContextVersionMissing)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();
    const QString engineVersion = QString::fromUtf8(an::engineVersionString()).trimmed();

    ASSERT_FALSE(engineVersion.isEmpty());
    EXPECT_TRUE(report.contains(QStringLiteral("AfterNight Version | %1").arg(engineVersion)));
    EXPECT_FALSE(report.contains(QStringLiteral("AfterNight Version | Unknown")));
}

TEST(WorkflowReportBuilder, SessionNotesRenderNearTopAndAreHtmlEscaped)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QDir root(tempDir.path());
    ASSERT_TRUE(root.mkpath(QStringLiteral(".afternight/image_integration")));
    const QString notesPath = root.filePath(QStringLiteral(".afternight/image_integration/session_notes.txt"));
    QFile notesFile(notesPath);
    ASSERT_TRUE(notesFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    notesFile.write("Night looked excellent.\nAvoid <script>alert(1)</script> in notes.");
    notesFile.close();

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    const int notesPos = report.indexOf(QStringLiteral("Session Notes"));
    const int executivePos = report.indexOf(QStringLiteral("Executive Summary"));
    ASSERT_GE(notesPos, 0);
    ASSERT_GE(executivePos, 0);
    EXPECT_LT(notesPos, executivePos);
    EXPECT_TRUE(report.contains(QStringLiteral("Night looked excellent.")));
    EXPECT_TRUE(report.contains(QStringLiteral("&lt;script&gt;alert(1)&lt;/script&gt;")));
    EXPECT_FALSE(report.contains(QStringLiteral("<script>alert(1)</script>")));

    const QRegularExpression lastUpdatedRx(QStringLiteral("_Last updated: \\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}_"));
    EXPECT_TRUE(report.contains(lastUpdatedRx));
}

TEST(WorkflowReportBuilder, IndexLinksHaveMatchingAnchorsEvenWhenSectionsAreEmpty)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = "/tmp/test";
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 1000;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("[Failure Diagnostics](#failure-diagnostics)")));
    EXPECT_TRUE(report.contains(QStringLiteral("<a id=\"failure-diagnostics\"></a>")));
    EXPECT_TRUE(report.contains(QStringLiteral("## \U0001FA7A Failure Diagnostics")));
    EXPECT_TRUE(report.contains(QStringLiteral("No failures, partial failures, or errors")));

    EXPECT_TRUE(report.contains(QStringLiteral("[Warnings & Errors](#warnings-errors)")));
    EXPECT_TRUE(report.contains(QStringLiteral("<a id=\"warnings-errors\"></a>")));
    EXPECT_TRUE(report.contains(QStringLiteral("## \u26A0\uFE0F Warnings & Errors")));
    EXPECT_TRUE(report.contains(QStringLiteral("No warnings or errors were recorded")));
}

TEST(WorkflowReportBuilder, EquipmentUsedSectionShowsMetadataPerMasterGroup)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir runDir(tempDir.path());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString masterA =
        writeMasterWithEquipmentMetadata(runDir.filePath(QStringLiteral("stacked_m83.fit")), 3008, 3004,
                                         QStringLiteral("ASI2600MM"), QStringLiteral("Esprit 100"), false, 100, 50,
                                         QStringLiteral("34.0500"), QStringLiteral("-118.2500"), QStringLiteral("M83"));
    const QString masterB = writeMasterWithEquipmentMetadata(
        runDir.filePath(QStringLiteral("stacked_ngc5128.fit")), 4788, 3194, QStringLiteral("ASI6200MM"),
        QStringLiteral("C9.25 EdgeHD"), true, 800, 30, QStringLiteral("-33.9500"), QStringLiteral("151.1800"),
        QStringLiteral("NGC 5128"));

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup gA;
    gA.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|L|M83|-")}};
    gA.outputFilename = QStringLiteral("stacked_m83.fit");
    gA.object = QStringLiteral("M83");
    manifest.integrationGroups.push_back(gA);

    WorkflowManifest::IntegrationGroup gB;
    gB.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("9576x6388|-|-|-|Ha|NGC 5128|-")}};
    gB.outputFilename = QStringLiteral("stacked_ngc5128.fit");
    gB.object = QStringLiteral("NGC 5128");
    manifest.integrationGroups.push_back(gB);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult rA;
    rA.groupKey = gA.groupKey.toString();
    rA.success = true;
    rA.outputPath = masterA;
    rA.outputFilename = gA.outputFilename;
    rA.lightFrameCount = 10;
    result.integrationGroupResults.push_back(rA);

    IntegrationGroupResult rB;
    rB.groupKey = gB.groupKey.toString();
    rB.success = true;
    rB.outputPath = masterB;
    rB.outputFilename = gB.outputFilename;
    rB.lightFrameCount = 12;
    result.integrationGroupResults.push_back(rB);

    WorkflowReportBuilder builder(manifest, result, runDir.path(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Equipment Used")));
    EXPECT_TRUE(report.contains(QStringLiteral("[Equipment Used](#equipment-used)")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Integration Group | Target | Camera | Capture Resolution | Output "
                                               "Resolution | ISO/Gain | Offset | Telescope | Location (GPS) |")));
    EXPECT_TRUE(report.contains(QStringLiteral("Capture Resolution comes from grouping/capture metadata")));
    EXPECT_TRUE(report.contains(QStringLiteral("ASI2600MM")));
    EXPECT_TRUE(report.contains(QStringLiteral("ASI6200MM")));
    EXPECT_TRUE(report.contains(QStringLiteral("Esprit 100")));
    EXPECT_TRUE(report.contains(QStringLiteral("C9.25 EdgeHD")));
    EXPECT_TRUE(report.contains(QStringLiteral("Gain 100")));
    EXPECT_TRUE(report.contains(QStringLiteral("ISO 800")));
    EXPECT_TRUE(report.contains(QStringLiteral("| M83 | ASI2600MM |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| NGC 5128 | ASI6200MM |")));
    EXPECT_TRUE(report.contains(QStringLiteral("34.0500, -118.2500")));
    EXPECT_TRUE(report.contains(QStringLiteral("-33.9500, 151.1800")));
    EXPECT_TRUE(report.contains(QStringLiteral("3008 \u00D7 3004")));
    EXPECT_TRUE(report.contains(QStringLiteral("9576 \u00D7 6388")));
    EXPECT_TRUE(report.contains(QStringLiteral("4788 \u00D7 3194")));
}

TEST(WorkflowReportBuilder, EquipmentUsedSectionDeduplicatesIdenticalRows)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir runDir(tempDir.path());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString masterA =
        writeMasterWithEquipmentMetadata(runDir.filePath(QStringLiteral("stacked_m83_a.fit")), 3008, 3004,
                                         QStringLiteral("ASI2600MM"), QStringLiteral("Esprit 100"), false, 100, 50,
                                         QStringLiteral("34.0500"), QStringLiteral("-118.2500"), QStringLiteral("M83"));
    const QString masterB =
        writeMasterWithEquipmentMetadata(runDir.filePath(QStringLiteral("stacked_m83_b.fit")), 3008, 3004,
                                         QStringLiteral("ASI2600MM"), QStringLiteral("Esprit 100"), false, 100, 50,
                                         QStringLiteral("34.0500"), QStringLiteral("-118.2500"), QStringLiteral("M83"));

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup gA;
    gA.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    gA.outputFilename = QStringLiteral("stacked_m83_a.fit");
    gA.object = QStringLiteral("M83");
    gA.resolution = QStringLiteral("3008x3004");
    manifest.integrationGroups.push_back(gA);

    WorkflowManifest::IntegrationGroup gB;
    gB.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupB")}};
    gB.outputFilename = QStringLiteral("stacked_m83_b.fit");
    gB.object = QStringLiteral("M83");
    gB.resolution = QStringLiteral("3008x3004");
    manifest.integrationGroups.push_back(gB);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult rA;
    rA.groupKey = gA.groupKey.toString();
    rA.success = true;
    rA.outputPath = masterA;
    rA.outputFilename = gA.outputFilename;
    result.integrationGroupResults.push_back(rA);

    IntegrationGroupResult rB;
    rB.groupKey = gB.groupKey.toString();
    rB.success = true;
    rB.outputPath = masterB;
    rB.outputFilename = gB.outputFilename;
    result.integrationGroupResults.push_back(rB);

    WorkflowReportBuilder builder(manifest, result, runDir.path(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("| GroupA, GroupB | M83 | ASI2600MM |")));
}

TEST(WorkflowReportBuilder, SnrProxyFormattingUsesTwoDecimals)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir runDir(tempDir.path());

    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());
    const QString master = writeNoisyMaster(runDir.filePath(QStringLiteral("stacked_noisy.fit")), 64, 64);

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupNoisy")}};
    group.outputFilename = QStringLiteral("stacked_noisy.fit");
    group.object = QStringLiteral("M83");
    group.resolution = QStringLiteral("64x64");
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult gr;
    gr.groupKey = group.groupKey.toString();
    gr.success = true;
    gr.outputPath = master;
    gr.outputFilename = group.outputFilename;
    result.integrationGroupResults.push_back(gr);

    WorkflowReportBuilder builder(manifest, result, runDir.path(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    const QRegularExpression snrRx(QStringLiteral("<li><strong>SNR proxy</strong>: \\d+\\.\\d{2}</li>"));
    EXPECT_TRUE(report.contains(snrRx));
}

TEST(WorkflowReportBuilder, FullProcessingParametersRegistrationSettingsAreSharedNotRepeatedPerGroup)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");

    const QString regKey = QStringLiteral("RegGroupX");
    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{regKey};
    regGroup.settings.radius = 7;
    regGroup.settings.threshold = 1.25;
    manifest.registrationGroups.push_back(regGroup);

    for (int i = 0; i < 3; ++i)
    {
        WorkflowManifest::IntegrationGroup group;
        group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("Group_%1").arg(i)}};
        group.registrationGroupKey = GroupKey{regKey};
        group.stacking.normalizationMode = NormalizationMode::None;
        manifest.integrationGroups.push_back(group);
    }

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("### Registration (Shared)")));
    EXPECT_EQ(countOccurrences(report, QStringLiteral("Detection Radius")), 1);
}

TEST(WorkflowReportBuilder, QualityGateShowsPassWarnFailStatuses)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup groupKnown;
    groupKnown.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupKnown")}};
    groupKnown.object = QStringLiteral("M83");
    groupKnown.filter = QStringLiteral("L");
    groupKnown.exposure = QStringLiteral("60");
    groupKnown.lightFiles = {FrameId(QStringLiteral("/tmp/known_1.fit")), FrameId(QStringLiteral("/tmp/known_2.fit")),
                             FrameId(QStringLiteral("/tmp/known_3.fit")), FrameId(QStringLiteral("/tmp/known_4.fit"))};
    manifest.integrationGroups.push_back(groupKnown);

    WorkflowManifest::IntegrationGroup groupUnknown;
    groupUnknown.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupUnknown")}};
    groupUnknown.object = QStringLiteral("M83");
    groupUnknown.filter = QStringLiteral("Ha");
    groupUnknown.lightFiles = {FrameId(QStringLiteral("/tmp/unknown_1.fit"))};
    manifest.integrationGroups.push_back(groupUnknown);

    WorkflowExecutor::Result result;
    result.success = false;
    result.errors.append(QStringLiteral("Fatal integration failure"));
    result.capturedLog =
        QStringLiteral("[2026-02-11T02:54:52.837] [DETAIL] Inputs: Lights: 100 | Effective: 90 | Registered: 85\n");

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("## \U0001F9ED Quality Gate")));
    EXPECT_TRUE(report.contains(QStringLiteral("Interpretation: PASS/WARN/FAIL thresholds are heuristic defaults")));
    EXPECT_TRUE(report.contains(QStringLiteral("Usable Frames (Registered/Lights)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Registration Success (Registered/Effective)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Exposure Metadata Completeness")));
    EXPECT_TRUE(report.contains(QStringLiteral("Fatal Errors")));
    EXPECT_TRUE(report.contains(QStringLiteral("<span class=\"an-quality-badge an-quality-pass\">PASS</span>")));
    EXPECT_TRUE(report.contains(QStringLiteral("<span class=\"an-quality-badge an-quality-warn\">WARN</span>")));
    EXPECT_TRUE(report.contains(QStringLiteral("<span class=\"an-quality-badge an-quality-fail\">FAIL</span>")));
    EXPECT_TRUE(report.contains(QStringLiteral("85.0% (85/100)")));
    EXPECT_TRUE(report.contains(QStringLiteral("94.4% (85/90)")));
    EXPECT_TRUE(report.contains(QStringLiteral("80.0% (4/5)")));
}

TEST(WorkflowReportBuilder, MultiTargetRunsIncludeTargetSummaryCards)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup m83Group;
    m83Group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("Group_M83")}};
    m83Group.object = QStringLiteral("M83");
    m83Group.filter = QStringLiteral("Blue");
    m83Group.exposure = QStringLiteral("60");
    m83Group.outputFilename = QStringLiteral("stacked_m83.fit");
    m83Group.lightFiles = {FrameId(QStringLiteral("/tmp/m83_1.fit")), FrameId(QStringLiteral("/tmp/m83_2.fit")),
                           FrameId(QStringLiteral("/tmp/m83_3.fit"))};
    manifest.integrationGroups.push_back(m83Group);

    WorkflowManifest::IntegrationGroup ngcGroup;
    ngcGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("Group_NGC5128")}};
    ngcGroup.object = QStringLiteral("NGC 5128");
    ngcGroup.filter = QStringLiteral("Ha");
    ngcGroup.exposure = QStringLiteral("180");
    ngcGroup.outputFilename = QStringLiteral("stacked_ngc5128.fit");
    ngcGroup.lightFiles = {FrameId(QStringLiteral("/tmp/ngc5128_1.fit")),
                           FrameId(QStringLiteral("/tmp/ngc5128_2.fit"))};
    manifest.integrationGroups.push_back(ngcGroup);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult m83Result;
    m83Result.groupKey = m83Group.groupKey.toString();
    m83Result.success = true;
    m83Result.outputFilename = m83Group.outputFilename;
    m83Result.lightFrameCount = 3;
    result.integrationGroupResults.push_back(m83Result);

    IntegrationGroupResult ngcResult;
    ngcResult.groupKey = ngcGroup.groupKey.toString();
    ngcResult.success = true;
    ngcResult.outputFilename = ngcGroup.outputFilename;
    ngcResult.lightFrameCount = 2;
    result.integrationGroupResults.push_back(ngcResult);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("## \U0001F3AF Target Summaries")));
    EXPECT_TRUE(report.contains(QStringLiteral("SNR proxy is mean/background-noise from unstretched masters")));
    EXPECT_TRUE(report.contains(QStringLiteral("<div class=\"an-target-summary-grid\">")));
    EXPECT_EQ(countOccurrences(report, QStringLiteral("class=\"an-target-summary-card\"")), 2);
    EXPECT_TRUE(report.contains(QStringLiteral("M83")));
    EXPECT_TRUE(report.contains(QStringLiteral("NGC 5128")));
    EXPECT_TRUE(report.contains(QStringLiteral("Filters")));
    EXPECT_TRUE(report.contains(QStringLiteral("SNR proxy")));
    EXPECT_TRUE(report.contains(QStringLiteral("Overlap retention")));
    EXPECT_TRUE(report.contains(QStringLiteral("stacked_m83.fit")));
    EXPECT_TRUE(report.contains(QStringLiteral("stacked_ngc5128.fit")));
}

TEST(WorkflowReportBuilder, ReproducibilityFingerprintIsPresentAndDeterministic)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;
    manifest.executionId = QStringLiteral("exec-fingerprint-01");

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group.object = QStringLiteral("M83");
    group.filter = QStringLiteral("L");
    group.exposure = QStringLiteral("120");
    group.lightFiles = {FrameId(QStringLiteral("/tmp/light1.fit")), FrameId(QStringLiteral("/tmp/light2.fit"))};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    QJsonObject context;
    context.insert(QStringLiteral("appVersion"), QStringLiteral("1.2.3-test"));
    context.insert(QStringLiteral("executionId"), manifest.executionId);
    context.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2026-02-11T12:34:56.789"));

    WorkflowReportBuilder builderA(manifest, result, QString(), context);
    const QString reportA = builderA.build();
    WorkflowReportBuilder builderB(manifest, result, QString(), context);
    const QString reportB = builderB.build();

    const QRegularExpression fpRx(QStringLiteral("\\| Reproducibility Fingerprint \\| `([0-9a-f]{64})` \\|"));
    const auto matchA = fpRx.match(reportA);
    const auto matchB = fpRx.match(reportB);

    ASSERT_TRUE(matchA.hasMatch());
    ASSERT_TRUE(matchB.hasMatch());
    const QString fingerprintA = matchA.captured(1);
    const QString fingerprintB = matchB.captured(1);

    EXPECT_FALSE(fingerprintA.isEmpty());
    EXPECT_EQ(fingerprintA, fingerprintB);
    EXPECT_TRUE(reportA.contains(QStringLiteral("## \U0001F9EA Reproducibility Fingerprint")));
    EXPECT_TRUE(reportA.contains(QStringLiteral("Manifest/Settings Fingerprint")));
    EXPECT_TRUE(reportA.contains(QStringLiteral("Input Fingerprint")));
    EXPECT_TRUE(reportA.contains(QStringLiteral("| AfterNight Version | 1.2.3-test |")));
}

TEST(WorkflowReportBuilder, ShowsComparedToPreviousRunDeltasWhenHistoryHasSuccessfulRun)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QDir outputRoot(tempDir.path());
    ASSERT_TRUE(outputRoot.mkpath(QStringLiteral("history/20260210_010000_prev")));
    ASSERT_TRUE(outputRoot.mkpath(QStringLiteral("history/20260211_010000_current")));

    WorkflowManifest previousManifest;
    previousManifest.outputDirectory = outputRoot.path();
    previousManifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::IntegrationGroup prevGroup;
    prevGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("PrevGroup")}};
    prevGroup.exposure = QStringLiteral("60");
    prevGroup.lightFiles = {FrameId(QStringLiteral("/tmp/prev_light_1.fit")),
                            FrameId(QStringLiteral("/tmp/prev_light_2.fit"))};
    previousManifest.integrationGroups.push_back(prevGroup);

    QJsonObject previousRoot;
    previousRoot.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
    previousRoot.insert(QStringLiteral("manifest"), previousManifest.toJson());
    QJsonArray previousGroupResults;
    QJsonObject prevResult;
    prevResult.insert(QStringLiteral("groupKey"), QStringLiteral("PrevGroup"));
    prevResult.insert(QStringLiteral("result"), QStringLiteral("success"));
    prevResult.insert(QStringLiteral("outputBytes"), 2000);
    previousGroupResults.append(prevResult);
    previousRoot.insert(QStringLiteral("integrationGroupResults"), previousGroupResults);

    QFile previousManifestFile(
        outputRoot.filePath(QStringLiteral("history/20260210_010000_prev/workflow_manifest.json")));
    ASSERT_TRUE(previousManifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    previousManifestFile.write(QJsonDocument(previousRoot).toJson(QJsonDocument::Indented));
    previousManifestFile.close();

    WorkflowManifest currentManifest;
    currentManifest.outputDirectory = outputRoot.path();
    currentManifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::IntegrationGroup currentGroup;
    currentGroup.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("CurrentGroup")}};
    currentGroup.exposure = QStringLiteral("60");
    currentGroup.lightFiles = {FrameId(QStringLiteral("/tmp/current_light_1.fit")),
                               FrameId(QStringLiteral("/tmp/current_light_2.fit")),
                               FrameId(QStringLiteral("/tmp/current_light_3.fit"))};
    currentManifest.integrationGroups.push_back(currentGroup);

    WorkflowExecutor::Result currentResult;
    currentResult.success = true;
    IntegrationGroupResult currentGroupResult;
    currentGroupResult.groupKey = QStringLiteral("CurrentGroup");
    currentGroupResult.success = true;
    currentGroupResult.outputBytes = 3000;
    currentResult.integrationGroupResults.push_back(currentGroupResult);

    WorkflowReportBuilder builder(currentManifest, currentResult,
                                  outputRoot.filePath(QStringLiteral("history/20260211_010000_current")));
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Compared to Previous Successful Run")));
    EXPECT_TRUE(report.contains(QStringLiteral("Deltas are Current minus Previous")));
    EXPECT_TRUE(report.contains(QStringLiteral("Light Frames")));
    EXPECT_TRUE(report.contains(QStringLiteral("Integration Time")));
    EXPECT_TRUE(report.contains(QStringLiteral("Output Size")));
    EXPECT_TRUE(report.contains(QStringLiteral("Quality Metrics (SNR Proxy)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Quality Metrics (Overlap Retention)")));
    EXPECT_TRUE(report.contains(QStringLiteral("+1")));
    EXPECT_TRUE(report.contains(QStringLiteral("+1m 0s")));
}

TEST(WorkflowReportBuilder, FailureDiagnosticsBlockAppearsForFailedRuns)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowExecutor::Result result;
    result.success = false;
    result.partialSuccess = false;
    result.errors.append(QStringLiteral("File not found: /tmp/missing.fit"));

    WorkflowExecutor::Result::StageTiming stage;
    stage.stageName = QStringLiteral("LightsStacking");
    stage.status = QStringLiteral("failed");
    result.stageResults.push_back(stage);

    IntegrationGroupResult failedGroup;
    failedGroup.groupKey = QStringLiteral("GroupFailed");
    failedGroup.success = false;
    result.integrationGroupResults.push_back(failedGroup);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Failure Diagnostics")));
    EXPECT_TRUE(report.contains(QStringLiteral("Likely Cause Category")));
    EXPECT_TRUE(report.contains(QStringLiteral("I/O or file access")));
    EXPECT_TRUE(report.contains(QStringLiteral("Lights Stacking")));
    EXPECT_TRUE(report.contains(QStringLiteral("GroupFailed")));
    EXPECT_TRUE(report.contains(QStringLiteral("[Pipeline Stages](#pipeline-stages)")));
    EXPECT_TRUE(report.contains(QStringLiteral("[Warnings & Errors](#warnings-errors)")));
    EXPECT_TRUE(report.contains(QStringLiteral("[Technical Appendix](#technical-appendix)")));
}

TEST(WorkflowReportBuilder, MetadataCompletenessScoreSummarizesKeyFields)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir root(tempDir.path());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString light1 = writeLightFrameWithMetadata(root.filePath(QStringLiteral("light_1.fit")), 120.0, 800, true,
                                                       25, QStringLiteral("M83"), QStringLiteral("Esprit 100"),
                                                       QStringLiteral("34.0500"), QStringLiteral("-118.2500"));
    const QString light2 =
        writeLightFrameWithMetadata(root.filePath(QStringLiteral("light_2.fit")), 120.0, std::nullopt, true,
                                    std::nullopt, QStringLiteral("M83"), QString(), QString(), QString());

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupMeta")}};
    group.lightFiles = {FrameId(light1), FrameId(light2)};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result, QString(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Metadata Completeness Score")));
    EXPECT_TRUE(
        report.contains(QStringLiteral("metadata/header quality (provenance completeness), not image quality")));
    EXPECT_TRUE(report.contains(QStringLiteral("EXPTIME")));
    EXPECT_TRUE(report.contains(QStringLiteral("ISO/Gain")));
    EXPECT_TRUE(report.contains(QStringLiteral("Offset")));
    EXPECT_TRUE(report.contains(QStringLiteral("OBJECT")));
    EXPECT_TRUE(report.contains(QStringLiteral("TELESCOP")));
    EXPECT_TRUE(report.contains(QStringLiteral("Site Coordinates")));
    EXPECT_TRUE(report.contains(QStringLiteral("100.0% (2/2)")));
    EXPECT_TRUE(report.contains(QStringLiteral("50.0% (1/2)")));
}

TEST(WorkflowReportBuilder, FrameQualityWeightingSectionShowsKpisAndHistograms)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QDir root(tempDir.path());

    const QString groupKey = QStringLiteral("RegGroupA");
    const QString f1 = root.filePath(QStringLiteral("light_1.fit"));
    const QString f2 = root.filePath(QStringLiteral("light_2.fit"));
    const QString f3 = root.filePath(QStringLiteral("light_3.fit"));

    QList<QJsonObject> rows;
    rows.append(QJsonObject{{QStringLiteral("frame_id"), f1},
                            {QStringLiteral("stars"), 800},
                            {QStringLiteral("fwhm"), 2.2},
                            {QStringLiteral("snr"), 28.0},
                            {QStringLiteral("eccentricity"), 0.42},
                            {QStringLiteral("photometric_zero_point"), 24.8},
                            {QStringLiteral("astrometric_rms_arcsec"), 0.42},
                            {QStringLiteral("catalog_matched_stars"), 140},
                            {QStringLiteral("succeeded"), true},
                            {QStringLiteral("output_path"), QStringLiteral("registered_1.fit")}});
    rows.append(QJsonObject{{QStringLiteral("frame_id"), f2},
                            {QStringLiteral("stars"), 600},
                            {QStringLiteral("fwhm"), 3.5},
                            {QStringLiteral("snr"), 15.0},
                            {QStringLiteral("eccentricity"), 0.55},
                            {QStringLiteral("photometric_zero_point"), 23.9},
                            {QStringLiteral("astrometric_rms_arcsec"), 0.95},
                            {QStringLiteral("catalog_matched_stars"), 90},
                            {QStringLiteral("succeeded"), true},
                            {QStringLiteral("output_path"), QStringLiteral("registered_2.fit")}});
    rows.append(QJsonObject{{QStringLiteral("frame_id"), f3},
                            {QStringLiteral("stars"), 100},
                            {QStringLiteral("fwhm"), 6.0},
                            {QStringLiteral("snr"), 4.0},
                            {QStringLiteral("eccentricity"), 0.78},
                            {QStringLiteral("photometric_zero_point"), 21.0},
                            {QStringLiteral("astrometric_rms_arcsec"), 2.5},
                            {QStringLiteral("catalog_matched_stars"), 20},
                            {QStringLiteral("succeeded"), false},
                            {QStringLiteral("output_path"), QStringLiteral("registered_3.fit")}});

    ASSERT_TRUE(
        writeRegistrationMetricsDb(root.filePath(QStringLiteral("registration_metrics.db")), groupKey, 1, rows));

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{groupKey};
    regGroup.lightFiles = {FrameId(f1), FrameId(f2), FrameId(f3)};
    manifest.registrationGroups.push_back(regGroup);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupA")}};
    group.registrationGroupKey = GroupKey{groupKey};
    group.filter = QStringLiteral("L");
    group.lightFiles = {FrameId(f1), FrameId(f2), FrameId(f3)};
    group.lightExposureSeconds = {60.0, 90.0, 120.0};
    group.stacking.weighting = QStringLiteral("Weighted FWHM");
    group.stacking.minWeightFloor = 0.05;
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Frame Quality & Weighting")));
    EXPECT_TRUE(report.contains(QStringLiteral("[Frame Quality & Weighting](#frame-quality-weighting)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Weight Distribution")));
    EXPECT_TRUE(report.contains(QStringLiteral("Quality Score Distribution")));
    EXPECT_TRUE(report.contains(QStringLiteral("Weighted Integration Time")));
    EXPECT_TRUE(report.contains(QStringLiteral("Weighting Mode")));
    EXPECT_TRUE(report.contains(QStringLiteral("Weighted FWHM")));
    EXPECT_TRUE(report.contains(QStringLiteral("Median Zero Point")));
    EXPECT_TRUE(report.contains(QStringLiteral("Median Astrometric RMS")));
    EXPECT_TRUE(report.contains(QStringLiteral("Median Catalog Matches")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-weighting-kpi-grid")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-quality-hist-bar")));
    EXPECT_TRUE(report.contains(QStringLiteral("Per-Group Histograms & Top Contributors")));
    EXPECT_TRUE(report.contains(QStringLiteral("Top contributing frames (Pareto by weighted exposure)")));
    EXPECT_TRUE(report.contains(QStringLiteral("`light_1.fit`")));
    EXPECT_TRUE(report.contains(QStringLiteral("`light_2.fit`")));

    const int registrationPos = report.indexOf(QStringLiteral("## \U0001F4CD Registration Summary"));
    const int qualityGatePos = report.indexOf(QStringLiteral("## \U0001F9ED Quality Gate"));
    const int frameQualityPos = report.indexOf(QStringLiteral("## \U0001F4C9 Frame Quality & Weighting"));
    EXPECT_GE(registrationPos, 0);
    EXPECT_GT(qualityGatePos, registrationPos);
    EXPECT_GT(frameQualityPos, qualityGatePos);
}

TEST(WorkflowReportBuilder, PhotometricWeightingUsesSharedHelperAndOmitsFloorDiagnostic)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString groupKey = QStringLiteral("reg|photometric");
    const QString f1 = writeLightFrameWithMetadata(root.filePath(QStringLiteral("light_p1.fit")), 60.0, 100, true, 10,
                                                   QStringLiteral("M42"), QStringLiteral("Telescope"),
                                                   QStringLiteral("-30"), QStringLiteral("-64"));
    const QString f2 = writeLightFrameWithMetadata(root.filePath(QStringLiteral("light_p2.fit")), 60.0, 100, true, 10,
                                                   QStringLiteral("M42"), QStringLiteral("Telescope"),
                                                   QStringLiteral("-30"), QStringLiteral("-64"));

    QList<QJsonObject> rows;
    rows.append(QJsonObject{{QStringLiteral("frame_id"), f1},
                            {QStringLiteral("stars"), 900},
                            {QStringLiteral("fwhm"), 2.3},
                            {QStringLiteral("snr"), 28.0},
                            {QStringLiteral("eccentricity"), 0.12},
                            {QStringLiteral("photometric_zero_point"), 24.0},
                            {QStringLiteral("astrometric_rms_pixels"), 1.0},
                            {QStringLiteral("astrometric_rms_arcsec"), 0.8},
                            {QStringLiteral("catalog_matched_stars"), 50},
                            {QStringLiteral("succeeded"), true},
                            {QStringLiteral("output_path"), QStringLiteral("registered_p1.fit")}});
    rows.append(QJsonObject{{QStringLiteral("frame_id"), f2},
                            {QStringLiteral("stars"), 950},
                            {QStringLiteral("fwhm"), 2.1},
                            {QStringLiteral("snr"), 29.0},
                            {QStringLiteral("eccentricity"), 0.11},
                            {QStringLiteral("photometric_zero_point"), 24.8},
                            {QStringLiteral("astrometric_rms_pixels"), 1.0},
                            {QStringLiteral("astrometric_rms_arcsec"), 0.8},
                            {QStringLiteral("catalog_matched_stars"), 55},
                            {QStringLiteral("succeeded"), true},
                            {QStringLiteral("output_path"), QStringLiteral("registered_p2.fit")}});

    ASSERT_TRUE(
        writeRegistrationMetricsDb(root.filePath(QStringLiteral("registration_metrics.db")), groupKey, 1, rows));

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{groupKey};
    regGroup.lightFiles = {FrameId(f1), FrameId(f2)};
    manifest.registrationGroups.push_back(regGroup);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("PhotometricGroup")}};
    group.registrationGroupKey = GroupKey{groupKey};
    group.filter = QStringLiteral("L");
    group.lightFiles = {FrameId(f1), FrameId(f2)};
    group.lightExposureSeconds = {60.0, 60.0};
    group.stacking.weighting = QStringLiteral("Photometric");
    group.stacking.minWeightFloor = 0.05;
    manifest.integrationGroups.push_back(group);

    // The engine's model with its default penalties, floored at the group's setting.
    const std::vector<an::PhotometricFrameMeasurement> measurements = {
        {.zeroPointMagnitudes = 24.0,
         .zeroPointStandardUncertaintyMagnitudes = std::nullopt,
         .astrometricRootMeanSquarePixels = 1.0},
        {.zeroPointMagnitudes = 24.8,
         .zeroPointStandardUncertaintyMagnitudes = std::nullopt,
         .astrometricRootMeanSquarePixels = 1.0}};
    an::PhotometricWeightingRequest request;
    request.minimumRawWeight = 0.05;
    std::vector<an::PhotometricStackWeight> expected(measurements.size());
    {
        an::Result<an::consumer::OperationScope> scope = session->admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        const auto weighted = an::computePhotometricStackWeights(scope->execution(), measurements, request, expected);
        ASSERT_TRUE(weighted.hasValue()) << weighted.status().message();
    }

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result, QString(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Photometric")));
    EXPECT_TRUE(report.contains(QString::number(expected[1].value, 'f', 3)));
    EXPECT_FALSE(report.contains(QStringLiteral("minimum-weight floor")));
    EXPECT_FALSE(report.contains(QStringLiteral("0.0 to 1.0")));
}

// "Number of images" weighs each light by how many frames its header says it combines, so the report's weights are
// those counts, relative to the most, read in the session the builder is given. The report gave every light one.
TEST(WorkflowReportBuilder, NumberOfImagesWeightsAreEachLightsCombinedFrameCount)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const auto writeMaster = [&root](const QString& name, const char* frames) {
        const QString path = root.filePath(name);
        EXPECT_TRUE(
            writeFloatImage(path, 16, 10, FixtureChannels::Mono, 0.2, {},
                            {FixtureCard{"NCOMBINE", frames, "", an::tests::fixtures::FixtureCardKind::Integer}}));
        return path;
    };
    const QString f1 = writeMaster(QStringLiteral("master_n1.fit"), "7");
    const QString f2 = writeMaster(QStringLiteral("master_n2.fit"), "40");
    const QString groupKey = QStringLiteral("reg|counts");
    QList<QJsonObject> rows;
    for (const QString& frame : {f1, f2})
    {
        rows.append(QJsonObject{{QStringLiteral("frame_id"), frame},
                                {QStringLiteral("stars"), 900},
                                {QStringLiteral("fwhm"), 2.3},
                                {QStringLiteral("snr"), 28.0},
                                {QStringLiteral("eccentricity"), 0.12},
                                {QStringLiteral("succeeded"), true},
                                {QStringLiteral("output_path"), QStringLiteral("registered.fit")}});
    }
    ASSERT_TRUE(
        writeRegistrationMetricsDb(root.filePath(QStringLiteral("registration_metrics.db")), groupKey, 1, rows));

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{groupKey};
    regGroup.lightFiles = {FrameId(f1), FrameId(f2)};
    manifest.registrationGroups.push_back(regGroup);
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("CountsGroup")}};
    group.registrationGroupKey = GroupKey{groupKey};
    group.filter = QStringLiteral("L");
    group.lightFiles = {FrameId(f1), FrameId(f2)};
    group.lightExposureSeconds = {60.0, 60.0};
    group.stacking.weighting = QStringLiteral("Number of images");
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    WorkflowReportBuilder builder(manifest, result, QString(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();
    EXPECT_TRUE(report.contains(QStringLiteral("Number of images")));
    EXPECT_TRUE(report.contains(QString::number(7.0 / 40.0, 'f', 3))) << "the lighter master's weight is not shown";
}

namespace
{
/// A light's row in a group's top contributors: its name and its weight as the report prints it.
QString contributorRow(const QString& path, const double weight)
{
    return QStringLiteral("| `%1` | <span class=\"an-num\">%2</span> |")
        .arg(QFileInfo(path).fileName(), QString::number(weight, 'f', 3));
}
} // namespace

// The report's width weights are the stack's: a light's width is penalized for the stars it lacks against the group's
// reference before the inverse square ranks it, each weight shown relative to the heaviest, and the floor is counted
// where the stack raised a light to it. The report ranked the raw widths and showed the hazy light the heaviest.
TEST(WorkflowReportBuilder, WeightedFwhmWeightsAreTheStacksWithItsStarCountPenalty)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString groupKey = QStringLiteral("reg|widths");
    const QStringList lights{QDir(root.path()).filePath(QStringLiteral("fwhm_reference.fit")),
                             QDir(root.path()).filePath(QStringLiteral("fwhm_hazy.fit")),
                             QDir(root.path()).filePath(QStringLiteral("fwhm_fair.fit"))};
    const std::vector<double> stars{400.0, 100.0, 300.0};
    const std::vector<double> widths{3.0, 2.0, 2.5};
    QList<QJsonObject> rows;
    for (qsizetype i = 0; i < lights.size(); ++i)
    {
        rows.append(QJsonObject{{QStringLiteral("frame_id"), lights[i]},
                                {QStringLiteral("stars"), static_cast<int>(stars[static_cast<std::size_t>(i)])},
                                {QStringLiteral("fwhm"), widths[static_cast<std::size_t>(i)]},
                                {QStringLiteral("snr"), 20.0},
                                {QStringLiteral("eccentricity"), 0.2},
                                {QStringLiteral("succeeded"), true},
                                {QStringLiteral("output_path"), QStringLiteral("registered.fit")}});
    }
    ASSERT_TRUE(
        writeRegistrationMetricsDb(root.filePath(QStringLiteral("registration_metrics.db")), groupKey, 1, rows));

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{groupKey};
    regGroup.lightFiles = {FrameId(lights[0]), FrameId(lights[1]), FrameId(lights[2])};
    manifest.registrationGroups.push_back(regGroup);
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("WidthsGroup")}};
    group.registrationGroupKey = GroupKey{groupKey};
    group.filter = QStringLiteral("L");
    group.lightFiles = regGroup.lightFiles;
    group.lightExposureSeconds = {60.0, 60.0, 60.0};
    group.stacking.weighting = QStringLiteral("Weighted FWHM");
    group.stacking.minWeightFloor = 0.01;
    group.stacking.normalizationMode = NormalizationMode::Global;
    group.normalizationReferenceFrames = {FrameId(lights[0])};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    WorkflowReportBuilder builder(manifest, result, QString(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    const std::vector<double> stack = an::tests::integration::stackWidthWeights(widths, stars, 0u, 0.01);
    for (qsizetype i = 0; i < lights.size(); ++i)
    {
        const double relative = stack[static_cast<std::size_t>(i)] / stack[0];
        EXPECT_TRUE(report.contains(contributorRow(lights[i], relative)))
            << contributorRow(lights[i], relative).toStdString();
    }
    EXPECT_TRUE(report.contains(QStringLiteral("1 frame(s) at minimum-weight floor")));
}

// The weights the report shows are the ones the stack recorded combining each light with, where the run left them:
// after a spatial re-stack left a light out, the kept lights' weights are relative among themselves, and the light left
// out weighs nothing. The report derived the weights again over every registered light, so each kept light's was wrong.
TEST(WorkflowReportBuilder, TheWeightsShownAreTheOnesTheStackRecordedCombiningEachLightWith)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString groupKey = QStringLiteral("reg|recorded");
    const QStringList lights{QDir(root.path()).filePath(QStringLiteral("recorded_reference.fit")),
                             QDir(root.path()).filePath(QStringLiteral("recorded_flagged.fit")),
                             QDir(root.path()).filePath(QStringLiteral("recorded_fair.fit"))};
    const std::vector<double> stars{400.0, 100.0, 300.0};
    const std::vector<double> widths{3.0, 2.0, 2.5};
    QList<QJsonObject> rows;
    for (qsizetype i = 0; i < lights.size(); ++i)
    {
        rows.append(QJsonObject{{QStringLiteral("frame_id"), lights[i]},
                                {QStringLiteral("stars"), static_cast<int>(stars[static_cast<std::size_t>(i)])},
                                {QStringLiteral("fwhm"), widths[static_cast<std::size_t>(i)]},
                                {QStringLiteral("snr"), 20.0},
                                {QStringLiteral("eccentricity"), 0.2},
                                {QStringLiteral("succeeded"), true},
                                {QStringLiteral("output_path"), QStringLiteral("registered.fit")}});
    }
    ASSERT_TRUE(
        writeRegistrationMetricsDb(root.filePath(QStringLiteral("registration_metrics.db")), groupKey, 1, rows));

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{groupKey};
    regGroup.lightFiles = {FrameId(lights[0]), FrameId(lights[1]), FrameId(lights[2])};
    manifest.registrationGroups.push_back(regGroup);
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("RecordedGroup")}};
    group.registrationGroupKey = GroupKey{groupKey};
    group.filter = QStringLiteral("L");
    group.lightFiles = regGroup.lightFiles;
    group.lightExposureSeconds = {60.0, 60.0, 60.0};
    group.stacking.weighting = QStringLiteral("Weighted FWHM");
    group.stacking.minWeightFloor = 0.01;
    group.stacking.normalizationMode = NormalizationMode::Global;
    group.normalizationReferenceFrames = {FrameId(lights[0])};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    IntegrationGroupResult stacked;
    stacked.groupKey = group.groupKey.toString();
    stacked.success = true;
    stacked.outputFilename = QStringLiteral("recorded_master.fit");
    stacked.qualityReport.valid = true;
    stacked.qualityReport.spatialRestackApplied = true;
    stacked.qualityReport.spatialRestackExcludedFrameIndices = {1u};
    stacked.qualityReport.lightWeights = {StackLightWeight{.light = lights[0], .weight = 1.25},
                                          StackLightWeight{.light = lights[2], .weight = 0.75}};
    result.integrationGroupResults.push_back(stacked);
    WorkflowReportBuilder builder(manifest, result, QString(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(contributorRow(lights[0], 1.0))) << contributorRow(lights[0], 1.0).toStdString();
    EXPECT_TRUE(report.contains(contributorRow(lights[2], 0.6))) << contributorRow(lights[2], 0.6).toStdString();
    const std::vector<double> derived = an::tests::integration::stackWidthWeights(widths, stars, 0u, 0.01);
    EXPECT_FALSE(report.contains(contributorRow(lights[2], derived[2] / derived[0])))
        << "the report derived the weights again over every light";
}

// The report's noise weights are the stack's: one over the square of each light's background noise, as the
// registration recorded it, shown relative to the heaviest. The report ranked the lights' signal-to-noise ratios. The
// floor raises no noise weight, so the report counts no light at it.
TEST(WorkflowReportBuilder, NoiseWeightsAreTheStacksFromEachLightsBackgroundNoise)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    const QString groupKey = QStringLiteral("reg|noise");
    const QString quiet = QDir(root.path()).filePath(QStringLiteral("noise_quiet.fit"));
    const QString noisy = QDir(root.path()).filePath(QStringLiteral("noise_noisy.fit"));
    {
        an::ui::RegistrationMetricsWriter writer(root.filePath(QStringLiteral("registration_metrics.db")),
                                                 QStringLiteral("ReportNoiseSeedWriter"));
        QString error;
        ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
        // The quiet light has the lower signal-to-noise ratio, so a ranking by that ratio would favour the noisy one.
        const auto seed = [&](const QString& path, const double snr, const double noise) {
            an::ui::RegistrationMetricRecord metric;
            metric.frameId = FrameId(path);
            metric.groupKey = groupKey;
            metric.settingsVersion = 1;
            metric.metrics.stars = 100;
            metric.metrics.fwhm = 2.0;
            metric.metrics.snr = snr;
            metric.metrics.succeeded = true;
            metric.noise.background_noise = noise;
            metric.noise.global_scale = 1.0;
            return writer.upsertMetric(metric, &error);
        };
        ASSERT_TRUE(seed(quiet, 10.0, 0.01)) << error.toStdString();
        ASSERT_TRUE(seed(noisy, 50.0, 0.02)) << error.toStdString();
    }

    WorkflowManifest manifest;
    manifest.outputDirectory = root.path();
    manifest.executionMode = ExecutionMode::Full;
    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{groupKey};
    regGroup.lightFiles = {FrameId(quiet), FrameId(noisy)};
    manifest.registrationGroups.push_back(regGroup);
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("NoiseGroup")}};
    group.registrationGroupKey = GroupKey{groupKey};
    group.filter = QStringLiteral("L");
    group.lightFiles = regGroup.lightFiles;
    group.lightExposureSeconds = {60.0, 60.0};
    group.stacking.weighting = QStringLiteral("Noise");
    group.stacking.minWeightFloor = 0.5;
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    WorkflowReportBuilder builder(manifest, result, QString(), QJsonObject(), nullptr, &*session);
    const QString report = builder.build();

    const std::vector<double> stack = an::tests::integration::stackNoiseWeights({0.01, 0.02});
    EXPECT_TRUE(report.contains(contributorRow(quiet, 1.0))) << contributorRow(quiet, 1.0).toStdString();
    EXPECT_TRUE(report.contains(contributorRow(noisy, stack[1] / stack[0])))
        << contributorRow(noisy, stack[1] / stack[0]).toStdString();
    EXPECT_FALSE(report.contains(QStringLiteral("minimum-weight floor")));
}

TEST(WorkflowReportBuilder, FullProcessingParametersSectionIncludesNormalizationReferencesAndAdvancedSettings)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");

    const QString lightA = QStringLiteral("/data/lights/light_A.fit");
    const QString lightB = QStringLiteral("/data/lights/light_B.fit");
    const QString normRef = QStringLiteral("/data/lights/light_ref.fit");
    const QString regKey = QStringLiteral("RegGroupA");

    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{regKey};
    regGroup.lightFiles = {FrameId(lightA), FrameId(lightB), FrameId(normRef)};
    regGroup.referenceFrame = FrameId(normRef);
    regGroup.settings.autoReferenceDuringRegistration = false;
    regGroup.settings.radius = 7;
    regGroup.settings.threshold = 1.25;
    regGroup.settings.roundnessRangeEnabled = true;
    regGroup.settings.profileType = QStringLiteral("Moffat");
    regGroup.settings.minimumBeta = 2.2;
    manifest.registrationGroups.push_back(regGroup);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("LightGroupA")}};
    group.registrationGroupKey = GroupKey{regKey};
    group.lightFiles = {FrameId(lightA), FrameId(lightB), FrameId(normRef)};
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
    group.stacking.scaleEstimator = ScaleEstimatorMethod::IKSS;
    group.stacking.localNormalization.enabled = true;
    group.stacking.localNormalization.referenceStrategy = QStringLiteral("Integrate Best Frames");
    group.stacking.localNormalization.maxIntegratedFrames = 12;
    group.stacking.localNormalization.minDetectionSnr = 22.0;
    group.normalizationReferenceFrames = {FrameId(normRef)};
    group.normalizationCoefficientsArtifactPath = QStringLiteral("normalization/groupA/coeffs.xisf");
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("## \U0001F9F0 Full Processing Parameters")));
    EXPECT_TRUE(report.contains(QStringLiteral("[Full Processing Parameters](#full-processing-parameters)")));
    EXPECT_TRUE(report.contains(QStringLiteral("### Normalization Reference Frame(s)")));
    EXPECT_TRUE(report.contains(QStringLiteral("light_ref.fit")));
    EXPECT_TRUE(report.contains(QStringLiteral("Auto-detected during integration (not selected by user)")));
    EXPECT_TRUE(report.contains(QStringLiteral("Normalization Coefficients Artifact")));
    // Local normalization's global match is Additive With Scaling, not the method the group stores for Global.
    EXPECT_TRUE(report.contains(QStringLiteral("| Global Normalization Method | Additive + Scaling (set by local "
                                               "normalization) |")));
    EXPECT_FALSE(report.contains(QStringLiteral("Multiplicative + Scaling")));
    // The floor is the flux scale's, not the detection's.
    EXPECT_TRUE(report.contains(QStringLiteral("| Min SNR (flux scale) | 22 |")));
    EXPECT_TRUE(report.contains(QStringLiteral("Detection Radius")));
    EXPECT_TRUE(report.contains(QStringLiteral("Roundness Range Enabled")));
    EXPECT_TRUE(report.contains(QStringLiteral("Profile Type")));
    EXPECT_TRUE(report.contains(QStringLiteral("Chosen Reference Frame")));
}

TEST(WorkflowReportBuilder, AutoParametersShowResolvedInlineLabelsAndDecisionDetails)
{
    QTemporaryDir runDir;
    ASSERT_TRUE(runDir.isValid());

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();

    const QString lightA = runDir.filePath(QStringLiteral("light_A.fit"));
    const QString lightB = runDir.filePath(QStringLiteral("light_B.fit"));
    const QString regKey = QStringLiteral("AutoRegGroup");

    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{regKey};
    regGroup.lightFiles = {FrameId(lightA), FrameId(lightB)};
    regGroup.referenceFrame = FrameId(lightA);
    regGroup.settings.autoReferenceDuringRegistration = true;
    regGroup.settings.referenceImage.clear();
    regGroup.settings.registrationMethod = QStringLiteral("star_matching");
    regGroup.settings.transformation = QStringLiteral("Homography");
    regGroup.settings.transformMode = QStringLiteral("Auto");
    regGroup.settings.transformOrder = 0;
    regGroup.settings.twoPassRefinementEnabled = true;
    regGroup.settings.interpolation = QStringLiteral("Auto");
    regGroup.settings.cfaPattern = QStringLiteral("Auto");
    manifest.registrationGroups.push_back(regGroup);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("AutoLightGroup")}};
    group.registrationGroupKey = GroupKey{regKey};
    group.lightFiles = {FrameId(lightA), FrameId(lightB)};
    group.stacking.stackMethod = QStringLiteral("Average");
    group.stacking.requestedRejectionMethod = QStringLiteral("Auto");
    group.stacking.rejectionMethod = QStringLiteral("GESDT");
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.normalizationReferenceFrames = {FrameId(lightA)};
    manifest.integrationGroups.push_back(group);

    QJsonObject transform;
    transform.insert(QStringLiteral("schemaVersion"), 3);
    transform.insert(QStringLiteral("kind"), QStringLiteral("polynomial2"));

    QJsonObject metricRow;
    metricRow.insert(QStringLiteral("frame_id"), lightB);
    metricRow.insert(QStringLiteral("stars"), 48);
    metricRow.insert(QStringLiteral("fwhm"), 2.1);
    metricRow.insert(QStringLiteral("snr"), 34.0);
    metricRow.insert(QStringLiteral("eccentricity"), 0.42);
    metricRow.insert(QStringLiteral("succeeded"), true);
    metricRow.insert(QStringLiteral("transform_json"),
                     QString::fromUtf8(QJsonDocument(transform).toJson(QJsonDocument::Compact)));
    ASSERT_TRUE(
        writeRegistrationMetricsDb(runDir.filePath(QStringLiteral("registration_metrics.db")), regKey, 1, {metricRow}));

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result, runDir.path());
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("| Transform Model | Auto (Homography + Polynomial 2) |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Interpolation | Auto (Lanczos4) |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| CFA Pattern | Auto (from metadata) |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Rejection Method | Auto (GESDT) |")));
    EXPECT_TRUE(report.contains(QStringLiteral("## Auto Decisions")));
    EXPECT_TRUE(report.contains(QStringLiteral("Transform Model")));
    EXPECT_TRUE(report.contains(QStringLiteral("Auto requested; metrics show Polynomial 2 was used")));
    EXPECT_TRUE(report.contains(QStringLiteral("Registration Interpolation")));
    EXPECT_TRUE(report.contains(QStringLiteral("Auto resolves to Lanczos4")));
    EXPECT_TRUE(report.contains(QStringLiteral("Stacking Rejection")));
    EXPECT_TRUE(report.contains(QStringLiteral("Auto resolved to GESDT")));
}

// The report names the model the star matcher fits: a linear mode's, whatever order is stored beside it.
TEST(WorkflowReportBuilder, AnOrderBesideALinearTransformModeIsReportedAsTheLinearModel)
{
    QTemporaryDir runDir;
    ASSERT_TRUE(runDir.isValid());

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    const QString lightA = runDir.filePath(QStringLiteral("light_A.fit"));
    const QString lightB = runDir.filePath(QStringLiteral("light_B.fit"));

    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{QStringLiteral("LinearRegGroup")};
    regGroup.lightFiles = {FrameId(lightA), FrameId(lightB)};
    regGroup.referenceFrame = FrameId(lightA);
    regGroup.settings.registrationMethod = QStringLiteral("star_matching");
    regGroup.settings.transformation = QStringLiteral("Affine");
    regGroup.settings.transformMode = QStringLiteral("linear");
    regGroup.settings.transformOrder = 3;
    regGroup.settings.twoPassRefinementEnabled = true;
    manifest.registrationGroups.push_back(regGroup);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("LinearLightGroup")}};
    group.registrationGroupKey = regGroup.groupKey;
    group.lightFiles = regGroup.lightFiles;
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    const QString report = WorkflowReportBuilder(manifest, result, runDir.path()).build();

    EXPECT_TRUE(report.contains(QStringLiteral("| Transform Model | Affine |"))) << report.toStdString();
    EXPECT_FALSE(report.contains(QStringLiteral("Polynomial 3")));
}

// The registration parameters name the star cap as the dialog does - it caps the stars detected in each frame, not
// the pairs matched - and leave out two-pass refinement, which only restates the transform mode and no run reads.
TEST(WorkflowReportBuilder, RegistrationParametersNameTheStarCapAndLeaveOutWhatTheModeAlreadyStates)
{
    QTemporaryDir runDir;
    ASSERT_TRUE(runDir.isValid());

    WorkflowManifest manifest;
    manifest.outputDirectory = runDir.path();
    const QString lightA = runDir.filePath(QStringLiteral("light_A.fit"));
    const QString lightB = runDir.filePath(QStringLiteral("light_B.fit"));

    WorkflowManifest::RegistrationGroup regGroup;
    regGroup.groupKey = GroupKey{QStringLiteral("PolynomialRegGroup")};
    regGroup.lightFiles = {FrameId(lightA), FrameId(lightB)};
    regGroup.referenceFrame = FrameId(lightA);
    regGroup.settings.registrationMethod = QStringLiteral("star_matching");
    regGroup.settings.maxStarPairs = 1500;
    regGroup.settings.transformation = QStringLiteral("Homography");
    regGroup.settings.transformMode = QStringLiteral("polynomial");
    regGroup.settings.transformOrder = 3;
    regGroup.settings.twoPassRefinementEnabled = true;
    manifest.registrationGroups.push_back(regGroup);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("PolynomialLightGroup")}};
    group.registrationGroupKey = regGroup.groupKey;
    group.lightFiles = regGroup.lightFiles;
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    const QString report = WorkflowReportBuilder(manifest, result, runDir.path()).build();

    EXPECT_TRUE(report.contains(QStringLiteral("| Max Stars per Frame | 1500 |"))) << report.toStdString();
    EXPECT_FALSE(report.contains(QStringLiteral("Max Star Pairs")));
    EXPECT_TRUE(report.contains(QStringLiteral("| Transform Mode | polynomial |")));
    EXPECT_FALSE(report.contains(QStringLiteral("Two-pass Refinement")));
}

TEST(WorkflowReportBuilder, FullProcessingParametersOmitsNormalizationBlocksWhenDisabled)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("LightGroupNoNorm")}};
    group.stacking.normalizationMode = NormalizationMode::None;
    group.stacking.localNormalization.enabled = false;
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Full Processing Parameters")));
    EXPECT_TRUE(report.contains(QStringLiteral(
        "Normalization disabled for this group; normalization reference and parameter details are omitted")));
    EXPECT_FALSE(report.contains(QStringLiteral("### Normalization Reference Frame(s)")));
    EXPECT_FALSE(report.contains(QStringLiteral("### Local Normalization Settings")));
}

// A drizzled Local group's stack fits its own surfaces against the group's reference: the report says which of its
// local-normalization settings do not apply, and its stacking summary names no reference strategy. It listed every
// setting as if the normalization stage had measured the group.
TEST(WorkflowReportBuilder, ADrizzledLocalGroupsReportSaysWhatItsStackDoesNotRead)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("LightGroupDrizzled")}};
    group.lightFiles = {FrameId(QStringLiteral("/data/lights/light_A.fit"))};
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.stacking.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;
    IntegrationGroupResult stacked;
    stacked.groupKey = QStringLiteral("LightGroupDrizzled");
    stacked.success = true;
    stacked.lightFrameCount = 1;
    result.integrationGroupResults.push_back(stacked);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    ASSERT_TRUE(report.contains(QStringLiteral("### Local Normalization Settings")));
    EXPECT_TRUE(report.contains(QStringLiteral("_The drizzled stack fits these surfaces against the group's "
                                               "reference: the reference strategy, the evaluation, the PSF flux "
                                               "scale and the hot-pixel and noise filters do not apply._")));
    EXPECT_TRUE(report.contains(QStringLiteral("fitted by the drizzled stack"))) << "the stacking summary";
    EXPECT_FALSE(report.contains(QStringLiteral("ref: Single Best Frame")));
}

TEST(WorkflowReportBuilder, FullProcessingParametersIncludeHdrExposureMergeSummary)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("HdrGroup")}};
    group.lightFiles = {
        FrameId(QStringLiteral("/data/lights/long_01.fit")), FrameId(QStringLiteral("/data/lights/long_02.fit")),
        FrameId(QStringLiteral("/data/lights/short_01.fit")), FrameId(QStringLiteral("/data/lights/short_02.fit"))};
    group.lightExposureSeconds = {120.0, 120.0, 30.0, 30.0};
    group.stacking.stackMethod = QStringLiteral("Average");
    group.stacking.hdr.enabled = true;
    manifest.integrationGroups.push_back(group);

    WorkflowExecutor::Result result;
    result.success = true;

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("| Stack Method | HDR Merge (Average) |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| HDR Exposure Merge | Yes |")));
    EXPECT_TRUE(report.contains(QStringLiteral("| HDR Exposure Buckets | 2 × 30s, 2 × 120s |")));
}

TEST(WorkflowReportBuilder, IncludesPrintFooterContextWithVersionAndExecutionId)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionId = QStringLiteral("exec-print-001");

    WorkflowExecutor::Result result;
    result.success = true;

    QJsonObject context;
    context.insert(QStringLiteral("appVersion"), QStringLiteral("3.4.5-test"));
    context.insert(QStringLiteral("executionId"), manifest.executionId);
    context.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2026-02-11T18:22:30.000"));

    WorkflowReportBuilder builder(manifest, result, QString(), context);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("an-print-footer")));
    EXPECT_TRUE(report.contains(QStringLiteral("AfterNight 3.4.5-test")));
    EXPECT_TRUE(report.contains(QStringLiteral("Execution exec-print-001")));
    EXPECT_TRUE(report.contains(QStringLiteral("Generated 2026-02-11 18:22:30")));
}

TEST(WorkflowReportBuilder, PipelineStagesUseSeverityPills)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.success = false;

    WorkflowExecutor::Result::StageTiming okStage;
    okStage.stageName = QStringLiteral("BiasStacking");
    okStage.status = QStringLiteral("completed");
    result.stageResults.push_back(okStage);

    WorkflowExecutor::Result::StageTiming failedStage;
    failedStage.stageName = QStringLiteral("LightsStacking");
    failedStage.status = QStringLiteral("failed");
    result.stageResults.push_back(failedStage);

    WorkflowExecutor::Result::StageTiming partialStage;
    partialStage.stageName = QStringLiteral("LightsRegistration");
    partialStage.status = QStringLiteral("partial");
    result.stageResults.push_back(partialStage);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("an-severity-pill an-severity-success")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-severity-pill an-severity-error")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-severity-pill an-severity-warning")));
}

TEST(WorkflowReportBuilder, WarningsErrorsUseSeverityStyledList)
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
    result.success = false;
    result.errors.append(QStringLiteral("Fatal failure A"));
    result.warnings.append(QStringLiteral("Potential issue B"));

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("<ul class=\"an-issues-list\">")));
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"an-issue-item an-issue-error\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("class=\"an-issue-item an-issue-warning\"")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-severity-pill an-severity-error")));
    EXPECT_TRUE(report.contains(QStringLiteral("an-severity-pill an-severity-warning")));
}

TEST(WorkflowReportBuilder, StackedMastersRenderStructuredQualityReportMetrics)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("QualityGroup")}};
    integration.outputFilename = QStringLiteral("stacked_QualityGroup.fit");
    integration.lightFiles = {FrameId(QStringLiteral("light_1.fit")), FrameId(QStringLiteral("light_2.fit")),
                              FrameId(QStringLiteral("light_3.fit")), FrameId(QStringLiteral("light_4.fit"))};
    integration.resolution = QStringLiteral("32x32");
    integration.exposure = QStringLiteral("60");
    manifest.integrationGroups.push_back(integration);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("QualityGroup");
    group.outputFilename = QStringLiteral("stacked_QualityGroup.fit");
    group.outputBytes = 4096;
    group.lightFrameCount = 4;
    group.durationMs = 900;
    group.success = true;
    group.qualityReport.valid = true;
    group.qualityReport.inputFrameCount = 4;
    group.qualityReport.effectiveFrameCount = 3.75;
    group.qualityReport.theoreticalSnrGain = 2.0;
    group.qualityReport.actualSnrGain = std::sqrt(3.75);
    group.qualityReport.integrationEfficiency = 0.9375;
    group.qualityReport.minCoverage = 3.0;
    group.qualityReport.medianCoverage = 4.0;
    group.qualityReport.maxCoverage = 4.0;
    group.qualityReport.coverageStddev = 0.12;
    group.qualityReport.hasRejectionStats = true;
    group.qualityReport.globalRejectionRate = 0.015625;
    group.qualityReport.minFrameRejectionRate = 0.0;
    group.qualityReport.meanFrameRejectionRate = 0.015625;
    group.qualityReport.medianFrameRejectionRate = 0.0;
    group.qualityReport.maxFrameRejectionRate = 0.0625;
    group.qualityReport.hasWeightingStats = true;
    group.qualityReport.weightMin = 0.8;
    group.qualityReport.weightMax = 1.2;
    group.qualityReport.weightStddev = 0.15;
    group.qualityReport.weightEfficiency = 0.96;
    group.qualityReport.hasSpatialRejectionClusters = true;
    group.qualityReport.spatialClusterCount = 2;
    group.qualityReport.spatialClusterPixels = 48;
    group.qualityReport.spatialLargestClusterPixels = 36;
    group.qualityReport.spatialClusterFraction = 0.046875;
    group.qualityReport.spatialLargestClusterFraction = 0.03515625;
    group.qualityReport.spatialClusters.push_back(
        {RejectionClusterMapKind::High, 0, 36, 10, 12, 21, 14, 15.5, 13.0, 0.42, 0.9, 15.12});
    group.qualityReport.spatialRestackRequested = true;
    group.qualityReport.spatialRestackApplied = true;
    group.qualityReport.spatialRestackInitialClusterCount = 2;
    group.qualityReport.spatialRestackFlaggedFrameThreshold = 0.025;
    group.qualityReport.spatialRestackExcludedFrameIndices = {1};
    group.qualityReport.spatialRestackExcludedFrameRates = {0.0625};
    group.qualityReport.normalization = QStringLiteral("The normalization stage's coefficients, reference light_3.fit");
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(
        QStringLiteral("**Normalization**: The normalization stage's coefficients, reference light_3.fit")))
        << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("Stacking Quality Report")));
    EXPECT_TRUE(report.contains(QStringLiteral("Effective Frames")));
    EXPECT_TRUE(report.contains(QStringLiteral("Integration Efficiency")));
    EXPECT_TRUE(report.contains(QStringLiteral("Global Rejection Rate")));
    EXPECT_TRUE(report.contains(QStringLiteral("Weight Efficiency")));
    EXPECT_TRUE(report.contains(QStringLiteral("Spatial Rejection Clusters")));
    EXPECT_TRUE(report.contains(QStringLiteral("Spatial Re-Stack")));
    EXPECT_TRUE(report.contains(QStringLiteral("light_2.fit")));
}

TEST(WorkflowReportBuilder, StackedMastersReferenceQualityMapArtifacts)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("QualityMapsGroup")}};
    integration.outputFilename = QStringLiteral("stacked_QualityMapsGroup.fit");
    integration.lightFiles = {FrameId(QStringLiteral("light_1.fit")), FrameId(QStringLiteral("light_2.fit"))};
    integration.resolution = QStringLiteral("32x32");
    integration.exposure = QStringLiteral("60");
    integration.stacking.createQualityMaps = true;
    manifest.integrationGroups.push_back(integration);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("QualityMapsGroup");
    group.outputFilename = QStringLiteral("stacked_QualityMapsGroup.fit");
    group.outputBytes = 2048;
    group.lightFrameCount = 2;
    group.durationMs = 400;
    group.success = true;
    group.qualityMaps.push_back(QualityMapResult{
        QStringLiteral("Coverage Map"), QStringLiteral("/tmp/stacked_QualityMapsGroup_coverage.fit"), {}});
    group.qualityMaps.push_back(QualityMapResult{
        QStringLiteral("SNR Gain Map"), QStringLiteral("/tmp/stacked_QualityMapsGroup_snrgain.fit"), {}});
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Quality Maps")));
    EXPECT_TRUE(report.contains(QStringLiteral("Coverage Map")));
    EXPECT_TRUE(report.contains(QStringLiteral("SNR Gain Map")));
}

TEST(WorkflowReportBuilder, DisabledQualityMapsSuppressOldArtifactsButKeepStatisticsFallbackAndRejectionMaps)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Full;

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("QualityPolicyGroup")}};
    integration.outputFilename = QStringLiteral("stacked_QualityPolicyGroup.fit");
    integration.lightFiles = {FrameId(QStringLiteral("light_1.fit")), FrameId(QStringLiteral("light_2.fit"))};
    integration.stacking.createQualityMaps = false;
    integration.stacking.createRejectionMaps = true;
    manifest.integrationGroups.push_back(integration);

    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("QualityPolicyGroup");
    group.outputFilename = QStringLiteral("stacked_QualityPolicyGroup.fit");
    group.outputBytes = 2048;
    group.lightFrameCount = 2;
    group.success = true;
    group.qualityReport.valid = true;
    group.qualityReport.inputFrameCount = 2;
    group.qualityReport.effectiveFrameCount = 1.8;
    group.qualityReport.actualSnrGain = 1.34;
    group.qualityReport.theoreticalSnrGain = 1.41;
    group.qualityReport.integrationEfficiency = 0.9;
    group.qualityReport.hasWeightingStats = true;
    group.qualityReport.weightMin = 0.7;
    group.qualityReport.weightMax = 1.3;
    group.qualityReport.weightStddev = 0.2;
    group.qualityReport.weightEfficiency = 0.95;
    group.qualityReport.hasLocalWeightingStats = true;
    group.qualityReport.localWeightWindowSize = 9;
    group.qualityReport.localWeightGridSpacing = 16;
    group.qualityReport.localWeightSnrExponent = 2.0;
    group.qualityReport.localWeightGridCount = 2;
    group.qualityReport.localWeightMin = 0.6;
    group.qualityReport.localWeightMax = 1.4;
    group.qualityReport.localWeightMean = 1.0;
    group.qualityReport.localWeightStddev = 0.25;

    group.qualityMaps.push_back(QualityMapResult{QStringLiteral("Coverage Map"),
                                                 QStringLiteral("/tmp/stacked_QualityPolicyGroup_coverage.fit"),
                                                 QStringLiteral("old_coverage.jpg")});
    group.qualityMaps.push_back(QualityMapResult{QStringLiteral("Weight Sum Map"),
                                                 QStringLiteral("/tmp/stacked_QualityPolicyGroup_weightsum.fit"),
                                                 QStringLiteral("old_weight_sum.jpg")});
    group.rejectionMaps.push_back(RejectionMapResult{QStringLiteral("Rejection Map"),
                                                     QStringLiteral("/tmp/stacked_QualityPolicyGroup_rejection.fit"),
                                                     QStringLiteral("rejection.jpg")});
    result.integrationGroupResults.push_back(group);

    WorkflowReportBuilder builder(manifest, result);
    const QString report = builder.build();

    EXPECT_TRUE(report.contains(QStringLiteral("Stacking Quality Report")));
    EXPECT_TRUE(report.contains(QStringLiteral("Effective Frames")));
    EXPECT_TRUE(report.contains(QStringLiteral("Weight Efficiency")));
    EXPECT_TRUE(report.contains(QStringLiteral("Per-Pixel (Local) Weighting")));
    EXPECT_TRUE(report.contains(QStringLiteral("Spatially Varying Weights")));
    EXPECT_TRUE(
        report.contains(QStringLiteral("Enable Quality Maps in stacking settings for a full weight sum map preview.")));
    EXPECT_FALSE(report.contains(QStringLiteral("- **Quality Maps**:")));
    EXPECT_FALSE(report.contains(QStringLiteral("<summary>Quality maps</summary>")));
    EXPECT_FALSE(report.contains(QStringLiteral("Coverage Map")));
    EXPECT_FALSE(report.contains(QStringLiteral("Weight Sum Map")));
    EXPECT_FALSE(report.contains(QStringLiteral("old_coverage.jpg")));
    EXPECT_FALSE(report.contains(QStringLiteral("old_weight_sum.jpg")));
    EXPECT_TRUE(report.contains(QStringLiteral("Rejection Map")));
    EXPECT_TRUE(report.contains(QStringLiteral("rejection.jpg")));
}

TEST(MarkdownHtmlRenderer, UsesThemeTokensForCssVariablesAndNoHardcodedDarkCardBlock)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString htmlPath = tempDir.filePath(QStringLiteral("report.html"));
    QString error;
    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        QStringLiteral(
            "# Test report\n\n<div class=\"an-secondary-gallery\"><div class=\"an-secondary-card\">card</div></div>\n"),
        htmlPath, QStringLiteral("Report"), &error))
        << error.toStdString();

    QFile file(htmlPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());

    const auto& activeTokens = an::ui::tokens();
    EXPECT_TRUE(html.contains(QStringLiteral("--an-bg: %1;").arg(activeTokens.colors.bgPrimary.name(QColor::HexRgb))));
    EXPECT_TRUE(
        html.contains(QStringLiteral("--an-accent: %1;").arg(activeTokens.colors.accentPrimary.name(QColor::HexRgb))));
    EXPECT_TRUE(html.contains(QStringLiteral("--an-radius-sm:")));
    EXPECT_FALSE(html.contains(QStringLiteral(".an-secondary-card { background: #161b22; border-color: #30363d; }")));
    EXPECT_FALSE(html.contains(QStringLiteral(".an-media-card { background: #161b22; border-color: #30363d; }")));
    EXPECT_FALSE(html.contains(QStringLiteral(".an-histogram")));
}

TEST(MarkdownHtmlRenderer, SupportsForcedLightAndDarkThemeModes)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString markdown = QStringLiteral("# Test report\n");
    const QString lightHtmlPath = tempDir.filePath(QStringLiteral("report_light.html"));
    const QString darkHtmlPath = tempDir.filePath(QStringLiteral("report_dark.html"));
    QString error;

    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        markdown, lightHtmlPath, QStringLiteral("Report"), ImageIntegration::Reporting::ReportThemeMode::Light, &error))
        << error.toStdString();

    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        markdown, darkHtmlPath, QStringLiteral("Report"), ImageIntegration::Reporting::ReportThemeMode::Dark, &error))
        << error.toStdString();

    QFile lightFile(lightHtmlPath);
    ASSERT_TRUE(lightFile.open(QIODevice::ReadOnly));
    const QString lightHtml = QString::fromUtf8(lightFile.readAll());

    QFile darkFile(darkHtmlPath);
    ASSERT_TRUE(darkFile.open(QIODevice::ReadOnly));
    const QString darkHtml = QString::fromUtf8(darkFile.readAll());

    const auto& lightTokens = an::ui::resolvedLightTokens();
    const auto& darkTokens = an::ui::resolvedDarkTokens();

    EXPECT_TRUE(lightHtml.contains(QStringLiteral("html { color-scheme: light; }")));
    EXPECT_TRUE(
        lightHtml.contains(QStringLiteral("--an-bg: %1;").arg(lightTokens.colors.bgPrimary.name(QColor::HexRgb))));

    EXPECT_TRUE(darkHtml.contains(QStringLiteral("html { color-scheme: dark; }")));
    EXPECT_TRUE(
        darkHtml.contains(QStringLiteral("--an-bg: %1;").arg(darkTokens.colors.bgPrimary.name(QColor::HexRgb))));
}

TEST(MarkdownHtmlRenderer, ExecutedStagesListRendersCorrectlyAfterDetailsBlock)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/test");
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.incrementalIntegrationGroups.push_back(
        IntegrationGroupKey{GroupKey{QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")}});

    WorkflowExecutor::Result result;
    result.success = true;
    WorkflowExecutor::Result::StageTiming stage;
    stage.stageName = QStringLiteral("LightsStacking");
    stage.status = QStringLiteral("completed");
    stage.durationMs = 1000;
    stage.groupsProcessed = 1;
    result.stageResults.push_back(stage);

    WorkflowReportBuilder builder(manifest, result);
    const QString markdown = builder.build();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString htmlPath = tempDir.filePath(QStringLiteral("report.html"));
    QString error;
    ASSERT_TRUE(
        ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(markdown, htmlPath, QStringLiteral("Report"), &error))
        << error.toStdString();

    QFile file(htmlPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());

    // This should render as a list item, not as raw markdown text.
    EXPECT_FALSE(html.contains(QStringLiteral("- **Executed Stages**")));
    EXPECT_TRUE(html.contains(QStringLiteral("<strong>Executed Stages</strong>")));
}

TEST(MarkdownHtmlRenderer, UsesAfterNightBrandedReportTheme)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString htmlPath = tempDir.filePath(QStringLiteral("report.html"));
    QString error;
    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        QStringLiteral("# Test report\n\n## Section\n\n| A | B |\n|---|---|\n| 1 | 2 |\n"), htmlPath,
        QStringLiteral("Report"), &error))
        << error.toStdString();

    QFile file(htmlPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());

    EXPECT_TRUE(html.contains(QStringLiteral("--an-accent")));
    EXPECT_TRUE(html.contains(QStringLiteral("radial-gradient")));
    EXPECT_TRUE(html.contains(QStringLiteral("--an-font-body:")));
    EXPECT_TRUE(html.contains(QStringLiteral("--an-font-mono:")));
    EXPECT_TRUE(html.contains(QStringLiteral(".an-report-hero")));
    EXPECT_TRUE(html.contains(QStringLiteral("@media print")));
    // The report should not embed the full GitHub markdown stylesheet surface.
    EXPECT_FALSE(html.contains(QStringLiteral("task-list-item-checkbox")));
    EXPECT_FALSE(html.contains(QStringLiteral(".markdown-alert")));
}

TEST(MarkdownHtmlRenderer, PrintStylesIncludeFooterAndPageBreakGuards)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString htmlPath = tempDir.filePath(QStringLiteral("report.html"));
    QString error;
    ASSERT_TRUE(ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(
        QStringLiteral(
            "# Test report\n\n<div class=\"an-print-footer\"><span class=\"an-print-footer-text\">ctx</span></div>\n"),
        htmlPath, QStringLiteral("Report"), &error))
        << error.toStdString();

    QFile file(htmlPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());

    EXPECT_TRUE(html.contains(QStringLiteral("@page { margin:")));
    EXPECT_TRUE(html.contains(QStringLiteral(".an-print-footer::after { content: \"Page \" counter(page);")));
    EXPECT_TRUE(html.contains(QStringLiteral("details > *:not(summary) { display: block !important; }")));
    EXPECT_TRUE(html.contains(QStringLiteral("font-variant-numeric: tabular-nums;")));
}
