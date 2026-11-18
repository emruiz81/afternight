// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/ui_models/IntegrationStageStates.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "processes/ImageIntegration/workflow/ManifestDiff.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"

#include <QCoreApplication>
#include <QSet>

#include <AfterNightPlatform/Logging.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace ImageIntegration
{

namespace
{

using an::ui::StageRow;
using an::ui::StageStatus;

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("ImageIntegration::IntegrationStageStates", text);
}

/// The stages a run goes through, in its order.
constexpr std::array kPipeline = {
    WorkflowStage::Bias,
    WorkflowStage::Dark,
    WorkflowStage::Flat,
    WorkflowStage::LightsCalibration,
    WorkflowStage::LightsRegistration,
    WorkflowStage::LightsAlignment,
    WorkflowStage::LightsNormalization,
    WorkflowStage::LightsDrizzle,
    WorkflowStage::LightsStacking,
};

/// A stage's place in a run's order; past the end for none.
[[nodiscard]] std::size_t pipelineIndex(const WorkflowStage stage)
{
    const auto found = std::find(kPipeline.begin(), kPipeline.end(), stage);
    return static_cast<std::size_t>(found - kPipeline.begin());
}

/// A stage by the name its record states it by; none for a name no stage has.
[[nodiscard]] WorkflowStage stageNamed(const QString& name)
{
    struct Named
    {
        const char* name;
        WorkflowStage stage;
    };
    static constexpr Named kNames[] = {
        {"BiasStacking", WorkflowStage::Bias},
        {"DarkStacking", WorkflowStage::Dark},
        {"FlatStacking", WorkflowStage::Flat},
        {"LightsCalibration", WorkflowStage::LightsCalibration},
        {"LightsRegistration", WorkflowStage::LightsRegistration},
        {"LightsAlignment", WorkflowStage::LightsAlignment},
        {"LightsNormalization", WorkflowStage::LightsNormalization},
        {"LightsDrizzle", WorkflowStage::LightsDrizzle},
        {"LightsStacking", WorkflowStage::LightsStacking},
    };
    for (const Named& entry : kNames)
    {
        if (name == QLatin1String(entry.name))
        {
            return entry.stage;
        }
    }
    return WorkflowStage::None;
}

/// The stages a row stands for: one, or the registration's two.
struct RowStages
{
    WorkflowStage first = WorkflowStage::None;
    WorkflowStage second = WorkflowStage::None;

    [[nodiscard]] bool holds(const WorkflowStage stage) const noexcept
    {
        return stage != WorkflowStage::None && (stage == first || stage == second);
    }

    /// The last of its stages in a run's order.
    [[nodiscard]] WorkflowStage last() const noexcept
    {
        return second != WorkflowStage::None ? second : first;
    }
};

[[nodiscard]] RowStages stagesOfRow(const QString& rowId)
{
    if (rowId == QLatin1String("bias"))
    {
        return {WorkflowStage::Bias};
    }
    if (rowId == QLatin1String("darks"))
    {
        return {WorkflowStage::Dark};
    }
    if (rowId == QLatin1String("flats"))
    {
        return {WorkflowStage::Flat};
    }
    if (rowId == QLatin1String("calibration"))
    {
        return {WorkflowStage::LightsCalibration};
    }
    if (rowId == QLatin1String("registration"))
    {
        return {WorkflowStage::LightsRegistration, WorkflowStage::LightsAlignment};
    }
    if (rowId == QLatin1String("normalization"))
    {
        return {WorkflowStage::LightsNormalization};
    }
    if (rowId == QLatin1String("drizzle"))
    {
        return {WorkflowStage::LightsDrizzle};
    }
    if (rowId == QLatin1String("stacking"))
    {
        return {WorkflowStage::LightsStacking};
    }
    return {};
}

/// How much a standing asks of a person: the one that asks most stands for a row of two stages.
[[nodiscard]] int weight(const StageStanding standing) noexcept
{
    switch (standing)
    {
    case StageStanding::OutOfDate:
        return 3;
    case StageStanding::NeverRun:
        return 2;
    case StageStanding::Current:
        return 1;
    case StageStanding::Off:
        break;
    }
    return 0;
}

[[nodiscard]] StageStanding standingOf(const StageStandings& standings, const RowStages& stages)
{
    const StageStanding first = standings.of(stages.first);
    if (stages.second == WorkflowStage::None)
    {
        return first;
    }
    const StageStanding second = standings.of(stages.second);
    return weight(second) > weight(first) ? second : first;
}

/// Whether a manifest's job runs a stage.
[[nodiscard]] bool runsStage(const WorkflowManifest& manifest, const WorkflowStage stage)
{
    const auto hasGroups = [](const std::optional<WorkflowManifest::CalibrationStage>& masters) {
        return masters.has_value() && !masters->groups.empty();
    };
    switch (stage)
    {
    case WorkflowStage::Bias:
        return hasGroups(manifest.biasStage);
    case WorkflowStage::Dark:
        return hasGroups(manifest.darkStage);
    case WorkflowStage::Flat:
        return hasGroups(manifest.flatStage);
    case WorkflowStage::LightsCalibration:
        return std::any_of(manifest.lightsCalibration.begin(), manifest.lightsCalibration.end(),
                           [](const WorkflowManifest::LightsCalibrationParams& light) { return light.calibrates(); });
    case WorkflowStage::LightsRegistration:
        return !manifest.registrationGroups.empty();
    case WorkflowStage::LightsAlignment:
        return manifest.stageSequence.contains(QStringLiteral("LightsAlignment"));
    case WorkflowStage::LightsNormalization:
        return manifest.stageSequence.contains(QStringLiteral("LightsNormalization"));
    case WorkflowStage::LightsDrizzle:
        return manifest.stageSequence.contains(QStringLiteral("LightsDrizzle"));
    case WorkflowStage::LightsStacking:
        return !manifest.integrationGroups.empty();
    case WorkflowStage::None:
        break;
    }
    return false;
}

using ChangeType = ManifestDiff::CalibrationGroupChange::ChangeType;

/// Whether a group's change asks for a run: one added or modified; a group that went asks for none.
[[nodiscard]] bool asksForARun(const ChangeType change) noexcept
{
    return change == ChangeType::Added || change == ChangeType::Modified;
}

/// "1 frame", "12 frames".
[[nodiscard]] QString counted(const int count, const char* one, const char* many)
{
    return count == 1 ? translated(one) : translated(many).arg(count);
}

[[nodiscard]] QString joined(const QString& lhs, const QString& rhs)
{
    if (lhs.isEmpty())
    {
        return rhs;
    }
    return rhs.isEmpty() ? lhs : lhs + QStringLiteral(" · ") + rhs;
}

/// What a row's state adds in a sentence, with its page's issues spelled out under it.
[[nodiscard]] QString toolTipFor(const QString& said, const StagePageIssues* issues)
{
    if (issues == nullptr || issues->toolTip.isEmpty())
    {
        return said;
    }
    return said.isEmpty() ? issues->toolTip : said + QStringLiteral("\n\n") + issues->toolTip;
}

/// What a row is resolved from.
struct RowInput
{
    StageStanding standing = StageStanding::Off;
    const StagePageIssues* issues = nullptr; ///< The page's, for the row named as the page.
    bool runningHere = false;
    bool passedByTheRun = false;
    bool failedHere = false;
    bool cancelledHere = false;
};

struct Resolved
{
    StageStatus status = StageStatus::Pending;
    QString said;
};

[[nodiscard]] Resolved resolve(const RowInput& input)
{
    const bool issues = input.issues != nullptr && input.issues->count > 0;
    if (input.runningHere)
    {
        return {StageStatus::Running, {}};
    }
    if (issues && input.issues->errors)
    {
        return {StageStatus::Failed, {}};
    }
    if (input.failedHere && input.standing != StageStanding::Current)
    {
        return {StageStatus::Failed, translated("The last run failed at this stage.")};
    }
    if (input.passedByTheRun &&
        (input.standing == StageStanding::NeverRun || input.standing == StageStanding::OutOfDate))
    {
        return {StageStatus::Done, {}};
    }
    if (input.standing == StageStanding::OutOfDate)
    {
        return {StageStatus::OutOfDate, translated("Out of date: what it reads or its settings changed since the last "
                                                   "run, so the next run makes it again.")};
    }
    if (issues)
    {
        return {StageStatus::Warning, {}};
    }
    if (input.cancelledHere && input.standing != StageStanding::Current)
    {
        return {StageStatus::Warning, translated("The last run was cancelled at this stage.")};
    }
    if (input.standing == StageStanding::Current)
    {
        return {StageStatus::Done, translated("Done: what the last run made is current.")};
    }
    if (input.standing == StageStanding::NeverRun)
    {
        return {StageStatus::Ready, translated("Ready: the next run makes it.")};
    }
    return {StageStatus::Pending, {}};
}

/// A line led by its page's issues, when it has some.
[[nodiscard]] QString withIssues(const StagePageIssues* issues, const QString& line)
{
    if (issues == nullptr || issues->count <= 0)
    {
        return line;
    }
    return joined(counted(issues->count, "1 issue", "%1 issues"), line);
}

[[nodiscard]] int percent(const float fraction)
{
    return (std::clamp)(static_cast<int>(std::lround(static_cast<double>(fraction) * 100.0)), 0, 100);
}

} // namespace

std::optional<StagePage> stagePageOfRow(const QString& rowId)
{
    for (const StageRowSpec& row : kStageRows)
    {
        if (rowId == QLatin1String(row.id))
        {
            return row.page;
        }
    }
    return std::nullopt;
}

QString stageRowOfPage(const StagePage page)
{
    switch (page)
    {
    case StagePage::Bias:
        return QStringLiteral("bias");
    case StagePage::Darks:
        return QStringLiteral("darks");
    case StagePage::Flats:
        return QStringLiteral("flats");
    case StagePage::Lights:
        return QStringLiteral("lights");
    case StagePage::Calibration:
        return QStringLiteral("calibration");
    case StagePage::Registration:
        return QStringLiteral("registration");
    case StagePage::Stacking:
        return QStringLiteral("stacking");
    case StagePage::Summary:
        break;
    }
    return QStringLiteral("summary");
}

QString stageRowTitle(const QString& rowId)
{
    struct Title
    {
        const char* id;
        const char* title;
    };
    static constexpr Title kTitles[] = {
        {"bias", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Bias")},
        {"darks", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Darks")},
        {"flats", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Flats")},
        {"lights", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Lights")},
        {"calibration", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Calibration")},
        {"registration", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Registration")},
        {"normalization", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Normalization")},
        {"drizzle", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Drizzle")},
        {"stacking", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Stacking")},
        {"summary", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationStageStates", "Summary")},
    };
    for (const Title& entry : kTitles)
    {
        if (rowId == QLatin1String(entry.id))
        {
            return translated(entry.title);
        }
    }
    return rowId;
}

StageStandings stageStandings(const WorkflowManifest& current, const std::optional<WorkflowManifest>& previous)
{
    StageStandings standings;
    if (!previous.has_value())
    {
        for (const WorkflowStage stage : kPipeline)
        {
            standings.set(stage, runsStage(current, stage) ? StageStanding::NeverRun : StageStanding::Off);
        }
        return standings;
    }

    const ManifestDiff diff = compareManifests(*previous, current);
    std::array<bool, kWorkflowStageSlots> changed{};
    const auto mark = [&changed](const WorkflowStage stage) { changed[static_cast<std::size_t>(stage)] = true; };
    const auto is = [&changed](const WorkflowStage stage) { return changed[static_cast<std::size_t>(stage)]; };

    // The masters: a group added, or one whose frames, settings or masters changed.
    for (const ManifestDiff::CalibrationGroupChange& change : diff.calibrationChanges)
    {
        if (!asksForARun(change.change))
        {
            continue;
        }
        if (change.frameType == QLatin1String("BIAS"))
        {
            mark(WorkflowStage::Bias);
        }
        else if (change.frameType == QLatin1String("DARK"))
        {
            mark(WorkflowStage::Dark);
        }
        else if (change.frameType == QLatin1String("FLAT"))
        {
            mark(WorkflowStage::Flat);
        }
    }

    // The lights' calibration: a light the job still holds whose masters or settings changed, or a master to make.
    QSet<QString> lights;
    for (const WorkflowManifest::LightsCalibrationParams& light : current.lightsCalibration)
    {
        lights.insert(light.lightPath.canonicalPath());
    }
    const bool calibrationMoved =
        std::any_of(diff.lightsCalibrationChanges.begin(), diff.lightsCalibrationChanges.end(),
                    [&lights](const ManifestDiff::LightsCalibrationChange& change) {
                        return change.changed && lights.contains(change.lightPath.canonicalPath());
                    });
    if (calibrationMoved || is(WorkflowStage::Bias) || is(WorkflowStage::Dark) || is(WorkflowStage::Flat))
    {
        mark(WorkflowStage::LightsCalibration);
    }

    // The registration reads the calibrated lights.
    const bool registrationMoved =
        std::any_of(diff.registrationChanges.begin(), diff.registrationChanges.end(),
                    [](const ManifestDiff::RegistrationGroupChange& change) { return asksForARun(change.change); });
    if (registrationMoved || is(WorkflowStage::LightsCalibration))
    {
        mark(WorkflowStage::LightsRegistration);
    }

    // The stages of a stacking group, each for the settings it reads, and each for the stages before it.
    bool groupsMoved = false;
    LightStages settingsMoved;
    for (const ManifestDiff::IntegrationGroupChange& change : diff.integrationChanges)
    {
        if (!asksForARun(change.change))
        {
            continue;
        }
        if (change.change == ChangeType::Added || change.lightFilesChanged || change.calibrationAssignmentsChanged)
        {
            groupsMoved = true;
        }
        for (const LightStage stage :
             {LightStage::Alignment, LightStage::Normalization, LightStage::Drizzle, LightStage::Stacking})
        {
            if (!change.settingsChanged.of(stage).isEmpty())
            {
                settingsMoved = settingsMoved.with(stage);
            }
        }
    }
    if (groupsMoved || settingsMoved.contains(LightStage::Alignment) || is(WorkflowStage::LightsRegistration))
    {
        mark(WorkflowStage::LightsAlignment);
    }
    if (groupsMoved || settingsMoved.contains(LightStage::Normalization) || is(WorkflowStage::LightsAlignment))
    {
        mark(WorkflowStage::LightsNormalization);
    }
    if (groupsMoved || settingsMoved.contains(LightStage::Drizzle) || is(WorkflowStage::LightsNormalization))
    {
        mark(WorkflowStage::LightsDrizzle);
    }
    if (groupsMoved || settingsMoved.contains(LightStage::Stacking) || is(WorkflowStage::LightsDrizzle))
    {
        mark(WorkflowStage::LightsStacking);
    }

    for (const WorkflowStage stage : kPipeline)
    {
        if (!runsStage(current, stage))
        {
            standings.set(stage, StageStanding::Off);
        }
        else if (!runsStage(*previous, stage))
        {
            standings.set(stage, StageStanding::NeverRun);
        }
        else
        {
            standings.set(stage, is(stage) ? StageStanding::OutOfDate : StageStanding::Current);
        }
    }
    return standings;
}

StageModelFacts stageModelFacts(const IntegrationDataModel& model)
{
    StageModelFacts facts;
    facts.frames = {static_cast<int>(model.biasFrames().size()), static_cast<int>(model.darkFrames().size()),
                    static_cast<int>(model.flatFrames().size()), static_cast<int>(model.lightFrames().size())};
    facts.groups = {static_cast<int>(model.biasGroups().size()), static_cast<int>(model.darkGroups().size()),
                    static_cast<int>(model.flatGroups().size()), static_cast<int>(model.lightGroups().size())};
    for (const FrameInfo& light : model.lightFrames().frames())
    {
        if (!light.excluded)
        {
            ++facts.includedLights;
        }
    }
    facts.readyToRun = model.isReadyForIntegration();
    return facts;
}

StageRunFacts stageRunFacts(const WorkflowManifest& current, const std::optional<WorkflowManifest>& previous,
                            const std::optional<WorkflowRunRecord>& lastRun)
{
    StageRunFacts facts;
    facts.known = true;
    facts.standings = stageStandings(current, previous);
    facts.lastRun = lastRun;
    facts.calibratedLights = static_cast<int>(
        std::count_if(current.lightsCalibration.begin(), current.lightsCalibration.end(),
                      [](const WorkflowManifest::LightsCalibrationParams& light) { return light.calibrates(); }));
    facts.registrationGroups = static_cast<int>(current.registrationGroups.size());
    facts.integrationGroups = static_cast<int>(current.integrationGroups.size());

    // A word a row says only when every group it counts agrees on it.
    const auto agreed = [](QString& word, const QString& said, const bool first) {
        if (first)
        {
            word = said;
        }
        else if (word != said)
        {
            word.clear();
        }
    };
    bool firstStack = true;
    for (const WorkflowManifest::IntegrationGroup& group : current.integrationGroups)
    {
        if (!group.normalizationReferenceFrames.empty())
        {
            const QString mode =
                group.stacking.normalizationMode == NormalizationMode::Local
                    ? translated("local")
                    : (group.stacking.normalizationMode == NormalizationMode::Global ? translated("global")
                                                                                     : QString());
            agreed(facts.normalization, mode, facts.normalizedGroups == 0);
            ++facts.normalizedGroups;
        }
        if (group.drizzle.has_value() && group.drizzle->enabled)
        {
            agreed(facts.drizzle, QString::number(group.drizzle->scale, 'g', 3) + QLatin1Char('x'),
                   facts.drizzledGroups == 0);
            ++facts.drizzledGroups;
        }
        agreed(facts.stackMethod, group.stacking.stackMethod.trimmed().toLower(), firstStack);
        firstStack = false;
    }
    return facts;
}

StageRunFacts readStageRunFacts(const QJsonObject& modelSnapshot, const FullWorkflowOptions& options)
{
    try
    {
        IntegrationDataModel model;
        model.fromJson(modelSnapshot);
        WorkflowManifest manifest = buildFullWorkflowManifest(model, options);
        normalizeManifestForExecution(manifest);
        std::optional<WorkflowManifest> previous;
        std::optional<WorkflowRunRecord> lastRun;
        if (!manifest.outputDirectory.trimmed().isEmpty())
        {
            const WorkflowRunStateStore store(manifest.outputDirectory);
            previous = store.loadLastSuccessfulManifest();
            lastRun = store.loadRunRecord();
        }
        return stageRunFacts(manifest, previous, lastRun);
    }
    catch (const std::exception& error)
    {
        LOG_WARNING("IntegrationStageStates: the job's stages could not be read: {}", error.what());
    }
    catch (...)
    {
        LOG_WARNING("IntegrationStageStates: the job's stages could not be read");
    }
    return {};
}

std::vector<StageRow> integrationStageRows(const StageModelFacts& model, const StageRunFacts& run,
                                           const std::optional<WorkflowProgress>& progress)
{
    const int lights = model.frames[static_cast<std::size_t>(StagePage::Lights)];
    const bool running = progress.has_value() && progress->currentStage != WorkflowStage::None;
    const std::size_t runAt = running ? pipelineIndex(progress->currentStage) : kPipeline.size();
    const bool lastFailed = run.lastRun.has_value() && run.lastRun->end == WorkflowRunEnd::Failed;
    const bool lastCancelled = run.lastRun.has_value() && run.lastRun->end == WorkflowRunEnd::Cancelled;
    const WorkflowStage lastStoppedAt =
        (lastFailed || lastCancelled) ? stageNamed(run.lastRun->stage) : WorkflowStage::None;

    std::vector<StageRow> rows;
    rows.reserve(kStageRows.size());
    for (const StageRowSpec& spec : kStageRows)
    {
        const QString id = QString::fromLatin1(spec.id);
        const std::size_t page = static_cast<std::size_t>(spec.page);
        const StagePageIssues* const issues = stageRowOfPage(spec.page) == id ? &model.issues[page] : nullptr;
        const RowStages stages = stagesOfRow(id);
        StageRow row;
        row.id = id;
        row.title = stageRowTitle(id);

        if (id == QLatin1String("summary"))
        {
            // The job: the step a run is at, what stops it, how its last run ended, whether it is ready.
            const bool anyOutOfDate =
                std::any_of(kPipeline.begin(), kPipeline.end(), [&run](const WorkflowStage stage) {
                    const StageStanding standing = run.standings.of(stage);
                    return standing == StageStanding::OutOfDate || standing == StageStanding::NeverRun;
                });
            if (running)
            {
                row.status = StageStatus::Running;
                row.progress = percent(progress->overallProgress);
                row.line = translated("running");
                // The stage the run is at, by its row's name.
                for (const StageRowSpec& other : kStageRows)
                {
                    if (stagesOfRow(QString::fromLatin1(other.id)).holds(progress->currentStage))
                    {
                        row.line = joined(translated("running"), stageRowTitle(QString::fromLatin1(other.id)));
                        break;
                    }
                }
            }
            else if (issues != nullptr && issues->count > 0 && issues->errors)
            {
                row.status = StageStatus::Failed;
                row.line = counted(issues->count, "1 issue", "%1 issues");
                row.toolTip = issues->toolTip;
            }
            else if (run.lastRun.has_value())
            {
                const WorkflowRunRecord& last = *run.lastRun;
                const QString masters = counted(last.masters, "1 master", "%1 masters");
                const QString stopped = last.stage.isEmpty() ? QString() : integrationStageTitle(last.stage);
                switch (last.end)
                {
                case WorkflowRunEnd::Completed:
                    if (run.known && anyOutOfDate)
                    {
                        row.status = StageStatus::OutOfDate;
                        row.line = translated("out of date");
                        row.toolTip = translated("The job changed since its last run: the next run makes again the "
                                                 "stages that are out of date.");
                    }
                    else
                    {
                        row.status = StageStatus::Done;
                        row.line = joined(masters, integrationJobDurationText(last.durationMs));
                    }
                    break;
                case WorkflowRunEnd::CompletedWithWarnings:
                    row.status = StageStatus::Warning;
                    row.line = joined(masters, counted(last.warnings, "1 warning", "%1 warnings"));
                    break;
                case WorkflowRunEnd::Partial:
                    row.status = StageStatus::Warning;
                    row.line = joined(translated("partly done"), masters);
                    break;
                case WorkflowRunEnd::Failed:
                    row.status = StageStatus::Failed;
                    row.line = stopped.isEmpty() ? translated("failed") : translated("failed at %1").arg(stopped);
                    break;
                case WorkflowRunEnd::Cancelled:
                    row.status = StageStatus::Warning;
                    row.line = stopped.isEmpty() ? translated("cancelled") : translated("cancelled at %1").arg(stopped);
                    break;
                }
                row.toolTip = toolTipFor(row.toolTip, issues);
            }
            else
            {
                row.status = model.readyToRun ? StageStatus::Ready : StageStatus::Pending;
                row.line = withIssues(issues, model.readyToRun ? translated("ready to run") : translated("not ready"));
                if (issues != nullptr && issues->count > 0)
                {
                    row.status = StageStatus::Warning;
                }
                row.toolTip = toolTipFor({}, issues);
            }
            rows.push_back(std::move(row));
            continue;
        }

        RowInput input;
        input.issues = issues;
        input.runningHere = running && stages.holds(progress->currentStage);
        input.passedByTheRun = running && stages.first != WorkflowStage::None && pipelineIndex(stages.last()) < runAt;
        input.failedHere = lastFailed && stages.holds(lastStoppedAt);
        input.cancelledHere = lastCancelled && stages.holds(lastStoppedAt);

        QString line;
        if (page < kFrameSourcePageCount)
        {
            const int frames = model.frames[page];
            const int groups = model.groups[page];
            if (frames <= 0)
            {
                line = translated("no frames");
                input.standing = StageStanding::Off;
            }
            else
            {
                line = counted(frames, "1 frame", "%1 frames");
                if (issues == nullptr || issues->count <= 0)
                {
                    line = joined(line, counted(groups, "1 group", "%1 groups"));
                }
                if (spec.page == StagePage::Lights)
                {
                    // The lights are what a run takes: done once a run that succeeded registered them as they are.
                    input.standing =
                        run.known && run.standings.of(WorkflowStage::LightsRegistration) == StageStanding::Current
                            ? StageStanding::Current
                            : StageStanding::NeverRun;
                    input.passedByTheRun = running && pipelineIndex(WorkflowStage::LightsRegistration) < runAt;
                }
                else
                {
                    input.standing = run.known ? standingOf(run.standings, stages) : StageStanding::NeverRun;
                }
            }
        }
        else
        {
            input.standing = run.known ? standingOf(run.standings, stages) : StageStanding::Off;
            if (id == QLatin1String("calibration"))
            {
                if (lights <= 0)
                {
                    line = translated("no lights");
                }
                else if (!run.known)
                {
                    line = counted(model.includedLights, "1 light", "%1 lights");
                }
                else if (run.calibratedLights <= 0)
                {
                    line = translated("no masters matched");
                }
                else
                {
                    line = translated("%1 of %2 lights").arg(run.calibratedLights).arg(model.includedLights);
                }
            }
            else if (id == QLatin1String("registration"))
            {
                if (lights <= 0)
                {
                    line = translated("no lights");
                }
                else
                {
                    line = joined(run.known ? counted(run.registrationGroups, "1 group", "%1 groups") : QString(),
                                  counted(model.includedLights, "1 frame", "%1 frames"));
                }
            }
            else if (id == QLatin1String("normalization"))
            {
                if (run.known)
                {
                    line = run.normalizedGroups <= 0
                               ? translated("off")
                               : joined(run.normalization, counted(run.normalizedGroups, "1 group", "%1 groups"));
                }
            }
            else if (id == QLatin1String("drizzle"))
            {
                if (run.known)
                {
                    line = run.drizzledGroups <= 0
                               ? translated("off")
                               : joined(run.drizzle, counted(run.drizzledGroups, "1 group", "%1 groups"));
                }
            }
            else if (id == QLatin1String("stacking"))
            {
                if (lights <= 0)
                {
                    line = translated("no lights");
                }
                else if (run.known)
                {
                    line = joined(counted(run.integrationGroups, "1 group", "%1 groups"), run.stackMethod);
                }
            }
        }

        const Resolved resolved = resolve(input);
        row.status = resolved.status;
        row.toolTip = toolTipFor(resolved.said, issues);
        if (resolved.status == StageStatus::Running)
        {
            row.progress = percent(progress->stageProgress);
            row.line = progress->totalGroups > 0
                           ? joined(translated("running"), QStringLiteral("%1 / %2")
                                                               .arg((std::max)(progress->currentGroup, 0))
                                                               .arg(progress->totalGroups))
                           : translated("running");
        }
        else
        {
            row.line = withIssues(issues, line);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace ImageIntegration
