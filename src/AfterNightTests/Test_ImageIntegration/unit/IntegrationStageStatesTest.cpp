// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where each stage of an integration job stands, as the stage list says it: the rows, one for each page; each
/// workflow stage's standing from the manifest a job would run and the one its last successful run left - off, never
/// run, current, out of date, with every later stage that reads a changed one; each row's mark and line from the
/// job's facts, what its output folder says and a run on its way; and the stacking row, which stands for the
/// normalization and the drizzle with the stack.

#include "processes/ImageIntegration/ui_models/IntegrationStageStates.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{

using an::ui::StageRow;
using an::ui::StageStatus;
using ImageIntegration::integrationStageRows;
using ImageIntegration::StageModelFacts;
using ImageIntegration::StagePage;
using ImageIntegration::StagePageIssues;
using ImageIntegration::StageRunFacts;
using ImageIntegration::StageStanding;
using ImageIntegration::StageStandings;
using ImageIntegration::stageStandings;

[[nodiscard]] const StageRow& row(const std::vector<StageRow>& rows, const char* id)
{
    for (const StageRow& candidate : rows)
    {
        if (candidate.id == QLatin1String(id))
        {
            return candidate;
        }
    }
    ADD_FAILURE() << "no row " << id;
    static const StageRow none;
    return none;
}

/// A job of lights alone: three frames in one group, registered and stacked.
[[nodiscard]] WorkflowManifest lightsJob()
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("C:/jobs/m42");
    const std::vector<FrameId> lights = {FrameId(QStringLiteral("C:/lights/a.fit")),
                                         FrameId(QStringLiteral("C:/lights/b.fit")),
                                         FrameId(QStringLiteral("C:/lights/c.fit"))};
    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("64x48|M 42")};
    registration.lightFiles = lights;
    manifest.registrationGroups.push_back(registration);
    WorkflowManifest::IntegrationGroup stack;
    stack.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("64x48|M 42|L")}};
    stack.outputFilename = QStringLiteral("stacked_L.fit");
    stack.lightFiles = lights;
    stack.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(stack);
    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsAlignment"),
                              QStringLiteral("LightsStacking")};
    return manifest;
}

/// The facts of a job of three lights in one group that is ready to run.
[[nodiscard]] StageModelFacts lightsFacts()
{
    StageModelFacts facts;
    facts.frames = {0, 0, 0, 3};
    facts.groups = {0, 0, 0, 1};
    facts.includedLights = 3;
    facts.readyToRun = true;
    return facts;
}

/// What the output folder says of the lights' job, every stage it runs standing the same.
[[nodiscard]] StageRunFacts lightsRun(const StageStanding standing)
{
    StageRunFacts run = ImageIntegration::stageRunFacts(lightsJob(), std::nullopt, std::nullopt);
    for (const WorkflowStage stage :
         {WorkflowStage::LightsRegistration, WorkflowStage::LightsAlignment, WorkflowStage::LightsStacking})
    {
        run.standings.set(stage, standing);
    }
    return run;
}

} // namespace

// The rows are the pages, in the order a run reaches them, the summary last: a row for each page and no page shown by
// two rows - the normalization and the drizzle, whose settings are the stacking page's, are no rows of their own - and
// a page is selected by the row named as it.
TEST(IntegrationStageStates, TheRowsAreThePagesInTheWorkflowsOrder)
{
    QStringList ids;
    QStringList titles;
    for (const ImageIntegration::StageRowSpec& spec : ImageIntegration::kStageRows)
    {
        ids.append(QString::fromLatin1(spec.id));
        titles.append(ImageIntegration::stageRowTitle(QString::fromLatin1(spec.id)));
    }
    EXPECT_EQ(ids, (QStringList{QStringLiteral("bias"), QStringLiteral("darks"), QStringLiteral("flats"),
                                QStringLiteral("lights"), QStringLiteral("calibration"), QStringLiteral("registration"),
                                QStringLiteral("stacking"), QStringLiteral("summary")}));
    EXPECT_EQ(titles,
              (QStringList{QStringLiteral("Bias"), QStringLiteral("Darks"), QStringLiteral("Flats"),
                           QStringLiteral("Lights"), QStringLiteral("Calibration"), QStringLiteral("Registration"),
                           QStringLiteral("Stacking"), QStringLiteral("Summary")}));
    ASSERT_EQ(static_cast<int>(ImageIntegration::kStageRows.size()), ImageIntegration::kStagePageCount)
        << "a row for each page";
    for (std::size_t at = 0; at < ImageIntegration::kStageRows.size(); ++at)
    {
        EXPECT_EQ(static_cast<std::size_t>(ImageIntegration::kStageRows[at].page), at)
            << ImageIntegration::kStageRows[at].id << " shows a page of its own, in the pages' order";
    }
    EXPECT_EQ(ImageIntegration::stagePageOfRow(QStringLiteral("lights")), std::optional(StagePage::Lights));
    EXPECT_EQ(ImageIntegration::stagePageOfRow(QStringLiteral("stacking")), std::optional(StagePage::Stacking));
    EXPECT_FALSE(ImageIntegration::stagePageOfRow(QStringLiteral("normalization")).has_value())
        << "a stage with no page of its own is no row";
    EXPECT_FALSE(ImageIntegration::stagePageOfRow(QStringLiteral("drizzle")).has_value());
    EXPECT_EQ(ImageIntegration::stagePageOfRow(QStringLiteral("summary")), std::optional(StagePage::Summary));
    EXPECT_FALSE(ImageIntegration::stagePageOfRow(QStringLiteral("no such row")).has_value());
    for (int page = 0; page < ImageIntegration::kStagePageCount; ++page)
    {
        const QString id = ImageIntegration::stageRowOfPage(static_cast<StagePage>(page));
        EXPECT_EQ(ImageIntegration::stagePageOfRow(id), std::optional(static_cast<StagePage>(page)))
            << id.toStdString();
    }
    EXPECT_EQ(ImageIntegration::stageRowOfPage(StagePage::Stacking), QStringLiteral("stacking"));

    const std::vector<StageRow> rows = integrationStageRows({}, {}, std::nullopt);
    ASSERT_EQ(rows.size(), ImageIntegration::kStageRows.size());
    for (std::size_t at = 0; at < rows.size(); ++at)
    {
        EXPECT_EQ(rows[at].id, ids[static_cast<qsizetype>(at)]);
        EXPECT_EQ(rows[at].title, titles[static_cast<qsizetype>(at)]);
    }
}

// A stage the job does not run is off; with no earlier manifest every stage it runs has never run; with the same
// manifest every one is current.
TEST(IntegrationStageStates, AStageIsOffNeverRunOrCurrent)
{
    const WorkflowManifest job = lightsJob();
    const StageStandings first = stageStandings(job, std::nullopt);
    EXPECT_EQ(first.of(WorkflowStage::Bias), StageStanding::Off);
    EXPECT_EQ(first.of(WorkflowStage::LightsCalibration), StageStanding::Off) << "no light is calibrated";
    EXPECT_EQ(first.of(WorkflowStage::LightsRegistration), StageStanding::NeverRun);
    EXPECT_EQ(first.of(WorkflowStage::LightsAlignment), StageStanding::NeverRun);
    EXPECT_EQ(first.of(WorkflowStage::LightsNormalization), StageStanding::Off);
    EXPECT_EQ(first.of(WorkflowStage::LightsDrizzle), StageStanding::Off);
    EXPECT_EQ(first.of(WorkflowStage::LightsStacking), StageStanding::NeverRun);

    const StageStandings same = stageStandings(job, job);
    EXPECT_EQ(same.of(WorkflowStage::Bias), StageStanding::Off);
    EXPECT_EQ(same.of(WorkflowStage::LightsRegistration), StageStanding::Current);
    EXPECT_EQ(same.of(WorkflowStage::LightsAlignment), StageStanding::Current);
    EXPECT_EQ(same.of(WorkflowStage::LightsStacking), StageStanding::Current);
}

// A stage whose settings the comparison of the manifests found changed is out of date, and so is every later stage
// that reads what it makes: a stacking setting concerns the stack alone, a registration setting everything after it.
TEST(IntegrationStageStates, AStageTheComparisonSaysMustRunAgainIsOutOfDate)
{
    const WorkflowManifest previous = lightsJob();

    WorkflowManifest restacked = previous;
    restacked.integrationGroups.front().stacking.stackMethod = QStringLiteral("Median");
    const StageStandings stack = stageStandings(restacked, previous);
    EXPECT_EQ(stack.of(WorkflowStage::LightsRegistration), StageStanding::Current);
    EXPECT_EQ(stack.of(WorkflowStage::LightsAlignment), StageStanding::Current);
    EXPECT_EQ(stack.of(WorkflowStage::LightsStacking), StageStanding::OutOfDate);

    WorkflowManifest registered = previous;
    registered.registrationGroups.front().settings.minStarPairs += 5;
    const StageStandings registration = stageStandings(registered, previous);
    EXPECT_EQ(registration.of(WorkflowStage::LightsRegistration), StageStanding::OutOfDate);
    EXPECT_EQ(registration.of(WorkflowStage::LightsAlignment), StageStanding::OutOfDate);
    EXPECT_EQ(registration.of(WorkflowStage::LightsStacking), StageStanding::OutOfDate);

    // A light more: the group's lights changed, so its registration and all after it.
    WorkflowManifest more = previous;
    more.registrationGroups.front().lightFiles.push_back(FrameId(QStringLiteral("C:/lights/d.fit")));
    more.integrationGroups.front().lightFiles.push_back(FrameId(QStringLiteral("C:/lights/d.fit")));
    const StageStandings lights = stageStandings(more, previous);
    EXPECT_EQ(lights.of(WorkflowStage::LightsRegistration), StageStanding::OutOfDate);
    EXPECT_EQ(lights.of(WorkflowStage::LightsStacking), StageStanding::OutOfDate);

    // A stage the job gained since the last run has never run: drizzle turned on.
    WorkflowManifest drizzled = previous;
    drizzled.integrationGroups.front().drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{.enabled = true};
    drizzled.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsDrizzle"),
                              QStringLiteral("LightsStacking")};
    const StageStandings drizzle = stageStandings(drizzled, previous);
    EXPECT_EQ(drizzle.of(WorkflowStage::LightsDrizzle), StageStanding::NeverRun);
    EXPECT_EQ(drizzle.of(WorkflowStage::LightsAlignment), StageStanding::Off);
    EXPECT_EQ(drizzle.of(WorkflowStage::LightsRegistration), StageStanding::Current);
}

// What the manifest says of the job's shape: its groups, and the words every group agrees on.
TEST(IntegrationStageStates, TheRunFactsSayTheJobsShape)
{
    WorkflowManifest job = lightsJob();
    WorkflowManifest::IntegrationGroup second = job.integrationGroups.front();
    second.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("64x48|M 42|R")}};
    second.normalizationReferenceFrames = {second.lightFiles.front()};
    second.stacking.normalizationMode = NormalizationMode::Local;
    second.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{.enabled = true, .scale = 2.0};
    job.integrationGroups.push_back(second);
    const StageRunFacts facts = ImageIntegration::stageRunFacts(job, std::nullopt, std::nullopt);
    EXPECT_TRUE(facts.known);
    EXPECT_EQ(facts.registrationGroups, 1);
    EXPECT_EQ(facts.integrationGroups, 2);
    EXPECT_EQ(facts.normalizedGroups, 1);
    EXPECT_EQ(facts.normalization, QStringLiteral("local"));
    EXPECT_EQ(facts.drizzledGroups, 1);
    EXPECT_EQ(facts.drizzle, QStringLiteral("2x"));
    EXPECT_EQ(facts.stackMethod, QStringLiteral("average"));
    EXPECT_FALSE(facts.lastRun.has_value());

    job.integrationGroups.front().stacking.stackMethod = QStringLiteral("Median");
    EXPECT_TRUE(ImageIntegration::stageRunFacts(job, std::nullopt, std::nullopt).stackMethod.isEmpty())
        << "a word is said only where every group agrees";
}

// Not started, ready, done and out of date: a source with no frames and a stage that is off are not started; a stage
// the job runs is ready until a run made it, done while that is current, and out of date once the job changed.
TEST(IntegrationStageStates, ARowIsNotStartedReadyDoneOrOutOfDate)
{
    // Nothing yet: no frames, nothing read.
    {
        const std::vector<StageRow> rows = integrationStageRows({}, {}, std::nullopt);
        for (const StageRow& each : rows)
        {
            EXPECT_EQ(each.status, StageStatus::Pending) << each.id.toStdString();
            EXPECT_FALSE(each.progress.has_value());
        }
        EXPECT_EQ(row(rows, "lights").line, QStringLiteral("no frames"));
        EXPECT_EQ(row(rows, "registration").line, QStringLiteral("no lights"));
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("not ready"));
    }
    // Frames, and nothing read yet: the sources are ready, the workflow's rows wait for the read.
    {
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), {}, std::nullopt);
        EXPECT_EQ(row(rows, "lights").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "lights").line, QStringLiteral("3 frames \u00B7 1 group"));
        EXPECT_EQ(row(rows, "bias").status, StageStatus::Pending);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Pending);
        EXPECT_EQ(row(rows, "registration").line, QStringLiteral("3 frames"));
        EXPECT_EQ(row(rows, "summary").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("ready to run"));
    }
    // Read: never run.
    {
        const std::vector<StageRow> rows =
            integrationStageRows(lightsFacts(), lightsRun(StageStanding::NeverRun), std::nullopt);
        EXPECT_EQ(row(rows, "lights").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "calibration").status, StageStatus::Pending);
        EXPECT_EQ(row(rows, "calibration").line, QStringLiteral("no masters matched"));
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "registration").line, QStringLiteral("1 group \u00B7 3 frames"));
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "stacking").line, QStringLiteral("1 group \u00B7 average"));
        EXPECT_TRUE(row(rows, "stacking").toolTip.startsWith(QStringLiteral("Ready")));
    }
    // A run that succeeded, and nothing changed since.
    {
        StageRunFacts run = lightsRun(StageStanding::Current);
        run.lastRun = WorkflowRunRecord{.durationMs = 3060000, .end = WorkflowRunEnd::Completed, .masters = 1};
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), run, std::nullopt);
        EXPECT_EQ(row(rows, "lights").status, StageStatus::Done);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Done);
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::Done);
        EXPECT_EQ(row(rows, "summary").status, StageStatus::Done);
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("1 master \u00B7 51m 0s"));
        EXPECT_EQ(row(rows, "bias").status, StageStatus::Pending) << "a source with no frames stays not started";
    }
    // The stack's settings changed since.
    {
        StageRunFacts run = lightsRun(StageStanding::Current);
        run.standings.set(WorkflowStage::LightsStacking, StageStanding::OutOfDate);
        run.lastRun = WorkflowRunRecord{.durationMs = 3060000, .end = WorkflowRunEnd::Completed, .masters = 1};
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), run, std::nullopt);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Done);
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::OutOfDate);
        EXPECT_TRUE(row(rows, "stacking").toolTip.startsWith(QStringLiteral("Out of date")));
        EXPECT_EQ(row(rows, "summary").status, StageStatus::OutOfDate);
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("out of date"));
    }
    // The registration's two stages are one row: the one that asks most stands for it.
    {
        StageRunFacts run = lightsRun(StageStanding::Current);
        run.standings.set(WorkflowStage::LightsAlignment, StageStanding::OutOfDate);
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), run, std::nullopt);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::OutOfDate);
        EXPECT_EQ(row(rows, "lights").status, StageStatus::Done) << "the lights are as the last run registered them";
    }
}

// Warning and failed: a page's validation is its row's - an error fails it, anything less warns - with the issues
// counted in its line and spelled out in its tooltip; the stage the last run failed at is failed, the one it was
// cancelled at warns, and the summary says how the last run ended.
TEST(IntegrationStageStates, ARowWarnsOrFailsForItsIssuesAndItsLastRun)
{
    StageModelFacts facts = lightsFacts();
    facts.issues[static_cast<std::size_t>(StagePage::Calibration)] =
        StagePageIssues{.count = 2, .errors = false, .text = QStringLiteral("2"), .toolTip = QStringLiteral("two")};
    facts.issues[static_cast<std::size_t>(StagePage::Stacking)] =
        StagePageIssues{.count = 1, .errors = true, .text = QStringLiteral("1"), .toolTip = QStringLiteral("one")};
    {
        const std::vector<StageRow> rows = integrationStageRows(facts, lightsRun(StageStanding::Current), std::nullopt);
        EXPECT_EQ(row(rows, "calibration").status, StageStatus::Warning);
        EXPECT_EQ(row(rows, "calibration").line, QStringLiteral("2 issues \u00B7 no masters matched"));
        EXPECT_EQ(row(rows, "calibration").toolTip, QStringLiteral("two"));
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::Failed);
        EXPECT_EQ(row(rows, "stacking").line, QStringLiteral("1 issue \u00B7 1 group \u00B7 average"));
        EXPECT_TRUE(row(rows, "stacking").toolTip.endsWith(QStringLiteral("\n\none")))
            << "its issues, under what its stages say: " << row(rows, "stacking").toolTip.toStdString();
    }
    // An issue outranks a state it leaves standing: a stage that is done and warns says both.
    {
        StageModelFacts warned = lightsFacts();
        warned.issues[static_cast<std::size_t>(StagePage::Registration)] =
            StagePageIssues{.count = 1, .errors = false, .text = QStringLiteral("1"), .toolTip = QStringLiteral("w")};
        std::vector<StageRow> rows = integrationStageRows(warned, lightsRun(StageStanding::Current), std::nullopt);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Warning);
        rows = integrationStageRows(warned, lightsRun(StageStanding::OutOfDate), std::nullopt);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::OutOfDate) << "what must run again is said first";
        EXPECT_TRUE(row(rows, "registration").toolTip.endsWith(QStringLiteral("\n\nw")));
    }

    StageRunFacts failed = lightsRun(StageStanding::NeverRun);
    failed.lastRun = WorkflowRunRecord{.end = WorkflowRunEnd::Failed, .stage = QStringLiteral("LightsRegistration")};
    {
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), failed, std::nullopt);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Failed);
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "summary").status, StageStatus::Failed);
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("failed at Registration"));
    }
    StageRunFacts cancelled = lightsRun(StageStanding::NeverRun);
    cancelled.lastRun = WorkflowRunRecord{.end = WorkflowRunEnd::Cancelled, .stage = QStringLiteral("LightsStacking")};
    {
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), cancelled, std::nullopt);
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::Warning);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "summary").status, StageStatus::Warning);
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("cancelled at Stacking"));
    }
    StageRunFacts warnings = lightsRun(StageStanding::Current);
    warnings.lastRun = WorkflowRunRecord{.end = WorkflowRunEnd::CompletedWithWarnings, .masters = 4, .warnings = 2};
    EXPECT_EQ(row(integrationStageRows(lightsFacts(), warnings, std::nullopt), "summary").status, StageStatus::Warning);
    EXPECT_EQ(row(integrationStageRows(lightsFacts(), warnings, std::nullopt), "summary").line,
              QStringLiteral("4 masters \u00B7 2 warnings"));
    StageRunFacts partial = lightsRun(StageStanding::Current);
    partial.lastRun = WorkflowRunRecord{.end = WorkflowRunEnd::Partial, .masters = 3};
    EXPECT_EQ(row(integrationStageRows(lightsFacts(), partial, std::nullopt), "summary").line,
              QStringLiteral("partly done \u00B7 3 masters"));
}

// Running: the row of the stage a run is at runs, with how far the stage has come and the group it is at; the stages
// the run has passed are done; the ones after it wait as they stood; and the summary says the stage and how far the
// whole has come.
TEST(IntegrationStageStates, ARunMarksTheStageItIsAtAndTheOnesItPassed)
{
    WorkflowProgress progress;
    progress.currentStage = WorkflowStage::LightsAlignment;
    progress.currentGroup = 1;
    progress.totalGroups = 4;
    progress.stageProgress = 0.25f;
    progress.overallProgress = 0.58f;
    StageModelFacts facts = lightsFacts();
    facts.frames = {12, 0, 0, 3};
    facts.groups = {1, 0, 0, 1};
    StageRunFacts run = lightsRun(StageStanding::NeverRun);
    run.standings.set(WorkflowStage::Bias, StageStanding::OutOfDate);
    const std::vector<StageRow> rows = integrationStageRows(facts, run, progress);
    EXPECT_EQ(row(rows, "registration").status, StageStatus::Running) << "the alignment is the registration's row";
    EXPECT_EQ(row(rows, "registration").progress, std::optional<int>(25));
    EXPECT_EQ(row(rows, "registration").line, QStringLiteral("Alignment \u00B7 1 / 4"))
        << "a row of several stages names the step a run is at";
    EXPECT_EQ(row(rows, "bias").status, StageStatus::Done) << "the run made the master again";
    EXPECT_EQ(row(rows, "lights").status, StageStatus::Done) << "the run has registered them";
    EXPECT_EQ(row(rows, "stacking").status, StageStatus::Ready);
    EXPECT_FALSE(row(rows, "stacking").progress.has_value());
    EXPECT_EQ(row(rows, "summary").status, StageStatus::Running);
    EXPECT_EQ(row(rows, "summary").progress, std::optional<int>(58));
    EXPECT_EQ(row(rows, "summary").line, QStringLiteral("running \u00B7 Registration"));

    progress.currentStage = WorkflowStage::LightsRegistration;
    EXPECT_EQ(row(integrationStageRows(facts, run, progress), "lights").status, StageStatus::Ready)
        << "a run still at their registration has not passed them";

    progress.currentStage = WorkflowStage::LightsStacking;
    progress.totalGroups = 0;
    const std::vector<StageRow> later = integrationStageRows(facts, run, progress);
    EXPECT_EQ(row(later, "registration").status, StageStatus::Done);
    EXPECT_EQ(row(later, "lights").status, StageStatus::Done);
    EXPECT_EQ(row(later, "stacking").status, StageStatus::Running);
    EXPECT_EQ(row(later, "stacking").line, QStringLiteral("running"));
}

// The lights are what a run takes, and their row is done once a run that succeeded took them as they are: calibrated
// them, where the job calibrates, or registered them where it calibrates none. So the row never waits behind the
// one after it, and a run has passed the lights once it is past that stage.
TEST(IntegrationStageStates, TheLightsAreDoneOnceARunTookThemAsTheyAre)
{
    // A job that calibrates: its lights are calibrated as they are, though their registration must run again.
    StageRunFacts run = lightsRun(StageStanding::OutOfDate);
    run.standings.set(WorkflowStage::LightsCalibration, StageStanding::Current);
    run.calibratedLights = 3;
    {
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), run, std::nullopt);
        EXPECT_EQ(row(rows, "lights").status, StageStatus::Done);
        EXPECT_EQ(row(rows, "calibration").status, StageStatus::Done);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::OutOfDate);
    }
    // Their calibration must run again: they wait with it.
    run.standings.set(WorkflowStage::LightsCalibration, StageStanding::OutOfDate);
    {
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), run, std::nullopt);
        EXPECT_EQ(row(rows, "lights").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "calibration").status, StageStatus::OutOfDate);
    }
    // A job that calibrates none: registered as they are, or waiting with their registration.
    {
        StageRunFacts plain = lightsRun(StageStanding::Current);
        EXPECT_EQ(row(integrationStageRows(lightsFacts(), plain, std::nullopt), "lights").status, StageStatus::Done);
        plain.standings.set(WorkflowStage::LightsRegistration, StageStanding::OutOfDate);
        EXPECT_EQ(row(integrationStageRows(lightsFacts(), plain, std::nullopt), "lights").status, StageStatus::Ready);
    }
    // A run of a job that calibrates: at their calibration it is still at them, past it it has taken them.
    {
        StageRunFacts fresh = lightsRun(StageStanding::NeverRun);
        fresh.standings.set(WorkflowStage::LightsCalibration, StageStanding::NeverRun);
        WorkflowProgress progress;
        progress.currentStage = WorkflowStage::LightsCalibration;
        EXPECT_EQ(row(integrationStageRows(lightsFacts(), fresh, progress), "lights").status, StageStatus::Ready);
        progress.currentStage = WorkflowStage::LightsRegistration;
        EXPECT_EQ(row(integrationStageRows(lightsFacts(), fresh, progress), "lights").status, StageStatus::Done);
    }
}

// The calibration is a stage of a run only where its lights are written: a job that calibrates its lights in memory,
// as they are read, runs no calibration stage, so its row is not done, not ready and not out of date - it says how
// many lights are calibrated and that it is in memory - and the lights' row follows their registration.
TEST(IntegrationStageStates, ACalibrationInMemoryIsNoStageOfARun)
{
    WorkflowManifest job = lightsJob();
    for (const FrameId& light : job.integrationGroups.front().lightFiles)
    {
        job.lightsCalibration.push_back(WorkflowManifest::LightsCalibrationParams{
            .lightPath = light, .darkPath = FrameId(QStringLiteral("C:/masters/dark.fit"))});
    }

    // Written to disk: a stage, which runs before the registration.
    job.calibrateLightsInMemory = false;
    job.stageSequence.prepend(QStringLiteral("LightsCalibration"));
    {
        const StageRunFacts never = ImageIntegration::stageRunFacts(job, std::nullopt, std::nullopt);
        EXPECT_FALSE(never.calibrationInMemory);
        EXPECT_EQ(never.standings.of(WorkflowStage::LightsCalibration), StageStanding::NeverRun);
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), never, std::nullopt);
        EXPECT_EQ(row(rows, "calibration").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "calibration").line, QStringLiteral("3 of 3 lights"));

        const StageRunFacts same = ImageIntegration::stageRunFacts(job, job, std::nullopt);
        const std::vector<StageRow> done = integrationStageRows(lightsFacts(), same, std::nullopt);
        EXPECT_EQ(row(done, "calibration").status, StageStatus::Done);
        EXPECT_EQ(row(done, "lights").status, StageStatus::Done);
    }

    // In memory: no stage, whatever the last run left.
    job.calibrateLightsInMemory = true;
    job.stageSequence.removeAll(QStringLiteral("LightsCalibration"));
    {
        const StageRunFacts same = ImageIntegration::stageRunFacts(job, job, std::nullopt);
        EXPECT_TRUE(same.calibrationInMemory);
        EXPECT_EQ(same.calibratedLights, 3);
        EXPECT_EQ(same.standings.of(WorkflowStage::LightsCalibration), StageStanding::Off);
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), same, std::nullopt);
        EXPECT_EQ(row(rows, "calibration").status, StageStatus::Pending) << "no stage to be done";
        EXPECT_EQ(row(rows, "calibration").line, QStringLiteral("3 of 3 \u00B7 in memory"));
        EXPECT_TRUE(row(rows, "calibration").toolTip.contains(QStringLiteral("in memory")))
            << row(rows, "calibration").toolTip.toStdString();
        EXPECT_EQ(row(rows, "lights").status, StageStatus::Done) << "registered as they are";
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Done);

        // Their registration must run again: the lights wait with it, and the calibration's row says what it said.
        StageRunFacts moved = same;
        moved.standings.set(WorkflowStage::LightsRegistration, StageStanding::OutOfDate);
        const std::vector<StageRow> waiting = integrationStageRows(lightsFacts(), moved, std::nullopt);
        EXPECT_EQ(row(waiting, "lights").status, StageStatus::Ready);
        EXPECT_EQ(row(waiting, "calibration").status, StageStatus::Pending);
        EXPECT_EQ(row(waiting, "registration").status, StageStatus::OutOfDate);
    }
}

// The stacking row stands for the three stages its page holds the settings of - the normalization and the drizzle
// before the stack: the one that asks most is its mark; its line says the groups and, where a group is drizzled, the
// drizzle; its tooltip says each of the three; a run at any of them is at its row, which names the step; and a run
// that stopped at one stopped at its row.
TEST(IntegrationStageStates, TheStackingRowStandsForNormalizationDrizzleAndTheStack)
{
    const auto stacking = [](const StageRunFacts& run, const std::optional<WorkflowProgress>& progress = std::nullopt) {
        return row(integrationStageRows(lightsFacts(), run, progress), "stacking");
    };

    // Its mark: the stage that asks most.
    {
        StageRunFacts run = lightsRun(StageStanding::Current);
        run.standings.set(WorkflowStage::LightsNormalization, StageStanding::OutOfDate);
        EXPECT_EQ(stacking(run).status, StageStatus::OutOfDate) << "the normalization must run again";
        run.standings.set(WorkflowStage::LightsNormalization, StageStanding::Current);
        run.standings.set(WorkflowStage::LightsDrizzle, StageStanding::NeverRun);
        EXPECT_EQ(stacking(run).status, StageStatus::Ready) << "a drizzle the job gained since its last run";
        run.standings.set(WorkflowStage::LightsDrizzle, StageStanding::Current);
        EXPECT_EQ(stacking(run).status, StageStatus::Done);
    }

    // Its line and its tooltip.
    {
        WorkflowManifest job = lightsJob();
        const StageRow plain = stacking(ImageIntegration::stageRunFacts(job, std::nullopt, std::nullopt));
        EXPECT_EQ(plain.line, QStringLiteral("1 group \u00B7 average"));
        EXPECT_TRUE(plain.toolTip.startsWith(QStringLiteral("Ready"))) << plain.toolTip.toStdString();
        EXPECT_TRUE(plain.toolTip.endsWith(
            QStringLiteral("\n\nNormalization: off\nDrizzle: off\nStacking: 1 group \u00B7 average")))
            << plain.toolTip.toStdString();

        // Every group drizzled at one scale: the drizzle, in the method's place, which the tooltip keeps.
        WorkflowManifest::IntegrationGroup& first = job.integrationGroups.front();
        first.normalizationReferenceFrames = {first.lightFiles.front()};
        first.stacking.normalizationMode = NormalizationMode::Global;
        first.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{.enabled = true, .scale = 2.0};
        const StageRow drizzled = stacking(ImageIntegration::stageRunFacts(job, std::nullopt, std::nullopt));
        EXPECT_EQ(drizzled.line, QStringLiteral("1 group \u00B7 drizzle 2x"));
        EXPECT_TRUE(drizzled.toolTip.endsWith(QStringLiteral(
            "\n\nNormalization: global \u00B7 1 group\nDrizzle: 2x \u00B7 1 group\nStacking: 1 group \u00B7 average")))
            << drizzled.toolTip.toStdString();

        // One group of two: how many are.
        WorkflowManifest::IntegrationGroup second = lightsJob().integrationGroups.front();
        second.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("64x48|M 42|R")}};
        job.integrationGroups.push_back(second);
        const StageRow some = stacking(ImageIntegration::stageRunFacts(job, std::nullopt, std::nullopt));
        EXPECT_EQ(some.line, QStringLiteral("2 groups \u00B7 1 drizzled"));
        EXPECT_TRUE(some.toolTip.contains(QStringLiteral("\nDrizzle: 2x \u00B7 1 group\n")))
            << some.toolTip.toStdString();

        // Both, at scales that differ: no scale to say.
        job.integrationGroups.back().drizzle =
            WorkflowManifest::IntegrationGroup::DrizzleSettings{.enabled = true, .scale = 3.0};
        const StageRow differ = stacking(ImageIntegration::stageRunFacts(job, std::nullopt, std::nullopt));
        EXPECT_EQ(differ.line, QStringLiteral("2 groups \u00B7 2 drizzled"));
        EXPECT_TRUE(differ.toolTip.contains(QStringLiteral("\nDrizzle: 2 groups\n"))) << differ.toolTip.toStdString();
    }

    // A run at the normalization or the drizzle is at the stacking row, which names the step.
    {
        WorkflowProgress progress;
        progress.currentStage = WorkflowStage::LightsNormalization;
        progress.currentGroup = 2;
        progress.totalGroups = 4;
        progress.stageProgress = 0.5f;
        progress.overallProgress = 0.7f;
        const StageRunFacts run = lightsRun(StageStanding::NeverRun);
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), run, progress);
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::Running);
        EXPECT_EQ(row(rows, "stacking").progress, std::optional<int>(50));
        EXPECT_EQ(row(rows, "stacking").line, QStringLiteral("Normalization \u00B7 2 / 4"));
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Done) << "the run has passed it";
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("running \u00B7 Stacking"));

        progress.currentStage = WorkflowStage::LightsDrizzle;
        progress.totalGroups = 0;
        EXPECT_EQ(stacking(run, progress).status, StageStatus::Running);
        EXPECT_EQ(stacking(run, progress).line, QStringLiteral("Drizzle"));

        progress.currentStage = WorkflowStage::LightsStacking;
        progress.totalGroups = 4;
        EXPECT_EQ(stacking(run, progress).line, QStringLiteral("running \u00B7 2 / 4")) << "the stage it is named for";
    }

    // A run that failed at the drizzle failed at the stacking row.
    {
        StageRunFacts failed = lightsRun(StageStanding::NeverRun);
        failed.lastRun = WorkflowRunRecord{.end = WorkflowRunEnd::Failed, .stage = QStringLiteral("LightsDrizzle")};
        const std::vector<StageRow> rows = integrationStageRows(lightsFacts(), failed, std::nullopt);
        EXPECT_EQ(row(rows, "stacking").status, StageStatus::Failed);
        EXPECT_EQ(row(rows, "registration").status, StageStatus::Ready);
        EXPECT_EQ(row(rows, "summary").line, QStringLiteral("failed at Drizzle"));
    }
}
