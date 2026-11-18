// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Where each stage of an integration job stands, as a stage list says it: its mark and its one line.
///
/// A stage list has a row for each page of Image Integration: the frame sources a person fills - bias, darks, flats,
/// lights - the pages of the workflow that runs over them - calibration, registration, stacking - and the job's
/// summary last. A row stands for the stages of the workflow its page holds the settings of: one for most, the
/// registration and the alignment for the registration's, and the normalization, the drizzle and the stack for the
/// stacking's. A row's state is read from three places and never guessed:
///
/// - **the job as it stands** (`StageModelFacts`): its frames and their groups, whether it is ready to run, and what
///   its validation says of each page, read from the model on the GUI thread;
/// - **the output folder** (`StageRunFacts`): the manifest the job would run now compared with the one its last
///   successful run left there - a stage that comparison says must run again is out of date - and the record of its
///   last run, whatever its end; both read off the GUI thread (`readStageRunFacts`);
/// - **a run on its way**: the stage the run is at and how far it has come.

#include "AfterNightProcessesExport.h"

#include "components/AnStageList.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"

#include <QJsonObject>
#include <QString>

#include <array>
#include <optional>
#include <vector>

class IntegrationDataModel;

namespace ImageIntegration
{

struct FullWorkflowOptions;

/// A page of Image Integration, in the order a person meets them and its Navigate menu counts them: the four frame
/// sources, then the workflow's pages.
enum class StagePage : int
{
    Bias,
    Darks,
    Flats,
    Lights,
    Calibration,
    Registration,
    Stacking,
    Summary
};

inline constexpr int kStagePageCount = 8;
inline constexpr int kFrameSourcePageCount = 4;

/// What a page's validation says: how many issues, whether one of them stops a run, the short text a badge shows and
/// the issues spelled out.
struct StagePageIssues
{
    int count = 0;
    bool errors = false;
    QString text;
    QString toolTip;

    [[nodiscard]] bool operator==(const StagePageIssues&) const = default;
};

/// A row of the stage list: what the list tells it by, and the page a person is shown when it is selected.
struct StageRowSpec
{
    const char* id;
    StagePage page;
};

/// The rows, in the order the workflow reaches them: one for each page, so that no two rows show the same page.
/// Normalization and drizzle are stages of the workflow with no page of their own - their settings are the stacking
/// page's - and so no rows: the stacking row stands for them.
inline constexpr std::array<StageRowSpec, kStagePageCount> kStageRows = {{
    {"bias", StagePage::Bias},
    {"darks", StagePage::Darks},
    {"flats", StagePage::Flats},
    {"lights", StagePage::Lights},
    {"calibration", StagePage::Calibration},
    {"registration", StagePage::Registration},
    {"stacking", StagePage::Stacking},
    {"summary", StagePage::Summary},
}};

/// The page a row shows; nothing for an id no row has.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<StagePage> stagePageOfRow(const QString& rowId);

/// The row a page is selected by: its own.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString stageRowOfPage(StagePage page);

/// A row's title, as the tabs name its page.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString stageRowTitle(const QString& rowId);

/// Where what a job's last successful run made of a workflow stage stands against what the job asks now.
enum class StageStanding : unsigned char
{
    /// The job as it stands does not run the stage.
    Off,
    /// The job runs the stage, and no successful run in its output folder has made it.
    NeverRun,
    /// The last successful run made it, and nothing it reads has changed since.
    Current,
    /// The last successful run made it, and the comparison of the two manifests says it must run again.
    OutOfDate
};

/// How many stages the workflow names, `WorkflowStage::None` among them: the size of a table indexed by a stage.
inline constexpr std::size_t kWorkflowStageSlots = 10;

/// Each workflow stage's standing, by the stage.
struct StageStandings
{
    std::array<StageStanding, kWorkflowStageSlots> stages{};

    [[nodiscard]] StageStanding of(const WorkflowStage stage) const noexcept
    {
        return stages[static_cast<std::size_t>(stage)];
    }

    void set(const WorkflowStage stage, const StageStanding standing) noexcept
    {
        stages[static_cast<std::size_t>(stage)] = standing;
    }

    [[nodiscard]] bool operator==(const StageStandings&) const = default;
};

/// Each stage's standing from the manifest a job would run now and the one its last successful run left: a stage the
/// job does not run is off; with no earlier manifest every other stage has never run; otherwise a stage whose groups
/// or settings the comparison found changed is out of date, and so is every later stage that reads what it makes -
/// the masters feed the lights' calibration, and each stage of the lights the ones after it.
///
/// @param current the manifest the job would run now, adjusted for execution as a run adjusts it.
/// @param previous the manifest of the last successful run; none when no run succeeded.
[[nodiscard]] AFTERNIGHTPROCESSES_API StageStandings stageStandings(const WorkflowManifest& current,
                                                                    const std::optional<WorkflowManifest>& previous);

/// What the job says of itself, read from its model on the GUI thread.
struct StageModelFacts
{
    /// The frames and the groups of each frame source, in the pages' order: bias, darks, flats, lights.
    std::array<int, kFrameSourcePageCount> frames{};
    std::array<int, kFrameSourcePageCount> groups{};

    /// The lights a run would take: the ones not excluded.
    int includedLights = 0;

    /// Whether the job can be executed as it stands.
    bool readyToRun = false;

    /// What the validation says of each page.
    std::array<StagePageIssues, kStagePageCount> issues{};

    [[nodiscard]] bool operator==(const StageModelFacts&) const = default;
};

/// The job's facts, less its issues, which its window states.
[[nodiscard]] AFTERNIGHTPROCESSES_API StageModelFacts stageModelFacts(const IntegrationDataModel& model);

/// What the job's manifest and its output folder say of its stages, read off the GUI thread.
struct StageRunFacts
{
    /// Whether a read has landed: until one has, the workflow's rows say only what the model says.
    bool known = false;

    StageStandings standings;

    /// The record of the last run, whatever its end; none when no run left one.
    std::optional<WorkflowRunRecord> lastRun;

    /// The groups the job registers and stacks, and the lights it calibrates - in memory, as they are read, where it
    /// writes no calibrated copy of them, which makes the calibration no stage of a run.
    int calibratedLights = 0;
    bool calibrationInMemory = false;
    int registrationGroups = 0;
    int integrationGroups = 0;

    /// The stacking groups that are normalized and how, in a word, when they agree - "local", "global".
    int normalizedGroups = 0;
    QString normalization;

    /// The stacking groups that are drizzled and their scale, when they agree - "2x".
    int drizzledGroups = 0;
    QString drizzle;

    /// The stack method, in lower case, when the groups agree - "average".
    QString stackMethod;

    [[nodiscard]] bool operator==(const StageRunFacts&) const = default;
};

/// The run facts of a manifest against the last successful one and the last run's record.
[[nodiscard]] AFTERNIGHTPROCESSES_API StageRunFacts stageRunFacts(const WorkflowManifest& current,
                                                                  const std::optional<WorkflowManifest>& previous,
                                                                  const std::optional<WorkflowRunRecord>& lastRun);

/// Reads a job's run facts: builds the manifest a snapshot of its model would run, adjusted for execution, and reads
/// the last successful manifest and the last run's record from its output folder. It reads no image and no artifact;
/// for a thread of the pool.
///
/// @param modelSnapshot the model, as `IntegrationDataModel::toJson` states it.
/// @param options the execution options the stacking page states (`workflow/WorkflowPreparation.h`).
[[nodiscard]] AFTERNIGHTPROCESSES_API StageRunFacts readStageRunFacts(const QJsonObject& modelSnapshot,
                                                                      const FullWorkflowOptions& options);

/// The stage list's rows, in the order of `kStageRows`.
///
/// A row is, in this order: running, while a run is at its stage, with how far the stage has come; failed, when its
/// page's validation holds an error or the last run failed at its stage; out of date; a warning, when its page's
/// validation holds one or the last run was cancelled at its stage; done, when its stage is current - and, during a
/// run, once the run has passed it; ready, when it has what it needs and has not run; and not started otherwise - a
/// source with no frames, a stage that is off. The lights are ready once there are some and done once a run that
/// succeeded took them as they are - calibrated them, where the calibration is a stage of the run, or registered
/// them where it is none - so that their row never waits behind the one after it. The calibration is a stage only
/// where the calibrated lights are written: a job that calibrates its lights in memory, as they are read, runs no
/// calibration stage, and its row is not started, saying how many lights are calibrated and that it is in memory.
/// The summary's row says the job: ready or not, the step a run is at, and how the last run ended.
///
/// A row of several stages is marked by the one that asks most - out of date before never run, never run before
/// current - is where a run at any of them is, and then names the step unless it is the one the row is named for:
/// "Drizzle - 2 / 4". The stacking row's line says its groups and their method, or the drizzle where a group is
/// drizzled, and its tooltip says each of its three stages, one a line.
///
/// @param model the job's facts.
/// @param run what the output folder says; `known` false while nothing was read yet.
/// @param progress a run on its way; none outside a run.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<an::ui::StageRow>
integrationStageRows(const StageModelFacts& model, const StageRunFacts& run,
                     const std::optional<WorkflowProgress>& progress);

} // namespace ImageIntegration
