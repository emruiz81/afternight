// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A run's archive read back as a report model: the report the archive keeps reads as the model it was built as,
/// and an archive that keeps none, a document cut short and a document of another format are each refused by name.
/// Reading the runs of a folder opens their stored reports and nothing else.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportModelJson.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <optional>

using namespace an::tests::integration::report;
using namespace ImageIntegration::Reporting;

namespace
{
/// Writes bytes as the stored report of a run's folder, which is made when it is not there.
void writeStored(const QString& runDirectory, const QByteArray& bytes)
{
    ASSERT_TRUE(QDir().mkpath(runDirectory));
    QFile file(QDir(runDirectory).filePath(QLatin1String(kStoredReportFileName)));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(bytes), bytes.size());
}

/// A small model, as a run of one master states it.
ReportModel smallModel()
{
    ReportModel model;
    model.run.title = QStringLiteral("NGC 253");
    model.run.executionId = QStringLiteral("20260326_071204_118");
    model.run.frames = 18;
    model.run.framesUsed = 17;
    ReportMaster master;
    master.key = QStringLiteral("64x48|-|-|-|Luminance|NGC 253|-");
    master.name = QStringLiteral("Luminance");
    master.snapshot.backgroundNoise = 0.0125;
    model.masters.push_back(master);
    return model;
}

QByteArray documentOf(const ReportModel& model)
{
    return QJsonDocument(ReportModelJson::toJson(model)).toJson(QJsonDocument::Indented);
}

void expectRefused(const ReportArchiveRead& read, const ReportArchiveRefusal refusal)
{
    EXPECT_FALSE(read.isRead());
    EXPECT_FALSE(read.model.has_value());
    ASSERT_TRUE(read.refusal.has_value());
    EXPECT_EQ(*read.refusal, refusal);
    EXPECT_FALSE(read.why.trimmed().isEmpty()) << "a refusal says why";
}
} // namespace

TEST(ReportArchiveReader, TheReportARunStoredReadsBackAsTheModelItWasBuiltAs)
{
    auto scenario = buildReportScenario(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(archiveScenarioRun(*scenario));
    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    const WorkflowReportStored stored = storeWorkflowReport(model, scenario->runDirectory);
    ASSERT_TRUE(stored.isStored()) << stored.error.toStdString();
    EXPECT_EQ(QFileInfo(stored.modelPath).fileName(), QLatin1String(kStoredReportFileName));
    EXPECT_EQ(QFileInfo(stored.reportPath).fileName(), QLatin1String(kWorkflowReportFileName));
    EXPECT_EQ(QFileInfo(stored.modelPath).absolutePath(), QFileInfo(scenario->runDirectory).absoluteFilePath());

    const ReportArchiveRead read = ReportArchiveReader::read(scenario->runDirectory);

    ASSERT_TRUE(read.isRead()) << read.why.toStdString();
    EXPECT_FALSE(read.refusal.has_value());
    EXPECT_TRUE(read.why.isEmpty());
    EXPECT_EQ(*read.model, model);
}

TEST(ReportArchiveReader, TheStoredReportIsWithinItsSize)
{
    // A history is bounded in size and every run of it keeps its report: 100 KB and 1 KB a light.
    for (const ReportScenarioId id : {ReportScenarioId::FiveFilters, ReportScenarioId::FiveFiltersAfterTwoEarlierRuns,
                                      ReportScenarioId::TwoThousandLights})
    {
        SCOPED_TRACE(scenarioName(id));
        auto scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        ASSERT_TRUE(archiveScenarioRun(*scenario));
        const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);
        const WorkflowReportStored stored = storeWorkflowReport(model, scenario->runDirectory);
        ASSERT_TRUE(stored.isStored()) << stored.error.toStdString();

        const qint64 bytes = QFileInfo(stored.modelPath).size();
        const qint64 lights = model.run.frames;
        ASSERT_GT(lights, 0);
        EXPECT_GT(bytes, 0);
        EXPECT_LE(bytes, 100 * 1024 + lights * 1024) << bytes << " bytes for " << lights << " lights";
        EXPECT_TRUE(ReportArchiveReader::read(scenario->runDirectory).isRead());
    }
}

TEST(ReportArchiveReader, AnArchiveThatKeepsNoStoredReportIsRefusedByName)
{
    auto scenario = buildReportScenario(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(archiveScenarioRun(*scenario));
    // The archive holds the run's manifest and results, and nothing is built from them.
    ASSERT_TRUE(QFileInfo::exists(QDir(scenario->runDirectory).filePath(QStringLiteral("workflow_manifest.json"))));

    const ReportArchiveRead archive = ReportArchiveReader::read(scenario->runDirectory);
    expectRefused(archive, ReportArchiveRefusal::NoStoredReport);
    EXPECT_TRUE(archive.why.contains(QLatin1String(kStoredReportFileName))) << archive.why.toStdString();

    expectRefused(ReportArchiveReader::read(QDir(scenario->runDirectory).filePath(QStringLiteral("not-there"))),
                  ReportArchiveRefusal::NoStoredReport);
    expectRefused(ReportArchiveReader::read(QString()), ReportArchiveRefusal::NoStoredReport);
}

TEST(ReportArchiveReader, AStoredReportCutShortIsRefusedByName)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QByteArray whole = documentOf(smallModel());
    ASSERT_TRUE(ReportArchiveReader::parse(whole).isRead());

    // Cut at every tenth of its length, the document never reads and never crashes the reader.
    for (int tenth = 0; tenth < 10; ++tenth)
    {
        SCOPED_TRACE(tenth);
        const QByteArray cut = whole.left(whole.size() * tenth / 10);
        expectRefused(ReportArchiveReader::parse(cut), ReportArchiveRefusal::NotReadable);
        const QString run = QDir(root.path()).filePath(QStringLiteral("run-%1").arg(tenth));
        writeStored(run, cut);
        expectRefused(ReportArchiveReader::read(run), ReportArchiveRefusal::NotReadable);
    }

    // A document that is JSON and is no object, and bytes that are no document at all.
    expectRefused(ReportArchiveReader::parse(QByteArrayLiteral("[1, 2, 3]")), ReportArchiveRefusal::NotReadable);
    expectRefused(ReportArchiveReader::parse(QByteArrayLiteral("42")), ReportArchiveRefusal::NotReadable);
    expectRefused(ReportArchiveReader::parse(QByteArrayLiteral("   \n")), ReportArchiveRefusal::NotReadable);
    expectRefused(ReportArchiveReader::parse(QByteArray("\xff\xfe\x00\x01{", 5)), ReportArchiveRefusal::NotReadable);
}

TEST(ReportArchiveReader, AStoredReportOfAnotherFormatIsRefusedByName)
{
    QJsonObject newer = ReportModelJson::toJson(smallModel());
    newer.insert(QStringLiteral("reportSchema"), kReportSchema + 1);
    const ReportArchiveRead read = ReportArchiveReader::parse(QJsonDocument(newer).toJson());
    expectRefused(read, ReportArchiveRefusal::OtherFormat);
    EXPECT_TRUE(read.why.contains(QString::number(kReportSchema + 1))) << read.why.toStdString();
    EXPECT_TRUE(read.why.contains(QString::number(kReportSchema))) << read.why.toStdString();

    QJsonObject older = newer;
    older.insert(QStringLiteral("reportSchema"), 0);
    expectRefused(ReportArchiveReader::parse(QJsonDocument(older).toJson()), ReportArchiveRefusal::OtherFormat);

    QJsonObject unversioned = newer;
    unversioned.remove(QStringLiteral("reportSchema"));
    expectRefused(ReportArchiveReader::parse(QJsonDocument(unversioned).toJson()), ReportArchiveRefusal::OtherFormat);

    // An object that is no report at all states no format either.
    expectRefused(ReportArchiveReader::parse(QByteArrayLiteral("{\"overallResult\":\"success\"}")),
                  ReportArchiveRefusal::OtherFormat);
}

TEST(ReportArchiveReader, TheFilesAModelIsBuiltThroughGiveTheSameAnswers)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QDir history(root.path());
    const ReportModel model = smallModel();
    writeStored(history.filePath(QStringLiteral("stored")), documentOf(model));
    writeStored(history.filePath(QStringLiteral("cut")), documentOf(model).left(40));
    ASSERT_TRUE(QDir().mkpath(history.filePath(QStringLiteral("empty"))));

    const ProductionReportSources production(nullptr, QString());
    const ReportSources sources = production.sources();
    ASSERT_NE(sources.files, nullptr);

    const ReportArchiveRead stored =
        ReportArchiveReader::read(history.filePath(QStringLiteral("stored")), *sources.files);
    ASSERT_TRUE(stored.isRead()) << stored.why.toStdString();
    EXPECT_EQ(*stored.model, model);
    expectRefused(ReportArchiveReader::read(history.filePath(QStringLiteral("cut")), *sources.files),
                  ReportArchiveRefusal::NotReadable);
    expectRefused(ReportArchiveReader::read(history.filePath(QStringLiteral("empty")), *sources.files),
                  ReportArchiveRefusal::NoStoredReport);
    expectRefused(ReportArchiveReader::read(history.filePath(QStringLiteral("not-there")), *sources.files),
                  ReportArchiveRefusal::NoStoredReport);
}

TEST(ReportArchiveReader, ListingTheRunsOfAFolderReadsNoImageOfAnEarlierRun)
{
    auto scenario = buildReportScenario(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(archiveScenarioRun(*scenario));

    // With no session an image cannot be read at all, and the runs of the folder are listed all the same: their
    // figures are in the reports their archives keep.
    const ProductionReportSources blind(nullptr, scenario->metricsDatabasePath);
    const ReportModelInputs inputs{.manifest = scenario->manifest,
                                   .result = scenario->result,
                                   .runDirectory = scenario->runDirectory,
                                   .context = scenario->context,
                                   .counters = std::nullopt};
    const ReportModel listed = buildReportModel(inputs, blind.sources());
    ASSERT_EQ(listed.history.size(), 3u);
    EXPECT_TRUE(listed.history[0].backgroundNoise.has_value());
    EXPECT_TRUE(listed.history[1].backgroundNoise.has_value());
    ASSERT_TRUE(listed.comparison.has_value());
    EXPECT_TRUE(listed.comparison->previous.snr.has_value());

    // With one, the images read are this run's own: none lies in the folder's history.
    const ProductionReportSources production(scenario->session(), scenario->metricsDatabasePath);
    ReportSources sources = production.sources();
    CountingImages counting(sources.images);
    sources.images = &counting;
    const ReportModel model = buildReportModel(inputs, sources);

    ASSERT_EQ(model.history.size(), 3u);
    const QString history = QDir::cleanPath(QFileInfo(scenario->runDirectory).dir().absolutePath());
    EXPECT_GT(counting.total(), 0);
    EXPECT_LE(counting.reads.size(), scenario->facts.mastersMade + scenario->facts.calibrationMasters);
    for (auto read = counting.reads.cbegin(); read != counting.reads.cend(); ++read)
    {
        EXPECT_FALSE(QDir::cleanPath(read.key()).startsWith(history, Qt::CaseInsensitive)) << read.key().toStdString();
    }
}
