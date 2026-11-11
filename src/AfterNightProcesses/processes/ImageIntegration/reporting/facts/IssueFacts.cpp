// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

/**
 * @file IssueFacts.cpp
 * @brief The issues of a run: a fixed catalog of kinds, each computed from what the model already states, and then
 * every warning and error of the run in the run's own words.
 *
 * Nothing here reads the run's log, and no message of the run is matched to a kind: a fact may appear once as a kind
 * and once as the run wrote it. The thresholds of the three ratios are the ones the report has always held a run to.
 */

namespace ImageIntegration::Reporting
{
namespace
{
/// The share of the lights the masters combine under which a run is warned about, and under which the warning is
/// worded more strongly.
constexpr double kFrameUseWarning = 0.80;
constexpr double kFrameUseStrong = 0.60;

/// The same for the lights whose registration succeeded.
constexpr double kRegistrationWarning = 0.90;
constexpr double kRegistrationStrong = 0.75;

/// The same for the lights that state an exposure.
constexpr double kExposureWarning = 0.95;
constexpr double kExposureStrong = 0.80;

/// The anchors of the places that show an issue.
[[nodiscard]] QString masterAnchor(const ReportMaster& master)
{
    return QStringLiteral("file-%1").arg(master.slug);
}

const char kCalibrationAnchor[] = "cal";
const char kRegistrationAnchor[] = "registration";
const char kOutcomeAnchor[] = "outcome";
const char kFramesAnchor[] = "frames";

[[nodiscard]] QString percent(const double fraction)
{
    return QStringLiteral("%1 %").arg(formatFigure(fraction * 100.0));
}

/// The issues of one build, in the order the catalog lists their kinds.
class Issues
{
public:
    void add(const ReportIssueKind kind, const ReportIssueSeverity severity, const QString& title,
             const QString& detail = {}, const QString& action = {}, const QString& groupKey = {},
             const QString& anchor = {})
    {
        m_issues.push_back(ReportIssue{.kind = kind,
                                       .severity = severity,
                                       .title = title,
                                       .detail = detail,
                                       .action = action,
                                       .groupKey = groupKey,
                                       .anchor = anchor});
    }

    /// The issues, the most severe first and each severity in the catalog's order.
    [[nodiscard]] std::vector<ReportIssue> take()
    {
        std::stable_sort(m_issues.begin(), m_issues.end(), [](const ReportIssue& left, const ReportIssue& right) {
            return static_cast<int>(left.severity) < static_cast<int>(right.severity);
        });
        return std::move(m_issues);
    }

private:
    std::vector<ReportIssue> m_issues;
};

[[nodiscard]] int countOfState(const ReportMaster& master, const ReportFrameState state)
{
    return static_cast<int>(
        std::count_if(master.frames.begin(), master.frames.end(),
                      [state](const ReportFrame& frame) { return frame.state.has_value() && *frame.state == state; }));
}

/// How the run ended, when it stopped: what was kept and what a new run would make.
void addRunEnd(const ReportBuild& build, const ReportModel& model, Issues& issues)
{
    const WorkflowExecutor::Result& result = build.result();
    if (result.success && !result.cancelled)
    {
        return;
    }
    const ReportGroupCounts& groups = model.run.groups;
    const int remaining = groups.total - groups.made - groups.reused;
    const QString action = groups.made > 0
                               ? reportText("The %1 masters the run made are kept. A new run makes the other %2.")
                                     .arg(groups.made)
                                     .arg(remaining)
                               : QString();
    if (result.cancelled)
    {
        issues.add(ReportIssueKind::RunCancelled, ReportIssueSeverity::Critical, reportText("The run was cancelled"),
                   result.errorMessage.trimmed(), action, {}, QString::fromLatin1(kOutcomeAnchor));
        return;
    }
    issues.add(ReportIssueKind::RunFailed, ReportIssueSeverity::Critical, reportText("The run failed"),
               result.errorMessage.trimmed(), action, {}, QString::fromLatin1(kOutcomeAnchor));
}

/// The masters the run did not make, and the ones whose copy the archive did not keep.
void addMasterOutcomes(const ReportModel& model, Issues& issues, const ReportIssueKind kind)
{
    for (const ReportMaster& master : model.masters)
    {
        switch (kind)
        {
        case ReportIssueKind::MasterFailed:
            if (master.state == ReportMasterState::Failed)
            {
                issues.add(kind, ReportIssueSeverity::Critical, reportText("The master %1 failed").arg(master.name),
                           master.failure, {}, master.key, masterAnchor(master));
            }
            break;
        case ReportIssueKind::MasterNotReached:
            if (master.state == ReportMasterState::NotReached || master.state == ReportMasterState::Cancelled)
            {
                issues.add(kind, ReportIssueSeverity::Critical,
                           reportText("The master %1 was not reached").arg(master.name), master.failure, {}, master.key,
                           masterAnchor(master));
            }
            break;
        case ReportIssueKind::OutputNotRetained:
            if (master.retention == ReportRetention::NotRetainedByArchiveFailure)
            {
                issues.add(kind, ReportIssueSeverity::Warning,
                           reportText("The archive holds no copy of the master %1").arg(master.name),
                           master.retentionReason, {}, master.key, masterAnchor(master));
            }
            break;
        case ReportIssueKind::OutputNotRetainedByPolicy:
            if (master.retention == ReportRetention::NotRetainedByPolicy)
            {
                issues.add(kind, ReportIssueSeverity::Note,
                           reportText("The history had no room for a copy of the master %1").arg(master.name),
                           master.retentionReason, {}, master.key, masterAnchor(master));
            }
            break;
        case ReportIssueKind::MasterWithoutAstrometry:
            if (master.state == ReportMasterState::Made && master.astrometry == ReportAstrometryOutcome::None)
            {
                issues.add(kind, ReportIssueSeverity::Warning,
                           reportText("The master %1 has no astrometric solution").arg(master.name),
                           master.astrometryDetail, {}, master.key, masterAnchor(master));
            }
            break;
        case ReportIssueKind::LightsLeftOutByStack:
            if (const int leftOut = countOfState(master, ReportFrameState::LeftOutByStack); leftOut > 0)
            {
                // The reasons the group's result recorded, each with the lights it left out.
                QStringList reasons;
                QHash<QString, int> lightsByReason;
                for (const ReportFrame& frame : master.frames)
                {
                    if (frame.state != ReportFrameState::LeftOutByStack)
                    {
                        continue;
                    }
                    if (!lightsByReason.contains(frame.stateDetail))
                    {
                        reasons.append(frame.stateDetail);
                    }
                    ++lightsByReason[frame.stateDetail];
                }
                QStringList stated;
                for (const QString& reason : reasons)
                {
                    const int lights = lightsByReason.value(reason);
                    stated.append(lights == 1 ? reportText("1 light: %1").arg(reason)
                                              : reportText("%1 lights: %2").arg(lights).arg(reason));
                }
                issues.add(
                    kind, ReportIssueSeverity::Warning,
                    leftOut == 1
                        ? reportText("The stack of %1 left 1 registered light out").arg(master.name)
                        : reportText("The stack of %1 left %2 registered lights out").arg(master.name).arg(leftOut),
                    stated.join(QLatin1Char(' ')), {}, master.key, masterAnchor(master));
            }
            break;
        case ReportIssueKind::StackedTwice:
            if (master.stack.has_value() && master.stack->restack.has_value() && master.stack->restack->applied)
            {
                issues.add(kind, ReportIssueSeverity::Note,
                           reportText("The master %1 was stacked twice").arg(master.name),
                           master.stack->restack->excluded.size() == 1
                               ? reportText("The first stack flagged 1 light, and the master is the second stack, "
                                            "made without it.")
                               : reportText("The first stack flagged %1 lights, and the master is the second stack, "
                                            "made without them.")
                                     .arg(master.stack->restack->excluded.size()),
                           {}, master.key, masterAnchor(master));
            }
            break;
        case ReportIssueKind::MasterReused:
            if (master.state == ReportMasterState::Reused)
            {
                issues.add(kind, ReportIssueSeverity::Note,
                           reportText("The master %1 was still valid and was not made again").arg(master.name),
                           master.madeByRun.isEmpty() ? QString()
                                                      : reportText("The run %1 made it.").arg(master.madeByRun),
                           {}, master.key, masterAnchor(master));
            }
            break;
        default:
            break;
        }
    }
}

/// What the groups of lights take and do not take.
void addCalibrationGaps(const ReportBuild& build, const ReportModel& model, Issues& issues)
{
    const auto masterOf = [&model](const QString& key) -> const ReportMaster* {
        const auto found = std::find_if(model.masters.begin(), model.masters.end(),
                                        [&key](const ReportMaster& master) { return master.key == key; });
        return found == model.masters.end() ? nullptr : &*found;
    };
    const auto anchorOf = [&masterOf](const QString& masterKey) {
        const ReportMaster* master = masterOf(masterKey);
        return master != nullptr ? masterAnchor(*master) : QString();
    };

    int calibrated = 0;
    int withFlat = 0;
    int withDarkOrBias = 0;
    for (const ReportLightGroup& group : model.lightGroups)
    {
        if (group.preCalibrated)
        {
            continue;
        }
        ++calibrated;
        withFlat += group.flatMaster.isEmpty() ? 0 : 1;
        withDarkOrBias += group.darkMaster.isEmpty() && group.biasMaster.isEmpty() ? 0 : 1;
    }
    const QString action = reportText("Load the missing frames and run again: only this group is made again.");
    if (withFlat > 0)
    {
        for (const ReportLightGroup& group : model.lightGroups)
        {
            if (!group.preCalibrated && group.flatMaster.isEmpty())
            {
                issues.add(ReportIssueKind::LightsWithoutFlat, ReportIssueSeverity::Warning,
                           reportText("The lights of %1 take no flat").arg(group.name),
                           reportText("Other lights of the run are calibrated with one."), action, group.masterKey,
                           anchorOf(group.masterKey));
            }
        }
    }
    if (withDarkOrBias > 0)
    {
        for (const ReportLightGroup& group : model.lightGroups)
        {
            if (!group.preCalibrated && group.darkMaster.isEmpty() && group.biasMaster.isEmpty())
            {
                issues.add(ReportIssueKind::LightsWithoutDark, ReportIssueSeverity::Warning,
                           reportText("The lights of %1 take neither a dark nor a bias").arg(group.name),
                           reportText("Other lights of the run are calibrated with one."), action, group.masterKey,
                           anchorOf(group.masterKey));
            }
        }
    }
    if (calibrated > 0 && withFlat == 0)
    {
        issues.add(ReportIssueKind::NoFlats, ReportIssueSeverity::Note, reportText("No light of the run takes a flat"),
                   {}, {}, {}, QString::fromLatin1(kCalibrationAnchor));
    }
    if (calibrated > 0 && withDarkOrBias == 0)
    {
        issues.add(ReportIssueKind::NoDarks, ReportIssueSeverity::Note,
                   reportText("No light of the run takes a dark or a bias"), {}, {}, {},
                   QString::fromLatin1(kCalibrationAnchor));
    }

    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        if (master.kind == ReportCalibrationKind::Flat && master.biasMaster.isEmpty() && master.darkMaster.isEmpty() &&
            (master.state == ReportCalibrationState::Made || master.state == ReportCalibrationState::Reused))
        {
            issues.add(ReportIssueKind::FlatMasterUncalibrated, ReportIssueSeverity::Warning,
                       reportText("The master flat %1 is not calibrated").arg(master.name),
                       reportText("Its frames take neither a bias nor a dark master."), {}, master.key,
                       QString::fromLatin1(kCalibrationAnchor));
        }
    }

    // Cosmetic correction finds its defective pixels in the dark: without one nothing is corrected.
    const WorkflowManifest& manifest = build.manifest();
    QHash<QString, QString> masterByLight;
    for (const auto& group : manifest.integrationGroups)
    {
        for (const FrameId& light : group.lightFiles)
        {
            masterByLight.insert(light.canonicalPath(), group.groupKey.toString());
        }
    }
    QSet<QString> uncorrected;
    for (const auto& calibration : manifest.lightsCalibration)
    {
        if (!calibration.settings.cosmeticCorrection || calibration.darkPath.has_value())
        {
            continue;
        }
        const QString masterKey = masterByLight.value(calibration.lightPath.canonicalPath());
        if (masterKey.isEmpty() || uncorrected.contains(masterKey))
        {
            continue;
        }
        uncorrected.insert(masterKey);
        const ReportMaster* master = masterOf(masterKey);
        issues.add(ReportIssueKind::CosmeticCorrectionWithoutDark, ReportIssueSeverity::Warning,
                   reportText("Cosmetic correction is on for the lights of %1, which take no dark")
                       .arg(master != nullptr ? master->name : friendlyGroupKey(masterKey)),
                   reportText("Defective pixels are found in the dark master, so none is corrected."), {}, masterKey,
                   anchorOf(masterKey));
    }
}

/// The lights whose registration failed, by registration group.
void addRegistrationFailures(const ReportModel& model, Issues& issues)
{
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        if (group.failed.value_or(0) > 0)
        {
            issues.add(ReportIssueKind::RegistrationFailures, ReportIssueSeverity::Warning,
                       reportText("%1 of the %2 lights of %3 failed registration")
                           .arg(*group.failed)
                           .arg(group.lights)
                           .arg(group.name),
                       reportText("They are in no master."), {}, group.key, QString::fromLatin1(kRegistrationAnchor));
        }
    }
}

/// The three ratios the report holds a run to: the lights the masters combine, the lights registered, and the lights
/// that state an exposure.
void addRatios(const ReportModel& model, Issues& issues, const ReportIssueKind kind)
{
    if (kind == ReportIssueKind::LowFrameUse)
    {
        // Over the masters the run made: the lights of one it did not make were never offered to a stack.
        int taken = 0;
        int used = 0;
        for (const ReportMaster& master : model.masters)
        {
            if (master.state != ReportMasterState::Made || master.frames.empty())
            {
                continue;
            }
            // A light the user left out was never offered to the stack either.
            taken += static_cast<int>(master.frames.size()) - countOfState(master, ReportFrameState::ExcludedByUser) -
                     countOfState(master, ReportFrameState::StackExcludedByUser);
            used += countOfState(master, ReportFrameState::Used);
        }
        if (taken > 0 && static_cast<double>(used) / taken < kFrameUseWarning)
        {
            const double share = static_cast<double>(used) / taken;
            issues.add(kind, ReportIssueSeverity::Warning,
                       share < kFrameUseStrong ? reportText("Fewer than three in five lights were combined")
                                               : reportText("Fewer than four in five lights were combined"),
                       reportText("The masters combine %1 of the %2 lights taken for them (%3).")
                           .arg(used)
                           .arg(taken)
                           .arg(percent(share)),
                       {}, {}, QString::fromLatin1(kFramesAnchor));
        }
        return;
    }
    if (kind == ReportIssueKind::RegistrationSuccessLow)
    {
        int lights = 0;
        int registered = 0;
        for (const ReportRegistrationGroup& group : model.registrationGroups)
        {
            if (group.registered.has_value())
            {
                lights += group.lights;
                registered += *group.registered;
            }
        }
        if (lights > 0 && static_cast<double>(registered) / lights < kRegistrationWarning)
        {
            const double share = static_cast<double>(registered) / lights;
            issues.add(kind, ReportIssueSeverity::Warning,
                       share < kRegistrationStrong ? reportText("Fewer than three in four lights were registered")
                                                   : reportText("Fewer than nine in ten lights were registered"),
                       reportText("The registration succeeded on %1 of %2 lights (%3).")
                           .arg(registered)
                           .arg(lights)
                           .arg(percent(share)),
                       {}, {}, QString::fromLatin1(kRegistrationAnchor));
        }
        return;
    }
    int frames = 0;
    int withExposure = 0;
    for (const ReportMaster& master : model.masters)
    {
        frames += master.exposure.frames;
        withExposure += master.exposure.framesWithExposure;
    }
    if (frames > 0 && static_cast<double>(withExposure) / frames < kExposureWarning)
    {
        const double share = static_cast<double>(withExposure) / frames;
        issues.add(kind, ReportIssueSeverity::Warning,
                   share < kExposureStrong ? reportText("Many lights state no exposure")
                                           : reportText("Some lights state no exposure"),
                   reportText("%1 of %2 lights state one (%3), so the integration times are less than was taken.")
                       .arg(withExposure)
                       .arg(frames)
                       .arg(percent(share)),
                   {}, {}, QString::fromLatin1(kFramesAnchor));
    }
}

/// The calibration masters the run found still valid, and the groups of lights marked as calibrated already.
void addCalibrationNotes(const ReportModel& model, Issues& issues)
{
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        if (master.state == ReportCalibrationState::Reused)
        {
            const QString kind = master.kind == ReportCalibrationKind::Bias   ? reportText("bias")
                                 : master.kind == ReportCalibrationKind::Dark ? reportText("dark")
                                                                              : reportText("flat");
            issues.add(ReportIssueKind::CalibrationMasterReused, ReportIssueSeverity::Note,
                       reportText("The master %1 %2 was still valid and was not made again").arg(kind, master.name), {},
                       {}, master.key, QString::fromLatin1(kCalibrationAnchor));
        }
    }
    for (const ReportLightGroup& group : model.lightGroups)
    {
        if (group.preCalibrated)
        {
            issues.add(ReportIssueKind::PreCalibratedLights, ReportIssueSeverity::Note,
                       reportText("The lights of %1 were marked as calibrated already").arg(group.name),
                       reportText("No master is applied to them."), {}, group.masterKey,
                       QString::fromLatin1(kCalibrationAnchor));
        }
    }
}
} // namespace

void fillIssues(ReportBuild& build, ReportModel& model)
{
    Issues issues;
    addRunEnd(build, model, issues);
    addMasterOutcomes(model, issues, ReportIssueKind::MasterFailed);
    addMasterOutcomes(model, issues, ReportIssueKind::MasterNotReached);
    addMasterOutcomes(model, issues, ReportIssueKind::OutputNotRetained);
    addCalibrationGaps(build, model, issues);
    addRegistrationFailures(model, issues);
    addMasterOutcomes(model, issues, ReportIssueKind::MasterWithoutAstrometry);
    addRatios(model, issues, ReportIssueKind::LowFrameUse);
    addRatios(model, issues, ReportIssueKind::RegistrationSuccessLow);
    addMasterOutcomes(model, issues, ReportIssueKind::LightsLeftOutByStack);
    addRatios(model, issues, ReportIssueKind::ExposureMetadataIncomplete);
    addMasterOutcomes(model, issues, ReportIssueKind::StackedTwice);
    addMasterOutcomes(model, issues, ReportIssueKind::MasterReused);
    addCalibrationNotes(model, issues);
    addMasterOutcomes(model, issues, ReportIssueKind::OutputNotRetainedByPolicy);
    model.issues = issues.take();

    // Every message of the run, as the run wrote it, after the kinds: nothing is matched or removed.
    const WorkflowExecutor::Result& result = build.result();
    for (const QString& error : result.errors)
    {
        model.issues.push_back(ReportIssue{.kind = ReportIssueKind::RunMessage,
                                           .severity = ReportIssueSeverity::Critical,
                                           .title = error,
                                           .detail = {},
                                           .action = {},
                                           .groupKey = {},
                                           .anchor = {}});
    }
    for (const QString& warning : result.warnings)
    {
        model.issues.push_back(ReportIssue{.kind = ReportIssueKind::RunMessage,
                                           .severity = ReportIssueSeverity::Warning,
                                           .title = warning,
                                           .detail = {},
                                           .action = {},
                                           .groupKey = {},
                                           .anchor = {}});
    }

    // A run that completed is held to its issues: a critical one makes it partial, a warning says so.
    ReportRun& run = model.run;
    if (run.outcome == ReportOutcome::Completed || run.outcome == ReportOutcome::CompletedWithWarnings)
    {
        const auto has = [&model](const ReportIssueSeverity severity) {
            return std::any_of(model.issues.begin(), model.issues.end(),
                               [severity](const ReportIssue& issue) { return issue.severity == severity; });
        };
        run.outcome = has(ReportIssueSeverity::Critical)  ? ReportOutcome::Partial
                      : has(ReportIssueSeverity::Warning) ? ReportOutcome::CompletedWithWarnings
                                                          : ReportOutcome::Completed;
        run.verdict = reportVerdict(run.outcome, run.groups);
    }
}
} // namespace ImageIntegration::Reporting
