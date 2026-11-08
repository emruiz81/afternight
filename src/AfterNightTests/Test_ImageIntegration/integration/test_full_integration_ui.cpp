// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration window as a whole, under the application context the suite lends it: its nested tabs and action
/// bar, a plan preparation dismissed before it finishes, the warning a regroup shows and hides, the report exported
/// from a run's archive - built on the thread pool in the context's engine session - and the badge that summarizes the
/// workflow's issues.

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPushButton>
#include <QProgressDialog>
#include <QTabWidget>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThreadPool>
#include <QTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/tabs/SummaryTab.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"

namespace
{

// Helper to create dummy frames
FrameInfo createTestFrame(const QString& path, FrameType type)
{
    FrameMetadata meta(type, QSize(1000, 1000), 1.0, std::nullopt, std::nullopt, std::nullopt, "L", "TestObject",
                       QDateTime::currentDateTimeUtc(), "");
    return FrameInfo(FrameId(path), path, meta);
}

class GlobalThreadPoolReservation
{
public:
    GlobalThreadPoolReservation()
        : m_pool(QThreadPool::globalInstance()),
          m_threadCount(m_pool->maxThreadCount() > 0 ? m_pool->maxThreadCount() : 1)
    {
        for (int i = 0; i < m_threadCount; ++i)
        {
            m_pool->reserveThread();
        }
    }

    ~GlobalThreadPoolReservation()
    {
        release();
    }

    void release()
    {
        if (m_released)
        {
            return;
        }

        m_released = true;
        for (int i = 0; i < m_threadCount; ++i)
        {
            m_pool->releaseThread();
        }
    }

private:
    QThreadPool* m_pool = nullptr;
    int m_threadCount = 1;
    bool m_released = false;
};

} // namespace

class FullIntegrationUiTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scope.context().valid());
        process = new ImageIntegrationProcess(nullptr);
    }

    void TearDown() override
    {
        // The window's workers read the context's session; they finish before the window goes.
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        delete process;
    }

    an::tests::integration::ScopedProcessWindowContext m_scope;
    ImageIntegrationProcess* process = nullptr;
};

TEST_F(FullIntegrationUiTest, NestedTabStructureIsCorrect)
{
    QTabWidget* root = process->findChild<QTabWidget*>("rootTabWidget");
    ASSERT_NE(root, nullptr);

    QTabWidget* sources = process->findChild<QTabWidget*>("sourceTabWidget");
    ASSERT_NE(sources, nullptr);

    QTabWidget* workflow = process->findChild<QTabWidget*>("workflowTabWidget");
    ASSERT_NE(workflow, nullptr);

    // Verify Root has 2 tabs
    EXPECT_EQ(root->count(), 2);
    EXPECT_EQ(root->tabText(0), "Frame Sources");
    EXPECT_EQ(root->tabText(1), "Workflow");

    // Verify Sources has 4 tabs
    EXPECT_EQ(sources->count(), 4);
    EXPECT_EQ(sources->tabText(0), "Bias");
    EXPECT_EQ(sources->tabText(1), "Darks");
    EXPECT_EQ(sources->tabText(2), "Flats");
    EXPECT_EQ(sources->tabText(3), "Lights");

    // Verify Workflow has 4 tabs
    EXPECT_EQ(workflow->count(), 4);
    EXPECT_EQ(workflow->tabText(0), "Calibration");
    EXPECT_EQ(workflow->tabText(1), "Registration");
    EXPECT_EQ(workflow->tabText(2), "Stacking");
    EXPECT_EQ(workflow->tabText(3), "Summary");
}

TEST_F(FullIntegrationUiTest, BottomActionBarExists)
{
    QWidget* bottomBar = process->findChild<QWidget*>("bottomActionBar");
    ASSERT_NE(bottomBar, nullptr);

    QLineEdit* outputEdit = bottomBar->findChild<QLineEdit*>("outputDirectoryEdit");
    QPushButton* browseBtn = bottomBar->findChild<QPushButton*>("outputDirectoryBrowseButton");

    EXPECT_NE(outputEdit, nullptr);
    EXPECT_NE(browseBtn, nullptr);
}

TEST_F(FullIntegrationUiTest, BottomActionBarHasResetAndCloseButtons)
{
    QWidget* bottomBar = process->findChild<QWidget*>("bottomActionBar");
    ASSERT_NE(bottomBar, nullptr);

    QDialogButtonBox* box = bottomBar->findChild<QDialogButtonBox*>("dialogButtonBox");
    ASSERT_NE(box, nullptr);
    EXPECT_NE(box->button(QDialogButtonBox::Reset), nullptr);
    EXPECT_NE(box->button(QDialogButtonBox::Close), nullptr);
}

TEST_F(FullIntegrationUiTest, DismissingPlanPreparationDoesNotOpenStaleExecutionPlan)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString light_path = QDir(temp_dir.path()).filePath(QStringLiteral("light.fit"));
    QFile light_file(light_path);
    ASSERT_TRUE(light_file.open(QIODevice::WriteOnly));
    light_file.write("x");
    light_file.close();

    IntegrationDataModel* model = process->model();
    ASSERT_NE(model, nullptr);
    model->setOutputDirectory(temp_dir.path());
    model->addFrames(FrameType::Light, {createTestFrame(light_path, FrameType::Light)});
    QApplication::processEvents();
    ASSERT_TRUE(model->isReadyForIntegration());

    QAction* execute_action = nullptr;
    for (auto* action : process->findChildren<QAction*>())
    {
        if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Execute Integration"))
        {
            execute_action = action;
            break;
        }
    }
    ASSERT_NE(execute_action, nullptr);
    ASSERT_TRUE(execute_action->isEnabled());

    GlobalThreadPoolReservation reservation;

    execute_action->trigger();
    auto* progress = process->findChild<QDialog*>(QStringLiteral("executionPlanPreparationDialog"));
    ASSERT_NE(progress, nullptr);
    progress->close();
    EXPECT_TRUE(progress->property("anPlanPreparationDismissed").toBool());

    reservation.release();
    QTRY_VERIFY_WITH_TIMEOUT(process->findChild<QDialog*>(QStringLiteral("executionPlanPreparationDialog")) == nullptr,
                             5000);
    EXPECT_EQ(process->findChild<QDialog*>(QStringLiteral("executionPlanDialog")), nullptr);
}

TEST_F(FullIntegrationUiTest, ShowsWarningWhenRegroupResetsSettings)
{
    QWidget* lightTab = process->findChild<QWidget*>("lightTab");
    ASSERT_NE(lightTab, nullptr);

    QLabel* warning = lightTab->findChild<QLabel*>("regroupWarningLabel");
    ASSERT_NE(warning, nullptr);
    EXPECT_TRUE(warning->isHidden());

    IntegrationDataModel* model = process->model();
    ASSERT_NE(model, nullptr);

    // Use signal spy to verify the signal is emitted
    QSignalSpy spy(model, &IntegrationDataModel::regroupResetSettings);

    model->addFrames(FrameType::Light, {createTestFrame(QStringLiteral("C:/images/light_001.fit"), FrameType::Light)});
    QApplication::processEvents();

    // Verify we have integration groups
    ASSERT_FALSE(model->integrationGroups().empty())
        << "Should have at least one integration group after adding frames";

    const GroupKey group_key = model->integrationGroups().front();
    model->setStackingSettings(FrameType::Light, group_key, StackingSettings{});
    QApplication::processEvents();

    // Verify stacking settings were actually set
    ASSERT_FALSE(model->stackingSettings(FrameType::Light).empty()) << "Stacking settings should not be empty";

    // Trigger a regroup by toggling a grouping option.
    // Note: We toggle an option that's guaranteed to change state regardless of any
    // previously loaded session data by getting the current value and flipping it.
    GroupingOptions options = model->groupingOptions(FrameType::Light);
    options.byExposure = !options.byExposure; // Toggle to ensure change regardless of current state
    model->setGroupingOptions(FrameType::Light, options);
    QApplication::processEvents();

    // The signal should have been emitted
    EXPECT_GT(spy.count(), 0) << "regroupResetSettings signal should have been emitted";

    EXPECT_FALSE(warning->isHidden()) << "Warning label should be visible";
    EXPECT_FALSE(warning->text().trimmed().isEmpty()) << "Warning text should not be empty";
}

TEST_F(FullIntegrationUiTest, WarningAutoHidesAfterTimeout)
{
    QWidget* lightTab = process->findChild<QWidget*>("lightTab");
    ASSERT_NE(lightTab, nullptr);

    QLabel* warning = lightTab->findChild<QLabel*>("regroupWarningLabel");
    ASSERT_NE(warning, nullptr);

    IntegrationDataModel* model = process->model();
    ASSERT_NE(model, nullptr);

    // Use signal spy to verify the signal is emitted
    QSignalSpy spy(model, &IntegrationDataModel::regroupResetSettings);

    model->addFrames(FrameType::Light, {createTestFrame(QStringLiteral("C:/images/light_001.fit"), FrameType::Light)});
    QApplication::processEvents();

    ASSERT_FALSE(model->integrationGroups().empty()) << "Should have at least one integration group";

    const GroupKey group_key = model->integrationGroups().front();
    model->setStackingSettings(FrameType::Light, group_key, StackingSettings{});
    QApplication::processEvents();

    ASSERT_FALSE(model->stackingSettings(FrameType::Light).empty()) << "Stacking settings should not be empty";

    // Trigger a regroup to show the warning.
    // Toggle an option to ensure change regardless of any previously loaded session data.
    GroupingOptions options = model->groupingOptions(FrameType::Light);
    options.byExposure = !options.byExposure;
    model->setGroupingOptions(FrameType::Light, options);
    QApplication::processEvents();

    ASSERT_GT(spy.count(), 0) << "regroupResetSettings signal should have been emitted";
    ASSERT_FALSE(warning->isHidden()) << "Warning should be visible after regroup";

    // Wait for auto-hide timeout (5 seconds + small buffer)
    // Process events for up to 6 seconds
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 6000 && !warning->isHidden())
    {
        QApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    EXPECT_TRUE(warning->isHidden()) << "Warning should auto-hide after ~5 seconds";
}

TEST_F(FullIntegrationUiTest, ExportReportGeneratesHumanReadableMarkdown)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString output_dir = temp_dir.path();
    IntegrationDataModel* model = process->model();
    ASSERT_NE(model, nullptr);
    model->setOutputDirectory(output_dir);
    model->addFrames(FrameType::Light,
                     {createTestFrame(QDir(output_dir).filePath(QStringLiteral("light1.fit")), FrameType::Light)});

    SummaryTab* summary = process->findChild<SummaryTab*>();
    ASSERT_NE(summary, nullptr);

    const QDir run_dir(QDir(output_dir).filePath(QStringLiteral("history/20250101_120000_000")));
    ASSERT_TRUE(QDir().mkpath(run_dir.path()));

    QJsonObject drizzle;
    drizzle.insert(QStringLiteral("enabled"), true);
    drizzle.insert(QStringLiteral("scale"), 2.0);
    drizzle.insert(QStringLiteral("pixelFraction"), 0.9);
    drizzle.insert(QStringLiteral("kernel"), QStringLiteral("Square"));

    QJsonObject local_norm;
    local_norm.insert(QStringLiteral("gridSize"), 64);
    local_norm.insert(QStringLiteral("referenceStrategy"), QStringLiteral("Automatic"));
    local_norm.insert(QStringLiteral("localScaleEnabled"), true);

    QJsonObject stacking;
    stacking.insert(QStringLiteral("stackMethod"), QStringLiteral("Average"));
    stacking.insert(QStringLiteral("rejectionMethod"), QStringLiteral("Winsorized Sigma Clip"));
    stacking.insert(QStringLiteral("sigmaLow"), 2.8);
    stacking.insert(QStringLiteral("sigmaHigh"), 3.0);
    stacking.insert(QStringLiteral("weighting"), QStringLiteral("FWHM"));
    stacking.insert(QStringLiteral("starCountScaling"), QStringLiteral("Sqrt"));
    stacking.insert(QStringLiteral("exponentialAlpha"), 2.0);
    stacking.insert(QStringLiteral("minWeightFloor"), 0.25);
    stacking.insert(QStringLiteral("perChannelFwhmWeighting"), true);
    stacking.insert(QStringLiteral("normalizationMode"), static_cast<int>(NormalizationMode::Local));
    stacking.insert(QStringLiteral("globalNormalizationMethod"),
                    static_cast<int>(GlobalNormalizationMethod::AdditiveScaling));
    stacking.insert(QStringLiteral("scaleEstimator"), static_cast<int>(ScaleEstimatorMethod::MAD));
    stacking.insert(QStringLiteral("localNormalization"), local_norm);
    stacking.insert(QStringLiteral("drizzle"), drizzle);

    QJsonObject int_group;
    int_group.insert(QStringLiteral("groupKey"), QStringLiteral("TestGroup"));
    int_group.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_TestGroup.fit"));
    int_group.insert(QStringLiteral("object"), QStringLiteral("M42"));
    int_group.insert(QStringLiteral("filter"), QStringLiteral("L"));
    int_group.insert(QStringLiteral("exposure"), 60.0);
    int_group.insert(QStringLiteral("resolution"), QStringLiteral("1000x1000"));
    int_group.insert(QStringLiteral("lightFiles"), QJsonArray{});
    int_group.insert(QStringLiteral("stacking"), stacking);
    int_group.insert(QStringLiteral("registrationGroupKey"), QStringLiteral("RegGroup"));

    QJsonObject reg_settings;
    reg_settings.insert(QStringLiteral("alignmentMode"), QStringLiteral("Star"));
    reg_settings.insert(QStringLiteral("transformation"), QStringLiteral("Affine"));
    reg_settings.insert(QStringLiteral("interpolation"), QStringLiteral("Lanczos3"));
    reg_settings.insert(QStringLiteral("cfaPattern"), QString());
    reg_settings.insert(QStringLiteral("debayerMethod"), QStringLiteral("VNG"));

    QJsonObject reg_group;
    reg_group.insert(QStringLiteral("groupKey"), QStringLiteral("RegGroup"));
    reg_group.insert(QStringLiteral("settings"), reg_settings);

    QJsonObject manifest;
    manifest.insert(QStringLiteral("outputDirectory"), output_dir);
    manifest.insert(QStringLiteral("executionMode"), 0);
    QJsonArray int_groups;
    int_groups.append(int_group);
    manifest.insert(QStringLiteral("integrationGroups"), int_groups);
    QJsonArray reg_groups;
    reg_groups.append(reg_group);
    manifest.insert(QStringLiteral("registrationGroups"), reg_groups);

    QJsonObject run_json;
    run_json.insert(QStringLiteral("schemaVersion"), 3);
    run_json.insert(QStringLiteral("archivedAtUtc"), QStringLiteral("2025-01-01T12:00:00.000Z"));
    run_json.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2025-01-01T12:00:00.000"));
    run_json.insert(QStringLiteral("executionId"), QStringLiteral("test-exec-full-001"));
    run_json.insert(QStringLiteral("archiveReason"), QStringLiteral("overwrite"));
    run_json.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
    run_json.insert(QStringLiteral("executionScope"), QStringLiteral("full"));
    run_json.insert(QStringLiteral("totalDurationMs"), 120000);
    run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
    run_json.insert(QStringLiteral("warningCount"), 0);
    run_json.insert(QStringLiteral("manifest"), manifest);
    run_json.insert(QStringLiteral("stageResults"),
                    QJsonArray{QJsonObject{{QStringLiteral("stage"), QStringLiteral("LightsStacking")},
                                           {QStringLiteral("status"), QStringLiteral("completed")},
                                           {QStringLiteral("durationMs"), 120000},
                                           {QStringLiteral("groupsProcessed"), 1}}});
    run_json.insert(
        QStringLiteral("integrationGroupResults"),
        QJsonArray{QJsonObject{
            {QStringLiteral("groupKey"), QStringLiteral("TestGroup")},
            {QStringLiteral("outputPath"), QDir(output_dir).filePath(QStringLiteral("stacked_TestGroup.fit"))},
            {QStringLiteral("outputFilename"), QStringLiteral("stacked_TestGroup.fit")},
            {QStringLiteral("outputBytes"), 1024 * 1024},
            {QStringLiteral("previewPath"), QStringLiteral("groups/TestGroup/preview.png")},
            {QStringLiteral("lightFrameCount"), 1},
            {QStringLiteral("registrationGroupKey"), QStringLiteral("RegGroup")},
            {QStringLiteral("durationMs"), 120000},
            {QStringLiteral("result"), QStringLiteral("success")}}});
    run_json.insert(QStringLiteral("warnings"), QJsonArray{});
    run_json.insert(QStringLiteral("errors"), QJsonArray{});

    const QString manifest_path = run_dir.filePath(QStringLiteral("workflow_manifest.json"));
    QFile file(manifest_path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(run_json).toJson(QJsonDocument::Indented));
    file.close();

    summary->exportReportRequested(run_dir.path());

    // The report is built on the thread pool in the context's session and written before the window hears of it.
    const QString report_path = run_dir.filePath(QStringLiteral("workflow_report.md"));
    QThreadPool::globalInstance()->waitForDone();
    EXPECT_TRUE(QFileInfo::exists(report_path));
    QCoreApplication::processEvents();
    QFile reportFile(report_path);
    ASSERT_TRUE(reportFile.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(reportFile.readAll());

    EXPECT_TRUE(content.contains(QStringLiteral("Integration Report")));
    EXPECT_TRUE(content.contains(QStringLiteral("Executive Summary")));
    EXPECT_TRUE(content.contains(QStringLiteral("Summary")));
    EXPECT_TRUE(content.contains(QStringLiteral("Pipeline Stages")));
    EXPECT_TRUE(content.contains(QStringLiteral("Input Files")));
    EXPECT_FALSE(content.contains(QStringLiteral("```json")));
    // The archived run stored the per-channel width weighting an earlier version offered; it is read as these
    // settings, which hold none, and the report says nothing of it.
    EXPECT_FALSE(content.contains(QStringLiteral("Per-channel"), Qt::CaseInsensitive));
}

// Re-run from the Summary tab checks the output directory as every other run does: registration records a newer version
// wrote refuse the run, said in a message, and are left as they are. A re-run started without the check.
TEST_F(FullIntegrationUiTest, ARerunIsRefusedOverRegistrationRecordsANewerVersionWrote)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString output_dir = temp_dir.path();
    const QString metrics = QDir(output_dir).filePath(QStringLiteral("registration_metrics.db"));
    const int newer = stage_utils::kRegistrationMetricsSchemaVersion + 1;
    const auto userVersion = [&metrics](const QString& connection) {
        int version = -1;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(metrics);
            if (db.open())
            {
                QSqlQuery query(db);
                if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
                {
                    version = query.value(0).toInt();
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        return version;
    };
    {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("rerun-newer"));
            db.setDatabaseName(metrics);
            ASSERT_TRUE(db.open());
            QSqlQuery query(db);
            ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA user_version = %1").arg(newer)));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("rerun-newer"));
    }
    ASSERT_EQ(userVersion(QStringLiteral("rerun-before")), newer);

    const QDir run_dir(QDir(output_dir).filePath(QStringLiteral("history/20250101_120000_000")));
    ASSERT_TRUE(QDir().mkpath(run_dir.path()));
    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), WorkflowManifest::kFormatVersion);
    manifest.insert(QStringLiteral("outputDirectory"), output_dir);
    manifest.insert(QStringLiteral("executionMode"), 0);
    manifest.insert(QStringLiteral("integrationGroups"), QJsonArray{});
    manifest.insert(QStringLiteral("registrationGroups"), QJsonArray{});
    QJsonObject run_json;
    run_json.insert(QStringLiteral("schemaVersion"), 3);
    run_json.insert(QStringLiteral("manifest"), manifest);
    {
        QFile file(run_dir.filePath(QStringLiteral("workflow_manifest.json")));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(run_json).toJson(QJsonDocument::Indented));
    }

    SummaryTab* summary = process->findChild<SummaryTab*>();
    ASSERT_NE(summary, nullptr);
    summary->rerunRequested(run_dir.path());
    QCoreApplication::processEvents();
    const QList<QMessageBox*> boxes = process->findChildren<QMessageBox*>();
    EXPECT_TRUE(std::any_of(boxes.cbegin(), boxes.cend(), [](const QMessageBox* box) {
        return box->windowTitle() == QStringLiteral("Registration Records");
    })) << "the re-run did not check the output directory";
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_EQ(userVersion(QStringLiteral("rerun-after")), newer);
}

// A re-run from a run archive whose manifest is of another format, or states none, is refused naming the archive's
// manifest, the format found and the one this version reads; nothing in the output directory is looked at or started.
TEST_F(FullIntegrationUiTest, ARerunOfAManifestOfAnotherFormatIsRefused)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString output_dir = temp_dir.path();
    SummaryTab* summary = process->findChild<SummaryTab*>();
    ASSERT_NE(summary, nullptr);
    int run = 0;
    for (const int format : {0, WorkflowManifest::kFormatVersion + 1})
    {
        const QDir run_dir(QDir(output_dir).filePath(QStringLiteral("history/20250101_120000_00%1").arg(run++)));
        ASSERT_TRUE(QDir().mkpath(run_dir.path()));
        QJsonObject manifest;
        if (format > 0)
        {
            manifest.insert(QStringLiteral("version"), format);
        }
        manifest.insert(QStringLiteral("outputDirectory"), output_dir);
        manifest.insert(QStringLiteral("executionMode"), 0);
        manifest.insert(QStringLiteral("integrationGroups"), QJsonArray{});
        manifest.insert(QStringLiteral("registrationGroups"), QJsonArray{});
        QJsonObject run_json;
        run_json.insert(QStringLiteral("schemaVersion"), 3);
        run_json.insert(QStringLiteral("manifest"), manifest);
        const QString manifest_path = run_dir.filePath(QStringLiteral("workflow_manifest.json"));
        {
            QFile file(manifest_path);
            ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(run_json).toJson(QJsonDocument::Indented));
        }

        summary->rerunRequested(run_dir.path());
        QCoreApplication::processEvents();
        const QString found = format > 0 ? QStringLiteral("was written in format %1").arg(format)
                                         : QStringLiteral("states no format version");
        const QString reads =
            QStringLiteral("this version of AfterNight reads format %1").arg(WorkflowManifest::kFormatVersion);
        const QList<QMessageBox*> boxes = process->findChildren<QMessageBox*>();
        EXPECT_TRUE(std::any_of(boxes.cbegin(), boxes.cend(),
                                [&](const QMessageBox* box) {
                                    return box->windowTitle() == QStringLiteral("Re-run") &&
                                           box->text().contains(QDir::toNativeSeparators(manifest_path)) &&
                                           box->text().contains(found) && box->text().contains(reads);
                                }))
            << "no refusal naming the manifest, " << found.toStdString() << " and " << reads.toStdString();
        EXPECT_FALSE(std::any_of(boxes.cbegin(), boxes.cend(), [](const QMessageBox* box) {
            return box->windowTitle() == QStringLiteral("Registration Records");
        })) << "the output directory was looked at";
        for (QMessageBox* box : boxes)
        {
            box->close();
            box->deleteLater();
        }
        QCoreApplication::processEvents();
    }
    EXPECT_FALSE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("registration_metrics.db"))));
}

TEST_F(FullIntegrationUiTest, WorkflowParentBadgeShowsSummary)
{
    // 1. Add frames to trigger calibration issues (Light frame with no calibration)
    IntegrationDataModel* model = process->model();
    ASSERT_NE(model, nullptr);

    // Ensure we have grouping options set
    GroupingOptions opts;
    opts.byFilter = true;
    model->setGroupingOptions(FrameType::Light, opts);

    model->addFrames(FrameType::Light, {createTestFrame(QStringLiteral("C:/images/L_001.fit"), FrameType::Light)});

    // 2. Ensure output directory is empty (triggers Summary issue)
    model->setOutputDirectory(QString());

    // Wait for debounce timer (200ms)
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 500)
        QApplication::processEvents();

    // Verify groups exist (prerequisite for calibration validation)
    ASSERT_FALSE(model->lightGroups().empty()) << "Light groups should not be empty";

    // 3. Find the parent badge for Workflow (index 1)
    QTabWidget* root = process->findChild<QTabWidget*>("rootTabWidget");
    ASSERT_NE(root, nullptr);

    // Search recursively for the badge by object name
    QLabel* badge = root->findChild<QLabel*>("parentBadge_1");
    ASSERT_NE(badge, nullptr) << "Parent badge for Workflow not found";
    EXPECT_TRUE(badge->property("anTabIssueBadge").toBool()) << "Parent badge should be a tokenized tab issue badge";
    EXPECT_FALSE(badge->testAttribute(Qt::WA_TransparentForMouseEvents))
        << "Badge must receive hover events so tooltip can be shown";

    EXPECT_FALSE(badge->isHidden()) << "Parent badge should be visible (isHidden() == false)";

    QString tooltip = badge->toolTip();
    // Summary issue: Output dir -> "Summary Errors (1)" or similar

    EXPECT_TRUE(tooltip.contains("Summary")) << "Tooltip should mention Summary tab: " << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains("(1)")) << "Tooltip should show count (1): " << tooltip.toStdString();
}
