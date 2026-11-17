// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The record every run of the integration workflow leaves in its output folder as it ends, whatever its end: written
/// whole and read back, refused when it is of another format or version or cut short, and stating how each end of a
/// run came - completed, with warnings, partly, failed or cancelled at a stage, and cancelled before its first stage.

#include "fixtures/common/IntegrationFrameFiles.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// A stage that answers what it is told to, under the name a stage of the workflow has.
class ScriptedStage final : public IWorkflowStage
{
public:
    ScriptedStage(QString name, const WorkflowStage stage, StageResult result)
        : m_name(std::move(name)), m_stage(stage), m_result(std::move(result))
    {
    }

    [[nodiscard]] QString name() const override
    {
        return m_name;
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return m_stage;
    }

    [[nodiscard]] bool shouldSkip(const WorkflowManifest& /*manifest*/) const override
    {
        return false;
    }

    StageResult execute(const WorkflowManifest& /*manifest*/, CancellationToken& /*token*/,
                        ProgressReporter& /*reporter*/) override
    {
        return m_result;
    }

private:
    QString m_name;
    WorkflowStage m_stage;
    StageResult m_result;
};

/// A manifest the workflow validates - one light of 8 x 8 in one registration and one integration group - whose output
/// folder is a temporary one of its own, beside the light.
WorkflowManifest validManifest(const QString& folder)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(folder).filePath(QStringLiteral("output"));
    EXPECT_TRUE(QDir().mkpath(manifest.outputDirectory));
    const QString light = QDir(folder).filePath(QStringLiteral("light_8x8.fit"));
    std::string error;
    EXPECT_TRUE(an::tests::fixtures::writeAcquisitionFrame(
        an::tests::fixtures::acquisitionFrame("light_8x8.fit", an::tests::fixtures::AcquisitionFacts{}, 8, 8),
        std::filesystem::path(light.toStdWString()), error))
        << error;

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|8x8|M42")};
    registration.resolution = QStringLiteral("8x8");
    registration.object = QStringLiteral("M42");
    registration.lightFiles = {FrameId(light)};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|M42|L")}};
    integration.outputFilename = QStringLiteral("stacked_M42_L.fit");
    integration.resolution = QStringLiteral("8x8");
    integration.object = QStringLiteral("M42");
    integration.filter = QStringLiteral("L");
    integration.exposure = QStringLiteral("60");
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);
    return manifest;
}

/// The record a run with these stages leaves in a folder of its own.
std::optional<WorkflowRunRecord> recordOfARunWith(std::vector<std::unique_ptr<IWorkflowStage>> stages,
                                                  const bool cancelFirst = false)
{
    QTemporaryDir folder;
    EXPECT_TRUE(folder.isValid());
    const WorkflowManifest manifest = validManifest(folder.path());
    WorkflowExecutor executor;
    for (std::unique_ptr<IWorkflowStage>& stage : stages)
    {
        executor.addStage(std::move(stage));
    }
    if (cancelFirst)
    {
        executor.requestCancel();
    }
    const QDateTime before = QDateTime::currentDateTimeUtc().addMSecs(-1);
    (void)executor.execute(manifest);
    const QDateTime after = QDateTime::currentDateTimeUtc().addMSecs(1);
    std::optional<WorkflowRunRecord> record = WorkflowRunStateStore(manifest.outputDirectory).loadRunRecord();
    if (record.has_value())
    {
        EXPECT_TRUE(record->ended.isValid());
        EXPECT_GE(record->ended, before);
        EXPECT_LE(record->ended, after);
        EXPECT_GE(record->durationMs, 0);
    }
    return record;
}

std::unique_ptr<IWorkflowStage> stage(const QString& name, const WorkflowStage id, StageResult result)
{
    return std::make_unique<ScriptedStage>(name, id, std::move(result));
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

} // namespace

// A record is written whole where the run state is kept, states its format and version, and reads back as it was.
TEST(IntegrationRunRecord, ARecordIsWrittenWholeAndReadBack)
{
    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    WorkflowRunStateStore store(output.path());
    EXPECT_FALSE(store.loadRunRecord().has_value()) << "a folder no run ended in has a record";

    WorkflowRunRecord record;
    record.ended = QDateTime(QDate(2026, 7, 27), QTime(23, 54, 1, 250), QTimeZone::UTC);
    record.durationMs = 3060000;
    record.end = WorkflowRunEnd::Cancelled;
    record.stage = QStringLiteral("LightsRegistration");
    record.masters = 3;
    record.warnings = 2;
    ASSERT_TRUE(store.saveRunRecord(record));

    EXPECT_EQ(
        QDir::cleanPath(store.runRecordPath()),
        QDir::cleanPath(QDir(output.path()).filePath(QStringLiteral(".afternight/image_integration/last_run.json"))));
    const QJsonObject written = readJson(store.runRecordPath());
    EXPECT_EQ(written.value(QStringLiteral("format")).toString(), QString::fromLatin1(kWorkflowRunRecordFormat));
    EXPECT_EQ(written.value(QStringLiteral("version")).toInt(), kWorkflowRunRecordVersion);
    EXPECT_EQ(written.value(QStringLiteral("end")).toString(), QStringLiteral("cancelled"));

    const std::optional<WorkflowRunRecord> read = store.loadRunRecord();
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, record);

    // The next run's record takes its place.
    record.end = WorkflowRunEnd::CompletedWithWarnings;
    record.stage.clear();
    ASSERT_TRUE(store.saveRunRecord(record));
    ASSERT_TRUE(store.loadRunRecord().has_value());
    EXPECT_EQ(store.loadRunRecord()->end, WorkflowRunEnd::CompletedWithWarnings);
}

// A record of another format or version, one cut short and one that states an end no run has are not read: nothing is
// guessed at.
TEST(IntegrationRunRecord, ARecordOfAnotherFormatOrVersionOrCutShortIsNotRead)
{
    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    WorkflowRunStateStore store(output.path());
    WorkflowRunRecord record;
    record.ended = QDateTime(QDate(2026, 11, 2), QTime(22, 3), QTimeZone::UTC);
    record.durationMs = 552000;
    record.masters = 2;
    ASSERT_TRUE(store.saveRunRecord(record));
    const QJsonObject good = readJson(store.runRecordPath());
    ASSERT_FALSE(good.isEmpty());

    const auto refused = [&store](const QJsonObject& object) {
        EXPECT_TRUE(writeBytes(store.runRecordPath(), QJsonDocument(object).toJson()));
        return !store.loadRunRecord().has_value();
    };
    QJsonObject newer = good;
    newer.insert(QStringLiteral("version"), kWorkflowRunRecordVersion + 1);
    EXPECT_TRUE(refused(newer)) << "a newer version was read";
    QJsonObject another = good;
    another.insert(QStringLiteral("format"), QStringLiteral("afternight-project"));
    EXPECT_TRUE(refused(another)) << "another format was read";
    QJsonObject noEnd = good;
    noEnd.remove(QStringLiteral("end"));
    EXPECT_TRUE(refused(noEnd)) << "a record that states no end was read";
    QJsonObject unknownEnd = good;
    unknownEnd.insert(QStringLiteral("end"), QStringLiteral("finished"));
    EXPECT_TRUE(refused(unknownEnd)) << "an end no run has was read";
    QJsonObject noTime = good;
    noTime.remove(QStringLiteral("ended"));
    EXPECT_TRUE(refused(noTime)) << "a record that states no time was read";

    const QByteArray bytes = QJsonDocument(good).toJson();
    ASSERT_TRUE(writeBytes(store.runRecordPath(), bytes.left(bytes.size() / 2)));
    EXPECT_FALSE(store.loadRunRecord().has_value()) << "a record cut short was read";
}

// Every end of a run leaves its record: completed, with warnings, failed and cancelled at the stage it stopped at, and
// cancelled before its first stage.
TEST(IntegrationRunRecord, EveryEndOfARunLeavesItsRecord)
{
    {
        std::vector<std::unique_ptr<IWorkflowStage>> stages;
        stages.push_back(
            stage(QStringLiteral("LightsRegistration"), WorkflowStage::LightsRegistration, StageResult::success()));
        const std::optional<WorkflowRunRecord> record = recordOfARunWith(std::move(stages));
        ASSERT_TRUE(record.has_value()) << "a run that completed left no record";
        EXPECT_EQ(record->end, WorkflowRunEnd::Completed);
        EXPECT_TRUE(record->stage.isEmpty());
        EXPECT_EQ(record->warnings, 0);
    }
    {
        StageResult warned = StageResult::success();
        warned.warnings = {QStringLiteral("Two lights have no flat.")};
        std::vector<std::unique_ptr<IWorkflowStage>> stages;
        stages.push_back(stage(QStringLiteral("LightsCalibration"), WorkflowStage::LightsCalibration, warned));
        const std::optional<WorkflowRunRecord> record = recordOfARunWith(std::move(stages));
        ASSERT_TRUE(record.has_value()) << "a run that completed with a warning left no record";
        EXPECT_EQ(record->end, WorkflowRunEnd::CompletedWithWarnings);
        EXPECT_EQ(record->warnings, 1);
    }
    {
        std::vector<std::unique_ptr<IWorkflowStage>> stages;
        stages.push_back(
            stage(QStringLiteral("LightsCalibration"), WorkflowStage::LightsCalibration, StageResult::success()));
        stages.push_back(stage(QStringLiteral("LightsRegistration"), WorkflowStage::LightsRegistration,
                               StageResult::failure(QStringLiteral("No stars were found."))));
        const std::optional<WorkflowRunRecord> record = recordOfARunWith(std::move(stages));
        ASSERT_TRUE(record.has_value()) << "a run that failed left no record";
        EXPECT_EQ(record->end, WorkflowRunEnd::Failed);
        EXPECT_EQ(record->stage, QStringLiteral("LightsRegistration"));
        EXPECT_EQ(record->masters, 0);
    }
    {
        std::vector<std::unique_ptr<IWorkflowStage>> stages;
        stages.push_back(
            stage(QStringLiteral("LightsCalibration"), WorkflowStage::LightsCalibration, StageResult::success()));
        stages.push_back(
            stage(QStringLiteral("LightsRegistration"), WorkflowStage::LightsRegistration, StageResult::cancelled()));
        const std::optional<WorkflowRunRecord> record = recordOfARunWith(std::move(stages));
        ASSERT_TRUE(record.has_value()) << "a run cancelled at a stage left no record";
        EXPECT_EQ(record->end, WorkflowRunEnd::Cancelled);
        EXPECT_EQ(record->stage, QStringLiteral("LightsRegistration"));
    }
    {
        std::vector<std::unique_ptr<IWorkflowStage>> stages;
        stages.push_back(
            stage(QStringLiteral("LightsCalibration"), WorkflowStage::LightsCalibration, StageResult::success()));
        const std::optional<WorkflowRunRecord> record = recordOfARunWith(std::move(stages), true);
        ASSERT_TRUE(record.has_value()) << "a run cancelled before its first stage left no record";
        EXPECT_EQ(record->end, WorkflowRunEnd::Cancelled);
        EXPECT_TRUE(record->stage.isEmpty());
    }
}

// What a record states of a run's result: the masters it left - made, or found still valid - and how it ended when a
// group of it failed.
TEST(IntegrationRunRecord, TheRecordCountsTheMastersARunLeft)
{
    WorkflowExecutor::Result result;
    result.success = true;
    result.generatedLightMasters = {QStringLiteral("stacked_L.fit"), QStringLiteral("stacked_R.fit")};
    result.reusedIntegrationGroups.push_back(ReusedIntegrationGroup{.groupKey = QStringLiteral("G"), .outputPath = {}});
    result.totalDurationMs = 1234;
    const QDateTime ended(QDate(2026, 11, 1), QTime(3, 40), QTimeZone::UTC);

    WorkflowRunRecord record = workflowRunRecord(result, ended);
    EXPECT_EQ(record.end, WorkflowRunEnd::Completed);
    EXPECT_EQ(record.masters, 3);
    EXPECT_EQ(record.durationMs, 1234);
    EXPECT_EQ(record.ended, ended);

    // A group whose stack failed while the others were made: the run went to its end, partly.
    result.failedIntegrationGroups.push_back(FailedIntegrationGroup{});
    record = workflowRunRecord(result, ended);
    EXPECT_EQ(record.end, WorkflowRunEnd::Partial);

    // A run that failed after it made a master is partly done too; one that made none failed.
    result.failedIntegrationGroups.clear();
    result.success = false;
    record = workflowRunRecord(result, ended);
    EXPECT_EQ(record.end, WorkflowRunEnd::Partial);
    result.generatedLightMasters.clear();
    result.reusedIntegrationGroups.clear();
    record = workflowRunRecord(result, ended);
    EXPECT_EQ(record.end, WorkflowRunEnd::Failed);
    EXPECT_EQ(record.masters, 0);
}
